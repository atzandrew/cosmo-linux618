#!/bin/sh
# Touch T1 sleep check. Usage: touch1.sh <sleep_s>
# As the 85 mA runs: Wi-Fi off, fb0 blanked, USB controller unbound.
# Waits for the USB cable to be pulled, then logs the average current
# over the sleep from the coulomb counter, and the touch IRQ count before
# and after. After wake it counts touch IRQs for 30 s: tap the screen
# then (touch working after wake = count rises). keep_wdt 0 for sleeps over 20 s (systemd's
# 30 s timeout would reset them); the in-sleep escape is then the SPM
# PCM watchdog and the power key.
S=${1:?sleep seconds}
L=/var/tmp/touch1-$S.log; P=/sys/module/cosmo_spm_suspend/parameters
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
touch_irq() {
	echo "$1: $(grep -E 'nt36672a|0-0001' /proc/interrupts | tr -s ' ')"
}
echo "== $(date -Is) sleep $S s"
touch_irq before
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
touch_irq after
dmesg | grep -i -E "cosmo-spm-suspend|sleep-guard|nvt|novatek|i2c|PM: |error|fail" | head -60
restore
echo "tap the screen now (30 s)"; sync
sleep 30
touch_irq "30 s after wake"
dmesg | grep -i -E "nvt|novatek|i2c" | tail -20
echo "== done $(date -Is)"; sync
