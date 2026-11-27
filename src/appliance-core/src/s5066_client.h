// The engine as a STANAG 5066 client: a ModemDriver that hands each frame to
// the station's 5066 node over the SIS (s5066.h), so the node's ARQ carries it.
//
// This is greybeard-ale's interface 7: TCP to the node's SIS port (5066) on
// this box, bind on SAP 3 (HMTP, Annex F Table F-1) with ARQ, S_UNIDATA_REQUEST
// out, confirm or reject back, S_UNIDATA_INDICATION in. If the node runs on
// another host, that port is forwarded over the ssh link, so the client still
// connects to 127.0.0.1.
//
// The frame's dst picks the node address from the peers file (CALL ADDRESS per
// line). A frame to ALL goes non-ARQ to the all-stations address. A frame
// bigger than the node's MTU goes as numbered pieces (s5066::split), joined by
// the far client before the core sees it.
//
// What stays ours: the core's message ACK and retry rule. A 5066 confirm means
// the far node (or client) has the bytes; our ACK means the far engine stored
// the message. Only the ACK ends a message's life in the queue.
#pragma once

#include "modem.h"
#include "s5066.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <deque>
#include <thread>
#include <vector>

namespace pna {

struct S5066Config {
    std::string host = "127.0.0.1";
    uint16_t port = s5066::kPort;
    uint8_t sap = 3;              // HMTP, as interface 7 fixes it; every appliance in a net uses the same
    uint8_t rank = 0;
    uint8_t confirm = s5066::kNodeConfirm;  // or kClientConfirm
    uint32_t ttl_seconds = 600;   // per U_PDU; a send fails when it runs out
    uint8_t broadcast_retries = 2;
    // How a message to ALL goes. Each peer: one non-ARQ copy to every station
    // in the peers file, which any node can carry (greybeard-ale's s5066-node
    // has no broadcast address yet). Group: one copy to the all-stations
    // address, for a node that takes it.
    bool broadcast_each_peer = true;
    std::string peers_file;       // CALL ADDRESS per line
    int reconnect_ms = 3000;
    int poll_ms = 100;
};

class S5066Client : public ModemDriver {
public:
    explicit S5066Client(S5066Config cfg);
    ~S5066Client() override;

    void start(Callbacks cb) override;
    void stop() override;
    bool ready() const override { return bound_; }
    bool busy() const override { return sending_ || flow_off_; }
    bool send(const std::string& bytes) override;
    std::string describe() const override;

    // For status and tests.
    uint16_t mtu() const { return mtu_; }
    size_t peers() const;

private:
    using Action = std::function<void()>;
    void run();
    bool connect_and_bind();
    void close_link(const std::string& why, std::vector<Action>& after);
    void handle(const s5066::Prim& p, std::vector<Action>& after);
    void finish_send(bool ok, const std::string& err, std::vector<Action>& after);
    bool write_all(const std::string& bytes);
    void reload_peers_if_changed();
    void event(const std::string& text);

    S5066Config cfg_;
    Callbacks cb_;

    mutable std::mutex mu_;          // everything below but the atomics
    int fd_ = -1;
    s5066::Reader reader_;
    s5066::Joiner joiner_;
    std::map<std::string, uint32_t> peers_;
    long long peers_mtime_ = -1;
    uint16_t seq_ = 0;
    int outstanding_ = 0;            // confirms still due for the current send
    std::chrono::steady_clock::time_point deadline_;
    std::string pending_fail_;       // a send that failed before reaching the node
    bool pending_ok_ = false;        // a broadcast handed to the node
    // A broadcast waits a moment for the node to refuse its copies: a non-ARQ
    // request gets no confirm, only a refusal.
    int bcast_copies_ = 0;
    int bcast_refused_ = 0;
    std::string bcast_why_;
    std::chrono::steady_clock::time_point bcast_until_;
    std::vector<std::string> notes_; // log lines to give out without our lock

