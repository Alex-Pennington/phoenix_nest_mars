// MIL-STD-188-110D Appendix A driver: the packet codec, and two cores passing
// traffic through stand-in modems.
//
// The stand-in is not a modem. It speaks the modem's side of the A.5.1 TCP
// data socket: the handshake, transmit arm/start/drain with Tx Status, NACKs,
// and on the far stand-in CARRIER DETECTED, the data in blocking-factor
// chunks, NO CARRIER. It has no waveform. test_appa_m110 runs the driver
// against greybeard-ale's real m110_lan.
#include "../src/appa.h"
#include "../src/appa_modem.h"
#include "../src/core.h"
#include "check.h"
#include "services_fixture.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace pna;
namespace fs = std::filesystem;

static bool wait_for(const std::function<bool()>& cond, int ms = 8000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}

static std::string hex(const std::string& s) {
    static const char* d = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        o.push_back(d[c >> 4]);
        o.push_back(d[c & 15]);
    }
    return o;
}

// The A.5.3 CRC worked a second way: the polynomial written high bit first
// (0x9949), each byte and the result bit-reversed.
static uint8_t rev8(uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++)
        if (b & (1 << i)) r |= uint8_t(0x80 >> i);
    return r;
}
static uint16_t crc_other_way(const std::string& s) {
    uint16_t crc = 0;
    for (unsigned char c : s) {
        crc ^= uint16_t(rev8(c)) << 8;
        for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x9949) : uint16_t(crc << 1);
    }
    uint16_t r = 0;
    for (int i = 0; i < 16; i++)
        if (crc & (1 << i)) r |= uint16_t(0x8000 >> i);
    return r;
}

// ---- The stand-in modems ------------------------------------------------

class StubAir;

class StubModem {
public:
    StubModem(StubAir& air, uint32_t rate, uint32_t blocking) : air_(air), rate_(rate), blocking_(blocking) {
        lfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(lfd_, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
        ::listen(lfd_, 4);
        socklen_t len = sizeof sa;
        ::getsockname(lfd_, reinterpret_cast<sockaddr*>(&sa), &len);
        port_ = ntohs(sa.sin_port);
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }
    ~StubModem() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        if (cfd_ >= 0) ::close(cfd_);
        ::close(lfd_);
    }

    uint16_t port() const { return port_; }
    std::atomic<int> nak_next{0};
    std::atomic<bool> drop_client{false};
    std::atomic<int> connections{0};
    std::atomic<int> refused{0};

    std::mutex mu;
    std::vector<std::string> sent;      // each whole transmission, as the DTE gave it
    std::vector<uint8_t> orders;        // every data packet's order, in turn
    int keep_alives = 0;
    bool established = false;

