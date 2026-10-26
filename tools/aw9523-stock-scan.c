// SPDX-License-Identifier: GPL-2.0
/*
 * aw9523-stock-scan: run the stock Cosmo keyboard scan (vendor
 * drivers/misc/mediatek/aw9523/aw9523_key.c, gemian/cosmo-linux-kernel-4.4)
 * from userspace over i2c-dev, and print every change of the key state.
 *
 * Usage: aw9523-stock-scan <stock|p17in|mainline> <seconds> [bus] [addr]
 *   stock:    vendor sequence exactly (P1_7 kept an output driven low).
 *   p17in:    the same, but P1_7 left an input (what mainline does).
 *   mainline: the register writes of 6.18 matrix_keypad + pinctrl-aw9523
 *             (cached regmap: writes only on change, one P0 read per row,
 *             a column is set high before it goes back to input).
 * Polled every 10 ms (the vendor rescan period) with the chip's P0
 * interrupt off, so no interrupt handler runs. Unbind the kernel keypad
 * first; the chip is soft-reset, so reboot (or rebind) afterwards.
 */
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define P0_INPUT  0x00
#define P1_OUTPUT 0x03
#define P1_CONFIG 0x05
#define P0_CONFIG 0x04
#define P0_INT    0x06
#define P1_INT    0x07
#define ID_REG    0x10
#define SW_RSTN   0x7f
#define P0_KROW_MASK 0xff
#define P1_KCOL_MASK 0x7f
#define NCOL 7
#define NROW 8

/* Vendor key_map[], index = col * 8 + row. */
static const char *names[NCOL * NROW] = {
	"1", "U", "S", "Z", ",", "'", "8", "J",
	"2", "W", "D", "C", "ALT-L", "LEFT", "9", "K",
	"3", "Y", "TAB", "N", "M", "DOWN", "BACKSPACE", "I",
	"4", "T", "F", "X", "META-L(Fn)", "SHIFT-R", "P", "-",
	"5", "E", "G", "V", "SPACE", "UP", "O", "-",
	"6", "Q", "A", "B", ".", "RIGHT", "ENTER", "-",
	"7", "R", "H", "SHIFT-L", "CTRL", "L", "0", "-",
};

static int fd, addr;

static int xfer(struct i2c_msg *m, int n)
{
	struct i2c_rdwr_ioctl_data d = { m, n };
	return ioctl(fd, I2C_RDWR, &d);
}

static void wr(uint8_t reg, uint8_t val)
{
	uint8_t b[2] = { reg, val };
	struct i2c_msg m = { addr, 0, 2, b };
	if (xfer(&m, 1) < 0) { perror("i2c write"); exit(1); }
}

static uint8_t rd(uint8_t reg)
{
	uint8_t b = reg;
	struct i2c_msg m[2] = { { addr, 0, 1, &b }, { addr, I2C_M_RD, 1, &b } };
	if (xfer(m, 2) < 0) { perror("i2c read"); exit(1); }
	return b;
}

/* Shadow of a regmap-cached register: write only when the value changes. */
static uint8_t sh_out, sh_conf;

static void upd(uint8_t reg, uint8_t *sh, uint8_t mask, uint8_t val)
{
	uint8_t n = (*sh & ~mask) | (val & mask);
	if (n != *sh) {
		wr(reg, n);
		*sh = n;
	}
}

