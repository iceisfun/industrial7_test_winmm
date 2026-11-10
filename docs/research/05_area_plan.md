# 05 - Area plan for the winmm corpus (first ultracode pass)

Role: AREA PLANNER. Date: 2026-11-10. Inputs: `docs/research/01_export_map.md` .. `04_vm_audio_design.md`,
`tools/winmm_exports.{txt,json}`, `tools/winmmbase_exports.txt`, `tools/mmdevapi_exports.txt`, the method docs
in `/home/iceisfun/test_pattern/docs` (METHOD.md, WRITING_TESTS.md, SAFETY.md, WORKFLOW.md), the harness
(`common/t.h`, `common/modules.h`, `Makefile`), plus spot decomp reads from the Win10 reference corpus
(`/api/function/winmm/{mciGetCreatorTask,mciGetYieldProc,mciSetYieldProc,mciGetDriverData,mciDriverYield,mciGetDeviceIDFromElementIDW}/decomp`,
`/api/function/winmmbase/{mmioSendMessage,mmioSetBuffer,mmioRenameW,mmioFlush,mmioAdvance,midiConnect,auxSetVolume}/decomp`).

Output: `tools/areas.json`: 38 areas, `[{a, d}]`, about 63 kB of scope text. The JSON came from a scratch
generator script, so the JSON is the artifact. Each description starts with
"Primary exports (must each get a real U()/UN() call): ...". Those lists give every one of the 180 named
winmm exports exactly one owning area, and `export_surface_legacy` also owns the unnamed ordinal 2 and the
export table. An export can also appear in other areas as a helper or for a parity check, but its owner
must call it.

Labels in the area text: **[decomp]** read from the corpus decompilation or asm; **[VM]** observed on the
Win10 22H2 oracle (audprobe / fc02probe boots, docs 02 and 04); **(verify)** / **t_note** means a writer must
read the decomp or record the value before any hard CHECK.

## 1. How the areas were cut

1. **Consumer slices come first and go deep.** Doc 03 section D ranks them. In order:
   - timers (3 areas): `time_basic`, `time_events`, `time_event_lifetime`. Industrial7 stubs timeSetEvent.
     That makes these the highest-value tests, and they need no device.
   - PlaySound (2 areas): the headless `playsound_contract` and the device-gated `playsound_device`.
   - waveOut playback (5 areas, plus formats): open contract, header state machine, playback/position/loop,
     callbacks of every type, and controls/messages. `wave_formats` is the WAVE_FORMAT_QUERY matrix, which
     includes EXTENSIBLE, float and ACM tags through the mapper.
   - MCI wave: `mci_waveaudio_device` runs on top of the parser and command areas.
2. **Shared-body facts get one area each.**
   - `mme_error_text`: one table for the 8 error-text exports. They sit on 4 winmmbase bodies.
   - `mme_enumeration_endpoints`: device counts, mapper caps and the mmdevapi cross-check.
   - `mme_handles_threads`: ValidateHandle rules across all handle types, plus thread and lifetime cases.
   - `aw_parity_sweep`: all 17+ A/W pairs, including the structure-size gates.
3. **Device dependence decides the split, not the name prefix.** Contract areas run on every guest with
   exact codes, because winmmbase checks parameters before devices. Device areas gate on `*GetNumDevs` and
   still assert the zero-device contract when no device exists. This follows doc 04 rule 1.
4. **MIDI follows the VM facts.**
   - The GS synth and the MIDI mapper are always enumerated, but on a guest with no audio they cannot be
     opened (code 1).
   - The synth is single-open per process (code 4).
   - So `midiout_contract` is headless, and `midiout_device` and `midistream` gate on a wave device being
     present.
5. **dx_ areas:**
   - `dx_mixer_volume_set` covers endpoint mute/volume, per-app session volume, and midi/aux volume, always
     with restore.
   - `dx_legacy_process` covers joyConfigChanged(0) broadcast, WOWAppExit, winmmbaseFreeMMEHandles and
     sndOpenSound, each in a CHILD process.
   - Everything else is non-destructive.
6. **Size:** 38 areas, 2 to 15 primary exports each. Writer prompts are dense, so per-area work stays at one
   writer plus one reviewer.

## 2. Area list (order = suggested pipeline order)

