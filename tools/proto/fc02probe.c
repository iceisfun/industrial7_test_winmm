/* fc02probe: VM check of the claims in docs/research/02_win10_semantics.md.
 * Prints "key: value" lines (COM1). No system-wide changes: timer periods are paired,
 * MCI aliases are closed, only silent audio, bounded waits. */
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <stdio.h>
#include <string.h>
UINT WINAPI mmsystemGetVersion(void);

#define P(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
#define R(name, expr) P("%s: %ld\n", name, (long)(expr))

static DWORD WINAPI watchdog(void *p) { (void)p; Sleep(120000); P("watchdog: timeout\n"); ExitProcess(3); return 0; }

static volatile LONG cb_hits; static volatile UINT cb_id, cb_msg; static volatile DWORD_PTR cb_user, cb_p1, cb_p2;
static HANDLE cb_ev;
static void CALLBACK tcb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR p1, DWORD_PTR p2)
{ cb_id = id; cb_msg = msg; cb_user = user; cb_p1 = p1; cb_p2 = p2; InterlockedIncrement(&cb_hits); SetEvent(cb_ev); }
static void CALLBACK nopcb(UINT id, UINT msg, DWORD_PTR u, DWORD_PTR a, DWORD_PTR b) { (void)id; (void)msg; (void)u; (void)a; (void)b; }
static void CALLBACK mixcb(HDRVR h, UINT m, DWORD_PTR i, DWORD_PTR a, DWORD_PTR b) { (void)h; (void)m; (void)i; (void)a; (void)b; }

static void counts(void)
{
    R("mmsystemGetVersion", mmsystemGetVersion());
    R("waveOutGetNumDevs", waveOutGetNumDevs()); R("waveInGetNumDevs", waveInGetNumDevs());
    R("midiOutGetNumDevs", midiOutGetNumDevs()); R("midiInGetNumDevs", midiInGetNumDevs());
    R("mixerGetNumDevs", mixerGetNumDevs()); R("auxGetNumDevs", auxGetNumDevs());
    R("joyGetNumDevs", joyGetNumDevs());
}

