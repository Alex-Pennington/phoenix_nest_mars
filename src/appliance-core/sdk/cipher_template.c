/*
 * cipher_template.c: the starting point for a cipher module.
 * docs/cipher-adapter-guide.md walks through filling it in.
 *
 * Copy this file, give the module its name, and fill in the four functions
 * marked FILL IN with calls into the cipher you are adapting. Build it as a
 * shared library and run pna-cipher-check on it until every line says PASS:
 *
 *   cc -shared -fPIC -fvisibility=hidden -O2 -I path/to/sdk \
 *      -o module.so my_cipher_module.c -l<your cipher's library> \
 *      -Wl,-rpath,'$ORIGIN/lib'
 *   pna-cipher-check ./module.so
 *
 * As it stands it builds and loads, then fails the harness at "messages
 * round-trip", because nothing is enciphered yet.
 */
#include "pna_cipher.h"

#include <string.h>

/* FILL IN: the size of one key on the key tape, in bytes. */
#define KEY_BYTES 32

/* Writes a reason for the operator into err. */
static int say(char* err, size_t err_cap, const char* why, int code) {
    if (err_cap) {
        strncpy(err, why, err_cap - 1);
        err[err_cap - 1] = '\0';
    }
    return code;
}

/* Puts n in *len; false when the room the appliance gave is too small. */
static int room(size_t n, size_t* len) {
    int ok = *len >= n;
    *len = n;
    return ok;
}

/*
 * load_key: one key from the tape, KEY_BYTES long. Check it (size, and
 * anything else your cipher refuses) and write the form the other three
 * functions take into ready. That can be the key itself, or a key schedule.
 */
static int load_key(const char* indicator, const unsigned char* key, size_t key_len, unsigned char* ready,
                    size_t* ready_len, char* err, size_t err_cap) {
    (void)indicator;
    if (key_len != KEY_BYTES) return say(err, err_cap, "a key for this cipher is 32 bytes", PNA_CIPHER_BAD_KEY);
    /* FILL IN: any other check your cipher makes on a key. */
    if (!room(KEY_BYTES, ready_len)) return PNA_CIPHER_SHORT;
    memcpy(ready, key, KEY_BYTES);
    return PNA_CIPHER_OK;
}

/*
 * key_check: 1 to 16 of A-Z and 0-9 that two stations read to each other to
 * confirm they hold the same key. It must give nothing usable away about the
 * key. If your cipher has its own check value, use it.
 */
static int key_check(const char* indicator, const unsigned char* ready, size_t ready_len, char* check,
                     size_t* check_len, char* err, size_t err_cap) {
    (void)indicator;
    (void)ready;
    (void)ready_len;
    /* FILL IN: your cipher's check value. This placeholder is not one. */
    if (!room(4, check_len)) return PNA_CIPHER_SHORT;
    memcpy(check, "FILL", 4);
    (void)err;
    (void)err_cap;
    return PNA_CIPHER_OK;
}

/*
 * encrypt: message text in, cipher out. With PNA_CIPHER_BYTES (below) the
 * output is any bytes and the appliance writes the groups; with
 * PNA_CIPHER_TEXT it is your cipher's own on-air text (A-Z, 0-9, space,
 * newline). Use a fresh nonce or message key for every message if your
 * cipher has one.
 */
static int encrypt(const char* indicator, const unsigned char* ready, size_t ready_len, const char* dtg,
                   const unsigned char* text, size_t text_len, unsigned char* out, size_t* out_len, char* err,
                   size_t err_cap) {
    (void)indicator, (void)ready, (void)ready_len, (void)dtg, (void)text, (void)text_len, (void)out, (void)out_len;
    /* FILL IN: work out the output size, call room(), then encipher. */
    return say(err, err_cap, "this module is the template: encrypt is not written yet", PNA_CIPHER_FAILED);
}

/*
 * decrypt: cipher in, message text out. Return PNA_CIPHER_REFUSED when it
 * does not check (damaged, changed, or another key), never garbage.
 */
static int decrypt(const char* indicator, const unsigned char* ready, size_t ready_len, const char* dtg,
                   const unsigned char* in, size_t in_len, unsigned char* text, size_t* text_len, char* err,
                   size_t err_cap) {
    (void)indicator, (void)ready, (void)ready_len, (void)dtg, (void)in, (void)in_len, (void)text, (void)text_len;
    /* FILL IN: decipher, check, and write the text. */
    return say(err, err_cap, "this module is the template: decrypt is not written yet", PNA_CIPHER_FAILED);
}

static const pna_cipher_module kModule = {
    PNA_CIPHER_ABI,
    "my-cipher",      /* FILL IN: the name key tapes and handouts will carry */
    "0.1",            /* FILL IN: your version */
    PNA_CIPHER_BYTES, /* or PNA_CIPHER_TEXT */
    KEY_BYTES,
    load_key,
    key_check,
    encrypt,
    decrypt,
};

__attribute__((visibility("default"))) const pna_cipher_module* pna_cipher_module_v1(void) { return &kModule; }
