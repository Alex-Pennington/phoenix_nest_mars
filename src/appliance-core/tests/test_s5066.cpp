// STANAG 5066 client: the SIS codec, pieces, and two cores passing traffic
// through a stand-in 5066 node.
//
// The stand-in is not a 5066 node. It speaks just enough SIS to accept a
// bind, take S_UNIDATA_REQUEST, confirm or reject it, and hand it on as
// S_UNIDATA_INDICATION to the client bound on the far "node". It has no ARQ,
// no D_PDUs and no radio. greybeard-ale's real node replaces it.
#include "../src/core.h"
#include "../src/frame.h"
#include "../src/s5066.h"
#include "../src/s5066_client.h"
#include "check.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
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

// ---- The stand-in node network -------------------------------------------

class StubNet;

class StubNode {
public:
    StubNode(StubNet& net, uint32_t addr, uint16_t mtu) : net_(net), addr_(addr), mtu_(mtu) {
        lfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sa.sin_port = 0;
        ::bind(lfd_, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
        ::listen(lfd_, 4);
        socklen_t len = sizeof sa;
        ::getsockname(lfd_, reinterpret_cast<sockaddr*>(&sa), &len);
        port_ = ntohs(sa.sin_port);
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }
    ~StubNode() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        if (cfd_ >= 0) ::close(cfd_);
        ::close(lfd_);
    }

    uint16_t port() const { return port_; }
    uint32_t addr() const { return addr_; }
    std::atomic<int> reject_next{0};
    std::atomic<int> reject_reason{3};
    std::atomic<bool> drop_client{false};

    // What the node saw, for checks.
    std::mutex mu;
    std::vector<s5066::Prim> requests;
    std::vector<s5066::Prim> binds;
    int keepalive_replies = 0;

    bool bound_on(uint8_t sap) {
        std::lock_guard<std::mutex> g(mu);
        return cfd_ >= 0 && bound_ && sap_ == sap;
    }
    void to_client(const std::string& bytes) {
        std::lock_guard<std::mutex> g(wmu_);
        if (cfd_ >= 0) (void)::send(cfd_, bytes.data(), bytes.size(), MSG_NOSIGNAL);
    }

private:
    void run();
    void on_prim(const s5066::Prim& p);

