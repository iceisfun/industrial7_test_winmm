/* wavein_capture: open / buffer queue / start / stop / reset / close on a real capture device.
 * Device half only (NEED_DEV). Input content is not checked (the virtual line-in delivers silence).
 * Flags: DONE 1, PREPARED 2, INQUEUE 0x10. Recording per test stays <= ~1 s; waits are bounded.
 */
#include "wic.h"

#define FIRST_MS 3000      /* first buffer of a fresh stream: start-up latency up to ~1 s [VM] */

TEST(wic_open_mapper_event) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; EXPECT_EQ(wic_open_std(&s, WAVE_MAPPER), MMSYSERR_NOERROR);
    if (!s.h) { wic_teardown(&s); return; }
    /* WIM_OPEN signals the event before waveInOpen returns */
    EXPECT_EQ(WaitForSingleObject(s.ev, 0), WAIT_OBJECT_0);
    HANDLE ev = s.ev; s.ev = NULL;
    EXPECT_EQ(U(waveInClose)(s.h), MMSYSERR_NOERROR); s.h = NULL;
    EXPECT_EQ(WaitForSingleObject(ev, 0), WAIT_OBJECT_0);   /* WIM_CLOSE */
    CloseHandle(ev);
    wic_teardown(&s);
}

TEST(wic_open_each_device_event) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    for (UINT i = 0; i < wic_ndev(); i++) {
        wic_s s; MMRESULT r = wic_open_std(&s, i);
        CHECKF(r == 0, "device %u r=%u", i, r);
        if (s.h) {
            UINT id = 0x77;
            EXPECT_EQ(U(waveInGetID)(s.h, &id), 0);
            EXPECT_EQ(id, i);
        }
        EXPECT_EQ(wic_teardown(&s), 0);
    }
}

TEST(wic_open_0xffff_is_mapper) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; EXPECT_EQ(wic_open_std(&s, 0xFFFF), 0);
    if (s.h) {
        UINT id = 0x77;
        EXPECT_EQ(U(waveInGetID)(s.h, &id), 0);
        EXPECT_EQ(id, WAVE_MAPPER);   /* 0xFFFF is rewritten to 0xFFFFFFFF before it is stored [decomp] */
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_getid_mapper_handle) {
    REQUIRE(U(waveInGetID)); NEED_DEV();
    wic_s s; EXPECT_EQ(wic_open_std(&s, WAVE_MAPPER), 0);
    if (s.h) {
        UINT id = 0x77;
        EXPECT_EQ(U(waveInGetID)(s.h, &id), 0);
        EXPECT_EQ(id, WAVE_MAPPER);   /* stored requested id, not the device the mapper picked */
        EXPECT_EQ(U(waveInGetID)(s.h, NULL), MMSYSERR_INVALPARAM);
    }
    wic_teardown(&s);
}

TEST(wic_open_mapped_flag_device0) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    MMRESULT r = wic_open_flags(&s, 0, &f, WAVE_MAPPED);
    EXPECT_EQ(r, 0);
    if (s.h) {
        UINT id = 0x77; EXPECT_EQ(U(waveInGetID)(s.h, &id), 0);
        EXPECT_EQ(id, 0);   /* [VM] WAVE_MAPPED keeps the requested id */
    }
    wic_teardown(&s);
}

TEST(wic_open_allowsync_flag) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    EXPECT_EQ(wic_open_flags(&s, WAVE_MAPPER, &f, WAVE_ALLOWSYNC), 0);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_open_direct_device0) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    MMRESULT r = wic_open_flags(&s, 0, &f, WAVE_FORMAT_DIRECT);
    EXPECT_EQ(r, 0);   /* [VM] 44.1 kHz/16/1 opens directly on the HDA line-in (shared-mode engine converts) */
    wic_teardown(&s);
}

