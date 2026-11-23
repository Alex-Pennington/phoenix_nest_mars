#include "appa.h"

#include <cstring>

namespace pna {
namespace appa {

namespace {

const char kPreamble[3] = {0x49, 0x50, 0x55};

void put16(std::string& b, uint32_t v) {
    b += char((v >> 8) & 0xFF);
    b += char(v & 0xFF);
}
void put32(std::string& b, uint32_t v) {
    put16(b, v >> 16);
    put16(b, v & 0xFFFF);
}
uint32_t get16(const std::string& s, size_t i) {
    return uint32_t(uint8_t(s[i])) << 8 | uint8_t(s[i + 1]);
}
uint32_t get32(const std::string& s, size_t i) { return get16(s, i) << 16 | get16(s, i + 2); }

}  // namespace

uint16_t crc16(const uint8_t* p, size_t n) {
    uint16_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        for (unsigned mask = 0x01; mask <= 0x80; mask <<= 1) {
            unsigned bit = (crc & 1u) ^ ((p[i] & mask) ? 1u : 0u);
            crc >>= 1;
            if (bit) crc ^= 0x9299;
        }
    }
    return crc;
}

std::string packet(uint8_t type, const std::string& payload) {
    std::string b(kPreamble, 3);
    b += char(type);
    put16(b, uint32_t(payload.size()));
    put16(b, crc16(b));
    if (!payload.empty()) {
        b += payload;
        put16(b, crc16(payload));
    }
    return b;
}

std::string data_transfer(uint8_t order, const PacketId& id, const std::string& data) {
    std::string p;
    p += char(kDataTransfer);
    p += char(order);
    p.append(reinterpret_cast<const char*>(id.data()), id.size());
    p += data;
    return packet(kData, p);
}

uint8_t order_for(size_t index, size_t count) {
    if (count <= 1) return kFirstAndLast;
    if (index == 0) return kFirstOnly;
    if (index + 1 == count) return kLast;
    return kContinuation;
}

std::vector<Packet> Reader::feed(const std::string& bytes) {
    buf_ += bytes;
    std::vector<Packet> out;
    for (;;) {
        size_t at = buf_.find(std::string(kPreamble, 3));
        if (at == std::string::npos) {
            // Keep a tail that may be the start of a preamble.
            if (buf_.size() > 2) buf_.erase(0, buf_.size() - 2);
            break;
        }
        buf_.erase(0, at);
        if (buf_.size() < kHeaderLen) break;
        if (crc16(reinterpret_cast<const uint8_t*>(buf_.data()), 6) != get16(buf_, 6)) {
            dropped_++;
            buf_.erase(0, 1);
            continue;
        }
        size_t len = get16(buf_, 4);
        if (len > kMaxPayload) {
            dropped_++;
            buf_.erase(0, 1);
            continue;
        }
        size_t total = kHeaderLen + (len ? len + 2 : 0);
        if (buf_.size() < total) break;
        Packet pk;
        pk.type = uint8_t(buf_[3]);
        if (len) {
            pk.payload = buf_.substr(kHeaderLen, len);
            if (crc16(pk.payload) != get16(buf_, kHeaderLen + len)) {
                dropped_++;
                buf_.erase(0, 1);
                continue;
            }
        }
        buf_.erase(0, total);
        out.push_back(std::move(pk));
    }
    return out;
}

bool read_tx_status(const std::string& p, TxStatus& out) {
    if (p.size() != 18 || uint8_t(p[0]) != kTxStatus) return false;
    out.state = uint8_t(p[1]);
    out.space = get32(p, 2);
    out.fill = get32(p, 6);
    out.critical_ms = get32(p, 10);
    out.critical_bytes = get32(p, 14);
    return true;
}

bool read_carrier(const std::string& p, bool& up, RateAndBlock& out) {
    if (p.size() != 10 || uint8_t(p[0]) != kCarrierDetect) return false;
    up = p[1] != 0;
    out.rate = get32(p, 2);
    out.blocking = get32(p, 6);
    return true;
}

bool read_transmit_setup(const std::string& p, RateAndBlock& out) {
    if (p.size() != 9 || uint8_t(p[0]) != kTransmitSetup) return false;
    out.rate = get32(p, 1);
    out.blocking = get32(p, 5);
    return true;
}

bool read_data_transfer(const std::string& p, uint8_t& order, std::string& data) {
    if (p.size() < 14 || uint8_t(p[0]) != kDataTransfer) return false;
    order = uint8_t(p[1]);
    data = p.substr(14);
    return true;
}

bool read_nak(const std::string& p, uint8_t& cause) {
    if (p.size() != 14 || uint8_t(p[0]) != kTxDataNak) return false;
    cause = uint8_t(p[1]);
    return true;
}

std::string tx_state_text(uint8_t s) {
    switch (s) {
    case kFlushed: return "flushed";
    case kArmedNotReady: return "armed, not ready";
    case kArmedReady: return "armed and ready";
    case kStarted: return "started";
    case kDrainingOk: return "draining";
    case kDrainingForced: return "draining, forced";
    default: return "state " + std::to_string(s);
    }
}

std::string nak_text(uint8_t c) {
    switch (c) {
    case 0: return "transmit queues not armed";
    case 1: return "transmit underrun";
    case 2: return "missing first packet";
    case 3: return "more than one first packet";
    default: return "cause " + std::to_string(c);
    }
}

std::string tx_status(uint8_t state, uint32_t space, uint32_t fill) {
    std::string p;
    p += char(kTxStatus);
    p += char(state);
    put32(p, space);
    put32(p, fill);
    put32(p, 0);
    put32(p, 0);
    return packet(kData, p);
}

std::string carrier_detect(bool up, uint32_t rate, uint32_t blocking) {
    std::string p;
    p += char(kCarrierDetect);
    p += char(up ? 1 : 0);
    put32(p, rate);
    put32(p, blocking);
    return packet(kData, p);
}

std::string transmit_setup(uint32_t rate, uint32_t blocking) {
    std::string p;
    p += char(kTransmitSetup);
    put32(p, rate);
    put32(p, blocking);
    return packet(kData, p);
}

std::string initial_setup(uint32_t rtt_ms, uint32_t min_ms, uint32_t max_ms) {
    std::string p;
    p += char(kInitialSetup);
    put32(p, rtt_ms);
    put32(p, min_ms);
    put32(p, max_ms);
    p += char(0);  // asynchronous
    p += char(3);  // 8 data bits
    p += char(0);  // 1 stop bit
    p += char(0);  // no parity
    p += char(1);  // data only
    return packet(kData, p);
}

std::string nak(uint8_t cause, const PacketId& id) {
    std::string p;
    p += char(kTxDataNak);
    p += char(cause);
    p.append(reinterpret_cast<const char*>(id.data()), id.size());
    return packet(kData, p);
}

}  // namespace appa
}  // namespace pna
