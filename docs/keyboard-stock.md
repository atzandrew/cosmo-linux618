# Keyboard: stock implementation and mainline comparison

Sources: vendor `drivers/misc/mediatek/aw9523/aw9523_key.c` and
`drivers/misc/mediatek/aw9524/aw9524_key.c` (gemian/cosmo-linux-kernel-4.4,
extracted with `git show HEAD:<path>`), the stock DTB appended to the Gemian
boot image and the stock `dtbo` (both decompiled with dtc). Live checks on
2026-10-25 with `tools/aw9523-stock-scan.c` on the try25 kernel.

## What stock does

| Item | Stock | Receipt |
|---|---|---|
| Chip | AW9523, i2c4 @ 0x5b (`aw9523_key@5b`) | dtbo fragment@40 |
| i2c4 | 400 kHz, `mediatek,use-open-drain` | dtbo fragment@40 |
| Reset | GPIO175 (`aw9523_hwen_high/low`): 5 ms low, 5 ms high | dtbo fragment@10, `aw9523_hw_reset()` |
| Interrupt | EINT 12, `interrupts = <12 IRQ_TYPE_LEVEL_LOW>`, GPIO12 pull-up, no debounce | dtbo fragment@59, `aw9523pin` |
| Init | soft reset; P0 inputs; P1 all outputs low (P1_7 included); P0 interrupt on, P1 off. GCR (0x11) and LED-mode registers not written | `aw9523_init_keycfg()` |
| Matrix | rows P0_0..7, columns P1_0..6, keymap index col*8+row; same table as the Gemini PDA | `key_map[]` |
| Scan | on interrupt: P0 interrupt off (0x06 = 0xff), read P0; 1 ms later scan: per column, P1_CONFIG and P1_OUTPUT read-modify-write (that column output low, the others inputs, P1_7 untouched), then one P0_INPUT read | `aw9523_int_work()`, `aw9523_key_eint_work()` |
| Held keys | rescan every 10 ms by hrtimer, no interrupts; when all keys are up, columns back to low and the P0 interrupt back on | `aw9523_schedule_matrix_rescan()`, `aw9523_reset_to_monitor_for_state_change()` |
| Ghosting | a change is reported only when no two pressed keys share a row bit across columns; ctrl+shift+z/x/c/v special case | `aw9523_key_eint_work()` |
| Fn | (4,3) = KEY_LEFTMETA | `key_map[]` |
| Lid | hall notifier: closed = reset line low, keyboard off; open = reset + init + rescan | `aw9523_hall_notifier_callback()` |
| Esc | not in this driver; stock also registers `mtk-kpd` (`kp@10010000`) | stock DTB |

AW9524 (i2c3 @ 0x5b, reset GPIO109): all pins in LED (current) mode,
`P0_2` lit at 0x40, the rest off; a proc file sets the cover RGB LED
currents. The `kbd_backlight` LED class device drives SoC PWM1 (FIFO mode)
and GPIO178, not the AW9524. The AW9524 is not part of the matrix.

## Live checks (try25 kernel, keypad driver unbound)

`tools/aw9523-stock-scan` drives the AW9523 over `/dev/i2c-4`, polled every
10 ms with the chip interrupt off. Keys pressed: 2, 4, 5, 6, 7, P,
Backspace, U, S, J, 1, 3, Fn.

| Run | Sequence | Result |
|---|---|---|
| A `stock` | vendor sequence exactly | every key at its stock position; no (0,0) on column-0 keys |
| B `p17in` | A with P1_7 left an input | same as A (P not seen; P also gave only 25-150 ms blips in A) |
| C `mainline` | the register writes of matrix_keypad + pinctrl-aw9523 (cached writes, one P0 read per row, column set high before input) | same as A |
| D real driver | try25 kernel, drivers rebound, evtest + /proc/interrupts | 1, U, P reported (U without (0,0)); 2, 3, 4 lost. EINT 12 counted ~40, the nested row IRQs 0 |

Wiring, keymap, P1_7 and the scan sequence are not the fault: both register
sequences read every key from userspace. What differs is the interrupt path,
which only the kernel driver uses, and the losses vary between runs (3 worked
on try25, not in run D).

## Mainline interrupt path (6.18), where it departs from stock

- `aw9523_irq_thread_func()` reads both input ports and raises a nested row
  IRQ only for bits that differ from `cached_gpio`, which only this thread
  updates. matrix_keypad's own scan reads P0_INPUT too (clearing the chip
  interrupt) and toggles the rows while it drives columns, so the cached
  copy can hold a mid-scan value; a later press that matches it raises no
  row IRQ and is never scanned.
- `disable_irq_nosync()` is lazy: the chip's P0 interrupt stays enabled
  during the scan (stock writes 0x06 = 0xff first), so the thread runs
  concurrently with the scan. The regmap has `disable_locking` and the
  thread does not take `i2c_lock`; `irq_bus_lock` sets the whole regmap
  cache-only, where a volatile read returns -EBUSY and matrix_keypad's
  `row_asserted()` turns a negative value into "pressed" (a ghost).
- EINT 12 is falling-edge in our DTS; stock uses level-low, which re-runs
  the handler while the chip holds INT low.
- matrix_keypad only scans after a row IRQ; stock scans after any chip
  interrupt and then polls every 10 ms until all keys are up.

## Options (not yet chosen)

1. DT only, stock values: EINT 12 level-low, i2c4 400 kHz. Small; does not
   remove the `cached_gpio` comparison.
2. A keyboard driver that follows the stock algorithm (interrupt as "something
   changed", chip interrupt off while polling, one P0 read per column) on top
   of the AW9523 regmap.
3. Patch pinctrl-aw9523 / matrix_keypad for the lazy-disable and cache-only
   races (upstream work).
