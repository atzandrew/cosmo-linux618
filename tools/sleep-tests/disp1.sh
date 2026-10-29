#!/bin/sh
# Display-on-wake test. Usage: disp1.sh on|blank. 10 s deep sleep, timer wake.
# on:    display left on; kernel path alone must bring it back.
# blank: fb0 blanked first, unblanked 2 s after resume (stock: userspace unblank).
L=/var/tmp/disp1-$1.log; P=/sys/module/cosmo_spm_suspend/parameters
BL=/sys/class/backlight/mt6370-backlight
exec >"$L" 2>&1
st() {
	echo "$1: bl $(cat $BL/brightness)/$(cat $BL/actual_brightness) power $(cat $BL/bl_power) fb0 $(cat /sys/class/graphics/fb0/blank)"
	for r in /sys/class/regulator/*; do
		n=$(cat "$r/name"); case "$n" in mt6370-dsv*) echo "  $n $(cat "$r/state")";; esac
	done
}
echo "== $(date -Is) mode $1"
st start
echo 0 > /dev/wmtWifi; sleep 2
[ "$1" = blank ] && { echo 4 > /sys/class/graphics/fb0/blank; sleep 2; st blanked; }
echo 10 > $P/wake_s; echo 0 > $P/keep_wdt; echo 1 > $P/enable || { echo "enable refused"; exit 1; }
dmesg -C
echo 11201000.usb > /sys/bus/platform/drivers/mtu3/unbind
echo mem > /sys/power/state; echo "state exit=$?"
st resumed
sleep 2; st "resume+2s"
[ "$1" = blank ] && { echo 0 > /sys/class/graphics/fb0/blank; echo "unblank exit=$?"; sleep 2; st unblanked; }
echo 11201000.usb > /sys/bus/platform/drivers/mtu3/bind
dmesg | grep -i -E "panel|dsi|drm|backlight|bias|sleep-guard|cosmo-spm-suspend: (CPU|wake)|error|fail" | head -40
systemctl restart cosmo-wifi
echo "== end $(date -Is)"; sync
