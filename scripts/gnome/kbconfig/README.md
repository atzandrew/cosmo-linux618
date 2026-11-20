# Cosmo keyboard setup for GNOME

The Cosmo images ship with Plasma. If you install GNOME, run this once to
get the Cosmo keyboard working there:

```
cd scripts/gnome/kbconfig
sudo sh cosmo-gnome-keys.sh
```

Then log out and back in.

For a different keyboard, add its layout name:

| Layout | Command |
|---|---|
| English (US), default | `sudo sh cosmo-gnome-keys.sh` |
| English (UK) | `sudo sh cosmo-gnome-keys.sh gb` |
| German | `sudo sh cosmo-gnome-keys.sh de` |
| Swiss German | `sudo sh cosmo-gnome-keys.sh ch-de` |
| Austrian | `sudo sh cosmo-gnome-keys.sh at` |

Running it again with another layout name switches to that layout.

## What it does

- Installs the Cosmo keyboard layout file
  (`/usr/share/X11/xkb/symbols/cosmo`) unless this version is already there. Cosmo
  images up to Beta1.2 carry the English layouts only.
- Registers the `cosmo` keyboard layout in `/etc/xkb/rules/evdev.xml`.
  GNOME ignores layouts that are not in the xkb registry and falls back to
  US, where Fn acts as Super. `/etc/xkb` is read alongside the xkb-data copy
  and is not touched by package updates.
- Sets GNOME defaults (system dconf database `local`):
  - input source: the chosen Cosmo layout
  - auto-rotate off
  - Fn-layer shortcuts
- Sets the system keyboard layout in `/etc/default/keyboard` to the same
  Cosmo layout, so Plasma and the text console match, and GNOME copies it at
  a user's first login.
- Clears the GNOME layout of the user running the script. GNOME stores the
  layout it found at that user's first login, and that would override the
  new default. If the script cannot do this, it prints the command to run.
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
| Fn+R (German layouts: Fn+Z) | screenshot |

The German layouts follow the Gemian Cosmo layout: Fn+A/O/U give ä/ö/ü,
Fn+S gives ß, Fn+R types `|` and Print is on Fn+Z.

Layouts or shortcuts you change yourself in GNOME Settings take priority
over these defaults.

## Undo

To go back to English (US), run `sudo sh cosmo-gnome-keys.sh`. To remove
the GNOME settings:

```
sudo rm /etc/xkb/rules/evdev.xml /etc/dconf/db/local.d/10-cosmo /etc/dconf/db/gdm.d/10-cosmo
sudo dconf update
```
