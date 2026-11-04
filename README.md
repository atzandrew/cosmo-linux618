# cosmo-linux618

Mainline Linux **6.18 LTS** for the Planet Computers **Cosmo Communicator**
(MediaTek MT6771 / Helio P70), replacing Gemian's vendor 4.4 kernel. The
root filesystem is a fresh Debian trixie install built by
[gemini-debian](https://github.com/atzandrew/gemini-debian) (`DEVICE=cosmo`),
with a Plasma Wayland desktop.

The approach follows the Gemini PDA projects
([gemini-nixos](https://github.com/atzandrew/gemini-nixos),
[gemini-debian](https://github.com/atzandrew/gemini-debian)): a pinned
upstream kernel plus a small tree of changed files. The Cosmo needs far less
than the Gemini did, because its MT6771 is register-compatible with the
MT8183 that mainline already supports well. Where the hardware needs
specific values, they are taken from the stock kernel and device tree.

## Status

Most everyday functions work on the test unit. No prebuilt images yet;
beta images are planned.

**Working**
- Boot, display, GPU (panfrost, Mesa GLES 3.1), Plasma Wayland, graphical login
- Touchscreen, keyboard and keyboard backlight, power key
- Wi-Fi
- Speaker and headphones
- Charging, including USB-C PD fast charging; battery level
- Deep sleep at stock power draw (~85 mA), wake on Esc or the power key
- USB device mode on the left port; host mode on both ports (automatic on
  the right, selected by hand on the left)
- Cover display shows time and battery
- CPU frequency scaling, CPU idle power-down, thermal limits, RTC

**Known issues**
- After sleep, the right USB-C port and USB power output on the left port
  stop working until a reboot.
- Wi-Fi is switched off during sleep.
- The lid does not sleep or wake the device yet.
- The bootloader shows its orange-state warning at every boot.

**Not supported yet**
- Modem and SIM, Bluetooth, GPS
- Cameras
- Screen rotation and other sensors
- microSD, HDMI out, vibration, NFC, fingerprint reader

**Hardware variants:** tested on a single unit with the NT36672 panel.
Units with the SSD2092 panel (and Solomon touch controller) are not
supported, and the CPU voltage tables have not yet been checked on other
units. Do not flash this on a device you cannot recover with mtk.

## Build

Needs gcc for arm64 (native on an arm64 host, otherwise the cross
`aarch64-linux-gnu-gcc`), plus `flex`, `bison`, `bc` and the OpenSSL and
libelf headers:

- Debian/Ubuntu: `flex bison bc libssl-dev libelf-dev`
- Fedora: `flex bison bc openssl-devel elfutils-libelf-devel`

```sh
bin/build-kernel.sh dtbs   # device tree only (fast check)
bin/build-kernel.sh        # Image.gz, DTB, modules → out/
```

The first run clones the pinned stable tag into `build/src`.

## Documentation

- `docs/bringup-checklist.md`: every hardware block and its state
- `docs/hardware-map.md`, `docs/stock-reference.md`: hardware and stock values
- `docs/session-log.md`: bring-up history and test results
