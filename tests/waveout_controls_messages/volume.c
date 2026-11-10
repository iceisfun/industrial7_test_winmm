/* waveOutGetVolume / waveOutSetVolume (winmm ord 171 / 180, bodies in winmmbase !0x10150 / !0x10210).
 *
 * winmmbase: GetVolume checks pdwVolume == NULL -> MMSYSERR_INVALPARAM (11) before anything else. Both then
 * turn the value 0xFFFF into WAVE_MAPPER, and accept either a live HWAVEOUT (ValidateHandle type 1 ->
 * waveMessage WODM_GETVOLUME/SETVOLUME) or a device id (waveIDMessage on the low 32 bits: id < count or
 * id == 0xFFFFFFFF -> driver, otherwise MMSYSERR_BADDEVICEID (2), output untouched).
 *
 * SetVolume is only called here as a write-back of the value just read on a handle the test opened, or with
 * values that cannot reach any device. Real volume changes belong to the dx_ mixer area. */
#include "wcm.h"

/* ---------------------------------------------------------------- exports / forwarding */

TEST(wcm_vol_exports_resolve)
{
    REQUIRE(U(waveOutGetVolume));
    REQUIRE(U(waveOutSetVolume));
    CHECK(UIN("winmm.dll", waveOutGetVolume) != NULL);
    CHECK(UIN("winmm.dll", waveOutSetVolume) != NULL);
    CHECK(UIN("winmmbase.dll", waveOutGetVolume) != NULL);
    CHECK(UIN("winmmbase.dll", waveOutSetVolume) != NULL);
    EXPECT_EQ(t_proc_ord("winmm.dll", 171), t_proc_in("winmm.dll", "waveOutGetVolume"));
    EXPECT_EQ(t_proc_ord("winmm.dll", 180), t_proc_in("winmm.dll", "waveOutSetVolume"));
    EXPECT_EQ(t_proc_ord("winmmbase.dll", 126), t_proc_in("winmmbase.dll", "waveOutGetVolume"));
    EXPECT_EQ(t_proc_ord("winmmbase.dll", 135), t_proc_in("winmmbase.dll", "waveOutSetVolume"));
}

TEST(wcm_vol_winmm_and_winmmbase_agree)
{
    REQUIRE(U(waveOutGetVolume));
    __typeof__(&waveOutGetVolume) a = UIN("winmm.dll", waveOutGetVolume);
    __typeof__(&waveOutGetVolume) b = UIN("winmmbase.dll", waveOutGetVolume);
    REQUIRE(a); REQUIRE(b);
    EXPECT_EQ(a((HWAVEOUT)0, NULL), WCM_INVALPARAM);
    EXPECT_EQ(b((HWAVEOUT)0, NULL), WCM_INVALPARAM);
    DWORD va = WCM_SENT, vb = WCM_SENT;
    EXPECT_EQ(a((HWAVEOUT)(ULONG_PTR)9999, &va), WCM_BADDEVICEID);
    EXPECT_EQ(b((HWAVEOUT)(ULONG_PTR)9999, &vb), WCM_BADDEVICEID);
    EXPECT_EQ(va, WCM_SENT);
    EXPECT_EQ(vb, WCM_SENT);
    UINT n = wcm_nout();
    for (UINT id = 0; id < n && id < 4; id++) {
        va = vb = WCM_SENT;
        MMRESULT ra = a((HWAVEOUT)(ULONG_PTR)id, &va), rb = b((HWAVEOUT)(ULONG_PTR)id, &vb);
        EXPECT_EQ(ra, rb);
        EXPECT_EQ(va, vb);
    }
}

/* ---------------------------------------------------------------- NULL output pointer: 11 first */

TEST(wcm_vol_get_null_ptr_nonhandles)
{
    REQUIRE(U(waveOutGetVolume));
    wcm_note_devices();
    for (const wcm_val *v = wcm_nonhandles; v->name; v++)
        CHECKF(U(waveOutGetVolume)((HWAVEOUT)v->v, NULL) == WCM_INVALPARAM, "value %s", v->name);
}

