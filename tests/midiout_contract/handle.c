/* Handle-only midiOut calls.
 *   midiOutShortMsg (!0x8570): handle midiOut(3)/stream(9) else 5.
 *   midiOutReset (!0x86f0):    handle 3/9 else 5.
 *   midiOutClose (!0x8000):    handle 3 only else 5.
 *   midiOutGetID (!0x94a0):    puDeviceID NULL -> 11 FIRST; handle 3 else 5; *pu = the id the handle was opened with.
 *   midiOutCachePatches (!0x8780) / midiOutCacheDrumPatches (!0x8850):
 *       array NULL -> 11; fuCache & ~7 -> 10; handle 3/9 else 5; driver MODM_CACHEPATCHES / CACHEDRUMPATCHES.
 */
#include "moc.h"

#define BH(i) ((HMIDIOUT)moc_bad_handles[i])

/* ================================================================ ShortMsg */

TEST(midiout_contract_shortmsg_bad_handle_5) {
    REQUIRE(U(midiOutShortMsg));
    static const DWORD msgs[] = { 0, 0x90, 0x403C90, 0x7BB0, 0xFE, 0xFF, 0xF0, 0x12345678, 0xFFFFFFFF };
    for (int i = 0; i < moc_nbad; i++)
        for (int j = 0; j < NELEM(msgs); j++)
            CHECKF(U(midiOutShortMsg)(BH(i), msgs[j]) == MMSYSERR_INVALHANDLE, "h=%p msg=0x%lx", BH(i), msgs[j]);
}

TEST(midiout_contract_shortmsg_mmio_handle_5) {
    REQUIRE(U(midiOutShortMsg));
    HMMIO m = moc_mmio_open();
    if (!m) { t_skip("no mmio handle"); return; }
    EXPECT_EQ(U(midiOutShortMsg)((HMIDIOUT)m, 0x7BB0), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutShortMsg)((HMIDIOUT)m, 0), MMSYSERR_INVALHANDLE);
    moc_mmio_close(m);
}

/* silent channel-mode messages are accepted by the synth */
TEST(midiout_contract_shortmsg_live_silent_messages_0) {
    REQUIRE(U(midiOutShortMsg)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    for (DWORD ch = 0; ch < 16; ch++) {
        CHECKF(U(midiOutShortMsg)(h, 0x7BB0 | ch) == MMSYSERR_NOERROR, "all notes off ch %lu", ch);   /* all notes off */
        CHECKF(U(midiOutShortMsg)(h, 0x79B0 | ch) == MMSYSERR_NOERROR, "reset controllers ch %lu", ch); /* reset all controllers */
        CHECKF(U(midiOutShortMsg)(h, 0x78B0 | ch) == MMSYSERR_NOERROR, "all sound off ch %lu", ch);   /* all sound off */
    }
    moc_close(h);
}

/* the upper byte of the DWORD is ignored, odd status values are passed through: record what the synth says */
TEST(midiout_contract_shortmsg_live_odd_messages) {
    REQUIRE(U(midiOutShortMsg)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    static const DWORD msgs[] = { 0x00000000, 0x00000040, 0xFF007BB0, 0x000000FE, 0x000000F8, 0x00000000F6 };
    for (int i = 0; i < NELEM(msgs); i++) {
        MMRESULT r = U(midiOutShortMsg)(h, msgs[i]);
        t_note("shortmsg 0x%08lx -> %u", msgs[i], r);
        CHECKF(r != MMSYSERR_INVALHANDLE, "msg 0x%lx", msgs[i]);
    }
    moc_close(h);
}

/* ================================================================ Reset / Close */

TEST(midiout_contract_reset_bad_handle_5) {
    REQUIRE(U(midiOutReset));
    for (int i = 0; i < moc_nbad; i++)
        CHECKF(U(midiOutReset)(BH(i)) == MMSYSERR_INVALHANDLE, "h=%p", BH(i));
}

TEST(midiout_contract_close_bad_handle_5) {
    REQUIRE(U(midiOutClose));
    for (int i = 0; i < moc_nbad; i++)
        CHECKF(U(midiOutClose)(BH(i)) == MMSYSERR_INVALHANDLE, "h=%p", BH(i));
}

/* a foreign winmm handle is rejected and stays usable */
TEST(midiout_contract_reset_close_mmio_handle_5) {
    REQUIRE(U(midiOutReset)); REQUIRE(U(midiOutClose)); REQUIRE(U(mmioClose));
    HMMIO m = moc_mmio_open();
    if (!m) { t_skip("no mmio handle"); return; }
    EXPECT_EQ(U(midiOutReset)((HMIDIOUT)m), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutClose)((HMIDIOUT)m), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(mmioClose)(m, 0), 0);
}

TEST(midiout_contract_close_live_then_5) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); REQUIRE(U(midiOutReset));
    HMIDIOUT h = moc_open_live();
    if (!h) return;
    EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_NOERROR);
    EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_INVALHANDLE);
}

