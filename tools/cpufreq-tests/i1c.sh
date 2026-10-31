#!/bin/sh
# I1c: stock MCDI init values, then core-off on one CPU. Usage: i1c.sh CPU
L=/var/tmp/i1c-cpu$1.log
exec > $L 2>&1
D=/sys/devices/system/cpu/cpu$1/cpuidle
insmod /home/atzero/cosmo_mcdi.ko write=1 2>/dev/null
dmesg | grep cosmo_mcdi | tail -3 | sed 's/^.*cosmo_mcdi: //'
sync
echo "$(date +%T) enable cpu$1 state1"
echo 0 > $D/state1/disable
for i in $(seq 1 12); do
	sleep 5
	echo "$(date +%T) cpu$1 off usage $(cat $D/state1/usage) time $(cat $D/state1/time) wfi $(cat $D/state0/usage)"
	sync
done
echo done
