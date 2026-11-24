# Local API

One JSON object per line over TCP on 127.0.0.1 (port 4100 unless
`--api-port` says otherwise). Every reply is one line with `"ok"`; on
`"ok": false` it also has `"error"`. Request values may be strings, numbers or
booleans; multi-line values are strings with `\n`.

## What the Messages panel calls

| To show or do | Call |
|---|---|
| Station, modem and counts, refreshed every few seconds | `status` |
| The list of templates and procedures for a "new message" picker | `compose_templates` (once) |
| The form for the template the operator picked | `compose_form` |
| The guidance while the operator types | `compose_check` (on each change, or on a short pause) |
| "What goes here?" beside a field, or "help me word this" | `compose_advise` |
| Send | `compose_send` |
| Messages waiting to go, in sending order | `queue` |
| The activity log, new lines only | `log` with `after` |
| A message's history and body | `list`, `get` |

`send`, `send_acp127`, `send_acp126`, `send_chat`, `parse_acp127` and
`parse_traffic` stay as they were for tools that build the envelope
themselves.

## status

`{"cmd":"status"}` →
`{"ok":true,"station":"AAR4XX","modem":"...","modem_ready":true,"modem_busy":false,"sending":"",
"queued":0,"sent":0,"acked":0,"failed":0,"received":0,"acks_waiting":0,"frames_dropped":0,"advisor":""}`

`advisor` names the helper plugged in with `--advisor`, or is empty.

With the station book on, `status` also has `on_duty`, `net` (the open net) and
`next_net`, the scheduled net that matters now (see Net schedules), or null:

    "next_net":{"name":"TUESDAY NET","state":"upcoming","start":1795654800,"end":1795658400,"net":""}

`link` is greybeard-ale's link state, read from ale-scan's control socket
(`--link-socket`):

    "link":{"gate":true,"held":1,"source":"ale-scan","socket":"/run/user/1000/ale-scan.sock",
      "reachable":true,"error":"","polled_at":1795391434,"scanner":"scanning","plan":"day",
      "engine":true,"clock":"wwv","hold_window":0,"links_reported":true,
      "stations":[{"station":"AAR4XY","state":"linked","since":1795391434,"up":true}]}

- `gate`: messages wait for a link (`--link-gate`). `held`: how many wait now.
- `source` is empty when no socket is given; `reachable` and `error` say whether
  the last read worked.