TEST(wic_open_two_handles_concurrently) {
    REQUIRE(U(waveInOpen)); NEED_DEV();
    wic_s a, b;
    EXPECT_EQ(wic_open_std(&a, WAVE_MAPPER), 0);
    MMRESULT r = wic_open_std(&b, WAVE_MAPPER);
    EXPECT_EQ(r, 0);   /* shared-mode capture: several clients on one endpoint */
    if (a.h && b.h) {
        CHECK(a.h != b.h);
        EXPECT_EQ(wic_buf(&a, 0, wic_bytes_ms(wic_wfx(&a), 50)), 0);
        EXPECT_EQ(wic_buf(&b, 0, wic_bytes_ms(wic_wfx(&b), 50)), 0);
        EXPECT_EQ(wic_add(&a, 0), 0); EXPECT_EQ(wic_add(&b, 0), 0);
        EXPECT_EQ(U(waveInStart)(a.h), 0); EXPECT_EQ(U(waveInStart)(b.h), 0);
        CHECK(wic_wait_done(&a, 0, FIRST_MS)); CHECK(wic_wait_done(&b, 0, FIRST_MS));
        EXPECT_EQ(a.hdr[0].dwBytesRecorded, a.hdr[0].dwBufferLength);
        EXPECT_EQ(b.hdr[0].dwBytesRecorded, b.hdr[0].dwBufferLength);
    }
    EXPECT_EQ(wic_teardown(&b), 0);
    EXPECT_EQ(wic_teardown(&a), 0);
}

/* ---- header protocol on a real handle ---- */

