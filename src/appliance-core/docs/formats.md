# Station files the appliance imports

A MARS station is handed four kinds of file besides traffic: a member roster,
an address book, a frequency card and a net schedule. The appliance reads all
four into its own model with one command:

    appliance-cli import FILE            # works out which kind it is
    appliance-cli import FILE --kind freq

or the API call `mars_import` (docs/api.md). This page says what each file
should look like. It is written against the usual shapes of these files, not
against leadership's own copies yet. When blanked samples of those arrive,
the header names and quirks they use get added here and to the tests.

Key, cipher and passphrase files are not imported. Our cipher is our own
(docs/crypto.md).

## Any file

- **Spreadsheets:** `.xlsx`. Every sheet is read, and each sheet can be a
  different kind. Older `.xls` files aren't read: save them as `.xlsx` or CSV.
- **Text:** comma, tab, semicolon or `|` separated, with `"quoted, cells"`.
  A text table whose columns are lined up with two or more spaces works too.
- **The header row** is found by its names, anywhere in the first 20 rows, so
  title lines and blank lines above it are fine. A header repeated further
  down (a printed sheet's page break) is skipped, and so are blank rows.
- **Header names** match without regard to case, spaces or punctuation:
  `MARS Call`, `mars call` and `MARS-CALL` are the same. The names each field
  answers to are listed below.
- Anything the import doesn't use is listed back under `left_out`, so nothing
  is dropped silently.
- **Files with no header** are read when their shape is plain: MSC's
  `channels.csv` (a designator and a kHz frequency on each line), an old
  address list whose first column is calls, and a checkerboard (below). The
  reply says how it read them.

These shapes were checked against the files MSC stations hold today (address
books, a regional roster, address lists, a frequency annex, `channels.csv`,
and two kinds of checkerboard). The tests use made-up copies of those shapes.

## Member roster

One row per member station. It fills the station book.

| Field | Header names | Goes to |
|---|---|---|
| Call (required) | MARS Call, Call, Callsign, Call Signs Asgn/Billet (any header starting Call Sign), Billet, Station | the station's book entry |
| ALE address | ALE, ALE Address, Self Address | the entry's ALE address |
| Routing indicator | RI, Routing Indicator | the entry's RI |
| Role | Position, Role, Duty, Assignment, Title | the entry's note |
| Region | Region, State, ST, Area, District | the note, and a collective per value (`REGION 4`, `ZZ`) |
| Group | Group, Collective, Team, Distribution | a collective per value, `;` or `,` between several |

**Personal details are never imported:** names, email, phone, street address,
city, ZIP, county, birth date, next of kin, amateur call and the like. Their
columns are listed under `left_out`. The station book keeps how to reach a
station, not who the operator is. A grid square (MGR) is left out too.

- A call with a short suffix (`CALL/M`, `CALL/P`) is the same station.
- A call written with underscores (`ABC1__`) is a billet not yet filled: it is
  left out, and the reply counts them in one line.

## Address book

Addressing for stations and collectives. It also fills the station book.

| Field | Header names |
|---|---|
| Name | PLA, Plain Language Address, Short Name, Tactical, Addressee |
| Call | Call, Call Sign, Station |
| Routing indicator | RI, Routing Indicator |
| ALE address | ALE, ALE Address, Self Address |
| Members | Members, Includes (for a collective; `;`, `,` or `/` between) |
| Kind | Kind, Type (`collective`, `group`, `CAD` or `AIG` make a collective) |

A row needs a name or a call. A row with members is a collective. Collectives
are stored after the stations, so they can name stations further down.

MSC's address books (`AMARS_AB.csv`, `RGN_AB.csv` and the rest) are read as
they come:

- A header that starts with **PLA** is an address book, even though its
  point-of-contact columns (POC Name, POC Email, POC Phone, Org Name,
  Location) look like a roster's. Those columns are never imported.
- A line with no call, RI or ALE address is a **date stamp or notice**
  (`TEST_AB 01JAN2026`, `***UPDATED ...***`) and is skipped.
- A **PLA with no call** is reached through its RI: the RI becomes what goes on
  the air, and the note says `BY RI`. So is one whose call column holds an
  office title longer than 12 letters.
- Marks a book name can't hold (`&`, `(`, `,`) become spaces.
- An address book with a header and no rows (MSC's empty personal book) reads
  as an empty book, with no problem.

## Frequency card

One row per channel. It fills the station's channel plan.

| Field | Header names |
|---|---|
| Designator (required) | Channel, Chan, CH, DES, Designator, Channel Name, ID |
| Frequency (required) | Frequency, Freq, kHz, MHz, Freq (kHz), QRG, Dial |
| Mode | Mode, Emission, Sideband |
| Use | Use, Purpose, Net, Name, Remarks, Notes |

- Frequencies are read as MHz below 1000 (`5.000`), kHz from 1000
  (`5000.0`), and Hz from a million, unless the header or the cell says the
  unit (`Freq (kHz)`, `5 MHz`). Anything from 100 kHz to 3 GHz is accepted.
- A designator is 1 to 12 letters, digits or `-`, with at least one letter.
  A designator that looks like a frequency is refused.

**Frequencies are controlled.** They go into one file beside the database
(`NAME.channels`, readable only by the engine), and nowhere else. The import's
reply, `channels_list`, the log, the book, schedules and every message name
channels by designator only. The one exception is `channels_get_full`, the
read behind gale-web's CHANNELS screen (Alex's ruling 4 of 2026-11-26). Problems with a row are reported by row number
and designator, never by frequency. The plan is there so the radio side can
tune by designator; how greybeard-ale reads it is still to be agreed with
them.

## Net schedule

One row per net. It fills the net schedules (`sched_list`).

| Field | Header names |
|---|---|
| Net (required) | Net, Net Name, Name |
| Days (required) | Day, Days, When, Schedule |
| Start (required) | Time, Start, Time (UTC), UTC, Zulu, Z |
| End | End, End Time |
| Length | Duration, Length, Minutes |
| Net control | NCS, Net Control |
| Channel | Channel, Chan, Designator |
| Frequency | Frequency, Freq (only to find the designator) |
| Roster | Roster, Stations, Participants |
| Notes | Notes, Remarks |

- **Days** as people write them: `Daily`, `Weekdays`, `Mon-Fri`, `Tue/Thu`,
  `Tuesdays & Thursdays`, `1st & 3rd Sat`, `Last Sunday of the month`.
- **Start** must be UTC: `1900Z`, `19:00 UTC`, `1900`. A local time
  (`7:00 PM ET`) is refused for that row, not guessed at.
- **Length** from an End column, or Duration in minutes or hours (`90 min`,
  `1 hr`). One hour when neither is given.
- **Channel:** a schedule should name the channel's designator. When it gives
  a frequency instead, the import looks it up in the channel plan and stores
  the designator. A frequency the plan doesn't hold is never stored: the row
  imports with no channel, and `frequencies_withheld` counts it. So load the
  frequency card first.

## Checkerboard

A checkerboard is a week of nets on a grid: a row of day names across the
top, a time down the side, and in each cell the net's designators
(`C11/C22/C33`), sometimes with its name and a tag in brackets. It has no
header row this import knows, so it is found by its row of day names (five
or more), anywhere in the first 20 rows.

- **Time:** a column of UTC times (`1101Z`) is used when there is one. If
  there is only a local column (`7AM / 6AM`, `0600L`), the zone must be in its
  header (`EDST/CDST`, `EST`, `CDT` and the like). The first zone and the
  first time are used, and the reply says the times were converted and from
  which offset, so a sheet titled standard time but headed daylight can be
  checked. With no zone the sheet is refused, not guessed at.
- **Day cells:** the first designator is the net's channel, the others go in
  its note as `ALT`, and a bracketed tag is kept in the note. Words that aren't
  designators name the net; a cell of designators only is `NET`. The name ends
  with the UTC start (`NET 1100Z`). The same cell at the same time on several
  days is one net on those days. A time that falls past midnight UTC moves to
  the next day.
- **Channel columns** between the time and the first day (`PRIMARY`) say
  which channel to use each hour. Each run of hours on one channel becomes a
  daily schedule named for the column, with the note `CHANNEL PLAN`.
- A slot runs to the next slot's start, an hour by default.

The plan's file is `PNA CHANNEL PLAN 1` and then one line a channel:
`DESIGNATOR|HZ|MODE|USE|SOURCE|ANT_RX|ANT_TX|WORLD|LABEL`. `WORLD` is `mars`
or `amateur` and `LABEL` the HFLink label (station design v1.9). A line
written before the later fields reads as ports 1 and 1, `mars`, no label. A
card imported again keeps each channel's ports, world and label.

## What comes back

`mars_import` answers with one entry per sheet: `kind`, `header_row`, `rows`,
`imported`, `names` (book names, designators or nets as stored), `left_out`
(columns not used), `problems` (by row) and, for schedules,
`frequencies_withheld`. `ok` is true when anything was imported. Every import
is noted in the log by count only.

## The gale-rx schedule file (written, ask 28)

The core's net schedules are the station's one schedule. With
`rx-schedule-dir DIR` set (appliance.conf), the core writes two files there,
whole (a temp file renamed over each), mode 600, on every change: the files
gale-rx reads (greybeard-ale docs/rx-scheduler.md). With none set, nothing is
written.

`DIR/net_schedule.txt`, in the form greybeard-ale's tools/checkerboard.py
writes, a line a slot, in week order from Monday 0000Z:

    # DAY HHMM(Zulu) PLAN MODE -- written by appliance-core from its net schedules (sched_put); a slot runs until the next
    TUE 1900 net_plans/net03.plan net  # TUESDAY-NET + P1
    TUE 2000 net_plans/net03.plan scan  # end of TUESDAY-NET

`DIR/slot-channels.txt`, a line for each slot with a plan channel and for each
sitting's end:

    # DAY HHMM INDEX [MODE] or DAY HHMM park -- written by appliance-core from its net schedules (sched_put)
    TUE 1900 1 LSB
    TUE 2000 park

- Only a schedule with a `plan` is written. Each of its days in the week from
  today gives a `net` slot at its start: `DAY` is `MON` to `SUN`, `HHMM` UTC,
  `PLAN` is `net_plans/PLAN.plan`. The comment carries the schedule's name
  (letters and digits, anything else as `-`) and, after `+`, its channel's
  designator when it names one: gale-rx's rule 2 then holds that plan
  channel.
- A schedule's `index` gives `DAY HHMM INDEX MODE` in slot-channels.txt
  (`MODE` USB when not set): gale-rx's rule 1.
- Each sitting ends `minutes` after its start in a `scan` slot of the same
  plan, with `park` in slot-channels.txt, so gale-rx parks between nets. No
  end is written where another sitting starts or is under way. A sitting may
  run past midnight and round the week.
- A week-of-month day (`1SAT`, `LSUN`) is in the file only in the week it
  falls; the core writes the files again when the week moves on.
- Two sittings starting at the same minute: the first by schedule name is
  written, the other left out (gale-rx refuses two slots at one time).
- No frequency is in either file: plans are named by label and channels by
  index or designator.

## Tests

`test_import` covers all four, and made-up copies of the MSC shapes above,
with made-up stations. The frequencies in it
are public time-signal frequencies, standing in for a channel plan. No MARS
channel, member or key appears in this repository. `test_records` covers the
gale-rx schedule file and the plan file's world and label.
