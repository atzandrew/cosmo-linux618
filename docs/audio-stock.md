# Audio: stock inventory

How the stock 4.4 kernel (gemian cosmo-linux-kernel-4.4) and the stock
device tree (base DTB + dtbo overlay, p0 dump 2026-10-25) drive audio, and
what mainline 6.18 has for each part. File paths without a prefix are in
the stock kernel tree.

## Parts

| Part | Stock | Mainline 6.18 |
|---|---|---|
| AFE (SoC audio front end) | `mediatek,mt_soc_pcm_dl1` @ 0x11220000, IRQ SPI 161 level-low, 25 clocks | `mt8183.dtsi` `afe` (same base, same IRQ), `mt8183-afe-pcm` |
| Codec | MT6358 analog, `sound/soc/mediatek/codec/mt6358/mtk-soc-codec-6358.c` | `sound/soc/codecs/mt6358.c`, node `mt6358codec` in `mt6358.dtsi` |
| Speaker amps | two pulse-mode GPIO amps, GPIO153 and GPIO111 | `sound/soc/codecs/aw8738.c` (same pulse sequence) |
| Machine driver | vendor `mt-snd-card` | none fits as is (mt8183 boards use I2S amps) |
| Headset detect | `drivers/misc/mediatek/accdet/mt6771/accdet.c` | no MT6358 ACCDET driver (only `mt6359-accdet`) |

## AFE

- Stock base DTB: `audio@11220000` (`mediatek,audio`, syscon, 0x1000) and
  `mt_soc_dl1_pcm@11220000`, `interrupts = <0 0xa1 8>` (SPI 161,
  level-low). `audio_sram@11221000` size 0xc000.
- Clock list, in order, matches `mt8183.dtsi` `afe`: every stock clock
  index is the mainline `dt-bindings/clock/mt8183-clk.h` index + 1
  (audio: afe 9/8, dac 3/2, dac_predis 2/1, adc 4/3, adda6 14/13,
  22m 8/7, 24m 7/6, apll tuners 5,6/4,5, tml 1/0; topckgen: mux_audio
  0x0c/11, intbus 0x0d/12, syspll_d2_d4 0x31/48, aud_1 0x14/19,
  apll1_ck 0x49/72, aud_2 0x15/20, apll2_ck 0x4d/76, aud_eng1 0x1f/30,
  apll1_d8 0x4c/75, aud_eng2 0x20/31, apll2_d8 0x50/79; infracfg audio
  0x30/47, audio_26m 0x38/55). Stock also takes `scp_sys_audio` (scpsys);
  mainline uses `power-domains = <&spm MT8183_POWER_DOMAIN_AUDIO>`.

## Speaker

- Codec side (`Speaker_Amp_Change`, mtk-soc-codec-6358.c:3755): the
  speaker is fed from the MT6358 line-out, LOL ("Switch LOL MUX to audio
  DAC", AUDDEC_ANA_CON7 = 0x011b). Mainline: `LOL Mux` -> `LINEOUT L`.
- Amp side (`Ext_Speaker_Amp_Change`, mtk-soc-codec-6358.c:3887): both
  amps are switched together. On: headphone_en low, both amp pins low,
  1-20 ms, then `AudDrv_GPIO_EXTAMP_Select(true, 3)` and
  `AudDrv_GPIO_EXTAMP2_Select(true, 3)`, then 25 ms warm-up. Off: both
  pins low, 500 us.
- `AudDrv_GPIO_EXTAMP_Select` (common_int/mtk-auddrv-gpio.c:510): mode 3
  = three pulses of (low, 2 us, high, 2 us). Off = low.
- Mainline `aw8738.c` POST_PMU: `awinic,mode` pulses of (0, 2 us, 1,
  2 us), then 40 ms; PRE_PMD: low, 1-2 ms. Same sequence with
  `awinic,mode = <3>`.
- Pins (dtbo fragment@16): extamp = GPIO153 (`pins = <0x9900>`), extamp2
  = GPIO111 (`0x6f00`), both GPIO mode, output.
- Stock skips the speaker while HDMI is plugged
  (`gpio_get_value(hdmi_det_gpio)`, hdmi_det-gpio from the fusb302
  Type-C node).
- `/proc/AEON_ENABLE_SPEAKER` (mtk-auddrv-gpio.c): manual amp on/off
  from userspace, both amps or one at a time.
- Our DTS holds GPIO111 low with the `extamp2-hog` (G1, stock idle level).
  An amp node that owns GPIO111 replaces the hog.

## Headphone GPIOs

- dtbo fragment@16: headphone_en = GPIO108 (`0x6c00`), headphone_cs =
  GPIO107 (`0x6b00`), GPIO mode, output.
- headphone_en is driven high only by `Voice_Amp_Change` (earpiece path,
  :3618) and by `/proc/headphone_cs`; it is driven low when the speaker
  turns on (:3897), when the earpiece turns off (:3686) and when the ADC
  turns off (:5431). Headphone playback itself (`Headset_Amp_Change`)
  does not touch either pin.
- `/proc/headphone_cs` (mtk-soc-codec-6358.c:7376): `1` = cs high + en
  high, `0` = cs low + en high, `2` = en low. What the switch selects is
  not known from the source.
- Idle levels on our kernel match stock (180-pin readback, session log
  2026-10-29).

## PMIC audio interface pins

- dtbo fragment@16, `audgpio` pinctrl: aud_clk_mosi GPIO136, aud_dat_mosi
  GPIO138/139, aud_dat_miso GPIO142/143; `_on` = mode 1, `_off` = mode 0.
- Mainline mt8183 machine drivers use the same state names
  (`aud_clk_mosi_on/off`, `aud_dat_mosi_on/off`, `aud_dat_miso_on/off`).

## Microphones

- Main mic: AIN0, MICBIAS0 1.9 V (AUDENC_ANA_CON9 = 0x0021).
- Headset mic: AIN1, MICBIAS1 2.6 V (AUDENC_ANA_CON10 = 0x0061).
- Reference mic: AIN2 on the R PGA (mtk-soc-codec-6358.c:3555).
- Mainline mt6358.c has AIN0/AIN1/AIN2 inputs and the PGA muxes.

## Headset detect (ACCDET)

- dtbo fragment@13: accdet-mic-vol 6, accdet-mic-mode 1, plugout
  debounce 1, headset-eint-level-pol 8, headset-mode-setting
  `<0x500 0x500 1 0x1f0 0x800 0x800 0x20 0x44>`, three-key thresholds
  `<0 0x50 0xdc 0x190>`, four-key `<0 0x3a 0x79 0xc0 0x190>`.
- Stock driver: accdet/mt6771/accdet.c (2776 lines). Mainline has no
  MT6358 ACCDET; mt6359-accdet.c is the closest base.

## Suspend

- Stock turns the amps off by driving both pins low; the codec power-down
  paths (`Speaker_Amp_Change(false)`, `TurnOffDacPower`) run when the
  stream stops. In mainline the same happens through DAPM when the
  stream closes; aw8738 PRE_PMD drives the pin low.
