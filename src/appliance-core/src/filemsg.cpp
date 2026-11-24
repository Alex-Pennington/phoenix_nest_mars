#include "filemsg.h"

#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <zlib.h>

namespace pna::filemsg {

namespace {
const char* kB32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string crc_hex(const std::string& d) {
    uLong c = crc32(0L, Z_NULL, 0);
    c = crc32(c, reinterpret_cast<const Bytef*>(d.data()), static_cast<uInt>(d.size()));
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08lX", static_cast<unsigned long>(c & 0xFFFFFFFFUL));
    return buf;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}
}  // namespace

std::string base32(const std::string& bytes) {
    std::string out;
    uint32_t buf = 0;
    int bits = 0;
    for (unsigned char c : bytes) {
        buf = (buf << 8) | c;
        bits += 8;
        while (bits >= 5) {
            out.push_back(kB32[(buf >> (bits - 5)) & 31]);
            bits -= 5;
        }
    }
    if (bits > 0) out.push_back(kB32[(buf << (5 - bits)) & 31]);
    return out;
}

bool unbase32(const std::string& text, std::string& bytes) {
    bytes.clear();
    uint32_t buf = 0;
    int bits = 0;
    for (char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= '2' && c <= '7') v = c - '2' + 26;
        else return false;
        buf = (buf << 5) | static_cast<uint32_t>(v);
        bits += 5;
        if (bits >= 8) {
            bytes.push_back(static_cast<char>((buf >> (bits - 8)) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}

std::string base64(const std::string& bytes) {
    std::string out;
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        uint32_t v = (uint8_t(bytes[i]) << 16) | (uint8_t(bytes[i + 1]) << 8) | uint8_t(bytes[i + 2]);
        for (int k = 3; k >= 0; --k) out.push_back(kB64[(v >> (6 * k)) & 63]);
    }
    if (i < bytes.size()) {
        uint32_t v = uint8_t(bytes[i]) << 16;
        if (i + 1 < bytes.size()) v |= uint8_t(bytes[i + 1]) << 8;
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? kB64[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

std::string base64_decode_or_empty(const std::string& b64, bool* ok) {
    std::string out;
    uint32_t buf = 0;
    int bits = 0;
    bool good = true;
    for (char c : b64) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        const char* p = std::strchr(kB64, c);
        if (!p || !*p) {
            good = false;
            break;
        }
        buf = (buf << 6) | static_cast<uint32_t>(p - kB64);
        bits += 6;
        if (bits >= 8) {
            out.push_back(static_cast<char>((buf >> (bits - 8)) & 0xFF));
            bits -= 8;
        }
    }
    if (ok) *ok = good;
    return good ? out : std::string();
}

std::string encode(const std::string& name, const std::string& data, std::string* error) {
    if (name.empty() || name.size() > kMaxName) {
        if (error) *error = "the file name must be 1 to " + std::to_string(kMaxName) + " bytes";
        return "";
    }
    for (char c : name)
        if (c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20) {
            if (error) *error = "the file name can't hold a slash or a control character";
            return "";
        }
    std::string out = "FILE SIZE " + std::to_string(data.size()) + " CRC " + crc_hex(data) + "\n";
    out += "NAME " + base32(name) + "\n";
    std::string b = base32(data);
    for (size_t i = 0; i < b.size(); i += kLine) out += b.substr(i, kLine) + "\n";
    out += "END FILE\n";
    return out;
}

bool find(const std::string& text, bool* present, File& out, std::string* error) {
    if (present) *present = false;
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        for (std::string l; std::getline(in, l);) lines.push_back(trim(l));
    }
    size_t i = 0;
    while (i < lines.size() && lines[i].compare(0, 10, "FILE SIZE ") != 0) ++i;
    if (i == lines.size()) return fail("no file in this message");
    if (present) *present = true;
    unsigned long size = 0;
    char crc[16] = {0};
    if (std::sscanf(lines[i].c_str(), "FILE SIZE %lu CRC %8s", &size, crc) != 2) return fail("the file's first line is damaged");
    if (i + 1 >= lines.size() || lines[i + 1].compare(0, 5, "NAME ") != 0) return fail("the file's name line is missing");
    std::string name;
    if (!unbase32(lines[i + 1].substr(5), name) || name.empty()) return fail("the file's name line is damaged");
    std::string b;
    size_t k = i + 2;
    for (; k < lines.size() && lines[k] != "END FILE"; ++k) b += lines[k];
    if (k == lines.size()) return fail("the file was cut short: no END FILE line");
    std::string data;
    if (!unbase32(b, data)) return fail("the file's data holds a character base32 doesn't use");
    if (data.size() != size) return fail("the file came " + std::to_string(data.size()) + " bytes, not " + std::to_string(size));
    if (crc_hex(data) != crc) return fail("the file's CRC doesn't match: it came damaged");
    out.name = name;
    out.data = data;
    return true;
}

}  // namespace pna::filemsg
