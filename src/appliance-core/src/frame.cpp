#include "frame.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace pna {

namespace {
constexpr char SOH = 0x01;
constexpr char EOT = 0x04;
constexpr const char* kMagic = "PNA1";

uint32_t crc_table[256];
bool crc_ready = false;

void init_crc() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
    crc_ready = true;
}

std::string hex(uint32_t v, int digits) {
    char b[16];
    std::snprintf(b, sizeof b, "%0*X", digits, v);
    return b;
}

bool parse_hex(const std::string& s, size_t digits, uint32_t& out) {
    if (s.size() != digits) return false;
    uint32_t v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return false;
    }
    out = v;
    return true;
}

bool parse_len(const std::string& s, size_t& out) {
    if (s.empty() || s.size() > 5) return false;
    size_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > 28) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}
}  // namespace

uint32_t crc32(const void* data, size_t len, uint32_t seed) {
    if (!crc_ready) init_crc();
    uint32_t c = seed ^ 0xFFFFFFFFu;
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool valid_station(const std::string& s) {
    if (s.empty() || s.size() > 16) return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

bool valid_prec(char p) { return p == 'Z' || p == 'O' || p == 'P' || p == 'R'; }

int prec_rank(char p) {
    switch (p) {
        case 'Z': return 0;
        case 'O': return 1;
        case 'P': return 2;
        default: return 3;
    }
}

const char* prec_name(char p) {
    switch (p) {
        case 'Z': return "FLASH";
        case 'O': return "IMMEDIATE";
        case 'P': return "PRIORITY";
        default: return "ROUTINE";
    }
}

std::string encode(const Frame& f) {
    std::string head = std::string(kMagic) + " " + (f.type == FrameType::Ack ? "ACK" : "MSG") + " " +
                       f.id + " " + f.src + " " + f.dst + " " + std::string(1, f.prec) + " " +
                       std::to_string(f.body.size());
    uint32_t hck = crc32(head.data(), head.size()) & 0xFFFF;
    uint32_t crc = crc32(f.body.data(), f.body.size(), crc32(head.data(), head.size()));
    std::string out;
    out.reserve(head.size() + f.body.size() + 20);
    out += SOH;
    out += head;
    out += " " + hex(hck, 4) + " " + hex(crc, 8) + "\n";
    out += f.body;
    out += EOT;
    return out;
}

std::vector<Frame> FrameParser::feed(const std::string& bytes) {
    std::vector<Frame> out;
    buf_ += bytes;
    for (;;) {
        size_t soh = buf_.find(SOH);
        if (soh == std::string::npos) {
            buf_.clear();
            break;
        }
        if (soh) buf_.erase(0, soh);

        size_t nl = buf_.find('\n');
        if (nl == std::string::npos || nl > kMaxHeader) {
            if (nl == std::string::npos && buf_.size() <= kMaxHeader) break;  // wait for more
            buf_.erase(0, 1);
            ++dropped_;
            continue;
        }

        std::istringstream hs(buf_.substr(1, nl - 1));
        std::string magic, type, id, src, dst, prec, len_s, hck_s, crc_s, extra;
        hs >> magic >> type >> id >> src >> dst >> prec >> len_s >> hck_s >> crc_s;
        bool ok = !hs.fail() && !(hs >> extra);
        size_t len = 0;
        uint32_t hck = 0, crc = 0;
        ok = ok && magic == kMagic && (type == "MSG" || type == "ACK") && valid_id(id) &&
             valid_station(src) && valid_station(dst) && prec.size() == 1 && valid_prec(prec[0]) &&
             parse_len(len_s, len) && len <= kMaxBody && parse_hex(hck_s, 4, hck) &&
             parse_hex(crc_s, 8, crc);
        std::string head;
        if (ok) {
            head = magic + " " + type + " " + id + " " + src + " " + dst + " " + prec + " " + len_s;
            ok = (crc32(head.data(), head.size()) & 0xFFFF) == hck;
        }
        if (!ok) {
            buf_.erase(0, 1);
            ++dropped_;
            continue;
        }

        size_t total = nl + 1 + len + 1;
        if (buf_.size() < total) break;  // header is good, body still arriving

        std::string body = buf_.substr(nl + 1, len);
        bool good = buf_[nl + 1 + len] == EOT &&
                    crc32(body.data(), body.size(), crc32(head.data(), head.size())) == crc;
        if (!good) {
            buf_.erase(0, 1);
            ++dropped_;
            continue;
        }

        Frame f;
        f.type = type == "ACK" ? FrameType::Ack : FrameType::Msg;
        f.id = id;
        f.src = src;
        f.dst = dst;
        f.prec = prec[0];
        f.body = body;
        out.push_back(std::move(f));
        buf_.erase(0, total);
    }
    return out;
}

}  // namespace pna
