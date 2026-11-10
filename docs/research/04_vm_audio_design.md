# 04: VM audio design (what the Win10 oracle guest exposes, and how tests use it)

Question: when the corpus runs on the Win10 22H2 oracle (a `WIN10_RUN` runner, see docs/VM_RUN.md; build 19045.2965,
auto-logon Administrator), which winmm / mmdevapi devices exist at run.cmd time, under which QEMU audio
configuration, how fast do they appear, and can a payload's real playback be checked on the host?

Everything below is measured with `tools/proto/audprobe.c` (guest probe) driven by
`tools/proto/run_audio_configs.sh` (one boot per QEMU configuration) and `tools/proto/wavscan.py`
(host-side analysis of the QEMU `wav` audiodev capture). Raw logs (kept outside this repo): `winmm-audio-r1`,
`-r2`, `-r3` (serial.log, out.wav, wavscan.txt per configuration; r1 = hda-duplex-wav, r2 = noaudio, r3 =
hda-output-wav and multi. `winmm-audio-r2/multi` is an aborted attempt with an empty serial.log: every multi
number below comes from r3). Reproduce with
`tools/proto/run_audio_configs.sh OUTROOT noaudio hda-duplex-wav hda-output-wav multi` (build audprobe.exe with
`x86_64-w64-mingw32-gcc -O2 -o audprobe.exe audprobe.c -lwinmm -lole32 -luuid -ladvapi32`); one boot per
configuration, about 2 minutes of guest time each, plus whatever the shared lock queue costs.

## 1. Recommendation (TL;DR)

- Default corpus configuration:
  `W10_EXTRA='-audiodev wav,id=snd0,path=<OUT>/out.wav -device intel-hda -device hda-duplex,audiodev=snd0'`
  (or `-audiodev none,...` when the host capture is not wanted). Inbox `hdaudio.inf` binds with no driver
  install; the guest has 1 waveOut, 1 waveIn, 2 mixers, 1 render + 1 capture endpoint, all defaults set,
  already when run.cmd starts. No service start is needed.
- Alternative with the same guest contract: `-device usb-audio,audiodev=snd0` (inbox usbaudio, render only).
  AC97, ES1370 and SB16 are not usable (no inbox driver / no ISA PnP). If several devices are present the
  default endpoint is not necessarily the first controller (usb-audio won over two HDA codecs).
- Every test must also pass on a guest with no audio hardware (configuration `noaudio`): it is the
  contract-only path (MMSYSERR_BADDEVICEID for WAVE_MAPPER and id 0, MIDI mapper present but unopenable).
  Run the corpus twice per oracle pass when possible: once with `noaudio`, once with HDA duplex.
- Do not gate on AudioSrv: it is RUNNING in both configurations by logon. winmm itself waits for AudioSrv
  (see 4) so the first winmm call may block on a very early boot (up to about 122 s). It does not fail
  outright, but if AudioSrv is still not running when the wait gives up, winmm carries on and the counts
  are whatever it can enumerate (likely 0); this timeout path is from the decomp and was not observed.
- Real playback is checkable: the `wav` audiodev records exactly what the guest renders (2 ch, 16-bit,
  44100 Hz, resampled by QEMU; on HDA the -6 dB endpoint volume is not applied, on usb-audio the level was 0.5 dB lower). Frequency, level and duration of known tones
  are recovered by wavscan.py. Capture content is NOT injectable (the wav backend has no input voice; the
  capture device records silence), so capture tests can only check the buffer protocol and timing.

## 2. Configurations measured

| config | QEMU devices (W10_EXTRA) | waveOut | waveIn | midiOut | midiIn | mixer | aux | joy | render ep | capture ep |
|---|---|---|---|---|---|---|---|---|---|---|
| noaudio | (none) | 0 | 0 | 1 | 0 | 0 | 0 | 16 | 0 | 0 |
| hda-duplex-wav | intel-hda + hda-duplex, wav | 1 | 1 | 1 | 0 | 2 | 0 | 16 | 1 | 1 |
| hda-output-wav | intel-hda + hda-output, wav | 1 | 0 | 1 | 0 | 1 | 0 | 16 | 1 | 0 |
| multi | intel-hda + hda-output (none); ich9-intel-hda + hda-micro (none); usb-audio (wav); AC97, ES1370, sb16 (none) (run r3) | 3 | 1 | 1 | 0 | 4 | 0 | 16 | 3 | 1 |

