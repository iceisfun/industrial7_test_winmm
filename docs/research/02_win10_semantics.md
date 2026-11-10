# 02 - Windows 10 winmm semantics (from the decomp)

Source of truth: the Win10 22H2 reference corpus (`winmm.dll` 10.0.19041.546,
`winmmbase.dll` 10.0.19041.1, `kernel32.dll` time functions, `mmdevapi.dll`).
Every statement below cites the function and branch it was read from.
Lines marked **[inference]** are not directly visible in the decomp (usually
because the behaviour lives in `wdmaud.drv`, which is NOT in the corpus) and
must be confirmed on the Win10 VM before a test hard-CHECKs them. Lines marked
**[VM]** are what a test should observe and record (t_note) before any CHECK.

Numeric codes used throughout:

| code | name | code | name |
|---|---|---|---|
| 0 | MMSYSERR_NOERROR | 32 | WAVERR_BADFORMAT |
| 1 | MMSYSERR_ERROR | 33 | WAVERR_STILLPLAYING |
| 2 | MMSYSERR_BADDEVICEID | 34 | WAVERR_UNPREPARED |
| 4 | MMSYSERR_ALLOCATED | 35 | WAVERR_SYNC |
| 5 | MMSYSERR_INVALHANDLE | 64 | MIDIERR_UNPREPARED |
| 6 | MMSYSERR_NODRIVER | 65 | MIDIERR_STILLPLAYING |
| 7 | MMSYSERR_NOMEM | 68 | MIDIERR_NODEVICE |
| 8 | MMSYSERR_NOTSUPPORTED | 71 | MIDIERR_DONT_CONTINUE |
| 9 | MMSYSERR_BADERRNUM | 97 | TIMERR_NOCANDO |
| 10 | MMSYSERR_INVALFLAG | 129 | TIMERR_STRUCT |
| 11 | MMSYSERR_INVALPARAM | 165/166/167 | JOYERR_PARMS / NOCANDO / UNPLUGGED |
| 12 | MMSYSERR_HANDLEBUSY | 256+ | MMIOERR_* / MCIERR_* |
| 21 | MMSYSERR_MOREDATA | 1024.. | MIXERR_INVALLINE/CONTROL/VALUE |

## 0. Module layout: who implements what

* `winmm.dll` is mostly a forwarding shell. Its exports for `wave*`, `midi*`,
  `mixer*`, `aux*`, `mmio*`, `*Driver*`, `mmTask*` are 7-byte stubs
  (`waveOutOpen` at winmm!0xa820 is a single `jmp` through the delay-load IAT
  to `api-ms-win-mm-mme-l1-1-0` / `api-ms-win-mm-misc-l1-1-0`, i.e.
  `winmmbase.dll`). 136 of winmm's 180 exports are also winmmbase exports.
* Implemented inside `winmm.dll` itself (44 exports): `PlaySound[A|W]`,
  `sndPlaySound[A|W]`, all `mci*`, all `joy*`, `timeSetEvent`, `timeKillEvent`,
  `mmsystemGetVersion` (returns constant `0x030A`), `WOWAppExit`.
* `timeGetTime`, `timeGetSystemTime`, `timeGetDevCaps`, `timeBeginPeriod`,
  `timeEndPeriod` are imported from `api-ms-win-mm-time-l1-1-0` and resolve to
  **kernel32.dll** (kernel32!0x1c1a0 timeGetTime, !0x1e110 timeBeginPeriod,
  !0x1d190 timeEndPeriod, !0x22840 timeGetDevCaps, !0x6a040 timeGetSystemTime).
  winmm's exports for them are jump stubs.
* Tests therefore exercise winmmbase/kernel32 logic through winmm's export
  table; resolving via `U(waveOutOpen)` on winmm.dll is the right contract.

## 1. Device enumeration and the audio stack

`ClientUpdatePnpInfo` (winmmbase!0xedf0) runs at the top of nearly every
wave/midi/mixer/aux API (open, caps, GetNumDevs, message, prepare, write, ...).

1. First call in the process (`DAT_180020134` one-shot):
   `winmmWaitForService` -> `AudioSrvBinding` -> `InitDevices` -> `SetEvent(hEventApiInit)`.
   Later/racing callers block on `hEventApiInit` (INFINITE).
2. `winmmWaitForService`: opens the SCM, queries service **AudioSrv**.
   Running/paused states (4..7) -> ok. START_PENDING (2) -> poll every 2000 ms
   for up to 120 s. STOPPED with exit code 1077 (never started) and start type
   AUTO -> keeps polling; any other stopped state -> give up immediately.
   **Consequence for tests:** on a VM where AudioSrv is still starting the
   first winmm call can block for many seconds; run a cheap call (e.g.
   `waveOutGetNumDevs`) first and do not time it.
3. `AudioSrvBinding`: composes an RPC string binding to audiosrv
   (`RpcStringBindingComposeW` / `RpcBindingFromStringBindingW`).
4. `InitDevices`: resets the device lists (`waveoutdrvZ`, `waveindrvZ`,
   `midioutdrvZ`, `midiindrvZ`, `auxdrvZ`, `mixerdrvZ`) and loads legacy
   drivers named in the `Drivers32` profile section: `wave`, `wave1`..`wave9`,
   `midi`, `midi1`..`midi9`, then aux/mixer the same way, via `OpenDriver` +
   `mmDrvInstall` (one install per direction: 0x4002 waveOut, 0x4001 waveIn,
   0x4004 midiOut, 0x4003 midiIn). On a stock Win10 the `wave`/`midi`/`mixer`/
   `aux` entries all name **wdmaud.drv** **[inference: registry content]**.
