#!/bin/sh
# Cosmo Communicator: apply the Cosmo keyboard setup to GNOME.
# Run once from this folder with: sudo sh cosmo-gnome-keys.sh
# then log out and back in. See README.md.
set -eu

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo."; exit 1; }

HERE=$(cd "$(dirname "$0")" && pwd)
[ -f "$HERE/evdev.xml" ] || { echo "evdev.xml not found next to this script."; exit 1; }

# dconf-cli is not part of a minimal GNOME install (--no-install-recommends).
if ! command -v dconf >/dev/null 2>&1; then
    echo "Installing dconf-cli..."
    apt-get install -y dconf-cli
fi

REV=f7736f5163abb3d6103a50d3c5fcb8ff6ec7ccef
URL=https://raw.githubusercontent.com/atzandrew/gemini-debian/$REV/overlay-cosmo/usr/share/X11/xkb/symbols/cosmo
XKB=/usr/share/X11/xkb/symbols/cosmo

# 1. Cosmo xkb layout (Fn layer, Fn+Alt = Super). Already present on Cosmo images.
if [ ! -f "$XKB" ]; then
    echo "Downloading Cosmo keyboard layout..."
    tmp=$(mktemp)
    curl -fsSL "$URL" -o "$tmp"
    install -D -m 644 "$tmp" "$XKB"
    rm -f "$tmp"
fi

# 2. Register the layout with libxkbregistry. GNOME drops input sources that
#    are not in the registry and falls back to US (Fn then acts as Super).
#    /etc/xkb is read in addition to the xkb-data copy and survives updates.
install -D -m 644 "$HERE/evdev.xml" /etc/xkb/rules/evdev.xml

# 3. GNOME defaults: Cosmo layout, no auto-rotate, Fn-layer shortcuts.
#    GNOME matches shortcuts by the key's base symbol, so the Fn layer
#    (Mod5) is bound as <Mod5>+letter, matching the cosmo layout.
mkdir -p /etc/dconf/profile /etc/dconf/db/local.d /etc/dconf/db/gdm.d
grep -qs '^system-db:local' /etc/dconf/profile/user || \
    printf 'user-db:user\nsystem-db:local\n' > /etc/dconf/profile/user
grep -qs '^system-db:gdm' /etc/dconf/profile/gdm || \
    printf 'user-db:user\nsystem-db:gdm\nfile-db:/usr/share/gdm/greeter-dconf-defaults\n' > /etc/dconf/profile/gdm

cat > /etc/dconf/db/local.d/10-cosmo <<'EOF'
[org/gnome/desktop/input-sources]
sources=[('xkb', 'cosmo+us')]

[org/gnome/settings-daemon/peripherals/touchscreen]
orientation-lock=true

[org/gnome/settings-daemon/plugins/media-keys]
volume-down=['<Mod5>c']
volume-up=['<Mod5>v']
volume-mute=['<Mod5>t']
screen-brightness-down=['<Mod5>b']
screen-brightness-up=['<Mod5>n']
play=['<Mod5>q']
previous=['<Mod5>w']
next=['<Mod5>e']

[org/gnome/shell/keybindings]
show-screenshot-ui=['Print', '<Mod5>r']
EOF
cp /etc/dconf/db/local.d/10-cosmo /etc/dconf/db/gdm.d/10-cosmo
dconf update

# 4. GPU setting the Plasma session sets (needed by any desktop on Cosmo).
mkdir -p /etc/environment.d
echo 'PAN_MESA_DEBUG=noafbc' > /etc/environment.d/50-cosmo.conf

echo "Done. Log out and back in."
echo "If the layout does not change, remove other layouts in Settings > Keyboard"
echo "(a layout or shortcut you set yourself takes priority over these defaults)."
