/* A pause-only setting; the colour adjustment runs in the final present pass. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
static int s_pause, s_handheld = 1;
extern volatile int nv2a_fixture_pad_trigger;
static unsigned s_y, s_y_owned;
static unsigned s_start_down, s_start_epoch, s_pause_start_epoch;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;
static char s_path[512];
static void load_visibility(void) {
#ifdef __SWITCH__
    const char *dir = "sdmc:/switch/buffyx";
#else
    const char *dir = getenv("GAME_SAVE_DIR");
#endif
    if (dir && snprintf(s_path,sizeof s_path,"%s/buffy_visibility.cfg",dir) < (int)sizeof s_path) {
        FILE *f=fopen(s_path,"r");int n;
        if (f) { if (fscanf(f,"%d",&n)==1 && (n==0 || n==1)) s_handheld=n; fclose(f); }
    } else s_path[0]=0;
    const char *env=getenv("GAME_VISIBILITY");
    if(env) s_handheld=strcmp(env,"original")!=0;
}
void buffy_pause_visibility(int open) {
    int before=__atomic_exchange_n(&s_pause,!!open,__ATOMIC_ACQ_REL);
    if(before!=!!open) fprintf(stderr,"[pause] %s\n",open ? "open" : "closed");
    static int captured;
    if(open && !before && !captured && getenv("GAME_CAPTURE_FIRST_PAUSE")) {
        nv2a_fixture_pad_trigger=1;captured=1;
    }
}
/* SetOpen can be reached by both gameplay and the GUI for one physical
 * Start press. Own that press at the transition boundary, not the USB report.
 * Check first, then commit only if the original transition was accepted. */
int buffy_pause_duplicate_start(int current,int requested) {
    return !!current!=!!requested && __atomic_load_n(&s_start_down,__ATOMIC_ACQUIRE)
        && __atomic_load_n(&s_start_epoch,__ATOMIC_ACQUIRE)==s_pause_start_epoch;
}
void buffy_pause_accept_start(void) {
    s_pause_start_epoch=__atomic_load_n(&s_start_epoch,__ATOMIC_ACQUIRE);
}
int buffy_pause_start_owned(void) {
    /* Ownership survives release: the GUI closes on release, not key-down. */
    return __atomic_load_n(&s_pause,__ATOMIC_ACQUIRE)
        && __atomic_load_n(&s_start_epoch,__ATOMIC_ACQUIRE)==s_pause_start_epoch;
}
float xbox_platform_visibility(void) {
    pthread_once(&s_once,load_visibility);
    return __atomic_load_n(&s_handheld,__ATOMIC_RELAXED) ? 0.045f : 0.0f;
}
const char *xbox_platform_pause_option(void) {
    if(!__atomic_load_n(&s_pause,__ATOMIC_ACQUIRE)) return NULL;
    return xbox_platform_visibility() > 0 ? "Y: HANDHELD VISIBILITY - HANDHELD" : "Y: HANDHELD VISIBILITY - ORIGINAL";
}
/* Sparse first-person diagnostics, opt-in only; records real input unchanged. */
static uint16_t s_aim_digital;
static uint8_t s_aim_analog[8];
static int16_t s_aim_thumbs[4];
int buffy_trace_aim_enabled(void) {
    static int cached;
    int state=__atomic_load_n(&cached,__ATOMIC_RELAXED);
    if(!state) {
        const char *e=getenv("GAME_TRACE_AIM");state=e && *e=='1' ? 2 : 1;
        __atomic_store_n(&cached,state,__ATOMIC_RELAXED);
    }
    return state==2;
}
void buffy_aim_input_snapshot(uint16_t *digital,uint8_t analog[8],int16_t thumbs[4]) {
    *digital=__atomic_load_n(&s_aim_digital,__ATOMIC_RELAXED);
    for(int i=0;i<8;i++)analog[i]=__atomic_load_n(&s_aim_analog[i],__ATOMIC_RELAXED);
    for(int i=0;i<4;i++)thumbs[i]=__atomic_load_n(&s_aim_thumbs[i],__ATOMIC_RELAXED);
}
void xbox_platform_input_filter(unsigned port,uint16_t *digital,uint8_t analog[8],int16_t thumbs[4]) {
    (void)thumbs;
    if(port) return;
    if(buffy_trace_aim_enabled()) {
        __atomic_store_n(&s_aim_digital,*digital,__ATOMIC_RELAXED);
        for(int i=0;i<8;i++)__atomic_store_n(&s_aim_analog[i],analog[i],__ATOMIC_RELAXED);
        for(int i=0;i<4;i++)__atomic_store_n(&s_aim_thumbs[i],thumbs[i],__ATOMIC_RELAXED);
        /* Record trigger/ stick-click transitions even after first-person
         * camera updates stop. Never filter or manufacture a pad sample. */
        static uint32_t last_buttons=~0u;
        uint32_t buttons=analog[6] | ((uint32_t)analog[7]<<8)
                         | ((uint32_t)(*digital & 0x00C0)<<16);
        if(__atomic_exchange_n(&last_buttons,buttons,__ATOMIC_RELAXED)!=buttons)
            fprintf(stderr,"[aim-pad] LT/RT=%u/%u dig=%04X L=%d,%d R=%d,%d\n",
                analog[6],analog[7],*digital,thumbs[0],thumbs[1],thumbs[2],thumbs[3]);
    }
    /* Observe physical edges. Do not manufacture a release when entering the
     * menu: its GUI also has a cached Start event independent of USB polling. */
    unsigned start=(*digital & 0x0010)!=0;
    unsigned previous=__atomic_exchange_n(&s_start_down,start,__ATOMIC_ACQ_REL);
    if(start && !previous) __atomic_add_fetch(&s_start_epoch,1,__ATOMIC_RELEASE);
    int paused=__atomic_load_n(&s_pause,__ATOMIC_ACQUIRE);
    unsigned y=analog[3]!=0;
    if(!y) s_y_owned=0;
    if(__atomic_load_n(&s_pause,__ATOMIC_ACQUIRE)) {
        if(y && !s_y) {
            pthread_once(&s_once,load_visibility);
            int next=!__atomic_load_n(&s_handheld,__ATOMIC_RELAXED);
            __atomic_store_n(&s_handheld,next,__ATOMIC_RELAXED);
            if(s_path[0]) { FILE *f=fopen(s_path,"w");if(f){fprintf(f,"%d\n",next);fclose(f);} }
            fprintf(stderr,"[visibility] %s\n",next ? "Handheld" : "Original");
        }
        if(y) s_y_owned=1;
    }
    if(paused || s_y_owned) analog[3]=0;
    s_y=y;
}