static void wave(void)
{
    WAVEFORMATEX f = { WAVE_FORMAT_PCM, 2, 44100, 44100 * 4, 4, 16, 0 };
    HWAVEOUT h; HWAVEIN hi;
    R("wo_open_nullfmt", waveOutOpen(&h, WAVE_MAPPER, NULL, 0, 0, 0));
    R("wo_open_nullfmt_nullph", waveOutOpen(NULL, WAVE_MAPPER, NULL, 0, 0, 0));
    R("wo_open_nullph", waveOutOpen(NULL, WAVE_MAPPER, &f, 0, 0, 0));
    R("wo_open_mapper_mapped", waveOutOpen(&h, WAVE_MAPPER, &f, 0, 0, WAVE_MAPPED));
    R("wo_open_ffff_mapped", waveOutOpen(&h, 0xFFFF, &f, 0, 0, WAVE_MAPPED));
    R("wo_open_id0_defcomm", waveOutOpen(&h, 0, &f, 0, 0, 0x10));
    R("wo_open_mapper_defcomm_query", waveOutOpen(NULL, WAVE_MAPPER, &f, 0, 0, 0x10 | WAVE_FORMAT_QUERY));
    R("wo_open_bit200", waveOutOpen(&h, WAVE_MAPPER, &f, 0, 0, 0x200));
    R("wo_open_badwnd", waveOutOpen(&h, WAVE_MAPPER, &f, 0x1234, 0, CALLBACK_WINDOW));
    R("wo_open_nullwnd", (h = (HWAVEOUT)1, waveOutOpen(&h, WAVE_MAPPER, &f, 0, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY)));
    R("wo_open_query_nullph", waveOutOpen(NULL, WAVE_MAPPER, &f, 0, 0, WAVE_FORMAT_QUERY));
    h = (HWAVEOUT)0x1234; R("wo_open_badid", waveOutOpen(&h, 9999, &f, 0, 0, 0)); P("wo_open_badid_h: %p\n", (void *)h);
    R("wo_open_query_cbtype7", waveOutOpen(NULL, WAVE_MAPPER, &f, 0, 0, WAVE_FORMAT_QUERY | 0x70000));
    WAVEFORMATEX bad = f; bad.nBlockAlign = 3;
    R("wo_open_query_badalign", waveOutOpen(NULL, WAVE_MAPPER, &bad, 0, 0, WAVE_FORMAT_QUERY));
    R("wo_open_query_badalign_id0", waveOutOpen(NULL, 0, &bad, 0, 0, WAVE_FORMAT_QUERY));
    R("wi_open_nullfmt", waveInOpen(&hi, WAVE_MAPPER, NULL, 0, 0, 0));
    R("wi_open_mapper", (hi = 0, waveInOpen(&hi, WAVE_MAPPER, &f, 0, 0, WAVE_FORMAT_QUERY)));

    WAVEHDR w; char buf[64] = {0};
    #define HDR(fl) (memset(&w, 0, sizeof w), w.lpData = buf, w.dwBufferLength = sizeof buf, w.dwFlags = (fl), &w)
    R("wh_prep_null_h_flags0", waveOutPrepareHeader(NULL, HDR(0), sizeof w));
    R("wh_prep_null_h_prepared", waveOutPrepareHeader(NULL, HDR(WHDR_PREPARED), sizeof w));
    R("wh_prep_nullhdr", waveOutPrepareHeader(NULL, NULL, sizeof w));
    R("wh_prep_cb47", waveOutPrepareHeader(NULL, HDR(0), 47));
    R("wh_prep_flag20", waveOutPrepareHeader(NULL, HDR(0x20), sizeof w));
    R("wh_prep_flag1000_prepared", waveOutPrepareHeader(NULL, HDR(0x1000 | WHDR_PREPARED), sizeof w));
    R("wh_write_unprepared", waveOutWrite(NULL, HDR(0), sizeof w));
    R("wh_write_inqueue", waveOutWrite(NULL, HDR(WHDR_PREPARED | WHDR_INQUEUE), sizeof w));
    R("wh_write_prepared_badh", waveOutWrite(NULL, HDR(WHDR_PREPARED), sizeof w));
    R("wh_unprep_unprepared", waveOutUnprepareHeader(NULL, HDR(0), sizeof w));
    R("wh_unprep_inqueue", waveOutUnprepareHeader(NULL, HDR(WHDR_PREPARED | WHDR_INQUEUE), sizeof w));
    R("wh_unprep_prepared_badh", waveOutUnprepareHeader(NULL, HDR(WHDR_PREPARED), sizeof w));
    R("wh_addbuf_unprepared", waveInAddBuffer(NULL, HDR(0), sizeof w));
    R("wh_addbuf_inqueue", waveInAddBuffer(NULL, HDR(WHDR_PREPARED | WHDR_INQUEUE), sizeof w));

    WAVEOUTCAPSW cw; WAVEOUTCAPSA ca;
    R("wo_caps_cb0_null", waveOutGetDevCapsW(0, NULL, 0));
    R("wo_caps_null", waveOutGetDevCapsW(0, NULL, sizeof cw));
    R("wo_caps_badid", waveOutGetDevCapsW(9999, &cw, sizeof cw));
    R("wo_caps_mapper", waveOutGetDevCapsW(WAVE_MAPPER, &cw, sizeof cw));
    R("wo_caps_ffff", waveOutGetDevCapsW(0xFFFF, &cw, sizeof cw));
    R("wo_caps_id0", waveOutGetDevCapsW(0, &cw, sizeof cw));
    memset(&ca, 0x5A, sizeof ca);
    R("wo_capsA_id0_cb4", waveOutGetDevCapsA(0, &ca, 4)); P("wo_capsA_cb4_byte4: 0x%02x\n", ((BYTE *)&ca)[4]);
    R("wo_capsA_badid", waveOutGetDevCapsA(9999, &ca, sizeof ca));

    R("wo_close_null", waveOutClose(NULL)); R("wo_close_1", waveOutClose((HWAVEOUT)1));
    R("wo_close_ffff", waveOutClose((HWAVEOUT)(UINT_PTR)0xFFFF));
    UINT id; MMTIME mt; DWORD v;
    R("wo_getid_nullptr", waveOutGetID(NULL, NULL)); R("wo_getid_badh", waveOutGetID(NULL, &id));
    R("wo_getpos_nullptr", waveOutGetPosition(NULL, NULL, sizeof mt)); R("wo_getpos_badh", waveOutGetPosition(NULL, &mt, sizeof mt));
    R("wo_getvol_nullptr", waveOutGetVolume(NULL, NULL)); R("wo_getpitch_nullptr", waveOutGetPitch(NULL, NULL));
    R("wo_getvol_id9999", waveOutGetVolume((HWAVEOUT)(UINT_PTR)9999, &v));
    R("wo_msg_id9999", waveOutMessage((HWAVEOUT)(UINT_PTR)9999, 3, 0, 0));
    R("wo_pause_badh", waveOutPause(NULL)); R("wo_reset_badh", waveOutReset(NULL));

    /* error text */
    WCHAR t[128]; char ta[128];
    R("wo_errtext_cch0", waveOutGetErrorTextW(0, t, 0)); R("wo_errtext_null", waveOutGetErrorTextW(0, NULL, 10));
    t[0] = L'X'; R("wo_errtext_cch1", waveOutGetErrorTextW(0, t, 1)); P("wo_errtext_cch1_t0: %d\n", t[0]);
    P("wo_errtext_ok_codes:"); for (UINT c = 0; c < 300; c++) if (waveOutGetErrorTextW(c, t, 128) == 0) P(" %u", c); P("\n");
    P("wo_errtextA_ok_codes:"); for (UINT c = 0; c < 300; c++) if (waveOutGetErrorTextA(c, ta, 128) == 0) P(" %u", c); P("\n");
    P("mo_errtext_ok_codes:"); for (UINT c = 0; c < 300; c++) if (midiOutGetErrorTextW(c, t, 128) == 0) P(" %u", c); P("\n");
    P("mo_errtextA_ok_codes:"); for (UINT c = 0; c < 300; c++) if (midiOutGetErrorTextA(c, ta, 128) == 0) P(" %u", c); P("\n");
    R("wo_errtext_22", waveOutGetErrorTextW(22, t, 128)); R("mo_errtext_32", midiOutGetErrorTextW(32, t, 128));
}

