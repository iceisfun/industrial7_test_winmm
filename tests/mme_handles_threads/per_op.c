#include "mh.h"
/* One TEST per handle-taking entry point: every never-valid value (NULL, 1, 2, 0x8000, 0xFFFF, -1, 0xFFFFFFFF, -2),
 * a just-freed handle value and a live handle of another type (an mmio memory file) give the entry point's
 * invalid-handle result (MMSYSERR_INVALHANDLE for MMRESULT functions; -1 for mmioRead/Write/Seek; 0 for
 * mmioSendMessage; MMIOERR_CANNOTSEEK for the chunk helpers that fail in their internal seek). The live mmio handle
 * is still valid afterwards. VM_TIMEOUT: 120 */

TEST(mh_op_waveOutClose) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutClose"); }
TEST(mh_op_waveOutReset) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutReset"); }
TEST(mh_op_waveOutPause) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutPause"); }
TEST(mh_op_waveOutRestart) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutRestart"); }
TEST(mh_op_waveOutBreakLoop) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutBreakLoop"); }
TEST(mh_op_waveOutGetID) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutGetID"); }
TEST(mh_op_waveOutGetPosition) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutGetPosition"); }
TEST(mh_op_waveOutGetPitch) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutGetPitch"); }
TEST(mh_op_waveOutGetPlaybackRate) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutGetPlaybackRate"); }
TEST(mh_op_waveOutSetPitch) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutSetPitch"); }
TEST(mh_op_waveOutSetPlaybackRate) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutSetPlaybackRate"); }
TEST(mh_op_waveOutPrepareHeader) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutPrepareHeader"); }
TEST(mh_op_waveOutUnprepareHeader) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutUnprepareHeader"); }
TEST(mh_op_waveOutWrite) { mh_op_contract(MH_WAVEOUT_OPS, MH_NWAVEOUT_OPS, "waveOutWrite"); }
TEST(mh_op_waveInClose) { mh_op_contract(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, "waveInClose"); }
TEST(mh_op_waveInReset) { mh_op_contract(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, "waveInReset"); }
TEST(mh_op_waveInStart) { mh_op_contract(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, "waveInStart"); }
TEST(mh_op_waveInStop) { mh_op_contract(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, "waveInStop"); }
TEST(mh_op_waveInGetID) { mh_op_contract(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, "waveInGetID"); }
TEST(mh_op_waveInGetPosition) { mh_op_contract(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, "waveInGetPosition"); }
TEST(mh_op_waveInPrepareHeader) { mh_op_contract(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, "waveInPrepareHeader"); }
TEST(mh_op_waveInUnprepareHeader) { mh_op_contract(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, "waveInUnprepareHeader"); }
TEST(mh_op_waveInAddBuffer) { mh_op_contract(MH_WAVEIN_OPS, MH_NWAVEIN_OPS, "waveInAddBuffer"); }
TEST(mh_op_midiOutClose) { mh_op_contract(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, "midiOutClose"); }
TEST(mh_op_midiOutReset) { mh_op_contract(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, "midiOutReset"); }
TEST(mh_op_midiOutShortMsg) { mh_op_contract(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, "midiOutShortMsg"); }
TEST(mh_op_midiOutLongMsg) { mh_op_contract(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, "midiOutLongMsg"); }
TEST(mh_op_midiOutPrepareHeader_NULL_hdr) { mh_op_contract(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, "midiOutPrepareHeader(NULL hdr)"); }
TEST(mh_op_midiOutUnprepareHeader_NULL_hdr) { mh_op_contract(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, "midiOutUnprepareHeader(NULL hdr)"); }
TEST(mh_op_midiOutGetID) { mh_op_contract(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, "midiOutGetID"); }
TEST(mh_op_midiOutCachePatches) { mh_op_contract(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, "midiOutCachePatches"); }
TEST(mh_op_midiOutCacheDrumPatches) { mh_op_contract(MH_MIDIOUT_OPS, MH_NMIDIOUT_OPS, "midiOutCacheDrumPatches"); }
TEST(mh_op_midiInClose) { mh_op_contract(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiInClose"); }
TEST(mh_op_midiInReset) { mh_op_contract(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiInReset"); }
TEST(mh_op_midiInStart) { mh_op_contract(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiInStart"); }
TEST(mh_op_midiInStop) { mh_op_contract(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiInStop"); }
TEST(mh_op_midiInGetID) { mh_op_contract(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiInGetID"); }
TEST(mh_op_midiInPrepareHeader) { mh_op_contract(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiInPrepareHeader"); }
TEST(mh_op_midiInUnprepareHeader) { mh_op_contract(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiInUnprepareHeader"); }
TEST(mh_op_midiInAddBuffer) { mh_op_contract(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiInAddBuffer"); }
TEST(mh_op_midiConnect_h_NULL) { mh_op_contract(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiConnect(h, NULL)"); }
TEST(mh_op_midiDisconnect_h_NULL) { mh_op_contract(MH_MIDIIN_OPS, MH_NMIDIIN_OPS, "midiDisconnect(h, NULL)"); }
TEST(mh_op_midiStreamClose) { mh_op_contract(MH_STREAM_OPS, MH_NSTREAM_OPS, "midiStreamClose"); }
TEST(mh_op_midiStreamPause) { mh_op_contract(MH_STREAM_OPS, MH_NSTREAM_OPS, "midiStreamPause"); }
TEST(mh_op_midiStreamRestart) { mh_op_contract(MH_STREAM_OPS, MH_NSTREAM_OPS, "midiStreamRestart"); }
TEST(mh_op_midiStreamStop) { mh_op_contract(MH_STREAM_OPS, MH_NSTREAM_OPS, "midiStreamStop"); }
TEST(mh_op_midiStreamPosition) { mh_op_contract(MH_STREAM_OPS, MH_NSTREAM_OPS, "midiStreamPosition"); }
TEST(mh_op_midiStreamPosition_NULL) { mh_op_contract(MH_STREAM_OPS, MH_NSTREAM_OPS, "midiStreamPosition(NULL)"); }
TEST(mh_op_midiStreamProperty) { mh_op_contract(MH_STREAM_OPS, MH_NSTREAM_OPS, "midiStreamProperty"); }
TEST(mh_op_midiStreamProperty_NULL_0) { mh_op_contract(MH_STREAM_OPS, MH_NSTREAM_OPS, "midiStreamProperty(NULL, 0)"); }
TEST(mh_op_midiStreamOut_NULL) { mh_op_contract(MH_STREAM_OPS, MH_NSTREAM_OPS, "midiStreamOut(NULL)"); }
TEST(mh_op_mmioClose) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioClose"); }
TEST(mh_op_mmioGetInfo) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioGetInfo"); }
TEST(mh_op_mmioGetInfo_NULL) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioGetInfo(NULL)"); }
TEST(mh_op_mmioSetInfo_NULL) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioSetInfo(NULL)"); }
TEST(mh_op_mmioRead) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioRead"); }
TEST(mh_op_mmioRead_NULL_0) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioRead(NULL, 0)"); }
TEST(mh_op_mmioWrite) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioWrite"); }
TEST(mh_op_mmioSeek_SET) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioSeek(SET)"); }
TEST(mh_op_mmioSeek_END) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioSeek(END)"); }
TEST(mh_op_mmioFlush) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioFlush"); }
TEST(mh_op_mmioAdvance) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioAdvance"); }
TEST(mh_op_mmioSetBuffer) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioSetBuffer"); }
TEST(mh_op_mmioSendMessage) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioSendMessage"); }
TEST(mh_op_mmioCreateChunk) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioCreateChunk"); }
TEST(mh_op_mmioAscend) { mh_op_contract(MH_MMIO_OPS, MH_NMMIO_OPS, "mmioAscend"); }
TEST(mh_op_mixerClose) { mh_op_contract(MH_MIXER_OPS, MH_NMIXER_OPS, "mixerClose"); }
TEST(mh_op_mixerGetID_HWAVEOUT) { mh_op_contract(MH_MIXER_OPS, MH_NMIXER_OPS, "mixerGetID(HWAVEOUT)"); }
TEST(mh_op_mixerGetID_HWAVEIN) { mh_op_contract(MH_MIXER_OPS, MH_NMIXER_OPS, "mixerGetID(HWAVEIN)"); }
TEST(mh_op_mixerGetID_HMIDIOUT) { mh_op_contract(MH_MIXER_OPS, MH_NMIXER_OPS, "mixerGetID(HMIDIOUT)"); }
TEST(mh_op_mixerGetLineInfoW_HWAVEOUT) { mh_op_contract(MH_MIXER_OPS, MH_NMIXER_OPS, "mixerGetLineInfoW(HWAVEOUT)"); }
TEST(mh_op_mixerGetLineControlsW_HWAVEOUT) { mh_op_contract(MH_MIXER_OPS, MH_NMIXER_OPS, "mixerGetLineControlsW(HWAVEOUT)"); }
TEST(mh_op_mixerGetControlDetailsW_HWAVEOUT) { mh_op_contract(MH_MIXER_OPS, MH_NMIXER_OPS, "mixerGetControlDetailsW(HWAVEOUT)"); }
