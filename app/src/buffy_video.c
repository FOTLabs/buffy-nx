/* Native equivalents of the movie decoder's byte-copy and half-pixel
 * interpolation kernels. Integer rounding matches the translated MMX path;
 * fewer emulated instructions leave more time to refill movie audio. */
#include "recomp_types.h"
#include <stdint.h>
#include <string.h>

void buffy_movie_mc_half(void)
{
    uint32_t stack = g_esp;
    uint8_t *dst = (uint8_t *)XBOX_PTR(MEM32(stack + 8));
    const uint8_t *src = (const uint8_t *)XBOX_PTR(MEM32(stack + 12));
    int32_t stride = (int32_t)MEM32(stack + 16);
    int horizontal = MEM32(stack + 20) == 0;
    int vertical = MEM32(stack + 24) == 0;
    for (unsigned row = 0; row < 8; row++) {
        uint8_t a[8], b[8], c[8], d[8], out[8];
        memcpy(a, src, 8);
        if (horizontal) memcpy(b, src + 1, 8);
        if (vertical) memcpy(c, src + stride, 8);
        if (horizontal && vertical) memcpy(d, src + stride + 1, 8);
        for (unsigned x = 0; x < 8; x++) {
            unsigned value = a[x];
            if (horizontal && vertical) value = (value + b[x] + c[x] + d[x] + 1) >> 2;
            else if (horizontal) value = (value + b[x]) >> 1;
            else if (vertical) value = (value + c[x]) >> 1;
            out[x] = (uint8_t)value;
        }
        memcpy(dst, out, 8);
        src += stride;
        dst += stride;
    }
    g_esp = stack + 32;
}

void buffy_movie_mc_copy(void)
{
    uint32_t stack = g_esp;
    uint8_t *y = (uint8_t *)XBOX_PTR(MEM32(stack + 4));
    uint8_t *u = (uint8_t *)XBOX_PTR(MEM32(stack + 8));
    uint8_t *v = (uint8_t *)XBOX_PTR(MEM32(stack + 12));
    const uint8_t *sy = (const uint8_t *)XBOX_PTR(MEM32(stack + 16));
    const uint8_t *su = (const uint8_t *)XBOX_PTR(MEM32(stack + 20));
    const uint8_t *sv = (const uint8_t *)XBOX_PTR(MEM32(stack + 24));
    int32_t ys = (int32_t)MEM32(stack + 28), cs = (int32_t)MEM32(stack + 32);
    for (unsigned row = 0; row < 16; row++) {
        uint64_t word;
        memcpy(&word, sy, 8); memcpy(y, &word, 8);
        memcpy(&word, sy + 8, 8); memcpy(y + 8, &word, 8);
        sy += ys; y += ys;
    }
    for (unsigned row = 0; row < 8; row++) {
        uint64_t word;
        memcpy(&word, su, 8); memcpy(u, &word, 8);
        memcpy(&word, sv, 8); memcpy(v, &word, 8);
        su += cs; sv += cs; u += cs; v += cs;
    }
    g_esp = stack + 36;
}