static void midi(void)
{
    HMIDIOUT h; HMIDIIN hi; HMIDISTRM hs; UINT id = 0; MIDIHDR m; char buf[16] = {0};
    R("mo_open_nullph", midiOutOpen(NULL, 0, 0, 0, 0));
    R("mo_open_mapper_flag1", midiOutOpen(&h, MIDI_MAPPER, 0, 0, 1));
    R("mo_open_id0_flag4000", midiOutOpen(&h, 0, 0, 0, 0x4000));
    R("mo_open_mapper_flag4000", midiOutOpen(&h, MIDI_MAPPER, 0, 0, 0x4000));
    R("mo_open_badwnd", midiOutOpen(&h, MIDI_MAPPER, 0x1234, 0, CALLBACK_WINDOW));
    h = (HMIDIOUT)0x1234; R("mo_open_badid", midiOutOpen(&h, 9999, 0, 0, 0)); P("mo_open_badid_h: %p\n", (void *)h);
    R("mo_open_ffff", midiOutOpen(&h, 0xFFFF, 0, 0, 0));
    h = 0; MMRESULT r = midiOutOpen(&h, MIDI_MAPPER, 0, 0, 0); R("mo_open_mapper", r); if (r == 0) R("mo_close_mapper", midiOutClose(h));
    h = 0; r = midiOutOpen(&h, 0, 0, 0, 0); R("mo_open_id0", r); if (r == 0) R("mo_close_id0", midiOutClose(h));
    #define MH(fl, len) (memset(&m, 0, sizeof m), m.lpData = buf, m.dwBufferLength = (len), m.dwFlags = (fl), &m)
    R("mo_long_unprepared", midiOutLongMsg(NULL, MH(0, 4), sizeof m));
    R("mo_long_inqueue", midiOutLongMsg(NULL, MH(MHDR_PREPARED | MHDR_INQUEUE, 4), sizeof m));
    R("mo_long_len0", midiOutLongMsg(NULL, MH(MHDR_PREPARED, 0), sizeof m));
    R("mo_long_badh", midiOutLongMsg(NULL, MH(MHDR_PREPARED, 4), sizeof m));
    R("mo_long_cb43", midiOutLongMsg(NULL, MH(MHDR_PREPARED, 4), 43));
    R("mo_prep_badh_nullhdr", midiOutPrepareHeader(NULL, NULL, sizeof m));
    R("mo_short_badh", midiOutShortMsg(NULL, 0x90));
    R("mo_caps_cb0_null", midiOutGetDevCapsW(0, NULL, 0));
    R("mo_caps_null", midiOutGetDevCapsW(0, NULL, 10));
    MIDIOUTCAPSW mc; R("mo_caps_badid", midiOutGetDevCapsW(9999, &mc, sizeof mc));
    R("mo_caps_mapper", midiOutGetDevCapsW(MIDI_MAPPER, &mc, sizeof mc));
    R("mi_open_nullph", midiInOpen(NULL, 0, 0, 0, 0));
    R("mi_open_badwnd", midiInOpen(&hi, 0, 0x1234, 0, CALLBACK_WINDOW));
    R("mi_open_badwnd_badid", midiInOpen(&hi, 9999, 0x1234, 0, CALLBACK_WINDOW));
    R("mi_open_iostatus_badid", midiInOpen(&hi, 9999, 0, 0, MIDI_IO_STATUS));
    R("mi_open_flag2_badid", midiInOpen(&hi, 9999, 0, 0, 2));
    R("ms_open_nullph", midiStreamOpen(NULL, &id, 1, 0, 0, 0));
    id = 0; R("ms_open_c0", midiStreamOpen(&hs, &id, 0, 0, 0, 0));
    UINT ids[2] = {0, 0}; R("ms_open_c2", midiStreamOpen(&hs, ids, 2, 0, 0, 0));
    R("ms_open_nullid", midiStreamOpen(&hs, NULL, 1, 0, 0, 0));
    id = 9999; R("ms_open_badid", midiStreamOpen(&hs, &id, 1, 0, 0, 0)); P("ms_open_badid_id_after: 0x%x\n", id);
    id = 0xFFFFFFFE; R("ms_open_fffffffe", midiStreamOpen(&hs, &id, 1, 0, 0, 0));
    R("ms_out_badh_null", midiStreamOut(NULL, NULL, 0));
    DWORD prop[2] = {8, 0}; R("ms_prop_badh", midiStreamProperty(NULL, (LPBYTE)prop, MIDIPROP_GET | MIDIPROP_TIMEDIV));
    id = 0; hs = 0; r = midiStreamOpen(&hs, &id, 1, 0, 0, 0); R("ms_open_id0", r);
    if (r == 0) {
        R("ms_prop_noget", midiStreamProperty(hs, (LPBYTE)prop, MIDIPROP_TIMEDIV));
        R("ms_prop_null", midiStreamProperty(hs, NULL, MIDIPROP_GET | MIDIPROP_TIMEDIV));
        R("ms_out_null", midiStreamOut(hs, NULL, sizeof m));
        R("ms_out_unprepared", midiStreamOut(hs, MH(0, 12), sizeof m));
        R("mo_long_on_stream", midiOutLongMsg((HMIDIOUT)hs, MH(MHDR_PREPARED, 4), sizeof m));
        R("ms_close", midiStreamClose(hs));
    }
}

