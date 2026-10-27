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

## 2026-10-27 — rear LED runtime results

Status: rear LED current control PASS; complete wiring/colour mapping
PARTIAL. Normal image boot prefix was read back and matches
`d5d4694e7f590a2762b529d352d976e1953f03750e2d8588e85856db20bd4aec`.

Visual mapping, viewed from the rear:

| Position | Red | Green | Blue |
|---|---|---|---|
| Far right, beside camera | P0_3 | P0_4 | P0_5 |
| Bottom-left fingerprint light | not observed | P1_1 | P1_2 |
| Top-left fingerprint light | not observed | P1_4 | P1_5 |

- All four stock third/blue channels at 32/64 lit three rear LEDs.
  Turning P0_2 off made no visible change; P0_5 off extinguished the
  rightmost LED; P1_2 off the bottom-left; P1_5 off the top-left.
- Green channels P0_4/P1_1/P1_4 at 32/64 lit all three green.
- Red candidates P0_3/P1_0/P1_3 at both 32 and 64 lit only the rightmost
  LED. This is not proof that the fingerprint lights lack red emitters.
- Mixed P0_3=32, P0_4=0, P0_5=32 appeared purple on the rightmost LED.
- A red charging indicator stayed lit with all AW9524 channels zero.
  Its controller/relationship to the Q-key LED remains unidentified.
- P0_0 at 8 and blinking at 64 produced no identified visible response.
  P0_1 has not been individually tested. Other unexposed pins remain
  untested; do not infer their wiring.
- Module unload/reload PASS: class devices removed, ID 0x23 reprobed,
  all twelve brightness attributes returned to zero. No AW9524/I2C
  errors appeared in the filtered log. Physical reset-line assertion
  and per-position brightness gradation were not separately observed.
- End state: all triggers none and all twelve brightness values zero.
  The independent charging indicator may remain lit.

### Stock red-channel recheck

Vendor aw9524_key.c:338 names brightness_red=8. Lines 364–370 explicitly
write P1_0 (DIM 0x20) for selector 2 red; 372–378 write P1_3 (DIM 0x23)
for selector 3 red. Selector 6 writes those two and P0_3 together
(380–385). The current module uses those same addresses. No separate
red mode or red-enable operation appears in that path. The four unused
named DIM outputs appear only in register definitions in the source
search, not in these controls.

The source exposes red operations for those electrical groups but does
not identify their physical LED packages. Physical fingerprint placement
comes from the live green/blue tests. Neither source nor the unsuccessful
red tests establishes the installed LED part or all of its wiring.

Next diagnostic, if pursued: read the chip's actual DIM and mode registers
through the driver. LED brightness sysfs readbacks currently reflect
software state; successful writes alone do not verify register retention.
Live stock comparison is deferred; no stock restore is planned.

### Keyboard backlight follow-up

Stock dtbo entry 0 fragment@49 gpio_init_default contains 192 seven-cell
rows. GPIO90 is mode 1 (PWM_A); other PWM_A candidates GPIO6/107/150/153
are mode 0. This identifies the stock PWM pin without guessing.
Vendor pwm/mtk_pwm.c:1278–1292,1367–1389 sets FIFO fields, clock/divider,
then channel enable. pwm/mt6771/mtk_pwm_hal.c:69–98 prepares PWM top,
HCLK and channel clocks; channel 1 base is +0x10 (122–127).

Read-only live clock summary: infra_pwm and infra_pwm1 124800000 Hz,
infra_pwm_hclk 156000000 Hz. Do not substitute the vendor header's
nominal 66 MHz or change clock parents without evidence.
GPIO178 is shared with HDMI: aw9524_key.c:304–316 preserves it on
keyboard-off when GPIO54 detect is high; usb_c/fusb302/usb_typec.c:329–335
also asserts it and reads detect. No keyboard PWM/GPIO178 writes have
been made. The Q-key LED has not been investigated or identified.

## 2026-10-27 — keyboard FIFO implementation and Q-key investigation

