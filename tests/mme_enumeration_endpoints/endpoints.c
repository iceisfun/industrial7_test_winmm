#include "ee.h"
/* winmm device ids vs mmdevapi endpoints.
 *
 * winmmbase builds its waveOut/waveIn/mixer lists from mmdevapi's active render/capture endpoints (one wdmaud driver
 * entry, one device, per endpoint). The id -> endpoint mapping is exposed through driver messages that winmmbase
 * answers itself on the id path (mregHandleInternalMessages, reached from waveIDMessage / midiIDMessage):
 *   DRV_QUERYFUNCTIONINSTANCEID (0x811)     dw1 WCHAR buffer, dw2 size in BYTES: copies the endpoint id string;
 *                                           NULL dw1 -> 11; too small -> 21 (MOREDATA); no id stored: "" (or 21 if size 0)
 *   DRV_QUERYFUNCTIONINSTANCEIDSIZE (0x812) dw1 ULONG*: (wcslen + 1) * 2 when the driver has an interface, else 2
 *   DRV_QUERYDEVICEINTERFACE[SIZE] (0x80C/0x80D) the PnP interface path (\\?\...), same protocol
 *   DRV_QUERYSTRINGID[SIZE] (0x80E/0x80F), DRV_QUERYIDFROMSTRINGID (0x810)  winmm string ids and their reverse lookup
 *   DRVM_MAPPER_PREFERRED_GET (0x2015), DRVM_MAPPER_CONSOLEVOICECOM_GET (0x2017): mapper entry only (else 11)
 *   0x801..0x804 and 0x806 -> 8 (NOTSUPPORTED); 0x809 DRV_QUERYMODULE -> the driver's HMODULE
 * mmdevapi is reached through COM (ole32 loaded at run time, CLSID_MMDeviceEnumerator). E_NOTFOUND (0x80070490) is
 * the default-endpoint result when there is no active endpoint for the flow.
 * VM_TIMEOUT: 120 */

#define E_NOTFOUND_HR ((HRESULT)0x80070490)
static UINT nwo(void) { return ee_numdevs(F_WAVEOUT); }
static UINT nwi(void) { return ee_numdevs(F_WAVEIN); }

/* ---- the enumerator itself -------------------------------------------------------------------------------- */
TEST(ep_mmdevapi_enumerator_creates) {
    REQUIRE(U(waveOutGetNumDevs));
    HRESULT hr = E_FAIL;
    IMMDeviceEnumerator *e = ee_enum(&hr);
    EXPECT_EQ(hr, S_OK);
    CHECK(e != NULL);
    if (e) IMMDeviceEnumerator_Release(e);
}
TEST(ep_render_count_equals_waveout) {
    REQUIRE(U(waveOutGetNumDevs));
    int r = ee_endpoint_count(eRender, DEVICE_STATE_ACTIVE);
    CHECK(r >= 0);
    EXPECT_EQ(r, U(waveOutGetNumDevs)());
}
TEST(ep_capture_count_equals_wavein) {
    REQUIRE(U(waveInGetNumDevs));
    int c = ee_endpoint_count(eCapture, DEVICE_STATE_ACTIVE);
    CHECK(c >= 0);
    EXPECT_EQ(c, U(waveInGetNumDevs)());
}
TEST(ep_all_count_equals_mixer) {
    REQUIRE(U(mixerGetNumDevs));
    int a = ee_endpoint_count(eAll, DEVICE_STATE_ACTIVE);
    CHECK(a >= 0);
    EXPECT_EQ(a, U(mixerGetNumDevs)());
}
TEST(ep_render_plus_capture_equals_mixer) {
    REQUIRE(U(mixerGetNumDevs));
    int r = ee_endpoint_count(eRender, DEVICE_STATE_ACTIVE), c = ee_endpoint_count(eCapture, DEVICE_STATE_ACTIVE);
    EXPECT_EQ(r + c, U(mixerGetNumDevs)());
    EXPECT_EQ(ee_endpoint_count(eAll, DEVICE_STATE_ACTIVE), r + c);
}
TEST(ep_statemask_all_includes_active) {
    REQUIRE(U(waveOutGetNumDevs));
    int act = ee_endpoint_count(eRender, DEVICE_STATE_ACTIVE), all = ee_endpoint_count(eRender, DEVICE_STATEMASK_ALL);
    CHECKF(all >= act, "all %d active %d", all, act);
    int cact = ee_endpoint_count(eCapture, DEVICE_STATE_ACTIVE), call = ee_endpoint_count(eCapture, DEVICE_STATEMASK_ALL);
    CHECKF(call >= cact, "all %d active %d", call, cact);
    t_note("endpoints render active=%d all=%d capture active=%d all=%d", act, all, cact, call);
}
TEST(ep_enum_bad_arguments) {
    REQUIRE(U(waveOutGetNumDevs));
    IMMDeviceEnumerator *e = ee_enum(NULL);
    CHECK(e != NULL);
    if (!e) return;
    IMMDeviceCollection *c = (IMMDeviceCollection *)(ULONG_PTR)1;
    EXPECT_EQ(IMMDeviceEnumerator_EnumAudioEndpoints(e, (EDataFlow)3, DEVICE_STATE_ACTIVE, &c), E_INVALIDARG);
    c = (IMMDeviceCollection *)(ULONG_PTR)1;
    EXPECT_EQ(IMMDeviceEnumerator_EnumAudioEndpoints(e, eRender, 0x10, &c), E_INVALIDARG);
    EXPECT_EQ(IMMDeviceEnumerator_EnumAudioEndpoints(e, eRender, DEVICE_STATE_ACTIVE, NULL), E_POINTER);
    IMMDevice *d = (IMMDevice *)(ULONG_PTR)1;
    EXPECT_EQ(IMMDeviceEnumerator_GetDefaultAudioEndpoint(e, eRender, eConsole, NULL), E_POINTER);
    HRESULT hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(e, (EDataFlow)3, eConsole, &d);
    EXPECT_EQ(hr, E_INVALIDARG);
    d = (IMMDevice *)(ULONG_PTR)1;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(e, eRender, (ERole)3, &d);
    EXPECT_EQ(hr, E_INVALIDARG);
    IMMDeviceEnumerator_Release(e);
}

