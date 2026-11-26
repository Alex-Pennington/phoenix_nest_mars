# Local API

One JSON object per line over TCP on 127.0.0.1 (port 4100 unless
`--api-port` says otherwise). The same calls also answer over a WebSocket on
the web port: see "Web port" below. Every reply is one line with `"ok"`; on
`"ok": false` it also has `"error"`. Request values may be strings, numbers or
booleans; multi-line values are strings with `\n`.

Every reply also ends with `"store_mode"`: `"real"`, or `"training"` when the
practice station answered it. Any request may carry `"expect_store"`
(`real` or `training`): when the store in force is the other one (another
screen turned training on or off), the call does nothing and is refused with
`store_mismatch: true`, `expected`, and `store_mode` naming the store in
force. `training` and `training_status` aren't held to it. The field is
taken off the request once it is checked, so no call sees it as one of its
own fields or settings.

## What the Messages panel calls

| To show or do | Call |
|---|---|
| Station, modem and counts, refreshed every few seconds | `status` |
| The list of templates and procedures for a "new message" picker | `compose_templates` (once) |
| The form for the template the operator picked | `compose_form` |
| The guidance while the operator types | `compose_check` (on each change, or on a short pause) |
| "What goes here?" beside a field, or "help me word this" | `compose_advise` |
| Send | `compose_send` |
| Messages waiting to go, in sending order, then those written and waiting for their key | `queue` |
| Whether a sent message has reached `written` (and later `sent` or `failed`) | `get` or `queue`, polled: see "Following a message" under "list, get" |
| The activity log, new lines only | `log` with `after` |
| A message's history and body | `list`, `get` |
| Any level-zero form (setup, book, channels, schedules, bundles, keymasters, tapes, backups) | `form_list`, `form_get`, `form_check`, `form_submit` |
| The y dialog that keys a written burst | `key_prepare` when it opens, `key_confirm` on the keyboard y, `key_cancel` on No or leaving it |
| The y dialog that turns an automatic mode on, and turning it off | `standing_prepare`, `key_confirm`, `key_cancel`; `standing_off` |

`send`, `send_acp127`, `send_acp126`, `send_chat`, `parse_acp127` and
`parse_traffic` stay as they were for tools that build the envelope
themselves.

## First start: setup_status, setup_station

A box with no call (none in the settings, none set before) runs in setup
mode: the API port answers only these two calls until the call is given.

- `setup_status`: `setup_needed` (true or false), `call` once set, and `next`.
- `setup_station {call}`: sets the call, makes the station's keys and card,
  and starts the station on the same port. Returns `call`, `keys_made`,
  `fingerprint` and `card` (the card text to send). Refused once a call is set.

gale-term shows this as its first-run screen when `setup_needed` is true.
In setup mode the form calls below also answer, for the SETUP form only.

## Web port: the operator screen (gale-web)

With `--web-port N` (off unless given; `4101` on the appliance) the core also
serves the kiosk screen, on 127.0.0.1 only, no TLS
(docs/gale-web-screens.md section 5):

- `GET /` and `GET /<file>`: gale-web's built files from `--web-root`
  (default `/opt/gale/web`); `/` is `index.html`. Content types for html, js,
  css, json, woff2, svg and png (others are `application/octet-stream`),
  `Cache-Control: no-cache`, `Connection: close`. No directory listing.
  404 for anything else: other methods, directories, missing files, hidden
  names (`.env`), `..` written plainly or %-encoded, bad %-escapes, and
  symlinks that lead out of the web root. A web root that isn't there yet
  only makes every file 404; the WebSocket still works.
- `GET /ws`: a WebSocket (RFC 6455, version 13). Each text frame is one
  request, the same JSON object as one line on the API port, answered by the
  same handler, one text frame per reply, in order. Fragmented messages,
  ping/pong and the close handshake work; frames from the client must be
  masked.
  - `rid`: put `"rid": <int>` in a request and the reply carries it back
    unchanged as its first member (`{"rid":7,"ok":true,...}`). A number stays
    a number, a string a string. The handler never sees `rid`. Without `rid`
    the frame goes to the handler exactly as sent. A reply with no `rid` is a
    push (none are sent yet: a screen polls, as "Following a message to
    `written`, `sent` or `failed`" says).
  - Limits: 1 MiB per frame and per whole message (close 1009); binary
    frames close with 1003, bad UTF-8 with 1007, protocol errors with 1002.
    When the core stops, open sockets get close 1001.
- Who may connect: the `Host` header must be `127.0.0.1:N` or `localhost:N`
  (else 403), and a WebSocket's `Origin`, when sent, must be
  `http://127.0.0.1:N` or `http://localhost:N` (else 403). A browser always
  sends `Origin`. A request with no `Origin` is a local program, not a
  page: it is taken only when it comes from this box itself (a loopback
  peer) and refused with 403 from anywhere else.
- At most 32 connections at once (503 beyond that).
- In setup mode (no call yet) the screen's requests go through the setup
  calls above, so the first-run screen works in the kiosk too; when the call
  is set the port closes and reopens on the station (the screen reconnects).
- With `--loopback-pair` A's screen is on N and B's on N+1. The web port must
  not overlap the API port(s).

## Forms

Every document a box needs from level zero is entered by a form drawn from
a template (docs/forms.md). gale-term needs no code per form: it draws what
`form_get` returns and sends it back with `form_submit`. Field values go at
the top level of the request, beside `cmd` and `form`.

- `form_list {level}`: `forms`, each `id`, `title`, `summary`, `level_zero`,
  `order`, `origin` (`built in`, `edited here`, `from bundle NCS-A`) and
  `changed`. `level=zero` lists only level-zero forms.
- `form_get {form, <prefill key>}`: the form to draw. `title`, `summary`,
  `origin`, `prefilled` and `fields`, each with `key`, `label`, `kind`
  (`text`, `textblock`, `choice`, `yesno`, `time`, `pattern`), `mask`,
  `suffix`, `required`, `upper`, `secret`, `multi`, `nosend`, `fill`, `as`,
  `check`, `max`, `again`, `source`, `default`, `choices` (each `value`,
  `label`), `help` and `value`.
  A secret field never has a value; `has_value` says one is held. Also
  `fixed` (values the template sets, each `key`, `value`, to show the
  member), `actions` (each `cmd`, and `when`, `key=VALUE`, when the call
  depends on an answer) and `next_forms`. With the prefill key given
  (`name=P1` for CHANNEL), the fields are filled from what the station holds.
  How each answer reaches the call (docs/forms.md, the field words):
  - `nosend`: true when the field is checked but never sent to the call.
    A field with `again` (a passphrase typed twice) and the field an
    `actions` entry's `when` names aren't sent either.
  - `fill`: true when a blank answer takes the `default`. When false, a
    field left out takes the `default`, but one sent blank isn't sent
    and the call keeps what it holds.
  - `as`: only when the template sets it: the call's parameter the answer is
    sent under, instead of `key`.
- `form_check {form, fields...}`: `ready`, `problems`, each `field` and
  `problem` in plain words, and `filled`, each `field` whose answer the core
  will fill with its default for these answers, and that `value`. Changes nothing. Call it as the member types.
- `form_submit {form, fields...}`: checks, then runs the form's call. Returns
  `ok`, `form`, `action` (the call run), `error`, `problems` (nothing was
  run), `not_done` (stations a send missed), `next`, `next_forms` and
  `result`, the call's own reply.
- `form_template_get {form}`: `text`, `origin`, `builtin`, `changed`.
- `form_template_put {form, text}`: checks and keeps a template. `result` is
  `added`, `replaced` or `same`; a bad template returns `error` naming the
  line. In use at once.
- `form_template_reset {form}`: drops the local copy. `result`: `built in
  again`, or `removed` for a form that isn't built in.

## Guide

The operator guide (docs/guide.md). Every page comes with `lines`: the page
laid out for the 80x25 screen (title, text, keys, example, see also), ready
to draw. These answer in setup mode too, from the built-in pages.

- `guide_list`: `pages`, each `id`, `title`, `kind` (`start`, `screen`,
  `topic`, `form`, `type`), `origin` and `screens`.
- `guide_get {id}`: `found`, and `page` with `id`, `title`, `kind`, `help`
  (the key strip), `screens`, `keys` (each `key`, `what`), `text`,
  `example`, `see`, `origin` and `lines`.
- `guide_help {screen, type?}`: the page for `?` on that screen. With
  `type` on the compose screen, also `type`, that message type's page. A
  screen with no page yet gives the MAIN page with `found` false.
- `guide_start`: the walkthrough. `steps` (each `n`, `id`, `title`, `form`,
  `screen`, `how`, `done`), `done`, `of`, `next`, `next_title`,
  `next_screen`, and the START page.
- `guide_page_get {id}`, `guide_page_put {text}`, `guide_page_reset {id}`:
  edit a written page, as `form_template_*` do for forms. A page holding a
  frequency is refused, and so is a FORM- or TYPE- id.

Every guide reply has `training`.

## Training

- `training {on:Y|N, reload:Y}` and `training_status`: `on`, `since`,
  `samples` (sample messages played in), `practice_station` (TRAIN-NCS) and
  `real` (`received`, `queued`, `unacked`, `emcon` of the real station).
  These two are always answered by the real station.

While training is on, every other call is answered by the practice station
and its reply has `"training":true`. See docs/guide.md, Training mode.

## status

`{"cmd":"status"}` →
`{"ok":true,"station":"AAR4XX","modem":"...","modem_ready":true,"modem_busy":false,"sending":"",
"queued":0,"sent":0,"acked":0,"failed":0,"received":0,"unacked":0,"held":0,"cancelled":0,
"overheard":0,"garbled":0,"acks_waiting":0,"frames_dropped":0,"emcon":false,
"cap":{"per_hour":12,"used":0,"room":12,"resets_at":0,"covers":["parts_resend","bbs_auto"],
"sources":[{"source":"parts_resend","active":true,"per_hour":12,"used":0,"room":12,"resets_at":0},
{"source":"bbs_auto","active":false,"per_hour":12,"used":0,"room":12,"resets_at":0}]},"advisor":""}`

`unacked`, `held`, `cancelled`, `overheard`, `garbled` and `emcon` are
explained under "Working with MSC stations" below.

`cap` is the airtime cap, for a status strip: one number for every source
of transmissions the operator doesn't press for one by one. Each source has
its own cap over a rolling hour (not a clock hour):

- `parts_resend`: the resend queue of asked-for parts
  (`--parts-resend-per-hour`, default 12; see parts_queue). Each send counts
  once, however many stations asked. Always active.
- `bbs_auto`: machine mode's automatic sends, answers and routed traffic
  (`auto_per_hour` in bbs_set, default 12; see BBS). Active only while
  machine mode is on.

The top-level numbers add up the active sources:
- `per_hour`: the total cap. 0 when a source has no cap
  (`--parts-resend-per-hour 0`); `room` is then -1.
- `used`: sends counted in the last hour. `room`: how many more may go now.
- `resets_at`: Unix time the oldest counted send ages out of its hour and
  room opens again; 0 when nothing counts against the cap.

`sources` gives the same for each source, with `active`. Ordinary traffic
the operator sends, ACKs and orderwire have no per-hour cap.

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

`tuner` is the LDG tuner daemon's status block, passed through unchanged
(`--tuner-socket`; see Tuner below and greybeard-ale docs/ldg-tuner.md 5.4):
`contract_rev`, `channel {ant_rx, ant_tx, recalled_on}`, `relays.antenna`,
`keyed`, `tx_locked`, `meter` and the rest. It is read at most once a second.
It is left out when no tuner socket is set or nothing answers on it.

`keying` says whether the core can ask gale-key to key (see "Keying: the core
asks gale-key" below):

    "keying":{"gale_key":"not answering","socket":"/run/gale-key/key.sock",
      "why":"gale-key isn't running (no socket at /run/gale-key/key.sock): nothing is keyed, bursts stay written",
      "standing":[],"held":0,"answers_written":0}

- `gale_key`: `answering` (the last request had its reply), `busy` (gale-key
  took the last request and hadn't answered within the timeout, as while it
  keys; with `why`), `not answering` (with `why`), `not asked yet`, or `none`
  (no gale-key set up for this station: the loopback, STANAG 5066 and
  Appendix A links, and training).
- `standing`: the standing modes on (`machine`, `routing`, `auto_qsl`).
- `held`: y dialogs holding a token now.
- `answers_written`: ACK and orderwire bursts in the outbox with no result
  from gale-key yet. They key only under the standing mode `machine`.

`radio_channel` is the channel the radio is on, as `tuner_channel` last set
it, with or without a tuner daemon (see Tuner). Clients read it here; the
operator never types it:

    "radio_channel":{"label":"P1","world":"mars","since":"2026-11-26T15:00:00Z","source":"tuner_channel"}

- `label`: the channel's designator. Never a frequency.
- `world`: the world of the channel plan's entry for `label` (`mars` or
  `amateur`), read when the call is answered; null when the plan holds no
  channel by that name.
- `since`: when it was set, UTC.
- `source`: always `tuner_channel`.
- `label`, `world` and `since` are null when no channel has been set since
  the core started.

## Worlds: MARS only, never amateur (ask 37)

Alex's hard rule: there is no forward or auto-copy route between MARS and
amateur, ever. The core is the MARS message center, so every item it holds
is MARS:

- Every message (`list`, `get`, the in-box row) carries `"world":"mars"`.
- Every station-book entry carries `world` (`mars` default, or `amateur`);
  `book_put` takes it, and `book_import`/`book_export` have a `world` column.
- Every routing rule (`route_list`) carries `"world":"mars"`; `route_set`
  with any other world is refused.

A send to a station of the amateur world is refused in code, with an error
that says why, and is never rerouted or queued. A station is amateur when it
is one of this station's own amateur addresses (the scanner's
`amateur/own_address.txt`), when it was heard on the amateur channels (the
amateur word log), or when the station book marks it `amateur`. The check is
in `Core::submit`, so it covers `send` (a collective holding one amateur
station refuses the whole send, before any copy is queued), `compose_send`,
`send_acp127`, `send_acp126`, `send_chat`, `retransmit`, `qsl` receipts, net
call-ups, the BBS's machine answers and routed sends, and the resend queue;
`requeue` and `release` check again, and so does the airtime point itself: a
queued item whose station became amateur after it was queued is failed
there ("refused at the air"), and an ack or answer to one is dropped with a
log line. A routing rule naming an amateur ALE address is refused.

