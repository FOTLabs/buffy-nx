#include "../app/src/buffy_rumble.c"
#include <assert.h>

int main(void)
{
 float low,high,lf,hf,previous_low=0;
 unsetenv("GAME_RUMBLE_STRENGTH");
 low=high=1; xbox_platform_rumble_filter(0,2,&low,&high,&lf,&hf);
 assert(fabsf(low+high-0.55f)<1e-6f && low>high);
 assert(low<0.4f && high<0.25f && lf==135 && hf==285);
 low=high=0; xbox_platform_rumble_filter(0,0,&low,&high,&lf,&hf);
 assert(low==0 && high==0); /* Stop is immediate, without an envelope tail. */
 for(unsigned side=0;side<3;side++)for(unsigned i=0;i<=256;i++) {
  rumble_map((float)i/256,0,0.55f,side,&low,&high,&lf,&hf);
  if(i)assert(low>=previous_low);previous_low=low;
  assert(high==0);
  for(unsigned j=0;j<=256;j++) {
   rumble_map((float)i/256,(float)j/256,0.55f,side,&low,&high,&lf,&hf);
   assert(isfinite(low)&&isfinite(high)&&low>=0&&high>=0&&low+high<=0.550001f);
   assert(lf>=120&&lf<=160&&hf>=250&&hf<=320);
   if(!i)assert(low==0);if(!j)assert(high==0);
  }
 }
 rumble_map(0.1f,0,0.55f,2,&low,&high,&lf,&hf);float small_ratio=low/0.1f;
 assert(lf==120&&hf==250);
 rumble_map(1,0,0.55f,2,&low,&high,&lf,&hf);assert(small_ratio>low);
 rumble_map(0,1,0.55f,2,&low,&high,&lf,&hf);assert(lf==150&&hf==320);
 rumble_map(1,1,0,2,&low,&high,&lf,&hf);assert(low==0&&high==0);
 rumble_map(NAN,INFINITY,0.55f,2,&low,&high,&lf,&hf);assert(low==0&&high==0);
 float left_low,left_high;
 rumble_map(1,1,0.55f,0,&left_low,&left_high,&lf,&hf);
 rumble_map(1,1,0.55f,1,&low,&high,&lf,&hf);
 assert(left_low>low&&left_high<high);
 float again_low,again_high,again_lf,again_hf;
 rumble_map(1,1,0.55f,1,&again_low,&again_high,&again_lf,&again_hf);
 assert(again_low==low&&again_high==high&&again_lf==lf&&again_hf==hf);
 puts("PASS: HD rumble strength cap, band separation, grip balance, stable carriers, small cues and immediate stop");
 return 0;
}