/* ---- default endpoints ------------------------------------------------------------------------------------ */
static void default_none(EDataFlow flow) {
    IMMDeviceEnumerator *e = ee_enum(NULL);
    CHECK(e != NULL);
    if (!e) return;
    for (int role = eConsole; role <= eCommunications; role++) {
        IMMDevice *d = (IMMDevice *)(ULONG_PTR)1;
        HRESULT hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(e, flow, (ERole)role, &d);
        CHECKF(hr == E_NOTFOUND_HR, "flow %d role %d hr 0x%lx", flow, role, hr);
        CHECKF(d == NULL, "flow %d role %d out %p", flow, role, d);
        if (SUCCEEDED(hr) && d) IMMDevice_Release(d);
    }
    IMMDeviceEnumerator_Release(e);
}
TEST(ep_default_render_notfound_without_waveout) {
    REQUIRE(U(waveOutGetNumDevs));
    if (nwo()) { t_skip("render endpoints present"); return; }
    default_none(eRender);
}
TEST(ep_default_capture_notfound_without_wavein) {
    REQUIRE(U(waveInGetNumDevs));
    if (nwi()) { t_skip("capture endpoints present"); return; }
    default_none(eCapture);
}
TEST(ep_default_exists_iff_devices) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveInGetNumDevs));
    WCHAR id[256];
    HRESULT hr = ee_default_id(eRender, eConsole, id, 256);
    EXPECT_EQ(SUCCEEDED(hr), nwo() != 0);
    hr = ee_default_id(eCapture, eConsole, id, 256);
    EXPECT_EQ(SUCCEEDED(hr), nwi() != 0);
}

/* ---- DRV_QUERYFUNCTIONINSTANCEID: winmm id -> endpoint id --------------------------------------------------- */
static int find_id(WCHAR ids[][256], int n, const WCHAR *s) { for (int i = 0; i < n; i++) if (ee_wieq(ids[i], s)) return i; return -1; }
static void fiid_maps_to_endpoints(int fam, EDataFlow flow) {
    UINT n = ee_numdevs(fam);
    WCHAR eps[16][256];
    int ne = ee_endpoint_ids(flow, eps, 16);
    EXPECT_EQ(ne, n);
    int used[16] = { 0 };
    for (UINT i = 0; i < n && i < 16; i++) {
        WCHAR s[512];
        MMRESULT r = ee_fiid(fam, i, s, 512);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        int k = find_id(eps, ne, s);
        CHECKF(k >= 0, "%s %u: \"%ls\" is not an active endpoint", ee_family_name[fam], i, s);
        if (k >= 0) { CHECKF(!used[k], "two ids map to endpoint %d", k); used[k] = 1; }
        t_note("%s %u -> %ls", ee_family_name[fam], i, s);
    }
}
TEST(ep_fiid_waveout_maps_to_render_endpoints) { REQUIRE(U(waveOutMessage)); fiid_maps_to_endpoints(F_WAVEOUT, eRender); }
TEST(ep_fiid_wavein_maps_to_capture_endpoints) { REQUIRE(U(waveInMessage)); fiid_maps_to_endpoints(F_WAVEIN, eCapture); }

static void default_is_id0(int fam, EDataFlow flow, ERole role) {
    if (!ee_numdevs(fam)) { t_skip("no %s device", ee_family_name[fam]); return; }
    WCHAR def[256], s[512];
    HRESULT hr = ee_default_id(flow, role, def, 256);
    EXPECT_EQ(hr, S_OK);
    EXPECT_EQ(ee_fiid(fam, 0, s, 512), MMSYSERR_NOERROR);
    CHECKF(ee_wieq(def, s), "default \"%ls\" id0 \"%ls\"", def, s);
}
TEST(ep_waveout0_is_default_console_render) { REQUIRE(U(waveOutMessage)); default_is_id0(F_WAVEOUT, eRender, eConsole); }
TEST(ep_waveout0_is_default_multimedia_render) { REQUIRE(U(waveOutMessage)); default_is_id0(F_WAVEOUT, eRender, eMultimedia); }
TEST(ep_wavein0_is_default_console_capture) { REQUIRE(U(waveInMessage)); default_is_id0(F_WAVEIN, eCapture, eConsole); }
TEST(ep_wavein0_is_default_multimedia_capture) { REQUIRE(U(waveInMessage)); default_is_id0(F_WAVEIN, eCapture, eMultimedia); }

