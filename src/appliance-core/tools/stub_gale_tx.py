#!/usr/bin/env python3
"""Stand-in for greybeard-ale's gale-tx, until the real one lands.

  stub_gale_tx.py --waveform 110a --mode MODE --in PAYLOAD.bin --out BURST.wav

Same command line and outputs as gale-tx was described (8 kHz 16-bit mono WAV,
one-line JSON summary on stdout), but the audio is NOT a modem signal: each
payload byte is one sample (1000 + 100 * byte), between 0.2 s of silence before
and after, so tools/stub_channel.py can read it back. It exists only so the core
can be tested end to end on files. Standard library only.
"""
import argparse
import hashlib
import json
import struct
import sys
import wave

RATE = 8000
MODES = {f"{r}{l}" for r in (75, 150, 300, 600, 1200, 2400, 4800) for l in "SL"} | {"600H", "1200H"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--waveform", required=True)
    ap.add_argument("--mode", required=True)
    ap.add_argument("--in", dest="inp", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    if a.waveform != "110a":
        print("stub_gale_tx: only 110a", file=sys.stderr)
        return 2
    if a.mode.upper() not in MODES:
        print(f"stub_gale_tx: unknown mode {a.mode}", file=sys.stderr)
        return 2
    data = open(a.inp, "rb").read()
    if not data:
        print("stub_gale_tx: empty payload", file=sys.stderr)
        return 2
    pad = [0] * (RATE // 5)
    samples = pad + [1000 + 100 * b for b in data] + pad
    with wave.open(a.out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    print(json.dumps({"ok": True, "stub": True, "waveform": "110a", "mode": a.mode.upper(), "bytes": len(data),
                      "samples": len(samples), "seconds": round(len(samples) / RATE, 3),
                      "sha256": hashlib.sha256(data).hexdigest()}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
