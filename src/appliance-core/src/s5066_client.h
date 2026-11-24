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
#include <mutex>
#include <string>
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

}  // namespace pna
