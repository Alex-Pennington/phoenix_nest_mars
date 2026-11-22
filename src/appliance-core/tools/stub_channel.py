#!/usr/bin/env python3
"""Stand-in for the air between two stations' audio files.

  stub_channel.py --from OUTBOX_A --to CAPTURES_B [--from OUTBOX_B --to CAPTURES_A]
                  [--verdict MODEM_VERDICT_PY] [--once]

Moves each burst a station's core writes to its outbox into the other station's
captures directory as CAPTURE.wav with a CAPTURE.json sidecar, as greybeard-ale's
capture stage leaves it, and handled bursts to OUTBOX/sent/.

With --verdict, greybeard-ale's own receive step decodes it: modem_verdict.py
(tools/ in greybeard-ale) runs m110a_rx over the WAV and writes CAPTURE.m110a.N.bin
and CAPTURE.modems.json. That is the real 110A modem, on files; set GALE_M110A_RX
if m110a_rx is not where modem_verdict.py looks.

Without it, the burst must be one made by stub_gale_tx.py (one sample per byte),
and this writes the payload file and CAPTURE.modems.json itself, in
modem_verdict.py's format: {"verdicts": [{"waveform", "success", "payload_file",
"payload_bytes", "sha256"}]}. Standard library only.
"""
import subprocess
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


def move(outbox, captures, verdict=None):
    done = 0
    sent = os.path.join(outbox, "sent")
    os.makedirs(sent, exist_ok=True)
    os.makedirs(captures, exist_ok=True)
    for n in sorted(os.listdir(outbox)):
        if not n.endswith(".wav") or n.startswith("."):
            continue
        src = os.path.join(outbox, n)
        cap = "cap-" + n[:-4]
        wav = os.path.join(captures, cap + ".wav")
        shutil.copy(src, wav)
        with open(os.path.join(captures, cap + ".json"), "w") as f:
            json.dump({"capture": cap + ".wav", "source": "stub_channel"}, f)
        if verdict:
            r = subprocess.run([sys.executable, verdict, wav], capture_output=True, text=True, timeout=600)
            print(r.stdout.strip() or r.stderr.strip()[-300:], flush=True)
            shutil.move(src, os.path.join(sent, n))
            done += 1
            continue
        data = decode(src)
        binname = cap + ".m110a.1.bin"
        with open(os.path.join(captures, binname), "wb") as f:
            f.write(data)
        listing = {"capture": cap + ".wav",
                   "verdicts": [{"waveform": "110A", "success": 1, "eom": 1, "bytes": len(data),
                                 "payload_file": binname, "payload_bytes": len(data),
                                 "sha256": hashlib.sha256(data).hexdigest(), "payload_kind": "user data"}]}
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
    ap.add_argument("--verdict", help="greybeard-ale's tools/modem_verdict.py: decode with the real modem")
    ap.add_argument("--once", action="store_true")
    a = ap.parse_args()
    if len(a.src) != len(a.dst):
        print("give one --to for each --from", file=sys.stderr)
        return 2
    while True:
        for s, d in zip(a.src, a.dst):
            move(s, d, a.verdict)
        if a.once:
            return 0
        time.sleep(0.3)


if __name__ == "__main__":
    raise SystemExit(main())
