# Codress, keys, signatures and the digest

This is how appliance-core protects and checks traffic. It is our own design
from standard parts. It is **not** the MARS cipher. Nobody outside this project
has reviewed or approved it, and it talks only to other appliances that hold
the same keys. When MARS leadership supplies their cipher, it goes in next to
this one under its own procedure, and this one stays for practice and for
nets that choose it.

Before you send codress on the air, check that your authority allows it on
that channel. Messages enciphered to hide their meaning are not allowed on
amateur frequencies. Signatures and the digest hide nothing and may go
anywhere plain text goes.

All of it uses OpenSSL's libcrypto, version 3.0 or later. Nothing here is a
home-made algorithm.

## What there is

| Piece | What it does | Built from |
|---|---|---|
| Digest | Spots a message that was garbled on the air | MD5, as Message Creator (MSC) computes it |
| Codress | Hides a message's text from anyone without the net key | AES-256-GCM, HKDF-SHA256 |
| Key tape | Gives every station in a net the same dated keys | 256-bit keys from OpenSSL's random generator |
| Station keys | Each station's own keypair, made on the box | Ed25519 to sign, X25519 to receive |
| Station card | The public half, to hand out by USB or over the air | Text, self-signed, with a fingerprint you can read aloud |
| Signature | Proves who sent a message and that nobody changed it | Ed25519 |
| Sealed tape | Sends a net's key tape to one station only | X25519, HKDF-SHA256, AES-256-GCM, signed with Ed25519 |
| Backup | Lets a rebuilt box keep its station keys and the cards it held | scrypt, AES-256-GCM |
| Handout | A member's paper or stick to join a net, and to rally from on a bad day | Text, signed by the keymaster with Ed25519 |

## The digest

MSC puts a `[DIGEST:...]` line before NNNN. appliance-core computes it the same
way, so MSC stations can check ours and we can check theirs. It is checked
against MSC's own messages in `tests/fixtures` (samples 06, 08, 34 and 36 of
the second batch).

The rule, as MSC applies it: take the text between the two BT lines, leave
out the first line that starts UNCLAS, collapse every run of whitespace to one
space, and compute the MD5 in upper-case hex. The reader also trims each line
before it looks for BT and UNCLAS, so indented traffic still checks.

- Every message `compose_send` builds carries one, except chat lines.
- `parse_traffic` says `"digest":"pass"`, `"fail"` or `"none"`.
- On `fail` the message is still shown and stored, marked possibly garbled,
  with a ready `SVC` repeat request in `repeat_request`. It isn't refused,
  because most of a garbled message is usually readable.

**The digest is an error check, not a signature.** Anyone can change a message
and compute a new digest. It tells you the text arrived as it was sent, not who
sent it. Signatures do that.

## Codress

A codress message keeps format lines 1 to 4 in the clear, so it can be relayed
like any other. Format line 5 is the date-time group and the key indicator, for
example `231040Z NOV 2026 QXR01`. Then come `GR n`, BT, the cipher groups, BT,
the outer digest and NNNN.

- The key for each message is drawn with HKDF-SHA256 from the tape key and a
  fresh random 12-byte nonce, so no key and nonce pair is ever used twice.
- AES-256-GCM enciphers everything after format line 5, including the inner
  digest and the signature. The cleartext header (the indicator and the
  date-time group) is bound in as associated data. If anyone changes the
  header, the groups or the key, the message won't decipher, and the reader
  says so. It never shows a wrong text.
- The groups are 8 characters, A to Z and 0 to 9, so they pass through every
  ACP 127 station. Each group carries 5 bytes. The nonce and the tag add 28
  bytes to every message, about 6 groups.
- With no key for today, codress isn't offered. A message for a key we don't
  hold is stored as received and listed with the reason.

## Key tapes

A keymaster makes a tape for a net:

    pna-keymaster --net "PRACTICE NET" --days 31 --out /media/usb/practice.tape

Each period, a day by default, gets a fresh random 256-bit key. A station
loads the tape with `appliance-cli keys import FILE`.

- A key sends from its start time until its end time. It still reads traffic
  for 6 hours after that, then the core zeroizes it on its own.
- `keys list` shows each key's state, and `keys zeroize` wipes a key or a
  whole tape at once.
- The keyring file (`NAME.keys`, next to the database) is readable only by the
  engine's user. Each change is written to a new file, the old one is
  overwritten with zeros, and the new one takes its place.
- A tape is plain text with a SHA-256 check at the end. Copy it by USB, and
  wipe the stick afterwards.

The tape format is ours. The December 2025 Intel HEX loader idea is not used.

