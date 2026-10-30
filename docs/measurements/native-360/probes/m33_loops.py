#!/usr/bin/env python3
"""Innermost loops of Clang Thumb-2 assembly with static instruction counts.

    python3 m33_loops.py build/m33/*.s
"""

import re
import sys


# Clang annotates loop blocks in comments that may span several lines after
# the label: "This Inner Loop Header: Depth=N" or "in Loop: Header=BBx_y".
def parse(path):
    lines = open(path).read().splitlines()
    blocks, func, cur = [], None, None
    for ln in lines:
        m = re.match(r'^([A-Za-z_]\w*):', ln)
        if m:
            func = m.group(1)
            cur = None
            continue
        m = re.match(r'^\.?(LBB\d+_\d+):(.*)$', ln)
        if m:
            cur = {'func': func, 'label': m.group(1).replace('LBB', 'BB'), 'note': m.group(2), 'ins': []}
            blocks.append(cur)
            continue
        s = ln.strip()
        if cur is None:
            continue
        if s.startswith('@'):
            if not cur['ins']:
                cur['note'] += ' ' + s
            continue
        if s and not s.startswith('.'):
            cur['ins'].append(s)
    return blocks

for path in sys.argv[1:]:
    blocks = parse(path)
    loops = {}
    for b in blocks:
        if 'Inner Loop Header' in b['note']:
            loops[b['label']] = [b]
    for b in blocks:
        m = re.search(r'in Loop: Header=(BB\d+_\d+)', b['note'])
        if m and m.group(1) in loops and b not in loops[m.group(1)]:
            loops[m.group(1)].append(b)
    for k, bs in loops.items():
        n = sum(len(b['ins']) for b in bs)
        stores = sum(1 for b in bs for i in b['ins'] if i.startswith(('strh', 'str.w', 'strh.w')))
        print(f"{path.split('/')[-1]:16s} {bs[0]['func']:28s} {k:8s} blocks={len(bs)} instrs={n} strh={stores}")
