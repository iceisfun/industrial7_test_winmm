/* wavein_capture: the zero-device / out-of-range half. Runs on every guest.
 *
 * With waveInGetNumDevs() == 0 the ids WAVE_MAPPER, 0xFFFF (16-bit mapper alias), 0 and 1 are all invalid;
 * with n devices the ids n, n+1 and 9999 are. waveInOpen zeroes *phwi before the device check and returns
 * MMSYSERR_BADDEVICEID (2) [decomp winmmbase waveInOpen 0x11290; VM noaudio]; no callback fires because
 * no handle exists. WAVE_FORMAT_QUERY goes through the same device check (2) [VM].
 */
#include "wic.h"

typedef struct { const char *name; WORD tag; DWORD rate; WORD bits, ch; int ext; } fmtrow;
static const fmtrow k_fmts[] = {
    {"pcm44100_16_1", WAVE_FORMAT_PCM, 44100, 16, 1, 0},
    {"pcm8000_8_1", WAVE_FORMAT_PCM, 8000, 8, 1, 0},
    {"pcm48000_16_2", WAVE_FORMAT_PCM, 48000, 16, 2, 0},
    {"pcm22050_8_2", WAVE_FORMAT_PCM, 22050, 8, 2, 0},
    {"pcm96000_24_2", WAVE_FORMAT_PCM, 96000, 24, 2, 0},
    {"float48000_2", WAVE_FORMAT_IEEE_FLOAT, 48000, 32, 2, 0},
    {"ext_pcm48000_16_2", WAVE_FORMAT_EXTENSIBLE, 48000, 16, 2, 1},
    {"ext_float48000_2", WAVE_FORMAT_EXTENSIBLE, 48000, 32, 2, 2},
};
#define NFMT (int)(sizeof k_fmts / sizeof k_fmts[0])

static void mkfmt(const fmtrow *r, WAVEFORMATEXTENSIBLE *f) {
    if (r->ext) wic_ext(f, r->rate, r->bits, r->ch, r->ext == 2);
    else if (r->tag == WAVE_FORMAT_IEEE_FLOAT) { memset(f, 0, sizeof *f); wic_float(&f->Format, r->rate, r->ch); }
    else { memset(f, 0, sizeof *f); wic_pcm(&f->Format, r->rate, r->bits, r->ch); }
}

/* the ids that are invalid on this guest */
static int bad_ids(UINT *ids) {
    UINT n = wic_ndev(); int k = 0;
    if (!n) { ids[k++] = WAVE_MAPPER; ids[k++] = 0xFFFF; ids[k++] = 0; ids[k++] = 1; ids[k++] = 9999; }
    else { ids[k++] = n; ids[k++] = n + 1; ids[k++] = 9999; ids[k++] = 0xFFFE; }
    return k;
}

static volatile LONG g_cb_hits;
static void CALLBACK cb_count(HWAVEIN h, UINT m, DWORD_PTR i, DWORD_PTR a, DWORD_PTR b) { InterlockedIncrement(&g_cb_hits); }

TEST(wic_nodev_open_event_table) {
    REQUIRE(U(waveInOpen));
    UINT ids[8]; int k = bad_ids(ids);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    for (int i = 0; i < k; i++)
        for (int j = 0; j < NFMT; j++) {
            WAVEFORMATEXTENSIBLE f; mkfmt(&k_fmts[j], &f);
            HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
            MMRESULT r = U(waveInOpen)(&h, ids[i], &f.Format, (DWORD_PTR)ev, 0, CALLBACK_EVENT);
            CHECKF(r == MMSYSERR_BADDEVICEID, "id %u fmt %s r=%u", ids[i], k_fmts[j].name, r);
            CHECKF(h == NULL, "id %u fmt %s handle not zeroed", ids[i], k_fmts[j].name);
        }
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);   /* no WIM_OPEN for a failed open */
    CloseHandle(ev);
}

TEST(wic_nodev_open_function_table) {
    REQUIRE(U(waveInOpen));
    UINT ids[8]; int k = bad_ids(ids);
    g_cb_hits = 0;
    for (int i = 0; i < k; i++)
        for (int j = 0; j < NFMT; j++) {
            WAVEFORMATEXTENSIBLE f; mkfmt(&k_fmts[j], &f);
            HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
            MMRESULT r = U(waveInOpen)(&h, ids[i], &f.Format, (DWORD_PTR)cb_count, 0x77, CALLBACK_FUNCTION);
            CHECKF(r == MMSYSERR_BADDEVICEID, "id %u fmt %s r=%u", ids[i], k_fmts[j].name, r);
            CHECK(h == NULL);
        }
    EXPECT_EQ(g_cb_hits, 0);
}

