// MIL-STD-188-110D Appendix A, the modem's LAN interface: the TCP data socket
// (A.5.1), DTE side. Written from the appendix's own text and listings.
//
// Packet (A.5.1.1, Figure A-1): preamble 49 50 55, type, 2-byte payload size,
// 2-byte header CRC over the six bytes before it, then the payload and a
// 2-byte payload CRC over the payload alone. An empty payload has no payload
// CRC. All fields are big-endian (A.4.4).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pna {
namespace appa {

constexpr uint16_t kPort = 3000;            // A.4.2, suggested
constexpr size_t kHeaderLen = 8;
constexpr size_t kMaxPayload = 4086;        // A.5.1.1
constexpr size_t kMaxData = 4072;           // A.5.1.1.5: 4086 less the 14-byte data header
constexpr uint8_t kVersion = 12;            // A.5.1.1.2

// Table A-I
enum Type : uint8_t { kData = 0x00, kConnect = 0x01, kConnectAck = 0x02, kError = 0xFF };

// Table A-II
enum Cmd : uint8_t {
    kDataTransfer = 0x00,
    kTransmitArm = 0x01,
    kTransmitStart = 0x02,
    kRequestTxStatus = 0x03,
    kTxDataNak = 0x04,
    kTxStatus = 0x05,
    kAbortReception = 0x06,
    kCarrierDetect = 0x08,
    kTransmitSetup = 0x09,
    kInitialSetup = 0x0A,
    kConnectionProbe = 0x0B,
};

// Table A-III
enum Order : uint8_t { kFirstOnly = 1, kFirstAndLast = 2, kContinuation = 3, kLast = 4 };

// Table A-V
enum TxState : uint8_t {
    kFlushed = 1,
    kArmedNotReady = 2,
    kArmedReady = 3,
    kStarted = 4,
    kDrainingOk = 5,
    kDrainingForced = 6,
};

using PacketId = std::array<uint8_t, 12>;  // A.5.1.1.5.2

// A.5.3: x^16+x^15+x^12+x^11+x^8+x^6+x^3+1, bits taken LSB first, register
// starting at 0, the register returned.
uint16_t crc16(const uint8_t* p, size_t n);
inline uint16_t crc16(const std::string& s) {
    return crc16(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::string packet(uint8_t type, const std::string& payload);
inline std::string connect() { return packet(kConnect, std::string(1, char(kVersion))); }
inline std::string connect_ack() { return packet(kConnectAck, std::string(1, char(kVersion))); }
inline std::string keep_alive() { return packet(kData, ""); }  // A.5.1.2.2
inline std::string command(uint8_t cmd) { return packet(kData, std::string(1, char(cmd))); }
std::string data_transfer(uint8_t order, const PacketId& id, const std::string& data);

// The orders for a message split into n data packets.
uint8_t order_for(size_t index, size_t count);

// One packet with good CRCs, as read.
struct Packet {
    uint8_t type = 0;
    std::string payload;
};

// Reads packets from the stream. A packet with a bad CRC is dropped without a
// word (A.5.1.2.3): the reader steps one byte on and looks for the next
// preamble.
class Reader {
public:
    std::vector<Packet> feed(const std::string& bytes);
    size_t dropped() const { return dropped_; }

private:
    std::string buf_;
    size_t dropped_ = 0;
};

// Fields of the modem's DATA commands this side reads.
struct TxStatus {
    uint8_t state = 0;
    uint32_t space = 0, fill = 0, critical_ms = 0, critical_bytes = 0;
};
struct RateAndBlock {
    uint32_t rate = 0, blocking = 0;  // bit/s and blocking factor in bits
};
bool read_tx_status(const std::string& payload, TxStatus& out);          // Figure A-5
bool read_carrier(const std::string& payload, bool& up, RateAndBlock& out);  // Figure A-6
bool read_transmit_setup(const std::string& payload, RateAndBlock& out);  // Figure A-7
// Figure A-3: order, packet ID, data.
bool read_data_transfer(const std::string& payload, uint8_t& order, std::string& data);
// Figure A-4: the cause (Table A-IV).
bool read_nak(const std::string& payload, uint8_t& cause);

std::string tx_state_text(uint8_t state);
std::string nak_text(uint8_t cause);

// Builders for the modem's side, used by the stand-in modem in the tests.
std::string tx_status(uint8_t state, uint32_t space, uint32_t fill);
std::string carrier_detect(bool up, uint32_t rate, uint32_t blocking);
std::string transmit_setup(uint32_t rate, uint32_t blocking);
std::string initial_setup(uint32_t rtt_ms, uint32_t min_ms, uint32_t max_ms);
std::string nak(uint8_t cause, const PacketId& id);

}  // namespace appa
}  // namespace pna