A call that carries `world` other than `mars` is refused, except calls that
only read or set up (`word_feed`, `heard_stations`, `scanner_status`,
`settings_*`, `identity_*`, `keying_settings`, `book_put`, `book_get`,
`book_list`, `station_health`, `captures_list`, `status`, `channel_put`,
`channels_list`, `channels_get_full`, and `form_get`, `form_check` and
`form_submit`, whose action is checked again when it runs). The refusal reads
`refused: world amateur: the MARS message center never sends, copies or
forwards into the amateur world`; a station refusal reads `K0TEST is an
amateur station (heard on amateur channels); the MARS message center never
sends to the amateur world`. Broadcasts (ALL) are not a station and still go.

### Channels of the amateur world (station design v1.9, ask 40)

Each channel in the plan carries `world`, `mars` (the default) or `amateur`,
and an optional HFLink `label` (see Station files). An amateur channel is
there for CHANNELS and to listen on. The core's routing and automatic sends
use only MARS channels:

- `net_open`, `sched_put` and `net_callup` naming an amateur channel are
  refused (`channel: A1 is an amateur channel; a MARS net meets on a MARS
  channel (ask 40)`);
- a burst written while the radio was on an amateur channel is never sent to
  gale-key (Keying, below);
- `bundle_make` leaves amateur channels out, and `bundle_load` never changes
  a channel this station keeps for the amateur world;
- a frequency is named by a MARS channel's designator; the radio side names
  one held only by an amateur channel by its label (its designator when it
  has none), and a schedule imported with a frequency never takes an amateur
  channel's designator.

### The BBS's own automatic sends (ask 40)

gale-term only guards the switch that turns routing on, so the core guards
every automatic path itself. The BBS never answers, QSLs, lists for QRU, lets
another station pull, or routes on a message whose world isn't `mars` or that
came from a station of the amateur world. Each is refused with a log line
(`BBS: ID not answered or routed: it came from K7AMA is an amateur station
...`), never sent somewhere else:

- received traffic and machine commands: no QSL, no answer, no route;
- held traffic (QRU, ZEE pulls, `route_rerun`): an amateur sender's traffic
  is never held;
- a routed message is checked again when it comes due, so one whose sender
  became amateur, or whose world changed, after it was routed is `refused`
  with `why`;
- `bbs_set ack_to` naming an amateur station is refused, and every answer is
  checked before the hourly gate;
- unacked traffic to a station later known as amateur is failed when that
  station is heard ("not sent again"), never sent again.

Behind these, every send the BBS makes goes through `Core::submit`, and every
resend (unacked, ack timeout, `requeue`, the rule's next ALE address) passes
the airtime check, so no rule or resend can skip the guard.

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
the reply names the key used. `"hold":"Y"` queues it held, to go when it is
released (as the resend queue does for a net slot).

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
FLASH, IMMEDIATE, PRIORITY, ROUTINE, oldest first within each). After them
come the messages whose burst is `written` to the outbox and waits for its
key, in the same order. Each message has the same fields as `list`, `state`
included; `limit` counts both.
With `--link-gate`, a message whose station has no link yet carries
`"waiting_for_link":true`; it goes as soon as one is reported, and the log says
"waiting for a link to B" and then "link to B is up". ACKs are never held.

## log

`{"cmd":"log","after":0,"limit":100}` →
`{"ok":true,"events":[{"seq":41,"ts":1795269300,"id":"AAR4XX-17","text":"queued IMMEDIATE for AAR4XY"}, ...],"last":41}`

Oldest first. Pass the reply's `last` as `after` next time to get only new
lines.

No line quotes a capture's file name that carries its dial (the engine's
`_<dial>kHz` tag), nor the listing or payload files named after it. Each
becomes `a capture on DESIGNATOR` when a channel in the plan has that dial,
else `a capture`. Each line goes to standard output as the log holds it.

## list, get

`{"cmd":"list","limit":50}` and `{"cmd":"get","id":"AAR4XX-17"}` →
`{"ok":true,"messages":[{"id","direction","from","to","prec","precedence","state","attempts",
"created_at","updated_at","body","sends","last_error"?}]}`

`sends` counts how many times a message went out whole on the air.

### Following a message to `written`, `sent` or `failed`

The core pushes nothing: every reply answers a request, on the API port and
on the WebSocket alike. A burst is written on the core's own thread, a
moment after the send, so a screen polls for it:

- `get {id}` every second or so while its `state` is `queued` or `sending`.
  `written` means the burst is in the outbox and the y dialog
  (`key_prepare`) can open. Keep polling, more slowly, until `sent` (gale-key
  keyed it) or `failed` (`last_error` says why). `updated_at` changes with
  every state.
- Or poll `queue`: a message stays in it while `queued`, `sending` or
  `written`, each with its `state`, and leaves it once `sent` or `failed`.
- `log` with `after` carries the same steps as lines ("written to the outbox
  as ...", "keyed by gale-key", "not keyed by gale-key: ...") for the
  activity log; read the state from `get` or `queue`, not from the text.

`list` with `dir`, `get`'s in-box extras and `log` with `stream` are under
"In box" below.

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

With the setting `book-auto-add-heard` on (ask 30; off by default), a MARS
station the core hears (any frame from it) that the book doesn't know is
added as a `station`, its call its name, `source` `heard`, `world` `mars`.
An entry already there is left as it is, and a station of the amateur world
is never added.

`send` uses the book: a station name goes to its call, and a collective sends
one copy to each member, returned as `ids`. A name the book doesn't have goes as
written. `send` to `ALL` is a broadcast: every station takes it, none acks it,
and it is marked sent once it is on the air. No station may be called ALL.

### Nets

- `net_open` `{name, roster, exercise, ncs, note, channel}` returns `id`
  (NET-n) and the net. `roster` can name collectives; they are expanded when
  the net opens. `ncs` defaults to this station. `channel` (optional) is the
  net's channel by its designator, one in the channel plan and of the MARS
  world (an amateur channel is refused, ask 40). A net opened
  from a schedule takes the schedule's channel, and its sitting's end. The
  net shows `channel` and `sitting_end` (0: none).
- `net_checkin` `{net, station, traffic, remark}` checks a station in by hand.
  `traffic` is how many messages it holds for the net.
- `net_checkout {net, station}` checks a station out.
- `net_close {net}` closes the net and returns it with its check-ins.
- `net_status {net}` returns the net with `checkins`, `missing` (on the roster
  but not heard) and `in_roster` for each check-in.
- `net_list {limit}` lists nets, newest first.

`net` can be left out while exactly one net is open. When the core hears any
frame from a station (a message, an ACK or traffic for someone else), the net
it was heard on checks that station in with `via: "radio"` (audit C):

- A net with a channel takes only stations heard on that channel: the one
  the radio was last set to with `tuner_channel`. With no channel known, no
  net with a channel takes anyone by radio.
- A net with no channel takes stations heard only while it is the one net
  open; with two or more open, it takes nobody by radio.
- A net opened from a schedule during its sitting takes nobody by radio
  after the sitting's end, though it stays open.
- A station of the amateur world (see Worlds) is never checked in.

A station that checked out isn't put back in by radio. The operator can
still check anyone in by hand. `status` adds `net`, the open net's id.

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
- `sched_remove {name}` returns `{ok, rx_file}`.

What gale-rx needs (ask 28) is part of each schedule:

- `sched_put` also takes `plan`, `index` and `mode`. `plan` is the label of
  a scan plan in gale-rx's `net_plans` folder (`net03` for
  `net_plans/net03.plan`): 1 to 32 letters, digits, `-` or `_`, with a
  letter, never a frequency, its case kept. `index` is the plan's channel
  gale-rx holds, 0 to 999 counted from 0 in the plan's file order; with none,
  gale-rx takes the plan channel named by `channel`, else the plan's first.
  `mode` is `USB`, `LSB`, `CW` or `AM`, given only with an `index` (gale-rx
  sets a mode only with one; blank is USB). An `index` needs a `plan`. Each
  left out keeps what the schedule had; sent empty, it is cleared.
- `sched_list`, `sched_next` and `sched_put`'s reply show `plan`, `index`
  (a number, or `null` for none) and `mode` on each schedule.
