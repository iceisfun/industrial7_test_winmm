/* vmrun.c - guest-side runner for the Win10 VM (docs/VM_RUN.md). Built to build/tools/vmrun.exe by `make tools`.
 *
 *   vmrun.exe [-t SECONDS] [-o OUT] [-n NAME] -- EXE [ARGS...]
 *       Runs EXE in a kill-on-close job object, stdout+stderr on one pipe, and forwards everything to OUT (default
 *       \\.\COM1; this process is the only COM1 writer while the exe runs):
 *           @@I7 BEGIN <name> <cmdline>
 *           ...exe output verbatim...
 *           @@I7 END <name> exit=0x<code> ms=<elapsed> timeout=<0|1> bytes=<n>
 *       On timeout the whole job is terminated and exit=0xDEAD0001; spawn failure exit=0xDEAD0000.
 *
 *   vmrun.exe --audio-ready [SECONDS] [--want-render N] [-o OUT]
 *       Makes the audio stack ready before the tests: AudioEndpointBuilder and AudioSrv must be RUNNING (started when
 *       STOPPED), then the winmm and mmdevapi device counts are polled until they have been unchanged for 3 s and the
 *       active render endpoint count is >= N (default 0). Prints
 *           @@I7 AUDIO ready=<0|1> ms=<n> aeb=<state> audiosrv=<state> started=<mask> waveout=<n> wavein=<n>
 *                 midiout=<n> midiin=<n> mixer=<n> aux=<n> render=<n> capture=<n> polls=<n>
 *       and one '@@I7 AUDIODEV <kind> <index> <name>' line per winmm device (names in UTF-8).
 *       Exit code 0 when ready, 1 otherwise. This process links winmm itself; it is a tool, not a test.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

static HANDLE g_out; static volatile LONG g_bytes;
static void out_write(const void *b, DWORD n) { DWORD w; while (n) { if (!WriteFile(g_out, b, n, &w, NULL) || !w) return; b = (const char *)b + w; n -= w; } }
static void out_printf(const char *fmt, ...) {
    char t[2048]; va_list a; va_start(a, fmt); int n = vsnprintf(t, sizeof t, fmt, a); va_end(a);
    if (n > 0) out_write(t, n < (int)sizeof t ? (DWORD)n : (DWORD)sizeof t - 1);
}
static HANDLE open_out(const char *path) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, strncmp(path, "\\\\.\\", 4) ? CREATE_ALWAYS : OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) h = GetStdHandle(STD_OUTPUT_HANDLE);
    return h;
}

static DWORD WINAPI pump(LPVOID p) {
    HANDLE r = p; char buf[16384]; DWORD n;
    while (ReadFile(r, buf, sizeof buf, &n, NULL) && n) { out_write(buf, n); InterlockedAdd(&g_bytes, (LONG)n); }
    return 0;
}

/* ---- audio readiness ---------------------------------------------------- */
static DWORD svc_state(SC_HANDLE scm, const char *name, int start, int *started) {
    SC_HANDLE s = OpenServiceA(scm, name, SERVICE_QUERY_STATUS | (start ? SERVICE_START : 0));
    SERVICE_STATUS st; DWORD r = 0;
    if (!s) return 0;
    if (QueryServiceStatus(s, &st)) r = st.dwCurrentState;
    if (start && r == SERVICE_STOPPED) { if (StartServiceA(s, 0, NULL)) *started = 1; if (QueryServiceStatus(s, &st)) r = st.dwCurrentState; }
    CloseServiceHandle(s);
    return r;
}

static const CLSID my_CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID my_IID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static int endpoint_count(EDataFlow flow) {
    IMMDeviceEnumerator *e = NULL; IMMDeviceCollection *c = NULL; UINT n = 0;
    if (FAILED(CoCreateInstance(&my_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &my_IID_IMMDeviceEnumerator, (void **)&e)) || !e) return -1;
    if (SUCCEEDED(IMMDeviceEnumerator_EnumAudioEndpoints(e, flow, DEVICE_STATE_ACTIVE, &c)) && c) { IMMDeviceCollection_GetCount(c, &n); IMMDeviceCollection_Release(c); }
    else n = (UINT)-1;
    IMMDeviceEnumerator_Release(e);
    return (int)n;
}

typedef struct { int wo, wi, mo, mi, mx, ax, ren, cap; } counts;
static void get_counts(counts *c) {
    c->wo = waveOutGetNumDevs(); c->wi = waveInGetNumDevs(); c->mo = midiOutGetNumDevs(); c->mi = midiInGetNumDevs();
    c->mx = mixerGetNumDevs(); c->ax = auxGetNumDevs(); c->ren = endpoint_count(eRender); c->cap = endpoint_count(eCapture);
}
static void print_name(const char *kind, UINT i, const WCHAR *w) {
    char u[256]; if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL)) strcpy(u, "?");
    out_printf("@@I7 AUDIODEV %s %u %s\r\n", kind, i, u);
}
static void list_devices(const counts *c) {
    for (int i = 0; i < c->wo; i++) { WAVEOUTCAPSW k; if (!waveOutGetDevCapsW(i, &k, sizeof k)) print_name("waveout", i, k.szPname); }
    for (int i = 0; i < c->wi; i++) { WAVEINCAPSW k; if (!waveInGetDevCapsW(i, &k, sizeof k)) print_name("wavein", i, k.szPname); }
    for (int i = 0; i < c->mo; i++) { MIDIOUTCAPSW k; if (!midiOutGetDevCapsW(i, &k, sizeof k)) print_name("midiout", i, k.szPname); }
    for (int i = 0; i < c->mi; i++) { MIDIINCAPSW k; if (!midiInGetDevCapsW(i, &k, sizeof k)) print_name("midiin", i, k.szPname); }
    for (int i = 0; i < c->mx; i++) { MIXERCAPSW k; if (!mixerGetDevCapsW(i, &k, sizeof k)) print_name("mixer", i, k.szPname); }
    for (int i = 0; i < c->ax; i++) { AUXCAPSW k; if (!auxGetDevCapsW(i, &k, sizeof k)) print_name("aux", i, k.szPname); }
}

