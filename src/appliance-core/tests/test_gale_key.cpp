// The core as gale-key's caller (gale_key.h, docs/api.md "Keying: the core
// asks gale-key"), against a fake gale-key on a Unix socket. The fake answers
// as greybeard-ale's docs/gale-tx.md says (prepare, key, grant, revoke,
// status) and, for every key whose WAV is in the outbox, writes api.md's
// BURST.key.json result, keyed or refused. The core never keys anything here.
#include "../src/api_server.h"
#include "../src/bbs.h"
#include "../src/core.h"
#include "../src/gale_key.h"
#include "../src/greybeard_modem.h"
#include "../src/json.h"
#include "../src/ops.h"
#include "../src/tuner.h"
#include "check.h"

#include <sqlite3.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
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

// ---------------------------------------------------------------- the fake

struct FakeGaleKey {
    fs::path sock, outbox;
    std::string tx_world = "mars";
    std::string refuse_prepare;  // when set, an air prepare is refused with this reason
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

    Station(const fs::path& d, const std::string& name, const fs::path& sock) : dir(d) {
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
        // No tuner daemon: the channel is still remembered by designator.
        tuner = std::make_shared<TunerKeying>(nullptr);
        core->set_tuner(tuner);
        gk = std::make_shared<GaleKey>(std::make_shared<GaleKeySocket>(sock.string(), 2000), gc.outbox_dir);
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
