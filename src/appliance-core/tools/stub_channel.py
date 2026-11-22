#!/usr/bin/env python3
"""Stand-in for the air and greybeard-ale's receiver, until both exist.

  stub_channel.py --from OUTBOX_A --to CAPTURES_B [--from OUTBOX_B --to CAPTURES_A] [--once]

Moves each burst a station's core writes to its outbox (made by stub_gale_tx.py)
into the other station's captures directory the way greybeard-ale was described
to write a decoded burst: CAPTURE.wav, CAPTURE.m110a.0.bin and CAPTURE.modems.json
with each payload file's byte count and sha256. Handled bursts are moved to
OUTBOX/sent/. The JSON field names (files, file, bytes, sha256) are a guess until
greybeard-ale documents them. Standard library only.
"""
import argparse
import hashlib
import json
import os
import shutil
import struct
import sys
import time
import wave


def decode(path):
    with wave.open(path, "rb") as w:
        raw = w.readframes(w.getnframes())
    s = struct.unpack(f"<{len(raw) // 2}h", raw)
    return bytes((v - 1000) // 100 for v in s if v)


def move(outbox, captures):
    done = 0
    sent = os.path.join(outbox, "sent")
    os.makedirs(sent, exist_ok=True)
    os.makedirs(captures, exist_ok=True)
    for n in sorted(os.listdir(outbox)):
        if not n.endswith(".wav") or n.startswith("."):
            continue
        src = os.path.join(outbox, n)
        data = decode(src)
        cap = "cap-" + n[:-4]
        shutil.copy(src, os.path.join(captures, cap + ".wav"))
        binname = cap + ".m110a.0.bin"
        with open(os.path.join(captures, binname), "wb") as f:
            f.write(data)
        listing = {"files": [{"file": binname, "waveform": "110a", "bytes": len(data),
                              "sha256": hashlib.sha256(data).hexdigest()}]}
        tmp = os.path.join(captures, "." + cap + ".tmp")
        with open(tmp, "w") as f:
            json.dump(listing, f)
        os.rename(tmp, os.path.join(captures, cap + ".modems.json"))
        shutil.move(src, os.path.join(sent, n))
        done += 1
    return done


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="src", action="append", required=True)
    ap.add_argument("--to", dest="dst", action="append", required=True)
    ap.add_argument("--once", action="store_true")
    a = ap.parse_args()
    if len(a.src) != len(a.dst):
        print("give one --to for each --from", file=sys.stderr)
        return 2
    while True:
        for s, d in zip(a.src, a.dst):
            move(s, d)
        if a.once:
            return 0
        time.sleep(0.3)


if __name__ == "__main__":
    raise SystemExit(main())
