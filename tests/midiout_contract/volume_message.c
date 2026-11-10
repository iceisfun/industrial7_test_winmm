/* midiOutGetVolume (!0x7af0), midiOutSetVolume (!0x7c10), midiOutMessage (!0x7760).
 *
 * These take an HMIDIOUT that may also be a device id: a valid midiOut/stream handle goes to its driver,
 * anything else goes to midiIDMessage: id < count or id == 0xFFFFFFFF -> the driver (DRV_QUERY* and
 * DRVM_MAPPER_* are answered inside winmm), a value that is a valid midiOut handle -> driver, else 2.
 * So a foreign handle (mmio) or a closed handle answers 2 (BADDEVICEID), not 5.
 *   GetVolume: pdwVolume NULL -> 11 first.
 *   SetVolume: no pointer; only ever called here with ids that do not exist, or as a write-back of the
 *   value just read.
 */
#include "moc.h"

#define GV(h, p) U(midiOutGetVolume)((HMIDIOUT)(ULONG_PTR)(h), (p))
#define SV(h, v) U(midiOutSetVolume)((HMIDIOUT)(ULONG_PTR)(h), (v))
#define MSG(h, m, a, b) U(midiOutMessage)((HMIDIOUT)(ULONG_PTR)(h), (m), (DWORD_PTR)(a), (DWORD_PTR)(b))

/* ================================================================ GetVolume */

TEST(midiout_contract_getvolume_null_11_any) {
    REQUIRE(U(midiOutGetVolume));
    static const ULONG_PTR v[] = { 0, 1, 9, 0xFFFF, 0x10000, 0x12345670, 0xFFFFFFFE, 0xFFFFFFFF, (ULONG_PTR)-1 };
    for (int i = 0; i < NELEM(v); i++)
        CHECKF(GV(v[i], NULL) == MMSYSERR_INVALPARAM, "h=0x%llx", (unsigned long long)v[i]);
}

