#!/bin/sh
# Cosmo Communicator: apply the Cosmo keyboard setup to GNOME.
# Run once with: sudo sh cosmo-gnome-keys.sh   then log out and back in.
set -eu

[ "$(id -u)" -eq 0 ] || { echo "Run with sudo."; exit 1; }

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

# 2. GNOME defaults: Cosmo layout, no auto-rotate.
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
EOF
cp /etc/dconf/db/local.d/10-cosmo /etc/dconf/db/gdm.d/10-cosmo
dconf update

# 3. GPU setting the Plasma session sets (needed by any desktop on Cosmo).
mkdir -p /etc/environment.d
echo 'PAN_MESA_DEBUG=noafbc' > /etc/environment.d/50-cosmo.conf

echo "Done. Log out and back in."
echo "If the layout does not change, remove other layouts in Settings > Keyboard"
echo "(a layout you picked yourself takes priority over these defaults)."
