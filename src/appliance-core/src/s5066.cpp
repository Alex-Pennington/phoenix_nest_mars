#include "s5066.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace pna::s5066 {

namespace {

void put16(std::string& s, uint32_t v) {
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>(v & 0xFF));
}

uint8_t u8(const std::string& s, size_t i) { return static_cast<uint8_t>(s[i]); }
uint16_t get16(const std::string& s, size_t i) { return static_cast<uint16_t>((u8(s, i) << 8) | u8(s, i + 1)); }

void put_address(std::string& s, uint32_t addr, bool group = false) {
    uint32_t v = (7u << 29) | (group ? (1u << 28) : 0u) | (addr & 0x0FFFFFFF);
    s.push_back(static_cast<char>(v >> 24));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>(v & 0xFF));
}

uint32_t get_address(const std::string& s, size_t i, bool* group = nullptr) {
    uint32_t v = (static_cast<uint32_t>(u8(s, i)) << 24) | (static_cast<uint32_t>(u8(s, i + 1)) << 16) |
                 (static_cast<uint32_t>(u8(s, i + 2)) << 8) | u8(s, i + 3);
    if (group) *group = (v >> 28) & 1;
    return v & 0x0FFFFFFF;
}

uint8_t service_byte(const ServiceType& st) {
    return static_cast<uint8_t>(((st.tx_mode & 0x0F) << 4) | ((st.confirm & 0x03) << 2) |
                                (st.as_they_arrive ? 0x02 : 0x00));
}

ServiceType read_service(uint8_t b1, uint8_t b2) {
    ServiceType st;
    st.tx_mode = b1 >> 4;
    st.confirm = (b1 >> 2) & 0x03;
    st.as_they_arrive = (b1 & 0x02) != 0;
    st.retries = b2 >> 4;
    return st;
}

}  // namespace

std::string wrap(const std::string& prim) {
    std::string out;
    out.reserve(prim.size() + 5);
    out.push_back(static_cast<char>(0x90));
    out.push_back(static_cast<char>(0xEB));
    out.push_back('\0');
    put16(out, static_cast<uint32_t>(prim.size()));
    out += prim;
    return out;
}

std::vector<std::string> Reader::feed(const std::string& bytes) {
    std::vector<std::string> out;
    buf_ += bytes;
    size_t pos = 0;
    while (true) {
        // Find the next sync.
        size_t start = pos;
        while (start + 1 < buf_.size() && !(u8(buf_, start) == 0x90 && u8(buf_, start + 1) == 0xEB)) ++start;
        if (start + 1 >= buf_.size()) {
            // Keep a trailing 0x90 that may start the next sync.
            size_t keep = (!buf_.empty() && u8(buf_, buf_.size() - 1) == 0x90) ? 1 : 0;
            skipped_ += buf_.size() - keep - pos;
            buf_.erase(0, buf_.size() - keep);
            return out;
        }
        skipped_ += start - pos;
        if (buf_.size() < start + 5) {
            buf_.erase(0, start);
            return out;
        }
        if (u8(buf_, start + 2) != 0x00) {
            // Not version 0: not a real sync. Skip past it.
            pos = start + 2;
            skipped_ += 2;
            continue;
        }
        size_t size = get16(buf_, start + 3);
        if (buf_.size() < start + 5 + size) {
            buf_.erase(0, start);
            return out;
        }
        if (size > 0) out.push_back(buf_.substr(start + 5, size));
        pos = start + 5 + size;
    }
}

