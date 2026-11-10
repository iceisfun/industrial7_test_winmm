/* time_basic: shared helpers for the timeGetTime / timeGetSystemTime / timeGetDevCaps /
 * timeBeginPeriod / timeEndPeriod area.
 *
 * On Windows 10 x64 the five winmm exports are 7-byte "jmp [rip+disp32]" thunks into
 * kernel32 (imported through api-ms-win-mm-time-l1-1-0). The kernel32 bodies share one
 * lazily initialised state (TimeInit, run once through InitOnceExecuteOnce):
 *   NtQueryTimerResolution(&maxRes, &minRes, &cur)
 *   wPeriodMin (TDD_MAXRESOLUTION) = (minRes + 9900) / 10000, at least 1   (10 if the query fails)
 *   uMax                           = (maxRes + 9999) / 10000 if that sum < 550000, else 55
 * timeBeginPeriod/timeEndPeriod keep a 16-bit reference count for every period in
 * [wPeriodMin, uMax); periods >= uMax are accepted and ignored, periods < wPeriodMin fail.
 * Counts are per process; every test that calls timeBeginPeriod balances it before returning.
 */
#ifndef TB_H
#define TB_H
#include "t.h"
#include <mmsystem.h>

UDECL(NTSTATUS, NtQueryTimerResolution, (PULONG MaximumTime, PULONG MinimumTime, PULONG CurrentTime));

#define TB_NOERROR  0u
#define TB_NOCANDO  97u   /* TIMERR_NOCANDO */
#define TB_STRUCT   129u  /* TIMERR_STRUCT  */

static inline int tb_res(ULONG *mx, ULONG *mn, ULONG *cur) {
    NtQueryTimerResolution_pfn q = (NtQueryTimerResolution_pfn)t_proc_in("ntdll.dll", "NtQueryTimerResolution");
    ULONG a = 0, b = 0, c = 0;
    if (!q || q(&a, &b, &c) < 0) return 0;
    if (mx) *mx = a;
    if (mn) *mn = b;
    if (cur) *cur = c;
    return 1;
}
static inline ULONG tb_cur_res(void) { ULONG c = 0; tb_res(NULL, NULL, &c); return c; }

/* Bounds the kernel32 implementation derives from NtQueryTimerResolution (see header comment). */
static inline UINT tb_pmin(void) {
    ULONG mx, mn;
    if (!tb_res(&mx, &mn, NULL)) return 10;
    UINT v = (mn + 9900u) / 10000u;
    return v ? v : 1;
}
static inline UINT tb_umax(void) {
    ULONG mx, mn;
    if (!tb_res(&mx, &mn, NULL)) return 55;
    return (mx + 9999u < 550000u) ? (mx + 9999u) / 10000u : 55u;
}

/* KUSER_SHARED_DATA.InterruptTime (100 ns units), read the same way kernel32 does. */
static inline ULONGLONG tb_itime(void) {
    volatile LONG *base = (volatile LONG *)(ULONG_PTR)0x7FFE0008;
    LONG hi1, hi2; ULONG lo;
    do { hi1 = base[1]; lo = (ULONG)base[0]; hi2 = base[2]; } while (hi1 != hi2);
    return ((ULONGLONG)(ULONG)hi1 << 32) | lo;
}

/* signed distance between two DWORD millisecond counters (wrap safe) */
static inline LONG tb_diff(DWORD later, DWORD earlier) { return (LONG)(later - earlier); }

static inline MMRESULT tb_begin(UINT p) { return U(timeBeginPeriod)(p); }
static inline MMRESULT tb_end(UINT p) { return U(timeEndPeriod)(p); }

/* Drain a period's count completely (used only as cleanup after a failed CHECK). */
static inline void tb_drain(UINT p) { for (int i = 0; i < 70000 && tb_end(p) == 0; i++) {} }

#endif
