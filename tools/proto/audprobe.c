/* audprobe: what audio does the Win10 guest expose at run.cmd time?
 *
 *   audprobe.exe poll <seconds>   uptime-stamped counts until endpoints are stable (or the deadline)
 *   audprobe.exe full             services, every legacy device class + caps, mmdevapi endpoints,
 *                                 format matrix, then (if devices exist) a short known tone sequence
 *                                 through waveOut, PlaySound(SND_MEMORY), a MIDI note, and a short capture.
 *
 * Output is "key: value" lines meant for COM1. Read-only towards system state: no volume, default
 * device or registry changes. All waits are bounded; a watchdog ends the process after 240 s.
 * Tone plan (for host-side analysis of an audiodev wav capture):
 *   waveOut  : 600 ms 440 Hz, 600 ms 1000 Hz (stereo 44.1k/16, -12 dBFS)
 *   PlaySound: 400 ms 660 Hz (mono 22.05k/16, -12 dBFS)
 *   MIDI     : note 81 (A5 = 880 Hz) vel 64, 400 ms, on MIDI_MAPPER
 */
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdio.h>
#include <math.h>

#ifndef DRV_QUERYFUNCTIONINSTANCEID
#define DRV_QUERYFUNCTIONINSTANCEID      (DRV_RESERVED + 17)
#define DRV_QUERYFUNCTIONINSTANCEIDSIZE  (DRV_RESERVED + 18)
#endif
#ifndef DRVM_MAPPER_PREFERRED_GET
#define DRVM_MAPPER_PREFERRED_GET 0x2015
#endif

static ULONGLONG up(void) { return GetTickCount64(); }
#define P(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)

static DWORD WINAPI watchdog(void *p) { Sleep((DWORD)(UINT_PTR)p); P("watchdog: timeout, exiting\n"); ExitProcess(3); return 0; }

static void wfmt(const WCHAR *w, char *out, int cap) { if (!w) { strcpy(out, "(null)"); return; } WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL); }

/* ---------------- services ---------------- */
static const char *svc_state_name(DWORD s) {
    switch (s) { case 1: return "STOPPED"; case 2: return "START_PENDING"; case 3: return "STOP_PENDING"; case 4: return "RUNNING";
    case 5: return "CONTINUE_PENDING"; case 6: return "PAUSE_PENDING"; case 7: return "PAUSED"; } return "?"; }
static DWORD svc_state(const char *name, DWORD *start_type) {
    DWORD st = 0; if (start_type) *start_type = 0xffffffff;
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT); if (!scm) return 0;
    SC_HANDLE s = OpenServiceA(scm, name, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
    if (s) {
        SERVICE_STATUS ss; if (QueryServiceStatus(s, &ss)) st = ss.dwCurrentState;
        if (start_type) { BYTE buf[8192]; DWORD need; if (QueryServiceConfigA(s, (QUERY_SERVICE_CONFIGA *)buf, sizeof buf, &need)) *start_type = ((QUERY_SERVICE_CONFIGA *)buf)->dwStartType; }
        CloseServiceHandle(s);
    }
    CloseServiceHandle(scm); return st;
}
static void services(void) {
    const char *names[] = { "AudioSrv", "AudioEndpointBuilder", "MMCSS", "hdaudbus", "HdAudAddService", "usbaudio", "wdmaud", "sysaudio" };
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        DWORD stt, st = svc_state(names[i], &stt);
        P("svc %s: state=%s(%lu) start_type=%ld\n", names[i], st ? svc_state_name(st) : "absent", st, (long)stt);
    }
}

