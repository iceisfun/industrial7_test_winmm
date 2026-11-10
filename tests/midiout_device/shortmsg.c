/* midiOutShortMsg on a live synth (id 0) and through the mapper.
 *
 * winmmbase!midiOutShortMsg (0x8570) [D]: ValidateHandle type 3 (else type 9 -> midiStreamMessage) and then
 * midiMessage(MODM_DATA = 7, dwMsg, 0): winmm does not look at the message bytes at all, the driver answers.
 * midiMessage returns 6 (NODRIVER) when the handle's 'gone' bit is set and 12 when a close is in progress [D].
 *
 * Content stays quiet: velocities <= 20, notes <= 100 ms, controllers restored to their defaults, all-notes-off
 * on every channel before close (mod_close).
 */
#include "mod.h"

static const UINT g_ids[] = { 0, MIDI_MAPPER };

static MMRESULT sm(HMIDIOUT h, DWORD m) { return U(midiOutShortMsg)(h, m); }

/* run body for id 0 and the mapper */
#define FOR_IDS(h) for (int ii_ = 0; ii_ < NELEM(g_ids); ii_++) \
    for (HMIDIOUT h = mod_open_null(g_ids[ii_]), *once_ = (HMIDIOUT *)1; once_ && h; once_ = NULL, mod_close(h))

/* ================================================================ channel voice messages */

TEST(midiout_device_short_quiet_note) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        CHECKF(mod_note(h, 0, 60, 16, 80) == MMSYSERR_NOERROR, "id 0x%x", g_ids[ii_]);
    }
}

/* note on + note off on each of the 16 channels (10 is drums on the mapper; same answer) */
TEST(midiout_device_short_note_each_channel) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        for (int ch = 0; ch < 16; ch++) {
            CHECKF(sm(h, MOD_MSG(0x90 | ch, 64, 12)) == MMSYSERR_NOERROR, "id 0x%x ch %d on", g_ids[ii_], ch);
            Sleep(5);
            CHECKF(sm(h, MOD_MSG(0x80 | ch, 64, 0)) == MMSYSERR_NOERROR, "id 0x%x ch %d off", g_ids[ii_], ch);
        }
    }
}

/* note on with velocity 0 is a note off: same 0 */
TEST(midiout_device_short_note_on_velocity0) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        EXPECT_EQ(sm(h, MOD_MSG(0x90, 67, 10)), MMSYSERR_NOERROR);
        Sleep(30);
        EXPECT_EQ(sm(h, MOD_MSG(0x90, 67, 0)), MMSYSERR_NOERROR);
        /* note off for a note that is not sounding */
        EXPECT_EQ(sm(h, MOD_MSG(0x80, 20, 0)), MMSYSERR_NOERROR);
        EXPECT_EQ(sm(h, MOD_MSG(0x80, 127, 127)), MMSYSERR_NOERROR);
    }
}

/* the whole note range, silent (velocity 0 note-ons and note-offs) */
TEST(midiout_device_short_note_range_silent) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        int bad = 0;
        for (int n = 0; n < 128; n++) {
            if (sm(h, MOD_MSG(0x90, n, 0)) != MMSYSERR_NOERROR) bad++;
            if (sm(h, MOD_MSG(0x80, n, 64)) != MMSYSERR_NOERROR) bad++;
        }
        CHECKF(bad == 0, "id 0x%x: %d non-zero answers", g_ids[ii_], bad);
    }
}

/* every program on channel 0 and the drum channel */
TEST(midiout_device_short_program_change_all) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        int bad = 0;
        for (int p = 0; p < 128; p++) {
            if (sm(h, MOD_MSG(0xC0, p, 0)) != MMSYSERR_NOERROR) bad++;
            if (sm(h, MOD_MSG(0xC9, p, 0)) != MMSYSERR_NOERROR) bad++;
        }
        CHECKF(bad == 0, "id 0x%x: %d non-zero answers", g_ids[ii_], bad);
        EXPECT_EQ(sm(h, MOD_MSG(0xC0, 0, 0)), MMSYSERR_NOERROR);
        EXPECT_EQ(sm(h, MOD_MSG(0xC9, 0, 0)), MMSYSERR_NOERROR);
    }
}

/* a program change then a quiet note on the new program */
TEST(midiout_device_short_program_then_note) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        static const BYTE prog[] = { 0, 19, 40, 73 };
        for (int i = 0; i < NELEM(prog); i++) {
            EXPECT_EQ(sm(h, MOD_MSG(0xC1, prog[i], 0)), MMSYSERR_NOERROR);
            EXPECT_EQ(mod_note(h, 1, 72, 10, 40), MMSYSERR_NOERROR);
        }
        EXPECT_EQ(sm(h, MOD_MSG(0xC1, 0, 0)), MMSYSERR_NOERROR);
    }
}

