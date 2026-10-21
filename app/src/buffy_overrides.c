/*
 * Buffy-specific overrides -- game bugs, not missing framework behaviour.
 * Each entry names the function (final.map) and why it is here.
 *
 * Built on the lifted register model, like the framework's xdk_hooks.c.
 */
#include <stdint.h>
#include <stddef.h>
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include <stdio.h>

/* ── File_Read(const TFileName&, void*& buf, long& size, long) (0x002B46D0) ──
 * Returns false *without writing buf or size* when the file does not exist.
 * TConsole::LoadVariables (0x00014030) ignores the result: it compares
 * size - 0x18 and finally delete[]s buf -- its own uninitialised locals. On
 * the console those slots evidently hold harmless values (the title runs
 * there on a fresh hard disk, where TDATA\45410012\vars.cfg does not exist
 * yet). Here they held stack leftovers: a pointer into the XBE image was
 * freed, RtlpDeCommitFreeBlock decommitted 0x580000.. and the heap code then
 * zeroed live CRT code (_output's switch table) -> jumps to 0 after the
 * attract video. Give the outputs the empty-file values first. */
extern void sub_002B46D0_gen(void);
void sub_002B46D0(void)
{
    uint32_t pbuf = MEM32(esp + 8), psize = MEM32(esp + 12);
    if (pbuf)
        MEM32(pbuf) = 0;
    if (psize)
        MEM32(psize) = 0;
    sub_002B46D0_gen();
}

/* ── TIF_Render_Implement::Movie_Play (0x00297A90), GAME_SKIP_MOVIES=1 ──────
 * thiscall (TFileName const&, bool, bool, bool, void*) -> movie handle, ret
 * 0x14. Returning no movie makes every logo/attract movie end at once, the
 * way a missing file would. Bring-up/test speed only (the movies themselves
 * play: WMVDEC is lifted and works, it is just slow on lavapipe). */
#include <stdlib.h>

/* TGO::GetPlayer reads this singleton's +0xF0. Read under the guest lock. */
extern void xa2_set_frontend(int active);
extern void xa2_set_frontend_boost(float gain);
static void buffy_frontend_audio(void)
{
    uint32_t game = MEM32(0x7D0090);
    int frontend = !game || !MEM32(game + 0xF0);
    static int last = -1;
    if (frontend != last) {
        last = frontend;
        const char *boost = getenv("GAME_FRONTEND_BOOST");
        xa2_set_frontend_boost(boost ? (float)atof(boost) : 1.41421356f);
        xa2_set_frontend(frontend);
        if (getenv("GAME_TRACE_AUDIO_CONFIG"))
            fprintf(stderr, "[audio-config] frontend %d\n", frontend);
    }
}
extern void sub_00297A90_gen(void);
void sub_00297A90(void)
{
    buffy_frontend_audio();
    static int skip = -1;
    if (skip < 0) {
        const char *e = getenv("GAME_SKIP_MOVIES");
        skip = e && *e == '1';
    }
    if (skip) {
        eax = 0;
        esp += 4 + 0x14;
        return;
    }
    sub_00297A90_gen();
}

/* ── MultiThread_Callback (0x000670C0): sound-buffer worker, bring-up trace ── */
#include <stdio.h>
extern void sub_000670C0_gen(void);
void sub_000670C0(void)
{
    if (getenv("GAME_TRACE_SNDWORKER")) {
        uint32_t obj = MEM32(esp + 4), vt = obj ? MEM32(obj) : 0;
        fprintf(stderr, "[snd] worker start obj 0x%08X vtable 0x%08X [+0x4C]=0x%08X state %u\n",
                obj, vt, vt ? MEM32(vt + 0x4C) : 0, obj ? MEM32(obj + 0x40) : 0);
#if !defined(__SWITCH__) && !defined(_WIN32)
        if (getenv("GAME_TRACE_SNDWATCH")) {
            extern void game_watch_now(uint32_t va, uint32_t len);
            game_watch_now(obj, 4);
        }
#endif
    }
    sub_000670C0_gen();
}

/* Stop waits for a buffer in state 1 to finish its background preparation.
 * Trace entry/return without bypassing the guest wait or altering its state. */
