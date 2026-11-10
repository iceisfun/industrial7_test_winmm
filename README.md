# winmm.dll conformance corpus

Black-box conformance tests for Windows 10 x64 `winmm.dll` (22H2, build 19045), written in C and cross-compiled with
mingw-w64. The DLL is never linked: each test loads it with `LoadLibrary`, resolves entry points with `GetProcAddress`
(`U(Name)` gives a pointer typed from the SDK header; `UIN("module.dll", Name)` pins one module, which matters for
forwarders) and calls through the pointer. Modules searched by `U()`: winmm.dll winmmbase.dll mmdevapi.dll user32.dll
ntdll.dll kernel32.dll. The tests record what a real Windows 10 build does so that other implementations of winmm can
be checked against it.

## Build

    make            # cross-compile build/<area>.exe for every tests/<area>/ (needs x86_64-w64-mingw32-gcc)

`common/` holds the harness (`t.h`, `modules.h`); `tools/*_exports.txt` list the named exports of winmm, winmmbase,
mmdevapi and user32.

## Run on a Windows machine

Copy `build\*.exe` and `tools\run_all.bat` (and `tools\summarize.py` if Python is installed) to the machine.

    build\<area>.exe [substr]     run one area; optional test-name filter
    tools\run_all.bat             run every area, writes results\<area>.txt
    tools\run_all.bat /destructive   also run dx_* areas (they change system state)
    python tools\summarize.py results [--failures]    one status line per area, optionally the failing checks

Tests that need a second process re-launch the exe (`--child <name>`). Scratch files live in `%TEMP%\winmmt_<pid>\`
and are removed on exit. Exit code is 1 if any test failed. A Windows 10 guest under QEMU can be driven with
`tools/vm_run.sh`, `tools/vm_matrix.sh` and `tools/wav_capture.sh`; see `docs/VM_RUN.md` (set `WIN10_RUN`) and
`docs/VM_AUDIO.md`. Every area is meant to pass with and without audio devices.

## Result format

One line per test, `RUN`, then `OK`, `FAIL` or `SKIP`. Each failed check adds `  FAIL file:line expr (detail)`.
`NOTE` lines record observed, version-dependent behaviour (informational, never a failure). The last line is `SUMMARY`.

## Status

`done+evidence`: the area exists and has a `tests/<area>/EVIDENCE.md` recording the Windows 10 observations behind its
checks. `done`: the area exists without an evidence file. `planned`: scoped in `tools/areas.json`, not written yet.

| area | status | scope |
|---|---|---|
| `time_basic` | done+evidence | timeGetTime, timeGetSystemTime, timeGetDevCaps, timeBegin/EndPeriod |
| `time_events` | done+evidence | timeSetEvent / timeKillEvent: periodic, one-shot, callback kinds, bad ids |
| `time_event_lifetime` | done+evidence | multi-thread and lifetime semantics of timeSetEvent/timeKillEvent |
| `playsound_contract` | done+evidence | PlaySound A/W, sndPlaySound A/W, flags and return values |
| `playsound_device` | done+evidence | PlaySound behaviour with and without a wave output device |
| `waveout_open_contract` | done+evidence | waveOutOpen / Close / GetID, format and flag validation |
| `waveout_header_protocol` | done+evidence | waveOut Prepare / Unprepare / Write header state machine |
| `waveout_playback` | done+evidence | waveOut pause, restart, reset, break loop, position (needs audio device) |
| `waveout_callbacks` | done+evidence | waveOut callback kinds and notification messages |
| `waveout_controls_messages` | done | waveOut volume, pitch, playback rate, waveOutMessage |
| `wave_formats` | done+evidence | wave format acceptance matrix for waveOut/waveIn (WAVE_FORMAT_QUERY) |
| `wavein_contract` | done+evidence | waveIn open/close, devcaps, header protocol, error paths |
| `wavein_capture` | done+evidence | waveIn capture path: buffers, WIM_DATA, position (needs capture device) |
| `mme_error_text` | done+evidence | *GetErrorText A/W for wave, midi, mixer, aux: every code, truncation |
| `mme_enumeration_endpoints` | done+evidence | GetNumDevs and device enumeration across all MME families |
| `mme_handles_threads` | done+evidence | MME handle validity and cross-thread use |
| `midiout_contract` | done | midiOut open/close/devcaps/short and long messages, error paths |
| `midiout_device` | done | midiOut on a live synth |
| `midistream` | planned | midiStream open/out/pause/position/property |
| `midiin_thru` | planned | midiIn open/close/devcaps/buffers/start/stop, midiConnect thru |
| `mixer_contract` | planned | mixer open/close/devcaps/line and control queries, struct size checks |
| `mixer_topology` | planned | read-only mixer topology on a guest with endpoints |
| `dx_mixer_volume_set` | planned | DESTRUCTIVE: sets mixer/endpoint volume state (run with /destructive) |
| `aux_devices` | planned | aux devcaps, volume, auxOutMessage |
| `joystick` | planned | joyGetDevCaps/Pos/PosEx, thresholds, capture, no-device error paths |
| `mmio_memory_buffered` | planned | mmio read/write/seek/getinfo/setbuffer/flush/advance on memory files |
| `mmio_riff_chunks` | planned | mmioDescend / Ascend / CreateChunk, FOURCC conversion |
| `mmio_files_ioprocs` | planned | mmioOpen A/W, rename, custom IOProcs |
| `mci_string_parser` | planned | mciSendString A/W command parser |
| `mci_command_interface` | planned | mciSendCommand A/W, mciGetErrorString A/W |
| `mci_waveaudio_device` | planned | MCI waveaudio device beyond open/close (needs wave device) |
| `mci_devices_misc` | planned | mciGetDeviceID*, creator task, yield proc and related helpers |
| `mci_driver_side` | planned | MCI driver-side API: notify, yield, driver data, command resources |
| `driver_api` | planned | OpenDriver / CloseDriver / SendDriverMessage / DefDriverProc / module handles |
| `driver_callback_tasks` | planned | DriverCallback and mmTask* helpers |
| `export_surface_legacy` | planned | mmsystemGetVersion, WOWAppExit, mmDrvInstall |
| `aw_parity_sweep` | planned | ANSI/Unicode parity across every A/W export pair |
| `dx_legacy_process` | planned | DESTRUCTIVE: process- and session-wide legacy paths (run with /destructive) |

Research notes behind the area plan are in `docs/research/`.
