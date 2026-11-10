#!/usr/bin/env python3
"""Split a Win10 VM serial.log (framed by build/tools/vmrun.exe) into OUTDIR/<area>.txt and validate every area.

usage: vm_split.py SERIAL_LOG OUTDIR [--expect a,b,c] [--audio MODE] [--quiet]

Frames written by vmrun.exe:
    @@I7 BEGIN <name> <cmdline>
    ...exe output verbatim...
    @@I7 END <name> exit=0x<code> ms=<n> timeout=<0|1> bytes=<n>
    @@I7 AUDIO ready=.. waveout=.. ...        (vmrun --audio-ready; copied to OUTDIR/_audio.log)
    @@I7 AUDIODEV <kind> <index> <name>       (same)

Status per area (appended to OUTDIR/_vm_status.log; exit code 1 unless every area is PASS):
    PASS       END present, exit=0, last SUMMARY has failed=0
    FAIL       SUMMARY failed>0 (exit=1)
    TIMEOUT    vmrun killed it at its deadline
    CRASHED    END present but no SUMMARY, or an exit code other than 0/1
    TRUNCATED  BEGIN without END (guest died / ORACLE_TIMEOUT), fewer bytes than vmrun forwarded, or a SUMMARY
               whose test count differs from the RUN lines seen
    MISSING    an expected area never started
"""
import os, re, sys

BEGIN = re.compile(rb"@@I7 BEGIN (\S+)")
END = re.compile(rb"@@I7 END (\S+) exit=0x([0-9a-fA-F]+) ms=(\d+) timeout=(\d) bytes=(-?\d+)")
SUMMARY = re.compile(r"^SUMMARY tests=(\d+) failed=(\d+) skipped=(\d+) checks_pass=(\d+) checks_fail=(\d+)", re.M)
RUNLINE = re.compile(r"^RUN  \S+", re.M)


def arg(a, k, d=None):
    return a[a.index(k) + 1] if k in a and a.index(k) + 1 < len(a) else d


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        print(__doc__)
        return 2
    log, out = a[0], a[1]
    expect = [x for x in (arg(a, "--expect") or "").split(",") if x]
    audio = arg(a, "--audio", "")
    quiet = "--quiet" in a
    data = open(log, "rb").read() if os.path.exists(log) else b""
    os.makedirs(out, exist_ok=True)

    aud = [ln for ln in data.replace(b"\r", b"").split(b"\n") if ln.startswith(b"@@I7 AUDIO")]
    if aud:
        with open(os.path.join(out, "_audio.log"), "ab") as f:
            f.write(("# audio=%s %s\n" % (audio, os.path.abspath(log))).encode() + b"\n".join(aud) + b"\n")

    areas, order, pos = {}, [], 0
    while True:
        m = BEGIN.search(data, pos)
        if not m:
            break
        name = m.group(1).decode("latin1")
        body_start = data.find(b"\n", m.end())
        body_start = len(data) if body_start < 0 else body_start + 1
        e = END.search(data, body_start)
        nb = BEGIN.search(data, body_start)
        if e and (not nb or e.start() < nb.start()) and e.group(1).decode("latin1") == name:
            body = data[body_start:e.start()]
            info = dict(exit=int(e.group(2), 16), ms=int(e.group(3)), timeout=int(e.group(4)), bytes=int(e.group(5)))
            pos = e.end()
        else:
            stop = nb.start() if nb else len(data)
            body, info, pos = data[body_start:stop], None, stop
        if name not in areas:
            order.append(name)
        areas[name] = (body, info)

    for name in expect:
        if name not in areas:
            order.append(name)
            areas[name] = (None, None)
    rc, lines = 0, []
    for name in order:
        body, info = areas[name]
        text = body.decode("utf-8", "replace").replace("\r\n", "\n") if body is not None else ""
        if text.endswith("\n\n"):       # vmrun writes "\r\n" before END
            text = text[:-1]
        sm = None
        for sm in SUMMARY.finditer(text):
            pass
        nrun = len(RUNLINE.findall(text))
        detail = ""
        if body is None:
            st = "MISSING"
        elif info is None:
            st, detail = "TRUNCATED", "no END frame (guest died or ORACLE_TIMEOUT)"
        elif info["timeout"]:
            st, detail = "TIMEOUT", "killed after %d ms" % info["ms"]
        elif info["bytes"] >= 0 and len(body.rstrip(b"\r\n")) + 4 < info["bytes"]:
            st, detail = "TRUNCATED", "captured %d of %d bytes" % (len(body), info["bytes"])
        elif not sm:
            st, detail = "CRASHED", "no SUMMARY (exit=0x%x)" % info["exit"]
        elif info["exit"] not in (0, 1):
            st, detail = "CRASHED", "exit=0x%x" % info["exit"]
        elif int(sm.group(1)) != nrun:
            st, detail = "TRUNCATED", "SUMMARY tests=%s but %d RUN lines" % (sm.group(1), nrun)
        elif int(sm.group(2)) > 0 or info["exit"] == 1:
            st = "FAIL"
        else:
            st = "PASS"
        if sm:
            detail = ("tests=%s failed=%s skipped=%s checks_fail=%s %s" % (sm.group(1), sm.group(2), sm.group(3), sm.group(5), detail)).strip()
        if info:
            detail += " ms=%d" % info["ms"]
        if audio:
            detail += " audio=%s" % audio
        if body is not None:
            with open(os.path.join(out, name + ".txt"), "w") as f:
                f.write(text)
                if st not in ("PASS", "FAIL"):
                    f.write("\nVMRUN %s %s\n" % (st, detail))
        lines.append("%-10s %-32s %s" % (st, name, detail))
        if st != "PASS":
            rc = 1
    with open(os.path.join(out, "_vm_status.log"), "a") as f:
        for ln in lines:
            f.write(ln + "\n")
    if not quiet:
        for ln in lines:
            print(ln)
    if not order:
        print("vm_split: no frames in %s" % log)
        return 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
