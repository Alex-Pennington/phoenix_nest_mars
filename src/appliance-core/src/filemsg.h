// A file carried as message text. ACP 127 carries text only, in a small
// character set (A-Z, 0-9, space and - ? : ( ) . , /) with lines of at most
// 69 characters, so a file goes as base32 (A-Z and 2-7), which fits inside
// any ACP 127 message text and passes through a CP station as plain text:
//
//   FILE SIZE 1234 CRC 1A2B3C4D
//   NAME <the file name, base32>
//   <data, base32, 64 characters a line>
//   END FILE
//
// CRC is the CRC-32 of the file's bytes. The block can sit anywhere in the
// text; the first one found is read.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pna::filemsg {

constexpr size_t kMaxName = 40;     // bytes; its base32 fits one 69-character line
constexpr size_t kLine = 64;        // base32 characters a line

std::string base32(const std::string& bytes);
bool unbase32(const std::string& text, std::string& bytes);
std::string base64_decode_or_empty(const std::string& b64, bool* ok);
std::string base64(const std::string& bytes);

// The block for a file; empty with *error set when the name is unusable.
std::string encode(const std::string& name, const std::string& data, std::string* error);

struct File {
    std::string name;
    std::string data;
};
// true when text holds a block; *error says why a block that is there
// could not be read (cut short, bad characters, wrong size or CRC).
bool find(const std::string& text, bool* present, File& out, std::string* error);

}  // namespace pna::filemsg