/* __activate_col() through aw9523_direction_output/_set/_direction_input */
static void ml_col(int c, int on)
{
	if (on) {
		upd(P1_OUTPUT, &sh_out, 1 << c, 0);	/* active-low: level 0 */
		upd(P1_CONFIG, &sh_conf, 1 << c, 0);	/* output */
	} else {
		upd(P1_OUTPUT, &sh_out, 1 << c, 1 << c);	/* inactive = high */
		upd(P1_CONFIG, &sh_conf, 1 << c, 1 << c);	/* input */
	}
}

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	uint8_t keep, old[NCOL], st[NCOL], v;
	char dev[32];
	double t0, end;
	int i, j, idle;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <stock|p17in> <seconds> [bus] [addr]\n", argv[0]);
		return 2;
	}
	int ml = !strcmp(argv[1], "mainline");

	keep = strcmp(argv[1], "stock") ? 0x80 : 0x00;	/* P1_7 config bit */
	snprintf(dev, sizeof(dev), "/dev/i2c-%s", argc > 3 ? argv[3] : "4");
	addr = argc > 4 ? strtol(argv[4], NULL, 0) : 0x5b;
	fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }

	printf("mode %s, %s 0x%02x, chip id 0x%02x\n", argv[1], dev, addr, rd(ID_REG));

	/* aw9523_init_keycfg(); P0 interrupt left off (polled). */
	wr(SW_RSTN, 0x00);
	wr(P0_CONFIG, 0xff);
	wr(P1_CONFIG, 0x00 | keep);
	wr(P1_OUTPUT, 0x00);
	wr(P0_INT, 0xff);
	wr(P1_INT, 0xff);
	printf("init: P1_CONFIG 0x%02x P1_OUTPUT 0x%02x P0_INPUT 0x%02x\n",
	       rd(P1_CONFIG), rd(P1_OUTPUT), rd(P0_INPUT));

	if (ml) {
		/* pinctrl-aw9523 hw_init: GPIO mode, P0 open-drain, all inputs */
		wr(0x12, 0xff);
		wr(0x13, 0xff);
		wr(0x11, 0x00);
		wr(P0_CONFIG, 0xff);
		wr(P1_CONFIG, 0xff);
		wr(P1_OUTPUT, 0xff);
		sh_conf = 0xff;
		sh_out = 0xff;
		for (i = 0; i < NCOL; i++)
			ml_col(i, 1);	/* activate_all_cols(true) when idle */
		printf("mainline idle: P1_CONFIG 0x%02x P1_OUTPUT 0x%02x\n",
		       rd(P1_CONFIG), rd(P1_OUTPUT));
	}

	memset(old, P0_KROW_MASK, sizeof(old));
	t0 = now();
	end = t0 + atof(argv[2]);
	fflush(stdout);
	while (now() < end) {
		if (ml) {
			/* matrix_keypad_scan() */
			for (i = 0; i < NCOL; i++)
				ml_col(i, 0);
			for (i = 0; i < NCOL; i++) {
				ml_col(i, 1);
				st[i] = 0;
				for (j = 0; j < NROW; j++)	/* one read per row */
					st[i] |= rd(P0_INPUT) & (1 << j);
				ml_col(i, 0);
			}
			for (i = 0; i < NCOL; i++)
				ml_col(i, 1);
			goto report;
		}
		/* aw9523_key_eint_work(): one column low at a time. */
		for (i = 0; i < NCOL; i++) {
			v = rd(P1_CONFIG);
			wr(P1_CONFIG, (P1_KCOL_MASK | v) & ~(1 << i));
			v = rd(P1_OUTPUT);
			wr(P1_OUTPUT, (P1_KCOL_MASK | v) & ~(1 << i));
			st[i] = rd(P0_INPUT) & P0_KROW_MASK;
		}
report:
		if (memcmp(old, st, sizeof(st))) {
			printf("%7.3f", now() - t0);
			for (i = 0; i < NCOL; i++)
				printf(" %02x", st[i]);
			printf("  pressed:");
			for (i = 0; i < NCOL; i++)
				for (j = 0; j < NROW; j++)
					if (!(st[i] & (1 << j)))
						printf(" %s(%d,%d)", names[i * NROW + j], j, i);
			printf("\n");
			fflush(stdout);
			memcpy(old, st, sizeof(st));
		}
		idle = 1;
		for (i = 0; i < NCOL; i++)
			idle &= st[i] == P0_KROW_MASK;
		if (idle && !ml) {
			/* aw9523_reset_to_monitor_for_state_change() */
			v = rd(P1_CONFIG);
			wr(P1_CONFIG, v & ~P1_KCOL_MASK);
			v = rd(P1_OUTPUT);
			wr(P1_OUTPUT, v & ~P1_KCOL_MASK);
			rd(P0_INPUT);
		}
		usleep(10000);
	}
	printf("done: P1_CONFIG 0x%02x\n", rd(P1_CONFIG));
	return 0;
}
