/**
 * Buffy the Vampire Slayer (Xbox) - recompiled game entry point
 *
 * Boot order (same on every host):
 *   crash reporter -> load default.xbe -> xbox_MemoryLayoutInit ->
 *   xbox_kernel_init -> xbox_path_init -> xbox_kernel_bridge_init ->
 *   g_esp = XBOX_STACK_TOP -> recomp_dispatch_init -> xbe_entry_point()
 *
 * Game data is the extracted disc (default.xbe, dvddata/, media/). Where it is
 * looked for:
 *   Windows/Linux: $GAME_GAME_DIR, else ./game
 *   Switch:        sdmc:/switch/buffyx/game
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE        /* REG_EFL for the GAME_WATCH_VA single-step */
#endif
#ifdef _WIN32
#  include <windows.h>
#  include <dbghelp.h>
#else
#  include <signal.h>
#  include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <xbox/xboxrecomp.h>

#ifdef __SWITCH__
/* libnx lives in switch_nx.c: <switch.h> and the Win32 vocabulary collide. */
void switch_boot(void);
void switch_shutdown(void);
void switch_guard_chain_load(void);
void switch_error_dialog(const char *msg, const char *details);
#elif !defined(_WIN32)
int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *message,
                             void *window);
#endif

#ifdef RECOMP_ICALL_FEEDBACK
/* Indirect-branch feedback (xboxrecomp docs/technical/indirect-calls.md):
 * dumped at exit, from the crash handler, and on SIGUSR1 so a hung run can
 * be harvested before it is killed. GAME_ICALL_DUMP names the file. */
#include "recomp_icall_feedback.h"
static void icall_feedback_dump(void)
{
    const char *p = getenv("GAME_ICALL_DUMP");
    recomp_icall_feedback_dump(p && *p ? p : "icall.txt");
}
#endif

extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern RECOMP_TLS uint32_t g_ebx, g_esi, g_edi;
extern ptrdiff_t g_xbox_mem_offset;
extern volatile uint32_t g_icall_trace[16];
extern volatile uint32_t g_icall_trace_idx;

typedef struct MCPXAPUState MCPXAPUState;
extern MCPXAPUState *mcpx_apu_init_standalone(uint8_t *ram_ptr);
extern MCPXAPUState *g_apu_state;
extern uint64_t mcpx_apu_mmio_read(MCPXAPUState *d, uint64_t addr, unsigned int size);
extern void mcpx_apu_mmio_write(MCPXAPUState *d, uint64_t addr, uint64_t val,
                                unsigned int size);

#define GAME_APU_BASE 0xFE800000u
#define GAME_APU_END  0xFE880000u

static uint32_t apu_mmio_rd(void *opaque, uint32_t off, unsigned size)
{
    return (uint32_t)mcpx_apu_mmio_read((MCPXAPUState *)opaque, off, size);
}

static void apu_mmio_wr(void *opaque, uint32_t off, uint32_t val, unsigned size)
{
    mcpx_apu_mmio_write((MCPXAPUState *)opaque, off, val, size);
}

extern void xbe_entry_point(void);
extern int recomp_dispatch_init(void);
extern void xbox_path_init(const char *game_dir, const char *save_dir);

#define GAME_ENTRY_POINT 0x00579EA2u


#if defined(__SWITCH__)
#  define GAME_DEFAULT_GAME_DIR "sdmc:/switch/buffyx/game"
#  define GAME_DEFAULT_SAVE_DIR "sdmc:/switch/buffyx/save"
#else
#  define GAME_DEFAULT_GAME_DIR "game"
#  define GAME_DEFAULT_SAVE_DIR NULL
#endif

/* ------------------------------------------------------------------ */
/* Crash reporting                                                     */
/* ------------------------------------------------------------------ */

/* What the guest was doing: its registers and the return addresses on its
 * stack. This is the useful half of any crash report -- the host PC names a
 * generated sub_XXXXXXXX at best, the guest stack names the whole chain. */
