/*
 * switch_host.c -- console-side host for fixture replays
 * (run without anyone at the console).
 *
 * buffyx-host.nro (a copy of the NRO) launched with --host listens on TCP
 * 4580 for PING, PUT <path> <size>, GET <path>, LIST <dir>, DEL <path>,
 * RUN <nro> [args] and QUIT. RUN chain-loads the candidate (envSetNextLoad);
 * a replay run given RECOMP_RETURN_NRO chain-loads the host again when it
 * ends, so one launch serves any number of jobs.
 */
#ifdef __SWITCH__
#include <switch.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define HOST_PORT 4580

/* envSetNextLoad keeps the pointers: they must outlive main(). */
static char s_next_path[512], s_next_args[1024];

void switch_chain_load(const char *nro, const char *args)
{
    snprintf(s_next_path, sizeof s_next_path, "%s", nro);
    snprintf(s_next_args, sizeof s_next_args, "%s%s%s", nro, args && *args ? " " : "", args ? args : "");
    envSetNextLoad(s_next_path, s_next_args);
}

/* The NVK shim flips its BO arena chunks (heap memory) to uncached and never
 * flips them back; hbloader then cannot map the next NRO over that heap
 * (svc 2001-0106) -- the crash on exit after a replay. Clear the attribute on
 * every region that still has it. */
void switch_reset_uncached(void)
{
    uint64_t addr = 0;
    unsigned cleared = 0;
    int guard;
    for (guard = 0; guard < (1 << 20); guard++) {
        MemoryInfo info;
        u32 page = 0;
        uint64_t next;
        memset(&info, 0, sizeof info);
        if (R_FAILED(svcQueryMemory(&info, &page, addr)))
            break;
        if ((info.attr & MemAttr_IsUncached) && info.size) {
            Result rc = svcSetMemoryAttribute((void *)info.addr, info.size, MemAttr_IsUncached, 0);
            if (R_SUCCEEDED(rc)) cleared++;
            else fprintf(stderr, "[host] uncached reset failed %llx +%llx attr=%x type=%x rc=%x\n",
                    (unsigned long long)info.addr, (unsigned long long)info.size,
                    info.attr, info.type, rc);
        }
        next = info.addr + info.size;
        if (next <= addr)
            break;
        addr = next;
    }
    fprintf(stderr, "[host] cleared the uncached attribute on %u region(s)\n", cleared);
}

/* Stacks of threads that ended without anyone closing them (libnx unmaps a
 * stack in threadClose): each is MappedMemory backed by a borrowed heap block
 * of the same size, and hbloader cannot load the next NRO over borrowed heap
 * (2001-0106 after every replay). Pair each mapped region with a borrowed
 * block of its size and unmap it -- a wrong pair just fails. The calling
 * thread's own stack is left alone. */
void switch_unmap_dead_stacks(void)
{
    enum { MAXR = 64 };
    struct { uint64_t a, n; int used; } map[MAXR], heap[MAXR];
    extern int switch_unmap_recorded_dead_stacks(void);
    int recorded = switch_unmap_recorded_dead_stacks();
    int nm = 0, nh = 0, i, j, done = 0, guard;
    uint64_t addr = 0, sp = (uint64_t)(uintptr_t)&guard;
    for (guard = 0; guard < 4096; guard++) {
        MemoryInfo info;
        u32 page = 0;
        uint32_t type;
        uint64_t next;
        memset(&info, 0, sizeof info);
        if (R_FAILED(svcQueryMemory(&info, &page, addr)))
            break;
        type = info.type & MemState_Type;
        if (type == MemType_MappedMemory && nm < MAXR &&
            !(sp >= info.addr && sp < info.addr + info.size)) {
            map[nm].a = info.addr; map[nm].n = info.size; map[nm].used = 0; nm++;
        } else if (type == MemType_Heap && (info.attr & MemAttr_IsBorrowed) && nh < MAXR) {
            heap[nh].a = info.addr; heap[nh].n = info.size; heap[nh].used = 0; nh++;
        }
        next = info.addr + info.size;
        if (next <= addr)
            break;
        addr = next;
    }
    for (i = 0; i < nm; i++)
        for (j = 0; j < nh; j++) {
            if (heap[j].used || heap[j].n != map[i].n)
                continue;
            if (R_SUCCEEDED(svcUnmapMemory((void *)map[i].a, (void *)heap[j].a, map[i].n))) {
                heap[j].used = map[i].used = 1;
                done++;
                break;
            }
        }
    fprintf(stderr, "[host] unmapped %d recorded + %d scanned dead stacks (%d scanned mapped regions)\n", recorded, done, nm);
    {   /* the log is closed by now: leave the count where the bench pulls results */
        FILE *f = fopen("sdmc:/switch/buffyx/rp/exit.txt", "w");
        if (f) {
            fprintf(f, "unmapped %d recorded + %d scanned dead stacks (%d scanned mapped regions)\n", recorded, done, nm);
            fclose(f);
        }
    }
}

