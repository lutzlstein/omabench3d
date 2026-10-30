import sys,re
lines=open(sys.argv[1],errors='ignore').read().split('\n')
start=max(i for i,l in enumerate(lines) if 'GPU Profile for Frame' in l)
for l in lines[start:]:
    p=l.split('┃')
    if len(p)<4: continue
    m=re.search(r'([\d.]+) ms',p[2]); ex=re.search(r'([\d.]+) ms',p[1])
    if not m: continue
    name=p[3].rstrip(); depth=(len(name)-len(name.lstrip()))//3
    t=float(m.group(1)); e=float(ex.group(1)) if ex else 0
    if t>=float(sys.argv[2]) and depth<=int(sys.argv[3]): print(f"{t:7.2f} {e:6.2f} {'  '*depth}{name.strip()[:70]}")
