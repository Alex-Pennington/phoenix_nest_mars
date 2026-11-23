# Operator quick start

appliance-core is the message side of the MARS operator appliance. It keeps
the traffic, the station book, nets and logs, and hands bursts to greybeard-ale,
which does the radio side on the same computer. It runs as a background
service for your user, and you work it from gale-term's Messages panel or from
the `appliance-cli` command.

## 1. Install

You need a Linux computer with a C++17 compiler, CMake and SQLite3's headers.
On Debian or Raspberry Pi OS:

    sudo apt install build-essential cmake libsqlite3-dev

Then, from this folder (`src/appliance-core` in the repository):

    packaging/install.sh

It builds, runs the tests, and installs for your user only, with no root:

| What | Where |
|---|---|
| Programs | `~/.local/bin/appliance-core`, `~/.local/bin/appliance-cli` |
| Settings | `~/.config/phoenix-nest-mars/appliance.conf` |
| Messages, book, nets, logs | `~/.local/share/phoenix-nest-mars` |
| Service | `appliance-core`, a systemd user service |

Running it again upgrades the programs and keeps your settings and data.

## 2. Set up your station

Open `~/.config/phoenix-nest-mars/appliance.conf` and fill in three settings:

- `station`: your station's call.
- `captures`: greybeard-ale's capture directory.
- `tx-cmd`: the full path to gale-tx. The installer fills it in when gale-tx
  is on your PATH.

Leave channel plans and frequencies out of this file: the appliance never
needs them. Then check the file:

    appliance-core --config ~/.config/phoenix-nest-mars/appliance.conf --check

It says what would run and lists anything to fix. "ready to run" means it's
fine.

## 3. Start it

    systemctl --user enable --now appliance-core

It now starts at every login. To keep it running with nobody logged in, for an
appliance that boots on its own, run this once:

    sudo loginctl enable-linger $USER

To see that it's up:

    appliance-cli status

Its log is in the journal:

    journalctl --user -u appliance-core -f

After an edit to the settings, check them again and restart:

    systemctl --user restart appliance-core

## 4. Practise with no radio

To try everything first, put `#` in front of `station` and remove it from
`loopback-pair = NCS-A,STA-B`. Two practice stations then run back to back in
the one service: NCS-A answers `appliance-cli`, STA-B answers
`appliance-cli --port 4101`. Nothing goes on the air.

## 5. Everyday use

The Messages panel in gale-term does all of this with forms. From the shell:

| To | Type |
|---|---|
| See the station | `appliance-cli status` |
| Send a message | `appliance-cli send --to STA-B "TEXT OF THE MESSAGE"` |
| See the traffic | `appliance-cli list 20` |
| Add a station to the book | `appliance-cli api book_put name=IRONSIDE call=STA-B` |
| Go on duty | `appliance-cli api duty_on operator=OP1` |
| Open a net | `appliance-cli api net_open name="PRACTICE NET" roster="STA-B,STA-C"` |
| Call the stations not yet in | `appliance-cli api net_callup` |
| Check a station in by hand | `appliance-cli api net_checkin station=STA-C` |
| Close the net | `appliance-cli api net_close` |
| Leave a turnover note | `appliance-cli api duty_off turnover="QUIET WATCH"` |
| This month's report | `appliance-cli api report_monthly` |

A net that meets on a schedule only needs setting up once. This one meets
every Tuesday at 0100Z and opens itself:

    appliance-cli api sched_put name="TUESDAY NET" days=TUE time=0100 roster="STA-B,STA-C" auto_open=yes

The station notes it in the log 15 minutes before the start. Every call is in
`docs/api.md`.

## 6. Backups

Everything is in `~/.local/share/phoenix-nest-mars`, one file per station
(`NCS-A.db` and so on). Stop the service, copy the folder, start it again:

    systemctl --user stop appliance-core
    cp -a ~/.local/share/phoenix-nest-mars /media/backup/
    systemctl --user start appliance-core

Copy the settings file too. The address book also exports as CSV:
`appliance-cli api book_export`.

## 7. When something's wrong

- **`appliance-cli` says nothing is answering.** Check
  `systemctl --user status appliance-core` and the journal. A wrong setting
  shows there, and `--check` names it.
- **"cannot open API port".** Another program has port 4100. Set `api-port`
  to another number, and give that number to gale-term and to `appliance-cli
  --port`.
- **Messages stay queued.** With `link-gate = yes` they wait for greybeard-ale
  to report a link to that station; `appliance-cli status` shows how many wait
  and why. Without it, check that gale-tx runs and greybeard-ale picks up the
  bursts from the outbox.
- **The clock.** `appliance-cli status` says whether WWV set the clock. Until
  it has, message times come from this computer's clock.

## 8. Remove it

    packaging/install.sh --uninstall

This removes the programs and the service and keeps your settings and data.
