# 01 Export map: winmm.dll (Windows 10 x64, 10.0.19041.x)

Scope: every export of `winmm.dll` (181 entries, ordinals 2..182, one of them nameless), the extra exports of
`winmmbase.dll` (141 named) and `mmdevapi.dll` (35, mostly ordinal-only), sorted into API families, with what each
export really runs, whether it is documented, and how much of it a test can check on a headless VM.

Sources: `tools/winmm_exports.json`, `tools/winmmbase_exports.json`, `tools/mmdevapi_exports.json`, plus the
Win10 reference corpus API (`/api/modules/winmm/imports`, `/api/function/winmm/<rva>`, `/callees`, `/asm`,
`/decomp`, `/api/docs/for-symbol`). Labels: **[decomp]** read from the corpus disassembly or decompilation;
**[docs]** a vendored Learn page exists; **[VM?]** has to be confirmed on the Win10 guest before a hard CHECK.

## 1. How winmm.dll is built (the key fact for the corpus)

winmm.dll is mostly a **facade**. The export table has **no PE forwarders** (every `forwarder` field is null).
Instead, 140 exports are 7-byte `jmp qword ptr [rip+IAT]` thunks whose IAT slots are delay-load imports
[decomp]:

| winmm imports from | resolves to | exports thunked | which |
|---|---|---:|---|
| `api-ms-win-mm-mme-l1-1-0` (delay) | winmmbase.dll | 102 | all wave out/in, midi out/in/stream, mixer, aux, midiConnect/Disconnect |
| `api-ms-win-mm-misc-l1-1-0` (delay) | winmmbase.dll | 22 | all 21 mmio* plus mmDrvInstall |
| `WINMMBASE.dll` (delay, by name) | winmmbase.dll | 11 | OpenDriver, CloseDriver, SendDriverMessage, DefDriverProc, DrvGetModuleHandle, GetDriverModuleHandle, mmTaskCreate/Block/Signal/Yield, mmGetCurrentTask |
| `api-ms-win-mm-time-l1-1-0` (static) | **kernel32.dll** | 5 | timeBeginPeriod, timeEndPeriod, timeGetDevCaps, timeGetSystemTime, timeGetTime |
| (none: implemented in winmm) | winmm.dll | 41 | PlaySound*, sndPlaySound*, all mci*, all joy*, timeSetEvent/timeKillEvent, DriverCallback, WOWAppExit, mmsystemGetVersion, ordinal 2 |

The import of `winmmbaseFreeMMEHandles` (12th WINMMBASE import) is used internally (WOWAppExit path), not
re-exported.

Reverse edge: winmmbase.dll imports `timeSetEvent` / `timeKillEvent` from `api-ms-win-mm-misc-l2-1-0`, which
resolves back to **winmm.dll** (0x2a20 / 0x2e50) [decomp]. So winmm and winmmbase depend on each other; the
time-event thread lives in winmm, the MME device layer in winmmbase.

Device enumeration in winmmbase: every `*GetNumDevs` calls `ClientUpdatePnpInfo`, which binds to the
**Audiosrv** RPC endpoint, waits for the service, calls `mmdevapi!InitializeDeviceAPI` (ordinal 3) and
`mmdevapi!MMDeviceGetDeviceEnumerator` (ordinal 5), gathers endpoints and MIDI interfaces, then rebuilds the
device lists and the mapper [decomp]. The per-device driver (wdmaud.drv) is **not in the corpus**, so everything
below the `wodMessage`/`widMessage`/`modMessage` boundary is evidence-by-VM only.

### Identical-code folding (aliases that tests can pin)

Same RVA, so behaviour must be identical byte for byte [decomp]:

| export | same body as |
|---|---|
| winmm ordinal 2 (no name) | `PlaySound` (winmm 0x98b0, a jmp to `PlaySoundA`) |
| `PlaySound` | `PlaySoundA` |
| `GetDriverModuleHandle` | `DrvGetModuleHandle` (one winmmbase body; the winmm thunk jumps through the DrvGetModuleHandle IAT slot) |
| `waveOutGetErrorTextA/W` | `waveInGetErrorTextA/W` (winmmbase 0x10340 / 0x10320) |
| `midiOutGetErrorTextA/W` | `midiInGetErrorTextA/W` (winmmbase 0x7d60 / 0x7d40) |

Consequence: the wave-out and wave-in error tables are one table, as are midi-out and midi-in; a test can assert
equal output for every code, and `waveInGetErrorText(WAVERR_*)` returns wave text too.

Other notable bodies [decomp]:

* `sndPlaySoundA(psz, f)` = `PlaySoundA(psz, NULL, f)` (tail jump, flags unchanged).
* `sndPlaySoundW(psz, f)` = internal `sndPlaySoundI(psz, NULL, f)`, the same worker `PlaySoundW` ends in.
* `joyGetNumDevs` = `mov eax,16; ret` (constant 16, no device query).
* `mmsystemGetVersion` = `mov eax,0x30a; ret`.
* `mciDriverNotify(hwnd, devid, status)` = `PostMessageW(hwnd, MM_MCINOTIFY 0x3b9, status, devid)`.
* `DriverCallback`: winmm has **its own** copy (0x1426c, 165 bytes) separate from winmmbase's (0x5560). Type is
  `dwFlags & 7`: 1 window -> PostMessageA, 2 task -> PostThreadMessageA, 3 function -> call, 5 event -> SetEvent;
  NULL target or any other type returns 0 / delivers nothing.
