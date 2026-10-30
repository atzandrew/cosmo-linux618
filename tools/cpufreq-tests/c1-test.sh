#!/bin/sh
# CPU DVFS step check (docs/cpufreq.md, C1 onwards). Run as root on the Cosmo.
# When mtk-ccifreq is bound, each step also checks the CCI: the other
# cluster is held at 793 MHz, so the CCI must run at the OPP paired with
# the swept frequency (same index, required-opps).
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

DF=$(ls -d /sys/class/devfreq/* 2>/dev/null | head -1)
# CCI kHz paired with policy $1 at CPU kHz $2 (same OPP index: docs/cpufreq.md)
cci_for() {
	case $2 in
	793000) echo 273000; return;; 910000) echo 338000; return;;
	1014000) echo 403000; return;; 1131000) echo 481000; return;;
	1248000) echo 546000; return;; 1326000) echo 624000; return;;
	1417000) echo 689000; return;; 1508000) echo 767000; return;;
	1586000) echo 845000; return;;
	esac
	if [ $1 = 0 ]; then
		case $2 in
		1625000) echo 871000;; 1677000) echo 923000;; 1716000) echo 962000;;
		1781000) echo 1027000;; 1846000) echo 1092000;; 1924000) echo 1144000;;
		1989000) echo 1196000;;
		esac
	else
		case $2 in
		1651000) echo 871000;; 1716000) echo 923000;; 1781000) echo 962000;;
		1846000) echo 1027000;; 1924000) echo 1092000;; 1989000) echo 1144000;;
		2106000) echo 1196000;;
		esac
	fi
}

# Table voltages (uV) per kHz, docs/cpufreq.md "Our table"
v_little() {
	case $1 in
	793000) echo 600000;; 910000) echo 637500;; 1014000) echo 668750;;
	1131000) echo 706250;; 1248000) echo 737500;; 1326000) echo 768750;;
	1417000) echo 800000;; 1508000) echo 818750;; 1586000) echo 843750;;
	1625000) echo 856250;; 1677000) echo 881250;; 1716000) echo 893750;;
	1781000) echo 925000;; 1846000) echo 950000;; 1924000) echo 975000;;
	1989000) echo 1000000;;
	esac
}
v_big() {
	case $1 in
	793000) echo 706250;; 910000) echo 731250;; 1014000) echo 756250;;
	1131000) echo 781250;; 1248000) echo 812500;; 1326000) echo 831250;;
	1417000) echo 856250;; 1508000) echo 868750;; 1586000) echo 893750;;
	1651000) echo 906250;; 1716000) echo 925000;; 1781000) echo 950000;;
	1846000) echo 975000;; 1924000) echo 1000000;; 1989000) echo 1025000;;
	2106000) echo 1050000;;
	esac
}
v_cci() {
	case $1 in
	273000) echo 600000;; 338000) echo 637500;; 403000) echo 668750;;
	481000) echo 706250;; 546000) echo 737500;; 624000) echo 768750;;
	689000) echo 800000;; 767000) echo 818750;; 845000) echo 843750;;
	871000) echo 856250;; 923000) echo 881250;; 962000) echo 893750;;
	1027000) echo 925000;; 1092000) echo 950000;; 1144000) echo 975000;;
	1196000) echo 1000000;;
	esac
}
reg() { for r in /sys/class/regulator/regulator.*; do [ "$(cat $r/name)" = $1 ] && echo $r; done; }
clampc() { # value min max
	v=$1; [ $v -lt $2 ] && v=$2; [ $v -gt $3 ] && v=$3; echo $v
}
# Expected Vproc for a rail: table value clamped to the DTS constraints
# (a pinned rail has min = max).
expect() { # rail table-uV
	r=$(reg $1); clampc $2 $(cat $r/min_microvolts) $(cat $r/max_microvolts)
}

# Compare Vproc11/12 and Vsram11/12 with the current OPPs. Empty = OK.
check_volts() {
	f0=$(cat $CF/policy0/cpuinfo_cur_freq); f4=$(cat $CF/policy4/cpuinfo_cur_freq)
	t12=$(v_little $f0)
	if [ -n "$DF" ]; then
		tc=$(v_cci $(( $(cat $DF/cur_freq) / 1000 )))
		[ $tc -gt $t12 ] && t12=$tc
	fi
	e11=$(expect vproc11 $(v_big $f4)); e12=$(expect vproc12 $t12)
	g11=$(cat $(reg vproc11)/microvolts); g12=$(cat $(reg vproc12)/microvolts)
	s11=$(cat $(reg vsram_proc11)/microvolts); s12=$(cat $(reg vsram_proc12)/microvolts)
	[ $g11 = $e11 ] || echo "FAIL vproc11 $g11 want $e11"
	[ $g12 = $e12 ] || echo "FAIL vproc12 $g12 want $e12"
	w=$(clampc $((g11 + 100000)) 900000 1118750); [ $s11 = $w ] || echo "FAIL vsram_proc11 $s11 want $w"
	w=$(clampc $((g12 + 100000)) 900000 1118750); [ $s12 = $w ] || echo "FAIL vsram_proc12 $s12 want $w"
}

steps() {
	for p in 0 4; do
		echo userspace > $CF/policy$p/scaling_governor || { echo "FAIL governor"; return 1; }
		echo 793000 > $CF/policy$p/scaling_setspeed
	done
	for p in 0 4; do
		pol=$CF/policy$p
		[ $p = 0 ] && name="LL" || name="L"
		for f in $(cat $pol/scaling_available_frequencies); do
			echo $f > $pol/scaling_setspeed
			sleep 0.3
			cur=$(cat $pol/cpuinfo_cur_freq)
			hw=$(hw_khz "$name")
			d=$(( hw > f ? hw - f : f - hw ))
			v=$(check_volts)
			ok=1
			[ $d -le $((f / 1000)) ] && [ "$cur" = "$f" ] && [ -z "$v" ] || ok=0
			c=""
			if [ -n "$DF" ]; then
				want=$(cci_for $p $f); chw=$(hw_khz CCI)
				cdf=$(( $(cat $DF/cur_freq) / 1000 ))
				[ "$chw" = "$want" ] && [ "$cdf" = "$want" ] || ok=0
				c=" cci hw $chw devfreq $cdf want $want"
			fi
			if [ $ok = 1 ]; then
				echo "PASS policy$p $f kHz: cur $cur hw $hw$c v11 $(cat $(reg vproc11)/microvolts) v12 $(cat $(reg vproc12)/microvolts)"
			else
				echo "FAIL policy$p $f kHz: cur $cur hw $hw$c $v"; fail=1
			fi
		done
		echo 793000 > $pol/scaling_setspeed
	done
	for p in 0 4; do
		echo schedutil > $CF/policy$p/scaling_governor
	done
	echo "CCI hw $(hw_khz CCI) kHz${DF:+ devfreq $(cat $DF/cur_freq)}"
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
		echo "$(date +%T) temp $(cat /sys/class/thermal/thermal_zone0/temp) v11 $(cat $(reg vproc11)/microvolts) v12 $(cat $(reg vproc12)/microvolts) $(check_volts) hwLL $(hw_khz LL) hwL $(hw_khz L) hwCCI $(hw_khz CCI)"
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
