/* Shared helpers for the mme_handles_threads area.
 *
 * winmmbase ValidateHandle(h, type) accepts a value only if it is > 0xFFFF, not -1 and not 0xFFFFFFFF, inside the
 * user VA range, its header (h - 0x50) is on the global handle list, and the stored type (h - 0x48) equals the
 * expected type: 1 waveOut, 2 waveIn, 3 midiOut, 4 midiIn, 5 mmio, 8 mixer, 9 midiStream. Everything else is
 * MMSYSERR_INVALHANDLE (5) in the MME entry points, without dereferencing the value.
 */
#ifndef MH_H
#define MH_H
#include "t.h"
#include <mmsystem.h>

/* winmmbase-only exports (not in the SDK headers). */
UDECL(ULONG_PTR, winmmbaseHandle32FromHandle16, (WORD));
UDECL(WORD, winmmbaseGetWOWHandle, (HANDLE));
UDECL(void, winmmbaseSetWOWHandle, (HANDLE, WORD));
#define WB(name) ((name##_pfn)t_proc_in("winmmbase.dll", #name))

/* Driver message numbers (mmddk.h, not in the mingw headers). */
#ifndef WODM_GETPOS
#define WODM_GETPOS 13
#endif
#ifndef WIDM_GETPOS
#define WIDM_GETPOS 60
#endif
#ifndef MODM_GETVOLUME
#define MODM_GETVOLUME 10
#endif
#ifndef MIDM_GETDEVCAPS
#define MIDM_GETDEVCAPS 54
#endif
#ifndef MXDM_GETDEVCAPS
#define MXDM_GETDEVCAPS 2
#endif

/* Handle type codes stored by NewHandle. */
enum { MHT_WAVEOUT = 1, MHT_WAVEIN = 2, MHT_MIDIOUT = 3, MHT_MIDIIN = 4, MHT_MMIO = 5, MHT_MIXER = 8, MHT_STREAM = 9 };

/* Values that are never valid handles: NULL, small integers, the 0xFFFF boundary, -1 and its 32-bit form. */
typedef struct { const char *name; ULONG_PTR v; } mh_badv;
extern const mh_badv MH_BAD[];
extern const int MH_NBAD;

/* A function under test bound to a handle-like value; returns the raw result (MMRESULT, LONG or LRESULT). */
typedef ULONG_PTR (*mh_call)(ULONG_PTR h);
typedef struct {
    const char *name;
    mh_call call;
    ULONG_PTR bad;            /* result for any value that is not a live handle of an accepted type */
    unsigned accept_mask;     /* bit (1 << type) of every handle type the entry point accepts */
} mh_op;

/* Families (each table is defined in its own file). */
extern const mh_op MH_WAVEOUT_OPS[]; extern const int MH_NWAVEOUT_OPS;
extern const mh_op MH_WAVEIN_OPS[];  extern const int MH_NWAVEIN_OPS;
extern const mh_op MH_MIDIOUT_OPS[]; extern const int MH_NMIDIOUT_OPS;
extern const mh_op MH_MIDIIN_OPS[];  extern const int MH_NMIDIIN_OPS;
extern const mh_op MH_STREAM_OPS[];  extern const int MH_NSTREAM_OPS;
extern const mh_op MH_MMIO_OPS[];    extern const int MH_NMMIO_OPS;
extern const mh_op MH_MIXER_OPS[];   extern const int MH_NMIXER_OPS;

/* Runs every op with value h and checks the op's "bad" result; tag goes into failure messages. */
void mh_expect_bad(const mh_op *ops, int n, ULONG_PTR h, const char *tag);
/* Runs every op with every MH_BAD value. */
void mh_expect_all_bad(const mh_op *ops, int n);

/* Device counts, read once per process (the first call may wait for AudioSrv: never timed). */
typedef struct { UINT wo, wi, mo, mi, mx; } mh_counts;
const mh_counts *mh_devs(void);

/* mmio memory file: always available live handle (type 5). */
HMMIO mh_mmio_open(void);
/* Opens and closes an mmio memory file; the returned value is a handle that has just been freed. Use it before any
 * other handle is created in the test (a new handle may reuse the address). */
ULONG_PTR mh_closed_mmio(void);

/* 44100 Hz / 16 bit / stereo PCM. */
void mh_pcm(WAVEFORMATEX *wf);
/* Opens WAVE_MAPPER (CALLBACK_EVENT when ev != NULL, else CALLBACK_NULL); 0 on success. */
MMRESULT mh_waveout_open(HWAVEOUT *h, HANDLE ev);
MMRESULT mh_wavein_open(HWAVEIN *h);
/* Plays one silent buffer of ms milliseconds on h and waits (<= wait_ms) for DONE; returns 0 when everything
 * returned 0 and the header came back DONE|PREPARED, else the first failing code (0xFFFF0000 = wait timed out). */
DWORD mh_play_silence(HWAVEOUT h, int ms, DWORD wait_ms);

/* Live handles of every type that can be opened in this environment (NULL when unavailable). */
typedef struct {
    HMMIO mmio;
    HWAVEOUT wo;
    HWAVEIN wi;
    HMIXER mx;
    HMIDIOUT mo;      /* only when waveOut devices exist and midiOutOpen(0) succeeds */
} mh_zoo;
void mh_zoo_open(mh_zoo *z);
void mh_zoo_close(mh_zoo *z);
/* Checks every op of the family against every live handle in z whose type it does not accept. */
void mh_zoo_cross(const mh_zoo *z, const mh_op *ops, int n, const char *family);
#endif