TEST(midiout_contract_closed_handle_all_handle_calls_5) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); REQUIRE(U(midiOutReset)); REQUIRE(U(midiOutShortMsg));
    REQUIRE(U(midiOutGetID)); REQUIRE(U(midiOutCachePatches)); REQUIRE(U(midiOutCacheDrumPatches));
    HMIDIOUT h = moc_closed_handle();
    if (!h) return;
    WORD arr[128] = {0}; UINT id = 0xABCD;
    EXPECT_EQ(U(midiOutShortMsg)(h, 0x7BB0), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutReset)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutClose)(h), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutGetID)(h, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 0xABCD);
    EXPECT_EQ(U(midiOutCachePatches)(h, 0, arr, MIDI_CACHE_QUERY), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutCacheDrumPatches)(h, 0, arr, MIDI_CACHE_QUERY), MMSYSERR_INVALHANDLE);
}

/* ================================================================ GetID */

TEST(midiout_contract_getid_null_11_first) {
    REQUIRE(U(midiOutGetID));
    for (int i = 0; i < moc_nbad; i++)
        CHECKF(U(midiOutGetID)(BH(i), NULL) == MMSYSERR_INVALPARAM, "h=%p", BH(i));
}

TEST(midiout_contract_getid_bad_handle_5_untouched) {
    REQUIRE(U(midiOutGetID));
    for (int i = 0; i < moc_nbad; i++) {
        UINT id = 0xA5A5A5A5;
        CHECKF(U(midiOutGetID)(BH(i), &id) == MMSYSERR_INVALHANDLE, "h=%p", BH(i));
        CHECKF(id == 0xA5A5A5A5, "h=%p wrote %u", BH(i), id);
    }
}

