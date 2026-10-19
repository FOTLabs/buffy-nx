"""With an MSVC MAP the function list is known exactly. Drop tools.disasm starts
that the heuristics invented *inside* a MAP function (gap_prologue: "code after
a ret with no padding looks like a new function") and extend the function
before it over the gap. Buffy: CMusicBufferDS::AutoCommit (0x685D0) was cut at
0x68628, the lifter lost its loop exit and its ret, and the caller's pop esi
took the return address -> null vtable calls, hung sound shutdown.
  python3 tools/map_prune.py <functions.json> <map_names.json>   (in place)"""
import json, sys
fp, mp = sys.argv[1:3]
funcs = json.load(open(fp))
mapped = {int(k, 16) for k in json.load(open(mp))}
funcs.sort(key=lambda e: int(e["start"], 16))
out, dropped = [], 0
for e in funcs:
    s = int(e["start"], 16)
    # gap_prologue: code after a ret; imm_ref_target: an immediate (EH scope table,
    # jump table) pointing into the body; cc_boundary. Real call targets and
    # tail-jump aliases stay: other code enters there.
    # IPickup::Event_PostPhysics (0xC1D70) was cut at 0xC1D8C (imm_ref_target):
    # no ret, caller's esp off by one slot, garbage element cast -> crash.
    if (e.get("detection_method") in ("gap_prologue", "imm_ref_target", "cc_boundary")
            and s not in mapped and out
            and int(out[-1]["end"], 16) == s and out[-1].get("section") == e.get("section")):
        prev = out[-1]
        prev["end"] = e["end"]
        prev["size"] = int(e["end"], 16) - int(prev["start"], 16)
        prev["num_instructions"] = prev.get("num_instructions", 0) + e.get("num_instructions", 0)
        dropped += 1
        continue
    out.append(e)
json.dump(out, open(fp, "w"), indent=2)
print("map_prune: merged %d heuristic starts into their MAP functions" % dropped)