/* controllers: a table of (cc, value set, default restored) */
TEST(midiout_device_short_controllers) {
    MOD_NEED_LIVE();
    static const struct { BYTE cc, v, def; } t[] = {
        { 0, 0, 0 }, { 1, 40, 0 }, { 7, 60, 100 }, { 10, 20, 64 }, { 11, 90, 127 }, { 32, 0, 0 },
        { 64, 127, 0 }, { 66, 127, 0 }, { 67, 127, 0 }, { 91, 10, 40 }, { 93, 10, 0 },
        { 98, 0x7F, 0x7F }, { 99, 0x7F, 0x7F }, { 100, 0, 0x7F }, { 101, 0, 0x7F }, { 6, 2, 2 }, { 38, 0, 0 },
    };
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(t); i++) {
            CHECKF(sm(h, MOD_MSG(0xB0, t[i].cc, t[i].v)) == MMSYSERR_NOERROR, "id 0x%x cc %u", g_ids[ii_], t[i].cc);
            CHECKF(sm(h, MOD_MSG(0xB0, t[i].cc, t[i].def)) == MMSYSERR_NOERROR, "id 0x%x cc %u restore", g_ids[ii_], t[i].cc);
        }
    }
}

/* channel mode messages 120..127 on every channel */
TEST(midiout_device_short_channel_mode) {
    MOD_NEED_LIVE();
    static const BYTE cc[] = { 120, 121, 123, 124, 125, 127, 126 };
    FOR_IDS(h) {
        int bad = 0;
        for (int ch = 0; ch < 16; ch++)
            for (int i = 0; i < NELEM(cc); i++)
                if (sm(h, MOD_MSG(0xB0 | ch, cc[i], cc[i] == 126 ? 1 : 0)) != MMSYSERR_NOERROR) bad++;
        CHECKF(bad == 0, "id 0x%x: %d non-zero", g_ids[ii_], bad);
        /* back to omni off / poly (GM default) */
        for (int ch = 0; ch < 16; ch++) { sm(h, MOD_MSG(0xB0 | ch, 124, 0)); sm(h, MOD_MSG(0xB0 | ch, 127, 0)); }
    }
}

/* all-notes-off 0x7BB0 on each channel (the cleanup message) */
TEST(midiout_device_short_all_notes_off) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        for (int ch = 0; ch < 16; ch++)
            CHECKF(sm(h, 0x7BB0 | ch) == MMSYSERR_NOERROR, "id 0x%x ch %d", g_ids[ii_], ch);
        /* notes are sounding when it arrives */
        sm(h, MOD_MSG(0x90, 60, 10)); sm(h, MOD_MSG(0x90, 64, 10)); sm(h, MOD_MSG(0x90, 67, 10));
        Sleep(30);
        EXPECT_EQ(sm(h, 0x7BB0), MMSYSERR_NOERROR);
    }
}

TEST(midiout_device_short_pitch_bend) {
    MOD_NEED_LIVE();
    static const WORD v[] = { 0x0000, 0x1000, 0x2000, 0x3000, 0x3FFF };
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(v); i++)
            CHECKF(sm(h, MOD_MSG(0xE0, v[i] & 0x7F, v[i] >> 7)) == MMSYSERR_NOERROR, "id 0x%x bend 0x%x", g_ids[ii_], v[i]);
        EXPECT_EQ(sm(h, MOD_MSG(0xE0, 0x00, 0x40)), MMSYSERR_NOERROR);   /* center */
    }
}

TEST(midiout_device_short_pressure) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        EXPECT_EQ(sm(h, MOD_MSG(0x90, 62, 8)), MMSYSERR_NOERROR);
        EXPECT_EQ(sm(h, MOD_MSG(0xA0, 62, 4)), MMSYSERR_NOERROR);   /* poly aftertouch */
        EXPECT_EQ(sm(h, MOD_MSG(0xD0, 4, 0)), MMSYSERR_NOERROR);    /* channel pressure */
        Sleep(20);
        EXPECT_EQ(sm(h, MOD_MSG(0x80, 62, 0)), MMSYSERR_NOERROR);
        EXPECT_EQ(sm(h, MOD_MSG(0xD0, 0, 0)), MMSYSERR_NOERROR);
    }
}

/* every status byte 0x80..0xEF with zero data */
TEST(midiout_device_short_every_voice_status) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        int bad = 0;
        for (int st = 0x80; st < 0xF0; st++) {
            BYTE d1 = 0, d2 = 0;
            if ((st & 0xF0) == 0x90) d2 = 0;       /* note on velocity 0 = off */
            if ((st & 0xF0) == 0xB0) d1 = 123;     /* all notes off */
            if ((st & 0xF0) == 0xE0) d2 = 0x40;    /* centered bend */
            MMRESULT r = sm(h, MOD_MSG(st, d1, d2));
            if (r != MMSYSERR_NOERROR) { bad++; t_note("id 0x%x status 0x%02x -> %u", g_ids[ii_], st, r); }
        }
        CHECKF(bad == 0, "id 0x%x: %d", g_ids[ii_], bad);
    }
}

