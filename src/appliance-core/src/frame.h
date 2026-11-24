// Appliance link frame: how one core hands a message or an ack to another
// core over the modem data port.
//
// This is the appliance's own test framing for milestone 1. It is NOT the
// MARS transmission header (classchecksum.h) and does not try to match it.
// It will be replaced once the message engine (milestone 3) and the
// authorized header spec (milestone 8) land.
//
// Wire form (all ASCII except the body):
//   SOH "PNA1" SP type SP id SP src SP dst SP prec SP len SP hck SP crc LF body EOT
//   type  MSG or ACK
//   prec  Z O P R  (FLASH, IMMEDIATE, PRIORITY, ROUTINE)
//   len   body length in bytes, decimal
//   hck   low 16 bits of CRC-32 over the header text before " hck", 4 hex
//         digits, so a damaged length is caught before waiting on the body
//   crc   CRC-32 over the header text before " hck" plus the body, 8 hex digits
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pna {

enum class FrameType { Msg, Ack };

struct Frame {
    FrameType type = FrameType::Msg;
    std::string id;    // "<station>-<seq>"
    std::string src;   // sending station
    std::string dst;   // receiving station
    char prec = 'R';   // Z O P R
    std::string body;  // message text (empty for ACK)
};

constexpr size_t kMaxBody = 8192;
constexpr size_t kMaxHeader = 160;

uint32_t crc32(const void* data, size_t len, uint32_t seed = 0);

// True for 1-16 chars of A-Z, 0-9 and '-'.
bool valid_station(const std::string& s);
// True for Z, O, P, R.
bool valid_prec(char p);
// The broadcast address: every station that hears a frame sent to it takes
// it, and none acks it, so the sender marks it sent once it is on the air.
constexpr const char* kBroadcast = "ALL";
inline bool is_broadcast(const std::string& dst) { return dst == kBroadcast; }
// Lower number goes first: Z=0, O=1, P=2, R=3.
int prec_rank(char p);
const char* prec_name(char p);

std::string encode(const Frame& f);

// Feeds received bytes in any chunking and returns each complete frame whose
// CRC checks. Two other kinds of bytes are kept for the core to collect:
//  - a frame whose header checks but whose body doesn't (garbled()): its
//    body with every byte outside printable ASCII, CR, LF and tab shown as
//    '.', so the operator sees what came and can ask for a repeat;
//  - bytes outside any frame (loose()): bare traffic from a station that
//    doesn't use PNA1 frames, such as an MSC CP station's ACP 127 text or
//    chat. The core cuts it into messages (core.h).
class FrameParser {
public:
    std::vector<Frame> feed(const std::string& bytes);
    size_t dropped() const { return dropped_; }
    std::vector<Frame> take_garbled();
    std::string take_loose();

private:
    std::string buf_;
    size_t dropped_ = 0;
    std::vector<Frame> garbled_;
    std::string loose_;
};

// Every byte outside printable ASCII, CR, LF and tab as '.' (as CP shows a
// damaged copy).
std::string printable(const std::string& bytes);

}  // namespace pna