TEST(wic_nodev_open_null_callback_table) {
    REQUIRE(U(waveInOpen));
    UINT ids[8]; int k = bad_ids(ids);
    for (int i = 0; i < k; i++)
        for (int j = 0; j < NFMT; j++) {
            WAVEFORMATEXTENSIBLE f; mkfmt(&k_fmts[j], &f);
            HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
            CHECKF(U(waveInOpen)(&h, ids[i], &f.Format, 0, 0, CALLBACK_NULL) == MMSYSERR_BADDEVICEID, "id %u fmt %s", ids[i], k_fmts[j].name);
            CHECK(h == NULL);
        }
}

TEST(wic_nodev_open_thread_table) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(PeekMessageA));
    MSG m; U(PeekMessageA)(&m, NULL, 0, 0, PM_NOREMOVE);   /* create the queue */
    UINT ids[8]; int k = bad_ids(ids);
    for (int i = 0; i < k; i++) {
        WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
        HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
        EXPECT_EQ(U(waveInOpen)(&h, ids[i], &f, GetCurrentThreadId(), 0, CALLBACK_THREAD), MMSYSERR_BADDEVICEID);
        CHECK(h == NULL);
    }
    CHECK(!U(PeekMessageA)(&m, (HWND)-1, MM_WIM_OPEN, MM_WIM_DATA, PM_REMOVE));
}

TEST(wic_nodev_query_table) {
    REQUIRE(U(waveInOpen));
    UINT ids[8]; int k = bad_ids(ids);
    for (int i = 0; i < k; i++)
        for (int j = 0; j < NFMT; j++) {
            WAVEFORMATEXTENSIBLE f; mkfmt(&k_fmts[j], &f);
            CHECKF(U(waveInOpen)(NULL, ids[i], &f.Format, 0, 0, WAVE_FORMAT_QUERY) == MMSYSERR_BADDEVICEID,
                   "query id %u fmt %s", ids[i], k_fmts[j].name);
            HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
            /* with QUERY the handle pointer is ignored entirely (not zeroed) [decomp] */
            CHECKF(U(waveInOpen)(&h, ids[i], &f.Format, 0, 0, WAVE_FORMAT_QUERY) == MMSYSERR_BADDEVICEID,
                   "query+ph id %u fmt %s", ids[i], k_fmts[j].name);
            CHECK(h == (HWAVEIN)(ULONG_PTR)0x5555);
        }
}

TEST(wic_nodev_direct_query_table) {
    REQUIRE(U(waveInOpen));
    UINT ids[8]; int k = bad_ids(ids);
    for (int i = 0; i < k; i++) {
        if (ids[i] == WAVE_MAPPER || ids[i] == 0xFFFF) continue;   /* DIRECT is legal on both, but keep it to ids */
        WAVEFORMATEX f; wic_pcm(&f, 48000, 16, 2);
        EXPECT_EQ(U(waveInOpen)(NULL, ids[i], &f, 0, 0, WAVE_FORMAT_DIRECT_QUERY), MMSYSERR_BADDEVICEID);
        EXPECT_EQ(U(waveInOpen)(NULL, ids[i], &f, 0, 0, WAVE_FORMAT_QUERY | WAVE_ALLOWSYNC), MMSYSERR_BADDEVICEID);
    }
}

/* WAVE_MAPPED with a specific id is allowed by the flag mask (0x7E10) and resolves the mapper driver; with no
 * device at all it still fails with 2. */
TEST(wic_nodev_open_mapped_flag) {
    REQUIRE(U(waveInOpen));
    if (wic_ndev()) { t_skip("devices present: WAVE_MAPPED covered in the device half"); return; }
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
    EXPECT_EQ(U(waveInOpen)(&h, 0, &f, 0, 0, WAVE_MAPPED), MMSYSERR_BADDEVICEID);
    CHECK(h == NULL);
}

