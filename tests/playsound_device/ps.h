/* playsound_device: shared helpers.
 *
 * Every PlaySound call in this area uses an in-memory or in-scratch WAVE image that is silent or quiet
 * (<= -30 dBFS) and always passes SND_NODEFAULT, so no system sound can ever play.
 * Device gating: ps_ndev() reads waveOutGetNumDevs() once per process (the first winmm call may wait for AudioSrv:
 * it is never timed). PlaySound always opens WAVE_MAPPER, so "a device exists" means ps_ndev() > 0.
 *
 * Probe for "a sound is currently playing": PlaySoundW(L"", NULL, SND_NOSTOP | SND_NODEFAULT).
 *   sndPlaySoundI checks SND_NOSTOP against the current wave header (lpWavHdr) before anything else and returns
 *   FALSE while one is open; otherwise an empty name (no SND_MEMORY) returns TRUE before sndMessage, so the probe
 *   neither stops nor posts anything. ps_playing() = !probe.
 */
#ifndef PS_H
#define PS_H
#include "t.h"
#include <mmsystem.h>

/* PlaySound flag values (mmsystem.h names, spelled out for the bits some headers lack). */
#define PS_SYNC       0x00000000u
#define PS_ASYNC      0x00000001u
#define PS_NODEFAULT  0x00000002u
#define PS_MEMORY     0x00000004u
#define PS_LOOP       0x00000008u
#define PS_NOSTOP     0x00000010u
#define PS_PURGE      0x00000040u
#define PS_APPLICATION 0x00000080u
#define PS_NOWAIT     0x00002000u
#define PS_ALIAS      0x00010000u
#define PS_FILENAME   0x00020000u
#define PS_RESOURCE   0x00040004u
#define PS_SENTRY     0x00080000u
#define PS_RING       0x00100000u
#define PS_SYSTEM     0x00200000u

#define PS_MEMSYNC   (PS_MEMORY | PS_SYNC | PS_NODEFAULT)
#define PS_MEMASYNC  (PS_MEMORY | PS_ASYNC | PS_NODEFAULT)
#define PS_MEMLOOP   (PS_MEMORY | PS_ASYNC | PS_LOOP | PS_NODEFAULT)

/* ---- environment ------------------------------------------------------- */
UINT   ps_ndev(void);              /* waveOutGetNumDevs(), read once; notes count and caps names */
int    ps_have_api(void);          /* PlaySoundW/PlaySoundA/sndPlaySoundW/sndPlaySoundA resolve */
double ps_now(void);               /* milliseconds, QPC */
void   ps_stop(void);              /* PlaySoundW(NULL, NULL, 0), checked TRUE */
int    ps_playing(void);           /* 1 while a sound is open (see header comment) */
int    ps_wait_playing(int want, DWORD timeout_ms);  /* poll ps_playing() == want every 10 ms; 1 if reached */

/* ---- WAVE image builder -------------------------------------------------
 * The buffer is a zeroed heap block of 'cap' bytes; writes never exceed it. Bytes after the image stay zero, so
 * parsers that read a little past the claimed end (the memory parser has no length bound) see zeros. */
typedef struct { BYTE *b; DWORD cap; DWORD n; } wb_t;
void  wb_init(wb_t *w, DWORD cap);                        /* "RIFF" <len> "WAVE" */
void  wb_init_magic(wb_t *w, DWORD cap, const char riff[4], const char wave[4]);
void  wb_raw(wb_t *w, const void *p, DWORD len);
void  wb_u16(wb_t *w, WORD v);
void  wb_u32(wb_t *w, DWORD v);
void  wb_fill(wb_t *w, BYTE v, DWORD len);
void  wb_chunk(wb_t *w, const char id[4], DWORD len);    /* header only */
DWORD wb_avg(WORD ch, DWORD rate, WORD bits);
/* "fmt " with cb 16: consistent PCM/float style fields (align = ch*bits/8, avg = rate*align). */
void  wb_fmt(wb_t *w, WORD tag, WORD ch, DWORD rate, WORD bits);
/* Raw "fmt " chunk: cb is the chunk length; fields are written up to cb (cbSize at 16 if cb >= 18, then extra). */
void  wb_fmt_raw(wb_t *w, DWORD cb, WORD tag, WORD ch, DWORD rate, DWORD avg, WORD align, WORD bits,
                 WORD cbsize, const void *extra, DWORD extralen);
/* WAVE_FORMAT_EXTENSIBLE (cb 40, cbSize 22) */
void  wb_fmt_ext(wb_t *w, WORD ch, DWORD rate, WORD bits, WORD valid, DWORD mask, const GUID *sub, DWORD avg_override);
void  wb_data(wb_t *w, DWORD bytes, BYTE fill);           /* "data" chunk filled with 'fill' */
void  wb_finish(wb_t *w);                                 /* RIFF length = n - 8 */
void  wb_set_riff(wb_t *w, DWORD riff_len);
void  wb_free(wb_t *w);

/* Complete images: silent PCM/float of 'ms' milliseconds (cb 16 fmt; 8-bit silence 0x80). */
void  ps_silent(wb_t *w, WORD tag, WORD ch, DWORD rate, WORD bits, DWORD ms);
/* 16-bit mono sine at about -30 dBFS (amplitude 1036). */
void  ps_tone(wb_t *w, DWORD rate, DWORD hz, DWORD ms);

extern const GUID ps_sub_pcm, ps_sub_float, ps_sub_bogus;

/* Write an image to a file in the scratch dir; returns the wide path in out. */
int   ps_write_file(const wb_t *w, DWORD nbytes, const char *leaf, WCHAR *out, size_t cch);

/* Call helpers (each resolves through the harness; never linked). */
BOOL  psW(const void *p, DWORD f);              /* PlaySoundW(p, NULL, f) */
BOOL  psA(const void *p, DWORD f);              /* PlaySoundA(p, NULL, f) */
BOOL  spsW(const void *p, UINT f);              /* sndPlaySoundW */
BOOL  spsA(const void *p, UINT f);              /* sndPlaySoundA */
/* Timed sync play: returns BOOL, *ms = wall time of the call. */
BOOL  ps_timed(BOOL (*fn)(const void *, DWORD), const void *p, DWORD f, double *ms);

#define PS_REQ() do { REQUIRE(U(PlaySoundW)); REQUIRE(U(PlaySoundA)); REQUIRE(U(sndPlaySoundW)); \
    REQUIRE(U(sndPlaySoundA)); REQUIRE(U(waveOutGetNumDevs)); (void)ps_ndev(); } while (0)
#endif