5. Every subsequent call: if `winmmGetNewPnpEvents()` reports a change,
   `winmmGetEndpointsWorker` asks **mmdevapi** (`MMDeviceGetDeviceEnumerator`)
   for the render/capture endpoints and `winmmGetMidiInterfaces` enumerates
   MIDI interfaces (mmdevapi + devobj + `CM_Get_DevNode_Status`);
   `ClientPnpChange` -> `wdmPnpUpdateDriver` rebuilds per-driver device counts;
   then `InvalidatePreferredDevices` / `RefreshPreferredDevices` and
   `KickMapper(1..7)` re-sync the mappers.
6. Device ids are flat indices across all drivers in a list
   (`waveReferenceDriverById`: walks the circular list, subtracting each
   driver's device count; drivers with flag bit 2 = "mapper" are skipped).
   Out-of-range id -> MMSYSERR_BADDEVICEID (2).
7. The wave mapper is a separate driver loaded lazily by `WaveMapperInit`
   (Drivers32 `wavemapper`, i.e. msacm32.drv) **only if
   `wTotalWaveInDevs + wTotalWaveOutDevs != 0`**. MIDI mapper likewise via
   `MidiMapperInit` (midimap.dll).

So the legacy MME path on Win10 is:
`app -> winmm stub -> winmmbase -> wdmaud.drv (driver proc, wodMessage etc.)
-> mmdevapi/audioses (WASAPI shared-mode stream) -> audiosrv/audiodg`. winmmbase
itself never touches IAudioClient; format support, WOM_* callbacks and the
buffer pump all live in wdmaud.drv **[inference: wdmaud.drv not in corpus]**.

### Zero devices

* `waveOutGetNumDevs`/`waveInGetNumDevs`/`midiOutGetNumDevs`/`midiInGetNumDevs`/
  `mixerGetNumDevs`/`auxGetNumDevs` just return the cached counters after
  `ClientUpdatePnpInfo` (no error path). A headless VM without an audio
  endpoint reports 0 wave devices **[VM]**. MIDI out may still be 1 (Microsoft
  GS Wavetable Synth is a software device) **[VM: record, do not assume]**.
* `waveOutOpen`/`waveInOpen`: `wTotalWaveOutDevs == 0` -> **2 (BADDEVICEID)**
  for every id, including WAVE_MAPPER and WAVE_FORMAT_QUERY (checked after the
  parameter validation of section 2.1, so parameter errors still win).
* Caps/volume/message by id with 0 devices: `waveIDMessage` id >= count and id
  != -1 -> 2; id == -1 (mapper) -> `waveReferenceDriverById(-1)` on an empty
  list -> 2. So `waveOutGetDevCaps(WAVE_MAPPER)` with 0 devices -> 2 **[VM]**.
* `joyGetNumDevs` returns the constant **16** regardless of hardware.

## 2. waveOut / waveIn

### 2.1 waveOutOpen (winmmbase!0x103b0); waveInOpen (!0x11290) is identical with the waveIn lists and WIDM_OPEN (0x34)

Exact validation order:

1. `pwfx == NULL` -> **11**.
2. `dwCallback != 0 && (fdwOpen>>16 & 7) == 1` (CALLBACK_WINDOW) and
   `!IsWindow(dwCallback)` -> **11**. (A NULL window with CALLBACK_WINDOW is
   accepted: no callback. No other callback type is validated here.)
3. `uDeviceID == 0xFFFF` is rewritten to `0xFFFFFFFF` (16-bit WAVE_MAPPER
   compatibility; both mean mapper).
4. Flag check: mapper -> `fdwOpen & 0x7E04`, specific device ->
   `fdwOpen & 0x7E10`; nonzero -> **10 (INVALFLAG)**. I.e.
   WAVE_MAPPED (0x4) is invalid with WAVE_MAPPER;
   WAVE_MAPPED_DEFAULT_COMMUNICATION_DEVICE (0x10) is invalid with a specific
   id; bits 0x0200..0x4000 are always invalid. WAVE_FORMAT_QUERY (1),
   WAVE_ALLOWSYNC (2), WAVE_FORMAT_DIRECT (8), 0x20/0x40/0x80/0x100/0x8000 pass
   this layer. The CALLBACK_TYPE field (0x70000) is not range-checked here
   **[inference: wdmaud rejects unknown types; VM]**.
5. (pointer-wrap check on `pwfx` for non-PCM with cbSize; unreachable for
   valid pointers.)
6. If not WAVE_FORMAT_QUERY: `phwo == NULL` -> **11**, else `*phwo = NULL`
   (the out handle is zeroed BEFORE the device check, so a failed open leaves
   NULL). With WAVE_FORMAT_QUERY `phwo` is ignored and may be NULL.
7. `ClientUpdatePnpInfo`; zero devices -> **2**.
8. Resolve the driver (WAVE_MAPPED forces the mapper driver): failure -> **2**.
9. Mapper requested but no mapper driver installed -> try every device id
   0..N-1 recursively, returning the first success, else the last error
   (initial value 4). With WAVE_MAPPED: ask the preferred-device logic
   (internal message 0x805) and reopen through it.
10. Otherwise allocate a handle (`NewHandle(type 1)`; failure -> **7**), mark
    it "busy" (flag 2), send **WODM_OPEN (5)** (WIDM_OPEN 0x34 for input) to
    the driver with a `WAVEOPENDESC {hWave, lpFormat, dwCallback, dwInstance,
    uMappedDeviceID, dnDevNode}` and `fdwOpen`. Success -> clear busy, store
    handle in `*phwo`. Failure -> free the handle and return the driver code.

Format validation (WAVEFORMATEX fields, nBlockAlign/nAvgBytesPerSec
consistency, WAVE_FORMAT_EXTENSIBLE cbSize >= 22, SubFormat) is entirely the
driver's: WAVERR_BADFORMAT (32) for unsupported formats with QUERY and with a
real open **[inference/VM]**. Through the mapper, msacm can convert PCM
formats the endpoint does not natively take (so mapper accepts more than a
direct id; WAVE_FORMAT_DIRECT disables ACM) **[VM]**.

### 2.2 Handle validation (`ValidateHandle`, winmmbase)

A handle is valid only if: value > 0xFFFF, not -1, not 0xFFFFFFFF, within the
user VA range, its header (handle-0x50) is on the global `pHandleList`, and
its stored type matches the expected type (1 waveOut, 2 waveIn, 3 midiOut,
4 midiIn, 5 mmio, 8 mixer, 9 midiStream). Consequences:

* Any small integer, NULL, -1, a closed handle (freed from the list), or a
  handle of another kind -> **5 (INVALHANDLE)**, never a crash. Garbage
  pointers are looked up in a list, not dereferenced; still, do not test
  arbitrary pointers (invalid-test policy) - use NULL, small ints, closed
  handles, and cross-type handles (an HWAVEIN passed to waveOut*).
* Handle state flags (handle-0x44): bit 1 "device gone" -> most calls return
  **6 (NODRIVER)** (`waveMessage`); bit 2 "busy/closing" -> **12
  (HANDLEBUSY)**.

### 2.3 Header protocol (WAVEHDR, flags DONE 1, PREPARED 2, BEGINLOOP 4, ENDLOOP 8, INQUEUE 0x10)

Common front check in Prepare/Unprepare/Write/AddBuffer:
`pwh == NULL || cbwh < 0x30 || (pwh->dwFlags & 0xFFFFCFE0)` -> **11**. So
sizeof(WAVEHDR)=48 on x64 is the minimum; any undefined flag bit (other than
0x1F and the internal 0x1000/0x2000) is INVALPARAM. These checks come
**before** handle validation.

* `waveOutPrepareHeader` (!0x10910): already PREPARED -> **0 without touching
  the driver or validating the handle** (so even an invalid handle returns 0
  here). Else handle check (5), then `dwFlags &= 0xC` (keeps BEGIN/ENDLOOP,
  clears DONE/INQUEUE), send WODM_PREPARE (7); a driver answer of 8
  (NOTSUPPORTED) is treated as success; on success sets PREPARED.
* `waveOutUnprepareHeader` (!0x109b0): INQUEUE -> **33 (STILLPLAYING)**;
  not PREPARED -> **0** (no handle check); else handle check (5), WODM_UNPREPARE
  (8); 8 -> success; 6 on a "device gone" handle -> 0; success clears PREPARED.
* `waveOutWrite` (!0x10a70): not PREPARED -> **34 (UNPREPARED)**; INQUEUE ->
  **33**; handle check (5); clears DONE, sends WODM_WRITE (9). The driver sets
  INQUEUE, and on completion clears INQUEUE, sets DONE and delivers WOM_DONE
  **[inference: wdmaud; VM]**.
* `waveInAddBuffer` (!0x118c0): same ladder (34 / 33 / 5), WIDM_ADDBUFFER 0x38.
* Writing a zero-length buffer: not rejected by winmmbase; driver behaviour
  **[VM]**.

### 2.4 Other waveOut entry points

All validate the handle (type 1) after `ClientUpdatePnpInfo` and forward:

| API | check before forward | driver msg |
|---|---|---|
| waveOutClose (!0x10800) | handle (5); busy -> 12 | WODM_CLOSE 6; nonzero driver result (e.g. 33 while buffers queued) is returned and the handle stays open |
| waveOutPause / Restart / BreakLoop | handle (5) | 10 / 11 / 0x14 |
| waveOutReset (!0x10bb0) | handle (5) | 0x0C; 6 on gone handle -> 0 |
| waveOutGetPosition (!0x10c60) | `pmmt == NULL` -> 11 (before the handle check), then handle | 0x0D with cbmmt (driver validates cbmmt and may change wType to a supported format) |
| waveOutGetPitch / GetPlaybackRate | NULL out ptr -> 11 first | 0x0E / 0x12 |
| waveOutSetPitch / SetPlaybackRate | handle | 0x0F / 0x13 (wdmaud answers 8 NOTSUPPORTED **[VM]**) |
| waveOutGetVolume (!0x10150) | `pdwVolume == NULL` -> 11; accepts a handle OR a device id (0xFFFF -> mapper) | 0x10 |
| waveOutSetVolume (!0x10210) | handle or id | 0x11 |
| waveOutGetID (!0x11c00) | `puDeviceID == NULL` -> 11 first, then handle (5); returns the id stored at open (no driver call). For a mapper-opened handle it is the id the open resolved **[VM]** |
| waveOutMessage (!0xfd70) | if not a valid handle, the value is treated as a device id (`waveIDMessage`): id >= count -> 2 | any |

Close semantics: `waveOutClose` with queued buffers returns whatever the
driver says (classic contract: WAVERR_STILLPLAYING 33) **[VM]**; after Reset
all buffers come back with DONE set and INQUEUE cleared, then Close succeeds.

### 2.5 Caps

`waveOutGetDevCapsW` (!0xfe00): **`cbwoc == 0` -> 0 (nothing written,
pointer not checked)**; then `pwoc == NULL` -> 11; then id 0xFFFF -> mapper;
then a valid HWAVEOUT is accepted in place of an id (`ValidateHandle` ->
`waveMessage`), otherwise `waveIDMessage` (out of range -> 2). The driver
receives (pwoc, cbwoc) and copies at most cbwoc bytes **[inference]**.
`waveOutGetDevCapsA` (!0xffa0): same order; asks for a full 0x84-byte
WAVEOUTCAPS2W internally, converts szPname with `Iwcstombs(...,0x20)`, and
copies `min(cbwoc, 100)` bytes (100 = sizeof(WAVEOUTCAPS2A)), so a short
cbwoc truncates the structure without error.

### 2.6 Error text

`waveOutGetErrorTextW/A` (shared with waveIn; !0x10320 / !0x10340):
`cchText == 0` -> **0**, buffer untouched; `pszText == NULL` -> **11**;
otherwise `pszText[0] = 0`; codes 0..21 and 32..35 are valid, anything else
-> **9 (BADERRNUM)** (with empty string). `cchText == 1` -> **0** with an
empty string (LoadString is skipped when cch < 2). Text comes from winmmbase
string resources (`LoadString(ghInst, code)`), so it is locale dependent: do
not CHECK exact text, CHECK non-empty and NUL-terminated within cch.
`midiOutGetErrorText` (!0x7d40, `midiGetErrorTextW`): valid 0..21 and 64..71.

### 2.7 Callbacks and message sequence

`DriverCallback` (winmmbase!0x5560) dispatches on the low 3 bits of the
callback flags (CALLBACK_* >> 16): 1 WINDOW -> `PostMessageA(hwnd, msg,
hdev, dwParam1)`; 2 THREAD -> `PostThreadMessageA(tid, msg, hdev, dwParam1)`;
3 FUNCTION -> `cb(hdev, msg, dwInstance, dwParam1, dwParam2)` called
synchronously on the driver's thread; 5 EVENT -> `SetEvent((HANDLE)cb)`; a
NULL callback target -> nothing. Any other type -> no delivery.
Sequence for waveOut **[inference: wdmaud; VM]**: WOM_OPEN (0x3BB) during
waveOutOpen (before it returns), one WOM_DONE (0x3BD, dwParam1 = LPWAVEHDR) per
buffer in submission order, WOM_CLOSE (0x3BC) during waveOutClose. waveIn:
WIM_OPEN 0x3BE, WIM_DATA 0x3C0, WIM_CLOSE 0x3BF. With CALLBACK_EVENT the event
is signalled for open, each done, and close. A WAVE_FORMAT_QUERY open never
creates a handle and sends no callback.

## 3. MIDI

### midiOutOpen (!0x7dd0)
1. `phmo == NULL` -> 11 (checked FIRST, unlike wave).
2. Flags: MIDI_MAPPER (0xFFFFFFFF) -> `fdwOpen & 0xBFF3`, other ids ->
   `& 0x3FF3`; nonzero -> 10. (Only the low 16 bits are checked; 0xFFFF is NOT
   translated to the mapper for MIDI.)
3. CALLBACK_WINDOW with a non-window -> 11.
4. `*phmo = NULL`; `ClientUpdatePnpInfo`; `midiReferenceDriverById` failure
   (bad id, or empty list for the mapper) -> 2.
5. Mapper with no mapper driver: 0 devices -> **6**, else try each device.
   A device whose driver has no entry proc -> 6.
6. `NewHandle(type 3)` failure -> 7; MODM_OPEN (3); success stores handle.
The mapper is midimap.dll (`MidiMapperInit`) which routes to the default
synth, normally "Microsoft GS Wavetable Synth" **[VM]**.

### Messages and headers
* `midiOutShortMsg` (!0x8570): handle must be midiOut (3) or midiStream (9),
  else 5. No validation of the message value. On a stream handle it is
  forwarded under the stream lock.
* `midiOutLongMsg` (!0x8620): header checks FIRST: `pmh == NULL || cbmh < 44
  || dwFlags & 0xFFFFCFD0` -> 11; not PREPARED -> **64**; INQUEUE (4) -> **65**;
  `dwBufferLength == 0` -> 11; then the handle (5); on a stream handle -> 8.
* `midiOutPrepareHeader` (!0x8110): handle FIRST (5), then `ValidateHeader`
  (11), already PREPARED -> 0; `dwFlags` reset to 0 before MODM_PREPARE (5);
  driver 8 -> treated as success. Stream handles: `dwBufferLength > 0x10000`
  -> 11; a shadow header per stream device is allocated.
* `midiOutClose` (!0x8000): like waveOutClose (MODM_CLOSE 4).
* Error-text range 64..71; caps ladder same as wave (cbmoc 0 -> 0, NULL -> 11).

### midiStream (!0x60f0 midiStreamOpen)
1. `phms == NULL` -> 11; CALLBACK_WINDOW non-window -> 11;
   `puDeviceID == NULL && cMidi != 0` -> 11; then `*phms = NULL`;
   `cMidi != 1` -> 11. (So cMidi 0 and cMidi 2 are both INVALPARAM.)
2. An id of 0xFFFFFFFE -> 2; `midiOutGetDevCapsA` on the id fails -> that
   error and the id slot is overwritten with 0xFFFFFFFE.
3. Devices without MIDICAPS_STREAM (8) are driven through the in-winmm stream
   emulator (`mseMessage`), so midiStream works on any midiOut device.
* `midiStreamOut` (!0x6c40): handle (type 9) FIRST (5); then header checks
  (11); not PREPARED -> 64; INQUEUE -> 65; `dwBytesRecorded > dwBufferLength`
  or not a multiple of 4 -> 11; `dwBufferLength >= 0x10000` or not a multiple
  of 4 -> 11. If no device accepted it -> 0x44 (68 NODEVICE).
* `midiStreamProperty` (!0x6940): handle (5); `(dwProperty & 0xC0000000) == 0`
  (neither MIDIPROP_GET nor MIDIPROP_SET) or NULL data -> 11.
* `midiStreamPosition` (!0x69b0): handle (5); NULL -> 11.
* midiStreamPause/Restart/Stop/Close: handle checks then broadcast.
* Stream callbacks: MOM_DONE (0x3C9) via `DriverCallback` when a buffer
  completes; MOM_POSITIONCB for MEVT_F_CALLBACK events.

### midiIn (!0x8d90 midiInOpen)
`phmi == NULL` -> 11 (falls through to the final return); flags mask
`0xBFDA` (mapper) / `0x3FDA` -> 10 (so MIDI_IO_STATUS 0x20 is accepted);
CALLBACK_WINDOW non-window -> 7 (note: NOMEM, the preloaded value, not 11
**[VM]**); bad id -> 2; no driver entry -> 6; MIDM_OPEN 0x37.

## 4. Mixer (!0xa400 mixerOpen and friends)

* `mixerOpen`: `phmx == NULL` -> 11; `*phmx = NULL` after
  `ClientUpdatePnpInfo`; CALLBACK_FUNCTION (type 3) -> **10** (not allowed
  for mixer); CALLBACK_WINDOW non-window -> 11; `fdwOpen & 0x0FF8FFFF` -> 10
  (only the CALLBACK_* field and the MIXER_OBJECTF_* high nibble pass);
  `IMixerGetID` maps a wave/midi/aux id or handle to a mixer id according to
  MIXER_OBJECTF_* (error -> returned); `mixerReferenceDriverById` (bad id ->
  error). Multiple opens of the same mixer share one driver instance (the
  `gpMixerDevHeader` list).
* `IMixerGetID` (used by mixerGetID): flag bits other than the top nibble ->
  10; `puMxId == NULL` -> 11; writes -1 first, then maps from endpoint or by
  matching caps.
* `mixerGetLineInfoW` (!0xb2a0): `fdwInfo & 0x0FFFFFF0` -> 10 FIRST; NULL ->
  11; `cbStruct < 0x11C` (sizeof MIXERLINEW = 284, packed) -> 11; query type
  0 DESTINATION, 1 SOURCE, 2 LINEID, 3 COMPONENTTYPE (componentType must be in
  0..8 or 0x1000..0x100A else 11), 4 TARGETTYPE (Target.dwType 0..5 else 11),
  other -> 10. Driver errors (MIXERR_INVALLINE 1024) propagate. On success
  winmm normalizes: cbStruct forced to 0x11C, `fdwLine` masked to
  0x80008001, out-of-range componentType -> 0, cChannels 0 -> 1, SOURCE lines
  get MIXERLINE_LINEF_SOURCE and cConnections 0, names NUL-terminated,
  Target.dwDeviceID resolved via `IMixerLineFindTarget`.
* `mixerGetControlDetailsW` (!0xb920): flags `& 0x0FFFFFF0` -> 10; NULL -> 11;
  `cbStruct < 0x20` -> 11; VALUE query (0): cChannels == 0 requires
  cMultipleItems == 0 and cbDetails != 0, else `cbDetails < 4` -> 11; LISTTEXT
  (1): cChannels 0 -> 11, cMultipleItems < 2 -> 11, cbDetails < 0x88 -> 11;
  other query -> 10; `paDetails == NULL` -> 11; then id/handle resolution.
  `mixerSetControlDetails` is the same shape. **Safety:** Set only after Get,
  writing back the identical value, and restore on exit.
* `mixerGetDevCapsW` (!0x9c70): cb 0 -> 0, NULL -> 11, accepts id or HMIXER.
* `mixerClose`: handle type 8 (5 otherwise); the last close of a shared
  instance sends MXDM_CLOSE.
* Callbacks: MM_MIXM_LINE_CHANGE (0x3D0) / MM_MIXM_CONTROL_CHANGE (0x3D1) to
  a window only.

## 5. aux

`auxGetNumDevs` returns the cached count. `auxGetDevCapsW` (!0x5930): cb 0 ->
0, NULL -> 11, then `auxOutMessage(id, AUXDM_GETDEVCAPS 4)`. `auxOutMessage`
(!0x5810): `auxReferenceDriverById` failure -> 2 (BADDEVICEID); driver without
entry -> 6. `auxGetVolume`: NULL -> 11 first. On Win10 wdmaud usually exposes
0 aux devices **[VM]**, so auxGetDevCaps(0) -> 2. Never call auxSetVolume in a
non-dx test.

## 6. Time

### kernel32 implementation (`TimeInit` one-shot)
`NtQueryTimerResolution(max, min, cur)`: `TDD_MAXRESOLUTION` (= reported
wPeriodMin) = round(min/10000) clamped to >= 1 (normally **1**); the upper
bound `uMax` = ceil(maxRes/10000) (normally **16** for the 15.625 ms tick;
0x37 = 55 if the query is odd).

* `timeGetDevCaps` (!0x22840): `cbtc < 8 || ptc == NULL` -> **97
  (TIMERR_NOCANDO)**; else wPeriodMin = TDD_MAXRESOLUTION, **wPeriodMax =
  1000000**. (Note: not TIMERR_STRUCT.)
* `timeBeginPeriod` (!0x1e110): `uPeriod < wPeriodMin` (so 0) -> **97**;
  `uPeriod >= uMax` (e.g. 16..) -> **0 without counting anything**; otherwise a
  per-period 16-bit reference count is incremented (count already 0xFFFF ->
  97); on the 0->1 transition, if the period is finer than the current
  process setting, `NtSetTimerResolution(uPeriod*10000)`; failure undoes the
  count and returns 97.
* `timeEndPeriod` (!0x1d190): `uPeriod < wPeriodMin` -> 97; `uPeriod >= uMax`
  -> **0**; count for that period already 0 -> **97** (unbalanced End is
  detectable); on the last release of the active period the resolution is
  recomputed from the next finest period still counted.
  Test contract: Begin(1)/End(1) -> 0/0; End(1) once more -> 97; Begin(0) ->
  97; Begin(1000) -> 0 and End(1000) -> 0 (no-op pair).
* `timeGetTime` (!0x1c1a0): `(ReadInterruptTick() - base)/10000 + tickBase`
  where base was captured together with `GetTickCount()` at init, so it starts
  in step with GetTickCount and has 1 ms granularity in units but advances
  with the interrupt time (resolution = current timer resolution). Monotonic,
  wraps at 2^32. Falls back to `GetTickCount` if init failed.
* `timeGetSystemTime` (!0x6a040): `cbmmt < 12 || NULL` -> **129
  (TIMERR_STRUCT)**; else `wType = TIME_MS`, `u.ms = timeGetTime()`.

### timeSetEvent / timeKillEvent (winmm!0x2a20 / !0x2e50, `timeThread`)
* `fuEvent & 0xFFFFFE0E` -> returns **0** (invalid). Valid bits: TIME_PERIODIC
  1, TIME_CALLBACK_EVENT_SET 0x10, TIME_CALLBACK_EVENT_PULSE 0x20, 0x40, 0x80,
  TIME_KILL_SYNCHRONOUS 0x100.
* `uDelay` must satisfy `wPeriodMin <= uDelay <= 1000000`, else **0**. So
  uDelay 0 fails.
* `uResolution` is clamped into [wPeriodMin, TimerData]; if larger than
  uDelay the default is used. The chosen period is passed to
  `timeBeginPeriod` (failure -> 0) and released by `timeEndPeriod` when the
  event dies.
* The fptc pointer is not validated; a NULL function callback is simply not
  called (`timeThread` checks it) **[VM before relying on it]**.
* At most **16** live events per process (`timeThreadSetEvent` scans 16
  slots; 17th -> 0). Ids are `generation*16 + slot` with the generation
  counter stepping by 0x10, so ids are nonzero and not reused immediately.
* One shared worker thread (`timeThread`) waits on per-event NT timers and
  runs callbacks serially: function callbacks as `fptc(uTimerID, 0, dwUser,
  0, 0)` (uMsg is 0); EVENT_SET -> `SetEvent((HANDLE)fptc)`; EVENT_PULSE ->
  `PulseEvent`. One-shot events (no TIME_PERIODIC) free their slot and call
  `timeEndPeriod` right after the callback; periodic ones re-arm from the
  scheduled due time (absolute, drift-free).
* `timeKillEvent`: unknown/stale id -> **97**; success -> 0 and
  `timeEndPeriod`. With TIME_KILL_SYNCHRONOUS on the event and a call from a
  thread other than the timer thread while that callback runs, it waits for
  the callback to finish. Killing an already-fired one-shot -> 97.

## 7. mmio (winmmbase)

* `mmioOpenW/A` (!0x16c0): `fdwOpen & 0xFFFC0000` -> NULL **without setting
  wErrorRet**. Otherwise `wErrorRet = 0`; MMIO_ALLOCBUF with cchBuffer 0 ->
  default 8192 buffer; the IO proc is selected by `SetIOProc` from the file
  name (`+` compound syntax / extension) or the mmioinfo fccIOProc/pIOProc;
  memory files (FOURCC_MEM) need pchBuffer/cchBuffer. With MMIO_PARSE,
  MMIO_DELETE, MMIO_EXIST or MMIO_GETTEMP (`0x24300`) the call returns
  **(HMMIO)1** on success (no handle) and NULL on failure, the error in
  wErrorRet (e.g. 257 FILENOTFOUND). Allocation failure -> wErrorRet 258.
* `mmioClose`, `mmioRead`, `mmioSeek`, `mmioGetInfo` validate the handle type
  5: `mmioClose`/`mmioGetInfo` -> 5; `mmioRead`/`mmioSeek` -> -1.
  `mmioRead(pch == NULL)` -> -1. `mmioGetInfo(NULL info)` -> 11.
* `mmioSeek` (!0x1e50): origin SEEK_SET/CUR/END; **any other origin is
  treated as offset 0 from the start** (returns 0) **[VM]**; for memory files
  seeking outside the buffer -> -1.
* `mmioDescend` (!0x2be0): flags other than FINDCHUNK 0x10 / FINDRIFF 0x20 /
  FINDLIST 0x40 -> 10; NULL ckinfo -> 11; `pmmcki->dwFlags = 0`; NULL hmmio ->
  258; reads 8-byte headers; 'MThd'/'MTrk' sizes are byte-swapped (MIDI
  files are big-endian); fccType read only for RIFF/LIST; child chunks must
  lie inside the parent; for DOS files cksize is clamped to the remaining
  file length; not found -> **265 (CHUNKNOTFOUND)**; seek failure -> 263.
  FINDCHUNK compares ckid, FINDRIFF/FINDLIST compare fccType.
* `mmioAscend` (!0x2e10): fuAscend != 0 -> 10; NULL -> 11; for chunks
  created with mmioCreateChunk (dwFlags MMIO_DIRTY 0x10000000) it pads to even
  length and patches cksize (write failures -> 262); seeks to
  `dwDataOffset + cksize + (cksize & 1)`; bad offsets -> 272 INVALIDFILE.
* `mmioCreateChunk` (!0x2f30): flags other than CREATERIFF 0x20 / CREATELIST
  0x40 -> 10; NULL -> 11; writes 8 bytes (12 for RIFF/LIST, ckid forced to
  'RIFF'/'LIST'), dwDataOffset = pos + 8, sets MMIO_DIRTY.
* `mmioStringToFOURCCA` (!0x2540): NULL -> 0; copies at most 4 chars, pads
  with spaces after the NUL ("ab" -> 'ab  '), MMIO_TOUPPER (0x10) uppercases.
  The W form converts to ANSI and calls A.
* `mmioInstallIOProcW` (!0x25c0): flags outside MMIO_INSTALLPROC 0x10000 /
  GLOBALPROC 0x10000000 / REMOVEPROC 0x20000 / FINDPROC 0x40000 -> NULL.
  Always remove any proc a test installs.

## 8. PlaySound (winmm `sndPlaySoundI`)

`PlaySoundW(psz, hmod, f)` and `sndPlaySoundW(psz, f)` both call
`sndPlaySoundI` with the flags unchanged (sndPlaySound passes hmod NULL).
* `f & 0xFF80DF20` -> **FALSE** (invalid bits: 0x20, 0x100..0x1000, 0x4000,
  0x8000, >= 0x800000). Valid: SYNC 0, ASYNC 1, NODEFAULT 2, MEMORY 4, LOOP 8,
  NOSTOP 0x10, PURGE 0x40, APPLICATION 0x80, NOWAIT 0x2000, ALIAS 0x10000,
  FILENAME 0x20000, RESOURCE 0x40004, SENTRY 0x80000, RING 0x100000,
  SYSTEM 0x200000.
* SND_NOSTOP while a sound is playing -> FALSE.
* `psz == NULL` (any flags) or an empty string: stops the current sound and
  returns **TRUE**.
* SND_ASYNC requires the notify window (`CreatehwndNotify`); async plays are
  posted to it (message 0x3CC) and the API returns the PostMessage result.
* SND_ASYNC|SND_MEMORY: the image must start with 'RIFF' and have 'WAVE' at
  offset 8, else FALSE; size = RIFF length + 8 must be < 2 MiB; it is copied,
  so the caller's buffer may be freed after return. Synchronous SND_MEMORY
  plays from the caller's buffer.
* SND_RESOURCE: `FindResource(hmod, psz, "SOUND")` then "WAVE"; missing ->
  FALSE.
* SND_ALIAS_ID: id translated by `TransAlias` and loaded as a string; unknown
  -> FALSE.
* Name lookup without SND_FILENAME: try `_GetSoundAlias` (registry
  AppEvents) then file name; files are searched with `SearchPathW(".WAV")`
  then the `MediaPath` from the Setup key.
* Not found + SND_NODEFAULT -> **FALSE**; without NODEFAULT the default
  system sound is played **(never do that in tests)**.
* Zero devices: a synchronous memory play returns FALSE (open fails) **[VM]**.
Test only with in-memory silent WAVE images, SND_MEMORY (+ SND_NODEFAULT),
and stop with `PlaySound(NULL, NULL, 0)`.

## 9. MCI (winmm)

* `mciSendCommandW` (!0xd690) and `mciSendStringW` (!0xd9f0) first run
  `mciInitDeviceList`; failure -> 264 (OUT_OF_MEMORY).
* Return encoding: errors >= 0x200 get the device id in the high word
  (`mciId << 16 | err`); `mciGetErrorString` uses that high word to load the
  text from the device driver's resources, falling back to winmm's table.
* `mciSendStringW` -> `mciSendStringInternal` (!0xbb20 area):
  - **The command string is wcslen'd BEFORE the NULL check** (asm: length scan
    at winmm!0x18000bbaf, NULL test at !0x18000bbcf). `mciSendString(NULL)`
    faults; never test it.
  - Tabs outside quotes become spaces; quotes toggle literal mode.
  - `lpstrReturnString[0] = 0` first when non-NULL.
  - `mciSeparateCommandParts` splits verb / device / rest. Device "new" with
    no existing alias is allowed only for `open` (verb 0x803) and requires an
    alias: `open new type waveaudio` without `alias` -> **299
    (NEW_REQUIRES_ALIAS)**; `new` with another verb -> **263
    (INVALID_DEVICE_NAME)**.
  - Unknown verb -> **261 (UNRECOGNIZED_COMMAND)**.
  - Empty device name (other than `sysinfo` 0x812 forms) -> **292
    (MISSING_DEVICE_NAME)**.
  - Device not open and verb not open/close/sysinfo/break/sound: auto-open
    (`mciAutoOpenDevice`); `notify` on an auto-open -> 300.
  - Device "all" with a command that does not support it -> **279
    (CANNOT_USE_ALL)**.
  - A device opened by another task -> 288 (DEVICE_LOCKED) in the
    shareable check; a pending notify from a different thread routes through
    `mciSendSystemString`.
  - `sysinfo` special words: `notify`, `wait`.
  - Return values are converted to text by `mciConvertReturnValue` only when
    a return buffer was supplied.