- `sched_put` and `sched_remove` answer `rx_file`: `written`, `off` (no
  `rx-schedule-dir` set: no file is written) or `not written: WHY`.

With `rx-schedule-dir DIR` set (a host setting in appliance.conf; none by
default), the core writes `DIR/net_schedule.txt` and `DIR/slot-channels.txt`
on every `sched_put` and `sched_remove`, at start, and whenever its half-minute
look at the schedules finds them changed (an import, a bundle, a restore, or
a week-of-month day coming into the week). The format is in docs/formats.md,
"The gale-rx schedule file". A failed write is logged once.

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
  designator), `world` (`mars` or `amateur`), `label` (its HFLink label, or
  ""), `mode`, `use`, `source`, `ant_rx` and `ant_tx`. Never a frequency.
- `channels_get_full {}`: the same, with each channel's `frequency_hz` (a
  whole number of Hz) after `label`. **It is the one reply that carries a
  frequency** (Alex's ruling 4 of 2026-11-26, asks 27 and 29: CHANNELS is the
  one screen that shows one). It is for gale-web's CHANNELS screen only, and
  each read is logged (`channels_get_full: the channel plan read with its
  frequencies (N channels)`), never with a frequency. Every other reply, log
  line and event stays frequency-free.
- `channel_put {name, frequency?, unit?, mode?, use?, ant_rx?, ant_tx?,
  world?, label?}`: enters or changes one channel by hand. `frequency` reads
  as kHz (5000), unless `unit` is `khz`, `mhz` or `hz`. **Left out, a channel
  already in the plan keeps its frequency**, so a change to its ports, mode,
  use, world or label needs no retyping; a new channel needs one, and
  `frequency` sent empty is refused. A mode, use or antenna left out keeps
  the old one; a mode left out or blank is `USB` on a new channel. The
  CHANNEL form leaves out a mode left blank, so it never resets a held
  sideband. `world` is `mars` or `amateur`; left out, it keeps the
  channel's (`mars` for a new one). `label` is the channel's HFLink label: 1
  to 16 letters, digits or `- / . _`, with a letter, never a frequency, kept
  in capitals; left out it is kept, sent empty it is cleared. `ant_rx` and `ant_tx` are the tuner
  ports the channel receives and transmits on: `1` or `2`, blank meaning 1;
  anything else is refused. Left out, each keeps the channel's port (the
  CHANNEL form leaves out a port left blank). A new channel, and every channel in a plan
  written before these fields, is 1 and 1. A frequency card imported again
  keeps the ports set by hand, and the world and label. Returns `name`,
  `result` (`added`, `replaced` or `same`), `frequency_changed`, `mode`,
  `use`, `ant_rx`, `ant_tx`, `world` and `label`. The frequency is stored like
  an imported one and never comes back in a reply or the log; only
  `channels_get_full` reads it back.
- `channel_remove {name}`: takes a channel out of the plan.
- `sched_put` also takes `channel`, a designator, and `sched_list` returns it.
  A frequency there is refused, and so is an amateur channel (ask 40).

## Tuner: the LDG AT-200PC through the core

The tuner's driver is greybeard-ale's daemon (`ldg-tuner/1`, its
docs/ldg-tuner.md section 5). The core forwards these calls to its socket,
set with `--tuner-socket PATH` or `tuner-socket` in the settings file, and
passes the reply through: `ok`, `result` as the daemon gave it, and on a
refusal `code` and `error` as the daemon gave them (`refused-antenna`,
`refused-power`, `refused-no-channel`, `refused-keyed`, `tuner-absent`,
`busy`, `timeout`, ...). `tuner` is the daemon's whole status block,
unchanged: `contract_rev`, `channel {ant_rx, ant_tx, recalled_on}`,
`relays.antenna` and the rest. With no socket set, `tuner_status` answers ok
with `tuner` null, `tuner_channel` records the channel (below), and the others
answer `no-tuner`.

A channel is named by its designator. A frequency never comes in or goes out
on this port: the core looks it up and sends it only to the local socket. A
request holding `freq_hz` is refused.

- `tuner_status`: the status block.
- `tuner_channel {channel}`: a channel change. Sends the daemon
  `tuner_channel {freq_hz, ant_rx, ant_tx}` from the channel plan; it
  switches to `ant_rx` with RF off and recalls the memory for `ant_tx`.
  Returns `channel`, `steps`, `result` and `tuner`. A channel outside 1.8 to
  54 MHz is refused (`refused-range`) without asking the daemon. The core
  records the channel whether or not a daemon is set up: it is the channel
  gale-key keys each burst on (see "Keying: the core asks gale-key"). On a
  station without the LDG daemon, gale-rx's `tuner_channel` call sets it. With
  no daemon set up or answering, ok: there is nothing to tell it.
- `tuner_recall {channel}`: recalls that channel's memory.
- `tuner_bypass`, `tuner_store`, `tuner_pre_tx`, `tuner_post_tx`: as the
  daemon's calls.
- `tuner_antenna {port}`: `1` or `2`.
- `tuner_set {L, C, hilo}`: L and C 0 to 127, hilo `hi` or `lo`.
- `tuner_tune {kind, confirm, watts?, store?}`: refused while transmit is
  pinned to gale-tx/WAV (`refused-tx-locked`), as it is today, because a
  tune is a transmit. Unpinned, it is refused under EMCON, without the
  operator's `confirm` of `Y`, or above 25 W (`refused-power`; `watts`
  defaults to 10). Then: `tuner_pre_tx`, a carrier of `watts` keyed by CAT or
  PTT, `tuner_tune`, `tuner_store` (unless `store` is `N`), carrier off,
  `tuner_post_tx`.

The core keys nothing itself: gale-key does (see "Keying: the core asks
gale-key"). Around every `key` the core sends gale-key, the operator's and the
standing ones alike, it runs the daemon's sequence:

1. `tuner_pre_tx`, with RF off: the tuner switches to the channel's `ant_tx`.
2. `key` to gale-key, only when `tuner_pre_tx` answered ok. When it refused,
   gale-key isn't asked, the burst stays `written`, and the reason
   (`tuner_pre_tx CODE: why`) is shown: in `key_confirm`'s error for the
   operator's y, and in the message's log for a standing key, which is tried
   again 30 s later.
3. After gale-key's reply, keyed or not, `tuner_post_tx` (back to `ant_rx`),
   retried on `refused-keyed` with a bounded backoff, each try in the log.
   gale-key answers a key when it has ended. When its reply came after
   `--gale-key-timeout-ms` (it was still keying), the core first waits until
   gale-key's `status` says it is no longer keyed (at most 130 s, gale-key's
   120 s key cap and a margin). When `tuner_post_tx` is never taken, the
   message's log says the tuner didn't go back to the RX antenna.

With no tuner daemon set up, or none answering on its socket, gale-key is
asked exactly as without one. Keying is by CAT or a hardware PTT line only:
a keyer that keys any other way, VOX above all, is refused.

## Modem decodes: unverified, whole

Every burst greybeard-ale's 110A or 110D modem decodes (each payload file its
`CAPTURE.modems.json` lists, its size and sha256 checked) goes into the inbox
as it came, before the core reads it (issue 3 ruling: flagged UNVERIFIED, no
bytes hidden):

- id `DEC-` and the first 12 hex digits of the burst's sha256; `direction`
  `in`; `core_state` `unverified`, and `"unverified": true` on each `list`,
  `get` and `search` row; from and to `UNKNOWN`; precedence R.
- The body is every byte: printable ASCII and line feeds as they are, a
  backslash as `\\`, tab as `\t`, carriage return as `\r`, any other byte as
  `\xNN`, so the bytes can be had back exactly.
- Nothing about it is checked by the core and nothing is acked. The log line
  names the waveform, the byte count, the payload file and the sha256.
- The core still reads the same bytes as usual, so a message, ack or chat it
  finds in them is its own row too, with its own state.
- The same bytes heard again (another capture of the same burst) aren't stored
  twice.
- `status` counts them as `unverified`. They aren't in `pending` (not a
  message to act on) or its errors.

## The radio side: scanner, calls heard, health, recordings (gale-web)

greybeard-ale's scanner, decoder and clock write files and answer on the
scanner's control socket; the core reads them and passes them on, so gale-web
needs only port 4100 (issue 3). The core does no ALE or modem work here.

**Channels go by name, never by frequency.** A frequency read from a file or
the scanner becomes a name before it leaves the core: the channel's
designator in the channel plan, else its label in the scan plan
(`record`/`voice HZ:LABEL`), else its place in the scan plan, `W<window>C<channel>`
(1-based), else, in a scanner's reply, its place in the running window the
reply names, else `unlisted`. No reply carries a key with `khz`, `hz` or `freq`
in it.

**Where the files are.** Settings (config keys, or the same as `--options`),
each defaulting to `GALE-DIR/<usual name>`: `gale-dir` (default `.`; the
terminal image runs the core in `/var/lib/gale/local`), `words` (a prefix: the
file itself, else the newest `PREFIX-YYYYMMDD.jsonl`, else `PREFIX.jsonl`),
`scan-log`, `wwv-dir`, `gps-status` (`gps/status.json`), `health-file`,
`engine-status`, `lqa-db` (read only), `favourites`, `own-address`,
`scan-plan` (`plans/scan.plan`), `radio-file`; recordings come from
`captures` (else `GALE-DIR/captures`). On Gangleri they point under
`/srv/greybeard-store/greybeard-ale/gangleri/`.

**A store that isn't there.** A missing file or folder never fails a call: the
part that needed it says `"available": false` with `why`. With `store-root`
set, every reply below carries `"store": {"root", "mounted", "why"?}`;
`mounted` false means the drive isn't mounted on it, so what the reply shows
is not the store.

### scanner_status

`{"cmd":"scanner_status"}` returns `{ok, source, reachable, polled_at,
last_ok_at, error?, view, scanner, scanner_stale?, incoming?, store?}`.

- `view`: `incoming` (a call to our own address in its world, MARS or
  amateur, heard in the last 60 s; then `incoming {caller, channel, world,
  end}`), `scanning`, `hold`, `recording`, `wwv`,
  `starting`, `stopping`, or `no scanner` (no `link-socket`, or it isn't
  answering).