/* Endpoint friendly name, truncated to MAXPNAMELEN - 1 characters, is the caps szPname. */
static void names_match(int fam) {
    UINT n = ee_numdevs(fam);
    if (!n) { t_skip("no %s device", ee_family_name[fam]); return; }
    for (UINT i = 0; i < n; i++) {
        WCHAR s[512], fn[256]; WAVEOUTCAPSW c;  /* WAVEINCAPSW has the same szPname offset */
        EXPECT_EQ(ee_fiid(fam, i, s, 512), 0);
        CHECK(ee_endpoint_name(s, fn, 256));
        memset(&c, 0, sizeof c);
        EXPECT_EQ(ee_caps(fam, 1, i, &c, fam == F_WAVEOUT ? sizeof(WAVEOUTCAPSW) : sizeof(WAVEINCAPSW)), 0);
        WCHAR want[MAXPNAMELEN]; lstrcpynW(want, fn, MAXPNAMELEN);
        CHECKF(!lstrcmpW(c.szPname, want), "%s %u caps \"%ls\" endpoint \"%ls\"", ee_family_name[fam], i, c.szPname, fn);
        t_note("%s %u endpoint name \"%ls\" (%d chars)", ee_family_name[fam], i, fn, lstrlenW(fn));
    }
}
TEST(ep_waveout_name_is_truncated_endpoint_name) { REQUIRE(U(waveOutGetDevCapsW)); names_match(F_WAVEOUT); }
TEST(ep_wavein_name_is_truncated_endpoint_name) { REQUIRE(U(waveInGetDevCapsW)); names_match(F_WAVEIN); }
TEST(ep_waveout_a_name_is_truncated_endpoint_name) {
    REQUIRE(U(waveOutGetDevCapsA)); REQUIRE(U(waveOutMessage));
    UINT n = nwo();
    if (!n) { t_skip("no waveOut device"); return; }
    for (UINT i = 0; i < n; i++) {
        WCHAR s[512], fn[256]; char fa[512]; WAVEOUTCAPSA c;
        EXPECT_EQ(ee_fiid(F_WAVEOUT, i, s, 512), 0);
        CHECK(ee_endpoint_name(s, fn, 256));
        WideCharToMultiByte(CP_ACP, 0, fn, -1, fa, sizeof fa, NULL, NULL);
        fa[MAXPNAMELEN - 1] = 0;
        EXPECT_EQ(U(waveOutGetDevCapsA)(i, &c, sizeof c), 0);
        EXPECT_STR(c.szPname, fa);
    }
}
/* Mixer names: the set of mixer names is the set of truncated endpoint names (render + capture). */
TEST(ep_mixer_names_are_endpoint_names) {
    REQUIRE(U(mixerGetNumDevs)); REQUIRE(U(mixerGetDevCapsW));
    UINT n = U(mixerGetNumDevs)();
    if (!n) { t_skip("no mixer"); return; }
    WCHAR eps[16][256]; int ne = ee_endpoint_ids(eAll, eps, 16);
    EXPECT_EQ(ne, n);
    WCHAR names[16][MAXPNAMELEN];
    for (int k = 0; k < ne; k++) { WCHAR fn[256]; CHECK(ee_endpoint_name(eps[k], fn, 256)); lstrcpynW(names[k], fn, MAXPNAMELEN); }
    int used[16] = { 0 };
    for (UINT i = 0; i < n && i < 16; i++) {
        MIXERCAPSW c; memset(&c, 0, sizeof c);
        EXPECT_EQ(U(mixerGetDevCapsW)(i, &c, sizeof c), 0);
        int hit = -1;
        for (int k = 0; k < ne; k++) if (!used[k] && !lstrcmpW(c.szPname, names[k])) { hit = k; break; }
        CHECKF(hit >= 0, "mixer %u \"%ls\" has no endpoint", i, c.szPname);
        if (hit >= 0) used[hit] = 1;
        t_note("mixer %u \"%ls\" dest=%lu", i, c.szPname, c.cDestinations);
    }
}
/* Mixer order: render mixers first in waveOut order, then capture mixers in waveIn order [VM: mixer 0 Speakers,
 * mixer 1 Line In]. Checked through the names. */
TEST(ep_mixer_order_render_then_capture) {
    REQUIRE(U(mixerGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveInGetDevCapsW));
    UINT wo = nwo(), wi = nwi(), mx = U(mixerGetNumDevs)();
    if (!mx) { t_skip("no mixer"); return; }
    if (mx != wo + wi) { CHECKF(0, "mixer %u != %u + %u", mx, wo, wi); return; }
    for (UINT i = 0; i < mx; i++) {
        MIXERCAPSW m; WAVEOUTCAPSW o; WAVEINCAPSW c;
        EXPECT_EQ(U(mixerGetDevCapsW)(i, &m, sizeof m), 0);
        if (i < wo) { EXPECT_EQ(U(waveOutGetDevCapsW)(i, &o, sizeof o), 0); CHECKF(!lstrcmpW(m.szPname, o.szPname), "mixer %u \"%ls\" vs waveOut \"%ls\"", i, m.szPname, o.szPname); }
        else { EXPECT_EQ(U(waveInGetDevCapsW)(i - wo, &c, sizeof c), 0); CHECKF(!lstrcmpW(m.szPname, c.szPname), "mixer %u \"%ls\" vs waveIn \"%ls\"", i, m.szPname, c.szPname); }
    }
}
/* The endpoint each id maps to is ACTIVE and has the right data flow. */
TEST(ep_fiid_endpoint_state_and_flow) {
    REQUIRE(U(waveOutMessage)); REQUIRE(U(waveInMessage));
    IMMDeviceEnumerator *e = ee_enum(NULL);
    CHECK(e != NULL);
    if (!e) return;
    static const GUID IID_IMMEndpoint_ = { 0x1BE09788, 0x6894, 0x4089, { 0x85, 0x86, 0x9A, 0x2A, 0x6C, 0x26, 0x5A, 0xC5 } };
    for (int fam = F_WAVEOUT; fam <= F_WAVEIN; fam++) {
        UINT n = ee_numdevs(fam);
        for (UINT i = 0; i < n; i++) {
            WCHAR s[512];
            EXPECT_EQ(ee_fiid(fam, i, s, 512), 0);
            IMMDevice *d = NULL;
            HRESULT hr = IMMDeviceEnumerator_GetDevice(e, s, &d);
            EXPECT_EQ(hr, S_OK);
            if (FAILED(hr) || !d) continue;
            DWORD st = 0;
            EXPECT_EQ(IMMDevice_GetState(d, &st), S_OK);
            EXPECT_EQ(st, DEVICE_STATE_ACTIVE);
            IMMEndpoint *ep = NULL;
            if (SUCCEEDED(IMMDevice_QueryInterface(d, &IID_IMMEndpoint_, (void **)&ep)) && ep) {
                EDataFlow f = (EDataFlow)7;
                EXPECT_EQ(IMMEndpoint_GetDataFlow(ep, &f), S_OK);
                EXPECT_EQ(f, fam == F_WAVEOUT ? eRender : eCapture);
                IMMEndpoint_Release(ep);
            } else CHECK(0);
            IMMDevice_Release(d);
        }
    }
    IMMDeviceEnumerator_Release(e);
}