bool parse(const std::string& p, Prim& o) {
    o = Prim{};
    if (p.empty()) return false;
    o.type = u8(p, 0);
    size_t n = p.size();
    switch (o.type) {
    case kBindRequest:
        if (n < 4) return false;
        o.sap = u8(p, 1) >> 4;
        o.rank = u8(p, 1) & 0x0F;
        o.service = read_service(u8(p, 2), u8(p, 3));
        o.tx_mode = o.service.tx_mode;
        return true;
    case kUnbindRequest:
    case kDataflowOn:
    case kDataflowOff:
    case kKeepAlive:
        return true;
    case kBindAccepted:
        if (n < 4) return false;
        o.sap = u8(p, 1) >> 4;
        o.mtu = get16(p, 2);
        return true;
    case kBindRejected:
    case kUnbindIndication:
        if (n < 2) return false;
        o.reason = u8(p, 1);
        return true;
    case kSubnetAvailability:
        if (n < 3) return false;
        o.status = u8(p, 1);
        o.reason = u8(p, 2);
        return true;
    case kUnidataRequest: {
        // type, prio|sap, dst(4), service(1), retries|ttl-hi(1), ttl(2), size(2), data
        if (n < 12) return false;
        o.priority = u8(p, 1) >> 4;
        o.sap = u8(p, 1) & 0x0F;
        o.dst = get_address(p, 2, &o.dst_group);
        o.service = read_service(u8(p, 6), u8(p, 7));
        o.tx_mode = o.service.tx_mode;
        uint32_t ttl = (static_cast<uint32_t>(u8(p, 7) & 0x0F) << 16) | get16(p, 8);
        o.ttl_seconds = ttl * 2;
        size_t size = get16(p, 10);
        if (n < 12 + size) return false;
        o.data = p.substr(12, size);
        return true;
    }
    case kUnidataIndication: {
        // type, prio|dst_sap, dst(4), txmode|src_sap, src(4), size(2), data
        if (n < 13) return false;
        o.priority = u8(p, 1) >> 4;
        o.sap = u8(p, 1) & 0x0F;
        o.dst = get_address(p, 2, &o.dst_group);
        o.tx_mode = u8(p, 6) >> 4;
        o.src_sap = u8(p, 6) & 0x0F;
        o.src = get_address(p, 7);
        size_t size = get16(p, 11);
        size_t at = 13;
        // Some nodes put two block counts before the data (Wireshark notes
        // Rockwell Collins v2.1 and earlier). Skip them when the length says so.
        if (n == at + size + 4) at += 4;
        if (o.tx_mode == 3) return false;  // non-ARQ with errors: we never ask for it
        if (n < at + size) return false;
        o.data = p.substr(at, size);
        return true;
    }
    case kUnidataRequestConfirm:
    case kUnidataRequestRejected: {
        // type, (reason|)sap, dst(4), size(2), data part
        if (n < 8) return false;
        if (o.type == kUnidataRequestRejected) o.reason = u8(p, 1) >> 4;
        o.sap = u8(p, 1) & 0x0F;
        o.dst = get_address(p, 2, &o.dst_group);
        size_t size = get16(p, 6);
        o.data = p.substr(8, std::min(size, n - 8));
        return true;
    }
    default:
        return false;
    }
}

std::string bind_request(uint8_t sap, uint8_t rank, const ServiceType& st) {
    std::string p;
    p.push_back(static_cast<char>(kBindRequest));
    p.push_back(static_cast<char>(((sap & 0x0F) << 4) | (rank & 0x0F)));
    p.push_back(static_cast<char>(service_byte(st)));
    p.push_back(static_cast<char>((st.retries & 0x0F) << 4));
    return wrap(p);
}

std::string unbind_request() { return wrap(std::string(1, static_cast<char>(kUnbindRequest))); }
std::string keep_alive() { return wrap(std::string(1, static_cast<char>(kKeepAlive))); }

std::string unidata_request(uint8_t priority, uint8_t dst_sap, uint32_t dst, const ServiceType& st,
                            uint32_t ttl_seconds, const std::string& data) {
    std::string p;
    p.push_back(static_cast<char>(kUnidataRequest));
    p.push_back(static_cast<char>(((priority & 0x0F) << 4) | (dst_sap & 0x0F)));
    put_address(p, dst, dst == kBroadcastAddress);
    p.push_back(static_cast<char>(service_byte(st)));
    uint32_t ttl = (ttl_seconds + 1) / 2;
    if (ttl > 0xFFFFF) ttl = 0xFFFFF;
    p.push_back(static_cast<char>(((st.retries & 0x0F) << 4) | ((ttl >> 16) & 0x0F)));
    put16(p, ttl & 0xFFFF);
    put16(p, static_cast<uint32_t>(data.size()));
    p += data;
    return wrap(p);
}

