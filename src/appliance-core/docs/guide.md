# The operator guide and training mode

The guide is built into the box from day one. gale-term shows it with one
key (`?`) on any screen, by that screen's id, and its first-start walkthrough
takes a new member from SETUP to the first message sent. Training mode lets a
member with no traffic yet see every screen work, on practice traffic that
never goes on the air.

Both are served by the core API (docs/api.md, Guide and Training). gale-term
draws them and needs no code per page.

## Pages

A page is plain text, like a form template (docs/forms.md), and can be
changed on the box without a rebuild:

    ; The In Box.
    PAGE MSG-INBOX
    TITLE In Box
    KIND screen
    SCREENS MSG-INBOX
    HELP Enter open  ? guide
    KEY Enter Opens the message.
    SEE MSG-VIEW MSG-PENDING
    BEGIN_TEXT
    Every message that came in, newest first. ...
    END
    BEGIN_EXAMPLE
    A PRIORITY message from NCS-A arrives at 1412Z. ...
    END

- `PAGE`: the page id (letters, digits, `-` and `_`).
- `KIND`: `start` (the walkthrough), `screen` (a screen's own page) or
  `topic` (an idea several screens share, like EMCON).
- `SCREENS`: the screen ids whose `?` opens this page.
- `HELP`: the one-line key strip for the bottom of that screen.
- `KEY`: a key and what it does, one per line.
- `SEE`: other page ids worth reading next.
- `BEGIN_TEXT` / `BEGIN_EXAMPLE` ... `END`: the text, and a worked example.

A page may not hold a tab or a frequency; the check is the one compose uses.
Text is wrapped to 78 columns for the 80x25 screen.

The built-in pages are in `guide/*.page`, compiled into the program. A page
written on the box goes in `DB_DIR/CALL.guide/` and wins over the built-in
one until it is reset.

How `guide_help {screen}` finds the page for a screen (ask 39), first match
wins:

1. `FORM-<ID>`: that form's own page.
2. A page written or changed on this box (`guide_page_put`) whose `SCREENS`
   line lists the screen.
3. A built-in page whose `SCREENS` line lists it.
4. The page whose id is the screen.
5. None: `found` is false and the MAIN page comes back, so `?` never leads
   nowhere.

Within 2 and 3, pages are taken in order of their id. So gale-web can bring
its own page for any screen with `guide_page_put`, and it answers `?` there
ahead of a built-in page for the same screen, until it is reset.

The gale-web screens with built-in pages: GW-SCHEDULE (screens GW-SCHEDULE
and SCHEDULE), GW-CHANNELS (GW-CHANNELS, CHANNELS), GW-BOOK (GW-BOOK, BOOK)
and GW-STATION (GW-STATION, STATION).

Two kinds of page aren't written: they are made from what they explain, so
an edited template changes its page at once.

- `FORM-<ID>`, one per form: what the form does, each field with its help,
  what comes next. Its screen id is `FORM-<ID>`.
- `TYPE-<ID>`, one per message type (compose templates): what it is for, the
  procedures it goes in, each field with its hint and example.

Built-in screen and topic pages: START, MAIN, MSG-INBOX, MSG-PENDING,
MSG-VIEW, MSG-OUTBOX, MSG-COMPOSE, MSG-SEARCH, MSG-ACTIVITY, MSG-REPORTS,
BBS-SETUP, BBS-GUARD, BBS-ROUTES, EMCON, KEYS, RESENDS, TRAINING. The screen
ids follow docs/gale-term-mm-bbs-screens.md. Some of those screens (inbox
marks, machine mode, routing) are still being built in the core; their
pages describe the screens as specified.

## The first-start walkthrough

`guide_start` gives seven steps, each ticked off from what the station
holds, not from what was clicked:

| Step | Done when | Form / screen |
|---|---|---|
| SETUP | the call is set and the station keys made | FORM-SETUP |
| CARD | a card has been checked, or a keymaster accepted | FORM-CARD_SAVE |
| KEYMASTER | the member has accepted a keymaster | FORM-KEYMASTER |
| CHANNELS | the station holds channels | FORM-BUNDLE_LOAD |
| SCHEDULE | the station holds a net schedule | FORM-SCHEDULE |
| KEYS | a key is current for sending | FORM-KEY_OPEN |
| FIRST | the operator has sent a message of their own | MSG-COMPOSE |

`next` is the first step not done. In setup mode (no call yet) the guide
answers from the built-in pages only, and the walkthrough starts at SETUP.

## Training mode

`training {on:Y}` turns it on. While it is on, every API call is answered
by a practice copy of the station, and every reply carries
`"training":true` so gale-term can show TRAINING on every screen.

The practice station:

- keeps its own store, keys and station book in `DB_DIR/CALL.training/`,
  apart from the real station's;
- has no radio. Its modem is joined only to a practice net control,
  TRAIN-NCS, inside the program, which acknowledges what it is sent. The
  real radio is never keyed by anything done in training;
- is filled the first time with the scrubbed MARS samples in
  `tests/fixtures/mars-samples*` (about eighty, made-up calls and routing
  indicators), heard as bare text the way gale-rx hands traffic over. The
  routing indicator each was sent to is changed to the station's own call,
  so they come in as received; a few stay overheard or garbled, as on a
  real net;
- gets a practice key tape from TRAIN-NCS, sealed to it and opened, and
  three messages: a welcome, a check-in request, and one enciphered in the
  practice key with our own cipher.

The real station keeps running underneath: it receives and acknowledges as
always, and `training_status` shows its counts in `real`.

Resending in training stays in training: `retransmit`, `requeue`,
`parts_queue_send` and every other resend reach the practice store and
TRAIN-NCS only. The real radio hears nothing and the real store doesn't
change (test_training checks both).

Every reply names its store in `store_mode`: `"real"` or `"training"`. A
screen that may share the core with another one (gale-web in the kiosk and
on a laptop) sends `"expect_store"` with the store it is showing. If another
screen has turned training on or off since, the call is refused with
`store_mismatch: true` and the store now in force, and nothing is done: the
screen shows the change and asks again.

Training refuses what would reach outside the practice copy: writing files
(`out`), reading files in (`file`; its material is built in), and
`station_backup`, `station_restore`, `cipher_install`, `cipher_remove`.

`training {on:N}` goes back to the real station. The practice store stays,
so turning training on again picks up where the member left off.
`training {reload:Y}` empties it and fills it again, as new. Training is
off whenever the program starts.

Guide calls made while training is on answer from the real station's
guide, with `training` true and a TRAINING line at the top of each page.
