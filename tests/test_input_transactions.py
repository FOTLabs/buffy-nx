"""Exercise the production input guard with controlled thread interleavings."""
from pathlib import Path
import subprocess, tempfile
source=(Path(__file__).resolve().parents[1]/"app/src/buffy_input.c").read_text()
source=source.replace('#include "recomp_types.h"',
    '#include <stdint.h>\n#define RECOMP_TLS _Thread_local\nextern RECOMP_TLS uint32_t g_esp;')
harness=r"""
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
_Thread_local uint32_t g_esp;
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static int mode,entered,finish,suspended,resumed,poll_calls,update_calls,samples,frames;
void buffy_input_poll(void),buffy_input_update(void);
int xbox_gil_suspend(void){
 pthread_mutex_lock(&gate);suspended++;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&gate);
 return 7;
}
void xbox_gil_resume(int depth){assert(depth==7);resumed++;}
static void hold_transaction(void){
 pthread_mutex_lock(&gate);entered=1;pthread_cond_broadcast(&cv);
 while(!finish)pthread_cond_wait(&cv,&gate);
 pthread_mutex_unlock(&gate);
}
void sub_00024E90(void){
 poll_calls++;samples++;
 if(mode==3)hold_transaction();
 frames++;g_esp+=4;
}
void sub_00024B50(void){
 if(mode==1){g_esp-=4;buffy_input_poll();}
 if(mode==2)hold_transaction();
 assert(samples==frames);samples=frames=0;update_calls++;g_esp+=8;
}
static void *poll_thread(void *unused){
 g_esp=1000;buffy_input_poll();assert(g_esp==1004);return 0;
}
static void *update_thread(void *unused){
 g_esp=2000;buffy_input_update();assert(g_esp==2008);return 0;
}
static void reset(int next){
 mode=next;entered=finish=suspended=resumed=poll_calls=update_calls=samples=frames=0;
}
static void wait_entered(void){
 pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&cv,&gate);pthread_mutex_unlock(&gate);
}
static void release_transaction(void){
 pthread_mutex_lock(&gate);finish=1;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&gate);
}
int main(void){
 pthread_t a,b;
 /* An update may synchronously request more samples on its own thread. */
 reset(1);g_esp=3000;buffy_input_update();
 assert(g_esp==3008 && poll_calls==1 && update_calls==1 && !suspended);
 /* A background poll must skip, without waiting behind a consumer. */
 reset(2);pthread_create(&a,0,update_thread,0);wait_entered();
 g_esp=4000;buffy_input_poll();
 assert(g_esp==4004 && poll_calls==0 && !suspended);
 release_transaction();pthread_join(a,0);assert(update_calls==1);
 /* A consumer waits for a whole sample, releasing the guest lock to do so. */
 reset(3);pthread_create(&a,0,poll_thread,0);wait_entered();
 pthread_create(&b,0,update_thread,0);
 pthread_mutex_lock(&gate);while(!suspended)pthread_cond_wait(&cv,&gate);
 assert(update_calls==0 && samples==1 && frames==0);pthread_mutex_unlock(&gate);
 release_transaction();pthread_join(a,0);pthread_join(b,0);
 assert(poll_calls==1 && update_calls==1 && suspended==1 && resumed==1);
 puts("PASS: input transactions serialize publication, allow nested polling, skip busy callbacks and preserve guest stack cleanup");
}
"""
with tempfile.TemporaryDirectory() as tmp:
    exe=tmp+"/test"
    subprocess.run(["cc","-x","c","-pthread","-O2","-o",exe,"-"],
                   input=harness+source,text=True,check=True)
    subprocess.run([exe],check=True,timeout=5)
