import re,sys,collections,statistics as st
f=sys.argv[1]
ts=lambda l:float(re.search(r'\s(\d+\.\d+):',l).group(1))
vb=[];fl=[];q={};run={};done=[];cpuf=collections.defaultdict(list);gpuf=[];ksw=collections.Counter()
for l in open(f):
    if 'drm_vblank_event:' in l:
        m=re.search(r'seq=(\d+), time=(\d+)',l); vb.append((ts(l),int(m.group(1)),int(m.group(2))/1e9))
    elif 'drm_vblank_event_delivered' in l:
        fl.append((ts(l),int(re.search(r'seq=(\d+)',l).group(1))))
    elif 'drm_sched_job_queue' in l:
        m=re.search(r'fence=(\S+),.*client_id:(\d+)',l); q[m.group(1)]=(ts(l),l.split('-')[0].strip(),m.group(2))
    elif 'drm_sched_job_run' in l:
        run[re.search(r'fence=(\S+),',l).group(1)]=ts(l)
    elif 'drm_sched_job_done' in l:
        fe=re.search(r'fence=(\S+) ',l).group(1)
        if fe in q and fe in run: done.append((q[fe][0],run[fe],ts(l),q[fe][1],q[fe][2]))
    elif 'cpu_frequency:' in l:
        m=re.search(r'state=(\d+) cpu_id=(\d+)',l); cpuf[int(m.group(2))].append((ts(l),int(m.group(1))))
    elif 'devfreq_frequency' in l and '13040000.gpu' in l:
        m=re.search(r'freq=(\d+)\s+prev_freq=(\d+)\s+load=(\d+)',l); gpuf.append((ts(l),int(m.group(1))//1000000,int(m.group(3))))
def hist(xs,edges):
    c=collections.Counter()
    for x in xs:
        for e in edges:
            if x<=e: c[e]+=1;break
        else: c['>']+=1
    return c
print('== vblank ==')
if len(vb)>1:
    d=[(b[2]-a[2])*1000 for a,b in zip(vb,vb[1:]) if b[1]==a[1]+1]
    dt=[(b[0]-a[0])*1000 for a,b in zip(vb,vb[1:]) if b[1]==a[1]+1]
    jumps=[b[1]-a[1] for a,b in zip(vb,vb[1:]) if b[1]!=a[1]+1]
    span=vb[-1][0]-vb[0][0]; nseq=vb[-1][1]-vb[0][1]
    print(f'events {len(vb)} seq advance {nseq} over {span:.2f}s -> {nseq/span:.2f} Hz by seq; vblank-core time median {st.median(d):.3f} ms; irq-time intervals: median {st.median(dt):.3f} ms, <2ms:{sum(x<2 for x in dt)} 2-12:{sum(2<=x<12 for x in dt)} 12-20:{sum(12<=x<20 for x in dt)} >20:{sum(x>=20 for x in dt)}; seq jumps {collections.Counter(jumps)}')
print('== flips delivered to KWin (presented frames) ==')
if len(fl)>1:
    gaps=[b[1]-a[1] for a,b in zip(fl,fl[1:])]
    c=collections.Counter(gaps)
    # bursts: consecutive flips with gap<=4 vblanks = animation
    print('seq gap histogram (1=every vblank, 2=one repeat ...):',dict(sorted(c.items())[:12]), 'max',max(gaps))
    inburst=[g for g in gaps if g<=6]
    print(f'in-animation frames {len(inburst)}: on-time(1) {sum(g==1 for g in inburst)}, missed 1 ({sum(g==2 for g in inburst)}), missed 2+ ({sum(g>2 for g in inburst)})')
    w=[(b[0]-a[0])*1000 for a,b in zip(fl,fl[1:]) if b[1]-a[1]<=6]
    if w: print(f'wall interval in animation ms: median {st.median(w):.2f} p90 {sorted(w)[int(.9*len(w))]:.2f} max {max(w):.2f}')
print('== GPU jobs ==')
if done:
    by=collections.defaultdict(list)
    for qt,rt,dt_,comm,cid in done: by[(comm,cid)].append((rt-qt,dt_-rt))
    for k,v in sorted(by.items(),key=lambda x:-len(x[1])):
        ex=[b*1000 for a,b in v]; wt=[a*1000 for a,b in v]
        print(f'{k}: {len(v)} jobs, exec median {st.median(ex):.2f} p90 {sorted(ex)[int(.9*len(ex))]:.2f} max {max(ex):.2f} ms; queue wait median {st.median(wt):.3f} max {max(wt):.2f} ms')
    t0=min(d[1] for d in done);t1=max(d[2] for d in done)
    busy=sum(d[2]-d[1] for d in done)
    print(f'GPU busy (sum of job exec, overlap counted) {busy*1000:.0f} ms over {(t1-t0):.2f}s')
print('== GPU freq transitions ==')
print([(round(t,2),f,ld) for t,f,ld in gpuf][:40])
print('== CPU freq (cpu0 little, cpu4 big) ==')
for c in sorted(cpuf):
    v=[x[1]//1000 for x in cpuf[c]]
    print(c,len(v),'MHz min',min(v),'max',max(v),'seq',v[:25])
