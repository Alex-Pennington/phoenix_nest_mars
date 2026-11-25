#!/usr/bin/python3 -I
"""Tests draws-findcat with no hardware, using pseudo-terminals as fake ports.

Fake ports: a radio that answers only at its own CAT RATE with 8N2, a silent
device, an echo device and one that answers garbage. Checks: the radio is
found on whichever port it sits; a rate mismatch finds nothing and sends at
one rate only; every byte any fake port received was the read-only query
00 00 00 00 03; --write updates RIG_DEVICE and RIG_BAUD and nothing else;
two radios refuse to pick; a busy port is refused; --list opens nothing.
"""

import os
import pty
import select
import subprocess
import sys
import tempfile
import termios
import threading
import time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FINDCAT = os.path.join(HERE, "bin", "draws-findcat")
QUERY = bytes([0, 0, 0, 0, 3])
SPEED = {4800: termios.B4800, 9600: termios.B9600, 38400: termios.B38400}


class FakePort:
    """One pseudo-terminal. kind: radio, silent, echo, garbage."""

    def __init__(self, kind, baud=38400):
        self.kind, self.baud = kind, baud
        self.master, slave = pty.openpty()
        self.path = os.ttyname(slave)
        os.close(slave)  # the probe opens it by path, like a real port
        self.received = b""
        self.rates_seen = set()
        self.stop = False
        self.t = threading.Thread(target=self.run, daemon=True)
        self.t.start()

    def run(self):
        pending = b""
        while not self.stop:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if not r:
                continue
            try:
                data = os.read(self.master, 64)
            except OSError:
                time.sleep(0.01)  # no one has the port open
                continue
            attrs = termios.tcgetattr(self.master)  # the slave side's settings
            self.rates_seen.add(attrs[5])
            self.received += data
            pending += data
            while len(pending) >= 5:
                frame, pending = pending[:5], pending[5:]
                if self.kind == "echo":
                    os.write(self.master, frame)
                elif self.kind == "garbage":
                    os.write(self.master, b"\xff\xfe\x13\x37\x00")
                elif self.kind == "radio":
                    right_rate = attrs[5] == SPEED[self.baud]
                    two_stop = bool(attrs[2] & termios.CSTOPB)
                    if frame == QUERY and right_rate and two_stop:
                        os.write(self.master, bytes([0x01, 0x00, 0x00, 0x00, 0x01]))

    def close(self):
        self.stop = True
        self.t.join(1)
        os.close(self.master)


def run(env_file, byid, *args):
    env = dict(os.environ, DRAWS_ENV=env_file,
               DRAWS_SERIAL_GLOB=os.path.join(byid, "*"), DRAWS_CAT_TIMEOUT="0.3")
    p = subprocess.run([FINDCAT, *args], env=env, capture_output=True, text=True,
                       timeout=30)
    return p.returncode, p.stdout + p.stderr


def fail(msg, out=""):
    print(f"FAIL: {msg}\n{out}")
    sys.exit(1)


def ok(msg):
    print(f"ok   {msg}")


