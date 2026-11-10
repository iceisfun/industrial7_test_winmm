#include "ee.h"
/* Enumeration across the other families: mappers, the GS synth, the no-mixer-mapper rule, and how each family's
 * caps entry point treats special ids.
 *  - waveOut rewrites id 0xFFFF to WAVE_MAPPER; waveIn's caps entry does NOT (waveInGetDevCapsW passes the id through:
 *    0xFFFF is an ordinary out-of-range id) [decomp winmmbase!0x11000].
 *  - wave ids are masked to 32 bits (waveIDMessage(..., id & 0xffffffff)); midi ids are compared as 64-bit values
 *    (midiIDMessage: id < count || id == 0xFFFFFFFF), so (UINT_PTR)-1 is NOT the MIDI mapper.
 *  - mixer ids are truncated to 32 bits (IMixerMessageId(uint)); there is no mixer mapper: -1 -> 2.
 *  - The wave mapper (msacm32.drv) loads when waveOut + waveIn devices exist; it serves both directions.
 *  - midiOut always lists the MIDI mapper (midimap) and the Microsoft GS Wavetable Synth [VM].
 * VM_TIMEOUT: 120 */

static UINT nwo(void) { return ee_numdevs(F_WAVEOUT); }
static UINT nwi(void) { return ee_numdevs(F_WAVEIN); }