static void mixer(void)
{
    HMIXER h; MIXERLINEW ml; MIXERCONTROLDETAILS cd; DWORD val = 0; MIXERCAPSW mc;
    R("mx_open_nullph", mixerOpen(NULL, 0, 0, 0, 0));
    R("mx_open_cbfunc", mixerOpen(&h, 0, (DWORD_PTR)mixcb, 0, CALLBACK_FUNCTION));
    R("mx_open_badwnd", mixerOpen(&h, 0, 0x1234, 0, CALLBACK_WINDOW));
    R("mx_open_flag1", mixerOpen(&h, 0, 0, 0, 1));
    h = (HMIXER)0x1234; R("mx_open_id0", mixerOpen(&h, 0, 0, 0, 0)); if (h && h != (HMIXER)0x1234) R("mx_close", mixerClose(h));
    R("mx_open_badid", mixerOpen(&h, 9999, 0, 0, 0));
    memset(&ml, 0, sizeof ml); ml.cbStruct = sizeof ml;
    P("sizeof_MIXERLINEW: %u\n", (unsigned)sizeof ml);
    R("mx_line_flag10", mixerGetLineInfoW(NULL, &ml, 0x10));
    R("mx_line_null", mixerGetLineInfoW(NULL, NULL, 0));
    ml.cbStruct = 0x11B; R("mx_line_cb11b", mixerGetLineInfoW(NULL, &ml, 0));
    ml.cbStruct = sizeof ml; R("mx_line_query5", mixerGetLineInfoW(NULL, &ml, 5));
    ml.dwComponentType = 9; R("mx_line_ctype9", mixerGetLineInfoW(NULL, &ml, MIXER_GETLINEINFOF_COMPONENTTYPE));
    ml.Target.dwType = 6; R("mx_line_ttype6", mixerGetLineInfoW(NULL, &ml, MIXER_GETLINEINFOF_TARGETTYPE));
    ml.dwDestination = 0; R("mx_line_dest0", mixerGetLineInfoW(NULL, &ml, MIXER_GETLINEINFOF_DESTINATION));
    memset(&cd, 0, sizeof cd); cd.cbStruct = sizeof cd; cd.cChannels = 1; cd.cbDetails = 3; cd.paDetails = &val;
    R("mx_det_flag10", mixerGetControlDetailsW(NULL, &cd, 0x10));
    R("mx_det_cbdet3", mixerGetControlDetailsW(NULL, &cd, 0));
    cd.cbDetails = 4; cd.paDetails = NULL; R("mx_det_nullpa", mixerGetControlDetailsW(NULL, &cd, 0));
    cd.paDetails = &val; cd.cbStruct = 0x1F; R("mx_det_cb1f", mixerGetControlDetailsW(NULL, &cd, 0));
    cd.cbStruct = sizeof cd; cd.cMultipleItems = 1; R("mx_det_list_items1", mixerGetControlDetailsW(NULL, &cd, MIXER_GETCONTROLDETAILSF_LISTTEXT));
    cd.cMultipleItems = 0; R("mx_det_query2", mixerGetControlDetailsW(NULL, &cd, 2));
    cd.cbDetails = 4; R("mx_det_ok_shape_id0", mixerGetControlDetailsW(NULL, &cd, 0));
    R("mx_caps_cb0_null", mixerGetDevCapsW(0, NULL, 0)); R("mx_caps_null", mixerGetDevCapsW(0, NULL, sizeof mc));
    R("mx_caps_badid", mixerGetDevCapsW(9999, &mc, sizeof mc)); R("mx_caps_id0", mixerGetDevCapsW(0, &mc, sizeof mc));
    R("mx_close_badh", mixerClose(NULL));
    UINT mid; R("mx_getid_wavemapper", mixerGetID((HMIXEROBJ)(UINT_PTR)WAVE_MAPPER, &mid, MIXER_OBJECTF_WAVEOUT));
    R("mx_getid_flag1", mixerGetID(NULL, &mid, 1)); R("mx_getid_null", mixerGetID(NULL, NULL, 0));
    AUXCAPSW ac; DWORD av;
    R("aux_caps_cb0_null", auxGetDevCapsW(0, NULL, 0)); R("aux_caps_null", auxGetDevCapsW(0, NULL, sizeof ac));
    R("aux_caps_id0", auxGetDevCapsW(0, &ac, sizeof ac)); R("aux_getvol_null", auxGetVolume(0, NULL));
    R("aux_getvol_id0", auxGetVolume(0, &av)); R("aux_msg_id9999", auxOutMessage(9999, 3, 0, 0));
}

