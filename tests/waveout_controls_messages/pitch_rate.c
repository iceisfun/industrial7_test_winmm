/* waveOutGetPitch / SetPitch / GetPlaybackRate / SetPlaybackRate (winmm ord 168 / 178 / 169 / 179; winmmbase
 * !0x10cd0 / !0x10d30 / !0x10d80 / !0x10de0).
 *
 * winmmbase: the Get forms check the output pointer first (NULL -> 11), then ValidateHandle(type 1): unlike
 * GetVolume there is NO device-id path, anything that is not a live waveOut handle -> MMSYSERR_INVALHANDLE (5)
 * with the output untouched. A live handle -> waveMessage WODM_GETPITCH 14 / SETPITCH 15 / GETPLAYBACKRATE 18 /
 * SETPLAYBACKRATE 19 with dw2 = 0, so waveOutMessage(h, WODM_*, p, 0) reaches the driver identically.
 * Values are 16.16 fixed point; 0x10000 is 1.0 (the default). The Set forms are only called on live handles with
 * the value just read (or 1.0 when the driver has no value), i.e. never change the stream. */
#include "wcm.h"

typedef MMRESULT (WINAPI *get_fn)(HWAVEOUT, LPDWORD);
typedef MMRESULT (WINAPI *set_fn)(HWAVEOUT, DWORD);

typedef struct { const char *name; get_fn get; set_fn set; UINT gmsg, smsg; int ord_get, ord_set, bord_get, bord_set; } ctl;

static int ctl_table(ctl out[2])
{
    out[0] = (ctl){ "pitch", (get_fn)U(waveOutGetPitch), (set_fn)U(waveOutSetPitch), WCM_WODM_GETPITCH, WCM_WODM_SETPITCH,
                    168, 178, 123, 133 };
    out[1] = (ctl){ "rate", (get_fn)U(waveOutGetPlaybackRate), (set_fn)U(waveOutSetPlaybackRate),
                    WCM_WODM_GETPLAYBACKRATE, WCM_WODM_SETPLAYBACKRATE, 169, 179, 124, 134 };
    return out[0].get && out[0].set && out[1].get && out[1].set;
}

/* ---------------------------------------------------------------- exports */

/* UNVERIFIED: pin after the first Win10 run */
TEST(wcm_pr_exports_resolve)
{
    REQUIRE(U(waveOutGetPitch));
    REQUIRE(U(waveOutSetPitch));
    REQUIRE(U(waveOutGetPlaybackRate));
    REQUIRE(U(waveOutSetPlaybackRate));
    EXPECT_EQ(t_proc_ord("winmm.dll", 168), t_proc_in("winmm.dll", "waveOutGetPitch"));
    EXPECT_EQ(t_proc_ord("winmm.dll", 178), t_proc_in("winmm.dll", "waveOutSetPitch"));
    EXPECT_EQ(t_proc_ord("winmm.dll", 169), t_proc_in("winmm.dll", "waveOutGetPlaybackRate"));
    EXPECT_EQ(t_proc_ord("winmm.dll", 179), t_proc_in("winmm.dll", "waveOutSetPlaybackRate"));
    EXPECT_EQ(t_proc_ord("winmmbase.dll", 123), t_proc_in("winmmbase.dll", "waveOutGetPitch"));
    EXPECT_EQ(t_proc_ord("winmmbase.dll", 133), t_proc_in("winmmbase.dll", "waveOutSetPitch"));
    EXPECT_EQ(t_proc_ord("winmmbase.dll", 124), t_proc_in("winmmbase.dll", "waveOutGetPlaybackRate"));
    EXPECT_EQ(t_proc_ord("winmmbase.dll", 134), t_proc_in("winmmbase.dll", "waveOutSetPlaybackRate"));
}

TEST(wcm_pr_winmm_and_winmmbase_agree)
{
    static const char *gets[] = { "waveOutGetPitch", "waveOutGetPlaybackRate" };
    static const char *sets[] = { "waveOutSetPitch", "waveOutSetPlaybackRate" };
    for (int k = 0; k < 2; k++) {
        get_fn a = (get_fn)t_proc_in("winmm.dll", gets[k]), b = (get_fn)t_proc_in("winmmbase.dll", gets[k]);
        set_fn sa = (set_fn)t_proc_in("winmm.dll", sets[k]), sb = (set_fn)t_proc_in("winmmbase.dll", sets[k]);
        REQUIRE(a); REQUIRE(b); REQUIRE(sa); REQUIRE(sb);
        DWORD v = WCM_SENT;
        CHECKF(a(NULL, NULL) == WCM_INVALPARAM && b(NULL, NULL) == WCM_INVALPARAM, "%s NULL", gets[k]);
        CHECKF(a(NULL, &v) == WCM_INVALHANDLE && b(NULL, &v) == WCM_INVALHANDLE, "%s bad handle", gets[k]);
        EXPECT_EQ(v, WCM_SENT);
        CHECKF(sa(NULL, 0x10000) == WCM_INVALHANDLE && sb(NULL, 0x10000) == WCM_INVALHANDLE, "%s bad handle", sets[k]);
    }
}

