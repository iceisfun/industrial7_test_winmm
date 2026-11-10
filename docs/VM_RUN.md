# Running the corpus on the Win10 VM

The oracle is a Windows 10 22H2 guest (build 19045.2965, auto-logon Administrator). The tools here do not boot it
themselves: they call a runner script named by the `WIN10_RUN` environment variable (default empty; the tools then
print an error and exit 2).

Runner interface: `WIN10_RUN PAYLOAD_DIR BOOT_DIR`, with optional environment `W10_EXTRA` (extra QEMU arguments, used
for the virtual audio devices) and `ORACLE_TIMEOUT` (seconds before the guest is killed). The runner must boot the
guest with the contents of `PAYLOAD_DIR` available as a read-only CD, run `run.cmd` from it automatically at logon,
capture the guest's COM1 serial output into `BOOT_DIR/serial.log`, and power the guest off when `run.cmd` finishes or
the timeout expires. For wav-backed audio modes the `W10_EXTRA` path points into `BOOT_DIR` (`out.wav`). Only one
guest should run at a time, so batch many areas per boot.

    export WIN10_RUN=/path/to/your/boot-win10-guest.sh

    make                                          # build/<area>.exe + build/tools/vmrun.exe
    tools/vm_run.sh core_smoke                    # default audio (hda), results/win10/core_smoke.txt
    tools/vm_run.sh waveout_basic,mixer_query --audio noaudio      # no-device contract, results/win10-noaudio/
    tools/vm_run.sh --all --filter waveout_open   # every non-dx area, test-name filter
    tools/vm_matrix.sh                            # --all in hda and noaudio, status tables + failures
    tools/vm_matrix.sh --twice                    # same, twice per mode: flags FLAKY tests
    tools/wav_capture.sh playsound_memory         # boot with a wav backend and check announced tones (docs/VM_AUDIO.md)

## 1. The chain

1. **Build**: `make` builds every `tests/<area>/` into `build/<area>.exe` and the guest runner
   `tools/vmrun/vmrun.c` into `build/tools/vmrun.exe` (vmrun links winmm itself; it is a tool, not a test).
2. **Payload**: `<runs>/<tag>-<stamp>-<pid>-b<N>/payload/` holds the exes, `vmrun.exe` and a generated `run.cmd`
   (`--dry-run` prints it without booting):

       >COM1 echo @@I7 RUNSTART winmm-hda boot=1 audio=hda
       >COM1 ver
       vmrun.exe --audio-ready 90 --want-render 1
       vmrun.exe -t 60 -n core_smoke -- core_smoke.exe
       >COM1 echo @@I7 RUNEND winmm-hda boot=1