Columns are the winmm `*GetNumDevs` results (midiOut counts the GS Wavetable Synth, not the mapper; joy is
`joyGetNumDevs`, which is 16 regardless of hardware) and the mmdevapi `EnumAudioEndpoints(DEVICE_STATEMASK_ALL)`
counts. The same numbers were seen in the first poll (about 30-40 s of uptime, i.e. as soon as run.cmd runs)
and stayed stable; no configuration needed a later appearance.

Output-only (hda-output) differences that matter for tests:
- With 0 waveIn devices but a waveOut device present, `waveInGetDevCaps(WAVE_MAPPER)` still SUCCEEDS
  ("Microsoft Sound Mapper", mid 1 pid 2 ver 0x500, formats 0xFFFFF, 2 ch) while `waveInGetDevCaps(0)` is
  MMSYSERR_BADDEVICEID; `waveInOpen(WAVE_MAPPER)` and every WAVE_FORMAT_QUERY on the waveIn mapper return
  MMSYSERR_BADDEVICEID; DRVM_MAPPER_PREFERRED_GET on waveIn returns 0 with device -1 (0xFFFFFFFF) and flags 0.
  In `noaudio` the waveOut and waveIn mapper caps both fail with 2. So "mapper caps succeed" is not
  "devices exist"; tests must key on GetNumDevs.
- One mixer only (the render endpoint), no capture endpoint, default capture 0x80070490.
- The codec PnP id is `DEV_0012` (hda-output) vs `DEV_0022` (hda-duplex); endpoint GUIDs differ per codec.
- Playback, PlaySound and MIDI behave exactly as with duplex; the wav capture matched the tone plan again
  (440/1000/660 Hz at -15.2/-15.3/-15.3 dBFS rms, GS synth 880 Hz at -39.0 dBFS).