| # | area | primary exports | needs |
|---|---|---:|---|
| 1 | `time_basic` | 5 | none |
| 2 | `time_events` | 2 | none |
| 3 | `time_event_lifetime` | 0 | none |
| 4 | `playsound_contract` | 5 | none (0-dev branch) |
| 5 | `playsound_device` | 0 | waveOut |
| 6 | `waveout_open_contract` | 3 | contract + waveOut |
| 7 | `waveout_header_protocol` | 3 | contract + waveOut |
| 8 | `waveout_playback` | 5 | waveOut |
| 9 | `waveout_callbacks` | 0 | waveOut (+waveIn) |
| 10 | `waveout_controls_messages` | 7 | contract + waveOut |
| 11 | `wave_formats` | 0 | waveOut/waveIn |
| 12 | `wavein_contract` | 13 | none |
| 13 | `wavein_capture` | 0 | waveIn (duplex) |
| 14 | `mme_error_text` | 8 | none |
| 15 | `mme_enumeration_endpoints` | 9 | both configs |
| 16 | `mme_handles_threads` | 0 | mmio always; waveOut optional |
| 17 | `midiout_contract` | 15 | none (synth always enumerated) |
| 18 | `midiout_device` | 0 | waveOut (synth renders through it) |
| 19 | `midistream` | 8 | contract + waveOut |
| 20 | `midiin_thru` | 14 | none |
| 21 | `mixer_contract` | 13 | none |
| 22 | `mixer_topology` | 0 | endpoints |
| 23 | `dx_mixer_volume_set` | 0 | endpoints, dx |
| 24 | `aux_devices` | 5 | none |
| 25 | `joystick` | 9 | none |
| 26 | `mmio_memory_buffered` | 10 | none |
| 27 | `mmio_riff_chunks` | 5 | none |
| 28 | `mmio_files_ioprocs` | 6 | none (scratch dir) |
| 29 | `mci_string_parser` | 2 | none |
| 30 | `mci_command_interface` | 4 | none |
| 31 | `mci_waveaudio_device` | 0 | waveOut/waveIn |
| 32 | `mci_devices_misc` | 8 | partly waveOut |
| 33 | `mci_driver_side` | 6 | none |
| 34 | `driver_api` | 6 | none |
| 35 | `driver_callback_tasks` | 6 | none |
| 36 | `export_surface_legacy` | 3 | none |
| 37 | `aw_parity_sweep` | 0 | gates none; values with devices |
| 38 | `dx_legacy_process` | 0 | none, dx (CHILD) |

Areas with 0 primary exports are cross-cutting. `time_event_lifetime`, `playsound_device`, `waveout_callbacks`,
`wave_formats`, `wavein_capture`, `midiout_device`, `mixer_topology`, `dx_mixer_volume_set`,
`mci_waveaudio_device`, `aw_parity_sweep` and `dx_legacy_process` test behaviour of exports that other areas
own.

## 3. Primary-export ownership (180 named exports, each exactly once)

