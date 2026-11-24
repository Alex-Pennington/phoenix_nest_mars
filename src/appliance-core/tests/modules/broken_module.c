/*
 * A BROKEN module for the tests only: it takes any key and "enciphers" by
 * copying the text. The harness must fail it, and the appliance must refuse
 * to install it.
 */
#include "../../sdk/pna_cipher.h"

#include <string.h>

static int copy(const unsigned char* in, size_t n, unsigned char* out, size_t* ol) {
    if (*ol < n) {
        *ol = n;
        return PNA_CIPHER_SHORT;
    }
    memcpy(out, in, n);
    *ol = n;
    return PNA_CIPHER_OK;
}
static int load_key(const char* i, const unsigned char* k, size_t kl, unsigned char* r, size_t* rl, char* e, size_t c) {
    (void)i, (void)e, (void)c;
    if (kl == 0) {
        r[0] = 0;
        *rl = 1;
        return PNA_CIPHER_OK;
    }
    return copy(k, kl, r, rl);
}
static int key_check(const char* i, const unsigned char* r, size_t rl, char* ch, size_t* cl, char* e, size_t c) {
    (void)i, (void)r, (void)rl, (void)e, (void)c;
    return copy((const unsigned char*)"AAAA", 4, (unsigned char*)ch, cl);
}
static int encrypt(const char* i, const unsigned char* r, size_t rl, const char* d, const unsigned char* t, size_t tl,
                   unsigned char* o, size_t* ol, char* e, size_t c) {
    (void)i, (void)r, (void)rl, (void)d, (void)e, (void)c;
    memcpy(o, t, tl < *ol ? tl : *ol); /* writes what fits and says OK: wrong on purpose */
    *ol = tl < *ol ? tl : *ol;
    return PNA_CIPHER_OK;
}
static int decrypt(const char* i, const unsigned char* r, size_t rl, const char* d, const unsigned char* in, size_t il,
                   unsigned char* o, size_t* ol, char* e, size_t c) {
    (void)i, (void)r, (void)rl, (void)d, (void)e, (void)c;
    return copy(in, il, o, ol);
}
static const pna_cipher_module kModule = {PNA_CIPHER_ABI, "broken", "0", PNA_CIPHER_BYTES, 32,
                                          load_key,       key_check, encrypt, decrypt};
__attribute__((visibility("default"))) const pna_cipher_module* pna_cipher_module_v1(void) { return &kModule; }