    std::atomic<uint16_t> mtu_{0};
    std::atomic<bool> bound_{false};
    std::atomic<bool> sending_{false};
    std::atomic<bool> flow_off_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
};

// ---- link = controller ----------------------------------------------------
// The core as the networking controller over STANAG 5066 SIS to greybeard-ale's
// station controller, the link controller (MIL-STD-188-141B D.4.2.8.3; docs/api.md,
// "Keying"; decision D11, "lean SIS"). The primitives and their encoding are
// STANAG 5066 Annex A v1.2, A.2.1 and A.2.2; the codec is s5066.h.
//
//  - One Unix socket per service, DIR/sis-<service>.sock. The transport is the
//    only departure: the controller checks the core's unit by SO_PEERCRED,
//    which a TCP peer has not got. Framing is Annex A A.2.2.2.
//  - On each service's socket the operator's sender binds SAP 13 and, where
//    the service has automatic modes, the automatic sender binds SAP 14, each
//    on its own connection (S_BIND_REQUEST, A.2.1.1 and A.2.2.3). The service
//    type is non-ARQ (Transmission Mode 0010), no delivery confirmation, in
//    order, no retransmission (Figure A-3).
//  - One frame per S_UNIDATA_REQUEST (A.2.1.6, Figure A-20), to the all-stations
//    address as a group address (A.2.2.28.1), with the same delivery mode
//    given explicitly (Figure A-29). A U_PDU over the MTU of S_BIND_ACCEPTED
//    (A.2.1.3: 2048 for a general-purpose node) is never submitted.
//  - Every request waits for its one answer, S_UNIDATA_REQUEST_CONFIRM (A.2.1.7)
//    or S_UNIDATA_REQUEST_REJECTED (A.2.1.8). Annex A issues these only when
//    confirmation was asked; the controller departs and answers every request,
//    after the key ends. The answer names its U_PDU by a copy of its first
//    bytes (A.2.1.7 (7)), which matches it to its request. No answer within the
//    time to live plus a grace, or a dropped connection, is a failure.
//  - S_KEEP_ALIVE (A.2.1.17) is answered in kind.
// The controller's own bounds (greybeard-ale docs/station-controller.md and
// tools/gale_rx.py): no claim on the radio is held past claim-max-s, and an
// order to gale-key is waited on for the order timeout.
constexpr int kSisClaimMaxS = 120;
constexpr int kSisOrderTimeoutS = 150;

struct SisLinkConfig {
    std::string dir = "/run/gale-rx";  // --controller-sis-dir: DIR/sis-<service>.sock
    uint8_t sap_operator = 13;
    uint8_t sap_automatic = 14;
    uint32_t ttl_seconds = 600;        // s5066-ttl, per U_PDU, sent in 2 s units (Figure A-20); 0 no limit
    int answer_grace_s = 30;           // waited past the TTL for the answer
    int reconnect_ms = 3000;
    uint16_t mtu = 2048;               // A.2.1.3 (7), and the controller's
    // A frame nobody is waiting on the answer of is settled from the
    // controller's status.last (gale_key.h, settle). How long it may wait there
    // for a result the controller does not have yet, and how often status is
    // read again meanwhile.
    int settle_wait_s = kSisClaimMaxS + kSisOrderTimeoutS;
    int settle_retry_ms = 2000;
};

constexpr size_t kSisMtu = 2048;

// One frame handed down.
struct SisSend {
    std::string service, burst, msg_id, frame, sha256;
    bool automatic = false;  // SAP 14, else SAP 13
    uint8_t priority = 0;    // 0-15 (A.2.1.6 (2))
};

class SisControllerLink {
public:
    // confirmed: S_UNIDATA_REQUEST_CONFIRM. Otherwise reason is the
    // REJECTED reason (A.2.1.8), or 0 with why for no answer or a drop.
    using Result = std::function<void(const SisSend& s, bool confirmed, uint8_t reason, const std::string& why)>;

    explicit SisControllerLink(SisLinkConfig cfg);
    ~SisControllerLink();
    SisControllerLink(const SisControllerLink&) = delete;
    SisControllerLink& operator=(const SisControllerLink&) = delete;

    // Before start: a connection for this service's operator sender, and one
    // for its automatic sender when automatic is set.
    void add(const std::string& service, bool automatic);
    // on_bound: called from the link's thread at every S_BIND_ACCEPTED, the
    // first and each one after a dropped connection.
    using Bound = std::function<void(const std::string& service, bool automatic)>;
    void start(Result on_result, std::function<void(const std::string&)> log, Bound on_bound = {});
    void stop();

    // Writes one S_UNIDATA_REQUEST. False with *why when it was not written:
    // no such connection, not bound, or a U_PDU over the MTU.
    bool submit(const SisSend& s, std::string* why);
    bool bound(const std::string& service, bool automatic) const;
    bool has(const std::string& service, bool automatic) const;
    size_t outstanding() const;
    std::string path_for(const std::string& service) const;
    const SisLinkConfig& config() const { return cfg_; }

private:
    struct Pending {
        SisSend send;
        std::chrono::steady_clock::time_point deadline;
    };
    struct Conn {
        std::string service;
        bool automatic = false;
        uint8_t sap = 0;
        int fd = -1;
        bool bound = false;
        uint16_t mtu = 0;
        s5066::Reader reader;
        std::deque<Pending> pending;
        std::chrono::steady_clock::time_point next_try{};
        std::string told;  // the last state logged, so it is said once
    };
    struct Done {
        SisSend send;
        bool confirmed = false;
        uint8_t reason = 0;
        std::string why;
    };
    void run();
    void open(Conn& c, std::vector<std::string>& notes);  // caller holds mu_
    void drop(Conn& c, const std::string& why, std::vector<Done>& done, std::vector<std::string>& notes);
    void handle(Conn& c, const s5066::Prim& p, std::vector<Done>& done, std::vector<std::string>& notes);
    std::vector<std::pair<std::string, bool>> bound_now_;  // binds accepted, not yet told (mu_)
    Bound on_bound_;
    Conn* find(const std::string& service, bool automatic);
    const Conn* find(const std::string& service, bool automatic) const;

    SisLinkConfig cfg_;
    Result on_result_;
    std::function<void(const std::string&)> log_;
    mutable std::mutex mu_;
    std::vector<std::unique_ptr<Conn>> conns_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace pna
