# wave_formats: evidence

Area: format negotiation for waveOut and waveIn, using `WAVE_FORMAT_QUERY` (no handle is created) and real
open+close (CALLBACK_NULL, nothing played or recorded). Each cell is checked on these targets: `WAVE_MAPPER`, the
16-bit alias `0xFFFF`, device 0, `WAVE_FORMAT_DIRECT` variants, and device 0 with `WAVE_MAPPED`.

Files: `wf.h` / `wf_util.c` (format builders, gate, cell judge), `contract.c` (the open layer, which runs before
the driver), `pcm_matrix.c` (plain PCM grid and off-grid values), `extensible.c` (WAVE_FORMAT_EXTENSIBLE),
`tags.c` (IEEE float, ACM codec tags, unknown tags), `open_query.c` (real open compared with the query).

Oracle runs: the Win10 22H2 VM (build 19045.2965) through `tools/vm_run.sh`, in three audio modes:
- `hda`: 1 waveOut, 1 waveIn.
- `hda-out`: 1 waveOut, 0 waveIn.
- `noaudio`: 0 / 0.

The `hda-out` render cells were byte-identical to the `hda` render cells.

Legend: **[D]** = read in the winmmbase decomp, **[VM]** = observed on the Win10 VM and pinned with CHECK,
**[K]** = background knowledge, used only to choose the cases (never pinned without [D] or [VM]).

## 1. Open layer, before any driver (contract.c): exact on every guest

Source: winmmbase `waveOutOpen` (RVA 0x103b0) and `waveInOpen` (RVA 0x11290). The two have the same body with the
waveIn driver list. **[D]**, confirmed **[VM]** in all three modes.

1. `pwfx == NULL` -> 11, whatever the flags or the id (it wins over bad flags).
2. `CALLBACK_WINDOW` with a non-NULL `dwCallback` that `IsWindow` rejects -> 11. This also applies to a query, and
   it is checked before flags and before the device count. A NULL window is accepted. Other callback types are not
   validated at this layer: an event, a thread id, a NULL function and the undefined types 4, 6 and 7 all give
   the plain verdict with QUERY.
3. `0xFFFF` is rewritten to `WAVE_MAPPER`. The flag mask is `0x7E04` for the mapper and `0x7E10` for a specific
   id, and a hit returns 10:
   - `WAVE_MAPPED` with the mapper -> 10.
   - `0x10` with an id -> 10.
   - Bits 0x200..0x4000 -> 10 everywhere, also with a bad format and an out-of-range id.
   - `WAVE_ALLOWSYNC`, `WAVE_FORMAT_DIRECT`, 0x20/0x40/0x80/0x100/0x8000, and 0x10 with the mapper all pass this
     layer. **[VM]** the HDA driver accepts each of them on core PCM (0).
4. With QUERY, `phwo` is neither checked nor written. NULL is fine, and a sentinel survives every result (0, 2 and
   32). Without QUERY: `phwo == NULL` -> 11 before the device count, else `*phwo = NULL` before the device check,
   so a failed open leaves NULL.
5. No device in the direction -> 2 for every id and every format, the mapper included: the device check comes
   before the format check. An id >= count -> 2. **[VM]** this includes ids 0x7FFF, 0xFFFE, 0x10000, 0x7FFFFFFF
   and 0xFFFFFFFE.
6. `WAVE_MAPPED` with a specific id resolves the mapper driver and passes it the id **[D]**. Results **[VM]**:
   - id 0 -> the mapper's verdict, so ACM conversion applies.
   - id n and id n+50 -> 2 from the mapper.
7. **[VM]** On the render-only guest, `waveInGetDevCaps(WAVE_MAPPER)` = 0 while every waveIn mapper query = 2.
   The mapper driver loads when waveOut + waveIn != 0 **[D]** (`WaveMapperInit`), so tests must key on
   `*GetNumDevs`.
8. **[VM]** 64 consecutive queries leave nothing behind (a real open still works). A query also answers while a
   real stream is open on the same device.

## 2. Plain PCM (pcm_matrix.c) [VM]

- Grid {8000, 11025, 22050, 44100, 48000, 96000, 192000} x {8, 16, 24, 32} bits x {1, 2} ch: every cell is 0 on
  the mapper, on id 0, with and without DIRECT, for both out and in. Only the 8/16-bit 8k..48k core is CHECKed.
  The 24/32-bit and 96k/192k cells come from the shared-mode engine of the endpoint, so they produce a note only
  when they are not 0.
- More than 2 channels (3, 4, 5, 6, 7, 8, 16, 255) -> 32 on every target, DIRECT included.
- Any non-zero rate from 1 Hz to 1 MHz -> 0, DIRECT included (the engine resamples).
- `wBitsPerSample` that is not a multiple of 8 (1, 4, 7, 12, 20) -> 32. Whole bytes beyond 32 bits (40, 48, 64)
  -> 0.
- `nChannels` 0, `nSamplesPerSec` 0, `wBitsPerSample` 0 or 12, all zero -> 32 on the query path.
- `nBlockAlign` 0, 1, 2, 3, 8 and 0xFFFF for 2ch/16 -> 0 on the query path. Inconsistent `nBlockAlign` is not
  validated by QUERY.
- `nAvgBytesPerSec` 0, 1, 44100, 176401 and 0xFFFFFFFF -> 0. For PCM `cbSize` 1, 2, 22 and 100 -> 0 (ignored).

## 3. WAVE_FORMAT_EXTENSIBLE (extensible.c) [VM]

Targets: mapper, mapper|DIRECT, id 0, id 0|DIRECT. waveOut and waveIn give the same results.

