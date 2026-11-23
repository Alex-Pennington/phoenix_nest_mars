#include "appa_modem.h"

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace pna {

using Clock = std::chrono::steady_clock;

AppaModem::AppaModem(AppaConfig cfg) : cfg_(std::move(cfg)) {}

AppaModem::~AppaModem() { stop(); }

void AppaModem::event(const std::string& text) {
    if (cb_.on_event) cb_.on_event(text);
}

size_t AppaModem::dropped() const {
    std::lock_guard<std::mutex> g(mu_);
    return reader_.dropped();
}

std::string AppaModem::describe() const {
    std::string s = "110D App A " + cfg_.host + ":" + std::to_string(cfg_.port);
    if (up_) s += ", " + std::to_string(tx_rate_.load()) + " bit/s";
    else s += ", not connected";
    return s;
}

void AppaModem::start(Callbacks cb) {
    if (running_) return;
    cb_ = std::move(cb);
    running_ = true;
    worker_ = std::thread([this] { run(); });
}

void AppaModem::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> g(mu_);
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    up_ = false;
}

// Caller holds mu_.
bool AppaModem::write_all(const std::string& bytes) {
    size_t off = 0;
    while (off < bytes.size()) {
        ssize_t n = ::send(fd_, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    last_sent_ = Clock::now();
    return true;
}

// Caller holds mu_. A.5.1.1.5.2: unique per data packet.
appa::PacketId AppaModem::next_id() {
    appa::PacketId id{};
    uint64_t v = ++id_counter_;
    for (int i = 11; i >= 4; i--) {
        id[i] = uint8_t(v & 0xFF);
        v >>= 8;
    }
    return id;
}

bool AppaModem::send(const std::string& bytes) {
    std::lock_guard<std::mutex> g(mu_);
    if (!up_ || sending_ || fd_ < 0 || bytes.empty()) return false;
    if (!write_all(appa::command(appa::kTransmitArm))) return false;
    pending_ = bytes;
    stage_ = Stage::kArming;
    sending_ = true;
    deadline_ = Clock::now() + std::chrono::milliseconds(cfg_.arm_ms);
    return true;
}

// Caller holds mu_.
void AppaModem::finish_send(bool ok, const std::string& err, std::vector<Action>& after) {
    if (!sending_) return;
    sending_ = false;
    stage_ = Stage::kIdle;
    pending_.clear();
    after.push_back([this, ok, err] {
        if (cb_.on_tx_done) cb_.on_tx_done(ok, err);
    });
}

// Caller holds mu_.
void AppaModem::close_link(const std::string& why, std::vector<Action>& after) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    bool was = up_.exchange(false);
    carrier_ = false;
    reader_ = appa::Reader();
    if (sending_) finish_send(false, "modem connection lost (" + why + ")", after);
    if (was || why != last_why_) after.push_back([this, why] { event("110D App A modem: " + why); });
    last_why_ = why;
}

bool AppaModem::connect_link() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(cfg_.port);
    if (::inet_pton(AF_INET, cfg_.host.c_str(), &sa.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0) {
        ::close(fd);
        return false;
    }
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    std::lock_guard<std::mutex> g(mu_);
    fd_ = fd;
    reader_ = appa::Reader();
    got_connect_ = got_ack_ = got_probe_ = got_setup_ = heard_any_ = false;
    connected_at_ = Clock::now();
    // A.5.1.2.1: each side sends CONNECT and answers the other's with CONNECT_ACK.
    if (!write_all(appa::connect())) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

// Caller holds mu_.
void AppaModem::handle(const appa::Packet& p, std::vector<Action>& after) {
    using namespace appa;
    if (p.type == kError) {
        close_link("the modem reported a protocol error", after);
        return;
    }
    if (p.type == kConnect || p.type == kConnectAck) {
        if (p.payload.size() != 1 || uint8_t(p.payload[0]) != kVersion) {
            close_link("the modem speaks Appendix A version " +
                           (p.payload.empty() ? std::string("?") : std::to_string(uint8_t(p.payload[0]))) +
                           ", not 12",
                       after);
            return;
        }
        if (p.type == kConnect) {
            got_connect_ = true;
            write_all(connect_ack());
        } else {
            got_ack_ = true;
        }
        return;
    }
    if (p.type != kData || p.payload.empty()) return;  // keep-alive, or a type we don't use

    uint8_t cmd = uint8_t(p.payload[0]);
    switch (cmd) {
    case kConnectionProbe:
        // The modem times the round trip with this during the handshake. Once
        // the link is up, the modem only ever echoes ours, so nothing is sent back.
        if (!up_ && p.payload.size() == 1) {
            got_probe_ = true;
            write_all(command(kConnectionProbe));
        }
        break;
    case kInitialSetup:
        got_setup_ = true;
        break;
    case kTransmitSetup: {
        RateAndBlock rb;
        if (read_transmit_setup(p.payload, rb)) {
            tx_rate_ = rb.rate;
            tx_blocking_ = rb.blocking;
        }
        break;
    }
    case kTxStatus: {
        TxStatus st;
        if (!read_tx_status(p.payload, st)) break;
        // The first status after the setup packets says the link is up (A.5.1.2.1 item 3).
        if (!up_ && got_setup_) {
            up_ = true;
            last_why_.clear();
            after.push_back([this] { event("110D App A modem: connected, " + describe()); });
        }
        if (!sending_) break;
        if (stage_ == Stage::kArming && st.state == kArmedReady) {
            std::string out;
            size_t count = (pending_.size() + kMaxData - 1) / kMaxData;
            for (size_t i = 0; i < count; i++)
                out += data_transfer(order_for(i, count), next_id(), pending_.substr(i * kMaxData, kMaxData));
            out += command(kTransmitStart);
            if (!write_all(out)) {
                close_link("write failed", after);
                return;
            }
            stage_ = Stage::kStarted;
            uint32_t rate = tx_rate_ ? tx_rate_.load() : 75;
            auto air_ms = static_cast<long long>(pending_.size()) * 8 * 1000 / rate;
            deadline_ = Clock::now() + std::chrono::milliseconds(air_ms + cfg_.drain_extra_ms);
        } else if (stage_ == Stage::kStarted && st.state == kFlushed) {
            finish_send(true, "", after);
        } else if (stage_ == Stage::kStarted && st.state == kDrainingForced) {
            finish_send(false, "the modem cut the transmission short (draining, forced)", after);
        }
        break;
    }
    case kTxDataNak: {
        uint8_t cause = 0;
        if (read_nak(p.payload, cause) && sending_)
            finish_send(false, "the modem refused the data: " + nak_text(cause), after);
        break;
    }
    case kCarrierDetect: {
        bool up = false;
        RateAndBlock rb;
        if (!read_carrier(p.payload, up, rb)) break;
        bool was = carrier_.exchange(up);
        if (!up_) break;  // the one sent with the setup packets
        if (up && !was)
            after.push_back([this, rb] { event("110D App A modem: carrier, " + std::to_string(rb.rate) + " bit/s"); });
        else if (!up && was)
            after.push_back([this] { event("110D App A modem: no carrier"); });
        break;
    }
    case kDataTransfer: {
        uint8_t order = 0;
        std::string data;
        if (read_data_transfer(p.payload, order, data) && !data.empty())
            after.push_back([this, data] {
                if (cb_.on_rx) cb_.on_rx(data);
            });
        break;
    }
    default:
        break;
    }
}

void AppaModem::run() {
    auto next_try = Clock::now();
    bool told_down = false;
    while (running_) {
        std::vector<Action> after;
        int fd;
        {
            std::lock_guard<std::mutex> g(mu_);
            fd = fd_;
            auto now = Clock::now();
            if (fd_ >= 0) {
                if (!up_ && now - connected_at_ > std::chrono::milliseconds(cfg_.handshake_ms)) {
                    close_link("no handshake from the modem in time", after);
                    next_try = now + std::chrono::milliseconds(cfg_.reconnect_ms);
                } else if (sending_ && now > deadline_) {
                    finish_send(false,
                                stage_ == Stage::kArming ? "the modem didn't arm in time"
                                                         : "the modem didn't finish the transmission in time",
                                after);
                } else if (now - last_sent_ >= std::chrono::milliseconds(cfg_.keep_alive_ms)) {
                    if (!write_all(appa::keep_alive())) close_link("write failed", after);
                }
                fd = fd_;
            }
        }
        for (auto& a : after) a();
        after.clear();

        if (fd < 0) {
            if (Clock::now() >= next_try) {
                if (!connect_link()) {
                    if (!told_down)
                        event("110D App A modem not reachable at " + cfg_.host + ":" + std::to_string(cfg_.port) +
                              "; trying every " + std::to_string(cfg_.reconnect_ms / 1000) + " s");
                    told_down = true;
                    next_try = Clock::now() + std::chrono::milliseconds(cfg_.reconnect_ms);
                } else {
                    told_down = false;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.poll_ms));
            continue;
        }

        pollfd pfd{fd, POLLIN, 0};
        int r = ::poll(&pfd, 1, cfg_.poll_ms);
        if (r <= 0) continue;
        char buf[8192];
        ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        std::unique_lock<std::mutex> g(mu_);
        if (fd_ != fd) continue;
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
            // A.4.1: a modem that already has a DTE closes a new connection with no bytes.
            close_link(heard_any_ ? "connection closed" : "the modem is held by another program", after);
            next_try = Clock::now() + std::chrono::milliseconds(cfg_.reconnect_ms);
        } else {
            heard_any_ = true;
            for (auto& pk : reader_.feed(std::string(buf, static_cast<size_t>(n)))) {
                handle(pk, after);
                if (fd_ < 0) {
                    next_try = Clock::now() + std::chrono::milliseconds(cfg_.reconnect_ms);
                    break;
                }
            }
        }
        // Run the callbacks without our lock: the core takes its own.
        g.unlock();
        for (auto& a : after) a();
    }
}

}  // namespace pna
