/* time_basic: timeGetSystemTime.
 * kernel32!timeGetSystemTime: (cbmmt < 12 || pmmt == NULL) -> 129 TIMERR_STRUCT; otherwise
 * u.ms = timeGetTime(), wType = TIME_MS (1), returns 0. Only bytes 0..7 are written; the
 * input wType is ignored (any requested format comes back as TIME_MS).
 */
#include "tb.h"

static int untouched(const BYTE *b, size_t from, size_t to, BYTE v) {
    for (size_t i = from; i < to; i++) if (b[i] != v) return 0;
    return 1;
}

TEST(time_basic_systime_layout) {
    REQUIRE(U(timeGetSystemTime));
    EXPECT_EQ(sizeof(MMTIME), 12);
    EXPECT_EQ(offsetof(MMTIME, wType), 0);
    EXPECT_EQ(offsetof(MMTIME, u), 4);
    EXPECT_EQ(TIME_MS, 1);
    EXPECT_EQ(TIME_SAMPLES, 2);
    EXPECT_EQ(TIME_BYTES, 4);
    EXPECT_EQ(TIME_SMPTE, 8);
    EXPECT_EQ(TIME_MIDI, 0x10);
    EXPECT_EQ(TIME_TICKS, 0x20);
}

TEST(time_basic_systime_ok) {
    REQUIRE(U(timeGetSystemTime)); REQUIRE(U(timeGetTime));
    MMTIME mt; memset(&mt, 0xCC, sizeof mt);
    DWORD t0 = U(timeGetTime)();
    EXPECT_EQ(U(timeGetSystemTime)(&mt, sizeof mt), 0);
    DWORD t1 = U(timeGetTime)();
    EXPECT_EQ(mt.wType, TIME_MS);
    CHECKF(tb_diff(mt.u.ms, t0) >= 0, "ms=%lu t0=%lu", mt.u.ms, t0);
    CHECKF(tb_diff(t1, mt.u.ms) >= 0, "ms=%lu t1=%lu", mt.u.ms, t1);
}

TEST(time_basic_systime_null_12) {
    REQUIRE(U(timeGetSystemTime));
    EXPECT_EQ(U(timeGetSystemTime)(NULL, 12), TB_STRUCT);
}

TEST(time_basic_systime_null_table) {
    REQUIRE(U(timeGetSystemTime));
    static const UINT cbs[] = { 0, 1, 4, 8, 11, 12, 13, 16, 64, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof cbs / sizeof cbs[0]; i++) {
        MMRESULT r = U(timeGetSystemTime)(NULL, cbs[i]);
        CHECKF(r == TB_STRUCT, "cb=%u r=%u", cbs[i], r);
    }
}

TEST(time_basic_systime_cb11) {
    REQUIRE(U(timeGetSystemTime));
    BYTE b[16]; memset(b, 0x6B, sizeof b);
    EXPECT_EQ(U(timeGetSystemTime)((LPMMTIME)b, 11), TB_STRUCT);
    CHECK(untouched(b, 0, sizeof b, 0x6B));
}

TEST(time_basic_systime_not_nocando) {
    REQUIRE(U(timeGetSystemTime));
    MMTIME mt;
    MMRESULT r = U(timeGetSystemTime)(&mt, 4);
    CHECK(r != TIMERR_NOCANDO);
    CHECK(r != MMSYSERR_INVALPARAM);
    EXPECT_EQ(r, TIMERR_STRUCT);
}

TEST(time_basic_systime_small_cb_table) {
    REQUIRE(U(timeGetSystemTime));
    for (UINT cb = 0; cb < 12; cb++) {
        BYTE b[32]; memset(b, 0xD2, sizeof b);
        MMRESULT r = U(timeGetSystemTime)((LPMMTIME)b, cb);
        CHECKF(r == TB_STRUCT, "cb=%u r=%u", cb, r);
        CHECKF(untouched(b, 0, sizeof b, 0xD2), "cb=%u wrote on failure", cb);
    }
}