/* ---------------- mmdevapi ---------------- */
static IMMDeviceEnumerator *g_en;
static int ep_count(EDataFlow flow, DWORD mask) {
    IMMDeviceCollection *c = NULL; UINT n = 0;
    if (!g_en || FAILED(IMMDeviceEnumerator_EnumAudioEndpoints(g_en, flow, mask, &c))) return -1;
    IMMDeviceCollection_GetCount(c, &n); IMMDeviceCollection_Release(c); return (int)n;
}
static void print_fmt(const char *tag, const WAVEFORMATEX *f) {
    P("%s: tag=0x%x ch=%u rate=%lu bits=%u align=%u avg=%lu cb=%u", tag, f->wFormatTag, f->nChannels, f->nSamplesPerSec, f->wBitsPerSample, f->nBlockAlign, f->nAvgBytesPerSec, f->cbSize);
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE *x = (const void *)f;
        P(" valid=%u mask=0x%lx sub=%08lx", x->Samples.wValidBitsPerSample, x->dwChannelMask, x->SubFormat.Data1);
    }
    P("\n");
}
static void ep_dump(void) {
    if (!g_en) { P("mmdev: no enumerator\n"); return; }
    const char *fl[] = { "render", "capture" };
    for (int f = 0; f < 2; f++) {
        IMMDeviceCollection *c = NULL; UINT n = 0;
        HRESULT hr = IMMDeviceEnumerator_EnumAudioEndpoints(g_en, (EDataFlow)f, DEVICE_STATEMASK_ALL, &c);
        if (FAILED(hr)) { P("mmdev %s enum hr=0x%08lx\n", fl[f], hr); continue; }
        IMMDeviceCollection_GetCount(c, &n);
        P("mmdev %s endpoints (all states): %u\n", fl[f], n);
        for (UINT i = 0; i < n; i++) {
            IMMDevice *d = NULL; LPWSTR id = NULL; DWORD st = 0; char ids[512], name[512] = "?", desc[512] = "?", ifn[512] = "?";
            if (FAILED(IMMDeviceCollection_Item(c, i, &d))) continue;
            IMMDevice_GetId(d, &id); IMMDevice_GetState(d, &st); wfmt(id, ids, sizeof ids);
            IPropertyStore *ps = NULL;
            if (SUCCEEDED(IMMDevice_OpenPropertyStore(d, STGM_READ, &ps))) {
                PROPVARIANT v; PropVariantInit(&v);
                if (SUCCEEDED(IPropertyStore_GetValue(ps, &PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) wfmt(v.pwszVal, name, sizeof name);
                PropVariantClear(&v);
                if (SUCCEEDED(IPropertyStore_GetValue(ps, &PKEY_Device_DeviceDesc, &v)) && v.vt == VT_LPWSTR) wfmt(v.pwszVal, desc, sizeof desc);
                PropVariantClear(&v);
                if (SUCCEEDED(IPropertyStore_GetValue(ps, &PKEY_DeviceInterface_FriendlyName, &v)) && v.vt == VT_LPWSTR) wfmt(v.pwszVal, ifn, sizeof ifn);
                PropVariantClear(&v);
                IPropertyStore_Release(ps);
            }
            P("  ep[%u] state=0x%lx id=%s\n        name=\"%s\" desc=\"%s\" adapter=\"%s\"\n", i, st, ids, name, desc, ifn);
            if (st == DEVICE_STATE_ACTIVE) {
                IAudioClient *ac = NULL;
                if (SUCCEEDED(IMMDevice_Activate(d, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&ac))) {
                    WAVEFORMATEX *mf = NULL; REFERENCE_TIME def = 0, mn = 0;
                    if (SUCCEEDED(IAudioClient_GetMixFormat(ac, &mf))) { print_fmt("        mixfmt", mf); CoTaskMemFree(mf); }
                    if (SUCCEEDED(IAudioClient_GetDevicePeriod(ac, &def, &mn))) P("        period default=%lld min=%lld (100ns)\n", def, mn);
                    IAudioClient_Release(ac);
                }
                IAudioEndpointVolume *ev = NULL;
                if (SUCCEEDED(IMMDevice_Activate(d, &IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void **)&ev))) {
                    float sc = -1, db = 0, mi = 0, ma = 0, inc = 0; BOOL mute = 0; UINT ch = 0;
                    IAudioEndpointVolume_GetMasterVolumeLevelScalar(ev, &sc); IAudioEndpointVolume_GetMasterVolumeLevel(ev, &db);
                    IAudioEndpointVolume_GetMute(ev, &mute); IAudioEndpointVolume_GetChannelCount(ev, &ch); IAudioEndpointVolume_GetVolumeRange(ev, &mi, &ma, &inc);
                    P("        volume scalar=%.3f db=%.2f range=[%.2f,%.2f]/%.3f mute=%d channels=%u\n", sc, db, mi, ma, inc, mute, ch);
                    IAudioEndpointVolume_Release(ev);
                }
            }
            CoTaskMemFree(id); IMMDevice_Release(d);
        }
        IMMDeviceCollection_Release(c);
        const char *rn[] = { "console", "multimedia", "communications" };
        for (int r = 0; r < 3; r++) {
            IMMDevice *d = NULL; HRESULT h = IMMDeviceEnumerator_GetDefaultAudioEndpoint(g_en, (EDataFlow)f, (ERole)r, &d);
            if (SUCCEEDED(h)) { LPWSTR id = NULL; char ids[512]; IMMDevice_GetId(d, &id); wfmt(id, ids, sizeof ids); P("mmdev default %s/%s: %s\n", fl[f], rn[r], ids); CoTaskMemFree(id); IMMDevice_Release(d); }
            else P("mmdev default %s/%s: hr=0x%08lx\n", fl[f], rn[r], h);
        }
    }
}

/* ---------------- legacy counts ---------------- */
typedef struct { int wo, wi, mo, mi, mx, ax, js, epr, epc; DWORD srv; } counts_t;
static void get_counts(counts_t *c) {
    c->wo = waveOutGetNumDevs(); c->wi = waveInGetNumDevs(); c->mo = midiOutGetNumDevs(); c->mi = midiInGetNumDevs();
    c->mx = mixerGetNumDevs(); c->ax = auxGetNumDevs(); c->js = joyGetNumDevs();
    c->epr = ep_count(eRender, DEVICE_STATE_ACTIVE); c->epc = ep_count(eCapture, DEVICE_STATE_ACTIVE);
    c->srv = svc_state("AudioSrv", NULL);
}
static void print_counts(const char *tag, const counts_t *c) {
    P("%s t=%llums waveOut=%d waveIn=%d midiOut=%d midiIn=%d mixer=%d aux=%d joy=%d ep_render=%d ep_capture=%d AudioSrv=%s\n",
      tag, up(), c->wo, c->wi, c->mo, c->mi, c->mx, c->ax, c->js, c->epr, c->epc, c->srv ? svc_state_name(c->srv) : "absent");
}

/* ---------------- legacy caps ---------------- */
static void instance_id(const char *cls, UINT id, MMRESULT (WINAPI *msg)(HANDLE, UINT, DWORD_PTR, DWORD_PTR)) {
    ULONG cb = 0; MMRESULT r = msg((HANDLE)(UINT_PTR)id, DRV_QUERYFUNCTIONINSTANCEIDSIZE, (DWORD_PTR)&cb, 0);
    WCHAR buf[1024] = {0}; char s[1100] = "";
    MMRESULT r2 = (r == 0 && cb && cb <= sizeof buf) ? msg((HANDLE)(UINT_PTR)id, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)buf, cb) : 0xffff;
    wfmt(buf, s, sizeof s);
    P("    %s[%d] instanceid: size_r=%u cb=%lu r=%u \"%s\"\n", cls, (int)id, r, cb, r2, s);
}
/* waveOutMessage & friends take HWAVEOUT etc.; with a device id cast they address the device */
static MMRESULT WINAPI wom(HANDLE h, UINT m, DWORD_PTR a, DWORD_PTR b) { return waveOutMessage((HWAVEOUT)h, m, a, b); }
static MMRESULT WINAPI wim(HANDLE h, UINT m, DWORD_PTR a, DWORD_PTR b) { return waveInMessage((HWAVEIN)h, m, a, b); }
static MMRESULT WINAPI mom(HANDLE h, UINT m, DWORD_PTR a, DWORD_PTR b) { return midiOutMessage((HMIDIOUT)h, m, a, b); }

