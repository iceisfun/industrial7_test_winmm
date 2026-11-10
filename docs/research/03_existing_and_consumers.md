# 03: consumers of winmm and corpus priorities

Scope: Win10 x64 winmm.dll 10.0.19041.546 (most exports are 7-byte `jmp [rip+x]` thunks into winmmbase.dll, and the time* family (timeGetTime, timeBeginPeriod, timeEndPeriod, timeGetDevCaps, timeGetSystemTime) thunks into kernel32.dll; bodies that live in winmm.dll itself (corpus size != 7) are PlaySoundA/W, sndPlaySoundA/W, timeSetEvent, timeKillEvent, all joy*, all mci* (mciSendCommand/String, mciGetErrorString, mciGetDeviceID*, driver data/yield/command-resource helpers, mciExecute), WOWAppExit and mmsystemGetVersion; 180 named exports plus ordinal 2, which is the PlaySound body).

Source of the consumer data: the Win10 reference corpus API (callers of every winmm export, depth 1, 3007 indexed modules).

## A. Who calls winmm in the Win10 corpus

52 non-winmm modules import winmm; one of them is winmmbase.dll itself, which calls back into winmm timeSetEvent/timeKillEvent.

| module | #distinct winmm exports | what it uses |
|---|---|---|
| mciwave.dll | 42 | MCI driver glue (DefDriverProc, mciDriver*, mciGet/SetDriverData, resource loaders, waveOut/In, mmTask*) |
| quartz.dll | 38 | DirectShow: midiOut/midiStream* (incl. midiStreamProperty/Position), waveOut* (incl. GetPosition, Get/SetVolume, GetErrorTextW), mixer* W, time* (BeginPeriod/EndPeriod/GetDevCaps/GetTime/SetEvent/KillEvent); no waveIn |
| wmp.dll | 33 | Media Player: PlaySoundW, mciSendCommandW, mciGetErrorStringW, mixer*, waveIn* (capture, not waveOut), mmio* (incl. CreateChunk/Advance/GetInfo), timeSetEvent/KillEvent, OpenDriver/CloseDriver/GetDriverModuleHandle |
| mciavi32.dll | 27 | MCI AVI (waveOut, DriverCallback, mmio) |
| mstscax.dll | 25 | Remote Desktop client: timeBeginPeriod/EndPeriod, timeSetEvent/KillEvent, timeGetTime, waveIn (audio redirection), waveOut (incl. waveOutGetPitch, Get/SetVolume, Message) |
| mciseq.dll | 24 | MIDI sequencer MCI driver (midiOut*, timers) |
| avicap32.dll | 22 | video capture: mmio*, mciSendStringW, OpenDriver/SendDriverMessage |
| qcap.dll | 16 | capture graph: mixer*, waveIn* |
| mcicda.dll | 15 | CD audio MCI |
| tapi3.dll | 14 | telephony: wave/midi/mixer *Message and open/close |
| msvfw32.dll | 8 | VfW: mciSendString/Command, mciGetErrorString, OpenDriver/CloseDriver, GetDriverModuleHandle |
| dinput.dll, devenum.dll, avifil32.dll (7,7,5) | | mmio* RIFF reading (dinput force-feedback files, avifil32 AVI), device enumeration |
| qedit.dll, sbe.dll, vfwwdm32.dll, serwvdrv.dll, msdtctm.dll, mshtml.dll, dmusic.dll, mfds.dll, tapi32/tapisrv | 2 to 5 | timers (timeBeginPeriod/EndPeriod/timeGetDevCaps, timeSetEvent) for qedit, sbe, msdtctm, mshtml, dmusic, mfds; DefDriverProc/DriverCallback for serwvdrv and vfwwdm32; wave/midi In/Out *Message only for tapi32/tapisrv |
| PlaySoundW only | | authui (logon sound), comctl32 (CCPlaySound), conhostv1, explorerframe, fxsst, ieframe, inputservice, logoncontroller, playsndsrv, stobject, themeui, tsf3gip, webcamui, wiadefui, shell32 |
| timeGetTime only | | cpfilters, psisdecd, rdpendp, termmgr, twinui, shell32, explorerframe |
| DefDriverProc + GetDriverModuleHandle only | | VfW codecs: msrle32, msvidc32, msyuv, tsbyuv, iyuv_32, mciqtz32 |

