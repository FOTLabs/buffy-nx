/* Link into the normal Buffy executable with -Wl,--wrap=xbe_entry_point.
 * Runs the original translated MASin/CRT code against initialized XBE data.
 * Unlike a host-only math test, this catches wrong guest operand widths.
 */
#include "recomp_funcs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
void __wrap_xbe_entry_point(void) {
 double vals[]={0,.001,.05,.2,.5,.99,1,-.2,-1};int bad=0;
 uint32_t base=g_esp;
 for(unsigned i=0;i<sizeof(vals)/sizeof(vals[0]);i++) {
  g_esp=base-8;MEM32(g_esp)=0;MEMF(g_esp+4)=(float)vals[i];g_fp_top=0;
  sub_002A9740();double result=g_fp_stack[g_fp_top];
  double expected=asin((float)vals[i]);
  fprintf(stderr,"[math-probe] asin %.9g = %.12g expected %.12g top=%u esp=%X\n",vals[i],result,expected,g_fp_top,g_esp);
  if(!isfinite(result) || fabs(result-expected)>1e-6 || g_esp!=base)bad++;
 }
 fprintf(stderr,"[math-probe] failures=%d\n",bad);fflush(stderr);_Exit(bad?1:0);
}