extern void sub_00066F80_gen(void);
void sub_00066F80(void)
{
    uint32_t obj = ecx;
    int trace = getenv("GAME_TRACE_SNDWORKER") != NULL;
    if (trace)
        fprintf(stderr, "[snd] stop obj %08X state %u vtable %08X from %08X\n",
                obj, MEM32(obj + 0x40), MEM32(obj), MEM32(esp));
    sub_00066F80_gen();
    if (trace)
        fprintf(stderr, "[snd] stop returned obj %08X state %u\n",
                obj, MEM32(obj + 0x40));
}

/* ── sound-buffer lifecycle trace (GAME_TRACE_SNDWORKER=1, bring-up) ── */
extern void sub_00066F10_gen(void);
void sub_00066F10(void)                    /* CMultiThreadSoundBuffer::ReleaseSoundBuffer */
{
    if (getenv("GAME_TRACE_SNDWORKER"))
        fprintf(stderr, "[snd] release obj 0x%08X state %u (from 0x%08X)\n",
                ecx, MEM32(ecx + 0x40), MEM32(esp));
    sub_00066F10_gen();
    if (getenv("GAME_TRACE_SNDWORKER"))
        fprintf(stderr, "[snd] release done\n");
}
extern void sub_000684A0_gen(void);
void sub_000684A0(void)                    /* CMultiThreadSoundBuffer deleting dtor */
{
    if (getenv("GAME_TRACE_SNDWORKER"))
        fprintf(stderr, "[snd] delete obj 0x%08X state %u (from 0x%08X)\n",
                ecx, MEM32(ecx + 0x40), MEM32(esp));
    sub_000684A0_gen();
}
extern void sub_00066CF0_gen(void);
void sub_00066CF0(void)                    /* MultiThread_Commit (trace) */
{
    uint32_t in_esi = esi, in_ecx = ecx;
    sub_00066CF0_gen();
    if (getenv("GAME_TRACE_SNDWORKER")) {
        static unsigned n;
        if (n++ < 6 || esi != in_esi)
            fprintf(stderr, "[snd] commit this 0x%08X esi 0x%08X -> 0x%08X ret %u\n",
                    in_ecx, in_esi, esi, eax & 0xFF);
    }
}

/* Callee-saved register check (bring-up): which callee returns with esi/edi/ebx changed. */
static void abi_report(const char *name, uint32_t a, uint32_t b, uint32_t c)
{
    static int n;
    if ((esi != a || edi != b || ebx != c) && n++ < 8)
        fprintf(stderr, "[abi] %s clobbered esi %08X->%08X edi %08X->%08X ebx %08X->%08X\n",
                name, a, esi, b, edi, c, ebx);
}
extern void sub_002AA870_gen(void);
void sub_002AA870(void)
{
    uint32_t a = esi, b = edi, c = ebx;
    sub_002AA870_gen();
    abi_report("CLocker::Lock", a, b, c);
}
extern void sub_002AA950_gen(void);
void sub_002AA950(void)
{
    uint32_t a = esi, b = edi, c = ebx;
    sub_002AA950_gen();
    abi_report("CLocker::Unlock", a, b, c);
}
extern void sub_002B4500_gen(void);
void sub_002B4500(void)
{
    uint32_t a = esi, b = edi, c = ebx;
    sub_002B4500_gen();
    abi_report("File_Rename", a, b, c);
}
extern void sub_002B4570_gen(void);
void sub_002B4570(void)
{
    uint32_t a = esi, b = edi, c = ebx;
    sub_002B4570_gen();
    abi_report("File_Copy", a, b, c);
}
extern void sub_002B45F0_gen(void);
void sub_002B45F0(void)
{
    uint32_t a = esi, b = edi, c = ebx;
    sub_002B45F0_gen();
    abi_report("File_Delete", a, b, c);
}
extern void sub_002B4670_gen(void);
void sub_002B4670(void)
{
    uint32_t a = esi, b = edi, c = ebx;
    sub_002B4670_gen();
    abi_report("File_Exists", a, b, c);
}
extern void sub_002B6D90_gen(void);
void sub_002B6D90(void)
{
    uint32_t a = esi, b = edi, c = ebx;
    sub_002B6D90_gen();
    abi_report("GetFullFilePath", a, b, c);
}
extern void sub_00068810_gen(void);
void sub_00068810(void)
{
    uint32_t a = esi, b = edi, c = ebx;
    sub_00068810_gen();
    abi_report("CMusicBufferDS::MT_CreateSoundBuffer", a, b, c);
}
extern void sub_00068760_gen(void);
void sub_00068760(void)
{
    uint32_t a = esi, b = edi, c = ebx;
    sub_00068760_gen();
    abi_report("CMusicBufferDS::MT_Commit", a, b, c);
}
extern void sub_00068560_gen(void);
void sub_00068560(void)
{
    uint32_t a = esi, b = edi, c = ebx;
    sub_00068560_gen();
    abi_report("CMusicBufferDS::MT_ReleaseSoundBuffer", a, b, c);
}