* `mciGetErrorStringW` (!0xcac0): NULL buffer -> FALSE; driver-specific code
  with an unknown device -> uses 0x116 (MCIERR_DRIVER, 278) text; returns TRUE
  if LoadString found the text, else writes an empty string and FALSE.
  Unknown codes -> FALSE.
* `mciGetDeviceIDW`: lookup by alias for the calling thread's task; unknown ->
  0.
* Devices: waveaudio (mciwave), sequencer (mciseq), cdaudio (mcicda),
  avivideo (mciavi32); configured under `[MCI32]`. `sysinfo all quantity`
  lists them **[VM]**. With zero wave devices `open new type waveaudio alias
  x` may still succeed (no device is opened until record/play) **[VM]**.
* Notification: MM_MCINOTIFY (0x3B9) posted to hwndCallback with
  MCI_NOTIFY_SUCCESSFUL 1 / SUPERSEDED 2 / ABORTED 4 / FAILURE 8 and the
  device id in lParam.

## 10. Joystick (winmm)

* `joyGetNumDevs` -> constant 16.
* `joyGetDevCapsW` (!0x6220): NULL -> 11; cbjc must be exactly 0x2D8
  (JOYCAPSW) or 0x308 (JOYCAPS2W), else **165**; `JoyInit` failure -> 166;
  uJoyID == -1 -> zeroes the struct, fills only szRegKey, returns 0;
  id < 16 -> `joyOpen` (not connected -> 165); id >= 16 -> **6**.
  (A form: JOYCAPSA/JOYCAPS2A sizes.)
