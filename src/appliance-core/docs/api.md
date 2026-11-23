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
               {"id":"ACP127_CODRESS",...,"available":false,"why_not":"Codress needs the MARS cipher, ..."}],
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
whole message as it will go, serials shown as 1), `"to_station"` and `"prec"`.

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

`{"ok":true,"id":"AAR4XX-17","procedure":"16L_P2P_ABBR","message":"VZCZC\r\r\n...NNNN","issues":[...]}`

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
written.

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
