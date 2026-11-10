#include "wf.h"
/* The open layer of WAVE_FORMAT_QUERY (winmmbase waveOutOpen !0x103b0 / waveInOpen !0x11290), which runs before any
 * driver sees the format and therefore holds with and without audio devices:
 *   1. pwfx NULL, or CALLBACK_WINDOW with a non-NULL dwCallback that is not a window -> 11
 *   2. 0xFFFF -> WAVE_MAPPER; flags & (mapper ? 0x7E04 : 0x7E10) -> 10
 *   3. QUERY: phwo is neither checked nor written (no handle is created; the driver gets hWave NULL)
 *   4. zero devices in that direction -> 2; id not resolvable -> 2
 * VM_TIMEOUT: 240 */

static void pcm16(wf_fmt *f) { wf_pcm(f, WAVE_FORMAT_PCM_, 2, 44100, 16); }

/* The core format every endpoint takes: 0 when the device exists, 2 otherwise. */
static int core_exp(int dir, UINT id) { int g = wf_pre_driver(dir, id); return g >= 0 ? g : 0; }

static MMRESULT q_raw(int dir, void *ph, UINT id, const void *fmt, DWORD_PTR cb, DWORD flags) {
    if (dir == WF_OUT) return U(waveOutOpen)((LPHWAVEOUT)ph, id, (LPCWAVEFORMATEX)fmt, cb, 0, flags);
    return U(waveInOpen)((LPHWAVEIN)ph, id, (LPCWAVEFORMATEX)fmt, cb, 0, flags);
}

#define REQ_OPEN() do { REQUIRE(U(waveOutOpen)); REQUIRE(U(waveInOpen)); REQUIRE(U(waveOutGetNumDevs)); \
    REQUIRE(U(waveInGetNumDevs)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveInClose)); } while (0)

/* ---- device gating record ------------------------------------------------------------------------------------- */

TEST(wf_gate_device_record) {
    REQ_OPEN(); REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(waveInGetDevCapsW));
    UINT no = wf_ndev(WF_OUT), ni = wf_ndev(WF_IN);
    t_note("WFENV waveout=%u wavein=%u", no, ni);
    for (UINT i = 0; i < no; i++) {
        WAVEOUTCAPSW c; memset(&c, 0, sizeof c);
        EXPECT_EQ(U(waveOutGetDevCapsW)(i, &c, sizeof c), MMSYSERR_NOERROR);
        t_note("waveOut %u \"%ls\" dwFormats 0x%lx wChannels %u", i, c.szPname, c.dwFormats, c.wChannels);
    }
    for (UINT i = 0; i < ni; i++) {
        WAVEINCAPSW c; memset(&c, 0, sizeof c);
        EXPECT_EQ(U(waveInGetDevCapsW)(i, &c, sizeof c), MMSYSERR_NOERROR);
        t_note("waveIn %u \"%ls\" dwFormats 0x%lx wChannels %u", i, c.szPname, c.dwFormats, c.wChannels);
    }
    WAVEOUTCAPSW oc; WAVEINCAPSW ic;
    EXPECT_EQ(U(waveOutGetDevCapsW)(no, &oc, sizeof oc), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInGetDevCapsW)(ni, &ic, sizeof ic), MMSYSERR_BADDEVICEID);
    /* The mapper is loaded when waveOut + waveIn != 0, so its caps answer for BOTH directions as soon as either
     * direction has a device (render-only guest: waveIn mapper caps 0 with 0 waveIn devices) [VM]. */
    MMRESULT mo = U(waveOutGetDevCapsW)(WAVE_MAPPER, &oc, sizeof oc), mi = U(waveInGetDevCapsW)(WAVE_MAPPER, &ic, sizeof ic);
    if (no + ni == 0) { EXPECT_EQ(mo, MMSYSERR_BADDEVICEID); EXPECT_EQ(mi, MMSYSERR_BADDEVICEID); }
    else { EXPECT_EQ(mo, MMSYSERR_NOERROR); EXPECT_EQ(mi, MMSYSERR_NOERROR); }
}

