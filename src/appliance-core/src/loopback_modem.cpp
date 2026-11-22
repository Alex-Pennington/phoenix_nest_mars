#include "loopback_modem.h"

#include <chrono>

namespace pna {

LoopbackModem::LoopbackModem(int bps) : bps_(bps) {}

LoopbackModem::~LoopbackModem() { stop(); }

void LoopbackModem::connect(LoopbackModem& a, LoopbackModem& b) {
    a.peer_ = &b;
    b.peer_ = &a;
}

void LoopbackModem::start(Callbacks cb) {
    {
        std::lock_guard<std::mutex> g(cb_mu_);
        cb_ = std::move(cb);
    }
    running_ = true;
    worker_ = std::thread([this] { run(); });
}

void LoopbackModem::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool LoopbackModem::send(const std::string& bytes) {
    if (!ready() || sending_) return false;
    sending_ = true;
    {
        std::lock_guard<std::mutex> g(mu_);
        pending_.push_back(bytes);
    }
    cv_.notify_all();
    return true;
}

std::string LoopbackModem::describe() const {
    return bps_ ? "loopback " + std::to_string(bps_) + " bps" : "loopback";
}

void LoopbackModem::deliver(const std::string& bytes) {
    if (!peer_) return;
    std::string b = bytes;
    if (corrupt_ > 0 && !b.empty()) {
        --corrupt_;
        b[b.size() / 2] ^= 0x20;
    }
    Callbacks cb;
    {
        std::lock_guard<std::mutex> g(peer_->cb_mu_);
        cb = peer_->cb_;
    }
    // Deliver in small chunks, as a modem data port does.
    for (size_t off = 0; off < b.size(); off += 7)
        if (cb.on_rx) cb.on_rx(b.substr(off, 7));
}

void LoopbackModem::run() {
    while (running_) {
        std::string bytes;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return !running_ || !pending_.empty(); });
            if (!running_) break;
            bytes = std::move(pending_.front());
            pending_.pop_front();
        }
        bool ok = true;
        std::string err;
        if (fail_sends_ > 0) {
            --fail_sends_;
            ok = false;
            err = "simulated modem failure";
        } else {
            if (bps_ > 0) {
                auto ms = static_cast<long>(bytes.size() * 8 * 1000 / bps_);
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
            deliver(bytes);
        }
        sending_ = false;
        Callbacks cb;
        {
            std::lock_guard<std::mutex> g(cb_mu_);
            cb = cb_;
        }
        if (cb.on_tx_done) cb.on_tx_done(ok, err);
    }
}

}  // namespace pna
