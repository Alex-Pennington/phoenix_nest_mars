# The engine as a STANAG 5066 client

Status: the client is built and tested against a stand-in node
(`tests/test_s5066.cpp`). It has not met greybeard-ale's real 5066 node yet,
because that node isn't built. Until it is, stations keep sending through
gale-tx (`link = burst`, the default).

## The requirement

Alex, Nov 23 2026: "make sure were meeting these requirements STANAG 5066 ARQ,
110D Appendix A, single-channel 3G ALE".

The work splits like this:

- **greybeard-ale's scope:** the waveforms, single-channel 3G ALE, the modem's
  side of the 110D Appendix A LAN interface, and the STANAG 5066 node (the
  data link with ARQ). Their integration plan calls the engine's link to the
  node "interface 7".
- **Ours:** the message engine as a 5066 client. It hands each frame to the
  node and takes received frames from it.

## Turning it on

```
appliance-core --station STA-A --link s5066 --s5066-peers /path/peers
```

| Setting | Default | What it does |
|---|---|---|
| `link` | `burst` | `s5066` sends through the 5066 node; `burst` keeps gale-tx |
| `s5066-peers` | none, required | the peers file: one `CALL ADDRESS` per line |
| `s5066-port` | 5066 | the node's SIS port on 127.0.0.1 |
| `s5066-sap` | 3 | the SAP to bind; every appliance in a net uses the same |
| `s5066-ttl` | 600 | time to live per send, in seconds |
| `s5066-confirm` | `node` | `node` or `client` delivery confirmation |
| `s5066-broadcast` | `each` | `each`: a message to ALL goes as one copy to every station in the peers file; `group`: one copy to the all-stations address |

The peers file maps calls to 5066 node addresses. Addresses are written like
`0.0.1.2`, with the first part 0 to 15 (28 bits). `#` starts a comment. The
file is read again whenever it changes, so a new station needs no restart.

```
# made-up stations
NCS-A  0.0.0.1
STA-B  0.0.0.2
```

`--check` reads the peers file and says how many stations it holds. The node
need not be up at start: the client tries again every 3 seconds and logs when
it binds.

## How it works

`S5066Client` (`src/s5066_client.h`) is a `ModemDriver`, so nothing above it
changed. It reads the frame's own header for where the frame goes (`dst`) and
how urgent it is (`prec`).

1. **Bind.** On connect it sends `S_BIND_REQUEST` on SAP 3, rank 0, ARQ, in
   order, with the delivery confirmation set. `S_BIND_ACCEPTED` gives the
   node's MTU, and the driver is ready. `S_BIND_REJECTED`,
   `S_UNBIND_INDICATION` or a dropped connection logs the reason and starts
   over.
2. **Send.** One `S_UNIDATA_REQUEST` per piece (below) to the far station's
   address, ARQ, in order, with the priority from the precedence and the
   time to live from the settings.
3. **Result.** When every piece is confirmed (`S_UNIDATA_REQUEST_CONFIRM`), the
   send is done. A reject (`S_UNIDATA_REQUEST_REJECTED`) fails it with the
   node's reason, and the queue retries as it always has. So does no confirm
   within the time to live plus 30 seconds.
4. **Receive.** `S_UNIDATA_INDICATION` on our SAP goes to the frame reader,
   after the pieces are joined.
5. **Flow and keep-alive.** `S_DATAFLOW_OFF` holds sends until
   `S_DATAFLOW_ON`. `S_KEEP_ALIVE` is answered with `S_KEEP_ALIVE`.
   `S_SUBNET_AVAILABILITY` is logged.

| Precedence | 5066 priority (0-15, higher first) |
|---|---|
| Z FLASH | 15 |
| O IMMEDIATE | 10 |
| P PRIORITY | 5 |
| R ROUTINE | 0 |

**To ALL.** A frame to `ALL` goes non-ARQ, no confirmation, 2 repeats. By
default (`s5066-broadcast each`) it goes as one copy to every station in the
peers file but this one, because greybeard-ale's node has no all-stations
address yet and refuses one. With `group` it goes as one copy to the
all-stations address (15.255.255.255), for a node that takes it. A non-ARQ
copy gets no confirm, only a refusal, and the node refuses at once, so the
client waits 1.5 s: each refused copy is logged with its address, and the
broadcast counts as sent unless every copy was refused.

**No address.** A frame to a call that isn't in the peers file fails with "no
5066 address for CALL in the peers file", and the queue treats it as any other
failed send.

**Pieces.** The node caps each U_PDU at its MTU, and our frames reach 8 KB. A
bigger frame goes as numbered pieces, each starting with 7 bytes: `P` `S`,
version 1, a 2-byte sequence, the piece's index, and the count. With ARQ and
in-order delivery the pieces arrive in order or the send is refused, so the
far side only joins them per source address. A piece out of order loses that
frame, which is counted; our ACK and retry cover it.

## The wire form

As greybeard-ale's interface 7 fixes it, and as Wireshark's s5066sis dissector
(`epan/dissectors/packet-s5066sis.c`) reads it. greybeard-ale names that
dissector as the reference for the codes, and its exit test for the node is
that the dissector decodes every frame with no malformed fields.

- Each primitive: `0x90 0xEB`, version `0x00`, a 2-byte size, then the
  primitive's type byte and fields. Everything is big-endian.
- Types used: 1 bind request, 2 unbind request, 3 bind accepted, 4 bind
  rejected, 5 unbind indication, 14 subnet availability, 15 data flow on,
  16 data flow off, 17 keep-alive, 20 unidata request, 21 unidata indication,
  22 unidata confirm, 23 unidata rejected.