- `time_basic`: timeGetTime, timeGetSystemTime, timeGetDevCaps, timeBeginPeriod, timeEndPeriod
- `time_events`: timeSetEvent, timeKillEvent
- `playsound_contract`: PlaySound, PlaySoundA, PlaySoundW, sndPlaySoundA, sndPlaySoundW
- `waveout_open_contract`: waveOutOpen, waveOutClose, waveOutGetID
- `waveout_header_protocol`: waveOutPrepareHeader, waveOutUnprepareHeader, waveOutWrite
- `waveout_playback`: waveOutPause, waveOutRestart, waveOutReset, waveOutBreakLoop, waveOutGetPosition
- `waveout_controls_messages`: waveOutGetVolume, waveOutSetVolume, waveOutGetPitch, waveOutSetPitch, waveOutGetPlaybackRate, waveOutSetPlaybackRate, waveOutMessage
- `wavein_contract`: waveInOpen, waveInClose, waveInGetDevCapsA, waveInGetDevCapsW, waveInPrepareHeader, waveInUnprepareHeader, waveInAddBuffer, waveInStart, waveInStop, waveInReset, waveInGetPosition, waveInGetID, waveInMessage
- `mme_error_text`: waveOutGetErrorTextA, waveOutGetErrorTextW, waveInGetErrorTextA, waveInGetErrorTextW, midiOutGetErrorTextA, midiOutGetErrorTextW, midiInGetErrorTextA, midiInGetErrorTextW
- `mme_enumeration_endpoints`: waveOutGetNumDevs, waveInGetNumDevs, midiOutGetNumDevs, midiInGetNumDevs, mixerGetNumDevs, auxGetNumDevs, joyGetNumDevs, waveOutGetDevCapsA, waveOutGetDevCapsW
- `midiout_contract`: midiOutOpen, midiOutClose, midiOutGetDevCapsA, midiOutGetDevCapsW, midiOutShortMsg, midiOutLongMsg, midiOutPrepareHeader, midiOutUnprepareHeader, midiOutReset, midiOutGetID, midiOutGetVolume, midiOutSetVolume, midiOutCachePatches, midiOutCacheDrumPatches, midiOutMessage
- `midistream`: midiStreamOpen, midiStreamClose, midiStreamOut, midiStreamPause, midiStreamPosition, midiStreamProperty, midiStreamRestart, midiStreamStop
- `midiin_thru`: midiInOpen, midiInClose, midiInGetDevCapsA, midiInGetDevCapsW, midiInPrepareHeader, midiInUnprepareHeader, midiInAddBuffer, midiInStart, midiInStop, midiInReset, midiInGetID, midiInMessage, midiConnect, midiDisconnect
- `mixer_contract`: mixerOpen, mixerClose, mixerGetDevCapsA, mixerGetDevCapsW, mixerGetID, mixerMessage, mixerGetLineInfoA, mixerGetLineInfoW, mixerGetLineControlsA, mixerGetLineControlsW, mixerGetControlDetailsA, mixerGetControlDetailsW, mixerSetControlDetails
- `aux_devices`: auxGetDevCapsA, auxGetDevCapsW, auxGetVolume, auxSetVolume, auxOutMessage
- `joystick`: joyGetDevCapsA, joyGetDevCapsW, joyGetPos, joyGetPosEx, joyGetThreshold, joySetThreshold, joySetCapture, joyReleaseCapture, joyConfigChanged
- `mmio_memory_buffered`: mmioRead, mmioWrite, mmioSeek, mmioGetInfo, mmioSetInfo, mmioSetBuffer, mmioFlush, mmioAdvance, mmioClose, mmioSendMessage
- `mmio_riff_chunks`: mmioDescend, mmioAscend, mmioCreateChunk, mmioStringToFOURCCA, mmioStringToFOURCCW
- `mmio_files_ioprocs`: mmioOpenA, mmioOpenW, mmioRenameA, mmioRenameW, mmioInstallIOProcA, mmioInstallIOProcW
- `mci_string_parser`: mciSendStringA, mciSendStringW
- `mci_command_interface`: mciSendCommandA, mciSendCommandW, mciGetErrorStringA, mciGetErrorStringW
- `mci_devices_misc`: mciGetDeviceIDA, mciGetDeviceIDW, mciGetDeviceIDFromElementIDA, mciGetDeviceIDFromElementIDW, mciGetCreatorTask, mciGetYieldProc, mciSetYieldProc, mciExecute
- `mci_driver_side`: mciDriverNotify, mciDriverYield, mciGetDriverData, mciSetDriverData, mciLoadCommandResource, mciFreeCommandResource
- `driver_api`: OpenDriver, CloseDriver, SendDriverMessage, DefDriverProc, DrvGetModuleHandle, GetDriverModuleHandle
- `driver_callback_tasks`: DriverCallback, mmTaskCreate, mmTaskBlock, mmTaskSignal, mmTaskYield, mmGetCurrentTask
- `export_surface_legacy`: mmsystemGetVersion, WOWAppExit, mmDrvInstall

The unnamed ordinal 2 is owned by `export_surface_legacy`, and `playsound_contract` calls it for parity.
Check with `python3 tools/coverage.py .` after each phase: the "not called" list must end empty.

## 4. Facts used in the area text that this pass added (decomp reads)

- `mciGetCreatorTask(id)`:
  - Returns NULL for id 0, for id >= next id, and for a free slot.
  - Otherwise returns the task stored in the device entry (+0x50).
- `mciGetYieldProc(id, pdw)`:
  - Returns NULL for an unknown id.
  - For a live id it returns the proc at +0x28, and writes `*pdw` only when pdw is not NULL.
  - `mciSetYieldProc` returns FALSE for an unknown id. For a live id it returns TRUE and stores the proc and
    the data.
- `mciDriverYield(id)` returns 0 when the id is unknown or no yield proc is set. Otherwise it calls
  `proc(id, data)` and returns that result.
- `mciGetDriverData` returns 0 for an unknown id.
- `mciGetDeviceIDFromElementIDW(elem, NULL type)` returns 0. Otherwise it matches entries that have flag
  0x800, the element id, and a type string compared with lstrcmpi.
- `mmioSendMessage(bad handle)` returns 0. Otherwise it calls the handle's IO proc.
- `mmioFlush(bad)` returns 5. On a memory file the flush is a no-op (fccIOProc 'MEM '). A short disk write
  returns 262.
- `mmioAdvance`:
  - bad handle -> 5;
  - no buffer -> 0x10A (MMIOERR_UNBUFFERED);
  - memory file without MMIO_WRITE -> 0x105;
  - memory file with no room to grow or a zero increment -> 0x108;
  - disk read failure -> 0x105.
- `mmioSetBuffer(bad)` returns 5. A NULL buffer with a positive size allocates internally; an allocation
  failure returns 0x102.
