/* Shared helpers for the waveout_playback area (waveOutPause / Restart / Reset / BreakLoop / GetPosition).
 *
 * Device gating: wp_nout() reads waveOutGetNumDevs() once (the first winmm call of the process may wait for AudioSrv;
 * it is never timed). Device tests call WP_NEED_DEV(), which checks the no-device open contract and skips when the
 * guest has no waveOut device. All audio written is silence unless a test announces a TONE (<= -30 dBFS). */
#ifndef WP_H
#define WP_H
#include "t.h"
#include <mmsystem.h>

#define WP_RATE   44100
#define WP_CH     2
#define WP_BITS   16
#define WP_ALIGN  (WP_CH * WP_BITS / 8)
#define WP_MS2FR(ms) ((DWORD)((DWORDLONG)(ms) * WP_RATE / 1000))

/* Bounded waits (ms). */
#define WP_OPEN_WAIT   2000
#define WP_DONE_SLACK  3000   /* added to the nominal play time of what is queued */

UINT wp_nout(void);           /* waveOutGetNumDevs, read once */
UINT wp_nin(void);            /* waveInGetNumDevs, read once */
int  wp_have_dev(void);       /* wp_nout() > 0, with the device list noted once */
/* Contract half when there is no device (open of the mapper and of id 0 -> BADDEVICEID), then skip. */
int  wp_need_dev(void);
#define WP_NEED_DEV() do { if (!wp_need_dev()) return; } while (0)

void wp_fmt(WAVEFORMATEX *wf, DWORD rate, WORD ch, WORD bits);
void wp_fmt_std(WAVEFORMATEX *wf);   /* 44100 / 16 / 2 */

typedef struct {
    HWAVEOUT h;
    HANDLE ev;               /* CALLBACK_EVENT target (auto-reset) or NULL */
    WAVEFORMATEX wf;
    UINT id;
} wp_dev;

/* Opens id with CALLBACK_EVENT and consumes the WOM_OPEN signal. Returns the MMRESULT. */
MMRESULT wp_open_ex(wp_dev *d, UINT id, const WAVEFORMATEX *wf);
MMRESULT wp_open(wp_dev *d);          /* WAVE_MAPPER, 44100/16/2 */
/* Reset + unprepare nothing + close; tolerant (for cleanup). Returns the waveOutClose result. */
MMRESULT wp_close(wp_dev *d);

/* Silence buffer of ms milliseconds (or bytes) for d's format; freed with wp_free_hdr. */
void wp_hdr_ms(WAVEHDR *wh, const wp_dev *d, DWORD ms);
void wp_hdr_bytes(WAVEHDR *wh, DWORD bytes);
/* Fills a 16-bit stereo/mono buffer with a sine at about -30 dBFS peak, 5 ms ramps. */
void wp_fill_tone(WAVEHDR *wh, const wp_dev *d, int hz);
void wp_free_hdr(WAVEHDR *wh);

MMRESULT wp_prep(wp_dev *d, WAVEHDR *wh);
MMRESULT wp_write(wp_dev *d, WAVEHDR *wh);
MMRESULT wp_unprep(wp_dev *d, WAVEHDR *wh);

/* Waits until every header in hs[0..n) has WHDR_DONE, bounded by ms. Returns 1 when all done. */
int  wp_wait_all(wp_dev *d, WAVEHDR *hs, int n, DWORD ms);
int  wp_wait_done(wp_dev *d, WAVEHDR *wh, DWORD ms);

/* waveOutGetPosition with the given type; returns the MMRESULT, *val gets u.cb (all union members alias it),
 * *type_out gets the wType that came back. */
MMRESULT wp_pos(HWAVEOUT h, UINT type, DWORD *val, UINT *type_out);
DWORD    wp_samples(HWAVEOUT h);      /* TIME_SAMPLES, 0xFFFFFFFF on error */
DWORD    wp_bytes(HWAVEOUT h);        /* TIME_BYTES, 0xFFFFFFFF on error */

/* A handle value that was a waveOut handle and is now closed (needs a device); NULL if unavailable. */
HWAVEOUT wp_closed_handle(void);

/* Values that are never valid waveOut handles and that ValidateHandle rejects without dereferencing
 * (it only compares the value against its handle list). */
typedef struct { const char *name; HWAVEOUT h; } wp_badh;
int wp_bad_handles(wp_badh *out, int cap);   /* fills the device-independent set */

#define WP_EQ_RC(call, want) EXPECT_EQ((call), (want))
#endif
