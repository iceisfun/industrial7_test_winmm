/* time_event_lifetime: shared helpers.
 *
 * Model of the implementation under test (winmm!timeSetEvent 0x2a20, timeKillEvent 0x2e50,
 * timeThreadSetEvent 0x2b90, timeThread 0x26c0, TimeInitThread 0x2d7c, TimeInit 0x23c0):
 *  - 16 event slots per process, id = generation(16-bit, += 0x10 per allocation, never 0) + slot.
 *  - one worker thread (TIME_CRITICAL priority) runs every callback outside the timer lock.
 *  - each live event holds timeBeginPeriod(period); kill / one-shot completion calls timeEndPeriod(period).
 *    period = uResolution clamped to [wPeriodMin, TimerData]; uDelay < period -> TimerData.
 *    TimerData = ceil(NtQueryTimerResolution maximum / 10000) (0x37 when >= 55 ms).
 *  - timeKillEvent waits for a running callback only when the event has TIME_KILL_SYNCHRONOUS, the
 *    callback running is that event's, and the caller is not the timer thread.
 * Children (CHILD) are used where a clean slot table or a fresh process is needed. A child records
 * "key value" lines into a file whose path is argv[1]; the parent reads them back and checks them.
 */
#ifndef TL_H
#define TL_H
#include "t.h"
#include <mmsystem.h>
#include <stdlib.h>

#ifndef TIME_KILL_SYNCHRONOUS
#define TIME_KILL_SYNCHRONOUS 0x0100
#endif
#define TL_NOCANDO 97u   /* TIMERR_NOCANDO */

typedef LONG (NTAPI *tl_NtQueryTimerResolution_fn)(PULONG maxres, PULONG minres, PULONG cur);

/* Resolve once per call site, cheap enough (GetProcAddress) */
#define TSE   U(timeSetEvent)
#define TKE   U(timeKillEvent)
#define TBP   U(timeBeginPeriod)
#define TEP   U(timeEndPeriod)
#define TGT   U(timeGetTime)

static inline int tl_have(void) {
    return U(timeSetEvent) && U(timeKillEvent) && U(timeBeginPeriod) && U(timeEndPeriod) && U(timeGetTime);
}
#define TL_REQUIRE() do { REQUIRE(U(timeSetEvent)); REQUIRE(U(timeKillEvent)); REQUIRE(U(timeBeginPeriod)); \
    REQUIRE(U(timeEndPeriod)); REQUIRE(U(timeGetTime)); } while (0)

/* wPeriodMin from timeGetDevCaps (1 on the VM) */
static inline UINT tl_pmin(void) {
    TIMECAPS tc; memset(&tc, 0, sizeof tc);
    if (U(timeGetDevCaps) && U(timeGetDevCaps)(&tc, sizeof tc) == 0 && tc.wPeriodMin) return tc.wPeriodMin;
    return 1;
}
/* TimerData / kernel32 uMax: ceil(max resolution / 1 ms), 0x37 when the query fails or >= 55 ms */
static inline UINT tl_umax(void) {
    ULONG mx = 0, mn = 0, cur = 0;
    tl_NtQueryTimerResolution_fn q = (tl_NtQueryTimerResolution_fn)t_proc_in("ntdll.dll", "NtQueryTimerResolution");
    if (!q || q(&mx, &mn, &cur) < 0) return 0x37;
    if (mx + 9999u < 550000u) return (mx + 9999u) / 10000u;
    return 0x37;
}
static inline ULONG tl_cur_res(void) {
    ULONG mx = 0, mn = 0, cur = 0;
    tl_NtQueryTimerResolution_fn q = (tl_NtQueryTimerResolution_fn)t_proc_in("ntdll.dll", "NtQueryTimerResolution");
    if (!q || q(&mx, &mn, &cur) < 0) return 0;
    return cur;
}
/* The period an event with (delay, res) holds a reference on (per the timeSetEvent decomp). */
static inline UINT tl_event_period(UINT delay, UINT res) {
    UINT td = tl_umax(), mn = tl_pmin(), p = td;
    if (res <= td) { p = res; if (res < mn) p = mn; }
    if (delay < p) p = td;
    return p;
}
/* Is a period counted at all by kernel32 (uMax == TimerData by the same formula)? */
static inline int tl_counted(UINT p) { return p >= tl_pmin() && p < tl_umax(); }

