// link = controller (docs/api.md, "Keying" and "Transmit"; decision D11, "lean
// SIS"): the core hands each frame to greybeard-ale's station controller over
// STANAG 5066 SIS, one Unix socket per service, against a fake controller. The
// fake speaks SIS as Annex A v1.2 encodes it (A.2.2) on sis-<service>.sock, and
// the controller's local JSON socket for status, grant and revoke. Every call
// and channel label is made up; no frequency appears.
#include "../src/api_server.h"
#include "../src/bbs.h"
#include "../src/core.h"
#include "../src/gale_key.h"
#include "../src/greybeard_modem.h"
#include "../src/json.h"
#include "../src/ops.h"
#include "../src/s5066.h"
#include "../src/s5066_client.h"
#include "../src/sha256.h"
#include "check.h"
#include "services_fixture.h"

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

using namespace pna;
namespace fs = std::filesystem;

static bool wait_for(const std::function<bool()>& cond, int ms) {
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}
static std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.is_object() ? v.get(k) : nullptr;
    return x && x->type == JsonValue::Type::String ? x->s : std::string();
}
static bool flag(const JsonValue& v, const char* k) {
    const JsonValue* x = v.is_object() ? v.get(k) : nullptr;
    return x && x->type == JsonValue::Type::Bool && x->b;
}
static JsonValue sub(const JsonValue& v, const char* k) {
    const JsonValue* x = v.is_object() ? v.get(k) : nullptr;
    return x ? *x : JsonValue();
}
static JsonValue parsed(const std::string& s) {
    JsonValue v;
    parse_json(s, v);
    return v;
}
static bool ok(const std::string& reply) { return flag(parsed(reply), "ok"); }
static bool has(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }
static std::string hex(const std::string& b) {
    static const char* h = "0123456789abcdef";
    std::string o;
    for (unsigned char c : b) {
        o += h[c >> 4];
        o += h[c & 15];
    }
    return o;
}

static int listen_on(const fs::path& p) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, p.c_str(), sizeof a.sun_path - 1);
    ::unlink(p.c_str());
    CHECK(p.string().size() < sizeof a.sun_path);
    CHECK(::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
    CHECK(::listen(fd, 8) == 0);
    return fd;
}

// ---------------------------------------------------------------- the fake controller

struct FakeController {
    struct Bind {
        std::string service;
        uint8_t sap = 0, tx_mode = 0, confirm = 0;
    };
    struct Req {
        std::string service;
        uint8_t sap = 0;  // the connection's bound SAP
        s5066::Prim p;
        int fd = -1;
        bool answered = false;
    };
    fs::path dir;
    std::map<std::string, std::string> send_channel{{"mars", "P1"}, {"amateur", "AMX"}};
    std::string mode = "confirm";  // confirm, reject, hold, drop
    std::string radio_channel = "P1", radio_service = "mars";  // where the transmitting radio is
    std::string reject_why = "refused: channel P1: passband outside the data segments of service mars";
    std::mutex mu;
    std::vector<Bind> binds;
    int unbound_sends = 0;
    std::vector<Req> reqs;
    std::vector<JsonValue> json;  // every local request
    std::vector<std::string> last;  // status.last entries, JSON text, newest first
    std::atomic<bool> run{false};
    std::vector<int> lfds;
    std::vector<std::string> lnames;  // the service of each SIS listener ("" the local socket)
    std::vector<std::thread> threads;
    std::vector<int> conns;

    void start(const std::vector<std::string>& services) {
        run = true;
        for (auto& s : services) {
            lfds.push_back(listen_on(dir / ("sis-" + s + ".sock")));
            lnames.push_back(s);
        }
        lfds.push_back(listen_on(dir / "control.sock"));
        lnames.push_back("");
        threads.emplace_back([this] { accept_loop(); });
    }
    void stop() {
        if (!run.exchange(false)) return;
        for (auto& t : threads) t.join();
        for (int f : lfds) ::close(f);
        std::lock_guard<std::mutex> g(mu);
        for (int c : conns) ::close(c);
    }
    ~FakeController() { stop(); }

