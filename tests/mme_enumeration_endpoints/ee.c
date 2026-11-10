#include "ee.h"
#include <propsys.h>

const char *const ee_family_name[F_COUNT] = { "waveOut", "waveIn", "midiOut", "midiIn", "mixer", "aux" };

typedef UINT (WINAPI *numdevs_fn)(void);
typedef MMRESULT (WINAPI *caps_fn)(UINT_PTR, void *, UINT);
typedef MMRESULT (WINAPI *msg_fn)(UINT_PTR, UINT, DWORD_PTR, DWORD_PTR);

static const char *const numdevs_names[F_COUNT] = {
    "waveOutGetNumDevs", "waveInGetNumDevs", "midiOutGetNumDevs", "midiInGetNumDevs", "mixerGetNumDevs", "auxGetNumDevs" };
static const char *const caps_names[F_COUNT][2] = {
    { "waveOutGetDevCapsA", "waveOutGetDevCapsW" }, { "waveInGetDevCapsA", "waveInGetDevCapsW" },
    { "midiOutGetDevCapsA", "midiOutGetDevCapsW" }, { "midiInGetDevCapsA", "midiInGetDevCapsW" },
    { "mixerGetDevCapsA", "mixerGetDevCapsW" }, { "auxGetDevCapsA", "auxGetDevCapsW" } };
static const char *const msg_names[F_COUNT] = {
    "waveOutMessage", "waveInMessage", "midiOutMessage", "midiInMessage", NULL, "auxOutMessage" };

UINT ee_numdevs(int fam) {
    numdevs_fn f = (numdevs_fn)t_proc_in("winmm.dll", numdevs_names[fam]);
    return f ? f() : ~0u;
}
UINT ee_numdevs_base(int fam) {
    numdevs_fn f = (numdevs_fn)t_proc_in("winmmbase.dll", numdevs_names[fam]);
    return f ? f() : ~0u;
}
MMRESULT ee_caps(int fam, int wide, UINT_PTR id, void *buf, UINT cb) {
    caps_fn f = (caps_fn)t_proc_in("winmm.dll", caps_names[fam][wide ? 1 : 0]);
    return f ? f(id, buf, cb) : 0xFFFFFFFFu;
}
UINT ee_caps_size(int fam, int wide) {
    switch (fam) {
    case F_WAVEOUT: return wide ? sizeof(WAVEOUTCAPSW) : sizeof(WAVEOUTCAPSA);
    case F_WAVEIN:  return wide ? sizeof(WAVEINCAPSW) : sizeof(WAVEINCAPSA);
    case F_MIDIOUT: return wide ? sizeof(MIDIOUTCAPSW) : sizeof(MIDIOUTCAPSA);
    case F_MIDIIN:  return wide ? sizeof(MIDIINCAPSW) : sizeof(MIDIINCAPSA);
    case F_MIXER:   return wide ? sizeof(MIXERCAPSW) : sizeof(MIXERCAPSA);
    default:        return wide ? sizeof(AUXCAPSW) : sizeof(AUXCAPSA);
    }
}
MMRESULT ee_msg(int fam, UINT_PTR id, UINT msg, DWORD_PTR p1, DWORD_PTR p2) {
    if (!msg_names[fam]) return 0xFFFFFFFFu;
    if (fam == F_AUX) {
        typedef MMRESULT (WINAPI *aux_fn)(UINT, UINT, DWORD_PTR, DWORD_PTR);
        aux_fn f = (aux_fn)t_proc_in("winmm.dll", "auxOutMessage");
        return f ? f((UINT)id, msg, p1, p2) : 0xFFFFFFFFu;
    }
    msg_fn f = (msg_fn)t_proc_in("winmm.dll", msg_names[fam]);
    return f ? f(id, msg, p1, p2) : 0xFFFFFFFFu;
}
void ee_snapshot(ee_counts *c) {
    for (int f = 0; f < F_COUNT; f++) c->n[f] = ee_numdevs(f);
    numdevs_fn j = (numdevs_fn)t_proc_in("winmm.dll", "joyGetNumDevs");
    c->joy = j ? j() : ~0u;
}

