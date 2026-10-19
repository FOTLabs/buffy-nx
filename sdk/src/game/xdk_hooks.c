/*
 * Runtime bodies for XDK library hooks (tools.xdk_hooks).
 *
 * Compiled by the game project, not the runtime libraries: the bodies use the
 * lifted register model (recomp_types.h from the game's --gen-dir). Each hook
 * is reached from generated glue that tools.xdk_hooks writes per title, after
 * resolving the XDK symbol to that title's address -- nothing here knows an
 * address. `orig` is the lifted original (sub_X_gen); a hook that replaces
 * the function outright ignores it. `va` is where the function is in this
 * title, for hooks that decode a global or a field offset from its code.
 *
 * Add a body here only when a title needs it, and name the title(s) that did.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"   /* register model (recomp_types.h) + accessors */

typedef void (*xdk_fn)(void);

/* ── crt_memmove: MSVC CRT memmove/memcpy ──────────────────────────────────
 * Its tail and backward-copy paths dispatch through jump tables the lifter
 * cannot follow: indirect tail calls into the middle of the function
 * ("[ICALL] Failed to resolve VA" just past its start) and copies whose
 * length is not a multiple of 4 losing their tail. Seen in a reference title, Conker
 * (0x0046E670) and Buffy (0x00580200, first ICALL failure at boot).
 * Replaced by the host memmove: cdecl (dst, src, count), returns dst. */
void xdk_hook_crt_memmove(xdk_fn orig, uint32_t va)
{
    uint32_t dst = MEM32(esp + 4);
    uint32_t src = MEM32(esp + 8);
    uint32_t n   = MEM32(esp + 12);

    (void)orig; (void)va;
    if (n)
        memmove((void *)XBOX_PTR(dst), (const void *)XBOX_PTR(src), n);
    eax = dst;
    esp += 4;   /* return address; cdecl, the caller pops the arguments */
}

/* ── dsound_gp_parse_effect_image / dsound_gp_commit_changes ───────────────
 * DirectSound CMcpxGPDspManager, thiscall (ecx = the manager). Both post a
 * command to the GP DSP through the word at scratch + 0x810, scratch =
 * ***(this + 8 + 0x10), and spin until the DSP zeroes it. There is no DSP:
 * register the word with the runtime's completion list (xbox_ApuDspAckWord)
 * before the original body. Same layout in XDK 4432 (Buffy 0x005A9A3C,
 * 0x005A9BD8) and 5849 (Conker 0x004C763D/0x004C77D9, a reference title 0x0032EB65). */
extern int xbox_ApuDspAckWord(uint32_t va);

static void dsp_ack_register(uint32_t self)
{
    uint32_t obj = MEM32(self + 8);
    uint32_t blk = obj ? MEM32(obj + 0x10) : 0;
    uint32_t scratch = blk ? MEM32(blk) : 0;

    if (scratch)
        xbox_ApuDspAckWord(scratch + 0x810);
}

void xdk_hook_dsound_gp_parse_effect_image(xdk_fn orig, uint32_t va)
{
    (void)va;
    dsp_ack_register(ecx);
    orig();
}

void xdk_hook_dsound_gp_commit_changes(xdk_fn orig, uint32_t va)
{
    (void)va;
    dsp_ack_register(ecx);
    orig();
}

/* ── d3d_block_on_time: D3D::BlockOnTime(fence, flags) ─────────────────────
 * D3D's fence wait. The pushbuffer executor performs the GPU's semaphore
 * releases (BACK_END_WRITE_SEMAPHORE_RELEASE) and must know the semaphore
 * word: D3D keeps a pointer to it in its device. Both the device global and
 * that field's offset differ by XDK version, and both are the first thing
 * BlockOnTime loads, so read them from its code instead of a table:
 *
 *     mov r32, [abs32]          8B /r (mod 00, rm 101)  the device global
 *     mov eax, [r32 + d8]       8B 40+r d8              the semaphore pointer field
 *     mov ecx, [eax]            8B 08                   ...dereferenced
 *
 * Buffy (4432): 8B 3D <0x5A29A0> / 8B 47 34 / 8B 08 -- field +0x34.
 * Conker (5849): 8B 35 <0x5499E8> / 8B 46 30 / 8B 08 -- field +0x30. */
extern void nv2a_pb_set_semaphore_target(uint32_t guest_va);