/* __RTDynamicCast(inptr, VfDelta, SrcType, TargetType, isRef) trace (GAME_TRACE_RTTI=1) */
extern void sub_0057E837_gen(void);
void sub_0057E837(void)
{
    if (getenv("GAME_TRACE_RTTI")) {
        uint32_t p = MEM32(esp + 4), vt = p ? MEM32(p) : 0;
        uint32_t col = (vt > 0x10000 && vt < 0x4000000) ? MEM32(vt - 4) : 0;
        static unsigned n;
        if (n++ < 3 || !(col > 0x10000 && col < 0x4000000) || MEM32(esp) == 0x0009C8BBu)
            fprintf(stderr, "[rtti] cast obj 0x%08X vtable 0x%08X col 0x%08X target 0x%08X ret 0x%08X\n",
                    p, vt, col, MEM32(esp + 16), MEM32(esp));
    }
    sub_0057E837_gen();
}

/* TIC_Physics::CallPostPhysics(TArray<IObject*>&) bad-entry scan (GAME_TRACE_PHYS=1) */
extern void sub_0009C860_gen(void);
void sub_0009C860(void)
{
    if (getenv("GAME_TRACE_PHYS")) {
        static uint32_t seen[16]; static int ns;
        uint32_t m = MEM32(ecx + 8), vt = m ? MEM32(m) : 0, k, f = vt ? MEM32(vt + 0x1CC) : 0;
        for (k = 0; k < (uint32_t)ns && seen[k] != f; k++) { }
        if (k == (uint32_t)ns && ns < 16 && (seen[ns++] = f, 1))
            fprintf(stderr, "[phys] filler: obj 0x%08X vt 0x%08X [+0x1CC] 0x%08X\n", m, vt, vt ? MEM32(vt + 0x1CC) : 0);
    }
    if (0) {
        static int shown;
        uint32_t arr = MEM32(esp + 4), data = MEM32(arr), n = MEM32(arr + 4), i;
        if (shown < 6 && n < 100000)
            for (i = 0; i < n; i++) {
                uint32_t o = MEM32(data + i * 4), vt = o ? MEM32(o) : 0;
                if (o && !(vt >= 0x616D80 && vt < 0x750EA0) && shown++ < 6)
                    fprintf(stderr, "[phys] arr 0x%08X data 0x%08X n %u [%u] slot 0x%08X = obj 0x%08X vt 0x%08X\n",
                            arr, data, n, i, data + i * 4, o, vt);
            }
    }
    sub_0009C860_gen();
}

/* TArray<T*>::Add(const T&) (0x000AB380, shared by pointer arrays): log appends
 * made from ICharacter::Event_PostPhysics (GAME_TRACE_PHYS=1). */
extern void sub_000AB380_gen(void);
void sub_000AB380(void)
{
    uint32_t ret = MEM32(esp);
    if (ret >= 0x001460F0u && ret < 0x00146800u && getenv("GAME_TRACE_PHYS")) {
        static int shown;
        uint32_t ref = MEM32(esp + 4), v = ref ? MEM32(ref) : 0;
        if (shown++ < 4)
            fprintf(stderr, "[phys] Add arr 0x%08X ref 0x%08X value 0x%08X (vt 0x%08X) ret 0x%08X\n",
                    ecx, ref, v, v ? MEM32(v) : 0, ret);
    }
    sub_000AB380_gen();
}

/* ICharacter::Event_PostPhysics(TEventParams_PostPhysics&) (0x001460F0): params before/after */
extern void sub_001460F0_gen(void);
void sub_001460F0(void)
{
    uint32_t p = MEM32(esp + 4), b0 = MEM32(p), b1 = MEM32(p + 4), b2 = MEM32(p + 8);
    sub_001460F0_gen();
    if (getenv("GAME_TRACE_PHYS")) {
        static int shown;
        if (shown++ < 3)
            fprintf(stderr, "[phys] PostPhysics params 0x%08X: %08X %08X %08X -> %08X %08X %08X\n",
                    p, b0, b1, b2, MEM32(p), MEM32(p + 4), MEM32(p + 8));
    }
}

