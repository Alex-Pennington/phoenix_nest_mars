// The few cryptographic primitives the message engine uses, all from
// OpenSSL's libcrypto (nothing here is home-made):
//
//   AES-256-GCM    authenticated encryption for codress (codress.h)
//   HKDF-SHA256    one message key per key-tape key and purpose (RFC 5869)
//   SHA-256        the key tape's end check (keys.h)
//   MD5            only for the MSC-compatible [DIGEST:] line (digest.h), an
//                  error check with no key, never used for security
//   Ed25519        station signatures (RFC 8032): signed messages, cards, sealed tapes
//   X25519         sealing a key tape to one station (RFC 7748)
//   scrypt         the passphrase on a station's key backup (RFC 7914)
//   RAND_bytes     keys and nonces
#pragma once

#include <cstdint>
#include <string>

namespace pna {
namespace crypto {

// n bytes from OpenSSL's CSPRNG. Throws std::runtime_error if it fails.
std::string random_bytes(size_t n);

// HKDF-SHA256 (RFC 5869). Returns `len` bytes, or "" on failure.
std::string hkdf_sha256(const std::string& ikm, const std::string& salt, const std::string& info, size_t len);

// AES-256-GCM with a 12-byte nonce and a 16-byte tag. seal() returns
// ciphertext || tag, or "" on failure (bad key or nonce size). open() returns
// false when the tag does not match (tampered, garbled or wrong key).
std::string aes256gcm_seal(const std::string& key, const std::string& nonce, const std::string& aad,
                           const std::string& plain);
bool aes256gcm_open(const std::string& key, const std::string& nonce, const std::string& aad,
                    const std::string& sealed, std::string& plain);

std::string sha256_hex(const std::string& data);   // lower-case hex
std::string md5_hex_upper(const std::string& data);  // upper-case hex, as MSC writes it

// Ed25519: 32-byte private seed and public key, 64-byte signatures.
bool ed25519_keygen(std::string& priv, std::string& pub);
std::string ed25519_public(const std::string& priv);  // "" on failure
std::string ed25519_sign(const std::string& priv, const std::string& msg);  // "" on failure
bool ed25519_verify(const std::string& pub, const std::string& msg, const std::string& sig);

// X25519: 32-byte keys. x25519() is the shared secret, "" on failure
// (including the all-zero result from a bad public key).
bool x25519_keygen(std::string& priv, std::string& pub);
std::string x25519_public(const std::string& priv);
std::string x25519(const std::string& priv, const std::string& peer_pub);

// scrypt (N = 2^15, r = 8, p = 1), `len` bytes, "" on failure.
std::string scrypt(const std::string& pass, const std::string& salt, size_t len);

std::string to_hex(const std::string& bytes);       // lower-case
bool from_hex(const std::string& hex, std::string& bytes);

// Overwrites the string's bytes (OPENSSL_cleanse) and empties it.
void wipe(std::string& s);

}  // namespace crypto
}  // namespace pna
