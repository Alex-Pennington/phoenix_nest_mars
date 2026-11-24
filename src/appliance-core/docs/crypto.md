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
- All of this is our own module, `pna-aes`, in the **cipher slot**. A net can
  run another module instead, handed out in the member's packet:
  docs/cipher-slot.md is the spec and docs/cipher-adapter-guide.md the guide.
  The layout above stays the appliance's; only the groups between the BT
  lines come from the module.

## Key tapes

A keymaster makes a tape for a net:

    pna-keymaster --net "PRACTICE NET" --days 31 --out /media/usb/practice.ekt

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
- On the stick it travels as a **key tape file**, framed the way MSC frames
  its `.ekt` tapes: a run of records, each a 4-byte big-endian length and its
  bytes. The records are a head label (`[PNA KEY TAPE]`), the warning
  `[WARNING] - Editing this file will corrupt the keys!`, `<CHECKSUM>` with
  the SHA-256 of the tape, and `<KEYTAPE>` with the tape. `--text` writes the
  plain tape instead; `keys import` takes either.
- **An MSC tape is refused.** Its head label says `[ENCRYPTED KEY TAPE]`, and
  the import stops at that first record: it never reads the rest, never opens
  MSC's keys, and says to load the tape the net's keymaster made.
- Each key carries the **five labels MSC's key list shows**, as notes on the
  tape, so a keymaster used to MSC reads ours the same way:

      # KEY_CKR:QXR01
      # KEYNAME:QXR01 QXR1126
      # KEYDESC:PRACTICE NET KEY 1 OF 31
      # INTRVAL:Daily
      # CREATED:23Nov2026@10:40:00
      key QXR01 202611240000Z 202611250000Z ...

  KEY_CKR is the key's indicator, KEYNAME adds the tape and the month and year
  it starts, INTRVAL is Daily, Weekly, Monthly or Every N hours, and CREATED is
  when the tape was made (UTC). They are not secrets.

- A tape for a cipher module other than `pna-aes` is version 2: a
  `cipher NAME` line after the head, and keys of the module's size. Version 1
  tapes are `pna-aes`. A tape for a module not in the slot is refused.

The tape and its cipher are ours; only the file's framing and labels follow
MSC's. The December 2025 Intel HEX loader idea is not used.

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
Naming a keymaster is the member's own choice: see the next section.

The groups on the air are cipher. The key tape is never in the clear.