/* TCharState_Game::LeaveCurrentSequence (0x0014CEC0): which [vt+0x4C0] callee clobbers esi */
extern void sub_0014CEC0_gen(void);
void sub_0014CEC0(void)
{
    if (getenv("GAME_TRACE_PHYS")) {
        static int once;
        uint32_t o = MEM32(MEM32(ecx + 0xEC) + 0x44), f = MEM32(MEM32(o) + 0x4C0);
        if (!once++)
            fprintf(stderr, "[phys] LeaveCurrentSequence this 0x%08X calls 0x%08X\n", ecx, f);
    }
    sub_0014CEC0_gen();
}

/* Stack-balance check (bring-up): ICharacter::InformLeavingSequence (0x0012EA50), ret 4 */
extern void sub_0012EA50_gen(void);
void sub_0012EA50(void)
{
    uint32_t e0 = esp;
    sub_0012EA50_gen();
    if (esp != e0 + 8) {
        static int n;
        if (n++ < 3)
            fprintf(stderr, "[abi] InformLeavingSequence esp delta %d (want 8)\n", (int)(esp - e0));
    }
}

/* Stack-leak check: a call must return with esp >= entry + 4 (bring-up). */
static void leak_report(uint32_t va, uint32_t e0)
{
    static int n;
    if (esp < e0 + 4 && n++ < 6)
        fprintf(stderr, "[abi] 0x%08X leaked: esp delta %d\n", va, (int)(esp - e0));
}
extern void sub_0001CF20_gen(void);
void sub_0001CF20(void)
{
    uint32_t e0 = esp;
    sub_0001CF20_gen();
    leak_report(0x0001CF20u, e0);
}
extern void sub_001386B0_gen(void);
void sub_001386B0(void)
{
    uint32_t e0 = esp;
    sub_001386B0_gen();
    leak_report(0x001386B0u, e0);
}
extern void sub_00138650_gen(void);
void sub_00138650(void)
{
    uint32_t e0 = esp;
    sub_00138650_gen();
    leak_report(0x00138650u, e0);
}
extern void sub_001383D0_gen(void);
void sub_001383D0(void)
{
    uint32_t e0 = esp;
    sub_001383D0_gen();
    leak_report(0x001383D0u, e0);
}

/* Opt-in front-end audio settings trace; preserves the title's arguments. */
extern void sub_000674C0_gen(void);
void sub_000674C0(void)
{
    float requested = MEMF(esp + 4);
    sub_000674C0_gen();
    if (getenv("GAME_TRACE_AUDIO_CONFIG"))
        fprintf(stderr, "[audio-config] music %.6g -> %.6g\n", requested, MEMF(0x787D20));
}
extern void sub_002970C0_gen(void);
void sub_002970C0(void)
{
    float requested = MEMF(esp + 4);
    sub_002970C0_gen();
    if (getenv("GAME_TRACE_AUDIO_CONFIG"))
        fprintf(stderr, "[audio-config] movie %.6g -> %.6g\n", requested, MEMF(0x7715C4));
}

extern void sub_00069350_gen(void);
void sub_00069350(void)
{
    buffy_frontend_audio();
    sub_00069350_gen();
}

/* Opt-in emitter trace, sampled per object to keep manual console runs quiet. */
extern uint32_t nv2a_pb_flip_count(void);
extern void sub_001B25A0_gen(void);
void sub_001B25A0(void)
{
    if (getenv("GAME_TRACE_PARTICLES")) {
        static struct { uint32_t obj, frame; } seen[64];
        uint32_t obj = ecx, frame = nv2a_pb_flip_count(), slot;
        for (slot = 0; slot < 64 && seen[slot].obj && seen[slot].obj != obj; ++slot) {}
        if (slot < 64 && (!seen[slot].obj || frame - seen[slot].frame >= 120)) {
            uint32_t game = MEM32(0x7D0090), player = game ? MEM32(game + 0xF0) : 0;
            uint32_t params = MEM32(obj + 0x274), name = params ? MEM32(params + 0x490) : 0;
            char text[64] = {0};
            if (name >= 0x10000 && (name & 0x7FFFFFFFu) < 0x03FFFFC0)
                for (unsigned i = 0; i < sizeof text - 1; ++i) {
                    text[i] = (char)MEM8(name + i);
                    if (!text[i]) break;
                }
            seen[slot].obj = obj; seen[slot].frame = frame;
            fprintf(stderr, "[particle] f%u obj %08X %s parent %08X lock %08X/%u world %.3f %.3f %.3f player %08X %.3f %.3f %.3f\n",
                frame, obj, text, MEM32(obj + 0x380), MEM32(obj + 0x2A4), MEM8(obj + 0x2E0),
                MEMF(obj + 0x240), MEMF(obj + 0x244), MEMF(obj + 0x248), player,
                player ? MEMF(player + 0x240) : 0, player ? MEMF(player + 0x244) : 0, player ? MEMF(player + 0x248) : 0);
        }
    }
    sub_001B25A0_gen();
}

