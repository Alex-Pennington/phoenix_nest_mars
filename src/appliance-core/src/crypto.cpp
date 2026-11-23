// OpenSSL libcrypto wrappers. See crypto.h.
#include "crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <memory>
#include <stdexcept>

namespace pna {
namespace crypto {

namespace {
using Ctx = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
const unsigned char* u(const std::string& s) { return reinterpret_cast<const unsigned char*>(s.data()); }

std::string digest_of(const EVP_MD* md, const std::string& data) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int n = 0;
    if (EVP_Digest(data.data(), data.size(), out, &n, md, nullptr) != 1) return "";
    return std::string(reinterpret_cast<char*>(out), n);
}
}  // namespace

std::string random_bytes(size_t n) {
    std::string b(n, '\0');
    if (n && RAND_bytes(reinterpret_cast<unsigned char*>(&b[0]), static_cast<int>(n)) != 1)
        throw std::runtime_error("the random number generator failed");
    return b;
}

std::string hkdf_sha256(const std::string& ikm, const std::string& salt, const std::string& info, size_t len) {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> pc(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr),
                                                                   &EVP_PKEY_CTX_free);
    if (!pc || EVP_PKEY_derive_init(pc.get()) != 1 || EVP_PKEY_CTX_set_hkdf_md(pc.get(), EVP_sha256()) != 1 ||
        EVP_PKEY_CTX_set1_hkdf_key(pc.get(), u(ikm), static_cast<int>(ikm.size())) != 1 ||
        (!salt.empty() && EVP_PKEY_CTX_set1_hkdf_salt(pc.get(), u(salt), static_cast<int>(salt.size())) != 1) ||
        EVP_PKEY_CTX_add1_hkdf_info(pc.get(), u(info), static_cast<int>(info.size())) != 1)
        return "";
    std::string out(len, '\0');
    size_t n = len;
    if (EVP_PKEY_derive(pc.get(), reinterpret_cast<unsigned char*>(&out[0]), &n) != 1 || n != len) {
        wipe(out);
        return "";
    }
    return out;
}

std::string aes256gcm_seal(const std::string& key, const std::string& nonce, const std::string& aad,
                           const std::string& plain) {
    if (key.size() != 32 || nonce.size() != 12) return "";
    Ctx c(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    int n = 0;
    std::string out(plain.size() + 16, '\0');
    unsigned char* o = reinterpret_cast<unsigned char*>(&out[0]);
    if (!c || EVP_EncryptInit_ex(c.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
        EVP_EncryptInit_ex(c.get(), nullptr, nullptr, u(key), u(nonce)) != 1)
        return "";
    if (!aad.empty() && EVP_EncryptUpdate(c.get(), nullptr, &n, u(aad), static_cast<int>(aad.size())) != 1) return "";
    int len = 0;
    if (!plain.empty() && EVP_EncryptUpdate(c.get(), o, &len, u(plain), static_cast<int>(plain.size())) != 1) return "";
    int fin = 0;
    if (EVP_EncryptFinal_ex(c.get(), o + len, &fin) != 1) return "";
    if (EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_GET_TAG, 16, o + plain.size()) != 1) return "";
    return out;
}

bool aes256gcm_open(const std::string& key, const std::string& nonce, const std::string& aad,
                    const std::string& sealed, std::string& plain) {
    plain.clear();
    if (key.size() != 32 || nonce.size() != 12 || sealed.size() < 16) return false;
    Ctx c(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    const size_t clen = sealed.size() - 16;
    std::string out(clen, '\0');
    unsigned char* o = reinterpret_cast<unsigned char*>(&out[0]);
    int n = 0, len = 0, fin = 0;
    std::string tag = sealed.substr(clen);
    if (!c || EVP_DecryptInit_ex(c.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
        EVP_DecryptInit_ex(c.get(), nullptr, nullptr, u(key), u(nonce)) != 1)
        return false;
    if (!aad.empty() && EVP_DecryptUpdate(c.get(), nullptr, &n, u(aad), static_cast<int>(aad.size())) != 1) return false;
    if (clen && EVP_DecryptUpdate(c.get(), o, &len, u(sealed), static_cast<int>(clen)) != 1) {
        wipe(out);
        return false;
    }
    if (EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_SET_TAG, 16, &tag[0]) != 1 ||
        EVP_DecryptFinal_ex(c.get(), o + len, &fin) != 1) {
        wipe(out);
        return false;
    }
    plain = std::move(out);
    return true;
}

std::string sha256_hex(const std::string& data) { return to_hex(digest_of(EVP_sha256(), data)); }

std::string md5_hex_upper(const std::string& data) {
    std::string h = to_hex(digest_of(EVP_md5(), data));
    for (auto& c : h)
        if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
    return h;
}

std::string to_hex(const std::string& bytes) {
    static const char* d = "0123456789abcdef";
    std::string o;
    o.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        o += d[c >> 4];
        o += d[c & 15];
    }
    return o;
}

bool from_hex(const std::string& hex, std::string& bytes) {
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    bytes.clear();
    if (hex.size() % 2) return false;
    for (size_t i = 0; i < hex.size(); i += 2) {
        int a = v(hex[i]), b = v(hex[i + 1]);
        if (a < 0 || b < 0) {
            wipe(bytes);
            return false;
        }
        bytes += static_cast<char>(a * 16 + b);
    }
    return true;
}

void wipe(std::string& s) {
    if (!s.empty()) OPENSSL_cleanse(&s[0], s.size());
    s.clear();
}

}  // namespace crypto
}  // namespace pna