- `scanner`: the scanner's whole status reply (greybeard-ale
  `src/control_socket.h`), polled every `link-poll-ms`, with its channels
  named: `state`, `plan`, `windows`, `window`, `channels`,
  `channel_labels`, `channel_count`, `visit`, `visit_s`, `visits`,
  `stream_unix`, `hold` (`{window, left_s, record_channel?, record_label}`
  or null), `order_pending`,
  `plan_pending`, `engine`, `visit_remote`, `clock` (`wwv`|`host`),
  `clock_counter`, `wwv_blocks`, `last_wwv_unix`, `plan_switches`, `note`.
  The window's channels come out the same from either scanner: `channels`
  is their names in window order (a label, or `W<window>C<n>` for a channel
  with none), `channel_labels` the labels (null for none) and
  `channel_count` how many. A newer scanner (greybeard-ale
  docs/amateur-world.md) sends `channel_labels` and a count; an older one
  sent `channels_khz`, which is named here and dropped (a dial named nowhere
  is `W<window>C<n>` by its place in the reply's `window`, as above). That older form is
  read for one more release only. The newer scanner's world fields pass
  through as they are: `world` (`mars`, `amateur` or `both`),
  `channel_worlds`, `cycle_s`, `cycle_long`, `amateur_channels`,
  `amateur_auto_channels`, `trim_available`, `trim_channels`, and with an
  identity file `own`, `listen_only`, `own_error`.
  Fields the scanner adds later pass through, named the same way. Null
  before the first good reply; when the scanner stops answering, the last
  one stays with `scanner_stale: true`.

### scanner_hold, scanner_release, scanner_record, scanner_plan

Orders to the scanner, forwarded to its control socket as the tuner calls are
to the tuner daemon. (`hold` and `release` are the message calls; these have
their own names.) Each returns the scanner's own reply, named the same way:
`{ok:true, order, window?, seconds?}` or `{ok:false, error}`. The scanner
takes an order between visits: `scanner_status` shows `order_pending` until
it has. None of them transmits.

- `scanner_hold {window, seconds?}`: stay on plan window `window` (1-based),
  for `seconds` (above 0, at most 900; default 600).
- `scanner_release {}`: scan again.
- `scanner_record {channel, seconds}`: hold that channel's window and record
  it (seconds above 0, at most 900). `channel` is a name as above: a
  designator or `W<w>C<c>` is looked up and sent to the local socket only;
  any other name goes as the scanner's own label.
- `scanner_plan {name}`: switch to plan file `name` in the scanner's plan
  folder (`[A-Za-z0-9._+-]`, no path).

Refused by the core, before anything is sent: a bad window or seconds, an
empty or malformed channel or plan name, no `link-socket`.

### settings_list, settings_set, settings_reset, keying_settings

The station's settings as the operator sees them (src/settings.h). Every
setting is one row of a table compiled into the core. Alex's ruling
(2026-11-26): only host plumbing is kept from the operator. Store paths,
ports, programs and link plumbing are `locked` and stay in appliance.conf;
the operator edits every other setting, the station call included.

- `settings_list {group?, all?}` returns `{ok, settings[], file, records[]}`.
  `records` is each kind of record and its class (ask 26, below). Each row:
  `{key, group, label, type, class, gate, applies, choices?, min?, max?,
  unit?, value, default, source, pending_restart, help?, warning?}`, or for
  an `action` row (`amateur-calls`, `cipher`) `call` in place of the value:
  the call that sets it. `type` is `text`, `int`, `bool` (`yes`/`no`),
  `choice`, `call`, `list` or `action`. `source` is `operator`, `command
  line`, `config`, `world file` or `default`. `applies` is `now` or
  `restart`; `pending_restart` is true when the saved value differs from the
  one in force since start. `group` filters (Station, Worlds, Keying,
  Display, Traffic, Book); locked rows show only with `all:"Y"`.
- `settings_set {key, value, confirm?}`: `key` takes `-` or `_`
  (`ptt_line`). A row with `gate:"confirm"` (station, world, tx-world, and
  cat-ptt when turned on) answers `{ok:false, confirm_needed:true, warning}`
  until it is sent again with `confirm:"Y"`. Reply: `{ok, key, value,
  changed, applies, restart_needed, also_set[]?, keys_made?, fingerprint?,
  note?}`. A new station call has its keys made at once (`fingerprint`) and
  starts its own message store at the next start. A new `world` is written to
  GALE-DIR/world, the file the scanner picks its world from; when tx-world no
  longer fits it, tx-world follows (`also_set`).
- `settings_reset {key}`: drops the operator's value; the config's or the
  default stands again.
- `keying_settings {}` returns `{ok, keying, file}`, `keying` being the
  object in the keying file.

The operator's values are kept in DB_DIR/operator-settings (`key = "value"`,
mode 600), written whole by the core, never by hand. The core never writes
appliance.conf. At start they are read after appliance.conf and the command
line, so the operator's word stands; a line no longer valid is skipped with
a log line and never stops the core.

The new rows (asks 35, 36):

| key | values | applies |
|---|---|---|
| `world` | `mars`, `amateur`, `both` (default: GALE-DIR/world, else mars with GALE-DIR/net_plans, else amateur) | now |
| `tx-world` | `mars` or `amateur`, one of the worlds worked (default: amateur for an amateur station, else mars) | now |
| `ptt-line` | `RTS` or `DTR`; must match switch S1001 in the SCU-17; unset until chosen | now |
| `cat-ptt` | `yes`/`no`, default `no`; turning it on needs the operator's yes (a PTT-line kill switch won't stop it) | now |
| `engineering-readouts` | `yes`/`no`, default `no` | now |
| `book-auto-add-heard` (group Book, ask 30) | `yes`/`no`, default `no`: MARS stations heard are added to the book | now |

Two host rows are new as well, locked like the other host plumbing:
`rx-schedule-dir` (where gale-rx's schedule files are written, ask 28) and
`lock` (the kinds of record the host locked, ask 26).

**The keying file** (`keying-file`, default GALE-DIR/keying.json, mode 644)
is for gale-key, greybeard-ale's host keying service. The core writes it
whole (temp file renamed over it) at start and on every change of a keying
row:

```
{"version":1,"ptt_line":"RTS"|null,"cat_ptt":false,"world":"both",
 "tx_world":"mars","engineering_readouts":false,"station":"CALL",
 "updated_at":UNIX}
```

The core never keys the radio. There is no VOX setting, and none will be
added.

### Record classes (ask 26)

The station's records come in kinds, each with a class: `operator` (the
default: the kiosk changes it) or `locked` (the core refuses every call that
would change it). Only the host locks a kind, with a `lock = KIND` line in
appliance.conf (repeatable, or `--lock KIND`); the kiosk can't. An unknown
kind stops the core at start, with the list of kinds.

| kind | label | calls refused while it is locked |
|---|---|---|
| `channels` | Channel plan | `channel_put`, `channel_remove`, `mars_import`, `bundle_load`, `station_restore` |
| `schedule` | Net schedule | `sched_put`, `sched_remove`, `mars_import`, `bundle_load`, `station_restore` |
| `book` | Station book | `book_put`, `book_remove`, `book_import`, `mars_import`, `bundle_load`, `station_restore` |
| `key-tape` | Key tape | `key_import`, `key_open`, `key_zeroize`, `net_tape`, `station_restore` |
| `cipher` | Cipher module | `cipher_install`, `cipher_remove`, `handout_import`, `station_restore` |
| `cards` | Cards and keymasters | `card_import`, `card_check`, `card_keymaster`, `keymaster_accept`, `keymaster_refuse`, `station_restore` |
| `handouts` | Member handouts | `handout_import`, `station_restore` |
| `forms` | Form templates | `form_template_put`, `form_template_reset`, `station_restore` |
| `guide` | Guide pages | `guide_page_put`, `guide_page_reset`, `station_restore` |
| `bbs-rules` | BBS guard list, aliases and routes | `guard_set`, `alias_set`, `alias_del`, `route_set`, `route_del`, `route_move`, `station_restore` |

The check is made once, before any handler runs, so it covers `form_submit`
too: a form's action comes back through the same door. A refusal is
`{ok:false, error:"locked: Channel plan (channels) is locked in
appliance.conf on this box: channel_put can't change it from here",
refused:"locked", record:"channels"}`, and the core logs `refused
channel_put: channels is locked in appliance.conf`. Reading is never
refused. `settings_list` gives `records: [{kind, label, class, calls[]}]`.

### identity_get, identity_set (the amateur world's own addresses)

The calls this station answers to in the amateur world, kept apart from
MARS's `own-address` file (greybeard-ale docs/amateur-world.md). The
kiosk's startup call prompt saves through `identity_set`.

- `identity_get {}` returns `{ok, world:"amateur", file_found, addresses[],
  listen_only}`.
