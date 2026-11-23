// ACP 127 codress: a plaindress message enciphered whole after its date-time
// group, so the addresses travel inside the cipher (ACP 127(G) 216, 302).
//
// On the air a codress message keeps format lines 1 to 4 in the clear, then:
//
//   P 231040Z NOV 2026 QXR01        format line 5: DTG and the key indicator
//   GR 21                           group count
//   BT
//   0KX4Q7ZD 8M1C0QPA ...           cipher groups, 8 characters, 7 a line
//   BT
//   [DIGEST:...]                    MSC-style error check over the groups
//   NNNN
//
// as MARS codress traffic is laid out (batch 2 samples 02 and 03). What the
// groups carry is ours, not the MARS cipher (which this project does not
// have and does not reconstruct): AES-256-GCM from OpenSSL, under a message
// key made with HKDF-SHA256 from the key tape's key, with a fresh random
// 12-byte nonce for every message. The groups carry nonce || ciphertext ||
// tag, 5 bytes to a group written in base 36 (A-Z then 0-9), which survives
// any HF text mode that carries letters and figures. The tag covers the
// key indicator and the date-time group, so a changed DTG, a changed group
// or the wrong key is refused, never shown as garbage. docs/crypto.md has
// the whole design.
#pragma once

#include <string>

namespace pna {
namespace codress {

constexpr int kGroupChars = 8;
constexpr int kGroupsPerLine = 7;

// The key indicator of a codress message ("QXR01"), or "" if the text is not
// one (no indicator after the DTG, or no GR line).
std::string indicator_of(const std::string& text);
inline bool is_codress(const std::string& text) { return !indicator_of(text).empty(); }

// Enciphers a built plaindress message (start to NNNN, as acp127::build and
// digest::append make it) under `key` (the tape key, 32 bytes) and its
// indicator. The result is the codress message with its own [DIGEST:] line.
bool encipher(const std::string& plaindress, const std::string& indicator, const std::string& key,
              std::string& out, std::string* error);

// Turns a codress message back into the plaindress it was made from, with
// the tape key for its indicator. False with *error when the groups are
// damaged or missing, or the tag does not check (tampered or wrong key).
bool decipher(const std::string& codress, const std::string& key, std::string& plaindress, std::string* error);

}  // namespace codress
}  // namespace pna
