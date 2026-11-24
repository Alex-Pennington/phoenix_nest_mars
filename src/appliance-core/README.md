# appliance-core

The headless message core of the MARS operator appliance: the message store,
the precedence queue (Z FLASH, O IMMEDIATE, P PRIORITY, R ROUTINE), retries and
acks, and a local JSON-lines API on 127.0.0.1. It has no modem, waveform or ALE
code. Its modem is greybeard-ale, run as separate programs on the same box;
interop with other stations is over the air only.

Milestones 1 and 3: the headless core and the message engine.

## Install it as the appliance

    packaging/install.sh --enable

Builds, tests and installs for your user (no root), writes a settings file to
`~/.config/phoenix-nest-mars/appliance.conf`, and runs the core as the systemd
user service `appliance-core`. `appliance-core --config FILE --check` says
what would run and what to fix. docs/quick-start.md is the operator's guide:
setup, practice with no radio, everyday commands, backups and removal.

`appliance-cli api CMD key=value ...` sends any call in docs/api.md from the
shell.

## Build and test

    cmake -S . -B build && cmake --build build && (cd build && ctest)

Needs a C++17 compiler, CMake, SQLite3, OpenSSL 3 (libcrypto) and python3
(for the stand-ins).

## Run one station on greybeard-ale

    appliance-core --station A --tx-mode 600S --captures /path/to/captures --db-dir ./data

- **Send.** Each frame is written to a payload file and the core runs
  `gale-tx --waveform 110a --mode MODE --in PAYLOAD.bin --out BURST.wav`. The WAV
  is moved, whole, into the outbox (`--outbox`, default `DB-DIR/NAME-outbox`) for
  greybeard-ale's transmit stage. The core never keys a radio.
- **Receive.** The core watches `--captures` for `CAPTURE.modems.json`, checks each
  payload file it lists (`CAPTURE.m110a.N.bin`, `CAPTURE.m110d.N.bin`) against its
  byte count and sha256, and takes the bytes. A file still being written is
  waited for (30 s); a wrong one is skipped and logged. Listings already handled
  are kept in `DB-DIR/NAME-captures-seen.txt`; on a first start everything
  already in the directory counts as handled.
- **Operator.** gale-term's Messages panel (`--core-port`) or `appliance-cli`.

`CAPTURE.modems.json` is read as greybeard-ale's `tools/modem_verdict.py` writes
it: a `verdicts` list, and for each decoded data burst `payload_file`,
`payload_bytes` and `sha256`. Bursts with no payload and Harris voice payloads
are passed over.

## Two stations through the real 110A modem, on audio files

Tested 2026-11-22 with greybeard-ale main a5a4cda and the modem
(pennington_m110a_demod) at 8839b43. Build the modem's transmit and receive
programs with greybeard-ale's own recipe, without installing anything:

    M110A_BUILD=$PWD/m110a-build greybeard-ale/tools/m110a_tx/build.sh pennington_m110a_demod $PWD/lib
    export GALE_M110A_TX_FILE=$PWD/lib/m110a_tx_file GALE_M110A_RX=$PWD/m110a-build/bin/m110a_rx

