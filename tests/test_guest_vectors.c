/* Link with --wrap=xbe_entry_point into the initialized Buffy executable.
 * Exercise its actual SSE Vector3Float_Normalize, not a host replacement. */
#include "recomp_funcs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
void __wrap_xbe_entry_point(void) {
 const float inputs[][3]={{3,4,0},{1,2,2},{2,-3,6},{-4,0,3}};
 unsigned failures=0;uint32_t base=g_esp,vector=base-128;
 for(unsigned i=0;i<sizeof(inputs)/sizeof(inputs[0]);i++) {
  double length=0;for(unsigned k=0;k<3;k++){MEMF(vector+k*4)=inputs[i][k];length+=(double)inputs[i][k]*inputs[i][k];}
  length=sqrt(length);g_esp=base-8;MEM32(g_esp)=0;MEM32(g_esp+4)=vector;
  sub_002A9850();
  fprintf(stderr,"[vector-probe] %.3g %.3g %.3g -> %.8g %.8g %.8g\n",inputs[i][0],inputs[i][1],inputs[i][2],MEMF(vector),MEMF(vector+4),MEMF(vector+8));
  for(unsigned k=0;k<3;k++) if(!isfinite(MEMF(vector+k*4)) || fabs(MEMF(vector+k*4)-inputs[i][k]/length)>1e-6) failures++;
  if(g_esp!=base-4)failures++;
 }
 fprintf(stderr,"[vector-probe] failures=%u\n",failures);fflush(stderr);_Exit(failures?1:0);
}
