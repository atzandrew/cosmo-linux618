// SPDX-License-Identifier: GPL-2.0-only
/*
 * Keyboard matrix scanned by an Awinic AW9523B I/O expander.
 *
 * Rows are port 0 inputs, columns are port 1 outputs. The scan follows the
 * vendor driver of the Planet Cosmo Communicator (aw9523_key.c in
 * gemian/cosmo-linux-kernel-4.4):
 *
 * - While idle all columns are driven low and the chip's port-0 interrupt
 *   is on; any key press pulls a row low and the chip asserts INT.
 * - On INT the chip interrupt is switched off and the matrix is scanned:
 *   one column driven low at a time (the others are inputs), one read of
 *   port 0 per column.
 * - While any key is down the matrix is rescanned every 10 ms with the
 *   interrupt off. When all keys are up the columns go low again and the
 *   interrupt is switched back on.
 *
 * Port-0 inputs stay level-sensitive: the SoC interrupt is level-low, so a
 * press that lands while the interrupt is being switched back on is never
 * lost (the idle read below also catches it).
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pm.h>
#include <linux/workqueue.h>

#define AW9523_P0_INPUT		0x00
#define AW9523_P1_INPUT		0x01
#define AW9523_P1_OUTPUT	0x03
#define AW9523_P0_CONFIG	0x04
#define AW9523_P1_CONFIG	0x05
#define AW9523_P0_INT		0x06	/* 1 = interrupt disabled */
#define AW9523_P1_INT		0x07
#define AW9523_CHIP_ID		0x10
#define AW9523_SW_RESET		0x7f

#define AW9523_CHIP_ID_VAL	0x23
#define AW9523_MAX_ROWS		8
#define AW9523_MAX_COLS		8

#define AW9523_FIRST_SCAN_MS	1	/* vendor: MATRIX_SCAN_NANO_SECONDS_FAST */
#define AW9523_RESCAN_MS	10	/* vendor: MATRIX_SCAN_NANO_SECONDS_SLOW */

struct aw9523_keypad {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *reset_gpio;
	struct delayed_work work;
	unsigned int rows, cols, row_shift;
	u8 row_mask, col_mask;
	/* per column: bit set = key down at that row */
	u8 state[AW9523_MAX_COLS];
	/* the irq is disabled and the scan work owns re-enabling it */
	bool irq_off;
	/* ctrl/shift sharing a column: see aw9523_kp_ctrl_shift() */
	int cs_col, cs_shift_row, cs_ctrl_row;
};

static int aw9523_kp_write(struct aw9523_keypad *kp, u8 reg, u8 val)
{
	int ret = i2c_smbus_write_byte_data(kp->client, reg, val);

	if (ret)
		dev_err_ratelimited(&kp->client->dev, "write %#x: %d\n", reg, ret);
	return ret;
}

static int aw9523_kp_read(struct aw9523_keypad *kp, u8 reg)
{
	int ret = i2c_smbus_read_byte_data(kp->client, reg);

	if (ret < 0)
		dev_err_ratelimited(&kp->client->dev, "read %#x: %d\n", reg, ret);
	return ret;
}

/* vendor aw9523_hw_reset(): reset low 5 ms, release, 5 ms */
static void aw9523_kp_hw_reset(struct aw9523_keypad *kp)
{
	if (!kp->reset_gpio)
		return;
	gpiod_set_value_cansleep(kp->reset_gpio, 1);
	msleep(5);
	gpiod_set_value_cansleep(kp->reset_gpio, 0);
	msleep(5);
}

/*
 * vendor aw9523_init_keycfg(): soft reset, port 0 inputs, port 1 outputs
 * driven low (unused port-1 pins included, as the vendor driver keeps
 * P1_7), port-1 interrupts off. The port-0 interrupt is left off here; the
 * scan work turns it on when it goes idle.
 */
static int aw9523_kp_init_chip(struct aw9523_keypad *kp)
{
	int ret;

	ret = aw9523_kp_write(kp, AW9523_SW_RESET, 0x00);
	ret = ret ?: aw9523_kp_write(kp, AW9523_P0_CONFIG, 0xff);
	ret = ret ?: aw9523_kp_write(kp, AW9523_P1_CONFIG, 0x00);
	ret = ret ?: aw9523_kp_write(kp, AW9523_P1_OUTPUT, 0x00);
	ret = ret ?: aw9523_kp_write(kp, AW9523_P0_INT, 0xff);
	ret = ret ?: aw9523_kp_write(kp, AW9523_P1_INT, 0xff);
	return ret;
}

