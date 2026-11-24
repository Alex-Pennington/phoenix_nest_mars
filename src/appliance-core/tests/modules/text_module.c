/*
 * A TOY module for the tests only: NOT A CIPHER FOR USE. It exists to prove
 * the slot carries a module that writes its own on-air text (PNA_CIPHER_TEXT)
 * through a whole codress message. A SHA-256 keystream with a 4-byte check,
 * written as letters A to P, five to a group.
 */
#include "../../sdk/pna_cipher.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>

#define KEY 16
#define NONCE 4
#define MAC 4

static int say(char* err, size_t cap, const char* why, int code) {
    if (cap) {
        strncpy(err, why, cap - 1);
        err[cap - 1] = 0;
    }
    return code;
}

static void sha(const unsigned char* a, size_t an, const char* b, const unsigned char* c, size_t cn,
                unsigned char out[32]) {
    EVP_MD_CTX* x = EVP_MD_CTX_new();
    EVP_DigestInit_ex(x, EVP_sha256(), NULL);
    EVP_DigestUpdate(x, a, an);
    EVP_DigestUpdate(x, b, strlen(b));
    EVP_DigestUpdate(x, c, cn);
    EVP_DigestFinal_ex(x, out, NULL);
    EVP_MD_CTX_free(x);
}

/* Keystream byte i, and the check, both bound to the indicator and DTG. */
static void stream(const unsigned char* key, const unsigned char* nonce, const char* ind, const char* dtg,
                   unsigned char* buf, size_t n) {
    unsigned char seed[KEY + NONCE + 4], h[32];
    memcpy(seed, key, KEY);
    memcpy(seed + KEY, nonce, NONCE);
    for (size_t i = 0; i < n; i += 32) {
        unsigned int blk = (unsigned int)(i / 32);
        seed[KEY + NONCE] = (unsigned char)(blk >> 24);
        seed[KEY + NONCE + 1] = (unsigned char)(blk >> 16);
        seed[KEY + NONCE + 2] = (unsigned char)(blk >> 8);
        seed[KEY + NONCE + 3] = (unsigned char)blk;
        char ctx[96];
        snprintf(ctx, sizeof ctx, "S|%s|%s", ind, dtg);
        sha(seed, sizeof seed, ctx, (const unsigned char*)"", 0, h);
        for (size_t j = 0; j < 32 && i + j < n; ++j) buf[i + j] ^= h[j];
    }
}

static void mac(const unsigned char* key, const char* ind, const char* dtg, const unsigned char* body, size_t n,
                unsigned char out[MAC]) {
    char ctx[96];
    unsigned char h[32];
    snprintf(ctx, sizeof ctx, "M|%s|%s", ind, dtg);
    sha(key, KEY, ctx, body, n, h);
    memcpy(out, h, MAC);
}

static int load_key(const char* ind, const unsigned char* key, size_t key_len, unsigned char* ready,
                    size_t* ready_len, char* err, size_t cap) {
    (void)ind;
    if (key_len != KEY) return say(err, cap, "a toy key is 16 bytes", PNA_CIPHER_BAD_KEY);
    if (*ready_len < KEY) {
        *ready_len = KEY;
        return PNA_CIPHER_SHORT;
    }
    memcpy(ready, key, KEY);
    *ready_len = KEY;
    return PNA_CIPHER_OK;
}

static int key_check(const char* ind, const unsigned char* ready, size_t rl, char* check, size_t* cl, char* err,
                     size_t cap) {
    unsigned char h[32];
    if (rl != KEY) return say(err, cap, "not a ready toy key", PNA_CIPHER_FAILED);
    if (*cl < 6) {
        *cl = 6;
        return PNA_CIPHER_SHORT;
    }
    sha(ready, KEY, "CHECK", (const unsigned char*)ind, strlen(ind), h);
    for (int i = 0; i < 6; ++i) check[i] = (char)('A' + h[i] % 26);
    *cl = 6;
    return PNA_CIPHER_OK;
}

/* Bytes to letters A-P, two a byte, five a group, ten groups a line. */
static size_t letters_for(size_t n) {
    size_t chars = 2 * n, groups = (chars + 4) / 5;
    return chars + (groups - 1) + 1; /* spaces/newlines between groups; padding with 'A' is part of chars */
}