/* What the process still holds at exit, for hbloader's 2001-0106 (it cannot
 * map the next NRO over the heap). */
void switch_log_memory_map(void)
{
    uint64_t addr = 0;
    int guard;
    for (guard = 0; guard < 4096; guard++) {
        MemoryInfo info;
        u32 page = 0;
        uint32_t type;
        uint64_t next;
        memset(&info, 0, sizeof info);
        if (R_FAILED(svcQueryMemory(&info, &page, addr)))
            break;
        type = info.type & MemState_Type;
        if (type != MemType_Unmapped && type != MemType_Reserved)
            fprintf(stderr, "[host] mem %010llx +%010llx type=%02x attr=%x perm=%x ipc=%u dev=%u\n",
                    (unsigned long long)info.addr, (unsigned long long)info.size, type,
                    info.attr, info.perm, info.ipc_refcount, info.device_refcount);
        next = info.addr + info.size;
        if (next <= addr)
            break;
        addr = next;
    }
}

static int read_line(int fd, char *line, size_t cap)
{
    size_t n = 0;
    char c;
    for (;;) {
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0)
            return 0;
        if (c == '\n')
            break;
        if (n + 1 >= cap)
            return 0;
        line[n++] = c;
    }
    line[n] = 0;
    return 1;
}

static int send_all(int fd, const void *data, size_t size)
{
    const char *p = (const char *)data;
    while (size) {
        ssize_t n = send(fd, p, size, 0);
        if (n <= 0)
            return 0;
        p += n;
        size -= (size_t)n;
    }
    return 1;
}

static int send_text(int fd, const char *t) { return send_all(fd, t, strlen(t)); }

static void make_parents(const char *path)
{
    char tmp[512];
    char *p;
    snprintf(tmp, sizeof tmp, "%s", path);
    for (p = strchr(tmp, '/'); p; p = strchr(p + 1, '/')) {
        if (p == tmp || p[-1] == ':')
            continue;
        *p = 0;
        mkdir(tmp, 0777);
        *p = '/';
    }
}

/* PUT <path> <size>\n<bytes>: written to <path>.part, renamed on completion. */
static const char *handle_put(int fd, const char *path, uint64_t size, char *buf, size_t cap)
{
    char part[540];
    FILE *f;
    uint64_t left = size;
    int ok = 1;
    make_parents(path);
    snprintf(part, sizeof part, "%s.part", path);
    if (!(f = fopen(part, "wb")))
        return "ERR open\n";
    while (left) {
        ssize_t n = recv(fd, buf, (size_t)(left < cap ? left : cap), 0);
        if (n <= 0 || fwrite(buf, 1, (size_t)n, f) != (size_t)n) { ok = 0; break; }
        left -= (uint64_t)n;
    }
    fclose(f);
    if (!ok) { remove(part); return "ERR short\n"; }
    remove(path);
    return rename(part, path) == 0 ? "OK\n" : "ERR rename\n";
}

