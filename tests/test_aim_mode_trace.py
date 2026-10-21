"""Exercise the actual optional mode wrapper's guest ret4/register contract."""
from pathlib import Path
import re, subprocess, tempfile
source=(Path(__file__).resolve().parents[1]/"app/src/buffy_overrides.c").read_text()
function=re.search(r"void buffy_trace_first_person_switch\(void\)\n\{.*?^\}",source,re.M|re.S).group()
harness=r"""#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
static uint8_t memory[4096];
static uint32_t ecx,esp,eax,calls;
#define MEM32(a) (*(uint32_t*)(memory+(a)))
#define MEM8(a) (memory[(a)])
static void buffy_aim_input_snapshot(uint16_t*d,uint8_t a[8],int16_t t[4]){
 *d=0;memset(a,0,8);memset(t,0,8);a[6]=255;a[7]=255;
}
static void sub_000E5810(void){assert(ecx==256 && esp==512);calls++;esp+=8;eax=0x76543210;ecx=0xabcdef01;}
"""+function+r"""
int main(void){
 for(unsigned mode=0;mode<2;mode++)for(unsigned repeat=0;repeat<2;repeat++){
  ecx=256;esp=512;eax=0x11111111;MEM32(esp)=0x12345678;MEM32(esp+4)=mode;
  buffy_trace_first_person_switch();
  assert(esp==520 && eax==0x76543210 && ecx==0xabcdef01);
 }
 assert(calls==4);puts("PASS: mode tracing forwards every request and preserves original ret4/register effects");
}
"""
with tempfile.TemporaryDirectory() as tmp:
 exe=tmp+"/test"
 subprocess.run(["cc","-x","c","-O2","-o",exe,"-"],input=harness,text=True,check=True)
 subprocess.run([exe],check=True)
