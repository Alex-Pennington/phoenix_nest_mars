# appliance-core

The headless message core of the MARS operator appliance: the message store,
the precedence queue (Z FLASH, O IMMEDIATE, P PRIORITY, R ROUTINE), retries and
acks, and a local JSON-lines API on 127.0.0.1. It has no modem, waveform or ALE
code. Its modem is greybeard-ale, run as separate programs on the same box;
interop with other stations is over the air only.

Milestones 1 and 3: the headless core and the message engine.

## Build and test

    cmake -S . -B build && cmake --build build && (cd build && ctest)

Needs a C++17 compiler, CMake, SQLite3 and python3 (for the stand-ins).

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

Both interfaces are as greybeard-ale's orchestrator described them on
2026-11-22. Not yet fixed by its docs:
- the field names in `CAPTURE.modems.json` (the core accepts `files`/`file`/`bytes`/`sha256`
  and a few other likely spellings);
- the exact `--mode` spelling (passed through as given).

## Until gale-tx and the payload files land

`tools/stub_gale_tx.py` takes gale-tx's command line and writes a WAV that is
not a modem signal (one sample per byte) so `tools/stub_channel.py` can carry it
to the other station's captures directory as greybeard-ale would. Two stations
on one machine:

    appliance-core --station A --tx-mode 600S --tx-cmd python3 --tx-arg tools/stub_gale_tx.py \
        --captures data/A-cap --db-dir data --api-port 4100
    appliance-core --station B --tx-mode 600S --tx-cmd python3 --tx-arg tools/stub_gale_tx.py \
        --captures data/B-cap --db-dir data --api-port 4101
    tools/stub_channel.py --from data/A-outbox --to data/B-cap --from data/B-outbox --to data/A-cap

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
  13, 15 and 16. Lines end 2CR 1LF, and 2CR 8LF before NNNN. No line is over 69
  characters; text is wrapped at spaces. Only A-Z, 0-9, space and - ? : ( ) . , /
  are allowed.
- Only UNCLAS is built or accepted, because the appliance sends plain text
  (the cipher is a stand-in).
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
  groups. The groups are kept as received and not read (the cipher isn't
  here); a count that differs from `GR` is warned.
- `16L_P2P_*` and `16L_RI_*`: the folded header, from a call sign or an RI.
  FULL has FM/TO, ABBR doesn't, SIMPLE has no precedence prosign by design.
- `ACP126_PLAINDRESS` and `ACP126_SIMPLE`, as above.

    {"cmd":"parse_traffic","id":"B-3"}

`parse_acp127` stays strict, for the appliance's own messages.

## Traffic samples

`tests/fixtures/mars-samples` holds 43 received messages, scrubbed twice of
call signs, RIs, tactical calls, channel designators, names, places and
authenticators; the README there says what was replaced, kept and left out.
`test_received` reads every one. 42 read; sample 46 is too garbled to find its
VZCZC, and the test expects that.

`test_templates` checks the reader's procedure against `TEMPLATES.csv` in each
fixture directory, and the batch 2 fixtures (`mars-samples-b2`) where they are
present. It skips what isn't there, so the build passes without them.
