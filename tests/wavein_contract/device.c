/* Device half of the waveIn handle contract: a real HWAVEIN (CALLBACK_NULL, nothing recorded beyond a
 * few hundred milliseconds of silence) through Start/Stop/Reset/GetPosition/GetID/Message/Close, and the
 * same handle after close. Runs only when waveInGetNumDevs() > 0.
 */
#include "wic.h"

static char g_buf[8192];

TEST(wavein_contract_dev_inventory) {
    REQUIRE(U(waveInGetNumDevs)); REQUIRE(U(waveInGetDevCapsW));
    UINT n = wic_nin();
    t_note("waveIn devices=%u waveOut devices=%u", n, wic_nout());
    for (UINT i = 0; i < n && i < 8; i++) {
        WAVEINCAPSW c; memset(&c, 0, sizeof(c));
        MMRESULT r = U(waveInGetDevCapsW)(i, &c, sizeof(c));
        char nm[64]; WideCharToMultiByte(CP_UTF8, 0, c.szPname, -1, nm, sizeof(nm), NULL, NULL);
        t_note("waveIn %u: r=%u \"%s\"", i, r, nm);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
    }
    /* the count is stable across calls */
    EXPECT_EQ(U(waveInGetNumDevs)(), n);
}

/* Start/Stop/Reset on an idle handle with no buffers -> 0, repeatable in any order. */
TEST(wavein_contract_dev_idle_start_stop_reset) {
    REQUIRE(U(waveInStart)); REQUIRE(U(waveInStop)); REQUIRE(U(waveInReset)); REQUIRE(U(waveInClose));
    HWAVEIN h = wic_open_any(); if (!h) return;
    EXPECT_EQ(U(waveInStop)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInReset)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInStart)(h), MMSYSERR_NOERROR);
    MMRESULT r = U(waveInStart)(h);
    t_note("second Start -> %u", r);
    EXPECT_EQ(U(waveInStop)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInStop)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInReset)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInReset)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* Close while started (no buffers) succeeds. */
TEST(wavein_contract_dev_close_while_started) {
    REQUIRE(U(waveInStart)); REQUIRE(U(waveInClose));
    HWAVEIN h = wic_open_any(); if (!h) return;
    EXPECT_EQ(U(waveInStart)(h), MMSYSERR_NOERROR);
    MMRESULT r = U(waveInClose)(h);
    t_note("close while started -> %u", r);
    if (r != MMSYSERR_NOERROR) { U(waveInReset)(h); EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR); }
    else CHECK(1);
}

/* Double close: the second is 5. */
TEST(wavein_contract_dev_double_close) {
    REQUIRE(U(waveInClose));
    HWAVEIN h = wic_open_any(); if (!h) return;
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_INVALHANDLE);
}

