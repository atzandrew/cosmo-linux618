#!/bin/sh
# GPU G2 sleep check. Usage: gpu1.sh <sleep_s>
# As the 85 mA runs: Wi-Fi off, fb0 blanked, USB controller unbound.
# Waits for the USB cable to be pulled, then logs the average current
# over the sleep from the coulomb counter, and the GPU rails and MFG
# domains before and after. keep_wdt 0 for sleeps over 20 s (systemd's
# 30 s timeout would reset them); the in-sleep escape is then the SPM
# PCM watchdog and the power key.
S=${1:?sleep seconds}
L=/var/tmp/gpu1-$S.log; P=/sys/module/cosmo_spm_suspend/parameters
B=/sys/class/power_supply/battery; C=/sys/class/power_supply/mt6370-charger/online
exec >"$L" 2>&1
# Always leave the board usable: USB bound, sleep params back, screen
# on, Wi-Fi back.
done_=0
restore() {
	[ $done_ = 1 ] && return; done_=1
	echo 11201000.usb > /sys/bus/platform/drivers/mtu3/bind 2>/dev/null
	echo 10 > $P/wake_s; echo 0 > $P/keep_wdt
	echo 0 > /sys/class/graphics/fb0/blank
	systemctl restart cosmo-wifi
	echo "restored; == end $(date -Is)"; sync
}
trap restore EXIT INT TERM
gpu() {
	echo "$1:"
	for r in /sys/class/regulator/*; do
		n=$(cat "$r/name"); case "$n" in vgpu|vsram_gpu)
			echo "  $n $(cat "$r/microvolts") $(cat "$r/state")";; esac
	done
	grep -E "^mfg" /sys/kernel/debug/pm_genpd/pm_genpd_summary | sed 's/^/  /'
}
echo "== $(date -Is) sleep $S s"
gpu before
echo "waiting for USB unplug"; sync
w=0; while [ "$(cat $C)" = 1 ]; do sleep 2; w=$((w + 2)); [ $w -ge 600 ] && { echo "no unplug, abort"; exit 1; }; done
sleep 10
echo 0 > /dev/wmtWifi; sleep 2
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
gpu after
dmesg | grep -i -E "cosmo-spm-suspend|sleep-guard|panfrost|mfg|genpd|PM: |error|fail" | head -60
restore
