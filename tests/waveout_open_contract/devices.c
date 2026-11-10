#include "wo.h"
/* Device ids against the device count: the zero-device contract (always) and the id range with devices.
 * Counts are read once (wo_ndev). With 0 waveOut devices every open that passes the gates returns 2, for
 * every id including WAVE_MAPPER and WAVE_FORMAT_QUERY, and a non-QUERY open leaves *phwo NULL. */

TEST(wo_dev_environment_record) {
    REQUIRE(U(waveOutGetNumDevs)); REQUIRE(U(waveOutGetDevCapsA));
    UINT n = wo_ndev();
    t_note("AUDIOENV waveout=%u wavein=%u", n, wo_nwavein());
    for (UINT i = 0; i < n && i < 8; i++) t_note("waveOut %u \"%s\"", i, wo_capsname(i));
    if (n) t_note("mapper \"%s\"", wo_capsname(WAVE_MAPPER));
    CHECK(n < 64);
}

TEST(wo_dev_zero_contract_all_ids) {
    REQUIRE(U(waveOutOpen));
    if (wo_ndev() != 0) { t_note("%u devices: zero-device contract not applicable", wo_ndev()); return; }
    WAVEFORMATEX wf; wo_std(&wf);
    static const UINT ids[] = { 0, 1, 2, 7, 0xFFFF, WAVE_MAPPER, WO_BADID, 0xFFFE, 0x10000, 0x7FFFFFFF, 0xFFFFFFFE };
    static const DWORD fls[] = { 0, WAVE_FORMAT_QUERY, WAVE_ALLOWSYNC, WAVE_FORMAT_DIRECT, WAVE_FORMAT_DIRECT_QUERY,
                                 CALLBACK_EVENT, CALLBACK_FUNCTION, CALLBACK_THREAD, CALLBACK_WINDOW, 0x20, 0x8000, 0x70000 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++)
        for (unsigned f = 0; f < sizeof fls / sizeof fls[0]; f++) {
            HWAVEOUT h = WO_SENTINEL;
            MMRESULT r = wo_open(&h, ids[i], &wf, 0, 0, fls[f]);
            CHECKF(r == MMSYSERR_BADDEVICEID, "id 0x%x fl 0x%lx -> %u", ids[i], fls[f], r);
            HWAVEOUT after = h;
            if (fls[f] & WAVE_FORMAT_QUERY) CHECK(after == WO_SENTINEL); else CHECKF(after == NULL, "*phwo %p", after);
            wo_release(r, fls[f], h);
            r = wo_open(NULL, ids[i], &wf, 0, 0, fls[f] | WAVE_FORMAT_QUERY);
            CHECKF(r == MMSYSERR_BADDEVICEID, "query id 0x%x fl 0x%lx -> %u", ids[i], fls[f], r);
        }
}

TEST(wo_dev_zero_contract_mapped_flags) {
    REQUIRE(U(waveOutOpen));
    if (wo_ndev() != 0) return;
    WAVEFORMATEX wf; wo_std(&wf);
    /* WAVE_MAPPED with an id and 0x10 with the mapper pass the masks, then hit the zero-device check. */
    static const UINT ids[] = { 0, 1, WO_BADID };
    for (unsigned i = 0; i < 3; i++) {
        HWAVEOUT h = WO_SENTINEL;
        EXPECT_EQ(wo_open(&h, ids[i], &wf, 0, 0, WAVE_MAPPED), MMSYSERR_BADDEVICEID);
        HWAVEOUT after = h; EXPECT_EQ(after, NULL);
        EXPECT_EQ(wo_open(NULL, ids[i], &wf, 0, 0, WAVE_MAPPED | WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    }
    HWAVEOUT h = WO_SENTINEL;
    EXPECT_EQ(wo_open(&h, WAVE_MAPPER, &wf, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE), MMSYSERR_BADDEVICEID);
    HWAVEOUT after = h; EXPECT_EQ(after, NULL);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(wo_open(NULL, 0xFFFF, &wf, 0, 0, WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE | WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
}

TEST(wo_dev_zero_contract_gates_still_win) {
    REQUIRE(U(waveOutOpen));
    if (wo_ndev() != 0) return;
    WAVEFORMATEX wf; wo_std(&wf);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, NULL, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, 0, &wf, 0, 0, 0x200), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_MAPPED | WAVE_FORMAT_QUERY), MMSYSERR_INVALFLAG);
    EXPECT_EQ(wo_open(NULL, 0, &wf, 0, 0, 0), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wo_open(NULL, 0, &wf, WO_NOWND, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY), MMSYSERR_INVALPARAM);
}

TEST(wo_dev_zero_contract_any_format) {
    REQUIRE(U(waveOutOpen));
    if (wo_ndev() != 0) return;
    /* With no device the format is never looked at: unsupported formats are 2 as well. */
    WAVEFORMATEX fs[5];
    wo_pcm(&fs[0], 6, 44100, 16);
    wo_pcm(&fs[1], 2, 0, 16);
    wo_pcm(&fs[2], 2, 44100, 16); fs[2].wFormatTag = 0x7777;
    wo_pcm(&fs[3], 0, 0, 0);
    wo_pcm(&fs[4], 1, 8000, 8);
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &fs[i], 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
        HWAVEOUT h = WO_SENTINEL;
        EXPECT_EQ(wo_open(&h, 0, &fs[i], 0, 0, 0), MMSYSERR_BADDEVICEID);
        HWAVEOUT after = h; EXPECT_EQ(after, NULL);
    }
}

