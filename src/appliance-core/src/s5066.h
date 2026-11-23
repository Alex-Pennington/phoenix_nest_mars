// STANAG 5066 Subnetwork Interface Sublayer (SIS): the client side of the
// primitives the engine uses, the node side the stand-in node needs in tests,
// and the engine's own piece header for frames larger than the node's MTU.
//
// Wire form, as greybeard-ale's interface 7 fixes it (docs/integration-plan.md
// there) and as Wireshark's s5066sis dissector reads it:
//   0x90 0xEB  version 0x00  size (2 bytes, big-endian, bytes after this field)
//   then one S_ primitive: its type byte, then its fields.
// Primitive type numbers and field layouts are taken from that dissector
// (epan/dissectors/packet-s5066sis.c), which greybeard-ale names as the
// reference for the codes. Every multi-byte field is big-endian.
//
// A 5066 address is 4 bytes on the SIS: the top 3 bits are the address size
// in half-bytes (we always send 7, a full 28-bit address), then a group bit,
// then 28 bits of address. It is written like an IPv4 address with the first
// part 0-15, e.g. 0.0.1.2.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pna::s5066 {

constexpr uint16_t kPort = 5066;

enum Type : uint8_t {
    kBindRequest = 1,
    kUnbindRequest = 2,
    kBindAccepted = 3,
    kBindRejected = 4,
    kUnbindIndication = 5,
    kSubnetAvailability = 14,
    kDataflowOn = 15,
    kDataflowOff = 16,
    kKeepAlive = 17,
    kUnidataRequest = 20,
    kUnidataIndication = 21,
    kUnidataRequestConfirm = 22,
    kUnidataRequestRejected = 23,
};

// Transmission modes (service type, top nibble).
constexpr uint8_t kArq = 1;
constexpr uint8_t kNonArqBroadcast = 2;
// Delivery confirmation (service type, bits 3-2).
constexpr uint8_t kNoConfirm = 0;
constexpr uint8_t kNodeConfirm = 1;
constexpr uint8_t kClientConfirm = 2;

// The all-stations address: every node takes it.
constexpr uint32_t kBroadcastAddress = 0x0FFFFFFF;

struct ServiceType {
    uint8_t tx_mode = kArq;
    uint8_t confirm = kNodeConfirm;
    bool as_they_arrive = false;  // false: in-order delivery
    uint8_t retries = 0;          // non-ARQ only: minimum retransmissions, 0-15
};

// One decoded primitive. Only the fields its type carries are set.
struct Prim {
    uint8_t type = 0;
    uint8_t sap = 0;        // bind: our SAP; unidata: destination SAP
    uint8_t src_sap = 0;    // indication only
    uint8_t rank = 0;       // bind request
    uint8_t priority = 0;   // unidata request and indication, 0-15
    uint8_t reason = 0;     // rejected, unbind indication, subnet availability
    uint8_t status = 0;     // subnet availability
    uint8_t tx_mode = 0;    // indication, or the service type of a request
    ServiceType service;    // bind and unidata request
    uint32_t dst = 0;       // unidata: destination address
    uint32_t src = 0;       // indication: source address
    bool dst_group = false;
    uint16_t mtu = 0;       // bind accepted
    uint32_t ttl_seconds = 0;  // unidata request; 0 = no limit
    std::string data;       // the U_PDU, or the part of it a confirm or reject returns
};

// Wraps one primitive (type byte first) in the SIS preamble and size.
std::string wrap(const std::string& prim);

// Splits a TCP byte stream into primitives (each returned from its type byte
// on). Bytes before a sync are skipped and counted.
class Reader {
public:
    std::vector<std::string> feed(const std::string& bytes);
    size_t skipped() const { return skipped_; }

private:
    std::string buf_;
    size_t skipped_ = 0;
};

// Decodes one primitive as Reader returns it. False if it is short or of a
// type we don't read.
bool parse(const std::string& prim, Prim& out);

// Client to node.
std::string bind_request(uint8_t sap, uint8_t rank, const ServiceType& st);
std::string unbind_request();
std::string keep_alive();
std::string unidata_request(uint8_t priority, uint8_t dst_sap, uint32_t dst, const ServiceType& st,
                            uint32_t ttl_seconds, const std::string& data);

// Node to client (the stand-in node in tests uses these).
std::string bind_accepted(uint8_t sap, uint16_t mtu);
std::string bind_rejected(uint8_t reason);
std::string unbind_indication(uint8_t reason);
std::string dataflow(bool on);
std::string subnet_availability(uint8_t status, uint8_t reason);
std::string unidata_indication(uint8_t priority, uint8_t dst_sap, uint32_t dst, uint8_t tx_mode, uint8_t src_sap,
                               uint32_t src, const std::string& data);
std::string unidata_confirm(uint8_t dst_sap, uint32_t dst, const std::string& data_part);
std::string unidata_rejected(uint8_t reason, uint8_t dst_sap, uint32_t dst, const std::string& data_part);

// Text for the reason codes, for the log.
std::string bind_rejected_text(uint8_t reason);
std::string unbind_text(uint8_t reason);
std::string unidata_rejected_text(uint8_t reason);

// "0.0.1.2" <-> 28-bit address. parse_address refuses anything past 28 bits.
bool parse_address(const std::string& text, uint32_t& out);
std::string format_address(uint32_t addr);

// The station's map of calls to 5066 node addresses, read from a text file:
// one "CALL ADDRESS" per line, # starts a comment. Calls are upper-cased.
bool read_peers(const std::string& path, std::map<std::string, uint32_t>& out, std::string* error);

// ---- Pieces --------------------------------------------------------------
// A frame bigger than the node's MTU goes as numbered pieces, one U_PDU each.
// With ARQ and in-order delivery they arrive in order or not at all, so the
// far side only has to join them. Each piece starts with 7 bytes:
//   'P' 'S'  version 1  seq (2 bytes)  index  count
constexpr size_t kPieceHeader = 7;
constexpr size_t kMaxPieces = 255;

// Splits data into pieces of at most mtu bytes each, header included. Empty
// if mtu is too small or the data needs more than kMaxPieces.
std::vector<std::string> split(const std::string& data, uint16_t seq, size_t mtu);

// Joins pieces per source address. Returns each whole frame once its last
// piece arrives. A U_PDU with no piece header is returned as it came.
class Joiner {
public:
    std::vector<std::string> add(uint32_t src, const std::string& u_pdu);
    size_t dropped() const { return dropped_; }

private:
    struct Part {
        uint16_t seq = 0;
        uint8_t count = 0;
        std::vector<std::string> got;
    };
    std::map<uint32_t, Part> parts_;
    size_t dropped_ = 0;
};

// 5066 priority (0-15, higher goes first) for a message precedence.
uint8_t priority_for(char prec);

}  // namespace pna::s5066