std::string bind_accepted(uint8_t sap, uint16_t mtu) {
    std::string p;
    p.push_back(static_cast<char>(kBindAccepted));
    p.push_back(static_cast<char>((sap & 0x0F) << 4));
    put16(p, mtu);
    return wrap(p);
}

std::string bind_rejected(uint8_t reason) {
    std::string p;
    p.push_back(static_cast<char>(kBindRejected));
    p.push_back(static_cast<char>(reason));
    return wrap(p);
}

std::string unbind_indication(uint8_t reason) {
    std::string p;
    p.push_back(static_cast<char>(kUnbindIndication));
    p.push_back(static_cast<char>(reason));
    return wrap(p);
}

std::string dataflow(bool on) { return wrap(std::string(1, static_cast<char>(on ? kDataflowOn : kDataflowOff))); }

std::string subnet_availability(uint8_t status, uint8_t reason) {
    std::string p;
    p.push_back(static_cast<char>(kSubnetAvailability));
    p.push_back(static_cast<char>(status));
    p.push_back(static_cast<char>(reason));
    return wrap(p);
}

std::string unidata_indication(uint8_t priority, uint8_t dst_sap, uint32_t dst, uint8_t tx_mode, uint8_t src_sap,
                               uint32_t src, const std::string& data) {
    std::string p;
    p.push_back(static_cast<char>(kUnidataIndication));
    p.push_back(static_cast<char>(((priority & 0x0F) << 4) | (dst_sap & 0x0F)));
    put_address(p, dst, dst == kBroadcastAddress);
    p.push_back(static_cast<char>(((tx_mode & 0x0F) << 4) | (src_sap & 0x0F)));
    put_address(p, src);
    put16(p, static_cast<uint32_t>(data.size()));
    p += data;
    return wrap(p);
}

std::string unidata_confirm(uint8_t dst_sap, uint32_t dst, const std::string& data_part) {
    std::string p;
    p.push_back(static_cast<char>(kUnidataRequestConfirm));
    p.push_back(static_cast<char>(dst_sap & 0x0F));
    put_address(p, dst);
    put16(p, static_cast<uint32_t>(data_part.size()));
    p += data_part;
    return wrap(p);
}

std::string unidata_rejected(uint8_t reason, uint8_t dst_sap, uint32_t dst, const std::string& data_part) {
    std::string p;
    p.push_back(static_cast<char>(kUnidataRequestRejected));
    p.push_back(static_cast<char>(((reason & 0x0F) << 4) | (dst_sap & 0x0F)));
    put_address(p, dst);
    put16(p, static_cast<uint32_t>(data_part.size()));
    p += data_part;
    return wrap(p);
}

std::string bind_rejected_text(uint8_t r) {
    switch (r) {
    case 1: return "not enough resources";
    case 2: return "invalid SAP";
    case 3: return "SAP already taken";
    case 4: return "ARQ not available during a broadcast session";
    default: return "reason unknown";
    }
}

std::string unbind_text(uint8_t r) {
    switch (r) {
    case 1: return "pre-empted by a higher-ranking client";
    case 2: return "no answer to keep-alive";
    case 3: return "too many invalid primitives";
    case 4: return "too many expedited requests";
    case 5: return "ARQ not available during a broadcast session";
    default: return "reason unknown";
    }
}

std::string unidata_rejected_text(uint8_t r) {
    switch (r) {
    case 1: return "time to live ran out";
    case 2: return "the far station has no client on our SAP";
    case 3: return "the far station's node isn't answering";
    case 4: return "larger than the node's MTU";
    case 5: return "transmission mode not given";
    default: return "reason unknown";
    }
}

std::string address_text(uint32_t a) {
    return std::to_string((a >> 24) & 0x0F) + "." + std::to_string((a >> 16) & 0xFF) + "." +
           std::to_string((a >> 8) & 0xFF) + "." + std::to_string(a & 0xFF);
}

