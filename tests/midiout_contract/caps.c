/* midiOutGetDevCapsA (winmmbase !0x7910) / midiOutGetDevCapsW (!0x7820).
 *
 *   cbmoc == 0           -> 0 (nothing read or written, any id)
 *   pmoc NULL            -> 11
 *   uDeviceID: a valid HMIDIOUT goes to the handle's driver, anything else is an id:
 *     id < count or id == 0xFFFFFFFF (the mapper) -> driver MODM_GETDEVCAPS, else 2.
 *   A: the driver fills a local MIDIOUTCAPS2W (0x84 bytes), the name is converted, and min(cbmoc, 100)
 *      bytes of the MIDIOUTCAPS2A image are copied; A masks the id to 32 bits, W does not.
 *   W: the driver writes into the caller's buffer with cbmoc.
 *
 * Values [VM, every configuration]: mapper "Microsoft MIDI Mapper" MOD_MAPPER (5) dwSupport 0x9;
 * device 0 "Microsoft GS Wavetable Synth" MOD_SWSYNTH (7) 32 voices 32 notes chmask 0xFFFF dwSupport 0x1.
 */
#include "moc.h"

#define CAPA(id, p, cb) U(midiOutGetDevCapsA)((UINT_PTR)(id), (p), (cb))
#define CAPW(id, p, cb) U(midiOutGetDevCapsW)((UINT_PTR)(id), (p), (cb))
#define REQ2() do { REQUIRE(U(midiOutGetDevCapsA)); REQUIRE(U(midiOutGetDevCapsW)); } while (0)

static const WCHAR synth_w[] = L"Microsoft GS Wavetable Synth";
static const WCHAR mapper_w[] = L"Microsoft MIDI Mapper";

static int all_bytes(const void *p, size_t n, unsigned char v) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++) if (b[i] != v) return 0;
    return 1;
}

/* ---------------------------------------------------------------- cb 0 / NULL */

TEST(midiout_contract_caps_cb0_null_0_any_id) {
    REQ2();
    static const ULONG_PTR ids[] = { 0, 1, 9, 0xFFFF, 0x10000, 0x12345670, 0xFFFFFFFE, 0xFFFFFFFF, (ULONG_PTR)-1 };
    for (int i = 0; i < NELEM(ids); i++) {
        CHECKF(CAPA(ids[i], NULL, 0) == MMSYSERR_NOERROR, "A id=0x%llx", (unsigned long long)ids[i]);
        CHECKF(CAPW(ids[i], NULL, 0) == MMSYSERR_NOERROR, "W id=0x%llx", (unsigned long long)ids[i]);
    }
}

TEST(midiout_contract_caps_cb0_buffer_untouched) {
    REQ2();
    static const ULONG_PTR ids[] = { 0, 9, 0xFFFFFFFF };
    for (int i = 0; i < NELEM(ids); i++) {
        MIDIOUTCAPS2A a; MIDIOUTCAPS2W w;
        memset(&a, 0xCC, sizeof(a)); memset(&w, 0xCC, sizeof(w));
        EXPECT_EQ(CAPA(ids[i], (MIDIOUTCAPSA *)&a, 0), MMSYSERR_NOERROR);
        EXPECT_EQ(CAPW(ids[i], (MIDIOUTCAPSW *)&w, 0), MMSYSERR_NOERROR);
        CHECKF(all_bytes(&a, sizeof(a), 0xCC), "A id=0x%llx wrote", (unsigned long long)ids[i]);
        CHECKF(all_bytes(&w, sizeof(w), 0xCC), "W id=0x%llx wrote", (unsigned long long)ids[i]);
    }
}

TEST(midiout_contract_caps_null_11_any_id) {
    REQ2();
    static const ULONG_PTR ids[] = { 0, 1, 9, 0xFFFF, 0x10000, 0x12345670, 0xFFFFFFFE, 0xFFFFFFFF, (ULONG_PTR)-1 };
    static const UINT cbs[] = { 1, 2, 8, sizeof(MIDIOUTCAPSA), sizeof(MIDIOUTCAPSW), sizeof(MIDIOUTCAPS2W), 0xFFFFFFFF };
    for (int i = 0; i < NELEM(ids); i++)
        for (int j = 0; j < NELEM(cbs); j++) {
            CHECKF(CAPA(ids[i], NULL, cbs[j]) == MMSYSERR_INVALPARAM, "A id=0x%llx cb=%u", (unsigned long long)ids[i], cbs[j]);
            CHECKF(CAPW(ids[i], NULL, cbs[j]) == MMSYSERR_INVALPARAM, "W id=0x%llx cb=%u", (unsigned long long)ids[i], cbs[j]);
        }
}

