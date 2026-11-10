/* Shared helpers for the waveout_open_contract area (waveOutOpen / waveOutClose / waveOutGetID).
 *
 * Validation ladder of winmmbase!waveOutOpen (0x103b0), the order every table in this area is built from:
 *   1. pwfx == NULL                                              -> 11 (nothing written to *phwo)
 *   2. dwCallback != 0, ((fdwOpen >> 16) & 7) == 1, !IsWindow     -> 11 (nothing written)
 *   3. uDeviceID 0xFFFF is rewritten to WAVE_MAPPER
 *   4. mapper: fdwOpen & 0x7E04, other ids: fdwOpen & 0x7E10       -> 10 (nothing written)
 *   5. not WAVE_FORMAT_QUERY: phwo == NULL -> 11, else *phwo = NULL
 *   6. zero waveOut devices                                      -> 2
 *   7. driver lookup (WAVE_MAPPED forces the mapper driver) fails -> 2
 *   8. WODM_OPEN to the driver: its answer is returned
 */
#ifndef WO_H
#define WO_H
#include "t.h"
#include <mmsystem.h>
#include <mmreg.h>

#ifndef WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE
#define WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE 0x0010
#endif
#define WO_MASK_MAPPER  0x7E04u
#define WO_MASK_DEVICE  0x7E10u
#define WO_SENTINEL     ((HWAVEOUT)(ULONG_PTR)0x5A5A5A5Au)
#define WO_NOWND        ((DWORD_PTR)0x1234)     /* a value that is not a window: IsWindow() is FALSE */
#define WO_BADID        9999u
#define WO_UNSET        0xFFFFFFFFu             /* "no hard expectation" marker in tables */

/* Device count, read once per process (first call may block while AudioSrv starts: never timed). */
UINT wo_ndev(void);
UINT wo_nwavein(void);
/* Name of waveOut device id (or mapper) for notes; "" on failure. */
const char *wo_capsname(UINT id);

/* Formats */
void wo_pcm(WAVEFORMATEX *wf, WORD ch, DWORD rate, WORD bits);
void wo_std(WAVEFORMATEX *wf);                  /* PCM 44100 Hz 16-bit stereo */
void wo_ext(WAVEFORMATEXTENSIBLE *x, WORD ch, DWORD rate, WORD bits, WORD validbits, DWORD mask, int is_float);

/* Thin call wrappers (each resolves through U()). */
MMRESULT wo_open(HWAVEOUT *ph, UINT id, const WAVEFORMATEX *wf, DWORD_PTR cb, DWORD_PTR inst, DWORD fl);
MMRESULT wo_close(HWAVEOUT h);
MMRESULT wo_getid(HWAVEOUT h, UINT *id);
/* Open with the standard format and CALLBACK_NULL; returns the code, *ph valid only on 0. */
MMRESULT wo_open_std(HWAVEOUT *ph, UINT id, DWORD fl);
/* Close a handle that a call returned with r == 0 and without WAVE_FORMAT_QUERY. */
void wo_release(MMRESULT r, DWORD fl, HWAVEOUT h);

/* Expected code of the parameter gates (steps 1..5), or WO_UNSET when the call reaches the device part.
 * phwo_null: phwo is NULL; cb/fl as passed. */
UINT wo_gate(UINT id, int pwfx_null, DWORD_PTR cb, DWORD fl, int phwo_null, int cb_is_window);
/* Expected code for the whole call when it is decided without a driver: the gates, then zero devices -> 2,
 * then an id that is neither mapper nor < count (and no WAVE_MAPPED) -> 2. WO_UNSET otherwise. */
UINT wo_expect_nodriver(UINT id, int pwfx_null, DWORD_PTR cb, DWORD fl, int phwo_null, int cb_is_window);

/* user32 helpers (resolved at run time; the corpus does not link user32). */
HWND wo_msgwnd(void);                            /* message-only STATIC window owned by this thread */
void wo_destroy(HWND w);
BOOL wo_iswindow(HWND w);
/* Bounded pump: waits up to ms for message msg (any window, this thread), returns 1 and fills *out if seen. */
int  wo_wait_msg(UINT msg, DWORD ms, MSG *out);
void wo_drain(void);

/* CALLBACK_FUNCTION recorder (records only; never calls waveOut* inside). */
typedef struct { volatile LONG n; UINT msg[16]; HWAVEOUT h[16]; DWORD_PTR inst[16]; DWORD_PTR p1[16]; DWORD tid[16]; } wo_rec;
void CALLBACK wo_rec_cb(HWAVEOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2);
/* dwInstance must point at the wo_rec. Waits up to ms for at least n records. */
int  wo_rec_wait(wo_rec *r, LONG n, DWORD ms);

/* Run fn(arg) on a new thread and wait (bounded). Returns the thread's exit code or 0xDEAD0001 on timeout. */
DWORD wo_on_thread(DWORD (WINAPI *fn)(void *), void *arg, DWORD ms);

/* Zero-device half of a device test: asserts the zero-device contract for the given flags and returns 1 when
 * the caller must stop (no waveOut device). */
int wo_nodev_contract(DWORD fl);
#endif