## Station keys and cards

Each appliance makes its own keypair the first time it starts (`NAME.id`,
next to the database, readable only by the engine). There are two keys:

- **Ed25519** signs messages, cards and sealed tapes.
- **X25519** receives sealed tapes.

The two kinds exist because signing and receiving are separate jobs, and each
curve is the standard one for its job. Hardware tokens hold them the same way.
A private key never leaves the box except in a backup that the operator chooses
to make.

A card is the public half:

    appliance-cli station card

It is a short block of text, upper case, that goes in a message's text. Cards
can be passed by USB or with `card_send` over the air. The card signs itself,
so a damaged card is refused. The card's fingerprint is four groups of four
characters, such as `K7QM 2XRD 9FTA HW3C`. The fingerprint leaves out I, O, 0
and 1, so it reads cleanly over voice.

**Read the fingerprint back by voice** with the other station, then mark it
checked:

    appliance-cli cards check STA-B "K7QM 2XRD 9FTA HW3C"

An unchecked card still works, but replies say `checked: false`, so nobody
mistakes a card that came over the air for one that was confirmed. A different
card for a station you already hold is refused, unless you import it with
`--replace` after a voice check.

## Signatures

`compose_send` signs every message except chat (`sign=N` turns it off). The
signature is one line, `[SIG:CALL:...]`, after the digest. A station that
doesn't know it skips the line.

- The signature covers the text from the first date-time group or FM line
  through the last BT. Format lines 1 to 3 are left out, because relays
  rewrite them.
- `parse_traffic` gives `signature` as `good`, `bad`, `unknown` (no card held
  for the signer) or `none`, and `signer_checked` says whether that card was
  checked by voice.
- In codress, the signature is inside the cipher, so it shows only to stations
  that hold the key.

Plain traffic on the air can carry a signature, because a signature hides
nothing. That is the answer to "how do we know it came from leadership"
without enciphering. A keyed HMAC over plain text would do the same within one
net, but only for stations that hold the net key, and any of them could forge
it. Signatures need no shared secret, so we use them.

## Sealed tapes: sending keys over the air

A keymaster can send a net's tape to each member station, sealed to that
station's card:

    appliance-cli api net_tape net="PRACTICE NET" days=31 members="STA-B STA-C" send=Y

This makes the tape, loads it on the keymaster's own box, seals one copy to
each member whose card it holds, and with `send=Y` queues each copy. Members
with no card are listed under `no_card`. A copy for USB comes back as text.

The receiving station runs `keys open ID` (or `keys open FILE`). A sealed tape
opens only:

- at the station it was sealed to,
- with the keymaster's card already held, checked by voice and named a
  keymaster (from a handout, or `cards check CALL FP --keymaster`),
- with every group and the keymaster's signature intact.

A station that holds a card but hasn't named it keymaster won't take tapes
from it. That stops any member whose card you checked from handing you keys.

The groups on the air are cipher. The key tape is never in the clear.

## Building a net from nothing

If everything has to be rebuilt, leadership needs only the appliances:

1. Each station's box makes its keypair the first time it starts.
2. Each member sends its card to the keymaster, over the air or on a stick.
   The keymaster sends its own card back.
3. Each pair reads its fingerprints back by voice and runs `cards check`.
4. The keymaster runs `net_tape` with the members.
5. Each member runs `keys open`.

`test_station` does this with three stations: one over the air and one by USB.
It then does it a second time from empty folders, and checks that nothing from
the first net opens in the second.

## Member handouts

