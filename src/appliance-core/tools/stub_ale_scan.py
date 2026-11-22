#!/usr/bin/env python3
"""A stand-in for greybeard-ale's scanner control socket, for testing the core.

The real one is ale-scan --control PATH (greybeard-ale src/control_socket.h):
one text command per line, one JSON line back. This stand-in answers "status"
in ale-scan's shape, minus its channel list (it has no channels and no
frequencies), plus the per-station "links" field that the core reads.
greybeard-ale has no link control yet; "links" is the core's proposal to it,
not greybeard-ale's interface.

Stand-in only, to drive a test by hand or from a script:
    link STATION STATE      set a station's link: linked, calling, down, ...
    unlink STATION          forget it
    clock wwv|host          the clock source status reports
ale-scan's own hold, release, record and plan are refused here.

    tools/stub_ale_scan.py --socket /tmp/ale-scan.sock [--link B=linked ...]
    echo "link B linked" | socat - UNIX-CONNECT:/tmp/ale-scan.sock
"""
import argparse
import json
import os
import socket
import threading
import time


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--socket", required=True)
    ap.add_argument("--link", action="append", default=[], help="STATION=STATE at start (repeatable)")
    ap.add_argument("--plan", default="stand-in")
    a = ap.parse_args()

    lock = threading.Lock()
    links = {}
    for item in a.link:
        st, _, state = item.partition("=")
        links[st.upper()] = {"station": st.upper(), "state": state or "linked", "since": int(time.time())}
    clock = ["host"]
    visits = [0]

    def status():
        visits[0] += 1
        return {
            "ok": True, "state": "scanning", "plan": a.plan, "windows": 1, "window": 0,
            "visit": 0, "visit_s": 2.0, "visits": visits[0], "stream_unix": time.time(),
            "hold": None, "order_pending": False, "plan_pending": "", "engine": False,
            "visit_remote": False, "clock": clock[0], "clock_counter": 0, "wwv_blocks": 0,
            "last_wwv_unix": 0, "plan_switches": 0, "note": "stand-in, not ale-scan",
            "links": sorted(links.values(), key=lambda l: l["station"]),
        }

    def answer(line):
        w = line.split()
        if not w:
            return {"ok": False, "error": "empty command"}
        with lock:
            if w[0] == "status":
                return status()
            if w[0] == "link" and len(w) == 3:
                st = w[1].upper()
                links[st] = {"station": st, "state": w[2], "since": int(time.time())}
                return {"ok": True, "link": links[st]}
            if w[0] == "unlink" and len(w) == 2:
                links.pop(w[1].upper(), None)
                return {"ok": True}
            if w[0] == "clock" and len(w) == 2 and w[1] in ("wwv", "host"):
                clock[0] = w[1]
                return {"ok": True}
        return {"ok": False, "error": "not in the stand-in: " + w[0]}

    def serve(conn):
        with conn:
            f = conn.makefile("rwb")
            for raw in f:
                f.write((json.dumps(answer(raw.decode(errors="replace").strip())) + "\n").encode())
                f.flush()

    if os.path.exists(a.socket):
        os.unlink(a.socket)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind(a.socket)
    os.chmod(a.socket, 0o600)
    s.listen(8)
    print("stand-in scanner on", a.socket, flush=True)
    try:
        while True:
            c, _ = s.accept()
            threading.Thread(target=serve, args=(c,), daemon=True).start()
    except KeyboardInterrupt:
        pass
    finally:
        os.unlink(a.socket)


if __name__ == "__main__":
    main()