TEST(wcm_vol_get_null_ptr_ids)
{
    REQUIRE(U(waveOutGetVolume));
    static const UINT ids[] = { 0, 1, 2, 3, 9, 15, 255, 9999, 0xFFFE, 0xFFFF, 0x10000, 0x7FFFFFFF, 0xFFFFFFFE, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++)
        CHECKF(U(waveOutGetVolume)((HWAVEOUT)(ULONG_PTR)ids[i], NULL) == WCM_INVALPARAM, "id 0x%x", ids[i]);
}

TEST(wcm_vol_get_null_ptr_live_handles)
{
    REQUIRE(U(waveOutGetVolume));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    HWAVEOUT h;
    EXPECT_EQ(wcm_open_out(&h, WAVE_MAPPER), 0);
    if (h) { EXPECT_EQ(U(waveOutGetVolume)(h, NULL), WCM_INVALPARAM); wcm_close_out(h); }
    EXPECT_EQ(wcm_open_out(&h, 0), 0);
    if (h) { EXPECT_EQ(U(waveOutGetVolume)(h, NULL), WCM_INVALPARAM); wcm_close_out(h); }
}

TEST(wcm_vol_get_null_ptr_closed_handle)
{
    REQUIRE(U(waveOutGetVolume));
    HWAVEOUT c = wcm_closed_out();
    if (!c) { t_skip("no closed handle available (no device)"); return; }
    EXPECT_EQ(U(waveOutGetVolume)(c, NULL), WCM_INVALPARAM);
}

/* ---------------------------------------------------------------- id path, out of range */

TEST(wcm_vol_get_out_of_range_ids)
{
    REQUIRE(U(waveOutGetVolume));
    UINT n = wcm_nout();
    UINT ids[] = { n, n + 1, n + 7, 9999, 0xFFFE, 0x10000, 0x10001, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFEu };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        DWORD v = WCM_SENT;
        MMRESULT r = U(waveOutGetVolume)((HWAVEOUT)(ULONG_PTR)ids[i], &v);
        CHECKF(r == WCM_BADDEVICEID, "id 0x%x r=%u", ids[i], r);
        CHECKF(v == WCM_SENT, "id 0x%x output touched 0x%lx", ids[i], (unsigned long)v);
    }
}

TEST(wcm_vol_set_out_of_range_ids)
{
    REQUIRE(U(waveOutSetVolume));
    UINT n = wcm_nout();
    UINT ids[] = { n, n + 1, n + 7, 9999, 0xFFFE, 0x10000, 0x10001, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFEu };
    static const DWORD vols[] = { 0, 0xFFFFFFFFu, 0x80008000u };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++)
        for (size_t k = 0; k < 3; k++) {
            MMRESULT r = U(waveOutSetVolume)((HWAVEOUT)(ULONG_PTR)ids[i], vols[k]);
            CHECKF(r == WCM_BADDEVICEID, "id 0x%x vol 0x%lx r=%u", ids[i], (unsigned long)vols[k], r);
        }
}

/* The value is a HWAVEOUT parameter: on x64 only the low 32 bits become the id. */
TEST(wcm_vol_get_nonhandle_table)
{
    REQUIRE(U(waveOutGetVolume));
    UINT n = wcm_nout();
    for (const wcm_val *v = wcm_nonhandles; v->name; v++) {
        DWORD vol = WCM_SENT;
        MMRESULT r = U(waveOutGetVolume)((HWAVEOUT)v->v, &vol);
        int cls = wcm_id_class(v->v, n, 1);
        if (cls == WCM_ID_BAD) {
            CHECKF(r == WCM_BADDEVICEID, "value %s r=%u", v->name, r);
            CHECKF(vol == WCM_SENT, "value %s output touched", v->name);
        } else if (cls == WCM_ID_DEVICE) {
            CHECKF(r == WCM_OK, "value %s (device id %u) r=%u", v->name, (UINT)v->v, r);
        } else if (n == 0 && !wcm_mapper_present()) {
            CHECKF(r == WCM_BADDEVICEID, "value %s (mapper, no devices) r=%u", v->name, r);
            CHECKF(vol == WCM_SENT, "value %s output touched", v->name);
        } else {
            t_note("GETVOL mapper-class value %s r=%u vol=0x%lx", v->name, r, (unsigned long)vol);
        }
    }
}

