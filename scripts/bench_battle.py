"""Summary of a console battle benchmark (storm2/bench_battle.txt replay).

Reads the logs the console build writes to ux0:data/storm2port (copy them or
point at the mounted card) and prints, for the battle only:
  - the frame probe windows (10 s) whose task list has the fighters' task
    (821D1540): fps, ms/frame, game update, job wait, frame sync;
  - their median, and the mean of the per-frame costs;
  - CPU per thread from storm2_threads.log over the same time span;
  - the GLES reports that fall in that span (GPU thread breakdown).

usage: python -I scripts/bench_battle.py <dir with storm2.log> [label]
"""
import re
import statistics
import sys
from pathlib import Path

d = Path(sys.argv[1])
label = sys.argv[2] if len(sys.argv) > 2 else d.name
log = (d / 'storm2.log').read_text(encoding='utf-8', errors='replace').splitlines()

ts = re.compile(r'^\[\d{4}-\d\d-\d\d (\d\d):(\d\d):(\d\d)\.\d+\]')
probe = re.compile(r'frame probe: (\d+) frames in ([\d.]+) s \(([\d.]+) frames/s\); ms per frame, '
                   r'main thread \[calls, wall, cpu\]: (.*?); other threads')
part = re.compile(r'([a-z][a-z0-9 ]*?) \[([\d.]+), ([\d.]+), ([\d.]+)\]')


def secs(line):
    m = ts.match(line)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) if m else None


windows = []
pending = None
for line in log:
    m = probe.search(line)
    if m:
        parts = {p.group(1).strip(): (float(p.group(3)), float(p.group(4))) for p in part.finditer(m.group(4))}
        pending = (secs(line), float(m.group(3)), parts)
        continue
    if 'Storm 2 tasks' in line and pending:
        if ' 821D1540 ' in line and pending[1] > 1.0:  # < 1 fps: the load before the fight
            windows.append(pending)
        pending = None

print(f'== {label}: {len(windows)} battle windows of 10 s')
if not windows:
    sys.exit(0)
for t, fps, p in windows:
    g = lambda k: p.get(k, (0, 0))[0]
    print(f'  {t // 3600:02d}:{t // 60 % 60:02d}:{t % 60:02d}  {fps:5.2f} fps  {1000 / fps:6.1f} ms/frame  '
          f'update {g("update"):6.1f}  job wait {g("job wait"):6.1f}  frame sync {g("frame sync"):6.1f}')
fps_all = [w[1] for w in windows]
print(f'  median {statistics.median(fps_all):.2f} fps, mean {statistics.mean(fps_all):.2f}, '
      f'min {min(fps_all):.2f}, max {max(fps_all):.2f}')
for key in ('update', 'job wait', 'frame sync'):
    vals = [w[2].get(key, (0, 0))[0] for w in windows]
    print(f'  mean {key}: {statistics.mean(vals):.1f} ms/frame')

t0, t1 = windows[0][0] - 10, windows[-1][0]
# CPU per thread: storm2_threads.log counts process time (t=<s>), which stops
# while the console is suspended, so align its last report with the log's
# last line (the battle is at the end of a benchmark run).
start = None
for line in reversed(log):
    start = secs(line)
    if start is not None:
        break
thr = d / 'storm2_threads.log'
if thr.exists() and start is not None:
    lines = thr.read_text(encoding='utf-8', errors='replace').splitlines()
    last_t = max(int(m.group(1)) for m in (re.match(r'^t=(\d+)s', l) for l in lines) if m)
    start -= last_t
    cpu = {}
    blocks = 0
    take = False
    for line in lines:
        m = re.match(r'^t=(\d+)s', line)
        if m:
            t = start + int(m.group(1))
            take = t0 <= t <= t1
            blocks += take
            continue
        m = re.match(r'^\s+([\d.]+)%\s+core \d+\s+prio \d+\s+hw \d+ mask \w+\s+\w+\s+(.*)$', line)
        if take and m:
            name = re.sub(r'XThread[0-9A-F]+ ', '', m.group(2))
            cpu.setdefault(name, []).append(float(m.group(1)))
    print(f'  CPU per thread over {blocks} reports (% of one core):')
    for name, v in sorted(cpu.items(), key=lambda kv: -statistics.mean(kv[1]))[:10]:
        print(f'    {statistics.mean(v):5.1f}%  {name}')

print('  GLES reports in the battle span:')
for line in log:
    t = secs(line)
    if t is not None and t0 <= t <= t1 + 30 and 'GLES' in line and (
            'GLES: swap' in line or 'GPU thread' in line or 'GL calls' in line or 'time since' in line
            or 'detail' in line or 'GXM calls' in line or 'vertices:' in line or 'textures:' in line
            or 'constants' in line):
        print('    ' + line[line.find('GLES'):][:400])