/* ---------------------------------------------------------------- NULL out pointer first */

TEST(wcm_pr_get_null_ptr_nonhandles)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    for (int k = 0; k < 2; k++)
        for (const wcm_val *v = wcm_nonhandles; v->name; v++)
            CHECKF(c[k].get((HWAVEOUT)v->v, NULL) == WCM_INVALPARAM, "%s value %s", c[k].name, v->name);
}

TEST(wcm_pr_get_null_ptr_live_handles)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    UINT ids[2] = { WAVE_MAPPER, 0 };
    for (int i = 0; i < 2; i++) {
        HWAVEOUT h;
        EXPECT_EQ(wcm_open_out(&h, ids[i]), 0);
        if (!h) continue;
        for (int k = 0; k < 2; k++) CHECKF(c[k].get(h, NULL) == WCM_INVALPARAM, "%s id %u", c[k].name, ids[i]);
        wcm_close_out(h);
    }
}

TEST(wcm_pr_get_null_ptr_closed_handle)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    HWAVEOUT h = wcm_closed_out();
    if (!h) { t_skip("no closed handle available (no device)"); return; }
    for (int k = 0; k < 2; k++) EXPECT_EQ(c[k].get(h, NULL), WCM_INVALPARAM);
}

/* ---------------------------------------------------------------- not a handle -> 5, no id path */

TEST(wcm_pr_get_nonhandles_invalhandle)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    wcm_note_devices();
    for (int k = 0; k < 2; k++)
        for (const wcm_val *v = wcm_nonhandles; v->name; v++) {
            DWORD out = WCM_SENT;
            MMRESULT r = c[k].get((HWAVEOUT)v->v, &out);
            CHECKF(r == WCM_INVALHANDLE, "%s value %s r=%u", c[k].name, v->name, r);
            CHECKF(out == WCM_SENT, "%s value %s output touched 0x%lx", c[k].name, v->name, (unsigned long)out);
        }
}

TEST(wcm_pr_set_nonhandles_invalhandle)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    static const DWORD vals[] = { 0, 1, 0x8000, 0x10000, 0x20000, 0xFFFFFFFFu };
    for (int k = 0; k < 2; k++)
        for (const wcm_val *v = wcm_nonhandles; v->name; v++)
            for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
                MMRESULT r = c[k].set((HWAVEOUT)v->v, vals[i]);
                CHECKF(r == WCM_INVALHANDLE, "%s value %s set 0x%lx r=%u", c[k].name, v->name, (unsigned long)vals[i], r);
            }
}

/* Device ids, including 0 and the mapper, are not accepted here even when the devices exist. */
TEST(wcm_pr_device_ids_are_not_handles)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    UINT n = wcm_nout();
    UINT ids[] = { 0, 1, n, 0xFFFF, WAVE_MAPPER };
    for (int k = 0; k < 2; k++)
        for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
            DWORD out = WCM_SENT;
            CHECKF(c[k].get((HWAVEOUT)(ULONG_PTR)ids[i], &out) == WCM_INVALHANDLE, "%s get id 0x%x", c[k].name, ids[i]);
            EXPECT_EQ(out, WCM_SENT);
            CHECKF(c[k].set((HWAVEOUT)(ULONG_PTR)ids[i], 0x10000) == WCM_INVALHANDLE, "%s set id 0x%x", c[k].name, ids[i]);
        }
}

TEST(wcm_pr_closed_handle_invalhandle)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    HWAVEOUT h = wcm_closed_out();
    if (!h) { t_skip("no closed handle available (no device)"); return; }
    for (int k = 0; k < 2; k++) {
        DWORD out = WCM_SENT;
        EXPECT_EQ(c[k].get(h, &out), WCM_INVALHANDLE);
        EXPECT_EQ(out, WCM_SENT);
        EXPECT_EQ(c[k].set(h, 0x10000), WCM_INVALHANDLE);
    }
}

TEST(wcm_pr_closed_device_handle_invalhandle)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    HWAVEOUT h;
    EXPECT_EQ(wcm_open_out(&h, 0), 0);
    if (!h) return;
    EXPECT_EQ(U(waveOutClose)(h), 0);
    for (int k = 0; k < 2; k++) {
        DWORD out = WCM_SENT;
        EXPECT_EQ(c[k].get(h, &out), WCM_INVALHANDLE);
        EXPECT_EQ(out, WCM_SENT);
        EXPECT_EQ(c[k].set(h, 0x10000), WCM_INVALHANDLE);
    }
}