TEST(time_basic_systime_every_cb_0_to_64) {
    REQUIRE(U(timeGetSystemTime)); REQUIRE(U(timeGetTime));
    for (UINT cb = 0; cb <= 64; cb++) {
        BYTE b[80]; memset(b, 0x19, sizeof b);
        DWORD t0 = U(timeGetTime)();
        MMRESULT r = U(timeGetSystemTime)((LPMMTIME)b, cb);
        DWORD t1 = U(timeGetTime)();
        if (cb < 12) {
            CHECKF(r == TB_STRUCT, "cb=%u r=%u", cb, r);
            CHECKF(untouched(b, 0, sizeof b, 0x19), "cb=%u", cb);
        } else {
            MMTIME *m = (MMTIME *)b;
            CHECKF(r == 0, "cb=%u r=%u", cb, r);
            CHECKF(m->wType == TIME_MS, "cb=%u wType=%u", cb, m->wType);
            CHECKF(tb_diff(m->u.ms, t0) >= 0 && tb_diff(t1, m->u.ms) >= 0, "cb=%u ms=%lu [%lu,%lu]", cb, m->u.ms, t0, t1);
            CHECKF(untouched(b, 8, sizeof b, 0x19), "cb=%u wrote past 8 bytes", cb);
        }
    }
}

TEST(time_basic_systime_every_cb_0_to_1024) {
    REQUIRE(U(timeGetSystemTime));
    BYTE b[24];
    for (UINT cb = 0; cb <= 1024; cb++) {
        memset(b, 0x93, sizeof b);
        MMRESULT r = U(timeGetSystemTime)((LPMMTIME)b, cb);
        if (cb < 12) CHECKF(r == TB_STRUCT && untouched(b, 0, sizeof b, 0x93), "cb=%u r=%u", cb, r);
        else CHECKF(r == 0 && ((MMTIME *)b)->wType == TIME_MS && untouched(b, 8, sizeof b, 0x93), "cb=%u r=%u", cb, r);
    }
}

TEST(time_basic_systime_large_cb_table) {
    REQUIRE(U(timeGetSystemTime));
    static const UINT cbs[] = { 12, 13, 16, 20, 24, 0x100, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof cbs / sizeof cbs[0]; i++) {
        BYTE b[48]; memset(b, 0x81, sizeof b);
        MMRESULT r = U(timeGetSystemTime)((LPMMTIME)b, cbs[i]);
        CHECKF(r == 0, "cb=%u r=%u", cbs[i], r);
        CHECKF(((MMTIME *)b)->wType == TIME_MS, "cb=%u", cbs[i]);
        CHECKF(untouched(b, 8, sizeof b, 0x81), "cb=%u", cbs[i]);
    }
}

TEST(time_basic_systime_bytes_8_to_11_untouched) {
    REQUIRE(U(timeGetSystemTime));
    /* the union is 8 bytes wide (smpte/midi); TIME_MS writes only u.ms */
    MMTIME mt; memset(&mt, 0xAB, sizeof mt);
    EXPECT_EQ(U(timeGetSystemTime)(&mt, sizeof mt), 0);
    const BYTE *b = (const BYTE *)&mt;
    EXPECT_EQ(b[8], 0xAB); EXPECT_EQ(b[9], 0xAB); EXPECT_EQ(b[10], 0xAB); EXPECT_EQ(b[11], 0xAB);
}

TEST(time_basic_systime_wtype_ignored_table) {
    REQUIRE(U(timeGetSystemTime));
    static const UINT types[] = { 0, TIME_MS, TIME_SAMPLES, TIME_BYTES, TIME_SMPTE, TIME_MIDI, TIME_TICKS,
                                  3, 0x40, 0x80, 0xFFFF, 0x10000, 0x7FFFFFFF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        BYTE b[16]; memset(b, 0x00, sizeof b);
        ((MMTIME *)b)->wType = types[i];
        MMRESULT r = U(timeGetSystemTime)((LPMMTIME)b, 16);
        CHECKF(r == 0, "wType in=0x%x r=%u", types[i], r);
        CHECKF(((MMTIME *)b)->wType == TIME_MS, "wType in=0x%x out=0x%x", types[i], ((MMTIME *)b)->wType);
    }
}

