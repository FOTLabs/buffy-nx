"""Exercise the actual native reader with concurrent fake-libnx callers."""
from pathlib import Path
import re, subprocess, tempfile
src=(Path(__file__).resolve().parents[1]/"sdk/src/input/xinput_nx.c").read_text()
fn=re.search(r"int xbox_nx_pad_read\(.*?^\}",src,re.M|re.S).group()
buttons=sorted(set(re.findall(r"HidNpadButton_\w+",fn)))
styles=sorted(set(re.findall(r"HidNpadStyleTag_\w+",fn)))
h=r"""
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
typedef uint64_t u64; typedef uint32_t u32;
typedef struct {int x,y;} HidAnalogStickState;
typedef int PadState;
#define NX_PADS 2
#define NX_SRCS 3
enum {NX_XBOX_A,NX_XBOX_B,NX_XBOX_X,NX_XBOX_Y,NX_XBOX_BLACK,NX_XBOX_WHITE,NX_XBOX_LT,NX_XBOX_RT};
static pthread_mutex_t s_lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int owns_lock;
static int s_ready=1,s_positional;
static PadState s_src[NX_SRCS];
volatile int nv2a_fixture_pad_trigger;
static void mutexLock(pthread_mutex_t*p){pthread_mutex_lock(p);owns_lock=1;}
static void mutexUnlock(pthread_mutex_t*p){owns_lock=0;pthread_mutex_unlock(p);}
static void nx_pad_init(void){s_ready=1;}
static void nx_assign(unsigned m[2]){m[0]=1;m[1]=0;}
static void padUpdate(PadState*p){(void)p;sched_yield();}
static HidAnalogStickState padGetStickPos(PadState*p,int i){
 (void)p;return (HidAnalogStickState){i?12345:-12345,i?6789:-6789};
}
static void nx_sideways(u32 s,u64*b,HidAnalogStickState*t,HidAnalogStickState l,HidAnalogStickState r){abort();}
static int stick_mag(HidAnalogStickState s){return abs(s.x)+abs(s.y);}
static u64 armGetSystemTick(void){
 /* The shared trigger timestamp must be updated under the sampling lock. */
 assert(owns_lock);static u64 tick;return ++tick;
}
static u64 armTicksToNs(u64 t){return t*1000000;}
"""
h+="\nenum {"+",".join(f"{n}=1ULL<<{i}" for i,n in enumerate(buttons))+"};\n"
h+="\nenum {"+",".join(f"{n}=1U<<{i}" for i,n in enumerate(styles))+"};\n"
h+=r"""
static u64 padGetButtons(PadState*p){(void)p;return HidNpadButton_ZL|HidNpadButton_ZR;}
static u32 padGetStyleSet(PadState*p){(void)p;return HidNpadStyleTag_NpadFullKey;}
"""
h+=fn+r"""
static void *reader(void*arg){
 unsigned previous=0;
 for(int i=0;i<5000;i++){
  uint16_t d;uint8_t a[8];int16_t t[4];
  assert(xbox_nx_pad_read(0,&d,a,t)==1);
  assert(a[6]>=previous && a[6]==a[7]);previous=a[6];
  assert(t[0]==-12345 && t[2]==12345);
 }
 assert(previous==255);return NULL;
}
int main(void){
 setenv("RECOMP_NX_TRIGGER_RAMP_MS","100",1);setenv("RECOMP_NX_TRIGGER_TRACE","0",1);
 pthread_t a,b;pthread_create(&a,0,reader,0);pthread_create(&b,0,reader,0);
 pthread_join(a,0);pthread_join(b,0);
 uint16_t d;uint8_t v[8];int16_t t[4];
 assert(xbox_nx_pad_read(1,&d,v,t)==0);assert(xbox_nx_pad_read(2,&d,v,t)==0);
 assert(!owns_lock && !pthread_mutex_trylock(&s_lock));pthread_mutex_unlock(&s_lock);
 puts("PASS: concurrent native reads preserve held triggers and release locks on disconnected ports");
}
"""
with tempfile.TemporaryDirectory() as tmp:
 exe=tmp+"/test"
 subprocess.run(["cc","-x","c","-pthread","-O2","-o",exe,"-"],input=h,text=True,check=True)
 subprocess.run([exe],check=True)
