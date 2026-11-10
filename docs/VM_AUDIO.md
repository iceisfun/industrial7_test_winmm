# Audio on the Win10 VM: configurations the corpus uses

Background measurements (every QEMU audio model, timings, services, caps, mixer topology, format matrix):
docs/research/04_vm_audio_design.md. This page is the operational summary: which configuration the corpus runs in,
what the guest reports there, and how tests detect it.

## 1. Configurations

| `vm_run.sh --audio` | QEMU devices | role |
|---|---|---|
| **`hda`** (default) | `intel-hda` + `hda-duplex`, `-audiodev wav` to `BOOTDIR/out.wav` | **default corpus configuration**: one render and one capture device, host capture of everything played |
| **`noaudio`** | none | **no-device configuration**: the contract-only paths |
| `hda-none` | as hda, `-audiodev none` | same guest view, no host file (the backend never reaches the guest) |
| `hda-out` | `intel-hda` + `hda-output`, wav | render only: 0 waveIn while waveOut exists (asymmetric mapper cases) |
| `usb` | `usb-audio`, wav | render only, inbox usbaudio; different MIDs/PIDs and volume range |

Every area must PASS in both `hda` and `noaudio` (`tools/vm_matrix.sh` runs both). Inbox drivers bind with no
install (hdaudio.inf / wdma_usb.inf); AC97, ES1370 and SB16 are unusable on this image (no inbox driver / no ISA PnP).

## 2. What the guest reports

`hda` (vmrun `--audio-ready` line + docs/research/04; endpoint GUIDs are per install, never hard code them):

- winmm counts: waveOut 1, waveIn 1, midiOut 1, midiIn 0, mixer 2, aux 0; `joyGetNumDevs` 16 (a constant).
- waveOut 0 "Speakers (High Definition Audio" (31-char MAXPNAMELEN truncation), wMid 1, wPid 65535; mapper
  "Microsoft Sound Mapper" wMid 1 wPid 2 vDriverVersion 0x500.
- waveIn 0 "Line In (High Definition Audio " (truncated).
- midiOut 0 "Microsoft GS Wavetable Synth" (MOD_SWSYNTH); midiOut mapper "Microsoft MIDI Mapper".
- mixer 0 "Speakers (High Definition Audio" (DST_SPEAKERS, Mute + Volume), mixer 1 "Line In (High Definition Audio "
  (DST_WAVEIN).
- mmdevapi: 1 active render endpoint "Speakers (High Definition Audio Device)", 1 active capture endpoint
  "Line In (High Definition Audio Device)", both defaults for every role; render mix format float32 2 ch 48000 Hz.
- AudioSrv and AudioEndpointBuilder RUNNING at logon; nothing needs starting.

`noaudio`:

- winmm counts: waveOut 0, waveIn 0, midiOut 1 (the GS synth is still enumerated), midiIn 0, mixer 0, aux 0.
- `waveOutGetDevCaps(WAVE_MAPPER)` and `waveInGetDevCaps(WAVE_MAPPER)` = MMSYSERR_BADDEVICEID (2);
  `waveOutOpen(WAVE_MAPPER / 0)` = 2 with the handle untouched; every WAVE_FORMAT_QUERY = 2 (the device check comes
  before format validation); `midiOutOpen(MIDI_MAPPER)` = MMSYSERR_ERROR (1) although the synth is listed.
- mmdevapi: 0 endpoints; `GetDefaultAudioEndpoint` = 0x80070490.

Run-time record: every run writes the `@@I7 AUDIO` line and one `@@I7 AUDIODEV` line per device to
`<out>/_audio.log`, and `core_smoke`'s `smoke_audio_environment` notes `AUDIOENV waveout= wavein= midiout= mixer=`.

## 3. How tests detect the configuration (auto-detect, no flags)

- Key every device path on `waveOutGetNumDevs()` / `waveInGetNumDevs()` / `mixerGetNumDevs()` /
  `midiOutGetNumDevs()`. With 0: check the no-device contract (above) and return. With n > 0: run the full path on ids
  0..n-1 and use id n as the out-of-range id.
- "Mapper caps succeed" does not mean "a device exists" (`hda-out`: waveIn mapper caps succeed with 0 waveIn devices).
- `midiOutOpen` success is expected only when `waveOutGetNumDevs() > 0`.
- Never assume names, GUIDs or MIDs beyond `t_note`; the `usb` mode exists to catch such assumptions.
- The first winmm call in a process may wait for AudioSrv; the runner's readiness preamble makes this a no-op on the VM,
  but tests must still never time the first `*GetNumDevs` call.

## 4. Host-side playback verification (wav capture)

The `wav` audiodev writes everything the guest renders to `BOOTDIR/out.wav` (2 ch 16-bit 44100 Hz, resampled by
QEMU, the endpoint volume is NOT applied, silent stretches with no open stream are not recorded so positions in the
file are not wall-clock positions). vm_run.sh copies it to `<out>/_capture_b<N>.wav` and writes a
`tools/wavscan.py` segment summary to `_capture_b<N>.scan`.

Protocol: a test that plays a tone calls `t_note("TONE hz=%d ms=%d", hz, ms)` right before playing it. Then

    tools/wav_capture.sh AREA[,AREA...]          # boots with --audio hda into results/win10-capture, then checks
    tools/wav_capture.sh --check results/win10   # re-check an existing run

collects the announced tones of each boot in order and runs `wavscan.py CAPTURE --expect HZ:MS,...` (frequency within
3 %, duration within 25 % / 100 ms, segments matched in order). Missing tones fail the script (exit 1). Use distinct
frequencies per tone, at most about -12 dBFS, a few hundred ms. Capture content (waveIn) cannot be injected with the wav
backend: capture tests check the buffer protocol and the byte counts only.

## 5. Verification log

(Filled from the foundation boots; see below.)
