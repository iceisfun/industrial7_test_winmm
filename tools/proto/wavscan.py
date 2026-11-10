#!/usr/bin/env python3
"""Summarise a QEMU wav-audiodev capture: non-silent stretches and their dominant frequency.
   wavscan.py out.wav   (expects the audprobe tone plan: 440, 1000, 660, ~880 Hz)"""
import sys, wave
import numpy as np
w = wave.open(sys.argv[1], 'rb')
ch, sw, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
raw = w.readframes(n)
dt = {1: np.uint8, 2: np.int16, 4: np.int32}[sw]
a = np.frombuffer(raw[: len(raw) // (sw * ch) * sw * ch], dtype=dt).astype(np.float64)
if sw == 1: a -= 128
a = a.reshape(-1, ch); full = float(2 ** (8 * sw - 1))
print(f"file={sys.argv[1]} channels={ch} width={sw} rate={rate} frames={len(a)} seconds={len(a)/rate:.2f}")
mono = a.mean(axis=1) / full
print(f"overall peak={np.abs(mono).max():.4f} rms={np.sqrt((mono**2).mean()) if len(mono) else 0:.5f}")
win = rate // 20  # 50 ms windows
rows = []
for i in range(0, len(mono) - win, win):
    x = mono[i:i + win]; rms = float(np.sqrt((x ** 2).mean()))
    if rms < 1e-3: rows.append((i / rate, rms, None)); continue
    sp = np.abs(np.fft.rfft(x * np.hanning(len(x)))); f = float(np.argmax(sp[1:]) + 1) * rate / len(x)
    rows.append((i / rate, rms, f))
# merge consecutive windows with similar dominant frequency
segs = []
for t, rms, f in rows:
    key = None if f is None else round(f / 20) * 20
    if segs and segs[-1][2] == key: segs[-1][1] = t + win / rate; segs[-1][3].append(rms)
    else: segs.append([t, t + win / rate, key, [rms]])
for s, e, k, r in segs:
    if k is None: continue
    if e - s < 0.1: continue
    print(f"tone {s:7.2f}-{e:7.2f}s  ~{k:5d} Hz  rms={np.mean(r):.4f} ({20*np.log10(max(np.mean(r),1e-9)):.1f} dBFS)")
