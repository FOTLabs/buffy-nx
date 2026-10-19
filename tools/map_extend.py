"""Extend MAP functions over code their own branches reach (cold blocks past the
first ret). Recursive descent from the MAP start, bounded by the next MAP start;
follows jcc/jmp rel targets, stops at ret / indirect jmp. A disasm 'end' short
of the furthest reached instruction made the lifter treat those blocks as
separate tail-called functions (Buffy TCharacterInventory::InformLeavingSequence
0x12B400 ended at 0x12B561; jumps to 0x12B561/6C/75 came back as tail calls ->
esi/esp broken in ICharacter::InformLeavingSequence -> crash).
  python3 map_extend.py functions.json map_names.json xbe analysis.json   (in place)"""
import json, sys, bisect, capstone
fp, mp, xbe, an = sys.argv[1:5]
funcs = json.load(open(fp)); x = open(xbe, 'rb').read()
secs = json.load(open(an))['sections']
mapped = sorted(int(k, 16) for k in json.load(open(mp)))
def foff(va):
    for s in secs:
        v = int(s['virtual_addr'], 16)
        if v <= va < v + s['virtual_size'] and s['executable']:
            return int(s['raw_addr'], 16) + va - v
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
by = {int(e['start'], 16): e for e in funcs}
extended = merged = 0
newly = []
for i, a in enumerate(mapped):
    e = by.get(a)
    if not e: continue
    lim = mapped[i + 1] if i + 1 < len(mapped) else a + 0x10000
    lim = min(lim, a + 0x40000)
    seen, work, hi = set(), [a], int(e['end'], 16)
    while work:
        pc = work.pop()
        while a <= pc < lim and pc not in seen:
            o = foff(pc)
            if o is None: break
            ins = next(md.disasm(x[o:o + 16], pc), None)
            if not ins: break
            seen.add(pc); pc2 = pc + ins.size; hi = max(hi, pc2)
            m = ins.mnemonic
            if m.startswith('j') and ins.op_str.startswith('0x'):
                t = int(ins.op_str, 16)
                if a <= t < lim: work.append(t)
                if m == 'jmp': break
            elif m in ('ret', 'retn', 'jmp', 'int3', 'hlt'):
                break
            pc = pc2
    if hi > int(e['end'], 16):
        newly.append((int(e['end'], 16), hi))
        e['end'] = '0x%08X' % hi; e['size'] = hi - a; extended += 1
ms = set(mapped)
out = []
for e in sorted(funcs, key=lambda e: int(e['start'], 16)):
    s = int(e['start'], 16)
    # only entries the extension itself swallowed (cold blocks), never
    # aliases/targets inside the original extent: other code enters those
    if s not in ms and any(lo <= s < hi2 for lo, hi2 in newly)             and e.get('detection_method') != 'call_target':
        merged += 1; continue
    out.append(e)
json.dump(out, open(fp, 'w'), indent=2)
print('map_extend: %d MAP functions extended, %d inner starts merged' % (extended, merged))
