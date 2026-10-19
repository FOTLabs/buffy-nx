"""Check the actual pause wrapper ABI and rejected/accepted transitions."""
from pathlib import Path
import re,subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/"app/src/buffy_overrides.c").read_text()
gui=re.search(r"^void sub_00223C30\(void\)\n\{.*?^\}",s,re.M|re.S).group()
w=re.search(r"^void sub_00230AA0\(void\)\n\{.*?^\}",s,re.M|re.S).group()
h=r'''#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
static uint8_t mem[2048];static uint32_t esp,eax,ecx;
#define MEM8(a) mem[a]
#define MEM32(a) (*(uint32_t*)&mem[a])
static int duplicate,accepted,forwarded,visible,reject_original,owned,commands;
static int buffy_pause_start_owned(void){return owned;}
static void sub_00223C30_gen(void){commands++;esp+=8;}
static int buffy_pause_duplicate_start(int a,int b){return a!=b && duplicate;}
static void buffy_pause_accept_start(void){accepted++;}
static void buffy_pause_visibility(int a){visible=a;}
static void sub_00230AA0_gen(void){forwarded++;if(!reject_original)MEM8(ecx+0x274)=MEM8(esp+4);esp+=16;eax=reject_original?0:1;}
'''+w+'\n'+gui+r'''
int main(void){
 ecx=512;esp=128;eax=0x12345678;MEM32(esp)=0x223B27;MEM32(esp+4)=0;
 MEM8(ecx+0x274)=1;duplicate=1;sub_00230AA0();
 assert(esp==144 && eax==0x12345600 && !forwarded && !accepted && MEM8(ecx+0x274)==1);
 duplicate=0;esp=128;sub_00230AA0();assert(esp==144 && forwarded==1 && accepted==1 && visible==0);
 reject_original=1;esp=128;MEM32(esp+4)=1;sub_00230AA0();assert(accepted==1 && visible==0);
 owned=1;esp=128;MEM32(esp+4)=0x29;sub_00223C30();assert(esp==136 && !commands);
 owned=0;esp=128;sub_00223C30();assert(esp==136 && commands==1);
 owned=1;esp=128;MEM32(esp+4)=0x27;sub_00223C30();assert(esp==136 && commands==2);
 puts("PASS: duplicate pause transition rejected with ret12/false ABI; accepted ownership only after actual state change");
}
'''
with tempfile.TemporaryDirectory() as d:
 exe=d+"/test";subprocess.run(["cc","-x","c","-O2","-o",exe,"-"],input=h,text=True,check=True);subprocess.run([exe],check=True)
