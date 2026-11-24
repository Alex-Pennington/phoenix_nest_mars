/*
 * pna_cipher.h: the appliance's cipher slot, the one file an adapter author
 * needs. docs/cipher-slot.md is the spec; docs/cipher-adapter-guide.md walks
 * through writing a module.
 *
 * A cipher module is a shared library (module.so) with one exported entry
 * point, pna_cipher_module_v1(), which hands the appliance a table: the
 * module's name, the form of its output, the size of its keys, and four
 * functions:
 *
 *   load_key   checks one key from the key tape and turns it into the form
 *              the other three take ("ready")
 *   key_check  a short check value two stations read to each other by voice
 *   encrypt    message text in, cipher out
 *   decrypt    cipher in, message text out, or refused
 *
 * The appliance does everything else: the ACP 127 layout, groups, digest,
 * keys and their dates, zeroizing, and the radio. The module never sees a
 * file, a socket or the operator.
 *
 * Plain C, so a module can be written in C, C++, Rust or anything that can
 * export a C function. Every buffer belongs to the appliance; a module keeps
 * nothing between calls.
 */
#ifndef PNA_CIPHER_H
#define PNA_CIPHER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The slot's version. A module built against another version is refused. */
#define PNA_CIPHER_ABI 1

/* What each function returns. */
#define PNA_CIPHER_OK 0       /* done */
#define PNA_CIPHER_SHORT 1    /* the out buffer is too small: *len holds the size needed; nothing written */
#define PNA_CIPHER_BAD_KEY 2  /* load_key: this key is refused (wrong size, bad parity, weak, ...) */
#define PNA_CIPHER_REFUSED 3  /* decrypt: does not check (damaged, changed, or another key) */
#define PNA_CIPHER_FAILED 4   /* anything else; say why in err */

/* The form of what encrypt gives and decrypt takes. */
#define PNA_CIPHER_BYTES 0 /* any bytes; the appliance writes them as 8-character groups */
#define PNA_CIPHER_TEXT 1  /* the module's own on-air text: A-Z, 0-9, space and newline only */

/* Limits the appliance holds to. */
#define PNA_CIPHER_MAX_TEXT 65536  /* message text, and encrypt's output */
#define PNA_CIPHER_MAX_KEY 4096    /* one key on the tape, and one ready key */
#define PNA_CIPHER_MAX_CHECK 16    /* key_check: 1 to 16 of A-Z and 0-9 */

/*
 * Every function below:
 *  - writes a NUL-terminated reason in plain words to err (err_cap bytes,
 *    the NUL included) when it does not return PNA_CIPHER_OK;
 *  - takes an out buffer and its length: on the way in *len is the room
 *    there is; on PNA_CIPHER_OK it is what was written; on PNA_CIPHER_SHORT
 *    it is what is needed (the appliance then calls again);
 *  - is never called from two threads at once;
 *  - keeps no key, ready key or message text after it returns.
 *
 * indicator: the key indicator, five characters ("QXR01").
 * dtg: format line 5 as sent, before the indicator ("P 231040Z NOV 2026"). A module
 *      may bind it to the cipher so a changed DTG is refused; ours does.
 */
typedef struct pna_cipher_module {
    unsigned abi;        /* PNA_CIPHER_ABI */
    const char* name;    /* 1 to 32 of a-z, 0-9 and '-'; the name key tapes and handouts carry */
    const char* version; /* 1 to 32 printable characters */
    unsigned output;     /* PNA_CIPHER_BYTES or PNA_CIPHER_TEXT */
    size_t key_bytes;    /* the size of one key on the tape, 1 to PNA_CIPHER_MAX_KEY */

    int (*load_key)(const char* indicator, const unsigned char* key, size_t key_len, unsigned char* ready,
                    size_t* ready_len, char* err, size_t err_cap);

    int (*key_check)(const char* indicator, const unsigned char* ready, size_t ready_len, char* check,
                     size_t* check_len, char* err, size_t err_cap);

    int (*encrypt)(const char* indicator, const unsigned char* ready, size_t ready_len, const char* dtg,
                   const unsigned char* text, size_t text_len, unsigned char* out, size_t* out_len, char* err,
                   size_t err_cap);

    int (*decrypt)(const char* indicator, const unsigned char* ready, size_t ready_len, const char* dtg,
                   const unsigned char* in, size_t in_len, unsigned char* text, size_t* text_len, char* err,
                   size_t err_cap);
} pna_cipher_module;

/* The one symbol a module exports. The appliance calls it once, at load. */
typedef const pna_cipher_module* (*pna_cipher_entry)(void);
#define PNA_CIPHER_ENTRY "pna_cipher_module_v1"

#ifdef __cplusplus
}
#endif

#endif /* PNA_CIPHER_H */
