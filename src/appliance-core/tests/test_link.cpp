// Link state from greybeard-ale (link_monitor.h) and the core holding traffic
// until a station's link is up. A thread here stands in for ale-scan's control
// socket; greybeard-ale has no per-station links yet, so "links" is the core's
// proposed field.
#include "../src/core.h"
#include "../src/link_monitor.h"
#include "../src/loopback_modem.h"
#include "check.h"
#include "services_fixture.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static bool wait_for(const std::function<bool()>& cond, int ms = 5000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}

// Answers every line with the current reply, like ale-scan --control.
class FakeScanner {
public:
    explicit FakeScanner(std::string path) : path_(std::move(path)) {
        ::unlink(path_.c_str());
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        std::memcpy(a.sun_path, path_.c_str(), path_.size());
        ::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a);
        ::listen(fd_, 8);
        th_ = std::thread([this] { run(); });
    }
    ~FakeScanner() {
        stop_ = true;
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        th_.join();
        ::unlink(path_.c_str());
    }
    void set(const std::string& reply) {
        std::lock_guard<std::mutex> g(mu_);
        reply_ = reply;
    }
    int asked() const { return asked_; }

private:
    void run() {
        while (!stop_) {
            int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) continue;
            char buf[256];
            ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n > 0) {
                ++asked_;
                std::string r;
                {
                    std::lock_guard<std::mutex> g(mu_);
                    r = reply_ + "\n";
                }
                ::send(c, r.data(), r.size(), MSG_NOSIGNAL);
            }
            ::close(c);
        }
    }
    std::string path_;
    int fd_ = -1;
    std::thread th_;
    std::atomic<bool> stop_{false};
    std::atomic<int> asked_{0};
    std::mutex mu_;
    std::string reply_ = R"({"ok":true,"state":"scanning"})";
};

// ale-scan's status as it is today (no links), channel list left out.
static const char* kScanNoLinks =
    R"({"ok":true,"state":"scanning","plan":"day","windows":4,"window":2,"visit":3,"visit_s":2.0,)"
    R"("visits":118,"stream_unix":1763850000.5,"hold":null,"order_pending":false,"plan_pending":"",)"
    R"("engine":true,"visit_remote":false,"clock":"wwv","clock_counter":7,"wwv_blocks":2,)"
    R"("last_wwv_unix":1763849000,"plan_switches":1,"note":""})";

static std::string with_links(const std::string& links) {
    std::string s = kScanNoLinks;
    s.pop_back();
    return s + R"(,"links":)" + links + "}";
}

