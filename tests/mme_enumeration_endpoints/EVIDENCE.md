# mme_enumeration_endpoints: evidence

Area: device enumeration for every MME family, waveOutGetDevCaps A/W, and how winmm device ids map to mmdevapi
endpoints. Primary exports: waveOutGetNumDevs, waveInGetNumDevs, midiOutGetNumDevs, midiInGetNumDevs,
mixerGetNumDevs, auxGetNumDevs, joyGetNumDevs, waveOutGetDevCapsA, waveOutGetDevCapsW.

Files: `counts.c` (counts, invariants, threads, fresh-process races), `waveout_caps.c` (caps ladder, size protocol,
handles, 64-bit ids), `endpoints.c` (mmdevapi cross-check, DRV_* id messages, mapper preferred ids, mmdevapi
ordinals), `families.c` (MIDI mapper / GS synth, midiIn, mixer, aux, waveIn special ids), `ee.c`/`ee.h` (helpers:
family tables, COM through ole32 loaded at run time).

Markers: **[decomp]** = read in the Win10 19041 reference decompilation (winmm / winmmbase); **[VM]** = observed on
the local Win10 22H2 guest (build 19045.2965) in the configuration named; **[knowledge]** = SDK/documented contract,
held as a CHECK only where the VM agreed.

## 1. Counts

| fact | source |
|---|---|
| winmm!*GetNumDevs (except joy) are jmp stubs into winmmbase; the body runs ClientUpdatePnpInfo then returns the cached counter under NumDevsCritSec | [decomp] winmm!0xa760, winmmbase!0xfce0 / 0x99e0 / 0x58e0 / 0x7710 |
| joyGetNumDevs returns the constant 16 (`return 0x10`), not exported by winmmbase | [decomp] winmm!0x6330; [VM] all modes |
| first call in a process waits for AudioSrv and builds the lists; racing first callers block on hEventApiInit | [decomp] ClientUpdatePnpInfo; [VM] 4-thread first-call child agrees |
| counts: noaudio 0/0/1/0/0/0/16, hda 1/1/1/0/2/0/16, hda-out 1/0/1/0/1/0/16 (waveOut/waveIn/midiOut/midiIn/mixer/aux/joy) | [VM] |
| invariants checked instead of numbers: midiOut >= 1, joy == 16, mixer == waveOut + waveIn, count == number of ids whose GetDevCaps succeeds (n -> 2) for all six families, A and W | [VM] all modes |
| winmm and winmmbase exports return the same counts; values stable over 100 calls and 4/8 threads | [VM] |
| WODM_GETNUMDEVS sent by id reaches wdmaud with dwUser 0 and returns a driver-internal value (0x60000 on hda): recorded only | [VM] |

## 2. waveOutGetDevCaps

| fact | source |
|---|---|
| order: cb 0 -> 0 (pointer not looked at, nothing written); NULL -> 11 for any id; then id | [decomp] !0xfe00 / !0xffa0; [VM] |
| only the exact value 0xFFFF is rewritten to WAVE_MAPPER; 0x1_0000FFFF -> 2 | [decomp]; [VM] |
| id path masks the id to 32 bits: 0x1_00000000 = id 0, 0x1_FFFFFFFF and (UINT_PTR)-1 = mapper | [decomp] `uDeviceID & 0xffffffff`; [VM] |
| out of range -> 2 and the buffer is untouched (A and W) | [decomp]; [VM] |
| mapper caps succeed iff waveOut + waveIn > 0 (WaveMapperInit) else 2; "Microsoft Sound Mapper", wMid 1, wPid 2, vDriverVersion 0x500, dwFormats 0xFFFFF, wChannels 2, dwSupport 0x2E | [decomp] waveReferenceDriverById; [VM] |
| A form: zeroed 0x84-byte WAVEOUTCAPS2W internally, name via Iwcstombs(32), copies min(cb, 100); a short cb is success with a raw prefix; bytes past 100 untouched | [decomp]; [VM] sweep cb 1..160 |
| W form, wdmaud device: cb < 84 -> 11; cb >= 84 -> 0, WAVEOUTCAPSW, guids only when cb >= 132, bytes to cb zeroed. On the 11 path wdmaud still wrote ~81 bytes into the buffer (do not pass a short buffer) | [VM] |
| W form, mapper (msacm32): cb < 84 -> 11, nothing written; cb >= 84 -> exactly 84 bytes (no CAPS2 part) | [VM] |
| device 0 on hda: "Speakers (High Definition Audio" (31-char truncation of the endpoint name), wMid 1, wPid 65535, formats 0xFFFFF, 2 ch, support 0x2E: recorded | [VM] |
| an open HWAVEOUT is accepted in place of the id (same bytes); a mapper handle reports the mapper's caps; a closed handle, an HWAVEIN, HMIDIOUT or HMIXER is an out-of-range id (2) | [decomp] ValidateHandle; [VM] |

