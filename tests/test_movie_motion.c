/* Link with --wrap=xbe_entry_point into the initialized game. Compare native
 * movie kernels with the actual translated routines and retail constants. */
#include "recomp_funcs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
extern void buffy_movie_mc_half(void), buffy_movie_mc_copy(void);
static uint32_t seed = 12345;
static uint8_t random_byte(void) { seed = seed * 1664525u + 1013904223u; return (uint8_t)(seed >> 24); }
void __wrap_xbe_entry_point(void)
{
    uint32_t top = g_esp, stack = top - 128, src = top - 16000, dst = top - 8000;
    uint8_t expected[4096];
    for (unsigned n = 0; n < 4000; n++) {
        int stride = (int[]){16,24,31,64,-24,-64}[n % 6];
        unsigned offset = n % 8;
        uint32_t s = src + 2048 + offset, d = dst + 2048 + offset;
        for (unsigned i = 0; i < 4096; i++) MEM8(src + i) = random_byte();
        memset((void *)XBOX_PTR(dst), 0xA5, 4096);
        g_esp = stack; MEM32(stack)=0; MEM32(stack+4)=src;
        MEM32(stack+8)=d; MEM32(stack+12)=s; MEM32(stack+16)=(uint32_t)stride;
        MEM32(stack+20)=(n & 1) ? 7 : 0; MEM32(stack+24)=(n & 2) ? 9 : 0; MEM32(stack+28)=n;
        sub_005F73A2();
        if(g_esp != stack+32) _Exit(2);
        memcpy(expected, (void *)XBOX_PTR(dst), sizeof expected);
        memset((void *)XBOX_PTR(dst), 0xA5, sizeof expected); g_esp = stack;
        buffy_movie_mc_half();
        if(g_esp != stack+32 || memcmp(expected,(void *)XBOX_PTR(dst),sizeof expected)) {
            fprintf(stderr,"motion half mismatch case %u stride %d\n",n,stride); _Exit(3);
        }
        memset((void *)XBOX_PTR(dst), 0xA5, 4096);
        g_esp=stack; MEM32(stack+4)=dst+1024+offset; MEM32(stack+8)=dst+2304+offset; MEM32(stack+12)=dst+3328+offset;
        MEM32(stack+16)=src+1024+offset; MEM32(stack+20)=src+2304+offset; MEM32(stack+24)=src+3328+offset;
        MEM32(stack+28)=(uint32_t)stride; MEM32(stack+32)=(uint32_t)stride;
        sub_005F6F71(); if(g_esp != stack+36) _Exit(4);
        memcpy(expected,(void *)XBOX_PTR(dst),sizeof expected);
        memset((void *)XBOX_PTR(dst),0xA5,sizeof expected); g_esp=stack;
        buffy_movie_mc_copy();
        if(g_esp != stack+36 || memcmp(expected,(void *)XBOX_PTR(dst),sizeof expected)) _Exit(5);
    }
    if (getenv("MOVIE_BENCH")) {
        MEM32(stack+8)=dst+2048; MEM32(stack+12)=src+2048;
        MEM32(stack+16)=32; MEM32(stack+20)=0; MEM32(stack+24)=0;
        for (unsigned kind=0; kind<2; kind++) {
            struct timespec a,b; clock_gettime(CLOCK_MONOTONIC,&a);
            for (unsigned i=0;i<300000;i++) { g_esp=stack; if(kind) buffy_movie_mc_half(); else sub_005F73A2(); }
            clock_gettime(CLOCK_MONOTONIC,&b);
            fprintf(stderr,"movie half %s: %.3f ms / 300000 calls\n",kind?"native":"translated",
                (b.tv_sec-a.tv_sec)*1000.0+(b.tv_nsec-a.tv_nsec)/1e6);
        }
    }
    fprintf(stderr,"movie motion: 8000 translated/native comparisons passed\n");
    fflush(stderr); _Exit(0);
}
