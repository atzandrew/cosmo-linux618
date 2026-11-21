# ftlat.py TRACE.txt EVENTS.txt
# lower bound: event -> next flip delivered
# estimate: event -> flip of the first KWin frame whose GPU work was queued
# after the event (the frame that can contain it)
import re,sys,bisect
ts=lambda l:float(re.search(r'\s(\d+\.\d+):',l).group(1))
fl=[];kq=[];kd={};qf={}
for l in open(sys.argv[1]):
    if 'drm_vblank_event_delivered' in l: fl.append(ts(l))
    elif 'drm_sched_job_queue' in l and l.lstrip().startswith('kwin_wayland'):
        kq.append(ts(l))
ev=[float(x) for x in open(sys.argv[2])]
def stats(name,v):
    v=sorted(v); p=lambda q:v[int(q*(len(v)-1))]
    print(f'{name}: n={len(v)} min {v[0]:.1f} median {p(.5):.1f} p90 {p(.9):.1f} max {v[-1]:.1f} ms')
lb=[];est=[]
for t in ev:
    i=bisect.bisect_right(fl,t)
    if i<len(fl): lb.append((fl[i]-t)*1000)
    j=bisect.bisect_right(kq,t)
    if j<len(kq):
        k=bisect.bisect_right(fl,kq[j])
        if k<len(fl): est.append((fl[k]-t)*1000)
stats('event -> next flip (lower bound)',lb)
if est: stats('event -> flip of next KWin render',est)