/* ---- DRV_QUERYFUNCTIONINSTANCEID[SIZE] protocol ---------------------------------------------------------- */
static void fiid_protocol(int fam) {
    UINT n = ee_numdevs(fam);
    if (!n) {
        ULONG sz = 0; WCHAR s[64];
        EXPECT_EQ(ee_msg(fam, 0, DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0), MMSYSERR_BADDEVICEID);
        EXPECT_EQ(ee_msg(fam, 0, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)s, sizeof s), MMSYSERR_BADDEVICEID);
        return;
    }
    for (UINT i = 0; i < n; i++) {
        ULONG sz = 0xAAAA;
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0), MMSYSERR_NOERROR);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEIDSIZE, 0, 0), MMSYSERR_INVALPARAM);
        WCHAR s[512]; ee_fill(s, sizeof s, 0x11);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)s, sizeof s), MMSYSERR_NOERROR);
        int len = ee_wlen(s, 512);
        EXPECT_EQ(sz, (ULONG)(len + 1) * 2);
        CHECK(len > 10);
        /* Exact size works. */
        WCHAR t[512]; ee_fill(t, sizeof t, 0x22);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)t, sz), MMSYSERR_NOERROR);
        EXPECT_WSTR(t, s);
        /* One character short: MOREDATA, the copy is truncated and terminated (StringCchCopyW). */
        ee_fill(t, sizeof t, 0x22);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)t, sz - 2), MMSYSERR_MOREDATA);
        EXPECT_EQ(ee_wlen(t, 512), len - 1);
        CHECK(!memcmp(t, s, (len - 1) * 2));
        /* An odd byte count rounds down to characters. */
        ee_fill(t, sizeof t, 0x22);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)t, sz + 1), MMSYSERR_NOERROR);
        EXPECT_WSTR(t, s);
        /* Sizes 0 and 1 (0 characters): MOREDATA, nothing written. */
        ee_fill(t, sizeof t, 0x22);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)t, 0), MMSYSERR_MOREDATA);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)t, 1), MMSYSERR_MOREDATA);
        CHECK(ee_all(t, sizeof t, 0x22));
        /* Size 2 (1 character): MOREDATA with an empty string. */
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)t, 2), MMSYSERR_MOREDATA);
        EXPECT_EQ(t[0], 0);
        /* NULL buffer: INVALPARAM, whatever the size. */
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEID, 0, sz), MMSYSERR_INVALPARAM);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYFUNCTIONINSTANCEID, 0, 0), MMSYSERR_INVALPARAM);
    }
    /* Out of range: the id check comes first. */
    ULONG sz; WCHAR s[64];
    EXPECT_EQ(ee_msg(fam, n, DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(ee_msg(fam, n, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)s, sizeof s), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(ee_msg(fam, n, DRV_QUERYFUNCTIONINSTANCEID, 0, 0), MMSYSERR_BADDEVICEID);
}
TEST(ep_fiid_protocol_waveout) { REQUIRE(U(waveOutMessage)); fiid_protocol(F_WAVEOUT); }
TEST(ep_fiid_protocol_wavein) { REQUIRE(U(waveInMessage)); fiid_protocol(F_WAVEIN); }
TEST(ep_fiid_ids_are_mmdevapi_format) {
    REQUIRE(U(waveOutMessage));
    for (int fam = F_WAVEOUT; fam <= F_WAVEIN; fam++) {
        UINT n = ee_numdevs(fam);
        for (UINT i = 0; i < n; i++) {
            WCHAR s[512];
            EXPECT_EQ(ee_fiid(fam, i, s, 512), 0);
            /* "{0.0.0.00000000}.{guid}" render, "{0.0.1.00000000}.{guid}" capture */
            const WCHAR *want = fam == F_WAVEOUT ? L"{0.0.0.00000000}.{" : L"{0.0.1.00000000}.{";
            CHECKF(!wcsncmp(s, want, 18), "%s %u: \"%ls\"", ee_family_name[fam], i, s);
            EXPECT_EQ(lstrlenW(s), 18 + 36 + 1);
        }
    }
}
/* The mapper entry has no endpoint: SIZE reports 2 (an empty string), the id itself is "". */
static void fiid_mapper(int fam) {
    if (!(nwo() + nwi())) {
        ULONG sz;
        EXPECT_EQ(ee_msg(fam, WAVE_MAPPER, DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0), MMSYSERR_BADDEVICEID);
        return;
    }
    ULONG sz = 0xAAAA; WCHAR s[64]; ee_fill(s, sizeof s, 0x33);
    MMRESULT r = ee_msg(fam, WAVE_MAPPER, DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    EXPECT_EQ(sz, 2);
    r = ee_msg(fam, WAVE_MAPPER, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)s, sizeof s);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    EXPECT_EQ(s[0], 0);
    EXPECT_EQ(ee_msg(fam, WAVE_MAPPER, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)s, 0), MMSYSERR_MOREDATA);
    EXPECT_EQ(ee_msg(fam, WAVE_MAPPER, DRV_QUERYFUNCTIONINSTANCEID, 0, sizeof s), MMSYSERR_INVALPARAM);
}
TEST(ep_fiid_waveout_mapper_empty) { REQUIRE(U(waveOutMessage)); fiid_mapper(F_WAVEOUT); }
TEST(ep_fiid_wavein_mapper_empty) { REQUIRE(U(waveInMessage)); fiid_mapper(F_WAVEIN); }

