#!/bin/sh
# Cosmo Communicator: apply the Cosmo keyboard setup to GNOME.
# Run once from this folder with: sudo sh cosmo-gnome-keys.sh [LAYOUT]
# LAYOUT: us (default), gb, de, ch-de or at. Then log out and back in.
# See README.md.
set -eu

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo."; exit 1; }

VARIANT=${1:-us}
case "$VARIANT" in
    us|gb)       SHOT='<Mod5>r' ;;
    # German-language layouts: Print is on Fn+Z, Fn+R types a bar.
    de|ch-de|at) SHOT='<Mod5>z' ;;
    *) echo "Unknown layout '$VARIANT'. Use one of: us gb de ch-de at."; exit 1 ;;
esac

HERE=$(cd "$(dirname "$0")" && pwd)
[ -f "$HERE/evdev.xml" ] || { echo "evdev.xml not found next to this script."; exit 1; }

# dconf-cli is not part of a minimal GNOME install (--no-install-recommends).
if ! command -v dconf >/dev/null 2>&1; then
    echo "Installing dconf-cli..."
    apt-get install -y dconf-cli
fi

REV=239d23aefebc3ccae75722f33fb9335adb8bac27
SHA=581f6af947cad9d2520395f845cd361bea05345e79a7366ed18a4154b8867747
URL=https://raw.githubusercontent.com/atzandrew/gemini-debian/$REV/overlay-cosmo/usr/share/X11/xkb/symbols/cosmo
XKB=/usr/share/X11/xkb/symbols/cosmo

# 1. Cosmo xkb layout (Fn layer, Fn+Alt = Super). Cosmo images up to Beta1.2
#    carry a copy with the English variants only, so install this version
#    unless it is already there.
if ! echo "$SHA  $XKB" | sha256sum -c --status 2>/dev/null; then
    echo "Downloading Cosmo keyboard layout..."
    tmp=$(mktemp)
    curl -fsSL "$URL" -o "$tmp"
    echo "$SHA  $tmp" | sha256sum -c --status || { rm -f "$tmp"; echo "Download does not match the expected file."; exit 1; }
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

cat > /etc/dconf/db/local.d/10-cosmo <<EOF
[org/gnome/desktop/input-sources]
sources=[('xkb', 'cosmo+$VARIANT')]

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
show-screenshot-ui=['Print', '$SHOT']
EOF
cp /etc/dconf/db/local.d/10-cosmo /etc/dconf/db/gdm.d/10-cosmo
dconf update

# 4. System keyboard layout (Plasma, console, and what GNOME copies into a
#    user's settings at their first GNOME login).
if [ -f /etc/default/keyboard ]; then
    sed -i -e 's/^XKBLAYOUT=.*/XKBLAYOUT="cosmo"/' \
           -e "s/^XKBVARIANT=.*/XKBVARIANT=\"$VARIANT\"/" /etc/default/keyboard
fi

# 5. A user who has logged in to GNOME before keeps the layout GNOME copied
#    then, which overrides the default above. Clear it for the user running
#    this script.
RESET_HINT=
if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
    uid=$(id -u "$SUDO_USER")
    if [ -S "/run/user/$uid/bus" ] && command -v gsettings >/dev/null 2>&1; then
        runuser -u "$SUDO_USER" -- env DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$uid/bus" \
            gsettings reset org.gnome.desktop.input-sources sources || RESET_HINT=1
    else
        RESET_HINT=1
    fi
else
    RESET_HINT=1
fi

# 6. GPU setting the Plasma session sets (needed by any desktop on Cosmo).
mkdir -p /etc/environment.d
echo 'PAN_MESA_DEBUG=noafbc' > /etc/environment.d/50-cosmo.conf

echo "Done (layout: cosmo $VARIANT). Log out and back in."
if [ -n "$RESET_HINT" ]; then
    echo "If you have used GNOME before, also run this as yourself (not sudo):"
    echo "    gsettings reset org.gnome.desktop.input-sources sources"
fi
echo "If the layout does not change, remove other layouts in Settings > Keyboard"
echo "(a layout or shortcut you set yourself takes priority over these defaults)."
