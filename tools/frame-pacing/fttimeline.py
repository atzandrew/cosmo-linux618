import re,sys
f=sys.argv[1]
ts=lambda l:float(re.search(r'\s(\d+\.\d+):',l).group(1))
fl=[];gf=[];q={};run={};jobs=[]
for l in open(f):
    if 'drm_vblank_event_delivered' in l: fl.append((ts(l),int(re.search(r'seq=(\d+)',l).group(1))))
    elif 'devfreq_frequency' in l and '13040000.gpu' in l: gf.append((ts(l),int(re.search(r'freq=(\d+)',l).group(1))//10**6))
    elif 'drm_sched_job_run' in l: run[re.search(r'fence=(\S+),',l).group(1)]=ts(l)
    elif 'drm_sched_job_done' in l:
        fe=re.search(r'fence=(\S+) ',l).group(1)
        if fe in run: jobs.append((run[fe],ts(l)))
def gfreq(t):
    v=300
    for a,b in gf:
        if a<=t: v=b
    return v
for a,b in zip(fl,fl[1:]):
    g=b[1]-a[1]
    if g>6: print(f'{b[0]:.3f}  --- idle {g} vblanks ---'); continue
    ex=sum(min(e,b[0])-max(s,a[0]) for s,e in jobs if e>a[0] and s<b[0])*1000
    print(f'{b[0]:.3f} gap {g//2} frame(s)  gpu {gfreq(b[0])} MHz  gpu-busy-in-interval {ex:5.1f} ms {"LATE" if g>2 else ""}')