- `identity_set {addresses}`: `addresses` is one string, the calls separated
  by spaces or commas (`"N0CALL N0CALL@"`); each is 1 to 15 of A-Z, 0-9 and
  @, case-folded, duplicates dropped, at most 32. An empty string means no
  call: the scanner listens only. The core writes the `amateur-own-address`
  file (default GALE-DIR/amateur/own_address.txt) whole: a temp file renamed
  over it, file mode 600, folder 700. It then sends `reload-identity` to the
  scanner's control socket. Reply: `{ok, world, addresses[], listen_only,
  scanner_reloaded, scanner_own[]?, scanner_own_error?, note?}`. When the
  scanner can't be told, the save still stands and `note` says the scanner
  reads the file at its next start.

Refused: a bad address (the file is left as it was) and the amateur file set
to MARS's own-address file.

In training (ask 39) `identity_set` checks the addresses exactly as above and
writes nothing: the amateur address file belongs to the whole box, not the
practice station, and the scanner reads it live, so practice must never change
what the real station answers to. The reply is `{ok, training:true, world,
addresses[], listen_only, written:false, scanner_reloaded:false, note}`; a bad
address is refused as usual. `identity_get` in training reads the real file.

### word_feed

`{"cmd":"word_feed","world":"mars","limit":50,"since":UNIX,"incoming_s":60}` returns the
ALE transmissions in the newest word log, newest first, put back together as
greybeard-ale's `lqa_db.py` does (per channel; a gap over 1.2 s starts a new
one):

- `world`: which world's log this is, `mars` (the default) or `amateur`.
  The worlds keep separate logs (`words`, and `amateur-words`, default
  GALE-DIR/amateur/words) and never mix in one reply.
- `transmissions[]`: `{start, end, channel, world, caller, called[], kind, words,
  sinad, gaps, open, amd[]}`. `world` is the words' own, else the channel's
  `chan` line in the plan, else `mars`. `kind` is `sounding`, `call`, `call-partial`,
  `call-end` or `unknown`; `gaps` counts words decoded across lost samples;
  `open` is true while the channel may still be adding words (5 s); `amd` holds
  the short text messages sent in it.
- `incoming`: the newest call to one of our own addresses in that world in
  the last `incoming_s` seconds, `{caller, called[], channel, world, end}`,
  or null. MARS calls are checked against the `own-address` file, amateur
  ones against `amateur-own-address` (default
  GALE-DIR/amateur/own_address.txt); `own_address_file` and
  `amateur_own_address_file` say whether each was found.
- `last_word {unix, channel, world, window, sinad, engine}`, `last_hour {words, gaps,
  local, station, remote, primary, partial}` (`partial`: the log read didn't
  reach an hour back). The counts go by each word's `engine`: `station` is the
  terminal's own engine and `remote` the overflow engine, both primary decodes
  (`primary` is the two together); `local` is ale-scan's in-process fallback.
  A word with no engine named counts in `words` only.
  `source` (the word log's file name), `own_address_file`.

### heard_stations

`{"cmd":"heard_stations","limit":200}` returns `stations[]`: `{address, name,
favourite, last_heard, last_channel, heard_24h, heard, channels, best_sinad}`,
the most recently heard first, then favourites not heard yet (`last_heard`
null). `source` is `lqa` (the LQA database, read only; stamps before the replay
cut-off left out) or `word log` (no database: from the recent calls).
Favourites come from the favourites file (`ADDRESS [name]` per line); the box
doesn't edit it.

### station_health

`{"cmd":"station_health"}` returns:

- `clock {set_by: wwv|gps|none, wwv {fix, fixed_at, age_s, stale (over 8 h),
  offset_ms, sigma_ms?, tcxo_ppm?, why?, sync_running, sync_min?, last_fix
  {t, block?, count_err_ms?, count_hours?}|null}, gps {available, fresh (10 s),
  age_s, fix, sats, source, host_minus_utc_ms?, uncertainty_ms?, pps_age_s?,
  device_connected?}}`. The WWV band is not passed on.
- `health {available, state, age_s, stale (15 min), problems[]}`.
- `engine {available, age_s, stale (60 s), connected, last_frame_age_s?,
  overloads_total?, gap_visits?, local_visits?}`.
- `scanner_log {available, state: running|stopped|stale (2 min), age_s, since,
  stopped, link: up|down|null, last_visit: engine|local|null, overloads,
  restarts}`: counts since the last start, read as gale-term read them.
- `radio {available, radio, why, t?}`: the launcher's own word, e.g. "no
  radio" and why.
- `words_last_hour {words, gaps, local, station, remote, primary, partial}`,
  counted as in `word_feed`.
- `scanner {reachable, state, engine, clock, why?}` or null.
- `sources`: the files read.

### captures_list

`{"cmd":"captures_list","limit":20}` returns `total` and `captures[]`, newest
first: `{name, size, open, start?, seconds?, trigger?, channel?, window?,
clock?, lost_s?, modems, payloads}`. `open`: no sidecar yet (being written, or
cut off). `modems`: a modem verdict file is beside it; `payloads`: decoded
payload files. Playback isn't ruled on and isn't offered. A voice decode
derived from a capture, `<capture>.m<n>.melpe<rate>.wav` (n 1-11), is a
decode, not a recording: it is not listed and not counted in `total`.

With `--captures-fallback` set, the fallback folder the engine writes to while
the data store fails is listed too. A capture in both is listed once, from the
store. The reply then also has `store_folder` and `fallback_folder` (each:
the folder is there), and each capture `fallback` (true: it is in the
fallback folder). It is `available` when either folder is there.

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
  Given the id of one part of a long tape, it gathers the set; with parts
  missing it returns `ok` false, `error`, `tag`, `from`, `parts`, `have`,
  `missing_parts` and `id`.
- `keys_list`: `keys`, each with `indicator`, `net`, `tape`, `from`, `until`,
  `state`, `check` (to read by voice), `cipher` (the module the key is for) and
  `usable` (false when that module isn't in the slot).
- `key_zeroize {which}`: wipes one key (`QXR01`) or a whole tape (`QXR`).
  Returns `zeroized`, the count.

Checked against the code at this tip (ask 39): every shape above is what the
core answers. Two things changed since fb1ec9f. In training these four calls
work on the practice station's own keyring (DB_DIR/CALL.training/), never the
real one, and each reply carries `"training":true`. And a call that carries a
`world` other than `mars` is refused before it runs (ask 37).

### This station's keys

- `station_card {out}`: `station`, `fingerprint` and `card`, the text to
  hand out. `out` writes the card to a file too (a stick) and returns
  `written`.
- `station_backup {passphrase, out}`: `backup`, the station keys and held cards
  enciphered with the passphrase (12 characters or more), and `cards`, the
  count. `out` writes it to a file and returns `written`.
- `station_restore {text | file, passphrase}`: puts a backup back. It must be this
  station's. Returns `fingerprint`, `cards` (restored), `saved` and `age_days`
  (for dated backups) and `refused`.
- `rally_status`: for a box coming back on a bad day. `station`,
  `fingerprint`, `keymasters` (each `station`, `fingerprint`), `cards`,
  `keys_now` (each `indicator`, `net`, `check`), `undecided` (keymasters
  waiting on the member's answer, each `station`, `fingerprint`, `question`)
  and `next`, the step to take. When a keymaster is waiting, `next` asks about
  it first.

### Other stations' cards

- `card_import {text | id | file, source, replace}`: loads a card from text,
  a stored message or a file. Returns `station`, `fingerprint`, `result` (`added`, `same`
  or `replaced`), `checked` and `next`. A different card for a station already
  held is refused unless `replace` is `Y`.
- `cards_list`: this station and its `fingerprint`, and `cards`, each with
  `station`, `fingerprint`, `checked`, `keymaster`, `source` and `added`.
- `card_check {station, fingerprint, keymaster}`: marks a card checked when the
  fingerprint read back by voice matches. Case and spaces don't matter. With
  `keymaster=Y` it is also the member's acceptance of that keymaster, as
  `keymaster_accept`, and returns `handouts` the same way.
- `card_keymaster {station, keymaster}`: names a checked card a keymaster
  (`keymaster=N` takes it back). Only a keymaster's sealed tapes open here.
- `handout_import {text | id | file | packet, replace}`: loads a member handout, new or stale.
  `packet` is the path of a packet folder (`handout.txt`, and `cipher/` when
  the net uses another cipher module); the module is installed only when its
  set hash matches the handout's signed `CIPHER` line. Also returns `cipher`
  (the module the handout names, when it names one) and `cipher_installed`.
  It must be for this station and signed by its issuer. A different card for
  a station already held needs `replace=Y`. Loading it names no keymaster:
  `keymaster_choices` has one entry per keymaster line, each `station`,
  `fingerprint`, `choice` (`accepted`, `refused` or `undecided`) and, when
  undecided, `question` to put to the member ("NCS-A K7QM 2XRD 9FTA HW3C wants
  to be your keymaster. Accept?"). While the issuer is undecided the handout
  waits on the box (`waiting_on_member` true): no tape is opened, no cipher
  module installed, and its member cards are held unchecked. If the issuer
  was refused, nothing from it is kept. The keymasters' cards are held checked
  once the member accepts. Returns `net`, `by`, `issued`, `cards`, `stale`,
  `keymasters`, `keymaster_choices`, `waiting_on_member`, `tape_opened` (with
  `added`, `already`, `expired` when it opened), `next` when there's a step
  to take, and `refused`.
- `card_send {to}`: sends this station's card as a plain message. Returns `id`.

### Choosing keymasters

The member chooses whose tapes this box takes (docs/crypto.md, Choosing your
keymasters). These are the calls a panel needs to put that choice to them.

- `keymaster_accept {station, fingerprint, how}`: the member accepts
  `station` as a keymaster. `fingerprint` is the one read back by voice (or
  in person; `how` is `by voice` or `in person`, for the log) and must match
  the card held. Marks the card checked and keymaster, clears any refusal,
  and loads every handout waiting on that station. Returns `station`,
  `keymaster` (true) and `handouts`, one handout reply (as
  `handout_import`) for each that was waiting.
- `keymaster_refuse {station}`: the member refuses `station`. Its keymaster
  mark comes off, the refusal is kept against its fingerprint, and handouts
  waiting on it are dropped. Returns `station`, `keymaster` (false),
  `handouts_dropped` and `next`.
- `keymaster_list`: `accepted` (each `station`, `fingerprint`, `source`),
  `refused` (each `station`, `fingerprint`) and `undecided` (each `station`,
  `fingerprint`, `from_handout_by`, `question`).

### Startup bundles

The station book, channel designators, net schedules and link formats, signed
by the station that made them (docs/crypto.md, Startup bundles). A member's
own copy carries the frequencies sealed to that member's card; no frequency
is ever in the clear, in a reply or in the log. A bundle never changes a
card, key or keymaster.

- `bundle_make {label, out, for}`: builds this box's bundle. `out` is a file
  path to write it to (a stick). `for` makes that member's copy, with the
  frequencies sealed to its card when the card is held and checked here.
  Returns `tag`, `by`, `fingerprint`, `made`, `book`, `channels`,
  `schedules`, `links`, `forms` (counts; forms changed from the built-ins
  ride along), `bytes`, `parts` (on the air), `for`,
  `frequencies` (`sealed`, `no_card`, `card_not_checked`, `none_held`, or
  `not_sealed` with no `for`), `frequencies_sealed` (count), `text`,
  `written` when `out` was given, `next` when the copy has no frequencies,
  and `changed`: what was cut or left out (characters the radio can't send,
  frequency-like text).
- `bundle_send {to, label, text | file}`: sends a bundle to each station in
  `to` (calls, spaces or commas) as signed plain traffic, in parts when it
  is large. With `text` or `file` it sends that bundle as it is; otherwise
  each station gets its own copy, as `bundle_make for=`. Returns `by`,
  `parts` (total), `sent` (each `station`, `tag`, `parts`, `frequencies`,
  `ids`), `tag` when one bundle was given, and `failed`.
- `bundle_load {text | file | id, accept}`: loads a bundle. From `id`, every
  stored part with the same tag and signer is gathered; with parts missing it
  returns `loaded` false, `tag`, `from`, `parts`, `have`, `dropped`,
  `missing_parts` and `next`. Always returns `tag`, `by`,
  `fingerprint`, `signer` (`own`, `checked`, `unchecked`, `unknown` or
  `different`, when the card in the bundle isn't the one held), `label`,
  `made` and the counts. From a signer this box hasn't checked, without
  `accept=Y`, it returns `loaded` false, `needs_yes` true and `question` to
  put to the member, and changes nothing. Loaded, it returns `loaded` true,
  `signer_checked`, `book_added`, `book_replaced`, `book_same`,
  `schedules_added`, `schedules_replaced`, `schedules_same`,
  `channels_updated`, `links_set`, `for`, `frequencies` (`opened`,
  `sealed_to_other`, `not_opened`, `none_for_you` for a copy made for this
  station without them, or `none`), `frequencies_added`,
  `frequencies_changed`, `frequencies_same`, `frequencies_kept` (held here
  on another frequency and kept, because the signer isn't checked),
  `forms_added`, `forms_replaced`, `forms_same`, `forms_kept` (changed on
  this box, so kept) and `forms_held` (not taken: the signer isn't checked),
  `need_frequency` (designators this box still has no frequency for),
  `problems` and `next`. A bundle that was changed is
  an error.

### Parts of long tapes and bundles

A tape or bundle over 6000 bytes goes as numbered, signed parts
(docs/crypto.md, Parts).

- `parts_list {all}`: `hold_s`, `sets` and `requests`. Each set has `kind`
  (`tape` or `bundle`), `tag`, `from`, `parts`, `have`, `state` (`waiting`,
  `dropped`, or with `all=Y` also `complete`), `missing`, `id` (a part, for
  `key_open`, `bundle_load` or `parts_request`), `drops_in_s` while waiting,
  and `dropped_parts` and `refused_parts` when there are any. `requests`
  lists the requests that came in and are still open (all of them with
  `all=Y`): each has `id`, `from`, `kind`, `tag`, `missing` and `states`, one
  per part: `waiting` (in the queue), `held` (for a net slot), `sent`,
  `flagged` (asked again after resends), `not_ours` (never sent to that
  station) or `unchecked` (the signature can't be checked here).
- `parts_request {id}`: asks the set's sender for the missing parts, by
  number, in a signed message. Returns `to`, `tag`, `id` (the request sent)
  and `asked`.
- `parts_resend {id} | {to, tag, parts}`: sends parts again, for a request
  that came in (`id`; its signature must check) or as named (`parts="2 4"`).
  Only to a station they were sent to before. Returns `to`, `tag`,
  `resent` (each `part` and `id`), `not_found` and `failed`. Serves a flagged
  request too, by hand.
- `parts_queue`: the resend queue, pooled. Returns `summary` (one line for the
  operator), `sends`, `stations`, `items` (each `kind`, `tag`, `part`, `to`:
  a station or `ALL` when several asked, and `asked_by`), `flagged` (each
  `station`, `tag`, `part`, `request`), `held` (each `id`, `tag`, `part`,
  `goes_at`), `unchecked`, `not_ours`, `per_hour`, `sent_last_hour`, `room`
  (-1 with no cap), `emcon`, and `rows` for the RESENDS screen: one per
  request a station sent (signed, for parts this station sent it), at most
  `limit` (default 100), open ones first. Each has `request`, `message` (the
  first part this station sent of that block, to open in MSG-VIEW), `kind`,
  `tag`, `title`, `station`, `parts` ("2 4"), `asked`, `age_s` and `state`:
  `WAIT` (can go now), `HELD-EMCON`, `HELD-CAP` (past this hour's room),
  `FLAGGED` (asked again after two resends), `HELD-NET` (held in the outbox
  for a net slot), `SENT` or `DROPPED`.
- `parts_queue_send {when, net, request}`: sends the queue, or with `request`
  only the parts that request asked for (its flagged parts too: the
  operator chose to), each still pooled with every station asking for the
  same part. Returns `request` when given. `when=NOW` (the default;
  refused during EMCON) or `when=NET` with `net` naming a net on the schedule
  (default: the next to sit); parts for a slot still to come are held and go
  out when it starts. Up to the hourly cap. Returns `when`, `net`, `goes_at`,
  `sent` (each `id`, `tag`, `part`, `to`, `stations`), `sends`, `stations`,
  `held`, `left_for_cap` and `room_at` when the cap held some back, `failed`
  and `left` (what stays in the queue).
- `parts_queue_drop {request, part}`: drops a request's parts still waiting
  or flagged (one of them with `part`): they won't be resent. Returns
  `request`, `station`, `dropped`. Refused when nothing is left to drop.

### Keymaster

- `net_tape {net, start, days, period_hours, members, keymasters, send, handouts, out}`:
  makes a tape for `net` (`start` is YYYYMMDD or YYYYMMDDHHMM UTC, default
  today 0000Z; 31 days of 24-hour keys by default), loads it here, and seals a
  copy to each station in `members` (calls or station-book names, spaces or
  commas). `keymasters` names alternates: they get the tape too, and are named
  keymasters on every handout. Returns `net`, `tape`, `sealed` (each with
  `station`, `checked` and `text`, plus `id`, `parts` and `ids` when `send`
  is `Y`: a tape over 6000 bytes goes in numbered parts) and `no_card`
  (members whose card isn't held). With `handouts=Y` it also returns
  `handouts`, each with `station`, `has_tape`, `alternate` and `text`
  (docs/crypto.md). `out` is a folder (a stick): each handout is written
  there as `HANDOUT-<STATION>.TXT`, given as `file`, and implies
  `handouts=Y`.
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
  slot. Keys for the removed module stay held but aren't used. With no module
  installed it answers `ok` false, `error` "no installed cipher module".

`cipher_install` and `cipher_remove` are refused in training (the slot is the
real station's). `cipher_status` there describes the practice station's slot,
which always holds `pna-aes`.

## Working with MSC stations

See `docs/cp-link.md` for what these follow from.

### States

Outgoing: `queued`, `sending`, `sent`, `acked`, `failed`, and:

- `unacked`: went out 1 + `ack_retries` times with no ack. Goes again by
  itself when its station is heard.
- `written` (greybeard modem): the burst is in the outbox but nothing has
  keyed it yet. The core asks gale-key to key it: on the operator's y, or by
  itself under a standing mode (see "Keying: the core asks gale-key"). It
  becomes `sent` only when gale-key's key result says it was keyed, and
  `failed` ("not keyed: why") when gale-key refused. See "Transmit: written,
  then sent" below. `cancel` and `requeue` work on it.
- `held`: the operator held it.
- `cancelled`: the operator cancelled it.
- `expired`: still not through `--expire-h` hours after it was filed.
- `failed` also covers a message that waited `--link-wait` seconds for a link.

Incoming: `received`, and:

- `overheard`: traffic for another station.
- `garbled`: a copy that failed its check, or was cut short. Not acked.

### emcon

`{"cmd":"emcon","on":"Y"}` → `{"ok":true,"emcon":true}`. Nothing is sent,
acks included, until `{"cmd":"emcon","on":"N"}`. Without `on` it only reports.
It holds across a restart.

### hold, release, cancel, requeue

`{"cmd":"hold","id":"NCS-A-12"}` → `{"ok":true,"id":"NCS-A-12","state":"held"}`

- `hold`: a queued message waits until released.
- `release`: a held message goes back in the queue.
- `cancel`: a queued, held, sent, unacked or failed message never goes again.
  Not while it is on the air.
- `requeue`: an unacked, failed, cancelled or held message goes again with
  fresh counts.
- After a `release` or a `requeue` the message is the operator's own, even
  when it was an automatic send (machine, routing, auto-QSL): no standing
  grant keys its new burst, only `key_prepare` and the operator's y.

Each gives `"ok":false` with an `error` when the message can't take that step.

### link_format

`{"cmd":"link_format","station":"STA-C","format":"plain"}` →
`{"ok":true,"station":"STA-C","format":"plain","links":[{"station":"STA-C","format":"plain"}]}`

How the core talks to a station: `pna1` (the default, framed and acked) or
`plain` (the message text alone, for an MSC station). Without `format` it
reports. ALL can't be set.

## ACP 127 handling: orderwire, raw receive log, overdue, files

More in docs/cp-link.md, "ACP 127 handling".

`{"cmd":"orderwire","to":"STA-B","text":"QSL QRU"}` → `{"ok":true}`: one
line of printable text, at most 512 characters, sent as typed ahead of
traffic. No `to` means ALL. The log says `orderwire to STA-B: QSL QRU`.

`{"cmd":"rx_raw","limit":4000}` → `{"ok":true,"text":"== 2026-11-24T04:10:00Z 20 bytes\nSTA-B DE STA-C QRV\n..."}`:
the end of today's raw receive log, non-printing bytes as dots. Empty when
the log is off.

`{"cmd":"overdue"}` → `{"ok":true,"ids":["STA-A-12"]}`: messages past their
handling time and still queued, held or unacked.

`send_file` takes the same fields as `send_acp127`, plus `file_name` (1 to
40 bytes, no slash) and `file_b64` (the file, base64). Any `text` goes
before the file. The reply is `send_acp127`'s.

`{"cmd":"get_file","id":"STA-A-12"}` →
`{"ok":true,"name":"net-log.txt","size":300,"file_b64":"..."}`, or an error
saying why the file can't be read (no file, cut short, wrong size or CRC).

`requeue` takes an `expired` message too.

## BBS: machine mode, guard list, routing

The core side of the BBS-SETUP, BBS-GUARD and BBS-ROUTES screens
(docs/gale-term-mm-bbs-screens.md, section 3). Code in src/bbs.h. State lives
in the station's database (`bbs_*` tables). All of it is off until the
operator turns it on.

Nothing here keys the radio by itself unless the operator turned machine mode
on (and routing on, for routed traffic). Every automatic send is refused or
waits while EMCON is on, counts against `auto_per_hour`, and goes into the
ordinary queue, so it shows in the Out Box and Activity and goes out the
pinned transmit route. A command heard while machine mode was off is never
answered later.

Each automatic send carries its standing mode: routed traffic `routing`, a
QSL receipt `auto_qsl`, every machine-mode answer `machine`. Its burst is
keyed only while the core holds that mode on, which takes the operator's y
(`standing_prepare`; see "Keying: the core asks gale-key"). `bbs_set`
turning `machine_mode` or `routing` off, or `ack` to `off`, turns the matching
standing mode off and tells gale-key to revoke it. Turning one on grants
nothing by itself.

### Settings

- `bbs_get` returns the settings: `machine_mode`, `suffix` (`-CM`),
  `machine_call` (this call with `-CM`), `accept`, `ack`, `ack_delay_ms`,
  `ack_to`, `commands_ct_only`, `max_errors`, `accept_malformed`,
  `zee_multi`, `zee_max_bytes`, `data_rate_bps`, `duty_s`, `zee_limit` (the
  ZEE answer size used), `remote_sync`, `duplicates` (the rule, as text),
  `purge_days`, `auto_mark_days`, `auto_per_hour`, `auto_sent_last_hour`,
  `routing`, `broadcast_ri`, `version`, and `auto_send` with
  `auto_send_why_not` (empty, or `machine mode is off`, `EMCON is on`,
  `the hourly cap of N automatic sends is used up`, ...).
- `bbs_set` takes any of those keys and returns the same reply. A bad value
  refuses the whole call and changes nothing.

| key | default | values |
|---|---|---|
| `machine_mode` | false | answer `-CM` commands |
| `accept` | `calls` | `calls` (traffic to this call), `calls_ris` (and RIs on the guard list), `all` (everything heard) |
| `ack` | `auto` | `auto` sends a QSL receipt for plain ACP 127 traffic received, `off` |
| `ack_delay_ms` | 2000 | 0 to 600000 |
| `ack_to` | empty | empty is the sender; a call. Never `ALL` |
| `commands_ct_only` | true | act on commands only when they came enciphered |
| `max_errors` | 3 | a message with more parse warnings isn't held |
| `accept_malformed` | false | hold those anyway |
| `zee_multi` | false | ZEE may send more than one message |
| `zee_max_bytes` | 0 | 0: worked out from `data_rate_bps` x `duty_s` / 8 (256 to 8192) |
| `data_rate_bps`, `duty_s` | 600, 120 | for that |
| `remote_sync` | false | refused either way for now |
| `purge_days`, `auto_mark_days` | 30, 0 | `purge_days` is the age `purge` uses (In box, below); `auto_mark_days` is stored, not acted on yet |
| `auto_per_hour` | 12 | automatic sends an hour, all kinds; 0 is none |
| `routing` | false | send matching traffic on by the rules |
| `broadcast_ri` | empty | struck from routed traffic, never picks a route |

### Machine commands

Another station sends an ACP 201 line to this call with `-CM`:
`AAM499-CM DE AAR9AA // QRU // 1737Z K`. Enciphered, it is a codress message
whose text starts with that line; the compose form takes no `//`, so single
slashes (`AAM499-CM DE AAR9AA / QRU / 1737Z K`) are read the same. The answer
goes to the sender as one block: `HHMMZ`, `DE <call>-CM`, the lines, `AR`.