* `joyGetPosEx` (!0x63a0): NULL or dwSize < 52 -> 165; id > 15 -> 165;
  not connected -> 165; disconnected device -> 167.
* `joyGetPos`: NULL -> 11, else joyGetPosEx with JOY_RETURNX|Y|Z|BUTTONS.
* `joyGetThreshold`/`joySetThreshold`: errors are **11** (not JOYERR);
  threshold >= 0x10000 -> 11.
* `joySetCapture`: no user32 timer support -> 8; NULL/non-window hwnd or
  id > 15 -> 165; already captured -> 166. `joyReleaseCapture`: id > 15 ->
  165; never-opened id -> 11.
On a VM without a game controller every id 0..15 reports 165 from
joyGetPosEx/joyGetDevCaps **[VM]**.

## 11. Installable driver functions (winmmbase)

* `OpenDriver` (!0x3510): `InternalLoadDriver` (LoadLibrary + DriverProc)
  then broadcasts DRV_LOAD/DRV_ENABLE/DRV_OPEN (msg 3 = DRV_OPEN returns the
  instance id); a zero DRV_OPEN result frees the driver and returns NULL.
  Driver names come from the Drivers32 section unless a section is given.
* `SendDriverMessage`, `DefDriverProc` (DRV_LOAD/FREE/ENABLE/DISABLE return 1,
  DRV_QUERYCONFIGURE 0, unknown 0), `CloseDriver`, `DrvGetModuleHandle` =
  `GetDriverModuleHandle`.
