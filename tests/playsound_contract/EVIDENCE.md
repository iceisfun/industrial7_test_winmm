# playsound_contract: evidence

Exports: PlaySound (ordinal 10, also the unnamed ordinal 2), PlaySoundA (11), PlaySoundW (12), sndPlaySoundA (135),
sndPlaySoundW (136). Every call that names something passes SND_NODEFAULT and names only things that do not exist
or silent images this area builds (in memory, in its scratch directory, or as resources of a scratch copy of its own
exe). Every test ends with `PlaySound(NULL, NULL, 0)`.

Legend: **[D]** read in the winmm decomp (Win10 19041), **[VM]** observed on the Win10 22H2 guest in both audio
configurations (`hda`: 1 waveOut device; `noaudio`: 0), **[K]** general Win32 knowledge, only used for t_note.

## Files

| file | topic |
|---|---|
| ps.h, ps_common.c | entry-point dispatcher (A/W/PlaySound/ordinal 2/sndPlaySoundA/W), image builder, quiesce, sweep printing |
| flags.c | flag gate, single bits and all bit pairs for four never-playing name classes, stop request forms |
| memory.c | ASYNC header gate, RIFF length wrap, copy limits, sync parser accept/reject table, loop/NOSTOP |
| names.c | SND_ALIAS_ID, alias names, name length limits, file search via scratch files, ANSI conversion |
| resource.c | SND_RESOURCE against UpdateResource'd resources, hmod NULL through a child process |
| identity.c | ordinals and aliasing, cross-entry parity table, threads, consumer-shaped calls |

## Decomp-grounded facts [D], all hard-checked and [VM]

* `sndPlaySoundI`: `f & 0xFF80DF20` -> FALSE before anything else. The complement 0x007F20DF is the documented flags
  plus the undocumented 0x400000 (shell32 SHPlaySound passes it); 0x400000 is accepted.
* SND_NOSTOP with a current wave header (`lpWavHdr`) -> FALSE, before the NULL/empty checks: while an async loop
  plays, even `PlaySound(NULL, NULL, SND_NOSTOP)` fails and `("", SND_NOSTOP)` fails; with nothing current, NOSTOP
  is ignored. `("", NODEFAULT|NOSTOP)` is therefore a side-effect-free "is something playing" probe.
* psz NULL: stop (sndMessage(NULL) -> soundFree) -> TRUE; NULL + ASYNC posts the stop -> TRUE; NULL with
  ASYNC|MEMORY drops ASYNC. NULL with SND_RESOURCE runs FindResourceW(hmod, NULL, ..) -> FALSE.
* Empty name: TRUE right after the gate (no stop, no lookup), for any hmod and any valid word without the MEMORY bit.
* SND_ALIAS_ID: TransAlias maps the eight SND_ALIAS_SYSTEM* ids to winmm strings 3000..3007, other ids pass through;
  LoadStringW(winmm) miss -> FALSE before any post. Unknown ids are pre-checked with LoadStringW in the test.
* Name path: copied with StringCbCopyW into 256 WCHARs (>= 256 chars -> FALSE under NODEFAULT); alias lookup;
  SND_SYSTEM skips the file search (async posts -> TRUE even for unknown names; sync -> soundLoadFile fails -> FALSE).
  Rooted path that does not exist: ".WAV" appended if it fits and posted unchecked (async TRUE for length <= 255: from 252
  the extension no longer fits, the search runs, misses and falls through to the post as for relative names [VM];
  FALSE from 256 where the copy fails). Relative: GetFileAttributesW against the
  current directory, SearchPathW(NULL, name, ".WAV"), MediaPath; miss -> FALSE even for ASYNC.
* PlaySoundA: converts names (psz > 0xFFFF and not plain MEMORY, or RESOURCE) with CP_ACP into 256 WCHARs; 256+ chars
  -> FALSE; MEMORY pointers and MAKEINTRESOURCE ids pass unchanged. sndPlaySoundA = PlaySoundA(psz, NULL, f);
  sndPlaySoundW = sndPlaySoundI(psz, NULL, f). PlaySound/ordinal 2 is a jmp to PlaySoundA.
* ASYNC|MEMORY: 'RIFF' at 0 and 'WAVE' at 8 else FALSE; RIFF length + 8 wrapping below 8 (0xFFFFFFF8..FF) -> FALSE;
  size in [1, 0x1FFFFF] copied with LocalAlloc, larger posted with the caller's buffer.
* Sync MEMORY: soundInitWavHdr(image, 0xFFFFFFFF): rules in memory.c's header; rejected images -> FALSE in both
  audio configurations; accepted PCM -> TRUE only with a device. RIFF length is not consulted when fmt is the first
  chunk and data the next one. Chunk sizes are used raw (no pad byte): an odd chunk with its RIFF pad byte before
  fmt makes the parser lose sync -> FALSE.
* Files: soundLoadFile bounds the parse by the file size (RIFF length > file, data past EOF, < 12 bytes -> FALSE);
  opens with FILE_SHARE_READ|WRITE (a file held without sharing -> sync FALSE).
* SND_RESOURCE: FindResourceW "SOUND" first, then "WAVE"; hmod NULL searches the process exe, so sndPlaySound finds
  resources of the calling exe (child-process test).
* mmWndProc: a queued async play that sees a pending 0x3CD (posted by every sync call) is dropped; a sync stop is
  therefore a reliable quiesce.

## VM-observed only [VM]

* With 0 wave devices a sync play of a valid image returns FALSE at once (< 1 s); with the HDA device TRUE after about
  the image length. GetLastError is not checked.
* (filled in from the runs below)

## Notes only (t_note)

* The in-place fmt realignment of a sync image with an odd unpadded chunk before fmt (buffer mutation).
* Results of parser-accepted but unusual formats (cb 14, float, EXTENSIBLE, 24-bit, inconsistent avg below cb 18,
  zero-length data) depend on what the mapper opens.
* winmm string table entries 3000..3007 (names only; never played).

## Runs

(see below)
