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

## log

`{"cmd":"log","after":0,"limit":100}` →
`{"ok":true,"events":[{"seq":41,"ts":1795269300,"id":"AAR4XX-17","text":"queued IMMEDIATE for AAR4XY"}, ...],"last":41}`

Oldest first. Pass the reply's `last` as `after` next time to get only new
lines.

## list, get

`{"cmd":"list","limit":50}` and `{"cmd":"get","id":"AAR4XX-17"}` →
`{"ok":true,"messages":[{"id","direction","from","to","prec","precedence","state","attempts",
"created_at","updated_at","body","last_error"?}]}`