* `DriverCallback`: see 2.7. Returns TRUE/FALSE.
* `mmTaskCreate/Block/Signal/Yield`, `mmGetCurrentTask` = thread ids.

## 12. Internal message ids (driver protocol) used above

WODM: GETNUMDEVS 3, GETDEVCAPS 4, OPEN 5, CLOSE 6, PREPARE 7, UNPREPARE 8,
WRITE 9, PAUSE 10, RESTART 11, RESET 12, GETPOS 13, GETPITCH 14, SETPITCH 15,
GETVOLUME 16, SETVOLUME 17, GETPLAYBACKRATE 18, SETPLAYBACKRATE 19,
BREAKLOOP 20. WIDM: OPEN 0x34, ADDBUFFER 0x38, START 0x39, STOP 0x3A,
RESET 0x3B, GETPOS 0x3C. MODM: GETDEVCAPS 2, OPEN 3, CLOSE 4, PREPARE 5,
DATA 7, LONGDATA 8. Internal DRVM_* 0x801..0x80F are intercepted by
`mregHandleInternalMessages` (device interface queries, preferred device
0x805, mapper reconfiguration); a `waveOutMessage(id, DRV_QUERYDEVICEINTERFACESIZE ...)`
is answered by winmmbase, not the driver.

## 13. Test-writing consequences (summary)