- `VER`: `VER PNA MESSAGE CORE BBS 1`.
- `QRU [pattern,...]`: what is held here for those calls or RIs (default: the
  sender). `QTC n [patterns]`, then one line each:
  `n - <lines> <originator> #<serial> <filed> ---> <recipients>`. `QRU` when
  none. Lines the sender has QSL'd aren't listed again.
- `ZEE n[,n...] [COMP]`: sends lines of the sender's last list. With
  `zee_multi` off, the first only, and it says so. Long answers come as pages
  of at most `zee_limit` bytes, each starting `PAGE k OF m`. `[COMP]` is
  noted; this station sends uncompressed.
- `ZDK [n]`: page n of the last ZEE answer again (all pages with no n).
- `QSL n[,n...]`: marks those lines QSL by the sender. No answer.
- Anything else, or a plain command with `commands_ct_only` on, gets one
  `ERROR ...` line saying why.

`bbs_qsl {id}` returns `by`, the stations that sent QSL for that message.

### Guard list and RI alias map

- `guard_list` returns `items`. `guard_set {items}` replaces the list (comma
  list; an empty string clears it). A `-CM` call is refused.
- `alias_list` returns `aliases`, each `ri` and `members`.
  `alias_set {ri, members}` (members separated by `;`), `alias_del {ri}`.

