#include "../app/src/buffy_visibility.c"
#include <assert.h>
volatile int nv2a_fixture_pad_trigger;
#include <unistd.h>
#include <sys/wait.h>
int main(int argc,char **argv) {
    if(argc>1) {assert(xbox_platform_visibility()==0);return 0;}
    char dir[]="/tmp/buffy-visibility-XXXXXX";assert(mkdtemp(dir));setenv("GAME_SAVE_DIR",dir,1);
    uint16_t dig=0;uint8_t ana[8]={0};int16_t th[4]={0};
    assert(xbox_platform_visibility()>0);assert(!xbox_platform_pause_option());
    ana[3]=255;xbox_platform_input_filter(0,&dig,ana,th);assert(ana[3]==255);
    buffy_pause_visibility(1);xbox_platform_input_filter(0,&dig,ana,th);assert(xbox_platform_visibility()>0 && !ana[3]);
    ana[3]=0;xbox_platform_input_filter(0,&dig,ana,th);
    ana[3]=255;xbox_platform_input_filter(0,&dig,ana,th);assert(xbox_platform_visibility()==0);
    for(int i=0;i<1000;i++){ana[3]=255;xbox_platform_input_filter(0,&dig,ana,th);assert(xbox_platform_visibility()==0 && !ana[3]);}
    assert(strstr(xbox_platform_pause_option(),"ORIGINAL"));
    pid_t pid=fork();assert(pid>=0);if(!pid){execl(argv[0],argv[0],"load",NULL);_exit(99);}
    int status;waitpid(pid,&status,0);assert(WIFEXITED(status) && !WEXITSTATUS(status));
    buffy_pause_visibility(0);assert(!xbox_platform_pause_option());
    /* The second pause in the hardware log closes 22 ms after opening,
     * with no physical release. Both GUI and gameplay must share ownership. */
    for(int pause=0;pause<3;pause++) {
        dig=0;xbox_platform_input_filter(0,&dig,ana,th);
        dig=0x10;xbox_platform_input_filter(0,&dig,ana,th);assert(dig==0x10);
        assert(!buffy_pause_duplicate_start(0,1));
        buffy_pause_accept_start();buffy_pause_visibility(1);
        for(int i=0;i<1000;i++) {
            dig=0x10;xbox_platform_input_filter(0,&dig,ana,th);
            assert(dig==0x10 && buffy_pause_duplicate_start(1,0));
            assert(!buffy_pause_duplicate_start(1,1));
        }
        dig=0;xbox_platform_input_filter(0,&dig,ana,th);
        assert(buffy_pause_start_owned()); /* cached GUI release still belongs to opening press */
        assert(!buffy_pause_duplicate_start(1,0)); /* Resume after release */
        dig=0x10;xbox_platform_input_filter(0,&dig,ana,th);
        assert(!buffy_pause_duplicate_start(1,0)); /* new physical press */
        assert(!buffy_pause_start_owned());
        buffy_pause_accept_start();buffy_pause_visibility(0);
        assert(buffy_pause_duplicate_start(0,1));
    }
    buffy_pause_visibility(1);ana[3]=0;dig=0;xbox_platform_input_filter(0,&dig,ana,th);
    ana[3]=255;xbox_platform_input_filter(0,&dig,ana,th);assert(!ana[3]);
    buffy_pause_visibility(0);ana[3]=255;xbox_platform_input_filter(0,&dig,ana,th);assert(!ana[3]);
    ana[3]=0;xbox_platform_input_filter(0,&dig,ana,th);
    ana[3]=255;xbox_platform_input_filter(0,&dig,ana,th);assert(ana[3]==255);
    unlink(s_path);rmdir(dir);
    puts("PASS: default Handheld, pause-only toggle/consumption, held Y does not repeat, preference survives restart");
}