TEST(wcm_vol_set_nonhandle_table_unreachable_only)
{
    REQUIRE(U(waveOutSetVolume));
    UINT n = wcm_nout();
    int tried = 0;
    for (const wcm_val *v = wcm_nonhandles; v->name; v++) {
        int cls = wcm_id_class(v->v, n, 1);
        int safe = cls == WCM_ID_BAD || (n == 0 && !wcm_mapper_present());
        if (!safe) continue;          /* never write a real device or the mapper */
        tried++;
        MMRESULT r = U(waveOutSetVolume)((HWAVEOUT)v->v, 0x40004000u);
        CHECKF(r == WCM_BADDEVICEID, "value %s r=%u", v->name, r);
    }
    CHECK(tried > 0);
}

/* ---------------------------------------------------------------- zero devices */

TEST(wcm_vol_zero_devices_contract)
{
    REQUIRE(U(waveOutGetVolume));
    if (wcm_nout() || wcm_mapper_present()) { t_skip("devices present (contract covered by the device half)"); return; }
    static const ULONG_PTR vals[] = { 0, 1, 0xFFFF, 0xFFFFFFFFu, (ULONG_PTR)~(ULONG_PTR)0, 9999 };
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        DWORD v = WCM_SENT;
        CHECKF(U(waveOutGetVolume)((HWAVEOUT)vals[i], &v) == WCM_BADDEVICEID, "value 0x%llx", (unsigned long long)vals[i]);
        EXPECT_EQ(v, WCM_SENT);
        CHECKF(U(waveOutSetVolume)((HWAVEOUT)vals[i], 0xFFFFFFFFu) == WCM_BADDEVICEID, "set value 0x%llx",
               (unsigned long long)vals[i]);
        CHECKF(U(waveOutGetVolume)((HWAVEOUT)vals[i], NULL) == WCM_INVALPARAM, "NULL value 0x%llx",
               (unsigned long long)vals[i]);
    }
}

TEST(wcm_vol_zero_devices_open_fails)
{
    REQUIRE(U(waveOutOpen));
    if (wcm_nout()) { t_skip("devices present"); return; }
    HWAVEOUT h = (HWAVEOUT)(ULONG_PTR)0x1234;
    WAVEFORMATEX f; wcm_fmt(&f, 44100, 16, 2);
    EXPECT_EQ(U(waveOutOpen)(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL), WCM_BADDEVICEID);
    DWORD v = WCM_SENT;
    /* even if the open had left a value behind, it must not be a usable handle */
    MMRESULT r = U(waveOutGetVolume)(h, &v);
    EXPECT_EQ(r, WCM_BADDEVICEID);
    EXPECT_EQ(v, WCM_SENT);
    t_note("OPEN0 handle after failed open=%p", (void *)h);
}

/* ---------------------------------------------------------------- device ids */

/* UNVERIFIED: pin after the first Win10 run */
TEST(wcm_vol_get_device_ids)
{
    REQUIRE(U(waveOutGetVolume));
    UINT n = wcm_nout();
    if (!n) { t_skip("no waveOut device"); return; }
    wcm_note_devices();
    for (UINT id = 0; id < n; id++) {
        DWORD v = WCM_SENT;
        MMRESULT r = U(waveOutGetVolume)((HWAVEOUT)(ULONG_PTR)id, &v);
        EXPECT_EQ(r, WCM_OK);
        t_note("GETVOL id %u r=%u vol=0x%08lx", id, r, (unsigned long)v);
    }
}

