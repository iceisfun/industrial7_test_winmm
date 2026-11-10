/* Shared helpers for the wavein_contract area (waveIn* argument and handle contract).
 *
 * Device gating: waveInGetNumDevs is read once (wic_nin) - the first winmm call can block while
 * AudioSrv starts, so it is never timed. Every TEST runs its zero-device/contract half; the
 * device half runs only when wic_nin() > 0.
 */
#ifndef WIC_H
#define WIC_H
#include "t.h"
#include <mmsystem.h>

/* driver/mapper messages (mmddk.h values) */
#ifndef DRV_QUERYMAPPABLE
#define DRV_QUERYMAPPABLE               0x805
#endif
#ifndef DRV_QUERYDEVICEINTERFACE
#define DRV_QUERYDEVICEINTERFACE        0x80C
#endif
#ifndef DRV_QUERYDEVICEINTERFACESIZE
#define DRV_QUERYDEVICEINTERFACESIZE    0x80D
#endif
#ifndef DRV_QUERYSTRINGID
#define DRV_QUERYSTRINGID               0x80E
#endif
#ifndef DRV_QUERYSTRINGIDSIZE
#define DRV_QUERYSTRINGIDSIZE           0x80F
#endif
#ifndef DRV_QUERYIDFROMSTRINGID
#define DRV_QUERYIDFROMSTRINGID         0x810
#endif
#ifndef DRV_QUERYFUNCTIONINSTANCEID
#define DRV_QUERYFUNCTIONINSTANCEID     0x811
#endif
#ifndef DRV_QUERYFUNCTIONINSTANCEIDSIZE
#define DRV_QUERYFUNCTIONINSTANCEIDSIZE 0x812
#endif
#ifndef DRVM_MAPPER_PREFERRED_GET
#define DRVM_MAPPER_PREFERRED_GET       0x2015
#endif
#ifndef DRVM_MAPPER_CONSOLEVOICECOM_GET
#define DRVM_MAPPER_CONSOLEVOICECOM_GET 0x2017
#endif
#ifndef WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE
#define WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE 0x0010
#endif

/* WAVEHDR front check (Prepare/Unprepare/AddBuffer): pwh NULL, cbwh < 0x30, dwFlags & 0xFFFFCFE0 -> 11 */
#define WIC_HDR_BADMASK 0xFFFFCFE0u
#define WIC_SENT_HANDLE ((HWAVEIN)(ULONG_PTR)0x5A5A5A5A5A5A5A5Aull)

UINT wic_nin(void);   /* waveInGetNumDevs, read once */
UINT wic_nout(void);  /* waveOutGetNumDevs, read once */
void wic_pcm(WAVEFORMATEX *f, DWORD rate, WORD bits, WORD ch);
/* waveInOpen with CALLBACK_NULL and PCM 44100/16/1; returns the MMRESULT */
MMRESULT wic_open(HWAVEIN *ph, UINT id);
/* Open any HWAVEIN (mapper first, then 0); NULL when there is none (t_skip already printed). */
HWAVEIN wic_open_any(void);
/* A closed HWAVEIN (value of a handle that was valid), or NULL when no device. */
HWAVEIN wic_closed_handle(void);
/* Memory-backed HMMIO: always available, lives in the same handle list (type 5) */
HMMIO wic_mmio_open(void);
void wic_mmio_close(HMMIO h);
/* An HWAVEOUT for cross-type tests, NULL if no waveOut device can be opened */
HWAVEOUT wic_waveout_open(void);
void wic_hdr(WAVEHDR *h, void *buf, DWORD len, DWORD flags);

/* invalid (never-opened) handle values that ValidateHandle rejects without dereferencing */
extern const ULONG_PTR wic_bad_handles[];
extern const int wic_nbad;
#endif