/* Render-only guest: waveIn mapper caps succeed but every waveIn mapper query is 2 (the count gate). */
TEST(wf_gate_wavein_mapper_caps_vs_query) {
    REQ_OPEN(); REQUIRE(U(waveInGetDevCapsW));
    wf_fmt f; pcm16(&f);
    WAVEINCAPSW ic;
    MMRESULT caps = U(waveInGetDevCapsW)(WAVE_MAPPER, &ic, sizeof ic);
    MMRESULT q = wf_query(WF_IN, WAVE_MAPPER, &f, 0);
    if (wf_ndev(WF_IN) == 0) {
        EXPECT_EQ(q, MMSYSERR_BADDEVICEID);
        EXPECT_EQ(caps, wf_ndev(WF_OUT) ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID);
        if (wf_ndev(WF_OUT)) t_note("render-only guest: waveIn mapper caps %u, mapper query %u", caps, q);
    } else {
        EXPECT_EQ(caps, MMSYSERR_NOERROR);
        EXPECT_EQ(q, MMSYSERR_NOERROR);
    }
}

TEST(wf_gate_counts_stable) {
    REQ_OPEN();
    /* Re-reading the count (cached by wf_ndev) gives the same number: the gate is stable within the process. */
    EXPECT_EQ(U(waveOutGetNumDevs)(), wf_ndev(WF_OUT));
    EXPECT_EQ(U(waveInGetNumDevs)(), wf_ndev(WF_IN));
}

/* ---- pwfx NULL -> 11 before anything else ------------------------------------------------------------------- */

static void null_fmt(int dir) {
    static const UINT ids[] = { WAVE_MAPPER, 0xFFFF, 0, 1, 9999 };
    static const DWORD fl[] = { 0, WAVE_FORMAT_DIRECT, WAVE_ALLOWSYNC, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE };
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++)
        for (unsigned j = 0; j < sizeof fl / sizeof *fl; j++) {
            if (fl[j] == WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE && ids[i] != WAVE_MAPPER && ids[i] != 0xFFFF) continue;
            MMRESULT r = wf_query(dir, ids[i], NULL, fl[j]);
            CHECKF(r == MMSYSERR_INVALPARAM, "%s id %d flags 0x%lx: got %u want 11", wf_dirname(dir), (int)ids[i], fl[j], r);
        }
}
TEST(wf_out_query_null_format) { REQ_OPEN(); null_fmt(WF_OUT); }
TEST(wf_in_query_null_format)  { REQ_OPEN(); null_fmt(WF_IN); }

