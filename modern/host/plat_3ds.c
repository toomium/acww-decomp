// Host build runtime: platform services for the Nintendo 3DS (libctru, newlib; docs/3ds.md T2.2). See plat.h.
// The program's directory is sdmc:/3ds/acww/: the options (acww.cfg), the log (log.txt), by default the ROM
// (game.nds) and so the save beside it.
#ifdef __3DS__

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <3ds.h>

#include "plat.h"

#define DIR "sdmc:/3ds/acww/"

// The options: there is no environment on the 3DS, so acww.cfg's `NAME=value` lines (`#` comments) become one, and
// every ACWW_* option of docs/host.md works unchanged. stdout and stderr go to log.txt.
void host_plat_init(void) {
    freopen(DIR "log.txt", "w", stdout);
    freopen(DIR "log.txt", "a", stderr);
    setvbuf(stderr, 0, _IONBF, 0);
    FILE *f = fopen(DIR "acww.cfg", "r");
    char line[512];
    while (f && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq) continue;
        *eq = 0;
        setenv(line, eq + 1, 1);
    }
    if (f) fclose(f);
    setenv("ACWW_ROM", DIR "game.nds", 0);
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    fprintf(stderr, "3ds: %s, application memory %lu KB (%lu KB free), heap %lu KB, linear heap %lu KB\n",
            n3ds ? "New 3DS" : "Old 3DS", (unsigned long)osGetMemRegionSize(MEMREGION_APPLICATION) / 1024,
            (unsigned long)osGetMemRegionFree(MEMREGION_APPLICATION) / 1024, (unsigned long)envGetHeapSize() / 1024,
            (unsigned long)envGetLinearHeapSize() / 1024);
}

// svcGetSystemTick counts at the ARM11's base clock (SYSCLOCK_ARM11), whatever the New 3DS's speed
uint64_t host_plat_time_ns(void) {
    uint64_t t = svcGetSystemTick();
    return t / SYSCLOCK_ARM11 * 1000000000u + t % SYSCLOCK_ARM11 * 1000000000u / SYSCLOCK_ARM11;
}

void host_plat_sleep_until_ns(uint64_t t) {
    uint64_t now = host_plat_time_ns();
    if (now < t) svcSleepThread((s64)(t - now));
}

void host_plat_sleep_ms(unsigned ms) { svcSleepThread((s64)ms * 1000000); }

int host_plat_localtime(struct tm *out) {
    time_t now = time(0);
    return localtime_r(&now, out) != 0;
}

// shortcut: no restart in place on the 3DS; the caller reports it and the program ends (back to the launcher)
void host_plat_restart(char **argv) { (void)argv; }

struct Start { void (*fn)(void *); void *arg; };
static void thread_main(void *p) {
    struct Start s = *(struct Start *)p;
    free(p);
    s.fn(s.arg);
}
// at a lower priority than the game's thread (a higher number), on the application core: the 3DS doesn't share a
// core between threads by time slices that we know of (docs/3ds.md 1.3), so it runs while the game's thread waits
int host_plat_thread_start(void (*fn)(void *), void *arg) {
    struct Start *s = malloc(sizeof *s);
    if (!s) return 0;
    s->fn = fn;
    s->arg = arg;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    if (!threadCreate(thread_main, s, 16 * 1024, prio < 0x3f ? prio + 1 : prio, -2, true)) {
        free(s);
        return 0;
    }
    return 1;
}

// (no guard page: a 3DS application can't change its memory's protection)
void *host_plat_stack_alloc(size_t n, int flags) {
    (void)flags;
    return memalign(0x1000, (n + 0xfff) & ~(size_t)0xfff);
}

void host_plat_stack_free(void *stack, size_t n) {
    (void)n;
    free(stack);
}

void *host_plat_pages_alloc(size_t n) {
    void *p = memalign(0x1000, n);
    if (p) memset(p, 0, n);
    return p;
}

int host_plat_image_bounds(uintptr_t *lo, uintptr_t *hi) {
    (void)lo; (void)hi;
    return 0;
}

int host_plat_backtrace(void **pcs, int n) {
    (void)pcs; (void)n;
    return 0;
}

void host_plat_print_backtrace(void) {}

#endif