static void caps_all(void) {
    int n; char s[256];
    n = waveOutGetNumDevs();
    for (int i = -1; i <= n; i++) {
        WAVEOUTCAPSW c; memset(&c, 0, sizeof c); MMRESULT r = waveOutGetDevCapsW((UINT_PTR)(UINT)i, &c, sizeof c); wfmt(c.szPname, s, sizeof s);
        P("waveOutCaps[%d] r=%u mid=%u pid=%u ver=0x%x name=\"%s\" formats=0x%lx ch=%u support=0x%lx\n", i, r, c.wMid, c.wPid, c.vDriverVersion, s, c.dwFormats, c.wChannels, c.dwSupport);
        if (i >= 0 && i < n) instance_id("waveOut", i, wom);
    }
    { DWORD pref = 0xdead, fl = 0xdead; MMRESULT r = waveOutMessage((HWAVEOUT)(UINT_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&pref, (DWORD_PTR)&fl); P("waveOut mapper preferred: r=%u dev=%ld flags=0x%lx\n", r, (long)pref, fl); }
    n = waveInGetNumDevs();
    for (int i = -1; i <= n; i++) {
        WAVEINCAPSW c; memset(&c, 0, sizeof c); MMRESULT r = waveInGetDevCapsW((UINT_PTR)(UINT)i, &c, sizeof c); wfmt(c.szPname, s, sizeof s);
        P("waveInCaps[%d] r=%u mid=%u pid=%u ver=0x%x name=\"%s\" formats=0x%lx ch=%u\n", i, r, c.wMid, c.wPid, c.vDriverVersion, s, c.dwFormats, c.wChannels);
        if (i >= 0 && i < n) instance_id("waveIn", i, wim);
    }
    { DWORD pref = 0xdead, fl = 0xdead; MMRESULT r = waveInMessage((HWAVEIN)(UINT_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&pref, (DWORD_PTR)&fl); P("waveIn mapper preferred: r=%u dev=%ld flags=0x%lx\n", r, (long)pref, fl); }
    n = midiOutGetNumDevs();
    for (int i = -1; i <= n; i++) {
        MIDIOUTCAPSW c; memset(&c, 0, sizeof c); MMRESULT r = midiOutGetDevCapsW((UINT_PTR)(UINT)i, &c, sizeof c); wfmt(c.szPname, s, sizeof s);
        P("midiOutCaps[%d] r=%u mid=%u pid=%u ver=0x%x name=\"%s\" tech=%u voices=%u notes=%u chmask=0x%x support=0x%lx\n", i, r, c.wMid, c.wPid, c.vDriverVersion, s, c.wTechnology, c.wVoices, c.wNotes, c.wChannelMask, c.dwSupport);
        if (i >= 0 && i < n) instance_id("midiOut", i, mom);
    }
    n = midiInGetNumDevs();
    for (int i = -1; i <= n; i++) {
        MIDIINCAPSW c; memset(&c, 0, sizeof c); MMRESULT r = midiInGetDevCapsW((UINT_PTR)(UINT)i, &c, sizeof c); wfmt(c.szPname, s, sizeof s);
        P("midiInCaps[%d] r=%u mid=%u pid=%u name=\"%s\" support=0x%lx\n", i, r, c.wMid, c.wPid, s, c.dwSupport);
    }
    n = auxGetNumDevs();
    for (int i = -1; i <= n; i++) {
        AUXCAPSW c; memset(&c, 0, sizeof c); MMRESULT r = auxGetDevCapsW((UINT_PTR)(UINT)i, &c, sizeof c); wfmt(c.szPname, s, sizeof s);
        DWORD vol = 0xdeadbeef; MMRESULT rv = auxGetVolume((UINT)i, &vol);
        P("auxCaps[%d] r=%u mid=%u pid=%u name=\"%s\" tech=%u support=0x%lx getvol r=%u vol=0x%lx\n", i, r, c.wMid, c.wPid, s, c.wTechnology, c.dwSupport, rv, vol);
    }
    n = joyGetNumDevs();
    { JOYCAPSW c; memset(&c, 0, sizeof c); MMRESULT r = joyGetDevCapsW(JOYSTICKID1, &c, sizeof c); wfmt(c.szPname, s, sizeof s);
      JOYINFOEX ji; memset(&ji, 0, sizeof ji); ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNALL; MMRESULT rp = joyGetPosEx(JOYSTICKID1, &ji);
      P("joy num=%d caps[0] r=%u name=\"%s\" getposex r=%u\n", n, r, s, rp); }
    n = mixerGetNumDevs();
    for (int i = -1; i <= n; i++) {
        MIXERCAPSW c; memset(&c, 0, sizeof c); MMRESULT r = mixerGetDevCapsW((UINT_PTR)(UINT)i, &c, sizeof c); wfmt(c.szPname, s, sizeof s);
        P("mixerCaps[%d] r=%u mid=%u pid=%u ver=0x%x name=\"%s\" dests=%lu support=0x%lx\n", i, r, c.wMid, c.wPid, c.vDriverVersion, s, c.cDestinations, c.fdwSupport);
        if (r || i < 0 || i >= n) continue;
        for (DWORD d = 0; d < c.cDestinations && d < 8; d++) {
            MIXERLINEW ml; memset(&ml, 0, sizeof ml); ml.cbStruct = sizeof ml; ml.dwDestination = d;
            MMRESULT rl = mixerGetLineInfoW((HMIXEROBJ)(UINT_PTR)i, &ml, MIXER_OBJECTF_MIXER | MIXER_GETLINEINFOF_DESTINATION);
            char nm[128], sn[128]; wfmt(ml.szName, nm, sizeof nm); wfmt(ml.szShortName, sn, sizeof sn);
            P("  mixer[%d] dest %lu r=%u lineid=0x%lx type=0x%lx ch=%lu conns=%lu ctrls=%lu fdwLine=0x%lx name=\"%s\" short=\"%s\" target.type=%lu target.dev=%lu\n",
              i, d, rl, ml.dwLineID, ml.dwComponentType, ml.cChannels, ml.cConnections, ml.cControls, ml.fdwLine, nm, sn, ml.Target.dwType, ml.Target.dwDeviceID);
            if (rl == 0 && ml.cControls) {
                MIXERCONTROLW ctl[16]; MIXERLINECONTROLSW lc; memset(&lc, 0, sizeof lc); lc.cbStruct = sizeof lc; lc.dwLineID = ml.dwLineID;
                lc.cControls = ml.cControls < 16 ? ml.cControls : 16; lc.cbmxctrl = sizeof(MIXERCONTROLW); lc.pamxctrl = ctl;
                if (ml.cControls <= 16 && mixerGetLineControlsW((HMIXEROBJ)(UINT_PTR)i, &lc, MIXER_OBJECTF_MIXER | MIXER_GETLINECONTROLSF_ALL) == 0)
                    for (DWORD k = 0; k < lc.cControls; k++) { wfmt(ctl[k].szName, nm, sizeof nm);
                        P("    ctrl id=%lu type=0x%lx fl=0x%lx items=%lu min=%ld max=%ld steps=%lu name=\"%s\"\n", ctl[k].dwControlID, ctl[k].dwControlType, ctl[k].fdwControl, ctl[k].cMultipleItems, ctl[k].Bounds.lMinimum, ctl[k].Bounds.lMaximum, ctl[k].Metrics.cSteps, nm); }
            }
            for (DWORD sidx = 0; rl == 0 && sidx < ml.cConnections && sidx < 8; sidx++) {
                MIXERLINEW sl; memset(&sl, 0, sizeof sl); sl.cbStruct = sizeof sl; sl.dwDestination = d; sl.dwSource = sidx;
                MMRESULT rs = mixerGetLineInfoW((HMIXEROBJ)(UINT_PTR)i, &sl, MIXER_OBJECTF_MIXER | MIXER_GETLINEINFOF_SOURCE);
                wfmt(sl.szName, nm, sizeof nm);
                P("    src %lu r=%u lineid=0x%lx type=0x%lx ch=%lu ctrls=%lu name=\"%s\"\n", sidx, rs, sl.dwLineID, sl.dwComponentType, sl.cChannels, sl.cControls, nm);
            }
        }
    }
}

/* ---------------- format matrix ---------------- */
static void fmt_matrix(void) {
    static const DWORD rates[] = { 8000, 11025, 22050, 44100, 48000, 96000, 192000 };
    static const WORD bits[] = { 8, 16, 24, 32 }, chs[] = { 1, 2, 6 };
    UINT ids[] = { WAVE_MAPPER, 0 };
    for (int io = 0; io < 2; io++) for (int di = 0; di < 2; di++) {
        if ((io ? waveInGetNumDevs() : waveOutGetNumDevs()) == 0 && di == 1) continue;
        P("fmtq %s dev=%d:", io ? "in" : "out", (int)ids[di]);
        for (int r = 0; r < 7; r++) for (int b = 0; b < 4; b++) for (int c = 0; c < 3; c++) {
            WAVEFORMATEX f = { WAVE_FORMAT_PCM, chs[c], rates[r], 0, 0, bits[b], 0 };
            f.nBlockAlign = f.nChannels * f.wBitsPerSample / 8; f.nAvgBytesPerSec = f.nBlockAlign * f.nSamplesPerSec;
            MMRESULT m = io ? waveInOpen(NULL, ids[di], &f, 0, 0, WAVE_FORMAT_QUERY) : waveOutOpen(NULL, ids[di], &f, 0, 0, WAVE_FORMAT_QUERY);
            P(" %lu/%u/%u=%u", rates[r], bits[b], chs[c], m);
        }
        P("\n");
    }
    /* float32 stereo 48k via WAVE_FORMAT_IEEE_FLOAT */
    WAVEFORMATEX f = { WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 48000 * 8, 8, 32, 0 };
    P("fmtq out mapper float32/2/48000=%u\n", waveOutOpen(NULL, WAVE_MAPPER, &f, 0, 0, WAVE_FORMAT_QUERY));
}

/* ---------------- playback / capture ---------------- */
static void tone(short *dst, int frames, int ch, int rate, double hz, double amp, int fade) {
    for (int i = 0; i < frames; i++) {
        double env = 1.0; if (fade) { int e = rate / 100; if (i < e) env = (double)i / e; if (frames - i < e) env = (double)(frames - i) / e; }
        short v = (short)(sin(2 * M_PI * hz * i / rate) * amp * env * 32767);
        for (int c = 0; c < ch; c++) dst[i * ch + c] = v;
    }
}
static void play_waveout(void) {
    const int rate = 44100, ch = 2, seg = rate * 6 / 10; const double amp = 0.25;
    static short buf[44100 * 2 * 2];
    tone(buf, seg, ch, rate, 440, amp, 1); tone(buf + seg * ch, seg, ch, rate, 1000, amp, 1);
    WAVEFORMATEX f = { WAVE_FORMAT_PCM, ch, rate, rate * ch * 2, ch * 2, 16, 0 };
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL); HWAVEOUT h = NULL;
    ULONGLONG t0 = up(); MMRESULT r = waveOutOpen(&h, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT);
    P("play waveOutOpen(mapper,44100/16/2) r=%u open_ms=%llu\n", r, up() - t0);
    if (r) { CloseHandle(ev); return; }
    WaitForSingleObject(ev, 500); /* WOM_OPEN */
    UINT devid = 0xffff; waveOutGetID(h, &devid); DWORD vol = 0xdeadbeef; MMRESULT rv = waveOutGetVolume(h, &vol);
    P("play handle devid=%u getvolume r=%u vol=0x%08lx\n", devid, rv, vol);
    WAVEHDR hdr; memset(&hdr, 0, sizeof hdr); hdr.lpData = (LPSTR)buf; hdr.dwBufferLength = seg * 2 * ch * 2;
    r = waveOutPrepareHeader(h, &hdr, sizeof hdr); P("play prepare r=%u flags=0x%lx\n", r, hdr.dwFlags);
    t0 = up(); r = waveOutWrite(h, &hdr, sizeof hdr); P("play write r=%u flags=0x%lx\n", r, hdr.dwFlags);
    MMTIME mt; int mid_sampled = 0;
    while (!(hdr.dwFlags & WHDR_DONE) && up() - t0 < 5000) {
        WaitForSingleObject(ev, 100);
        if (!mid_sampled && up() - t0 > 600) { mt.wType = TIME_SAMPLES; waveOutGetPosition(h, &mt, sizeof mt); P("play pos@%llums samples=%lu\n", up() - t0, mt.u.sample); mid_sampled = 1; }
    }
    ULONGLONG el = up() - t0;
    mt.wType = TIME_SAMPLES; MMRESULT rp = waveOutGetPosition(h, &mt, sizeof mt); DWORD smp = mt.u.sample;
    mt.wType = TIME_BYTES; waveOutGetPosition(h, &mt, sizeof mt); DWORD byt = mt.u.cb;
    mt.wType = TIME_MS; waveOutGetPosition(h, &mt, sizeof mt);
    P("play done=%d elapsed_ms=%llu (expect ~1200) pos r=%u samples=%lu bytes=%lu ms_type=%u ms=%lu flags=0x%lx\n", !!(hdr.dwFlags & WHDR_DONE), el, rp, smp, byt, mt.wType, mt.u.ms, hdr.dwFlags);
    waveOutReset(h); r = waveOutUnprepareHeader(h, &hdr, sizeof hdr); MMRESULT rc = waveOutClose(h);
    P("play unprepare r=%u close r=%u\n", r, rc); CloseHandle(ev);
}
static void play_playsound(void) {
    const int rate = 22050, frames = rate * 4 / 10; static BYTE wav[44 + 22050 * 2];
    DWORD data = frames * 2; memcpy(wav, "RIFF", 4); *(DWORD *)(wav + 4) = 36 + data; memcpy(wav + 8, "WAVEfmt ", 8);
    *(DWORD *)(wav + 16) = 16; WAVEFORMATEX f = { WAVE_FORMAT_PCM, 1, rate, rate * 2, 2, 16, 0 }; memcpy(wav + 20, &f, 16);
    memcpy(wav + 36, "data", 4); *(DWORD *)(wav + 40) = data; tone((short *)(wav + 44), frames, 1, rate, 660, 0.25, 1);
    ULONGLONG t0 = up(); BOOL ok = PlaySoundA((LPCSTR)wav, NULL, SND_MEMORY | SND_SYNC | SND_NODEFAULT);
    P("playsound memory sync ok=%d gle=%lu elapsed_ms=%llu (expect ~400)\n", ok, ok ? 0 : GetLastError(), up() - t0);
}
static void play_midi(void) {
    HMIDIOUT h = NULL; ULONGLONG t0 = up(); MMRESULT r = midiOutOpen(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
    P("midi open(mapper) r=%u open_ms=%llu\n", r, up() - t0);
    if (r) return;
    UINT id = 0xffff; midiOutGetID(h, &id);
    Sleep(200);
    MMRESULT a = midiOutShortMsg(h, 0x00405190); /* note on ch1, key 81, vel 64 */
    Sleep(400);
    MMRESULT b = midiOutShortMsg(h, 0x00005180);
    Sleep(150);
    MMRESULT rr = midiOutReset(h), rc = midiOutClose(h);
    P("midi id=%u on r=%u off r=%u reset r=%u close r=%u\n", id, a, b, rr, rc);
}
static void capture(void) {
    const int rate = 44100; static short buf[44100 / 2];
    WAVEFORMATEX f = { WAVE_FORMAT_PCM, 1, rate, rate * 2, 2, 16, 0 };
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL); HWAVEIN h = NULL;
    MMRESULT r = waveInOpen(&h, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT);
    P("cap waveInOpen(mapper,44100/16/1) r=%u\n", r); if (r) { CloseHandle(ev); return; }
    WAVEHDR hdr; memset(&hdr, 0, sizeof hdr); hdr.lpData = (LPSTR)buf; hdr.dwBufferLength = sizeof buf;
    waveInPrepareHeader(h, &hdr, sizeof hdr); r = waveInAddBuffer(h, &hdr, sizeof hdr);
    ULONGLONG t0 = up(); MMRESULT rs = waveInStart(h);
    while (!(hdr.dwFlags & WHDR_DONE) && up() - t0 < 3000) WaitForSingleObject(ev, 100);
    ULONGLONG el = up() - t0; int peak = 0; double ss = 0; DWORD n = hdr.dwBytesRecorded / 2;
    for (DWORD i = 0; i < n; i++) { int v = abs(buf[i]); if (v > peak) peak = v; ss += (double)buf[i] * buf[i]; }
    P("cap add r=%u start r=%u done=%d elapsed_ms=%llu (expect ~500) recorded=%lu peak=%d rms=%.1f\n", r, rs, !!(hdr.dwFlags & WHDR_DONE), el, hdr.dwBytesRecorded, peak, n ? sqrt(ss / n) : 0.0);
    waveInReset(h); waveInUnprepareHeader(h, &hdr, sizeof hdr); P("cap close r=%u\n", waveInClose(h)); CloseHandle(ev);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    CreateThread(NULL, 0, watchdog, (void *)(UINT_PTR)240000, 0, NULL);
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&g_en);
    const char *mode = argc > 1 ? argv[1] : "full";
    P("audprobe %s uptime_ms=%llu enumerator hr=0x%08lx\n", mode, up(), hr);
    if (!strcmp(mode, "poll")) {
        int secs = argc > 2 ? atoi(argv[2]) : 60; ULONGLONG end = up() + secs * 1000ULL, stable_since = 0;
        counts_t prev = { -9 }, c;
        for (;;) {
            get_counts(&c);
            if (memcmp(&c, &prev, sizeof c)) { print_counts("poll", &c); prev = c; stable_since = up(); }
            int ready = c.wo > 0 && c.epr > 0;
            if (ready && up() - stable_since > 5000) { P("poll ready-stable\n"); break; }
            if (up() > end) { P("poll deadline\n"); break; }
            Sleep(500);
        }
        print_counts("final", &c);
    } else {
        counts_t c; get_counts(&c); print_counts("counts", &c);
        services(); ep_dump(); caps_all(); fmt_matrix();
        if (argc > 2 && !strcmp(argv[2], "noplay")) P("playback skipped\n");
        else {
            if (waveOutGetNumDevs()) { Sleep(300); play_waveout(); Sleep(300); play_playsound(); Sleep(300); }
            else { P("no waveOut devices: probing mapper/out-of-range contract\n");
                   WAVEFORMATEX f = { WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0 }; HWAVEOUT h = (HWAVEOUT)1;
                   P("nodev waveOutOpen(mapper) r=%u h=%p\n", waveOutOpen(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL), h);
                   h = (HWAVEOUT)1; P("nodev waveOutOpen(0) r=%u h=%p\n", waveOutOpen(&h, 0, &f, 0, 0, CALLBACK_NULL), h);
                   P("nodev PlaySound(memory) "); play_playsound(); }
            if (midiOutGetNumDevs()) { play_midi(); Sleep(300); }
            else { HMIDIOUT h = NULL; P("nodev midiOutOpen(mapper) r=%u\n", midiOutOpen(&h, MIDI_MAPPER, 0, 0, 0)); }
            if (waveInGetNumDevs()) capture();
            else { HWAVEIN h = NULL; WAVEFORMATEX f = { WAVE_FORMAT_PCM, 1, 44100, 88200, 2, 16, 0 }; P("nodev waveInOpen(mapper) r=%u\n", waveInOpen(&h, WAVE_MAPPER, &f, 0, 0, 0)); }
        }
        get_counts(&c); print_counts("counts-end", &c);
    }
    if (g_en) IMMDeviceEnumerator_Release(g_en);
    CoUninitialize();
    P("audprobe end uptime_ms=%llu\n", up());
    return 0;
}
