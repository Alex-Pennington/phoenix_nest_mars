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

## Member roster

One row per member station. It fills the station book.

| Field | Header names | Goes to |
|---|---|---|
| Call (required) | MARS Call, Call, Callsign, Billet, Station, Station Call | the station's book entry |
| ALE address | ALE, ALE Address, Self Address | the entry's ALE address |
| Routing indicator | RI, Routing Indicator | the entry's RI |
| Role | Position, Role, Duty, Assignment, Title | the entry's note |
| Region | Region, State, Area, District | the note, and a collective per value (`REGION 4`, `ZZ`) |
| Group | Group, Collective, Team, Distribution | a collective per value, `;` or `,` between several |

**Personal details are never imported:** names, email, phone, street address,
city, ZIP, county, birth date, next of kin, amateur call and the like. Their
columns are listed under `left_out`. The station book keeps how to reach a
station, not who the operator is.

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

## Frequency card

One row per channel. It fills the station's channel plan.

| Field | Header names |
|---|---|
| Designator (required) | Channel, Chan, CH, Designator, Channel Name, ID |
| Frequency (required) | Frequency, Freq, kHz, MHz, Freq (kHz), QRG, Dial |
| Mode | Mode, Emission, Sideband |
| Use | Use, Purpose, Net, Remarks, Notes |

- Frequencies are read as MHz below 1000 (`5.000`), kHz from 1000
  (`5000.0`), and Hz from a million, unless the header or the cell says the
  unit (`Freq (kHz)`, `5 MHz`). Anything from 100 kHz to 3 GHz is accepted.
- A designator is 1 to 12 letters, digits or `-`, with at least one letter.
  A designator that looks like a frequency is refused.

**Frequencies are controlled.** They go into one file beside the database
(`NAME.channels`, readable only by the engine), and nowhere else. The import's
reply, `channels_list`, the log, the book, schedules and every message name
channels by designator only. Problems with a row are reported by row number
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

## What comes back

`mars_import` answers with one entry per sheet: `kind`, `header_row`, `rows`,
`imported`, `names` (book names, designators or nets as stored), `left_out`
(columns not used), `problems` (by row) and, for schedules,
`frequencies_withheld`. `ok` is true when anything was imported. Every import
is noted in the log by count only.

## Tests

`test_import` covers all four with made-up stations. The frequencies in it
are public time-signal frequencies, standing in for a channel plan. No MARS
channel, member or key appears in this repository.
