// The core as gale-key's caller (gale_key.h, docs/api.md "Keying: the core
// asks gale-key"), against a fake gale-key on a Unix socket. The fake answers
// as greybeard-ale's docs/gale-tx.md says (prepare, key, grant, revoke,
// status) and, for every key whose WAV is in the outbox, writes api.md's
// BURST.key.json result, keyed or refused. The core never keys anything here.
// A fake tuner daemon shows the tuner's sequence around each of those keys.
#include "../src/api_server.h"
#include "../src/bbs.h"
#include "../src/core.h"
#include "../src/frame.h"
#include "../src/gale_key.h"
#include "../src/greybeard_modem.h"
#include "../src/json.h"
#include "../src/ops.h"
#include "../src/sha256.h"
#include "../src/tuner.h"
#include "check.h"

#include <sqlite3.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

using namespace pna;
namespace fs = std::filesystem;

static const std::string kSrc = PNA_SOURCE_DIR;

static bool wait_for(const std::function<bool()>& cond, int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
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

// One ordered list for the fake gale-key and the fake tuner, so order shows.
static std::mutex g_order_mu;
static std::vector<std::string> g_order;
static void order(const std::string& e) {
    std::lock_guard<std::mutex> g(g_order_mu);
    g_order.push_back(e);
}
static std::vector<std::string> order_now() {
    std::lock_guard<std::mutex> g(g_order_mu);
    return g_order;
}
static void order_clear() {
    std::lock_guard<std::mutex> g(g_order_mu);
    g_order.clear();
}

// ---------------------------------------------------------------- the fake

struct FakeGaleKey {
    fs::path sock, outbox;
    std::string tx_world = "mars";
    std::string refuse_prepare;  // when set, an air prepare is refused with this reason
    std::atomic<int> key_ms{0};  // how long a key lasts (one request at a time, as gale-key)
    std::atomic<bool> keyed{false};
    std::mutex mu;
    std::vector<JsonValue> got;              // every request, in order
    std::set<std::string> live, grants;      // tokens not yet spent; standing grants
    std::vector<std::string> issued;         // every token given out
    int n = 0, fd = -1;
    std::thread th;
    std::atomic<bool> run{false};

    void start() {
        fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        std::strncpy(a.sun_path, sock.c_str(), sizeof a.sun_path - 1);
        ::unlink(sock.c_str());
        CHECK(::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
        CHECK(::listen(fd, 8) == 0);
        run = true;
        th = std::thread([this] { loop(); });
    }
    void stop() {
        if (!run.exchange(false)) return;
        th.join();
        ::close(fd);
        ::unlink(sock.c_str());
    }
    ~FakeGaleKey() { stop(); }

    void loop() {
        while (run) {
            fd_set r;
            FD_ZERO(&r);
            FD_SET(fd, &r);
            timeval tv{0, 50000};
            if (::select(fd + 1, &r, nullptr, nullptr, &tv) <= 0) continue;
            int c = ::accept(fd, nullptr, nullptr);
            if (c < 0) continue;
            std::string buf;
            char ch[4096];
            while (buf.find('\n') == std::string::npos) {
                ssize_t k = ::recv(c, ch, sizeof ch, 0);
                if (k <= 0) break;
                buf.append(ch, static_cast<size_t>(k));
            }
            JsonValue req;
            std::string rep = R"({"ok":false,"exit":2,"reason":"one JSON object on one line"})";
            if (buf.find('\n') != std::string::npos && parse_json(buf.substr(0, buf.find('\n')), req) && req.is_object())
                rep = answer(req);
            rep += "\n";
            ::send(c, rep.data(), rep.size(), MSG_NOSIGNAL);
            ::close(c);
        }
    }

    void result(const std::string& wav, bool keyed, const std::string& why) {
        fs::path w(wav);
        if (w.parent_path() != fs::absolute(outbox).lexically_normal()) return;  // only bursts in the outbox
        const std::string burst = w.filename().string(), stem = w.stem().string();
        fs::path tmp = outbox / ("." + stem + ".key.json.tmp");
        {
            std::ofstream f(tmp);
            f << R"({"version":1,"burst":")" << burst << R"(","result":")" << (keyed ? "keyed" : "refused")
              << R"(","why":")" << why << R"(","at":"2099-01-01T00:00:00Z"})";
        }
        fs::rename(tmp, outbox / (stem + ".key.json"));
    }

    std::string answer(const JsonValue& req) {
        std::lock_guard<std::mutex> g(mu);
        got.push_back(req);
        const std::string cmd = str(req, "cmd");
        if (cmd == "prepare") {
            const std::string kind = str(req, "kind");
            if (kind.rfind("standing:", 0) == 0) {
                const std::string mode = kind.substr(9);
                std::string tok = "TOK-SECRET-" + std::to_string(++n);
                live.insert(tok);
                issued.push_back(tok);
                return R"({"ok":true,"exit":0,"token":")" + tok + R"(","expires_s":120,"confirm":{"kind":"standing","mode":")" +
                       mode + R"(","world":")" + tx_world + R"("}})";
            }
            if (!refuse_prepare.empty()) return R"({"ok":false,"exit":2,"reason":")" + refuse_prepare + R"("})";
            std::string tok = "TOK-SECRET-" + std::to_string(++n);
            live.insert(tok);
            issued.push_back(tok);
            // A summary with more in it than the kiosk may see: a dial frequency
            // and a note naming one. The core passes neither on.
            return R"({"ok":true,"exit":0,"token":")" + tok + R"(","expires_s":120,"confirm":{"kind":"air","world":")" +
                   str(req, "world") + R"(","channel":")" + str(req, "channel") + R"(","control":")" +
                   str(req, "control") + R"(","id":false,"ptt":"rts","dial_hz":13579246,"note":"tuned to 13.579 MHz"}})";
        }
        if (cmd == "key") {
            order("gale-key key");
            if (key_ms > 0) {
                keyed = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(key_ms.load()));
                keyed = false;
                order("gale-key key ended");
            }
            const std::string wav = str(req, "wav");
            bool good = false;
            std::string why;
            if (req.get("token")) {
                good = live.erase(str(req, "token")) == 1;
                if (!good) why = "the token is not one I gave, or it was used";
            } else if (req.get("standing")) {
                good = grants.count(str(req, "standing")) == 1;
                if (!good) why = "no standing grant for " + str(req, "standing");
            } else {
                why = "no operator confirmation";
            }
            result(wav, good, why);
            return std::string(R"({"ok":)") + (good ? "true" : "false") + R"(,"exit":)" + (good ? "0" : "2") +
                   R"(,"reason":")" + why + R"(","keyed":)" + (good ? "true" : "false") + "}";
        }
        if (cmd == "grant") {
            if (live.erase(str(req, "token")) != 1) return R"({"ok":false,"exit":2,"reason":"no operator confirmation"})";
            grants.insert(str(req, "mode"));
            return R"({"ok":true,"exit":0})";
        }
        if (cmd == "revoke") {
            if (str(req, "mode") == "all") grants.clear();
            else grants.erase(str(req, "mode"));
            return R"({"ok":true,"exit":0})";
        }
        if (cmd == "status") {
            order("gale-key status");
            std::string gs;
            for (auto& m : grants) gs += (gs.empty() ? "" : ",") + std::string("\"") + m + "\":\"" + tx_world + "\"";
            return R"({"ok":true,"service":"gale-key","ready":true,"keyed":false,"ptt":"rts","transmit_world":")" + tx_world +
                   R"(","grants":{)" + gs + R"(},"why_not":["channel P1 is 13.579 MHz"],"dial_hz":13579246})";
        }
        return R"({"ok":false,"exit":2,"reason":"unknown cmd"})";
    }

    int count(const std::string& cmd) {
        std::lock_guard<std::mutex> g(mu);
        int k = 0;
        for (auto& r : got)
            if (str(r, "cmd") == cmd) ++k;
        return k;
    }
    std::optional<JsonValue> last(const std::string& cmd) {
        std::lock_guard<std::mutex> g(mu);
        for (auto it = got.rbegin(); it != got.rend(); ++it)
            if (str(*it, "cmd") == cmd) return *it;
        return std::nullopt;
    }
    std::vector<std::string> tokens() {
        std::lock_guard<std::mutex> g(mu);
        return issued;
    }
};

