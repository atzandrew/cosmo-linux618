#!/bin/sh
# CPU DVFS step C1 check (docs/cpufreq.md). Run as root on the Cosmo.
#   c1-test.sh steps         every OPP of both policies, PLL readback
#   c1-test.sh stress SECS   all 8 CPUs at max OPP, md5 check, temps
#   c1-test.sh volts         Vproc/Vsram now
# Needs /home/atzero/cosmo_cpuclk.ko (tools/cosmo-pwr, read-only).
set -u
KO=/home/atzero/cosmo_cpuclk.ko
CF=/sys/devices/system/cpu/cpufreq
fail=0

volts() {
	for r in /sys/class/regulator/regulator.*; do
		n=$(cat $r/name)
		case $n in vproc1[12]|vsram_proc1[12])
			echo "$n $(cat $r/microvolts)";;
		esac
	done
}

# kHz the hardware runs at for cluster LL / L / CCI (PLL + MCUCFG divider)
hw_khz() {
	insmod $KO 2>/dev/null
	dmesg | grep "cosmo_cpuclk: $1 " | tail -1 | sed 's/.*-> \([0-9]*\) kHz/\1/'
}

check_volts() {
	volts | while read n uv; do
		case $n in vproc*) [ "$uv" = 800000 ] || echo "FAIL $n $uv";; esac
	done
}

steps() {
	for p in 0 4; do
		pol=$CF/policy$p
		[ $p = 0 ] && name="LL" || name="L"
		old=$(cat $pol/scaling_governor)
		echo userspace > $pol/scaling_governor || { echo "FAIL governor"; return 1; }
		for f in $(cat $pol/scaling_available_frequencies); do
			echo $f > $pol/scaling_setspeed
			sleep 0.3
			cur=$(cat $pol/cpuinfo_cur_freq)
			hw=$(hw_khz "$name")
			d=$(( hw > f ? hw - f : f - hw ))
			v=$(check_volts)
			if [ $d -le $((f / 1000)) ] && [ "$cur" = "$f" ] && [ -z "$v" ]; then
				echo "PASS policy$p $f kHz: cur $cur hw $hw"
			else
				echo "FAIL policy$p $f kHz: cur $cur hw $hw $v"; fail=1
			fi
		done
		echo $old > $pol/scaling_governor
	done
	echo "CCI hw $(hw_khz CCI) kHz (expect 598000, unchanged)"
	volts
	[ $fail = 0 ] && echo "steps PASS" || echo "steps FAIL"
}

stress() {
	secs=$1
	want=$(head -c 16M /dev/zero | md5sum | cut -d' ' -f1)
	for p in 0 4; do
		echo performance > $CF/policy$p/scaling_governor
	done
	echo "start $(date +%T): policy0 $(cat $CF/policy0/scaling_cur_freq) policy4 $(cat $CF/policy4/scaling_cur_freq)"
	end=$(( $(date +%s) + secs ))
	for c in 0 1 2 3 4 5 6 7; do
		taskset -c $c sh -c "n=0; while [ \$(date +%s) -lt $end ]; do s=\$(head -c 16M /dev/zero | md5sum | cut -d' ' -f1); [ \"\$s\" = $want ] || echo \"FAIL cpu$c md5 \$s\"; n=\$((n+1)); done; echo \"cpu$c runs \$n\"" &
	done
	while [ $(date +%s) -lt $end ]; do
		sleep 10
		echo "$(date +%T) temp $(cat /sys/class/thermal/thermal_zone0/temp) $(check_volts) hwLL $(hw_khz LL) hwL $(hw_khz L)"
	done
	wait
	for p in 0 4; do
		echo schedutil > $CF/policy$p/scaling_governor
	done
	volts
}

case ${1:-} in
steps) steps ;;
stress) stress ${2:-300} ;;
volts) volts ;;
*) echo "usage: $0 steps | stress SECS | volts"; exit 2 ;;
esac
