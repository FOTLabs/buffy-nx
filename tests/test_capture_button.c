#include "../app/src/buffy_visibility.c"
#include <assert.h>
volatile int nv2a_fixture_pad_trigger;
int main(int argc,char **argv) {
 int enabled=argc>1;
 if(enabled) setenv("GAME_CAPTURE_BUTTON","minus",1);
 else unsetenv("GAME_CAPTURE_BUTTON");
 uint16_t d=0x20;uint8_t a[8]={0,0,0,0,0,0,255,123};
 int16_t t[4]={123,456,789,321};
 xbox_platform_input_filter(0,&d,a,t);
 assert(d==(enabled?0:0x20));assert(nv2a_fixture_pad_trigger==enabled);
 assert(a[6]==255 && a[7]==123 && t[0]==123 && t[2]==789);
 nv2a_fixture_pad_trigger=0;d=0x20;
 xbox_platform_input_filter(0,&d,a,t);assert(!nv2a_fixture_pad_trigger);
 d=0;xbox_platform_input_filter(0,&d,a,t);
 d=0x20;xbox_platform_input_filter(0,&d,a,t);
 assert(nv2a_fixture_pad_trigger==enabled);
 nv2a_fixture_pad_trigger=0;d=0x20;
 xbox_platform_input_filter(1,&d,a,t);
 assert(d==0x20 && !nv2a_fixture_pad_trigger);
 puts("PASS: capture shortcut owns only Minus on player one when explicitly enabled");
}