(gale-tx needs Python's numpy and scipy.) Then run two stations, with
greybeard-ale's gale-tx as the transmitter and its modem_verdict.py as the
receiver, and `tools/stub_channel.py` carrying each burst WAV across:

    appliance-core --station AAR4XX --tx-mode 600S --tx-cmd python3 --tx-arg greybeard-ale/tools/gale_tx.py \
        --captures data/A-cap --db-dir data/A --api-port 4100
    appliance-core --station AAR4XY --tx-mode 600S --tx-cmd python3 --tx-arg greybeard-ale/tools/gale_tx.py \
        --captures data/B-cap --db-dir data/B --api-port 4101
    tools/stub_channel.py --verdict greybeard-ale/tools/modem_verdict.py \
        --from data/A/AAR4XX-outbox --to data/B-cap --from data/B/AAR4XY-outbox --to data/A-cap

A message composed on one station decodes on the other and its ACK comes back,
both ways. The audio is clean: no noise or fading is added.

## With no modem build at hand

`tools/stub_gale_tx.py` takes gale-tx's command line and writes a WAV that is
not a modem signal (one sample per byte), and `tools/stub_channel.py` without
`--verdict` carries it across and writes the payload file and listing itself.
`test_greybeard` uses these.

    appliance-core --station A --tx-mode 600S --tx-cmd python3 --tx-arg tools/stub_gale_tx.py \
        --captures data/A-cap --db-dir data --api-port 4100
    appliance-core --station B --tx-mode 600S --tx-cmd python3 --tx-arg tools/stub_gale_tx.py \
        --captures data/B-cap --db-dir data --api-port 4101
    tools/stub_channel.py --from data/A-outbox --to data/B-cap --from data/B-outbox --to data/A-cap

## Waiting for an ALE link

greybeard-ale does the scanning, calling and linking; the core only asks it.
Given ale-scan's control socket, the core reads its state every two seconds and
shows it in `status` as `link`. With `--link-gate` it also holds each message
until greybeard-ale reports a link to that station:

    appliance-core --station A ... --link-socket /run/user/1000/ale-scan.sock --link-gate

ale-scan reports no per-station links yet, so with the gate on nothing would go
on the real scanner today. To try the gate, use the stand-in:

    tools/stub_ale_scan.py --socket data/scan.sock &
    appliance-core --loopback-pair A,B --db-dir data --api-port 4100 \
        --link-socket data/scan.sock --link-gate
    appliance-cli send --to B "HELLO"          # waits: no link to B
    echo "link B linked" | socat - UNIX-CONNECT:data/scan.sock    # now it goes

## Nets, the address book and the monthly report

Each station keeps a book in its own store, offline: an address book with
collectives (import it from CSV), nets with a roster and check-ins, operator
duty with turnover notes, a station log and a monthly report. A station heard on
the air during a net is checked in automatically. Sending to a collective's name
sends one copy to each member. The calls are in docs/api.md under "The station
book"; tests/test_ops.cpp runs an exercise net from open to monthly report.

Nets can meet on a schedule (weekly, or the first Saturday of the month, in
UTC). The station says when one starts in 15 minutes and when one is due, and
can open it itself. A net call-up goes to every station at once as one
broadcast to ALL, which nobody acks, calling the stations not yet checked in.
Compose takes book names too: a tactical name goes to its station, a
collective gets one copy per station, and a plain name on a TO line gets its
routing indicator. tests/test_sched.cpp covers all three.

## Two stations with no modem at all

    appliance-core --loopback-pair A,B --db-dir ./data --api-port 4100

## Test framing

The PNA1 frame (`src/frame.h`) is a test stand-in for the link layer, not the
MARS transmission header.

## Message format: ACP 127

`src/acp127.h` builds and parses messages in the tape relay format of
**ACP 127(G), "Communications Instructions Tape Relay Procedures", November
1988** (frozen by the CCEB in December 1997), the public edition at
commsmuseum.co.uk. The current ACP series is not used. Paragraph numbers in the
code are from that edition.

- PLAINDRESS in the automatic (VZCZC) or manual (VV) form, format lines 1 to
  13, 15 and 16. Lines end 2CR 1LF, and 2CR 8LF before NNNN. The edition's
  69-character line limit (text wrapped at spaces) is the builder's default and
  can be turned off with `limit_lines`, as MARS stations send longer lines.
  Only A-Z, 0-9, space and - ? : ( ) . , / are allowed.
- Only UNCLAS is built or accepted. Codress (below and docs/crypto.md) is
  enciphered unclassified traffic, not classified traffic.
- LTRS/FIGS shifts and the FLASH bell have no ASCII form and are not written.
- The built message is the body of a core message, so it is queued and sent
  by precedence like any other. The PNA1 framing below is unchanged.

API commands:

    {"cmd":"send_acp127","to_station":"B","prec":"O","channel":"QXA",
     "routing":"RXAAAA","originator_ri":"RXCCCC","from":"RXCCCC/STATION ONE",
     "to":"RXAAAA/STATION TWO\nSTATION THREE","text":"..."}
    {"cmd":"parse_acp127","id":"B-3"}

`send_acp127` also takes `info`, `xmt` (one address a line), `transmission`
(format line 4 instructions), `line10` (`GR n`) and `"manual":"true"`. It
takes the next channel serial (001 to 999 per channel) and station serial,
and stamps the filing time and the date-time group from the clock in UTC
(zone Z, four-digit year).

Two things follow MARS practice rather than the edition, from the traffic
samples below:

- Format line 3 is written the way MARS stations write it,
  `DE RI #0038 3102053`: the station serial as four digits and the time filed
  as Julian day, hour and minute. `"fl3":"edition"` gives the edition's
  `DE RI 38 06/2053Z`. The station serial runs 1 to 9999, then starts again;
  the samples show four digits but not where stations wrap.
- A routing indicator may be 3 to 12 of A-Z, 0-9 and `-`, with at least one
  letter, because call signs (AAR4MW), tactical calls (KAPPAMAIN),
  collectives (B7Q) and suffixed RIs (UHDJRKW-OP) stand where RIs go.

Further TO, INFO and XMT addressees go on lines of their own, as the samples
show. Annex B of the edition, the format line table, was not readable in the
scan.

## Message format: ACP 126

`src/acp126.h` builds the ACP 126 forms MARS stations send: the call-up
`CALLED DE STATION NR 051` inside the ACP 127 envelope. **No public ACP 126
edition could be read**, so this is drawn from the traffic samples, not an
edition, and should be checked against one when it can be. The samples' notes
name two MSC templates, and the builder makes exactly those:

- **simple** (the default): call-up, BT, text, BT, NNNN (samples 44, 45).
- **plaindress**: call-up, a DTG line from the clock, FM and TO, then BT and
  text (sample 43). FM and at least one TO are required.

    {"cmd":"send_acp126","to_station":"B","calls":"NET","station":"AAR4XX",
     "procedure":"plaindress","from":"...","to":"...","text":"...","unclas":"false"}

NR is three digits, 001 to 999 per calling station. MSC sent sample 45 as
`ALL DE NR 001` when the station's own call wasn't set; the builder refuses an
empty calling station instead.

## Chat lines (ACP 201 style)

`src/acp201.h` reads and builds the chat lines that run over the same modem
path as traffic: `KAPPA71-CM DE AAR4XX // QRU U,A // 2001Z K`. It names VER,
`QRU U,A` (and `[COMP]`), `ZEE n`, `QSL n`, `TRAFFIC COMPLETE`, MSC's
automatic receipt (`QSL _R_<RI> #0025 3051616/<calls>`) and remote SQL
queries, which it reads and never runs. Text may hold `//` (a status strip
ends in it). Drawn from the batch 2 samples; no ACP 201 edition was read.

    {"cmd":"send_chat","to_station":"B","call":"KAPPA71-CM","station":"AAR4XX","text":"QRU U,A"}

`parse_traffic` reads a chat line as well as an enveloped message.

To a station whose link format is `plain` (an MSC station), `send_chat` builds
the MSC CP form instead, `chat|ID|STA-C DE NCS-A // text // K 24-0155Z`, and
waits for that station's `ack|ID`. See "Working with MSC stations".

## Working with MSC stations

The core now handles traffic the way the MSC Communications Processor does,
apart from its cipher, which stays a closed box behind our cipher slot:

- A message with no ack goes again after `--ack-timeout` seconds (120), up to
  `--ack-retries` more times (2), then waits as `unacked` and goes again when
  its station is heard.
- It waits `--tx-gap` seconds (6, none with `--loopback-pair`) after the channel goes quiet before keying up. `tx-gap`, `ack-timeout` and `ack-retries` can also go in the settings file.
- `emcon`, `hold`, `release`, `cancel` and `requeue` give the operator a hand
  on the queue.
- Garbled copies and traffic for other stations are kept, as `garbled` and
  `overheard`, and not acked.
- `link_format` set to `plain` sends an MSC station the bare text and reads
  its bare text back, several messages to a burst.

docs/cp-link.md says what was taken from CP and what was left alone;
docs/api.md has the calls. tests/test_cp_link.cpp covers it.

## Reading received traffic

`src/received.h` reads traffic in the forms MARS stations actually send, which
depart from the edition in many ways: text before VZCZC or after NNNN, FL2
and FL3 folded onto one line, no precedence prosign, an empty originator, a
relay's second RI after DE, `ZNY EEEEE` over unclassified text, `ZEK ZOB`, no
UNCLAS line, lines over 69 characters, page headers (also after the closing
BT), a missing NNNN after the closing BT, and the MSC `[DIGEST:...]` line. It
names every departure it took in `warnings`, and refuses anything not
unclassified or cut off before the closing BT.

It also names the MSC template that composed each message, as the samples'
SOURCES.csv does (`procedure` in the reply):

- `ACP127_PLAINDRESS`: unfolded header, FM and TO, plain text.
- `ACP127_CODRESS`: a key indicator after the DTG, a `GR` count and cipher
  groups. Groups enciphered by an appliance whose net key this station holds
  are deciphered (`deciphered`, `text`); others, MARS cipher traffic
  included, are kept as received with the reason in `why_not_deciphered`. A
  count that differs from `GR` is warned.
- `16L_P2P_*` and `16L_RI_*`: the folded header, from a call sign or an RI.
  FULL has FM/TO, ABBR doesn't, SIMPLE has no precedence prosign by design.
- `ACP126_PLAINDRESS` and `ACP126_SIMPLE`, as above.

    {"cmd":"parse_traffic","id":"B-3"}

`parse_acp127` stays strict, for the appliance's own messages.

## Codress, keys, signatures and the digest

docs/crypto.md has the design and the operator's steps. In short:

- Every message except chat carries MSC's `[DIGEST:...]` error check, and
  `parse_traffic` says whether it passed. On a failure it offers a repeat
  request.
- Each box makes its own station keypair on first start. Its card, a short
  text block with a fingerprint to read aloud, goes out by USB or over the
  air. Messages are signed, and `parse_traffic` says whether the signature is
  good.
- Codress goes through a **cipher slot** (docs/cipher-slot.md). Our own
  AES-256-GCM cipher, `pna-aes`, fills it unless a net's handout packet
  brings another module. `pna-cipher-check MODULE.so` tests any module
  against the slot, and docs/cipher-adapter-guide.md is the guide for writing
  one.
- Net keys come from a dated key tape. A keymaster makes tapes with
  `pna-keymaster`, or with `net_tape`, which seals a copy to each member's
  card and can send it over the air.
- `pna-aes` is not the MARS cipher, and it reads only traffic from other
  appliances.

## Composing with guidance

`src/compose.h` composes a message from a template, with guidance a
first-time operator can follow. There is one template for each form the two
sample batches show (GENADMIN and its simple, monthly, after-action and
code-group forms, AKNLDG, RI, RRI and its strip and METAR forms, MARSSTATUS,
bare strips, COMSPOT and both net reports, ORDER, WXOBS, SVC and free text),
each in any procedure it can go in (the 16L forms, ACP 127 plaindress, ACP 126,
chat). Codress is offered when this station holds a net key for now, and
refused with the reason when it doesn't.

A front end asks for a template's form (fields with labels, hints, examples
and defaults for this station and time), sends what the operator typed to
`compose_check`, and shows the issues: each says what is wrong, how to fix
it, and gives a value that would be right. With no errors, it shows the whole
message; `compose_send` takes the serials (the heading's serial and the MSGID
serial are always the same) and queues it. A frequency is never accepted:
channels go by designator.

Breaking lines at 69 characters is an option on each message ("Break lines
at 69 characters", off by default), as it is in Message Creator: MARS traffic
doesn't keep to 69, so each line goes as written. With it on, prose is broken
at spaces, and a strip (never broken) that won't fit is refused with the fix.

`compose_advise` explains a field or the next fixes from the built-in
guidance, or asks a local helper program given with `--advisor` (an AI
helper, for instance). The core calls no outside service and needs no key.

`queue` and `log` feed the panel's queue and activity views. `docs/api.md`
has every call and reply. `test_compose` composes every template in every
procedure it allows by doing only what the guidance says, and reads each back.

## Traffic samples

`tests/fixtures/mars-samples` holds 43 received messages, scrubbed twice of
call signs, RIs, tactical calls, channel designators, names, places and
authenticators; the README there says what was replaced, kept and left out.
`test_received` reads every one. 42 read; sample 46 is too garbled to find its
VZCZC, and the test expects that.

`test_templates` checks the reader's procedure against `TEMPLATES.csv` in each
fixture directory, and the batch 2 fixtures (`mars-samples-b2`) where they are
present. It skips what isn't there, so the build passes without them.