1. Order matters and is exact: e.g. `waveOutOpen(NULL phwo, ..., pwfx NULL)`
   -> 11 from pwfx; `waveOutWrite(bad handle, unprepared hdr)` -> 34 (header
   before handle); `waveOutPrepareHeader(bad handle, prepared hdr)` -> 0;
   `midiOutLongMsg(bad handle, unprepared)` -> 64; `midiOutPrepareHeader(bad
   handle, NULL hdr)` -> 5 (handle before header); `midiStreamOut(bad, NULL)`
   -> 5.
2. GetDevCaps with cb 0 -> 0 even with a NULL pointer (wave, midi, mixer, aux).
3. `waveOutGetErrorText(code, buf, 0)` -> 0; `(22, buf, n)` -> 9;
   `(32..35)` -> 0; `midiOutGetErrorText(64..71)` -> 0, `(32)` -> 9.
4. Zero devices: every wave open (incl. WAVE_MAPPER and QUERY) -> 2 after
   parameter validation; caps by id -> 2.
5. timeBeginPeriod/EndPeriod counting and the >= uMax no-op; timeGetDevCaps
   -> {1 (typ.), 1000000}; bad cb -> 97; timeGetSystemTime bad cb -> 129.
6. timeSetEvent: 0 on invalid flags/delay, 16-event cap, uMsg 0 in callbacks.
7. mmio: chunk round-trip in a memory file (CREATERIFF, CREATECHUNK, ASCEND,
   DESCEND FINDRIFF/FINDCHUNK), MThd byte-swap, CHUNKNOTFOUND 265.
8. PlaySound: invalid flags -> FALSE, NULL -> TRUE, memory image must be RIFF
   WAVE for ASYNC, NODEFAULT for any name lookup.
9. MCI: error codes 261/263/292/299, high-word device id encoding, never pass
   NULL to mciSendString.
10. Every **[inference]/[VM]** item above needs one VM boot to observe before a
    hard CHECK.