    StubNet& net_;
    uint32_t addr_;
    uint16_t mtu_;
    uint16_t port_ = 0;
    int lfd_ = -1;
    int cfd_ = -1;
    bool bound_ = false;
    uint8_t sap_ = 0;
    std::mutex wmu_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

class StubNet {
public:
    std::vector<StubNode*> nodes;
    void route(StubNode& from, const s5066::Prim& req) {
        if (req.dst == s5066::kBroadcastAddress) {
            for (auto* n : nodes)
                if (n != &from && n->bound_on(req.sap))
                    n->to_client(s5066::unidata_indication(req.priority, req.sap, req.dst, req.tx_mode, req.sap,
                                                           from.addr(), req.data));
            return;  // non-ARQ: no confirm
        }
        StubNode* to = nullptr;
        for (auto* n : nodes)
            if (n->addr() == req.dst) to = n;
        if (!to) {
            from.to_client(s5066::unidata_rejected(3, req.sap, req.dst, req.data.substr(0, 8)));
            return;
        }
        if (!to->bound_on(req.sap)) {
            from.to_client(s5066::unidata_rejected(2, req.sap, req.dst, req.data.substr(0, 8)));
            return;
        }
        to->to_client(
            s5066::unidata_indication(req.priority, req.sap, req.dst, req.tx_mode, req.sap, from.addr(), req.data));
        // Non-ARQ gets no confirm, as on greybeard-ale's s5066-node.
        if (req.service.tx_mode == s5066::kArq)
            from.to_client(s5066::unidata_confirm(req.sap, req.dst, req.data.substr(0, 8)));
    }
};

void StubNode::run() {
    s5066::Reader reader;
    while (running_) {
        if (cfd_ < 0) {
            pollfd p{lfd_, POLLIN, 0};
            if (::poll(&p, 1, 50) > 0) {
                int c = ::accept(lfd_, nullptr, nullptr);
                std::lock_guard<std::mutex> g(mu);
                cfd_ = c;
                bound_ = false;
                reader = s5066::Reader();
            }
            continue;
        }
        if (drop_client.exchange(false)) {
            std::lock_guard<std::mutex> g(mu);
            ::close(cfd_);
            cfd_ = -1;
            bound_ = false;
            continue;
        }
        pollfd p{cfd_, POLLIN, 0};
        if (::poll(&p, 1, 50) <= 0) continue;
        char buf[4096];
        ssize_t n = ::recv(cfd_, buf, sizeof buf, 0);
        if (n <= 0) {
            std::lock_guard<std::mutex> g(mu);
            ::close(cfd_);
            cfd_ = -1;
            bound_ = false;
            continue;
        }
        for (auto& raw : reader.feed(std::string(buf, static_cast<size_t>(n)))) {
            s5066::Prim pr;
            if (s5066::parse(raw, pr)) on_prim(pr);
        }
    }
}

void StubNode::on_prim(const s5066::Prim& p) {
    switch (p.type) {
    case s5066::kBindRequest:
        {
            std::lock_guard<std::mutex> g(mu);
            binds.push_back(p);
            bound_ = true;
            sap_ = p.sap;
        }
        to_client(s5066::bind_accepted(p.sap, mtu_));
        break;
    case s5066::kKeepAlive: {
        std::lock_guard<std::mutex> g(mu);
        ++keepalive_replies;
        break;
    }
    case s5066::kUnidataRequest:
        {
            std::lock_guard<std::mutex> g(mu);
            requests.push_back(p);
        }
        if (reject_next > 0) {
            --reject_next;
            to_client(s5066::unidata_rejected(static_cast<uint8_t>(reject_reason.load()), p.sap, p.dst, ""));
            break;
        }
        if (p.data.size() > mtu_) {
            to_client(s5066::unidata_rejected(4, p.sap, p.dst, ""));
            break;
        }
        net_.route(*this, p);
        break;
    default:
        break;
    }
}

static std::string state_of(Core& c, const std::string& id) {
    auto m = c.get(id);
    return m ? m->state : "";
}

int main() {
    // ---- Wire form -------------------------------------------------------
    {
        s5066::ServiceType st;  // ARQ, node confirm, in order
        // Bind on SAP 3, rank 0: preamble, version 0, size 4, then
        // type 1, SAP|rank 0x30, service 0x14 (ARQ<<4 | node confirm<<2), retries 0.
        CHECK(hex(s5066::bind_request(3, 0, st)) == "90EB00000401301400");
        CHECK(hex(s5066::bind_accepted(3, 2048)) == "90EB000004033008" "00");
        CHECK(hex(s5066::keep_alive()) == "90EB00000111");

        // Unidata request to 0.0.1.2, priority 10, SAP 3, TTL 600 s (field 300).
        std::string u = s5066::unidata_request(10, 3, 0x00000102, st, 600, "HI");
        // type 20, A3, address E0 00 01 02, service 14, retries|ttl-hi 00, ttl 012C, size 0002, data
        CHECK(hex(u) == "90EB00000E" "14" "A3" "E0000102" "14" "00" "012C" "0002" "4849");
        s5066::Reader r;
        // Fed a byte at a time, with junk ahead of it.
        std::vector<std::string> got;
        std::string stream = std::string("\x01\x90\x00junk", 7) + u + s5066::keep_alive();
        for (char c : stream)
            for (auto& p : r.feed(std::string(1, c))) got.push_back(p);
        CHECK(got.size() == 2);
        s5066::Prim p;
        CHECK(got.size() == 2 && s5066::parse(got[0], p));
        CHECK(p.type == s5066::kUnidataRequest && p.priority == 10 && p.sap == 3 && p.dst == 0x102 &&
              p.service.tx_mode == s5066::kArq && p.service.confirm == s5066::kNodeConfirm && p.ttl_seconds == 600 &&
              p.data == "HI");
        CHECK(got.size() == 2 && s5066::parse(got[1], p) && p.type == s5066::kKeepAlive);
        CHECK(r.skipped() == 7);

        // Indication, and the older layout with two block counts before the data.
        std::string ind = s5066::unidata_indication(5, 3, 0x102, s5066::kArq, 3, 0x0A0B0C0D, "DATA");
        r = s5066::Reader();
        auto v = r.feed(ind);
        CHECK(v.size() == 1 && s5066::parse(v[0], p) && p.type == s5066::kUnidataIndication && p.src == 0x0A0B0C0D &&
              p.src_sap == 3 && p.data == "DATA");
        std::string old = v[0];
        old.insert(13, std::string(4, '\0'));
        CHECK(s5066::parse(old, p) && p.data == "DATA");

        // Rejected: reason in the top nibble.
        v = s5066::Reader().feed(s5066::unidata_rejected(2, 3, 0x102, "ab"));
        CHECK(v.size() == 1 && s5066::parse(v[0], p) && p.reason == 2 && p.sap == 3 && p.data == "ab");
        CHECK(s5066::unidata_rejected_text(4) == "larger than the node's MTU");

        // Bad version after a sync is skipped, the next real sync read.
        v = s5066::Reader().feed(std::string("\x90\xEB\x07", 3) + s5066::keep_alive());
        CHECK(v.size() == 1);
    }

    // ---- Addresses and the peers file ---------------------------------------
    {
        uint32_t a = 0;
        CHECK(s5066::parse_address("0.0.1.2", a) && a == 0x102);
        CHECK(s5066::parse_address("15.255.255.254", a) && s5066::format_address(a) == "15.255.255.254");
        CHECK(!s5066::parse_address("16.0.0.1", a));  // past 28 bits
        CHECK(!s5066::parse_address("1.2.3", a));
        CHECK(!s5066::parse_address("1.2.3.4.5", a));
        CHECK(!s5066::parse_address("1.2.3.256", a));
        CHECK(!s5066::parse_address(" 1.2.3.4", a));

        fs::path pf = fs::temp_directory_path() / ("pna_peers_" + std::to_string(::getpid()));
        {
            std::ofstream f(pf);
            f << "# made-up stations\nsta-b 0.0.0.2\n  NCS-A   0.0.0.1   # net control\n\n";
        }
        std::map<std::string, uint32_t> m;
        std::string err;
        CHECK(s5066::read_peers(pf.string(), m, &err) && m.size() == 2 && m["STA-B"] == 2 && m["NCS-A"] == 1);
        {
            std::ofstream f(pf);
            f << "STA-B 0.0.0.2 extra\n";
        }
        CHECK(!s5066::read_peers(pf.string(), m, &err) && err.find("line 1") != std::string::npos);
        {
            std::ofstream f(pf);
            f << "STA-B 15.255.255.255\n";  // the broadcast address isn't a station
        }
        CHECK(!s5066::read_peers(pf.string(), m, &err));
        fs::remove(pf);
    }

    // ---- Pieces ----------------------------------------------------------
    {
        std::string data(6000, 'x');
        for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>('A' + i % 26);
        auto ps = s5066::split(data, 7, 2048);
        CHECK(ps.size() == 3);
        for (auto& p : ps) CHECK(p.size() <= 2048);
        s5066::Joiner j;
        std::vector<std::string> out;
        // Two sources interleaved still join.
        auto other = s5066::split(std::string(3000, 'q'), 1, 2048);
        for (size_t i = 0; i < 3; ++i) {
            for (auto& w : j.add(1, ps[i])) out.push_back(w);
            if (i < other.size())
                for (auto& w : j.add(2, other[i])) out.push_back(w);
        }
        CHECK(out.size() == 2);
        CHECK(out.size() == 2 && out[0] == std::string(3000, 'q') && out[1] == data);
        // A missing middle piece loses the frame, and is counted.
        out.clear();
        auto again = s5066::split(data, 8, 2048);
        j.add(1, again[0]);
        for (auto& w : j.add(1, again[2])) out.push_back(w);
        CHECK(out.empty() && j.dropped() == 1);
        // A U_PDU without our header passes through as it came.
        auto raw = j.add(3, "RAW");
        CHECK(raw.size() == 1 && raw[0] == "RAW");
        CHECK(s5066::split(data, 1, 7).empty());           // no room for data
        CHECK(s5066::split(std::string(300 * 255, 'z'), 1, 300).empty());  // too many pieces
        CHECK(s5066::priority_for('Z') > s5066::priority_for('O') && s5066::priority_for('O') > s5066::priority_for('P') &&
              s5066::priority_for('P') > s5066::priority_for('R'));
    }

    // ---- Two cores through the stand-in node ------------------------------
    fs::path dir = fs::temp_directory_path() / ("pna_s5066_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "peers");
        f << "STA-A 0.0.0.1\nSTA-B 0.0.0.2\n";
    }
    {
        StubNet net;
        // 2048: what greybeard-ale's node advertises in S_BIND_ACCEPTED.
        StubNode na(net, 1, 2048), nb(net, 2, 2048);
        net.nodes = {&na, &nb};

        auto make = [&](uint16_t port) {
            S5066Config c;
            c.port = port;
            c.peers_file = (dir / "peers").string();
            c.reconnect_ms = 200;
            c.poll_ms = 20;
            c.ttl_seconds = 60;
            return std::make_unique<S5066Client>(c);
        };
        auto ca = make(na.port());
        auto cb = make(nb.port());
        S5066Client* pa = ca.get();
        Core a(CoreConfig{"STA-A", (dir / "a.db").string(), 3, 1, true}, std::move(ca));
        Core b(CoreConfig{"STA-B", (dir / "b.db").string(), 3, 1, true}, std::move(cb));
        a.start();
        b.start();

        CHECK(wait_for([&] { return pa->ready() && na.bound_on(3) && nb.bound_on(3); }));
        CHECK(pa->mtu() == 2048 && pa->peers() == 2);
        {
            std::lock_guard<std::mutex> g(na.mu);
            CHECK(na.binds.size() == 1 && na.binds[0].sap == 3 && na.binds[0].service.tx_mode == s5066::kArq);
        }
        CHECK(pa->describe().find("SAP 3, MTU 2048") != std::string::npos);

        // A message and its ACK, both through 5066.
        std::string err;
        std::string id = a.submit("STA-B", 'P', "HELLO STA-B THIS IS STA-A", &err);
        CHECK(!id.empty());
        CHECK(wait_for([&] { return state_of(a, id) == "acked"; }));
        auto rx = b.get(id);
        CHECK(rx && rx->body == "HELLO STA-B THIS IS STA-A" && rx->src == "STA-A");
        {
            std::lock_guard<std::mutex> g(na.mu);
            CHECK(!na.requests.empty() && na.requests[0].priority == s5066::priority_for('P') &&
                  na.requests[0].dst == 2 && na.requests[0].ttl_seconds == 60 &&
                  na.requests[0].service.tx_mode == s5066::kArq);
        }

        // A 4 KB message goes as two pieces, each within the node's MTU, and arrives whole.
        std::string big;
        for (int i = 0; i < 100; ++i) big += "LINE " + std::to_string(i) + " OF A LONG MESSAGE FOR THE NET\n";
        size_t before;
        {
            std::lock_guard<std::mutex> g(na.mu);
            before = na.requests.size();
        }
        std::string bid = a.submit("STA-B", 'R', big, &err);
        CHECK(!bid.empty());
        CHECK(wait_for([&] { return state_of(a, bid) == "acked"; }));
        CHECK(b.get(bid) && b.get(bid)->body == big);
        {
            std::lock_guard<std::mutex> g(na.mu);
            CHECK(big.size() > 3 * 1024 && na.requests.size() - before == 2);
            for (size_t i = before; i < na.requests.size(); ++i) CHECK(na.requests[i].data.size() <= 2048);
        }

        // The node refuses once: the core retries and it goes.
        na.reject_next = 1;
        std::string rid = a.submit("STA-B", 'R', "AFTER ONE REFUSAL", &err);
        CHECK(wait_for([&] { return state_of(a, rid) == "acked"; }));
        CHECK(a.get(rid) && a.get(rid)->attempts == 2);

        // A station with no 5066 address fails, with the reason in the log.
        std::string nid = a.submit("STA-C", 'R', "NOBODY KNOWS STA-C", &err);
        CHECK(!nid.empty());
        CHECK(wait_for([&] { return state_of(a, nid) == "failed"; }, 15000));

        // The peers file is read again when it changes.
        {
            std::ofstream f(dir / "peers");
            f << "STA-A 0.0.0.1\nSTA-B 0.0.0.2\nSTA-C 0.0.0.3\n";
        }
        std::string cid = a.submit("STA-C", 'R', "STA-C NOW HAS AN ADDRESS BUT NO NODE", &err);
        CHECK(wait_for([&] { return state_of(a, cid) == "failed"; }, 15000));
        CHECK(pa->peers() == 3);

        // Broadcast goes non-ARQ, one copy to each station in the peers file
        // but this one. STA-C has no node, so its copy is refused; STA-B's
        // gets through, and the broadcast counts as sent.
        {
            std::lock_guard<std::mutex> g(na.mu);
            before = na.requests.size();
        }
        std::string all = a.submit("ALL", 'R', "QST NET MEETS AT 2100Z", &err);
        CHECK(!all.empty());
        CHECK(wait_for([&] { return b.get(all) && b.get(all)->body == "QST NET MEETS AT 2100Z"; }));
        CHECK(wait_for([&] { return state_of(a, all) == "sent"; }));
        {
            std::lock_guard<std::mutex> g(na.mu);
            CHECK(na.requests.size() == before + 2);
            std::set<uint32_t> to;
            for (size_t i = before; i < na.requests.size(); ++i) {
                auto& r = na.requests[i];
                CHECK(r.service.tx_mode == s5066::kNonArqBroadcast && r.service.confirm == s5066::kNoConfirm);
                to.insert(r.dst);
            }
            CHECK(to == (std::set<uint32_t>{2, 3}));
        }
        CHECK(wait_for([&] {
            for (auto& e : a.events(0, 500))
                if (e.text.find("refused the copy for 0.0.0.3") != std::string::npos) return true;
            return false;
        }));

        // The node answers keep-alive with keep-alive... and so does the client.
        na.to_client(s5066::keep_alive());
        CHECK(wait_for([&] {
            std::lock_guard<std::mutex> g(na.mu);
            return na.keepalive_replies == 1;
        }));

        // Data flow off holds sends; on lets them go.
        na.to_client(s5066::dataflow(false));
        CHECK(wait_for([&] { return pa->busy(); }));
        std::string held = a.submit("STA-B", 'R', "HELD BY FLOW CONTROL", &err);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(state_of(a, held) == "queued");
        na.to_client(s5066::dataflow(true));
        CHECK(wait_for([&] { return state_of(a, held) == "acked"; }));

        // The node drops the connection: the client binds again and traffic flows.
        na.drop_client = true;
        CHECK(wait_for([&] { return !pa->ready(); }, 3000));
        CHECK(wait_for([&] { return pa->ready(); }));
        {
            std::lock_guard<std::mutex> g(na.mu);
            CHECK(na.binds.size() == 2);
        }
        std::string after = a.submit("STA-B", 'R', "AFTER RECONNECT", &err);
        CHECK(wait_for([&] { return state_of(a, after) == "acked"; }));

        // An unbind from the node is the same as a lost connection.
        na.to_client(s5066::unbind_indication(1));
        CHECK(wait_for([&] { return pa->ready() && [&] {
                                       std::lock_guard<std::mutex> g(na.mu);
                                       return na.binds.size() == 3;
                                   }(); }));

        a.stop();
        b.stop();
    }

    // No node at all: the client is not ready and the core holds its traffic.
    {
        S5066Config c;
        c.port = 1;  // nothing listens there
        c.reconnect_ms = 100;
        S5066Client cl(c);
        std::vector<std::string> events;
        std::mutex emu;
        ModemDriver::Callbacks cbs;
        cbs.on_event = [&](const std::string& e) {
            std::lock_guard<std::mutex> g(emu);
            events.push_back(e);
        };
        cl.start(cbs);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(!cl.ready());
        CHECK(!cl.send(encode(Frame{FrameType::Msg, "X-1", "STA-A", "STA-B", 'R', "X"})));
        {
            std::lock_guard<std::mutex> g(emu);
            CHECK(events.size() == 1 && events[0].find("not reachable") != std::string::npos);
        }
        cl.stop();
    }

    fs::remove_all(dir);
    return check::report("test_s5066");
}