TEST(wcm_vol_get_device_ids_stable)
{
    REQUIRE(U(waveOutGetVolume));
    UINT n = wcm_nout();
    if (!n) { t_skip("no waveOut device"); return; }
    for (UINT id = 0; id < n; id++) {
        DWORD first = WCM_SENT;
        EXPECT_EQ(U(waveOutGetVolume)((HWAVEOUT)(ULONG_PTR)id, &first), WCM_OK);
        for (int i = 0; i < 16; i++) {
            DWORD v = WCM_SENT;
            EXPECT_EQ(U(waveOutGetVolume)((HWAVEOUT)(ULONG_PTR)id, &v), WCM_OK);
            CHECKF(v == first, "id %u read %d: 0x%lx != 0x%lx", id, i, (unsigned long)v, (unsigned long)first);
        }
    }
}

/* Only four bytes are written. */
TEST(wcm_vol_get_writes_exactly_one_dword)
{
    REQUIRE(U(waveOutGetVolume));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    static const ULONG_PTR vals[] = { 0, 0xFFFF, 0xFFFFFFFFu };
    for (size_t i = 0; i < 3; i++) {
        unsigned char buf[16];
        memset(buf, 0xA5, sizeof buf);
        MMRESULT r = U(waveOutGetVolume)((HWAVEOUT)vals[i], (DWORD *)(buf + 4));
        CHECKF(r == WCM_OK, "value 0x%llx r=%u", (unsigned long long)vals[i], r);
        for (int k = 0; k < 4; k++) CHECKF(buf[k] == 0xA5, "value 0x%llx byte %d before touched", (unsigned long long)vals[i], k);
        for (int k = 8; k < 16; k++) CHECKF(buf[k] == 0xA5, "value 0x%llx byte %d after touched", (unsigned long long)vals[i], k);
    }
}

TEST(wcm_vol_get_mapper_spellings_agree)
{
    REQUIRE(U(waveOutGetVolume));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    static const ULONG_PTR vals[] = { 0xFFFF, 0xFFFFFFFFu, (ULONG_PTR)~(ULONG_PTR)0, (ULONG_PTR)0x1FFFFFFFFull,
                                      (ULONG_PTR)0xABCD0000FFFFFFFFull };
    DWORD v0 = WCM_SENT;
    MMRESULT r0 = U(waveOutGetVolume)((HWAVEOUT)vals[0], &v0);
    t_note("GETVOL mapper 0xFFFF r=%u vol=0x%08lx", r0, (unsigned long)v0);
    for (size_t i = 1; i < sizeof vals / sizeof vals[0]; i++) {
        DWORD v = WCM_SENT;
        MMRESULT r = U(waveOutGetVolume)((HWAVEOUT)vals[i], &v);
        CHECKF(r == r0, "value 0x%llx r=%u vs %u", (unsigned long long)vals[i], r, r0);
        CHECKF(v == v0, "value 0x%llx vol 0x%lx vs 0x%lx", (unsigned long long)vals[i], (unsigned long)v, (unsigned long)v0);
    }
}

/* 0x100000000 and 0x123400000000 truncate to device id 0. */
TEST(wcm_vol_get_high_bits_truncate_to_id)
{
    REQUIRE(U(waveOutGetVolume));
    UINT n = wcm_nout();
    static const ULONG_PTR hi[] = { 0x100000000ull, 0x123400000000ull, 0x7FFF00000000ull };
    for (size_t i = 0; i < 3; i++) {
        DWORD a = WCM_SENT, b = WCM_SENT;
        MMRESULT ra = U(waveOutGetVolume)((HWAVEOUT)hi[i], &a);
        MMRESULT rb = U(waveOutGetVolume)((HWAVEOUT)0, &b);
        EXPECT_EQ(ra, rb);
        EXPECT_EQ(a, b);
        EXPECT_EQ(ra, n ? WCM_OK : WCM_BADDEVICEID);
    }
}

/* ---------------------------------------------------------------- handles */