TEST(midiout_contract_getid_mmio_handle_5) {
    REQUIRE(U(midiOutGetID));
    HMMIO m = moc_mmio_open();
    if (!m) { t_skip("no mmio handle"); return; }
    UINT id = 77;
    EXPECT_EQ(U(midiOutGetID)((HMIDIOUT)m, &id), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(id, 77);
    EXPECT_EQ(U(midiOutGetID)((HMIDIOUT)m, NULL), MMSYSERR_INVALPARAM);
    moc_mmio_close(m);
}

TEST(midiout_contract_getid_live_id0) {
    REQUIRE(U(midiOutGetID)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(0);
    if (!h) return;
    UINT id = 0xA5A5A5A5;
    EXPECT_EQ(U(midiOutGetID)(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, 0);
    EXPECT_EQ(U(midiOutGetID)(h, NULL), MMSYSERR_INVALPARAM);
    moc_close(h);
}

/* the mapper handle reports MIDI_MAPPER, not the device it maps to [VM] */
TEST(midiout_contract_getid_live_mapper) {
    REQUIRE(U(midiOutGetID)); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(MIDI_MAPPER);
    if (!h) return;
    UINT id = 0xA5A5A5A5;
    EXPECT_EQ(U(midiOutGetID)(h, &id), MMSYSERR_NOERROR);
    EXPECT_EQ(id, MIDI_MAPPER);
    moc_close(h);
}

/* ================================================================ CachePatches / CacheDrumPatches */

typedef MMRESULT (WINAPI *cache_fn)(HMIDIOUT, UINT, LPWORD, UINT);
static cache_fn cache_pick(int drum) {
    return drum ? (cache_fn)U(midiOutCacheDrumPatches) : (cache_fn)U(midiOutCachePatches);
}
#define REQC() do { REQUIRE(U(midiOutCachePatches)); REQUIRE(U(midiOutCacheDrumPatches)); } while (0)

/* NULL array -> 11 before the flags and the handle */
TEST(midiout_contract_cache_null_array_11_first) {
    REQC();
    static const UINT fl[] = { 0, MIDI_CACHE_ALL, MIDI_CACHE_BESTFIT, MIDI_CACHE_QUERY, MIDI_UNCACHE, 8, 0x10, 0xFFFFFFFF };
    for (int d = 0; d < 2; d++)
        for (int i = 0; i < moc_nbad; i++)
            for (int j = 0; j < NELEM(fl); j++)
                CHECKF(cache_pick(d)(BH(i), 0, NULL, fl[j]) == MMSYSERR_INVALPARAM, "%s h=%p fl=0x%x", d ? "drum" : "patch", BH(i), fl[j]);
}

/* any bit above 0x7 -> 10, before the handle */
TEST(midiout_contract_cache_bad_flag_bits_10) {
    REQC();
    WORD arr[128] = {0};
    for (int d = 0; d < 2; d++)
        for (int b = 3; b < 32; b++)
            for (int i = 0; i < moc_nbad; i += 3) {
                CHECKF(cache_pick(d)(BH(i), 0, arr, 1u << b) == MMSYSERR_INVALFLAG, "%s bit %d h=%p", d ? "drum" : "patch", b, BH(i));
                CHECKF(cache_pick(d)(BH(i), 0, arr, (1u << b) | MIDI_CACHE_QUERY) == MMSYSERR_INVALFLAG, "%s bit %d|3", d ? "drum" : "patch", b);
            }
}

/* flags 0..7 pass to the handle check: 5 */
TEST(midiout_contract_cache_legal_flags_bad_handle_5) {
    REQC();
    WORD arr[128] = {0};
    for (int d = 0; d < 2; d++)
        for (UINT fl = 0; fl < 8; fl++)
            for (int i = 0; i < moc_nbad; i++)
                CHECKF(cache_pick(d)(BH(i), 0, arr, fl) == MMSYSERR_INVALHANDLE, "%s fl=%u h=%p", d ? "drum" : "patch", fl, BH(i));
}

/* the bank / patch argument is not validated before the handle */
TEST(midiout_contract_cache_bank_not_checked) {
    REQC();
    WORD arr[128] = {0};
    static const UINT banks[] = { 0, 1, 127, 128, 0xFFFF, 0x10000, 0xFFFFFFFF };
    for (int d = 0; d < 2; d++)
        for (int i = 0; i < NELEM(banks); i++)
            CHECKF(cache_pick(d)(NULL, banks[i], arr, MIDI_CACHE_ALL) == MMSYSERR_INVALHANDLE, "%s bank=0x%x", d ? "drum" : "patch", banks[i]);
}

TEST(midiout_contract_cache_array_untouched_on_error) {
    REQC();
    WORD arr[128];
    for (int d = 0; d < 2; d++) {
        memset(arr, 0x5A, sizeof(arr));
        EXPECT_EQ(cache_pick(d)(NULL, 0, arr, MIDI_CACHE_QUERY), MMSYSERR_INVALHANDLE);
        EXPECT_EQ(cache_pick(d)(NULL, 0, arr, 0x100), MMSYSERR_INVALFLAG);
        CHECK(arr[0] == 0x5A5A && arr[127] == 0x5A5A);
    }
}

TEST(midiout_contract_cache_mmio_handle_5) {
    REQC();
    HMMIO m = moc_mmio_open();
    if (!m) { t_skip("no mmio handle"); return; }
    WORD arr[128] = {0};
    EXPECT_EQ(U(midiOutCachePatches)((HMIDIOUT)m, 0, arr, MIDI_CACHE_ALL), MMSYSERR_INVALHANDLE);
    EXPECT_EQ(U(midiOutCacheDrumPatches)((HMIDIOUT)m, 0, arr, MIDI_CACHE_ALL), MMSYSERR_INVALHANDLE);
    moc_mmio_close(m);
}

/* the GS synth does not implement patch caching [expected NOTSUPPORTED] */
TEST(midiout_contract_cache_live_synth) {
    REQC(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(0);
    if (!h) return;
    WORD arr[128];
    for (int d = 0; d < 2; d++)
        for (UINT fl = 0; fl <= MIDI_UNCACHE; fl++) {
            memset(arr, 0, sizeof(arr));
            MMRESULT r = cache_pick(d)(h, 0, arr, fl);
            t_note("%s synth fl=%u -> %u", d ? "drum" : "patch", fl, r);
            EXPECT_EQ(r, MMSYSERR_NOTSUPPORTED);
        }
    /* checks before the driver still apply on a live handle */
    EXPECT_EQ(U(midiOutCachePatches)(h, 0, NULL, MIDI_CACHE_ALL), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(midiOutCacheDrumPatches)(h, 0, arr, 8), MMSYSERR_INVALFLAG);
    moc_close(h);
}

TEST(midiout_contract_cache_live_mapper) {
    REQC(); REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose));
    HMIDIOUT h = moc_open_id(MIDI_MAPPER);
    if (!h) return;
    WORD arr[128] = {0};
    MMRESULT r1 = U(midiOutCachePatches)(h, 0, arr, MIDI_CACHE_QUERY);
    MMRESULT r2 = U(midiOutCacheDrumPatches)(h, 0, arr, MIDI_CACHE_QUERY);
    t_note("mapper cache patches %u drum %u", r1, r2);
    EXPECT_EQ(r1, MMSYSERR_NOTSUPPORTED);
    EXPECT_EQ(r2, MMSYSERR_NOTSUPPORTED);
    moc_close(h);
}