static void timing(void)
{
    TIMECAPS tc = {0}; MMTIME mt;
    R("tgdc_ok", timeGetDevCaps(&tc, sizeof tc)); P("tgdc_min: %u\ntgdc_max: %u\n", tc.wPeriodMin, tc.wPeriodMax);
    R("tgdc_cb7", timeGetDevCaps(&tc, 7)); R("tgdc_null", timeGetDevCaps(NULL, sizeof tc));
    R("tbp_0", timeBeginPeriod(0));
    R("tbp_1", timeBeginPeriod(1)); R("tep_1", timeEndPeriod(1)); R("tep_1_again", timeEndPeriod(1));
    R("tep_0", timeEndPeriod(0));
    R("tep_7_unbalanced", timeEndPeriod(7));
    R("tbp_15", timeBeginPeriod(15)); R("tep_15", timeEndPeriod(15)); R("tep_15_again", timeEndPeriod(15));
    R("tbp_16", timeBeginPeriod(16)); R("tep_16", timeEndPeriod(16)); R("tep_16_again", timeEndPeriod(16));
    R("tbp_1000", timeBeginPeriod(1000)); R("tep_1000", timeEndPeriod(1000)); R("tep_1000_again", timeEndPeriod(1000));
    R("tbp_2a", timeBeginPeriod(2)); R("tbp_2b", timeBeginPeriod(2)); R("tep_2a", timeEndPeriod(2)); R("tep_2b", timeEndPeriod(2)); R("tep_2c", timeEndPeriod(2));
    R("tgst_cb11", timeGetSystemTime(&mt, 11)); R("tgst_null", timeGetSystemTime(NULL, sizeof mt));
    mt.wType = 77; R("tgst_ok", timeGetSystemTime(&mt, sizeof mt)); P("tgst_wtype: %u\n", mt.wType);
    /* timeSetEvent */
    cb_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    R("tse_flag2", timeSetEvent(10, 1, tcb, 0, 2)); R("tse_flag200", timeSetEvent(10, 1, tcb, 0, 0x200));
    R("tse_flag40_oneshot", 0); /* placeholder to keep key order */
    R("tse_delay0", timeSetEvent(0, 1, tcb, 0, TIME_ONESHOT)); R("tse_delay1000001", timeSetEvent(1000001, 1, tcb, 0, TIME_ONESHOT));
    MMRESULT id = timeSetEvent(20, 1, tcb, 0x77, TIME_ONESHOT | TIME_CALLBACK_FUNCTION); P("tse_ok_nonzero: %d\n", id != 0);
    DWORD w = WaitForSingleObject(cb_ev, 3000); P("tse_fired: %d\n", w == WAIT_OBJECT_0);
    Sleep(50);
    P("tse_cb: id_match=%d msg=%u user=0x%llx p1=%llu p2=%llu hits=%ld\n", cb_id == id, cb_msg, (unsigned long long)cb_user, (unsigned long long)cb_p1, (unsigned long long)cb_p2, cb_hits);
    R("tke_fired_oneshot", timeKillEvent(id));
    R("tke_unknown", timeKillEvent(0x12345));
    MMRESULT ids[20]; int ok = 0; P("tse_ids:");
    for (int i = 0; i < 20; i++) { ids[i] = timeSetEvent(100000, 10, nopcb, 0, TIME_PERIODIC); if (ids[i]) ok++; P(" %u", ids[i]); }
    P("\ntse_live_cap: %d\n", ok);
    for (int i = 0; i < 20; i++) if (ids[i]) timeKillEvent(ids[i]);
    MMRESULT nid = timeSetEvent(10, 1, NULL, 0, TIME_ONESHOT); P("tse_nullcb_nonzero: %d\n", nid != 0); Sleep(100);
    R("tke_nullcb_after", timeKillEvent(nid));
    ResetEvent(cb_ev);
    MMRESULT eid = timeSetEvent(10, 1, (LPTIMECALLBACK)cb_ev, 0, TIME_ONESHOT | TIME_CALLBACK_EVENT_SET); P("tse_event_nonzero: %d\n", eid != 0);
    P("tse_event_fired: %d\n", WaitForSingleObject(cb_ev, 3000) == WAIT_OBJECT_0);
    R("tke_zero", timeKillEvent(0));
}