/* IPauseScreen::SetOpen(bool,bool,bool): observe the accepted state, including
 * calls the game's transition guard rejects. No input is synthesized. */
extern void sub_00230AA0_gen(void);
extern void buffy_pause_visibility(int open);
extern int buffy_pause_duplicate_start(int current,int requested);
extern void buffy_pause_accept_start(void);
void sub_00230AA0(void)
{
    uint32_t self=ecx, caller=MEM32(esp);
    int before=MEM8(self+0x274), requested=MEM8(esp+4);
    if(buffy_pause_duplicate_start(before,requested)) {
        if(getenv("GAME_TRACE_PAUSE"))
            fprintf(stderr,"[pause] duplicate held Start transition rejected caller=%08X\n",caller);
        eax=(eax & 0xFFFFFF00u);esp+=16;return; /* original false, ret 12 */
    }
    sub_00230AA0_gen();
    int after=MEM8(self+0x274);
    if(before!=after) {
        buffy_pause_accept_start();
        fprintf(stderr,"[pause] transition caller=%08X\n",caller);
    }
    buffy_pause_visibility(after);
}

/* GUI::TMenuPause::OnCommand(COMMAND). 0x29 is Start. Its original
 * handler arms +0x4C, and OnRender closes the menu once Start is released.
 * Do not let the press already consumed by gameplay arm that release. */
extern void sub_00223C30_gen(void);
extern int buffy_pause_start_owned(void);
void sub_00223C30(void)
{
    if(MEM32(esp+4)==0x29 && buffy_pause_start_owned()) {
        if(getenv("GAME_TRACE_PAUSE")) fprintf(stderr,"[pause] opening Start GUI command consumed\n");
        esp+=8;return; /* thiscall, ret 4 */
    }
    sub_00223C30_gen();
}

/* TTextureImplementation::Draw2D(TextureVertexInfo&, texture, RGBA, bool, bool).
 * The first pause submits empty GUI images, including its central image,
 * before normal painting. A NULL texture otherwise becomes an opaque white
 * quad. Skip empty pause images at the common textured-image boundary;
 * intentional untextured gameplay rendering keeps its original behaviour. */
extern void sub_00022F40_gen(void);
extern const char *xbox_platform_pause_option(void);
void sub_00022F40(void)
{
    if (!MEM32(esp+8) && xbox_platform_pause_option()) {
        if (getenv("GAME_TRACE_GUI_IMAGE"))
            fprintf(stderr,"[pause-image] skipped null texture caller=%08X\n",MEM32(esp));
        eax=0;
        esp += 36; /* thiscall: return address + 32 bytes */
        return;
    }
    sub_00022F40_gen();
}

/* Opt-in first-person input and math diagnostics (GAME_TRACE_AIM=1).
 * Buffy uses FNSTSW-based comparisons here rather than LAHF. */