/* Read the matrix: state[c] bit r set = key (r, c) down. */
static int aw9523_kp_read_matrix(struct aw9523_keypad *kp, u8 *state)
{
	unsigned int c;
	u8 drive;
	int v;

	for (c = 0; c < kp->cols; c++) {
		/* column c output low, the other columns inputs */
		drive = kp->col_mask & ~BIT(c);
		if (aw9523_kp_write(kp, AW9523_P1_CONFIG, drive) ||
		    aw9523_kp_write(kp, AW9523_P1_OUTPUT, drive))
			return -EIO;
		v = aw9523_kp_read(kp, AW9523_P0_INPUT);
		if (v < 0)
			return v;
		state[c] = ~v & kp->row_mask;
	}
	return 0;
}

/*
 * Vendor special case: with Ctrl and left Shift held (same column), a
 * key in Shift's row on another column also closes Ctrl's row there
 * (vendor: Ctrl+Shift+Z/X/C/V). Drop that ghost so the combination works.
 * The vendor driver limits this to four columns; here it applies to all.
 */
static bool aw9523_kp_ctrl_shift(struct aw9523_keypad *kp, u8 *state)
{
	u8 both = BIT(kp->cs_shift_row) | BIT(kp->cs_ctrl_row);
	unsigned int c;

	if (kp->cs_col < 0 || (state[kp->cs_col] & both) != both)
		return false;
	for (c = 0; c < kp->cols; c++)
		if (c != kp->cs_col && (state[c] & BIT(kp->cs_shift_row)))
			state[c] &= ~BIT(kp->cs_ctrl_row);
	return true;
}

/*
 * No diodes: two columns sharing a pressed row, one of them with more than
 * one key down, can show a key that is not pressed. Hold the previous
 * state until the ambiguity clears (vendor: "block both keys").
 */
static bool aw9523_kp_ghosting(struct aw9523_keypad *kp, const u8 *state,
			       bool cs_held)
{
	unsigned int i, j;
	u8 shared;

	for (i = 0; i < kp->cols; i++)
		for (j = i + 1; j < kp->cols; j++) {
			shared = state[i] & state[j];
			if (!shared ||
			    (hweight8(state[i]) < 2 && hweight8(state[j]) < 2))
				continue;
			/* resolved by aw9523_kp_ctrl_shift() */
			if (cs_held && (i == kp->cs_col || j == kp->cs_col) &&
			    shared == BIT(kp->cs_shift_row))
				continue;
			return true;
		}
	return false;
}

static void aw9523_kp_report(struct aw9523_keypad *kp, const u8 *state)
{
	const unsigned short *keycodes = kp->input->keycode;
	unsigned int c, r, code;
	u8 changed;
	int pass;

	/* presses first, then releases (vendor: keeps modifiers held) */
	for (pass = 1; pass >= 0; pass--) {
		for (c = 0; c < kp->cols; c++) {
			changed = kp->state[c] ^ state[c];
			for (r = 0; r < kp->rows; r++) {
				if (!(changed & BIT(r)) ||
				    !!(state[c] & BIT(r)) != pass)
					continue;
				code = MATRIX_SCAN_CODE(r, c, kp->row_shift);
				input_event(kp->input, EV_MSC, MSC_SCAN, code);
				input_report_key(kp->input, keycodes[code], pass);
			}
		}
	}
	input_sync(kp->input);
	memcpy(kp->state, state, kp->cols);
}

static void aw9523_kp_release_all(struct aw9523_keypad *kp)
{
	u8 none[AW9523_MAX_COLS] = { };

	aw9523_kp_report(kp, none);
}

static bool aw9523_kp_any_down(struct aw9523_keypad *kp, const u8 *state)
{
	unsigned int c;

	for (c = 0; c < kp->cols; c++)
		if (state[c])
			return true;
	return false;
}

/*
 * Idle: all columns low, port-0 interrupt on. Returns true if a key is
 * already down (then the interrupt stays off and the caller rescans).
 */