A tape of two weeks or less goes as one message. A longer one (a 31-day
tape is about 13 KB sealed, 62 days about 26 KB) goes in numbered parts: see
[Parts](#parts-long-tapes-and-bundles-over-the-air).

## Parts: long tapes and bundles over the air

One message carries at most 8192 bytes. A sealed tape of a month or more, or
a startup bundle for a region whose book has grown, is longer. It goes the way
ACP 127 sends a long message: in numbered sections, each one its own signed
plain message, headed so a part read alone says what it belongs to.

    PART 2 OF 5 PNA TAPE 4HNPQ2XR
    (lines of the sealed tape)
    END PART 2 OF 5

The tag (8 characters, from the block's hash) ties the parts of one block
together. Each part carries up to 6000 bytes and the sender's signature.

At the receiver:

- **Held until complete.** Parts are kept as they come. `key_open` or
  `bundle_load` on any part gathers the set and opens it only when every part
  is here, signed by the same station. Until then it says how many are here
  and which numbers are missing.
- **Signed, every part.** A part whose signature is bad, or signed by another
  station than the rest, isn't taken (`refused_parts`). The whole tape is still
  sealed and signed inside, as for a tape in one message.
- **Asking for the rest.** `parts_request` (form PARTS_ASK) sends the sender a
  signed request for the missing numbers:

      PNA PARTS REQUEST TAPE 4HNPQ2XR
      MISSING 2 4
      END REQUEST

- **The resend queue.** At a rally many members ask at once, often for the
  same part. Net control's box pools the requests (`parts_queue`, form
  PARTS_QUEUE): one row per station and part, with repeat asks from a
  station merged. A part several stations asked for goes once, to the whole
  net (`ALL`, which every station takes and nobody acks); a part only one
  station asked for goes to that station. The operator sees one line, such as
  "Resend queue: 7 sends for 20 stations", and sends it with one press, now or
  held for the next sitting of a net on the schedule (the held parts go out by
  themselves when the slot starts). The box never keys up on its own for a
  request: nothing goes out until the operator sends the queue.
- **Only to who it was for.** A request is served only when its signature
  checks and the part was sent to that station before (`not_ours` otherwise).
  A part that comes to `ALL` alone doesn't start a set at a member: some part
  of it must have come to that member by name.
- **Pacing.** At most 12 resends go out in any hour
  (`--parts-resend-per-hour`, or `parts-resend-per-hour` in the settings; 0
  for no cap). What the cap holds back stays in the queue, and the reply says
  when there is room again. A station that asks again for a part already sent
  to it twice is flagged, not served a third time: the operator looks into it
  (its antenna, the path, the band) and can still send it by hand with
  `parts_resend` (form PARTS_REPEAT). EMCON refuses a send now; a send held
  for a net slot waits, and goes when EMCON is off.
- **Tapes don't pool across members.** Each member's tape is sealed to that
  member, so its parts are that member's alone and go to that member. Only a
  block every station got the same, such as a bundle given as a file, goes
  once for all.
- **Dropped after the hold time.** A set still incomplete after 72 hours
  (`--parts-hold-h`, or `parts-hold-h` in the settings; 0 holds forever) is
  dropped: its old parts no longer count. Asking the sender to send the whole
  block again starts it afresh. A set that came complete is never dropped.

A short tape still goes whole, as before, so boxes built earlier take it. They
also don't take parts: send them a long tape on a stick. Bundle parts from
earlier builds (`PNA BUNDLE PART 2 OF 3 TAG 4HNPQ2XR`) are still read.

## Choosing your keymasters

A keymaster's tapes decide whose net a member is on, so the member chooses.
Nothing names a keymaster for them: not a handout, a bundle, a card or a
backup from someone else. Trust is given over the air, by a voice the member
knows, or in person.

- **Accept:** `keymaster accept CALL "FINGERPRINT"` (`keymaster_accept`),
  after reading the fingerprint back by voice, or `--in-person`. The
  fingerprint must match the card the box holds; a wrong one changes nothing.
  `cards check CALL FP --keymaster` does the same in one step.
- **Refuse:** `keymaster refuse CALL` (`keymaster_refuse`). The station's
  tapes are not taken, its keymaster mark comes off, and any handout from it
  waiting on the member is dropped. The refusal is kept against that card's
  fingerprint, so a later handout from it says so instead of asking again. A
  refused station can still be accepted later, by the same check.
- **Not yet decided:** a handout from a station the member hasn't chosen waits
  on the box. It opens no tape and installs no cipher module, and the
  member cards it carries are held unchecked. Accepting the keymaster loads
  every handout waiting on it.
- `keymaster list` shows who was accepted, refused, and who is still waiting
  on the member's answer. `rally` asks first when a keymaster is waiting.

Signed plain traffic needs no keymaster at all: a member who has chosen no one
yet, or refused everyone, still sends and checks signed messages.

## Building a net from nothing

If everything has to be rebuilt, leadership needs only the appliances:

1. Each station's box asks for its call the first time it starts
   (`setup_station`) and makes its keypair and card then.
2. Whoever holds the startup data (roster, frequency card, schedules) loads it
   with `import` and hands out a startup bundle (next section), so nobody types
   it in. With no documents at all, net control enters the channels by hand
   (`channel_put`) and the bundle carries them like imported ones.
3. Each member sends its card to the keymaster, over the air or on a stick.
   The keymaster sends its own card back.
4. Each pair reads its fingerprints back by voice and runs `cards check`. The
   member who chooses this keymaster runs `keymaster accept`.
5. The keymaster runs `net_tape` with the members.
6. Each member runs `keys open`.

Every step is also a form on the operator screen (docs/forms.md): SETUP,
CHANNEL, BOOK, SCHEDULE, CARD_SEND, CARD_CHECK, BUNDLE_SEND, KEYMASTER,
NET_TAPE, KEY_OPEN. `test_forms` builds a net from empty boxes that way,
with form calls only.

`test_station` does this with three stations: one over the air and one by USB.
It then does it a second time from empty folders, and checks that nothing from
the first net opens in the second.

## Startup bundles

A startup bundle carries what a box needs to work a net, so a member doesn't
type it in: the station book, the channel plan, the net schedules and how
each station is reached (plain or PNA1). Channels go by designator in the
clear; their frequencies go only sealed to one member's card. Any box can build
one from what it holds, whether that came from `import` (leadership's roster,
frequency card and schedule) or was typed in.

    PNA STARTUP BUNDLE 1
    LABEL PRACTICE REGION
    BY NCS-A
    MADE 202611240000Z
    SIGNER NCS-A K7QM 2XRD 9FTA HW3C
    FOR STA-B
    STEP 1 CHECK THE SIGNER FINGERPRINT ABOVE BY VOICE OR IN PERSON.
    ...
    BOOK STA-B
    KIND STATION
    RI UHAAAB
    CHANNEL P1
    MODE USB
    USE PRIMARY
    SCHEDULE PRACTICE NET
    DAYS TUE,THU
    START 1900
    CHAN P1
    ...
    LINK STA-D PLAIN
    PNA SEALED PLAN 1      (in a member's own copy: the frequencies,
    TO STA-B                sealed to that member's card)
    FROM NCS-A
    ...
    END
    (the signer's card)
    SIG ...
    SIG ...
    END BUNDLE

- **Make:** `bundle make FILE --label "PRACTICE REGION" --for STA-B`
  (`bundle_make`) writes STA-B's copy for a stick. The station signs it with
  its own key; its card is inside. Without `--for` the copy is for anyone and
  carries no frequencies.
- **Send:** `bundle send STA-B,STA-C` (`bundle_send`) sends each station its
  own copy over the air as ordinary signed plain traffic, in numbered parts of up to
  6000 bytes when it is large (`PART 2 OF 3 PNA BUNDLE 4HNPQ2XR`, see
  [Parts](#parts-long-tapes-and-bundles-over-the-air)).
- **Frequencies, sealed to each member.** A member's copy carries every
  frequency in the plan, sealed to that member's card with our own sealing
  (X25519, HKDF-SHA256, AES-256-GCM, signed: the same construction as a
  sealed tape, under its own head so one can't be opened as the other). It
  opens only on that member's box. The keymaster seals only to a card it has
  checked by voice. A member whose card isn't held, or isn't checked yet, gets
  a copy with no frequencies, and its steps say to send the card and check it
  by voice. The reply says which (`frequencies`: `sealed`, `no_card`,
  `card_not_checked`, `none_held`).
- **Load:** `bundle load FILE|ID` (`bundle_load`). From a message id, the box
  gathers every part with the same tag and says which are missing. A bundle
  that was changed on the way is refused, even with a yes.
- **The member's yes:** a bundle signed by a station whose card this box has
  checked loads at once. Any other signer's bundle loads only on the member's
  `--yes`, and every entry it adds is marked "signer not checked".
- **It is data, not trust.** Loading a bundle never adds, checks or names a
  card, and never touches a key or a keymaster. Trust comes only from the
  member's own choice (Choosing your keymasters).
- **Nothing is removed.** Entries are added or replaced by name. A schedule
  that is already the same is counted as same, and one that changed keeps the
  sitting this box last opened.
- **No frequency in the clear.** Free text that looks like a frequency is left
  out when the bundle is made (the reply lists what was left out), and no
  reply or log line shows one. Loading a member's own copy fills the plan:
  channels the box lacks are added; a channel the box holds on another
  frequency is changed only when the signer's card is checked here, and kept
  otherwise. A copy sealed to someone else opens nothing. A designator the
  box still has no frequency for comes back in `need_frequency`. Personal
  columns in a roster (names, email, phone) never reach the book, so never
  reach a bundle.
- Everything is in the radio's character set, lines of 69 or fewer.

A bundle is separate from a handout: the handout carries keys and the
keymaster's cards for one member; the bundle carries operating data for
anyone.

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
    KEY_CKR:QXR01
    KEYNAME:QXR01 QXR1126
    KEYDESC:PRACTICE NET KEY TAPE QXR
    KEYPASS:NONE. THE TAPE BELOW OPENS ONLY ON STA-B. CHECK 4HNP
    ===========================================
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

- **KEYMASTER lines** name who would issue this net's tapes: the keymaster
  first, then any alternates (`net_tape keymasters="ALT-K"`). Their cards are
  in the handout, but loading it names nobody. The reply asks the member about
  each one ("NCS-A K7QM 2XRD 9FTA HW3C wants to be your keymaster. Accept?").
  Until the member accepts the issuer, after a voice check or in person, the
  handout waits: no tape is taken and no cipher module installed. A member who
  refuses keeps none of it. See Choosing your keymasters.
- **The KEY_CKR block** is laid out like leadership's passphrase list
  (KEY_CKR, KEYNAME, KEYDESC, KEYPASS, then a rule of `=`), so a member reads
  it where they expect. It holds no passphrase: KEYPASS says the tape opens
  only on this member's box, and gives the check to read by voice.
- **CIPHER**, when the net uses a module other than `pna-aes`, names it and
  its set hash, and two step lines say to load the whole packet folder. The
  packet (`pna-keymaster packet`) holds the handout and the module; see
  docs/cipher-slot.md.
- **CHECK** is a check value for the first key: 4 characters from SHA-256 of
  the key with `pna-aes`, or whatever the net's module gives. Read it to the keymaster by voice to confirm the tape
  loaded. `keys list` shows the check for every key.
- **The sealed tape** opens only on the box it was sealed to. A member with no
  card at the keymaster yet gets a handout with no tape, and steps that say to
  send the card.
- An **alternate's handout** carries every member's card too, so the
  alternate can issue tapes if the keymaster is lost.
- The keymaster signs the whole handout. A handout that was changed, or is for
  another station, is refused.

The rest of the layout is ours. Leadership's key list and passphrase list were
compared with every key and passphrase blanked; no key, passphrase or MSC
cipher file is used here.

## BAD_DAY: rallying from stale material

The drill: every box is wiped, and members bring back what they kept, which
may be weeks old. A stale handout still works, because the cards in it don't
expire; only its tape does.

1. **Restore.** A member with a backup restores it: the station keys and every
   card it held come back, with their checked and keymaster marks. Net keys are
   never in a backup. A member without one starts fresh, and the box makes new
   keys.
2. **Load the old handout** (`handout import FILE`). A keymaster the member
   already accepted, or whose mark came back with the backup, isn't asked
   about again. A member starting fresh is asked, and accepts after a voice
   check. Its tape has ended, or was sealed to the old keys, and the reply
   says which.
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
next tape. A station that never accepted ALT-K refuses that tape.

## Ground zero: the box from the can

Each member keeps a sealed can with a computer and this appliance, likely
never updated once sealed. `test_groundzero` runs two drills.

- **Three empty boxes.** NCS-A imports a made-up roster, frequency card and
  net schedule, makes a bundle, sends it to STA-B over the air and gives it to
  STA-C on a stick. STA-B's box waits until STA-B checks NCS-A's card by
  voice, then loads it; STA-C loads it on its own yes, and it stays marked so.
  Neither box gains a card, key or keymaster from it. Copies made per member
  carry the frequencies sealed: STA-B's opens only on STA-B's box and fills its
  plan; a member with no checked card gets none and a step to send it; a
  member who hasn't checked the signer keeps its own frequency for a channel
  until it does. Then NCS-A hands out
  tapes: STA-B refuses at first (no tape taken, signed plain traffic still
  works), then accepts after a second voice check; STA-C accepts in person.
- **The stale box.** STA-B's box is sealed with OLD-K as keymaster, a 31-day
  tape, a bundle and a frequency card. Four years later (the clock is
  injected) every key has ended and OLD-K is gone. Using only what's on the
  box: signed plain traffic with a new keymaster NEW-K first; NEW-K's tape is
  refused until STA-B accepts it; a stranger's handout is refused; STA-B
  accepts NEW-K by voice, opens the tape, and codress works again. STA-B then
  hands its old book and schedules to NEW-K as a bundle of its own.

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
- **Long tapes over the air** go in numbered parts, up to 99 of 6000 bytes.
  Parts lost on the air are asked for and sent again by hand; a set still
  incomplete after the hold time is dropped.
- **Sealed frequencies follow the card.** A member who makes new station keys
  needs a new copy of the bundle, sealed to the new card.
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
