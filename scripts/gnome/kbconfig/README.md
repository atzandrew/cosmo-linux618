# Cosmo keyboard setup for GNOME

The Cosmo images ship with Plasma. If you install GNOME, run this once to
get the Cosmo keyboard working there:

```
cd scripts/gnome/kbconfig
sudo sh cosmo-gnome-keys.sh
```

Then log out and back in.

## What it does

- Registers the `cosmo` keyboard layout in `/etc/xkb/rules/evdev.xml`.
  GNOME ignores layouts that are not in the xkb registry and falls back to
  US, where Fn acts as Super. `/etc/xkb` is read alongside the xkb-data copy
  and is not touched by package updates.
- Sets GNOME defaults (system dconf database `local`):
  - input source: Cosmo English (US)
  - auto-rotate off
  - Fn-layer shortcuts
- Sets `PAN_MESA_DEBUG=noafbc`, as the Plasma session does.
- Installs `dconf-cli` if it is missing.

## Fn shortcuts

GNOME matches shortcuts by a key's base symbol, so the Fn layer is bound as
Fn (Mod5) + letter:

| Keys | Action |
|---|---|
| Fn+C / Fn+V | volume down / up |
| Fn+T | mute |
| Fn+B / Fn+N | screen brightness down / up |
| Fn+Q / Fn+W / Fn+E | play-pause / previous / next |
| Fn+R | screenshot |

Layouts or shortcuts you change yourself in GNOME Settings take priority
over these defaults.

## Undo

```
sudo rm /etc/xkb/rules/evdev.xml /etc/dconf/db/local.d/10-cosmo /etc/dconf/db/gdm.d/10-cosmo
sudo dconf update
```