bool parse_address(const std::string& text, uint32_t& out) {
    uint32_t parts[4];
    int n = 0;
    size_t i = 0;
    while (n < 4) {
        if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return false;
        uint32_t v = 0;
        size_t digits = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            v = v * 10 + static_cast<uint32_t>(text[i] - '0');
            if (++digits > 3) return false;
            ++i;
        }
        if (v > 255) return false;
        parts[n++] = v;
        if (n < 4) {
            if (i >= text.size() || text[i] != '.') return false;
            ++i;
        }
    }
    if (i != text.size() || parts[0] > 15) return false;
    out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return true;
}

std::string format_address(uint32_t a) {
    return std::to_string((a >> 24) & 0x0F) + "." + std::to_string((a >> 16) & 0xFF) + "." +
           std::to_string((a >> 8) & 0xFF) + "." + std::to_string(a & 0xFF);
}

bool read_peers(const std::string& path, std::map<std::string, uint32_t>& out, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "can't read " + path;
        return false;
    }
    std::map<std::string, uint32_t> m;
    std::string line;
    int ln = 0;
    while (std::getline(f, line)) {
        ++ln;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream in(line);
        std::string call, addr, extra;
        if (!(in >> call)) continue;
        if (!(in >> addr) || (in >> extra)) {
            if (error) *error = path + " line " + std::to_string(ln) + ": want CALL ADDRESS";
            return false;
        }
        for (auto& c : call) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        uint32_t a = 0;
        if (!parse_address(addr, a) || a == kBroadcastAddress) {
            if (error) *error = path + " line " + std::to_string(ln) + ": " + addr + " isn't a 5066 address like 0.0.1.2";
            return false;
        }
        m[call] = a;
    }
    out.swap(m);
    return true;
}

std::vector<std::string> split(const std::string& data, uint16_t seq, size_t mtu) {
    std::vector<std::string> out;
    if (mtu <= kPieceHeader) return out;
    size_t room = mtu - kPieceHeader;
    size_t count = data.empty() ? 1 : (data.size() + room - 1) / room;
    if (count > kMaxPieces) return out;
    for (size_t i = 0; i < count; ++i) {
        std::string p;
        p.push_back('P');
        p.push_back('S');
        p.push_back('\x01');
        put16(p, seq);
        p.push_back(static_cast<char>(i));
        p.push_back(static_cast<char>(count));
        p += data.substr(i * room, room);
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<std::string> Joiner::add(uint32_t src, const std::string& u) {
    std::vector<std::string> out;
    if (u.size() < kPieceHeader || u[0] != 'P' || u[1] != 'S' || u[2] != '\x01') {
        out.push_back(u);
        return out;
    }
    uint16_t seq = get16(u, 3);
    uint8_t index = u8(u, 5), count = u8(u, 6);
    std::string body = u.substr(kPieceHeader);
    if (count == 0 || index >= count) {
        ++dropped_;
        return out;
    }
    if (count == 1) {
        parts_.erase(src);
        out.push_back(body);
        return out;
    }
    auto it = parts_.find(src);
    if (index == 0) {
        if (it != parts_.end() && !it->second.got.empty()) ++dropped_;  // an unfinished frame is lost
        Part p;
        p.seq = seq;
        p.count = count;
        p.got.push_back(body);
        parts_[src] = std::move(p);
        return out;
    }
    if (it == parts_.end() || it->second.seq != seq || it->second.count != count ||
        it->second.got.size() != index) {
        // Out of order: with ARQ and in-order delivery this shouldn't happen.
        if (it != parts_.end()) parts_.erase(it);
        ++dropped_;
        return out;
    }
    it->second.got.push_back(body);
    if (it->second.got.size() == count) {
        std::string whole;
        for (auto& g : it->second.got) whole += g;
        parts_.erase(it);
        out.push_back(std::move(whole));
    }
    return out;
}

uint8_t priority_for(char prec) {
    switch (prec) {
    case 'Z': return 15;
    case 'O': return 10;
    case 'P': return 5;
    default: return 0;
    }
}

}  // namespace pna::s5066