TEST(time_basic_systime_samples_preset_16byte) {
    REQUIRE(U(timeGetSystemTime));
    struct { MMTIME m; DWORD guard; } s; memset(&s, 0x44, sizeof s);
    s.m.wType = TIME_SAMPLES;
    EXPECT_EQ(U(timeGetSystemTime)(&s.m, sizeof s), 0);
    EXPECT_EQ(s.m.wType, TIME_MS);
    EXPECT_EQ(s.guard, 0x44444444);
}

TEST(time_basic_systime_smpte_preset_keeps_tail) {
    REQUIRE(U(timeGetSystemTime));
    MMTIME mt; memset(&mt, 0, sizeof mt);
    mt.wType = TIME_SMPTE;
    mt.u.smpte.hour = 1; mt.u.smpte.min = 2; mt.u.smpte.sec = 3; mt.u.smpte.frame = 4;
    mt.u.smpte.fps = 30; mt.u.smpte.dummy = 0x55; mt.u.smpte.pad[0] = 0x66; mt.u.smpte.pad[1] = 0x77;
    EXPECT_EQ(U(timeGetSystemTime)(&mt, sizeof mt), 0);
    EXPECT_EQ(mt.wType, TIME_MS);
    /* bytes 4..7 (hour,min,sec,frame) now hold ms; fps/dummy/pad (bytes 8..11) are left alone */
    EXPECT_EQ(mt.u.smpte.fps, 30);
    EXPECT_EQ(mt.u.smpte.dummy, 0x55);
    EXPECT_EQ(mt.u.smpte.pad[0], 0x66);
    EXPECT_EQ(mt.u.smpte.pad[1], 0x77);
}

TEST(time_basic_systime_tracks_timegettime) {
    REQUIRE(U(timeGetSystemTime)); REQUIRE(U(timeGetTime));
    int ok = 1; LONG worst = 0;
    for (int i = 0; i < 2000; i++) {
        MMTIME mt;
        DWORD t0 = U(timeGetTime)();
        if (U(timeGetSystemTime)(&mt, sizeof mt) != 0 || mt.wType != TIME_MS) { ok = 0; break; }
        DWORD t1 = U(timeGetTime)();
        if (tb_diff(mt.u.ms, t0) < 0 || tb_diff(t1, mt.u.ms) < 0) { ok = 0; worst = tb_diff(mt.u.ms, t0); break; }
    }
    CHECKF(ok, "ms fell outside [timeGetTime before, after] (%ld)", worst);
}

TEST(time_basic_systime_monotonic) {
    REQUIRE(U(timeGetSystemTime));
    MMTIME mt; DWORD prev; int ok = 1;
    EXPECT_EQ(U(timeGetSystemTime)(&mt, sizeof mt), 0); prev = mt.u.ms;
    for (int i = 0; i < 1000; i++) {
        if (U(timeGetSystemTime)(&mt, sizeof mt) != 0) { ok = 0; break; }
        if (tb_diff(mt.u.ms, prev) < 0) { ok = 0; break; }
        prev = mt.u.ms;
    }
    CHECK(ok);
}

TEST(time_basic_systime_advances_over_sleep) {
    REQUIRE(U(timeGetSystemTime));
    MMTIME a, b;
    EXPECT_EQ(U(timeGetSystemTime)(&a, sizeof a), 0);
    Sleep(50);
    EXPECT_EQ(U(timeGetSystemTime)(&b, sizeof b), 0);
    LONG d = tb_diff(b.u.ms, a.u.ms);
    CHECKF(d >= 30 && d < 2000, "delta=%ld", d);
}