extern int buffy_trace_aim_enabled(void);
extern void buffy_aim_input_snapshot(uint16_t *,uint8_t [8],int16_t [4]);
static RECOMP_TLS unsigned s_first_person_input;
extern void sub_000E7EC0_gen(void);
void sub_000E7EC0(void)
{
    if(!buffy_trace_aim_enabled()) { sub_000E7EC0_gen();return; }
    uint32_t self=ecx,frame=nv2a_pb_flip_count();
    float yaw=MEMF(self+0x5C),pitch=MEMF(self+0x58),dt=MEMF(self+0x68);
    uint32_t game=MEM32(0x7D0090),tick=game?MEM32(game+0x20):0;
    float fov_before=tick?MEMF(tick+0xA4):0;
    uint32_t zoom_before=MEM32(self+0x78);
    s_first_person_input++;
    sub_000E7EC0_gen();
    s_first_person_input--;
    static uint32_t last;
    uint32_t zoom_after=MEM32(self+0x78);
    if(frame-last>=15 || zoom_before!=zoom_after) {
        uint16_t digital;uint8_t analog[8];int16_t thumbs[4];
        buffy_aim_input_snapshot(&digital,analog,thumbs);last=frame;
        /* TGO::Interface_Options returns game+0x20. Its +0xA4 field is
         * the zoom FOV, not a frame delta. Read command states without
         * issuing/consuming another guest input query. */
        uint32_t manager=MEM32(0x7CFEB8),stream=tick?MEM32(tick+0xE8):~0u;
        uint32_t states=manager && stream<32 ? MEM32(manager+0x274+4*stream):0;
        uint16_t camera=states?MEM16(states+0x3D0+16*0x12):0;
        float camera_value=states?MEMF(states+0x3D4+16*0x12):0;
        float zoom_value=states?MEMF(states+0x3D4+16*7):0;
        fprintf(stderr,"[aim-camera] f%u dt=%.6g zoom=%u->%u fov=%.6g->%.6g LT/RT=%u/%u stream=%u camera=%04X/%.4g zoom-input=%.4g yaw %.6g->%.6g pitch %.6g->%.6g\n",
            frame,dt,zoom_before,zoom_after,fov_before,tick?MEMF(tick+0xA4):0,
            analog[6],analog[7],stream,camera,camera_value,zoom_value,
            yaw,MEMF(self+0x5C),pitch,MEMF(self+0x58));
    }
}

/* Route only diagnostic virtual calls through this wrapper. The original
 * function still owns mode decisions and its ret4 stack cleanup. */
void buffy_trace_first_person_switch(void)
{
    uint32_t self=ecx,caller=MEM32(esp);
    unsigned enabled=MEM8(esp+4)!=0;
    static uint32_t last_request;
    uint32_t request=self | enabled;
    if(__atomic_exchange_n(&last_request,request,__ATOMIC_RELAXED)!=request) {
        uint16_t digital;uint8_t analog[8];int16_t thumbs[4];
        buffy_aim_input_snapshot(&digital,analog,thumbs);
        fprintf(stderr,"[aim-mode] enable=%u self=%08X caller=%08X LT/RT=%u/%u dig=%04X L=%d,%d R=%d,%d\n",
            enabled,self,caller,analog[6],analog[7],digital,
            thumbs[0],thumbs[1],thumbs[2],thumbs[3]);
    }
    sub_000E5810();
}

extern void sub_000E4B90_gen(void);
void sub_000E4B90(void)
{
    uint32_t move=MEM32(esp+4),rotate=MEM32(esp+8),other=MEM32(esp+12);
    sub_000E4B90_gen();
    if(s_first_person_input && buffy_trace_aim_enabled()) {
        static uint32_t last;
        uint32_t frame=nv2a_pb_flip_count();
        if(frame-last>=15) {
            uint16_t digital;uint8_t analog[8];int16_t thumbs[4];
            buffy_aim_input_snapshot(&digital,analog,thumbs);last=frame;
            fprintf(stderr,"[aim] f%u raw dig=%04X A/B/X/Y=%u/%u/%u/%u BW=%u/%u LT/RT=%u/%u L=%d,%d R=%d,%d -> move %.4g,%.4g,%.4g rotate %.4g,%.4g other %.4g,%.4g\n",
                frame,digital,analog[0],analog[1],analog[2],analog[3],analog[4],analog[5],analog[6],analog[7],
                thumbs[0],thumbs[1],thumbs[2],thumbs[3],MEMF(move),MEMF(move+4),MEMF(move+8),MEMF(rotate),MEMF(rotate+4),MEMF(other),MEMF(other+4));
        }
    }
}
extern void sub_001F1EF0_gen(void);
void sub_001F1EF0(void)
{
    if(buffy_trace_aim_enabled()) fprintf(stderr,"[aim] crossbow fire requested self=%08X ammo-owner=%08X cooldown=%.4g\n",ecx,MEM32(ecx+0xD8C),MEMF(ecx+0xF80));
    sub_001F1EF0_gen();
}

extern void sub_002A9740_gen(void);
void sub_002A9740(void)
{
    float input=MEMF(esp+4);
    sub_002A9740_gen();
    if(s_first_person_input && buffy_trace_aim_enabled()) {
        static uint32_t last;uint32_t frame=nv2a_pb_flip_count();
        if(frame-last>=15) { last=frame;fprintf(stderr,"[aim-math] f%u asin %.8g -> %.12g\n",frame,input,recomp_fp_base()[recomp_fp_top_ld()]); }
    }
}