/* ---- MIDI mapper and GS synth ----------------------------------------------------------------------------- */
TEST(fam_midiout_mapper_caps_w) {
    REQUIRE(U(midiOutGetDevCapsW)); REQUIRE(U(midiOutGetNumDevs));
    MIDIOUTCAPSW c; memset(&c, 0xEE, sizeof c);
    EXPECT_EQ(U(midiOutGetDevCapsW)(MIDI_MAPPER, &c, sizeof c), MMSYSERR_NOERROR);
    EXPECT_WSTR(c.szPname, L"Microsoft MIDI Mapper");
    EXPECT_EQ(c.wTechnology, MOD_MAPPER);
    EXPECT_EQ(c.dwSupport, MIDICAPS_VOLUME | MIDICAPS_STREAM);
    t_note("MIDI mapper: mid=%u pid=%u ver=0x%x voices=%u notes=%u mask=0x%x", c.wMid, c.wPid, c.vDriverVersion, c.wVoices, c.wNotes, c.wChannelMask);
}
TEST(fam_midiout_mapper_caps_a) {
    REQUIRE(U(midiOutGetDevCapsA));
    MIDIOUTCAPSA c; memset(&c, 0xEE, sizeof c);
    EXPECT_EQ(U(midiOutGetDevCapsA)(MIDI_MAPPER, &c, sizeof c), MMSYSERR_NOERROR);
    EXPECT_STR(c.szPname, "Microsoft MIDI Mapper");
    EXPECT_EQ(c.wTechnology, MOD_MAPPER);
    EXPECT_EQ(c.dwSupport, MIDICAPS_VOLUME | MIDICAPS_STREAM);
}
TEST(fam_midiout_mapper_independent_of_audio) {
    REQUIRE(U(midiOutGetDevCapsW)); REQUIRE(U(waveOutGetNumDevs));
    /* Present whether or not any wave device exists. */
    MIDIOUTCAPSW c;
    EXPECT_EQ(U(midiOutGetDevCapsW)(MIDI_MAPPER, &c, sizeof c), MMSYSERR_NOERROR);
    t_note("waveOut=%u, MIDI mapper caps ok", U(waveOutGetNumDevs)());
}
TEST(fam_midiout_gs_synth_is_device_0) {
    REQUIRE(U(midiOutGetDevCapsW)); REQUIRE(U(midiOutGetNumDevs));
    if (!U(midiOutGetNumDevs)()) { CHECKF(0, "no midiOut device"); return; }
    MIDIOUTCAPSW c; memset(&c, 0xEE, sizeof c);
    EXPECT_EQ(U(midiOutGetDevCapsW)(0, &c, sizeof c), MMSYSERR_NOERROR);
    EXPECT_WSTR(c.szPname, L"Microsoft GS Wavetable Synth");
    EXPECT_EQ(c.wTechnology, MOD_SWSYNTH);
    EXPECT_EQ(c.wVoices, 32);
    t_note("GS synth: mid=%u pid=%u ver=0x%x notes=%u mask=0x%x support=0x%lx", c.wMid, c.wPid, c.vDriverVersion, c.wNotes, c.wChannelMask, c.dwSupport);
}
TEST(fam_midiout_gs_synth_a_matches_w) {
    REQUIRE(U(midiOutGetDevCapsW)); REQUIRE(U(midiOutGetDevCapsA));
    MIDIOUTCAPSW w; MIDIOUTCAPSA a; char conv[64];
    memset(&w, 0, sizeof w); memset(&a, 0, sizeof a);
    EXPECT_EQ(U(midiOutGetDevCapsW)(0, &w, sizeof w), 0);
    EXPECT_EQ(U(midiOutGetDevCapsA)(0, &a, sizeof a), 0);
    WideCharToMultiByte(CP_ACP, 0, w.szPname, -1, conv, sizeof conv, NULL, NULL);
    EXPECT_STR(a.szPname, conv);
    EXPECT_EQ(a.wMid, w.wMid); EXPECT_EQ(a.wPid, w.wPid); EXPECT_EQ(a.vDriverVersion, w.vDriverVersion);
    EXPECT_EQ(a.wTechnology, w.wTechnology); EXPECT_EQ(a.wVoices, w.wVoices); EXPECT_EQ(a.wNotes, w.wNotes);
    EXPECT_EQ(a.wChannelMask, w.wChannelMask); EXPECT_EQ(a.dwSupport, w.dwSupport);
}
TEST(fam_midiout_one_synth_only) {
    REQUIRE(U(midiOutGetDevCapsW)); REQUIRE(U(midiOutGetNumDevs));
    UINT n = U(midiOutGetNumDevs)(), synths = 0;
    for (UINT i = 0; i < n; i++) {
        MIDIOUTCAPSW c;
        if (!U(midiOutGetDevCapsW)(i, &c, sizeof c) && c.wTechnology == MOD_SWSYNTH) synths++;
        t_note("midiOut %u \"%ls\" tech=%u", i, c.szPname, c.wTechnology);
    }
    EXPECT_EQ(synths, 1);
}
TEST(fam_midiout_mapper_not_counted) {
    REQUIRE(U(midiOutGetDevCapsW)); REQUIRE(U(midiOutGetNumDevs));
    /* No enumerated id reports MOD_MAPPER: the mapper only answers to MIDI_MAPPER. */
    UINT n = U(midiOutGetNumDevs)();
    for (UINT i = 0; i < n; i++) {
        MIDIOUTCAPSW c; EXPECT_EQ(U(midiOutGetDevCapsW)(i, &c, sizeof c), 0);
        CHECKF(c.wTechnology != MOD_MAPPER, "id %u is a mapper", i);
    }
}
TEST(fam_midiout_special_ids) {
    REQUIRE(U(midiOutGetDevCapsW)); REQUIRE(U(midiOutGetNumDevs));
    MIDIOUTCAPSW c;
    /* 64-bit compare: all-ones is not MIDI_MAPPER; 0xFFFF is not rewritten; a high dword is not ignored. */
    EXPECT_EQ(U(midiOutGetDevCapsW)((UINT_PTR)-1, &c, sizeof c), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(midiOutGetDevCapsW)(0xFFFF, &c, sizeof c), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(midiOutGetDevCapsW)((UINT_PTR)0x100000000ull, &c, sizeof c), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(midiOutGetDevCapsW)(0xFFFFFFFE, &c, sizeof c), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(midiOutGetDevCapsW)(0xFFFFFFFF, &c, sizeof c), MMSYSERR_NOERROR);
}
TEST(fam_midiout_ladder) {
    REQUIRE(U(midiOutGetDevCapsW)); REQUIRE(U(midiOutGetDevCapsA)); REQUIRE(U(midiOutGetNumDevs));
    UINT n = U(midiOutGetNumDevs)();
    UINT_PTR ids[] = { 0, n, MIDI_MAPPER, 0xFFFF, 77 };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        EXPECT_EQ(U(midiOutGetDevCapsW)(ids[i], NULL, 0), 0);
        EXPECT_EQ(U(midiOutGetDevCapsA)(ids[i], NULL, 0), 0);
        EXPECT_EQ(U(midiOutGetDevCapsW)(ids[i], NULL, sizeof(MIDIOUTCAPSW)), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(midiOutGetDevCapsA)(ids[i], NULL, sizeof(MIDIOUTCAPSA)), MMSYSERR_INVALPARAM);
    }
}