TEST(wic_prepare_sets_exactly_prepared) {
    REQUIRE(U(waveInPrepareHeader)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    static const DWORD pre[] = {0, WHDR_DONE, WHDR_BEGINLOOP, WHDR_ENDLOOP, WHDR_DONE | WHDR_BEGINLOOP | WHDR_ENDLOOP};
    for (int i = 0; i < 5; i++) {
        s.mem[i] = HeapAlloc(GetProcessHeap(), 0, 4410); s.nb = i + 1;
        memset(&s.hdr[i], 0, sizeof(WAVEHDR));
        s.hdr[i].lpData = s.mem[i]; s.hdr[i].dwBufferLength = 4410; s.hdr[i].dwFlags = pre[i];
        s.hdr[i].dwUser = 0xABCDEF; s.hdr[i].dwBytesRecorded = 0x1234;
        EXPECT_EQ(U(waveInPrepareHeader)(s.h, &s.hdr[i], sizeof(WAVEHDR)), 0);
        CHECKF(s.hdr[i].dwFlags == WHDR_PREPARED, "pre 0x%lx -> 0x%lx", pre[i], s.hdr[i].dwFlags);
        EXPECT_EQ(s.hdr[i].dwUser, 0xABCDEF);
        EXPECT_EQ(s.hdr[i].dwBufferLength, 4410);
        EXPECT_EQ(s.hdr[i].lpData, s.mem[i]);
        EXPECT_EQ(s.hdr[i].dwBytesRecorded, 0x1234);   /* [VM] prepare leaves dwBytesRecorded alone */
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_prepare_twice_and_unprepare_twice) {
    REQUIRE(U(waveInPrepareHeader)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(wic_buf(&s, 0, 4410), 0);
    EXPECT_EQ(U(waveInPrepareHeader)(s.h, &s.hdr[0], sizeof(WAVEHDR)), 0);
    EXPECT_EQ(s.hdr[0].dwFlags, WHDR_PREPARED);
    EXPECT_EQ(U(waveInUnprepareHeader)(s.h, &s.hdr[0], sizeof(WAVEHDR)), 0);
    EXPECT_EQ(s.hdr[0].dwFlags, 0);
    EXPECT_EQ(U(waveInUnprepareHeader)(s.h, &s.hdr[0], sizeof(WAVEHDR)), 0);
    EXPECT_EQ(s.hdr[0].dwFlags, 0);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_prepare_larger_cbwh) {
    REQUIRE(U(waveInPrepareHeader)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    struct { WAVEHDR w; char pad[32]; } big; memset(&big, 0, sizeof big);
    char *m = HeapAlloc(GetProcessHeap(), 0, 4410);
    big.w.lpData = m; big.w.dwBufferLength = 4410;
    EXPECT_EQ(U(waveInPrepareHeader)(s.h, &big.w, sizeof big), 0);
    EXPECT_EQ(big.w.dwFlags, WHDR_PREPARED);
    EXPECT_EQ(U(waveInUnprepareHeader)(s.h, &big.w, sizeof big), 0);
    EXPECT_EQ(big.w.dwFlags, 0);
    EXPECT_EQ(wic_teardown(&s), 0);
    HeapFree(GetProcessHeap(), 0, m);
}

TEST(wic_addbuffer_flags_prepared_inqueue) {
    REQUIRE(U(waveInAddBuffer)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 250);
    for (int i = 0; i < 2; i++) {
        EXPECT_EQ(wic_buf(&s, i, len), 0);
        EXPECT_EQ(s.hdr[i].dwFlags, WHDR_PREPARED);
        EXPECT_EQ(wic_add(&s, i), 0);
        CHECKF(s.hdr[i].dwFlags == (WHDR_PREPARED | WHDR_INQUEUE), "buffer %d flags 0x%lx", i, s.hdr[i].dwFlags);
    }
    EXPECT_EQ(s.hdr[0].dwFlags, 0x12);
    EXPECT_EQ(s.hdr[1].dwFlags, 0x12);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_addbuffer_twice_stillplaying) {
    REQUIRE(U(waveInAddBuffer)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(wic_buf(&s, 0, 4410), 0);
    EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(wic_add(&s, 0), WAVERR_STILLPLAYING);
    EXPECT_EQ(s.hdr[0].dwFlags, 0x12);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_addbuffer_unprepared_real_handle) {
    REQUIRE(U(waveInAddBuffer)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    char m[4410]; WAVEHDR w; memset(&w, 0, sizeof w); w.lpData = m; w.dwBufferLength = sizeof m;
    EXPECT_EQ(U(waveInAddBuffer)(s.h, &w, sizeof w), WAVERR_UNPREPARED);
    w.dwFlags = WHDR_DONE;
    EXPECT_EQ(U(waveInAddBuffer)(s.h, &w, sizeof w), WAVERR_UNPREPARED);
    EXPECT_EQ(w.dwFlags, WHDR_DONE);
    EXPECT_EQ(U(waveInAddBuffer)(s.h, &w, 47), MMSYSERR_INVALPARAM);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_unprepare_queued_stillplaying) {
    REQUIRE(U(waveInUnprepareHeader)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(wic_buf(&s, 0, 4410), 0);
    EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInUnprepareHeader)(s.h, &s.hdr[0], sizeof(WAVEHDR)), WAVERR_STILLPLAYING);
    EXPECT_EQ(s.hdr[0].dwFlags, 0x12);
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    EXPECT_EQ(s.hdr[0].dwFlags & WHDR_INQUEUE, 0);
    EXPECT_EQ(U(waveInUnprepareHeader)(s.h, &s.hdr[0], sizeof(WAVEHDR)), 0);
    EXPECT_EQ(s.hdr[0].dwFlags & WHDR_PREPARED, 0);
    EXPECT_EQ(wic_teardown(&s), 0);
}

/* not started: queued buffers are not filled */
TEST(wic_no_start_no_data) {
    REQUIRE(U(waveInAddBuffer)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(wic_wfx(&s), 20)), 0);
    s.hdr[0].dwBytesRecorded = 0;
    EXPECT_EQ(wic_add(&s, 0), 0);
    Sleep(300);
    EXPECT_EQ(s.hdr[0].dwFlags, 0x12);
    EXPECT_EQ(s.hdr[0].dwBytesRecorded, 0);
    EXPECT_EQ(wic_pos(s.h, TIME_BYTES, NULL), 0);
    EXPECT_EQ(wic_teardown(&s), 0);
}

/* ---- the main capture path ---- */

TEST(wic_capture_two_quarter_second_buffers) {
    REQUIRE(U(waveInStart)); NEED_DEV();
    wic_s s; WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    EXPECT_EQ(wic_open(&s, WAVE_MAPPER, &f), 0);
    if (!s.h) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(&f, 250);
    EXPECT_EQ(len, 22050);
    for (int i = 0; i < 2; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); EXPECT_EQ(s.hdr[i].dwFlags, 0x12); }
    DWORD t0 = wic_now();
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    int d0 = wic_wait_done(&s, 0, FIRST_MS);
    DWORD t1 = wic_now() - t0;
    CHECKF(d0, "first buffer not DONE within %u ms", FIRST_MS);
    t_note("first 250 ms buffer DONE after %lu ms", t1);
    if (d0) {
        EXPECT_EQ(s.hdr[0].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(s.hdr[0].dwBytesRecorded, len);
        /* completion order follows AddBuffer order: the second is not done before the first */
        CHECK(wic_wait_done(&s, 1, 1500));
        EXPECT_EQ(s.hdr[1].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(s.hdr[1].dwBytesRecorded, len);
        DWORD pos = wic_pos(s.h, TIME_BYTES, NULL);
        CHECKF(pos != 0xFFFFFFFF && pos >= 2 * len, "pos %lu < recorded %lu", pos, 2 * len);
    }
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    for (int i = 0; i < 2; i++) EXPECT_EQ(U(waveInUnprepareHeader)(s.h, &s.hdr[i], sizeof(WAVEHDR)), 0);
    EXPECT_EQ(s.hdr[0].dwFlags, WHDR_DONE);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_capture_order_invariant_eight) {
    REQUIRE(U(waveInStart)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 30);
    for (int i = 0; i < 8; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    DWORD t0 = wic_now(); int viol = 0, polls = 0;
    while (wic_now() - t0 < FIRST_MS + 1000) {
        DWORD fl[8]; for (int i = 7; i >= 0; i--) fl[i] = ((volatile WAVEHDR *)&s.hdr[i])->dwFlags;
        /* sample from the last to the first: if k is DONE, every j<k must be DONE too */
        for (int k = 7; k > 0; k--) if (fl[k] & WHDR_DONE) for (int j = 0; j < k; j++) if (!(fl[j] & WHDR_DONE)) viol++;
        polls++;
        if (fl[7] & WHDR_DONE) break;
        WaitForSingleObject(s.ev, 5);
    }
    EXPECT_EQ(viol, 0);
    for (int i = 0; i < 8; i++) {
        CHECKF(s.hdr[i].dwFlags == (WHDR_DONE | WHDR_PREPARED), "buffer %d flags 0x%lx", i, s.hdr[i].dwFlags);
        CHECKF(s.hdr[i].dwBytesRecorded == len, "buffer %d recorded %lu of %lu", i, s.hdr[i].dwBytesRecorded, len);
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_capture_many_small_buffers) {
    REQUIRE(U(waveInStart)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 10);   /* 441 frames */
    for (int i = 0; i < 20; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_all(&s, 0, 20, FIRST_MS + 1000));
    int full = 0; for (int i = 0; i < 20; i++) if (s.hdr[i].dwBytesRecorded == len && s.hdr[i].dwFlags == 3) full++;
    EXPECT_EQ(full, 20);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_capture_bytes_recorded_overwritten) {
    REQUIRE(U(waveInStart)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 50);
    EXPECT_EQ(wic_buf(&s, 0, len), 0);
    s.hdr[0].dwBytesRecorded = 0x77777; s.hdr[0].dwUser = 0x5150;
    EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(s.hdr[0].dwBytesRecorded, 0);   /* [VM] AddBuffer zeroes dwBytesRecorded */
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(s.hdr[0].dwBytesRecorded, len);
    EXPECT_EQ(s.hdr[0].dwUser, 0x5150);
    EXPECT_EQ(s.hdr[0].dwBufferLength, len);
    EXPECT_EQ(s.hdr[0].lpData, s.mem[0]);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_capture_data_written) {
    REQUIRE(U(waveInStart)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 50);
    EXPECT_EQ(wic_buf(&s, 0, len), 0);
    EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    if (wic_wait_done(&s, 0, FIRST_MS)) {
        DWORD cd = 0; short mn = 32767, mx = -32768;
        for (DWORD i = 0; i < len; i++) if ((unsigned char)s.mem[0][i] == 0xCD) cd++;
        for (DWORD i = 0; i + 1 < len; i += 2) { short v = *(short *)(s.mem[0] + i); if (v < mn) mn = v; if (v > mx) mx = v; }
        /* content is not specified (virtual line-in); only report it */
        t_note("50 ms capture: %lu of %lu bytes still 0xCD, sample range [%d, %d]", cd, len, mn, mx);
    } else CHECK(!"buffer not done");
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_start_without_buffers_then_add) {
    REQUIRE(U(waveInStart)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    Sleep(100);
    EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(wic_wfx(&s), 50)), 0);
    EXPECT_EQ(wic_add(&s, 0), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(s.hdr[0].dwBytesRecorded, s.hdr[0].dwBufferLength);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_addbuffer_after_start_appends) {
    REQUIRE(U(waveInStart)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 50);
    EXPECT_EQ(wic_buf(&s, 0, len), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    for (int i = 1; i < 4; i++) { EXPECT_EQ(wic_buf(&s, i, len), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    CHECK(wic_wait_all(&s, 0, 4, FIRST_MS + 1000));
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(s.hdr[i].dwFlags, 3);
        EXPECT_EQ(s.hdr[i].dwBytesRecorded, len);
    }
    DWORD pos = wic_pos(s.h, TIME_BYTES, NULL);
    CHECKF(pos >= 4 * len, "pos %lu", pos);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_requeue_done_buffer) {
    REQUIRE(U(waveInStart)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 40);
    EXPECT_EQ(wic_buf(&s, 0, len), 0); EXPECT_EQ(wic_buf(&s, 1, len), 0);
    EXPECT_EQ(wic_add(&s, 0), 0); EXPECT_EQ(wic_add(&s, 1), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    int cycles = 0;
    for (int c = 0; c < 6; c++) {
        int i = c & 1;
        if (!wic_wait_done(&s, i, FIRST_MS)) break;
        EXPECT_EQ(s.hdr[i].dwFlags, 3);
        EXPECT_EQ(s.hdr[i].dwBytesRecorded, len);
        /* AddBuffer again without re-preparing: DONE is cleared and INQUEUE set */
        EXPECT_EQ(wic_add(&s, i), 0);
        CHECKF(s.hdr[i].dwFlags == 0x12, "requeue flags 0x%lx", s.hdr[i].dwFlags);   /* [VM] DONE cleared */
        cycles++;
    }
    EXPECT_EQ(cycles, 6);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_start_twice) {
    REQUIRE(U(waveInStart)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(wic_wfx(&s), 50)), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    MMRESULT r2 = U(waveInStart)(s.h);
    EXPECT_EQ(r2, 0);   /* [VM] */
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(s.hdr[0].dwBytesRecorded, s.hdr[0].dwBufferLength);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_stop_without_start) {
    REQUIRE(U(waveInStop)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    EXPECT_EQ(wic_buf(&s, 0, 4410), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    Sleep(50);
    EXPECT_EQ(s.hdr[0].dwFlags, 0x12);   /* a stop on a stream that never started leaves the queue alone */
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_reset_without_start_returns_buffers) {
    REQUIRE(U(waveInReset)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    for (int i = 0; i < 3; i++) { EXPECT_EQ(wic_buf(&s, i, 4410), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    for (int i = 0; i < 3; i++) {
        CHECKF(s.hdr[i].dwFlags == (WHDR_DONE | WHDR_PREPARED), "buffer %d flags 0x%lx", i, s.hdr[i].dwFlags);
        EXPECT_EQ(s.hdr[i].dwBytesRecorded, 0);
    }
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_reset_while_recording_returns_all) {
    REQUIRE(U(waveInReset)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD small = wic_bytes_ms(wic_wfx(&s), 30), big = wic_bytes_ms(wic_wfx(&s), 250);
    EXPECT_EQ(wic_buf(&s, 0, small), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    for (int i = 1; i < 4; i++) { EXPECT_EQ(wic_buf(&s, i, big), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    /* every queued buffer is back (DONE, not INQUEUE) when Reset returns */
    for (int i = 0; i < 4; i++) CHECKF(s.hdr[i].dwFlags == 3, "buffer %d flags 0x%lx", i, s.hdr[i].dwFlags);
    for (int i = 1; i < 4; i++) CHECKF(s.hdr[i].dwBytesRecorded <= big, "buffer %d recorded %lu", i, s.hdr[i].dwBytesRecorded);
    t_note("reset mid-stream recorded: %lu %lu %lu", s.hdr[1].dwBytesRecorded, s.hdr[2].dwBytesRecorded, s.hdr[3].dwBytesRecorded);
    EXPECT_EQ(s.hdr[3].dwBytesRecorded, 0);    /* never reached */
    EXPECT_EQ(wic_pos(s.h, TIME_BYTES, NULL), 0);   /* position resets */
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_reset_then_start_again) {
    REQUIRE(U(waveInReset)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 50);
    EXPECT_EQ(wic_buf(&s, 0, len), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    /* Reset stops the stream: a new buffer is not filled until Start */
    EXPECT_EQ(wic_add(&s, 0), 0);
    Sleep(200);
    EXPECT_EQ(s.hdr[0].dwFlags & (WHDR_DONE | WHDR_INQUEUE), WHDR_INQUEUE);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(s.hdr[0].dwBytesRecorded, len);
    EXPECT_EQ(wic_teardown(&s), 0);
}

/* Stop in the middle of a buffer: the partially filled buffer comes back DONE with what was recorded
 * (classic contract) [VM]. */
TEST(wic_stop_returns_partial_buffer) {
    REQUIRE(U(waveInStop)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD small = wic_bytes_ms(wic_wfx(&s), 30), big = wic_bytes_ms(wic_wfx(&s), 1000);
    EXPECT_EQ(wic_buf(&s, 0, small), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(wic_buf(&s, 1, big), 0); EXPECT_EQ(wic_add(&s, 1), 0);
    EXPECT_EQ(wic_buf(&s, 2, small), 0); EXPECT_EQ(wic_add(&s, 2), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    if (!wic_wait_done(&s, 0, FIRST_MS)) { CHECK(!"stream did not start"); wic_teardown(&s); return; }
    Sleep(150);
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    int d1 = wic_wait_done(&s, 1, 1000);
    t_note("after Stop mid-buffer: flags 0x%lx recorded %lu of %lu (done=%d); next flags 0x%lx",
           s.hdr[1].dwFlags, s.hdr[1].dwBytesRecorded, big, d1, s.hdr[2].dwFlags);
    CHECK(d1);
    EXPECT_EQ(s.hdr[1].dwFlags, 3);
    CHECKF(s.hdr[1].dwBytesRecorded > 0 && s.hdr[1].dwBytesRecorded < big, "recorded %lu", s.hdr[1].dwBytesRecorded);
    EXPECT_EQ(s.hdr[1].dwBytesRecorded % wic_wfx(&s)->nBlockAlign, 0);
    /* the buffer behind it stays queued */
    EXPECT_EQ(s.hdr[2].dwFlags, 0x12);
    EXPECT_EQ(U(waveInStop)(s.h), 0);   /* twice */
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_stop_then_start_resumes) {
    REQUIRE(U(waveInStop)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 40);
    EXPECT_EQ(wic_buf(&s, 0, len), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    DWORD p1 = wic_pos(s.h, TIME_BYTES, NULL);
    CHECK(p1 >= len && p1 != 0xFFFFFFFF);
    EXPECT_EQ(wic_buf(&s, 1, len), 0); EXPECT_EQ(wic_add(&s, 1), 0);
    Sleep(100);
    EXPECT_EQ(s.hdr[1].dwFlags, 0x12);   /* stopped: not filled */
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 1, FIRST_MS));
    EXPECT_EQ(s.hdr[1].dwBytesRecorded, len);
    DWORD p2 = wic_pos(s.h, TIME_BYTES, NULL);
    CHECKF(p2 != 0xFFFFFFFF && p2 >= p1 + len, "position not continued: %lu then %lu", p1, p2);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_stop_with_empty_queue) {
    REQUIRE(U(waveInStop)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 30);
    EXPECT_EQ(wic_buf(&s, 0, len), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    Sleep(50);   /* starved stream */
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    EXPECT_EQ(s.hdr[0].dwBytesRecorded, len);
    EXPECT_EQ(wic_teardown(&s), 0);
}

/* ---- close ---- */

TEST(wic_close_with_queued_buffers_stillplaying) {
    REQUIRE(U(waveInClose)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    for (int i = 0; i < 2; i++) { EXPECT_EQ(wic_buf(&s, i, 4410), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInClose)(s.h), WAVERR_STILLPLAYING);
    /* the handle stays open */
    UINT id = 0x77; EXPECT_EQ(U(waveInGetID)(s.h, &id), 0);
    EXPECT_EQ(s.hdr[0].dwFlags, 0x12);
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    for (int i = 0; i < 2; i++) { EXPECT_EQ(s.hdr[i].dwFlags, 3); EXPECT_EQ(U(waveInUnprepareHeader)(s.h, &s.hdr[i], sizeof(WAVEHDR)), 0); }
    EXPECT_EQ(U(waveInClose)(s.h), 0);
    s.h = NULL;
    wic_teardown(&s);
}

TEST(wic_close_while_recording_stillplaying) {
    REQUIRE(U(waveInClose)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 250);
    EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(wic_wfx(&s), 20)), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(wic_buf(&s, 1, len), 0); EXPECT_EQ(wic_add(&s, 1), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(U(waveInClose)(s.h), WAVERR_STILLPLAYING);
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    EXPECT_EQ(s.hdr[1].dwFlags, 3);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_close_after_stop_with_buffers) {
    REQUIRE(U(waveInClose)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(wic_wfx(&s), 20)), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(wic_buf(&s, 1, wic_bytes_ms(wic_wfx(&s), 1000)), 0); EXPECT_EQ(wic_add(&s, 1), 0);
    EXPECT_EQ(wic_buf(&s, 2, 4410), 0); EXPECT_EQ(wic_add(&s, 2), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    wic_wait_done(&s, 1, 1000);
    /* buffer 2 is still queued after Stop: close refuses */
    EXPECT_EQ(U(waveInClose)(s.h), WAVERR_STILLPLAYING);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_close_after_all_done_ok) {
    REQUIRE(U(waveInClose)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    EXPECT_EQ(wic_buf(&s, 0, wic_bytes_ms(wic_wfx(&s), 30)), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    /* nothing queued: close works without Stop/Reset, even with a prepared header */
    EXPECT_EQ(s.hdr[0].dwFlags, 3);
    EXPECT_EQ(U(waveInClose)(s.h), 0);
    s.h = NULL;
    /* header stays as it was (the driver does not unprepare) */
    EXPECT_EQ(s.hdr[0].dwFlags, 3);
    s.hdr[0].dwFlags = 0;   /* teardown: nothing to unprepare on a closed handle */
    wic_teardown(&s);
}

TEST(wic_closed_handle_is_invalid) {
    REQUIRE(U(waveInClose)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    HWAVEIN h = s.h;
    EXPECT_EQ(U(waveInClose)(h), 0); s.h = NULL;
    MMTIME m; m.wType = TIME_BYTES; UINT id;
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInStart)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInStop)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInReset)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInGetPosition)(h, &m, sizeof m), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInGetID)(h, &id), MMSYSERR_INVALHANDLE);
    char d[64]; WAVEHDR w; memset(&w, 0, sizeof w); w.lpData = d; w.dwBufferLength = sizeof d;
    EXPECT_EQ(U(waveInPrepareHeader)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    w.dwFlags = WHDR_PREPARED;
    EXPECT_EQ(U(waveInAddBuffer)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInUnprepareHeader)(h, &w, sizeof w), MMSYSERR_INVALHANDLE);
    wic_teardown(&s);
}

TEST(wic_cross_type_waveout_handle) {
    REQUIRE(U(waveOutOpen)); NEED_DEV();
    if (!wic_nout()) { t_skip("no waveOut device for a cross-type handle"); return; }
    WAVEFORMATEX f; wic_pcm(&f, 44100, 16, 1);
    HWAVEOUT ho = NULL;
    if (U(waveOutOpen)(&ho, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL)) { t_skip("waveOutOpen failed"); return; }
    HWAVEIN hi = (HWAVEIN)ho; MMTIME m; m.wType = TIME_BYTES; UINT id;
    EXPECT_EQ(U(waveInStart)(hi), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInStop)(hi), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInReset)(hi), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInGetPosition)(hi, &m, sizeof m), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInGetID)(hi, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInClose)(hi), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveOutClose)(ho), 0);
    /* and the other way round */
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER) == 0) {
        EXPECT_EQ(U(waveOutReset)((HWAVEOUT)s.h), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(U(waveOutClose)((HWAVEOUT)s.h), MMSYSERR_INVALHANDLE);
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_odd_length_buffer) {
    REQUIRE(U(waveInAddBuffer)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    /* 16-bit mono: 1001 bytes is not a whole number of blocks */
    EXPECT_EQ(wic_buf(&s, 0, 1001), 0);
    MMRESULT r = wic_add(&s, 0);
    EXPECT_EQ(r, 0);   /* [VM] no block-alignment check on AddBuffer */
    if (r == 0) {
        EXPECT_EQ(U(waveInStart)(s.h), 0);
        CHECK(wic_wait_done(&s, 0, FIRST_MS));
        /* [VM] the whole buffer is filled, including the trailing half frame */
        EXPECT_EQ(s.hdr[0].dwBytesRecorded, 1001);
        EXPECT_EQ(s.hdr[0].dwFlags, 3);
    }
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_zero_length_buffer) {
    REQUIRE(U(waveInAddBuffer)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    MMRESULT rp = wic_buf(&s, 0, 0);
    EXPECT_EQ(rp, 0);
    EXPECT_EQ(s.hdr[0].dwFlags, WHDR_PREPARED);
    if (rp == 0) {
        EXPECT_EQ(wic_add(&s, 0), 0);      /* [VM] a zero-length buffer is accepted */
        EXPECT_EQ(s.hdr[0].dwFlags, 0x12);
        EXPECT_EQ(wic_buf(&s, 1, wic_bytes_ms(wic_wfx(&s), 30)), 0); EXPECT_EQ(wic_add(&s, 1), 0);
        EXPECT_EQ(U(waveInStart)(s.h), 0);
        int d1 = wic_wait_done(&s, 1, 1500);
        /* [VM] the empty buffer is returned DONE with 0 bytes; whether the buffer behind it completes is not
         * pinned (it did not within 3 s on the VM) */
        CHECK(wic_wait_done(&s, 0, 500));
        EXPECT_EQ(s.hdr[0].dwFlags, WHDR_DONE | WHDR_PREPARED);
        EXPECT_EQ(s.hdr[0].dwBytesRecorded, 0);
        t_note("zero-length buffer: flags 0x%lx recorded %lu; next buffer done=%d", s.hdr[0].dwFlags, s.hdr[0].dwBytesRecorded, d1);
    }
    wic_teardown(&s);
}

TEST(wic_message_interface_size) {
    REQUIRE(U(waveInMessage)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    ULONG sz = 0;
    MMRESULT r = U(waveInMessage)(s.h, 0x80C /* DRV_QUERYDEVICEINTERFACESIZE */, (DWORD_PTR)&sz, 0);
    t_note("waveInMessage(mapper handle, DRV_QUERYDEVICEINTERFACESIZE) -> %u size %lu", r, sz);
    /* [VM] the wave mapper does not answer the interface-size query on a handle: NOTSUPPORTED, size untouched */
    EXPECT_EQ(r, MMSYSERR_NOTSUPPORTED);
    EXPECT_EQ(sz, 0);
    ULONG sz0 = 0;
    MMRESULT r0 = U(waveInMessage)((HWAVEIN)(ULONG_PTR)0, 0x80C, (DWORD_PTR)&sz0, 0);
    t_note("waveInMessage(id 0, DRV_QUERYDEVICEINTERFACESIZE) -> %u size %lu", r0, sz0);
    /* [VM] a value that is not a handle is taken as a device id; the WDM capture driver answers 1 (ERROR) */
    EXPECT_EQ(r0, MMSYSERR_ERROR);
    EXPECT_EQ(sz0, 0);
    EXPECT_EQ(U(waveInMessage)((HWAVEIN)(ULONG_PTR)9999, 0x80C, (DWORD_PTR)&sz0, 0), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_stop_then_reset_returns_rest) {
    REQUIRE(U(waveInReset)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 30);
    EXPECT_EQ(wic_buf(&s, 0, len), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    for (int i = 1; i < 4; i++) { EXPECT_EQ(wic_buf(&s, i, wic_bytes_ms(wic_wfx(&s), 1000)), 0); EXPECT_EQ(wic_add(&s, i), 0); }
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    Sleep(60);
    EXPECT_EQ(U(waveInStop)(s.h), 0);
    CHECK(wic_wait_done(&s, 1, 1000));      /* partial buffer handed back by Stop */
    EXPECT_EQ(s.hdr[2].dwFlags, 0x12);
    EXPECT_EQ(s.hdr[3].dwFlags, 0x12);
    EXPECT_EQ(U(waveInReset)(s.h), 0);
    for (int i = 2; i < 4; i++) { EXPECT_EQ(s.hdr[i].dwFlags, 3); EXPECT_EQ(s.hdr[i].dwBytesRecorded, 0); }
    EXPECT_EQ(wic_teardown(&s), 0);
}

TEST(wic_unprepare_after_done_keeps_done) {
    REQUIRE(U(waveInUnprepareHeader)); NEED_DEV();
    wic_s s; if (wic_open_std(&s, WAVE_MAPPER)) { wic_teardown(&s); return; }
    DWORD len = wic_bytes_ms(wic_wfx(&s), 20);
    EXPECT_EQ(wic_buf(&s, 0, len), 0); EXPECT_EQ(wic_add(&s, 0), 0);
    EXPECT_EQ(U(waveInStart)(s.h), 0);
    CHECK(wic_wait_done(&s, 0, FIRST_MS));
    EXPECT_EQ(U(waveInUnprepareHeader)(s.h, &s.hdr[0], sizeof(WAVEHDR)), 0);
    EXPECT_EQ(s.hdr[0].dwFlags, WHDR_DONE);          /* only PREPARED is cleared [decomp] */
    EXPECT_EQ(s.hdr[0].dwBytesRecorded, len);
    EXPECT_EQ(wic_add(&s, 0), WAVERR_UNPREPARED);
    EXPECT_EQ(wic_teardown(&s), 0);
}