- `mmioRenameW`:
  - Picks the IO proc from the name, or from the MMIOINFO you pass.
  - Converts both names to ANSI and sends message 6 (MMIOM_RENAME) to that proc. This applies to custom
    procs as well.
- `midiConnect(hmi, hmo, p)` is `midiInSetThru(hmi, hmo, 1)`. pReserved is only traced.
- `auxSetVolume(id, v)` is `auxOutMessage(id, AUXDM_SETVOLUME 6, v, devinterface)`. With 0 aux devices it
  returns 2.

## 5. Safety paragraph (extraRules for the workflow)

> winmm extra safety rules:
>
> 1. **Audio output.**
>    - Only silence, or tones of 0.5 s or less at about -30 dBFS or quieter, built in memory.
>    - Never play a system sound, alias, or default sound.
>    - Always pass SND_NODEFAULT on every PlaySound/sndPlaySound call that names something.
>    - End every PlaySound test with `PlaySound(NULL, NULL, 0)`.
>    - Send all-notes-off (CC 0x7B on all 16 channels) before closing a MIDI handle.
> 2. **State restore.**
>    - Pair every `timeBeginPeriod` with `timeEndPeriod`.
>    - Kill every `timeSetEvent` timer before the TEST returns.
>    - Close every wave/midi/mixer/mmio handle, every MCI device (`close all`), and every installed mmio IO
>      proc (MMIO_REMOVEPROC).
>    - Delete scratch files.
>    - Outside `dx_` areas, a volume or mixer Set is allowed only as an exact write-back of the value just
>      read, on a handle the test opened. A real change belongs in `dx_mixer_volume_set` and is restored on
>      every path.
>    - Never change default devices, registry (`Drivers32`, `MCI32`, AppEvents), or MCI system settings.
> 3. **Known hazards.**
>    - Never call `mciSendStringA/W(NULL, ...)`: both fault.
>    - Never call `mciExecute` with a command that can fail: it opens a MessageBox.
>    - Never call `mmTaskBlock` unless a 0x400 thread message is already queued: it has no timeout.
>    - Never call `timeKillEvent(0)` as an "unknown id": it matches a free slot. Use 0x12345.
>    - Never call `joyConfigChanged(0)`, `WOWAppExit`, `winmmbaseFreeMMEHandles` or `mmDrvInstall` outside
>      `dx_legacy_process`, and there only in a CHILD.
>    - Never call `winmmbaseGet/SetWOWHandle` on a non-handle.
>    - Never pass garbage pointers. Bad handles are NULL, small ints, -1, closed handles, or live handles of
>      another type.
> 4. **Devices.**
>    - The first `*GetNumDevs` call can block while Audiosrv starts (up to about 2 min): never time it.
>    - Read the counts once per test and gate the device half on them. A succeeding mapper caps call is not
>      device presence.
>    - The GS synth is single-open per process: close each MIDI handle before the next open.
>    - With no wave device, `midiOutOpen` returns 1, not 0.
>    - Every test must pass on both VM configurations: `noaudio`, and HDA duplex
>      (`-audiodev none|wav,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0`).
> 5. **Waits and timing.**
>    - Every wait is 3 s or less, and every loop is bounded.
>    - Callbacks: window/thread callbacks use a bounded PeekMessage pump on a message-only window created by
>      the test. Function callbacks only record data (no waveOut*/midiOut* calls inside them).
>    - Timing assertions use only wide bounds. Positions and byte counts are exact.
> 6. **Linking.** The Makefile links only kernel32/advapi32/ntdll. user32, ole32 and mmdevapi COM calls go
>    through `t_module` + GetProcAddress, like the DLL under test. Declare prototypes missing from mingw
>    (mmddk.h driver-side MCI functions, mmTask*, DriverCallback, winmmbase extras) with UDECL/UN.

## 6. Notes for the workflow operator

- **VM boots.**
  - Batch every area exe into one payload and run it twice per oracle pass: once `noaudio` and once HDA
    duplex (W10_EXTRA). Boots stay under 15 minutes. Each exe has a 600 s cap in `run_all.bat`.
  - `dx_*` areas run only on `/destructive`, after the non-dx pass in the same boot.
- **Highest-value VM questions to settle first.** These are the "verify" items:
  - wdmaud callback order and close-with-queued -> 33;
  - pitch/playback-rate answers;
  - zero-length writes;
  - waveIn Stop with a partial buffer;
  - midiStream property defaults;
  - MCI notify codes on stop;
  - `open new type waveaudio` with zero devices;
  - mixerGetLineControls gates;
  - JOYCAPSA sizes;
  - mmio MMIO_GETTEMP and IO-proc unicode handling.
- **Wine** is only plumbing (does it compile, does it exit). Wine has no MME devices in the sandbox, so
  every device half takes the zero-device branch there.