    void to_dte(const std::string& bytes) {
        std::lock_guard<std::mutex> g(wmu_);
        if (cfd_ >= 0) (void)::send(cfd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
    }
    // The far side heard a transmission: carrier, data in blocking-factor chunks, no carrier.
    void receive(const std::string& data) {
        {
            std::lock_guard<std::mutex> g(mu);
            if (!established) return;
        }
        to_dte(appa::carrier_detect(true, rate_, blocking_));
        size_t chunk = blocking_ / 8;
        size_t count = (data.size() + chunk - 1) / chunk;
        for (size_t i = 0; i < count; i++) {
            appa::PacketId id{};
            id[11] = uint8_t(i);
            to_dte(appa::data_transfer(appa::order_for(i, count), id, data.substr(i * chunk, chunk)));
        }
        to_dte(appa::carrier_detect(false, 0, 0));
    }

private:
    void run();
    void on_packet(const appa::Packet& p);
    void status(uint8_t state) { to_dte(appa::tx_status(state, 1u << 20, uint32_t(queue_.size()))); }

    StubAir& air_;
    uint32_t rate_, blocking_;
    uint16_t port_ = 0;
    int lfd_ = -1;
    int cfd_ = -1;
    std::mutex wmu_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    // handshake and transmitter, used by the stand-in's own thread only
    bool got_connect_ = false, got_ack_ = false, probed_ = false;
    bool armed_ = false, started_ = false, have_first_ = false, have_last_ = false;
    std::string queue_;
};

class StubAir {
public:
    std::vector<StubModem*> modems;
    void transmit(StubModem& from, const std::string& data) {
        for (auto* m : modems)
            if (m != &from) m->receive(data);
    }
};

void StubModem::run() {
    appa::Reader reader;
    while (running_) {
        pollfd p[2] = {{lfd_, POLLIN, 0}, {cfd_, POLLIN, 0}};
        int n = ::poll(p, cfd_ >= 0 ? 2 : 1, 50);
        if (n <= 0) {
            if (drop_client.exchange(false) && cfd_ >= 0) {
                std::lock_guard<std::mutex> g(wmu_);
                ::close(cfd_);
                cfd_ = -1;
                std::lock_guard<std::mutex> g2(mu);
                established = false;
            }
            continue;
        }
        if (p[0].revents & POLLIN) {
            int c = ::accept(lfd_, nullptr, nullptr);
            if (cfd_ >= 0) {
                // A.4.1: one DTE at a time; the new one is closed with no bytes.
                ::close(c);
                ++refused;
            } else {
                {
                    std::lock_guard<std::mutex> g(wmu_);
                    cfd_ = c;
                }
                ++connections;
                reader = appa::Reader();
                got_connect_ = got_ack_ = probed_ = false;
                armed_ = started_ = have_first_ = have_last_ = false;
                queue_.clear();
                to_dte(appa::connect());
            }
        }
        if (cfd_ >= 0 && (p[1].revents & (POLLIN | POLLHUP))) {
            char buf[8192];
            ssize_t r = ::recv(cfd_, buf, sizeof buf, 0);
            if (r <= 0) {
                std::lock_guard<std::mutex> g(wmu_);
                ::close(cfd_);
                cfd_ = -1;
                std::lock_guard<std::mutex> g2(mu);
                established = false;
                continue;
            }
            for (auto& pk : reader.feed(std::string(buf, size_t(r)))) on_packet(pk);
        }
    }
}

void StubModem::on_packet(const appa::Packet& p) {
    using namespace appa;
    if (p.type == kConnect) {
        got_connect_ = true;
        to_dte(connect_ack());
    } else if (p.type == kConnectAck) {
        got_ack_ = true;
    }
    if (p.type == kConnect || p.type == kConnectAck) {
        if (got_connect_ && got_ack_ && !probed_) {
            probed_ = true;
            to_dte(command(kConnectionProbe));
        }
        return;
    }
    if (p.type != kData) return;
    if (p.payload.empty()) {
        std::lock_guard<std::mutex> g(mu);
        ++keep_alives;
        return;
    }
    uint8_t cmd = uint8_t(p.payload[0]);
    bool up;
    {
        std::lock_guard<std::mutex> g(mu);
        up = established;
    }
    if (cmd == kConnectionProbe && !up) {
        to_dte(initial_setup(1, 100, 5000));
        to_dte(transmit_setup(rate_, blocking_));
        status(kFlushed);
        to_dte(carrier_detect(false, 0, 0));
        std::lock_guard<std::mutex> g(mu);
        established = true;
        return;
    }
    if (cmd == kTransmitArm) {
        // Armed already: the modem answers with its current state (A.5.1.2.5 step 1).
        armed_ = true;
        status(kArmedReady);
        return;
    }
    if (cmd == kDataTransfer) {
        uint8_t order = 0;
        std::string data;
        if (!read_data_transfer(p.payload, order, data)) return;
        PacketId id{};
        std::copy(p.payload.begin() + 2, p.payload.begin() + 14, id.begin());
        if (!armed_ || nak_next > 0) {
            if (nak_next > 0) --nak_next;
            to_dte(nak(0, id));
            return;
        }
        {
            std::lock_guard<std::mutex> g(mu);
            orders.push_back(order);
        }
        if (order == kFirstOnly || order == kFirstAndLast) have_first_ = true;
        if (!have_first_) {
            to_dte(nak(2, id));
            return;
        }
        queue_ += data;
        if (order == kLast || order == kFirstAndLast) have_last_ = true;
    } else if (cmd == kTransmitStart) {
        started_ = true;
        status(kStarted);
    } else {
        return;
    }
    if (started_ && have_last_) {
        status(kDrainingOk);
        {
            std::lock_guard<std::mutex> g(mu);
            sent.push_back(queue_);
        }
        air_.transmit(*this, queue_);
        queue_.clear();
        armed_ = started_ = have_first_ = have_last_ = false;
        status(kFlushed);
    }
}

static std::string state_of(Core& c, const std::string& id) {
    auto m = c.get(id);
    return m ? m->state : "";
}

int main() {
    // ---- The codec --------------------------------------------------------
    {
        // The CRC agrees with the polynomial worked the other way.
        for (std::string s : {std::string("\x49\x50\x55\x01\x00\x01", 6), std::string("123456789"),
                              std::string(1, '\x0C'), std::string(300, '\xA5')})
            CHECK(appa::crc16(s) == crc_other_way(s));

        // CONNECT: preamble, type 1, size 1, header CRC, version 12, payload CRC.
        std::string c = appa::connect();
        CHECK(c.size() == 11);
        CHECK(hex(c.substr(0, 6)) == "495055010001");
        CHECK(appa::crc16(c.substr(0, 6)) == ((uint8_t(c[6]) << 8) | uint8_t(c[7])));
        CHECK(uint8_t(c[8]) == 12);
        CHECK(appa::crc16(c.substr(8, 1)) == ((uint8_t(c[9]) << 8) | uint8_t(c[10])));

        // An empty payload (the keep-alive) has no payload CRC.
        CHECK(appa::keep_alive().size() == 8 && hex(appa::keep_alive().substr(0, 6)) == "495055000000");

        // A data packet: command 0, order, 12-byte ID, data.
        appa::PacketId id{};
        id[11] = 7;
        std::string d = appa::data_transfer(appa::kFirstAndLast, id, "HI");
        CHECK(hex(d.substr(0, 6)) == "495055000010");
        CHECK(hex(d.substr(8, 16)) == "0002000000000000000000000007" + hex("HI"));

        CHECK(appa::order_for(0, 1) == appa::kFirstAndLast);
        CHECK(appa::order_for(0, 3) == appa::kFirstOnly && appa::order_for(1, 3) == appa::kContinuation &&
              appa::order_for(2, 3) == appa::kLast);

        // The reader, one byte at a time, with junk in front and a bad packet between.
        std::string bad = appa::command(appa::kTransmitArm);
        bad[bad.size() - 1] ^= 0x01;  // payload CRC broken
        std::string stream = std::string("\x49\x50junk\x49", 7) + appa::connect() + bad +
                             appa::tx_status(appa::kArmedReady, 1000, 5) + appa::keep_alive();
        appa::Reader r;
        std::vector<appa::Packet> got;
        for (char ch : stream)
            for (auto& p : r.feed(std::string(1, ch))) got.push_back(p);
        CHECK(got.size() == 3);
        CHECK(got.size() == 3 && got[0].type == appa::kConnect && got[1].type == appa::kData &&
              got[2].payload.empty());
        appa::TxStatus st;
        CHECK(got.size() == 3 && appa::read_tx_status(got[1].payload, st) && st.state == appa::kArmedReady &&
              st.space == 1000 && st.fill == 5);
        CHECK(r.dropped() >= 1);

        // The modem's other packets read back.
        appa::Reader r2;
        auto ps = r2.feed(appa::carrier_detect(true, 600, 360) + appa::transmit_setup(2400, 1440) +
                          appa::nak(2, id) + appa::data_transfer(appa::kLast, id, "XYZ"));
        CHECK(ps.size() == 4);
        bool up = false;
        appa::RateAndBlock rb;
        CHECK(ps.size() == 4 && appa::read_carrier(ps[0].payload, up, rb) && up && rb.rate == 600 &&
              rb.blocking == 360);
        CHECK(ps.size() == 4 && appa::read_transmit_setup(ps[1].payload, rb) && rb.rate == 2400 &&
              rb.blocking == 1440);
        uint8_t cause = 9;
        CHECK(ps.size() == 4 && appa::read_nak(ps[2].payload, cause) && cause == 2);
        uint8_t order = 0;
        std::string data;
        CHECK(ps.size() == 4 && appa::read_data_transfer(ps[3].payload, order, data) && order == appa::kLast &&
              data == "XYZ");
        CHECK(appa::nak_text(0) == "transmit queues not armed");
    }

    // ---- No modem ---------------------------------------------------------
    {
        AppaConfig cfg;
        cfg.port = 1;  // nothing listens there
        cfg.reconnect_ms = 200;
        cfg.poll_ms = 20;
        AppaModem m(cfg);
        std::mutex mu;
        std::vector<std::string> events;
        ModemDriver::Callbacks cb;
        cb.on_event = [&](const std::string& t) {
            std::lock_guard<std::mutex> g(mu);
            events.push_back(t);
        };
        m.start(cb);
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        CHECK(!m.ready());
        CHECK(!m.send("X"));
        m.stop();
        std::lock_guard<std::mutex> g(mu);
        CHECK(events.size() == 1 && events[0].find("not reachable") != std::string::npos);
    }

    // ---- Two cores through stand-in modems --------------------------------
    fs::path dir = fs::temp_directory_path() / ("pna_appa_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        StubAir air;
        StubModem ma(air, 600, 360), mb(air, 600, 360);
        air.modems = {&ma, &mb};

        // Something else holds modem A first: the driver says so and waits.
        int holder = ::socket(AF_INET, SOCK_STREAM, 0);
        {
            sockaddr_in sa{};
            sa.sin_family = AF_INET;
            sa.sin_port = htons(ma.port());
            sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            CHECK(::connect(holder, reinterpret_cast<sockaddr*>(&sa), sizeof sa) == 0);
        }
        CHECK(wait_for([&] { return ma.connections == 1; }));

        auto make = [&](uint16_t port) {
            AppaConfig c;
            c.port = port;
            c.reconnect_ms = 200;
            c.poll_ms = 20;
            c.drain_extra_ms = 5000;
            return std::make_unique<AppaModem>(c);
        };
        auto da = make(ma.port());
        auto db = make(mb.port());
        AppaModem* pa = da.get();
        std::mutex emu;
        std::vector<std::string> a_events;
        CoreConfig cfa{"STA-A", (dir / "a.db").string(), 3, 1, true};
        cfa.rx_service = "mars";  // a channel-less test link: MARS traffic
        Core a(cfa, std::move(da));
        a.set_services(test_services());
        Core b(on_mars_link(CoreConfig{"STA-B", (dir / "b.db").string(), 3, 1, true}), std::move(db));
        b.set_services(test_services());
        a.on_log([&](const std::string& t) {
            std::lock_guard<std::mutex> g(emu);
            a_events.push_back(t);
        });
        a.start();
        b.start();

        CHECK(wait_for([&] { return ma.refused >= 2; }));
        CHECK(!pa->ready());
        {
            std::lock_guard<std::mutex> g(emu);
            int held = 0;
            for (auto& e : a_events)
                if (e.find("held by another program") != std::string::npos) ++held;
            CHECK(held == 1);  // said once, not every try
        }
        ::close(holder);
        CHECK(wait_for([&] { return pa->ready(); }));
        CHECK(pa->tx_rate() == 600);
        CHECK(pa->describe() == "110D App A 127.0.0.1:" + std::to_string(ma.port()) + ", 600 bit/s");

        // A message and its ACK, both over Appendix A.
        std::string err;
        std::string id = a.submit("STA-B", 'P', "HELLO STA-B THIS IS STA-A", &err, SendOpts::in("mars"));
        CHECK(!id.empty());
        CHECK(wait_for([&] { return state_of(a, id) == "acked"; }));
        auto rx = b.get(id);
        CHECK(rx && rx->body == "HELLO STA-B THIS IS STA-A" && rx->src == "STA-A");
        {
            std::lock_guard<std::mutex> g(ma.mu);
            CHECK(ma.sent.size() == 1 && ma.orders.size() == 1 && ma.orders[0] == appa::kFirstAndLast);
        }

        // A message bigger than one data packet goes as FIRST then LAST.
        std::string big;
        for (int i = 0; i < 120; ++i) big += "LINE " + std::to_string(i) + " OF A LONG MESSAGE FOR THE NET\n";
        size_t before;
        {
            std::lock_guard<std::mutex> g(ma.mu);
            before = ma.orders.size();
        }
        std::string bid = a.submit("STA-B", 'R', big, &err, SendOpts::in("mars"));
        CHECK(!bid.empty());
        CHECK(wait_for([&] { return state_of(a, bid) == "acked"; }));
        CHECK(b.get(bid) && b.get(bid)->body == big);
        {
            std::lock_guard<std::mutex> g(ma.mu);
            CHECK(big.size() > appa::kMaxData && ma.orders.size() - before == 2 &&
                  ma.orders[before] == appa::kFirstOnly && ma.orders[before + 1] == appa::kLast);
        }

        // The modem refuses the data once: the core retries and it goes.
        ma.nak_next = 1;
        std::string rid = a.submit("STA-B", 'R', "AFTER ONE REFUSAL", &err, SendOpts::in("mars"));
        CHECK(wait_for([&] { return state_of(a, rid) == "acked"; }));
        CHECK(a.get(rid) && a.get(rid)->attempts == 2);

        // To ALL: heard by the far station.
        std::string all = a.submit("ALL", 'R', "QST NET MEETS AT 2100Z", &err, SendOpts::in("mars"));
        CHECK(wait_for([&] { return b.get(all) && b.get(all)->body == "QST NET MEETS AT 2100Z"; }));

        // Idle: the driver keeps the link alive (the modem closes after 30 s without).
        int ka0;
        {
            std::lock_guard<std::mutex> g(ma.mu);
            ka0 = ma.keep_alives;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2200));
        {
            std::lock_guard<std::mutex> g(ma.mu);
            CHECK(ma.keep_alives > ka0);
        }

        // The modem drops the connection: the driver connects again and traffic flows.
        int conns = ma.connections;
        ma.drop_client = true;
        CHECK(wait_for([&] { return !pa->ready(); }, 3000));
        CHECK(wait_for([&] { return pa->ready() && ma.connections == conns + 1; }));
        std::string after = a.submit("STA-B", 'R', "AFTER RECONNECT", &err, SendOpts::in("mars"));
        CHECK(wait_for([&] { return state_of(a, after) == "acked"; }));

        a.stop();
        b.stop();
    }
    fs::remove_all(dir);
    return check::report("test_appa");
}