* `joyConfigChanged(0)` closes all joysticks and **broadcasts** a registered message with
  `PostMessageW(HWND_BROADCAST, ...)`; non-zero flags return 165 (JOYERR_PARMS) without doing anything.
* `mciExecute(cmd)` = `mciSendString`, and on **failure shows a MessageBoxW** with the error text.
* `mmGetCurrentTask` = `GetCurrentThreadId` (tail jump in winmmbase).
* `mmTaskCreate` = CreateThread (stack 0x200) with optional event; `mmTaskBlock` pumps
  GetMessageA/DispatchMessageA; `mmTaskSignal` = PostThreadMessageA.
* `WOWAppExit(task)` closes every MCI device owned by the task (MCI_CLOSE), kills its timer events and frees MME
  handles (winmmbaseFreeMMEHandles).

## 2. Family table

"Headless" = a QEMU guest with no audio device (Audiosrv running, zero endpoints). "HDA" = guest started with
`W10_EXTRA='-audiodev none,id=snd0 -device intel-hda -device hda-duplex,audiodev=snd0'`.

| family | exports | documented | forwards to | testable headless | with HDA adds |
|---|---|---|---|---|---|
| **time** | timeGetTime, timeGetSystemTime, timeGetDevCaps, timeBeginPeriod, timeEndPeriod, timeSetEvent, timeKillEvent (7) | yes (timeapi; set/kill on Learn as obsolete) | first 5 -> kernel32 (api-ms-win-mm-time); set/kill local in winmm | **full**: monotonic time, MMTIME struct size gates (TIMERR_STRUCT 0x81 / TIMERR_NOCANDO 0x61), begin/end pairing and range, one-shot and periodic events with bounded waits, TIME_CALLBACK_EVENT_SET/PULSE, kill of bad ids | nothing |
| **wave out** | waveOutGetNumDevs, GetDevCapsA/W, GetErrorTextA/W, Open, Close, PrepareHeader, UnprepareHeader, Write, Pause, Restart, Reset, BreakLoop, GetPosition, GetPitch, SetPitch, GetPlaybackRate, SetPlaybackRate, GetVolume, SetVolume, GetID, Message (23) | yes (mmeapi) | winmmbase (mm-mme) | **contract**: 0 devices, GetDevCaps(id>=n) BADDEVICEID, WAVE_MAPPER open -> BADDEVICEID / format query, NULL and flag gates (INVALPARAM 11, INVALFLAG 10), WAVEHDR gates checked before the handle (NULL, cb<0x30, bad dwFlags), INVALHANDLE on NULL/closed handles, error texts for every MMSYSERR/WAVERR code | open/close per format, WAVE_FORMAT_QUERY matrix, prepare/write/done order, callback kinds (function, event, thread, window), position units, pause/restart/reset/breakloop, volume get/set (restore), STILLPLAYING/UNPREPARED |
| **wave in** | waveInGetNumDevs, GetDevCapsA/W, GetErrorTextA/W, Open, Close, PrepareHeader, UnprepareHeader, AddBuffer, Start, Stop, Reset, GetPosition, GetID, Message (16) | yes (mmeapi) | winmmbase (mm-mme) | contract, as wave out; prepare zeroes dwFlags first | capture (hda-duplex input, silence), AddBuffer/Start/Stop/Reset return order, WHDR_DONE with partial dwBytesRecorded |
| **midi out** | midiOutGetNumDevs, GetDevCapsA/W, GetErrorTextA/W, Open, Close, PrepareHeader, UnprepareHeader, ShortMsg, LongMsg, Reset, GetVolume, SetVolume, CachePatches, CacheDrumPatches, GetID, Message (18) | yes | winmmbase | contract + likely the software synth (Microsoft GS Wavetable Synth) as device 0 [VM?: whether it enumerates and opens with zero audio endpoints] | real open/short/long msg on the synth, volume restore |
| **midi in** | midiInGetNumDevs, GetDevCapsA/W, GetErrorTextA/W, Open, Close, PrepareHeader, UnprepareHeader, AddBuffer, Start, Stop, Reset, GetID, Message (15) | yes | winmmbase | contract only (no MIDI input on a VM) | none expected |
| **midi stream / thru** | midiStreamOpen, Close, Out, Pause, Restart, Stop, Position, Property, midiConnect, midiDisconnect (10) | yes | winmmbase | argument gates (cMidi must be 1, NULL id pointer, bad property flags), Open on bad id; on the GS synth if present [VM?] | MEVT_TEMPO / timediv property round trip, MEVT_NOP stream with callbacks |
| **mixer** | mixerGetNumDevs, GetDevCapsA/W, Open, Close, GetID, GetLineInfoA/W, GetLineControlsA/W, GetControlDetailsA/W, SetControlDetails, Message (14) | yes | winmmbase | contract: 0 mixers, bad id BADDEVICEID, MIXER_OBJECTF_* flag and struct-size gates (cbStruct), INVALHANDLE | line/control topology, details get, set only with restore (dx_), mixerGetID mapping from a wave handle |
| **aux** | auxGetNumDevs, GetDevCapsA/W, GetVolume, SetVolume, OutMessage (6) | yes | winmmbase | contract (bad id, NULL pointers) | probably still 0 aux devices on Win10 [VM?] |
| **joystick** | joyGetNumDevs, GetDevCapsA/W, GetPos, GetPosEx, GetThreshold, SetThreshold, SetCapture, ReleaseCapture, ConfigChanged (10) | yes (joystickapi) | local in winmm | **full negative surface**: constant 16 devices, no joystick -> error codes per call (JOYERR_UNPLUGGED 167 / JOYERR_PARMS 165 / MMSYSERR_NODRIVER [VM?]), JOYINFOEX dwSize gate, SetCapture needs a window; ConfigChanged(0) broadcasts, so only the non-zero-flags path outside dx_ | nothing (no joystick device) |
| **mmio** | mmioOpenA/W, Close, Read, Write, Seek, GetInfo, SetInfo, SetBuffer, Flush, Advance, Ascend, Descend, CreateChunk, InstallIOProcA/W, SendMessage, RenameA/W, StringToFOURCCA/W (21) | yes (mmiscapi) | winmmbase (mm-misc) | **full**: memory files (MMIO_ALLOCBUF/`+` memory files), scratch disk files, RIFF build/parse, buffered I/O, custom IO procs, FOURCC conversion, rename in scratch dir, error codes MMIOERR_* | nothing |
| **PlaySound** | PlaySound, PlaySoundA, PlaySoundW, sndPlaySoundA, sndPlaySoundW, ordinal 2 (6) | yes (playsoundapi / mmsystem) | local in winmm | stop (`NULL`) returns TRUE, flag combinations, SND_NODEFAULT failure on missing alias/file, SND_MEMORY with a short silent in-memory WAV (result depends on device: [VM?] FALSE or TRUE headless), SND_ASYNC + stop | play short silent memory WAV synchronously/asynchronously, SND_LOOP + stop |
| **MCI** | mciSendCommandA/W, mciSendStringA/W, mciGetErrorStringA/W, mciGetDeviceIDA/W, mciGetDeviceIDFromElementIDA/W, mciGetCreatorTask, mciGetYieldProc, mciSetYieldProc, mciExecute (14) | yes (mmsystem MCI pages) | local in winmm; device types load separate MCI driver DLLs (mciwave, mciseq, mciavi32: not in corpus) | error strings for every MCIERR code, unknown-command / unknown-device errors, `sysinfo all quantity`, `open new type waveaudio alias x` + `close`, `status`/`set` on a memory-free waveaudio element, mciGetDeviceID of unknown name = 0, notify via hidden window; **never mciExecute with a failing command** (message box) | `record`/`play` on waveaudio (silence), `capability` answers, MM_MCINOTIFY ordering |
| **MCI driver side** | mciDriverNotify, mciDriverYield, mciGetDriverData, mciSetDriverData, mciLoadCommandResource, mciFreeCommandResource (6) | DDK only (mmddk.h, legacy) | local in winmm | invalid device id paths (FALSE/0), DriverNotify posts MM_MCINOTIFY to a test window, Get/SetDriverData on an opened waveaudio id (restore) | same |
| **installable driver** | OpenDriver, CloseDriver, SendDriverMessage, DefDriverProc, DrvGetModuleHandle, GetDriverModuleHandle, DriverCallback (7) | yes (mmiscapi) | winmmbase (DriverCallback: local copy in winmm) | **full**: OpenDriver of a missing driver -> NULL, NULL-handle Send/Close, DefDriverProc default answers per DRV_* message, DriverCallback all DCB_* types with event/function/thread/window targets | opening the real wdmaud.drv entry (aliases in Drivers32) [VM?] |
| **mm task (legacy DDK)** | mmTaskCreate, mmTaskBlock, mmTaskSignal, mmTaskYield, mmGetCurrentTask (5) | DDK (mmddk.h) | winmmbase | **full**: mmGetCurrentTask == GetCurrentThreadId, create a task thread, signal/block round trip with bounded wait | nothing |
| **legacy / undocumented** | mmsystemGetVersion, WOWAppExit, mmDrvInstall (3); ordinal 2 is counted under PlaySound | no (version: header only) | version/WOW local; mmDrvInstall -> winmmbase | mmsystemGetVersion == 0x30a; **WOWAppExit and mmDrvInstall: existence only** (process-wide teardown / driver install) | nothing |