/* ================================================================ system and odd messages */

/* system common / real-time status bytes in a short message: the driver accepts them [VM] */
/* validated: Windows 10 22H2 19045.2965, no-audio and emulated-HDA runs */
TEST(midiout_device_short_system_bytes) {
    MOD_NEED_LIVE();
    static const DWORD m[] = { 0xF1, 0x00F2, 0x00F3, 0xF4, 0xF5, 0xF6, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE };
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(m); i++) {
            MMRESULT r = sm(h, m[i]);
            CHECKF(r == MMSYSERR_NOERROR, "id 0x%x msg 0x%lx r=%u", g_ids[ii_], m[i], r);
        }
    }
}

/* 0xFF is "system reset" on the wire; 0xF0 / 0xF7 (sysex framing) in a short message */
/* validated: Windows 10 22H2 19045.2965, no-audio and emulated-HDA runs */
TEST(midiout_device_short_reset_and_sysex_status) {
    MOD_NEED_LIVE();
    static const DWORD m[] = { 0xFF, 0xF0, 0xF7, 0x7FF0, 0x00F7F0 };
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(m); i++) {
            MMRESULT r = sm(h, m[i]);
            t_note("id 0x%x short 0x%06lx -> %u", g_ids[ii_], m[i], r);
            CHECKF(r == MMSYSERR_NOERROR, "id 0x%x msg 0x%lx r=%u", g_ids[ii_], m[i], r);
        }
    }
}

/* a data byte with no status (running status without a previous status) and the 0 message */
/* validated: Windows 10 22H2 19045.2965, no-audio and emulated-HDA runs */
TEST(midiout_device_short_data_only) {
    MOD_NEED_LIVE();
    static const DWORD m[] = { 0x40, 0x00, 0x7F, 0x0040, 0x147F40 };
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(m); i++) {
            MMRESULT r = sm(h, m[i]);
            t_note("id 0x%x data-only 0x%06lx -> %u", g_ids[ii_], m[i], r);
            /* Win10 22H2 (emulated HDA, Microsoft GS Wavetable Synth): the MIDI mapper answered MMSYSERR_ERROR (1) for the all-zero message in this
             * sequence (no status sent before it on the handle); not a general rule: see the running-status probe below */
            if (m[i] == 0 && g_ids[ii_] == MIDI_MAPPER) CHECKF(r == MMSYSERR_NOERROR || r == MMSYSERR_ERROR, "id 0x%x msg 0x%lx r=%u", g_ids[ii_], m[i], r);
            else CHECKF(r == MMSYSERR_NOERROR, "id 0x%x msg 0x%lx r=%u", g_ids[ii_], m[i], r);
        }
    }
}

/* PROBE (records only): the zero DWORD is DATA, not an invalid status, when a status was supplied earlier on the same handle (running status: the
 * low byte is then the first data byte, bits 8-15 the second). The earlier short_data_only row sends 0 with whatever preceded it; this pair keeps the
 * sequence explicit: Note Off 0x80 (channel 1, key 0, velocity 0) accepted, then 0x00000000 on the SAME handle, mapper and concrete device apart. */
TEST(midiout_device_short_zero_after_noteoff_probe) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        MMRESULT r1 = sm(h, 0x00000080);
        MMRESULT r2 = sm(h, 0x00000000);
        t_note("id 0x%x: 0x80 -> %u, then 0x0 on the same handle -> %u", g_ids[ii_], r1, r2);
    }
}

/* running status: a note-on, then data-only messages continue it (velocity 0 = off) */
/* validated: Windows 10 22H2 19045.2965, no-audio and emulated-HDA runs */
TEST(midiout_device_short_running_status) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        EXPECT_EQ(sm(h, MOD_MSG(0x90, 60, 0)), MMSYSERR_NOERROR);
        EXPECT_EQ(sm(h, MOD_MSG(62, 0, 0)), MMSYSERR_NOERROR);   /* 2 bytes: 62, 0 */
        EXPECT_EQ(sm(h, MOD_MSG(64, 0, 0)), MMSYSERR_NOERROR);
        EXPECT_EQ(sm(h, MOD_MSG(0xC0, 0, 0)), MMSYSERR_NOERROR);
        EXPECT_EQ(sm(h, MOD_MSG(0, 0, 0)), MMSYSERR_NOERROR);    /* running program change 0 */
    }
}

