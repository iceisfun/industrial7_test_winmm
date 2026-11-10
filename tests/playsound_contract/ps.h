/* Shared helpers for the playsound_contract area.
 *
 * Every call that names something passes SND_NODEFAULT, names only things that do not exist or silent images this
 * area writes itself, and every TEST ends with PlaySound(NULL, NULL, 0). Expectations come from the winmm decomp
 * (sndPlaySoundI, sndMessage, soundInitWavHdr, mmWndProc) and were run on the Win10 VM with and without a wave
 * device; see EVIDENCE.md. */
#ifndef PS_H
#define PS_H
#include "t.h"
#include <mmsystem.h>

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif
#ifndef WAVE_FORMAT_EXTENSIBLE
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
#endif

#define PTEST(n) TEST(playsound_contract_##n)

/* sndPlaySoundI rejects any of these bits before doing anything else. */
#define PS_BAD_MASK   0xFF80DF20u
#define PS_VALID_MASK 0x007F20DFu

/* Entry points that reach sndPlaySoundI. PlaySound (ordinal 10) and the unnamed ordinal 2 take the ANSI form. */
enum { EP_A, EP_W, EP_PS, EP_ORD2, EP_SNDA, EP_SNDW, EP_N };
extern const char *const ps_ep_name[EP_N];

/* What psz is: NULL, a wide string (converted with CP_ACP for the ANSI entry points), or a raw pointer/integer
 * passed unchanged to every entry point (memory images, MAKEINTRESOURCE ids, SND_ALIAS_ID values). */
enum { PSZ_NULL, PSZ_WIDE, PSZ_RAW };

int  ps_have_all(void);                     /* every entry point resolves */
BOOL ps_call(int ep, int kind, const void *psz, HMODULE hmod, DWORD f);  /* sndPlaySound* only when hmod == NULL */
BOOL ps_stop(void);                         /* PlaySoundW(NULL, NULL, 0) */
void ps_quiesce(void);                      /* stop, let the notify thread drain, stop again */
UINT ps_ndev(void);                         /* waveOutGetNumDevs, read once */
BOOL ps_is_playing(void);
BOOL ps_wait_playing(DWORD ms);              /* poll ps_is_playing up to ms */                   /* "" + SND_NOSTOP probe: FALSE from the API means a sound is playing */

/* Expected result of a sync play of an image/file the parser accepts: the wave mapper open decides. */
#define PS_SYNC_VALID_EXPECT() (ps_ndev() > 0)

/* ---- in-memory WAVE images ---------------------------------------------------------------------- */
typedef struct { BYTE *b; DWORD n, cap; } ps_img;
void  img_begin(ps_img *m, BYTE *buf, DWORD cap);          /* "RIFF" <len> "WAVE", zero-fills buf */
void  img_raw(ps_img *m, const void *p, DWORD n);
void  img_chunk(ps_img *m, const char *id, const void *data, DWORD n);         /* id, n, data (no pad) */
void  img_chunk_decl(ps_img *m, const char *id, const void *data, DWORD n, DWORD declared);
void  img_pad(ps_img *m);                                    /* one 0 byte if n is odd */
DWORD img_end(ps_img *m);                                    /* RIFF len = n - 8, returns n */
void  img_set_riff_len(ps_img *m, DWORD len);

#pragma pack(push, 1)
typedef struct { WORD tag, ch; DWORD rate, avg; WORD align, bits, cb; WORD valid; DWORD mask; GUID sub; } ps_fmt;
#pragma pack(pop)
void  fmt_pcm(ps_fmt *f, WORD ch, DWORD rate, WORD bits);
/* Canonical image: PCM 8 kHz 16-bit mono, ms of zeros. Returns the size. */
DWORD ps_make_pcm(BYTE *buf, DWORD cap, WORD ch, DWORD rate, WORD bits, DWORD ms);
DWORD ps_make_silent(BYTE *buf, DWORD cap);                  /* 8 kHz 16-bit mono, 100 ms */

/* ---- scratch files ------------------------------------------------------------------------------ */
const WCHAR *ps_tmpdirW(void);                               /* t_tmpdir() widened, trailing backslash */
BOOL  ps_write_fileW(const WCHAR *path, const void *p, DWORD n);
void  ps_pathW(WCHAR *out, size_t cch, const WCHAR *leaf);   /* tmpdir + leaf */

/* Bounded failure printing for table sweeps: every row is a counted check; only the first 12 mismatches of a
 * sweep are printed, the rest are folded into one failing summary check by ps_sweep_end(). */
void ps_sweep_begin(void);
void ps_expect(int ok, const char *file, int line, const char *fmt, ...);
void ps_sweep_end(const char *file, int line);
#define PS_EXPECT(ok, ...) ps_expect(!!(ok), __FILE__, __LINE__, __VA_ARGS__)
#define PS_SWEEP_END()     ps_sweep_end(__FILE__, __LINE__)
#endif