def main():
    tmp = tempfile.mkdtemp()
    env_file = os.path.join(tmp, "station.env")
    with open(os.path.join(HERE, "config", "station.env")) as f:
        original = f.read()
    with open(env_file, "w") as f:
        f.write(original)

    def setup(ports):
        byid = tempfile.mkdtemp(dir=tmp)
        for name, port in ports.items():
            os.symlink(port.path, os.path.join(byid, name))
        return byid

    all_ports = []

    def make(kind, baud=38400):
        p = FakePort(kind, baud)
        all_ports.append(p)
        return p

    # 1. --list opens nothing.
    radio = make("radio")
    byid = setup({"usb-Silent_Thing-if00": make("silent"),
                  "usb-FTDI_CAT_Cable_A1-if00-port0": radio,
                  "usb-Echo_Box-if00": make("echo"),
                  "usb-Noisy_Gadget-if00": make("garbage")})
    rc, out = run(env_file, byid, "--list")
    time.sleep(0.2)
    if rc != 0 or any(p.received for p in all_ports):
        fail("--list sent bytes or failed", out)
    ok("--list lists the ports and sends nothing")

    # 2. Probe finds the radio among the others, whichever position it is in.
    rc, out = run(env_file, byid)
    if rc != 0 or "usb-FTDI_CAT_Cable_A1" not in out.split("RIG_DEVICE=")[-1]:
        fail("radio not found", out)
    if "10.00000 MHz USB" not in out:
        fail("frequency not decoded", out)
    ok("probe found the radio among a silent, an echo and a garbage port")

    # 3. --write changes only RIG_DEVICE and RIG_BAUD.
    rc, out = run(env_file, byid, "--write", env_file)
    with open(env_file) as f:
        written = f.read()
    want_dev = f"RIG_DEVICE={os.path.join(byid, 'usb-FTDI_CAT_Cable_A1-if00-port0')}"
    if rc != 0 or want_dev not in written.splitlines():
        fail("RIG_DEVICE not written", out + written)
    a = [x for x in original.splitlines() if not x.startswith(("RIG_DEVICE=", "RIG_BAUD="))]
    b = [x for x in written.splitlines() if not x.startswith(("RIG_DEVICE=", "RIG_BAUD="))]
    if a != b or "RIG_BAUD=38400" not in written.splitlines():
        fail("--write changed other lines", written)
    ok("--write set RIG_DEVICE to the port that answered, other lines unchanged")

    # 4. Radio at 9600, station file says 38400: nothing found, one rate only.
    slow = make("radio", baud=9600)
    byid2 = setup({"usb-FTDI_CAT_Cable_B2-if00-port0": slow})
    rc, out = run(env_file, byid2)
    if rc != 1 or "No radio answered" not in out:
        fail("rate mismatch should find nothing", out)
    if slow.rates_seen != {termios.B38400}:
        fail(f"probed at more than one rate: {slow.rates_seen}", out)
    ok("rate mismatch: nothing found, and only one rate was tried")

    # 5. --baud matching the radio's menu finds it.
    rc, out = run(env_file, byid2, "--baud", "9600")
    if rc != 0 or "RIG_BAUD=9600" not in out:
        fail("--baud 9600 did not find the 9600 radio", out)
    ok("--baud 9600 finds a radio set to 9600")

    # 6. Two radios: refuses to pick.
    byid3 = setup({"usb-FTDI_CAT_Cable_C3-if00-port0": make("radio"),
                   "usb-FTDI_CAT_Cable_D4-if00-port0": make("radio")})
    rc, out = run(env_file, byid3, "--write", env_file)
    with open(env_file) as f:
        after = f.read()
    if rc != 4 or after != written:
        fail("two radios should refuse and write nothing", out)
    ok("two radios answered: refuses to pick, writes nothing")

    # 7. Busy port: refused before anything is sent.
    busy = make("radio")
    byid4 = setup({"usb-FTDI_CAT_Cable_E5-if00-port0": busy})
    holder = subprocess.Popen(["sleep", "30"], stdin=open(busy.path, "rb"))
    time.sleep(0.2)
    try:
        rc, out = run(env_file, byid4)
    finally:
        holder.kill()
        holder.wait()
    if rc != 3 or busy.received:
        fail("busy port should be refused untouched", out)
    ok("a port held open by another program is refused untouched")

    # 8. On-board UARTs are never candidates.
    byid5 = tempfile.mkdtemp(dir=tmp)
    os.symlink("/dev/ttyS0", os.path.join(byid5, "onboard"))
    rc, out = run(env_file, byid5)
    if rc != 1 or "no USB serial ports" not in out:
        fail("on-board UART should be skipped", out)
    ok("on-board UARTs (DRAWS GPS, Pi serial) are skipped")

    # 9. Every byte any fake port ever received was the read-only query.
    time.sleep(0.2)
    total = 0
    for p in all_ports:
        data = p.received
        if len(data) % 5 or any(data[i:i + 5] != QUERY for i in range(0, len(data), 5)):
            fail(f"{p.kind} port received something else: {data.hex(' ')}")
        total += len(data) // 5
    ok(f"all {total} frames sent were 00 00 00 00 03 (read frequency and mode)")

    for p in all_ports:
        p.close()
    print("all findcat checks passed")


if __name__ == "__main__":
    main()
