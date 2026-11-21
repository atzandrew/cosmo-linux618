# frame-pacing

Tools for measuring Plasma frame pacing, input latency and per-frame CPU
cost on the Cosmo. They need a kernel with event tracing
(`CONFIG_FTRACE=y`, `CONFIG_ENABLE_DEFAULT_TRACERS=y`; the function tracer
is not needed), plus `trace-cmd`, `sysstat` and, for the input feeders,
`python3-evdev`.

## Recording

    sudo ./cosmo-frametrace NAME 'WORKLOAD'

Records DRM vblank/flip, GPU scheduler, CPU and GPU frequency, thermal and
`sched_switch` events while WORKLOAD runs in the desktop session. Output
goes to `/var/tmp/ft/NAME.txt` (trace-cmd report), `NAME.pid` (pidstat of
kwin_wayland, plasmashell and Xwayland) and `NAME.gpufreq` (devfreq
trans_stat). Set `FT_ROOT=1` when the workload needs root, for example the
uinput feeders below.

## Input feeders (uinput, run as root)

- `ftmouse.py SECONDS STEP [nodrag]`: pointer motion every 8 ms (125 Hz),
  with Meta+left-button held (a window drag) unless `nodrag`. Logs event
  times to `/var/tmp/ft/events.txt`.
- `ftwheel.py SECONDS NOTCH_MS`: wheel down, then up.

## Analysis

- `ftanalyze.py NAME.txt`: vblank rate, flip sequence-gap histogram, GPU job
  run and queue times per client, GPU and CPU frequency changes.
- `fttimeline.py NAME.txt`: one line per presented frame (gap, GPU MHz,
  GPU busy time); marks late frames.
- `ftcpu.py NAME.txt`: CPU time per presented frame for the busiest threads.
- `ftlat.py NAME.txt events.txt`: input event to flip latency (lower bound,
  and to the first frame that can contain the event).

The display raises two vblank interrupts per 60 Hz frame (the OVL interrupt
enable is left on by LK), so a DRM sequence gap of 2 between flips means the
frame was on time.
