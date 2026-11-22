// Loopback modem: two of these wired together stand in for two radios, so the
// core runs and is tested with no modem or radio at all.
#pragma once

#include "modem.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace pna {

class LoopbackModem : public ModemDriver {
public:
    // bps sets the simulated airtime; 0 delivers at once.
    explicit LoopbackModem(int bps = 0);
    ~LoopbackModem() override;

    // Wires a and b so each one's transmissions reach the other.
    static void connect(LoopbackModem& a, LoopbackModem& b);

    // Test hooks: fail the next n sends, or flip a bit in the next n deliveries.
    void fail_next_sends(int n) { fail_sends_ = n; }
    void corrupt_next_deliveries(int n) { corrupt_ = n; }
    // While held, the modem reports not ready, so the core queues traffic.
    void hold(bool on) { held_ = on; }

    void start(Callbacks cb) override;
    void stop() override;
    bool ready() const override { return running_ && peer_ != nullptr && !held_; }
    bool busy() const override { return sending_; }
    bool send(const std::string& bytes) override;
    std::string describe() const override;

private:
    void run();
    void deliver(const std::string& bytes);

    int bps_;
    LoopbackModem* peer_ = nullptr;
    Callbacks cb_;
    std::mutex cb_mu_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> pending_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> sending_{false};
    std::atomic<int> fail_sends_{0};
    std::atomic<int> corrupt_{0};
    std::atomic<bool> held_{false};
};

}  // namespace pna
