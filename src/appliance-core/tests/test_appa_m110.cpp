// The Appendix A driver against greybeard-ale's real modem server, m110_lan
// (pennington_m110a_demod), with no radio: the driver sends bytes, m110_lan
// encodes them as 110A 600S to a WAV, the WAV is fed back as m110_lan's
// receive audio, and the decoded bytes must come back to the driver the same.
//
// Then two cores, each with its own m110_lan, the WAVs each one writes fed to
// the other as receive audio: a message and its ACK through real 110A audio.
//
// Set PNA_M110_LAN to the m110_lan program to run it. Without it the test
// says it was skipped and passes, so the suite runs on a box without the modem.
#include "../src/appa_modem.h"
#include "../src/core.h"
#include "check.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace pna;
namespace fs = std::filesystem;

static bool wait_for(const std::function<bool()>& cond, int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return cond();
}

static uint16_t free_port() {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    socklen_t len = sizeof sa;
    ::getsockname(s, reinterpret_cast<sockaddr*>(&sa), &len);
    uint16_t p = ntohs(sa.sin_port);
    ::close(s);
    return p;
}

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

struct Server {
    pid_t pid = -1;
    int in = -1;
    uint16_t port = 0;
    fs::path dir;
    int next = 1;        // the next tx_N.wav to hand on
    bool fed = false;    // the first WAV goes whole, later ones without their header
};

static bool launch(const char* exe, const fs::path& dir, Server& s) {
    fs::create_directories(dir);
    s.dir = dir;
    s.port = free_port();
    uint16_t rci = free_port(), udp = free_port();
    std::string wav = (dir / "tx_%d.wav").string();
    fs::path log = dir / "m110_lan.log";
    int p[2];
    if (::pipe(p) != 0) return false;
    s.pid = ::fork();
    if (s.pid == 0) {
        ::dup2(p[0], 0);
        ::close(p[1]);
        int lf = ::open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ::dup2(lf, 2);
        int nul = ::open("/dev/null", O_WRONLY);
        ::dup2(nul, 1);
        std::string ps = std::to_string(s.port), rs = std::to_string(rci), us = std::to_string(udp);
        ::execl(exe, exe, "--mode", "600S", "--tx-out", wav.c_str(), "--rx-in", "-", "--rx-fast", "--port", ps.c_str(),
                "--rci-port", rs.c_str(), "--udp-port", us.c_str(), "--bind", "127.0.0.1", (char*)nullptr);
        _exit(127);
    }
    ::close(p[0]);
    s.in = p[1];
    return wait_for([&] { return slurp(log).find("m110_lan: ready:") != std::string::npos; }, 10000);
}

static void feed(Server& to, const std::string& wav) {
    size_t off = to.fed ? 44 : 0;
    to.fed = true;
    while (off < wav.size()) {
        ssize_t n = ::write(to.in, wav.data() + off, wav.size() - off);
        if (n <= 0) break;
        off += size_t(n);
    }
}

static void finish(Server& s) {
    if (s.in >= 0) ::close(s.in);
    if (s.pid > 0) {
        ::kill(s.pid, SIGTERM);
        ::waitpid(s.pid, nullptr, 0);
    }
}

