# ftmouse.py SECONDS STEP: Meta+left-drag left/right with REL_X every 8 ms (125 Hz).
import sys,time
from evdev import UInput, ecodes as e
secs=float(sys.argv[1]); step=int(sys.argv[2]); drag=len(sys.argv)<4 or sys.argv[3]!='nodrag'
cap={e.EV_KEY:[e.BTN_LEFT,e.BTN_RIGHT,e.KEY_LEFTMETA],e.EV_REL:[e.REL_X,e.REL_Y]}
ui=UInput(cap,name='ft-mouse'); time.sleep(1.0)
if drag:
    ui.write(e.EV_KEY,e.KEY_LEFTMETA,1); ui.syn(); time.sleep(0.05)
    ui.write(e.EV_KEY,e.BTN_LEFT,1); ui.syn()
log=open('/var/tmp/ft/events.txt','w')
t0=time.monotonic(); n=0; d=step
while time.monotonic()-t0<secs:
    if n%40==0 and n: d=-d
    ui.write(e.EV_REL,e.REL_X,d); ui.syn(); log.write('%.6f\n'%time.monotonic()); n+=1
    nxt=t0+n*0.008
    time.sleep(max(0,nxt-time.monotonic()))
if drag:
    ui.write(e.EV_KEY,e.BTN_LEFT,0); ui.syn(); ui.write(e.EV_KEY,e.KEY_LEFTMETA,0); ui.syn()
time.sleep(0.3); ui.close(); log.close()