// A fake LDG tuner daemon (ldg-tuner/1): every call ok unless scripted.
struct FakeTuner : TunerLink {
    std::mutex mu;
    std::map<std::string, std::deque<std::string>> codes;  // cmd -> refusal codes, used in turn ("" ok)
    std::vector<std::string> sent;
    TunerReply call(const std::string& cmd, const std::string&, int) override {
        std::string code;
        {
            std::lock_guard<std::mutex> g(mu);
            sent.push_back(cmd);
            auto& q = codes[cmd];
            if (!q.empty()) {
                code = q.front();
                q.pop_front();
            }
        }
        if (cmd != "tuner_status") order(cmd);
        const std::string block = R"({"protocol":"ldg-tuner/1","contract_rev":2,"channel":{"ant_rx":1,"ant_tx":2}})";
        if (code.empty())
            return parse_tuner_reply(R"({"protocol":"ldg-tuner/1","id":1,"ok":true,"cmd":")" + cmd +
                                     R"(","result":{},"tuner":)" + block + "}");
        return parse_tuner_reply(R"({"protocol":"ldg-tuner/1","id":1,"ok":false,"cmd":")" + cmd +
                                 R"(","error":{"code":")" + code + R"(","reason":"the fake says )" + code +
                                 R"("},"tuner":)" + block + "}");
    }
    int count(const std::string& cmd) {
        std::lock_guard<std::mutex> g(mu);
        return static_cast<int>(std::count(sent.begin(), sent.end(), cmd));
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
static JsonValue parsed(const std::string& s) {
    JsonValue v;
    parse_json(s, v);
    return v;
}
static bool ok(const std::string& reply) { return flag(parsed(reply), "ok"); }

struct Station {
    fs::path dir;
    GreybeardConfig gc;
    std::unique_ptr<Core> core;
    std::shared_ptr<GaleKey> gk;
    std::shared_ptr<TunerKeying> tuner;
    std::atomic<double> now{1000.0};

    Station(const fs::path& d, const std::string& name, const fs::path& sock,
            std::shared_ptr<TunerLink> tuner_link = nullptr, int gk_timeout_ms = 2000, bool set_tuner = true)
        : dir(d) {
        fs::create_directories(dir);
        gc.mode = "600S";
        gc.tx_cmd = {"python3", "-I", kSrc + "/tools/stub_gale_tx.py"};
        gc.captures_dir = (dir / "cap").string();
        gc.work_dir = (dir / "work").string();
        gc.outbox_dir = (dir / "outbox").string();
        gc.state_file = (dir / "seen.txt").string();
        gc.poll_ms = 50;
        for (auto* p : {&gc.captures_dir, &gc.work_dir, &gc.outbox_dir}) fs::create_directories(*p);
        CoreConfig cc{name, (dir / (name + ".db")).string(), 3, 1, true};
        cc.ack_timeout_s = 600;
        core = std::make_unique<Core>(cc, std::make_unique<GreybeardModem>(gc));
        core->set_ops(std::make_shared<Ops>(name, cc.db_path));
        // No tuner daemon unless given: the channel is still remembered by designator.
        TunerTiming t;
        t.idle_poll_ms = 20;
        t.post_first_ms = 10;
        t.post_max_ms = 40;
        t.key_end_wait_ms = 10000;
        tuner = std::make_shared<TunerKeying>(tuner_link, t);
        if (set_tuner) core->set_tuner(tuner);
        gk = std::make_shared<GaleKey>(std::make_shared<GaleKeySocket>(sock.string(), gk_timeout_ms), gc.outbox_dir);
        gk->set_clock([this] { return now.load(); });
        core->set_gale_key(gk);
        core->start();
    }
    ~Station() {
        bbs::stop(*core);
        core->stop();
    }

    void set_channel(const std::string& name) {
        Channel c;
        c.name = name;
        tuner->channel(c);
    }
    std::string state(const std::string& id) {
        auto m = core->get(id);
        return m ? m->state : std::string();
    }
    // Queues a message and waits for its burst to be written.
    std::string written(const std::string& to, const std::string& body) {
        std::string err;
        std::string id = core->submit(to, 'R', body, &err);
        CHECK(!id.empty());
        CHECK(wait_for([&] { return state(id) == "written"; }, 10000));
        return id;
    }
    std::string events_text() {
        std::string all;
        for (auto& e : core->events(0, 5000)) all += e.text + "\n";
        return all;
    }
};

static bool no_secret(const std::string& text, FakeGaleKey& f) {
    for (auto& t : f.tokens())
        if (text.find(t) != std::string::npos) return false;
    return text.find("TOK-SECRET") == std::string::npos && text.find("token") == std::string::npos;
}
static bool no_frequency_in(const std::string& text) {
    return text.find("13579246") == std::string::npos && text.find("13.579") == std::string::npos &&
           text.find("dial_hz") == std::string::npos && text.find("MHz") == std::string::npos;
}

int main() {
    const fs::path base = fs::temp_directory_path() / ("pna-galekey-" + std::to_string(::getpid()));
    fs::remove_all(base);
    fs::create_directories(base);
    const fs::path sock = base / "key.sock";

    // ---- the kiosk never gets a frequency: a bare Hz figure or one with a
    // unit is withheld; a burst's name or a path's number is not one.
    {
        const std::string gone = "(gale-key's text named a frequency; not shown)";
        CHECK(no_frequency("dial 13579246") == gone);
        CHECK(no_frequency("tuned to 13.579 MHz") == gone);
        CHECK(no_frequency("13579 kHz") == gone);
        CHECK(no_frequency("20991231T235959Z-3.wav is keyed") == "20991231T235959Z-3.wav is keyed");
        CHECK(no_frequency("nothing answers on /run/x-1234567/key.sock") == "nothing answers on /run/x-1234567/key.sock");
        CHECK(no_frequency("channel P1") == "channel P1");
    }

    // ---- the operator's y: prepare, then confirm, keys; the token stays in the core
    {
        FakeGaleKey f;
        f.sock = sock;
        Station s(base / "A", "AAA", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel("P1");
        const std::string id = s.written("FARSTN", "KEY ME ON Y");

        std::string rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}});
        JsonValue v = parsed(rep);
        CHECK(flag(v, "ok"));
        const std::string pid = str(v, "prepare_id");
        CHECK(pid.rfind("KP-", 0) == 0);
        const JsonValue* cf = v.get("confirm");
        CHECK(cf && cf->is_object());
        if (cf && cf->is_object()) {
            CHECK(str(*cf, "kind") == "air" && str(*cf, "world") == "mars" && str(*cf, "channel") == "P1");
            CHECK(str(*cf, "control") == "local" && str(*cf, "ptt") == "rts" && str(*cf, "msg_id") == id);
            CHECK(!str(*cf, "burst").empty());
        }
        CHECK(f.count("prepare") == 1);
        // The token never reaches the kiosk, and no frequency does either.
        CHECK(no_secret(rep, f));
        CHECK(no_frequency_in(rep));
        auto prep = f.last("prepare");
        CHECK(prep && str(*prep, "kind") == "air" && str(*prep, "control") == "local" && str(*prep, "world") == "mars" &&
              str(*prep, "channel") == "P1" && prep->get("final") && !flag(*prep, "final"));
        const std::string wav = prep ? str(*prep, "wav") : std::string();
        CHECK(fs::path(wav).is_absolute() && fs::exists(wav));

        // Sooner than 0.5 s: refused here, nothing sent, the dialog still held.
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}});
        CHECK(!ok(rep) && str(parsed(rep), "error").find("too soon") != std::string::npos);
        CHECK(f.count("key") == 0);
        // Not Y: nothing sent.
        s.now = s.now + 2;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "N"}});
        CHECK(!ok(rep) && f.count("key") == 0);
        // Y: the key goes with the token, and the message is still written
        // until gale-key's BURST.key.json says it was keyed.
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}});
        v = parsed(rep);
        CHECK(flag(v, "ok") && flag(v, "asked") && str(v, "msg_id") == id);
        CHECK(no_secret(rep, f) && no_frequency_in(rep));
        auto key = f.last("key");
        CHECK(key && !f.tokens().empty() && str(*key, "token") == f.tokens().front());
        CHECK(key && str(*key, "wav") == wav && str(*key, "control") == "local" && str(*key, "channel") == "P1" &&
              str(*key, "world") == "mars" && str(*key, "kind") == "air" && !key->get("standing"));
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));
        CHECK(s.events_text().find("keyed by gale-key") != std::string::npos);
        // The token is in no log line.
        CHECK(no_secret(s.events_text(), f));
        // The same dialog can't key twice, and the burst can't be prepared again.
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}});
        CHECK(!ok(rep));
        CHECK(!ok(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})));
        CHECK(f.count("key") == 1);

        // ---- a cancelled token never keys
        const std::string c1 = s.written("FARSTN", "CANCEL ME");
        v = parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", c1}}));
        const std::string cid = str(v, "prepare_id");
        CHECK(!cid.empty());
        rep = call(*s.core, {{"cmd", "key_cancel"}, {"prepare_id", cid}});
        CHECK(ok(rep) && flag(parsed(rep), "dropped"));
        s.now = s.now + 2;
        CHECK(!ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", cid}, {"confirm", "Y"}})));
        CHECK(f.count("key") == 1);

        // ---- an expired token never keys (gale-key's 120 s)
        v = parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", c1}}));
        const std::string eid = str(v, "prepare_id");
        CHECK(!eid.empty() && v.get("expires_s") && v.get("expires_s")->n == 120);
        s.now = s.now + 121;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", eid}, {"confirm", "Y"}});
        CHECK(!ok(rep) && str(parsed(rep), "error").find("expired") != std::string::npos);
        CHECK(f.count("key") == 1);
        // A newer prepare for the same burst drops the older token.
        const std::string o1 = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"burst", ""}, {"msg_id", c1}})), "prepare_id");
        const std::string o2 = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", c1}})), "prepare_id");
        CHECK(!o1.empty() && !o2.empty() && o1 != o2);
        s.now = s.now + 2;
        CHECK(!ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", o1}, {"confirm", "Y"}})));
        CHECK(f.count("key") == 1);
        CHECK(ok(call(*s.core, {{"cmd", "key_cancel"}, {"prepare_id", o2}})));
        CHECK(s.state(c1) == "written");

        // ---- gale-key's refusal carries no frequency to the kiosk
        f.refuse_prepare = "channel P1 is 13.579 MHz, outside the plan";
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", c1}});
        CHECK(!ok(rep) && no_frequency_in(rep));
        f.refuse_prepare.clear();

        // ---- key_status and status: what gale-key says, nothing secret, no frequency
        rep = call(*s.core, {{"cmd", "key_status"}});
        v = parsed(rep);
        CHECK(flag(v, "ok") && no_frequency_in(rep) && no_secret(rep, f));
        CHECK(v.get("keying") && str(*v.get("keying"), "gale_key") == "answering");
        rep = call(*s.core, {{"cmd", "status"}});
        v = parsed(rep);
        CHECK(v.get("keying") && str(*v.get("keying"), "gale_key") == "answering");

        // ---- an amateur-world burst is never sent to gale-key by the core
        // (a station the book now marks amateur; an item whose world isn't mars)
        const std::string am = s.written("TESTAM-X", "NOT TO THE AMATEUR WORLD");
        CHECK(ok(call(*s.core, {{"cmd", "book_put"}, {"name", "TEST AM"}, {"call", "TESTAM-X"}, {"world", "amateur"}})));
        const int prepares = f.count("prepare");
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", am}});
        CHECK(!ok(rep) && str(parsed(rep), "error").find("amateur") != std::string::npos);
        const std::string w2 = s.written("FARSTN", "WORLD CHANGES UNDER IT");
        {
            sqlite3* db = nullptr;
            sqlite3_open((s.dir / "AAA.db").c_str(), &db);
            sqlite3_exec(db, ("UPDATE messages SET world = 'amateur' WHERE id = '" + w2 + "'").c_str(), nullptr, nullptr,
                         nullptr);
            sqlite3_close(db);
        }
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", w2}});
        CHECK(!ok(rep) && str(parsed(rep), "error").find("amateur") != std::string::npos);
        // The request itself naming the amateur world is refused before anything.
        CHECK(!ok(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", c1}, {"world", "amateur"}})));
        CHECK(f.count("prepare") == prepares);
        // And an automatic one: a routing send to the amateur station, with routing on.
        s.core->set_key_mode(am, "routing");
        s.core->set_key_mode(w2, "routing");

        // ---- standing modes: off means no key; grant on the y; revoke on off
        const std::string r1 = s.written("FARSTN", "ROUTED ONE");
        s.core->set_key_mode(r1, "routing");
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        CHECK(f.count("key") == 1 && s.state(r1) == "written");  // routing is off: not asked

        // amateur_auto is never asked for.
        rep = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "amateur_auto"}});
        CHECK(!ok(rep));
        CHECK(f.count("prepare") == prepares);

        rep = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "routing"}});
        v = parsed(rep);
        CHECK(flag(v, "ok") && no_secret(rep, f));
        const std::string sid = str(v, "prepare_id");
        CHECK(v.get("confirm") && str(*v.get("confirm"), "mode") == "routing" && str(*v.get("confirm"), "world") == "mars");
        auto sp = f.last("prepare");
        CHECK(sp && str(*sp, "kind") == "standing:routing");
        s.now = s.now + 2;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", sid}, {"confirm", "Y"}});
        v = parsed(rep);
        CHECK(flag(v, "ok") && flag(v, "on") && str(v, "mode") == "routing" && no_secret(rep, f));
        auto gr = f.last("grant");
        CHECK(gr && str(*gr, "mode") == "routing" && str(*gr, "token") == f.tokens().back());
        CHECK(s.gk->standing_on("routing"));
        // Now the written routing burst is keyed under the grant, by itself.
        CHECK(wait_for([&] { return s.state(r1) == "sent"; }, 5000));
        auto sk = f.last("key");
        CHECK(sk && str(*sk, "standing") == "routing" && str(*sk, "control") == "automatic" && !sk->get("token") &&
              str(*sk, "channel") == "P1" && str(*sk, "world") == "mars");
        const int keys_now = f.count("key");
        // The amateur ones were never asked, with routing on.
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(f.count("key") == keys_now && s.state(am) == "written" && s.state(w2) == "written");
        // A machine-mode answer waits: machine is not on.
        const std::string m1 = s.written("FARSTN", "MACHINE ANSWER");
        s.core->set_key_mode(m1, "machine");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(f.count("key") == keys_now && s.state(m1) == "written");

        // Off: revoked at gale-key, and the next routing burst is not asked.
        rep = call(*s.core, {{"cmd", "standing_off"}, {"mode", "routing"}});
        CHECK(ok(rep) && !flag(parsed(rep), "on"));
        auto rv = f.last("revoke");
        CHECK(rv && str(*rv, "mode") == "routing");
        CHECK(!s.gk->standing_on("routing"));
        const std::string r2 = s.written("FARSTN", "ROUTED TWO");
        s.core->set_key_mode(r2, "routing");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(f.count("key") == keys_now && s.state(r2) == "written");

        // Turning BBS routing off revokes the routing grant too.
        auto& e = bbs::engine(*s.core);
        std::string err;
        CHECK(e.set({{"routing", "true"}}, &err));
        const int revokes = f.count("revoke");
        CHECK(e.set({{"routing", "false"}}, &err));
        CHECK(f.count("revoke") == revokes + 1 && f.last("revoke") && str(*f.last("revoke"), "mode") == "routing");

        // A transmit world that isn't MARS: no standing grant is held.
        f.tx_world = "amateur";
        rep = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "machine"}});
        CHECK(!ok(rep) && s.gk->held() == 0);
        f.tx_world = "mars";

        // A mode on through the y, as the operator does it.
        auto grant = [&](const std::string& mode) {
            std::string pr = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", mode}});
            const std::string id = str(parsed(pr), "prepare_id");
            s.now = s.now + 2;
            return ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", id}, {"confirm", "Y"}}));
        };

        // EMCON on: every mode off in the core, and gale-key told to revoke all.
        CHECK(grant("machine") && s.gk->standing_on("machine"));
        s.core->set_emcon(true);
        CHECK(s.gk->standing().empty());
        CHECK(f.last("revoke") && str(*f.last("revoke"), "mode") == "all");
        CHECK(!ok(call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "machine"}})));  // not under EMCON
        s.core->set_emcon(false);

        // gale-key drops a grant by itself (its hourly cap): the next automatic
        // burst is refused by gale-key (failed, as its result says), and the
        // core turns the mode off, so the one after waits for the y.
        CHECK(grant("auto_qsl") && s.gk->standing_on("auto_qsl"));
        {
            std::lock_guard<std::mutex> g(f.mu);
            f.grants.clear();
        }
        const int keys_before = f.count("key");
        const std::string q1 = s.written("FARSTN", "QSL ONE");
        s.core->set_key_mode(q1, "auto_qsl");
        CHECK(wait_for([&] { return s.state(q1) == "failed"; }, 5000));
        CHECK(s.core->get(q1) && s.core->get(q1)->last_error.find("no standing grant") != std::string::npos);
        CHECK(wait_for([&] { return !s.gk->standing_on("auto_qsl"); }, 2000));
        const std::string q2 = s.written("FARSTN", "QSL TWO");
        s.core->set_key_mode(q2, "auto_qsl");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(f.count("key") == keys_before + 1 && s.state(q2) == "written");

        // ---- gale-key goes away: nothing keys, the burst stays written, status says why
        f.stop();
        const std::string g1 = s.written("FARSTN", "NO GALE-KEY");
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", g1}});
        CHECK(!ok(rep) && str(parsed(rep), "error").find("gale-key isn't running") != std::string::npos);
        v = parsed(call(*s.core, {{"cmd", "status"}}));
        CHECK(v.get("keying") && str(*v.get("keying"), "gale_key") == "not answering" &&
              str(*v.get("keying"), "why").find("gale-key isn't running") != std::string::npos);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(s.state(g1) == "written");
    }

    // ---- gale-key gone between the prepare and the y: not keyed, still written, may be asked again
    {
        FakeGaleKey f;
        f.sock = sock;
        Station s(base / "B", "BBB", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel("P2");
        const std::string id = s.written("FARSTN", "GONE BEFORE Y");
        const std::string pid = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})), "prepare_id");
        CHECK(!pid.empty());
        f.stop();
        s.now = s.now + 2;
        std::string rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}});
        CHECK(!ok(rep) && str(parsed(rep), "error").find("stays written") != std::string::npos);
        CHECK(s.state(id) == "written");
        FakeGaleKey f2;
        f2.sock = sock;
        f2.outbox = s.gc.outbox_dir;
        f2.start();
        const std::string p2 = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})), "prepare_id");
        CHECK(!p2.empty());
        s.now = s.now + 2;
        CHECK(ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p2}, {"confirm", "Y"}})));
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));
    }

    // ---- no channel when the burst was written: never sent to gale-key
    {
        FakeGaleKey f;
        f.sock = sock;
        Station s(base / "C", "CCC", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        const std::string id = s.written("FARSTN", "NO CHANNEL SET");
        std::string rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}});
        CHECK(!ok(rep) && str(parsed(rep), "error").find("no channel") != std::string::npos);
        CHECK(f.count("prepare") == 0);
    }

    // ---- key_prepare is bound to one written burst: its WAV, its sha256, its
    // world and channel, and the connection that opened the dialog
    {
        FakeGaleKey f;
        f.sock = sock;
        Station s(base / "E", "EEE", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel("P3");
        auto err_of = [](const std::string& r) { return str(parsed(r), "error"); };

        // 1. Empty, unbound, or not a written burst: refused, gale-key not asked.
        std::string rep = call(*s.core, {{"cmd", "key_prepare"}});
        CHECK(!ok(rep) && err_of(rep).find("msg_id or a burst") != std::string::npos);
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"burst", "NOSUCH.wav"}});
        CHECK(!ok(rep) && err_of(rep).find("no burst of a message of ours") != std::string::npos);
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", "NOSUCH-1"}});
        CHECK(!ok(rep) && err_of(rep).find("no outgoing message") != std::string::npos);
        std::string e;
        const std::string held = s.core->submit("FARSTN", 'R', "HELD, NOT WRITTEN", &e, true);
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", held}});
        CHECK(!ok(rep) && err_of(rep).find("is held, not a burst written") != std::string::npos);
        const std::string gone = s.written("FARSTN", "WAV REMOVED");
        std::string gb;
        for (auto& ev : s.core->events(0, 5000))
            if (ev.msg_id == gone && ev.text.rfind("written to the outbox as ", 0) == 0)
                gb = ev.text.substr(25, ev.text.find(';') - 25);
        CHECK(!gb.empty() && fs::remove(fs::path(s.gc.outbox_dir) / gb));
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", gone}});
        CHECK(!ok(rep) && err_of(rep).find("is not in the outbox") != std::string::npos);
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", gone}, {"burst", "OTHER.wav"}});
        CHECK(!ok(rep) && err_of(rep).find("is not " + gone + "'s burst") != std::string::npos);
        CHECK(f.count("prepare") == 0);

        // 3. The burst changed after the prepare: rewritten, removed, or no longer written.
        auto prep = [&](const std::string& id, std::string* burst) {
            JsonValue v = parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}}));
            const JsonValue* c = v.get("confirm");
            if (burst) *burst = c ? str(*c, "burst") : std::string();
            return str(v, "prepare_id");
        };
        std::string b1;
        const std::string m1 = s.written("FARSTN", "REWRITTEN UNDER THE DIALOG");
        std::string p1 = prep(m1, &b1);
        CHECK(!p1.empty() && !b1.empty());
        {
            std::ofstream w(fs::path(s.gc.outbox_dir) / b1, std::ios::binary | std::ios::app);
            w << "XX";
        }
        s.now = s.now + 2;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p1}, {"confirm", "Y"}});
        CHECK(!ok(rep) && err_of(rep).find("changed since the y dialog opened") != std::string::npos);
        std::string b2;
        const std::string m2 = s.written("FARSTN", "REMOVED UNDER THE DIALOG");
        std::string p2 = prep(m2, &b2);
        fs::remove(fs::path(s.gc.outbox_dir) / b2);
        s.now = s.now + 2;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p2}, {"confirm", "Y"}});
        CHECK(!ok(rep) && err_of(rep).find("no longer in the outbox") != std::string::npos);
        const std::string m3 = s.written("FARSTN", "CANCELLED UNDER THE DIALOG");
        std::string p3 = prep(m3, nullptr);
        CHECK(s.core->cancel(m3, &e));
        s.now = s.now + 2;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p3}, {"confirm", "Y"}});
        CHECK(!ok(rep) && err_of(rep).find("is cancelled, not a burst written") != std::string::npos);
        CHECK(f.count("key") == 0);
        CHECK(s.state(m1) == "written" && s.state(m2) == "written");

        // 4. Never confirmed nor cancelled: dropped by the core at gale-key's 120 s.
        const std::string m4 = s.written("FARSTN", "LEFT OPEN");
        std::string p4 = prep(m4, nullptr);
        CHECK(!p4.empty() && s.gk->held() == 1);
        s.now = s.now + 121;
        JsonValue st = parsed(call(*s.core, {{"cmd", "status"}}));
        CHECK(st.get("keying") && st.get("keying")->get("held") && st.get("keying")->get("held")->n == 0);
        CHECK(!ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p4}, {"confirm", "Y"}})));

        // 4. A connection that closes takes its dialogs with it; another
        // connection can't confirm them.
        uint16_t port = 0;
        std::unique_ptr<ApiServer> srv;
        for (int i = 0; i < 50 && !srv; ++i) {
            uint16_t p = static_cast<uint16_t>(20000 + (::getpid() * 7 + i * 131) % 30000);
            auto x = std::make_unique<ApiServer>(*s.core, p);
            if (x->start()) {
                srv = std::move(x);
                port = p;
            }
        }
        CHECK(srv != nullptr);
        auto dial = [&]() {
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(port);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
            return fd;
        };
        auto line = [&](int fd, const std::string& req) {
            std::string l = req + "\n";
            ::send(fd, l.data(), l.size(), MSG_NOSIGNAL);
            std::string buf;
            char ch[4096];
            while (buf.find('\n') == std::string::npos) {
                ssize_t k = ::recv(fd, ch, sizeof ch, 0);
                if (k <= 0) break;
                buf.append(ch, static_cast<size_t>(k));
            }
            return buf.substr(0, buf.find('\n'));
        };
        if (srv) {
            int a1 = dial(), a2 = dial();
            rep = line(a1, R"({"cmd":"key_prepare","msg_id":")" + m4 + R"("})");
            const std::string pa = str(parsed(rep), "prepare_id");
            CHECK(!pa.empty() && s.gk->held() == 1);
            s.now = s.now + 2;
            rep = line(a2, R"({"cmd":"key_confirm","prepare_id":")" + pa + R"(","confirm":"Y"})");
            CHECK(!ok(rep) && err_of(rep).find("another connection") != std::string::npos);
            CHECK(s.gk->held() == 1);
            ::close(a1);  // the page reloads with the dialog open
            CHECK(wait_for([&] { return s.gk->held() == 0; }, 3000));
            rep = line(a2, R"({"cmd":"key_confirm","prepare_id":")" + pa + R"(","confirm":"Y"})");
            CHECK(!ok(rep) && err_of(rep).find("no such y dialog") != std::string::npos);
            ::close(a2);
            srv->stop();
        }
        CHECK(f.count("key") == 0 && s.state(m4) == "written");
    }

    // ---- the mode-off coupling: gale-key's refusal for a grant it doesn't hold
    // is exit 2 like every refusal, so it is told by its wording, bound to the mode
    {
        GaleKeyReply r;
        r.reached = r.answered = true;
        r.exit = 2;
        r.reason = "no standing grant for machine: the operator's y when turning it on grants it";
        CHECK(gale_key_holds_no_grant(r, "machine"));
        CHECK(!gale_key_holds_no_grant(r, "routing"));
        r.exit = 3;
        CHECK(!gale_key_holds_no_grant(r, "machine"));
        r.exit = 2;
        r.reason = "no standing grant for amateur_auto: automatic control in the amateur world needs its own y";
        CHECK(!gale_key_holds_no_grant(r, "machine") && !gale_key_holds_no_grant(r, "auto_qsl"));
        r.reason = "the hourly cap is reached: no standing grant";
        CHECK(!gale_key_holds_no_grant(r, "machine"));
        r.reason = "no standing grant for machine: x";
        r.answered = false;
        CHECK(!gale_key_holds_no_grant(r, "machine"));
    }

    // ---- ACK and orderwire bursts: keyed under standing machine only, in the
    // world of what they answer (MARS only), on the core's channel; their
    // result is logged only
    {
        FakeGaleKey f;
        f.sock = sock;
        Station s(base / "F", "FFF", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel("P1");
        auto answers = [&] {
            JsonValue v = parsed(call(*s.core, {{"cmd", "status"}}));
            const JsonValue* k = v.get("keying");
            const JsonValue* n = k ? k->get("answers_written") : nullptr;
            return n && n->type == JsonValue::Type::Number ? static_cast<int>(n->n) : -1;
        };
        auto grant = [&](const std::string& mode) {
            std::string pr = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", mode}});
            const std::string id = str(parsed(pr), "prepare_id");
            s.now = s.now + 2;
            return ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", id}, {"confirm", "Y"}}));
        };
        // A message to us, heard on the air: the core acks it.
        Frame in;
        in.type = FrameType::Msg;
        in.id = "FARSTN-7";
        in.src = "FARSTN";
        in.dst = "FFF";
        in.prec = 'R';
        in.body = "HELLO FFF";
        const std::string bytes = encode(in);
        {
            std::ofstream b(fs::path(s.gc.captures_dir) / "in.m110a.0.bin", std::ios::binary);
            b << bytes;
        }
        {
            std::ofstream l(fs::path(s.gc.captures_dir) / "in.modems.json");
            l << R"({"files":[{"file":"in.m110a.0.bin","bytes":)" << bytes.size() << R"(,"sha256":")" << sha256_hex(bytes)
              << R"("}]})";
        }
        CHECK(wait_for([&] { return s.core->answer_bursts() == 1; }, 10000));
        std::string err;
        CHECK(s.core->orderwire("FARSTN", "QSL QRU", &err));
        CHECK(wait_for([&] { return s.core->answer_bursts() == 2; }, 10000));
        CHECK(answers() == 2);
        // Machine off: never asked, routing on or not.
        CHECK(grant("routing"));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(f.count("key") == 0 && s.core->answer_bursts() == 2);
        // Machine on: both keyed under it.
        CHECK(grant("machine"));
        CHECK(wait_for([&] { return s.core->answer_bursts() == 0; }, 5000));
        CHECK(f.count("key") == 2);
        {
            std::lock_guard<std::mutex> g(f.mu);
            for (auto& r : f.got) {
                if (str(r, "cmd") != "key") continue;
                CHECK(str(r, "standing") == "machine" && str(r, "control") == "automatic" && str(r, "world") == "mars" &&
                      str(r, "channel") == "P1" && !r.get("token"));
            }
        }
        const std::string ev = s.events_text();
        CHECK(ev.find("ack to FARSTN written to the outbox") != std::string::npos);
        CHECK(ev.find("orderwire to FARSTN written to the outbox") != std::string::npos);
        CHECK(ev.find(") keyed by gale-key") != std::string::npos);
        CHECK(answers() == 0);
        // Its result changes no message: the one acked stays as it was.
        auto heard = s.core->get("FARSTN-7");
        CHECK(heard && heard->state == "received");

        // A burst to a station the book marks amateur after it was written: never asked.
        CHECK(ok(call(*s.core, {{"cmd", "standing_off"}, {"mode", "machine"}})));
        CHECK(s.core->orderwire("TESTAM-Y", "QRV", &err));
        CHECK(wait_for([&] { return s.core->answer_bursts() == 1; }, 10000));
        CHECK(ok(call(*s.core, {{"cmd", "book_put"}, {"name", "TEST AM Y"}, {"call", "TESTAM-Y"}, {"world", "amateur"}})));
        CHECK(grant("machine"));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(f.count("key") == 2 && s.core->answer_bursts() == 1);
        CHECK(s.events_text().find("not asked of gale-key: TESTAM-Y is an amateur station") != std::string::npos);
    }

    // ---- no tuner daemon at all: tuner_channel still sets the channel gale-key keys on
    {
        FakeGaleKey f;
        f.sock = sock;
        Station s(base / "G", "GGG", sock, nullptr, 2000, /*set_tuner=*/false);
        f.outbox = s.gc.outbox_dir;
        f.start();
        CHECK(ok(call(*s.core, {{"cmd", "channel_put"}, {"name", "P1"}, {"frequency", "5000"}})));
        std::string rep = call(*s.core, {{"cmd", "tuner_channel"}, {"channel", "P1"}});
        CHECK(ok(rep) && str(parsed(rep), "channel") == "P1" && rep.find("5000") == std::string::npos);
        CHECK(s.core->tuner() && s.core->tuner()->current_channel() == "P1");
        CHECK(str(parsed(call(*s.core, {{"cmd", "tuner_bypass"}})), "code") == "no-tuner");
        const std::string id = s.written("FARSTN", "CHANNEL FROM GALE-RX");
        const std::string pid = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})), "prepare_id");
        CHECK(!pid.empty());
        s.now = s.now + 2;
        CHECK(ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}})));
        CHECK(f.last("key") && str(*f.last("key"), "channel") == "P1");
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));
    }

    // ---- a tuner daemon: pre_tx before every key, the key only on ok, post_tx after
    {
        FakeGaleKey f;
        f.sock = sock;
        auto ft = std::make_shared<FakeTuner>();
        Station s(base / "H", "HHH", sock, ft);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel("P1");
        auto confirm_new = [&](const std::string& body, std::string* id_out) {
            const std::string id = s.written("FARSTN", body);
            if (id_out) *id_out = id;
            const std::string pid = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})), "prepare_id");
            CHECK(!pid.empty());
            s.now = s.now + 2;
            return call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}});
        };
        const std::vector<std::string> seq = {"tuner_pre_tx", "gale-key key", "tuner_post_tx"};

        // The operator's y.
        std::string id;
        order_clear();
        CHECK(ok(confirm_new("TUNED ONE", &id)));
        CHECK(order_now() == seq);
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));

        // pre_tx refused: gale-key isn't asked, the burst stays written, the reason is shown.
        ft->codes["tuner_pre_tx"] = {"refused-no-channel"};
        const int keys = f.count("key");
        std::string rep = confirm_new("PRE TX REFUSED", &id);
        CHECK(!ok(rep));
        const std::string why = str(parsed(rep), "error");
        CHECK(why.find("tuner_pre_tx refused-no-channel") != std::string::npos &&
              why.find("stays written") != std::string::npos);
        CHECK(f.count("key") == keys && s.state(id) == "written" && ft->count("tuner_post_tx") == 1);
        // Asked again once pre_tx is ok.
        const std::string p2 = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})), "prepare_id");
        s.now = s.now + 2;
        CHECK(ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p2}, {"confirm", "Y"}})));
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));

        // post_tx refused-keyed: retried until taken.
        ft->codes["tuner_post_tx"] = {"refused-keyed", "refused-keyed"};
        const int posts = ft->count("tuner_post_tx");
        CHECK(ok(confirm_new("POST TX RETRIED", &id)));
        CHECK(ft->count("tuner_post_tx") == posts + 3);
        // Never taken: keyed all the same, and the log says the antenna didn't go back.
        ft->codes["tuner_post_tx"] = std::deque<std::string>(20, "refused-keyed");
        CHECK(ok(confirm_new("POST TX NEVER", &id)));
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));
        CHECK(s.events_text().find("the tuner didn't go back to the RX antenna") != std::string::npos);
        ft->codes["tuner_post_tx"].clear();

        // A standing key: the same sequence.
        auto grant = [&](const std::string& mode) {
            std::string pr = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", mode}});
            const std::string pid = str(parsed(pr), "prepare_id");
            s.now = s.now + 2;
            return ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}}));
        };
        CHECK(grant("routing"));
        order_clear();
        const std::string r1 = s.written("FARSTN", "ROUTED AND TUNED");
        s.core->set_key_mode(r1, "routing");
        CHECK(wait_for([&] { return s.state(r1) == "sent"; }, 5000));
        CHECK(order_now() == seq);
        // A standing key whose pre_tx is refused: not keyed, still written, the reason in its log.
        ft->codes["tuner_pre_tx"] = {"refused-antenna"};
        const int keys2 = f.count("key");
        const std::string r2 = s.written("FARSTN", "ROUTED, ANTENNA REFUSED");
        s.core->set_key_mode(r2, "routing");
        CHECK(wait_for([&] { return s.events_text().find("tuner_pre_tx refused-antenna") != std::string::npos; }, 5000));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(f.count("key") == keys2 && s.state(r2) == "written");
        CHECK(s.events_text().find("not asked, the burst stays written") != std::string::npos);
    }

    // ---- gale-key answers after the timeout (it is still keying): post_tx waits
    // until its status says the key has ended
    {
        FakeGaleKey f;
        f.sock = sock;
        f.key_ms = 900;
        auto ft = std::make_shared<FakeTuner>();
        Station s(base / "I", "III", sock, ft, 300);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel("P1");
        const std::string id = s.written("FARSTN", "A LONG KEY");
        const std::string pid = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})), "prepare_id");
        s.now = s.now + 2;
        order_clear();
        std::string rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}});
        JsonValue v = parsed(rep);
        CHECK(flag(v, "ok") && v.get("gale_key") && flag(*v.get("gale_key"), "reached") &&
              !flag(*v.get("gale_key"), "answered"));
        auto o = order_now();
        auto at = [&](const std::string& e) {
            auto it = std::find(o.begin(), o.end(), e);
            return it == o.end() ? -1 : static_cast<int>(it - o.begin());
        };
        CHECK(at("tuner_pre_tx") == 0 && at("gale-key key") == 1);
        CHECK(at("gale-key key ended") > 1 && at("tuner_post_tx") > at("gale-key key ended"));
        CHECK(at("gale-key status") > at("gale-key key ended"));
        CHECK(ft->count("tuner_post_tx") == 1);
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));
    }

    // ---- a station with no gale-key at all: the calls say so, status says why
    {
        GreybeardConfig gc;
        fs::path d = base / "D";
        fs::create_directories(d / "outbox");
        gc.mode = "600S";
        gc.tx_cmd = {"python3", "-I", kSrc + "/tools/stub_gale_tx.py"};
        gc.captures_dir = (d / "cap").string();
        gc.work_dir = (d / "work").string();
        gc.outbox_dir = (d / "outbox").string();
        gc.state_file = (d / "seen.txt").string();
        CoreConfig cc{"DDD", (d / "DDD.db").string(), 3, 1, true};
        Core c(cc, std::make_unique<GreybeardModem>(gc));
        c.start();
        CHECK(!ok(call(c, {{"cmd", "key_status"}})));
        JsonValue v = parsed(call(c, {{"cmd", "status"}}));
        CHECK(v.get("keying") && str(*v.get("keying"), "gale_key") == "none");
        c.stop();
    }

    fs::remove_all(base);
    return check::report("test_gale_key");
}