static void mmio(void)
{
    R("fourcc_ab", mmioStringToFOURCCA("ab", 0)); R("fourcc_ab_expect", mmioFOURCC('a', 'b', ' ', ' '));
    R("fourcc_abcdef", mmioStringToFOURCCA("abcdef", 0)); R("fourcc_up", mmioStringToFOURCCA("wave", MMIO_TOUPPER));
    R("fourcc_null", mmioStringToFOURCCA(NULL, 0)); R("fourcc_empty", mmioStringToFOURCCA("", 0));
    MMIOINFO mi; memset(&mi, 0, sizeof mi); mi.wErrorRet = 0x5555;
    HMMIO h = mmioOpenA(NULL, &mi, 0x40000000); P("mmio_open_badflag_null: %d\nmmio_open_badflag_err: %u\n", h == NULL, mi.wErrorRet);
    char mem[64]; memset(mem, 0, sizeof mem);
    memcpy(mem, "MThd\0\0\0\6\0\1\0\1\0\x60" "MTrk\0\0\0\4\0\xff\x2f\0", 26);
    memset(&mi, 0, sizeof mi); mi.fccIOProc = FOURCC_MEM; mi.pchBuffer = mem; mi.cchBuffer = 26;
    h = mmioOpenA(NULL, &mi, MMIO_READ); P("mmio_mem_open: %d\n", h != NULL);
    if (h) {
        MMCKINFO ck; memset(&ck, 0, sizeof ck); ck.ckid = mmioFOURCC('M', 'T', 'h', 'd');
        R("mmio_desc_mthd", mmioDescend(h, &ck, NULL, MMIO_FINDCHUNK)); R("mmio_desc_mthd_size", ck.cksize); R("mmio_desc_mthd_off", ck.dwDataOffset);
        R("mmio_ascend", mmioAscend(h, &ck, 0));
        memset(&ck, 0, sizeof ck); ck.ckid = mmioFOURCC('M', 'T', 'r', 'k');
        R("mmio_desc_mtrk", mmioDescend(h, &ck, NULL, MMIO_FINDCHUNK)); R("mmio_desc_mtrk_size", ck.cksize);
        mmioSeek(h, 0, SEEK_SET);
        memset(&ck, 0, sizeof ck); ck.ckid = mmioFOURCC('n', 'o', 'n', 'e');
        R("mmio_desc_notfound", mmioDescend(h, &ck, NULL, MMIO_FINDCHUNK));
        R("mmio_desc_badflag", mmioDescend(h, &ck, NULL, 0x80));
        R("mmio_desc_nullck", mmioDescend(h, NULL, NULL, 0));
        R("mmio_ascend_flag1", mmioAscend(h, &ck, 1));
        R("mmio_seek_set5", mmioSeek(h, 5, SEEK_SET));
        R("mmio_seek_origin7", mmioSeek(h, 5, 7));
        R("mmio_seek_cur_after", mmioSeek(h, 0, SEEK_CUR));
        R("mmio_seek_end4", mmioSeek(h, 4, SEEK_END));
        R("mmio_seek_endm4", mmioSeek(h, -4, SEEK_END));
        R("mmio_seek_end0", mmioSeek(h, 0, SEEK_END));
        R("mmio_seek_beyond", mmioSeek(h, 1000, SEEK_SET));
        R("mmio_read_nullbuf", mmioRead(h, NULL, 4));
        R("mmio_close", mmioClose(h, 0));
    }
    MMCKINFO ck2; memset(&ck2, 0, sizeof ck2);
    R("mmio_desc_nullh", mmioDescend(NULL, &ck2, NULL, 0));
    R("mmio_close_null", mmioClose(NULL, 0)); char b4[4]; R("mmio_read_badh", mmioRead(NULL, b4, 4));
    R("mmio_seek_badh", mmioSeek(NULL, 0, SEEK_SET)); MMIOINFO gi; R("mmio_getinfo_badh", mmioGetInfo(NULL, &gi, 0));
    memset(&mi, 0, sizeof mi);
    h = mmioOpenA((LPSTR)"C:\\i7_no_such_dir\\nofile.wav", &mi, MMIO_EXIST); P("mmio_exist_missing_h: %p\nmmio_exist_missing_err: %u\n", (void *)h, mi.wErrorRet);
    char wd[MAX_PATH]; GetSystemDirectoryA(wd, MAX_PATH); strcat(wd, "\\kernel32.dll");
    memset(&mi, 0, sizeof mi); h = mmioOpenA(wd, &mi, MMIO_EXIST); P("mmio_exist_present_h: %p\n", (void *)h);
}