- These all give 0 on every target:
  - SubFormat PCM 8/16/24/32 with valid bits equal to the container, or 24-in-32, 20-in-24 and 12-in-16.
  - FLOAT 16/32/64, and FLOAT 32 with 24 valid bits.
  - 1, 2, 4, 6 (0x3F and 0x60F) and 8 (0x63F) channels.
  - Channel mask 0, 7 (popcount != channels), 1, or 0xFFFFFFFF.
  - PCM with `nBlockAlign` +1 or 0, or with `nAvgBytesPerSec` 0 or +1.
  - `cbSize` 23 or 40.
- These all give 32 on every target:
  - `wValidBitsPerSample` 0, or larger than the container (24 in 16).
  - `nChannels` 0 or `nSamplesPerSec` 0.
  - **FLOAT with `nAvgBytesPerSec` 0**. PCM with the same field is accepted.
  - Every SubFormat other than PCM and IEEE_FLOAT: a-law, mu-law, ADPCM, DRM, or an unknown GUID.
- `cbSize` < 22 (0, 10, 21; PCM and FLOAT) gives three different codes:
  - id 0 -> **11**.
  - mapper|DIRECT -> **11** (the device's code is passed through).
  - plain mapper -> **32** (the mapper then tries ACM and fails).
- The engine mix format (EXTENSIBLE float32 2 ch 48 kHz, mask 3) -> 0 on the mapper, 0xFFFF and id 0.
- EXTENSIBLE PCM 16/2 gets the same result as the plain PCM tag at every core rate.

## 4. Other tags (tags.c) [VM]

Targets: mapper, mapper|DIRECT, id 0, id 0|DIRECT, id 0|MAPPED.

- **IEEE float (tag 3)**:
  - 0 on every target, DIRECT and the device id included, for 16/24/32/64-bit samples, 1-2 ch, and 8k..96k.
  - 6 ch, 0 ch and 0 Hz -> 32.
- **ACM codecs** (MS ADPCM, IMA ADPCM, GSM 6.10, mu-law, a-law; mono and stereo at 8k..44.1k):
  - Mapper -> 0, and id 0|MAPPED -> 0.
  - mapper|DIRECT, id 0 and id 0|DIRECT -> 32. Only the mapper converts.
  - **MPEG layer 3 (0x55)**: 0 for waveOut through the mapper, but 32 for waveIn. The inbox codec only decodes.
- Codec formats with damaged extra bytes -> 32 on every target. Cases: ADPCM cbSize 0, wNumCoef 0,
  wSamplesPerBlock 0, nBlockAlign 0 or bits 8; IMA cbSize 0, spb 0 or bits 3; GSM spb 0 or block 64; mu-law 16-bit;
  a-law 0 ch.
- Unregistered or unsupported tags -> 32 on every target, the mapper included: 0x0000, 0x0004, 0x0050, 0x0092,
  0x00FF, 0x0161, 0x1234 (with or without extra bytes), 0x1610, 0x2000, 0x8000, 0xFFFF.

## 5. Real open compared with the query (open_query.c) [VM]

- For these formats the query result equals the real-open result on the mapper, on id 0 and on id 0|DIRECT:
  - PCM 16/2/44.1k, 8/1/8k and 16/1/22k.
  - Float 2/48k.
  - EXT PCM16 and EXT float.
  - MS ADPCM, IMA ADPCM and mu-law (0 on the mapper, 32 on the device).
  - Tag 0x1234 (32).
- **Exception: plain 6-channel PCM**. The query gives 32 everywhere, and a real open through the mapper gives 32,
  but **a real open on device 0 gives 11** (with or without DIRECT).
- The real open validates header fields that the query ignores. On device 0, these opens give **11**: `nBlockAlign`
  3 for 2ch/16, `nSamplesPerSec` 0, `nChannels` 0, and `wBitsPerSample` 12 or 0. Through the mapper the same opens
  give **32**. `nAvgBytesPerSec` 0 opens fine on both (0).
- A mapper open of IMA ADPCM succeeds (an ACM stream) in both directions, and `waveOut/InGetID` on it returns
  `WAVE_MAPPER` (0xFFFFFFFF), the id stored at open **[D]**. A device-0 open returns id 0.
- Two concurrent streams in different formats on device 0 both open (shared mode), out and in.
- `wf_*_open_odd_direct_and_ext` records the mapper|DIRECT and EXTENSIBLE real-open cells as notes (section 6).

## 6. Recorded only (t_note, not CHECKed)

- PCM grid cells outside the core (24/32-bit, 96k/192k). They are endpoint-engine properties. On this VM all are 0.
- `pcm24/2/96000` in open_query.c, for the same reason.
- Real-open cells in `wf_*_open_odd_direct_and_ext`: mapper|DIRECT with bad headers, EXTENSIBLE cbSize 0, block
  align 5, float avg 0, 5.1, float64, and 1 MHz.

## 7. What drives the verdicts (for an implementation)

- winmm itself only does section 1 **[D]**. Everything about the format is the driver's job: wdmaud for an id, or
  the msacm wave mapper for `WAVE_MAPPER` / `WAVE_MAPPED`.
- The device layer behaves like a shared-mode engine with automatic PCM conversion **[VM]**:
  - It accepts any rate and any whole-byte PCM container, float 16/32/64, and any EXTENSIBLE channel layout.
  - It rejects plain PCM above 2 ch, non-byte depths and zero fields with 32, or with 11 for a real open.
  - It ignores `nBlockAlign` / `nAvgBytesPerSec` on the query path, except float EXTENSIBLE `nAvgBytesPerSec` 0.
  - It rejects a short EXTENSIBLE `cbSize` with 11.
- The mapper (when DIRECT is not set) adds ACM codecs and turns a device failure into 32 when no codec matches.
  With DIRECT it passes the device verdict through.
