# The engine straight to the modem: 110D Appendix A

Status: built, and tested against greybeard-ale's real modem server
(`m110_lan` in pennington_m110a_demod, commit 60835f2) with no radio: two
engines, each with its own `m110_lan`, passed a message and its ACK as 110A
600S audio. Not yet on a radio.

## When to use it

| Link | Use it when |
|---|---|
| `link = s5066` | the box runs a STANAG 5066 node. The node is the modem's Appendix A client and adds ARQ; the engine talks to the node (docs/stanag-5066-client.md). |
| `link = appa` | there is no 5066 node, or for bursts outside 5066. The engine is the modem's Appendix A client itself. |
| `link = burst` | the default: gale-tx makes a burst WAV, captures come back as files. |

Appendix A has no addresses and no ARQ. With `appa`, every station within
range hears every transmission, and the engine's own frame header says who it
is for, as with `burst`. Our message ACK and retry are the only delivery check.

## Turning it on

```
appliance-core --station STA-A --link appa
```

| Setting | Default | What it does |
|---|---|---|
| `link` | `burst` | `appa` drives the modem over Appendix A |
| `appa-port` | 3000 | the modem's TCP data socket on 127.0.0.1 (A.4.2) |

If the modem runs on another box, its port is forwarded over ssh, so the
engine still connects to 127.0.0.1. The waveform (600S and so on) is set on
the modem: Appendix A gives the DTE no field to choose it (A.5.1.1.9).

## How it works

`AppaModem` (`src/appa_modem.h`) is a `ModemDriver`, so nothing above it
changed. Packets are built and read by `src/appa.h`, written from the
appendix's text and listings.

1. **Connect** (A.5.1.2.1). CONNECT version 12 both ways, CONNECT_ACK both
   ways, the modem's CONNECTION_PROBE echoed, then Initial Setup, Transmit
   Setup (the modem's rate), Tx Status FLUSHED and Carrier Detect. The link is
   up at that Tx Status.
2. **Send** (A.5.1.2.5). TRANSMIT_ARM. When Tx Status says armed and ready,
   the frame goes as data packets (one FIRST AND LAST, or FIRST, CONTINUATION
   and LAST at 4072 bytes each), then TRANSMIT_START. The send is done when Tx
   Status comes back to FLUSHED. A NACK (Table A-IV), DRAINING FORCED, or no
   FLUSHED within the air time at the modem's rate plus 60 seconds fails the
   send, and the queue retries as it always has.
3. **Receive** (A.5.1.2.6). Between CARRIER DETECTED and NO CARRIER the data
   goes to the frame reader as it arrives. The core holds its own sends while
   carrier is up.
4. **Keep-alive** (A.5.1.2.2). The empty DATA packet after 1.5 seconds with
   nothing sent. The modem closes a link that sends nothing for 30 seconds.
5. **One DTE** (A.4.1). The modem holds one DTE at a time and closes any other
   connection with no bytes. The driver then logs "the modem is held by
   another program" once, and tries again every 3 seconds.
6. **Lost link.** A closed connection, an ERROR packet, a version other than
   12, or no handshake within 10 seconds closes the link and starts over. A
   send in progress fails and is retried.

A bad CRC is dropped without a word and the reader looks for the next
preamble (A.5.1.2.3).

## What it doesn't do

- **The UDP data socket** (A.5.2). TCP covers a modem on the same box or over
  an ssh forward. The UDP socket, with its Reed-Solomon packet coding, is for
  lossy LANs.
- **Synchronous and async standard modes** (A.5.1.2.8). `m110_lan` offers only
  async data-only mode, and so does this driver.
- **Choosing the waveform.** That's the modem's setting.

## Tests

`tests/test_appa.cpp` (always runs):

- The CRC against the A.5.3 polynomial worked a second way; CONNECT, the
  keep-alive and a data packet byte by byte; the reader fed one byte at a time
  with junk and a broken packet in the stream; the modem's Tx Status, Carrier
  Detect, Transmit Setup, NACK and data packets read back.
- No modem: not ready, sends refused, one line in the log.
- Two cores through stand-in modems: another program holding the modem (said
  once, then connected when it lets go); a message and its ACK; a message
  bigger than one data packet as FIRST then LAST; one NACK then a retry that
  goes; a message to ALL; keep-alives while idle; the modem dropping the link
  and the driver connecting again.

`tests/test_appa_m110.cpp` (runs when `PNA_M110_LAN` names the `m110_lan`
program; otherwise it says it was skipped):

- The driver against the real `m110_lan` at 600S: 304 bytes with every byte
  value sent, written to a WAV, fed back as receive audio, and the same 304
  bytes delivered.
- Two engines, STA-A and STA-B, each with its own `m110_lan`, each modem's
  transmit WAVs fed to the other's receive input: a message from STA-A to
  STA-B and STA-B's ACK, first try.

## Still to settle

1. Their core lockdown limits which settings the box's settings file may set.
   `link` and `appa-port` need adding to that list before a station can turn
   this on from the settings file. That ask has gone to the image owner.
2. On a radio, CARRIER DETECTED from `m110_lan` comes after the whole burst is
   decoded, not at the preamble (its doc says so: the receiver is a batch
   decoder). The core holds its sends only while carrier is up, so a send can
   start while a far station is still mid-burst. greybeard-ale's link gate
   (`link-gate`) covers that on air.
