# 03: existing tests, consumers, Industrial7 state

Scope: Win10 x64 winmm.dll 10.0.19041.546 (implementation mostly in winmmbase.dll behind 7-byte jmp thunks; 180 named exports plus ordinal 2, which is the PlaySound body). Sources: the third-party winmm test directory (read only, learned from, not reproduced), the Win10 corpus API (callers of every winmm export, depth 1, 3007 indexed modules), Industrial7 docs and crates, bb board, sibling corpora.

## A. Existing winmm tests

### A.1 Third-party suite: per file (what is exercised, which edge cases matter)

| file (lines) | exports touched | behaviours worth learning |
|---|---|---|
| wave.c (2510) | waveOutOpen/Close/GetDevCapsA/W/GetErrorTextA/GetID/GetNumDevs/GetPosition/GetVolume/SetVolume/Pause/Restart/Reset/Write/Prepare/Unprepare/Message, PlaySound(A), sndPlaySoundA/W, mmio* (to build wav data) | format matrix (rates, 8/16/24/32, mono/stereo, EXTENSIBLE, odd block align) accepted vs WAVERR_BADFORMAT with WAVE_FORMAT_QUERY; WAVE_MAPPER and WAVE_FORMAT_DIRECT; open twice on one device; callback kinds (window, thread, function, event) and which messages arrive (WOM_OPEN/DONE/CLOSE); close with a header still queued (WAVERR_STILLPLAYING); GetPosition in TIME_MS/SAMPLES/BYTES with fallback to bytes; header flag bookkeeping (PREPARED, DONE, INQUEUE); reentrant callback; fragment size; devcaps/GetID vs the mapper; volume round trip; sndPlaySound return values with alias ids |
| capture.c (741) | waveIn* full path, waveOut* for a loopback | same matrix for waveIn; Start/Stop/Reset; buffers returned with dwBytesRecorded; WIM_DATA delivery order; GetPosition |
| midi.c (1185) | midiOut/midiIn/midiStream*, midiOutGetID, volume, DriverCallback, mciSendStringA (sequencer) | device-less and device paths; short/long message validation (bad status, unprepared header); stream property get/set flags; stream position units; notification (callback) matrix |
| mixer.c (1113) | mixerOpen/Close/GetDevCaps/GetLineInfo/GetLineControls/GetControlDetails/SetControlDetails/GetNumDevs, A and W | struct size validation (cbStruct), MIXER_OBJECTF_* object-type routing, component types on source/dest lines, control details ranges, W vs A structure parity, bad handle |
| mci.c (2030) | mciSendString A/W, mciSendCommand A/W, mciGetDeviceIDA, waveOut/waveIn counts | parser (quotes, case, aliases, partial words, trailing junk), error codes MCIERR_*, open/close/play/seek/status/set time format, notify messages, devices wave/sequencer/avi/mpegvideo, auto open, threads |
| mcicda.c (612) | mciSendCommandA/StringA (cdaudio) | needs a CD drive; mostly not portable to a headless guest |
| mmio.c (1143) | mmioOpen A/W, Close, Read, Write, Seek, Descend, Ascend, CreateChunk, Advance, GetInfo, SetInfo, SetBuffer, InstallIOProcA | RIFF descend/ascend rules with MMIO_FINDRIFF/LIST/CHUNK, buffer pointers after reads, EOF handling, create/write/rename flows, custom IOProc via FOURCC, buffer sizes; fully host-side so best portable material |
| timer.c (274) | timeGetDevCaps, timeBeginPeriod/EndPeriod, timeGetTime, timeSetEvent/KillEvent | devcaps min/max, periodic vs one-shot, TIME_CALLBACK_EVENT_SET/PULSE, kill-after-fire, callback thread priority, timer lifetime; heavily timing-based (we assert only loose bounds) |
| joystick.c (235) | joyGetDevCaps/NumDevs/Pos/PosEx/Threshold/SetCapture/ReleaseCapture | no-device error paths (JOYERR_PARAMS, JOYERR_NOCANDO, MMSYSERR_NODRIVER) |
| generated.c (5148) | none (struct layout/size checks of MMSYSTEM types) | useful list of struct sizes and alignments; x64 values are Win10 facts we can verify by sizeof in C |