void print_guest_state(void)
{
    fprintf(stderr, "  guest: eax=%08X ecx=%08X edx=%08X ebx=%08X\n",
            g_eax, g_ecx, g_edx, g_ebx);
    fprintf(stderr, "         esi=%08X edi=%08X esp=%08X\n",
            g_esi, g_edi, g_esp);

    if (g_xbox_mem_offset && g_esp >= XBOX_STACK_BASE
            && g_esp < XBOX_STACK_BASE + XBOX_STACK_SIZE) {
        const uint32_t *sp =
            (const uint32_t *)((uintptr_t)g_xbox_mem_offset + g_esp);
        int shown = 0, i;
        fprintf(stderr, "  guest stack (return addresses, innermost first):\n");
        for (i = 0; i < 512 && shown < 24; i++) {
            uint32_t v = sp[i];
            if (g_esp + (uint32_t)i * 4u >= XBOX_STACK_BASE + XBOX_STACK_SIZE)
                break;
            if (v > g_xbox_code_lo && v < g_xbox_code_hi) {
                fprintf(stderr, "    [esp+%-4d] 0x%08X\n", i * 4, v);
                shown++;
            }
        }
    }
    {
        unsigned k;
        fprintf(stderr, "  recent ICALL targets:");
        for (k = 0; k < 16; k++)
            fprintf(stderr, " %08X", g_icall_trace[(g_icall_trace_idx + k) & 15]);
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

#ifdef _WIN32
static LONG CALLBACK veh_handler(PEXCEPTION_POINTERS ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        uintptr_t fault = ep->ExceptionRecord->ExceptionInformation[1];
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
        DWORD64 disp = 0;

        /* The NV2A aperture belongs to the runtime's own handlers. */
        if (fault >= 0xFD000000 && fault < 0xFE000000)
            return EXCEPTION_CONTINUE_SEARCH;

        fprintf(stderr, "[CRASH] access violation at RIP=0x%llX, %s 0x%llX "
                        "(guest VA 0x%08X)\n",
                (unsigned long long)ep->ContextRecord->Rip,
                ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                (unsigned long long)fault,
                (uint32_t)(fault - (uintptr_t)g_xbox_mem_offset));
        memset(buf, 0, sizeof(buf));
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        if (SymFromAddr(GetCurrentProcess(), ep->ContextRecord->Rip, &disp, sym))
            fprintf(stderr, "  in %s+0x%llX\n", sym->Name,
                    (unsigned long long)disp);
        print_guest_state();
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void install_crash_reporter(void)
{
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    AddVectoredExceptionHandler(1, veh_handler);
}
#elif defined(__SWITCH__)
/* The CPU exception handler is in switch_nx.c; it calls print_guest_state. */
static void install_crash_reporter(void) {}
#else
#include <execinfo.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <pthread.h>

/* GAME_WATCH_VA=<hex va>,<len> (debug): find who writes a guest range.
 * The host page holding it is made read-only; a write faults, is logged
 * (guest VA, value context, host backtrace = the lifted sub_XXXXXXXX) when it
 * lands in the range, the page is opened and the single-step flag set; the
 * SIGTRAP after that one instruction closes it again. */
static uintptr_t s_watch_page;
static uint32_t  s_watch_lo, s_watch_hi;
static int       s_watch_hits;
static int       s_watch_read;   /* GAME_WATCH_READ=1: reads fault too */

static void watch_protect(int ro)
{
    if (s_watch_page)
        mprotect((void *)s_watch_page, 4096,
                 ro ? (s_watch_read ? PROT_NONE : PROT_READ) : PROT_READ | PROT_WRITE);
}

static void watch_trap(int sig, siginfo_t *si, void *uc)
{
    ucontext_t *u = (ucontext_t *)uc;
    (void)sig; (void)si;
    u->uc_mcontext.gregs[REG_EFL] &= ~0x100;    /* single step done */
    watch_protect(1);
}

static void *watch_arm_later(void *arg)
{
    extern uint32_t nv2a_gl_frame_count(void);
    const char *e = getenv("GAME_WATCH_FRAME");
    uint32_t at = e ? (uint32_t)strtoul(e, NULL, 10) : 1000, i;
    (void)arg;
    while (nv2a_gl_frame_count() < at)
        usleep(20000);
    fprintf(stderr, "[WATCH-VA] armed at frame %u; range holds:", nv2a_gl_frame_count());
    for (i = s_watch_lo; i < s_watch_hi && i < s_watch_lo + 0x40; i += 4) {
        float x;
        memcpy(&x, (const void *)((uintptr_t)g_xbox_mem_offset + i), 4);
        fprintf(stderr, " %g", x);
    }
    fprintf(stderr, "\n");
    watch_protect(1);
    return NULL;
}

static void watch_arm(void)
{
    const char *e = getenv("GAME_WATCH_VA");
    struct sigaction sa;
    char *end;
    if (!e || !*e)
        return;
    s_watch_lo = (uint32_t)strtoul(e, &end, 16);
    s_watch_read = getenv("GAME_WATCH_READ") && *getenv("GAME_WATCH_READ") == '1';
    s_watch_hi = s_watch_lo + (*end == ',' ? (uint32_t)strtoul(end + 1, NULL, 0) : 4);
    s_watch_page = ((uintptr_t)g_xbox_mem_offset + s_watch_lo) & ~(uintptr_t)4095;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = watch_trap;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGTRAP, &sa, NULL);
    fprintf(stderr, "[WATCH-VA] guest %08X..%08X (host page %p), armed at frame 1000\n",
            s_watch_lo, s_watch_hi, (void *)s_watch_page);
    {
        /* Arm late: the page is busy while loading and the faults slow the
         * boot to a crawl. */
        static pthread_t th;
        pthread_create(&th, NULL, watch_arm_later, NULL);
    }
}

/* Arm the watch at run time (debug hooks that only learn the address late). */
void game_watch_now(uint32_t va, uint32_t len)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = watch_trap;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGTRAP, &sa, NULL);
    s_watch_lo = va;
    s_watch_hi = va + len;
    s_watch_page = ((uintptr_t)g_xbox_mem_offset + va) & ~(uintptr_t)4095;
    fprintf(stderr, "[WATCH-VA] now on %08X..%08X\n", s_watch_lo, s_watch_hi);
    watch_protect(1);
}