/* ---- midiIn ---------------------------------------------------------------------------------------------- */
TEST(fam_midiin_no_mapper_caps) {
    REQUIRE(U(midiInGetDevCapsW)); REQUIRE(U(midiInGetNumDevs));
    MIDIINCAPSW c;
    /* There is no MIDI-in mapper: -1 is an ordinary id that is out of range when midiIn has no device. */
    MMRESULT r = U(midiInGetDevCapsW)((UINT_PTR)(UINT)-1, &c, sizeof c);
    if (!U(midiInGetNumDevs)()) EXPECT_EQ(r, MMSYSERR_BADDEVICEID);
    else t_note("midiIn mapper caps with devices -> %u", r);
}
TEST(fam_midiin_ladder) {
    REQUIRE(U(midiInGetDevCapsW)); REQUIRE(U(midiInGetDevCapsA)); REQUIRE(U(midiInGetNumDevs));
    UINT n = U(midiInGetNumDevs)();
    MIDIINCAPSW w; MIDIINCAPSA a;
    EXPECT_EQ(U(midiInGetDevCapsW)(n, NULL, 0), 0);
    EXPECT_EQ(U(midiInGetDevCapsW)(n, NULL, sizeof w), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(midiInGetDevCapsA)(n, NULL, sizeof a), MMSYSERR_INVALPARAM);
    EXPECT_EQ(U(midiInGetDevCapsW)(n, &w, sizeof w), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(midiInGetDevCapsA)(n, &a, sizeof a), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(midiInGetDevCapsW)(0xFFFF, &w, sizeof w), MMSYSERR_BADDEVICEID);
}