TEST(wcm_vol_get_mapper_handle)
{
    REQUIRE(U(waveOutGetVolume));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    HWAVEOUT h;
    EXPECT_EQ(wcm_open_out(&h, WAVE_MAPPER), 0);
    if (!h) return;
    DWORD v = WCM_SENT;
    EXPECT_EQ(U(waveOutGetVolume)(h, &v), WCM_OK);
    t_note("GETVOL mapper handle vol=0x%08lx", (unsigned long)v);
    /* live session volume on Win10: no fixed value; write-back round trip only */
    EXPECT_EQ(U(waveOutSetVolume)(h, v), 0);
    wcm_close_out(h);
}

TEST(wcm_vol_get_device_handles)
{
    REQUIRE(U(waveOutGetVolume));
    UINT n = wcm_nout();
    if (!n) { t_skip("no waveOut device"); return; }
    for (UINT id = 0; id < n && id < 4; id++) {
        HWAVEOUT h;
        MMRESULT o = wcm_open_out(&h, id);
        EXPECT_EQ(o, 0);
        if (!h) continue;
        DWORD v = WCM_SENT, vid = WCM_SENT;
        EXPECT_EQ(U(waveOutGetVolume)(h, &v), WCM_OK);
        MMRESULT rid = U(waveOutGetVolume)((HWAVEOUT)(ULONG_PTR)id, &vid);
        t_note("GETVOL id %u handle vol=0x%08lx  by-id r=%u vol=0x%08lx", id, (unsigned long)v, rid, (unsigned long)vid);
        wcm_close_out(h);
    }
}

TEST(wcm_vol_get_handle_stable_across_reads)
{
    REQUIRE(U(waveOutGetVolume));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    HWAVEOUT h;
    EXPECT_EQ(wcm_open_out(&h, 0), 0);
    if (!h) return;
    DWORD first = WCM_SENT;
    EXPECT_EQ(U(waveOutGetVolume)(h, &first), WCM_OK);
    for (int i = 0; i < 32; i++) {
        DWORD v = WCM_SENT;
        EXPECT_EQ(U(waveOutGetVolume)(h, &v), WCM_OK);
        EXPECT_EQ(v, first);
    }
    wcm_close_out(h);
}

/* Two handles on the same device at once both answer. */
TEST(wcm_vol_get_two_handles)
{
    REQUIRE(U(waveOutGetVolume));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    HWAVEOUT a, b;
    EXPECT_EQ(wcm_open_out(&a, 0), 0);
    EXPECT_EQ(wcm_open_out(&b, WAVE_MAPPER), 0);
    if (a && b) {
        CHECK(a != b);
        DWORD va = WCM_SENT, vb = WCM_SENT;
        EXPECT_EQ(U(waveOutGetVolume)(a, &va), WCM_OK);
        EXPECT_EQ(U(waveOutGetVolume)(b, &vb), WCM_OK);
        t_note("GETVOL two handles dev0=0x%08lx mapper=0x%08lx", (unsigned long)va, (unsigned long)vb);
    }
    wcm_close_out(a);
    wcm_close_out(b);
}

TEST(wcm_vol_get_closed_handle_is_an_id)
{
    REQUIRE(U(waveOutGetVolume));
    HWAVEOUT c = wcm_closed_out();
    if (!c) { t_skip("no closed handle available (no device)"); return; }
    UINT n = wcm_nout();
    int cls = wcm_id_class((ULONG_PTR)c, n, 1);
    DWORD v = WCM_SENT;
    MMRESULT r = U(waveOutGetVolume)(c, &v);
    if (cls == WCM_ID_BAD) { EXPECT_EQ(r, WCM_BADDEVICEID); EXPECT_EQ(v, WCM_SENT); }
    else t_note("closed handle %p truncates to a live id, r=%u", (void *)c, r);
    if (cls == WCM_ID_BAD) EXPECT_EQ(U(waveOutSetVolume)(c, 0x12341234u), WCM_BADDEVICEID);
}

