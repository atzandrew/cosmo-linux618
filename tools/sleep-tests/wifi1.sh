#!/bin/sh
# Sleep with Wi-Fi on. Usage: wifi1.sh <sleep_s> [meas]
# Wi-Fi stays powered and associated through the sleep (no /dev/wmtWifi off,
# no cosmo-wifi restart), fb0 blanked as in the 85 mA runs, USB controller
# unbound for the sleep.
# meas: wait for the USB cable to be pulled first, then log the average
#       current over the sleep from the coulomb counter (as meas.sh).
# keep_wdt: 1 for sleeps up to 20 s (TOPRGU escape), 0 for longer ones
# (systemd's 30 s timeout would reset them).
# After resume: Wi-Fi link and a gateway ping over wlan0, without any
# restart. Only if that fails is cosmo-wifi restarted, to restore Wi-Fi.
S=${1:?sleep seconds}; M=${2:-}
L=/var/tmp/wifi1-$S$M.log; P=/sys/module/cosmo_spm_suspend/parameters
B=/sys/class/power_supply/battery; C=/sys/class/power_supply/mt6370-charger/online
exec >"$L" 2>&1
# Always leave the board usable: USB bound, sleep params back, screen on.
done_=0
restore() {
	[ $done_ = 1 ] && return; done_=1
	echo 11201000.usb > /sys/bus/platform/drivers/mtu3/bind 2>/dev/null
	echo 10 > $P/wake_s; echo 0 > $P/keep_wdt
	echo 0 > /sys/class/graphics/fb0/blank
	echo "restored; == end $(date -Is)"; sync
}
trap restore EXIT INT TERM
gw=$(ip route show default dev wlan0 | awk '{print $3; exit}')
link() {
	echo "$1: $(nmcli -t -f GENERAL.STATE,GENERAL.CONNECTION dev show wlan0 | tr '\n' ' ')"
	tail -1 /proc/net/wireless
	ping -I wlan0 -c 3 -W 2 "$gw" | tail -2
}
echo "== $(date -Is) sleep $S s $M; gw $gw"
link before
if [ "$M" = meas ]; then
	echo "waiting for USB unplug"; sync
	w=0; while [ "$(cat $C)" = 1 ]; do sleep 2; w=$((w + 2)); [ $w -ge 600 ] && { echo "no unplug, abort"; exit 1; }; done
	sleep 10
fi
echo 4 > /sys/class/graphics/fb0/blank; sleep 2
if [ "$S" -le 20 ]; then echo 1 > $P/keep_wdt; else echo 0 > $P/keep_wdt; fi
echo "$S" > $P/wake_s; echo 1 > $P/enable || { echo "enable refused"; exit 1; }
dmesg -C; sync
echo 11201000.usb > /sys/bus/platform/drivers/mtu3/unbind
t0=$(date +%s); c0=$(cat $B/charge_counter)
echo mem > /sys/power/state; r=$?
t1=$(date +%s); c1=$(cat $B/charge_counter)
dt=$((t1 - t0)); [ $dt -gt 0 ] && avg=$(( (c1 - c0) * 3600 / dt )) || avg=n/a
echo "state exit=$r dt $dt s, CAR d $((c1 - c0)) uAh, avg $avg uA, V $(cat $B/voltage_now)"
echo 11201000.usb > /sys/bus/platform/drivers/mtu3/bind
sleep 5
link "resume+5s"
dmesg | grep -i -E "cosmo-spm-suspend|sleep-guard|wlan|wmt|consys|btif|PM: |error|fail" | head -60
if ping -I wlan0 -c 2 -W 2 "$gw" >/dev/null; then echo "wifi after resume: OK, no restart"
else echo "wifi after resume: FAIL, restarting cosmo-wifi"; systemctl restart cosmo-wifi; sleep 15; link "after restart"; fi
restore