TEST(wcm_pr_wavein_handle_invalhandle)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    if (!wcm_nin()) { t_skip("no waveIn device"); return; }
    UINT ids[2] = { WAVE_MAPPER, 0 };
    for (int i = 0; i < 2; i++) {
        HWAVEIN hi;
        EXPECT_EQ(wcm_open_in(&hi, ids[i]), 0);
        if (!hi) continue;
        for (int k = 0; k < 2; k++) {
            DWORD out = WCM_SENT;
            CHECKF(c[k].get((HWAVEOUT)hi, &out) == WCM_INVALHANDLE, "%s waveIn id %u", c[k].name, ids[i]);
            EXPECT_EQ(out, WCM_SENT);
            CHECKF(c[k].set((HWAVEOUT)hi, 0x10000) == WCM_INVALHANDLE, "%s waveIn id %u", c[k].name, ids[i]);
            CHECKF(c[k].get((HWAVEOUT)hi, NULL) == WCM_INVALPARAM, "%s waveIn NULL id %u", c[k].name, ids[i]);
        }
        wcm_close_in(hi);
    }
}

/* A pointer into a live handle (not its start) is not the handle. */
TEST(wcm_pr_interior_pointer_invalhandle)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    HWAVEOUT h;
    EXPECT_EQ(wcm_open_out(&h, WAVE_MAPPER), 0);
    if (!h) return;
    static const int off[] = { 1, 2, 4, 8, -8 };
    for (int k = 0; k < 2; k++)
        for (size_t i = 0; i < sizeof off / sizeof off[0]; i++) {
            DWORD out = WCM_SENT;
            HWAVEOUT bad = (HWAVEOUT)((char *)h + off[i]);
            CHECKF(c[k].get(bad, &out) == WCM_INVALHANDLE, "%s offset %d", c[k].name, off[i]);
            EXPECT_EQ(out, WCM_SENT);
        }
    wcm_close_out(h);
}

/* ---------------------------------------------------------------- live handles */

static void live_probe(UINT id, const char *tag)
{
    ctl c[2];
    if (!ctl_table(c)) return;
    HWAVEOUT h;
    EXPECT_EQ(wcm_open_out(&h, id), 0);
    if (!h) return;
    for (int k = 0; k < 2; k++) {
        DWORD v = WCM_SENT;
        MMRESULT r = c[k].get(h, &v);
        t_note("PR %s %s get r=%u value=0x%08lx", tag, c[k].name, r, (unsigned long)v);
        DWORD back = (r == WCM_OK) ? v : 0x10000;
        MMRESULT rs = c[k].set(h, back);
        t_note("PR %s %s set(0x%lx) r=%u", tag, c[k].name, (unsigned long)back, rs);
        if (r == WCM_OK) {
            DWORD v2 = WCM_SENT;
            EXPECT_EQ(c[k].get(h, &v2), WCM_OK);
            EXPECT_EQ(v2, v);
        } else {
            CHECKF(v == WCM_SENT, "%s %s failed get touched output", tag, c[k].name);
        }
    }
    wcm_close_out(h);
}

TEST(wcm_pr_live_mapper_handle_record)
{
    REQUIRE(U(waveOutGetPitch));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    live_probe(WAVE_MAPPER, "mapper");
}

TEST(wcm_pr_live_device_handles_record)
{
    REQUIRE(U(waveOutGetPitch));
    UINT n = wcm_nout();
    if (!n) { t_skip("no waveOut device"); return; }
    wcm_note_devices();
    for (UINT id = 0; id < n && id < 4; id++) { char tag[16]; snprintf(tag, sizeof tag, "dev%u", id); live_probe(id, tag); }
}

/* waveOutMessage(h, WODM_GET*, p, 0) takes the same path as the API (waveMessage with dw2 = 0). */
TEST(wcm_pr_message_parity_on_live_handle)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    REQUIRE(U(waveOutMessage));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    UINT ids[2] = { WAVE_MAPPER, 0 };
    for (int i = 0; i < 2; i++) {
        HWAVEOUT h;
        EXPECT_EQ(wcm_open_out(&h, ids[i]), 0);
        if (!h) continue;
        for (int k = 0; k < 2; k++) {
            DWORD a = WCM_SENT, b = WCM_SENT;
            MMRESULT ra = c[k].get(h, &a);
            MMRESULT rb = U(waveOutMessage)(h, c[k].gmsg, (DWORD_PTR)&b, 0);
            CHECKF(ra == rb, "%s id %u api %u msg %u", c[k].name, ids[i], ra, rb);
            CHECKF(a == b, "%s id %u api 0x%lx msg 0x%lx", c[k].name, ids[i], (unsigned long)a, (unsigned long)b);
            DWORD back = ra == WCM_OK ? a : 0x10000;
            MMRESULT sa = c[k].set(h, back);
            MMRESULT sb = U(waveOutMessage)(h, c[k].smsg, back, 0);
            CHECKF(sa == sb, "%s set id %u api %u msg %u", c[k].name, ids[i], sa, sb);
        }
        wcm_close_out(h);
    }
}

