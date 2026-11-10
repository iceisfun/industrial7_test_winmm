/* Shared helpers for the time_events area (timeSetEvent / timeKillEvent).
 *
 * Model (winmm.dll 10.0.19041.546, timeSetEvent 0x2a20, timeKillEvent 0x2e50, timeThreadSetEvent 0x2b90,
 * timeSetTimerEvent 0x2c90, timeThread 0x26c0, TimeInit 0x23c0, TimeInitThread 0x2d7c):
 *   - 16 event slots per process; id = generation + slot, the 16-bit generation counter steps by 0x10 on every
 *     successful slot allocation (skipping 0), slot = id & 0xF is the lowest free slot.
 *   - fuEvent & 0xFFFFFE0E != 0 -> 0. wPeriodMin <= uDelay <= 1000000 else 0.
 *   - period = clamp(uResolution, wPeriodMin, TimerData); uDelay < period -> TimerData. TimerData is the coarsest
 *     timer resolution in ms (ceil, normally 16), the same value as kernel32's uMax, so a TimerData period is a
 *     timeBeginPeriod no-op while finer periods take one per-period reference that the event holds until it dies.
 *   - One worker thread (THREAD_PRIORITY_TIME_CRITICAL, alertable wait) runs every event serially:
 *     fuEvent & 0xF0 == 0 -> fptc(id, 0, dwUser, 0, 0) unless fptc is NULL; 0x10 SetEvent; 0x20 PulseEvent;
 *     anything else (0x30, 0x40, 0x80, ...) does nothing. One-shots free their slot after the dispatch.
 *   - timeKillEvent: slot(id & 0xF).id != id or no timer thread -> 97 (TIMERR_NOCANDO); otherwise 0.
 * Tests must leave no live event behind (te_free_slots() == 16 is checked by several tests) and must keep every
 * timeBeginPeriod/timeEndPeriod probe balanced.
 */
#ifndef TE_H
#define TE_H
#include "t.h"
#include <mmsystem.h>

#ifndef TIME_KILL_SYNCHRONOUS
#define TIME_KILL_SYNCHRONOUS 0x0100
#endif
#ifndef TIME_CALLBACK_EVENT_PULSE
#define TIME_CALLBACK_EVENT_PULSE 0x0020
#endif
#ifndef TIME_CALLBACK_EVENT_SET
#define TIME_CALLBACK_EVENT_SET 0x0010
#endif

#define TE_NOCANDO      97u          /* TIMERR_NOCANDO */
#define TE_FLAG_MASK    0xFFFFFE0Eu  /* fuEvent bits that make timeSetEvent return 0 */
#define TE_MAX_DELAY    1000000u
#define TE_SLOTS        16
#define TE_LONG         60000u       /* a delay that never fires during a test */
#define TE_COARSE       1000u        /* a resolution that clamps to TimerData (no period reference) */
#define TE_UNKNOWN_ID   0x12345u     /* slot 5, never a generation-tagged live id (ids are < 0x10010) */

#define TE_REQUIRE() do { REQUIRE(U(timeSetEvent)); REQUIRE(U(timeKillEvent)); } while (0)

static inline UINT te_set(UINT d, UINT r, LPTIMECALLBACK f, DWORD_PTR u, UINT fl) { return U(timeSetEvent)(d, r, f, u, fl); }
static inline UINT te_kill(UINT id) { return U(timeKillEvent)(id); }

/* Recording context for function callbacks (dwUser = pointer to it). Always global/static storage. */
typedef struct te_ctx {
    volatile LONG count;
    volatile UINT id, msg;
    volatile DWORD_PTR user, dw1, dw2;
    volatile DWORD tid;
    volatile int prio;
    volatile DWORD t_first, t_last;
    volatile LONG bad_args;            /* calls whose (msg, dw1, dw2) were not (0, 0, 0) */
    volatile LONG inside, max_inside;  /* concurrency tracking */
    volatile DWORD sleep_ms;           /* sleep inside the callback (first call only when sleep_once) */
    volatile int sleep_once;
    HANDLE fired;                      /* SetEvent on every call, if set */
    HANDLE entered;                    /* SetEvent on entry, if set */
    volatile LONG done;                /* incremented when the callback returns */
} te_ctx;

void CALLBACK te_cb_ctx(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR dw1, DWORD_PTR dw2);
void te_ctx_init(te_ctx *c);
void te_ctx_free(te_ctx *c);

/* Wait (bounded) until c->count >= n. Returns the final count. */
LONG te_wait_count(te_ctx *c, LONG n, DWORD max_ms);

/* Number of events that can be allocated right now (0..16); every probe event is killed again. */
int te_free_slots(void);
/* Allocate+kill one probe event (NULL callback, one-shot, long delay); returns its id (0 on failure). */
UINT te_probe_id(void);

/* Timer-period accounting probes (kernel32 per-period counts, through winmm's time* exports). */
UINT te_pmin(void);   /* TIMECAPS.wPeriodMin */
UINT te_umax(void);   /* first period >= wPeriodMin that timeEndPeriod accepts with nothing counted (normally 16) */
/* Is a reference on period p held right now? Probes with timeEndPeriod and restores with timeBeginPeriod. */
int te_period_held(UINT p);
/* Bitmask (bit p - pmin) of every held period in [pmin, umax). */
unsigned long long te_held_mask(void);
/* Expected event period from the timeSetEvent clamp. */
UINT te_expect_period(UINT delay, UINT res);

DWORD te_now(void);   /* timeGetTime via winmm */
DWORD te_thread_priority(DWORD tid);
#endif
