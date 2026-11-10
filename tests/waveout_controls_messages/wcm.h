/* Shared helpers for the waveout_controls_messages area: device gating, opening a silent (never written)
 * waveOut/waveIn handle, non-handle values, id-path expectations, wide-string notes.
 *
 * Nothing in this area renders audio: handles are opened and closed, never written to.
 * waveOutSetVolume is only ever called (a) as a write-back of the value just read on a handle this test
 * opened, or (b) on ids/values that cannot reach a device (out-of-range ids, or any id when no wave device
 * and no mapper exist). VM_TIMEOUT: 180 */
#ifndef WCM_H
#define WCM_H
#include "t.h"
#include <mmsystem.h>

/* Result codes (mmsystem.h values, spelled out so the expectations read as numbers). */
#define WCM_OK          0u   /* MMSYSERR_NOERROR */
#define WCM_ERROR       1u   /* MMSYSERR_ERROR */
#define WCM_BADDEVICEID 2u   /* MMSYSERR_BADDEVICEID */
#define WCM_INVALHANDLE 5u   /* MMSYSERR_INVALHANDLE */
#define WCM_NODRIVER    6u   /* MMSYSERR_NODRIVER */
#define WCM_NOMEM       7u   /* MMSYSERR_NOMEM */
#define WCM_NOTSUPP     8u   /* MMSYSERR_NOTSUPPORTED */
#define WCM_INVALPARAM 11u   /* MMSYSERR_INVALPARAM */
#define WCM_MOREDATA   21u   /* MMSYSERR_MOREDATA */

/* Driver message numbers (mmddk.h). */
#define WCM_WODM_GETNUMDEVS        3u
#define WCM_WODM_GETPOS           13u
#define WCM_WODM_GETPITCH         14u
#define WCM_WODM_SETPITCH         15u
#define WCM_WODM_GETVOLUME        16u
#define WCM_WODM_SETVOLUME        17u
#define WCM_WODM_GETPLAYBACKRATE  18u
#define WCM_WODM_SETPLAYBACKRATE  19u
#define WCM_DRV_RESERVED          0x800u
#define WCM_DRV_QUERYDRVENTRY     0x801u
#define WCM_DRV_QUERYDEVNODE      0x802u
#define WCM_DRV_QUERYNAME         0x803u
#define WCM_DRV_QUERYDRIVERIDS    0x804u
#define WCM_DRV_QUERYMAPPABLE     0x805u
#define WCM_DRV_RESERVED6         0x806u
#define WCM_DRV_DEVCOUNT          0x808u   /* answered by winmmbase: the driver's device count */
#define WCM_DRV_QUERYMODULE       0x809u
#define WCM_DRV_DRIVERNAME        0x80Au   /* answered by winmmbase: copies the driver's file name */
#define WCM_DRV_QUERYDEVICEINTERFACE      0x80Cu
#define WCM_DRV_QUERYDEVICEINTERFACESIZE  0x80Du
#define WCM_DRV_QUERYSTRINGID             0x80Eu
#define WCM_DRV_QUERYSTRINGIDSIZE         0x80Fu
#define WCM_DRV_QUERYIDFROMSTRINGID       0x810u
#define WCM_DRV_QUERYFUNCTIONINSTANCEID      0x811u
#define WCM_DRV_QUERYFUNCTIONINSTANCEIDSIZE  0x812u
#define WCM_DRVM_MAPPER_PREFERRED_GET        0x2015u
#define WCM_DRVM_MAPPER_CONSOLEVOICECOM_GET  0x2017u
#define WCM_UNKNOWN_MSG                      0x7FFFu

#define WCM_SENT 0xDEADDEADu      /* output sentinel */

/* ---- device counts: read once per process (the first call may wait for AudioSrv; never timed) ---- */
UINT wcm_nout(void);
UINT wcm_nin(void);
/* The wave mapper driver is loaded only when waveOut + waveIn devices != 0 (winmmbase WaveMapperInit). */
int  wcm_mapper_present(void);
void wcm_note_devices(void);      /* t_note counts and caps names once per process */

/* ---- handles (opened silent, CALLBACK_NULL, never written) ---- */
void wcm_fmt(WAVEFORMATEX *f, DWORD rate, WORD bits, WORD ch);
MMRESULT wcm_open_out(HWAVEOUT *h, UINT id);    /* 44100/16/2 PCM */
MMRESULT wcm_open_in(HWAVEIN *h, UINT id);      /* 44100/16/1 PCM */
void wcm_close_out(HWAVEOUT h);
void wcm_close_in(HWAVEIN h);
/* A handle that was valid and has been closed (its value only, never dereferenced by the API checks). */
HWAVEOUT wcm_closed_out(void);

/* ---- value tables ---- */
/* Values that are not waveOut handles; `low32` is what the id path sees. */
typedef struct { const char *name; ULONG_PTR v; } wcm_val;
extern const wcm_val wcm_nonhandles[];          /* table terminated by name == NULL */
/* For an id-path API (GetVolume, SetVolume, Message): what an id resolves to.
 * 0 = out of range (BADDEVICEID), 1 = a real device, 2 = the mapper. */
#define WCM_ID_BAD    0
#define WCM_ID_DEVICE 1
#define WCM_ID_MAPPER 2
/* map_ffff: waveOutGetVolume/SetVolume turn 0xFFFF into the mapper; waveOutMessage/waveInMessage do not. */
int wcm_id_class(ULONG_PTR v, UINT n, int map_ffff);

/* ---- strings ---- */
const char *wcm_w2a(const WCHAR *w);           /* rotating static buffers, ASCII-ish */
size_t wcm_wlen(const WCHAR *w, size_t cap);    /* wcsnlen */
#endif