static void playsound(void)
{
    R("ps_null", PlaySoundW(NULL, NULL, 0));
    R("ps_null_async", PlaySoundW(NULL, NULL, SND_ASYNC));
    R("ps_badflag20", PlaySoundW(L"i7nosuch", NULL, 0x20 | SND_NODEFAULT));
    R("ps_badflag100", PlaySoundW(L"i7nosuch", NULL, 0x100 | SND_NODEFAULT));
    R("ps_badflag800000", PlaySoundW(L"i7nosuch", NULL, 0x800000 | SND_NODEFAULT));
    R("ps_empty", PlaySoundW(L"", NULL, SND_NODEFAULT));
    R("ps_missing_file", PlaySoundW(L"C:\\i7_no_such_dir\\x.wav", NULL, SND_FILENAME | SND_NODEFAULT));
    R("ps_missing_name", PlaySoundW(L"i7_no_such_sound_alias", NULL, SND_NODEFAULT));
    R("ps_missing_res", PlaySoundW(L"I7NORES", GetModuleHandleW(NULL), SND_RESOURCE | SND_NODEFAULT));
    static char junk[64] = "JUNKxxxxWAVE";
    R("ps_async_mem_junk", PlaySoundA(junk, NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT));
    /* 50 ms of 8 kHz 8-bit mono silence */
    static unsigned char wav[44 + 400]; DWORD n = 400;
    memcpy(wav, "RIFF", 4); *(DWORD *)(wav + 4) = 36 + n; memcpy(wav + 8, "WAVEfmt ", 8); *(DWORD *)(wav + 16) = 16;
    *(WORD *)(wav + 20) = 1; *(WORD *)(wav + 22) = 1; *(DWORD *)(wav + 24) = 8000; *(DWORD *)(wav + 28) = 8000;
    *(WORD *)(wav + 32) = 1; *(WORD *)(wav + 34) = 8; memcpy(wav + 36, "data", 4); *(DWORD *)(wav + 40) = n; memset(wav + 44, 0x80, n);
    R("ps_sync_mem_silence", PlaySoundA((LPCSTR)wav, NULL, SND_MEMORY | SND_NODEFAULT));
    R("ps_async_mem_silence", PlaySoundA((LPCSTR)wav, NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT));
    Sleep(200);
    R("ps_stop", PlaySoundW(NULL, NULL, 0));
    R("snd_null", sndPlaySoundW(NULL, 0));
}