### Routing rules

Each rule: `id`, `pos`, `match` (RI, partial RI or call; `*` at the end says
the same), `route` (a name), `ale` (ALE addresses in calling order), `key`
(our cipher: a key indicator or net name; empty sends as received), `sched`
(empty or `HHMMZ`; a time that has passed today means tomorrow), `compress`
(noted, sent uncompressed), `mark_processed` (default true). First match by
position wins.

- `route_list` returns `routing`, `broadcast_ri`, `auto_send`,
  `auto_send_why_not`, `rules`, and `routed` (the last 20: `msg_id`, `rule`,
  `state` `due`/`queued`/`refused`/`failed`, `out_id`, `due`, `ale_pos`,
  `processed`, `why`). `ale_pos` is which of the rule's ALE addresses it is
  on (0: the first). Each rule also has `"world":"mars"`.
- `route_set {id?, match, route, ale, key, sched, compress,
  mark_processed}` adds a rule, or changes rule `id` keeping what isn't
  named. Returns `id`. Needs at least one ALE address; an unknown key is
  refused.
- `route_del {id}`, `route_move {id, dir}` (`up` or `down`) or
  `{id, to}` (a position).
- `route_rerun {dry_run}` runs the rules over held traffic not routed yet.
  A dry run unless `dry_run` is `false`. Returns `dry_run`, `count`, `items`
  and `auto_send_why_not`.

A routed message goes to the rule's first ALE address with the broadcast RI
struck off; the log names the whole calling order. When the copy to one
address fails, expires or goes unacked, the rule's next address is called
with the same message (the unacked copy is cancelled first, so the message
isn't delivered twice), and `ale_pos` moves on. An address the core refuses
(this station, or a station of the amateur world) is passed over for the
next. When no address is left, the routed message is `failed` with why; a
last copy that is unacked is left as it is, so it still goes if that
station is heard. With `key` set, plain
traffic is enciphered in our cipher slot first (it needs a date-time group
line). Traffic this station originated isn't routed.

## In box: the message screens (MSG-INBOX, MSG-PENDING, MSG-VIEW, MSG-SEARCH, MSG-ACTIVITY, MSG-REPORTS)

The core side of docs/gale-term-mm-bbs-screens.md, section 3. The operator's
marks (read, processed, discarded, QSL, tag, a corrected copy) are kept in
the station's database (`inbox_flags`); the message as received is never
changed. Each mark is written to the message's own events. Training mode
answers these calls from the practice store.

Principle 5: nothing here keys the radio by itself. `retransmit` needs
`confirm=Y`, and `qsl` queues a receipt only with `send=Y`. Both go into the
ordinary queue, so EMCON holds them and they show in the outbox. A discarded
message is no longer held for the BBS (QRU never lists it).

### In-box row

`list {dir:"in"}`, `pending` and `search` return rows:
`{"record","id","direction","dtg","prec","from","to","serial","type","state","core_state","mine",
"read","processed","discarded","delivered","format_error","enciphered","tag","corrected","created_at","qsl":[...]}`

- `state` is the in-box state, first that applies: `DISC` (discarded),
  `PROC` (processed, by the operator or by a routing rule), `DELIV` (sent on
  by a routing rule), `QSL` (a QSL from this station or by machine command),
  `READ`, `NEW`. `core_state` is the store's state (`received`, `overheard`,
  `garbled`).
- `record` is the row number (MM's record #). `dtg` is the message's DTG, its
  time filed, or when it came in. `type` is the USMTF MSGID type, if any.
- `mine`: addressed to this station (not overheard). `format_error`: heard
  damaged (`garbled`); these are on the errors list.
- `qsl` names the stations that QSL'd it.

### list with dir

`{"cmd":"list","dir":"in","mine":false,"hide_delivered":false,"include_processed":false,"limit":50,"after_id":"..."}`
→ `{"ok":true,"dir":"in","messages":[rows],"more":true,"last_id":"..."}`

Newest first. `include_processed` also shows processed and discarded ones.
Pass `last_id` as `after_id` for the next page. `dir:"out"` lists sent
messages the same way. `list` without `dir` is the plain call above.

### get: MSG-VIEW

`get` adds `inbox` (one row), `events` (the message's own log, oldest
first: `seq`, `ts`, `text`), `props` (`precedence`, `compress`, `encrypt`,
`key` (the indicator, never key material), `source`, `destination`, `ale`,
`data_rate`, `interleave` (both empty: they belong to the modem),
`checksum` (`pass`/`fail`/`none`, the MSC digest)), `decoded` (the clear
text of a message in our cipher when this station holds the key, else empty,
with `decoded_note` "No key for this message on this station.") and
`corrected_text`. `{"read":"Y"}` marks it read (MSG-VIEW opening it).

### pending, mark, mark_all

- `{"cmd":"pending","all":false,"limit":200}` →
  `{"pending_count","errors_count","last_processed","pending":[rows],"errors":[{"record","id","dtg","from","raw","errors":[...]}]}`.
  Pending: received for this station, not processed or discarded (`all`
  adds overheard traffic). Errors: heard damaged, not discarded.
- `{"cmd":"mark","id","state"}`: `processed`, `discarded`, `reviewed` (read)
  or `new` (clears all three). Returns the new `state`. Messages this
  station sent are refused.
- `{"cmd":"mark_all","list":"pending|errors","state"}`: `state` is
  `processed` or `discarded` (default: processed for pending, discarded for
  errors). Returns `marked` and `ids`, the ids it marked.
- `{"cmd":"reply_draft","id"}`: a reply to a message received, filled in
  for MSG-COMPOSE. Nothing is sent or stored. Returns `template` (GENADMIN
  for ACP 127 traffic, FREE_FORM for chat and anything else), `procedure`
  (the one the message came in, when the template takes it; empty for the
  template's own), `to` (the original FM line, else the sender's call),
  `to_station` (the call to send to over the air), `subject` (the
  original's SUBJ line, as it was; empty when it had none), `prec` (the
  original's), `values` (the same, by compose field key: `to`, `to_station`,
  `prec`, `subj`, and `routing` for ACP 127 plaindress) and `in_reply_to`
  (`id`, `from`, `subject`, `serial`). Pass `values` to `compose_check`
  and `compose_send` as the form's starting values.

### qsl, tag, correct, export

- `{"cmd":"qsl","id","send":"N"}` marks it QSL by this station. With
  `send=Y` it also queues MSC's receipt line to the originator, for plain
  ACP 127 traffic only (`not_sent` says why not: PNA1 traffic was acked on
  the air). Returns `sent`, `out_id`, and `emcon` when EMCON holds it.
- `{"cmd":"tag","id","text"}`: one line, 80 characters at most, no
  frequency. Empty clears it.
- `{"cmd":"correct","id","body"}` keeps a corrected copy beside the
  original (`E` in MSG-VIEW). Empty removes it. `retransmit` and `export`
  use it.
- `{"cmd":"export","id"}` writes the message as received (an enciphered one
  stays enciphered) to `<station folder>/export/<id>.txt`, and the corrected
  copy to `<id>.corrected.txt`. Returns `folder` and `files`.

### retransmit (T in MSG-VIEW)

`{"cmd":"retransmit","id","to","ale"?,"de"?,"key"?,"compress"?,"lines"?,"prec"?,"route"?,"confirm":"Y"}`
→ `{"ok":true,"out_id","to","key","route","route_note","emcon"?,"compress"?}`

Refused without `confirm=Y`: the screen sends it only after the operator's
`y` on the confirm panel. Goes to `ale` if given, else `to`. `de` must be
this station or empty (it sends as itself). `lines` picks lines of the
message (`3-5,9`), not of an enciphered one. `key` (an indicator or a net
name) enciphers in our cipher slot; the message needs a DTG line. `compress`
is noted and sent uncompressed (no compression yet). `route` is written to
the log; everything goes out the station's one transmit route (`route` in
the reply) until transmit is unlocked.

### search (MSG-SEARCH)

`{"cmd":"search","sender","serial","dtg","recipient","types":"SITREP,...","filters":"delivered,unprocessed,processed,discarded","expr","limit":200}`
→ `{"ok":true,"filter":"<effective filter, for line 22>","more":false,"messages":[rows]}`

Case-blind. `sender`, `recipient` and `dtg` match anywhere; `serial` is the
whole value; `%` is a wildcard in all four. `filters` are OR'd; with none,
discarded messages are left out. `expr` is the advanced line:
`field op value` joined by `AND`, `OR`, `NOT` and parentheses. Fields:
`from, to, serial, dtg, type, prec, state, tag, id, key, text`. Ops: `=`
(whole value, `%` wildcards), `!=`, `~` (contains). Quote values with
spaces: `text ~ "ALL QUIET"`. The core reads it into its own tree; nothing
of it reaches SQLite. A bad expression is refused with the character where
it went wrong.

### log streams and activity counters (MSG-ACTIVITY)

`{"cmd":"log","stream":"io|app|msg","after":0,"limit":100,"q":"..."}` →
`{"ok":true,"stream","events":[{"seq","ts","id","text","dir"?,"route"?}],"last"}`

- `io`: traffic in and out (`dir` `in`/`out`; `route` on `out`), and BBS
  answers and machine commands.
- `app`: the core's own lines (no message id).
- `msg`: lines about messages. With `q`, those whose message id, sender,
  serial or tag match, or whose text has `q` (the EVENTS tab search).

Poll with the reply's `last` as `after`. `log` without `stream` is the
plain call above.

`{"cmd":"activity_counters"}` →
`{"ok":true,"session_since","watched_routes","routing","stored","received_session","processed_session","duplicates_session","purged_session"}`.
The session starts when the core last started. `watched_routes` counts the
routing rules. `purged_session` counts what `purge` deleted since then.

### purge (BBS-SETUP: purge processed/discarded after N days)

`{"cmd":"purge","dry_run":"Y","days":30,"limit":200}` →
`{"ok":true,"dry_run":true,"days":30,"count","in","out","purged","kept":{"pending","owed_qsl","routing_due","on_qru","part_resend"},"items":[{"id","direction","state","age_days"}]}`

A dry run unless `dry_run` is `N`: the screen shows `count` first, then
asks (default No). `days` defaults to the BBS setting `purge_days`. It runs
only when called; nothing is purged by itself.

- In box: messages marked processed or discarded more than `days` ago.
- Out box: acked, cancelled, failed or expired, last changed more than
  `days` ago.
- Never: anything not yet processed or discarded (`pending`), a sent
  message still waiting for its ack or QSL, processed plain traffic this
  station hasn't QSL'd (`owed_qsl`), routed traffic not yet sent
  (`routing_due`), a message on a QRU list just given (`on_qru`), a sent part
  a resend may need (`part_resend`). `kept` counts the old ones held back,
  by reason.