The dedicated `leds-cosmo-kbd` module replaces the general PWM provider
at 0x11006000 using compatible `planet,cosmo-kbd-backlight`. The resource
is owned exclusively: no second mapping beside pwm-mediatek, and no raw
userspace MMIO. Other general PWM channels are unavailable under this
board-specific binding. Probe refuses any of the four channels already
enabled by firmware. The display PWM is a separate block and stays disabled.

The LED class device is `cosmo::kbd_backlight`, max_brightness 5.
Levels 1–5 reproduce the stock FIFO words above. PWM_CON is 0x7e03:
stop bit 63, divider enum 3 (divide 8), block clock, FIFO periodic mode,
idle/guard low and old mode disabled. HDUR/LDUR=1, GDUR/WAVENUM=0.
No clock parent or 26 MHz selector write. Stock register receipts:
`drivers/misc/mediatek/pwm/mt6771/mtk_pwm_hal.c:29–46`
(register word indices), 122–127 (channel bases);
`drivers/misc/mediatek/pwm/mt6771/include/mach/mtk_pwm_prv.h:44–69`
(control fields); `drivers/misc/mediatek/include/mt-plat/mt6771/include/mach/mtk_pwm_hal.h:89–100`
(divider enum).

Brightness zero stops channel 1 and releases the three acquired clock
references instead of leaving an all-zero FIFO running as stock does.
GPIO178 is lowered only when GPIO54 reads low, exactly preserving stock's
keyboard-off decision. A failed GPIO54 read leaves GPIO178 unchanged and
returns the error. Probe acquires GPIO178 as-is without driving its level.
Unregister/shutdown/suspend turn keyboard PWM off with that same HDMI check.
The module does not implement HDMI detection, connector probing, or the
USB-C driver's GPIO178 probing pulses. A future HDMI driver must share a
single owner/arbitration mechanism rather than independently requesting
GPIO178. HDMI operation and suspend remain untested.

Read-only live check before implementation: debugfs PWM reported all four
channels disabled and unrequested. GPIO debugfs showed no owner for
GPIO54/90/178. No keyboard PWM/GPIO178 writes were made on the device.

### Q-key LED: evidence, not a wiring assignment

Stock `arch/arm64/boot/dts/mediatek/mt6370.dtsi:116–130` declares
MT6370 sinks named green, blue, mt6370_pmu_led3, red in channel order.
The generic vendor LED entries in `k71v1_64_bsp.dts:34–64` have
led_mode=0 (disabled), including keyboard-backlight; stock AW9524's
separate driver supplies the actual keyboard LED class.

MT6370 sink 4 has separate charge-indicator registers (vendor
`drivers/misc/mediatek/pmic/mt6370/mt6370_pmu_rgbled.c:89–93`).
Mainline `drivers/leds/rgb/leds-mt6370-rgb.c:159–160,854–869`
programs RGB_EN and CHGIND_EN; registering an indicator is not a read-only
identification operation. Existing charge illumination may be autonomous
from firmware state. Neither stock names nor the red light remaining on
with AW9524 off prove which physical LED is beside Q.

Q-key controller remains UNIDENTIFIED. No MT6370 LED driver has been
enabled, no indicator register has been changed, and no stock boot is
planned. The next step is physical position/colour observation, followed
by a separately approved MT6370 indicator test if the evidence warrants it.

### Keyboard runtime pass/fail plan

After explicit flash/reboot approval and both new-config escape checks:

1. Load only the keyboard module, preserving Wi-Fi modules. PASS: one
   keyboard LED class device, max=5, brightness=0, no visible backlight.
2. With HDMI disconnected, set 1, then 0. PASS: keyboard illumination,
   then darkness; SSH/keyboard and charging remain functional.
3. Compare 1, 2, 3, 4, 5 briefly, then 0. PASS: visibly increasing
   illumination, no unrelated LED change or kernel errors.
4. Unload/reload. PASS: PWM stops, clocks balance, class returns at zero.
5. GPIO54-high preservation requires separate observed HDMI conditions;
   do not claim hardware PASS from source inspection alone.

Build checks and artifact identities are recorded in the session log.
Runtime keyboard result remains PENDING.
