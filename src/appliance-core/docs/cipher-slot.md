# The cipher slot

The appliance does codress through one slot. A **cipher module** fills it.
Our own cipher, `pna-aes`, is built in and fills the slot when nothing else
is installed. A net that uses another cipher hands its module out in the
member's handout packet, and the appliance loads it from there.

This page is the spec. `sdk/pna_cipher.h` is the one file a module author
needs, and `docs/cipher-adapter-guide.md` walks through writing one.

## What the slot does, and what the module does

The appliance does everything but the cipher itself:

- the ACP 127 layout: format lines 1 to 4 in the clear, format line 5 with
  the date-time group and the key indicator, `GR n`, BT, the groups, BT, the
  digest and NNNN;
- writing a module's bytes as 8-character groups, or a text module's lines as
  they are;
- the key tapes, the keyring, which key sends today and which keys still read,
  zeroizing keys when they run out;
- the handout, the packet and signatures;
- the radio.

The module does four things, and nothing else. It never sees a file, a socket
or the operator.

## The four functions

A module is a shared library, `module.so`, that exports one C function,
`pna_cipher_module_v1()`. It returns a table: the module's name and version,
the form of its output, the size of one key, and four functions.

| Function | Takes | Gives |
| --- | --- | --- |
| `load_key` | the indicator and one key from the tape | the key in the form the other three take ("ready"), or `BAD_KEY` |
| `key_check` | the indicator and the ready key | a short check, 1 to 16 of A-Z and 0-9, two stations read by voice |
| `encrypt` | the indicator, ready key, DTG and message text | the cipher |
| `decrypt` | the indicator, ready key, DTG and the cipher | the message text, or `REFUSED` |

- **indicator** is the five-character key indicator, as on the tape (`QXR01`).
- **dtg** is format line 5 as sent, before the indicator
  (`P 231040Z NOV 2026`). A module may bind it in so a changed DTG won't
  decipher. Ours does.
- **text** is everything after format line 5 of the plaindress: the body, the
  inner digest and the signature. Up to 65,536 bytes.
- **key** is the bytes on the tape, `key_bytes` long. Up to 4,096 bytes.

### The table

| Field | Rule |
| --- | --- |
| `abi` | `PNA_CIPHER_ABI`, now 1. Any other version is refused. |
| `name` | 1 to 32 of a-z, 0-9 and `-`. Tapes and handouts carry it. `pna-aes` is taken. |
| `version` | 1 to 32 printable characters. Shown to the operator. |
| `output` | `PNA_CIPHER_BYTES` or `PNA_CIPHER_TEXT` (below). |
| `key_bytes` | the size of one key on the tape, 1 to 4,096. |

### What each function returns

| Code | Meaning |
| --- | --- |
| `OK` (0) | done |
| `SHORT` (1) | the out buffer is too small. `*len` holds the size needed; nothing was written. The appliance gives a bigger buffer and calls once more. |
| `BAD_KEY` (2) | `load_key` only: this key is refused (wrong size, weak, bad parity). |
| `REFUSED` (3) | `decrypt` only: it does not check. Damaged, changed, or another key. |
| `FAILED` (4) | anything else |

On anything but `OK`, the module writes a reason in plain words to `err`,
NUL-terminated, at most `err_cap` bytes with the NUL. The operator sees it.

### Buffers and calls

- Every buffer belongs to the appliance. On the way in, `*len` is the room
  there is. On `OK` it is what was written. On `SHORT` it is what is needed.
  A module never writes past the room it was given.
- The appliance wipes every key, ready key and message buffer after each call.
  The module keeps nothing between calls: no key, no text, no state.
- The appliance never calls the module from two threads at once.

### Two output forms

- **BYTES.** `encrypt` gives any bytes. The appliance pads them to whole
  5-byte groups and writes each group as 8 characters of A-Z and 0-9, seven
  groups to a line. `decrypt` gets the same bytes back, with the pad taken off.
  `GR` counts the groups. Our own cipher uses this form.
- **TEXT.** `encrypt` gives the on-air text itself: A-Z, 0-9, space and
  newline only. The appliance writes it line by line between the two BT
  lines, one space between words, blank lines dropped, and `GR` counts its
  words. `decrypt` gets it back the same way: each line's words joined by one
  space, each line ending in a newline. A line may not start with the word
  `PAGE`, which the appliance keeps for page headings. Use this form when a
  net's cipher has its own group layout.

