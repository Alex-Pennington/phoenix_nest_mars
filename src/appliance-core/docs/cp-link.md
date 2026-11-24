# Working with MSC stations

The MSC Communications Processor (CP, in `src/cp`) is the program that moves
MSC traffic on and off the air. This page says what the core took from it,
what it left alone, and why. CP's cipher library was not opened or read;
where CP enciphers or deciphers, that step is a closed box and our cipher
slot (`docs/cipher-slot.md`) stands in its place, at the hand-off below.

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

## The cipher hand-off

The core hands a message to the cipher at the same point where CP hands one
to its cipher library, and the line stays there:

- **Sending:** the finished message text, after the form is checked, the
  serials are taken and the station's signature is added, goes to the slot
  one copy per station (`src/api_server.cpp`, marked CIPHER HAND-OFF (send)).
- **Receiving:** codress text as heard goes to the slot and comes back as the
  clear message (`src/core.cpp`, marked CIPHER HAND-OFF (receive)).

Everything on our side of that line works like MSC: the message format, the
plain-traffic framing, send, receive, ack and resend. Everything on the far
side is the cipher's. The slot's interface is our own (`docs/cipher-slot.md`)
and is not shaped to any other library's calls.

## What the core left alone, and why

- **CP's cipher library and what it does.** Everything CP leaves to its
  cipher library stays on the library's side of the hand-off below: the
  header CP puts in front of enciphered traffic, its checksum, and
  compression (CP passes its compression choice into the library call). The
  core neither builds nor reads any of it. The core runs our own cipher in
  the slot.
- **CP's TCP and UDP ports, its XML files and outside modem programs.**
  Out of scope: the appliance meets other stations only over the air.
- **Things CP does that the core does better already:** its queue order,
  its Baudot converter and its log clean-up.
- **Built from the public ACP 127 rules, not from CP's code:** orderwire,
  the raw receive log, giving up on a link, handling times and expiry, and
  files. CP's own versions of these were not read, so they may differ from
  CP in detail; see "ACP 127 handling" below.

## ACP 127 handling

These five came from the public ACP 127 rules and ordinary station practice,
not from CP's source.

- **Orderwire.** `orderwire` sends one line of operator text as typed, with
  no frame, serial or ack, ahead of queued traffic, to a station or to ALL.
  It is not kept as a message; the log keeps it. EMCON and the tx gap hold
  it like anything else.
- **Raw receive log.** Every burst heard goes, as heard, into a day file
  beside the station's database (`STATION.rx-raw/YYYY-MM-DD.log`, each burst
  under a `==` line with the UTC time and byte count). `rx_raw` shows the end
  of today's file. That is where orderwire from another station, and
  anything the core can't place, can be read. `--raw-rx-log off` stops it.
- **Giving up on a link.** With `--link-gate`, a message that has waited
  `--link-wait` seconds for a link to its station fails with "no link to
  STATION after N s"; `requeue` tries again. Off (0) by default. The wait
  starts again after a restart.
- **Handling times.** A message still queued, held or unacked past its
  precedence's handling time is said once in the log and listed by
  `overdue`. The defaults are FLASH 10 min, IMMEDIATE 30 min, PRIORITY 3 h
  and ROUTINE 6 h (`--handling 10,30,180,360`; 0 turns one off). Nothing is
  dropped for being late.
- **Expiry.** With `--expire-h H`, a message still queued, held or unacked H
  hours after it was filed is marked `expired` and not sent. `requeue` sends
  it again, and it is not expired a second time. Off (0) by default.
- **Files.** ACP 127 carries text only, in A-Z, 0-9, space and
  `- ? : ( ) . , /`, 69 characters a line. `send_file` puts a file in the
  text of an ACP 127 message as base32 (A-Z and 2-7), which any station
  passes as ordinary text:

  ```
  FILE SIZE 1234 CRC 1A2B3C4D
  NAME <the file name, base32>
  <data, base32, 64 characters a line>
  END FILE
  ```

  The far core logs "a file came in" and `get_file` gives it back after the
  size and CRC-32 check. A message holds 8 KB, so a file can be about 4.5 KB.
  A station without this core sees the block as text and can pass it on.