Alternative cards (multi boot, every QEMU 8.2 audio model at once):
- `usb-audio` (on the default `-usb` UHCI controller): inbox `wdma_usb.inf` (usbaudio service RUNNING), PnP
  `USB\VID_46F4&PID_0002&MI_00`, endpoint "Speakers (Audio Device)". It became the DEFAULT render endpoint for
  all three roles over the two HDA speakers, and winmm puts the default first: waveOut 0 = "Speakers (Audio
  Device)" (wMid 65535, wPid 65535), waveOut 1 / 2 = the HDA speakers ("Speakers (2- High Definition Au" is the
  31-char truncation of the second controller's name), mixer order follows the same ordering. USB endpoint
  volume range is [-128, +8] dB step 0.531, scalar 0.710 = 0 dB, period min 30000. The wav capture (attached to
  usb-audio) recovered the full tone plan: 440/1000 Hz at -15.7 dBFS (0.5 dB below the HDA path), the PlaySound
  660 Hz tone at -16.3 dBFS and the GS synth note at -40.1 dBFS. Only one usb-audio boot exists, so the exact
  offset is a single observation.
- `ich9-intel-hda` + `hda-micro`: inbox hdaudio, endpoints "Speakers (2- High Definition Audio Device)" and
  "Microphone (High Definition Audio Device)"; the capture mixer source is MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE
  (0x1003) instead of SRC_LINE (0x1002).
- `AC97` (PCI 8086:2415) and `ES1370` (PCI 1274:5000): enumerated as "Multimedia Audio Controller", problem code
  28 (no inbox driver). Unusable without third-party drivers; not worth installing into the base.
- `sb16` (ISA): not enumerated at all (no ISA PnP). `adlib`, `gus`, `cs4231a` are ISA too and were not tried.
- WAVE_MAPPER caps dwSupport was 0xE in this boot (no WAVECAPS_SAMPLEACCURATE) while every device reported 0x2E;
  with a single HDA device the mapper reported 0x2E. Mapper support bits depend on the device set: do not hard
  check them.
- Capture through hda-micro completed a 0.5 s buffer in 516 ms (1016 ms in the hda-duplex boot): capture start
  latency varies.

Backend `none` vs `wav`: the audiodev backend is host-side only; the guest sees the same codec (PCI/HDA ids,
endpoints, caps) for either. A dedicated `hda-duplex-none` boot was queued for this pass but cancelled
after about 50 minutes starved on the shared oracle lock (35+ waiters across workflows); the earlier
noaudio/duplex/output/multi boots plus the fact that the backend never reaches the guest make it redundant.
`none` discards output and supplies silent input; `wav` writes output to a host file and has no input voice.

## 3. Device details (hda-duplex)

PnP (pnputil): `HDAUDIO\FUNC_01&VEN_1AF4&DEV_0022&SUBSYS_1AF40022&REV_1001` "High Definition Audio Device",
class MEDIA, driver `hdaudio.inf` (inbox, Started). Endpoints (class AudioEndpoint, audioendpoint.inf):
`{0.0.0.00000000}.{...}` "Speakers (High Definition Audio Device)" and `{0.0.1.00000000}.{...}`
"Line In (High Definition Audio Device)". The endpoint GUIDs are per-install (stable across boots of the same
base image, but tests must not hard code them). Services: hdaudbus and HdAudAddService RUNNING (STOPPED in
noaudio), usbaudio STOPPED, wdmaud/sysaudio services absent (Win10 has no kernel wdmaud path; winmm goes
through AudioSrv / mmdevapi). The only PnP problem device in every configuration is `ACPI\QEMU0002`
(fw_cfg, code 28) which is unrelated.

mmdevapi:
- Render: state ACTIVE (0x1), mix format WAVE_FORMAT_EXTENSIBLE float 32-bit, 2 ch, 48000 Hz, mask 0x3;
  device period default 100000, minimum 26667 (100 ns units); endpoint volume scalar 0.671 = -6.00 dB, range
  [-74, 0] dB step 1.0, unmuted, 2 channels.
- Capture: ACTIVE, float32 2 ch 44100 Hz, period default 101587 min 29025, same volume range.
- Default device for eConsole, eMultimedia and eCommunications is the single endpoint of each flow. With no
  endpoint, `GetDefaultAudioEndpoint` returns 0x80070490 (HRESULT_FROM_WIN32(ERROR_NOT_FOUND)).

winmm caps (hda-duplex):
- waveOutGetDevCaps(WAVE_MAPPER): "Microsoft Sound Mapper", wMid 1, wPid 2, vDriverVersion 0x500, dwFormats
  0xFFFFF, wChannels 2, dwSupport 0x2E (WAVECAPS_PLAYBACKRATE 0x2 | VOLUME 0x4 | LRVOLUME 0x8 |
  SAMPLEACCURATE 0x20; no PITCH, no SYNC). The multi-boot mapper value 0xE is the same set minus
  SAMPLEACCURATE. Device 0: name
  "Speakers (High Definition Audio" (MAXPNAMELEN truncation, 31 chars), wMid 1, wPid 65535, version 0,
  formats 0xFFFFF, 2 ch, support 0x2E. Device 1 (out of range): MMSYSERR_BADDEVICEID (2).
- waveInGetDevCaps: mapper "Microsoft Sound Mapper" mid 1 pid 2 ver 0x500; device 0
  "Line In (High Definition Audio " (truncated), formats 0xFFFFF, 2 ch.
- DRV_QUERYFUNCTIONINSTANCEID on waveOut/waveIn 0 returns the mmdevapi endpoint id string (size 112 bytes
  including NUL), which is how a test maps winmm ids to endpoints.
- DRVM_MAPPER_PREFERRED_GET: device 0, flags 0. With no device: MMSYSERR_BADDEVICEID and the outputs untouched.
- midiOut: mapper "Microsoft MIDI Mapper" mid 1 pid 1 ver 0x500 wTechnology MOD_MAPPER (5) chmask 0xFFFF
  support 0x9 (MIDICAPS_VOLUME | MIDICAPS_STREAM); device 0 "Microsoft GS Wavetable Synth" mid 1 pid 27 ver 0x100 MOD_SWSYNTH (7) 32 voices 32
  notes chmask 0xFFFF support 0x1 (MIDICAPS_VOLUME). Present in every configuration, even noaudio.
  DRV_QUERYFUNCTIONINSTANCEIDSIZE on midiOut 0 returns 0 with size 2 (an empty wide string): the GS synth has
  no mmdevapi endpoint id.
- midiIn, aux: 0 devices; GetDevCaps on mapper/0 returns MMSYSERR_BADDEVICEID; auxGetVolume the same and the
  out value untouched.
- joy: joyGetNumDevs 16, joyGetDevCaps(0) JOYERR_PARMS (165), joyGetPosEx(0) JOYERR_PARMS.
- mixer: one mixer per endpoint. Mixer 0 "Speakers (High Definition Audio" with one destination "Master
  Volume" (short "Volume"), MIXERLINE_COMPONENTTYPE_DST_SPEAKERS (4), cChannels 1, 2 connections (sources
  "CD Audio" SRC_COMPACTDISC 0x1005 and "Master Volume" SRC_WAVEOUT 0x1008), 2 controls: Mute (id 1,
  BOOLEAN 0x20010002, min 0 max 1) and Volume (id 2, VOLUME 0x50030001, 0..65535, 192 steps). Mixer 1
  "Line In ..." destination DST_WAVEIN (7), 1 source "Master Volume" SRC_LINE (0x1002), same two controls.
  Line ids 0xFFFF0000 (destination), 0x0 / 0x10000 (sources). mixerGetDevCaps(-1) (there is no mixer
  mapper; -1 is just an out-of-range id) and the out-of-range index return MMSYSERR_BADDEVICEID.
  Further details read from the same logs: MIXERCAPS fdwSupport 0, wMid 1 wPid 65535 vDriverVersion 0 (the
  usb-audio mixer reports wMid 65535); destination fdwLine 0x1 (MIXERLINE_LINEF_ACTIVE); both controls have
  fdwControl 0x1 (MIXERCONTROL_CONTROLF_UNIFORM). The speaker destination's Target.dwType is 0
  (MIXERLINE_TARGETTYPE_UNDEFINED), not WAVEOUT, while the capture destination's is 2 (WAVEIN); Target.dwDeviceID
  0 in both. A test must not assume TARGETTYPE_WAVEOUT on a render destination.
