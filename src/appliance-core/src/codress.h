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
// groups carry comes from the cipher slot (cipher_slot.h): the module in
// the slot enciphers the text after format line 5, and this file lays it
// out. A module that gives bytes gets them written 5 bytes to a group in
// base 36 (A-Z then 0-9), which survives any HF text mode that carries
// letters and figures, after a pad (0x80 then zeros) to whole groups. A
// module that gives its own on-air text has its lines written as they are.
// The default module is our own pna-aes (modules/pna_aes.cpp), not the MARS
// cipher, which this project does not have and does not reconstruct.
// docs/crypto.md and docs/cipher-slot.md have the whole design.
#pragma once

#include <string>
#include <vector>

namespace pna {
namespace cipher {
class Slot;
}
namespace codress {

constexpr int kGroupChars = 8;
constexpr int kGroupsPerLine = 7;

// The key indicator of a codress message ("QXR01"), or "" if the text is not
// one (no indicator after the DTG, or no GR line).
std::string indicator_of(const std::string& text);
inline bool is_codress(const std::string& text) { return !indicator_of(text).empty(); }

// Enciphers a built plaindress message (start to NNNN, as acp127::build and
// digest::append make it) with the module in `slot`, under `key` (the tape
// key) and its indicator. The result is the codress message with its own
// [DIGEST:] line.
bool encipher(const cipher::Slot& slot, const std::string& plaindress, const std::string& indicator,
              const std::string& key, std::string& out, std::string* error);
// The same with our built-in cipher.
bool encipher(const std::string& plaindress, const std::string& indicator, const std::string& key,
              std::string& out, std::string* error);

// Turns a codress message back into the plaindress it was made from, with
// the tape key for its indicator. False with *error when the groups are
// damaged or missing, or the module refuses them (tampered or wrong key).
bool decipher(const cipher::Slot& slot, const std::string& codress, const std::string& key, std::string& plaindress,
              std::string* error);
bool decipher(const std::string& codress, const std::string& key, std::string& plaindress, std::string* error);

// The group form on its own, for other blocks that travel as text (sealed
// key tapes, station.h): bytes (a multiple of 5) to 8-character groups, and
// back. from_groups is false with *error on a group that isn't one.
std::vector<std::string> to_groups(const std::string& bytes);
bool from_groups(const std::vector<std::string>& groups, std::string& bytes, std::string* error);

}  // namespace codress
}  // namespace pna
