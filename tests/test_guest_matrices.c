/* Link into initialized Buffy with --wrap=xbe_entry_point.
 * Golden hash captured from the original, unmodified generated routines.
 * Includes 18,000 matrix/transform cases and overlapping destinations. */
#include "recomp_funcs.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static uint64_t hash=1469598103934665603ULL;
static void digest(const volatile void*p,size_t n){const volatile unsigned char*b=p;while(n--){hash^=*b++;hash*=1099511628211ULL;}}
static uint32_t random_state=0x79421;
static float value(void){random_state=random_state*1664525u+1013904223u;return (float)(int32_t)(random_state>>8)/65536.0f-128.0f;}
static __attribute__((noinline,optimize("fp-contract=off"))) void native_transform(float out[3],const float m[12],const float v[3]){
 for(unsigned c=0;c<3;c++){float a=v[0]*m[c],b=v[1]*m[c+3],d=v[2]*m[c+6];float ab=a+b,dt=d+m[c+9];out[c]=ab+dt;}
}
void __wrap_xbe_entry_point(void){
 uint32_t base=g_esp,m=base-512,v=base-256,out=base-128;unsigned bad=0;
 for(unsigned i=0;i<10000;i++){
  float matrix[12],vector[3],expected[3];for(unsigned k=0;k<12;k++)MEMF(m+4*k)=matrix[k]=value();for(unsigned k=0;k<3;k++)MEMF(v+4*k)=vector[k]=value();
  native_transform(expected,matrix,vector);
  g_esp=base-16;MEM32(g_esp)=0;MEM32(g_esp+4)=out;MEM32(g_esp+8)=m;MEM32(g_esp+12)=v;sub_002A94C0();
  for(unsigned k=0;k<3;k++){float result=MEMF(out+4*k);if(memcmp(&result,&expected[k],4)){if(bad<3)fprintf(stderr,"[transform-test] mismatch %u/%u %.9g %.9g\n",i,k,result,expected[k]);bad++;}}
  if(g_esp!=base)bad++;
 }
 /* Compare guest memory outputs with the unchanged generated object,
    including overlapping matrix operands and final SSE registers. */
 for(unsigned routine=0;routine<2;routine++)for(unsigned alias=0;alias<3;alias++)for(unsigned i=0;i<3000;i++){
  for(unsigned k=0;k<12;k++){MEMF(m+4*k)=value();MEMF(v+4*k)=value();MEMF(out+4*k)=value();}
  g_xmm0=(RecompXmm){0};g_xmm1=(RecompXmm){0};g_xmm2=(RecompXmm){0};g_xmm3=(RecompXmm){0};
  g_xmm4=(RecompXmm){0};g_xmm5=(RecompXmm){0};g_xmm6=(RecompXmm){0};g_xmm7=(RecompXmm){0};
  uint32_t dest=alias==1?m:alias==2?v:out;
  g_esp=base-16;MEM32(g_esp)=0;MEM32(g_esp+4)=dest;MEM32(g_esp+8)=m;MEM32(g_esp+12)=v;
  if(routine)sub_002A9230();else sub_002A94C0();
  if(g_esp!=base)bad++;
  digest(&MEM32(m),48);digest(&MEM32(v),48);digest(&MEM32(out),48);
  digest(&g_xmm0,16);digest(&g_xmm1,16);digest(&g_xmm2,16);digest(&g_xmm3,16);
  digest(&g_xmm4,16);digest(&g_xmm5,16);digest(&g_xmm6,16);digest(&g_xmm7,16);
 }
 fprintf(stderr,"[transform-test] output/SSE hash=%016llx\n",(unsigned long long)hash);
 if(hash!=0x57ae54e46253a1f8ULL)bad++;
 for(unsigned routine=0;routine<2;routine++){
  struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
  for(unsigned i=0;i<1000000;i++){
   g_esp=base-16;MEM32(g_esp)=0;MEM32(g_esp+4)=out;MEM32(g_esp+8)=m;MEM32(g_esp+12)=v;
   if(routine)sub_002A9230();else sub_002A94C0();
  }
  clock_gettime(CLOCK_MONOTONIC,&b);
  fprintf(stderr,"[transform-test] routine=%u million calls %.3f ms\n",routine,(b.tv_sec-a.tv_sec)*1000.0+(b.tv_nsec-a.tv_nsec)/1e6);
 }
 fprintf(stderr,"[transform-test] 10000 cases, bit mismatches=%u\n",bad);fflush(stderr);_Exit(bad?1:0);
}