/* flag errors are reported before the device check: same codes on every guest [decomp mask 0x7E04/0x7E10] */
TEST(wic_nodev_flag_ladder_before_device) {
    REQUIRE(U(waveInOpen));
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    static const struct { UINT id; DWORD fl; MMRESULT want; } t[] = {
        {WAVE_MAPPER, CALLBACK_EVENT | WAVE_MAPPED, MMSYSERR_INVALFLAG},
        {0xFFFF, CALLBACK_EVENT | WAVE_MAPPED, MMSYSERR_INVALFLAG},
        {0, CALLBACK_EVENT | 0x10, MMSYSERR_INVALFLAG},
        {5000, CALLBACK_EVENT | 0x10, MMSYSERR_INVALFLAG},
        {WAVE_MAPPER, CALLBACK_EVENT | 0x200, MMSYSERR_INVALFLAG},
        {WAVE_MAPPER, CALLBACK_EVENT | 0x400, MMSYSERR_INVALFLAG},
        {WAVE_MAPPER, CALLBACK_EVENT | 0x4000, MMSYSERR_INVALFLAG},
        {0, CALLBACK_FUNCTION | 0x800, MMSYSERR_INVALFLAG},
        {0, CALLBACK_NULL | 0x1000, MMSYSERR_INVALFLAG},
        {0, CALLBACK_NULL | 0x2000, MMSYSERR_INVALFLAG},
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        MMRESULT r = U(waveInOpen)(&h, t[i].id, &f, (DWORD_PTR)ev, 0, t[i].fl);
        CHECKF(r == t[i].want, "row %u id %u flags 0x%lx r=%u", i, t[i].id, t[i].fl, r);
        CHECKF(h == (HWAVEIN)(ULONG_PTR)0x5555, "row %u: handle written before the flag check", i);
        CloseHandle(ev);
    }
}

TEST(wic_nodev_null_format_before_device) {
    REQUIRE(U(waveInOpen));
    HWAVEIN h = (HWAVEIN)(ULONG_PTR)0x5555;
    UINT ids[] = {WAVE_MAPPER, 0, 9999};
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(U(waveInOpen)(&h, ids[i], NULL, 0, 0, CALLBACK_NULL), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveInOpen)(NULL, ids[i], NULL, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
    }
    CHECK(h == (HWAVEIN)(ULONG_PTR)0x5555);
    WAVEFORMATEX f; wic_pcm(&f, 8000, 8, 1);
    for (int i = 0; i < 3; i++) EXPECT_EQ(U(waveInOpen)(NULL, ids[i], &f, 0, 0, CALLBACK_NULL), MMSYSERR_INVALPARAM);
}

/* --- capture-op contract on handles that are not open waveIn handles (no device needed) --- */

static const ULONG_PTR k_badh[] = {0, 1, 2, 0xFFFF, (ULONG_PTR)-1, 0xFFFFFFFF};
#define NBADH (int)(sizeof k_badh / sizeof k_badh[0])

TEST(wic_nodev_start_stop_reset_bad_handles) {
    REQUIRE(U(waveInStart)); REQUIRE(U(waveInStop)); REQUIRE(U(waveInReset)); REQUIRE(U(waveInClose));
    for (int i = 0; i < NBADH; i++) {
        HWAVEIN h = (HWAVEIN)k_badh[i];
        CHECKF(U(waveInStart)(h) == MMSYSERR_INVALHANDLE, "start %llx", (unsigned long long)k_badh[i]);
        CHECKF(U(waveInStop)(h) == MMSYSERR_INVALHANDLE, "stop %llx", (unsigned long long)k_badh[i]);
        CHECKF(U(waveInReset)(h) == MMSYSERR_INVALHANDLE, "reset %llx", (unsigned long long)k_badh[i]);
        CHECKF(U(waveInClose)(h) == MMSYSERR_INVALHANDLE, "close %llx", (unsigned long long)k_badh[i]);
    }
}

TEST(wic_nodev_getposition_bad_handles) {
    REQUIRE(U(waveInGetPosition));
    static const UINT types[] = {TIME_MS, TIME_SAMPLES, TIME_BYTES, TIME_SMPTE, TIME_MIDI, TIME_TICKS};
    for (int i = 0; i < NBADH; i++)
        for (int t = 0; t < 6; t++) {
            MMTIME m; memset(&m, 0x5A, sizeof m); m.wType = types[t];
            CHECKF(U(waveInGetPosition)((HWAVEIN)k_badh[i], &m, sizeof m) == MMSYSERR_INVALHANDLE, "h %llx type %u", (unsigned long long)k_badh[i], types[t]);
            CHECK(m.wType == types[t]);          /* untouched */
            CHECK(m.u.cb == 0x5A5A5A5A);
            /* NULL mmtime is checked before the handle [decomp] */
            CHECK(U(waveInGetPosition)((HWAVEIN)k_badh[i], NULL, sizeof m) == MMSYSERR_INVALPARAM);
            CHECK(U(waveInGetPosition)((HWAVEIN)k_badh[i], NULL, 0) == MMSYSERR_INVALPARAM);
        }
}

