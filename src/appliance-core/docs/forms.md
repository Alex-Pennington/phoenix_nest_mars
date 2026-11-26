# Forms from templates

A member who has only this box can't edit a settings file. Everything a box
needs from level zero goes in through a form: the call, the station book, the
frequency card, net schedules, startup bundles, keymasters, key tapes,
handouts and backups. Every form is drawn from a plain-text template, so a
region can change a form, or add one of its own, on the box and send it out
signed in a startup bundle. No rebuild, no restart.

gale-term draws any form from `form_get` and sends it back with
`form_submit`. It needs no code per form.

## Where the idea comes from

MSC's Response Collector keeps each of its forms as a table template: a name
and an ordered list of `LABEL(mask)` columns, where the mask both says what
goes in and checks it. Message Creator adds labelled help blocks and fields
filled from the station.

What this engine copied:

- **The field is a label and a mask:** `Start(HHMM)Z` here, as in RC.
- **Masks:** `X` free text, `###` digits, `AA` letters, `DDHHMM` and `HHMM`
  times, a comma list for a choice, `Y,N` for yes or no.
- **Help:** a help block keyed by the field's label, as in Message Creator.
- **Live lists:** a field can list what the station holds (`@channels`), like
  Message Creator's FETCH.

What it changed:

- **One plain file per form:** RC keeps all its templates in one binary block
  marked "do not edit". Here each form is a text file anyone can read and
  change, on the box, through a form.
- **A form runs a call:** an RC form fills a table row. Here a form runs one
  core call from a fixed list, so a form can do nothing a member couldn't do
  by hand.
- **Secret fields:** a frequency or passphrase is never shown again, never
  logged, and wiped from memory after the call.
- **Plain-English checks:** a slip names the field and what it wants. Nothing
  changes until every field passes.
- **Signed travel:** changed forms ride in the startup bundle and are taken
  only from a signer this box has checked by voice. A member's own change is
  kept.
- **The member's choices stay theirs:** a template can't fix the answer to a
  trust question (keymaster, accept, fingerprint). What a template does fix is
  shown to the member.

## The forms

| Form | What it does | Runs |
|---|---|---|
| SETUP | The call, then keys and card | setup_station |
| CARD_SEND | Your card to a station | card_send |
| CARD_SAVE | Your card to a stick | station_card |
| CARD_LOAD | Take a card from the air or a stick | card_import |
| CARD_CHECK | Check a card by voice | card_check |
| BOOK | Station book entry | book_put |
| CHANNEL | Channel and frequency by hand | channel_put |
| SCHEDULE | Net schedule | sched_put |
| BUNDLE_MAKE | Startup bundle to a stick | bundle_make |
| BUNDLE_SEND | Startup bundle over the air | bundle_send |
| BUNDLE_LOAD | Load a bundle | bundle_load |
| KEYMASTER | Accept or refuse a keymaster | keymaster_accept or keymaster_refuse |
| NET_TAPE | Key tape for a net, handouts to a stick | net_tape |
| KEY_OPEN | Open a tape that came in | key_open |
| PARTS_ASK | Ask for the missing parts of a long tape or bundle | parts_request |
| PARTS_QUEUE | Send the pooled resend queue, now or at a net slot | parts_queue_send |
| PARTS_REPEAT | Send one station the parts it asked for, now | parts_resend |
| HANDOUT_LOAD | Load a handout from a stick | handout_import |
| BACKUP | Back up the station to a stick | station_backup |
| RESTORE | Put a backup back | station_restore |
| FORM_EDIT | Change or add a form | form_template_put |
| FORM_RESET | Put a form back as built | form_template_reset |

The built-ins are `forms/*.form` in the source tree, built into the program.

## The template language

```
; Channel and frequency, entered by hand.
FORM CHANNEL
TITLE Channel and frequency
SUMMARY Enters one channel of your frequency card by hand.
LEVEL ZERO
ORDER 40
ACTION channel_put
PREFILL channel name
FIELD name Designator(X) required upper max=12 check=designator
FIELD frequency Frequency(X) required secret check=frequency
FIELD unit Unit(KHZ,MHZ,HZ) default=KHZ
FIELD ant_rx RX antenna, tuner port(1,2) default=1 fill
FIELD ant_tx TX antenna, tuner port(1,2) default=1 fill
NEXT CHANNEL SCHEDULE
BEGIN_HELP
Designator:
The name everyone uses for the channel, as on the card: P1, S1.
END
```

