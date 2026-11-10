# winmm.dll conformance corpus

Black-box tests for Windows 10 x64 `winmm.dll`. The DLL is never linked: each test loads it with `LoadLibrary`,
resolves entry points with `GetProcAddress` (`U(Name)` gives a pointer typed from the SDK header;
`UIN("module.dll", Name)` pins one module, which matters for forwarders) and calls through the pointer.

Modules searched by `U()`: winmm.dll winmmbase.dll mmdevapi.dll user32.dll ntdll.dll kernel32.dll.

    make                         # cross-compile build/<area>.exe (mingw-w64)
    build\<area>.exe [substr]    # on Windows; optional test-name filter
    tools\run_all.bat            # everything, results\<area>.txt (add /destructive for dx_* areas)
    python tools\summarize.py results [--failures]

Output: `RUN/OK/FAIL/SKIP` per test, `  FAIL file:line expr (detail)` per failed check, `SUMMARY` last;
exit code 1 if any test failed. `NOTE` lines record observed, version-dependent behaviour.

Tests that need a second process re-launch the exe (`--child <name>`). Scratch files live in
`%TEMP%\winmmt_<pid>\` and are removed on exit. `tools/*_exports.txt` list the named exports.

Win10 VM (the oracle): `tools/vm_run.sh AREA[,AREA] [--audio hda|noaudio|...]`, `tools/vm_matrix.sh` (every area
in the default `hda` and the `noaudio` configuration), `tools/wav_capture.sh` (host check of played tones). See
`docs/VM_RUN.md` and `docs/VM_AUDIO.md`. Every area must pass with and without audio devices.

See `HANDOFF.md` (when present) for how to run and triage the Windows passes.