    void accept_loop() {
        std::vector<std::thread> mine;
        while (run) {
            fd_set r;
            FD_ZERO(&r);
            int mx = 0;
            for (int f : lfds) {
                FD_SET(f, &r);
                mx = std::max(mx, f);
            }
            timeval tv{0, 50000};
            if (::select(mx + 1, &r, nullptr, nullptr, &tv) <= 0) continue;
            for (size_t i = 0; i < lfds.size(); ++i) {
                if (!FD_ISSET(lfds[i], &r)) continue;
                int c = ::accept(lfds[i], nullptr, nullptr);
                if (c < 0) continue;
                if (lnames[i].empty()) {
                    local(c);
                    continue;
                }
                {
                    std::lock_guard<std::mutex> g(mu);
                    conns.push_back(c);
                }
                mine.emplace_back([this, c, s = lnames[i]] { sis(c, s); });
            }
        }
        for (auto& t : mine) t.join();
    }

    std::string status_text() {
        JsonWriter w;
        w.begin_object().field("ok", true);
        // As greybeard-ale tools/gale_rx.py req_status: the transmitting radio,
        // and a receive-only one driven by the scanner.
        w.begin_array("radios")
            .begin_object()
            .field("radio", "rsp2")
            .field("transmit", false)
            .field_null("radioChannel")
            .field_null("service")
            .field("radioKeyed", false)
            .field_null("claim")
            .field("driver", "ale-scan")
            .end_object()
            .begin_object()
            .field("radio", "ft897d")
            .field("transmit", true)
            .field("radioChannel", radio_channel)
            .field("service", radio_service)
            .field("since", "2026-11-27T10:00:00Z")
            .field("source", "schedule")
            .field("radioKeyed", false)
            .field_null("claim")
            .end_object()
            .end_array();
        w.begin_object("send_channel");
        for (auto& [k, v] : send_channel)
            if (v.empty()) w.field_null(k);
            else w.field(k, v);
        w.end_object().begin_array("waiting");
        for (auto& [k, v] : send_channel)
            if (v.empty())
                w.begin_object().field("service", k).field("why", "no channel for service " + k + " (no park)").end_object();
        w.end_array().end_object();
        std::string out = w.str();
        out.pop_back();  // the closing brace: "last" goes in by hand
        out += ",\"last\":[";
        for (size_t i = 0; i < last.size(); ++i) out += (i ? "," : "") + last[i];
        out += "]}";
        return out;
    }

    void local(int c) {
        std::string buf;
        char ch[4096];
        while (buf.find('\n') == std::string::npos) {
            ssize_t k = ::recv(c, ch, sizeof ch, 0);
            if (k <= 0) break;
            buf.append(ch, static_cast<size_t>(k));
        }
        JsonValue req;
        parse_json(buf.substr(0, buf.find('\n')), req);
        std::string reply;
        {
            std::lock_guard<std::mutex> g(mu);
            json.push_back(req);
            const std::string cmd = str(req, "cmd");
            if (cmd == "status") reply = status_text();
            else if (cmd == "prepare" && str(req, "kind").rfind("standing:", 0) == 0) {
                // Stays local: gale-key's standing prepare, passed on with its token.
                JsonWriter w;
                w.begin_object()
                    .field("ok", true)
                    .field("token", "TOK-STANDING-1")
                    .field("expires_s", 120)
                    .begin_object("confirm")
                    .field("service", str(req, "service"))
                    .end_object()
                    .end_object();
                reply = w.str();
            } else if (cmd == "prepare") reply = R"({"ok":false,"reason":"prepare is for standing:MODE only"})";
            else if (cmd == "grant")
                reply = str(req, "token") == "TOK-STANDING-1" ? R"({"ok":true})"
                                                              : R"({"ok":false,"reason":"grant needs the standing prepare's token"})";
            else if (cmd == "revoke") reply = R"({"ok":true})";
            else reply = R"({"ok":false,"reason":"not a request the core sends"})";
        }
        reply += "\n";
        (void)::send(c, reply.data(), reply.size(), MSG_NOSIGNAL);
        ::close(c);
    }

