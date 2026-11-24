# Operator quick start

appliance-core is the message side of the MARS operator appliance. It keeps
the traffic, the station book, nets and logs, and hands bursts to greybeard-ale,
which does the radio side on the same computer. It runs as a background
service for your user, and you work it from gale-term's Messages panel or from
the `appliance-cli` command.

## 1. Install

You need a Linux computer with a C++17 compiler, CMake, and SQLite3's and
OpenSSL's headers.
On Debian or Raspberry Pi OS:

    sudo apt install build-essential cmake libsqlite3-dev libssl-dev

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

Everything a box needs is entered through forms on the operator screen, so
a member with only this box never edits a file (docs/forms.md). A box with
no call shows one form the first time it starts: Your station. From the
operator screen, or:

    appliance-cli form fill SETUP call=STA-B     # or: appliance-cli setup STA-B

That sets the call, makes the station's keys and card, and prints the
fingerprint to read to your keymaster. The call is kept in the data folder,
never in the settings file, and can't be changed from the API once set. A
`station` in the settings file still wins over it.

On a box you set up yourself, open `~/.config/phoenix-nest-mars/appliance.conf`
and fill in:

- `station`: your station's call (leave it out to set it on first start).
- `captures`: greybeard-ale's capture directory.
- `tx-cmd`: the full path to gale-tx. The installer fills it in when gale-tx
  is on your PATH.

Leave channel plans and frequencies out of this file. A frequency card is
loaded with `appliance-cli import` (section 5b), or entered by hand one
channel at a time with no documents at all:

    appliance-cli form fill CHANNEL name=P1 frequency=5000 use=PRIMARY

The other forms work the same way: `appliance-cli form` lists them (station
book, net schedule, cards, startup bundle, keymaster, key tape, handout,
backup, restore), `form show ID` shows one with its help, and `form edit`
changes a form for your region.

Either way it is kept in its own file, never shown back, and travels only
sealed to a member's card in a startup bundle. Then check the file:

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

## 5a. Keys, cards and codress

Your box made its own station keys the first time it started. To work codress
or check signatures with another station:

| To | Type |
|---|---|
| Show your card and fingerprint | `appliance-cli station card` |
| Send your card to a station | `appliance-cli api card_send to=NCS-A` |
| Load a card that came in, or from a stick | `appliance-cli cards import ID` or `cards import FILE` |
| Mark a card checked, after reading the fingerprint back by voice | `appliance-cli cards check NCS-A "K7QM 2XRD 9FTA HW3C"` |
| Load the handout your keymaster gave you | `appliance-cli handout import FILE` |
| Accept a keymaster, after reading the fingerprint back by voice | `appliance-cli keymaster accept NCS-A "K7QM 2XRD 9FTA HW3C"` |
| Refuse a keymaster | `appliance-cli keymaster refuse NCS-A` |
| See who you accepted, refused, or haven't decided on | `appliance-cli keymaster list` |
| Load a startup bundle (book, channels, schedules) | `appliance-cli bundle load FILE` or `bundle load ID` |
| Make a member's startup bundle for a stick, frequencies sealed to them | `appliance-cli bundle make FILE --for STA-B` |
| Send each member their own copy over the air | `appliance-cli bundle send STA-B,STA-C` |
| See what your box can do now, and the next step | `appliance-cli rally` |
| Load a key tape from a stick | `appliance-cli keys import FILE` |
| Open a sealed key tape that came in | `appliance-cli keys open ID` |
| See your keys | `appliance-cli keys list` |
| Wipe a key or a whole tape now | `appliance-cli keys zeroize QXR01` |

**Whose net you join is your choice.** Loading a handout names nobody your
keymaster: the box asks about each one, and takes no tape until you accept.
Accept only after you have read the fingerprint back with a voice you know,
or in person. A bundle from a station you haven't checked loads only with
`--yes`, and changes no card or key either way. A bundle made for you carries
the frequencies sealed to your card, so your channel plan fills in with no
frequency card; to get one, send your card to the net's keymaster and check
it by voice.

When a key for today is loaded, the Messages panel offers codress, and every
message you send is signed. Keymasters, see docs/crypto.md for making tapes
and handouts and building a net from nothing.

**On a bad day,** with your box wiped: restore your station backup (or start
fresh), load your old handout and startup bundle, even stale ones, and run
`appliance-cli rally`. It says what to do next. With every tape ended and your
old keymaster gone, signed plain traffic still works: exchange cards with
whoever is running the net, choose them as your keymaster if you trust them,
and open their new tape.

## 5b. Your station's files

Load the roster, address book, frequency card and net schedule you were given,
spreadsheet or text, one at a time:

    appliance-cli import region-roster.xlsx
    appliance-cli import frequency-card.csv
    appliance-cli import net-schedule.xlsx

Load the frequency card before the schedule. The reply says what each file
was taken for, how many entries went in, and any row it couldn't use.
Personal details in a roster are left out. Frequencies are kept in their own
file, and everything else names channels by designator. docs/formats.md says
what each file should look like.

## 6. Backups

Everything is in `~/.local/share/phoenix-nest-mars`, one file per station
(`NCS-A.db` and so on). Stop the service, copy the folder, start it again:

    systemctl --user stop appliance-core
    cp -a ~/.local/share/phoenix-nest-mars /media/backup/
    systemctl --user start appliance-core

Copy the settings file too. The address book also exports as CSV:
`appliance-cli api book_export`.

That folder also holds your station keys (`NAME.id`) and net keys
(`NAME.keys`) in the clear, so treat the copy as you would the keys. To keep
only the station keys, enciphered, use
`appliance-cli api station_backup passphrase="..."` instead (docs/crypto.md).

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
