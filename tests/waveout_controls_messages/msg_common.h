/* waveOutMessage / waveInMessage shared table: both directions run the same id-path checks.
 *
 * winmmbase waveOutMessage (!0xfd70) / waveInMessage (!0x10f70): if the value is a live handle of the right type
 * the message goes straight to the driver (waveMessage). Otherwise the LOW 32 BITS are a device id
 * (waveIDMessage): id < count or id == 0xFFFFFFFF -> the owning driver (the mapper for 0xFFFFFFFF; there is no
 * 0xFFFF -> mapper conversion here, unlike GetVolume), else MMSYSERR_BADDEVICEID (2) with nothing written.
 * For an id, mregHandleInternalMessages answers 0x801..0x812 and DRVM_MAPPER_PREFERRED_GET /
 * CONSOLEVOICECOM_GET itself; everything else (WODM_*, unknown messages) reaches the driver. */
#ifndef WCM_MSG_COMMON_H
#define WCM_MSG_COMMON_H
#include "wcm.h"

typedef MMRESULT (WINAPI *wcm_msg_fn)(void *h, UINT msg, DWORD_PTR dw1, DWORD_PTR dw2);

typedef struct {
    const char *name;      /* "out" / "in" */
    wcm_msg_fn  fn;
    UINT        n;         /* device count of this direction */
    int         is_out;
} wcm_dir;

/* Fills d[0] = waveOut, d[1] = waveIn; returns 0 if an export is missing. */
int wcm_dirs(wcm_dir d[2]);
/* Mapper driver reachable through id 0xFFFFFFFF in this direction. */
int wcm_dir_has_mapper(const wcm_dir *d);
#endif