static int two_cores(const char* exe, const fs::path& dir) {
    Server sa, sb;
    CHECK(launch(exe, dir / "a", sa));
    CHECK(launch(exe, dir / "b", sb));
    // The air: each new transmit WAV goes to the other modem's receive input.
    std::atomic<bool> run{true};
    std::thread air([&] {
        while (run) {
            for (auto [from, to] : {std::pair<Server*, Server*>{&sa, &sb}, {&sb, &sa}}) {
                fs::path w = from->dir / ("tx_" + std::to_string(from->next) + ".wav");
                if (fs::exists(w)) {
                    feed(*to, slurp(w));
                    ++from->next;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    auto make = [](uint16_t port) {
        AppaConfig c;
        c.port = port;
        c.poll_ms = 20;
        c.reconnect_ms = 300;
        return std::make_unique<AppaModem>(c);
    };
    {
        Core a(CoreConfig{"STA-A", (dir / "a.db").string(), 3, 1, true}, make(sa.port));
        Core b(CoreConfig{"STA-B", (dir / "b.db").string(), 3, 1, true}, make(sb.port));
        a.start();
        b.start();
        std::string err;
        std::string id = a.submit("STA-B", 'P', "HELLO STA-B THIS IS STA-A OVER 110A", &err);
        CHECK(!id.empty());
        auto acked = [&] {
            auto m = a.get(id);
            return m && m->state == "acked";
        };
        CHECK(wait_for(acked, 120000));
        auto rx = b.get(id);
        CHECK(rx && rx->body == "HELLO STA-B THIS IS STA-A OVER 110A" && rx->src == "STA-A");
        auto m = a.get(id);
        std::cout << "two cores: message " << (m ? m->state : "?") << " after " << (m ? m->attempts : 0)
                  << " attempt(s); " << (sa.next - 1) << " transmission(s) from A, " << (sb.next - 1)
                  << " from B\n";
        a.stop();
        b.stop();
    }
    run = false;
    air.join();
    finish(sa);
    finish(sb);
    return 0;
}

int main() {
    const char* exe = std::getenv("PNA_M110_LAN");
    if (!exe || !*exe) {
        std::cout << "test_appa_m110: skipped (set PNA_M110_LAN to the m110_lan program)\n";
        return 0;
    }
    std::signal(SIGPIPE, SIG_IGN);
    fs::path dir = fs::temp_directory_path() / ("pna_m110_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    uint16_t port = free_port(), rci = free_port(), udp = free_port();
    std::string wav = (dir / "tx_%d.wav").string();
    fs::path log = dir / "m110_lan.log";

    int in[2];
    if (::pipe(in) != 0) return 1;
    pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(in[0], 0);
        ::close(in[1]);
        int lf = ::open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ::dup2(lf, 2);
        int nul = ::open("/dev/null", O_WRONLY);
        ::dup2(nul, 1);
        std::string ps = std::to_string(port), rs = std::to_string(rci), us = std::to_string(udp);
        ::execl(exe, exe, "--mode", "600S", "--tx-out", wav.c_str(), "--rx-in", "-", "--rx-fast", "--port", ps.c_str(),
                "--rci-port", rs.c_str(), "--udp-port", us.c_str(), "--bind", "127.0.0.1", (char*)nullptr);
        _exit(127);
    }
    ::close(in[0]);
    // m110_lan says when it listens; a probe connection would take the one DTE slot.
    CHECK(wait_for([&] { return slurp(log).find("m110_lan: ready:") != std::string::npos; }, 10000));

    AppaConfig cfg;
    cfg.port = port;
    cfg.poll_ms = 20;
    cfg.reconnect_ms = 300;
    AppaModem m(cfg);
    std::mutex mu;
    std::string rx;
    std::vector<std::string> events;
    std::atomic<int> done{0};
    std::atomic<bool> ok{false};
    std::string err;
    ModemDriver::Callbacks cb;
    cb.on_rx = [&](const std::string& b) {
        std::lock_guard<std::mutex> g(mu);
        rx += b;
    };
    cb.on_tx_done = [&](bool k, const std::string& e) {
        std::lock_guard<std::mutex> g(mu);
        ok = k;
        err = e;
        ++done;
    };
    cb.on_event = [&](const std::string& t) {
        std::lock_guard<std::mutex> g(mu);
        events.push_back(t);
    };
    m.start(cb);
    CHECK(wait_for([&] { return m.ready(); }, 10000));
    CHECK(m.tx_rate() == 600);

    // A frame-sized message with every byte value in it.
    std::string msg = "PNA APPENDIX A ROUND TRIP THROUGH M110_LAN 600S\n";
    for (int i = 0; i < 256; ++i) msg += char(i);
    CHECK(m.send(msg));
    CHECK(wait_for([&] { return done == 1; }, 60000));
    CHECK(ok);
    if (!ok) std::cout << "tx error: " << err << "\n";
    fs::path w1 = dir / "tx_1.wav";
    CHECK(fs::exists(w1));
    std::string audio = slurp(w1);
    CHECK(audio.size() > 44 && audio.compare(0, 4, "RIFF") == 0);

    // The transmission back in as receive audio.
    size_t off = 0;
    while (off < audio.size()) {
        ssize_t n = ::write(in[1], audio.data() + off, audio.size() - off);
        if (n <= 0) break;
        off += size_t(n);
    }
    CHECK(wait_for([&] {
        std::lock_guard<std::mutex> g(mu);
        return rx.size() >= msg.size();
    }, 60000));
    {
        std::lock_guard<std::mutex> g(mu);
        // 110A pads the last interleaver block, so the decode may run past the message.
        CHECK(rx.compare(0, msg.size(), msg) == 0);
        std::cout << "sent " << msg.size() << " bytes, received " << rx.size() << " bytes\n";
    }
    CHECK(wait_for([&] { return !m.busy(); }, 10000));

    m.stop();
    ::close(in[1]);
    ::kill(pid, SIGTERM);
    ::waitpid(pid, nullptr, 0);
    {
        std::lock_guard<std::mutex> g(mu);
        for (auto& e : events) std::cout << "event: " << e << "\n";
    }
    if (check::failures()) std::cout << slurp(log);
    two_cores(exe, dir / "pair");
    fs::remove_all(dir);
    return check::report("test_appa_m110");
}
