/* midiout_device: midiOut on a live synth.
 *
 * The GS software synth (midiOut 0) and the MIDI mapper are enumerated on every guest, but they can only be
 * opened when a wave device exists (the synth renders through it): mod_live() == waveOutGetNumDevs() > 0.
 * Without a wave device every area test checks the no-device contract (noaudio.c) and skips its live part.
 *
 * Rules every test follows:
 *  - The synth is single-open per process: mod_open() refuses (CHECK fails) if a handle of this area is still
 *    open, and every test closes what it opened through mod_close() before returning (also on early exit).
 *  - Notes: velocity <= 20, length <= 100 ms, all-notes-off (CC 123) + sound-off (CC 120) on 16 channels before
 *    each close (mod_close does it).
 *  - Callback functions only record (no midiOut* call inside them).
 *  - Waits are bounded (mod_wait_* poll every 5 ms, <= 2 s).
 *
 * The first winmm call in a process may wait for AudioSrv: counts are read once (mod_nwave/mod_nout), never timed.
 */
#ifndef MOD_H
#define MOD_H
#include "t.h"
#include <mmsystem.h>

#ifndef MIDIERR_STILLPLAYING
#define MIDIERR_STILLPLAYING 65
#endif
#ifndef DRV_QUERYFUNCTIONINSTANCEIDSIZE
#define DRV_QUERYFUNCTIONINSTANCEIDSIZE 0x812
#endif
#ifndef MODM_GETVOLUME
#define MODM_GETVOLUME 10
#endif
#ifndef MODM_GETDEVCAPS
#define MODM_GETDEVCAPS 2
#endif
#ifndef MODM_GETNUMDEVS
#define MODM_GETNUMDEVS 1
#endif

#define NELEM(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define MOD_SENT ((HMIDIOUT)(ULONG_PTR)0x5A5A5A5A5A5A5A5Aull)
#define MOD_HDR_BADMASK 0xFFFFCFD0u

/* ---- environment */
UINT mod_nout(void);          /* midiOutGetNumDevs, read once */
UINT mod_nwave(void);         /* waveOutGetNumDevs, read once */
int  mod_live(void);          /* mod_nwave() > 0 && mod_nout() > 0 */
/* REQUIRE the usual exports and skip when there is no wave device (after the caller's noaudio checks) */
#define MOD_NEED_LIVE() do { REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); REQUIRE(U(midiOutShortMsg)); \
    REQUIRE(U(midiOutReset)); \
    if (!mod_live()) { t_skip("no wave device (waveOutGetNumDevs=%u): synth not openable", mod_nwave()); return; } } while (0)

double mod_now(void);         /* ms, QPC */

/* ---- open/close */
MMRESULT mod_open(HMIDIOUT *ph, UINT id, DWORD_PTR cb, DWORD_PTR inst, DWORD flags); /* raw open + bookkeeping */
HMIDIOUT mod_open_null(UINT id);   /* CALLBACK_NULL; CHECK 0; NULL on failure */
void     mod_quiet(HMIDIOUT h);    /* CC 123 + CC 120 on 16 channels (results ignored) */
void     mod_close(HMIDIOUT h);    /* quiet, reset, close; CHECK close == 0 */
int      mod_open_count(void);     /* handles opened through mod_open and not closed through mod_close */
void     mod_forget(HMIDIOUT h);   /* a handle closed by other means (raw midiOutClose already returned 0) */

/* ---- callback recorder (CALLBACK_FUNCTION) */
typedef struct {
    HMIDIOUT h; UINT msg; DWORD_PTR inst, p1, p2; DWORD tid; double t;
    DWORD hflags;   /* MIDIHDR dwFlags snapshot when p1 is a header (MOM_DONE) */
} mod_rec_t;
#define MOD_MAXREC 512
void CALLBACK mod_cb(HMIDIOUT h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2);
void mod_rec_reset(void);
int  mod_rec_count(void);
int  mod_rec_count_msg(UINT msg);
const mod_rec_t *mod_rec(int i);
int  mod_wait_msg(UINT msg, int n, DWORD timeout_ms);  /* until mod_rec_count_msg(msg) >= n; 1 if reached */

/* thread message pump helpers for CALLBACK_THREAD / CALLBACK_WINDOW */
int  mod_pump_count(HWND w, UINT msg, DWORD timeout_ms, int want, WPARAM *wp, LPARAM *lp); /* (HWND)-1 = thread */
void mod_pump_drain(HWND w);
HWND mod_window(void);         /* message-only STATIC window or NULL */

/* ---- headers */
void mod_hdr(MIDIHDR *m, const void *buf, DWORD len);
int  mod_wait_done(volatile MIDIHDR *m, DWORD timeout_ms);  /* until MHDR_DONE; 1 if reached */
/* prepare + long msg + wait DONE + unprepare; CHECKs each step; returns the LongMsg result */
MMRESULT mod_send_long(HMIDIOUT h, const void *buf, DWORD len);

extern const BYTE mod_gs_reset[11];   /* F0 41 10 42 12 40 00 7F 00 41 F7 */
extern const BYTE mod_gm_on[6];       /* F0 7E 7F 09 01 F7 */
extern const BYTE mod_gm_off[6];      /* F0 7E 7F 09 02 F7 */

/* short message builders */
#define MOD_MSG(st, d1, d2) ((DWORD)(BYTE)(st) | ((DWORD)(BYTE)(d1) << 8) | ((DWORD)(BYTE)(d2) << 16))
/* one quiet note: note on (velocity <= 20), Sleep(ms <= 100), note off; returns the note-on result */
MMRESULT mod_note(HMIDIOUT h, int ch, int note, int vel, DWORD ms);
#endif