    // Writes the answer for one request, after putting it in status.last.
    void answer(Req& r, bool confirm, uint8_t reason = 0) {
        const std::string sha = sha256_hex(r.p.data);
        JsonWriter e;
        e.begin_object()
            .field("send", "K-1")
            .field("sha256", sha)
            .field("service", r.service)
            .field("channel", send_channel[r.service])
            .field("result", confirm ? "keyed" : "refused")
            .field("why", confirm ? std::string() : reject_why)
            .end_object();
        last.insert(last.begin(), e.str());
        // As the controller's SIS server: a copy of the U_PDU, its first 2048 bytes.
        const std::string part = r.p.data.substr(0, 2048);
        const std::string out = confirm ? s5066::unidata_confirm(r.sap, s5066::kBroadcastAddress, part)
                                        : s5066::unidata_rejected(reason, r.sap, s5066::kBroadcastAddress, part);
        (void)::send(r.fd, out.data(), out.size(), MSG_NOSIGNAL);
        r.answered = true;
    }
    // Answers the oldest request still waiting.
    bool answer_one(bool confirm) {
        std::lock_guard<std::mutex> g(mu);
        for (auto& r : reqs)
            if (!r.answered) {
                answer(r, confirm);
                return true;
            }
        return false;
    }

    void sis(int c, const std::string& service) {
        s5066::Reader rd;
        uint8_t bound = 0;
        char ch[8192];
        while (run) {
            fd_set r;
            FD_ZERO(&r);
            FD_SET(c, &r);
            timeval tv{0, 50000};
            if (::select(c + 1, &r, nullptr, nullptr, &tv) <= 0) continue;
            ssize_t k = ::recv(c, ch, sizeof ch, 0);
            if (k <= 0) return;
            for (auto& raw : rd.feed(std::string(ch, static_cast<size_t>(k)))) {
                s5066::Prim p;
                if (!s5066::parse(raw, p)) continue;
                std::lock_guard<std::mutex> g(mu);
                if (p.type == s5066::kBindRequest) {
                    binds.push_back(Bind{service, p.sap, p.service.tx_mode, p.service.confirm});
                    std::string out;
                    if (bound) out = s5066::bind_rejected(0);  // a second bind on a bound connection
                    else if (p.sap != 13 && p.sap != 14) out = s5066::bind_rejected(2);  // A.2.1.4: invalid SAP
                    else {
                        bound = p.sap;
                        out = s5066::bind_accepted(p.sap, 2048);
                    }
                    (void)::send(c, out.data(), out.size(), MSG_NOSIGNAL);
                } else if (p.type == s5066::kUnidataRequest) {
                    reqs.push_back(Req{service, bound, p, c, false});
                    if (!bound) {
                        ++unbound_sends;  // a U_PDU before any bind: rejected, reason 0
                        const std::string out = s5066::unidata_rejected(0, p.sap, p.dst, p.data.substr(0, 2048));
                        (void)::send(c, out.data(), out.size(), MSG_NOSIGNAL);
                        reqs.back().answered = true;
                    } else if (mode == "confirm") answer(reqs.back(), true);
                    else if (mode == "reject") answer(reqs.back(), false, 0);
                    else if (mode == "drop") {
                        ::shutdown(c, SHUT_RDWR);
                        return;
                    }
                }
            }
        }
    }

    size_t count_json(const std::string& cmd) {
        std::lock_guard<std::mutex> g(mu);
        size_t n = 0;
        for (auto& j : json)
            if (str(j, "cmd") == cmd) ++n;
        return n;
    }
    size_t count_reqs() {
        std::lock_guard<std::mutex> g(mu);
        return reqs.size();
    }
    Req req(size_t i) {
        std::lock_guard<std::mutex> g(mu);
        return i < reqs.size() ? reqs[i] : Req{};
    }
    bool bound(const std::string& service, uint8_t sap) {
        std::lock_guard<std::mutex> g(mu);
        for (auto& b : binds)
            if (b.service == service && b.sap == sap) return true;
        return false;
    }
};