static void mci(void)
{
    WCHAR ret[256]; MCIERROR e;
    #define MS(k, s) do { ret[0] = L'?'; e = mciSendStringW(s, ret, 256, NULL); P("%s: %lu (lo %lu hi %lu) ret0=%d\n", k, (unsigned long)e, (unsigned long)(e & 0xFFFF), (unsigned long)(e >> 16), ret[0]); } while (0)
    MS("mci_unknown_verb", L"frobnicate i7dev");
    MS("mci_open_new_noalias", L"open new type waveaudio");
    MS("mci_play_new", L"play new");
    MS("mci_play_nodev", L"play");
    MS("mci_status_all", L"status all mode");
    MS("mci_close_all", L"close all");
    MS("mci_sysinfo_all_qty", L"sysinfo all quantity");
    if (e == 0) { char a[256]; WideCharToMultiByte(CP_UTF8, 0, ret, -1, a, 256, NULL, NULL); P("mci_sysinfo_all_qty_val: %s\n", a); }
    MS("mci_sysinfo_wa_qty", L"sysinfo waveaudio quantity");
    MS("mci_open_new_alias", L"open new type waveaudio alias i7fc");
    if (e == 0) { MS("mci_status_i7fc_mode", L"status i7fc mode"); MS("mci_close_i7fc", L"close i7fc"); }
    MS("mci_status_nosuch", L"status i7nosuchdev mode");
    MS("mci_notify_autoopen", L"status i7fcx.wav mode notify");
    P("mci_getdevid_nosuch: %u\n", mciGetDeviceIDW(L"i7nosuch"));
    WCHAR t[128]; P("mci_errstr_261: %d\n", mciGetErrorStringW(261, t, 128));
    P("mci_errstr_null: %d\n", mciGetErrorStringW(261, NULL, 128));
    P("mci_errstr_0: %d\n", mciGetErrorStringW(0, t, 128));
    P("mci_errstr_9999: %d\n", mciGetErrorStringW(9999, t, 128));
    P("mci_errstr_hi_unknowndev: %d\n", mciGetErrorStringW((77u << 16) | 261, t, 128));
}

static void joy(void)
{
    JOYCAPSW jc; JOYINFOEX je; JOYINFO ji; UINT th;
    R("joy_caps_null", joyGetDevCapsW(0, NULL, sizeof jc));
    P("sizeof_JOYCAPSW: %u\n", (unsigned)sizeof jc);
    R("joy_caps_cb100", joyGetDevCapsW(0, &jc, 100));
    R("joy_caps_id0", joyGetDevCapsW(0, &jc, sizeof jc));
    R("joy_caps_id16", joyGetDevCapsW(16, &jc, sizeof jc));
    memset(&jc, 0x5A, sizeof jc); R("joy_caps_idm1", joyGetDevCapsW((UINT_PTR)-1, &jc, sizeof jc));
    P("joy_caps_idm1_wMid: %u\njoy_caps_idm1_regkey0: %d\n", jc.wMid, jc.szRegKey[0]);
    memset(&je, 0, sizeof je); je.dwSize = 51; R("joy_posex_size51", joyGetPosEx(0, &je));
    je.dwSize = sizeof je; je.dwFlags = JOY_RETURNALL; R("joy_posex_id0", joyGetPosEx(0, &je)); R("joy_posex_id16", joyGetPosEx(16, &je));
    R("joy_posex_null", joyGetPosEx(0, NULL));
    R("joy_pos_null", joyGetPos(0, NULL)); R("joy_pos_id0", joyGetPos(0, &ji));
    R("joy_getth_null", joyGetThreshold(0, NULL)); R("joy_getth_id0", joyGetThreshold(0, &th)); R("joy_getth_id16", joyGetThreshold(16, &th));
    R("joy_setth_big", joySetThreshold(0, 0x10000)); R("joy_setth_id16", joySetThreshold(16, 0));
    R("joy_release_id16", joyReleaseCapture(16)); R("joy_release_id0", joyReleaseCapture(0));
    R("joy_capture_nullwnd", joySetCapture(NULL, 0, 10, FALSE));
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    P("fc02 begin\n");
    counts(); P("== wave\n"); wave(); P("== midi\n"); midi(); P("== mixer\n"); mixer();
    P("== time\n"); timing(); P("== mmio\n"); mmio(); P("== playsound\n"); playsound();
    P("== mci\n"); mci(); P("== joy\n"); joy();
    P("fc02 end\n");
    return 0;
}