- `scanner`, `plan`, `engine`, `hold_window` and `clock` come from ale-scan's own
  status. `clock` is `wwv` once WWV has set the clock, `host` before;
  `last_wwv_at` is when WWV last set it (0 if never), and `clock_note` says it in
  words ("set by WWV 17 min ago", or "not set by WWV yet: times come from this
  computer's clock"). The log says when the clock source changes. The core never
  shows ale-scan's channel list.
- `stations`: per-station links from ale-scan's `links` field
  (`[{"station","state","since"}]`, state linked, calling or down), agreed with
  greybeard-ale. ale-scan leaves the field out until its link controller exists;
  then `links_reported` is false, the list is empty, and `links_note` says "no
  link data from greybeard-ale yet". `tools/stub_ale_scan.py` sends the field.

## compose_templates

`{"cmd":"compose_templates"}` →

```
{"ok":true,
 "procedures":[{"id":"16L_P2P_FULL","title":"16L station to station, full","summary":"...","available":true}, ...,
               {"id":"ACP127_CODRESS",...,"available":false,"why_not":"Codress needs a key: no key for today is loaded ..."}],
 "templates":[{"id":"GENADMIN_SIMPLE","title":"General admin message","summary":"...",
               "procedure":"16L_P2P_FULL","prec":"R","procedures":["16L_P2P_FULL", ...]}, ...]}
```

`procedure` and `prec` are what MARS stations use for that template in the
traffic samples; `procedures` lists every procedure the template may go in.
Ids are the ones the samples' SOURCES.csv uses, and `parse_traffic` names
received messages the same way.

Templates: GENADMIN_SIMPLE, GENADMIN, GENADMIN_AMARS_MONTHLY, GENADMIN_AAR,
GENADMIN_CODE_GROUPS, AKNLDG, RI, RRI, RRI_PASTE_STRIPS, RRI_METAR,
MARSSTATUS, BARE_STRIP, COMSPOT, COMSPOT_NET_REPORT, COMSPOT_GST_NET_REPORT,
ORDER, WXOBS, SVC, FREE_FORM.

## compose_form

`{"cmd":"compose_form","template":"MARSSTATUS"}` (add `"procedure"` to pick
another than the usual one) →

```
{"ok":true,"template":"MARSSTATUS","title":"Station status strip","summary":"...","procedure":"16L_P2P_ABBR",
 "fields":[{"key":"prec","label":"Precedence","kind":"prec","required":true,"default":"O",
            "hint":"How urgent it is: ...","example":"O","choices":["R","P","O","Z"]},
           {"key":"routing","label":"Addressees (heading line)","kind":"stations","required":true,"default":"",
            "hint":"...","example":"AAR4XY"},
           ...]}
```

Envelope fields come first, then the message's own. Every procedure but
chat ends its envelope with `wrap` ("Break lines at 69 characters", `yn`,
default `N`): N sends each line as written, the way MARS stations do; Y breaks
prose at 69 and refuses a strip that won't fit, since strips are never broken. `default` is already
filled in for this station and the current UTC time. `kind` says which input
fits:

| kind | input |
|---|---|
| `text`, `value` | one line (`value` is one strip field and may not hold `/`) |
| `lines`, `strips`, `addresses`, `metars`, `groups` | several lines |
| `call`, `stations` | one call sign; several, space separated |
| `address` | `RI/PLAIN NAME` or a plain name |
| `dtg`, `dtg_or_dash`, `ddhhmm`, `datetime` | `DDHHMMZ`; that or `-`; `DDHHMM`; `YYYYMMDDHHMMZ` |
| `choice`, `prec` | one of `choices` |
| `yn`, `number`, `state` | Y or N; digits; two letters |

## compose_check

`{"cmd":"compose_check","template":"MARSSTATUS","routing":"AAR4XY","town":"SPRINGFIELD", ...}`

Every key other than `cmd`, `template` and `procedure` is a field value. A
field left out takes its default; a field sent empty is empty. →

```
{"ok":true,"ready":false,"procedure":"16L_P2P_ABBR",
 "issues":[{"field":"county","level":"error","text":"County is empty.","fix":"Your county.","example":"GREENE"},
           {"field":"prec","level":"tip","text":"...","fix":"...","example":"R"}]}
```

`level` is `error` (must be fixed before it can be sent), `warning` (should
be looked at) or `tip`. `field` is empty for the message as a whole. Show
`text` and `fix` beside the field; `example` is a value that would be right
and can be offered as a one-tap fill. Following only the errors' examples
makes every template ready (tests/test_compose.cpp).

When there are no errors, the reply has `"ready":true`, `"preview"` (the
whole message as it will go, serials shown as 1), `"to_station"`, `"to_targets"`
(the calls it goes to over the air) and `"prec"`.

With the station book, compose understands its names:

- `to_station` may be a book name: a tactical name goes to its station's call,
  and a collective sends one copy to each of its stations. When `to_station`
  is empty, the first of the routing indicators or called stations is looked
  up the same way, so CALLS IRONSIDE goes to the call the book gives
  IRONSIDE. A tip says where it goes; a collective member the book doesn't have
  gets a warning and goes as written.
- CALLS NET (or ALL), with no `to_station`, is the whole net: one broadcast to
  `ALL`, which every station takes and none acks.
- A TO, INFO or FM line with a plain name and no routing indicator gets the
  book's RI for that name (IRONSIDE becomes RXXXB/IRONSIDE), with a tip.

What the checks catch, in plain words: empty required fields; bad
date-time groups and times; call signs and RIs in lower case or malformed; a
space where the RI's slash goes, and an RI with no plain name; `//` inside a
set and `/` inside a strip field; a strip or unbroken word over 69 when lines
are broken at 69;
characters the procedure can't carry (with a cleaned-up example); a 16L RI
message from a call sign or a P2P one from an RI; FLASH, and a precedence
other than the usual one; a chat with more than one line; unknown fields.
Anything shaped like a frequency is an error, and the issue never repeats it:
frequencies are controlled and are never sent in the clear, so channels are
named by designator.

## compose_send

Same request as `compose_check`. Refused with the issues while any error
stands. Otherwise the serials are taken (the station serial and the MSGID
serial are always the same number) and the message is queued:

`{"ok":true,"id":"AAR4XX-17","ids":["AAR4XX-17"],"to_targets":["AAR4XY"],"procedure":"16L_P2P_ABBR","message":"VZCZC\r\r\n...NNNN","issues":[...]}`

A collective queues one copy per station: `ids` and `to_targets` line up, and
`id` is the first. A station the core refuses is listed in `refused`.

Every message except chat carries the MSC digest and is signed with this
station's key; `"sign":"N"` sends it unsigned, and `signed` in the reply says
which. Codress (`ACP127_CODRESS`) takes `key`, one of the indicators
`compose_form` offers, and each copy is enciphered under it; `codress_key` in
the reply names the key used.

## compose_advise

`{"cmd":"compose_advise","template":"RRI","field":"ref_dtg","kind":"explain","question":"which DTG?", ...values}`

`kind` is `explain` (default) or `suggest`. `field` and `question` are
optional. → `{"ok":true,"source":"builtin","text":"..."}`

With no advisor, or when the advisor fails, the built-in guidance answers:
the field's hint, example and choices, and what is wrong with it now; or,
with no field, the template's summary and every error to fix, numbered.
`source` names the advisor when it answered; `advisor_error` says why it
didn't when it failed.

### Plugging in an advisor

`appliance-core --advisor PROG [--advisor-arg ARG ...]` runs PROG for each
`compose_advise`, with the request as its last argument:

```
{"kind":"explain","field":"ref_dtg","question":"which DTG?","template":"RRI","procedure":"",
 "template_title":"...","template_summary":"...","field_label":"...","field_hint":"...","field_example":"...",
 "values":[{"key":"ref_dtg","value":"2004Z"}, ...],
 "issues":[{"field":"ref_dtg","level":"error","text":"...","fix":"...","example":"022004Z"}, ...]}
```

It prints the answer as plain text or `{"text":"..."}`, within 10 seconds,
and exits 0. The core sends it nothing else, holds no key, and calls no
outside service; what the program does is up to it. In code, anything that
implements `compose::Advisor` can be set with `compose::set_advisor`.

## queue

`{"cmd":"queue","limit":100}` → `{"ok":true,"messages":[...]}`: outgoing
messages not yet sent, in the order they will go (the one on the air, then
FLASH, IMMEDIATE, PRIORITY, ROUTINE, oldest first within each). Each message
has the same fields as `list`.
With `--link-gate`, a message whose station has no link yet carries
`"waiting_for_link":true`; it goes as soon as one is reported, and the log says
"waiting for a link to B" and then "link to B is up". ACKs are never held.

## log

`{"cmd":"log","after":0,"limit":100}` →
`{"ok":true,"events":[{"seq":41,"ts":1795269300,"id":"AAR4XX-17","text":"queued IMMEDIATE for AAR4XY"}, ...],"last":41}`

Oldest first. Pass the reply's `last` as `after` next time to get only new
lines.

## list, get

`{"cmd":"list","limit":50}` and `{"cmd":"get","id":"AAR4XX-17"}` →
`{"ok":true,"messages":[{"id","direction","from","to","prec","precedence","state","attempts",
"created_at","updated_at","body","last_error"?}]}`

## The station book: address book, nets, duty, log, monthly report

All of it works offline and lives in the station's own SQLite file. Lists in a
request are one string: `roster` is comma-separated, `members` semicolon-separated.
Times are UTC unix seconds.

### Address book

- `book_put` `{name, kind, call, ri, ale, members, note}`: add or replace one
  entry. `kind` is `station` (default) or `collective` (needs `members`). A
  station's `call` defaults to its name. `note` is 80 characters at most. The
  book has no field for a person's name, phone or address.
- `book_import` `{csv, source}`: CSV with a header row using the columns name,
  kind, call, ri, ale, members, note. Other columns are left out and listed in
  `dropped_columns`. Rows that don't check out are listed in `errors` with their
  line number. Returns `imported`, the number of rows taken.
- `book_export` returns `csv` in the same layout. `book_list` returns `entries`.
- `book_get {name}` finds an entry by its name, call, RI or ALE address.
- `book_remove {name}` deletes one entry.
- `book_resolve {to}`: `to` is one name or a comma list. It returns `stations`
  (collectives expanded, each station once), `unknown` (names the book doesn't
  have) and `loops` (collectives that contain themselves).

`send` uses the book: a station name goes to its call, and a collective sends
one copy to each member, returned as `ids`. A name the book doesn't have goes as
written. `send` to `ALL` is a broadcast: every station takes it, none acks it,
and it is marked sent once it is on the air. No station may be called ALL.

### Nets

- `net_open` `{name, roster, exercise, ncs, note}` returns `id` (NET-n) and the
  net. `roster` can name collectives; they are expanded when the net opens.
  `ncs` defaults to this station.
- `net_checkin` `{net, station, traffic, remark}` checks a station in by hand.
  `traffic` is how many messages it holds for the net.
- `net_checkout {net, station}` checks a station out.
- `net_close {net}` closes the net and returns it with its check-ins.
- `net_status {net}` returns the net with `checkins`, `missing` (on the roster
  but not heard) and `in_roster` for each check-in.
- `net_list {limit}` lists nets, newest first.

`net` can be left out while exactly one net is open. When the core hears any
frame from a station (a message, an ACK or traffic for someone else), every open
net checks that station in with `via: "radio"`. A station that checked out
isn't put back in by radio. `status` adds `net`, the open net's id.

### Net schedules

- `sched_put` `{name, days, time, minutes, roster, exercise, ncs, note,
  auto_open}`: add or replace a schedule. `time` is the start in UTC as HHMM.
  `minutes` is how long the net runs (default 60). `days` is DAILY, WEEKDAYS,
  WEEKENDS or a list like `TUE,THU`; put 1 to 5 or L in front of a day for that
  week of the month (`1SAT` the first Saturday, `LSUN` the last Sunday).
- `sched_list` returns `schedules`, each with its current sitting: `state` is
  `upcoming`, `due` (started and not opened) or `open`, with `start`, `end`
  and `net`.
- `sched_next {limit}` returns `sittings`: due and open ones first, then the
  next to start.
- `sched_open {name}` opens the net now with the schedule's roster, net control
  and exercise mark. Opened up to 30 minutes early, or during the sitting, it
  counts as that sitting.
- `sched_remove {name}`.

The core looks at the schedules every 30 seconds. It logs, once each, a net
that starts in 15 minutes and one that is due and not open. A schedule with
`auto_open` opens itself at the start time. The operator opens the others.

### Net call-up

`net_callup` `{net, who, text, prec, unclas, channel}`: one
ACP 126 SIMPLE message from this station to `ALL`, the broadcast address. Every
station that hears it takes it and none acks it, and the sender marks it sent
once it is on the air. `who` is `missing` (default: roster stations not yet
checked in) or `all`. The called stations go on the call line when they fit
in 69 characters, else the call line is NET. The text is the appliance's own
wording:

    NET TUESDAY NET IS OPEN. NET CONTROL IS NCS-A.
    NOT YET CHECKED IN: STA-C STA-D
    CHECK IN WITH NET CONTROL.

An exercise net puts EXERCISE EXERCISE EXERCISE first and last. `text` adds a
line. No UNCLAS line unless `unclas` is true, as in the samples' net roster.
Returns `id`, `net`, `called` and `message`, and logs the call-up.

### Duty and the station log

- `duty_on {operator, note}` and `duty_off {turnover}`. One operator is on duty
  at a time.
- `duty_status` returns `on_duty`, `operator` and `since`, plus
  `last_turnover`, the last note left for the next operator. `status` adds
  `on_duty`.
- `log_add {kind, text}`: `kind` is `note`, `comspot` or `beadwindow`. An entry
  made during an exercise net is tagged as exercise.
- `station_log` `{from, to, limit}` returns `entries`, oldest first. The core
  adds its own entries for nets opening and closing, check-ins and check-outs,
  duty on and off, and turnovers. Nothing is ever pruned.

### Monthly report

`report_monthly {month: "2026-11"}` (default: this month, UTC) returns:

- `duty_tenths`: duty hours times ten.
- `nets`, `nets_exercise`, `nets_as_ncs` and `checkins`.
- `sent`, `sent_acked`, `sent_failed`, `received` and `exercise_messages`.
- `comspots` and `beadwindows`.
- `text`: the same report as plain upper-case lines of 69 characters or fewer,
  ready to send as a message body.

A message counts as an exercise when one of its lines starts with EXERCISE. The
report's layout is the appliance's own; MARS's monthly report form wasn't
available to copy.

## Station files: rosters, address books, frequency cards, net schedules

- `mars_import {data | text, kind, source}`: reads a member roster, address
  book, frequency card or net schedule. `data` is the file in base64 (a
  `.xlsx` or text), or `text` is a text table. `kind` is `auto` (the default),
  `roster`, `book`, `freq` or `schedule`. Returns `imported` and `sheets`,
  each with `sheet`, `kind`, `header_row`, `rows`, `imported`, `names`,
  `left_out`, `problems` and, for schedules, `frequencies_withheld`. What each
  file should look like is in docs/formats.md. Personal details are never
  imported, and frequencies go only to the channel plan's file.
- `channels_list`: the channel plan as `channels`, each with `name` (the
  designator), `mode`, `use` and `source`. Never a frequency.
- `channel_remove {name}`: takes a channel out of the plan.
- `sched_put` also takes `channel`, a designator, and `sched_list` returns it.
  A frequency there is refused.

## Keys, cards and signatures

docs/crypto.md explains each of these. Key and passphrase text is wiped from
memory after use and never written to the log.

### What parse_traffic adds

- `digest`: `pass`, `fail` or `none`. On `fail`, `possibly_garbled` is true and
  `repeat_request` holds a ready SVC request: `template`, `to_station`, `refs`
  and `svc_text`, for `compose_form`/`compose_send`.
- `signature`: `good`, `bad`, `unknown` (no card held for the signer) or
  `none`; `signer`, and `signer_checked` (that card was checked by voice).
- For codress: `codress` is true, `codress_key` is the indicator, and
  `deciphered` says whether this station read it. When it did, the fields
  describe the inner message, `inner_digest` is its digest check, and the
  outer digest stays in `digest`. When it didn't, `why_not_deciphered` says
  why.

### Net keys

- `key_import {text}`: loads a key tape. Returns `net`, `tape`, `added`,
  `already` and `expired`.
- `key_open {id | text}`: opens a sealed key tape (from a stored message or
  pasted text) and loads it, if its sender is named a keymaster here. Returns
  the same as `key_import`, plus `from` (the keymaster) and `from_checked`.
- `keys_list` also gives each key's `check`, to read by voice.
- `keys_list`: `keys`, each with `indicator`, `net`, `tape`, `from`, `until`,
  `state`, `cipher` (the module the key is for) and `usable` (false when that
  module isn't in the slot).
- `key_zeroize {which}`: wipes one key (`QXR01`) or a whole tape (`QXR`).
  Returns `zeroized`, the count.

### This station's keys

- `station_card`: `station`, `fingerprint` and `card`, the text to hand out.
- `station_backup {passphrase}`: `backup`, the station keys and held cards
  enciphered with the passphrase (12 characters or more), and `cards`, the
  count.
- `station_restore {text, passphrase}`: puts a backup back. It must be this
  station's. Returns `fingerprint`, `cards` (restored), `saved` and `age_days`
  (for dated backups) and `refused`.
- `rally_status`: for a box coming back on a bad day. `station`,
  `fingerprint`, `keymasters` (each `station`, `fingerprint`), `cards`,
  `keys_now` (each `indicator`, `net`, `check`) and `next`, the step to take.

### Other stations' cards

- `card_import {text | id, source, replace}`: loads a card from text or a
  stored message. Returns `station`, `fingerprint`, `result` (`added`, `same`
  or `replaced`), `checked` and `next`. A different card for a station already
  held is refused unless `replace` is `Y`.
- `cards_list`: this station and its `fingerprint`, and `cards`, each with
  `station`, `fingerprint`, `checked`, `keymaster`, `source` and `added`.
- `card_check {station, fingerprint, keymaster}`: marks a card checked when the
  fingerprint read back by voice matches. Case and spaces don't matter. With
  `keymaster=Y` it also names the station a keymaster.
- `card_keymaster {station, keymaster}`: names a checked card a keymaster
  (`keymaster=N` takes it back). Only a keymaster's sealed tapes open here.
- `handout_import {text | id | packet, replace}`: loads a member handout, new or stale.
  `packet` is the path of a packet folder (`handout.txt`, and `cipher/` when
  the net uses another cipher module); the module is installed only when its
  set hash matches the handout's signed `CIPHER` line. Also returns `cipher`
  (the module the handout names, when it names one) and `cipher_installed`.
  It must be for this station and signed by its issuer. Its cards are held as
  checked and its keymasters named; a different card for a station already
  held needs `replace=Y`. Returns `net`, `by`, `issued`, `cards`, `stale`,
  `keymasters`, `tape_opened` (with `added`, `already`, `expired` when it
  opened), `next` when there's a step to take, and `refused`.
- `card_send {to}`: sends this station's card as a plain message. Returns `id`.

### Keymaster

- `net_tape {net, start, days, period_hours, members, keymasters, send, handouts}`:
  makes a tape for `net` (`start` is YYYYMMDD or YYYYMMDDHHMM UTC, default
  today 0000Z; 31 days of 24-hour keys by default), loads it here, and seals a
  copy to each station in `members` (calls or station-book names, spaces or
  commas). `keymasters` names alternates: they get the tape too, and are named
  keymasters on every handout. Returns `net`, `tape`, `sealed` (each with
  `station`, `checked` and `text`, plus `id` when `send` is `Y`) and `no_card`
  (members whose card isn't held). With `handouts=Y` it also returns
  `handouts`, each with `station`, `has_tape`, `alternate` and `text`
  (docs/crypto.md).
- `tape_seal {text, to}`: seals a tape made with `pna-keymaster` to one
  station. Returns the same shape.

### The cipher slot

docs/cipher-slot.md is the spec.

- `cipher_status`: what is in the slot. `usable`, `name`, `version`,
  `builtin`, `output` (`bytes` or `text`), `key_bytes`, `source` and
  `set_hash`; `why` when the slot is empty (an installed module changed or
  won't load, so codress is off).
- `cipher_install {path}`: installs the module in a folder (`module.so` and
  `lib/`, or a packet folder holding `cipher/`). It must pass every slot
  check. Returns the same as `cipher_status`.
- `cipher_remove`: takes the installed module out; `pna-aes` is back in the
  slot. Keys for the removed module stay held but aren't used.
