#!/usr/bin/python3 -I
"""A stand-in for rigctld's TCP protocol, for tests of draws-setfreq.

    fake-rigctld.py PORTFILE LOGFILE

Listens on 127.0.0.1, on a free port it writes to PORTFILE. Logs every line
it receives to LOGFILE. Answers F (set frequency), f (read frequency) and m
(read mode) the way rigctld does; anything else gets RPRT -11 (not
available). FAKE_MODE sets the mode it reads back (default PKTUSB) and
FAKE_OFFSET is added to the frequency it reads back (default 0).
"""

import os
import socket
import sys
import threading

MODE = os.environ.get("FAKE_MODE", "PKTUSB")
OFFSET = int(os.environ.get("FAKE_OFFSET", "0"))


def serve(conn, log):
    freq = 0
    r = conn.makefile("r", encoding="ascii", newline="\n")
    for line in r:
        line = line.rstrip("\n")
        with open(log, "a") as f:
            f.write(line + "\n")
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "F" and len(parts) == 2 and parts[1].isdigit():
            freq = int(parts[1])
            conn.sendall(b"RPRT 0\n")
        elif parts == ["f"]:
            conn.sendall(f"{freq + OFFSET}\n".encode())
        elif parts == ["m"]:
            conn.sendall(f"{MODE}\n3000\n".encode())
        else:
            conn.sendall(b"RPRT -11\n")
    conn.close()


def main():
    portfile, log = sys.argv[1], sys.argv[2]
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", 0))
    s.listen(4)
    with open(portfile + ".tmp", "w") as f:
        f.write(str(s.getsockname()[1]))
    os.replace(portfile + ".tmp", portfile)
    while True:
        conn, _ = s.accept()
        threading.Thread(target=serve, args=(conn, log), daemon=True).start()


if __name__ == "__main__":
    main()
