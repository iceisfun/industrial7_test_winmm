/* Shared helpers for the mme_enumeration_endpoints area: device counts per family, generic caps calls by family,
 * and a dynamically loaded mmdevapi (IMMDeviceEnumerator) view of the audio endpoints.
 * Nothing here links winmm, ole32 or mmdevapi: every call is resolved at run time. */
#ifndef EE_H
#define EE_H
#ifndef COBJMACROS
#define COBJMACROS
#endif
#include "t.h"
#include <mmsystem.h>
#include <mmddk.h>
#include <objbase.h>
#include <mmdeviceapi.h>

/* Families in a fixed order (tables index by it). */
enum { F_WAVEOUT, F_WAVEIN, F_MIDIOUT, F_MIDIIN, F_MIXER, F_AUX, F_COUNT };
extern const char *const ee_family_name[F_COUNT];

UINT ee_numdevs(int fam);                          /* winmm!<fam>GetNumDevs; ~0u when the export is missing */
UINT ee_numdevs_base(int fam);                     /* same through winmmbase.dll directly */
/* <fam>GetDevCapsW/A(id, buf, cb). Returns the MMRESULT, 0xFFFFFFFF when the export is missing. */
MMRESULT ee_caps(int fam, int wide, UINT_PTR id, void *buf, UINT cb);
UINT ee_caps_size(int fam, int wide);              /* sizeof the classic (non-2) caps struct */
/* Offset and capacity (chars) of szPname in the caps struct (all families: right after wMid/wPid/vDriverVersion). */
#define EE_PNAME_OFF 8
/* Message by id or handle: waveOutMessage / waveInMessage / midiOutMessage / midiInMessage / auxOutMessage. */
MMRESULT ee_msg(int fam, UINT_PTR id, UINT msg, DWORD_PTR p1, DWORD_PTR p2);

/* Snapshot of every count; filled by ee_snapshot. */
typedef struct { UINT n[F_COUNT]; UINT joy; } ee_counts;
void ee_snapshot(ee_counts *c);

/* ---- mmdevapi through COM (ole32 loaded dynamically) ----------------------------------------------------- */
/* Returns an IMMDeviceEnumerator (AddRef'd, the caller releases) or NULL; *hr receives the CoCreateInstance result. */
IMMDeviceEnumerator *ee_enum(HRESULT *hr);
/* Active endpoint count for a flow (eRender/eCapture/eAll), -1 on failure. */
int ee_endpoint_count(EDataFlow flow, DWORD mask);
/* Endpoint ids of the active endpoints of a flow, CoTaskMem strings copied into a caller array of WCHAR[256]. */
int ee_endpoint_ids(EDataFlow flow, WCHAR ids[][256], int cap);
/* Friendly name (PKEY_Device_FriendlyName) of an endpoint by id; returns 1 on success. */
int ee_endpoint_name(const WCHAR *id, WCHAR *out, int cch);
/* Default endpoint id; returns the HRESULT of GetDefaultAudioEndpoint (out "" on failure). */
HRESULT ee_default_id(EDataFlow flow, ERole role, WCHAR *out, int cch);
void ee_cotaskmemfree(void *p);

/* waveOut/waveIn device id -> function instance id (DRV_QUERYFUNCTIONINSTANCEID); returns the MMRESULT. */
MMRESULT ee_fiid(int fam, UINT id, WCHAR *out, int cch);

/* Fills a buffer with a byte pattern / counts bytes still equal to it. */
static inline void ee_fill(void *p, size_t n, unsigned char v) { memset(p, v, n); }
static inline size_t ee_untouched_from(const void *p, size_t n, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p; size_t i = n;
    while (i > 0 && b[i - 1] == v) i--;
    return i;  /* index of the first byte of the trailing run of v (== n when the last byte changed) */
}
static inline int ee_all(const void *p, size_t n, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p; for (size_t i = 0; i < n; i++) if (b[i] != v) return 0; return 1;
}
/* Length of a wide string bounded by cap (cap when unterminated). */
static inline int ee_wlen(const WCHAR *s, int cap) { int i = 0; while (i < cap && s[i]) i++; return i; }
static inline int ee_alen(const char *s, int cap) { int i = 0; while (i < cap && s[i]) i++; return i; }
/* Case-insensitive wide compare (endpoint ids are compared that way). */
int ee_wieq(const WCHAR *a, const WCHAR *b);
#endif
