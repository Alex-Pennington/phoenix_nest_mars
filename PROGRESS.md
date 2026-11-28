# Progress

## 2026-11-28

Session: draws-rf-end worker for `ymir-draws-coordinator-1127-1042`, branch
`feat/draws-rf-end`, cut from `feat/draws-station`.

**What changed** (all under `stations/draws-pi/`, plus one CHANGELOG line):

- The Pi is the second station's RF end and never a compute target: it plays
  the WAV it is handed, records what the radio hears, keys the hardware PTT
  line and sets the frequency it is given. README and CHECKLIST say so; the
  modem-on-the-Pi plan is gone. The filter table holds Alex's 2026-11-28
  ruling; the passband edges stay unmeasured (RSP2 not available).
- `bin/draws-send` (upstream host): copies one WAV whole to the Pi, checks
  its SHA-256 there, then has `draws-txwav` send exactly that file. Exit 10:
  the copy failed, nothing keyed. Exit 11: ssh failed during the send order,
  the burst may have gone out, never retried automatically.
- `bin/draws-pull` (upstream host): copies finished recordings, verifies
  each, deletes from the Pi only what verified.
- `bin/draws-setfreq` (Pi): sends only `F`, `f` and `m` to rigctld, never
  sets the mode, refuses a wrong read-back, refuses during a transmission and
  while the PTT line reads anything but low.
- `config/upstream.env`: the Pi's login from munin (address, user, key path,
  HostKeyAlias); no secret.
- `systemd/rigctld-draws.service`: `-C ptt_type=None,cache_timeout=0,rts_state=OFF,dtr_state=OFF`.
  `-P NONE` alone left CAT PTT on.
- Every Hamlib claim is cited to the Pi's Hamlib 4.6.2 (tag 4.6.2, commit
  8703647c), with the differences from 4.5.5. The platform is named as found:
  Debian 13 trixie, kernel 6.18.50+rpt-rpi-v8.
- CHECKLIST steps 18-21: update the Pi, check CAT PTT is off and the
  frequency read-back, set up munin, first burst into the dummy load. Steps
  18 and 19 read rigctld directly, because the 4.6.2 `rigctl -m 2` client
  prints a banner line first.

**Verified** (sox from the Ubuntu packages unpacked under the worktree's
`.t/`, TMPDIR under `.t/tmp`):

- `bash -n` on `bin/draws-send`, `bin/draws-pull`, `tests/dry-run.sh`: exit 0.
- `python3 ast.parse` on `bin/draws-setfreq`, `tests/fake-rigctld.py`: exit 0.
- `bash stations/draws-pi/tests/dry-run.sh`: exit 0, 25 ok, 0 FAIL.
- `python3 -I stations/draws-pi/tests/findcat-test.py`: exit 0, 9 ok.
- The coordinator ran step 18 on the Pi: rigctld runs with the new `-C`
  list, and the CAT PTT check answers `RPRT -11`. It ran step 19 with the
  old command, which failed on the banner line; the fixed step 19 is not yet
  run on the Pi.

**What is next:**

- Run the fixed step 19 on the Pi.
- Steps 20 and 21 (munin set-up, first burst into the dummy load).
- Step 16, the passband sweep, when the RSP2 is available.
- Open, reported to the coordinator and not filed: rigctld's raw `w` command
  can send any CAT byte (config cannot close it); the VFO toggles twice at
  each rigctld start; the existing helpers' error messages print their exit
  code; the USB CAT cable's DTR/RTS wiring is not known.
