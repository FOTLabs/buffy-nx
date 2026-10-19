"""Compare an MSVC MAP's function symbols with tools.disasm output; write the
missing starts as seed_functions.json (generic: any title that ships a MAP)."""
import json, re, sys
xbe_json, map_path, funcs_json, out = sys.argv[1:5]
secs = json.load(open(xbe_json))['sections']
starts = {int(f['start'], 16) for f in json.load(open(funcs_json))}
rx = re.compile(r'^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+[0-9a-fA-F]{8}\s+f\b')
mapf = {}
for l in open(map_path, errors='ignore'):
    m = rx.match(l)
    if not m: continue
    n = int(m.group(1), 16)
    if n < 1 or n > len(secs) or not secs[n-1]['executable']: continue
    mapf.setdefault(int(secs[n-1]['virtual_addr'], 16) + int(m.group(2), 16), m.group(3))
miss = sorted(a for a in mapf if a not in starts)
print('map functions %d, disasm %d, map-only %d' % (len(mapf), len(starts), len(miss)))
for a in miss[:15]: print('  %08X %s' % (a, mapf[a]))
json.dump(['0x%08X' % a for a in miss], open(out, 'w'), indent=0)
json.dump({'0x%08X' % a: n for a, n in mapf.items()}, open(out.replace('seed_functions', 'map_names'), 'w'))
