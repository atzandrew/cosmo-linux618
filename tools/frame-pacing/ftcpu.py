# ftcpu.py TRACE.txt : per-frame CPU time of busy threads between presented frames.
import re,sys,collections
f=sys.argv[1]
ts=lambda l:float(re.search(r'\s(\d+\.\d+):',l).group(1))
fl=[];run=collections.defaultdict(list);cur={}
for l in open(f):
    if 'drm_vblank_event_delivered' in l: fl.append(ts(l))
    elif 'sched_switch:' in l:
        m=re.search(r'\[(\d+)\].*sched_switch:\s+(.+?):(\d+) .*==> (.+?):(\d+) ',l)
        if not m: continue
        cpu,pc,pp,nc,np_=m.groups(); t=ts(l)
        if cpu in cur:
            s,name=cur[cpu]
            if name[1]!='0': run[name].append((s,t))
        cur[cpu]=(t,(nc,np_))
if len(fl)<2: print('no frames'); sys.exit()
t0,t1=fl[0],fl[-1]
tot=collections.Counter()
for k,v in run.items():
    tot[k]=sum(min(b,t1)-max(a,t0) for a,b in v if b>t0 and a<t1)
span=t1-t0
print(f'frames {len(fl)} over {span:.2f}s; top threads by CPU (ms per presented frame, % of one core):')
for k,v in tot.most_common(12):
    print(f'  {k[0]:>18}:{k[1]:<6} {v*1000/len(fl):6.2f} ms/frame  {100*v/span:5.1f}%')
