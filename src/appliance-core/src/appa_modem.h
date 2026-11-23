// The engine driving the modem directly over MIL-STD-188-110D Appendix A: a
// ModemDriver that is the modem's DTE on its TCP data socket (A.5.1).
//
// For a box with no STANAG 5066 node, or for bursts outside 5066. The modem
// side is greybeard-ale's m110_lan (pennington_m110a_demod), port 3000 on this
// box; if the modem runs elsewhere, that port is forwarded over ssh, so the
// driver still connects to 127.0.0.1.
//
// One send is one transmission: TRANSMIT_ARM, the frame as data packets once
// the modem says armed and ready, TRANSMIT_START, then done when the modem's
// transmit state goes back to flushed. Received data between CARRIER DETECTED
// and NO CARRIER goes to the core as it arrives. The waveform is the modem's
// setting; Appendix A gives the DTE no field to choose it.
//
// The modem holds one DTE at a time (A.4.1). If another program holds it, the
// connection is closed with no bytes, and the driver says so and tries again.
#pragma once

#include "appa.h"
#include "modem.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pna {

struct AppaConfig {
    std::string host = "127.0.0.1";
    uint16_t port = appa::kPort;
    int reconnect_ms = 3000;
    int poll_ms = 100;
    int keep_alive_ms = 1500;   // A.5.1.2.2: the modem closes after 30 s with no DATA packet
    int handshake_ms = 10000;   // CONNECT both ways, the probe, the setup packets
    int arm_ms = 10000;         // TRANSMIT_ARM to armed and ready
    int drain_extra_ms = 60000; // on top of the air time at the modem's rate
};

class AppaModem : public ModemDriver {
public:
    explicit AppaModem(AppaConfig cfg);
    ~AppaModem() override;

    void start(Callbacks cb) override;
    void stop() override;
    bool ready() const override { return up_; }
    bool busy() const override { return sending_ || carrier_; }
    bool send(const std::string& bytes) override;
    std::string describe() const override;

    // For status and tests.
    uint32_t tx_rate() const { return tx_rate_; }
    size_t dropped() const;

private:
    using Action = std::function<void()>;
    enum class Stage { kIdle, kArming, kStarted };

    void run();
    bool connect_link();
    void close_link(const std::string& why, std::vector<Action>& after);
    void handle(const appa::Packet& p, std::vector<Action>& after);
    void finish_send(bool ok, const std::string& err, std::vector<Action>& after);
    bool write_all(const std::string& bytes);
    void event(const std::string& text);
    appa::PacketId next_id();

    AppaConfig cfg_;
    Callbacks cb_;

    mutable std::mutex mu_;  // everything below but the atomics
    int fd_ = -1;
    appa::Reader reader_;
    bool got_connect_ = false, got_ack_ = false, got_probe_ = false, got_setup_ = false;
    bool heard_any_ = false;  // a byte from the modem on this connection
    std::chrono::steady_clock::time_point connected_at_, last_sent_, deadline_;
    Stage stage_ = Stage::kIdle;
    std::string pending_;     // the frame waiting for armed and ready
    uint64_t id_counter_ = 0;
    uint32_t tx_blocking_ = 0;
    std::string last_why_;   // the same reason twice in a row is logged once

    std::atomic<uint32_t> tx_rate_{0};
    std::atomic<bool> up_{false};
    std::atomic<bool> sending_{false};
    std::atomic<bool> carrier_{false};
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace pna