/* 1 when the fault was the watch page (handled), 0 otherwise. */
static int watch_fault(siginfo_t *si, void *uc)
{
    uintptr_t a = (uintptr_t)si->si_addr;
    ucontext_t *u = (ucontext_t *)uc;
    if (!s_watch_page || a < s_watch_page || a >= s_watch_page + 4096)
        return 0;
    {
        extern uint32_t nv2a_gl_frame_count(void);
        uint32_t va = (uint32_t)(a - (uintptr_t)g_xbox_mem_offset);
        /* only once the title is up (boot-time heap traffic is noise) */
        void *frames[8];
        int n = 0, k, seen = 0;
        static void *sites[64];
        static int nsites;
        if (va >= s_watch_lo && va < s_watch_hi) {
            n = backtrace(frames, 8);
            /* each writing site once (frame 2: past this handler and the
             * signal trampoline), so bulk zeroing cannot hide the writer */
            for (k = 0; k < nsites; k++)
                if (n > 4 && sites[k] == (void *)((uintptr_t)frames[2] ^ (uintptr_t)frames[3] ^ ((uintptr_t)frames[4] << 1)))
                    seen = 1;
            if (!seen && n > 4 && nsites < 64)
                sites[nsites++] = (void *)((uintptr_t)frames[2] ^ (uintptr_t)frames[3] ^ ((uintptr_t)frames[4] << 1));
        }
        if (va >= s_watch_lo && va < s_watch_hi && !seen && s_watch_hits++ < 64) {
            fprintf(stderr, "[WATCH-VA] %s #%d at %08X (+0x%X)\n",
                    s_watch_read ? "access" : "write", s_watch_hits, va, va - s_watch_lo);
            backtrace_symbols_fd(frames, n, 2);
        }
    }
    watch_protect(0);
    u->uc_mcontext.gregs[REG_EFL] |= 0x100;     /* trap after this instruction */
    return 1;
}