/* ---- DRV_QUERYDEVICEINTERFACE[SIZE] ----------------------------------------------------------------------- */
static void devif_protocol(int fam) {
    UINT n = ee_numdevs(fam);
    for (UINT i = 0; i < n; i++) {
        ULONG sz = 0;
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0), MMSYSERR_NOERROR);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYDEVICEINTERFACESIZE, 0, 0), MMSYSERR_INVALPARAM);
        WCHAR s[1024]; ee_fill(s, sizeof s, 0x44);
        CHECK(sz >= 8 && sz <= sizeof s);
        if (sz < 8 || sz > sizeof s) continue;
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)s, sz), MMSYSERR_NOERROR);
        EXPECT_EQ((ULONG)(lstrlenW(s) + 1) * 2, sz);
        CHECKF(!wcsncmp(s, L"\\\\?\\", 4), "%s %u interface \"%ls\"", ee_family_name[fam], i, s);
        t_note("%s %u interface %ls", ee_family_name[fam], i, s);
        WCHAR t[1024]; ee_fill(t, sizeof t, 0x44);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)t, sz - 2), MMSYSERR_MOREDATA);
        EXPECT_EQ(lstrlenW(t), lstrlenW(s) - 1);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYDEVICEINTERFACE, 0, sz), MMSYSERR_INVALPARAM);
        ee_fill(t, sizeof t, 0x44);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)t, 0), MMSYSERR_MOREDATA);
        CHECK(ee_all(t, sizeof t, 0x44));
    }
    ULONG sz;
    EXPECT_EQ(ee_msg(fam, n, DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0), MMSYSERR_BADDEVICEID);
}
TEST(ep_devinterface_protocol_waveout) { REQUIRE(U(waveOutMessage)); devif_protocol(F_WAVEOUT); }
TEST(ep_devinterface_protocol_wavein) { REQUIRE(U(waveInMessage)); devif_protocol(F_WAVEIN); }
TEST(ep_devinterface_mapper) {
    REQUIRE(U(waveOutMessage));
    if (!(nwo() + nwi())) { t_skip("no wave device"); return; }
    ULONG sz = 0xAAAA; WCHAR s[64]; ee_fill(s, sizeof s, 0x55);
    EXPECT_EQ(ee_msg(F_WAVEOUT, WAVE_MAPPER, DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0), MMSYSERR_NOERROR);
    EXPECT_EQ(sz, 2);
    EXPECT_EQ(ee_msg(F_WAVEOUT, WAVE_MAPPER, DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)s, sizeof s), MMSYSERR_NOERROR);
    EXPECT_EQ(s[0], 0);
}
/* Render and capture endpoints of one codec share the interface of that codec's device. */
TEST(ep_devinterface_recorded_for_pairs) {
    REQUIRE(U(waveOutMessage));
    if (!nwo() || !nwi()) { t_skip("needs render and capture"); return; }
    WCHAR a[1024], b[1024];
    EXPECT_EQ(ee_msg(F_WAVEOUT, 0, DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)a, sizeof a), 0);
    EXPECT_EQ(ee_msg(F_WAVEIN, 0, DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)b, sizeof b), 0);
    t_note("waveOut 0 and waveIn 0 interfaces %s", ee_wieq(a, b) ? "equal" : "differ");
}

