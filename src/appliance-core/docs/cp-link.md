# Working with MSC stations

The MSC Communications Processor (CP, in `src/cp`) is the program that moves
MSC traffic on and off the air. This page says what the core took from it,
what it left alone, and why. CP's cipher library was not opened or read;
where CP enciphers or deciphers, that step is a closed box and our cipher
slot (`docs/cipher-slot.md`) stands in its place.

## What the core now does, as CP does

- **No ack, try again.** A message that went out and got no ack goes again
  after `ack_timeout_s` (120 s; `--ack-timeout`), up to `ack_retries` more
  times (2; `--ack-retries`). Then it waits as `unacked`. When that station
  is heard again, its unacked messages go again by themselves. A late ack
  still marks a message acked.
- **A gap before keying up.** The core waits `tx_gap_s` after its last send
  or the last byte it heard before it keys up again, acks included, so a
  half-duplex station has turned round. The appliance waits 6 s, as CP does
  (`--tx-gap`).
- **EMCON.** `emcon` stops every send, acks included, until it is lifted. It
  holds across a restart.
- **The operator's hand on the queue.** `hold`, `release`, `cancel` and
  `requeue` a message. A message on the air can't be cancelled.
- **Garbled and overheard copies kept.** A frame whose text fails its check
  is kept as `garbled` (bad bytes shown as dots) and not acked, so the far
  station sends it again. Traffic for another station is kept as
  `overheard`. Neither is acked.
- **Plain text both ways.** An MSC station sends traffic with no PNA1 frame.
  Set its link format to `plain` and the core sends it the message text
  alone, and reads what it sends: several messages in one burst, a message
  split over two bursts, or one cut short (kept as garbled once the channel
  has been quiet `loose_quiet_s`, 5 s).
- **CP's chat form and its answers.** To a plain station, `send_chat` builds
  CP's form, `chat|ID|TO DE FROM // text // K DD-HHMMZ`, and waits for
  `ack|ID`. A `nack|ID` sends it again. A CP chat to this station is kept
  and answered `ack|ID`; one cut short is answered `nack|ID`. CP acks only
  chat sent to its own call, never to ALL, and so does the core.

Formal traffic sent plain has no on-air ack, as with CP, so it is marked
sent once it is on the air.

## What the core left alone, and why

- **CP's cipher library and its header.** CP puts a header in front of
  enciphered traffic inside its cipher step. That is part of the closed box,
  so the core neither reads nor writes it. That library is fitted later,
  once CP has been studied.
- **Compression and CP's digest check.** CP may squeeze traffic before it
  goes and checks a digest after it comes in. Matching these needs a
  recording of a CP station sending in the clear; until there is one, the
  core sends and reads plain text as it stands.
- **CP's TCP and UDP ports, its XML files and outside modem programs.**
  Out of scope: the appliance meets other stations only over the air.
- **Things CP does that the core does better already:** its queue order,
  its Baudot converter and its log clean-up.
- **Not yet:** file transfer, sending raw orderwire, message expiry, a raw
  receive log, and giving up on a link after a set wait.