Totals: 7 + 23 + 16 + 18 + 15 + 10 + 14 + 6 + 10 + 21 + 6 + 14 + 6 + 7 + 5 + 3 = **181**, matching the export
table (the appendix lists every row).

## 3. winmmbase.dll: exports that are not in winmm

winmmbase (141 named, no ordinal-only) holds every MME body above. Exports winmm does **not** re-export:

| export | documented | what it does [decomp] | test |
|---|---|---|---|
| `sndOpenSound` | yes (mmiscapi page) | binds ncalrpc `Audiosrv`, NdrClientCall3 proc 8 of the AudioSrv interface | existence + the call shape only after VM observation; not an app-level API |
| `winmmbaseFreeMMEHandles(task)` | no | walks the MME handle list, resets and closes every handle of that owner | existence only (would close the test's own handles) |
| `winmmbaseGetWOWHandle(h)` | no | returns the WORD at `h - 0x38` of the internal handle | existence; only on a live handle, never garbage |
| `winmmbaseSetWOWHandle(h, w)` | no | stores the WORD | existence (round trip on a live handle at most) |
| `winmmbaseHandle32FromHandle16(w)` | no | list lookup by WOW handle, 0 when none | `(0xBEEF)` -> 0 is a safe probe |

`timeGetTime` and friends are not exported by winmmbase (it imports them from kernel32 like winmm does).

## 4. mmdevapi.dll

35 exports: `ActivateAudioInterfaceAsync` (documented, mmdeviceapi.h), the four COM `Dll*` entries, and 30
**ordinal-only** private APIs. Resolved names [decomp, symbols]:

| ord | name | ord | name |
|---:|---|---:|---|
| 2 | CleanupDeviceAPI | 18 | GetEndpointIdFromDeviceInterfaceId |
| 3 | InitializeDeviceAPI (imported by winmmbase) | 19 | mmdDevFindMmDevProperty |
| 4 | MMDeviceCreateRegistryPropertyStore | 20 | mmdDevGetInterfacePropertyStore |
| 5 | MMDeviceGetDeviceEnumerator (imported by winmmbase) | 21 | mmdDevGetInterfaceIdFromMMDevice |
| 6 | MMDeviceGetEndpointManager | 22 | mmdDevGetInterfaceDataFlow |
| 7 | GetClassFromEndpointId | 23 | mmdDevGetMMDeviceFromInterfaceId |
| 8 | GetEndpointGuidFromEndpointId | 24 | mmdDevGetInterfaceClassGuid |
| 9 | GetSessionIdFromEndpointId | 25 | mmdDevGetMMDeviceIdFromInterfaceId |
| 10 | RegisterForMediaCallback | 26 | mmdDevGetInstanceIdFromInterfaceId |
| 11 | UnregisterMediaCallback | 27 | mmdDevGetRelatedInterfaceId |
| 12 | GenerateMediaEvent | 28 | mmdDevGetInterfaceIdFromMMDeviceId |
| 13 | AETraceOutputDebugString | 29 | mmdDevGetInstanceIdFromMMDeviceId |
| 14 | MMDeviceGetPolicyConfig | 30 | mmdDevGetEndpointFormFactorFromMMDeviceId |
| 15 | FlushDeviceTopologyCache | 31 | mmdDevGetDeviceIdFromPnpInterface |
| 16 | GetNeverSetAsDefaultProperty | 32 | GetCategoryPath |
| 17 | ActivateAudioInterfaceAsync (named) | 33-36 | DllCanUnloadNow, DllGetClassObject, DllRegisterServer, DllUnregisterServer |

For this corpus mmdevapi is a **cross-check oracle**, not a test target: `CoCreateInstance(CLSID_MMDeviceEnumerator)`
-> `EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE)` count should match `waveOutGetNumDevs()` (and eCapture vs
`waveInGetNumDevs()`) [VM?: exact equality, the mapper, and disabled endpoints]. Ordinal-only private exports:
existence probes via `GetProcAddress(h, MAKEINTRESOURCE(n))` only.

## 5. Headless testability summary

| class | families | exports (winmm) |
|---|---|---:|
| fully testable with no audio hardware | time, mmio, installable driver + DriverCallback, mm task, mmsystemGetVersion | 41 |
| full negative surface, no positive path possible on any VM | joystick, aux (expected), midi in | 31 |
| contract headless, device paths need HDA | wave out, wave in, mixer, PlaySound (incl. ordinal 2), MCI, MCI driver side | 79 |
| midi out / stream: depends on whether the GS synth enumerates without an endpoint [VM?] | midi out, midi stream/thru | 28 |
| existence only (unsafe to drive) | WOWAppExit, mmDrvInstall (and joyConfigChanged(0) outside dx_) | 2 |

Device-count-aware pattern for every MME family: read `xxxGetNumDevs()`, always run the contract half
(id == n -> MMSYSERR_BADDEVICEID, WAVE_MAPPER / MIDI_MAPPER behaviour, NULL/flags/struct-size gates, INVALHANDLE),
and run the device half only when n > 0, emitting `t_note` with the count and caps names.

## 6. Hazards found while mapping (for WRITING_TESTS / SAFETY)

* `mciExecute` opens a **MessageBoxW** on any failing command: call it only with a command that succeeds, or not at all.
* `joyConfigChanged(0)` posts to **HWND_BROADCAST**: dx_ only; `joyConfigChanged(1)` -> 165 is the safe probe.
* `WOWAppExit`, `winmmbaseFreeMMEHandles`: process-wide teardown of MCI devices, timers and MME handles.
* `mmDrvInstall`: installs a driver into the process' driver lists.
* `winmmbaseGetWOWHandle/SetWOWHandle`: dereference `handle - 0x38` with no validation; never pass a non-handle.
* `timeBeginPeriod` must be paired with `timeEndPeriod` for the same value; resolution is process/system state.
* `waveOutSetVolume`, `midiOutSetVolume`, `auxSetVolume`, `mixerSetControlDetails`: read, change, restore.
* Volume/mute and default-device state live in Audiosrv and survive the process: dx_ areas only.

## Appendix: all 181 winmm exports

"winmm body" from `/api/function/winmm/<rva>` size and `/asm`; "resolves to" from `/callees` and the import table
(the API set in parentheses is the import DLL name); "documented" from `/api/docs/for-symbol` exact function
pages, else the header family where the vendored docs only mention the name.

| ord | export | winmm body | resolves to | documented |
|---:|---|---|---|---|
| 2 | `(ordinal only)` | alias of PlaySound (same RVA 0x98b0) | winmm!PlaySoundA (jmp) | no name; same body as documented PlaySound |
| 3 | `mciExecute` | local, 459 bytes | winmm (local) -> AllocUnicodeStr, mciEatToken, mciGetErrorStringW | yes, mmsystem.h (Learn, MCI) |
| 4 | `CloseDriver` | 7-byte `jmp [IAT]` thunk | winmmbase!CloseDriver (via WINMMBASE) | yes (mmiscapi) |
| 5 | `DefDriverProc` | 7-byte `jmp [IAT]` thunk | winmmbase!DefDriverProc (via WINMMBASE) | yes (mmiscapi) |
| 6 | `DriverCallback` | 5-byte jmp to local body (165 bytes) | winmm (local copy; winmmbase has its own at 0x5560) | yes (mmiscapi) |
| 7 | `DrvGetModuleHandle` | 7-byte `jmp [IAT]` thunk | winmmbase!DrvGetModuleHandle (via WINMMBASE) | yes (mmiscapi) |
| 8 | `GetDriverModuleHandle` | 7-byte `jmp [IAT]` thunk | winmmbase!DrvGetModuleHandle (via WINMMBASE) **(shared body)** | yes (mmiscapi) |
| 9 | `OpenDriver` | 7-byte `jmp [IAT]` thunk | winmmbase!OpenDriver (via WINMMBASE) | yes (mmiscapi) |
| 10 | `PlaySound` | 5-byte jmp | winmm!PlaySoundA | yes (Multimedia) |
| 11 | `PlaySoundA` | local, 196 bytes | winmm (local) -> Imbstowcs, sndPlaySoundI | yes, playsoundapi.h (Learn) |
| 12 | `PlaySoundW` | local, 127 bytes | winmm (local) -> sndPlaySoundI | yes, playsoundapi.h (Learn) |
| 13 | `SendDriverMessage` | 7-byte `jmp [IAT]` thunk | winmmbase!SendDriverMessage (via WINMMBASE) | yes (mmiscapi) |
| 14 | `WOWAppExit` | local, 495 bytes | winmm (local) -> mciSendCommandW, timeKillEvent | no |
| 15 | `auxGetDevCapsA` | 7-byte `jmp [IAT]` thunk | winmmbase!auxGetDevCapsA (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 16 | `auxGetDevCapsW` | 7-byte `jmp [IAT]` thunk | winmmbase!auxGetDevCapsW (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 17 | `auxGetNumDevs` | 7-byte `jmp [IAT]` thunk | winmmbase!auxGetNumDevs (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 18 | `auxGetVolume` | 7-byte `jmp [IAT]` thunk | winmmbase!auxGetVolume (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 19 | `auxOutMessage` | 7-byte `jmp [IAT]` thunk | winmmbase!auxOutMessage (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 20 | `auxSetVolume` | 7-byte `jmp [IAT]` thunk | winmmbase!auxSetVolume (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 21 | `joyConfigChanged` | local, 94 bytes | winmm (local) -> joyCloseAll | yes (joystickapi) |
| 22 | `joyGetDevCapsA` | local, 683 bytes | winmm (local) -> joyGetDevCapsW | yes (joystickapi) |
| 23 | `joyGetDevCapsW` | local, 262 bytes | winmm (local) -> joyOpen | yes (joystickapi) |
| 24 | `joyGetNumDevs` | local, 6 bytes | constant: `mov eax,0x10; ret` (16) | yes (joystickapi) |
| 25 | `joyGetPos` | local, 82 bytes | winmm (local) -> joyGetPosEx | yes (joystickapi) |
| 26 | `joyGetPosEx` | local, 1309 bytes | winmm (local) -> joyOpen | yes (joystickapi) |
| 27 | `joyGetThreshold` | local, 197 bytes | winmm (local) -> joyOpen | yes (joystickapi) |
| 28 | `joyReleaseCapture` | local, 172 bytes | winmm (local) | yes (joystickapi) |
| 29 | `joySetCapture` | local, 609 bytes | winmm (local) -> IsIsWindowPresent, IsSetTimerPresent, joyClose | yes (joystickapi) |
| 30 | `joySetThreshold` | local, 205 bytes | winmm (local) -> joyOpen | yes (joystickapi) |
| 31 | `mciDriverNotify` | local, 18 bytes | user32!PostMessageW(hwnd, MM_MCINOTIFY 0x3b9, status, devid) | yes, mmddk.h (legacy DDK, driver side) |
| 32 | `mciDriverYield` | local, 174 bytes | winmm (local) | yes, mmddk.h (legacy DDK, driver side) |
| 33 | `mciFreeCommandResource` | local, 432 bytes | winmm (local) | yes, mmddk.h (legacy DDK, driver side) |
| 34 | `mciGetCreatorTask` | local, 109 bytes | winmm (local) | yes, mmsystem.h (Learn, MCI) |
| 35 | `mciGetDeviceIDA` | local, 84 bytes | winmm (local) -> AllocUnicodeStr, mciGetDeviceIDInternal | yes, mmsystem.h (Learn, MCI) |
| 36 | `mciGetDeviceIDFromElementIDA` | local, 77 bytes | winmm (local) -> AllocUnicodeStr, mciGetDeviceIDFromElementIDW | yes, mmsystem.h (Learn, MCI) |
| 37 | `mciGetDeviceIDFromElementIDW` | local, 266 bytes | winmm (local) | yes, mmsystem.h (Learn, MCI) |
| 38 | `mciGetDeviceIDW` | local, 36 bytes | winmm (local) -> mciGetDeviceIDInternal | yes, mmsystem.h (Learn, MCI) |
| 39 | `mciGetDriverData` | local, 166 bytes | winmm (local) | yes, mmddk.h (legacy DDK, driver side) |
| 40 | `mciGetErrorStringA` | local, 286 bytes | winmm (local) | yes, mmsystem.h (Learn, MCI) |
| 41 | `mciGetErrorStringW` | local, 287 bytes | winmm (local) | yes, mmsystem.h (Learn, MCI) |
| 42 | `mciGetYieldProc` | local, 146 bytes | winmm (local) | yes, mmsystem.h (Learn, MCI) |
| 43 | `mciLoadCommandResource` | local, 1049 bytes | winmm (local) -> StringCchCopyW, mciEatCommandEntry, mciInitDeviceList | yes, mmddk.h (legacy DDK, driver side) |
| 44 | `mciSendCommandA` | local, 2194 bytes | winmm (local) -> AllocUnicodeStr, FindCommandItem, StringCbCopyA | yes, mmsystem.h (Learn, MCI) |
| 45 | `mciSendCommandW` | local, 297 bytes | winmm (local) -> mciInitDeviceList, mciSendCommandInternal | yes, mmsystem.h (Learn, MCI) |
| 46 | `mciSendStringA` | local, 542 bytes | winmm (local) -> AllocUnicodeStr, UnicodeStrToAsciiStr, mciSendStringW | yes, mmsystem.h (Learn, MCI) |
| 47 | `mciSendStringW` | local, 108 bytes | winmm (local) -> mciInitDeviceList, mciSendStringInternal | yes, mmsystem.h (Learn, MCI) |
| 48 | `mciSetDriverData` | local, 171 bytes | winmm (local) | yes, mmddk.h (legacy DDK, driver side) |
| 49 | `mciSetYieldProc` | local, 131 bytes | winmm (local) | yes, mmsystem.h (Learn, MCI) |
| 50 | `midiConnect` | 7-byte `jmp [IAT]` thunk | winmmbase!midiConnect (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 51 | `midiDisconnect` | 7-byte `jmp [IAT]` thunk | winmmbase!midiDisconnect (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 52 | `midiInAddBuffer` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInAddBuffer (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 53 | `midiInClose` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInClose (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 54 | `midiInGetDevCapsA` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInGetDevCapsA (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 55 | `midiInGetDevCapsW` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInGetDevCapsW (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 56 | `midiInGetErrorTextA` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInGetErrorTextA (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 57 | `midiInGetErrorTextW` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInGetErrorTextW (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 58 | `midiInGetID` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInGetID (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 59 | `midiInGetNumDevs` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInGetNumDevs (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 60 | `midiInMessage` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInMessage (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 61 | `midiInOpen` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInOpen (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 62 | `midiInPrepareHeader` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInPrepareHeader (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 63 | `midiInReset` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInReset (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 64 | `midiInStart` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInStart (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 65 | `midiInStop` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInStop (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 66 | `midiInUnprepareHeader` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInUnprepareHeader (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 67 | `midiOutCacheDrumPatches` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutCacheDrumPatches (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 68 | `midiOutCachePatches` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutCachePatches (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 69 | `midiOutClose` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutClose (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 70 | `midiOutGetDevCapsA` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutGetDevCapsA (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 71 | `midiOutGetDevCapsW` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutGetDevCapsW (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 72 | `midiOutGetErrorTextA` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInGetErrorTextA (via api-ms-win-mm-mme-l1-1-0) **(shared body)** | yes (mmeapi) |
| 73 | `midiOutGetErrorTextW` | 7-byte `jmp [IAT]` thunk | winmmbase!midiInGetErrorTextW (via api-ms-win-mm-mme-l1-1-0) **(shared body)** | yes (mmeapi) |
| 74 | `midiOutGetID` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutGetID (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 75 | `midiOutGetNumDevs` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutGetNumDevs (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 76 | `midiOutGetVolume` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutGetVolume (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 77 | `midiOutLongMsg` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutLongMsg (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 78 | `midiOutMessage` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutMessage (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 79 | `midiOutOpen` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutOpen (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 80 | `midiOutPrepareHeader` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutPrepareHeader (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 81 | `midiOutReset` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutReset (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 82 | `midiOutSetVolume` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutSetVolume (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 83 | `midiOutShortMsg` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutShortMsg (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 84 | `midiOutUnprepareHeader` | 7-byte `jmp [IAT]` thunk | winmmbase!midiOutUnprepareHeader (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 85 | `midiStreamClose` | 7-byte `jmp [IAT]` thunk | winmmbase!midiStreamClose (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 86 | `midiStreamOpen` | 7-byte `jmp [IAT]` thunk | winmmbase!midiStreamOpen (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 87 | `midiStreamOut` | 7-byte `jmp [IAT]` thunk | winmmbase!midiStreamOut (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 88 | `midiStreamPause` | 7-byte `jmp [IAT]` thunk | winmmbase!midiStreamPause (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 89 | `midiStreamPosition` | 7-byte `jmp [IAT]` thunk | winmmbase!midiStreamPosition (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 90 | `midiStreamProperty` | 7-byte `jmp [IAT]` thunk | winmmbase!midiStreamProperty (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 91 | `midiStreamRestart` | 7-byte `jmp [IAT]` thunk | winmmbase!midiStreamRestart (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 92 | `midiStreamStop` | 7-byte `jmp [IAT]` thunk | winmmbase!midiStreamStop (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 93 | `mixerClose` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerClose (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 94 | `mixerGetControlDetailsA` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerGetControlDetailsA (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 95 | `mixerGetControlDetailsW` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerGetControlDetailsW (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 96 | `mixerGetDevCapsA` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerGetDevCapsA (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 97 | `mixerGetDevCapsW` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerGetDevCapsW (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 98 | `mixerGetID` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerGetID (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 99 | `mixerGetLineControlsA` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerGetLineControlsA (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 100 | `mixerGetLineControlsW` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerGetLineControlsW (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 101 | `mixerGetLineInfoA` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerGetLineInfoA (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 102 | `mixerGetLineInfoW` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerGetLineInfoW (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 103 | `mixerGetNumDevs` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerGetNumDevs (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 104 | `mixerMessage` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerMessage (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 105 | `mixerOpen` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerOpen (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 106 | `mixerSetControlDetails` | 7-byte `jmp [IAT]` thunk | winmmbase!mixerSetControlDetails (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 107 | `mmDrvInstall` | 7-byte `jmp [IAT]` thunk | winmmbase!mmDrvInstall (via api-ms-win-mm-misc-l1-1-0) | no |
| 108 | `mmGetCurrentTask` | 7-byte `jmp [IAT]` thunk | winmmbase!mmGetCurrentTask (via WINMMBASE) | yes (mmddk) |
| 109 | `mmTaskBlock` | 7-byte `jmp [IAT]` thunk | winmmbase!mmTaskBlock (via WINMMBASE) | yes (mmddk) |
| 110 | `mmTaskCreate` | 7-byte `jmp [IAT]` thunk | winmmbase!mmTaskCreate (via WINMMBASE) | yes (mmddk) |
| 111 | `mmTaskSignal` | 7-byte `jmp [IAT]` thunk | winmmbase!mmTaskSignal (via WINMMBASE) | yes (mmddk) |
| 112 | `mmTaskYield` | 7-byte `jmp [IAT]` thunk | winmmbase!mmTaskYield (via WINMMBASE) | yes (mmddk) |
| 113 | `mmioAdvance` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioAdvance (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 114 | `mmioAscend` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioAscend (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 115 | `mmioClose` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioClose (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 116 | `mmioCreateChunk` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioCreateChunk (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 117 | `mmioDescend` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioDescend (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 118 | `mmioFlush` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioFlush (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 119 | `mmioGetInfo` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioGetInfo (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 120 | `mmioInstallIOProcA` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioInstallIOProcA (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 121 | `mmioInstallIOProcW` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioInstallIOProcW (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 122 | `mmioOpenA` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioOpenA (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 123 | `mmioOpenW` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioOpenW (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 124 | `mmioRead` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioRead (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 125 | `mmioRenameA` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioRenameA (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 126 | `mmioRenameW` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioRenameW (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 127 | `mmioSeek` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioSeek (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 128 | `mmioSendMessage` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioSendMessage (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 129 | `mmioSetBuffer` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioSetBuffer (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 130 | `mmioSetInfo` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioSetInfo (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 131 | `mmioStringToFOURCCA` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioStringToFOURCCA (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 132 | `mmioStringToFOURCCW` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioStringToFOURCCW (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 133 | `mmioWrite` | 7-byte `jmp [IAT]` thunk | winmmbase!mmioWrite (via api-ms-win-mm-misc-l1-1-0) | yes (mmiscapi) |
| 134 | `mmsystemGetVersion` | local, 6 bytes | constant: `mov eax,0x30a; ret` | header only (mmsystem.h), no doc page |
| 135 | `sndPlaySoundA` | local, 10 bytes | winmm!PlaySoundA(psz, NULL, fuSound) (tail jmp) | yes, mmsystem.h (Learn) |
| 136 | `sndPlaySoundW` | local, 10 bytes | winmm!sndPlaySoundI(psz, NULL, fuSound) (tail jmp) | yes, mmsystem.h (Learn) |
| 137 | `timeBeginPeriod` | 7-byte `jmp [IAT]` thunk | kernel32!timeBeginPeriod (via api-ms-win-mm-time-l1-1-0) | yes (timeapi) |
| 138 | `timeEndPeriod` | 7-byte `jmp [IAT]` thunk | kernel32!timeEndPeriod (via api-ms-win-mm-time-l1-1-0) | yes (timeapi) |
| 139 | `timeGetDevCaps` | 7-byte `jmp [IAT]` thunk | kernel32!timeGetDevCaps (via api-ms-win-mm-time-l1-1-0) | yes (timeapi) |
| 140 | `timeGetSystemTime` | 7-byte `jmp [IAT]` thunk | kernel32!timeGetSystemTime (via api-ms-win-mm-time-l1-1-0) | yes (timeapi) |
| 141 | `timeGetTime` | 7-byte `jmp [IAT]` thunk | kernel32!timeGetTime (via api-ms-win-mm-time-l1-1-0) | yes (timeapi) |
| 142 | `timeKillEvent` | local, 348 bytes | winmm (local) | yes, timeapi/mmsystem.h (Learn, obsolete) |
| 143 | `timeSetEvent` | local, 393 bytes | winmm (local) -> FUN_1800094e8, TimeInitThread, timeThreadSetEvent | yes, timeapi/mmsystem.h (Learn, obsolete) |
| 144 | `waveInAddBuffer` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInAddBuffer (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 145 | `waveInClose` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInClose (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 146 | `waveInGetDevCapsA` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInGetDevCapsA (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 147 | `waveInGetDevCapsW` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInGetDevCapsW (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 148 | `waveInGetErrorTextA` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInGetErrorTextA (via api-ms-win-mm-mme-l1-1-0) | yes, mmeapi |
| 149 | `waveInGetErrorTextW` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInGetErrorTextW (via api-ms-win-mm-mme-l1-1-0) | yes, mmeapi |
| 150 | `waveInGetID` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInGetID (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 151 | `waveInGetNumDevs` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInGetNumDevs (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 152 | `waveInGetPosition` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInGetPosition (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 153 | `waveInMessage` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInMessage (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 154 | `waveInOpen` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInOpen (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 155 | `waveInPrepareHeader` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInPrepareHeader (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 156 | `waveInReset` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInReset (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 157 | `waveInStart` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInStart (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 158 | `waveInStop` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInStop (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 159 | `waveInUnprepareHeader` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInUnprepareHeader (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 160 | `waveOutBreakLoop` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutBreakLoop (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 161 | `waveOutClose` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutClose (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 162 | `waveOutGetDevCapsA` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutGetDevCapsA (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 163 | `waveOutGetDevCapsW` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutGetDevCapsW (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 164 | `waveOutGetErrorTextA` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInGetErrorTextA (via api-ms-win-mm-mme-l1-1-0) **(shared body)** | yes, mmeapi |
| 165 | `waveOutGetErrorTextW` | 7-byte `jmp [IAT]` thunk | winmmbase!waveInGetErrorTextW (via api-ms-win-mm-mme-l1-1-0) **(shared body)** | yes, mmeapi |
| 166 | `waveOutGetID` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutGetID (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 167 | `waveOutGetNumDevs` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutGetNumDevs (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 168 | `waveOutGetPitch` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutGetPitch (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 169 | `waveOutGetPlaybackRate` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutGetPlaybackRate (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 170 | `waveOutGetPosition` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutGetPosition (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 171 | `waveOutGetVolume` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutGetVolume (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 172 | `waveOutMessage` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutMessage (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 173 | `waveOutOpen` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutOpen (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 174 | `waveOutPause` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutPause (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 175 | `waveOutPrepareHeader` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutPrepareHeader (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 176 | `waveOutReset` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutReset (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 177 | `waveOutRestart` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutRestart (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 178 | `waveOutSetPitch` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutSetPitch (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 179 | `waveOutSetPlaybackRate` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutSetPlaybackRate (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 180 | `waveOutSetVolume` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutSetVolume (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 181 | `waveOutUnprepareHeader` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutUnprepareHeader (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
| 182 | `waveOutWrite` | 7-byte `jmp [IAT]` thunk | winmmbase!waveOutWrite (via api-ms-win-mm-mme-l1-1-0) | yes (mmeapi) |
