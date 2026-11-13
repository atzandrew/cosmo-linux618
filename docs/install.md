# Installing Debian on the Cosmo Communicator (beta)

This replaces Android (or Gemian) on the Cosmo with Debian 13 and KDE
Plasma, running a mainline Linux 6.18 kernel.

**This is a beta.** Read the whole page once before you start.

## Before you start

**Check these first. If any of them is "no", stop here.**

- [ ] **Your Cosmo has the NT36672 screen.** Cosmos were made with two
      different screens. Only the NT36672 one works with this beta. See
      [Check your screen](#2-check-your-screen).
- [ ] **You have copied off everything you want to keep.** Installing
      erases the data partition: apps, photos, messages, files.
- [ ] **You are fine without the phone part.** No calls, no SMS, no
      mobile data: the modem and SIM card slot do not work yet. Cameras
      and GPS do not work either (see the README for the full list).
- [ ] **You have a Linux PC**, a USB-C cable, and about **20 GB free** on
      the PC (backup plus images).
- [ ] **The Cosmo is charged to at least 50 %.**

From the [releases page](https://github.com/atzandrew/cosmo-linux618/releases/latest),
download:

- `cosmo-boot-beta1.img`
- `cosmo-dtbo-beta1.img`
- `cosmo-debian-beta1.img.zst`
- `SHA256SUMS`

## 1. Install mtkclient (on the PC)

[mtkclient](https://github.com/bkerler/mtkclient) talks to the Cosmo over
USB. Follow its README to install it, including its udev rules, so `mtk`
works without root.

**How every `mtk` command works:**

1. Switch the Cosmo **off** and **unplug** it.
2. Start the `mtk` command on the PC. It waits for the device.
3. Plug the Cosmo in. It connects within a few seconds.

If it does not connect: unplug, hold the **power key for about 10 s**,
and try again.

Always give mtk **full paths** (`/home/you/cosmo/boot.img`, not
`~/cosmo/boot.img`).

## 2. Check your screen

Only the **NT36672** screen is supported. On a unit with the **SSD2092**
screen, Debian is meant to refuse to start the screen and switch the
Cosmo off.

> **This safety check has never been tested on an SSD2092 unit**: we do
> not have one. Do not rely on it. Check your screen below, and do not
> install if it is not the NT36672.

The Cosmo's
bootloader writes the screen's name into its own log, on the `expdb`
partition. Reading it changes nothing on the Cosmo, and works whether it
runs Android or Gemian:

```sh
mtk r expdb /home/you/cosmo-expdb.bin
grep -a -o -i 'nt36672\|ssd2092' /home/you/cosmo-expdb.bin | sort -u
```

| It prints | Meaning |
|---|---|
| `nt36672` | Supported. Carry on. |
| `ssd2092` | **Not supported. Stop here.** |
| nothing, or both | Unknown. **Stop**, and ask on the issue tracker. |

Running Gemian? This shows the same from a terminal on the Cosmo:

```sh
grep -a -o -i 'nt36672\|ssd2092' /proc/device-tree/chosen/atag,videolfb
```

## 3. Back up (do not skip)

The backup is your way back to Android. It also holds this unit's own
data that cannot be downloaded anywhere: IMEI, Wi-Fi and Bluetooth
addresses, and radio and sensor calibration.

```sh
mkdir -p ~/cosmo-backup && cd ~/cosmo-backup
mtk rl "$PWD" --skip userdata
```

This reads every partition except the data partition. It takes a while.

Check that the important files are there:

```sh
ls -l nvdata.bin nvram.bin proinfo.bin protect1.bin boot.bin dtbo.bin
```

All six must exist. **Copy the whole `cosmo-backup` folder to a second
place** (another disk or computer). Never share it: it holds your IMEI.

## 4. Check the downloads

```sh
cd ~/Downloads
sha256sum -c SHA256SUMS            # every line must say OK
zstd -d cosmo-debian-beta1.img.zst # makes cosmo-debian-beta1.img
```

## 5. Unlock the bootloader (once)

The Cosmo only starts a system that is not Planet's after the bootloader
is unlocked.

```sh
mtk da seccfg unlock
```

From now on the Cosmo shows an **"orange state"** warning for a few
seconds at every start. That is normal.

## 6. Install

This writes the three images and erases the data partition. Change
`/home/you/Downloads` to where your files are:

```sh
mtk w boot,dtbo,userdata /home/you/Downloads/cosmo-boot-beta1.img,/home/you/Downloads/cosmo-dtbo-beta1.img,/home/you/Downloads/cosmo-debian-beta1.img
```

Writing takes several minutes. Wait until mtk reports that it wrote
`userdata`. **If it stops part way, run the same command again.**

Then unplug the Cosmo.

## 7. First start

1. Hold the **power key** for about 3 seconds.
2. The orange-state warning appears, then boot messages. The first start
   takes a little longer: Debian grows to fill the whole data partition.
3. **Setup opens by itself.** Choose your language, user name and
   password. When it finishes, the Cosmo restarts.
4. Log in on the login screen.
5. Connect to Wi-Fi from the network icon in the panel.

**Super key:** the Cosmo has none; **Fn+Alt** works as Super (Meta), e.g.
to open the app launcher.

**UK keyboard?** The keyboard is set up for the US Cosmo layout, with
the Fn keys working. For the UK layout, run this in Konsole once and
restart:

```sh
sudo sed -i 's/^XKBVARIANT=.*/XKBVARIANT="gb"/' /etc/default/keyboard
```

Choosing another layout in System Settings turns the Fn keys (volume,
brightness, symbols) off.

Next: [beta testing](beta-testing.md).

## Power key and escapes

| Do this | What happens |
|---|---|
| Press the power key or Esc | Wakes the Cosmo from sleep |
| Hold the power key 3 s | Switches off |
| Hold the power key about 10 s | Forced restart, if the Cosmo is stuck |

## Rescue mode: open root shell over USB

If a start does not finish (Debian never reports that it came up), the
next start stays in a small rescue system instead of Debian; the start
after that tries Debian again. In rescue mode the Cosmo shows boot
messages on the screen and offers a **root shell with no password** over
its USB-C port (`telnet 10.15.19.83` from the PC it is plugged into).

- It is reachable only over the USB cable, not over Wi-Fi or Bluetooth.
- Anyone who plugs into the Cosmo while it is in rescue mode gets full
  access, including to your files on the data partition.
- Do not leave the Cosmo plugged into a computer you do not trust while
  it shows rescue messages. Hold the power key about 10 s to restart it.

## Going back to Android

You need the backup from step 3.

> These steps have not been tested on a Cosmo yet. If they fail, Planet's
> stock firmware (last paragraph) is the way back.

1. Write the original boot images back:

   ```sh
   mtk w boot,dtbo /home/you/cosmo-backup/boot.bin,/home/you/cosmo-backup/dtbo.bin
   ```

2. Erase the Debian data partition:

   ```sh
   mtk e userdata
   ```

3. Start the Cosmo. Android sets up its data partition again like a new
   phone (a factory reset).
4. Optional: lock the bootloader again (`mtk da seccfg lock`). **Only do
   this after Android starts.** A locked bootloader refuses to start
   anything that is not Planet's.

If Android does not start, flash Planet's stock firmware for the Cosmo
with Planet's own tools and instructions.

Your backup's other files (`nvdata`, `nvram`, `protect1`, ...) are not
changed by this beta. Keep them anyway: they are the only copy of this
unit's calibration.