A handout is what a keymaster gives each member to join a net, on paper or a
stick. `net_tape` makes one per member with `handouts=Y`. It holds no secret in
the clear:

    PNA MEMBER HANDOUT 1
    NET PRACTICE NET
    FOR STA-B
    ISSUED 202611230000Z
    BY NCS-A
    TAPE QXR 202611230000Z 202612240000Z
    CHECK QXR01 4HNP
    KEYMASTER NCS-A K7QM 2XRD 9FTA HW3C
    KEYMASTER ALT-K ...
    MEMBER STA-B ...
    MEMBER STA-C ...
    STEP 1 LOAD THIS HANDOUT ON YOUR APPLIANCE (HANDOUT IMPORT).
    STEP 2 READ THE CHECK FOR QXR01 TO A KEYMASTER BY VOICE.
    STEP 3 MAKE A STATION BACKUP. KEEP IT OFF THE BOX WITH THIS HANDOUT.
    STEP 4 BAD DAY: RESTORE YOUR BACKUP (OR START FRESH), LOAD THIS ...
    (each keymaster's card)
    (the tape, sealed to this member only)
    SIG ...
    SIG ...
    END HANDOUT

- **KEYMASTER lines** name who may issue this net's tapes: the keymaster first,
  then any alternates (`net_tape keymasters="ALT-K"`). Their cards are in the
  handout, so a member who loads it holds them checked and named, with no
  separate voice check.
- **CHECK** is a 4-character check value for the first key (from SHA-256 of
  the key, not the key). Read it to the keymaster by voice to confirm the tape
  loaded. `keys list` shows the check for every key.
- **The sealed tape** opens only on the box it was sealed to. A member with no
  card at the keymaster yet gets a handout with no tape, and steps that say to
  send the card.
- An **alternate's handout** carries every member's card too, so the
  alternate can issue tapes if the keymaster is lost.
- The keymaster signs the whole handout. A handout that was changed, or is for
  another station, is refused.

The layout is ours for now. Once leadership's member handout style is in hand
(with every key blanked), the handout follows its order and wording. The
contents stay the same.

## BAD_DAY: rallying from stale material

The drill: every box is wiped, and members bring back what they kept, which
may be weeks old. A stale handout still works, because the cards in it don't
expire; only its tape does.

1. **Restore.** A member with a backup restores it: the station keys and every
   card it held come back, with their checked and keymaster marks. Net keys are
   never in a backup. A member without one starts fresh, and the box makes new
   keys.
2. **Load the old handout** (`handout import FILE`). The keymasters come back,
   checked and named. Its tape has ended, or was sealed to the old keys, and
   the reply says which.
3. **Signed plain traffic works at once.** It needs no tape, and the keymaster
   can check every signature.
4. **New card, if the keys are new.** The member sends its card. The keymaster
   reads the fingerprint back by voice, then loads it with `--replace`.
5. **New tape.** Any keymaster on the handout runs `net_tape`. With the
   keymaster lost, the alternate restores its own backup and does the same.
6. `rally` shows, at each step, what the box holds and what to do next.

`test_badday` runs the whole drill. A normal day builds a net of NCS-A, its
alternate ALT-K, STA-B and STA-C, with handouts and backups (STA-C never makes
one). 45 days later every box is wiped. NCS-A and STA-B restore. STA-C rallies
from its stale handout and a new card. NCS-A issues a new tape, STA-B opens it
over the air and STA-C from its new handout, and codress works again. Then
NCS-A is lost: ALT-K restores, finds its own old tape ended, and issues the
next tape. A station that never named ALT-K keymaster refuses that tape.

## Backups

    appliance-cli api station_backup passphrase="..."

This returns the station keys and every card the station holds, with their
checked and keymaster marks, enciphered with a passphrase of 12 characters or
more (scrypt, N=2^15, r=8, p=1, then AES-256-GCM). It is dated, and net keys
are never in it. Keep it off the box with your handout. `station_restore`
puts it back on a rebuilt box for the same station: the card and fingerprint
stay the same, and the reply gives the backup's age. Older backups with the
station keys only still restore. Without a backup, make new keys and send the
new card. The voice check catches anyone pretending to be the station.

## Limits to know

- **Zeroizing on flash.** The core overwrites keys in memory and in its files
  before it lets go of them. But SD cards and SSDs move data around inside, so
  old copies can survive on the card. For a box that is lost or retired, wipe
  or destroy the card. Full-disk encryption on the appliance image would help
  too; that is greybeard-ale's image, so it's their call.
- **Shared net keys.** Every member of a net can read all of the net's
  traffic. Signatures, not the cipher, prove who sent a message.
- **No forward secrecy within a period.** If a day's key leaks, that day's
  traffic can be read. Short periods limit the damage.
- **The keymaster box** holds every tape it made. Keep it offline when it
  isn't making tapes.

## Moving to hardware tokens

The plan was YubiKey-style tokens, and the format leaves room for them:

- The card holds only public keys. A token that does Ed25519 signing and
  X25519 key agreement (OpenPGP cards and YubiKey 5 firmware 5.2.3 and later
  do both) can hold the private halves. Cards, fingerprints, signatures and
  sealed tapes don't change, so stations with and without tokens can share a
  net.
- Moving a station's keys onto a token means loading `NAME.id` onto the token
  and then deleting the file. Alternatively, make new keys on the token and
  send the new card.
- The code change is narrow. `station::sign` and the one X25519 step in
  `open_tape` would call the token through OpenSSL's provider interface or
  PKCS#11 instead of using the key in memory.
- A later tape version could seal each station's keys to its token alone, so
  that a stolen box without its token reads nothing.

Nothing is built for tokens yet.