static int audio_ready(DWORD secs, int want_render) {
    DWORD t0 = GetTickCount(), lim = secs * 1000, aeb = 0, as = 0; int started = 0, s1 = 0, s2 = 0, polls = 0, ready = 0;
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm) {
        for (;;) {   /* services first: winmm itself waits for AudioSrv on its first call (up to ~2 min) */
            aeb = svc_state(scm, "AudioEndpointBuilder", 1, &s1);
            as = svc_state(scm, "AudioSrv", aeb == SERVICE_RUNNING, &s2);
            if ((aeb == SERVICE_RUNNING && as == SERVICE_RUNNING) || GetTickCount() - t0 > lim) break;
            Sleep(500);
        }
        CloseServiceHandle(scm);
    }
    started = s1 | (s2 << 1);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    counts c, prev; DWORD stable_since = 0; int have_prev = 0;
    for (;;) {
        get_counts(&c); polls++;
        DWORD now = GetTickCount();
        if (!have_prev || memcmp(&c, &prev, sizeof c)) { prev = c; have_prev = 1; stable_since = now; }
        if (now - stable_since >= 3000 && c.ren >= want_render) { ready = 1; break; }
        if (now - t0 > lim) break;
        Sleep(500);
    }
    out_printf("@@I7 AUDIO ready=%d ms=%lu aeb=%lu audiosrv=%lu started=%d waveout=%d wavein=%d midiout=%d midiin=%d mixer=%d aux=%d render=%d capture=%d polls=%d\r\n",
               ready, GetTickCount() - t0, aeb, as, started, c.wo, c.wi, c.mo, c.mi, c.mx, c.ax, c.ren, c.cap, polls);
    list_devices(&c);
    CoUninitialize();
    return ready ? 0 : 1;
}

int main(int argc, char **argv) {
    DWORD secs = 600; const char *out = "\\\\.\\COM1", *name = NULL; int i = 1, audio = 0, want = 0;
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "-t") && i + 1 < argc) secs = (DWORD)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--want-render") && i + 1 < argc) want = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--audio-ready")) { audio = 1; secs = 90; if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') secs = (DWORD)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--")) { i++; break; }
        else break;
    }
    g_out = open_out(out);
    if (audio) { int r = audio_ready(secs, want); CloseHandle(g_out); return r; }
    if (i >= argc) { fprintf(stderr, "usage: vmrun [-t s] [-o out] [-n name] -- exe args | vmrun --audio-ready [s] [--want-render n]\n"); return 2; }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);   /* inherited by the child */

    char cmd[8192]; size_t o = 0; cmd[0] = 0;
    for (int k = i; k < argc && o < sizeof cmd - 4; k++) {
        int q = strpbrk(argv[k], " \t") != NULL || !argv[k][0];
        o += snprintf(cmd + o, sizeof cmd - o, "%s%s%s%s", k > i ? " " : "", q ? "\"" : "", argv[k], q ? "\"" : "");
    }
    if (!name) name = argv[i];
    out_printf("@@I7 BEGIN %s %s\r\n", name, cmd);

    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE }; HANDLE rd, wr;
    CreatePipe(&rd, &wr, &sa, 0); SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE job = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li; memset(&li, 0, sizeof li);
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    STARTUPINFOA si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); si.hStdOutput = wr; si.hStdError = wr;
    DWORD t0 = GetTickCount(), code = 0xDEAD0000; int tmo = 0;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        CloseHandle(wr);
        out_printf("vmrun: CreateProcess failed %lu\r\n", GetLastError());
    } else {
        AssignProcessToJobObject(job, pi.hProcess);
        ResumeThread(pi.hThread); CloseHandle(pi.hThread);
        CloseHandle(wr);
        HANDLE th = CreateThread(NULL, 0, pump, rd, 0, NULL);
        if (WaitForSingleObject(pi.hProcess, secs * 1000) != WAIT_OBJECT_0) {
            tmo = 1; TerminateJobObject(job, 0xDEAD0001); WaitForSingleObject(pi.hProcess, 10000); code = 0xDEAD0001;
        } else GetExitCodeProcess(pi.hProcess, &code);
        /* grandchildren that inherited the pipe keep it open: give the pump a moment, then kill the job */
        if (WaitForSingleObject(th, 3000) != WAIT_OBJECT_0) { TerminateJobObject(job, 0xDEAD0002); WaitForSingleObject(th, 3000); }
        CloseHandle(th); CloseHandle(pi.hProcess);
    }
    out_printf("\r\n@@I7 END %s exit=0x%lx ms=%lu timeout=%d bytes=%ld\r\n", name, code, GetTickCount() - t0, tmo, g_bytes);
    CloseHandle(job); CloseHandle(rd); FlushFileBuffers(g_out); CloseHandle(g_out);
    return (int)code;
}