static int bot_decode(uint32_t va, uint32_t *dev_global, uint32_t *sem_off)
{
    uint32_t i, j;
    for (i = 0; i < 12; i++) {
        uint8_t m = MEM8(va + i + 1);
        if (MEM8(va + i) != 0x8B || (m & 0xC7) != 0x05)
            continue;
        *dev_global = MEM32(va + i + 2);
        for (j = i + 6; j < i + 24; j++) {
            uint8_t m2 = MEM8(va + j + 1);
            if (MEM8(va + j) == 0x8B && (m2 & 0xC0) == 0x40 && (m2 & 7) == ((m >> 3) & 7)
                    && MEM8(va + j + 3) == 0x8B && (MEM8(va + j + 4) & 0xC7) == ((m2 >> 3) & 7)) {
                *sem_off = MEM8(va + j + 2);
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

void xdk_hook_d3d_block_on_time(xdk_fn orig, uint32_t va)
{
    static int state;                   /* 0 untried, 1 decoded, -1 failed */
    static uint32_t dev_global, sem_off, registered;
    if (!state) {
        state = bot_decode(va, &dev_global, &sem_off) ? 1 : -1;
        if (state > 0)
            fprintf(stderr, "[xdk] BlockOnTime 0x%08X: device at [0x%08X], semaphore pointer +0x%X\n",
                    va, dev_global, sem_off);
        else
            fprintf(stderr, "[xdk] BlockOnTime 0x%08X: prologue not recognised; GPU fences will not "
                    "complete (add this XDK's pattern to xdk_hooks.c)\n", va);
    }
    if (state > 0) {
        uint32_t dev = MEM32(dev_global);
        uint32_t sem = dev ? MEM32(dev + sem_off) : 0;
        if (sem && sem != registered) {
            nv2a_pb_set_semaphore_target(sem);
            fprintf(stderr, "[xdk] GPU semaphore at 0x%08X\n", sem);
            registered = sem;
        }
    }
    orig();
}

/* ── XInput tracing (RECOMP_TRACE_XINPUT=1) ────────────────────────────────
 * XInputOpen(type, port, slot, polling) -> handle; XGetDevices(type) -> port
 * bit mask; XInputGetState(handle, state) -> status, state = {PacketNumber,
 * wButtons, 8 analog bytes, 4 thumb shorts}. All stdcall, in XPP. Nothing is
 * changed: this answers "does the title see the pad at all" (Buffy's title
 * screen ignored Start on every root port). */
static int xinput_trace(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_TRACE_XINPUT");
        on = e && *e == '1';
    }
    return on;
}

void xdk_hook_xapi_xinput_open(xdk_fn orig, uint32_t va)
{
    uint32_t type = MEM32(esp + 4), port = MEM32(esp + 8), slot = MEM32(esp + 12);
    (void)va;
    orig();
    if (xinput_trace())
        fprintf(stderr, "[XINPUT] XInputOpen(type 0x%08X, port %u, slot %u) -> 0x%08X\n",
                type, port, slot, eax);
}

void xdk_hook_xapi_xget_devices(xdk_fn orig, uint32_t va)
{
    static uint32_t last = 0xFFFFFFFFu;
    uint32_t type = MEM32(esp + 4);
    (void)va;
    orig();
    if (xinput_trace() && eax != last) {
        fprintf(stderr, "[XINPUT] XGetDevices(type 0x%08X) -> ports 0x%X\n", type, eax);
        last = eax;
    }
}

void xdk_hook_xapi_xinput_get_state(xdk_fn orig, uint32_t va)
{
    static unsigned n;
    static uint32_t last_buttons = 0xFFFFFFFFu, last_status = 0xFFFFFFFFu;
    static uint32_t last_l = 0xFFFFFFFFu, last_r = 0xFFFFFFFFu;
    uint32_t handle = MEM32(esp + 4), state = MEM32(esp + 8);
    (void)va;
    orig();
    if (xinput_trace()) {
        uint32_t buttons = state ? MEM16(state + 4) | (MEM8(state + 6) << 16) | (MEM8(state + 7) << 24) : 0;
        uint32_t left = state ? MEM32(state + 14) : 0, right = state ? MEM32(state + 18) : 0;
        n++;
        if (eax != last_status || buttons != last_buttons || left != last_l || right != last_r || (n & 4095) == 1)
            fprintf(stderr, "[XINPUT] GetState #%u(handle 0x%08X) -> 0x%X packet %u buttons 0x%08X axes %d %d %d %d\n",
                    n, handle, eax, state ? MEM32(state) : 0, buttons,
                    state ? (int16_t)MEM16(state + 14) : 0, state ? (int16_t)MEM16(state + 16) : 0,
                    state ? (int16_t)MEM16(state + 18) : 0, state ? (int16_t)MEM16(state + 20) : 0);
        last_status = eax;
        last_buttons = buttons;
        last_l = left; last_r = right;
    }
}

/* ── d3d_vblank: D3D::CMiniport::VBlank ────────────────────────────────────
 * Ends by writing PCRTC_INTR and spinning until PMC_INTR bit 24 drops. Its
 * callers (the D3D DPC, D3D busy-waits) have read the bits already, so this
 * is the vblank being taken: clear them first (xbox_Nv2aVblankTaken) and the
 * spin ends at once. The register page is plain memory, so without this the
 * spin ran on the timer thread at DISPATCH holding the guest lock -- Buffy:
 * 65% of the game thread waiting, sound threads starved into a shutdown race.
 * Run at DISPATCH (the dispatch lock) so the DPC and a busy-wait never
 * interleave (a flip retires only when the vblank count matches exactly).
 * Conker (0x00540F70) and Buffy (0x00599B50). */
extern uint8_t xbox_KfRaiseIrql(uint8_t irql);
extern void xbox_KfLowerIrql(uint8_t irql);
extern uint8_t xbox_CurrentIrql(void);
extern int xbox_gil_suspend(void);
extern void xbox_gil_resume(int depth);
extern void xbox_Nv2aVblankTaken(void);

void xdk_hook_d3d_vblank(xdk_fn orig, uint32_t va)
{
    uint8_t old = xbox_CurrentIrql();
    (void)va;
    if (old < 2) {
        int d = xbox_gil_suspend();
        old = xbox_KfRaiseIrql(2);
        xbox_gil_resume(d);
    }
    xbox_Nv2aVblankTaken();
    orig();
    if (old < 2)
        xbox_KfLowerIrql(old);
}
