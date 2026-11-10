/* wave_formats: WAVE_FORMAT_QUERY / real-open format negotiation for waveOut and waveIn.
 *
 * Device gating: wf_ndev() reads waveOutGetNumDevs / waveInGetNumDevs once per process (the first call may wait for
 * AudioSrv, so it is never timed). Every cell first decides what the open layer itself must return:
 *   - winmmbase checks pwfx, the callback window and the flag masks before the device count (decomp waveOutOpen
 *     winmmbase!0x103b0, waveInOpen !0x11290), so those codes hold with and without devices;
 *   - then "no device in this direction" -> MMSYSERR_BADDEVICEID (2) for every id, the mapper included;
 *   - an id >= count -> 2;
 *   - otherwise the format verdict belongs to the driver (wdmaud / the msacm wave mapper): the cell's expectation.
 * Cells whose driver verdict is not pinned are recorded with t_note("CELL ...") and never fail. */
#ifndef WF_H
#define WF_H
#include "t.h"
#include <mmsystem.h>

enum { WF_OUT = 0, WF_IN = 1 };
#define WF_MAPPER        WAVE_MAPPER          /* 0xFFFFFFFF */
#define WF_NOTE          (-1)                 /* driver verdict not pinned: record only */
#define WF_ANY_OK_OR_BAD (-2)                 /* pinned to {0, 32}: never anything else */
#define WF_NOTE_IF_NOT0  (-3)                 /* not pinned: noted only when the verdict is not 0 */

#define WAVE_FORMAT_PCM_         0x0001
#define WAVE_FORMAT_ADPCM_       0x0002
#define WAVE_FORMAT_IEEE_FLOAT_  0x0003
#define WAVE_FORMAT_ALAW_        0x0006
#define WAVE_FORMAT_MULAW_       0x0007
#define WAVE_FORMAT_IMA_ADPCM_   0x0011
#define WAVE_FORMAT_GSM610_      0x0031
#define WAVE_FORMAT_MPEGLAYER3_  0x0055
#define WAVE_FORMAT_EXTENSIBLE_  0xFFFE

/* A format block: WAVEFORMATEX followed by up to 100 extra bytes (packed exactly like the SDK structs). */
#pragma pack(push, 1)
typedef struct {
    WORD  wFormatTag, nChannels;
    DWORD nSamplesPerSec, nAvgBytesPerSec;
    WORD  nBlockAlign, wBitsPerSample, cbSize;
    BYTE  extra[110];
} wf_fmt;
typedef struct {            /* WAVEFORMATEXTENSIBLE */
    WORD  wFormatTag, nChannels;
    DWORD nSamplesPerSec, nAvgBytesPerSec;
    WORD  nBlockAlign, wBitsPerSample, cbSize;
    WORD  wValidBitsPerSample;
    DWORD dwChannelMask;
    GUID  SubFormat;
    BYTE  pad[64];
} wf_ext;
#pragma pack(pop)

extern const GUID WF_SUB_PCM, WF_SUB_FLOAT, WF_SUB_ALAW, WF_SUB_MULAW, WF_SUB_ADPCM, WF_SUB_BOGUS, WF_SUB_DRM;

UINT wf_ndev(int dir);                                   /* cached *GetNumDevs */
const char *wf_dirname(int dir);
/* PCM-shaped WAVEFORMATEX with consistent nBlockAlign / nAvgBytesPerSec (bits rounded up to bytes). */
void wf_pcm(wf_fmt *f, WORD tag, WORD ch, DWORD rate, WORD bits);
void wf_extfmt(wf_ext *f, WORD ch, DWORD rate, WORD bits, WORD valid, DWORD mask, const GUID *sub);
/* ACM-codec formats with their codec-specific extra bytes. */
void wf_msadpcm(wf_fmt *f, WORD ch, DWORD rate);
void wf_imaadpcm(wf_fmt *f, WORD ch, DWORD rate);
void wf_gsm610(wf_fmt *f, DWORD rate);
void wf_g711(wf_fmt *f, WORD tag, WORD ch, DWORD rate);
void wf_mp3(wf_fmt *f, WORD ch, DWORD rate, DWORD kbps);

/* WAVE_FORMAT_QUERY | extra on direction dir; checks that the handle slot is left untouched (the query path never
 * writes *phwo). */
MMRESULT wf_query(int dir, UINT id, const void *fmt, DWORD extra);
/* Real open with CALLBACK_NULL | extra, then close (handle checked: non-NULL on success, NULL on failure). */
MMRESULT wf_open_close(int dir, UINT id, const void *fmt, DWORD extra);

/* What the open layer must return before the driver sees the format: 2 when the direction has no device or the id is
 * out of range, else -1 (the driver decides). */
int wf_pre_driver(int dir, UINT id);
/* One cell: query, then CHECK against the gate (2) or exp (>= 0) or note (WF_NOTE / WF_ANY_OK_OR_BAD).
 * Returns the code. label is printed in the failure detail / note. */
MMRESULT wf_cell(const char *label, int dir, UINT id, const void *fmt, DWORD extra, int exp);
/* Same with a real open + close. */
MMRESULT wf_cell_open(const char *label, int dir, UINT id, const void *fmt, DWORD extra, int exp);
/* Returns 1 (and t_skip's) when the direction has no device: for tests whose only content is the device half. */
int wf_need_dev(int dir);
#endif