// ---------------------------------------------------------------- a station

static std::string call(Core& c, const std::vector<std::pair<std::string, std::string>>& kv) {
    JsonWriter w;
    w.begin_object();
    for (auto& [k, v] : kv) w.field(k, v);
    w.end_object();
    return ApiServer::handle(c, w.str());
}

struct Station {
    fs::path dir;
    GreybeardConfig gc;
    std::unique_ptr<Core> core;
    std::shared_ptr<GaleKey> gk;
    std::shared_ptr<SisControllerLink> link;
    std::atomic<double> now{1000.0};

    Station(const fs::path& d, const fs::path& sock_dir) : dir(d) {
        fs::create_directories(dir);
        gc.frames_only = true;
        gc.captures_dir = (dir / "cap").string();
        gc.work_dir = (dir / "work").string();
        gc.outbox_dir = (dir / "outbox").string();
        gc.state_file = (dir / "seen.txt").string();
        gc.poll_ms = 50;
        CoreConfig cc{"AAA", (dir / "AAA.db").string(), 3, 1, true};
        cc.ack_timeout_s = 600;
        core = std::make_unique<Core>(cc, std::make_unique<GreybeardModem>(gc));
        core->set_services(test_services());
        core->set_ops(std::make_shared<Ops>("AAA", cc.db_path));
        // The channel plan's labels, in their services (made-up dials).
        for (auto& [label, khz, svc] : std::vector<std::tuple<std::string, std::string, std::string>>{
                 {"P1", "5000", "mars"}, {"P2", "5300", "mars"}, {"AMX", "15000", "amateur"}})
            CHECK(ok(call(*core, {{"cmd", "channel_put"}, {"name", label}, {"frequency", khz}, {"service", svc}})));
        SisLinkConfig sc;
        sc.dir = sock_dir.string();
        sc.reconnect_ms = 200;
        sc.ttl_seconds = 4;
        sc.answer_grace_s = 1;
        link = std::make_shared<SisControllerLink>(sc);
        // As main.cpp: each service that transmits; SAP 14 for MARS only until stage 5.
        for (const Service* x : test_services()->all())
            if (x->transmit) {
                link->add(x->name, false);
                if (x->name == "mars") link->add(x->name, true);
            }
        gk = std::make_shared<GaleKey>(std::make_shared<GaleKeySocket>((sock_dir / "control.sock").string(), 2000),
                                       gc.outbox_dir);
        gk->set_clock([this] { return now.load(); });
        gk->set_sis(link);
        core->set_gale_key(gk);
        core->start();
    }
    ~Station() {
        bbs::stop(*core);
        core->stop();
    }
    std::string state(const std::string& id) {
        auto m = core->get(id);
        return m ? m->state : std::string();
    }
    std::string written(const std::string& to, const std::string& body, const std::string& service = "mars") {
        std::string err;
        std::string id = core->submit(to, 'O', body, &err, SendOpts::in(service));
        CHECK(!id.empty());
        if (id.empty()) std::cerr << "  not queued: " << err << "\n";
        CHECK(wait_for([&] { return state(id) == "written"; }, 10000));
        return id;
    }
    std::string events_text() {
        std::string all;
        for (auto& e : core->events(0, 5000)) all += e.text + "\n";
        return all;
    }
    // Opens the y and answers it: the reply of key_confirm.
    JsonValue y(const std::string& id, std::string* prepare_reply = nullptr) {
        std::string rep = call(*core, {{"cmd", "key_prepare"}, {"msg_id", id}});
        if (prepare_reply) *prepare_reply = rep;
        JsonValue v = parsed(rep);
        if (!flag(v, "ok")) return v;
        now = now + 2;
        return parsed(call(*core, {{"cmd", "key_confirm"}, {"prepare_id", str(v, "prepare_id")}, {"confirm", "Y"}}));
    }
};

