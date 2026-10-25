# P0 backup — device-unique partitions, before any flash

Scope: only what is unique to this unit and cannot be restored from the
Planet firmware image. Everything else (preloader, lk, boot, odmdtbo,
system, vendor, md1img, …) comes back by re-flashing the stock image with
mtkclient, and userdata holds nothing worth keeping on a fresh device.

These partitions hold the IMEI, radio and sensor calibration, MAC
addresses and the lock state. **Never commit them to a repo.**

Written 2026-10-24, not yet run. Tool: mtkclient V2.1.4 (`/usr/bin/mtk` on
the flashing host); syntax checked against `mtk <cmd> --help` of that
version, not yet against a Cosmo.

## What to keep

| Partition | Holds |
|---|---|
| `proinfo` | product info: serial, factory test flags |
| `nvram` | raw NVRAM backup: IMEI, RF calibration, Wi-Fi/BT MAC |
| `nvcfg` | NVRAM config, some sensor calibration |
| `nvdata` | live NVRAM filesystem (IMEI, calibration) |
| `protect1`, `protect2` | modem protected data (SIM-lock, calibration copies) |
| `persist` | sensor/factory calibration (if present) |
| `seccfg` | bootloader lock state |
| GPT | layout reference (`printgpt` text + `gpt` dump) |

Names follow MediaTek's MT6771 Android layout and may differ on this unit:
check `printgpt.txt` first, keep any other partition that sounds
per-device (calibration, serial, keys), and drop names that don't exist.

## Steps

All paths absolute: mtkclient does not expand `~` (the shell expands
`$HOME`, which is fine).

```sh
B=$HOME/cosmo-backup/$(date +%Y%m%d)
mkdir -p "$B" && cd "$B"

# 0. Connect: start mtk first, then power-cycle the Cosmo while plugged in.
#    TODO(P0): record the Cosmo key combo and whether it lands in BROM or
#    preloader mode.

# 1. Partition table (also a connection test).
mtk printgpt | tee "$B/printgpt.txt"
mtk gpt "$B/gpt"

# 2. The device-unique partitions (adjust the list to printgpt.txt).
P="proinfo nvram nvcfg nvdata protect1 protect2 persist seccfg"
mtk r "$(echo $P | tr ' ' ,)" "$(for p in $P; do printf '%s,' "$B/$p.bin"; done | sed 's/,$//')"

# 3. Checksums.
( cd "$B" && sha256sum *.bin ) > "$B/SHA256SUMS"
```

## Done when

- [ ] `printgpt.txt`, `gpt/` and one `.bin` per kept partition, sizes
      matching the GPT; no mtk errors.
- [ ] `nvdata.bin` and `protect1.bin` are not all zeros or all `0xff`
      (`od -An -tx1 nvdata.bin | sort -u | head`).
- [ ] `SHA256SUMS` written; the set copied to a second machine and checked
      there with `sha256sum -c SHA256SUMS`.
- [ ] Logged in `docs/session-log.md`: date, partition list with sizes,
      sha256 of `SHA256SUMS`, where the two copies live.

## Restore (only if needed)

Re-flash the stock Planet image, then write the saved partitions back by
name, e.g. `mtk w nvdata <abs path>/nvdata.bin`. Writes are ask-first.
