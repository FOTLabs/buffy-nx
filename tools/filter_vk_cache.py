#!/usr/bin/env python3
"""Seed a title cache from native records matching that title's PC runs.
No driver binaries are rebuilt. Input files are read-only; output is separate.
Usage: filter_vk_cache.py NATIVE_DIR OUTPUT_DIR PC_RUN_DIR [PC_RUN_DIR ...]
"""
import hashlib, json, struct, sys
from pathlib import Path
PK_BYTES = 152
PROG_BYTES = 16 + PK_BYTES + 136 * 16
PIPE_BYTES = 200

def programs(path):
    data = path.read_bytes()
    if len(data) % PROG_BYTES: raise ValueError(f"Truncated program cache: {path}")
    for off in range(0, len(data), PROG_BYTES):
        record = data[off:off+PROG_BYTES]
        magic, xform, start, slots = struct.unpack_from("<4I", record)
        if magic != 0x4350564E or slots > 136 or start >= 136:
            raise ValueError(f"Invalid program record: {path}:{off}")
        vkey = 0
        if xform & 0xFFFF == 2:
            vkey = 1469598103934665603
            for pc in range(start, slots or 136):
                words = struct.unpack_from("<4I",record,16+PK_BYTES+pc*16)
                for word in words: vkey = ((vkey ^ word)*1099511628211)&0xFFFFFFFFFFFFFFFF
                if words[3]&1: break
            vkey = vkey or 1
        yield (vkey,xform>>16,record[16:16+PK_BYTES]), record

def spirv(path):
    data=path.read_bytes();off=0
    while off<len(data):
        if off+20>len(data):raise ValueError(f"Truncated SPIR-V cache: {path}")
        magic,lo,hi,n,stage=struct.unpack_from("<5I",data,off)
        end=off+16+n*4
        if magic!=0x31565053 or not 1<=n<=1<<22 or end>len(data):
            raise ValueError(f"Invalid SPIR-V record: {path}:{off}")
        yield (lo|(hi<<32),stage),data[off:end]
        off=end

def main():
    native,out,*runs=map(Path,sys.argv[1:])
    if not runs:raise SystemExit(__doc__)
    if out.resolve()==native.resolve():raise ValueError("Output must differ from native backup")
    out.mkdir(parents=True,exist_ok=True)
    wanted=set();spv_wanted=set()
    for run in runs:
        if (run/'progcache.bin').exists():wanted.update(k for k,_ in programs(run/'progcache.bin'))
        if (run/'vkspirv.bin').exists():spv_wanted.update(k for k,_ in spirv(run/'vkspirv.bin'))
    seen=set();kept=[];native_n=0
    for key,rec in programs(native/'progcache.bin'):
        native_n+=1
        if key in wanted and key not in seen:kept.append(rec);seen.add(key)
    if not kept:raise ValueError("No matching title programs; refusing empty migration")
    (out/'progcache.bin').write_bytes(b''.join(kept))
    identities={(k[0],k[2]) for k in seen};data=(native/'vkpipes.bin').read_bytes()
    if len(data)%PIPE_BYTES:raise ValueError("Truncated native pipeline list")
    pipes=[];pipe_seen=set()
    for off in range(0,len(data),PIPE_BYTES):
        rec=data[off:off+PIPE_BYTES];magic=struct.unpack_from('<I',rec)[0]
        if magic!=0x4C504B56:raise ValueError("Invalid pipeline record")
        key=(struct.unpack_from('<Q',rec,8)[0],rec[16:16+PK_BYTES])
        if key in identities and rec not in pipe_seen:pipes.append(rec);pipe_seen.add(rec)
    (out/'vkpipes.bin').write_bytes(b''.join(pipes))
    modules=[];spv_seen=set()
    for key,rec in spirv(native/'vkspirv.bin'):
        if key in spv_wanted and key not in spv_seen:modules.append(rec);spv_seen.add(key)
    (out/'vkspirv.bin').write_bytes(b''.join(modules))
    # Native driver cache is opaque and tied to the console device UUID.
    (out/'vkpipecache.bin').write_bytes((native/'vkpipecache.bin').read_bytes())
    result={'native_programs':native_n,'title_programs':len(kept),'native_pipeline_records':len(data)//PIPE_BYTES,'title_pipelines':len(pipes),'title_spirv_modules':len(modules),'files':{p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in out.glob('*.bin')}}
    (out/'migration.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='files'},indent=2))
if __name__=='__main__':main()