/* pwfx NULL wins over bad flags (step 1 before step 2). */
static void null_fmt_badflags(int dir) {
    EXPECT_EQ(wf_query(dir, WAVE_MAPPER, NULL, WAVE_MAPPED), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wf_query(dir, 0, NULL, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wf_query(dir, 0, NULL, 0x4000), MMSYSERR_INVALPARAM);
}
TEST(wf_out_query_null_format_beats_flags) { REQ_OPEN(); null_fmt_badflags(WF_OUT); }
TEST(wf_in_query_null_format_beats_flags)  { REQ_OPEN(); null_fmt_badflags(WF_IN); }

/* Without QUERY, pwfx NULL is still 11 and the handle is not written (the zeroing comes after step 2). */
static void null_fmt_open(int dir) {
    ULONG_PTR h = 0x1234;
    EXPECT_EQ(q_raw(dir, &h, WAVE_MAPPER, NULL, 0, CALLBACK_NULL), MMSYSERR_INVALPARAM);
    EXPECT_EQ(h, 0x1234);
    EXPECT_EQ(q_raw(dir, NULL, WAVE_MAPPER, NULL, 0, CALLBACK_NULL), MMSYSERR_INVALPARAM);
}
TEST(wf_out_open_null_format) { REQ_OPEN(); null_fmt_open(WF_OUT); }
TEST(wf_in_open_null_format)  { REQ_OPEN(); null_fmt_open(WF_IN); }

/* ---- QUERY ignores phwo ------------------------------------------------------------------------------------- */

static void null_phwo(int dir) {
    wf_fmt f; pcm16(&f);
    EXPECT_EQ(q_raw(dir, NULL, WAVE_MAPPER, &f, 0, WAVE_FORMAT_QUERY), core_exp(dir, WAVE_MAPPER));
    EXPECT_EQ(q_raw(dir, NULL, 0, &f, 0, WAVE_FORMAT_QUERY), core_exp(dir, 0));
    EXPECT_EQ(q_raw(dir, NULL, 0xFFFF, &f, 0, WAVE_FORMAT_QUERY), core_exp(dir, WAVE_MAPPER));
    EXPECT_EQ(q_raw(dir, NULL, wf_ndev(dir), &f, 0, WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    /* Without QUERY phwo NULL is 11, before the device check. */
    EXPECT_EQ(q_raw(dir, NULL, WAVE_MAPPER, &f, 0, CALLBACK_NULL), MMSYSERR_INVALPARAM);
    EXPECT_EQ(q_raw(dir, NULL, wf_ndev(dir), &f, 0, CALLBACK_NULL), MMSYSERR_INVALPARAM);
}
TEST(wf_out_query_null_phwo) { REQ_OPEN(); null_phwo(WF_OUT); }
TEST(wf_in_query_null_phwo)  { REQ_OPEN(); null_phwo(WF_IN); }

/* The handle slot keeps its value through every kind of query result: success, 2 and 32. */
static void slot_untouched(int dir) {
    wf_fmt f; pcm16(&f);
    wf_query(dir, WAVE_MAPPER, &f, 0);              /* wf_query checks the slot */
    wf_query(dir, 0, &f, 0);
    wf_query(dir, wf_ndev(dir), &f, 0);
    wf_pcm(&f, WAVE_FORMAT_PCM_, 6, 48000, 16);
    wf_query(dir, WAVE_MAPPER, &f, 0);
    wf_query(dir, 0, &f, WAVE_FORMAT_DIRECT);
    /* A failed real open zeroes it (step 3 of the non-query path). */
    ULONG_PTR h = 0x1234;
    wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 16);
    EXPECT_EQ(q_raw(dir, &h, wf_ndev(dir), &f, 0, CALLBACK_NULL), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(h, 0);
}
TEST(wf_out_query_slot_untouched) { REQ_OPEN(); slot_untouched(WF_OUT); }
TEST(wf_in_query_slot_untouched)  { REQ_OPEN(); slot_untouched(WF_IN); }

/* ---- flag masks with QUERY ----------------------------------------------------------------------------------- */

static void bad_flags(int dir) {
    wf_fmt f; pcm16(&f);
    static const DWORD bits[] = { 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x7E00 };
    for (unsigned i = 0; i < sizeof bits / sizeof *bits; i++) {
        CHECKF(wf_query(dir, WAVE_MAPPER, &f, bits[i]) == MMSYSERR_INVALFLAG, "%s mapper bit 0x%lx", wf_dirname(dir), bits[i]);
        CHECKF(wf_query(dir, 0xFFFF, &f, bits[i]) == MMSYSERR_INVALFLAG, "%s 0xFFFF bit 0x%lx", wf_dirname(dir), bits[i]);
        CHECKF(wf_query(dir, 0, &f, bits[i]) == MMSYSERR_INVALFLAG, "%s id0 bit 0x%lx", wf_dirname(dir), bits[i]);
        CHECKF(wf_query(dir, 9999, &f, bits[i]) == MMSYSERR_INVALFLAG, "%s id9999 bit 0x%lx", wf_dirname(dir), bits[i]);
        CHECKF(wf_query(dir, 0, &f, bits[i] | WAVE_FORMAT_DIRECT) == MMSYSERR_INVALFLAG, "%s id0 DIRECT|0x%lx", wf_dirname(dir), bits[i]);
    }
    /* WAVE_MAPPED (4) is invalid with the mapper (and its 0xFFFF alias); 0x10 is invalid with a specific id. */
    EXPECT_EQ(wf_query(dir, WAVE_MAPPER, &f, WAVE_MAPPED), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wf_query(dir, 0xFFFF, &f, WAVE_MAPPED), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wf_query(dir, WAVE_MAPPER, &f, WAVE_MAPPED | WAVE_FORMAT_DIRECT), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wf_query(dir, 0, &f, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wf_query(dir, 9999, &f, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wf_query(dir, 0, &f, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | WAVE_FORMAT_DIRECT), MMSYSERR_INVALFLAG);
}
TEST(wf_out_query_bad_flags) { REQ_OPEN(); bad_flags(WF_OUT); }
TEST(wf_in_query_bad_flags)  { REQ_OPEN(); bad_flags(WF_IN); }

/* The flag check precedes the format: an unsupportable format with a bad flag is 10, not 32 or 2. */
static void bad_flags_beat_format(int dir) {
    wf_fmt f; wf_pcm(&f, 0x1234, 6, 1, 7);
    EXPECT_EQ(wf_query(dir, WAVE_MAPPER, &f, WAVE_MAPPED), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wf_query(dir, 0, &f, 0x1000), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wf_query(dir, wf_ndev(dir) + 5, &f, 0x200), MMSYSERR_INVALFLAG);
}
TEST(wf_out_query_bad_flags_beat_format) { REQ_OPEN(); bad_flags_beat_format(WF_OUT); }
TEST(wf_in_query_bad_flags_beat_format)  { REQ_OPEN(); bad_flags_beat_format(WF_IN); }

/* Flags that pass the open layer: the result is the plain query result (core PCM: 0 / 2). */
static void ok_flags(int dir) {
    wf_fmt f; pcm16(&f);
    static const DWORD fl[] = { WAVE_ALLOWSYNC, WAVE_FORMAT_DIRECT, WAVE_ALLOWSYNC | WAVE_FORMAT_DIRECT };
    for (unsigned i = 0; i < sizeof fl / sizeof *fl; i++) {
        CHECKF(wf_query(dir, WAVE_MAPPER, &f, fl[i]) == (MMRESULT)core_exp(dir, WAVE_MAPPER), "%s mapper 0x%lx", wf_dirname(dir), fl[i]);
        CHECKF(wf_query(dir, 0, &f, fl[i]) == (MMRESULT)core_exp(dir, 0), "%s id0 0x%lx", wf_dirname(dir), fl[i]);
        CHECKF(wf_query(dir, wf_ndev(dir), &f, fl[i]) == MMSYSERR_BADDEVICEID, "%s id n 0x%lx", wf_dirname(dir), fl[i]);
    }
    /* 0x10 (default communication device) is allowed with the mapper only. */
    EXPECT_EQ(wf_query(dir, WAVE_MAPPER, &f, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), core_exp(dir, WAVE_MAPPER));
    EXPECT_EQ(wf_query(dir, 0xFFFF, &f, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), core_exp(dir, WAVE_MAPPER));
}
TEST(wf_out_query_passing_flags) { REQ_OPEN(); ok_flags(WF_OUT); }
TEST(wf_in_query_passing_flags)  { REQ_OPEN(); ok_flags(WF_IN); }

/* Bits 0x20..0x100 and 0x8000 are outside both masks: no device -> 2; with a device the driver sees them. */
static void undefined_bits(int dir) {
    wf_fmt f; pcm16(&f);
    static const DWORD bits[] = { 0x20, 0x40, 0x80, 0x100, 0x8000 };
    char lab[64];
    for (unsigned i = 0; i < sizeof bits / sizeof *bits; i++) {
        sprintf(lab, "pcm16/2/44100 flag0x%lx", bits[i]);
        wf_cell(lab, dir, WAVE_MAPPER, &f, bits[i], 0);
        wf_cell(lab, dir, 0, &f, bits[i], 0);
        wf_cell(lab, dir, wf_ndev(dir), &f, bits[i], 0);
    }
}
TEST(wf_out_query_undefined_bits) { REQ_OPEN(); undefined_bits(WF_OUT); }
TEST(wf_in_query_undefined_bits)  { REQ_OPEN(); undefined_bits(WF_IN); }

/* ---- callback field on the query path ------------------------------------------------------------------------ */

static HWND dead_window(void) {
    HWND w = CreateWindowExA(0, "STATIC", "wf", 0, 0, 0, 1, 1, NULL, NULL, NULL, NULL);
    if (w) DestroyWindow(w);
    return w;
}

static void callback_window(int dir) {
    wf_fmt f; pcm16(&f);
    HWND dead = dead_window();
    if (!dead) { t_skip("no window"); return; }
    /* A non-window with CALLBACK_WINDOW is 11 even for a query, and before flags and the device count. */
    EXPECT_EQ(q_raw(dir, NULL, WAVE_MAPPER, &f, (DWORD_PTR)dead, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
    EXPECT_EQ(q_raw(dir, NULL, 0, &f, (DWORD_PTR)dead, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
    EXPECT_EQ(q_raw(dir, NULL, 9999, &f, (DWORD_PTR)dead, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
    EXPECT_EQ(q_raw(dir, NULL, WAVE_MAPPER, &f, (DWORD_PTR)dead, CALLBACK_WINDOW | WAVE_FORMAT_QUERY | WAVE_MAPPED), MMSYSERR_INVALPARAM);
    /* NULL window: accepted (no callback). */
    EXPECT_EQ(q_raw(dir, NULL, WAVE_MAPPER, &f, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), core_exp(dir, WAVE_MAPPER));
    EXPECT_EQ(q_raw(dir, NULL, 0, &f, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), core_exp(dir, 0));
    /* The same handle value under another callback type is not validated by winmm. */
    EXPECT_EQ(q_raw(dir, NULL, WAVE_MAPPER, &f, (DWORD_PTR)dead, CALLBACK_THREAD | WAVE_FORMAT_QUERY), core_exp(dir, WAVE_MAPPER));
    EXPECT_EQ(q_raw(dir, NULL, 0, &f, (DWORD_PTR)dead, CALLBACK_EVENT | WAVE_FORMAT_QUERY), core_exp(dir, 0));
}
TEST(wf_out_query_callback_window) { REQ_OPEN(); callback_window(WF_OUT); }
TEST(wf_in_query_callback_window)  { REQ_OPEN(); callback_window(WF_IN); }

/* A live window, a NULL function, an event and the undefined type 7: the query path never uses the callback. */
static void callback_types(int dir) {
    wf_fmt f; pcm16(&f);
    HWND w = CreateWindowExA(0, "STATIC", "wf", 0, 0, 0, 1, 1, NULL, NULL, NULL, NULL);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    struct { DWORD_PTR cb; DWORD fl; const char *n; } c[] = {
        { (DWORD_PTR)w, CALLBACK_WINDOW, "window" }, { 0, CALLBACK_FUNCTION, "function NULL" },
        { (DWORD_PTR)ev, CALLBACK_EVENT, "event" }, { 0, CALLBACK_EVENT, "event NULL" },
        { (DWORD_PTR)GetCurrentThreadId(), CALLBACK_THREAD, "thread" }, { 0, 0x70000, "type 7" },
        { 0, 0x40000, "type 4" }, { 0, 0x60000, "type 6" },
    };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        if (c[i].fl == CALLBACK_WINDOW && !w) continue;
        MMRESULT a = q_raw(dir, NULL, WAVE_MAPPER, &f, c[i].cb, c[i].fl | WAVE_FORMAT_QUERY);
        MMRESULT b = q_raw(dir, NULL, 0, &f, c[i].cb, c[i].fl | WAVE_FORMAT_QUERY);
        CHECKF(a == (MMRESULT)core_exp(dir, WAVE_MAPPER), "%s mapper %s: got %u", wf_dirname(dir), c[i].n, a);
        CHECKF(b == (MMRESULT)core_exp(dir, 0), "%s id0 %s: got %u", wf_dirname(dir), c[i].n, b);
    }
    if (w) DestroyWindow(w);
    CloseHandle(ev);
}
TEST(wf_out_query_callback_types) { REQ_OPEN(); callback_types(WF_OUT); }
TEST(wf_in_query_callback_types)  { REQ_OPEN(); callback_types(WF_IN); }

/* ---- device id resolution ------------------------------------------------------------------------------------ */

/* Every id from n upward (and large ones) is 2, whatever the format, flags permitting. */
static void out_of_range(int dir) {
    UINT n = wf_ndev(dir);
    const UINT ids[] = { n, n + 1, n + 7, 0x7FFF, 0xFFFE, 0x10000, 0x7FFFFFFF, 0xFFFFFFFE };
    wf_fmt fs[4];
    wf_pcm(&fs[0], WAVE_FORMAT_PCM_, 2, 44100, 16);
    wf_pcm(&fs[1], WAVE_FORMAT_PCM_, 6, 48000, 16);
    wf_pcm(&fs[2], 0x1234, 2, 44100, 16);
    wf_pcm(&fs[3], WAVE_FORMAT_PCM_, 0, 0, 0);
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++)
        for (int k = 0; k < 4; k++) {
            CHECKF(wf_query(dir, ids[i], &fs[k], 0) == MMSYSERR_BADDEVICEID, "%s id 0x%x fmt %d", wf_dirname(dir), ids[i], k);
            CHECKF(wf_query(dir, ids[i], &fs[k], WAVE_FORMAT_DIRECT) == MMSYSERR_BADDEVICEID, "%s id 0x%x fmt %d DIRECT", wf_dirname(dir), ids[i], k);
        }
}
TEST(wf_out_query_id_out_of_range) { REQ_OPEN(); out_of_range(WF_OUT); }
TEST(wf_in_query_id_out_of_range)  { REQ_OPEN(); out_of_range(WF_IN); }

/* 0xFFFF is the 16-bit WAVE_MAPPER: identical verdicts on a few representative formats. */
static void alias_ffff(int dir) {
    wf_fmt fs[5];
    wf_pcm(&fs[0], WAVE_FORMAT_PCM_, 2, 44100, 16);
    wf_pcm(&fs[1], WAVE_FORMAT_PCM_, 1, 8000, 8);
    wf_pcm(&fs[2], WAVE_FORMAT_PCM_, 6, 48000, 16);
    wf_pcm(&fs[3], WAVE_FORMAT_IEEE_FLOAT_, 2, 48000, 32);
    wf_pcm(&fs[4], 0x1234, 2, 44100, 16);
    for (int k = 0; k < 5; k++) {
        MMRESULT a = wf_query(dir, WAVE_MAPPER, &fs[k], 0), b = wf_query(dir, 0xFFFF, &fs[k], 0);
        CHECKF(a == b, "%s fmt %d: mapper %u vs 0xFFFF %u", wf_dirname(dir), k, a, b);
        a = wf_query(dir, WAVE_MAPPER, &fs[k], WAVE_FORMAT_DIRECT); b = wf_query(dir, 0xFFFF, &fs[k], WAVE_FORMAT_DIRECT);
        CHECKF(a == b, "%s fmt %d DIRECT: mapper %u vs 0xFFFF %u", wf_dirname(dir), k, a, b);
        if (wf_ndev(dir) == 0) EXPECT_EQ(a, MMSYSERR_BADDEVICEID);
    }
}
TEST(wf_out_query_ffff_is_mapper) { REQ_OPEN(); alias_ffff(WF_OUT); }
TEST(wf_in_query_ffff_is_mapper)  { REQ_OPEN(); alias_ffff(WF_IN); }

/* WAVE_MAPPED with a specific id goes through the mapper driver restricted to that device. */
static void mapped_id(int dir) {
    wf_fmt f; pcm16(&f);
    UINT n = wf_ndev(dir);
    wf_cell("pcm16/2/44100 MAPPED", dir, 0, &f, WAVE_MAPPED, 0);
    wf_cell("pcm16/2/44100 MAPPED|DIRECT", dir, 0, &f, WAVE_MAPPED | WAVE_FORMAT_DIRECT, 0);
    /* Out-of-range id with WAVE_MAPPED: the driver lookup is for the mapper (-1), the bad id is handed to it. */
    MMRESULT r = wf_query(dir, n, &f, WAVE_MAPPED);
    if (n == 0) EXPECT_EQ(r, MMSYSERR_BADDEVICEID);
    else t_note("CELL query %s MAPPED id n=%u r=%u", wf_dirname(dir), n, r);
    r = wf_query(dir, n + 50, &f, WAVE_MAPPED);
    if (n == 0) EXPECT_EQ(r, MMSYSERR_BADDEVICEID);
    else t_note("CELL query %s MAPPED id n+50 r=%u", wf_dirname(dir), r);
}
TEST(wf_out_query_mapped_specific_id) { REQ_OPEN(); mapped_id(WF_OUT); }
TEST(wf_in_query_mapped_specific_id)  { REQ_OPEN(); mapped_id(WF_IN); }

/* Zero devices: every format family is 2 on the mapper and on id 0 (device check precedes the format). */
static void zero_dev_all(int dir) {
    if (wf_ndev(dir) != 0) { t_skip("wave%s devices present (%u)", dir == WF_OUT ? "Out" : "In", wf_ndev(dir)); return; }
    wf_fmt f; wf_ext e;
    const DWORD fl[] = { 0, WAVE_FORMAT_DIRECT, WAVE_ALLOWSYNC };
    for (int k = 0; k < 3; k++) {
        UINT ids[2] = { WAVE_MAPPER, 0 };
        for (int j = 0; j < 2; j++) {
            wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 16); EXPECT_EQ(wf_query(dir, ids[j], &f, fl[k]), MMSYSERR_BADDEVICEID);
            wf_pcm(&f, WAVE_FORMAT_PCM_, 6, 48000, 16); EXPECT_EQ(wf_query(dir, ids[j], &f, fl[k]), MMSYSERR_BADDEVICEID);
            wf_pcm(&f, WAVE_FORMAT_IEEE_FLOAT_, 2, 48000, 32); EXPECT_EQ(wf_query(dir, ids[j], &f, fl[k]), MMSYSERR_BADDEVICEID);
            wf_pcm(&f, 0x1234, 2, 44100, 16); EXPECT_EQ(wf_query(dir, ids[j], &f, fl[k]), MMSYSERR_BADDEVICEID);
            wf_pcm(&f, WAVE_FORMAT_PCM_, 0, 0, 0); EXPECT_EQ(wf_query(dir, ids[j], &f, fl[k]), MMSYSERR_BADDEVICEID);
            wf_msadpcm(&f, 1, 22050); EXPECT_EQ(wf_query(dir, ids[j], &f, fl[k]), MMSYSERR_BADDEVICEID);
            wf_extfmt(&e, 2, 48000, 16, 16, 3, &WF_SUB_PCM); EXPECT_EQ(wf_query(dir, ids[j], &e, fl[k]), MMSYSERR_BADDEVICEID);
            wf_extfmt(&e, 2, 48000, 16, 16, 3, &WF_SUB_BOGUS); EXPECT_EQ(wf_query(dir, ids[j], &e, fl[k]), MMSYSERR_BADDEVICEID);
        }
    }
    /* A real open is 2 as well, with the handle zeroed. */
    ULONG_PTR h = 0x1234;
    wf_pcm(&f, WAVE_FORMAT_PCM_, 2, 44100, 16);
    EXPECT_EQ(q_raw(dir, &h, WAVE_MAPPER, &f, 0, CALLBACK_NULL), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(h, 0);
    h = 0x1234;
    EXPECT_EQ(q_raw(dir, &h, 0, &f, 0, CALLBACK_NULL), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(h, 0);
}
TEST(wf_out_zero_devices_every_family) { REQ_OPEN(); zero_dev_all(WF_OUT); }
TEST(wf_in_zero_devices_every_family)  { REQ_OPEN(); zero_dev_all(WF_IN); }

/* Queries create nothing: many queries in a row and the device can still be opened and closed normally. */
static void repeat(int dir) {
    wf_fmt f; pcm16(&f);
    for (int i = 0; i < 64; i++) {
        MMRESULT r = wf_query(dir, i & 1 ? 0 : WAVE_MAPPER, &f, 0);
        if (r != (MMRESULT)core_exp(dir, 0)) { CHECKF(0, "iteration %d: %u", i, r); break; }
    }
    if (wf_ndev(dir)) EXPECT_EQ(wf_open_close(dir, WAVE_MAPPER, &f, 0), MMSYSERR_NOERROR);
}
TEST(wf_out_query_repeat_no_leak) { REQ_OPEN(); repeat(WF_OUT); }
TEST(wf_in_query_repeat_no_leak)  { REQ_OPEN(); repeat(WF_IN); }

/* A query while a real stream is open on the same device still answers (shared mode). */
static void query_while_open(int dir) {
    if (wf_need_dev(dir)) return;
    wf_fmt f; pcm16(&f);
    ULONG_PTR h = 0;
    MMRESULT r = q_raw(dir, &h, 0, &f, 0, CALLBACK_NULL);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    EXPECT_EQ(wf_query(dir, 0, &f, 0), MMSYSERR_NOERROR);
    EXPECT_EQ(wf_query(dir, WAVE_MAPPER, &f, 0), MMSYSERR_NOERROR);
    wf_fmt g; wf_pcm(&g, WAVE_FORMAT_PCM_, 1, 22050, 8);
    EXPECT_EQ(wf_query(dir, 0, &g, 0), MMSYSERR_NOERROR);
    if (dir == WF_OUT) EXPECT_EQ(U(waveOutClose)((HWAVEOUT)h), MMSYSERR_NOERROR);
    else EXPECT_EQ(U(waveInClose)((HWAVEIN)h), MMSYSERR_NOERROR);
}
TEST(wf_out_query_while_stream_open) { REQ_OPEN(); query_while_open(WF_OUT); }
TEST(wf_in_query_while_stream_open)  { REQ_OPEN(); query_while_open(WF_IN); }

/* cbSize is ignored for WAVE_FORMAT_PCM (no extra bytes are read); a large cbSize is not an error. */
static void pcm_cbsize(int dir) {
    wf_fmt f; pcm16(&f);
    static const WORD cb[] = { 1, 2, 22, 100 };
    char lab[48];
    for (unsigned i = 0; i < sizeof cb / sizeof *cb; i++) {
        f.cbSize = cb[i];
        sprintf(lab, "pcm16/2/44100 cbSize%u", cb[i]);
        wf_cell(lab, dir, WAVE_MAPPER, &f, 0, WF_NOTE);
        wf_cell(lab, dir, 0, &f, 0, WF_NOTE);
        wf_cell(lab, dir, 0, &f, WAVE_FORMAT_DIRECT, WF_NOTE);
    }
}
TEST(wf_out_query_pcm_cbsize_ignored) { REQ_OPEN(); pcm_cbsize(WF_OUT); }
TEST(wf_in_query_pcm_cbsize_ignored)  { REQ_OPEN(); pcm_cbsize(WF_IN); }