int ee_wieq(const WCHAR *a, const WCHAR *b) {
    for (;; a++, b++) {
        WCHAR x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* ---- COM -------------------------------------------------------------------------------------------------- */
static const GUID ee_CLSID_MMDeviceEnumerator = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID ee_IID_IMMDeviceEnumerator  = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const PROPERTYKEY ee_PKEY_FriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

typedef HRESULT (WINAPI *coinit_fn)(LPVOID, DWORD);
typedef HRESULT (WINAPI *cocreate_fn)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *);
typedef void (WINAPI *cofree_fn)(LPVOID);
typedef HRESULT (WINAPI *pvclear_fn)(PROPVARIANT *);

static DWORD g_com_tid;
static void ee_com_init(void) {
    if (g_com_tid == GetCurrentThreadId()) return;
    coinit_fn ci = (coinit_fn)t_proc_in("ole32.dll", "CoInitializeEx");
    if (ci) { ci(NULL, COINIT_MULTITHREADED); g_com_tid = GetCurrentThreadId(); }
}
void ee_cotaskmemfree(void *p) {
    cofree_fn f = (cofree_fn)t_proc_in("ole32.dll", "CoTaskMemFree");
    if (f && p) f(p);
}
IMMDeviceEnumerator *ee_enum(HRESULT *hr) {
    IMMDeviceEnumerator *e = NULL;
    ee_com_init();
    cocreate_fn cc = (cocreate_fn)t_proc_in("ole32.dll", "CoCreateInstance");
    HRESULT h = cc ? cc(&ee_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &ee_IID_IMMDeviceEnumerator, (void **)&e) : E_NOTIMPL;
    if (hr) *hr = h;
    return SUCCEEDED(h) ? e : NULL;
}
int ee_endpoint_count(EDataFlow flow, DWORD mask) {
    IMMDeviceEnumerator *e = ee_enum(NULL);
    if (!e) return -1;
    IMMDeviceCollection *c = NULL; UINT n = 0; int r = -1;
    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(e, flow, mask, &c)) && c) {
        if (SUCCEEDED(IMMDeviceCollection_GetCount(c, &n))) r = (int)n;
        IMMDeviceCollection_Release(c);
    }
    IMMDeviceEnumerator_Release(e);
    return r;
}
int ee_endpoint_ids(EDataFlow flow, WCHAR ids[][256], int cap) {
    IMMDeviceEnumerator *e = ee_enum(NULL);
    if (!e) return -1;
    IMMDeviceCollection *c = NULL; UINT n = 0; int k = -1;
    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(e, flow, DEVICE_STATE_ACTIVE, &c)) && c) {
        k = 0;
        if (SUCCEEDED(IMMDeviceCollection_GetCount(c, &n))) {
            for (UINT i = 0; i < n && k < cap; i++) {
                IMMDevice *d = NULL; LPWSTR s = NULL;
                if (SUCCEEDED(IMMDeviceCollection_Item(c, i, &d)) && d) {
                    if (SUCCEEDED(IMMDevice_GetId(d, &s)) && s) {
                        lstrcpynW(ids[k++], s, 256);
                        ee_cotaskmemfree(s);
                    }
                    IMMDevice_Release(d);
                }
            }
        }
        IMMDeviceCollection_Release(c);
    }
    IMMDeviceEnumerator_Release(e);
    return k;
}
int ee_endpoint_name(const WCHAR *id, WCHAR *out, int cch) {
    int ok = 0; out[0] = 0;
    IMMDeviceEnumerator *e = ee_enum(NULL);
    if (!e) return 0;
    IMMDevice *d = NULL;
    if (SUCCEEDED(IMMDeviceEnumerator_GetDevice(e, id, &d)) && d) {
        IPropertyStore *ps = NULL;
        if (SUCCEEDED(IMMDevice_OpenPropertyStore(d, STGM_READ, &ps)) && ps) {
            PROPVARIANT pv; memset(&pv, 0, sizeof pv);
            if (SUCCEEDED(IPropertyStore_GetValue(ps, &ee_PKEY_FriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) {
                lstrcpynW(out, pv.pwszVal, cch); ok = 1;
            }
            pvclear_fn pc = (pvclear_fn)t_proc_in("ole32.dll", "PropVariantClear");
            if (pc) pc(&pv);
            IPropertyStore_Release(ps);
        }
        IMMDevice_Release(d);
    }
    IMMDeviceEnumerator_Release(e);
    return ok;
}
HRESULT ee_default_id(EDataFlow flow, ERole role, WCHAR *out, int cch) {
    out[0] = 0;
    HRESULT hr;
    IMMDeviceEnumerator *e = ee_enum(&hr);
    if (!e) return hr;
    IMMDevice *d = NULL;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(e, flow, role, &d);
    if (SUCCEEDED(hr) && d) {
        LPWSTR s = NULL;
        if (SUCCEEDED(IMMDevice_GetId(d, &s)) && s) { lstrcpynW(out, s, cch); ee_cotaskmemfree(s); }
        IMMDevice_Release(d);
    }
    IMMDeviceEnumerator_Release(e);
    return hr;
}
MMRESULT ee_fiid(int fam, UINT id, WCHAR *out, int cch) {
    out[0] = 0;
    return ee_msg(fam, id, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)out, (DWORD_PTR)(cch * sizeof(WCHAR)));
}
