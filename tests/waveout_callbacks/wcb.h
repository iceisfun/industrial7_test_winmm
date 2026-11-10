/* Shared helpers for the waveout_callbacks area: device gating, callback recorders for the five
 * CALLBACK_* delivery types, silent buffers, bounded waits. VM_TIMEOUT: 240 */
#ifndef WCB_H
#define WCB_H
#include "t.h"
#include <mmsystem.h>

#define WCB_WAIT_MS 3000          /* every wait in the area is bounded by this */
#define WCB_MAXREC  128

/* Callback types as the CALLBACK_TYPEMASK field values. */
#define WCB_NULL     0x00000000u
#define WCB_WINDOW   0x00010000u
#define WCB_THREAD   0x00020000u
#define WCB_FUNCTION 0x00030000u
#define WCB_EVENT    0x00050000u

/* ---- device counts (read once, the first call may wait for AudioSrv: never timed) ---- */
UINT wcb_nout(void);
UINT wcb_nin(void);
void wcb_note_devices(void);      /* t_note counts and caps names once per process */

/* ---- formats and buffers ---- */
void wcb_fmt(WAVEFORMATEX *f, DWORD rate, WORD bits, WORD ch);
void wcb_fmt_default(WAVEFORMATEX *f);                  /* 44100 Hz, 16 bit, 2 ch */
DWORD wcb_bytes_for_ms(const WAVEFORMATEX *f, DWORD ms);
/* Fill a silent (zero signal) buffer of `ms` milliseconds; returns the byte length. Caller frees. */
char *wcb_silence(const WAVEFORMATEX *f, DWORD ms, DWORD *len);
void wcb_hdr(WAVEHDR *h, char *data, DWORD len);

/* ---- generic recorder ---- */
typedef struct {
    UINT      msg;
    DWORD_PTR h;          /* function: device handle; window/thread: wParam */
    DWORD_PTR inst;       /* function only */
    DWORD_PTR p1;         /* function: dwParam1; window/thread: lParam */
    DWORD_PTR p2;         /* function only */
    DWORD     tid;        /* thread that delivered/received it */
    DWORD     hdr_flags;  /* *(WAVEHDR*)p1 dwFlags at delivery time (DONE/DATA only) */
    DWORD     hdr_bytes;  /* dwBytesRecorded at delivery time (DONE/DATA only) */
    DWORD_PTR phandle;    /* value of *watch at delivery time (function only) */
} wcb_rec;

typedef struct {
    volatile LONG n;      /* recorded entries (capped at WCB_MAXREC, total in `total`) */
    volatile LONG total;
    wcb_rec r[WCB_MAXREC];
    HANDLE   ev;          /* auto-reset, set after each record */
    DWORD_PTR inst_key;   /* function: the dwInstance that routes to this log */
    volatile DWORD_PTR *watch; /* function: optional pointer read at delivery time */
} wcb_log;

void wcb_log_init(wcb_log *L);
void wcb_log_free(wcb_log *L);
int  wcb_count(wcb_log *L, UINT msg);
int  wcb_wait_total(wcb_log *L, LONG want, DWORD ms);   /* wait until total >= want (bounded); 1 if reached */

/* ---- function callback ---- */
/* The callback routes on dwInstance: a registered log whose inst_key matches, else the fallback log. */
void wcb_fn_register(wcb_log *L, DWORD_PTR inst);
void wcb_fn_unregister(wcb_log *L);
void CALLBACK wcb_fn_out(HWAVEOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2);
void CALLBACK wcb_fn_in(HWAVEIN h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2);
extern wcb_log *wcb_fn_fallback;

/* ---- window callback (message-only window owned by the calling thread) ---- */
HWND wcb_win_create(void);
void wcb_win_destroy(HWND w);
/* Pump (PeekMessage, PM_REMOVE, this window only) for at most ms, recording WOM_ and WIM_ messages
 * (0x3BB..0x3C0) into L. Stops early once L->total >= want. */
void wcb_win_pump(HWND w, wcb_log *L, LONG want, DWORD ms);
/* Number of queued messages in [lo,hi] for w without removing them. */
int  wcb_win_peek_count(HWND w, UINT lo, UINT hi);

/* ---- thread callback (worker thread with a message queue) ---- */
typedef struct {
    HANDLE   th;
    DWORD    tid;
    HANDLE   ready, quit;
    wcb_log  log;         /* every thread message, including WM_USER, in arrival order */
} wcb_thr;
int  wcb_thr_start(wcb_thr *T);
void wcb_thr_stop(wcb_thr *T);
/* Wait until the worker recorded `want` messages in [lo,hi] (bounded). */
int  wcb_thr_wait(wcb_thr *T, UINT lo, UINT hi, int want, DWORD ms);
int  wcb_count_range(wcb_log *L, UINT lo, UINT hi);

/* ---- event callback ---- */
/* Count signals of an auto-reset event over a bounded window: returns 1 if signalled within ms. */
int  wcb_ev_wait(HANDLE ev, DWORD ms);

/* ---- device helpers ---- */
/* Device ids to exercise: WAVE_MAPPER then 0..n-1 (bounded to 4 ids). */
int  wcb_out_ids(UINT *ids, int cap);
int  wcb_in_ids(UINT *ids, int cap);
const char *wcb_idname(UINT id);

/* Play `nbuf` silent buffers of `ms` each through an open handle; waits for completion through
 * the header DONE bits (bounded) without relying on callbacks. Returns the number done. */
int  wcb_play_silence_poll(HWAVEOUT h, const WAVEFORMATEX *f, int nbuf, DWORD ms);

/* Sequence check: rec entries with msg `m` carry p1 == hdrs[0..n-1] in order. */
int  wcb_check_done_order(wcb_log *L, UINT m, WAVEHDR *hdrs, int n);
const char *wcb_msgname(UINT m);
#endif