static bool aw9523_kp_go_idle(struct aw9523_keypad *kp)
{
	int v;

	if (aw9523_kp_write(kp, AW9523_P1_CONFIG, 0x00) ||
	    aw9523_kp_write(kp, AW9523_P1_OUTPUT, 0x00) ||
	    aw9523_kp_write(kp, AW9523_P0_INT, ~kp->row_mask))
		return true;
	/*
	 * Release INT. The scan's port-1 changes latch it even with the
	 * port-1 interrupt masked, and only a port-1 input read clears that
	 * (measured on the Cosmo 2026-10-25: GPIO12 stayed low after a P0
	 * read, went high after a P1 read). The vendor driver never reads
	 * P1, which would leave a level-low line asserted.
	 */
	aw9523_kp_read(kp, AW9523_P1_INPUT);
	v = aw9523_kp_read(kp, AW9523_P0_INPUT);
	return v < 0 || (~v & kp->row_mask);
}

static void aw9523_kp_work(struct work_struct *work)
{
	struct aw9523_keypad *kp =
		container_of(to_delayed_work(work), struct aw9523_keypad, work);
	u8 state[AW9523_MAX_COLS] = { };
	bool cs_held;

	/* vendor aw9523_int_work(): chip interrupt off while scanning */
	aw9523_kp_write(kp, AW9523_P0_INT, 0xff);

	if (aw9523_kp_read_matrix(kp, state))
		goto rescan;

	cs_held = aw9523_kp_ctrl_shift(kp, state);
	if (aw9523_kp_ghosting(kp, state, cs_held))
		goto rescan;

	if (memcmp(kp->state, state, kp->cols))
		aw9523_kp_report(kp, state);

	if (aw9523_kp_any_down(kp, state) || aw9523_kp_go_idle(kp))
		goto rescan;

	if (kp->irq_off) {
		kp->irq_off = false;
		enable_irq(kp->client->irq);
	}
	return;

rescan:
	schedule_delayed_work(&kp->work, msecs_to_jiffies(AW9523_RESCAN_MS));
}

static irqreturn_t aw9523_kp_irq(int irq, void *data)
{
	struct aw9523_keypad *kp = data;

	/* level-low line: keep it off until the scan work goes idle */
	disable_irq_nosync(irq);
	kp->irq_off = true;
	mod_delayed_work(system_wq, &kp->work,
			 msecs_to_jiffies(AW9523_FIRST_SCAN_MS));
	return IRQ_HANDLED;
}

/* Find the column Ctrl and left Shift share, if they do. */
static void aw9523_kp_find_ctrl_shift(struct aw9523_keypad *kp)
{
	const unsigned short *keycodes = kp->input->keycode;
	int ctrl = -1, shift = -1;
	unsigned int r, c, code;

	for (c = 0; c < kp->cols; c++)
		for (r = 0; r < kp->rows; r++) {
			code = MATRIX_SCAN_CODE(r, c, kp->row_shift);
			if (keycodes[code] == KEY_LEFTCTRL)
				ctrl = code;
			else if (keycodes[code] == KEY_LEFTSHIFT)
				shift = code;
		}

	kp->cs_col = -1;
	if (ctrl < 0 || shift < 0 ||
	    (ctrl & (BIT(kp->row_shift) - 1)) != (shift & (BIT(kp->row_shift) - 1)))
		return;
	kp->cs_col = ctrl & (BIT(kp->row_shift) - 1);
	kp->cs_ctrl_row = ctrl >> kp->row_shift;
	kp->cs_shift_row = shift >> kp->row_shift;
}

static void aw9523_kp_cancel(void *data)
{
	struct aw9523_keypad *kp = data;

	disable_irq(kp->client->irq);
	cancel_delayed_work_sync(&kp->work);
}