/* the high byte of dwMsg is not part of a short message: garbage there changes nothing */
/* validated: Windows 10 22H2 19045.2965, no-audio and emulated-HDA runs */
TEST(midiout_device_short_high_byte_ignored) {
    MOD_NEED_LIVE();
    static const DWORD hi[] = { 0x01000000, 0x80000000, 0xFF000000 };
    FOR_IDS(h) {
        for (int i = 0; i < NELEM(hi); i++) {
            CHECKF(sm(h, hi[i] | MOD_MSG(0xB0, 123, 0)) == MMSYSERR_NOERROR, "id 0x%x hi 0x%lx", g_ids[ii_], hi[i]);
            CHECKF(sm(h, hi[i] | MOD_MSG(0x80, 60, 0)) == MMSYSERR_NOERROR, "id 0x%x hi 0x%lx off", g_ids[ii_], hi[i]);
        }
    }
}

/* data bytes with the high bit set inside a voice message */
TEST(midiout_device_short_data_high_bit) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        MMRESULT r1 = sm(h, MOD_MSG(0xB0, 0xFB, 0x00));
        MMRESULT r2 = sm(h, MOD_MSG(0x80, 0x80 | 60, 0x80));
        MMRESULT r3 = sm(h, MOD_MSG(0xC0, 0x80, 0));
        t_note("id 0x%x high-bit data: cc=%u noteoff=%u prog=%u", g_ids[ii_], r1, r2, r3);
        CHECK(r1 == MMSYSERR_NOERROR && r2 == MMSYSERR_NOERROR && r3 == MMSYSERR_NOERROR);
        sm(h, MOD_MSG(0xC0, 0, 0));
    }
}

/* ================================================================ volume of traffic, threads */

TEST(midiout_device_short_burst) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        int bad = 0;
        for (int i = 0; i < 2000; i++)
            if (sm(h, MOD_MSG(0xB0 | (i & 15), 123, 0)) != MMSYSERR_NOERROR) bad++;
        CHECKF(bad == 0, "id 0x%x: %d of 2000 failed", g_ids[ii_], bad);
    }
}

typedef struct { HMIDIOUT h; int ch; int bad; } smt_t;
static DWORD WINAPI sm_thread(LPVOID p) {
    smt_t *t = (smt_t *)p;
    for (int i = 0; i < 300; i++) {
        if (U(midiOutShortMsg)(t->h, MOD_MSG(0xB0 | t->ch, 7, 100)) != MMSYSERR_NOERROR) t->bad++;
        if (U(midiOutShortMsg)(t->h, MOD_MSG(0x80 | t->ch, 60, 0)) != MMSYSERR_NOERROR) t->bad++;
    }
    return 0;
}

TEST(midiout_device_short_threads) {
    MOD_NEED_LIVE();
    FOR_IDS(h) {
        smt_t t[4]; HANDLE th[4];
        for (int i = 0; i < 4; i++) { t[i].h = h; t[i].ch = i; t[i].bad = 0; th[i] = CreateThread(NULL, 0, sm_thread, &t[i], 0, NULL); }
        CHECK(WaitForMultipleObjects(4, th, TRUE, 10000) != WAIT_TIMEOUT);
        for (int i = 0; i < 4; i++) { CHECKF(t[i].bad == 0, "id 0x%x thread %d: %d", g_ids[ii_], i, t[i].bad); CloseHandle(th[i]); }
    }
}

/* ShortMsg right after open (no wait for MOM_OPEN) and right before close */
TEST(midiout_device_short_immediately_after_open) {
    MOD_NEED_LIVE();
    for (int k = 0; k < 4; k++) {
        HMIDIOUT h = NULL;
        UINT id = (k & 1) ? MIDI_MAPPER : 0;
        if (mod_open(&h, id, (DWORD_PTR)mod_cb, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) { CHECKF(0, "open %d", k); continue; }
        EXPECT_EQ(sm(h, MOD_MSG(0xB0, 123, 0)), MMSYSERR_NOERROR);
        mod_close(h);
    }
}

/* ShortMsg does not produce callbacks */
TEST(midiout_device_short_no_callback) {
    MOD_NEED_LIVE();
    for (int i = 0; i < NELEM(g_ids); i++) {
        mod_rec_reset();
        HMIDIOUT h = NULL;
        if (mod_open(&h, g_ids[i], (DWORD_PTR)mod_cb, 0x51, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) { CHECK(0); continue; }
        mod_wait_msg(MOM_OPEN, 1, 1000);
        int before = mod_rec_count();
        for (int k = 0; k < 20; k++) sm(h, MOD_MSG(0xB0, 123, 0));
        mod_note(h, 0, 65, 10, 40);
        Sleep(50);
        EXPECT_EQ(mod_rec_count(), before);
        EXPECT_EQ(mod_rec_count_msg(MOM_DONE), 0);
        mod_close(h);
    }
}