/* Number of outstanding timeBeginPeriod references of this process on period p, measured by
 * draining with timeEndPeriod and restoring with timeBeginPeriod. Bounded by cap. */
static inline int tl_refs(UINT p, int cap) {
    int n = 0;
    while (n < cap && U(timeEndPeriod)(p) == 0) n++;
    for (int i = 0; i < n; i++) U(timeBeginPeriod)(p);
    return n;
}

/* Wait (bounded) until *v >= want; returns 1 on success */
static inline int tl_wait_ge(volatile LONG *v, LONG want, DWORD ms) {
    DWORD t0 = GetTickCount();
    while (*v < want) { if (GetTickCount() - t0 > ms) return 0; Sleep(1); }
    return 1;
}
static inline void tl_kill_all(UINT *ids, int n) {
    for (int i = 0; i < n; i++) if (ids[i]) { U(timeKillEvent)(ids[i]); ids[i] = 0; }
}

/* ---- child record file ------------------------------------------------- */
static FILE *tl_rec_f;
static inline void tl_rec_open(const char *path) { tl_rec_f = fopen(path, "w"); }
static inline void tl_rec(const char *k, long long v) {
    if (tl_rec_f) { fprintf(tl_rec_f, "%s %lld\n", k, v); fflush(tl_rec_f); }
}
static inline void tl_recf(long long v, const char *fmt, ...) {
    char k[96]; va_list a; va_start(a, fmt); vsnprintf(k, sizeof k, fmt, a); va_end(a); tl_rec(k, v);
}
static inline void tl_rec_close(void) { if (tl_rec_f) { fclose(tl_rec_f); tl_rec_f = NULL; } }

typedef struct { int n; char k[1024][80]; long long v[1024]; } tl_kv;
/* Runs child `name` with a fresh record file; returns exit code (0xDEAD0001 timeout). */
static inline DWORD tl_run(const char *name, const char *extra, DWORD timeout, tl_kv *kv) {
    static int seq; char path[MAX_PATH], leaf[64], args[MAX_PATH + 256];
    snprintf(leaf, sizeof leaf, "rec_%s_%d.txt", name, ++seq);
    t_tmpfile(path, sizeof path, leaf);
    DeleteFileA(path);
    snprintf(args, sizeof args, "\"%s\" %s", path, extra ? extra : "");
    DWORD code = t_run_child(name, args, timeout);
    kv->n = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[256];
        while (kv->n < 1024 && fgets(line, sizeof line, f)) {
            char k[80]; long long v;
            if (sscanf(line, "%79s %lld", k, &v) == 2) { strcpy(kv->k[kv->n], k); kv->v[kv->n] = v; kv->n++; }
        }
        fclose(f);
    }
    return code;
}
/* value of key, or dflt (use a sentinel) */
static inline long long tl_get(const tl_kv *kv, const char *k, long long dflt) {
    for (int i = kv->n - 1; i >= 0; i--) if (!strcmp(kv->k[i], k)) return kv->v[i];
    return dflt;
}
static inline long long tl_getf(const tl_kv *kv, long long dflt, const char *fmt, ...) {
    char k[96]; va_list a; va_start(a, fmt); vsnprintf(k, sizeof k, fmt, a); va_end(a); return tl_get(kv, k, dflt);
}
#define TL_MISSING (-0x7777777LL)
/* CHECK that key k recorded value want */
#define KV_EQ(kv, k, want) do { long long g_ = tl_get((kv), (k), TL_MISSING); \
    t_check(g_ == (long long)(want), __FILE__, __LINE__, k, "got %lld want %lld", g_, (long long)(want)); } while (0)

/* Child entry boilerplate: argv[1] = record path */
#define TL_CHILD_BEGIN() do { if (argc < 2) return 0xBAD; tl_rec_open(argv[1]); if (!tl_have()) { tl_rec("noexports", 1); tl_rec_close(); return 0xBAD1; } } while (0)

#endif