int main() {
    // A short directory: a Unix socket path holds at most 107 bytes.
    const fs::path base = fs::temp_directory_path() / ("sis" + std::to_string(::getpid()));
    fs::remove_all(base);
    fs::create_directories(base);

    // ---- the codec against Annex A v1.2, worked by hand from its figures
    {
        s5066::ServiceType st;
        st.tx_mode = s5066::kNonArqBroadcast;
        st.confirm = s5066::kNoConfirm;
        // A.2.2.2 preamble 0x90 0xEB, version 0, size 4; A.2.2.3 type 1, SAP 13
        // rank 0, service type 0010 00 0 0, retransmissions 0.
        CHECK(hex(s5066::bind_request(13, 0, st)) == "90eb00000401d02000");
        // A.2.2.20: type 20, priority 15 | SAP 13, the all-stations group
        // address (A.2.2.28.1: size 7, group 1, 28 bits of 1), delivery mode
        // 0x20, retransmissions 0 | TTL 600 s in 2 s units (300), size 2, "AB".
        CHECK(hex(s5066::unidata_request(15, 13, s5066::kBroadcastAddress, st, 600, "AB")) ==
              "90eb00000e" "14" "fd" "ffffffff" "20" "00012c" "0002" "4142");
        // A.2.2.22 and A.2.2.23: reason in the top nibble beside the SAP.
        s5066::Prim p;
        CHECK(s5066::parse(s5066::unidata_rejected(4, 13, s5066::kBroadcastAddress, "AB").substr(5), p));
        CHECK(p.type == s5066::kUnidataRequestRejected && p.reason == 4 && p.sap == 13 && p.data == "AB");
        CHECK(kSisMtu == 2048);
    }

    // ---- bind SAP 13 on every service that transmits, SAP 14 on MARS only
    {
        FakeController f;
        f.dir = base;
        f.start({"mars", "amateur"});
        Station s(base / "A", base);
        CHECK(wait_for([&] { return s.link->bound("mars", false) && s.link->bound("mars", true) &&
                                    s.link->bound("amateur", false); }, 5000));
        CHECK(f.bound("mars", 13) && f.bound("mars", 14) && f.bound("amateur", 13));
        CHECK(!f.bound("amateur", 14) && !s.link->has("amateur", true));
        {
            std::lock_guard<std::mutex> g(f.mu);
            for (auto& b : f.binds) CHECK(b.tx_mode == s5066::kNonArqBroadcast && b.confirm == s5066::kNoConfirm);
        }

        // ---- radio_channel from the real status shape: the transmitting radio's entry in radios[]
        s.gk->refresh_radio();
        JsonValue rc = sub(parsed(call(*s.core, {{"cmd", "status"}})), "radio_channel");
        CHECK(str(rc, "label") == "P1" && str(rc, "service") == "mars" && str(rc, "since") == "2026-11-27T10:00:00Z");

        // ---- the operator's y: send_channel shown, SAP 13, CONFIRM -> sent
        std::string prep;
        const std::string id = s.written("FARSTN", "KEY ME ON SIS");
        CHECK(s.core->get(id)->channel.empty());  // the controller chooses it
        JsonValue v = s.y(id, &prep);
        JsonValue cf = sub(parsed(prep), "confirm");
        if (str(cf, "channel") != "P1") std::cout << "  prepare: " << prep << "\n";
        CHECK(str(cf, "kind") == "air" && str(cf, "service") == "mars" && str(cf, "channel") == "P1");
        CHECK(cf.get("id") && !flag(cf, "id"));  // the MARS fixture sends no station ID
        CHECK(flag(v, "ok") && flag(v, "asked") && has(str(v, "note"), "SIS answer"));
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));
        CHECK(f.count_reqs() == 1);
        FakeController::Req r = f.req(0);
        CHECK(r.service == "mars" && r.sap == 13 && r.p.sap == 13);
        CHECK(r.p.dst == s5066::kBroadcastAddress && r.p.dst_group);
        CHECK(r.p.service.tx_mode == s5066::kNonArqBroadcast && r.p.service.confirm == s5066::kNoConfirm);
        CHECK(r.p.priority == 10);  // O
        CHECK(r.p.ttl_seconds == 4);
        CHECK(has(r.p.data, "KEY ME ON SIS"));
        // The Transport ack clock starts at the confirm, and the log names the channel.
        auto m = s.core->get(id);
        CHECK(m && m->sends == 1 && m->next_try_at > 0);
        CHECK(has(s.events_text(), "keyed by the station controller on P1: sent, waiting for ack"));
        // Nothing went to the local socket for a send: no prepare, key or cancel.
        CHECK(f.count_json("prepare") == 0 && f.count_json("key") == 0 && f.count_json("cancel") == 0);
        CHECK(!ok(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})));

        // ---- an amateur message: its own socket, and the y says the ID goes out
        const std::string am = s.written("FARAM", "AMATEUR ON SIS", "amateur");
        v = s.y(am, &prep);
        cf = sub(parsed(prep), "confirm");
        CHECK(str(cf, "service") == "amateur" && str(cf, "channel") == "AMX" && flag(cf, "id"));
        CHECK(wait_for([&] { return s.state(am) == "sent"; }, 5000));
        CHECK(f.count_reqs() == 2 && f.req(1).service == "amateur" && f.req(1).sap == 13);

        // ---- REJECTED -> failed, with the controller's sentence from status.last
        f.mode = "reject";
        const std::string rj = s.written("FARSTN", "REJECT ME");
        v = s.y(rj);
        CHECK(flag(v, "ok") && flag(v, "asked"));
        CHECK(wait_for([&] { return s.state(rj) == "failed"; }, 5000));
        m = s.core->get(rj);
        CHECK(m && has(m->last_error, "not keyed: refused: channel P1: passband outside the data segments"));

        // ---- over 2048 bytes: refused before the y, nothing handed down
        f.mode = "confirm";
        const size_t before = f.count_reqs();
        std::string err;
        const std::string big = s.core->submit("FARSTN", 'R', std::string(3000, 'X'), &err, SendOpts::in("mars"));
        CHECK(!big.empty());
        CHECK(wait_for([&] { return s.state(big) == "failed"; }, 5000));
        m = s.core->get(big);
        CHECK(m && has(m->last_error, "too long for one burst:") && has(m->last_error, "the station controller takes 2048"));
        CHECK(!ok(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", big}})));
        CHECK(f.count_reqs() == before);

        // ---- the channel moves between the y and the submit: re-asked, nothing sent
        const std::string mv = s.written("FARSTN", "CHANNEL MOVES");
        JsonValue p1 = parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", mv}}));
        CHECK(str(sub(p1, "confirm"), "channel") == "P1");
        {
            std::lock_guard<std::mutex> g(f.mu);
            f.send_channel["mars"] = "P2";
        }
        s.now = s.now + 2;
        v = parsed(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", str(p1, "prepare_id")}, {"confirm", "Y"}}));
        CHECK(flag(v, "ok") && v.get("asked") && !flag(v, "asked"));
        CHECK(str(v, "channel") == "P2" && str(v, "channel_was") == "P1" && str(sub(v, "confirm"), "channel") == "P2");
        CHECK(f.count_reqs() == before && s.state(mv) == "written");
        // The old dialog is spent; the new one sends.
        CHECK(!ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", str(p1, "prepare_id")}, {"confirm", "Y"}})));
        s.now = s.now + 2;
        v = parsed(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", str(v, "prepare_id")}, {"confirm", "Y"}}));
        CHECK(flag(v, "ok") && flag(v, "asked"));
        CHECK(wait_for([&] { return s.state(mv) == "sent"; }, 5000));
        CHECK(f.count_reqs() == before + 1);
        CHECK(has(s.events_text(), "on P2"));

        // ---- no send_channel for the service: refused with the controller's why
        {
            std::lock_guard<std::mutex> g(f.mu);
            f.send_channel["mars"] = "";
        }
        const std::string nc = s.written("FARSTN", "NO CHANNEL");
        std::string rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", nc}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "no channel for service mars"));
        {
            std::lock_guard<std::mutex> g(f.mu);
            f.send_channel["mars"] = "P1";
        }

        // ---- the controller chooses the channel: one given is refused
        err.clear();
        CHECK(s.core->submit("FARSTN", 'R', "PICKED", &err, SendOpts::in("mars", "P1")).empty());
        CHECK(has(err, "the station controller chooses the channel"));

        // ---- automatic sends: SAP 14, MARS only
        rep = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "routing"}, {"service", "amateur"}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "MARS only"));
        CHECK(!ok(call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "routing"}})));  // needs service
        JsonValue sp = parsed(call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "routing"}, {"service", "mars"}}));
        CHECK(flag(sp, "ok") && f.count_json("prepare") == 1);
        s.now = s.now + 2;
        v = parsed(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", str(sp, "prepare_id")}, {"confirm", "Y"}}));
        CHECK(flag(v, "ok") && flag(v, "on") && f.count_json("grant") == 1);
        {
            std::lock_guard<std::mutex> g(f.mu);
            for (auto& j : f.json)
                if (str(j, "cmd") == "grant") CHECK(str(j, "service") == "mars" && str(j, "token") == "TOK-STANDING-1");
        }
        const size_t b2 = f.count_reqs();
        err.clear();
        const std::string au = s.core->submit("FARSTN", 'R', "AUTOMATIC", &err, SendOpts::in("mars"));
        CHECK(!au.empty());
        s.core->set_key_mode(au, "routing");
        CHECK(wait_for([&] { return s.state(au) == "sent"; }, 8000));
        CHECK(f.count_reqs() == b2 + 1 && f.req(b2).service == "mars" && f.req(b2).sap == 14);
        // An amateur item under a mode never goes, on any SAP.
        const std::string aa = s.core->submit("FARAM", 'R', "AMATEUR AUTOMATIC", &err, SendOpts::in("amateur"));
        CHECK(!aa.empty());
        s.core->set_key_mode(aa, "routing");
        CHECK(wait_for([&] { return s.state(aa) == "written"; }, 5000));
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        CHECK(f.count_reqs() == b2 + 1 && s.state(aa) == "written");
        CHECK(has(s.events_text(), "MARS only"));

        // ---- no answer within the TTL plus the grace: failed
        f.mode = "hold";
        const std::string ho = s.written("FARSTN", "NO ANSWER");
        v = s.y(ho);
        CHECK(flag(v, "asked"));
        CHECK(wait_for([&] { return s.state(ho) == "failed"; }, 10000));
        CHECK(has(s.core->get(ho)->last_error, "no answer from the station controller within 5 s"));

        // ---- a dropped connection fails what it held, and the link binds again
        f.mode = "drop";
        const std::string dr = s.written("FARSTN", "DROPPED");
        v = s.y(dr);
        CHECK(flag(v, "asked"));
        CHECK(wait_for([&] { return s.state(dr) == "failed"; }, 5000));
        CHECK(has(s.core->get(dr)->last_error, "dropped"));
        f.mode = "confirm";
        CHECK(wait_for([&] { return s.link->bound("mars", false); }, 5000));
        const std::string again = s.written("FARSTN", "AFTER THE DROP");
        CHECK(flag(s.y(again), "asked"));
        CHECK(wait_for([&] { return s.state(again) == "sent"; }, 5000));
        {
            // Each connection bound once, and nothing was sent before a bind.
            std::lock_guard<std::mutex> g(f.mu);
            CHECK(f.unbound_sends == 0);
        }
        f.stop();
    }

    // ---- no controller: nothing is handed down, the message stays written
    {
        const fs::path empty = base / "none";
        fs::create_directories(empty);
        Station s(base / "B", empty);
        const std::string id = s.written("FARSTN", "NOBODY THERE");
        std::string rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "not prepared"));
        CHECK(s.state(id) == "written");
    }

    fs::remove_all(base);
    return check::report("test_sis_controller");
}
