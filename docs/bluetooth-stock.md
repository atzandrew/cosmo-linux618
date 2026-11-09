# Bluetooth (CONSYS) on the Cosmo: stock reference and port (B0)

Research 2026-11-04, no image. Sources: stock 4.4 tree
(`drivers/misc/mediatek/connectivity/bt/mt66xx`, `k71v1_64_bsp_defconfig`),
the stock `nvdata` partition (backup 2026-10-25, read with `debugfs`), the
Gemini PDA port (gemini-nixos `docs/bluetooth-bringup.md`,
`docs/bluetooth-a2dp.md`, delta `drv_bt/`), and live reads under 6.18.

## Stock

- Driver: `bt/mt66xx/stp_chrdev_bt.c` (`bt_drv`, built for `CONSYS_6771`
  with `CONFIG_MTK_COMBO_BT`). It makes `/dev/stpbt` (major 192): raw H4
  HCI frames to and from the STP BT channel (`BT_TASK_INDX` 0). `open`
  runs `mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT)`, `release` the matching
  func-off. The Android BT stack does the controller setup in userspace.
- No separate BT firmware: the ROMv4 patch that WMT loads at CONSYS power-on
  covers the whole MCU.
- Stock defconfig: `BT=y`, `BT_RFCOMM(+TTY)`, `BT_BNEP` (MC and protocol
  filters), `BT_HIDP`, `BT_HCIVHCI`, `UHID`, `MTK_COMBO_BT`.
- Per-unit record: nvdata `/APCFG/APRDEB/BT_Addr`, 112 bytes. First 6
  bytes = BD address; then voice (2), codec (4), radio (6), sleep (7),
  feature (2), TX power offset (3), same order as the vendor
  `btradio_conf_data`. The radio and sleep bytes differ from the MT6630
  (Gemini) defaults.

## Mainline port

- Driver: `hci_stp` (MediaTek's own BlueZ driver from the 3.18 vendor tree,
  as ported to 6.6 for the Gemini), at
  `kernel/delta/drivers/misc/mediatek-connectivity/bt/`. It uses the same
  WMT/STP calls as `stp_chrdev_bt` and the STP core's "bluez mode" RX hook
  (`mtk_wcn_stp_set_bluez`, `mtk_wcn_stp_register_if_rx`), which the
  Cosmo WMT delta already has.
- Left out from the Gemini port: the MT6630 vendor init script (wrong chip),
  the BTIF WAK keep-awake pulse (the Gemini had no chip-to-host wake IRQ),
  and the `HCI_QUIRK_EXT_INIT_BEST_EFFORT` net/bluetooth change (MT6630
  refused some LE init commands; to be tested on the MT6631 first).
- LE commands: the MT6631 firmware lists no LE commands in Read Local
  Supported Commands (octets 25-28 = 0) although LE works, so mainline
  enabled no LE advertising reports. `hci_stp` fills in the 4.0 core LE set
  in that reply (`le_cmds=1`, default). Stock Android sends a fixed LE event
  mask and does not need it.
- BLE HID (HOGP) needs `uhid` (/dev/uhid); BR/EDR HID needs `hidp`.
- Added: `hdev->set_bdaddr` with vendor command 0xfc1a (as mainline
  `btmtk_set_bdaddr`), so the nvdata address can be set from userspace.
- PSM: the Cosmo WMT runs stock STP PSM (`bPsmSupport`). On the Gemini, PSM
  was linked to A2DP stutter because the BGF wake EINT was not wired. On the
  Cosmo it is: `/proc/interrupts` shows `MT_SYSIRQ 289 Level
  BTIF_WAKEUP_IRQ` with 29 counts (2026-11-04, Wi-Fi only). PSM stays on;
  the fallback test is `echo "0 0" > /proc/driver/wmt_dbg`.
- Userspace (gemini-debian, already installed): bluez 5.82, PipeWire 1.4.2,
  WirePlumber 0.5.8, libspa-0.2-bluetooth.

## Steps

- B1 (PASS 2026-11-04): modules only, `modprobe hci_stp` by hand. hci0 up,
  BR/EDR and LE scan, BLE mouse paired and working.
- B2 (PASS): BD address from nvdata (B2a), loaded at boot (B2b), A2DP
  audio through PipeWire (B2c, 2026-11-09).
- B3 (PASS 2026-11-09): paired devices reconnect by themselves after
  sleep.
