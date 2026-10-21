#include "../app/src/buffy_visibility.c"
#include <assert.h>
volatile int nv2a_fixture_pad_trigger;
int main(void) {
 setenv("GAME_TRACE_AIM","1",1);
 uint16_t d=0x90,read_d=0;uint8_t a[8]={255,13,240,201,22,23,170,255},read_a[8];
 int16_t t[4]={-32768,32767,16000,-16000},read_t[4];
 uint8_t original_a[8];int16_t original_t[4];
 memcpy(original_a,a,sizeof a);memcpy(original_t,t,sizeof t);
 xbox_platform_input_filter(0,&d,a,t);
 assert(d==0x90 && !memcmp(a,original_a,sizeof a) && !memcmp(t,original_t,sizeof t));
 buffy_aim_input_snapshot(&read_d,read_a,read_t);
 assert(read_d==d && !memcmp(a,read_a,sizeof a) && !memcmp(t,read_t,sizeof t));
 uint16_t second=1;uint8_t second_a[8]={0};int16_t second_t[4]={0};
 xbox_platform_input_filter(1,&second,second_a,second_t);
 buffy_aim_input_snapshot(&read_d,read_a,read_t);assert(read_d==d && read_a[7]==255 && read_t[2]==16000);
 /* ZL must remain held while ZR changes and either stick moves. */
 for(unsigned step=0;step<256;step++) {
  d=0;a[6]=255;a[7]=(uint8_t)step;
  t[0]=(int16_t)(step*257-32768);t[2]=(int16_t)(32767-step*257);
  xbox_platform_input_filter(0,&d,a,t);
  buffy_aim_input_snapshot(&read_d,read_a,read_t);
  assert(a[6]==255 && a[7]==step && read_a[6]==255 && read_a[7]==step);
  assert(read_t[0]==t[0] && read_t[2]==t[2]);
 }
 puts("PASS: aim diagnostics preserve real buttons, both triggers and both sticks; second port isolated");
}