/* ---- string ids ------------------------------------------------------------------------------------------- */
static void stringid_roundtrip(int fam) {
    UINT n = ee_numdevs(fam);
    for (UINT i = 0; i < n; i++) {
        ULONG sz = 0;
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYSTRINGIDSIZE, (DWORD_PTR)&sz, 0), MMSYSERR_NOERROR);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYSTRINGIDSIZE, 0, 0), MMSYSERR_INVALPARAM);
        CHECK(sz > 2 && sz < 4096);
        if (sz <= 2 || sz >= 4096) continue;
        WCHAR s[2048]; ee_fill(s, sizeof s, 0x66);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYSTRINGID, (DWORD_PTR)s, sz), MMSYSERR_NOERROR);
        int len = ee_wlen(s, 2048);
        CHECKF((ULONG)(len + 1) * 2 <= sz, "string id length %d size %lu", len, sz);
        t_note("%s %u string id (size %lu, len %d) %ls", ee_family_name[fam], i, sz, len, s);
        /* Reverse lookup through any valid id (the message is answered by winmmbase, not by the device). */
        UINT back = 0xBEEF;
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYIDFROMSTRINGID, (DWORD_PTR)s, (DWORD_PTR)&back), MMSYSERR_NOERROR);
        EXPECT_EQ(back, i);
        back = 0xBEEF;
        EXPECT_EQ(ee_msg(fam, 0, DRV_QUERYIDFROMSTRINGID, (DWORD_PTR)s, (DWORD_PTR)&back), MMSYSERR_NOERROR);
        EXPECT_EQ(back, i);
        /* Too small for the string (exact length minus one character): MOREDATA. */
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYSTRINGID, (DWORD_PTR)s, (len + 1) * 2 - 2), MMSYSERR_MOREDATA);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYIDFROMSTRINGID, 0, (DWORD_PTR)&back), MMSYSERR_INVALPARAM);
        EXPECT_EQ(ee_msg(fam, i, DRV_QUERYIDFROMSTRINGID, (DWORD_PTR)s, 0), MMSYSERR_INVALPARAM);
    }
}
TEST(ep_stringid_roundtrip_waveout) { REQUIRE(U(waveOutMessage)); stringid_roundtrip(F_WAVEOUT); }
TEST(ep_stringid_roundtrip_wavein) { REQUIRE(U(waveInMessage)); stringid_roundtrip(F_WAVEIN); }
TEST(ep_stringid_unknown_string) {
    REQUIRE(U(waveOutMessage));
    if (!nwo()) { t_skip("no waveOut device"); return; }
    UINT back = 0xBEEF;
    MMRESULT r = ee_msg(F_WAVEOUT, 0, DRV_QUERYIDFROMSTRINGID, (DWORD_PTR)L"no such string id", (DWORD_PTR)&back);
    CHECKF(r != MMSYSERR_NOERROR, "unknown string id -> %u", r);
    t_note("unknown string id -> %u (back 0x%x)", r, back);
}
TEST(ep_stringids_distinct) {
    REQUIRE(U(waveOutMessage));
    UINT wo = nwo(), wi = nwi();
    WCHAR all[16][512]; int k = 0;
    for (int fam = F_WAVEOUT; fam <= F_WAVEIN; fam++)
        for (UINT i = 0; i < ee_numdevs(fam) && k < 16; i++)
            if (!ee_msg(fam, i, DRV_QUERYSTRINGID, (DWORD_PTR)all[k], sizeof all[k])) k++;
    EXPECT_EQ(k, (int)(wo + wi));
    for (int a = 0; a < k; a++) for (int b = a + 1; b < k; b++) CHECKF(lstrcmpW(all[a], all[b]) != 0, "ids %d and %d equal", a, b);
}

/* ---- mapper preferred device ------------------------------------------------------------------------------ */
TEST(ep_mapper_preferred_get_waveout) {
    REQUIRE(U(waveOutMessage));
    DWORD id = 0xAAAA, fl = 0xBBBB;
    MMRESULT r = U(waveOutMessage)((HWAVEOUT)(UINT_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&id, (DWORD_PTR)&fl);
    if (!(nwo() + nwi())) { EXPECT_EQ(r, MMSYSERR_BADDEVICEID); return; }
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (nwo()) EXPECT_EQ(id, 0);              /* device 0 is the default render endpoint */
    else EXPECT_EQ(id, 0xFFFFFFFF);
    CHECKF(fl == 0 || fl == DRVM_MAPPER_PREFERRED_FLAGS_PREFERREDONLY, "flags 0x%lx", fl);
    t_note("preferred waveOut id=%ld flags=0x%lx", (long)id, fl);
}
TEST(ep_mapper_preferred_get_wavein) {
    REQUIRE(U(waveInMessage));
    DWORD id = 0xAAAA, fl = 0xBBBB;
    MMRESULT r = U(waveInMessage)((HWAVEIN)(UINT_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&id, (DWORD_PTR)&fl);
    if (!(nwo() + nwi())) { EXPECT_EQ(r, MMSYSERR_BADDEVICEID); return; }
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (nwi()) EXPECT_EQ(id, 0);
    else EXPECT_EQ(id, 0xFFFFFFFF);
    CHECKF(fl == 0 || fl == DRVM_MAPPER_PREFERRED_FLAGS_PREFERREDONLY, "flags 0x%lx", fl);
}
TEST(ep_mapper_preferred_get_null_args) {
    REQUIRE(U(waveOutMessage));
    if (!(nwo() + nwi())) { t_skip("no wave device"); return; }
    DWORD id, fl;
    EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)(UINT_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, 0, (DWORD_PTR)&fl), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)(UINT_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&id, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveInMessage)((HWAVEIN)(UINT_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, 0, 0), MMSYSERR_INVALPARAM);
}
TEST(ep_mapper_preferred_get_on_device_is_invalparam) {
    REQUIRE(U(waveOutMessage));
    if (!nwo()) { t_skip("no waveOut device"); return; }
    DWORD id = 0xAAAA, fl = 0xBBBB;
    /* Only the mapper entry (driver flag 2) answers: a device id falls to INVALPARAM. */
    EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)0, DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&id, (DWORD_PTR)&fl), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)0, DRVM_MAPPER_CONSOLEVOICECOM_GET, (DWORD_PTR)&id, (DWORD_PTR)&fl), MMSYSERR_INVALPARAM);
}
TEST(ep_mapper_preferred_is_default_endpoint) {
    REQUIRE(U(waveOutMessage));
    if (!nwo()) { t_skip("no waveOut device"); return; }
    DWORD id = 0xAAAA, fl = 0;
    EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)(UINT_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&id, (DWORD_PTR)&fl), 0);
    WCHAR def[256], s[512];
    EXPECT_EQ(ee_default_id(eRender, eConsole, def, 256), S_OK);
    EXPECT_EQ(ee_fiid(F_WAVEOUT, id, s, 512), 0);
    CHECK(ee_wieq(def, s));
}
TEST(ep_mapper_consolevoicecom_is_communications_endpoint) {
    REQUIRE(U(waveOutMessage));
    DWORD id = 0xAAAA, fl = 0xBBBB;
    MMRESULT r = U(waveOutMessage)((HWAVEOUT)(UINT_PTR)WAVE_MAPPER, DRVM_MAPPER_CONSOLEVOICECOM_GET, (DWORD_PTR)&id, (DWORD_PTR)&fl);
    if (!(nwo() + nwi())) { EXPECT_EQ(r, MMSYSERR_BADDEVICEID); return; }
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    t_note("console voice com waveOut id=%ld flags=0x%lx", (long)id, fl);
    if (!nwo()) { EXPECT_EQ(id, 0xFFFFFFFF); return; }
    WCHAR def[256], s[512];
    EXPECT_EQ(ee_default_id(eRender, eCommunications, def, 256), S_OK);
    CHECK(id < nwo());
    if (id < nwo()) { EXPECT_EQ(ee_fiid(F_WAVEOUT, id, s, 512), 0); CHECK(ee_wieq(def, s)); }
}
TEST(ep_mapper_consolevoicecom_wavein) {
    REQUIRE(U(waveInMessage));
    DWORD id = 0xAAAA, fl = 0xBBBB;
    MMRESULT r = U(waveInMessage)((HWAVEIN)(UINT_PTR)WAVE_MAPPER, DRVM_MAPPER_CONSOLEVOICECOM_GET, (DWORD_PTR)&id, (DWORD_PTR)&fl);
    if (!(nwo() + nwi())) { EXPECT_EQ(r, MMSYSERR_BADDEVICEID); return; }
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (nwi()) EXPECT_EQ(id, 0); else EXPECT_EQ(id, 0xFFFFFFFF);
}