## The built-in module: pna-aes

- AES-256-GCM. The key for each message is drawn with HKDF-SHA256 from the
  tape key and a fresh 12-byte nonce.
- The indicator and DTG are bound in as associated data.
- Output is BYTES: the nonce, the cipher and the 16-byte tag.
- 32-byte keys. The key check is 4 characters from SHA-256 of the key.

It is also built as `pna-aes.so`, a module like any other, so the harness
and the guide have a working example. Codress made before the slot (labels
`PNA CODRESS 1`) does not read with the slot's `pna-aes`
(`PNA CODRESS 2`). The tapes are unchanged.

## The harness: does a module fit the slot?

One command checks any module and prints PASS, WARN or FAIL for each check:

    pna-cipher-check path/to/module.so
    pna-cipher-check --builtin

It exits 0 when nothing failed, 1 when something did, and 2 on a usage error.
The checks:

1. it loads, and its table is good;
2. a good key loads;
3. bad keys are refused: empty, one byte short, one byte over;
4. the key check is the same each time, and differs from key to key;
5. messages round-trip, from one character to 6,000, and the text never shows
   in the clear;
6. a whole codress message round-trips through the appliance;
7. the same text enciphers differently each time (WARN if not);
8. a wrong key is refused, and never reads;
9. a changed cipher, a changed DTG and another indicator are refused (WARN if
   not);
10. a small buffer gets `SHORT`, and nothing is written past it.

The appliance runs the same checks before it installs a module. A module with
any FAIL is not installed.

## How a module travels: the handout packet

A packet is a folder, on a stick:

    packet/
      handout.txt        the member's handout, signed by the keymaster
      tape.ekt           the key tape, if it isn't sealed in the handout
      cipher/
        module.so        the module
        lib/             any libraries module.so needs (optional)
        module.sha256    the set hash, for people to read
        ABOUT.txt        who made it and what it is (optional)

- The handout carries the module's name and **set hash** in a signed line:

      CIPHER TOY-TEXT 3F9A...(64 hex)

  and two step lines telling the member to load the whole folder.
- The **set hash** is SHA-256 over a list of `sha256  path` lines, one for
  `module.so` and one for every file under `lib/`, sorted by path. So a
  changed library changes it too. A link instead of a file is refused.
- `pna-keymaster packet --handout FILE [--tape FILE] --cipher FOLDER --out FOLDER`
  builds the folder. It checks the handout, checks the module's set hash
  against the CIPHER line, and never writes over a folder that exists.
- A handout with no CIPHER line is for a net on `pna-aes`. Its packet has no
  `cipher/` folder.

## Installing, and loading at startup

On the member's box, `handout import FOLDER` (the `handout_import` call with
`packet`):

1. checks the handout's signatures;
2. checks the module's set hash against the signed CIPHER line, and refuses a
   module that differs;
3. copies the module next to the station's database (`NAME.cipher/`) and
   hashes the copy again;
4. loads it and runs every check; any FAIL refuses it;
5. writes a **pin**: the name and set hash;
6. puts the module in the slot, then opens the tape.

At startup the appliance reads the pin:

- no pin: `pna-aes` is in the slot;
- a pin, and the files still hash to it: that module is in the slot;
- a pin, and the files changed or won't load: the slot is **empty**. Codress
  is off, with the reason, until the net's packet is loaded again. It never
  falls back to another cipher on its own.

An operator can also run `cipher` (what is in the slot), `cipher install
FOLDER` and `cipher remove` (back to `pna-aes`). See docs/api.md.

## Keys for another cipher

A tape for a module other than `pna-aes` is a version 2 tape:

    PNA KEY TAPE 2
    cipher toy-text
    ...

Its keys are `key_bytes` long. Version 1 tapes are unchanged and are always
`pna-aes`.

- A tape is refused unless its cipher is the one in the slot. The slot's
  `load_key` runs on every key; one refused key keeps the whole tape out.
- The keyring keeps each key's cipher. Keys for a cipher not in the slot are
  held, shown as not usable, and never offered for sending or reading.
- `pna-keymaster --cipher FOLDER` and `net_tape` make tapes for the cipher in
  the slot.

## What the slot never does

- It runs no module that failed a check, and no module the handout didn't
  name.
- It holds no module key in a file a module can reach. Keys stay in the
  keyring.
- It has no MSC code, names or adapter. Whoever leadership trusts writes an
  adapter for their cipher against this page alone.