/* ---- mixer ----------------------------------------------------------------------------------------------- */
TEST(fam_mixer_no_mapper) {
    REQUIRE(U(mixerGetDevCapsW)); REQUIRE(U(mixerGetDevCapsA));
    MIXERCAPSW w; MIXERCAPSA a;
    EXPECT_EQ(U(mixerGetDevCapsW)((UINT_PTR)(UINT)-1, &w, sizeof w), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(mixerGetDevCapsA)((UINT_PTR)(UINT)-1, &a, sizeof a), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(mixerGetDevCapsW)(0xFFFF, &w, sizeof w), MMSYSERR_BADDEVICEID);
}
TEST(fam_mixer_id_truncated_to_32_bits) {
    REQUIRE(U(mixerGetDevCapsW)); REQUIRE(U(mixerGetNumDevs));
    UINT n = U(mixerGetNumDevs)();
    MIXERCAPSW a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    MMRESULT r0 = U(mixerGetDevCapsW)(0, &a, sizeof a);
    MMRESULT r1 = U(mixerGetDevCapsW)((UINT_PTR)0x100000000ull, &b, sizeof b);
    EXPECT_EQ(r0, n ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID);
    EXPECT_EQ(r1, r0);
    if (!r0 && !r1) CHECK(!memcmp(&a, &b, sizeof a));
}
TEST(fam_mixer_names_match_wave_names) {
    REQUIRE(U(mixerGetDevCapsW)); REQUIRE(U(mixerGetNumDevs));
    UINT n = U(mixerGetNumDevs)();
    for (UINT i = 0; i < n; i++) {
        MIXERCAPSW m; EXPECT_EQ(U(mixerGetDevCapsW)(i, &m, sizeof m), 0);
        CHECKF(ee_wlen(m.szPname, MAXPNAMELEN) < MAXPNAMELEN && m.szPname[0], "mixer %u name", i);
        EXPECT_EQ(m.cDestinations, 1);
    }
}
TEST(fam_mixer_ladder) {
    REQUIRE(U(mixerGetDevCapsW)); REQUIRE(U(mixerGetDevCapsA)); REQUIRE(U(mixerGetNumDevs));
    UINT n = U(mixerGetNumDevs)();
    UINT_PTR ids[] = { 0, n, (UINT)-1, 500 };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        EXPECT_EQ(U(mixerGetDevCapsW)(ids[i], NULL, 0), 0);
        EXPECT_EQ(U(mixerGetDevCapsA)(ids[i], NULL, 0), 0);
        EXPECT_EQ(U(mixerGetDevCapsW)(ids[i], NULL, sizeof(MIXERCAPSW)), MMSYSERR_INVALPARAM);
        EXPECT_EQ(U(mixerGetDevCapsA)(ids[i], NULL, sizeof(MIXERCAPSA)), MMSYSERR_INVALPARAM);
    }
}

/* ---- aux ------------------------------------------------------------------------------------------------- */
TEST(fam_aux_mapper_and_ids) {
    REQUIRE(U(auxGetDevCapsW)); REQUIRE(U(auxGetDevCapsA)); REQUIRE(U(auxGetNumDevs));
    UINT n = U(auxGetNumDevs)();
    AUXCAPSW w; AUXCAPSA a;
    EXPECT_EQ(U(auxGetDevCapsW)(n, &w, sizeof w), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(auxGetDevCapsA)(n, &a, sizeof a), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(auxGetDevCapsW)(n, NULL, 0), 0);
    EXPECT_EQ(U(auxGetDevCapsW)(n, NULL, sizeof w), MMSYSERR_INVALPARAM);
    if (!n) {
        EXPECT_EQ(U(auxGetDevCapsW)((UINT_PTR)(UINT)-1, &w, sizeof w), MMSYSERR_BADDEVICEID);
        EXPECT_EQ(U(auxGetDevCapsA)((UINT_PTR)(UINT)-1, &a, sizeof a), MMSYSERR_BADDEVICEID);
    }
}