/* ---- other internal messages on the id path --------------------------------------------------------------- */
TEST(ep_internal_notsupported_messages) {
    REQUIRE(U(waveOutMessage));
    if (!nwo()) { t_skip("no waveOut device"); return; }
    static const UINT m[] = { DRV_RESERVED + 1, DRV_RESERVED + 2, DRV_RESERVED + 3, DRV_RESERVED + 4, DRV_RESERVED + 6 };
    for (size_t k = 0; k < sizeof m / sizeof m[0]; k++) {
        DWORD buf[64] = { 0 };
        MMRESULT r = U(waveOutMessage)((HWAVEOUT)0, m[k], (DWORD_PTR)buf, sizeof buf);
        CHECKF(r == MMSYSERR_NOTSUPPORTED, "msg 0x%x -> %u", m[k], r);
        r = U(waveInMessage)((HWAVEIN)(UINT_PTR)(nwi() ? 0 : 0), m[k], (DWORD_PTR)buf, sizeof buf);
        if (nwi()) CHECKF(r == MMSYSERR_NOTSUPPORTED, "waveIn msg 0x%x -> %u", m[k], r);
    }
}
TEST(ep_internal_querymodule_is_wdmaud) {
    REQUIRE(U(waveOutMessage));
    if (!nwo()) { t_skip("no waveOut device"); return; }
    HMODULE m = NULL;
    EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)0, DRV_QUERYMODULE, (DWORD_PTR)&m, 0), MMSYSERR_NOERROR);
    CHECK(m != NULL);
    char p[MAX_PATH] = "";
    if (m) GetModuleFileNameA(m, p, sizeof p);
    const char *leaf = strrchr(p, '\\'); leaf = leaf ? leaf + 1 : p;
    CHECKF(!lstrcmpiA(leaf, "wdmaud.drv"), "module \"%s\"", p);
    EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)0, DRV_QUERYMODULE, 0, 0), MMSYSERR_INVALPARAM);
}
TEST(ep_internal_querymodule_mapper_is_msacm32) {
    REQUIRE(U(waveOutMessage));
    if (!(nwo() + nwi())) { t_skip("no wave device"); return; }
    HMODULE m = NULL;
    EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)(UINT_PTR)WAVE_MAPPER, DRV_QUERYMODULE, (DWORD_PTR)&m, 0), MMSYSERR_NOERROR);
    char p[MAX_PATH] = "";
    if (m) GetModuleFileNameA(m, p, sizeof p);
    const char *leaf = strrchr(p, '\\'); leaf = leaf ? leaf + 1 : p;
    CHECKF(!lstrcmpiA(leaf, "msacm32.drv"), "mapper module \"%s\"", p);
}
TEST(ep_internal_driver_device_count) {
    REQUIRE(U(waveOutMessage));
    /* DRV_RESERVED + 8: the per-driver device count; one device per endpoint driver. */
    for (int fam = F_WAVEOUT; fam <= F_WAVEIN; fam++) {
        UINT n = ee_numdevs(fam);
        for (UINT i = 0; i < n; i++) {
            DWORD c = 0xAAAA;
            EXPECT_EQ(ee_msg(fam, i, DRV_RESERVED + 8, (DWORD_PTR)&c, 0), MMSYSERR_NOERROR);
            EXPECT_EQ(c, 1);
            EXPECT_EQ(ee_msg(fam, i, DRV_RESERVED + 8, 0, 0), MMSYSERR_INVALPARAM);
        }
    }
}
TEST(ep_internal_querymappable) {
    REQUIRE(U(waveOutMessage));
    if (!nwo()) {
        EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)0, DRV_QUERYMAPPABLE, 0, 0), MMSYSERR_BADDEVICEID);
        return;
    }
    for (UINT i = 0; i < nwo(); i++) EXPECT_EQ(U(waveOutMessage)((HWAVEOUT)(UINT_PTR)i, DRV_QUERYMAPPABLE, 0, 0), MMSYSERR_NOERROR);
}
/* The same messages sent to an OPEN handle are not intercepted: they go straight to the driver instance. */
TEST(ep_fiid_by_handle_recorded) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutMessage));
    if (!nwo()) { t_skip("no waveOut device"); return; }
    WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0 };
    HWAVEOUT h = NULL;
    EXPECT_EQ(U(waveOutOpen)(&h, 0, &wf, 0, 0, CALLBACK_NULL), 0);
    if (!h) return;
    WCHAR s[512] = L"x"; ULONG sz = 0xAAAA;
    MMRESULT r1 = U(waveOutMessage)(h, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)s, sizeof s);
    MMRESULT r2 = U(waveOutMessage)(h, DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0);
    t_note("by handle: FIID -> %u \"%ls\", SIZE -> %u (%lu)", r1, r1 ? L"" : s, r2, sz);
    EXPECT_EQ(U(waveOutClose)(h), 0);
}

