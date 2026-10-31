#!/bin/sh
# Enable core-off on one CPU after another; each must keep waking.
L=/var/tmp/i1c-rest.log
exec > $L 2>&1
run_on() { timeout 5 taskset -c $1 sh -c 'head -c 4M /dev/zero | md5sum' > /dev/null && echo "job on cpu$1 ok" || echo "job on cpu$1 FAILED"; }
run_on 7
for c in 6 5 4 3 2 1 0; do
	D=/sys/devices/system/cpu/cpu$c/cpuidle
	echo "$(date +%T) enable cpu$c"; sync
	echo 0 > $D/state1/disable
	u0=$(cat $D/state1/usage)
	sleep 20
	u1=$(cat $D/state1/usage)
	echo "$(date +%T) cpu$c off usage $u0 -> $u1 time $(cat $D/state1/time)"
	run_on $c; sync
done
for c in 0 1 2 3 4 5 6 7; do run_on $c; done
echo done; sync