The message and its plain copy go; its event lines stay. One log line says
how many went.

### report (MSG-REPORTS)

`{"cmd":"report","kind":"station_log|comtrafstat|comtrafsum","from":"YYYY-MM-DD","to":"YYYY-MM-DD","lines_per_page":60}`
→ `{"ok":true,"kind","from","to","lines","text","pages":[...],"draft"?}`

UTC days, both included; default today. Lines are 69 characters or fewer.
- `station_log`: the station log's entries and every message in and out,
  by time.
- `comtrafstat`: the status now (the dates aren't used): pending action,
  outgoing not yet delivered, the count of formatting errors.
- `comtrafsum`: counts for the period by precedence, received and sent,
  acknowledged, not delivered, overheard, errors, processed and discarded.
  `draft` (`template` FREE_FORM, `prec`, `subject`, `text`) opens in
  MSG-COMPOSE; nothing is sent from here.

`{"cmd":"report_export","kind":...,"from":...,"to":...}` (the same fields as
`report`) writes that report's `text` to the station's export folder, beside the
database, as `KIND-FROM-TO.txt` (gale-term's name; an earlier file of the same
name is replaced), and returns `{"ok":true,"kind","from","to","folder","file","bytes"}`.
It is MSG-REPORTS `e`. A bad kind or day gets `report`'s own error and writes
nothing. In training it writes to the practice station's folder, as `export` does.

## Transmit: written, then sent

The core never keys the radio. With the greybeard modem it writes each burst
to the outbox as `BURST.wav` and marks the message `written`. The core is the
caller that asks greybeard-ale's gale-key to key it, over gale-key's socket
(next section). gale-key keys it (or refuses), and for every key whose WAV is
in the outbox, socket keys included, it writes one result file beside it,
whole, by writing `.BURST.key.json.tmp` and renaming it to `BURST.key.json`:

```
{"version":1,"burst":"BURST.wav","result":"keyed","why":"","at":"2026-11-26T15:00:00Z"}
```

- `result` is `keyed` or `refused`; `why` says why it refused (shown to the
  operator as "not keyed: why").
- On `keyed` the message becomes `sent` and its ack clock starts then.
- The core reads and removes each result file. One it can't read, or one
  naming another burst, is renamed `.key.json.bad` and changes nothing.
- A result for an ack or orderwire burst is logged only.
- There is no timeout: a burst stays `written` until a result comes.
- The result file is the one source of truth. gale-key's reply on the socket
  is logged and shown to the operator, and never changes a message's state.
- `status` counts `written`. `tools/stub_channel.py --key-results` writes
  these files for testing.

## Keying: the core asks gale-key

gale-key (greybeard-ale, docs/gale-tx.md) is the station's one keying
service. The core asks it to key a burst already in the outbox; it never
keys the radio itself, and there is no VOX. The socket is `--gale-key-socket`
(default `/run/gale-key/key.sock`; `none` sets no gale-key), with
`--gale-key-timeout-ms` (default 5000) for its reply. Only a station on the
greybeard modem has one. Training refuses every call here.

The caller of each request, and what it sends:

| When | The core sends gale-key |
|---|---|
| `key_prepare` | `{"cmd":"prepare","kind":"air","wav":"/abs/OUTBOX/BURST.wav","world":"mars","channel":"P1","control":"local","final":false}` |
| `key_confirm` on a send | the same with `"cmd":"key"` and `"token"` |
| an automatic send, once written | `{"cmd":"key","kind":"air","wav":...,"world":"mars","channel":"P1","control":"automatic","final":false,"standing":"routing"}` |
| an ACK or orderwire, once written | the same with `"standing":"machine"` |
| `standing_prepare` | `{"cmd":"prepare","kind":"standing:routing"}` |
| `key_confirm` on a mode | `{"cmd":"grant","mode":"routing","token":...}` |
| `standing_off`, or the BBS mode turned off | `{"cmd":"revoke","mode":"routing"}` (or `"all"`) |
| `key_status` | `{"cmd":"status"}` |

A burst is sent to gale-key only when all of these hold. Otherwise the call is
refused with the reason, or, for an automatic send, the reason is logged once:

- it is a burst of a message of ours, still `written`, and gale-key hasn't
  been asked to key it yet (once asked, its result file decides);
- its world is the message's, and it is `mars`: the core never asks gale-key
  to key an item of the amateur world, nor one to a station of the amateur
  world (ask 40);
- it has a channel: the channel the radio was last set to through
  `tuner_channel` when the burst was written, with or without a tuner daemon.
  A burst written with no channel set is never sent to gale-key; requeue it
  once one is set;
- that channel isn't one the channel plan keeps for the amateur world
  (station design v1.9, ask 40): a burst written on an amateur channel is
  never sent to gale-key, by the operator's y or by a standing mode;
- EMCON is off.

Each `key` goes inside the tuner's sequence when a tuner daemon is set up
(see "Tuner" above).

### ACK and orderwire bursts

An ACK or an orderwire written to the outbox is not a message: the core keeps
it in memory only, until gale-key's result for it comes. It is keyed under the
standing mode `machine`, once, on the core's own thread, as an automatic send
is:

- its world is that of the message it answers, or, for an orderwire or an ACK
  of something not held, that of the station it goes to. It is keyed only in
  MARS: the core never asks gale-key to key one for the amateur world (ask 40).
  One to a station the station book marks amateur after it was written is never
  asked, and the log says so once;
- its channel is the one the core was on when it was written;
- with no `machine` grant it stays in the outbox, unkeyed, and `status` counts
  it in `keying.answers_written`;
- its result is logged only (see "Transmit: written, then sent").

### key_prepare, key_confirm, key_cancel: the operator's y

`{"cmd":"key_prepare","msg_id":"ID"}` (or `"burst":"BURST.wav"`), sent when
the kiosk opens its y dialog →

    {"ok":true,"prepare_id":"KP-0123456789abcdef","expires_s":120,
     "confirm":{"kind":"air","world":"mars","channel":"P1","control":"local","id":false,"ptt":"rts",
                "msg_id":"ID","burst":"BURST.wav"}}

A prepare is only for a burst already written: `msg_id` or `burst` must name
a message of ours in state `written` whose `BURST.wav` is in the outbox now.
An empty call, an unknown message or burst, a message in any other state, or
a burst whose WAV is gone is refused, and gale-key isn't asked. gale-key binds
its token to the WAV's bytes, so a prepare never covers a burst written later.

The core asks gale-key to prepare, which runs every gate and answers with a
one-shot token. The core holds the token against `prepare_id`. The token
never leaves the core: not in a reply, not in the log. `confirm` is gale-key's
summary for the y, and only the fields the kiosk shows: `kind`, `world`,
`channel` (the label), `control`, `id` (whether the ID goes out), `ptt`. No
frequency goes to the kiosk: any text from gale-key that names one is
replaced by "(gale-key's text named a frequency; not shown)". A refused
prepare holds nothing and returns gale-key's reason. A new `key_prepare` for
the same burst drops the token held before it.

The held token is bound to that exact burst: the WAV's path and sha256 (read
when the prepare is made), the message's world and the channel. It belongs to
the connection (API socket or gale-web's WebSocket) that opened the dialog: it
is confirmed only there, and dropped when that connection closes (a page
reloaded with the dialog open). One never confirmed or cancelled is dropped at
`expires_s` (gale-key's 120 s), whether or not anything asks for it again.

`{"cmd":"key_confirm","prepare_id":"KP-...","confirm":"Y"}`, sent on the
keyboard y →

    {"ok":true,"kind":"air","msg_id":"ID","burst":"BURST.wav","asked":true,
     "gale_key":{"reached":true,"answered":true,"ok":true,"exit":0,"reason":""},
     "note":"the message stays written until gale-key's BURST.key.json says keyed (sent) or refused (failed)"}

The core marks the burst asked and sends `key` with the token. `gale_key` is
gale-key's reply, frequency-free; `answered` is false when gale-key took the
request but answered after the timeout (it may still be keying). The message
stays `written` until the result file comes. The call is refused, and nothing
is sent:

- without `confirm` of `Y` (the core never makes up a Y; the dialog stays held);
- sooner than 0.5 s after the prepare (gale-key would refuse it; still held);
- after `key_cancel`, after the token was used, once it is past `expires_s`
  (gale-key's 120 s at most), or after its connection closed: open the dialog
  again;
- on another connection than the one that opened it;
- when the WAV is no longer in the outbox, or its sha256 differs from the
  prepare's;
- when the message is no longer `written` with that burst, its world or
  channel changed, gale-key was already asked to key it, or the checks above
  no longer hold.

When gale-key can't be reached, nothing keys: the call says why, the burst
stays `written` and may be prepared again.

`{"cmd":"key_cancel","prepare_id":"KP-..."}` → `{"ok":true,"prepare_id":"KP-...","dropped":true}`
drops a held token (`dropped` false when none was held). Send it when the
operator answers No or leaves the dialog.

### standing_prepare, standing_off: automatic sends

Routing, machine-mode answers and auto-QSL key under a standing grant per
mode, `machine`, `routing` or `auto_qsl`. The core never asks for
`amateur_auto`. A mode goes on only through the same prepare and y:

`{"cmd":"standing_prepare","mode":"routing"}` →
`{"ok":true,"prepare_id":"KP-...","expires_s":120,"confirm":{"kind":"standing","mode":"routing","world":"mars"}}`

then `key_confirm` with that `prepare_id` and `confirm` `Y` sends `grant` →
`{"ok":true,"kind":"standing","mode":"routing","on":true,"gale_key":{...}}`.
It is refused under EMCON, and when gale-key's transmit world isn't `mars`.
`key_cancel` drops it as for a send.

While a mode is on, each written burst of that mode is keyed with
`"standing":MODE`, once, on the core's own thread. When gale-key can't be
reached, it is tried again 30 s later. An automatic send the operator
requeues or releases is his own from then on: no grant keys it, only his y.

`{"cmd":"standing_off","mode":"routing"}` (or `"all"`) →
`{"ok":true,"mode":"routing","on":false,"gale_key":{...}}`. The mode is off in
the core at once, whatever gale-key answers, and gale-key is sent `revoke`.
EMCON turned on turns every mode off the same way. gale-key also revokes
grants itself (EMCON, the hourly cap, a change of transmit world). A burst
keyed under a grant gale-key no longer holds comes back refused, and the
message is `failed`; gale-key's reply turns that mode off in the core, so the
next bursts wait for the operator's y. gale-key has no exit code of its own
for this (it is exit 2, as every refusal is), so the core reads its wording:
a reason starting "no standing grant for MODE", for the mode it asked under. The core holds its modes in memory
only, as gale-key does: after a restart every mode is off until the operator
gives the y again.

### key_status

`{"cmd":"key_status"}` → `keying` as in `status`, `gale_key` (the reply), and
`station`, the parts of gale-key's own status the kiosk shows:

    "station":{"ready":true,"keyed":false,"ptt":"rts","transmit_world":"mars",
               "why_not":[],"grants":["routing"],
               "channel":{"label":"P1","world":"mars","since":"2026-11-26T15:00:00Z","source":"tuner_channel"}}

`station.channel` is the core's own `radio_channel` (see status), the same
object, so the kiosk shows the channel next to gale-key's state. `station` is
there only when gale-key answered.