static int encrypt(const char* ind, const unsigned char* ready, size_t rl, const char* dtg, const unsigned char* text,
                   size_t tl, unsigned char* out, size_t* ol, char* err, size_t cap) {
    if (rl != KEY) return say(err, cap, "not a ready toy key", PNA_CIPHER_FAILED);
    size_t n = NONCE + tl + MAC;
    size_t chars = 2 * n;
    size_t groups = (chars + 4) / 5;
    size_t need = groups * 5 + groups; /* each group plus a space or newline */
    (void)letters_for;
    if (*ol < need) {
        *ol = need;
        return PNA_CIPHER_SHORT;
    }
    unsigned char buf[NONCE + PNA_CIPHER_MAX_TEXT + MAC];
    if (tl > PNA_CIPHER_MAX_TEXT) return say(err, cap, "too long", PNA_CIPHER_FAILED);
    if (RAND_bytes(buf, NONCE) != 1) return say(err, cap, "no random numbers", PNA_CIPHER_FAILED);
    memcpy(buf + NONCE, text, tl);
    stream(ready, buf, ind, dtg, buf + NONCE, tl);
    mac(ready, ind, dtg, buf, NONCE + tl, buf + NONCE + tl);
    size_t w = 0;
    for (size_t g = 0; g < groups; ++g) {
        for (size_t k = 0; k < 5; ++k) {
            size_t c = g * 5 + k;
            unsigned char v = 0;
            if (c < chars) v = (c % 2 == 0) ? (buf[c / 2] >> 4) : (buf[c / 2] & 15);
            out[w++] = (unsigned char)('A' + v);
        }
        out[w++] = (g + 1) % 10 == 0 || g + 1 == groups ? '\n' : ' ';
    }
    memset(buf, 0, sizeof buf);
    *ol = w;
    return PNA_CIPHER_OK;
}

static int decrypt(const char* ind, const unsigned char* ready, size_t rl, const char* dtg, const unsigned char* in,
                   size_t il, unsigned char* text, size_t* tl, char* err, size_t cap) {
    if (rl != KEY) return say(err, cap, "not a ready toy key", PNA_CIPHER_FAILED);
    unsigned char buf[NONCE + PNA_CIPHER_MAX_TEXT + MAC + 8];
    size_t chars = 0;
    for (size_t i = 0; i < il; ++i) {
        if (in[i] == ' ' || in[i] == '\n') continue;
        if (in[i] < 'A' || in[i] > 'P') return say(err, cap, "not toy groups", PNA_CIPHER_REFUSED);
        if (chars / 2 >= sizeof buf) return say(err, cap, "too long", PNA_CIPHER_REFUSED);
        unsigned char v = (unsigned char)(in[i] - 'A');
        if (chars % 2 == 0) buf[chars / 2] = (unsigned char)(v << 4);
        else buf[chars / 2] |= v;
        ++chars;
    }
    size_t n = chars / 2;
    /* The last group's padding letters decode to zero bytes after the end; the
       real length is the longest one whose check matches. */
    for (size_t len = n; len >= NONCE + MAC; --len) {
        if (len + 3 < n) break; /* padding is at most 2 bytes */
        unsigned char m[MAC];
        mac(ready, ind, dtg, buf, len - MAC, m);
        if (memcmp(m, buf + len - MAC, MAC) == 0) {
            size_t t = len - NONCE - MAC;
            if (*tl < t) {
                *tl = t;
                return PNA_CIPHER_SHORT;
            }
            stream(ready, buf, ind, dtg, buf + NONCE, t);
            memcpy(text, buf + NONCE, t);
            memset(buf, 0, sizeof buf);
            *tl = t;
            return PNA_CIPHER_OK;
        }
    }
    memset(buf, 0, sizeof buf);
    return say(err, cap, "the toy check does not match", PNA_CIPHER_REFUSED);
}

static const pna_cipher_module kModule = {PNA_CIPHER_ABI, "toy-text", "1", PNA_CIPHER_TEXT, KEY,
                                          load_key,       key_check,  encrypt, decrypt};

__attribute__((visibility("default"))) const pna_cipher_module* pna_cipher_module_v1(void) { return &kModule; }
