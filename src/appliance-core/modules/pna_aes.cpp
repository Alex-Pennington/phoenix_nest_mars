// pna-aes: the appliance's own cipher, and the default module in the cipher
// slot (sdk/pna_cipher.h, docs/cipher-slot.md). It is built into the
// appliance, and also built as modules/pna-aes.so so the harness can be
// proven on a real loadable module.
//
// AES-256-GCM from OpenSSL under a message key made with HKDF-SHA256 from the
// tape key, a fresh random 12-byte nonce for every message, and the key
// indicator and date-time group bound in as associated data. Output is
// nonce || ciphertext || tag, as bytes; the appliance writes the groups.
#include "pna_aes.h"

#include "../src/crypto.h"
#include "../src/keys.h"

#include <cstring>
#include <string>

namespace {

constexpr size_t kKey = 32;
constexpr size_t kNonce = 12;
constexpr size_t kTag = 16;
const char* kInfo = "PNA CODRESS 2 AES-256-GCM ";
const char* kAad = "PNA CODRESS 2|";

int say(char* err, size_t cap, const char* why, int code) {
    if (err && cap) {
        std::strncpy(err, why, cap - 1);
        err[cap - 1] = '\0';
    }
    return code;
}

bool room(size_t need, size_t* len) {
    if (*len < need) {
        *len = need;
        return false;
    }
    *len = need;
    return true;
}

int load_key(const char* indicator, const unsigned char* key, size_t key_len, unsigned char* ready,
             size_t* ready_len, char* err, size_t err_cap) {
    if (!indicator || std::strlen(indicator) != 5) return say(err, err_cap, "the key indicator is not 5 characters", PNA_CIPHER_FAILED);
    if (key_len != kKey) return say(err, err_cap, "a pna-aes key is 32 bytes", PNA_CIPHER_BAD_KEY);
    bool zero = true;
    for (size_t i = 0; i < key_len; ++i) zero = zero && key[i] == 0;
    if (zero) return say(err, err_cap, "the key is all zeros", PNA_CIPHER_BAD_KEY);
    if (!room(kKey, ready_len)) return PNA_CIPHER_SHORT;
    std::memcpy(ready, key, kKey);
    return PNA_CIPHER_OK;
}

int key_check(const char* indicator, const unsigned char* ready, size_t ready_len, char* check, size_t* check_len,
              char* err, size_t err_cap) {
    if (ready_len != kKey) return say(err, err_cap, "not a ready pna-aes key", PNA_CIPHER_FAILED);
    std::string c = pna::keys::key_check(indicator, std::string(reinterpret_cast<const char*>(ready), ready_len));
    if (!room(c.size(), check_len)) return PNA_CIPHER_SHORT;
    std::memcpy(check, c.data(), c.size());
    return PNA_CIPHER_OK;
}

std::string message_key(const unsigned char* ready, const std::string& nonce, const char* indicator) {
    std::string k(reinterpret_cast<const char*>(ready), kKey);
    std::string mk = pna::crypto::hkdf_sha256(k, nonce, std::string(kInfo) + indicator, 32);
    pna::crypto::wipe(k);
    return mk;
}

std::string aad(const char* indicator, const char* dtg) { return std::string(kAad) + indicator + "|" + dtg; }

int encrypt(const char* indicator, const unsigned char* ready, size_t ready_len, const char* dtg,
            const unsigned char* text, size_t text_len, unsigned char* out, size_t* out_len, char* err,
            size_t err_cap) {
    if (ready_len != kKey) return say(err, err_cap, "not a ready pna-aes key", PNA_CIPHER_FAILED);
    if (!room(kNonce + text_len + kTag, out_len)) return PNA_CIPHER_SHORT;
    std::string nonce;
    try {
        nonce = pna::crypto::random_bytes(kNonce);
    } catch (...) {
        return say(err, err_cap, "no random numbers from the system", PNA_CIPHER_FAILED);
    }
    std::string mk = message_key(ready, nonce, indicator);
    std::string plain(reinterpret_cast<const char*>(text), text_len);
    std::string sealed = pna::crypto::aes256gcm_seal(mk, nonce, aad(indicator, dtg), plain);
    pna::crypto::wipe(mk);
    pna::crypto::wipe(plain);
    if (sealed.size() != text_len + kTag) return say(err, err_cap, "the cipher failed", PNA_CIPHER_FAILED);
    std::memcpy(out, nonce.data(), kNonce);
    std::memcpy(out + kNonce, sealed.data(), sealed.size());
    return PNA_CIPHER_OK;
}

int decrypt(const char* indicator, const unsigned char* ready, size_t ready_len, const char* dtg,
            const unsigned char* in, size_t in_len, unsigned char* text, size_t* text_len, char* err,
            size_t err_cap) {
    if (ready_len != kKey) return say(err, err_cap, "not a ready pna-aes key", PNA_CIPHER_FAILED);
    if (in_len < kNonce + kTag) return say(err, err_cap, "too short to be a pna-aes message", PNA_CIPHER_REFUSED);
    if (!room(in_len - kNonce - kTag, text_len)) return PNA_CIPHER_SHORT;
    std::string nonce(reinterpret_cast<const char*>(in), kNonce);
    std::string sealed(reinterpret_cast<const char*>(in) + kNonce, in_len - kNonce);
    std::string mk = message_key(ready, nonce, indicator);
    std::string plain;
    bool ok = pna::crypto::aes256gcm_open(mk, nonce, aad(indicator, dtg), sealed, plain);
    pna::crypto::wipe(mk);
    if (!ok) return say(err, err_cap, "damaged in transit, changed, or a different key", PNA_CIPHER_REFUSED);
    std::memcpy(text, plain.data(), plain.size());
    pna::crypto::wipe(plain);
    return PNA_CIPHER_OK;
}

const pna_cipher_module kModule = {PNA_CIPHER_ABI, "pna-aes", "2", PNA_CIPHER_BYTES, kKey,
                                   load_key,       key_check, encrypt, decrypt};

}  // namespace

extern "C" const pna_cipher_module* pna_aes_module(void) { return &kModule; }
