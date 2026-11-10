/* Shared helpers for the midiout_contract area (midiOut* argument, header and handle contract).
 *
 * Every guest enumerates midiOut device 0 (the GS software synth) and the MIDI mapper, so the id-based
 * contract (caps, volume, message, open ladder) runs everywhere. Live handles exist only when the synth can
 * render, i.e. when waveOutGetNumDevs() > 0 (with no wave device midiOutOpen(MIDI_MAPPER) = MMSYSERR_ERROR).
 * moc_open_live() gives a live handle or prints a skip; every live test closes what it opened (the synth is
 * single-open per process).
 *
 * The first winmm call in a process may wait for AudioSrv: the counts are read once and never timed.
 */
#ifndef MOC_H
#define MOC_H
#include "t.h"
#include <mmsystem.h>

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
#ifndef MMSYSERR_MOREDATA
#define MMSYSERR_MOREDATA               21
#endif
#ifndef MODM_GETNUMDEVS
#define MODM_GETNUMDEVS                 1
#endif
#ifndef MODM_GETDEVCAPS
#define MODM_GETDEVCAPS                 2
#endif
#ifndef MODM_GETVOLUME
#define MODM_GETVOLUME                  10
#endif

/* MIDIHDR front check of midiOutLongMsg and ValidateHeader(type 3): cbmh < 0x2C or dwFlags & this -> 11 */
#define MOC_HDR_BADMASK 0xFFFFCFD0u
#define MOC_HDR_MINCB   0x2Cu
/* midiOutOpen flag masks: MIDI_MAPPER and any other id */
#define MOC_OPEN_BADMASK_MAPPER 0xBFF3u
#define MOC_OPEN_BADMASK_ID     0x3FF3u
/* value never returned by midiOutOpen: proves *phmo was (not) written */
#define MOC_SENT_HANDLE ((HMIDIOUT)(ULONG_PTR)0x5A5A5A5A5A5A5A5Aull)

UINT moc_nout(void);       /* midiOutGetNumDevs, read once (1 on every guest: the GS synth) */
UINT moc_nwave(void);      /* waveOutGetNumDevs, read once: gates every live open */
int  moc_can_open(void);   /* moc_nwave() > 0 */

/* Live handle on id 0 (then the mapper), CALLBACK_NULL; NULL plus t_skip when none can be opened. */
HMIDIOUT moc_open_live(void);
HMIDIOUT moc_open_id(UINT id);              /* NULL on failure (t_skip printed) */
void     moc_close(HMIDIOUT h);             /* all-notes-off on 16 channels, reset, close; CHECKs close == 0 */
HMIDIOUT moc_closed_handle(void);           /* value of a handle that was valid and is now closed, or NULL */

/* Memory-backed HMMIO: lives in the same winmm handle list with another type; always available. */
HMMIO moc_mmio_open(void);
void  moc_mmio_close(HMMIO h);
HWND  moc_msg_window(void);                 /* message-only STATIC window, NULL if user32 unavailable */

void moc_hdr(MIDIHDR *h, void *buf, DWORD len, DWORD flags);

/* never-opened handle values ValidateHandle rejects without dereferencing (<= 0xFFFF, -1, 0xFFFFFFFF,
 * or not on the handle list) */
extern const ULONG_PTR moc_bad_handles[];
extern const int moc_nbad;
/* values that are neither a device id that exists nor a handle: the id-based paths answer 2 */
extern const ULONG_PTR moc_bad_ids[];
extern const int moc_nbadid;

#define NELEM(a) ((int)(sizeof(a) / sizeof((a)[0])))
#endif