/* ---------------------------------------------------------------- bad ids */

TEST(midiout_contract_caps_bad_ids_2_untouched) {
    REQ2();
    for (int i = 0; i < moc_nbadid; i++) {
        MIDIOUTCAPSA a; MIDIOUTCAPSW w;
        memset(&a, 0xCC, sizeof(a)); memset(&w, 0xCC, sizeof(w));
        CHECKF(CAPA(moc_bad_ids[i], &a, sizeof(a)) == MMSYSERR_BADDEVICEID, "A id=0x%llx", (unsigned long long)moc_bad_ids[i]);
        CHECKF(CAPW(moc_bad_ids[i], &w, sizeof(w)) == MMSYSERR_BADDEVICEID, "W id=0x%llx", (unsigned long long)moc_bad_ids[i]);
        CHECK(all_bytes(&a, sizeof(a), 0xCC));
        CHECK(all_bytes(&w, sizeof(w), 0xCC));
    }
}

TEST(midiout_contract_caps_id_count_2) {
    REQ2();
    UINT n = moc_nout();
    MIDIOUTCAPSA a; MIDIOUTCAPSW w;
    for (UINT k = 0; k < 3; k++) {
        CHECKF(CAPA(n + k, &a, sizeof(a)) == MMSYSERR_BADDEVICEID, "A id=%u", n + k);
        CHECKF(CAPW(n + k, &w, sizeof(w)) == MMSYSERR_BADDEVICEID, "W id=%u", n + k);
    }
}

