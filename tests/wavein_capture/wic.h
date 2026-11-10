/* wavein_capture: shared helpers for the waveIn capture-path tests.
 *
 * Device gating: wic_ndev() reads waveInGetNumDevs once (first call may block while AudioSrv starts; never
 * timed) and caches it. Every TEST either runs the zero-device / out-of-range-id contract (always) or the
 * device half (only when wic_ndev() > 0, via NEED_DEV()).
 * Recording budget: buffers are short (<= 0.25 s each, <= ~1 s per test), every wait is bounded.
 */
#ifndef WIC_H
#define WIC_H
#include "t.h"
#include <mmsystem.h>
#include <mmreg.h>

#define WIC_MAXB 24

#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 3
#endif
#ifndef WAVE_FORMAT_EXTENSIBLE
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE
#endif

UINT wic_ndev(void);          /* cached waveInGetNumDevs; notes count + caps names on first call */
UINT wic_nout(void);          /* cached waveOutGetNumDevs (output-only guest detection) */
#define NEED_DEV() do { if (!wic_ndev()) { t_skip("no waveIn device (waveInGetNumDevs = 0)"); return; } } while (0)

/* formats */
void wic_pcm(WAVEFORMATEX *f, DWORD rate, WORD bits, WORD ch);
void wic_float(WAVEFORMATEX *f, DWORD rate, WORD ch);                 /* WAVE_FORMAT_IEEE_FLOAT, 32-bit */
void wic_ext(WAVEFORMATEXTENSIBLE *f, DWORD rate, WORD bits, WORD ch, int is_float);
DWORD wic_bytes_ms(const WAVEFORMATEX *f, DWORD ms);                   /* block aligned */

/* one open capture stream with CALLBACK_EVENT (or other callback, see wic_open_cb) */
typedef struct {
    HWAVEIN h;
    HANDLE  ev;                     /* auto-reset event used as CALLBACK_EVENT target (NULL otherwise) */
    WAVEFORMATEXTENSIBLE fx;        /* format the stream was opened with */
    int     nb;
    WAVEHDR hdr[WIC_MAXB];
    char   *mem[WIC_MAXB];
} wic_s;

static inline const WAVEFORMATEX *wic_wfx(const wic_s *s) { return &s->fx.Format; }

MMRESULT wic_open(wic_s *s, UINT id, const WAVEFORMATEX *f);            /* CALLBACK_EVENT */
MMRESULT wic_open_flags(wic_s *s, UINT id, const WAVEFORMATEX *f, DWORD extra);
MMRESULT wic_open_cb(wic_s *s, UINT id, const WAVEFORMATEX *f, DWORD_PTR cb, DWORD_PTR inst, DWORD flags);
MMRESULT wic_open_std(wic_s *s, UINT id);                               /* 44100/16/1 event */
/* allocate buffer i of `bytes`, fill 0xCD, prepare it; returns the prepare result */
MMRESULT wic_buf(wic_s *s, int i, DWORD bytes);
MMRESULT wic_add(wic_s *s, int i);
int  wic_wait_done(wic_s *s, int i, DWORD ms);                         /* 1 when DONE within ms */
int  wic_wait_all(wic_s *s, int from, int to, DWORD ms);                /* buffers [from,to) all DONE */
void wic_pump(DWORD ms);                                                /* bounded sleep on the event-free path */
/* Reset + unprepare + close + free; returns the waveInClose result (0 when nothing was open). */
MMRESULT wic_teardown(wic_s *s);
DWORD wic_pos(HWAVEIN h, UINT type, UINT *out_type);                   /* GetPosition helper; 0xFFFFFFFF on error */

/* small TIME helpers */
static inline DWORD wic_now(void) { return GetTickCount(); }

/* callback log for CALLBACK_FUNCTION */
typedef struct { UINT msg; HWAVEIN h; DWORD_PTR inst, p1, p2; DWORD tid; DWORD flags; DWORD rec; DWORD len; } wic_cbrec;
extern volatile LONG wic_cbn;
extern wic_cbrec wic_cblog[64];
void wic_cb_reset(void);
void CALLBACK wic_cbproc(HWAVEIN h, UINT msg, DWORD_PTR inst, DWORD_PTR p1, DWORD_PTR p2);
int  wic_cb_count(UINT msg);

#endif