3. **Audio preamble** (`vmrun --audio-ready SECS --want-render N`): makes sure AudioEndpointBuilder and AudioSrv are
   RUNNING (starts them when STOPPED; on this image they are already running at logon), then polls the winmm counts
   (`waveOut/waveIn/midiOut/midiIn/mixer/aux GetNumDevs`) and the active mmdevapi render/capture endpoint counts every
   500 ms until they have been unchanged for 3 s and at least N render endpoints exist (N = 1 for the audio modes,
   0 for `noaudio`), within SECS (default 90; `--ready`). It prints one line and one line per device:

       @@I7 AUDIO ready=1 ms=3016 aeb=4 audiosrv=4 started=0 waveout=1 wavein=1 midiout=1 midiin=0 mixer=2 aux=0 render=1 capture=1 polls=7
       @@I7 AUDIODEV waveout 0 Speakers (High Definition Audio

   (`aeb`/`audiosrv` are SERVICE_* states, 4 = RUNNING; `started` bit 0/1 = vmrun started AEB/AudioSrv.) Even when
   readiness fails the exes still run: tests key on `GetNumDevs` themselves and the line records why counts are low.
   Rationale: winmm's first call waits for AudioSrv (up to about 122 s, docs/research/04 section 4); doing that wait
   once, outside the tests, keeps per-exe timeouts meaningful.
4. **Each exe** runs under `vmrun.exe -t T -n NAME -- NAME.exe [filter]`: a kill-on-close job object, stdout+stderr on
   one pipe forwarded to `\\.\COM1`, framed as

       @@I7 BEGIN <name> <cmdline>
       ...the exe's RUN/OK/FAIL/SKIP/NOTE/SUMMARY lines...
       @@I7 END <name> exit=0x<code> ms=<n> timeout=<0|1> bytes=<n>

   On timeout the job (exe and every child it spawned) is killed, `exit=0xDEAD0001`. Per-exe timeout: the largest
   `VM_TIMEOUT: N` comment in `tests/<area>/*.c`, else 120 s (`--timeout` overrides).
5. **Boot**: `W10_EXTRA=<audio devices> ORACLE_TIMEOUT=<sum of timeouts + 180 + ready> "$WIN10_RUN" PAYLOAD BOOTDIR`.
   Exes are packed into boots so that this stays under 840 s. The boot dir keeps `serial.log`, `run.log` and, for wav
   modes, `out.wav`.
6. **Split**: `tools/vm_split.py serial.log OUT --expect a,b --audio MODE` writes `OUT/<area>.txt` and appends a status
   line per area to `OUT/_vm_status.log`:

   | status | meaning |
   |---|---|
   | PASS | END frame, exit 0, last SUMMARY failed=0 and its test count equals the RUN lines |
   | FAIL | SUMMARY failed>0 (exit 1) |
   | TIMEOUT | vmrun killed the exe at its deadline |
   | CRASHED | END frame but no SUMMARY, or an exit code other than 0/1 |
   | TRUNCATED | BEGIN without END (guest died / ORACLE_TIMEOUT), fewer bytes captured than vmrun forwarded, or SUMMARY tests != RUN lines |
   | MISSING | an expected area never started |

   Non-PASS/FAIL files end with a `VMRUN <status> <detail>` line. The `@@I7 AUDIO*` lines go to `OUT/_audio.log`;
   each boot's raw log is copied to `OUT/_serial_b<N>.log`, a wav capture to `OUT/_capture_b<N>.wav` with its
   `tools/wavscan.py` summary in `_capture_b<N>.scan`.
7. **Exit code**: 0 only if every area is PASS; 1 otherwise; 2 for usage, build or lab errors.

`tools/summarize.py OUT --failures` aggregates any results dir (it only reads `*.txt`; the `_*` files are not `.txt`).

## 2. Options (tools/vm_run.sh)

`--all` (every `build/*.exe`; `dx_*` only with `--destructive`), `--destructive`, `--audio MODE` (below),
`--filter SUBSTR` (argv[1] of every exe), `--timeout S`, `--ready S`, `--out DIR` (default `results/win10` for `hda`,
`results/win10-<MODE>` otherwise), `--runs DIR` (default `build/vmruns`), `--tag NAME`, `--no-build`,
`--extra FILE` (copied into the payload, e.g. a data file; the payload is a read-only CD in the guest, so tests copy
data into their scratch dir), `--dry-run`.

Audio modes (QEMU devices, details and the device lists in docs/VM_AUDIO.md):

| mode | W10_EXTRA | guest sees |
|---|---|---|
| `hda` (default) | `-audiodev wav,id=snd0,path=BOOTDIR/out.wav -device intel-hda -device hda-duplex,audiodev=snd0` | 1 waveOut, 1 waveIn, 2 mixers |
| `hda-none` | same codec, `-audiodev none` | same as hda, nothing recorded on the host |
| `hda-out` | intel-hda + hda-output, wav | 1 waveOut, 0 waveIn, 1 mixer |
| `usb` | usb-audio, wav | 1 waveOut (render only) |
| `noaudio` | (none) | 0 waveOut / waveIn / mixer; MIDI mapper + GS synth still enumerated |

## 3. Rules for test writers that come from the runner

- Every area must PASS in both `hda` and `noaudio` (`tools/vm_matrix.sh`): key device paths on `GetNumDevs`.
- Put `VM_TIMEOUT: N` in a comment when an area needs more than 120 s; keep areas well under a few minutes.
- Output goes through a pipe to COM1: never wait for console input, never open UI that waits for a user.
- A test that plays a tone announces it with `t_note("TONE hz=%d ms=%d", ...)` right before playing it (in play order);
  `tools/wav_capture.sh` checks it on the host. In-guest assertions cannot see the capture.
- The guest clock runs about 1 h behind the host (`-rtc base=localtime`); do not compare guest and host timestamps.

## 4. Verified on this image

See docs/VM_AUDIO.md section 3 for the boots that proved the chain (core_smoke in `hda` and `noaudio`, the readiness
lines and the wav capture check).

## 5. Wine (plumbing only)

`tools/wine_run.sh build/<area>.exe` runs an exe under headless Wine to prove it builds, runs and prints a SUMMARY.
Wine is not an oracle: its values (for example `TIMECAPS.wPeriodMax` 0xFFFF instead of Windows' 1000000) never decide
an expectation.
