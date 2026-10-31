#!/bin/sh
# Awake idle current A/B/A: A = core-off idle on, B = state1 disabled on all CPUs.
# Waits for USB unplug, Wi-Fi off during the run, restores everything at the end.
L=/var/tmp/idle-ab.log
exec >"$L" 2>&1
B=/sys/class/power_supply/battery
CH=/sys/class/power_supply/mt6370-charger
SEG=180
setidle() { for c in 0 1 2 3 4 5 6 7; do echo $1 > /sys/devices/system/cpu/cpu$c/cpuidle/state1/disable; done; }
restore() { setidle 0; echo $BL0 > $BLD/brightness; systemctl restart cosmo-wifi; echo "restored $(date -Is)"; sync; }
trap 'restore; exit 1' INT TERM
echo "== $(date -Is) waiting for USB unplug"
i=0
while [ "$(cat $CH/online)" = 1 ]; do
	i=$((i+1)); [ $i -gt 900 ] && { echo "no unplug in 15 min, abort"; exit 1; }
	sleep 1
done
echo "unplugged $(date -Is) cap $(cat $B/capacity) V $(cat $B/voltage_now) T $(cat $B/temp) fb0 $(cat /sys/class/graphics/fb0/blank)"
BLD=/sys/class/backlight/mt6370-backlight; BL0=$(cat $BLD/brightness); echo 0 > $BLD/brightness; echo "backlight $BL0 -> $(cat $BLD/actual_brightness)"
echo 0 > /dev/wmtWifi
sleep 60
seg() { # name disable
	setidle $2; sleep 5
	u0=$(cat /sys/devices/system/cpu/cpu4/cpuidle/state1/usage)
	t0=$(cat /sys/class/rtc/rtc0/since_epoch); q0=$(cat $B/charge_counter)
	s=0; n=0
	end=$((t0+SEG))
	while [ "$(cat /sys/class/rtc/rtc0/since_epoch)" -lt $end ]; do
		s=$((s+$(cat $B/current_now))); n=$((n+1)); sleep 5
	done
	t1=$(cat /sys/class/rtc/rtc0/since_epoch); q1=$(cat $B/charge_counter)
	u1=$(cat /sys/devices/system/cpu/cpu4/cpuidle/state1/usage)
	dq=$((q1-q0)); dt=$((t1-t0))
	echo "$1 dis=$2 dt ${dt}s dq ${dq}uAh CAR $((dq*3600/dt/1000))mA mean_current_now $((s/n/1000))mA n $n cpu4_coreoff_usage +$((u1-u0)) V $(cat $B/voltage_now) T $(cat $B/temp) usb $(cat $CH/online)"
	sync
}
seg A1 0
seg B1 1
seg A2 0
restore
echo "== end $(date -Is)"; sync