TEST(wcm_vol_get_wavein_handle_is_an_id)
{
    REQUIRE(U(waveOutGetVolume));
    if (!wcm_nin()) { t_skip("no waveIn device"); return; }
    HWAVEIN hi;
    EXPECT_EQ(wcm_open_in(&hi, WAVE_MAPPER), 0);
    if (!hi) return;
    int cls = wcm_id_class((ULONG_PTR)hi, wcm_nout(), 1);
    DWORD v = WCM_SENT;
    MMRESULT r = U(waveOutGetVolume)((HWAVEOUT)hi, &v);
    if (cls == WCM_ID_BAD) {
        EXPECT_EQ(r, WCM_BADDEVICEID);
        EXPECT_EQ(v, WCM_SENT);
        EXPECT_EQ(U(waveOutSetVolume)((HWAVEOUT)hi, 0x11111111u), WCM_BADDEVICEID);
    }
    EXPECT_EQ(U(waveOutGetVolume)((HWAVEOUT)hi, NULL), WCM_INVALPARAM);
    wcm_close_in(hi);
}

/* ---------------------------------------------------------------- SetVolume write-back (no change) */

static void writeback_on(UINT id, const char *tag)
{
    HWAVEOUT h;
    EXPECT_EQ(wcm_open_out(&h, id), 0);
    if (!h) return;
    DWORD v = WCM_SENT;
    MMRESULT rg = U(waveOutGetVolume)(h, &v);
    EXPECT_EQ(rg, WCM_OK);
    if (rg == WCM_OK) {
        MMRESULT rs = U(waveOutSetVolume)(h, v);
        t_note("SETVOL %s write-back 0x%08lx r=%u", tag, (unsigned long)v, rs);
        EXPECT_EQ(rs, WCM_OK);
        DWORD after = WCM_SENT;
        EXPECT_EQ(U(waveOutGetVolume)(h, &after), WCM_OK);
        EXPECT_EQ(after, v);
        if (after != v) U(waveOutSetVolume)(h, v);   /* unconditional restore attempt */
    }
    wcm_close_out(h);
}

TEST(wcm_vol_set_writeback_mapper_handle)
{
    REQUIRE(U(waveOutSetVolume));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    writeback_on(WAVE_MAPPER, "mapper");
}

TEST(wcm_vol_set_writeback_device_handles)
{
    REQUIRE(U(waveOutSetVolume));
    UINT n = wcm_nout();
    if (!n) { t_skip("no waveOut device"); return; }
    for (UINT id = 0; id < n && id < 4; id++) { char tag[16]; snprintf(tag, sizeof tag, "dev%u", id); writeback_on(id, tag); }
}

TEST(wcm_vol_set_writeback_twice_idempotent)
{
    REQUIRE(U(waveOutSetVolume));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    HWAVEOUT h;
    EXPECT_EQ(wcm_open_out(&h, 0), 0);
    if (!h) return;
    DWORD v = WCM_SENT;
    if (U(waveOutGetVolume)(h, &v) == WCM_OK) {
        for (int i = 0; i < 3; i++) EXPECT_EQ(U(waveOutSetVolume)(h, v), WCM_OK);
        DWORD after = WCM_SENT;
        EXPECT_EQ(U(waveOutGetVolume)(h, &after), WCM_OK);
        EXPECT_EQ(after, v);
        U(waveOutSetVolume)(h, v);
    }
    wcm_close_out(h);
}

/* After close the device id still reports the same value as before the open (nothing changed). */
TEST(wcm_vol_writeback_leaves_device_value)
{
    REQUIRE(U(waveOutSetVolume));
    if (!wcm_nout()) { t_skip("no waveOut device"); return; }
    DWORD before = WCM_SENT, after = WCM_SENT;
    EXPECT_EQ(U(waveOutGetVolume)((HWAVEOUT)0, &before), WCM_OK);
    writeback_on(0, "dev0-cycle");
    EXPECT_EQ(U(waveOutGetVolume)((HWAVEOUT)0, &after), WCM_OK);
    EXPECT_EQ(after, before);
}