- An address is 4 bytes: 3 bits of address size (we send 7, a full 28-bit
  address), a group bit (set only for the all-stations address), then 28 bits.
- Service type: transmission mode (4 bits: 1 ARQ, 2 non-ARQ broadcast),
  delivery confirmation (2 bits: 1 node, 2 client), delivery order (1 bit:
  0 in order), extended (1 bit, 0), then 4 bits of repeats. In a unidata
  request the 20-bit time to live follows straight on, in units of 2 seconds.
- A unidata indication may carry two extra block counts before the data
  (Wireshark notes this for Rockwell Collins v2.1 and earlier); the reader
  skips them when the length says they're there.

## What stays the same

- **Message ACKs.** A 5066 confirm means the far node (or client) has the
  bytes. Our ACK means the far engine stored the message and it is in the
  operator's traffic. Only the ACK ends a message's life in the queue. ARQ
  makes retries rare, but the ACK and the retry rule stay.
- The PNA1 frame, the ACP 127/126 message formats, the digest, signatures,
  codress and key tapes are all unchanged. 5066 carries the frame's bytes as
  they are.
- Precedence ordering in our queue stays. The 5066 priority only orders what
  the node already holds.
- The burst-WAV driver stays for practice, the dummy load, and any station
  without a 5066 node.

## 110D Appendix A

Appendix A is the modem's LAN interface. Under 5066, the node is the modem's
Appendix A client and the engine talks SIS to the node, as above, so the
engine never speaks Appendix A itself. greybeard-ale's orchestrator confirmed
that is the normal stack.

A second driver that drives the modem over Appendix A directly, for bursts
outside 5066 or a box with no node, is built: `link = appa`, described in
docs/appendix-a-driver.md.

## Interop

Any other station's software that runs a 5066 node and binds SAP 3 with the
same frame can exchange traffic with us, over the air only. No MARS-wide SAP
or frame for 5066 is known to us. If MARS names one, the driver binds that SAP
and the engine adds a reader for that format.

## Tests

`tests/test_s5066.cpp`:

- The wire form byte for byte (bind, bind accepted, keep-alive, a unidata
  request), the reader fed one byte at a time with junk in front, the
  indication with and without the extra block counts, and a reject's reason.
- Addresses and the peers file, including bad lines and the broadcast address
  refused as a station.
- Pieces: split, joined with two sources interleaved, a lost middle piece
  counted, too many pieces refused.
- Two cores through a stand-in node (two "nodes" in the test, joined by a
  stand-in network, MTU 2048 as the real node advertises): a message and its
  ACK; a 4 KB message as two pieces; one refusal then a retry that goes; a call with no address failing;
  the peers file read again after a change; a broadcast as one copy per
  station, one refused and logged; keep-alive; data
  flow off and on; the node dropping the connection and the client binding
  again; an unbind indication.
- No node at all: not ready, sends refused, one line in the log.

The stand-in has no ARQ and no radio. Like the real node, it confirms ARQ
requests only.

## Run against greybeard-ale's node (Nov 24 2026)

Two cores, STA-A and STA-B, each bound to its own `s5066-node` (greybeard-ale
main at 7092946, addresses 0x101 and 0x102), joined by `s5066-channel` at
2400 bps, 50 ms delay, 300 ms turnaround and 10 % D_PDU loss. All of these
came through:

- a chat each way, each acked;
- a 5280-byte message, sent as three pieces, joined and acked;
- a message to ALL, received by STA-B.

The first run showed the one gap: the node refused the all-stations address
(reason 3) and the client still called the broadcast sent. That is why ALL
now goes one copy per station and refusals are counted. Nothing here has been
near a modem or a radio; the dummy load comes after the modem is in.

What the node's document settles for the client:

- Framing is edition 1.2 with the version byte, as the client sends.
- The node checks the low 29 bits of an address and accepts no more than 28
  bits, which is what the peers file allows.
- The node asks the far node for client delivery confirmation on every ARQ
  request, whatever `s5066-confirm` says, so a confirm means the far core
  has the piece. `s5066-confirm` matters only on another node.
- The node never sends keep-alive, data flow on/off, subnet availability or an
  unbind indication. The client still handles them, for other nodes.
- Non-ARQ deliveries are reported as mode 2; the client ignores the mode.

## Agreed with greybeard-ale (Nov 23 2026)

Their orchestrator confirmed these for the real node, and the client matches:

| Item | The node | The client |
|---|---|---|
| Transport | TCP 127.0.0.1:5066, several clients at once | connects to 127.0.0.1, port setting |
| Framing | Annex A: 0x90 0xEB, version 0x00, 2-byte size, one primitive | the same |
| MTU | 2048, sent in S_BIND_ACCEPTED; a bigger U_PDU is rejected | reads it from the reply and cuts pieces to fit |
| Rank | 0 to 15 accepted | 0 |
| SAP | 3 (HMTP), ARQ in the service type | the same |
| Own address | from the node's settings | never set by the client |
| Rebinding a bound SAP | refused | binds once per connection; binds again only after the connection drops |

Still to settle:

1. The address field: settled by their docs/s5066-node.md and the run above.
2. The time to live: settled the same way (4 bits of repeats, 20 bits of time
   to live).
3. Their core lockdown limits which settings the box's settings file may set.
   The new settings (`link`, `s5066-*`, `appa-port`) need adding to that list
   before a station can turn these on from the settings file. That ask has
   gone to the image owner.