## 3. Endpoints (mmdevapi)

| fact | source |
|---|---|
| active render count == waveOutGetNumDevs, capture == waveIn, eAll == mixer | [VM] all modes |
| no endpoint: GetDefaultAudioEndpoint -> 0x80070490 with *ppDevice NULL, every role | [VM] noaudio |
| DRV_QUERYFUNCTIONINSTANCEID (0x811, dw2 = bytes) returns the IMMDevice id ("{0.0.0.00000000}.{guid}" render, "{0.0.1.00000000}.{guid}" capture); id 0 == default eConsole/eMultimedia endpoint | [decomp] mregHandleInternalMessages; [VM] |
| FIID protocol: NULL -> 11; any copy failure (size 0, 1, short) -> 21 with StringCchCopyW truncation; SIZE (0x812) = (len+1)*2; mapper SIZE 2 and "" | [decomp]; [VM] |
| DRV_QUERYDEVICEINTERFACE[SIZE]: \\?\hdaudio#... path; short -> 21, size 0/1 -> 1 (STRSAFE_E_INVALID_PARAMETER is not mapped to MOREDATA here); mapper SIZE 2 | [decomp]; [VM] |
| DRV_QUERYSTRINGID[SIZE] / DRV_QUERYIDFROMSTRINGID round-trip to the same id, through any valid id; unknown string -> 11 | [decomp]; [VM] |
| DRVM_MAPPER_PREFERRED_GET / CONSOLEVOICECOM_GET: mapper only (device id -> 11), NULL args -> 11, id 0 / flags 0 on hda, matches default console / communications endpoint | [decomp]; [VM] |
| 0x801..0x804, 0x806 -> 8; DRV_QUERYMODULE -> wdmaud.drv (devices) / msacm32.drv (mapper); DRV_RESERVED+8 -> per-driver count 1; DRV_QUERYMAPPABLE -> 0 | [decomp]; [VM] |
| the same messages on an OPEN handle are not intercepted (driver returns 8) | [decomp] waveMessage; [VM] |
| mmdevapi exports ordinals 2..36, ordinal 1 and 37 absent, 17 = ActivateAudioInterfaceAsync, 33..36 Dll*; never called | export table; [VM] |
| one mixer per endpoint: render mixers first (waveOut order) then capture, names = endpoint names truncated to 31 | [VM] hda |

## 4. Other families

| fact | source |
|---|---|
| midiOut MIDI_MAPPER: "Microsoft MIDI Mapper", MOD_MAPPER, dwSupport 0x9 (wMid 1, wPid 1, ver 0x500); present with no audio | [VM] |
| midiOut 0: "Microsoft GS Wavetable Synth", MOD_SWSYNTH, 32 voices (wPid 27, ver 0x100, 32 notes, mask 0xFFFF, support 0x1) | [VM] |
| midi ids compared as 64-bit: (UINT_PTR)-1, 0xFFFF, 0x1_00000000 -> 2; only 0xFFFFFFFF is the mapper | [decomp] midiIDMessage; [VM] |
| waveInGetDevCaps has no 0xFFFF rewrite (-> 2); id masked to 32 bits like waveOut | [decomp] !0x11000; [VM] |
| mixer: no mapper (-1 -> 2), id truncated to 32 bits, cDestinations 1 per mixer | [decomp] mixerGetDevCapsW; [VM] |
| aux: 0 devices on every configuration; -1 and 0 -> 2 | [VM] |

## 5. VM runs

See the run log at the end of this file.