### A.2 Coverage of the 181 winmm exports by that suite

Touched by at least one file: about 113 names. NOT touched at all (this is where a Win10 oracle adds the most unique value, and where Industrial7 has no test to lean on):

- Driver API: CloseDriver, DefDriverProc, DrvGetModuleHandle, GetDriverModuleHandle, OpenDriver, SendDriverMessage, mciDriverNotify, mciDriverYield, mciGetDriverData, mciSetDriverData, mciLoadCommandResource, mciFreeCommandResource, mciGetCreatorTask, mciGetYieldProc, mciSetYieldProc
- Wave extras: waveOutBreakLoop, waveOutGet/SetPitch, waveOutGet/SetPlaybackRate, waveOutGetErrorTextW, waveInGetErrorTextW, waveInGetID, waveInReset, waveInGetDevCapsA
- Aux: auxGetDevCapsA/W, auxGetNumDevs, auxGetVolume, auxSetVolume, auxOutMessage
- MIDI extras: midiInGetDevCapsW, midiInGetErrorTextA/W, midiInGetID, midiInMessage, midiInStart, midiInStop, midiOutGetDevCapsW, midiOutGetErrorTextA/W, midiOutMessage, midiOutCachePatches, midiOutCacheDrumPatches, midiConnect, midiDisconnect
- MCI: mciExecute, mciGetDeviceIDW, mciGetDeviceIDFromElementIDA/W, mciGetErrorStringA/W (error strings are only read through the error-text paths)
- mmio: mmioFlush, mmioInstallIOProcW, mmioRenameA/W, mmioSendMessage, mmioStringToFOURCCA/W
- Misc: mmsystemGetVersion, mixerGetID, mixerMessage, timeGetSystemTime, joyConfigChanged, WAVEOUT PlaySoundW (only the A form), mmTask* (mmGetCurrentTask, mmTaskCreate, mmTaskBlock, mmTaskSignal, mmTaskYield, mmDrvInstall), WOWAppExit

The third-party suite also never pins exact Windows 10 return codes for argument validation of waveOut/waveIn/midi on zero devices; it skips ("No output devices available"). Our corpus must not skip: the winmmbase contract (see C) defines exact results with zero devices.

### A.3 Existing corpora on this host