int main() {
    // Parsing ale-scan's reply.
    {
        LinkState s;
        CHECK(parse_link_status(kScanNoLinks, &s));
        CHECK(s.scanner == "scanning" && s.plan == "day" && s.engine && s.clock == "wwv");
        CHECK(s.hold_window == 0 && !s.links_reported && s.links.empty());

        std::string held = kScanNoLinks;
        held.replace(held.find(R"("hold":null)"), 11, R"("hold":{"window":3,"left_s":40})");
        CHECK(parse_link_status(held, &s) && s.hold_window == 3);

        CHECK(parse_link_status(with_links(R"([{"station":"AAR4XY","state":"linked","since":1763850001},)"
                                           R"({"station":"AAR4XZ","state":"calling"}])"),
                                &s));
        CHECK(s.links_reported && s.links.size() == 2);
        CHECK(s.links[0].station == "AAR4XY" && s.links[0].up() && s.links[0].since == 1763850001);
        CHECK(s.links[1].station == "AAR4XZ" && !s.links[1].up());

        std::string one = kScanNoLinks;
        one.pop_back();
        one += R"(,"link":{"with":"AAR4XY","state":"linked"}})";
        CHECK(parse_link_status(one, &s) && s.links.size() == 1 && s.links[0].up());

        // The clock check.
        LinkState c;
        CHECK(parse_link_status(kScanNoLinks, &c) && c.last_wwv_at == 1763849000);
        CHECK(clock_note(c, 1763850000).empty());  // no socket: no link source
        c.socket = "x";
        c.reachable = true;
        CHECK(clock_note(c, 1763849000 + 30) == "set by WWV just now");
        CHECK(clock_note(c, 1763849000 + 17 * 60) == "set by WWV 17 min ago");
        CHECK(clock_note(c, 1763849000 + 5 * 3600) == "set by WWV 5 h ago");
        c.clock = "host";
        CHECK(clock_note(c, 1763850000).find("not set by WWV") == 0);
        c.reachable = false;
        CHECK(clock_note(c, 1763850000).find("can't tell") == 0);

        CHECK(!parse_link_status(R"({"ok":false,"error":"unknown command"})", &s) && s.error == "unknown command");
        CHECK(!parse_link_status("not json", &s) && !s.error.empty());
    }

    fs::path dir = fs::temp_directory_path() / ("pna_link_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string sock = (dir / "ale-scan.sock").string();

    // The monitor: no socket, then a socket, then gone again.
    {
        LinkMonitor mon(sock, 200);
        mon.poll_once();
        CHECK(!mon.state().reachable && mon.state().error.find("no socket") != std::string::npos);
        {
            FakeScanner fake(sock);
            fake.set(with_links(R"([{"station":"AAR4XY","state":"linked"}])"));
            mon.poll_once();
            CHECK(mon.state().reachable && mon.link_up("AAR4XY") && !mon.link_up("AAR4XZ"));
            fake.set(with_links(R"([{"station":"AAR4XY","state":"down"}])"));
            mon.poll_once();
            CHECK(!mon.link_up("AAR4XY"));
            fake.set(with_links(R"([{"station":"AAR4XY","state":"linked"}])"));
            mon.poll_once();
            CHECK(mon.link_up("AAR4XY"));
        }
        mon.poll_once();
        CHECK(!mon.state().reachable && !mon.link_up("AAR4XY"));
    }

    // The gate: A holds traffic for B until the scanner reports B linked.
    {
        FakeScanner fake(sock);
        fake.set(kScanNoLinks);
        auto mon = std::make_shared<LinkMonitor>(sock, 100);
        mon->start();

        auto ma = std::make_unique<LoopbackModem>(0);
        auto mb = std::make_unique<LoopbackModem>(0);
        LoopbackModem::connect(*ma, *mb);
        CoreConfig ca{"A", (dir / "a.db").string(), 3, 1, true};
        ca.link_gate = true;
        Core a(ca, std::move(ma));
        a.set_services(test_services());
        a.set_link_monitor(mon);
        Core b(CoreConfig{"B", (dir / "b.db").string(), 3, 1, true}, std::move(mb));
        b.set_services(test_services());
        a.start();
        b.start();

        std::string err;
        std::string id = a.submit("B", 'R', "HELD UNTIL LINKED", &err, SendOpts::in("mars"));
        CHECK(!id.empty());
        CHECK(wait_for([&] { return fake.asked() >= 3; }));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(a.get(id) && a.get(id)->state == "queued" && a.get(id)->attempts == 0);
        CHECK(a.waits_for_link("B"));
        CoreStatus st = a.status();
        CHECK(st.link_gate && st.held_for_link == 1 && st.link.reachable && st.link.clock == "wwv");
        bool told = false;
        for (auto& e : a.events(0, 200))
            if (e.text.find("waiting for a link to B") != std::string::npos) told = true;
        CHECK(told);
        bool clock_logged = false;
        for (auto& e : a.events(0, 200))
            if (e.text == "clock set by WWV") clock_logged = true;
        CHECK(clock_logged);

        // Another station's link doesn't release it.
        fake.set(with_links(R"([{"station":"C","state":"linked"}])"));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(a.get(id)->state == "queued");

        fake.set(with_links(R"([{"station":"B","state":"linked","since":1763850001}])"));
        CHECK(wait_for([&] { return a.get(id) && a.get(id)->state == "acked"; }));
        CHECK(b.get(id) && b.get(id)->body == "HELD UNTIL LINKED");
        CHECK(!a.waits_for_link("B") && a.status().held_for_link == 0);

        // B's ACK went back without any link reported for A: ACKs aren't held.
        // Link drops: the next message waits again.
        fake.set(with_links(R"([{"station":"B","state":"down"}])"));
        CHECK(wait_for([&] { return a.waits_for_link("B"); }));
        std::string id2 = a.submit("B", 'R', "SECOND", &err, SendOpts::in("mars"));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(a.get(id2)->state == "queued");
        // The scanner goes away: still held, and the status says why.
        fake.set(R"({"ok":false,"error":"busy"})");
        CHECK(wait_for([&] { return !a.status().link.reachable; }));
        CHECK(a.status().link.error == "busy" && a.get(id2)->state == "queued");
        std::string hosted = with_links("[]");
        hosted.replace(hosted.find(R"("clock":"wwv")"), 13, R"("clock":"host")");
        fake.set(hosted);
        CHECK(wait_for([&] {
            for (auto& e : a.events(0, 400))
                if (e.text.find("clock not set by WWV") == 0) return true;
            return false;
        }));

        a.stop();
        b.stop();
        mon->stop();
    }

    // No gate: the monitor is read but nothing waits.
    {
        FakeScanner fake(sock);
        auto mon = std::make_shared<LinkMonitor>(sock, 100);
        mon->start();
        auto ma = std::make_unique<LoopbackModem>(0);
        auto mb = std::make_unique<LoopbackModem>(0);
        LoopbackModem::connect(*ma, *mb);
        Core a(CoreConfig{"A", (dir / "a2.db").string(), 3, 1, true}, std::move(ma));
        a.set_services(test_services());
        a.set_link_monitor(mon);
        Core b(CoreConfig{"B", (dir / "b2.db").string(), 3, 1, true}, std::move(mb));
        b.set_services(test_services());
        a.start();
        b.start();
        std::string err;
        std::string id = a.submit("B", 'R', "NOT GATED", &err, SendOpts::in("mars"));
        CHECK(wait_for([&] { return a.get(id) && a.get(id)->state == "acked"; }));
        CHECK(!a.status().link_gate && a.status().held_for_link == 0);
        a.stop();
        b.stop();
        mon->stop();
    }

    fs::remove_all(dir);
    return check::report("test_link");
}