TEST(wo_dev_zero_contract_no_callback_fired) {
    REQUIRE(U(waveOutOpen));
    if (wo_ndev() != 0) return;
    WAVEFORMATEX wf; wo_std(&wf);
    wo_rec rec; memset(&rec, 0, sizeof rec);
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HWAVEOUT h;
    EXPECT_EQ(wo_open(&h, WAVE_MAPPER, &wf, (DWORD_PTR)wo_rec_cb, (DWORD_PTR)&rec, CALLBACK_FUNCTION), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(wo_open(&h, 0, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT), MMSYSERR_BADDEVICEID);
    Sleep(100);
    EXPECT_EQ(rec.n, 0);
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_TIMEOUT);
    CloseHandle(ev);
}

/* ---- with devices ---- */

TEST(wo_dev_id_equal_count_is_bad) {
    REQUIRE(U(waveOutOpen));
    if (wo_nodev_contract(0)) return;
    UINT n = wo_ndev();
    WAVEFORMATEX wf; wo_std(&wf);
    UINT ids[] = { n, n + 1, n + 7, 0xFFFE, 0x10000, 0x10000 + n - 1, 0x7FFFFFFF, 0xFFFFFFFE, 0x80000000 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        HWAVEOUT h = WO_SENTINEL;
        MMRESULT r = wo_open(&h, ids[i], &wf, 0, 0, 0);
        CHECKF(r == MMSYSERR_BADDEVICEID, "id 0x%x -> %u", ids[i], r);
        HWAVEOUT after = h; CHECK(after == NULL);
        wo_release(r, 0, h);
        r = wo_open(NULL, ids[i], &wf, 0, 0, WAVE_FORMAT_QUERY);
        CHECKF(r == MMSYSERR_BADDEVICEID, "query id 0x%x -> %u", ids[i], r);
    }
}

TEST(wo_dev_every_valid_id_queries_ok) {
    REQUIRE(U(waveOutOpen));
    if (wo_nodev_contract(WAVE_FORMAT_QUERY)) return;
    WAVEFORMATEX wf; wo_std(&wf);
    for (UINT i = 0; i < wo_ndev(); i++) {
        MMRESULT r = wo_open(NULL, i, &wf, 0, 0, WAVE_FORMAT_QUERY);
        CHECKF(r == MMSYSERR_NOERROR, "device %u \"%s\" query 44100/16/2 -> %u", i, wo_capsname(i), r);
    }
    EXPECT_EQ(wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
    EXPECT_EQ(wo_open(NULL, 0xFFFF, &wf, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_NOERROR);
}

TEST(wo_dev_every_valid_id_opens) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose)); REQUIRE(U(waveOutGetID));
    if (wo_nodev_contract(0)) return;
    for (UINT i = 0; i < wo_ndev(); i++) {
        HWAVEOUT h = WO_SENTINEL;
        MMRESULT r = wo_open_std(&h, i, 0);
        CHECKF(r == MMSYSERR_NOERROR, "device %u open -> %u", i, r);
        if (r) continue;
        CHECK(h != NULL && h != WO_SENTINEL);
        UINT id = 0xABCD;
        EXPECT_EQ(wo_getid(h, &id), MMSYSERR_NOERROR);
        EXPECT_EQ(id, i);
        EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
    }
}

