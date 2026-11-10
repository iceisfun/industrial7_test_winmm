/* waveout_header_protocol: shared helpers for the WAVEHDR state machine tests.
 *
 * winmmbase gates (decomp, waveOutPrepareHeader 0x10910, waveOutUnprepareHeader 0x109b0, waveOutWrite 0x10a70):
 *   pwh == NULL || cbwh < 0x30 || (pwh->dwFlags & 0xFFFFCFE0)          -> MMSYSERR_INVALPARAM (11), before the handle
 *   Prepare:   PREPARED set -> 0 (no handle check); else handle (5); dwFlags &= 0xC; WODM_PREPARE; PREPARED on 0/8
 *   Unprepare: INQUEUE -> 33; not PREPARED -> 0 (no handle check); else handle (5); WODM_UNPREPARE; clear PREPARED
 *   Write:     not PREPARED -> 34; INQUEUE -> 33; handle (5); clear DONE; WODM_WRITE
 */
#ifndef WHP_H
#define WHP_H
#include "t.h"
#include <mmsystem.h>
#include <mmreg.h>

#define WHP_GATE_MASK   0xFFFFCFE0u   /* any of these flag bits -> 11 */
#define WHP_ALLOWED     0x0000301Fu   /* DONE|PREPARED|BEGINLOOP|ENDLOOP|INQUEUE|0x1000|0x2000 */
#define WHP_MINCB       0x30u         /* sizeof(WAVEHDR) on x64 */
#define WHP_WAIT_MS     3000          /* bound for every completion wait */

typedef MMRESULT (WINAPI *whp_hdr_fn)(HWAVEOUT, LPWAVEHDR, UINT);
enum { WHP_PREP = 0, WHP_UNPREP = 1, WHP_WRITE = 2, WHP_NFN = 3 };
extern const char *const whp_fn_name[WHP_NFN];
whp_hdr_fn whp_fn(int which);          /* U()-resolved pointer, NULL if missing */
int whp_have_all(void);               /* all three exports resolved */

/* Handles that never name a live waveOut handle (ValidateHandle: <= 0xFFFF, -1, 0xFFFFFFFF, not in the list). */
#define WHP_NBAD 6
extern const ULONG_PTR whp_bad_handles[WHP_NBAD];

/* Header with sentinel payload fields so untouched-field checks are meaningful. */
void whp_hdr_init(WAVEHDR *h, DWORD flags, char *buf, DWORD len);
/* All fields other than dwFlags equal to a reference copy. */
int whp_hdr_same_except_flags(const WAVEHDR *a, const WAVEHDR *b);

/* ---- device side ---- */
UINT whp_numdevs(void);               /* waveOutGetNumDevs, read once per process, never timed */
void whp_fmt(WAVEFORMATEX *f, DWORD rate, WORD bits, WORD ch);
/* Opens id with 44100/16/2 PCM and CALLBACK_EVENT on *ev (created when NULL). Returns MMRESULT. */
MMRESULT whp_open(UINT id, HWAVEOUT *ph, HANDLE *ev);
MMRESULT whp_open_fmt(UINT id, const WAVEFORMATEX *f, HWAVEOUT *ph, HANDLE *ev);
/* Requires a device: skips the test (returns 0) when there is none or the open fails. */
int whp_need_dev(HWAVEOUT *ph, HANDLE *ev);
void whp_close(HWAVEOUT h, HANDLE ev); /* reset + unprepare-free close for cleanup; tolerant */
/* Waits until (dwFlags & WHDR_DONE), bounded. Returns 1 when done. */
int whp_wait_done(WAVEHDR *h, HANDLE ev, DWORD ms);
int whp_wait_all_done(WAVEHDR *h, int n, HANDLE ev, DWORD ms);
/* Silence buffer of ms milliseconds at 44100/16/2 (static storage, at most 2000 ms). */
char *whp_silence(DWORD ms, DWORD *len);
#define WHP_BPS (44100u * 4u)

#endif
