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
| Headset detect | `drivers/misc/mediatek/accdet/mt6771/accdet.c` | `sound/soc/codecs/mt6358-accdet.c` (Cosmo, ported from stock) |

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

- Stock speaker path is the headphone buffer, not the line-out:
  vendor `/etc/audio_param/AudioParamOptions.xml` (Android vendor
  partition, mmcblk0p35) has `MTK_AUDIO_SPEAKER_PATH = int_hp_buf` and
  `SPK_PATH_HP = yes`. `/etc/audio_device.xml` maps the speaker to the
  kcontrols `Headset_Speaker_Amp_Switch` (headphoneSpeaker_output) and
  `Ext_Speaker_Amp_Switch` (ext_speaker_output).
- `Headset_Speaker_Amp_Change` (mtk-soc-codec-6358.c:3992): HPL mux =
  line-out, HPR mux = DAC-R, HP and LO drivers on, HP/LO gain -10 dB.
  Mainline equivalent: HPL/HPR Mux "LoudSPK Playback"
  (`mtk_hp_spk_enable`).
- `Speaker_Amp_Change` (:3755, LOL only) exists in the kernel but is not
  the configured path on this device.
- Amp side (`Ext_Speaker_Amp_Change`, :3887): both amps switched
  together. On: headphone_en low, both amp pins low, 1-20 ms, then
  `AudDrv_GPIO_EXTAMP_Select(true, 3)` and
  `AudDrv_GPIO_EXTAMP2_Select(true, 3)`, then 25 ms warm-up. Off: both
  pins low, 500 us. Skipped while HDMI is plugged (`hdmi_det_gpio`).
- `AudDrv_GPIO_EXTAMP_Select` (common_int/mtk-auddrv-gpio.c:510): mode 3
  = three pulses of (low, 2 us, high, 2 us). Off = low. Mainline
  `aw8738.c` with `awinic,mode = <3>` sends the same sequence.
- Planet additions keep the amps on whenever no headset and no HDMI is
  present: accdet/mt6771/accdet.c:943 (`accdet_eint_work_callback`) and
  usb_c/fusb302/usb_typec.c:342.
- Pins (dtbo fragment@16): extamp = GPIO153, extamp2 = GPIO111, GPIO
  mode, output. `/proc/AEON_ENABLE_SPEAKER` switches them from userspace.

## Headphone GPIOs

- dtbo fragment@16: headphone_en = GPIO108 (`0x6c00`), headphone_cs =
  GPIO107 (`0x6b00`), GPIO mode, output.
- headphone_en is driven high only by `Voice_Amp_Change` (earpiece path,
  :3618) and by `/proc/headphone_cs`; it is driven low when the speaker
  turns on (:3897), when the earpiece turns off (:3686) and when the ADC
  turns off (:5431). Headphone playback itself (`Headset_Amp_Change`)
  does not touch either pin.
- `/proc/headphone_cs` (mtk-soc-codec-6358.c:7376): `1` = cs high + en
  high, `0` = cs low + en high, `2` = en low. Written at runtime by the
  Planet Android framework (string `/proc/headphone_cs` in
  system/framework/oat/arm64/services.vdex); the logic is not read yet.
  What the switch selects is not known.
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
  MT6358 ACCDET, and mt6359-accdet.c has a different register map, so
  `mt6358-accdet.c` follows the stock sequence (2026-11-24):
  - Config: CONFIG_ACCDET_EINT_IRQ + CONFIG_ACCDET_SUPPORT_EINT0, SW mode
    (HW_MODE_SUPPORT undefined), level-low EINT0 inside the PMIC.
  - Idle registers equal a Gemian dump with nothing plugged in: ACCDET_CON01
    0x4, CON02 0x808, CON03/04 0x4ff, CON05 0x81f0, CON13 0, CON15 0x2671,
    CON24 0xc018, AUDENC_ANA_CON6 0x60, CON10 0x64, CON11 0x807,
    AUD_TOP_INT_CON0 0x60.
  - Plug-in enables the ACCDET unit (CON01 EN, CON02 0x7 + PWM idle). This
    also connects MICBIAS1 to the headset mic: with the unit off the headset
    mic records ~25 dB too quiet. CON14 AB state 01 = headset with mic,
    00 = headphones (unit off again after 6 s, stock micbias_timer).
  - Mainline mt6358.c writes ACCDET_CON13 = 0x700e ("accdet s/w enable");
    stock leaves 0. Removed in the Cosmo codec delta.
  - Mainline `enum mt6358_irq_top_status_shift` has no XPP entry: stock
    TOP_INT_STATUS0 has XPP at bit 6, AUD at 7, MISC at 8 (upmu_hw.h
    PMIC_INT_STATUS_*_TOP_SHIFT). Without it, ACCDET interrupts are never
    dispatched and the level-high PMIC line loops. Fixed in the delta
    (include/linux/mfd/mt6358/core.h); an upstream bug.
  - Not a wake source: both IRQs are masked over suspend; a plug change in
    sleep is handled at resume.
- Headset mic level: stock Audio_PGA1_Setting 3 (18 dB, Gemian amixer).
  With the Apple headset that is ~-57 dBFS for speech; the image uses the
  24 dB maximum plus a 150 % default input volume (-37 dBFS RMS, -22 peak).
- Headset buttons are not handled yet (stock thresholds above).

## Suspend

- Stock turns the amps off by driving both pins low; the codec power-down
  paths (`Speaker_Amp_Change(false)`, `TurnOffDacPower`) run when the
  stream stops. In mainline the same happens through DAPM when the
  stream closes; aw8738 PRE_PMD drives the pin low.