- Format queries (WAVE_FORMAT_QUERY, PCM): every rate in {8000, 11025, 22050, 44100, 48000, 96000, 192000}
  x bits {8, 16, 24, 32} x {1, 2} channels returns MMSYSERR_NOERROR on both the mapper and device 0, out and
  in; plain WAVE_FORMAT_PCM with 6 channels returns WAVERR_BADFORMAT (32) everywhere a device exists. Where the
  direction has no device (noaudio out and in; hda-output in) every query, 6-channel included, returns
  MMSYSERR_BADDEVICEID (2): the device check comes before format validation. WAVE_FORMAT_EXTENSIBLE
  float32 2 ch 48000 on the mapper: 0.

## 4. Timing and services

- AudioSrv (Windows Audio, LocalServiceNetworkRestricted, AUTO_START, depends on AudioEndpointBuilder and
  RpcSs) and AudioEndpointBuilder (AUTO_START) are RUNNING at run.cmd time in every configuration, MMCSS too.
  `sc start` from run.cmd returns 1056 (already running): starting them is unnecessary.
- First poll at 30-41 s uptime already showed the final counts; the probe declares "stable" after 5 s of
  unchanged counts. Endpoint appearance is therefore not a factor for the corpus with this base image.
- winmmbase decomp: every winmmbase `*GetNumDevs` (wave/midi/mixer/aux; not `joyGetNumDevs`, which in winmm
  is a constant `return 0x10`) calls `ClientUpdatePnpInfo`, and so do nearly all wave/midi/mixer/aux entry
  points (GetDevCaps, Open, Close, Prepare, Write, Message, GetVolume, ...; 70+ callers). Its first call runs
  `winmmWaitForService`, then `AudioSrvBinding`, `InitDevices`, and sets `hEventApiInit`; concurrent callers
  wait on that event with INFINITE. `winmmWaitForService` opens AudioSrv and polls QueryServiceStatus every
  2 s while the state is START_PENDING, or STOPPED with exit code 1077 (never started) and start type
  AUTO_START; it stops waiting on RUNNING..PAUSED, on STOP_PENDING, on any other STOPPED, on any SCM error,
  or after the counter passes 120000 ms (61 sleeps, about 122 s). Its result is ignored by
  `ClientUpdatePnpInfo`. Later calls only re-enumerate when `winmmGetNewPnpEvents` reports an event. So counts follow hot plug without restarting the process, and a very early call blocks rather than
  reporting 0. A corpus timeout must allow for this on the first winmm call (it was not observed to block on
  the oracle: the first call returned within milliseconds).

## 5. Playback and capture behaviour (hda-duplex)

- waveOutOpen(WAVE_MAPPER, PCM 44100/16/2, CALLBACK_EVENT): 0 in about 16 ms. waveOutGetID on the mapper
  handle returns WAVE_MAPPER (0xFFFFFFFF), not the physical id. waveOutGetVolume: 0xFFFFFFFF.