/* The capability bits recorded next to what the driver answers. */
TEST(wcm_pr_caps_support_bits_record)
{
    REQUIRE(U(waveOutGetDevCapsW));
    UINT n = wcm_nout();
    if (!n) { t_skip("no waveOut device"); return; }
    for (UINT id = 0; id < n && id < 4; id++) {
        WAVEOUTCAPSW c; memset(&c, 0, sizeof c);
        EXPECT_EQ(U(waveOutGetDevCapsW)(id, &c, sizeof c), 0);
        t_note("CAPS dev%u dwSupport=0x%lx pitch=%d rate=%d volume=%d lrvolume=%d sync=%d sampleacc=%d", id,
               (unsigned long)c.dwSupport, !!(c.dwSupport & WAVECAPS_PITCH), !!(c.dwSupport & WAVECAPS_PLAYBACKRATE),
               !!(c.dwSupport & WAVECAPS_VOLUME), !!(c.dwSupport & WAVECAPS_LRVOLUME), !!(c.dwSupport & WAVECAPS_SYNC),
               !!(c.dwSupport & WAVECAPS_SAMPLEACCURATE));
    }
    WAVEOUTCAPSW m; memset(&m, 0, sizeof m);
    EXPECT_EQ(U(waveOutGetDevCapsW)(WAVE_MAPPER, &m, sizeof m), 0);
    t_note("CAPS mapper dwSupport=0x%lx", (unsigned long)m.dwSupport);
}

/* Each handle has its own state: reading one does not disturb another. */
TEST(wcm_pr_two_handles_independent)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    HWAVEOUT a, b;
    EXPECT_EQ(wcm_open_out(&a, 0), 0);
    EXPECT_EQ(wcm_open_out(&b, 0), 0);
    if (a && b) {
        for (int k = 0; k < 2; k++) {
            DWORD va = WCM_SENT, vb = WCM_SENT;
            MMRESULT ra = c[k].get(a, &va), rb = c[k].get(b, &vb);
            CHECKF(ra == rb, "%s %u vs %u", c[k].name, ra, rb);
            CHECKF(va == vb, "%s 0x%lx vs 0x%lx", c[k].name, (unsigned long)va, (unsigned long)vb);
        }
    }
    wcm_close_out(a);
    wcm_close_out(b);
}

/* A handle stays usable for these queries for its whole lifetime; repeated queries are stable. */
TEST(wcm_pr_repeat_stable)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    HWAVEOUT h;
    EXPECT_EQ(wcm_open_out(&h, 0), 0);
    if (!h) return;
    for (int k = 0; k < 2; k++) {
        DWORD v0 = WCM_SENT;
        MMRESULT r0 = c[k].get(h, &v0);
        for (int i = 0; i < 20; i++) {
            DWORD v = WCM_SENT;
            MMRESULT r = c[k].get(h, &v);
            EXPECT_EQ(r, r0);
            EXPECT_EQ(v, v0);
        }
    }
    wcm_close_out(h);
}

/* After a reset (nothing queued) and after pause/restart the handle still answers the same way. */
TEST(wcm_pr_after_reset_pause_restart)
{
    ctl c[2];
    REQUIRE(ctl_table(c));
    REQUIRE(U(waveOutReset));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    HWAVEOUT h;
    EXPECT_EQ(wcm_open_out(&h, 0), 0);
    if (!h) return;
    MMRESULT base[2]; DWORD bv[2];
    for (int k = 0; k < 2; k++) { bv[k] = WCM_SENT; base[k] = c[k].get(h, &bv[k]); }
    EXPECT_EQ(U(waveOutPause)(h), 0);
    for (int k = 0; k < 2; k++) { DWORD v = WCM_SENT; EXPECT_EQ(c[k].get(h, &v), base[k]); EXPECT_EQ(v, bv[k]); }
    EXPECT_EQ(U(waveOutRestart)(h), 0);
    EXPECT_EQ(U(waveOutReset)(h), 0);
    for (int k = 0; k < 2; k++) { DWORD v = WCM_SENT; EXPECT_EQ(c[k].get(h, &v), base[k]); EXPECT_EQ(v, bv[k]); }
    wcm_close_out(h);
}