static int aw9523_kp_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct aw9523_keypad *kp;
	struct input_dev *input;
	int ret, id = -ENODEV, tries;

	if (!client->irq)
		return dev_err_probe(dev, -EINVAL, "no interrupt\n");

	kp = devm_kzalloc(dev, sizeof(*kp), GFP_KERNEL);
	if (!kp)
		return -ENOMEM;
	kp->client = client;
	INIT_DELAYED_WORK(&kp->work, aw9523_kp_work);
	i2c_set_clientdata(client, kp);

	ret = matrix_keypad_parse_properties(dev, &kp->rows, &kp->cols);
	if (ret)
		return ret;
	if (kp->rows > AW9523_MAX_ROWS || kp->cols > AW9523_MAX_COLS)
		return dev_err_probe(dev, -EINVAL, "matrix %ux%u too large\n",
				     kp->rows, kp->cols);
	kp->row_shift = get_count_order(kp->cols);
	kp->row_mask = GENMASK(kp->rows - 1, 0);
	kp->col_mask = GENMASK(kp->cols - 1, 0);

	/* deasserted = chip running */
	kp->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(kp->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(kp->reset_gpio), "reset gpio\n");
	aw9523_kp_hw_reset(kp);

	/* vendor: up to 5 tries, 10 ms apart */
	for (tries = 0; tries < 5; tries++) {
		id = i2c_smbus_read_byte_data(client, AW9523_CHIP_ID);
		if (id == AW9523_CHIP_ID_VAL)
			break;
		msleep(10);
	}
	if (id != AW9523_CHIP_ID_VAL)
		return dev_err_probe(dev, -ENODEV, "chip id %d, expected %#x\n",
				     id, AW9523_CHIP_ID_VAL);

	ret = aw9523_kp_init_chip(kp);
	if (ret)
		return ret;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;
	kp->input = input;
	/* vendor input device name */
	input->name = "Integrated keyboard";
	input->phys = "aw9523-keypad/input0";
	input->id.bustype = BUS_I2C;
	input_set_capability(input, EV_MSC, MSC_SCAN);

	ret = matrix_keypad_build_keymap(NULL, NULL, kp->rows, kp->cols,
					 NULL, input);
	if (ret)
		return dev_err_probe(dev, ret, "keymap\n");
	if (!device_property_read_bool(dev, "linux,no-autorepeat"))
		__set_bit(EV_REP, input->evbit);
	input_set_drvdata(input, kp);
	aw9523_kp_find_ctrl_shift(kp);

	/* first scan from the work; it enables the irq when idle */
	kp->irq_off = true;
	ret = devm_request_irq(dev, client->irq, aw9523_kp_irq, IRQF_NO_AUTOEN,
			       dev_name(dev), kp);
	if (ret)
		return dev_err_probe(dev, ret, "irq %d\n", client->irq);

	ret = input_register_device(input);
	if (ret)
		return ret;

	schedule_delayed_work(&kp->work, 0);
	/* the irq is enabled when this runs, so disable_irq() balances */
	ret = devm_add_action(dev, aw9523_kp_cancel, kp);
	if (ret) {
		cancel_delayed_work_sync(&kp->work);
		return ret;
	}
	return 0;
}

static int aw9523_kp_suspend(struct device *dev)
{
	struct aw9523_keypad *kp = dev_get_drvdata(dev);

	disable_irq(kp->client->irq);
	cancel_delayed_work_sync(&kp->work);
	aw9523_kp_release_all(kp);
	return 0;
}

static int aw9523_kp_resume(struct device *dev)
{
	struct aw9523_keypad *kp = dev_get_drvdata(dev);

	/* vendor aw9523_i2c_resume(): reset and init again */
	aw9523_kp_hw_reset(kp);
	aw9523_kp_init_chip(kp);
	/*
	 * Hand the suspend's disable to the scan work, or drop it if the
	 * work already owns one.
	 */
	if (kp->irq_off)
		enable_irq(kp->client->irq);
	else
		kp->irq_off = true;
	schedule_delayed_work(&kp->work, 0);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(aw9523_kp_pm, aw9523_kp_suspend,
				aw9523_kp_resume);

static const struct of_device_id aw9523_kp_of_match[] = {
	{ .compatible = "awinic,aw9523-keypad" },
	{ }
};
MODULE_DEVICE_TABLE(of, aw9523_kp_of_match);

static const struct i2c_device_id aw9523_kp_id[] = {
	{ "aw9523-keypad" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw9523_kp_id);

static struct i2c_driver aw9523_kp_driver = {
	.driver = {
		.name = "aw9523-keypad",
		.of_match_table = aw9523_kp_of_match,
		.pm = pm_sleep_ptr(&aw9523_kp_pm),
	},
	.probe = aw9523_kp_probe,
	.id_table = aw9523_kp_id,
};
module_i2c_driver(aw9523_kp_driver);

MODULE_DESCRIPTION("AW9523B keyboard matrix (Planet Cosmo Communicator)");
MODULE_LICENSE("GPL");
