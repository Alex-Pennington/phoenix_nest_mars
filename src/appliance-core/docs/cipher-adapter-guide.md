# Writing a cipher module

This guide is for the person who puts a net's cipher into the appliance. You
write a small adapter, the **module**, that sits between the appliance's
cipher slot and the cipher you already have. You need only two files from
this repository:

- `sdk/pna_cipher.h`, the slot: four functions and a table;
- `sdk/cipher_template.c`, a module with the four functions left for you.

The spec is docs/cipher-slot.md. Read it once; this guide is the steps.

## 1. Copy the template

    cp sdk/cipher_template.c my_cipher_module.c

Change `.name` in the table at the bottom to your module's name: 1 to 32 of
a-z, 0-9 and `-`, such as `net-cipher`. Tapes and handouts carry it, so pick
it once. `pna-aes` is taken. Set `.version` too.

Set `KEY_BYTES` to the size of one key in bytes, as your key tapes will hold
it, up to 4,096.

## 2. Pick the output form

- **`PNA_CIPHER_BYTES`** if your cipher gives bytes. The appliance writes
  them as 8-character groups and gives the same bytes back to `decrypt`.
  This is the easy choice.
- **`PNA_CIPHER_TEXT`** if your cipher writes its own on-air groups. Give A-Z,
  0-9, space and newline only. See docs/cipher-slot.md, "Two output forms".

## 3. Fill in the four functions

Each is marked FILL IN in the template.

1. **`load_key`.** You get one key from the tape. Refuse it with
   `PNA_CIPHER_BAD_KEY` and a reason if it is the wrong size or your cipher
   won't take it. Otherwise write the form the other three functions take
   into `ready`: the key itself, or a key schedule.
2. **`key_check`.** Write 1 to 16 of A-Z and 0-9 that two stations read to
   each other by voice. It must be the same each time for one key, differ
   from key to key, and give nothing away about the key. If your cipher has
   its own check value, use it.
3. **`encrypt`.** Message text in, cipher out. Use a fresh nonce or message
   key every time if your cipher has one. You may bind in the indicator and
   the `dtg`.
4. **`decrypt`.** Cipher in, message text out. If it doesn't check (a damaged
   message, a changed one, another key), return `PNA_CIPHER_REFUSED`. Never
   return text you are not sure of.

Rules for all four:

- If the buffer you were given is too small, put the size you need in `*len`
  and return `PNA_CIPHER_SHORT`. Write nothing. The appliance calls again with
  more room.
- On anything but `PNA_CIPHER_OK`, put a plain reason in `err`.
- Keep nothing after you return: no key, no text, no state in globals. Wipe
  your own scratch buffers.
- Don't open files, sockets or windows, and don't print. The appliance does
  all of that.

## 4. Build it

    cc -shared -fPIC -fvisibility=hidden -O2 -I path/to/sdk \
       -o module.so my_cipher_module.c -l<your cipher's library> \
       -Wl,-rpath,'$ORIGIN/lib'

The `rpath` lets `module.so` find libraries you put in `lib/` beside it. Any
language that can export a C function works the same way; the only symbol the
appliance looks for is `pna_cipher_module_v1`.

## 5. Run the harness until every line says PASS

    pna-cipher-check ./module.so

It loads your module and runs every check in docs/cipher-slot.md: keys load,
bad keys are refused, messages round-trip, a wrong key is refused, a small
buffer is not overrun. It ends with a line such as

    RESULT: PASS (12 passed, 0 warnings, 0 failed)

and exits 0. A FAIL line says what went wrong. A WARN line is worth fixing
but doesn't stop the module. The appliance runs the same checks when it
installs a module and refuses one that fails any.

`pna-cipher-check --builtin` shows what a passing run looks like.

## 6. Put it in a folder

    net-cipher/
      module.so
      lib/            any libraries module.so needs (optional)
      ABOUT.txt       who made it, its version, who to call (optional)

Every file in `lib/` is part of the module's set hash, so a changed library is
caught. Links instead of files are refused.

## 7. Give it to the keymaster

The keymaster loads it into their own appliance first:

    appliance-cli cipher install net-cipher/

then makes the net's tapes and handouts as usual. With your module in the
slot, `net_tape` makes version 2 tapes for your cipher, and every handout
gets a signed `CIPHER` line with your module's name and set hash. For tapes
made off the box:

    pna-keymaster --net "TEST NET" --start 20261201 --days 31 \
       --cipher net-cipher/ --out test-net.ekt

## 8. The packet each member gets

    pna-keymaster packet --handout STA-B-handout.txt --cipher net-cipher/ \
       --out /media/usb/packet-sta-b

It checks the handout, checks that the module's set hash is the one the
handout names, and writes:

    packet-sta-b/
      handout.txt
      cipher/
        module.so
        lib/
        module.sha256
        ABOUT.txt

## 9. What the member does

    appliance-cli handout import /media/usb/packet-sta-b

The appliance checks the handout, checks the module against the signed hash,
runs the harness checks on it, installs it, then opens the tape. The member
reads the key check to the keymaster by voice, as for any net. If the module
was changed on the way, nothing is installed and the reason is shown.

If the installed module is changed later, the appliance turns codress off at
its next start and says why. Loading the packet again puts it right.

## Updating a module

Give it a new version, build it, run the harness, and make new handouts: the
set hash changes, so old handouts won't install the new module.

## What not to put in a module

- Keys. Keys come from the tape through `load_key`, every time.
- A way to skip the checks, or to read without the right key.
- Real keys, passphrases or traffic in any test you ship with it.
