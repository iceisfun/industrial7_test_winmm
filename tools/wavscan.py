#!/usr/bin/env python3
"""Analyse a QEMU wav-audiodev capture (what the guest rendered): non-silent segments with their dominant frequency,
duration and level, and optionally check a list of expected tones.

usage: wavscan.py CAPTURE.wav [--expect HZ:MS[,HZ:MS...]] [--json]

Segments are 50 ms windows merged while the dominant frequency stays in the same 20 Hz bin; windows below -60 dBFS
rms are silence. Positions are positions in the file, not wall-clock times (QEMU writes only while a guest stream
runs). With --expect every tone must match a distinct segment, in order: frequency within 3 % (and +-25 Hz) and
duration within 25 % (+-100 ms). Exit code 1 if an expected tone is missing or the file is unreadable.
"""
import json, math, sys, wave

try:
    import numpy as np
except ImportError:  # pragma: no cover
    print("wavscan: numpy required")
    sys.exit(2)


def load(path):
    w = wave.open(path, "rb")
    ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    if n == 0:  # header not finalised (qemu killed): take everything after the data chunk header
        blob = open(path, "rb").read()
        k = blob.find(b"data")
        raw = blob[k + 8:] if k >= 0 else b""
    dt = {1: np.uint8, 2: np.int16, 4: np.int32}[sw]
    a = np.frombuffer(raw[: len(raw) // (sw * ch) * sw * ch], dtype=dt).astype(np.float64)
    if sw == 1:
        a -= 128
    a = a.reshape(-1, ch) / float(2 ** (8 * sw - 1))
    return a.mean(axis=1), rate, ch, sw


def db(x):
    return 20 * math.log10(x) if x > 0 else -999.0


def segments(mono, rate):
    win = rate // 20
    segs = []
    for i in range(0, len(mono) - win + 1, win):
        x = mono[i:i + win]
        rms = float(np.sqrt((x ** 2).mean()))
        if rms < 1e-3:
            key = None
        else:
            sp = np.abs(np.fft.rfft(x * np.hanning(len(x))))
            k = int(np.argmax(sp[1:]) + 1)
            # parabolic interpolation of the peak bin
            if 1 <= k < len(sp) - 1:
                y0, y1, y2 = sp[k - 1], sp[k], sp[k + 1]
                d = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) else 0.0
            else:
                d = 0.0
            f = (k + d) * rate / len(x)
            key = round(f / 20) * 20
        t = i / rate
        if segs and segs[-1]["key"] == key:
            s = segs[-1]
            s["end"] = t + win / rate
            s["rms"].append(rms)
            if key is not None:
                s["f"].append(f)
        else:
            segs.append(dict(key=key, start=t, end=t + win / rate, rms=[rms], f=[f] if key is not None else []))
    out = []
    for s in segs:
        if s["key"] is None:
            continue
        out.append(dict(start=round(s["start"], 3), ms=int(round((s["end"] - s["start"]) * 1000)),
                        hz=round(float(np.median(s["f"])), 1), dbfs=round(db(float(np.sqrt(np.mean(np.square(s["rms"]))))), 1)))
    return out


def main():
    a = sys.argv[1:]
    if not a or a[0].startswith("-"):
        print(__doc__)
        return 2
    path = a[0]
    exp = []
    if "--expect" in a:
        for tok in a[a.index("--expect") + 1].split(","):
            if tok:
                hz, ms = tok.split(":")
                exp.append((float(hz), int(ms)))
    try:
        mono, rate, ch, sw = load(path)
    except Exception as e:  # noqa: BLE001
        print("wavscan: cannot read %s: %s" % (path, e))
        return 1
    segs = segments(mono, rate)
    pk = float(np.abs(mono).max()) if len(mono) else 0.0
    rms = float(np.sqrt((mono ** 2).mean())) if len(mono) else 0.0
    print("file=%s channels=%d width=%d rate=%d seconds=%.2f peak_dbfs=%.1f rms_dbfs=%.1f segments=%d"
          % (path, ch, sw, rate, len(mono) / rate if rate else 0, db(pk), db(rms), len(segs)))
    for s in segs:
        print("SEG start=%.2fs ms=%d hz=%.1f dbfs=%.1f" % (s["start"], s["ms"], s["hz"], s["dbfs"]))
    rc, j = 0, 0
    for hz, ms in exp:
        hit = None
        while j < len(segs):
            s = segs[j]
            j += 1
            if abs(s["hz"] - hz) <= max(25.0, 0.03 * hz) and abs(s["ms"] - ms) <= max(100, 0.25 * ms):
                hit = s
                break
        if hit:
            print("TONE OK hz=%g ms=%d -> start=%.2fs ms=%d hz=%.1f dbfs=%.1f" % (hz, ms, hit["start"], hit["ms"], hit["hz"], hit["dbfs"]))
        else:
            print("TONE MISSING hz=%g ms=%d" % (hz, ms))
            rc = 1
    if "--json" in a:
        print(json.dumps(segs))
    return rc


if __name__ == "__main__":
    sys.exit(main())
