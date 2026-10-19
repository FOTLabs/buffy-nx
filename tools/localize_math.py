"""Keep two profiled, call-free matrix routines' SSE intermediates in locals.
All guest-visible XMM values are written back on return. Run on fresh output
before comparing generated chunks, so normal regeneration is reproducible.
"""
from pathlib import Path
import re
import sys

FUNCTIONS = ("002A9230", "002A94C0")

def localize_function(source, name):
    marker = "void sub_" + name + "(void)"
    start = source.index(marker)
    end = source.index("\n}\n", start) + 3
    text = source[start:end]
    if "/* local SSE intermediates */" in text:
        return source
    if any(token in text for token in ("RECOMP_ABI_CALL", "RECOMP_ICALL", "RECOMP_SPIN", "goto ")):
        raise ValueError(name + " is no longer a straight-line leaf")
    split = text.index("    RECOMP_PREEMPT();") + len("    RECOMP_PREEMPT();")
    head, body = text[:split], text[split:]
    registers = sorted(set(re.findall(r"\bxmm[0-7]\b", body)))
    if not registers or "return;" not in body:
        raise ValueError(name + " has no SIMD body or return")
    for register in registers:
        body = re.sub(r"\b"+register+r"\b", "local_"+register, body)
    spill = " ".join("g_"+r+" = local_"+r+";" for r in registers)
    body = body.replace("return;", "{ "+spill+" return; }")
    loads = "\n    /* local SSE intermediates */\n" + "\n".join(
        "    RecompXmm local_"+r+" = g_"+r+";" for r in registers)
    # SSE ADDPS/MULPS round separately. Do not fuse adjacent emulated ops.
    attribute = '#if defined(__GNUC__) && !defined(__clang__)\n__attribute__((optimize("fp-contract=off")))\n#endif\n'
    return source[:start]+attribute+head+loads+body+source[end:]

def main():
    path = Path(sys.argv[1])/"recomp_0042.c"
    original = path.read_bytes()
    newline = "\r\n" if b"\r\n" in original else "\n"
    text = original.decode().replace("\r\n", "\n")
    for name in FUNCTIONS:
        text = localize_function(text, name)
    result = text.replace("\n", newline).encode()
    if result != original:
        path.write_bytes(result)
        print("Localised profiled SSE matrix intermediates in", path)

if __name__ == "__main__":
    main()