TEST(time_basic_systime_not_wallclock) {
    REQUIRE(U(timeGetSystemTime)); REQUIRE(U(timeGetTime));
    /* despite its name it is the timeGetTime counter, not the time of day */
    MMTIME mt; SYSTEMTIME st;
    DWORD t0 = U(timeGetTime)();
    EXPECT_EQ(U(timeGetSystemTime)(&mt, sizeof mt), 0);
    DWORD t1 = U(timeGetTime)();
    GetLocalTime(&st);
    CHECK(tb_diff(mt.u.ms, t0) >= 0 && tb_diff(t1, mt.u.ms) >= 0);
    t_note("ms=%lu localtime=%02u:%02u:%02u", mt.u.ms, st.wHour, st.wMinute, st.wSecond);
}

TEST(time_basic_systime_winmm_vs_kernel32) {
    REQUIRE(U(timeGetSystemTime));
    REQUIRE(UIN("kernel32.dll", timeGetSystemTime));
    MMTIME a, b;
    EXPECT_EQ(UIN("winmm.dll", timeGetSystemTime)(&a, sizeof a), 0);
    EXPECT_EQ(UIN("kernel32.dll", timeGetSystemTime)(&b, sizeof b), 0);
    EXPECT_EQ(a.wType, TIME_MS); EXPECT_EQ(b.wType, TIME_MS);
    CHECK(tb_diff(b.u.ms, a.u.ms) >= 0);
    EXPECT_EQ(UIN("kernel32.dll", timeGetSystemTime)(NULL, 12), TB_STRUCT);
    EXPECT_EQ(UIN("kernel32.dll", timeGetSystemTime)(&a, 11), TB_STRUCT);
}

TEST(time_basic_systime_preserves_last_error) {
    REQUIRE(U(timeGetSystemTime));
    MMTIME mt;
    SET_ERR(0x1234);
    EXPECT_EQ(U(timeGetSystemTime)(&mt, sizeof mt), 0);
    EXPECT_ERR(0x1234);
    SET_ERR(0x1234);
    EXPECT_EQ(U(timeGetSystemTime)(&mt, 0), TB_STRUCT);
    EXPECT_ERR(0x1234);
    SET_ERR(0x1234);
    EXPECT_EQ(U(timeGetSystemTime)(NULL, 12), TB_STRUCT);
    EXPECT_ERR(0x1234);
}

TEST(time_basic_systime_unaligned) {
    REQUIRE(U(timeGetSystemTime));
    BYTE b[32]; memset(b, 0x2F, sizeof b);
    EXPECT_EQ(U(timeGetSystemTime)((LPMMTIME)(b + 3), 12), 0);
    UINT w; memcpy(&w, b + 3, 4);
    EXPECT_EQ(w, TIME_MS);
    EXPECT_EQ(b[0], 0x2F); EXPECT_EQ(b[1], 0x2F); EXPECT_EQ(b[2], 0x2F);
    CHECK(untouched(b, 11, sizeof b, 0x2F));
}

CHILD(tb_systime_first) {
    MMTIME mt; memset(&mt, 0, sizeof mt);
    if (U(timeGetSystemTime)(NULL, 12) != 129) return 10;
    if (U(timeGetSystemTime)(&mt, 11) != 129) return 11;
    mt.wType = TIME_BYTES;
    if (U(timeGetSystemTime)(&mt, 12) != 0) return 12;
    if (mt.wType != TIME_MS) return 13;
    DWORD tc = GetTickCount();
    LONG d = tb_diff(mt.u.ms, tc);
    if (d < -1000 || d > 1000) return 14;
    return 0;
}
TEST(time_basic_systime_first_call_in_fresh_process) {
    REQUIRE(U(timeGetSystemTime));
    EXPECT_EQ(t_run_child("tb_systime_first", NULL, 20000), 0);
}