static void handle_get(int fd, const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    char hdr[64];
    long size;
    size_t n;
    if (!f) { send_text(fd, "ERR missing\n"); return; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    snprintf(hdr, sizeof hdr, "OK %ld\n", size);
    send_text(fd, hdr);
    while ((n = fread(buf, 1, cap, f)) > 0)
        if (!send_all(fd, buf, n))
            break;
    fclose(f);
}

static void handle_list(int fd, const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char line[600];
    if (d) {
        while ((e = readdir(d))) {
            char full[600];
            struct stat st;
            snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
            if (stat(full, &st) != 0)
                continue;
            snprintf(line, sizeof line, "%s%s %lld\n", e->d_name, S_ISDIR(st.st_mode) ? "/" : "",
                     (long long)(S_ISDIR(st.st_mode) ? 0 : st.st_size));
            send_text(fd, line);
        }
        closedir(d);
    }
    send_text(fd, "END\n");
}

/* 1: chain-loaded back by a finished job (argument --host); 0: launched from
 * the menu. PING reports it, so the PC can tell an unattended return from a
 * manual relaunch after a crash. */
int switch_host_by_chain;

int switch_host_main(void)
{
    int server, run_job = 0, yes = 1;
    struct sockaddr_in addr;
    struct in_addr ip;
    PadState pad;
    char line[1024], job_path[512] = "", job_args[512] = "";
    static char buf[1 << 20];

    consoleInit(NULL);
    socketInitializeDefault();
    server = socket(AF_INET, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HOST_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    if (server < 0 || bind(server, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(server, 2) != 0) {
        printf("buffyx host: cannot listen on %d\n", HOST_PORT);
        consoleUpdate(NULL);
        svcSleepThread(5000000000ull);
        socketExit();
        consoleExit(NULL);
        return 1;
    }
    ip.s_addr = gethostid();
    printf("Buffy bench host\n\nlistening on %s:%d\n\n"
           "A bench client drives this; press + to quit.\n\n", inet_ntoa(ip), HOST_PORT);
    {
        FILE *last = fopen("sdmc:/switch/buffyx/rp/timing.txt", "rb");
        if (last) {
            char text[1024] = { 0 };
            fread(text, 1, sizeof text - 1, last);
            fclose(last);
            printf("last job:\n%s\n", text);
        }
    }
    consoleUpdate(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop() && !run_job) {
        fd_set set;
        struct timeval tv = { 0, 100000 };
        int client, quit = 0;
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        FD_ZERO(&set);
        FD_SET(server, &set);
        if (select(server + 1, &set, NULL, NULL, &tv) <= 0)
            continue;
        if ((client = accept(server, NULL, NULL)) < 0)
            continue;
        while (read_line(client, line, sizeof line)) {
            if (!strcmp(line, "PING")) {
                send_text(client, switch_host_by_chain ? "PONG buffyx-bench 1 chain\n"
                                                       : "PONG buffyx-bench 1 menu\n");
            } else if (!strncmp(line, "PUT ", 4)) {
                char *sp = strrchr(line, ' ');
                uint64_t size = strtoull(sp + 1, NULL, 10);
                *sp = 0;
                printf("PUT %s (%llu bytes)\n", line + 4, (unsigned long long)size);
                consoleUpdate(NULL);
                send_text(client, handle_put(client, line + 4, size, buf, sizeof buf));
            } else if (!strncmp(line, "GET ", 4)) {
                handle_get(client, line + 4, buf, sizeof buf);
            } else if (!strncmp(line, "LIST ", 5)) {
                handle_list(client, line + 5);
            } else if (!strncmp(line, "DEL ", 4)) {
                send_text(client, remove(line + 4) == 0 ? "OK\n" : "ERR not removed\n");
            } else if (!strncmp(line, "RUN ", 4)) {
                char *rest = line + 4, *sp = strchr(rest, ' ');
                if (sp) { *sp = 0; snprintf(job_args, sizeof job_args, "%s", sp + 1); }
                snprintf(job_path, sizeof job_path, "%s", rest);
                send_text(client, "OK\n");
                run_job = 1;
                break;
            } else if (!strcmp(line, "QUIT")) {
                send_text(client, "OK\n");
                quit = 1;
                break;
            } else {
                send_text(client, "ERR unknown\n");
            }
        }
        close(client);
        if (quit)
            break;
    }
    close(server);
    socketExit();
    if (run_job) {
        printf("running %s %s\n", job_path, job_args);
        consoleUpdate(NULL);
        switch_chain_load(job_path, job_args);
    }
    consoleExit(NULL);
    return 0;
}
#endif
