# Cosmo LEDs: stock evidence and first controls

## 2026-10-27 — stock receipts

Source: gemian/cosmo-linux-kernel-4.4 at
`a741a2e89bcc2b0b7e2d79769aa5375fe13b0b03`, read with `git show HEAD:<path>`.
Line numbers below refer to that revision.

### AW9524

`drivers/misc/mediatek/aw9524/aw9524_key.c`:

- 156–164: HWEN reset low 5 ms, high 5 ms.
- 67, 470–481: chip ID register 0x10, expected value 0x23.
- 224–248: software reset 0x7f=0; LED mode 0x12/0x13=0;
  direction 0x04/0x05=0; global control 0x11=2 (quarter-scale current).
  P0_2 DIM set to 0x40, other explicitly listed channels to zero.
- 71–86: DIM map is non-linear: P1_0..3 at 0x20..23, P0_0..7 at
  0x24..2b, P1_4..7 at 0x2c..2f.
- 334–395: four independent triplets. Selector 1=P0_0..2,
  7=P0_3..5, 2=P1_0..2, 3=P1_3..5. Selector 6 groups the latter three.
  Red uses the supplied digit times 8, other channels times 10.
  This identifies electrical groups, not physical locations or the
  green/blue order. Those need visual observation.

Stock overlay evidence: entry 0 of the backed-up `dtbo` partition,
extracted by parsing its Android DT table and decompiled with dtc:
fragment@39 targets i2c3 (400 kHz, open-drain) and declares
`aw9524_key@5b`, compatible `mediatek,aw9524_key`;
fragment@12 uses pin 0x6d00 (GPIO109), low/high for reset.
This agrees with `arch/arm64/boot/dts/mediatek/k71v1_64_bsp.dts:327–349`.
Base `mt6771.dts:597–610`: i2c3 base 0x1100f000, DMA 0x11000400,
IRQ 84, no multi-channel register offset (unlike i2c4).

### Keyboard backlight (separate from AW9524 sinks)

Same driver, 304–331: GPIO178 plus PWM1 FIFO, CLK_DIV8,
PWM_CLK_NEW_MODE_BLOCK, stop bit 63, high/low durations 1,
guard duration 0, continuous wave count 0. GPIO178 is asserted for on,
but deasserted for off only if `hdmi_det_gpio` is low. Shared HDMI logic
must be preserved or explicitly resolved before claiming ownership.
`aeon_gpio.dtsi:641–659` identifies detect as GPIO54 and the output as
GPIO178.

Lines 424–445, brightness 0–5 SEND_DATA0/SEND_DATA1:

| Level | DATA0 | DATA1 |
|---|---|---|
| 0 | 0 | 0 |
| 1 | 0x00001fff | 0 |
| 2 | 0x07ffffff | 0 |
| 3 | 0xffffffff | 0x0000003f |
| 4 | 0xffffffff | 0x000fffff |
| 5 | 0xffffffff | 0xffffffff |

Stock `mt6771.dts:1632–1647`: controller at 0x11006000, four PWM
channel gates and PWM/HCLK gates. Mainline mt8183.dtsi already declares
this controller; `pwm-mediatek.c:pwm_mediatek_config()` programs old mode
(bit 15), not stock's FIFO mode. A generic pwm-leds declaration alone
does not reproduce the stock sequence.

## First implementation: AW9524 identification

`CONFIG_LEDS_AW9524=m`, module `leds-aw9524`, compatible
`awinic,aw9524-leds`. Device tree enables i2c3 and only its AW9524 child.
The board exposes the 12 stock-controlled channels under pin-based names
`cosmo:aw9524:p0-0` through `p0-5` and `p1-0` through `p1-5`.
The LED class blocking callback writes one DIM register and propagates
I2C errors. Chip ID, reset timing and global current follow stock.

Differences for first identification: all 16 DIM registers start zero,
no default-on channel or trigger; each exposed channel is capped at 64,
the stock initial blue value. This cap is not a measured current limit.
Reset is asserted on unbind/probe failure. No MT6370 LED driver enabled.
No keyboard PWM, panel, reset/watchdog, charger or Wi-Fi changes.

### Pass/fail before testing

1. Boot image identity, power-key and watchdog escapes must be checked as
   required by AGENTS.md; obtain approval before flashing/rebooting.
2. Load only the new LED module (retain the existing Wi-Fi modules).
   PASS: ID 0x23 in dmesg; 12 LED devices with max_brightness=64 and
   brightness=0; no I2C errors or unrelated devices newly bound.
3. One channel at a time at brightness 8, then 0, with visual observation.
   PASS: record position and colour per channel; observed LED goes dark
   at 0. No visible change is an unresolved channel, not proof of absence.
4. On a mapped channel compare brightness 8, 32, 64, 0.
   PASS: observed increasing intensity, then off; no transfer errors.
5. Unload module. PASS: LEDs dark, HWEN reset asserted, SSH and keyboard
   still functional. Reload and verify all channels start at zero.

Runtime tests pending. Keyboard PWM and charge-state policy pending.

## Build result

Source commit 6cd95d8. Full image/module build and final W=1 driver
compile passed. Binding and AW9524 board-node schema checks passed.
Normal and watchdog-test images packed with the PMIC-key safeguard check;
full hashes and pre-flash check are in docs/session-log.md. Runtime
results remain pending; build success is not hardware validation.