static void crash_signal(int sig, siginfo_t *si, void *uc)
{
    if (sig == SIGSEGV && watch_fault(si, uc))
        return;
    fprintf(stderr, "[CRASH] signal %d, fault addr %p (guest VA 0x%08X)\n",
            sig, si->si_addr,
            (uint32_t)((uintptr_t)si->si_addr - (uintptr_t)g_xbox_mem_offset));
    print_guest_state();
#ifdef RECOMP_ICALL_FEEDBACK
    icall_feedback_dump();
#endif
    {
        /* The host frames name the lifted function (sub_XXXXXXXX) that
         * faulted -- the guest stack alone does not on a worker thread. */
        void *frames[32];
        int n = backtrace(frames, 32);
        fprintf(stderr, "  host backtrace:\n");
        fflush(stderr);
        backtrace_symbols_fd(frames, n, 2);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_crash_reporter(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    {
        static char altstack[64 * 1024];
        stack_t ss;
        ss.ss_sp = altstack;
        ss.ss_size = sizeof(altstack);
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);
    }
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
#ifdef RECOMP_ICALL_FEEDBACK
    atexit(icall_feedback_dump);
    signal(SIGUSR1, (void (*)(int))icall_feedback_dump);
#endif
}
#endif

/* ------------------------------------------------------------------ */

static void *load_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *data;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    data = malloc((size_t)size);
    if (data && fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data)
        *out_size = (size_t)size;
    return data;
}

static void fatal(const char *msg)
{
    fprintf(stderr, "[FATAL] %s\n", msg);
#if defined(_WIN32)
    MessageBoxA(NULL, msg, "Buffy recomp", MB_ICONERROR);
#elif defined(__SWITCH__)
    switch_error_dialog(msg, NULL);
#else
    SDL_ShowSimpleMessageBox(0x10, "Buffy recomp", msg, NULL);
#endif
}

static int game_main(void);
void xbox_guest_pin(int interrupt);   /* win32_compat.c: the Xbox's one CPU */
void xbox_gil_enter(void);             /* kernel_bridge.c: the guest lock */
void xbox_gil_leave(void);
void xbox_gil_mark_main(void);

#ifdef __SWITCH__
#include <pthread.h>
/* The first guest thread runs inline on whichever host thread boots the
 * title, and Horizon's main thread stack is small for recompiled code; boot
 * on a thread with a stack sized like a desktop main thread instead. */
void xbox_nx_spread_thread(void);   /* win32_compat.c: off core 0, all cores allowed */
void xbox_nx_track_thread(void *entry);   /* win32_compat.c: Switch [perf] report */

static void *game_thread(void *arg)
{
    xbox_nx_spread_thread();
    xbox_nx_track_thread(NULL);          /* "game:" in the [perf] report */
    *(int *)arg = game_main();
    return NULL;
}

int switch_host_main(void);                       /* switch_host.c */
void switch_reset_uncached(void);
void switch_chain_load(const char *nro, const char *args);

/* XLaunchNewImage on the console (kernel_bridge.c calls this from
 * HalReturnToFirmware with the launch page): write the page next to the NRO,
 * shut down like a finished bench job and chain-load this NRO with
 * --relaunch, which restores RECOMP_LAUNCH_DATA / _PERSIST (environment
 * variables do not survive a chain load). */
#define RELAUNCH_DIR "sdmc:/switch/buffyx"
static const char *s_self_nro;
static int s_relaunch_requested;
void xbox_relaunch(const void *page, size_t len)
{
    FILE *f = fopen(RELAUNCH_DIR "/launchdata.bin", "wb");
    if (!f || fwrite(page, 1, len, f) != len || !s_self_nro) {
        fprintf(stderr, "  [relaunch] cannot relaunch (%s)\n", s_self_nro ? "write" : "no NRO path");
        if (f) fclose(f);
        return;
    }
    fclose(f);
    fprintf(stderr, "  [relaunch] chain-loading %s --relaunch\n", s_self_nro);
    extern int xbox_BridgeShutdown(void);
    extern void mcpx_apu_shutdown(MCPXAPUState *);
    if (!xbox_BridgeShutdown()) {
        fflush(NULL);
        /* Returning to hbloader here also unloads live code/mapped RAM. */
        extern void switch_abort_relaunch(void) __attribute__((noreturn));
        switch_abort_relaunch();
    }
    if (g_apu_state) {
        mcpx_apu_shutdown(g_apu_state);
        g_apu_state = NULL;
    }
    /* Finish chain-loading on main's stack. Sweeping stack mappings from
     * this game pthread also unmapped the live main thread waiting to join
     * us, so hbloader never received the relaunch request. */
    s_relaunch_requested = 1;
    extern int xbox_gil_suspend(void);
    xbox_gil_suspend();
    pthread_exit(NULL);
}