TEST(wic_nodev_addbuffer_ladder) {
    REQUIRE(U(waveInAddBuffer));
    char data[64];
    for (int i = 0; i < NBADH; i++) {
        HWAVEIN h = (HWAVEIN)k_badh[i];
        WAVEHDR w; memset(&w, 0, sizeof w); w.lpData = data; w.dwBufferLength = sizeof data;
        EXPECT_EQ(U(waveInAddBuffer)(h, &w, sizeof w), WAVERR_UNPREPARED);          /* 34 before handle */
        w.dwFlags = WHDR_PREPARED | WHDR_INQUEUE;
        EXPECT_EQ(U(waveInAddBuffer)(h, &w, sizeof w), WAVERR_STILLPLAYING);         /* 33 before handle */
        w.dwFlags = WHDR_PREPARED;
        EXPECT_EQ(U(waveInAddBuffer)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
        w.dwFlags = WHDR_PREPARED | WHDR_DONE;
        EXPECT_EQ(U(waveInAddBuffer)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(w.dwFlags, WHDR_PREPARED | WHDR_DONE);                            /* not touched */
        w.dwFlags = WHDR_PREPARED | 0x20;
        EXPECT_EQ(U(waveInAddBuffer)(h, &w, sizeof w), MMSYSERR_INVALPARAM);
        w.dwFlags = WHDR_PREPARED;
        EXPECT_EQ(U(waveInAddBuffer)(h, &w, sizeof w - 1), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveInAddBuffer)(h, NULL, sizeof w), MMSYSERR_INVALPARAM);
    }
}

TEST(wic_nodev_prepare_unprepare_ladder) {
    REQUIRE(U(waveInPrepareHeader)); REQUIRE(U(waveInUnprepareHeader));
    char data[64];
    for (int i = 0; i < NBADH; i++) {
        HWAVEIN h = (HWAVEIN)k_badh[i];
        WAVEHDR w; memset(&w, 0, sizeof w); w.lpData = data; w.dwBufferLength = sizeof data;
        EXPECT_EQ(U(waveInPrepareHeader)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(w.dwFlags, 0);
        w.dwFlags = WHDR_PREPARED;
        EXPECT_EQ(U(waveInPrepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);      /* already prepared: no handle check */
        EXPECT_EQ(U(waveInUnprepareHeader)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(w.dwFlags, WHDR_PREPARED);
        w.dwFlags = WHDR_PREPARED | WHDR_INQUEUE;
        EXPECT_EQ(U(waveInUnprepareHeader)(h, &w, sizeof w), WAVERR_STILLPLAYING);
        w.dwFlags = WHDR_DONE;
        EXPECT_EQ(U(waveInUnprepareHeader)(h, &w, sizeof w), MMSYSERR_NOERROR);    /* not prepared: nothing to do */
        EXPECT_EQ(w.dwFlags, WHDR_DONE);
    }
}

TEST(wic_nodev_getid_bad_handles) {
    REQUIRE(U(waveInGetID));
    for (int i = 0; i < NBADH; i++) {
        UINT id = 0x77;
        EXPECT_EQ(U(waveInGetID)((HWAVEIN)k_badh[i], &id), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(id, 0x77);
        EXPECT_EQ(U(waveInGetID)((HWAVEIN)k_badh[i], NULL), MMSYSERR_INVALPARAM);
    }
}

/* error texts for every code the capture path can return */
TEST(wic_nodev_errtext_capture_codes) {
    REQUIRE(U(waveInGetErrorTextA)); REQUIRE(U(waveInGetErrorTextW));
    static const MMRESULT codes[] = {0, 1, 2, 4, 5, 6, 7, 8, 10, 11, 12, 32, 33, 34, 35};
    for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        char a[256]; WCHAR w[256];
        memset(a, 0x7F, sizeof a);
        CHECKF(U(waveInGetErrorTextA)(codes[i], a, sizeof a) == 0, "code %u", codes[i]);
        CHECKF(a[0] != 0 && memchr(a, 0, sizeof a), "code %u text", codes[i]);
        CHECKF(U(waveInGetErrorTextW)(codes[i], w, 256) == 0, "W code %u", codes[i]);
        CHECK(w[0] != 0);
    }
    char a[8] = "xyz";
    EXPECT_EQ(U(waveInGetErrorTextA)(36, a, sizeof a), MMSYSERR_BADERRNUM);
    EXPECT_EQ(a[0], 0);
}

/* the handle is validated before the driver sees cbmmt: a short MMTIME on a bad handle is still 5 */
TEST(wic_nodev_getposition_short_cbmmt_bad_handle) {
    REQUIRE(U(waveInGetPosition));
    static const UINT sz[] = {0, 1, 4, 11};
    for (int i = 0; i < NBADH; i++)
        for (int j = 0; j < 4; j++) {
            MMTIME m; memset(&m, 0, sizeof m); m.wType = TIME_BYTES;
            CHECKF(U(waveInGetPosition)((HWAVEIN)k_badh[i], &m, sz[j]) == MMSYSERR_INVALHANDLE, "h %llx cb %u", (unsigned long long)k_badh[i], sz[j]);
        }
}