/* ---- waveIn and the shared wave mapper -------------------------------------------------------------------- */
TEST(fam_wavein_mapper_present_iff_wave_devices) {
    REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveInGetDevCapsA));
    WAVEINCAPSW w; WAVEINCAPSA a;
    MMRESULT want = (nwo() + nwi()) ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID;
    EXPECT_EQ(U(waveInGetDevCapsW)(WAVE_MAPPER, &w, sizeof w), want);
    EXPECT_EQ(U(waveInGetDevCapsA)(WAVE_MAPPER, &a, sizeof a), want);
    if (!want) {
        EXPECT_WSTR(w.szPname, L"Microsoft Sound Mapper");
        EXPECT_EQ(w.wMid, MM_MICROSOFT);
        EXPECT_EQ(w.wPid, MM_WAVE_MAPPER);
        EXPECT_STR(a.szPname, "Microsoft Sound Mapper");
        t_note("waveIn mapper: ver=0x%x formats=0x%lx ch=%u", w.vDriverVersion, w.dwFormats, w.wChannels);
    }
}
TEST(fam_wavein_0xffff_is_not_mapper) {
    REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveInGetDevCapsA));
    WAVEINCAPSW w; WAVEINCAPSA a;
    /* Unlike waveOutGetDevCaps, the waveIn caps entry has no 0xFFFF -> WAVE_MAPPER rewrite. */
    EXPECT_EQ(U(waveInGetDevCapsW)(0xFFFF, &w, sizeof w), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(waveInGetDevCapsA)(0xFFFF, &a, sizeof a), MMSYSERR_BADDEVICEID);
}
TEST(fam_wavein_high_dword_ignored) {
    REQUIRE(U(waveInGetDevCapsW));
    WAVEINCAPSW a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    MMRESULT r0 = U(waveInGetDevCapsW)(0, &a, sizeof a), r1 = U(waveInGetDevCapsW)((UINT_PTR)0x100000000ull, &b, sizeof b);
    EXPECT_EQ(r1, r0);
    EXPECT_EQ(r0, nwi() ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID);
    if (!r0) CHECK(!memcmp(&a, &b, sizeof a));
    MMRESULT rm = U(waveInGetDevCapsW)(WAVE_MAPPER, &a, sizeof a), rf = U(waveInGetDevCapsW)((UINT_PTR)-1, &b, sizeof b);
    EXPECT_EQ(rf, rm);
}
TEST(fam_wavein_device_names) {
    REQUIRE(U(waveInGetDevCapsW));
    for (UINT i = 0; i < nwi(); i++) {
        WAVEINCAPSW c; memset(&c, 0xEE, sizeof c);
        EXPECT_EQ(U(waveInGetDevCapsW)(i, &c, sizeof c), 0);
        int len = ee_wlen(c.szPname, MAXPNAMELEN);
        CHECKF(len >= 1 && len < MAXPNAMELEN, "waveIn %u name length %d", i, len);
        t_note("waveIn %u \"%ls\" mid=%u pid=%u formats=0x%lx ch=%u", i, c.szPname, c.wMid, c.wPid, c.dwFormats, c.wChannels);
    }
}
/* The mapper serves both directions from one driver: its waveOut and waveIn caps agree on the identity fields. */
TEST(fam_wave_mapper_same_identity_both_directions) {
    REQUIRE(U(waveInGetDevCapsW)); REQUIRE(U(waveOutGetDevCapsW));
    if (!(nwo() + nwi())) { t_skip("no wave device"); return; }
    WAVEOUTCAPSW o; WAVEINCAPSW i;
    EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, &o, sizeof o), 0);
    EXPECT_EQ(U(waveInGetDevCapsW)(WAVE_MAPPER, &i, sizeof i), 0);
    EXPECT_EQ(o.wMid, i.wMid);
    EXPECT_EQ(o.wPid, i.wPid);
    EXPECT_EQ(o.vDriverVersion, i.vDriverVersion);
    EXPECT_WSTR(o.szPname, i.szPname);
}
/* Names across families are <= 31 characters everywhere (MAXPNAMELEN). */
TEST(fam_all_names_fit_maxpnamelen) {
    REQUIRE(U(waveOutGetNumDevs));
    for (int f = 0; f < F_COUNT; f++) {
        UINT n = ee_numdevs(f);
        for (UINT i = 0; i < n; i++) {
            unsigned char w[512], a[512];
            ee_fill(w, sizeof w, 0xEE); ee_fill(a, sizeof a, 0xEE);
            EXPECT_EQ(ee_caps(f, 1, i, w, ee_caps_size(f, 1)), 0);
            EXPECT_EQ(ee_caps(f, 0, i, a, ee_caps_size(f, 0)), 0);
            CHECKF(ee_wlen((WCHAR *)(w + EE_PNAME_OFF), MAXPNAMELEN) < MAXPNAMELEN, "%s %u W", ee_family_name[f], i);
            CHECKF(ee_alen((char *)(a + EE_PNAME_OFF), MAXPNAMELEN) < MAXPNAMELEN, "%s %u A", ee_family_name[f], i);
        }
    }
}
/* The A/W name pair agrees for every enumerated device of every family. */
TEST(fam_all_a_names_convert_w_names) {
    REQUIRE(U(waveOutGetNumDevs));
    for (int f = 0; f < F_COUNT; f++) {
        UINT n = ee_numdevs(f);
        for (UINT i = 0; i < n; i++) {
            unsigned char w[512], a[512]; char conv[128];
            memset(w, 0, sizeof w); memset(a, 0, sizeof a);
            if (ee_caps(f, 1, i, w, ee_caps_size(f, 1)) || ee_caps(f, 0, i, a, ee_caps_size(f, 0))) { CHECK(0); continue; }
            WideCharToMultiByte(CP_ACP, 0, (WCHAR *)(w + EE_PNAME_OFF), -1, conv, sizeof conv, NULL, NULL);
            conv[MAXPNAMELEN - 1] = 0;
            CHECKF(!strcmp(conv, (char *)(a + EE_PNAME_OFF)), "%s %u \"%s\" vs \"%s\"", ee_family_name[f], i, conv, (char *)(a + EE_PNAME_OFF));
        }
    }
}
/* Handles are family-typed for caps: an HMIDIOUT passed to waveOutGetDevCaps is an (out-of-range) id. */
TEST(fam_cross_family_handle_is_id) {
    REQUIRE(U(midiOutOpen)); REQUIRE(U(midiOutClose)); REQUIRE(U(waveOutGetDevCapsW)); REQUIRE(U(midiOutGetDevCapsW));
    if (!nwo()) { t_skip("synth cannot be opened without audio [VM: 1]"); return; }
    HMIDIOUT h = NULL;
    MMRESULT r = U(midiOutOpen)(&h, 0, 0, 0, CALLBACK_NULL);
    EXPECT_EQ(r, 0);
    if (r) return;
    WAVEOUTCAPSW w; MIDIOUTCAPSW m, m0;
    EXPECT_EQ(U(waveOutGetDevCapsW)((UINT_PTR)h, &w, sizeof w), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(midiOutGetDevCapsW)((UINT_PTR)h, &m, sizeof m), 0);
    EXPECT_EQ(U(midiOutGetDevCapsW)(0, &m0, sizeof m0), 0);
    EXPECT_WSTR(m.szPname, m0.szPname);
    MIDIINCAPSW mi;
    EXPECT_EQ(U(midiInGetDevCapsW)((UINT_PTR)h, &mi, sizeof mi), MMSYSERR_BADDEVICEID);
    EXPECT_EQ(U(midiOutClose)(h), 0);
}
/* Mixer handle in place of the id gives the same caps. */
TEST(fam_mixer_caps_by_handle) {
    REQUIRE(U(mixerOpen)); REQUIRE(U(mixerClose)); REQUIRE(U(mixerGetDevCapsW)); REQUIRE(U(mixerGetNumDevs));
    UINT n = U(mixerGetNumDevs)();
    if (!n) { t_skip("no mixer"); return; }
    for (UINT i = 0; i < n; i++) {
        HMIXER m = NULL;
        EXPECT_EQ(U(mixerOpen)(&m, i, 0, 0, MIXER_OBJECTF_MIXER), 0);
        if (!m) continue;
        MIXERCAPSW a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
        EXPECT_EQ(U(mixerGetDevCapsW)(i, &a, sizeof a), 0);
        EXPECT_EQ(U(mixerGetDevCapsW)((UINT_PTR)m, &b, sizeof b), 0);
        CHECK(!memcmp(&a, &b, sizeof a));
        WAVEOUTCAPSW w;
        EXPECT_EQ(U(waveOutGetDevCapsW)((UINT_PTR)m, &w, sizeof w), MMSYSERR_BADDEVICEID);
        EXPECT_EQ(U(mixerClose)(m), 0);
    }
}