int main(int argc, char **argv)
{
    pthread_attr_t attr;
    pthread_t th;
    int rc = 1;

    s_self_nro = argc > 0 ? argv[0] : NULL;
    setenv("RECOMP_LAUNCH_DIR", RELAUNCH_DIR, 0);
    if (argc > 1 && !strcmp(argv[1], "--relaunch")) {
        setenv("RECOMP_LAUNCH_DATA", RELAUNCH_DIR "/launchdata.bin", 1);
        setenv("RECOMP_LAUNCH_PERSIST", RELAUNCH_DIR "/launchpersist.bin", 1);
    }

    /* Launched as buffyx-host.nro (hbmenu passes no arguments: the name
     * decides), or with --host: wait for bench jobs over TCP. */
    if ((argc > 1 && !strcmp(argv[1], "--host")) ||
        (argc > 0 && argv[0] && strstr(argv[0], "buffyx-host"))) {
        extern int switch_host_by_chain;
        switch_host_by_chain = argc > 1 && !strcmp(argv[1], "--host");
        return switch_host_main();
    }
    switch_boot();
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16u * 1024 * 1024);
    if (pthread_create(&th, &attr, game_thread, &rc) == 0)
        pthread_join(th, NULL);
    else
        fprintf(stderr, "[FATAL] cannot start the game thread\n");
    if (s_relaunch_requested) {
        extern void nv2a_vk_shutdown(void) __attribute__((weak));
        extern void xbox_nx_release_all(void);
        extern void switch_unmap_dead_stacks(void);
        if (nv2a_vk_shutdown)
            nv2a_vk_shutdown();
        xbox_MemoryLayoutShutdown();
        usleep(20000);
        xbox_nx_release_all();
        switch_chain_load(s_self_nro, "--relaunch");
        switch_shutdown();
        switch_guard_chain_load();
        switch_unmap_dead_stacks();
        switch_reset_uncached();
        extern void switch_flush_log(void);
        switch_flush_log();
        fflush(NULL);
        _Exit(0);
    }
    if (getenv("RECOMP_REPLAY")) {
        /* A bench job ends here: give back every guest memory
         * view, clear NVK's uncached arenas, arm the chain-load to the host,
         * flush, and _Exit -- no destructors or atexit work under still-live
         * renderer threads, but still libnx's exit path, so hbloader loads
         * the host. Each of the missing steps crashed the exit. */
        extern void xbox_nx_release_all(void);
        const char *ret = getenv("RECOMP_RETURN_NRO");
        /* The NV2A ack/flag/tick threads poll guest memory (the GPU
         * registers at 0xFD000000): stop them (the ordered layout shutdown
         * joins the ack thread; the other two see the flag within 1 ms) before
         * any view goes -- releasing under them crashed the exit. */
        {
            extern void nv2a_vk_shutdown(void) __attribute__((weak));
            if (nv2a_vk_shutdown)
                nv2a_vk_shutdown();     /* NVK's GPU mappings go first */
        }
        xbox_MemoryLayoutShutdown();
        usleep(20000);
        xbox_nx_release_all();
        if (ret && *ret)
            switch_chain_load(ret, "--host");
        /* Joins the log flusher and the profiler: their stacks must be dead
         * before the stack sweep below. Lines after this go to stderr only. */
        switch_shutdown();
        switch_guard_chain_load();
        {
            extern void switch_unmap_dead_stacks(void);
            extern void switch_log_memory_map(void);
            switch_unmap_dead_stacks();
            switch_reset_uncached();
            switch_log_memory_map();
            extern void switch_flush_log(void);
            switch_flush_log();
        }
        fflush(NULL);
        _Exit(rc);
    }
    switch_shutdown();
    return rc;
}
#else
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return game_main();
}
#endif