- waveOutPrepareHeader sets WHDR_PREPARED (0x2); after waveOutWrite the flags are 0x12 (PREPARED | INQUEUE);
  on completion 0x3 (DONE | PREPARED). 1.2 s of audio completed after 1187 ms; waveOutGetPosition TIME_SAMPLES
  returned 52920 (= 1.2 s x 44100, exact), TIME_MS request was answered as TIME_BYTES (type 4) 211680.
  Do not hard check durations; check the final position equals the frames written.
- PlaySound(SND_MEMORY | SND_SYNC) of a 400 ms in-memory wave returned TRUE after 421 ms.
- midiOutOpen(MIDI_MAPPER): 0 in about 60 ms; short messages and midiOutReset 0. With NO wave device the mapper
  open fails with MMSYSERR_ERROR (1) after about 110 ms although the GS synth is enumerated (single noaudio
  boot). Interpretation, not traced in the decomp: the synth renders through the audio engine and cannot open
  a render stream.
- waveInOpen(WAVE_MAPPER, 44100/16/1): 0; a 0.5 s buffer completed (WHDR_DONE) with dwBytesRecorded 44100;
  content is silence (peak 1 LSB) because no QEMU backend supplies input. The 0.5 s buffer took about
  1016 ms from waveInStart to WHDR_DONE (start-up latency of the capture stream): allow seconds, not ms.
- No-device contract (noaudio): waveOutOpen(WAVE_MAPPER) and waveOutOpen(0) return MMSYSERR_BADDEVICEID (2) and
  leave the output handle unchanged; waveInOpen(WAVE_MAPPER) 2; PlaySound(SND_MEMORY|SND_SYNC) FALSE with
  GetLastError 6 (ERROR_INVALID_HANDLE as left behind by the failed open; do not check the error code hard);
  waveOutGetDevCaps(mapper) 2; mixerGetNumDevs 0.

## 6. Host-side playback verification (wav audiodev)