TEST(midiout_contract_caps_id_ffff_not_mapper) {
    REQ2();
    MIDIOUTCAPSA a; MIDIOUTCAPSW w;
    EXPECT_EQ(CAPA(0xFFFF, &a, sizeof(a)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(CAPW(0xFFFF, &w, sizeof(w)), MMSYSERR_BADDEVICEID);
}

/* cb 1 still reaches the id check */
TEST(midiout_contract_caps_tiny_cb_bad_id_2) {
    REQ2();
    MIDIOUTCAPS2W w;
    for (UINT cb = 1; cb < 8; cb++) {
        memset(&w, 0xCC, sizeof(w));
        CHECKF(CAPA(9, (MIDIOUTCAPSA *)&w, cb) == MMSYSERR_BADDEVICEID, "A cb=%u", cb);
        CHECKF(CAPW(9, (MIDIOUTCAPSW *)&w, cb) == MMSYSERR_BADDEVICEID, "W cb=%u", cb);
        CHECK(all_bytes(&w, sizeof(w), 0xCC));
    }
}

/* ---------------------------------------------------------------- values */

TEST(midiout_contract_caps_mapper_w_values) {
    REQ2();
    MIDIOUTCAPSW w; memset(&w, 0xCC, sizeof(w));
    EXPECT_EQ(CAPW(MIDI_MAPPER, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_WSTR(w.szPname, mapper_w);
    EXPECT_EQ(w.wTechnology, MOD_MAPPER);
    EXPECT_EQ(w.dwSupport, MIDICAPS_VOLUME | MIDICAPS_STREAM);
    EXPECT_EQ(w.wChannelMask, 0xFFFF);
    EXPECT_EQ(w.wMid, MM_MICROSOFT);
    EXPECT_EQ(w.wPid, MM_MIDI_MAPPER);
    EXPECT_EQ(w.vDriverVersion, 0x500);
    t_note("mapper caps W: voices=%u notes=%u", w.wVoices, w.wNotes);
}

TEST(midiout_contract_caps_mapper_a_values) {
    REQ2();
    MIDIOUTCAPSA a; memset(&a, 0xCC, sizeof(a));
    EXPECT_EQ(CAPA(MIDI_MAPPER, &a, sizeof(a)), MMSYSERR_NOERROR);
    EXPECT_STR(a.szPname, "Microsoft MIDI Mapper");
    EXPECT_EQ(a.wTechnology, MOD_MAPPER);
    EXPECT_EQ(a.dwSupport, MIDICAPS_VOLUME | MIDICAPS_STREAM);
    EXPECT_EQ(a.wChannelMask, 0xFFFF);
    EXPECT_EQ(a.wMid, MM_MICROSOFT);
    EXPECT_EQ(a.wPid, MM_MIDI_MAPPER);
    EXPECT_EQ(a.vDriverVersion, 0x500);
}

TEST(midiout_contract_caps_synth_w_values) {
    REQ2();
    MIDIOUTCAPSW w; memset(&w, 0xCC, sizeof(w));
    EXPECT_EQ(CAPW(0, &w, sizeof(w)), MMSYSERR_NOERROR);
    EXPECT_WSTR(w.szPname, synth_w);
    EXPECT_EQ(w.wTechnology, MOD_SWSYNTH);
    EXPECT_EQ(w.wVoices, 32);
    EXPECT_EQ(w.wNotes, 32);
    EXPECT_EQ(w.wChannelMask, 0xFFFF);
    EXPECT_EQ(w.dwSupport, MIDICAPS_VOLUME);
    EXPECT_EQ(w.wMid, MM_MICROSOFT);
    EXPECT_EQ(w.wPid, 27);
    EXPECT_EQ(w.vDriverVersion, 0x100);
}

TEST(midiout_contract_caps_synth_a_values) {
    REQ2();
    MIDIOUTCAPSA a; memset(&a, 0xCC, sizeof(a));
    EXPECT_EQ(CAPA(0, &a, sizeof(a)), MMSYSERR_NOERROR);
    EXPECT_STR(a.szPname, "Microsoft GS Wavetable Synth");
    EXPECT_EQ(a.wTechnology, MOD_SWSYNTH);
    EXPECT_EQ(a.wVoices, 32);
    EXPECT_EQ(a.wNotes, 32);
    EXPECT_EQ(a.wChannelMask, 0xFFFF);
    EXPECT_EQ(a.dwSupport, MIDICAPS_VOLUME);
    EXPECT_EQ(a.wMid, MM_MICROSOFT);
    EXPECT_EQ(a.wPid, 27);
    EXPECT_EQ(a.vDriverVersion, 0x100);
}

/* A and W agree field by field for every valid id */
TEST(midiout_contract_caps_a_w_parity) {
    REQ2();
    UINT ids[8]; int n = 0;
    ids[n++] = MIDI_MAPPER;
    for (UINT i = 0; i < moc_nout() && n < 8; i++) ids[n++] = i;
    for (int i = 0; i < n; i++) {
        MIDIOUTCAPSA a; MIDIOUTCAPSW w;
        memset(&a, 0xCC, sizeof(a)); memset(&w, 0xCC, sizeof(w));
        EXPECT_EQ(CAPA(ids[i], &a, sizeof(a)), MMSYSERR_NOERROR);
        EXPECT_EQ(CAPW(ids[i], &w, sizeof(w)), MMSYSERR_NOERROR);
        CHECKF(a.wMid == w.wMid && a.wPid == w.wPid && a.vDriverVersion == w.vDriverVersion, "id=0x%x ids", ids[i]);
        CHECKF(a.wTechnology == w.wTechnology && a.wVoices == w.wVoices && a.wNotes == w.wNotes, "id=0x%x tech", ids[i]);
        CHECKF(a.wChannelMask == w.wChannelMask && a.dwSupport == w.dwSupport, "id=0x%x support", ids[i]);
        char conv[64] = {0};
        WideCharToMultiByte(CP_ACP, 0, w.szPname, -1, conv, sizeof(conv), NULL, NULL);
        CHECKF(!strcmp(conv, a.szPname), "id=0x%x name A '%s' W '%s'", ids[i], a.szPname, conv);
        t_note("midiOut 0x%x '%s' mid=%u pid=%u ver=0x%x tech=%u voices=%u notes=%u chmask=0x%x support=0x%lx",
               ids[i], a.szPname, a.wMid, a.wPid, a.vDriverVersion, a.wTechnology, a.wVoices, a.wNotes, a.wChannelMask, a.dwSupport);
    }
}

/* Repeated calls give identical bytes */
TEST(midiout_contract_caps_stable) {
    REQ2();
    MIDIOUTCAPS2W w1, w2;
    memset(&w1, 0, sizeof(w1)); memset(&w2, 0, sizeof(w2));
    EXPECT_EQ(CAPW(0, (MIDIOUTCAPSW *)&w1, sizeof(w1)), MMSYSERR_NOERROR);
    EXPECT_EQ(CAPW(0, (MIDIOUTCAPSW *)&w2, sizeof(w2)), MMSYSERR_NOERROR);
    CHECK(!memcmp(&w1, &w2, sizeof(w1)));
    memset(&w1, 0, sizeof(w1)); memset(&w2, 0, sizeof(w2));
    EXPECT_EQ(CAPW(MIDI_MAPPER, (MIDIOUTCAPSW *)&w1, sizeof(w1)), MMSYSERR_NOERROR);
    EXPECT_EQ(CAPW(MIDI_MAPPER, (MIDIOUTCAPSW *)&w2, sizeof(w2)), MMSYSERR_NOERROR);
    CHECK(!memcmp(&w1, &w2, sizeof(w1)));
}

/* ---------------------------------------------------------------- A: truncated copy */

/* A copies exactly min(cb, 100) bytes of the MIDIOUTCAPS2A image and leaves the rest of the buffer alone. */
static void a_trunc(UINT id) {
    MIDIOUTCAPS2A full; unsigned char buf[160];
    memset(&full, 0xCC, sizeof(full));
    EXPECT_EQ(CAPA(id, (MIDIOUTCAPSA *)&full, sizeof(full)), MMSYSERR_NOERROR);
    EXPECT_EQ(sizeof(full), 100);
    for (UINT cb = 1; cb <= sizeof(buf); cb++) {
        memset(buf, 0xCC, sizeof(buf));
        MMRESULT r = CAPA(id, (MIDIOUTCAPSA *)buf, cb);
        CHECKF(r == MMSYSERR_NOERROR, "id=0x%x cb=%u r=%u", id, cb, r);
        UINT n = cb < 100 ? cb : 100;
        CHECKF(!memcmp(buf, &full, n), "id=0x%x cb=%u prefix differs", id, cb);
        CHECKF(all_bytes(buf + n, sizeof(buf) - n, 0xCC), "id=0x%x cb=%u wrote past %u", id, cb, n);
    }
}
TEST(midiout_contract_caps_a_truncation_synth) { REQ2(); a_trunc(0); }
TEST(midiout_contract_caps_a_truncation_mapper) { REQ2(); a_trunc(MIDI_MAPPER); }

/* A with sizeof(MIDIOUTCAPSA) (52) fills the classic struct; the 2A tail is the GUIDs */
TEST(midiout_contract_caps_a_2a_guids) {
    REQ2();
    MIDIOUTCAPS2A a; MIDIOUTCAPS2W w;
    memset(&a, 0xCC, sizeof(a)); memset(&w, 0xCC, sizeof(w));
    EXPECT_EQ(sizeof(MIDIOUTCAPSA), 52);
    EXPECT_EQ(sizeof(MIDIOUTCAPS2W), 0x84);
    EXPECT_EQ(CAPA(0, (MIDIOUTCAPSA *)&a, sizeof(a)), MMSYSERR_NOERROR);
    EXPECT_EQ(CAPW(0, (MIDIOUTCAPSW *)&w, sizeof(w)), MMSYSERR_NOERROR);
    CHECK(!memcmp(&a.ManufacturerGuid, &w.ManufacturerGuid, sizeof(GUID)));
    CHECK(!memcmp(&a.ProductGuid, &w.ProductGuid, sizeof(GUID)));
    CHECK(!memcmp(&a.NameGuid, &w.NameGuid, sizeof(GUID)));
    t_note("synth ManufacturerGuid %08lx ProductGuid %08lx NameGuid %08lx",
           w.ManufacturerGuid.Data1, w.ProductGuid.Data1, w.NameGuid.Data1);
}

/* ---------------------------------------------------------------- W: driver-side copy */

static void w_trunc(UINT id) {
    MIDIOUTCAPS2W full; unsigned char buf[200];
    memset(&full, 0xCC, sizeof(full));
    EXPECT_EQ(CAPW(id, (MIDIOUTCAPSW *)&full, sizeof(full)), MMSYSERR_NOERROR);
    static const UINT cbs[] = { 1, 2, 3, 4, 7, 8, 9, 40, 72, 73, 80, 83, 84, 85, 100, 131, 132, 133, 200 };
    for (int i = 0; i < NELEM(cbs); i++) {
        UINT cb = cbs[i];
        memset(buf, 0xCC, sizeof(buf));
        MMRESULT r = CAPW(id, (MIDIOUTCAPSW *)buf, cb);
        CHECKF(r == MMSYSERR_NOERROR, "id=0x%x cb=%u r=%u", id, cb, r);
        UINT n = cb < sizeof(full) ? cb : sizeof(full);
        CHECKF(!memcmp(buf, &full, n), "id=0x%x cb=%u prefix differs", id, cb);
        CHECKF(all_bytes(buf + n, sizeof(buf) - n, 0xCC), "id=0x%x cb=%u wrote past %u", id, cb, n);
    }
}
TEST(midiout_contract_caps_w_truncation_synth) { REQ2(); w_trunc(0); }
TEST(midiout_contract_caps_w_truncation_mapper) { REQ2(); w_trunc(MIDI_MAPPER); }

/* ---------------------------------------------------------------- 64-bit ids */

/* A truncates the id to 32 bits before the lookup; W compares the full UINT_PTR. */
TEST(midiout_contract_caps_high_id_bits) {
    REQ2();
    MIDIOUTCAPSA a; MIDIOUTCAPSW w;
    MMRESULT ra = CAPA(0x100000000ull, &a, sizeof(a));
    MMRESULT rw = CAPW(0x100000000ull, &w, sizeof(w));
    t_note("caps id 0x100000000: A=%u W=%u", ra, rw);
    EXPECT_EQ(rw, MMSYSERR_BADDEVICEID);
    ra = CAPA((ULONG_PTR)-1, &a, sizeof(a));
    rw = CAPW((ULONG_PTR)-1, &w, sizeof(w));
    t_note("caps id (UINT_PTR)-1: A=%u W=%u", ra, rw);
    EXPECT_EQ(rw, MMSYSERR_BADDEVICEID);
}

/* ---------------------------------------------------------------- handles as ids */

TEST(midiout_contract_caps_mmio_handle_2) {
    REQ2();
    HMMIO m = moc_mmio_open();
    if (!m) { t_skip("no mmio handle"); return; }
    MIDIOUTCAPSA a; MIDIOUTCAPSW w;
    EXPECT_EQ(CAPA(m, &a, sizeof(a)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(CAPW(m, &w, sizeof(w)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(CAPA(m, NULL, 4), MMSYSERR_INVALPARAM);
    EXPECT_EQ(CAPW(m, &w, 0), MMSYSERR_NOERROR);
    moc_mmio_close(m);
}

TEST(midiout_contract_caps_live_handle_matches_id) {
    REQ2(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(0);
    if (!h) return;
    MIDIOUTCAPSW byid, byh; MIDIOUTCAPSA ah;
    memset(&byid, 0, sizeof(byid)); memset(&byh, 0, sizeof(byh));
    EXPECT_EQ(CAPW(0, &byid, sizeof(byid)), MMSYSERR_NOERROR);
    EXPECT_EQ(CAPW(h, &byh, sizeof(byh)), MMSYSERR_NOERROR);
    CHECK(!memcmp(&byid, &byh, sizeof(byid)));
    EXPECT_EQ(CAPA(h, &ah, sizeof(ah)), MMSYSERR_NOERROR);
    EXPECT_STR(ah.szPname, "Microsoft GS Wavetable Synth");
    EXPECT_EQ(CAPA(h, NULL, sizeof(ah)), MMSYSERR_INVALPARAM);
    EXPECT_EQ(CAPA(h, NULL, 0), MMSYSERR_NOERROR);
    moc_close(h);
}

TEST(midiout_contract_caps_live_mapper_handle) {
    REQ2(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(MIDI_MAPPER);
    if (!h) return;
    MIDIOUTCAPSW w; memset(&w, 0, sizeof(w));
    MMRESULT r = CAPW(h, &w, sizeof(w));
    t_note("caps by mapper handle: r=%u name='%ls' tech=%u", r, w.szPname, w.wTechnology);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    moc_close(h);
}

/* A closed handle value is just an unknown id */
TEST(midiout_contract_caps_closed_handle_2) {
    REQ2(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_closed_handle();
    if (!h) return;
    MIDIOUTCAPSA a; MIDIOUTCAPSW w;
    EXPECT_EQ(CAPA(h, &a, sizeof(a)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(CAPW(h, &w, sizeof(w)), MMSYSERR_BADDEVICEID);
}

/* winmm exports land on the same implementation */
TEST(midiout_contract_caps_winmm_winmmbase_agree) {
    __typeof__(&midiOutGetDevCapsW) a = UIN("winmm.dll", midiOutGetDevCapsW);
    __typeof__(&midiOutGetDevCapsW) b = UIN("winmmbase.dll", midiOutGetDevCapsW);
    REQUIRE(a); REQUIRE(b);
    MIDIOUTCAPSW w1, w2;
    memset(&w1, 0, sizeof(w1)); memset(&w2, 0, sizeof(w2));
    EXPECT_EQ(a(0, &w1, sizeof(w1)), MMSYSERR_NOERROR);
    EXPECT_EQ(b(0, &w2, sizeof(w2)), MMSYSERR_NOERROR);
    CHECK(!memcmp(&w1, &w2, sizeof(w1)));
    EXPECT_EQ(a(9, &w1, sizeof(w1)), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(a(0, NULL, 4), MMSYSERR_INVALPARAM);
}