- industrial7_test_winmm: only core_smoke (module loads, GetProcAddress negative, child round trip); tools/winmm_exports.{txt,json} (180 names) and winmmbase_exports.{txt,json} (141 names); common/modules.h lists winmm.
- Other corpora using winmm: industrial7_test_xcr tests/xstate_stress.c resolves timeSetEvent/timeKillEvent/timeBeginPeriod/timeEndPeriod as a timer-thread FP/AVX state stress (skips when absent); /home/iceisfun/w10lab/curl/win32-corpus/timing/09_timebeginperiod_winmm.c is a recorded Win10 timer-resolution probe (prior oracle data for the timeBeginPeriod effect on sleep granularity). No other test_* / industrial7_test_* tree has a winmm slice. bb depot search for winmm finds no recording; the only audio item is an unrelated USB audio lsusb dump.
- Industrial7 side tests: winmm-w10 (66 host tests passed per bb #824), w10-winmm-wave (15), w10-mmdevapi-model, w10-audioses-client, w10-audio-engine; all are host-only model tests pinned to corpus decompiles, not guest runs against a real Win10 (no Win10 oracle recordings).

## B. Consumers

### B.1 Who calls winmm in the Win10 corpus (callers API, 52 non-winmm modules)

| module | #distinct winmm exports | what it uses |
|---|---|---|
| mciwave.dll | 42 | MCI driver glue (DefDriverProc, mciDriver*, mciGet/SetDriverData, resource loaders, waveOut/In, mmTask*) |
| quartz.dll | 38 | DirectShow: midiOut/midiStream*, waveOut*/waveIn*, mixer*, timeSetEvent |
| wmp.dll | 33 | Media Player: PlaySoundW, mciSendCommandW, mixer*, waveOut*, OpenDriver |
| mciavi32.dll | 27 | MCI AVI (waveOut, DriverCallback, mmio) |
| mstscax.dll | 25 | Remote Desktop client: timeBeginPeriod/EndPeriod, timeSetEvent/KillEvent, timeGetTime, waveIn (audio redirection), waveOut |
| mciseq.dll | 24 | MIDI sequencer MCI driver (midiOut*, timers) |
| avicap32.dll | 22 | video capture: mmio*, mciSendStringW, OpenDriver/SendDriverMessage |
| qcap.dll | 16 | capture graph: mixer*, waveIn* |
| mcicda.dll | 15 | CD audio MCI |
| tapi3.dll | 14 | telephony: wave/midi/mixer *Message and open/close |
| msvfw32.dll | 8 | VfW: mciSendString/Command, mciGetErrorString, OpenDriver/CloseDriver, GetDriverModuleHandle |
| dinput.dll, devenum.dll, avifil32.dll (7,7,5) | | mmio* RIFF reading (dinput force-feedback files, avifil32 AVI), device enumeration |
| qedit.dll, sbe.dll, vfwwdm32.dll, serwvdrv.dll, msdtctm.dll, mshtml.dll, dmusic.dll, mfds.dll, tapi32/tapisrv | 2 to 5 | timers (timeBeginPeriod/EndPeriod/timeGetDevCaps, timeSetEvent), *Message |
| PlaySoundW only | | authui (logon sound), comctl32 (CCPlaySound), conhostv1, explorerframe, fxsst, ieframe, inputservice, logoncontroller, playsndsrv, stobject, themeui, tsf3gip, webcamui, wiadefui, shell32 |
| timeGetTime only | | cpfilters, psisdecd, rdpendp, termmgr, twinui, shell32, explorerframe |
| DefDriverProc + GetDriverModuleHandle only | | VfW codecs: msrle32, msvidc32, msyuv, tsbyuv, iyuv_32, mciqtz32 |

Export-by-importer-module ranking (distinct importing modules): PlaySoundW 16, DefDriverProc 12, timeGetTime 12, timeSetEvent 9, timeKillEvent 9, GetDriverModuleHandle 9, mmio Close/Read/Seek/Ascend/Descend/OpenW 6-7, waveOutWrite/Prepare/Reset/Open/Close/Message and waveInMessage 6, timeBeginPeriod 6, waveIn* 4-5, timeEndPeriod 5, mixer* 3-4, mciSendString/Command 2. Exports with zero importers in the corpus (all auxiliary or rare: joy*, aux*, midiIn*, waveOutBreakLoop/pitch/rate, mciExecute, sndPlaySoundA, PlaySoundA, mmio rename/FOURCC) are still correct to test as API but are low consumer value.

Caveat: the corpus is Win10 system modules only (callers resolved per call site through the IAT); third-party programs are covered by the Industrial7 import tables below.

### B.2 Third-party and Industrial7 targets (from /home/iceisfun/work/industrial7 docs)

- Chrome 109 (docs/stage4/chrome_imports_by_module.tsv, 57 winmm imports across chrome.dll, widevinecdm.dll, chrome_elf and others): timeBeginPeriod/EndPeriod/GetTime/GetDevCaps; waveOutOpen/Close/GetNumDevs/Pause/Prepare/Reset/Write..., waveInGetNumDevs; the full midiIn and midiOut device set (Web MIDI: GetNumDevs, GetDevCapsW, Open/Close, Prepare/Unprepare, Add/ShortMsg/LongMsg, Reset, Start). It also reaches mmdevapi via COM (WASAPI) and imports ordinal 17 of mmdevapi.
- xul.dll (Firefox/LibreWolf): waveOutGetPosition/GetDevCapsW/Message plus the waveOut set (owned-gap.tsv per AUDIO-PLAN).
- VSCodium 1.143 (bb #1106): 31 static winmm imports.
- shlwapi forwards one function to winmm (docs/TAINTED_DLL_TO_OWNED.md); TAINTED notes machine-control software typically uses winmm timers, printing and serial.
- Industrial applications (the project motive) use timeSetEvent/timeKillEvent and timeBeginPeriod for cycle timing, timeGetTime for stamps, PlaySound for alarms, waveOut for beeps.
- bb board: #989 ranks winmm + mmdevapi/audioses + HDA as item 8 of the DLL-corpus wishlist; #824/#926/#923/#957/#958/#993/#1022/#1105 cover the audio stack; i7mediaplayer (#957/#993) will play audio through winmm/mmdevapi.

## C. Industrial7 current winmm state

- Registry (docs/personality/registry.tsv): userland/guest/winmm-w10 (dll, unwired, WINMM-W10-LINK.md), crates/w10-winmm-wave (WINMM-WAVE.md), crates/w10-mmdevapi-model, userland/guest/mmdevapi-w10 (35 exports, zero endpoints), crates/w10-audioses-client, crates/w10-audio-engine, crates/i7audio-core; OWNED_STATUS lists winmm.dll and MMDevAPI.dll as "stock-only, not run" (the Win10 default still loads the Microsoft DLL; bb #523/#522 owners).
- Owned winmm-w10 (docs/design/win10/WINMM-W10-LINK.md): all 181 exports present; time* real (timeGetTime from KUSER_SHARED_DATA, timeBegin/EndPeriod via NtSet/QueryTimerResolution with counted periods, timeGetDevCaps, timeGetSystemTime); timeSetEvent returns 0 and timeKillEvent returns 0x61 (the timer thread arm is not modelled); everything else is the honest no-device answer. Corpus-pinned results to reproduce on Win10: wave/midi/aux *Message and Set/GetVolume give MMSYSERR_BADDEVICEID (2) for any id including WAVE_MAPPER; *GetDevCaps cb 0 gives 0, NULL gives 11 (INVALPARAM), else 2; GetID/GetPosition/GetPitch/GetPlaybackRate NULL gives 11 else INVALHANDLE (5); Open argument/flag gates (11 / 10 INVALFLAG) then 2; Prepare/Unprepare/Write: NULL header or cbwh < 0x30 or illegal flags give 11 before the handle is looked at, INQUEUE gives STILLPLAYING (0x21), unprepared write gives UNPREPARED (0x22). Known unverified divergences recorded there: CALLBACK_WINDOW IsWindow check not done; timeSetEvent good arm missing.
- WINMM-WAVE: waveOut state machine over a mock engine, 76 pinned winmmbase facts, but driver-layer (wdmaud wodMessage) behaviour is UNVERIFIED because wdmaud.drv is not in the corpus: header queue and loops, done-callback order, position units and TIME_BYTES fallback, devcaps layout, mapper caps name, format validation rule, WAVE_MAPPED handling, error text strings, unaligned write length, dwLoops==0, pitch/rate (returns NOTSUPPORTED). These are exactly the facts only the Win10 guest can settle.
- mmdevapi-model: GetDefaultAudioEndpoint role <= 2, ForPolicy order (E_POINTER, flow <= 1, policy <= 8, E_NOTFOUND 0x80070490), EnumAudioEndpoints mask rules, device id format {K.M.F.HHHHHHHH}.{guid}, GetPublicState bit mapping, notification client list semantics. Combase activation gap: CoCreateInstance(CLSID_MMDeviceEnumerator) fails (REGDB_E_CLASSNOTREG) until an in-proc server route exists.
- Audio kernel side (bb #1022 closed, #958 HDA, #1105): kernel null sink, QEMU WAV gate, virtio-snd then HDA; mmdevapi/winmm have no hardware sink yet.
- Net: Industrial7 has host models of the contract and zero guest-vs-Win10 evidence; the corpus is the first place those pins are checked against a real build.

## D. Consumer-driven slices, ranked

Rank = (consumer breadth in Win10 corpus + Industrial7 targets) x (how much is unverified in Industrial7) / (cost, devices needed).

1. **timer** (timeGetTime, timeGetDevCaps, timeBegin/EndPeriod incl. mismatched/unpaired and out-of-range periods, timeSetEvent/KillEvent: periodic, one-shot, CALLBACK_FUNCTION/EVENT_SET/EVENT_PULSE, TIME_KILL_SYNCHRONOUS, bad ids, kill twice, resolution clamp, uDelay/uResolution validation, timeGetSystemTime sizes). Consumers: Chrome, mstscax, qedit, sbe, dmusic, mfds, shell32, industrial control apps. Industrial7: timeSetEvent is a stub, the first real gap. No hardware needed. Highest value.
2. **playsound** (PlaySoundW/A, sndPlaySound, ordinal 2, SND_MEMORY with in-memory silent wav, SND_NODEFAULT, SND_ASYNC/NOSTOP/PURGE/NOWAIT, SND_ALIAS with unknown alias, return values and last error on 0 devices; headless returns). 16 importer modules (logon, console, shell, comctl32, wmp). Do not play system sounds.
3. **waveOut/waveIn zero-device and mapper contract** (open/query/flags/callbacks, devcaps cb and NULL cases, GetID, error text for every code in both A and W with string buffers and truncation, header prepare/write/unprepare state machine, Message/GetVolume/SetVolume, handle misuse): exact winmmbase codes, runs with 0 devices; Chrome, xul, quartz, mstscax, wmp. Then, gated on virtual HDA, the real playback path: silent buffer write, WOM_DONE, position units, pause/restart/reset/break loop, volume, pitch/rate, callback kinds, close-while-playing.
4. **mmio** (open memory/file, descend/ascend/createchunk, buffers, EOF, custom IOProc, rename, FOURCC conversion, flush/advance/sendmessage): purely host-side, deterministic, used by dinput, avifil32, avicap32, mciwave; no devices.
5. **MCI** (mciSendString/Command parser and error strings via mciGetErrorString A/W for the whole MCIERR range, mciGetDeviceID*, mciExecute not UI, wave/sequencer/avi device open on missing file or device, notify to a hidden message-only window, mciGetCreatorTask, yield proc get/set, driver data get/set, command resource loaders): msvfw32, wmp, avicap32, mci* drivers.
6. **midiOut/midiIn/midiStream + mixer + aux + joy contract** (zero-device and out-of-range ids, devcaps/errortext A/W, header protocol, stream property flags, cb struct size checks, JOYERR codes): Chrome Web MIDI, tapi3, qcap, devenum. Exact codes, with devices only if present.
7. **driver API** (OpenDriver/CloseDriver/SendDriverMessage/DefDriverProc/GetDriverModuleHandle/DrvGetModuleHandle/DriverCallback types, mmTask*, mmsystemGetVersion, WOWAppExit not called): 12 importers (VfW codecs), cheap, only invalid-input and DefDriverProc contract tests.
8. **mmdevapi device enumeration** via COM (IMMDeviceEnumerator default endpoint, enumerate, GetDevice, id format, property store keys) and the winmm-to-WASAPI forward: modest, gated on COM server availability; relevant to Chrome/Firefox.

Guest-evidence priority for Industrial7: every UNVERIFIED line in section C (driver-layer wave behaviour, mapper name, format validation, loop and done order, position units, error text strings) should become a hard CHECK from a recorded Win10 boot with and without the virtual HDA device; run both configurations in one boot where possible.