/* ---- MIDI: the GS synth is not an endpoint ---------------------------------------------------------------- */
TEST(ep_midiout_synth_fiid_recorded) {
    REQUIRE(U(midiOutMessage));
    WCHAR s[512]; ULONG sz = 0xAAAA; ee_fill(s, sizeof s, 0);
    MMRESULT r = U(midiOutMessage)((HMIDIOUT)0, DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0);
    MMRESULT r2 = U(midiOutMessage)((HMIDIOUT)0, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)s, sizeof s);
    t_note("midiOut 0 FIID size -> %u (%lu), id -> %u \"%ls\"", r, sz, r2, s);
    UINT n = U(midiOutGetNumDevs)();
    EXPECT_EQ(U(midiOutMessage)((HMIDIOUT)(UINT_PTR)n, DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&sz, 0), MMSYSERR_BADDEVICEID);
}

/* ---- mmdevapi export table: ordinal-only exports resolve, never called ------------------------------------ */
TEST(ep_mmdevapi_ordinals_resolve) {
    HMODULE m = t_module("mmdevapi.dll");
    CHECK(m != NULL);
    if (!m) return;
    int missing = 0;
    for (unsigned o = 2; o <= 36; o++) if (!t_proc_ord("mmdevapi.dll", o)) { missing++; t_note("ordinal %u missing", o); }
    EXPECT_EQ(missing, 0);
    EXPECT_EQ(t_proc_ord("mmdevapi.dll", 1), NULL);
    EXPECT_EQ(t_proc_ord("mmdevapi.dll", 37), NULL);
}
TEST(ep_mmdevapi_named_exports_match_ordinals) {
    REQUIRE(t_module("mmdevapi.dll"));
    EXPECT_EQ(t_proc_in("mmdevapi.dll", "ActivateAudioInterfaceAsync"), t_proc_ord("mmdevapi.dll", 17));
    EXPECT_EQ(t_proc_in("mmdevapi.dll", "DllCanUnloadNow"), t_proc_ord("mmdevapi.dll", 33));
    EXPECT_EQ(t_proc_in("mmdevapi.dll", "DllGetClassObject"), t_proc_ord("mmdevapi.dll", 34));
    EXPECT_EQ(t_proc_in("mmdevapi.dll", "DllRegisterServer"), t_proc_ord("mmdevapi.dll", 35));
    EXPECT_EQ(t_proc_in("mmdevapi.dll", "DllUnregisterServer"), t_proc_ord("mmdevapi.dll", 36));
    CHECK(t_proc_in("mmdevapi.dll", "ActivateAudioInterfaceAsync") != NULL);
}
TEST(ep_mmdevapi_ordinals_distinct_and_in_image) {
    HMODULE m = t_module("mmdevapi.dll");
    REQUIRE(m);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)m;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)m + dos->e_lfanew);
    ULONG_PTR lo = (ULONG_PTR)m, hi = lo + nt->OptionalHeader.SizeOfImage;
    FARPROC p[40] = { 0 };
    int out = 0, dup = 0;
    for (unsigned o = 2; o <= 36; o++) {
        p[o] = t_proc_ord("mmdevapi.dll", o);
        if ((ULONG_PTR)p[o] < lo || (ULONG_PTR)p[o] >= hi) out++;
        for (unsigned q = 2; q < o; q++) if (p[q] == p[o]) dup++;
    }
    EXPECT_EQ(out, 0);
    EXPECT_EQ(dup, 0);
}
TEST(ep_winmm_loads_mmdevapi) {
    REQUIRE(U(waveOutGetNumDevs));
    (void)U(waveOutGetNumDevs)();
    /* winmmbase asks mmdevapi for endpoints during the device-list build. */
    CHECK(GetModuleHandleA("winmmbase.dll") != NULL);
    t_note("mmdevapi loaded after GetNumDevs: %d", GetModuleHandleA("mmdevapi.dll") != NULL);
}
