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

**Beta.** Most everyday functions work on the test unit. Beta images
are on the [releases page](https://github.com/atzandrew/cosmo-linux618/releases):
read [docs/install.md](docs/install.md) before flashing, and see
[docs/beta-testing.md](docs/beta-testing.md) for what to test and how to
report problems.

**Working**
- Boot, display, GPU (panfrost, Mesa GLES 3.1), Plasma Wayland, SDDM login
- Touchscreen, keyboard and keyboard backlight, power key, lid
- Wi-Fi, Bluetooth (pairing, mice and keyboards)
- Speaker and headphones
- Charging, including USB-C PD fast charging; battery level
- Deep sleep at stock power draw (~85 mA): closing the lid sleeps,
  opening it, Esc or the power key wake
- USB device mode on the left port; host mode on both ports (automatic on
  the right, selected by hand on the left)
- microSD
- Cover display shows time and battery
- CPU frequency scaling, CPU idle power-down, thermal limits, RTC

**Known issues**
- After sleep, the right USB-C port may stop working until a restart.
- USB power output on the left port stops after the first sleep.
- Wi-Fi is switched off during sleep and back on at wake.
- Bluetooth audio (headphones, speakers) is not tested yet.
- No headset microphone, and plugging headphones in is not detected.
- The bootloader shows its orange-state warning at every boot.

**Not supported yet**
- Modem and SIM, GPS
- Cameras
- Screen rotation and other sensors
- HDMI out, vibration, NFC, fingerprint reader

**Screen variants: read before flashing.** Cosmos were made with two
different screens. Only the **NT36672** screen is supported; units with
the **SSD2092** screen (and its Solomon touch controller) are not. The
install guide shows how to check which one a unit has. The kernel refuses
to drive any other screen and powers the device off, but **that refusal
has never been tested on an SSD2092 unit**, as none was available: do not
rely on it.

Tested on a single unit. The CPU voltage tables have not been checked on
other units. Do not flash this on a device you cannot recover with mtk.

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

- `docs/install.md`: installing the beta, backup and going back to Android
- `docs/beta-testing.md`: what to test, known issues, reporting problems
- `docs/bringup-checklist.md`: every hardware block and its state
- `docs/hardware-map.md`, `docs/stock-reference.md`: hardware and stock values
- `docs/session-log.md`: bring-up history and test results
