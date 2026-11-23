# Design note: the engine as a STANAG 5066 client

Status: design only. Nothing here is built. It waits on greybeard-ale saying
where STANAG 5066 lives on the appliance.

## The requirement

Alex, Nov 23 2026: "make sure were meeting these requirements STANAG 5066 ARQ,
110D Appendix A, single-channel 3G ALE".

All three are on the radio side, so they are greybeard-ale's scope: the
MIL-STD-188-110D Appendix A waveforms, single-channel 3G ALE to set up the
link, and a STANAG 5066 node (the data link with ARQ) above the modem. This
note covers only what the message engine changes so that it hands messages to
that 5066 node, rather than writing a burst for the modem itself.

## Today

- The core talks to a modem only through `ModemDriver` (`src/modem.h`):
  `send(bytes)`, then `on_tx_done`, with received bytes arriving through
  `on_rx`.
- `GreybeardModem` turns each PNA1 frame (`src/frame.h`) into a burst WAV in
  an outbox, and reads frames back from greybeard-ale's captures.
- Reliability is ours. Every message frame gets an ACK frame from the far
  core once the message is stored. If no ACK comes, the queue retries by
  precedence and marks the message failed after the set number of tries.

## With STANAG 5066

A new driver, `S5066Client`, implements `ModemDriver`, so nothing above the
driver needs to know about 5066.

**The client interface.** The engine uses the Subnetwork Interface Sublayer
(SIS) client protocol of STANAG 5066 Annex A, over TCP to the 5066 node on the
same box. The well-known port is 5066. The engine always connects to
127.0.0.1, so nothing leaves the box except over the air.

1. On start, it sends `S_BIND_REQUEST` with our SAP and a service type of
   ARQ, in-order delivery and client delivery confirm. `ready()` is true once
   `S_BIND_ACCEPTED` comes back. On `S_BIND_REJECTED`, or a dropped
   connection, the driver logs the reason and tries again every few seconds.
2. `send()` becomes one `S_UNIDATA_REQUEST` (or a few: see the size limit
   below) to the far station's 5066 node address and our SAP. It sets ARQ
   mode, the priority mapped from the message's precedence (Z highest, then O,
   P, R), and a time to live taken from the queue's retry time.
3. `S_UNIDATA_REQUEST_CONFIRM` calls `on_tx_done(true)`.
   `S_UNIDATA_REQUEST_REJECTED`, or the time to live running out, calls
   `on_tx_done(false)` with the reason. The queue then retries as it does
   today.
4. `S_UNIDATA_INDICATION` delivers the bytes to `on_rx`, unchanged. The frame
   reader is the same one the modem path uses.

**The SAP.** STANAG 5066 Annex F assigns SAP numbers to its standard clients
(e.g. character stream, mail, file transfer, IP). Our engine is none of
those: it is its own message client, so it binds its own SAP. I haven't
checked the Annex F table itself. Before we pick a number, we check that
table for an unassigned or local-use SAP and agree it with greybeard-ale. It
goes in the settings file as `s5066-sap`, and every appliance in a net must
use the same one.

**Addresses.** 5066 sends to a node address, not a call. Each station in the
address book gets a `s5066_address` field, and the driver looks up the frame's
`dst`. A station with no address can't be sent to over 5066, and compose says
so. The `ModemDriver` interface gains a `send_to(dst, prec, bytes)`. The
loopback and burst drivers just ignore `dst` and `prec`.

**The size limit.** The 5066 node caps one U_PDU. Wikipedia gives 2048 bytes
for ARQ; the exact figure comes from the node's bind reply. Our frames can
reach 8 KB, so the driver splits a frame into numbered pieces, and the far
driver joins them before handing them to `on_rx`. With ARQ and in-order
delivery, a piece either arrives in order or the whole send is rejected.
Joining is therefore a counter and a buffer, with no retransmission logic of
our own.

## What stays the same

- **Message-level ACKs.** 5066's client delivery confirm means the far 5066
  client got the bytes. Our ACK means the far engine stored the message and
  it is in the operator's traffic. Those are different promises, and only
  the second one ends a message's life in the queue. ARQ makes retries rare,
  but the ACK and the retry rule stay.
- The PNA1 frame, the ACP 127/126 message formats, the digest, signatures,
  codress and key tapes are all unchanged. 5066 carries the frame's bytes as
  they are.
- Precedence ordering in our queue stays. The 5066 priority only orders what
  the node already holds.
- The burst-WAV driver stays for practice, for the dummy load, and for any
  station without a 5066 node. Which driver to use is a setting
  (`link = burst | s5066`).

## Interop

Any other station's software that runs a 5066 node and binds the same SAP
with the same frame can exchange traffic with us. That's still interop over
the air only. No MARS-wide SAP or frame for 5066 is known to us. If MARS
names one (MSC or NGT over 5066, for example), the driver binds that SAP and
the engine adds a reader for that format, as it did for the ACP 127 forms.

## What we need from greybeard-ale

1. Where the 5066 node runs, and whether it is their code or an existing node
   they wrap.
2. Its SIS port, and that it accepts clients on 127.0.0.1 only.
3. The U_PDU limit, and which delivery modes it supports.
4. Their numbering scheme for node addresses, so the address book can hold
   them.
5. How 3G ALE link set-up and teardown show up to a client, if at all. We
   expect them not to: the node brings the link up when it has traffic.

## Tests, when built

- A stand-in 5066 node in Python under `tools/`, like the other stand-ins. It
  speaks enough SIS to bind, take `S_UNIDATA_REQUEST`, confirm or reject, and
  deliver `S_UNIDATA_INDICATION` to a second client.
- Two cores through the stand-in. This covers a send and its ACK, a 6 KB
  message split and rejoined, a rejected send retried, an unknown address
  refused at compose, and precedence order.
- Interop with greybeard-ale's real node, once it exists, on audio files first
  and then the dummy load.

## To check

The primitive names, the delivery-mode fields and the priority field above
come from my knowledge of STANAG 5066 Edition 3 Annex A. I didn't read them
from the text this time: the pages I could reach show the SIS port (5066) and
the ARQ and non-ARQ modes, and nothing more. They get checked against the
standard's text before any code is written.
