"""Compile the actual guest wrapper against a tiny register/memory harness."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "app/src/buffy_overrides.c").read_text()
wrapper = re.search(r"^void sub_00022F40\(void\)\n\{.*?^\}", source, re.M | re.S)
assert wrapper, "pause image wrapper missing"
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t memory[256], esp, eax;
static int paused, forwarded;
#define MEM32(a) memory[(a)/4]
static const char *xbox_platform_pause_option(void) { return paused ? "pause" : NULL; }
static void sub_00022F40_gen(void) { assert(esp==128);forwarded++;esp+=36;eax=1; }
''' + wrapper.group(0) + r'''
int main(void) {
    unsetenv("GAME_TRACE_GUI_IMAGE");
    paused=1;esp=128;eax=99;MEM32(esp)=0x12345678;MEM32(esp+8)=0;
    sub_00022F40();assert(esp==164 && eax==0 && !forwarded);assert(MEM32(128)==0x12345678);
    paused=1;esp=128;MEM32(esp+8)=0x400;
    sub_00022F40();assert(esp==164 && eax==1 && forwarded==1);assert(MEM32(136)==0x400);
    paused=0;esp=128;MEM32(esp+8)=0;
    sub_00022F40();assert(esp==164 && eax==1 && forwarded==2);
    puts("PASS: null pause image skipped with exact guest stack cleanup; valid images and untextured gameplay forwarded");
}
'''
with tempfile.TemporaryDirectory(prefix="buffy-pause-abi-") as tmp:
    exe = str(Path(tmp) / "test")
    subprocess.run([os.environ.get("CC", "cc"), "-x", "c", "-O2", "-o", exe, "-"], input=harness, text=True, check=True)
    subprocess.run([exe], check=True)
