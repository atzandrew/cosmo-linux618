# ftwheel.py SECONDS NOTCH_MS : wheel down for SECONDS, then up for SECONDS.
import sys,time
from evdev import UInput, ecodes as e
secs=float(sys.argv[1]); per=float(sys.argv[2])/1000
cap={e.EV_KEY:[e.BTN_LEFT,e.BTN_RIGHT,e.BTN_MIDDLE],e.EV_REL:[e.REL_X,e.REL_Y,e.REL_WHEEL,e.REL_WHEEL_HI_RES]}
ui=UInput(cap,name='ft-wheel'); time.sleep(1.0)
for d in (-1,1):
    t0=time.monotonic(); n=0
    while time.monotonic()-t0<secs:
        ui.write(e.EV_REL,e.REL_WHEEL,d); ui.write(e.EV_REL,e.REL_WHEEL_HI_RES,120*d); ui.syn(); n+=1
        time.sleep(max(0,t0+n*per-time.monotonic()))
time.sleep(0.5); ui.close()