static int game_main(void)
{
    extern void xbox_nx_set_el0_tp(void);   /* win32_compat.c: -mtp=el0 lifted code */
    xbox_nx_set_el0_tp();
    xbox_guest_pin(0);     /* the boot thread becomes the title's first thread */
    char xbe_path[512];
    const char *game_dir;
    void *xbe_data;
    size_t xbe_size = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("=== Buffy the Vampire Slayer (Xbox) - static recompilation ===\n");
    install_crash_reporter();
    if (getenv("GAME_EXIT_TRACE"))
        atexit(print_guest_state);

    /* Runtime defaults this title needs; an explicit setting still wins.
     *   RECOMP_VBLANK      the linked D3D waits on its vblank ISR every frame
     *   RECOMP_AC97_READY  DirectSound needs the codec present to create; "plain"
     *                      is the variant that needs no fault handling */
#ifdef _WIN32
    if (!getenv("RECOMP_VBLANK"))     _putenv("RECOMP_VBLANK=1");
    if (!getenv("RECOMP_AC97_READY")) _putenv("RECOMP_AC97_READY=plain");
    if (!getenv("RECOMP_USB"))        _putenv("RECOMP_USB=1");
    /* Root port 2 of 4 is controller slot 1 (ports wire 3,4,1,2); the
     * front-end only listens to slot 1. */
    if (!getenv("RECOMP_USB_NDP"))    _putenv("RECOMP_USB_NDP=4");
    if (!getenv("RECOMP_USB_PORT"))   _putenv("RECOMP_USB_PORT=2");
#else
    setenv("RECOMP_VBLANK", "1", 0);
    setenv("RECOMP_AC97_READY", "plain", 0);
    setenv("RECOMP_USB", "1", 0);       /* the pad is on the MCPX's OHCI */
    setenv("RECOMP_USB_NDP", "4", 0);   /* root port 2 = controller slot 1 */
    setenv("RECOMP_USB_PORT", "2", 0);
    setenv("RECOMP_PB_EXEC", "1", 0);   /* the title draws through NV2A */
#endif
#if defined(__SWITCH__)
    /* Every periodic log line flushes to the SD card; keep the log to what
     * matters on a console. RECOMP_QUIET=0 in buffyx_env.txt brings it back. */
    setenv("RECOMP_QUIET", "1", 0);
    setenv("RECOMP_READAHEAD", "1", 0);
    setenv("RECOMP_READAHEAD_MEDIA", "1", 0);
    /* Buffy WMV playback is supported on Switch. Keep movie skipping opt-in
     * (GAME_SKIP_MOVIES=1) for scripted bring-up and short diagnostic runs. */
    /* The frame-rate thread gets the guest lock first, and GL runs on its
     * own thread (earlier Switch profiling): the executor was ~72% of a core and
     * the limit (console, 2026-10-04). The malloc crashes seen then came
     * with Mesa 26, also with the GL thread off. =0 turns either off. */
    setenv("RECOMP_GIL_EAGER", "1", 0);
    setenv("RECOMP_GL_THREAD", "1", 0);
    /* Rumble is validated; RECOMP_RUMBLE_TRACE remains opt-in. */
    /* Mesa's own disk cache writes to the SD card from the draw path;
     * progcache.bin already covers repeat compiles. */
    setenv("MESA_SHADER_CACHE_DISABLE", "true", 0);
#endif

    game_dir = getenv("GAME_GAME_DIR");
    if (!game_dir || !game_dir[0])
        game_dir = GAME_DEFAULT_GAME_DIR;
    snprintf(xbe_path, sizeof(xbe_path), "%s/default.xbe", game_dir);

    xbe_data = load_file(xbe_path, &xbe_size);
    if (!xbe_data) {
        /* Discs ship it as Default.xbe too (Buffy); case matters off Windows. */
        snprintf(xbe_path, sizeof(xbe_path), "%s/Default.xbe", game_dir);
        xbe_data = load_file(xbe_path, &xbe_size);
    }
    if (!xbe_data) {
        fprintf(stderr, "cannot read %s\n", xbe_path);
        fatal("Failed to load default.xbe. Put the extracted disc in the game "
              "directory (or set GAME_GAME_DIR).");
        return 1;
    }
    printf("XBE %s: %zu bytes\n", xbe_path, xbe_size);

    if (!xbox_MemoryLayoutInit(xbe_data, xbe_size)) {
        fatal("Failed to initialise the Xbox memory layout "
              "(the guest address range may be unavailable).");
        free(xbe_data);
        return 1;
    }
    g_xbox_mem_offset = xbox_GetMemoryOffset();
    printf("Xbox memory mapped at host offset 0x%llX\n",
           (unsigned long long)g_xbox_mem_offset);
#if !defined(_WIN32) && !defined(__SWITCH__)
    watch_arm();
#endif

    /* The emulated APU. DirectSound's accesses reach it through the MMIO
     * accessors the DSOUND section is lifted with (regen.sh passes
     * --mmio-sections DSOUND), so the APU registers stay plain memory and no
     * fault handling is needed -- the same on every host. GAME_APU=0 runs
     * without it (DirectSound then creates, but nothing ever plays).
     * Under RECOMP_AC97_READY=1 the runtime traps the APU instead (Windows). */
    if (getenv("RECOMP_AC97_READY") && !getenv("RECOMP_REPLAY")) {
        const char *apu = getenv("GAME_APU");
        int plain = strcmp(getenv("RECOMP_AC97_READY"), "plain") == 0;

        if (!plain || !apu || strcmp(apu, "0") != 0) {
            g_apu_state = mcpx_apu_init_standalone((uint8_t *)xbox_GetMemoryBase());
            fprintf(stderr, "[BOOT] emulated APU %s\n",
                    g_apu_state ? "up" : "FAILED to initialise");
            if (g_apu_state && plain)
                xbox_MmioRegister(GAME_APU_BASE, GAME_APU_END,
                                  apu_mmio_rd, apu_mmio_wr, g_apu_state);
        }
    }

    /* The USB host controllers the title's XPP library drives to find its
     * gamepad (RECOMP_USB). On hosts without fault handling their registers
     * are reached through the MMIO accessors the XPP section is lifted with. */
    if (!getenv("RECOMP_REPLAY")) {   /* a replay needs only the renderer */
        extern void xbox_OhciInit(void);
        xbox_OhciInit();
    }

#ifndef _WIN32
    /* The GPU renderer (GAME_GL=0 falls back to the executor's CPU one). */
    {
        const char *gl = getenv("GAME_GL");
        if (!gl || strcmp(gl, "0") != 0) {
#if defined(GAME_VULKAN)
            extern void nv2a_vk_install(void);
            nv2a_vk_install();
#else
            extern void nv2a_gl_install(void);
            nv2a_gl_install();
#endif
        }
    }
#endif

    /* RECOMP_REPLAY=<fixture>: drive the renderer from a recorded fixture
     * (nv2a_fixture.c) instead of running the title -- before the kernel
     * and device threads start, so only the renderer is alive at exit. */
    if (getenv("RECOMP_REPLAY")) {
        extern int nv2a_renderer_replay(void);
#ifdef __SWITCH__
        {   /* RECOMP_NX_CORE=<n>: the replay (render) thread on core n alone --
             * tests whether the driver's submit work shares our core */
            const char *c = getenv("RECOMP_NX_CORE");
            if (c && *c) {
                extern uint32_t svcSetThreadCoreMask(uint32_t handle, int32_t ideal, uint32_t mask);
                int core = atoi(c);
                uint32_t rc2 = svcSetThreadCoreMask(0xFFFF8000u, core, 1u << core);
                fprintf(stderr, "[replay] pinned to core %d (rc 0x%X)\n", core, rc2);
            }
        }
#endif
        int rc = nv2a_renderer_replay();
        fflush(stdout);
        fflush(stderr);
        return rc;
    }

    xbox_kernel_init();
    /* Writable partitions (T:, U:, Z: cache) go under GAME_SAVE_DIR when
     * set, so the extracted disc stays untouched. */
    {
        const char *save_dir = getenv("GAME_SAVE_DIR");
        if (!save_dir || !save_dir[0])
            save_dir = GAME_DEFAULT_SAVE_DIR;
        xbox_path_init(game_dir, save_dir);
    }
    xbox_kernel_bridge_init();

    g_esp = XBOX_STACK_TOP;

    if (!recomp_dispatch_init())
        fprintf(stderr, "[BOOT] flat dispatch unavailable; "
                        "indirect calls will use the binary search\n");

    xbox_WatchdogStart();

    printf("Starting guest at 0x%08X (esp=0x%08X)\n", GAME_ENTRY_POINT, g_esp);
    xbox_gil_mark_main();      /* the frame-rate thread (RECOMP_GIL_EAGER) */
    xbox_gil_enter();          /* guest code from here on (kernel_bridge.c) */
    xbe_entry_point();
    xbox_gil_leave();

    printf("Guest returned. Cleaning up.\n");
    xbox_kernel_shutdown();
    xbox_MemoryLayoutShutdown();
    free(xbe_data);
    return 0;
}