Export-by-importer-module ranking (distinct importing modules): PlaySoundW 16, DefDriverProc 12, timeGetTime 12, timeSetEvent 9, timeKillEvent 9, GetDriverModuleHandle 9, mmio Close/Read/Seek/Ascend/Descend/OpenW 6-7, waveOutWrite/Prepare/Reset/Open/Close/Message and waveInMessage 6, timeBeginPeriod 6, waveIn* 4-5, timeEndPeriod 5, mixer* 3-4, mciSendString/Command 2. sndPlaySoundW has 2 importers (cloudexperiencehostcommon, mciavi32). Exports with zero importers in the corpus: 78 (corrected from "about 70"): all joy*, all aux*, midiIn* except midiInMessage (tapi), midiConnect/Disconnect, waveOutBreakLoop, waveOutSetPitch, waveOutGet/SetPlaybackRate (waveOutGetPitch IS imported, by mstscax), mciExecute, mciSendCommandA, mciGetDeviceID*, mciGet/SetYieldProc, sndPlaySoundA, PlaySoundA, PlaySound, mmioRenameA (mmioRenameW is used by mciwave), mmioStringToFOURCCA/W, mmioInstallIOProcA/W, mmioSetBuffer/SetInfo/SendMessage, the A-form devcaps/errortext of wave/midi/mixer, waveInGetPosition, midiOutGetID, midiOut Get/SetVolume, midiStreamStop, timeGetSystemTime, mmsystemGetVersion, mmTaskBlock/Signal/Yield, mmDrvInstall, DrvGetModuleHandle, WOWAppExit. They are still correct to test as API but are low consumer value inside Windows (third-party apps differ: xul imports waveOutGetDevCapsA and waveInGetDevCapsA, Chrome and xul import midiIn*).

Caveat: the corpus is Win10 system modules only (callers resolved per call site through the IAT). Third-party programs
add to this: browsers (timeBeginPeriod/EndPeriod/GetTime, the waveOut set, Web MIDI via midiIn/midiOut), remote-desktop
and telephony clients, and industrial or machine-control software, which typically uses timeSetEvent/timeKillEvent and
timeBeginPeriod for cycle timing, timeGetTime for stamps, PlaySound for alarms and waveOut for beeps.

## B. What a conformance corpus should prioritise

Rank = consumer breadth x how easily an implementation gets it wrong / cost (devices needed).

1. **timer**: timeGetTime, timeGetDevCaps, timeBegin/EndPeriod (mismatched, unpaired, out-of-range periods), timeSetEvent/KillEvent (periodic, one-shot, CALLBACK_FUNCTION/EVENT_SET/EVENT_PULSE, TIME_KILL_SYNCHRONOUS, bad ids, kill twice, resolution clamp, uDelay/uResolution validation), timeGetSystemTime sizes. No hardware needed. Highest value.
2. **playsound**: PlaySoundW/A, sndPlaySound, ordinal 2, SND_MEMORY with an in-memory silent wav, SND_NODEFAULT, SND_ASYNC/NOSTOP/PURGE/NOWAIT, SND_ALIAS with an unknown alias, return values and last error with zero devices. 16 importer modules. Do not play system sounds.
3. **waveOut/waveIn zero-device and mapper contract**: open/query/flags/callbacks, devcaps cb and NULL cases, GetID, error text for every code in A and W with buffer truncation, header prepare/write/unprepare state machine, Message/GetVolume/SetVolume, handle misuse. Then, with a virtual HDA device, the real playback path: silent buffer write, WOM_DONE, position units, pause/restart/reset/break loop, volume, pitch/rate, callback kinds, close while playing.
4. **mmio**: open memory/file, descend/ascend/createchunk, buffers, EOF, custom IOProc, rename, FOURCC conversion, flush/advance/sendmessage. Host-side and deterministic; used by dinput, avifil32, avicap32, mciwave.
5. **MCI**: mciSendString/Command parser, error strings for the whole MCIERR range (A and W), mciGetDeviceID*, mciExecute without UI, device open on missing file or device, notify to a message-only window, creator task, yield proc, driver data, command resource loaders.
6. **midiOut/midiIn/midiStream, mixer, aux, joy contract**: zero-device and out-of-range ids, devcaps and error text A/W, header protocol, stream property flags, cbStruct checks, JOYERR codes.
7. **driver API**: OpenDriver/CloseDriver/SendDriverMessage/DefDriverProc/GetDriverModuleHandle/DrvGetModuleHandle/DriverCallback, mmTask*, mmsystemGetVersion; invalid-input and DefDriverProc contract tests only.
8. **mmdevapi device enumeration** via COM (default endpoint, enumeration, GetDevice, id format, property keys) and the winmm-to-WASAPI forward; gated on COM server availability.

Driver-layer behaviour (wdmaud) is only observable on a real guest: record it from a Win10 boot with and without a virtual HDA device, ideally both in one boot where possible.