TEST(midiout_contract_getvolume_bad_ids_2_untouched) {
    REQUIRE(U(midiOutGetVolume));
    for (int i = 0; i < moc_nbadid; i++) {
        DWORD vol = 0xA5A5A5A5;
        CHECKF(GV(moc_bad_ids[i], &vol) == MMSYSERR_BADDEVICEID, "id=0x%llx", (unsigned long long)moc_bad_ids[i]);
        CHECKF(vol == 0xA5A5A5A5, "id=0x%llx wrote", (unsigned long long)moc_bad_ids[i]);
    }
    DWORD vol = 0;
    EXPECT_EQ(GV(moc_nout(), &vol), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(GV((ULONG_PTR)-1, &vol), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(GV(0x100000000ull, &vol), MMSYSERR_BADDEVICEID);
}

/* By id, with no handle open in this process, the synth answers MMSYSERR_NODRIVER (no driver instance yet);
 * the mapper id answers NOTSUPPORTED [VM]. */
TEST(midiout_contract_getvolume_id0_synth) {
    REQUIRE(U(midiOutGetVolume));
    DWORD vol = 0xA5A5A5A5;
    MMRESULT r = GV(0, &vol);
    t_note("midiOutGetVolume(id 0) = %u vol=0x%08lx", r, vol);
    EXPECT_EQ(r, MMSYSERR_NODRIVER);
    EXPECT_EQ(vol, 0xA5A5A5A5);
}

TEST(midiout_contract_getvolume_mapper) {
    REQUIRE(U(midiOutGetVolume));
    DWORD vol = 0xA5A5A5A5;
    MMRESULT r = GV(MIDI_MAPPER, &vol);
    t_note("midiOutGetVolume(MIDI_MAPPER) = %u vol=0x%08lx", r, vol);
    EXPECT_EQ(r, MMSYSERR_NOTSUPPORTED);
    EXPECT_EQ(vol, 0xA5A5A5A5);
}

/* while a synth handle is open the id path reaches the same driver instance; after the close it is gone */
TEST(midiout_contract_getvolume_id0_follows_open_handle) {
    REQUIRE(U(midiOutGetVolume)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(0);
    if (!h) return;
    DWORD v = 0xA5A5A5A5;
    EXPECT_EQ(GV(0, &v), MMSYSERR_NOERROR);
    t_note("synth volume by id while open 0x%08lx", v);
    moc_close(h);
    v = 0xA5A5A5A5;
    EXPECT_EQ(GV(0, &v), MMSYSERR_NODRIVER);
    EXPECT_EQ(v, 0xA5A5A5A5);
}

TEST(midiout_contract_getvolume_mmio_handle_2) {
    REQUIRE(U(midiOutGetVolume));
    HMMIO m = moc_mmio_open();
    if (!m) { t_skip("no mmio handle"); return; }
    DWORD vol = 0xA5A5A5A5;
    EXPECT_EQ(GV(m, &vol), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(vol, 0xA5A5A5A5);
    EXPECT_EQ(GV(m, NULL), MMSYSERR_INVALPARAM);
    moc_mmio_close(m);
}

TEST(midiout_contract_getvolume_closed_handle_2) {
    REQUIRE(U(midiOutGetVolume)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_closed_handle();
    if (!h) return;
    DWORD vol = 0;
    EXPECT_EQ(GV(h, &vol), MMSYSERR_BADDEVICEID);
}

TEST(midiout_contract_getvolume_live_handle_matches_id) {
    REQUIRE(U(midiOutGetVolume)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(0);
    if (!h) return;
    DWORD vh = 0xA5A5A5A5, vi = 0x5A5A5A5A;
    EXPECT_EQ(GV(h, &vh), MMSYSERR_NOERROR);
    EXPECT_EQ(GV(0, &vi), MMSYSERR_NOERROR);
    t_note("synth volume by handle 0x%08lx by id 0x%08lx", vh, vi);
    EXPECT_EQ(vh, vi);
    EXPECT_EQ(GV(h, NULL), MMSYSERR_INVALPARAM);
    moc_close(h);
}

TEST(midiout_contract_getvolume_live_mapper_handle) {
    REQUIRE(U(midiOutGetVolume)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(MIDI_MAPPER);
    if (!h) return;
    DWORD v = 0xA5A5A5A5;
    MMRESULT r = GV(h, &v);
    t_note("mapper handle volume r=%u v=0x%08lx", r, v);
    EXPECT_EQ(r, MMSYSERR_NOTSUPPORTED);   /* despite MIDICAPS_VOLUME in the mapper caps [VM] */
    EXPECT_EQ(v, 0xA5A5A5A5);
    moc_close(h);
}

/* ================================================================ SetVolume */

TEST(midiout_contract_setvolume_bad_ids_2) {
    REQUIRE(U(midiOutSetVolume));
    static const DWORD vols[] = { 0, 0xFFFF, 0xFFFFFFFF };
    for (int i = 0; i < moc_nbadid; i++)
        for (int j = 0; j < NELEM(vols); j++)
            CHECKF(SV(moc_bad_ids[i], vols[j]) == MMSYSERR_BADDEVICEID, "id=0x%llx", (unsigned long long)moc_bad_ids[i]);
    EXPECT_EQ(SV(moc_nout(), 0xFFFFFFFF), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(SV((ULONG_PTR)-1, 0xFFFFFFFF), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(SV(0x100000000ull, 0xFFFFFFFF), MMSYSERR_BADDEVICEID);
}

TEST(midiout_contract_setvolume_mmio_handle_2) {
    REQUIRE(U(midiOutSetVolume));
    HMMIO m = moc_mmio_open();
    if (!m) { t_skip("no mmio handle"); return; }
    EXPECT_EQ(SV(m, 0xFFFFFFFF), MMSYSERR_BADDEVICEID);
    moc_mmio_close(m);
}

/* By id with no handle open: NODRIVER, like GetVolume (nothing changes). Write-back of the read value while
 * a handle is open: 0 and the value reads back unchanged. */
TEST(midiout_contract_setvolume_id0_writeback) {
    REQUIRE(U(midiOutGetVolume)); REQUIRE(U(midiOutSetVolume)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(0);
    if (!h) return;
    DWORD v = 0;
    EXPECT_EQ(GV(0, &v), MMSYSERR_NOERROR);
    EXPECT_EQ(SV(0, v), MMSYSERR_NOERROR);
    DWORD v2 = ~v;
    EXPECT_EQ(GV(0, &v2), MMSYSERR_NOERROR);
    EXPECT_EQ(v2, v);
    moc_close(h);
    EXPECT_EQ(SV(0, v), MMSYSERR_NODRIVER);
}

TEST(midiout_contract_setvolume_mapper_8) {
    REQUIRE(U(midiOutGetVolume)); REQUIRE(U(midiOutSetVolume));
    DWORD v = 0;
    if (GV(MIDI_MAPPER, &v) != MMSYSERR_NOTSUPPORTED) { t_skip("mapper volume readable"); return; }
    /* the mapper refuses volume by id, so a set cannot change anything */
    EXPECT_EQ(SV(MIDI_MAPPER, 0xFFFFFFFF), MMSYSERR_NOTSUPPORTED);
}

TEST(midiout_contract_setvolume_live_handle_writeback) {
    REQUIRE(U(midiOutGetVolume)); REQUIRE(U(midiOutSetVolume)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(0);
    if (!h) return;
    DWORD v = 0;
    if (GV(h, &v) == MMSYSERR_NOERROR) {
        EXPECT_EQ(SV(h, v), MMSYSERR_NOERROR);
        DWORD v2 = ~v;
        EXPECT_EQ(GV(h, &v2), MMSYSERR_NOERROR);
        EXPECT_EQ(v2, v);
    }
    moc_close(h);
}

TEST(midiout_contract_setvolume_closed_handle_2) {
    REQUIRE(U(midiOutSetVolume)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_closed_handle();
    if (!h) return;
    EXPECT_EQ(SV(h, 0xFFFFFFFF), MMSYSERR_BADDEVICEID);
}

/* ================================================================ midiOutMessage */

TEST(midiout_contract_message_bad_ids_2) {
    REQUIRE(U(midiOutMessage));
    static const UINT msgs[] = { 0, MODM_GETNUMDEVS, MODM_GETDEVCAPS, MODM_GETVOLUME, DRV_QUERYFUNCTIONINSTANCEIDSIZE,
                                 DRV_QUERYDEVICEINTERFACESIZE, DRVM_MAPPER_PREFERRED_GET, 0x7FFF, 0xFFFFFFFF };
    DWORD a = 0, b = 0;
    for (int i = 0; i < moc_nbadid; i++)
        for (int j = 0; j < NELEM(msgs); j++)
            CHECKF(MSG(moc_bad_ids[i], msgs[j], &a, &b) == MMSYSERR_BADDEVICEID, "id=0x%llx msg=0x%x", (unsigned long long)moc_bad_ids[i], msgs[j]);
    EXPECT_EQ(MSG(moc_nout(), DRV_QUERYFUNCTIONINSTANCEIDSIZE, &a, 0), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(MSG((ULONG_PTR)-1, DRV_QUERYFUNCTIONINSTANCEIDSIZE, &a, 0), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(MSG(9, 0, 0, 0), MMSYSERR_BADDEVICEID);
}

TEST(midiout_contract_message_mmio_handle_2) {
    REQUIRE(U(midiOutMessage));
    HMMIO m = moc_mmio_open();
    if (!m) { t_skip("no mmio handle"); return; }
    DWORD sz = 0;
    EXPECT_EQ(MSG(m, DRV_QUERYFUNCTIONINSTANCEIDSIZE, &sz, 0), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(MSG(m, MODM_GETVOLUME, &sz, 0), MMSYSERR_BADDEVICEID);
    moc_mmio_close(m);
}

/* The synth has no function instance id: size 2 (an empty wide string) [VM] */
TEST(midiout_contract_message_funcinstid_size_id0) {
    REQUIRE(U(midiOutMessage));
    DWORD sz = 0xA5A5A5A5;
    EXPECT_EQ(MSG(0, DRV_QUERYFUNCTIONINSTANCEIDSIZE, &sz, 0), MMSYSERR_NOERROR);
    EXPECT_EQ(sz, 2);
    EXPECT_EQ(MSG(0, DRV_QUERYFUNCTIONINSTANCEIDSIZE, NULL, 0), MMSYSERR_INVALPARAM);
}

TEST(midiout_contract_message_funcinstid_id0) {
    REQUIRE(U(midiOutMessage));
    WCHAR buf[16];
    memset(buf, 0xCC, sizeof(buf));
    EXPECT_EQ(MSG(0, DRV_QUERYFUNCTIONINSTANCEID, buf, sizeof(buf)), MMSYSERR_NOERROR);
    EXPECT_EQ(buf[0], 0);
    memset(buf, 0xCC, sizeof(buf));
    EXPECT_EQ(MSG(0, DRV_QUERYFUNCTIONINSTANCEID, buf, 2), MMSYSERR_NOERROR);
    EXPECT_EQ(buf[0], 0);
    EXPECT_EQ(buf[1], 0xCCCC);
    /* fewer than one WCHAR of room: MOREDATA, nothing written */
    memset(buf, 0xCC, sizeof(buf));
    EXPECT_EQ(MSG(0, DRV_QUERYFUNCTIONINSTANCEID, buf, 0), MMSYSERR_MOREDATA);
    EXPECT_EQ(MSG(0, DRV_QUERYFUNCTIONINSTANCEID, buf, 1), MMSYSERR_MOREDATA);
    EXPECT_EQ(buf[0], 0xCCCC);
    EXPECT_EQ(MSG(0, DRV_QUERYFUNCTIONINSTANCEID, NULL, sizeof(buf)), MMSYSERR_INVALPARAM);
}

TEST(midiout_contract_message_devinterface_size_id0) {
    REQUIRE(U(midiOutMessage));
    DWORD sz = 0xA5A5A5A5;
    MMRESULT r = MSG(0, DRV_QUERYDEVICEINTERFACESIZE, &sz, 0);
    t_note("synth DRV_QUERYDEVICEINTERFACESIZE r=%u size=%lu", r, sz);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    EXPECT_EQ(sz, 2);
    EXPECT_EQ(MSG(0, DRV_QUERYDEVICEINTERFACESIZE, NULL, 0), MMSYSERR_INVALPARAM);
}

TEST(midiout_contract_message_devinterface_id0) {
    REQUIRE(U(midiOutMessage));
    WCHAR buf[16];
    memset(buf, 0xCC, sizeof(buf));
    MMRESULT r = MSG(0, DRV_QUERYDEVICEINTERFACE, buf, sizeof(buf));
    t_note("synth DRV_QUERYDEVICEINTERFACE r=%u first=0x%x", r, buf[0]);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    EXPECT_EQ(buf[0], 0);
    EXPECT_EQ(MSG(0, DRV_QUERYDEVICEINTERFACE, NULL, sizeof(buf)), MMSYSERR_INVALPARAM);
    memset(buf, 0xCC, sizeof(buf));
    EXPECT_EQ(MSG(0, DRV_QUERYDEVICEINTERFACE, buf, 0), MMSYSERR_MOREDATA);
    EXPECT_EQ(buf[0], 0xCCCC);
}

/* the mapper id answers the same internal queries */
TEST(midiout_contract_message_mapper_queries) {
    REQUIRE(U(midiOutMessage));
    DWORD sz = 0xA5A5A5A5;
    MMRESULT r = MSG(MIDI_MAPPER, DRV_QUERYFUNCTIONINSTANCEIDSIZE, &sz, 0);
    t_note("mapper DRV_QUERYFUNCTIONINSTANCEIDSIZE r=%u size=%lu", r, sz);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    EXPECT_EQ(MSG(MIDI_MAPPER, DRV_QUERYFUNCTIONINSTANCEIDSIZE, NULL, 0), MMSYSERR_INVALPARAM);
}

/* DRVM_MAPPER_PREFERRED_GET: answered for the mapper (preferred id, flags 0), rejected for a device */
TEST(midiout_contract_message_preferred_get) {
    REQUIRE(U(midiOutMessage));
    DWORD id = 0xA5A5A5A5, fl = 0xA5A5A5A5;
    MMRESULT r = MSG(MIDI_MAPPER, DRVM_MAPPER_PREFERRED_GET, &id, &fl);
    t_note("mapper PREFERRED_GET r=%u id=0x%lx flags=0x%lx", r, id, fl);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    EXPECT_EQ(fl, 0);
    CHECKF(id == 0xFFFFFFFF || id < moc_nout(), "preferred id 0x%lx", id);
    EXPECT_EQ(MSG(MIDI_MAPPER, DRVM_MAPPER_PREFERRED_GET, NULL, &fl), MMSYSERR_INVALPARAM);
    EXPECT_EQ(MSG(MIDI_MAPPER, DRVM_MAPPER_PREFERRED_GET, &id, NULL), MMSYSERR_INVALPARAM);
    id = 0xA5A5A5A5; fl = 0xA5A5A5A5;
    r = MSG(0, DRVM_MAPPER_PREFERRED_GET, &id, &fl);
    t_note("device 0 PREFERRED_GET r=%u", r);
    EXPECT_EQ(r, MMSYSERR_INVALPARAM);
}

/* reserved DRV_ query range 0x801..0x804 and 0x806: NOTSUPPORTED inside winmm */
TEST(midiout_contract_message_reserved_queries_8) {
    REQUIRE(U(midiOutMessage));
    static const UINT msgs[] = { 0x801, 0x802, 0x803, 0x804, 0x806 };
    for (int i = 0; i < NELEM(msgs); i++) {
        MMRESULT r = MSG(0, msgs[i], 0, 0);
        CHECKF(r == MMSYSERR_NOTSUPPORTED, "msg 0x%x -> %u", msgs[i], r);
    }
}

/* MODM_* through the message entry reach the driver: record the synth's answers */
TEST(midiout_contract_message_driver_messages_id0) {
    REQUIRE(U(midiOutMessage)); REQUIRE(U(midiOutGetDevCapsW));
    DWORD n = 0xA5A5A5A5;
    MMRESULT r = MSG(0, MODM_GETNUMDEVS, 0, 0);
    t_note("synth MODM_GETNUMDEVS -> %u", r);
    EXPECT_EQ(r, 1);
    MIDIOUTCAPSW c, ref; memset(&c, 0, sizeof(c)); memset(&ref, 0, sizeof(ref));
    r = MSG(0, MODM_GETDEVCAPS, &c, sizeof(c));
    t_note("synth MODM_GETDEVCAPS -> %u name='%ls'", r, c.szPname);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    if (r == MMSYSERR_NOERROR) {
        EXPECT_EQ(U(midiOutGetDevCapsW)(0, &ref, sizeof(ref)), MMSYSERR_NOERROR);
        CHECK(!memcmp(&c, &ref, sizeof(c)));
    }
    r = MSG(0, MODM_GETVOLUME, &n, 0);
    t_note("synth MODM_GETVOLUME -> %u v=0x%lx", r, n);
    EXPECT_EQ(r, MMSYSERR_NODRIVER);
    r = MSG(0, 0x7FFF, 0, 0);
    t_note("synth unknown msg 0x7FFF -> %u", r);
    EXPECT_EQ(r, MMSYSERR_NOTSUPPORTED);
}

/* a live handle sends the message to its driver instance */
TEST(midiout_contract_message_live_handle) {
    REQUIRE(U(midiOutMessage)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(0);
    if (!h) return;
    DWORD v = 0xA5A5A5A5;
    MMRESULT r = MSG(h, MODM_GETVOLUME, &v, 0);
    t_note("handle MODM_GETVOLUME -> %u v=0x%lx", r, v);
    EXPECT_EQ(r, MMSYSERR_NOERROR);
    DWORD sz = 0xA5A5A5A5;
    r = MSG(h, DRV_QUERYFUNCTIONINSTANCEIDSIZE, &sz, 0);
    t_note("handle DRV_QUERYFUNCTIONINSTANCEIDSIZE -> %u size=%lu", r, sz);
    /* by handle the query goes to the driver instance, which does not answer it (the id path does) [VM] */
    EXPECT_EQ(r, MMSYSERR_NOTSUPPORTED);
    EXPECT_EQ(sz, 0xA5A5A5A5);
    moc_close(h);
}

TEST(midiout_contract_message_closed_handle_2) {
    REQUIRE(U(midiOutMessage)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_closed_handle();
    if (!h) return;
    DWORD sz = 0;
    EXPECT_EQ(MSG(h, DRV_QUERYFUNCTIONINSTANCEIDSIZE, &sz, 0), MMSYSERR_BADDEVICEID);
}