/* GetPosition on an idle handle. */
TEST(wavein_contract_dev_getpos_idle) {
    REQUIRE(U(waveInGetPosition)); REQUIRE(U(waveInClose));
    HWAVEIN h = wic_open_any(); if (!h) return;
    MMTIME t;
    memset(&t, 0xA5, sizeof(t)); t.wType = TIME_BYTES;
    EXPECT_EQ(U(waveInGetPosition)(h, &t, sizeof(t)), MMSYSERR_NOERROR);
    EXPECT_EQ(t.wType, TIME_BYTES);
    EXPECT_EQ(t.u.cb, 0);
    memset(&t, 0xA5, sizeof(t)); t.wType = TIME_SAMPLES;
    EXPECT_EQ(U(waveInGetPosition)(h, &t, sizeof(t)), MMSYSERR_NOERROR);
    EXPECT_EQ(t.wType, TIME_SAMPLES);
    EXPECT_EQ(t.u.sample, 0);
    EXPECT_EQ(U(waveInGetPosition)(h, NULL, sizeof(t)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* Unsupported time formats come back rewritten to TIME_BYTES. */
TEST(wavein_contract_dev_getpos_types) {
    REQUIRE(U(waveInGetPosition)); REQUIRE(U(waveInClose));
    HWAVEIN h = wic_open_any(); if (!h) return;
    static const UINT types[] = { TIME_MS, TIME_SAMPLES, TIME_BYTES, TIME_SMPTE, TIME_MIDI, TIME_TICKS, 0, 0x77 };
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        MMTIME t; memset(&t, 0, sizeof(t)); t.wType = types[i];
        MMRESULT r = U(waveInGetPosition)(h, &t, sizeof(t));
        t_note("getpos type=0x%x -> %u wType=0x%x val=%lu", types[i], r, t.wType, t.u.cb);
        EXPECT_EQ(r, MMSYSERR_NOERROR);
        if (types[i] == TIME_SAMPLES) EXPECT_EQ(t.wType, TIME_SAMPLES);
        else EXPECT_EQ(t.wType, TIME_BYTES);
    }
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* The size argument goes to the driver: record short sizes. */
TEST(wavein_contract_dev_getpos_short_cb) {
    REQUIRE(U(waveInGetPosition)); REQUIRE(U(waveInClose));
    HWAVEIN h = wic_open_any(); if (!h) return;
    static const UINT cbs[] = { 0, 1, 4, 8, sizeof(MMTIME) - 1, sizeof(MMTIME), sizeof(MMTIME) + 8 };
    for (unsigned i = 0; i < sizeof(cbs) / sizeof(cbs[0]); i++) {
        MMTIME t[2]; memset(t, 0xA5, sizeof(t)); t[0].wType = TIME_BYTES;
        MMRESULT r = U(waveInGetPosition)(h, t, cbs[i]);
        t_note("getpos cb=%u -> %u wType=0x%x", cbs[i], r, t[0].wType);
        if (cbs[i] >= sizeof(MMTIME)) EXPECT_EQ(r, MMSYSERR_NOERROR);
        CHECK(((unsigned char *)t)[sizeof(t) - 1] == 0xA5);
    }
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* Position advances while started with a buffer queued, Reset returns it to 0. */
TEST(wavein_contract_dev_getpos_after_reset) {
    REQUIRE(U(waveInGetPosition)); REQUIRE(U(waveInStart)); REQUIRE(U(waveInReset));
    REQUIRE(U(waveInPrepareHeader)); REQUIRE(U(waveInUnprepareHeader)); REQUIRE(U(waveInAddBuffer));
    HWAVEIN h = wic_open_any(); if (!h) return;
    WAVEHDR w; wic_hdr(&w, g_buf, sizeof(g_buf), 0);   /* 8192 bytes = ~93 ms at 44.1k/16/1 */
    EXPECT_EQ(U(waveInPrepareHeader)(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInAddBuffer)(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInStart)(h), MMSYSERR_NOERROR);
    for (int i = 0; i < 300 && !(w.dwFlags & WHDR_DONE); i++) Sleep(10);
    MMTIME t; t.wType = TIME_BYTES;
    EXPECT_EQ(U(waveInGetPosition)(h, &t, sizeof(t)), MMSYSERR_NOERROR);
    t_note("after one buffer: flags=0x%lx rec=%lu pos=%lu", w.dwFlags, w.dwBytesRecorded, t.u.cb);
    EXPECT_EQ(U(waveInReset)(h), MMSYSERR_NOERROR);
    for (int i = 0; i < 100 && (w.dwFlags & WHDR_INQUEUE); i++) Sleep(10);
    CHECK(!(w.dwFlags & WHDR_INQUEUE));
    CHECK(w.dwFlags & WHDR_DONE);
    t.wType = TIME_BYTES;
    EXPECT_EQ(U(waveInGetPosition)(h, &t, sizeof(t)), MMSYSERR_NOERROR);
    EXPECT_EQ(t.u.cb, 0);
    EXPECT_EQ(U(waveInUnprepareHeader)(h, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
}

/* GetID returns the id given to waveInOpen (no driver call). */
TEST(wavein_contract_dev_getid_values) {
    REQUIRE(U(waveInGetID)); REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    HWAVEIN h = NULL;
    EXPECT_EQ(wic_open(&h, WAVE_MAPPER), MMSYSERR_NOERROR);
    if (h) {
        UINT id = 0xCAFE;
        EXPECT_EQ(U(waveInGetID)(h, &id), MMSYSERR_NOERROR);
        EXPECT_EQ(id, WAVE_MAPPER);
        EXPECT_EQ(U(waveInGetID)(h, NULL), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
        id = 0xCAFE;
        EXPECT_EQ(U(waveInGetID)(h, &id), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(id, 0xCAFE);
    }
    h = NULL;
    EXPECT_EQ(wic_open(&h, 0), MMSYSERR_NOERROR);
    if (h) {
        UINT id = 0xCAFE;
        EXPECT_EQ(U(waveInGetID)(h, &id), MMSYSERR_NOERROR);
        EXPECT_EQ(id, 0);
        EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
    }
}

/* waveInMessage on the device id and through the handle. */
TEST(wavein_contract_dev_message_interface_size) {
    REQUIRE(U(waveInMessage));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    DWORD sz = 0;
    MMRESULT r = U(waveInMessage)((HWAVEIN)0, DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0);
    t_note("id0 QUERYDEVICEINTERFACESIZE -> %u size=%lu", r, sz);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    CHECK(sz >= 2 && sz % 2 == 0 && sz < 4096);
    if (r || sz < 2 || sz >= 4096) return;
    WCHAR buf[2048]; memset(buf, 0xA5, sizeof(buf));
    r = U(waveInMessage)((HWAVEIN)0, DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)buf, sz);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    EXPECT_EQ(lstrlenW(buf) * 2 + 2, sz);
    char nm[256]; WideCharToMultiByte(CP_UTF8, 0, buf, -1, nm, sizeof(nm), NULL, NULL);
    t_note("interface \"%s\"", nm);
    /* a too-small buffer */
    memset(buf, 0xA5, sizeof(buf));
    r = U(waveInMessage)((HWAVEIN)0, DRV_QUERYDEVICEINTERFACE, (DWORD_PTR)buf, sz - 2);
    t_note("QUERYDEVICEINTERFACE short -> %u", r);
    EXPECT_EQ(r, MMSYSERR_MOREDATA);
    r = U(waveInMessage)((HWAVEIN)0, DRV_QUERYDEVICEINTERFACESIZE, 0, 0);
    EXPECT_EQ(r, MMSYSERR_INVALPARAM);
}

TEST(wavein_contract_dev_message_on_handle) {
    REQUIRE(U(waveInMessage)); REQUIRE(U(waveInClose));
    HWAVEIN h = wic_open_any(); if (!h) return;
    DWORD sz = 0;
    MMRESULT r = U(waveInMessage)(h, DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0);
    t_note("handle QUERYDEVICEINTERFACESIZE -> %u size=%lu", r, sz);
    r = U(waveInMessage)(h, 0x4000, 0, 0);
    t_note("handle user msg 0x4000 -> %u", r);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInMessage)(h, DRV_QUERYDEVICEINTERFACESIZE, (DWORD_PTR)&sz, 0), MMSYSERR_BADDEVICEID);
}

TEST(wavein_contract_dev_message_mapper_preferred) {
    REQUIRE(U(waveInMessage));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    DWORD id = 0xCAFE, fl = 0xCAFE;
    MMRESULT r = U(waveInMessage)((HWAVEIN)(ULONG_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&id, (DWORD_PTR)&fl);
    t_note("PREFERRED_GET -> %u id=0x%lx flags=0x%lx", r, id, fl);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    CHECK(id < wic_nin());
    EXPECT_EQ(U(waveInMessage)((HWAVEIN)(ULONG_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, 0, (DWORD_PTR)&fl), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(waveInMessage)((HWAVEIN)(ULONG_PTR)WAVE_MAPPER, DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&id, 0), MMSYSERR_INVALPARAM);
    id = 0xCAFE; fl = 0xCAFE;
    r = U(waveInMessage)((HWAVEIN)(ULONG_PTR)WAVE_MAPPER, DRVM_MAPPER_CONSOLEVOICECOM_GET, (DWORD_PTR)&id, (DWORD_PTR)&fl);
    t_note("CONSOLEVOICECOM_GET -> %u id=0x%lx flags=0x%lx", r, id, fl);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    /* preferred-device messages are mapper-only: a device id is answered 11 by the internal handler */
    r = U(waveInMessage)((HWAVEIN)0, DRVM_MAPPER_PREFERRED_GET, (DWORD_PTR)&id, (DWORD_PTR)&fl);
    t_note("PREFERRED_GET on id0 -> %u", r);
}

/* A closed HWAVEIN is rejected by every entry point. */
TEST(wavein_contract_dev_closed_handle_everywhere) {
    REQUIRE(U(waveInStart)); REQUIRE(U(waveInStop)); REQUIRE(U(waveInReset)); REQUIRE(U(waveInClose));
    REQUIRE(U(waveInGetPosition)); REQUIRE(U(waveInGetID)); REQUIRE(U(waveInMessage));
    REQUIRE(U(waveInPrepareHeader)); REQUIRE(U(waveInUnprepareHeader)); REQUIRE(U(waveInAddBuffer));
    REQUIRE(U(waveInGetDevCapsW));
    HWAVEIN h = wic_closed_handle(); if (!h) return;
    EXPECT_EQ(U(waveInStart)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInStop)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInReset)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_INVALHANDLE);
    MMTIME t; t.wType = TIME_BYTES;
    EXPECT_EQ(U(waveInGetPosition)(h, &t, sizeof(t)), MMSYSERR_INVALHANDLE);
    UINT id = 1;
    EXPECT_EQ(U(waveInGetID)(h, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInMessage)(h, DRV_QUERYDEVICEINTERFACESIZE, 0, 0), MMSYSERR_BADDEVICEID);
    WAVEHDR w; wic_hdr(&w, g_buf, 64, 0);
    EXPECT_EQ(U(waveInPrepareHeader)(h, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    w.dwFlags = WHDR_PREPARED;
    EXPECT_EQ(U(waveInAddBuffer)(h, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(waveInUnprepareHeader)(h, &w, sizeof(w)), MMSYSERR_INVALHANDLE);
    WAVEINCAPSW c;
    EXPECT_EQ(U(waveInGetDevCapsW)((UINT_PTR)h, &c, sizeof(c)), MMSYSERR_BADDEVICEID);
}

/* A full minimal lifecycle with every primary export on one handle. */
TEST(wavein_contract_dev_full_lifecycle) {
    REQUIRE(U(waveInOpen)); REQUIRE(U(waveInClose)); REQUIRE(U(waveInStart)); REQUIRE(U(waveInStop)); REQUIRE(U(waveInReset));
    REQUIRE(U(waveInPrepareHeader)); REQUIRE(U(waveInUnprepareHeader)); REQUIRE(U(waveInAddBuffer));
    REQUIRE(U(waveInGetPosition)); REQUIRE(U(waveInGetID)); REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveInGetDevCapsA));
    if (!wic_nin()) { t_skip("no waveIn device"); return; }
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    WAVEFORMATEX f; wic_pcm(&f, 8000, 8, 1);
    HWAVEIN h = NULL;
    EXPECT_EQ(U(waveInOpen)(&h, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT), MMSYSERR_NOERROR);
    if (!h) { CloseHandle(ev); return; }
    WaitForSingleObject(ev, 1000);
    WAVEINCAPSW cw; WAVEINCAPSA ca;
    EXPECT_EQ(U(waveInGetDevCapsW)((UINT_PTR)h, &cw, sizeof(cw)), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInGetDevCapsA)((UINT_PTR)h, &ca, sizeof(ca)), MMSYSERR_NOERROR);
    WAVEHDR w[2];
    for (int i = 0; i < 2; i++) {
        wic_hdr(&w[i], g_buf + i * 800, 800, 0);    /* 100 ms each at 8 kHz/8/1 */
        EXPECT_EQ(U(waveInPrepareHeader)(h, &w[i], sizeof(w[i])), MMSYSERR_NOERROR);
        EXPECT_EQ(U(waveInAddBuffer)(h, &w[i], sizeof(w[i])), MMSYSERR_NOERROR);
    }
    EXPECT_EQ(U(waveInStart)(h), MMSYSERR_NOERROR);
    DWORD t0 = GetTickCount();
    while (!(w[1].dwFlags & WHDR_DONE) && GetTickCount() - t0 < 3000) WaitForSingleObject(ev, 100);
    CHECK(w[0].dwFlags & WHDR_DONE);
    CHECK(w[1].dwFlags & WHDR_DONE);
    t_note("rec0=%lu rec1=%lu ms=%lu", w[0].dwBytesRecorded, w[1].dwBytesRecorded, GetTickCount() - t0);
    MMTIME t; t.wType = TIME_BYTES;
    EXPECT_EQ(U(waveInGetPosition)(h, &t, sizeof(t)), MMSYSERR_NOERROR);
    CHECK(t.u.cb >= w[0].dwBytesRecorded);
    EXPECT_EQ(U(waveInStop)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(waveInReset)(h), MMSYSERR_NOERROR);
    for (int i = 0; i < 2; i++) {
        EXPECT_EQ(U(waveInUnprepareHeader)(h, &w[i], sizeof(w[i])), MMSYSERR_NOERROR);
        EXPECT_EQ(w[i].dwFlags & WHDR_PREPARED, 0);
    }
    UINT id = 7;
    EXPECT_EQ(U(waveInGetID)(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, WAVE_MAPPER);
    EXPECT_EQ(U(waveInClose)(h), MMSYSERR_NOERROR);
    CloseHandle(ev);
}