`-audiodev wav,id=snd0,path=$OUT/out.wav` writes everything the guest renders to a RIFF file on the host,
2 ch 16-bit 44100 Hz (QEMU's default backend format, the guest stream is resampled). QEMU warns
"Could not create a backend for voice `adc'" for every capture open: harmless.

Measured (hda-duplex-wav): the probe's tone plan was recovered by wavscan.py:

    tone 0.00-0.60s ~440 Hz  -15.2 dBFS   waveOut segment 1
    tone 0.60-1.20s ~1000 Hz -15.3 dBFS   waveOut segment 2
    tone 1.20-1.60s ~660 Hz  -15.2 dBFS   PlaySound SND_MEMORY
    tone 2.05-2.45s ~880 Hz  -39.2 dBFS   MIDI note 81 through the GS synth

The source tones were -12 dBFS peak (-15 dBFS rms): the captured level equals the source level, so the
endpoint volume (-6 dB) is not applied to the wav file and the sample stream is unaltered except for
resampling. The whole run (about 3 s of wall time from the first waveOutOpen to the MIDI note off) produced
2.46 s of audio, so stretches with no open render stream are not recorded: tone positions in the file are not
wall-clock positions, while order, frequency, level and duration are reliable.

How a test uses it: the test emits a known, short, quiet, distinctive signal (for example a 0.3 s tone at a
unique frequency, or a frequency-coded marker before and after), prints what it played to COM1, and the host
runner checks out.wav with wavscan.py (or a stricter comparator) after the boot. In-guest assertions cannot
see the file; host checks belong to the runner, not to TEST() bodies. Captured content cannot be checked
(no input injection with the wav backend). QEMU 8.2 has no backend whose input reads a file directly, but the
host QEMU (8.2.2) also has the `pa`, `pipewire`, `jack`, `alsa` and `sdl` backends. Feeding a host-side virtual
source (for example a PulseAudio/PipeWire pipe or null source playing a file) into `-audiodev pa` or
`pipewire` might allow capture injection. UNVERIFIED: not tried, and it would need a host sound server
running headless next to the lock script.

## 7. Design rules for the corpus

1. Every device test begins with `waveOutGetNumDevs()` / `waveInGetNumDevs()` / `mixerGetNumDevs()`; with 0
   it checks the no-device contract (codes above) and returns; with devices it runs the full path. Never
   assume a device index other than 0..n-1 and never assume endpoint names or GUIDs.
2. The MIDI mapper and GS synth are always enumerated; midiOutOpen success is only expected when
   waveOutGetNumDevs() > 0.
3. Mixer tests may read controls; any write must restore the original value (dx_ area gating per SAFETY.md).
4. Output only silence or short tones at about -12 dBFS or lower; each audible test registers its signal so
   the host runner can verify it.
5. Timing: wall-clock checks only as wide bounds (completion within several seconds); positions are exact.
6. The joystick family always reports 16 devices (a constant in the decomp) and JOYERR_PARMS for absent
   ones (measured: joyGetDevCaps(0) and joyGetPosEx(0) on this VM); this does not depend on the audio
   configuration. JOYERR_UNPLUGGED was not observed on the VM (UNVERIFIED here).

## Verification (adversarial fact-check pass)

Re-read every raw log (`winmm-audio-r1/hda-duplex-wav`, `-r2/noaudio`, `-r3/hda-output-wav`,
`-r3/multi`: serial.log and run.log), re-ran `tools/proto/wavscan.py` on the three out.wav files (output
identical to the stored wavscan.txt; all three RIFF headers are finalized), checked flag values against the
mingw `mmeapi.h`, and re-read the winmmbase decomp of `winmmWaitForService`, `ClientUpdatePnpInfo` (and its
callers list), all five winmmbase `*GetNumDevs`, and winmm `joyGetNumDevs`. No new VM boot was needed.

Confirmed against the logs: every count in the table (all four configs, first poll at 30.3-41.3 s uptime,
stable thereafter); AudioSrv / AudioEndpointBuilder / MMCSS RUNNING and `sc start` 1056; PnP ids, driver
names, problem code 28 for AC97 (8086:2415) / ES1370 (1274:5000) / ACPI\QEMU0002; endpoint names, mix
formats, periods and volume ranges; caps of mapper / device 0 / out-of-range ids; 31-char name truncation;
instance id size 112; DRVM_MAPPER_PREFERRED_GET results including dev -1 on the output-only waveIn mapper and
untouched 0xdead outputs with no device; mixer topology; format query matrix; playback flags 0x2 / 0x12 / 0x3,
positions 52920 / 211680 with TIME_MS answered as TIME_BYTES (4); PlaySound and MIDI timings; capture 44100
bytes, peak 1, 1016 / 516 ms; no-device contract (handle left at the probe's sentinel 1, PlaySound FALSE
with GetLastError 6, MIDI mapper 1); usb-audio first in winmm order while last in mmdevapi enumeration; QEMU
"Could not create a backend for voice `adc'" warnings in run.log; QEMU 8.2.2 on the host. Tone levels
match wavscan output (source amplitude 0.25 = -12 dBFS peak, as in audprobe.c).

Corrected in this pass:
- dwSupport 0x2E was decoded as "VOLUME | LRVOLUME | SAMPLEACCURATE | 0x20"; it is PLAYBACKRATE (0x2) |
  VOLUME | LRVOLUME | SAMPLEACCURATE (0x20).
- "the first winmm call may block briefly ..., never fail": the decomp wait lasts up to about 122 s and then
  proceeds regardless; reworded, with the exact stop conditions.
- "every `*GetNumDevs` calls ClientUpdatePnpInfo": true for winmmbase wave/midi/mixer/aux only, not for
  joyGetNumDevs (constant 16), and almost every other wave/midi/mixer/aux API calls it as well.
- usb-audio wav levels: only the 440/1000 Hz tones were at -15.7 dBFS (660 Hz -16.3, synth -40.1).
- Run provenance: the r2 multi directory is an aborted run with an empty serial.log; multi data is r3.
- Capture injection: "QEMU 8.2 does not offer" a file input backend is literally true, but the pa/pipewire
  backends with a host virtual source are an untried option (UNVERIFIED).

Added: MIDICAPS 0x9 decode; GS synth has an empty function instance id; mixer Target.dwType 0 (UNDEFINED) on
the speaker destination vs 2 on the capture one, fdwSupport 0, LINEF_ACTIVE, CONTROLF_UNIFORM; format queries
on a direction without devices return 2 for every format (device check precedes format check).

UNVERIFIED (unchanged by this pass): guest view with the `none` backend (no hda-duplex-none boot; expected
identical because the backend is host-only); behaviour of the 122 s AudioSrv wait (decomp only, never
observed); the MIDI-mapper failure explanation; JOYERR_UNPLUGGED; ISA cards adlib/gus/cs4231a and backends
other than none/wav; single-boot observations (usb-audio level offset, capture latency 516 vs 1016 ms, mapper
dwSupport 0xE with several devices).