TEST(wo_dev_query_flags_matrix) {
    REQUIRE(U(waveOutOpen));
    if (wo_nodev_contract(WAVE_FORMAT_QUERY)) return;
    WAVEFORMATEX wf; wo_std(&wf);
    struct { UINT id; DWORD fl; } rows[] = {
        { WAVE_MAPPER, WAVE_FORMAT_QUERY }, { 0xFFFF, WAVE_FORMAT_QUERY }, { 0, WAVE_FORMAT_QUERY },
        { WAVE_MAPPER, WAVE_FORMAT_QUERY | WAVE_ALLOWSYNC }, { 0, WAVE_FORMAT_QUERY | WAVE_ALLOWSYNC },
        { WAVE_MAPPER, WAVE_FORMAT_DIRECT_QUERY }, { 0, WAVE_FORMAT_DIRECT_QUERY },
        { 0, WAVE_FORMAT_QUERY | WAVE_MAPPED }, { 0, WAVE_FORMAT_QUERY | WAVE_MAPPED | WAVE_FORMAT_DIRECT },
        { WAVE_MAPPER, WAVE_FORMAT_QUERY | WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE },
        { 0xFFFF, WAVE_FORMAT_QUERY | WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE },
        { WAVE_MAPPER, WAVE_FORMAT_QUERY | CALLBACK_EVENT }, { 0, WAVE_FORMAT_QUERY | CALLBACK_FUNCTION },
        { WAVE_MAPPER, WAVE_FORMAT_QUERY | CALLBACK_WINDOW }, { 0, WAVE_FORMAT_QUERY | CALLBACK_THREAD },
        { WAVE_MAPPER, WAVE_FORMAT_QUERY | 0x70000 }, { 0, WAVE_FORMAT_QUERY | 0x70000 },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        MMRESULT r = wo_open(NULL, rows[i].id, &wf, 0, 0, rows[i].fl);
        CHECKF(r == MMSYSERR_NOERROR, "id 0x%x fl 0x%lx -> %u", rows[i].id, rows[i].fl, r);
    }
}

/* Bits that pass the winmmbase mask go to the driver; what the driver does with them is recorded. */
TEST(wo_dev_query_passthrough_bits) {
    REQUIRE(U(waveOutOpen));
    if (wo_nodev_contract(WAVE_FORMAT_QUERY)) return;
    WAVEFORMATEX wf; wo_std(&wf);
    static const DWORD bits[] = { 0x20, 0x40, 0x80, 0x100, 0x8000, 0x80000, 0x00100000, 0x01000000, 0x80000000 };
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        MMRESULT a = wo_open(NULL, WAVE_MAPPER, &wf, 0, 0, WAVE_FORMAT_QUERY | bits[i]);
        MMRESULT b = wo_open(NULL, 0, &wf, 0, 0, WAVE_FORMAT_QUERY | bits[i]);
        t_note("OBS query bit 0x%lx: mapper %u, id0 %u", bits[i], a, b);
    }
}

TEST(wo_dev_mapped_badid) {
    REQUIRE(U(waveOutOpen));
    if (wo_nodev_contract(WAVE_MAPPED)) return;
    WAVEFORMATEX wf; wo_std(&wf);
    UINT n = wo_ndev();
    /* WAVE_MAPPED resolves the mapper driver, so the id range check is the mapper's. */
    HWAVEOUT h = WO_SENTINEL;
    MMRESULT r = wo_open(&h, n, &wf, 0, 0, WAVE_MAPPED);
    HWAVEOUT after = h;
    t_note("OBS WAVE_MAPPED id %u (== count) -> %u h=%p", n, r, after);
    wo_release(r, 0, after);
    r = wo_open(NULL, WO_BADID, &wf, 0, 0, WAVE_MAPPED | WAVE_FORMAT_QUERY);
    t_note("OBS WAVE_MAPPED|QUERY id 9999 -> %u", r);
}

TEST(wo_dev_handle_value_as_id) {
    REQUIRE(U(waveOutOpen)); REQUIRE(U(waveOutClose));
    if (wo_nodev_contract(0)) return;
    HWAVEOUT h;
    MMRESULT r = wo_open_std(&h, 0, 0);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r) return;
    /* Unlike caps/volume, waveOutOpen does not accept a handle in place of the id. */
    UINT asid = (UINT)(ULONG_PTR)h;
    if (asid < wo_ndev() || asid == 0xFFFF || asid == WAVE_MAPPER) t_note("handle low 32 bits 0x%x look like an id", asid);
    else {
        HWAVEOUT h2 = WO_SENTINEL;
        EXPECT_EQ(wo_open_std(&h2, asid, 0), MMSYSERR_BADDEVICEID);
        HWAVEOUT after = h2; EXPECT_EQ(after, NULL);
        WAVEFORMATEX wf; wo_std(&wf);
        EXPECT_EQ(wo_open(NULL, asid, &wf, 0, 0, WAVE_FORMAT_QUERY), MMSYSERR_BADDEVICEID);
    }
    EXPECT_EQ(wo_close(h), MMSYSERR_NOERROR);
}