- **`FORM ID`:** first line. Capitals, digits and `_`, up to 24.
- **`TITLE`, `SUMMARY`:** what the member reads.
- **`LEVEL ZERO`:** listed for a box starting from nothing.
- **`ORDER n`:** where it sits in the list.
- **`ACTION call [WHEN key=VALUE]`:** the call the form runs. With several,
  the first whose WHEN matches. Only the calls listed below can be named.
- **`SET key value`:** a fixed value, shown to the member. Not allowed for
  keymaster, accept, replace, fingerprint or how.
- **`PREFILL kind key`:** opening the form with that field filled
  (`form_get` with `name=P1`) fills the rest from what the station holds:
  book, channel, schedule or template. A secret is never filled; the field
  says it has a value.
- **`FIELD key Label(mask)suffix flags`:** one field. The suffix is shown
  after the box (`Z`, `kHz`). A label can't hold `(`: the mask starts there.
- **`NEXT IDs`:** forms offered when this one is done.
- **`BEGIN_HELP`:** help blocks, each a `Label:` line, text, then `END`.
- **`;`:** a comment.

### Masks

| Mask | Kind |
|---|---|
| `X` | free text, one line |
| `TEXT` | a block of text |
| `A,B,C` | a choice from the list |
| `Y,N` | yes or no |
| `HHMM`, `DDHHMM`, `YYYYMMDD`, `YYYYMMDDHHMM` | a time or date |
| `@source` | a choice from what the station holds; `@source+` for several |
| anything else | a pattern: `X` any, `A` a letter, `N` a letter or digit, `#` a digit, others as written |

Sources: `@channels`, `@cards`, `@cards_unchecked`, `@stations`, `@nets`,
`@schedules`, `@book`, `@keymasters_undecided`, `@forms`, `@msg_cards`,
`@msg_bundles`, `@msg_tapes`, `@parts_waiting` (sets still missing parts),
`@parts_requests` (stations asking for parts; flagged ones say so),
`@parts_queue` (the resend queue, as one choice with its summary). A tape or bundle in parts is
one choice, with how many of its parts are in.

### Flags

`required`, `optional`, `upper`, `secret`, `nosend` (checked but not sent),
`fill` (a blank answer takes the default; without it, a field left blank is
not sent and the call keeps what is held), `default=value` (may hold spaces), `again=key` (must match another field, as a
passphrase typed twice), `as=name` (sent under another name), `max=n`,
`check=` one of station, stations, designator, frequency, hhmm, date,
fingerprint, passphrase, number, path. `form_get` shows `nosend`, `fill` and
`as` on each field, and `form_check` lists the defaults it will fill in
(`filled`), so a screen can show them before `form_submit`.

### Calls a form may run

setup_station, station_card, book_put, book_remove, channel_put,
channel_remove, sched_put, sched_remove, card_send, card_import, card_check,
keymaster_accept, keymaster_refuse, bundle_make, bundle_send, bundle_load,
net_tape, handout_import, key_open, parts_request, parts_resend,
parts_queue_send,
station_backup, station_restore,
form_template_put, form_template_reset.

A template is at most 16 KB and 40 fields.

## Where templates live

Built-ins are in the program. A form changed on the box, or taken from a
bundle, is kept beside the station's database, in `<CALL>.forms/` (for
example `STA-B.forms/WX_NET.form`), never in the settings file. The folder is
read on every call. Each form says where it came from: `built in`,
`edited here` or `from bundle NCS-A`.

## Forms in a startup bundle

`bundle_make` and `bundle_send` carry every form that differs from the
built-ins, compressed in radio-safe lines. `bundle_load` takes them only
when the signer's card is checked here:

- **forms_added, forms_replaced:** taken.
- **forms_same:** already the same here.
- **forms_kept:** this box changed that form itself, so its own copy stays.
- **forms_held:** the signer isn't checked here, so none were taken.

## First start

Until the call is set, the box answers the form calls for SETUP only, from
the built-in template. Once SETUP is done it isn't offered again.

## The calls

See [api.md](api.md#forms). From the CLI:

```
appliance-cli form                          # list
appliance-cli form show CHANNEL name=P1     # the form, filled from what's held
appliance-cli form check CHANNEL name=P4    # what still needs a look
appliance-cli form fill CHANNEL name=P4 frequency=3330 use=TIME
appliance-cli form template SCHEDULE        # the template text
appliance-cli form edit SCHEDULE my.form    # change it
appliance-cli form reset SCHEDULE           # back as built
```
