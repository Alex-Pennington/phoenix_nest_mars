// The core as the station controller's caller (gale_key.h; docs/api.md
// "Keying: the core asks the station controller"), against a fake controller
// on a Unix socket. The fake answers in the shape of greybeard-ale
// docs/station-controller.md (MIL-STD-188-141B D.4.2.8): prepare and key carry
// `message` and `control`; a key is accepted at once, and the result file
// BURST.key.json comes when the key is over, keyed or refused. The core never
// keys anything here. Every call and frequency is made up.
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
#include "services_fixture.h"

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
static JsonValue sub(const JsonValue& v, const char* k) {
    const JsonValue* x = v.is_object() ? v.get(k) : nullptr;
    return x && x->is_object() ? *x : JsonValue();
}
// A network message's two parts (D.4.2.8.3).
static std::string ctl(const JsonValue& req, const char* k) { return str(sub(req, "control"), k); }
static std::string wav_of(const JsonValue& req) { return str(sub(req, "message"), "wav"); }

// ---------------------------------------------------------------- the fake

struct FakeController {
    fs::path sock, outbox;
    std::string standing_service;  // when set, a standing prepare's summary names this service
    std::string refuse_prepare;    // when set, an air prepare is refused with this reason
    std::string refuse_key;        // when set, a key is not accepted, with this reason
    std::atomic<int> key_ms{0};    // how long a key lasts before its result file comes
    std::string radio, radio_service = "mars";  // where status says the radio is ("" none)
    std::mutex mu;
    std::vector<JsonValue> got;              // every request, in order
    std::set<std::string> live, grants;      // tokens not yet spent; standing grants
    std::vector<std::string> issued;         // every token given out
    std::vector<std::thread> keys;           // keys still running
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
        std::vector<std::thread> k;
        {
            std::lock_guard<std::mutex> g(mu);
            k.swap(keys);
        }
        for (auto& t : k) t.join();
    }
    ~FakeController() { stop(); }

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
            std::string rep = R"({"ok":false,"reason":"one JSON object on one line"})";
            if (buf.find('\n') != std::string::npos && parse_json(buf.substr(0, buf.find('\n')), req) && req.is_object())
                rep = answer(req);
            rep += "\n";
            ::send(c, rep.data(), rep.size(), MSG_NOSIGNAL);
            ::close(c);
        }
    }

    // The result file beside the burst: gale-key's, or the controller's refusal.
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
                const std::string svc = standing_service.empty() ? str(req, "service") : standing_service;
                std::string tok = "TOK-SECRET-" + std::to_string(++n);
                live.insert(tok);
                issued.push_back(tok);
                return R"({"ok":true,"token":")" + tok + R"(","expires_s":120,"confirm":{"kind":"standing","mode":")" +
                       mode + R"(","service":")" + svc + R"("}})";
            }
            if (!refuse_prepare.empty()) return R"({"ok":false,"reason":")" + refuse_prepare + R"("})";
            std::string tok = "TOK-SECRET-" + std::to_string(++n);
            live.insert(tok);
            issued.push_back(tok);
            // A summary with more in it than the kiosk may see: a dial frequency
            // and a note naming one. The core passes neither on.
            return R"({"ok":true,"token":")" + tok + R"(","expires_s":120,"confirm":{"kind":"air","service":")" +
                   ctl(req, "service") + R"(","channel":")" + ctl(req, "channel") + R"(","control":")" +
                   ctl(req, "control") + R"(","id":false,"ptt":"rts","dial_hz":13579246,"note":"tuned to 13.579 MHz"}})";
        }
        if (cmd == "key") {
            if (!refuse_key.empty()) return R"({"ok":false,"accepted":false,"reason":")" + refuse_key + R"("})";
            const std::string wav = wav_of(req);
            bool good = false;
            std::string why;
            const JsonValue c = sub(req, "control");
            if (c.get("token")) {
                good = live.erase(str(c, "token")) == 1;
                if (!good) why = "the token is not one I gave, or it was used";
            } else if (c.get("standing")) {
                good = grants.count(str(c, "standing")) == 1;
                if (!good) why = "no standing grant for " + str(c, "standing");
            } else {
                why = "no operator confirmation";
            }
            // Accepted at once; the result follows when the key is over.
            const int ms = key_ms.load();
            keys.emplace_back([this, wav, good, why, ms] {
                if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                result(wav, good, why);
            });
            return R"({"ok":true,"accepted":true,"send":"K-)" + std::to_string(++n) + R"(","waiting":""})";
        }
        if (cmd == "grant") {
            if (live.erase(str(req, "token")) != 1) return R"({"ok":false,"reason":"no operator confirmation"})";
            grants.insert(str(req, "mode"));
            return R"({"ok":true})";
        }
        if (cmd == "revoke") {
            if (str(req, "mode") == "all") grants.clear();
            else grants.erase(str(req, "mode"));
            return R"({"ok":true})";
        }
        if (cmd == "status") {
            std::string gs;
            for (auto& m : grants) gs += (gs.empty() ? "" : ",") + std::string("\"") + m + "\":\"mars\"";
            const std::string r = radio.empty() ? std::string(R"("radio":null)")
                                                : R"("radio":{"channel":")" + radio + R"(","service":")" +
                                                      radio_service +
                                                      R"(","since":"2099-01-01T00:00:00Z","source":"schedule"})";
            return R"({"ok":true,)" + r + R"(,"ready":true,"keyed":false,"ptt":"rts","grants":{)" + gs +
                   R"(},"why_not":["channel P1 is 13.579 MHz"],"waiting":[{"send":"K-9","service":"mars","channel":"P1","priority":60,"why":"waiting for the slot on 13.579 MHz"}],"dial_hz":13579246})";
        }
        return R"({"ok":false,"reason":"unknown cmd"})";
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
    void set_radio(const std::string& label, const std::string& service) {
        std::lock_guard<std::mutex> g(mu);
        radio = label;
        radio_service = service;
    }
};

// A fake LDG tuner daemon (ldg-tuner/1): every call ok, each one counted.
struct FakeTuner : TunerLink {
    std::mutex mu;
    std::vector<std::string> sent;
    TunerReply call(const std::string& cmd, const std::string&, int) override {
        {
            std::lock_guard<std::mutex> g(mu);
            sent.push_back(cmd);
        }
        const std::string block = R"({"protocol":"ldg-tuner/1","contract_rev":2,"channel":{"ant_rx":1,"ant_tx":2}})";
        return parse_tuner_reply(R"({"protocol":"ldg-tuner/1","id":1,"ok":true,"cmd":")" + cmd +
                                 R"(","result":{},"tuner":)" + block + "}");
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
            std::shared_ptr<TunerLink> tuner_link = nullptr, int timeout_ms = 2000)
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
        core->set_services(test_services());
        core->set_ops(std::make_shared<Ops>(name, cc.db_path));
        // The channel plan's labels, in their services (made-up dials).
        for (auto& [label, khz, svc] : std::vector<std::tuple<std::string, std::string, std::string>>{
                 {"P1", "5000", "mars"}, {"P2", "5300", "mars"}, {"P3", "5600", "mars"}, {"AMX", "15000", "amateur"}})
            CHECK(ok(call(*core, {{"cmd", "channel_put"}, {"name", label}, {"frequency", khz}, {"world", svc}})));
        TunerTiming t;
        t.idle_poll_ms = 20;
        t.post_first_ms = 10;
        t.post_max_ms = 40;
        t.key_end_wait_ms = 10000;
        tuner = std::make_shared<TunerKeying>(tuner_link, t);
        core->set_tuner(tuner);
        gk = std::make_shared<GaleKey>(std::make_shared<GaleKeySocket>(sock.string(), timeout_ms), gc.outbox_dir);
        gk->set_clock([this] { return now.load(); });
        core->set_gale_key(gk);
        core->start();
    }
    ~Station() {
        bbs::stop(*core);
        core->stop();
    }

    // The controller reports the radio on this channel; the core reads it now.
    void set_channel(FakeController& f, const std::string& label, const std::string& service = "mars") {
        f.set_radio(label, service);
        gk->refresh_radio();
    }
    std::string state(const std::string& id) {
        auto m = core->get(id);
        return m ? m->state : std::string();
    }
    // Queues a message and waits for its burst to be written.
    std::string written(const std::string& to, const std::string& body, const std::string& service = "mars",
                        const std::string& channel = "") {
        std::string err;
        std::string id = core->submit(to, 'R', body, &err, SendOpts::in(service, channel));
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
};

static bool no_secret(const std::string& text, FakeController& f) {
    for (auto& t : f.tokens())
        if (text.find(t) != std::string::npos) return false;
    return text.find("TOK-SECRET") == std::string::npos && text.find("token") == std::string::npos;
}
static bool no_frequency_in(const std::string& text) {
    return text.find("13579246") == std::string::npos && text.find("13.579") == std::string::npos &&
           text.find("dial_hz") == std::string::npos && text.find("MHz") == std::string::npos;
}
static bool has(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

int main() {
    const fs::path base = fs::temp_directory_path() / ("pna-galekey-" + std::to_string(::getpid()));
    fs::remove_all(base);
    fs::create_directories(base);
    const fs::path sock = base / "control.sock";

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
        CHECK(std::string(kControllerSocket) == "/run/gale-rx/control.sock" && kControllerTimeoutMs == 5000);
    }

    // ---- the operator's y: prepare, then confirm, keys; the token stays in the core
    {
        FakeController f;
        f.sock = sock;
        Station s(base / "A", "AAA", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel(f, "P1");
        const std::string id = s.written("FARSTN", "KEY ME ON Y");

        std::string rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}});
        JsonValue v = parsed(rep);
        CHECK(flag(v, "ok"));
        const std::string pid = str(v, "prepare_id");
        CHECK(pid.rfind("KP-", 0) == 0);
        const JsonValue cf = sub(v, "confirm");
        CHECK(str(cf, "kind") == "air" && str(cf, "service") == "mars" && str(cf, "world") == "mars" &&
              str(cf, "channel") == "P1");
        CHECK(str(cf, "control") == "local" && str(cf, "ptt") == "rts" && str(cf, "msg_id") == id);
        CHECK(!str(cf, "burst").empty());
        CHECK(f.count("prepare") == 1);
        // The token never reaches the kiosk, and no frequency does either.
        CHECK(no_secret(rep, f));
        CHECK(no_frequency_in(rep));
        // D.4.2.8.3: the message, and its control information.
        auto prep = f.last("prepare");
        CHECK(prep && ctl(*prep, "kind") == "air" && ctl(*prep, "control") == "local" && ctl(*prep, "service") == "mars" &&
              ctl(*prep, "channel") == "P1" && ctl(*prep, "to") == "FARSTN" && sub(*prep, "control").get("final") &&
              !flag(sub(*prep, "control"), "final") && !prep->get("world") && !prep->get("wav"));
        const std::string wav = prep ? wav_of(*prep) : std::string();
        CHECK(fs::path(wav).is_absolute() && fs::exists(wav));

        // Sooner than 0.5 s: refused here, nothing sent, the dialog still held.
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "too soon"));
        CHECK(f.count("key") == 0);
        // Not Y: nothing sent.
        s.now = s.now + 2;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "N"}});
        CHECK(!ok(rep) && f.count("key") == 0);
        // Y: the key goes with the token in its control part, and the message
        // is still written until the result file says it was keyed.
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}});
        v = parsed(rep);
        CHECK(flag(v, "ok") && flag(v, "asked") && str(v, "msg_id") == id);
        CHECK(flag(sub(v, "controller"), "accepted") && flag(sub(v, "gale_key"), "accepted"));  // the alias, one release
        CHECK(no_secret(rep, f) && no_frequency_in(rep));
        auto key = f.last("key");
        CHECK(key && !f.tokens().empty() && ctl(*key, "token") == f.tokens().front());
        CHECK(key && wav_of(*key) == wav && ctl(*key, "control") == "local" && ctl(*key, "channel") == "P1" &&
              ctl(*key, "service") == "mars" && ctl(*key, "kind") == "air" && ctl(*key, "to") == "FARSTN" &&
              !sub(*key, "control").get("standing"));
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));
        CHECK(has(s.events_text(), "keyed by gale-key"));
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

        // ---- an expired token never keys (the token's 120 s)
        v = parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", c1}}));
        const std::string eid = str(v, "prepare_id");
        CHECK(!eid.empty() && v.get("expires_s") && v.get("expires_s")->n == 120);
        s.now = s.now + 121;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", eid}, {"confirm", "Y"}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "expired"));
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

        // ---- the controller's refusal carries no frequency to the kiosk
        f.refuse_prepare = "channel P1 is 13.579 MHz, outside the plan";
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", c1}});
        CHECK(!ok(rep) && no_frequency_in(rep));
        f.refuse_prepare.clear();

        // ---- key_status and status: what the controller says, nothing secret, no frequency
        rep = call(*s.core, {{"cmd", "key_status"}});
        v = parsed(rep);
        CHECK(flag(v, "ok") && no_frequency_in(rep) && no_secret(rep, f));
        CHECK(str(sub(v, "keying"), "controller") == "answering" && str(sub(v, "keying"), "gale_key") == "answering");
        {
            const JsonValue st = sub(v, "station");
            const JsonValue* wt = st.get("waiting");
            CHECK(wt && wt->is_array() && wt->arr.size() == 1 && str(wt->arr[0], "service") == "mars" &&
                  str(wt->arr[0], "channel") == "P1");
            CHECK(str(sub(st, "channel"), "label") == "P1" && str(sub(st, "channel"), "source") == "controller");
        }
        rep = call(*s.core, {{"cmd", "status"}});
        v = parsed(rep);
        CHECK(str(sub(v, "keying"), "controller") == "answering");

        // ---- crossings: the route function before anything is handed down ----
        const int prepares = f.count("prepare");
        // (1) A burst to a station the book now holds in another service.
        const std::string am = s.written("TEST-HAM2", "NOT ACROSS");
        CHECK(ok(call(*s.core, {{"cmd", "book_put"}, {"name", "TEST HAM"}, {"call", "TEST-HAM2"}, {"world", "amateur"}})));
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", am}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "crossing: a mars message to an amateur station"));
        // (2) A message whose service changed under it: an amateur message on a mars channel.
        const std::string w2 = s.written("FARSTN", "SERVICE CHANGES UNDER IT");
        {
            sqlite3* db = nullptr;
            sqlite3_open((s.dir / "AAA.db").c_str(), &db);
            sqlite3_exec(db, ("UPDATE messages SET world = 'amateur' WHERE id = '" + w2 + "'").c_str(), nullptr, nullptr,
                         nullptr);
            sqlite3_close(db);
        }
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", w2}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "crossing: an amateur message on a mars channel (P1)"));
        // (3) A mars burst written while the controller reported an amateur channel.
        s.set_channel(f, "AMX", "amateur");
        const std::string w3 = s.written("FARSTN", "WRITTEN ON AN AMATEUR CHANNEL");
        s.set_channel(f, "P1");
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", w3}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "crossing: a mars message on an amateur channel (AMX)"));
        // (4) The request naming another service than the message's.
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", c1}, {"service", "amateur"}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "crossing"));
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", c1}, {"world", "amateur"}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "crossing"));
        CHECK(f.count("prepare") == prepares);

        // ---- one service end to end: an amateur message, an amateur station,
        // an amateur channel the operator picked: prepared and keyed ----------
        {
            const std::string ham = s.written("TEST-HAM2", "PLAIN TEXT FOR THE HAM", "", "AMX");
            auto m = s.core->get(ham);
            CHECK(m && m->world == "amateur" && m->channel == "AMX");
            v = parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", ham}}));
            CHECK(flag(v, "ok") && str(sub(v, "confirm"), "service") == "amateur" &&
                  str(sub(v, "confirm"), "channel") == "AMX");
            auto hp = f.last("prepare");
            CHECK(hp && ctl(*hp, "service") == "amateur" && ctl(*hp, "channel") == "AMX" && ctl(*hp, "to") == "TEST-HAM2");
            s.now = s.now + 2;
            CHECK(ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", str(v, "prepare_id")}, {"confirm", "Y"}})));
            auto hk = f.last("key");
            CHECK(hk && ctl(*hk, "service") == "amateur" && ctl(*hk, "channel") == "AMX");
            CHECK(wait_for([&] { return s.state(ham) == "sent"; }, 5000));
            // A mars message picked onto that channel is refused at compose.
            std::string e;
            CHECK(s.core->submit("FARSTN", 'R', "NO", &e, SendOpts::in("mars", "AMX")).empty() &&
                  has(e, "crossing: a mars message on an amateur channel (AMX)"));
        }
        const int keys_before_auto = f.count("key");

        // An automatic send of another service is never handed down (stage 5).
        s.core->set_key_mode(am, "routing");
        s.core->set_key_mode(w2, "routing");
        s.core->set_key_mode(w3, "routing");

        // ---- standing modes: off means no key; grant on the y; revoke on off
        const std::string r1 = s.written("FARSTN", "ROUTED ONE");
        s.core->set_key_mode(r1, "routing");
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        CHECK(f.count("key") == keys_before_auto && s.state(r1) == "written");  // routing is off: not asked

        // amateur_auto is never asked for; nor a mode in another service until stage 5.
        CHECK(!ok(call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "amateur_auto"}})));
        rep = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "routing"}, {"service", "amateur"}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "stage 5"));
        const int prepares2 = f.count("prepare");

        rep = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "routing"}});
        v = parsed(rep);
        CHECK(flag(v, "ok") && no_secret(rep, f));
        const std::string sid = str(v, "prepare_id");
        CHECK(str(sub(v, "confirm"), "mode") == "routing" && str(sub(v, "confirm"), "service") == "mars");
        auto sp = f.last("prepare");
        CHECK(f.count("prepare") == prepares2 + 1 && sp && str(*sp, "kind") == "standing:routing" &&
              str(*sp, "service") == "mars");
        s.now = s.now + 2;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", sid}, {"confirm", "Y"}});
        v = parsed(rep);
        CHECK(flag(v, "ok") && flag(v, "on") && str(v, "mode") == "routing" && str(v, "service") == "mars" &&
              no_secret(rep, f));
        auto gr = f.last("grant");
        CHECK(gr && str(*gr, "mode") == "routing" && str(*gr, "service") == "mars" && str(*gr, "token") == f.tokens().back());
        CHECK(s.gk->standing_on("routing"));
        // Now the written routing burst is keyed under the grant, by itself.
        CHECK(wait_for([&] { return s.state(r1) == "sent"; }, 5000));
        auto sk = f.last("key");
        CHECK(sk && ctl(*sk, "standing") == "routing" && ctl(*sk, "control") == "automatic" &&
              !sub(*sk, "control").get("token") && ctl(*sk, "channel") == "P1" && ctl(*sk, "service") == "mars");
        const int keys_now = f.count("key");
        // The other services' ones were never asked, with routing on.
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(f.count("key") == keys_now && s.state(am) == "written" && s.state(w2) == "written" &&
              s.state(w3) == "written");
        // A machine-mode answer waits: machine is not on.
        const std::string m1 = s.written("FARSTN", "MACHINE ANSWER");
        s.core->set_key_mode(m1, "machine");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(f.count("key") == keys_now && s.state(m1) == "written");

        // Off: revoked at the controller, and the next routing burst is not asked.
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

        // The controller's summary naming another service: no standing grant is held.
        f.standing_service = "amateur";
        rep = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "machine"}});
        CHECK(!ok(rep) && s.gk->held() == 0);
        f.standing_service.clear();

        // A mode on through the y, as the operator does it.
        auto grant = [&](const std::string& mode) {
            std::string pr = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", mode}});
            const std::string id = str(parsed(pr), "prepare_id");
            s.now = s.now + 2;
            return ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", id}, {"confirm", "Y"}}));
        };

        // EMCON on: every mode off in the core, and the controller told to revoke all.
        CHECK(grant("machine") && s.gk->standing_on("machine"));
        s.core->set_emcon(true);
        CHECK(s.gk->standing().empty());
        CHECK(f.last("revoke") && str(*f.last("revoke"), "mode") == "all");
        CHECK(!ok(call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "machine"}})));  // not under EMCON
        s.core->set_emcon(false);

        // gale-key drops a grant by itself (its hourly cap): the controller accepts
        // the next automatic burst, its result file comes back refused with
        // gale-key's wording (failed), and the core turns the mode off, so the
        // one after waits for the y.
        CHECK(grant("auto_qsl") && s.gk->standing_on("auto_qsl"));
        {
            std::lock_guard<std::mutex> g(f.mu);
            f.grants.clear();
        }
        const int keys_before = f.count("key");
        const std::string q1 = s.written("FARSTN", "QSL ONE");
        s.core->set_key_mode(q1, "auto_qsl");
        CHECK(wait_for([&] { return s.state(q1) == "failed"; }, 5000));
        CHECK(s.core->get(q1) && has(s.core->get(q1)->last_error, "no standing grant"));
        CHECK(wait_for([&] { return !s.gk->standing_on("auto_qsl"); }, 2000));
        const std::string q2 = s.written("FARSTN", "QSL TWO");
        s.core->set_key_mode(q2, "auto_qsl");
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(f.count("key") == keys_before + 1 && s.state(q2) == "written");

        // ---- the controller goes away: nothing keys, the burst stays written, status says why
        f.stop();
        const std::string g1 = s.written("FARSTN", "NO CONTROLLER");
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", g1}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "the station controller isn't running"));
        v = parsed(call(*s.core, {{"cmd", "status"}}));
        CHECK(str(sub(v, "keying"), "controller") == "not answering" &&
              has(str(sub(v, "keying"), "why"), "the station controller isn't running"));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(s.state(g1) == "written");
    }

    // ---- the controller gone between the prepare and the y: not keyed, still
    // written, may be asked again
    {
        FakeController f;
        f.sock = sock;
        Station s(base / "B", "BBB", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel(f, "P2");
        const std::string id = s.written("FARSTN", "GONE BEFORE Y");
        const std::string pid = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})), "prepare_id");
        CHECK(!pid.empty());
        f.stop();
        s.now = s.now + 2;
        std::string rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "stays written"));
        CHECK(s.state(id) == "written");
        FakeController f2;
        f2.sock = sock;
        f2.outbox = s.gc.outbox_dir;
        f2.radio = "P2";
        f2.start();
        const std::string p2 = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})), "prepare_id");
        CHECK(!p2.empty());
        s.now = s.now + 2;
        CHECK(ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p2}, {"confirm", "Y"}})));
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));
    }

    // ---- no channel when the burst was written (none picked, and the
    // controller reported none): never handed down
    {
        FakeController f;
        f.sock = sock;
        Station s(base / "C", "CCC", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        const std::string id = s.written("FARSTN", "NO CHANNEL SET");
        std::string rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "no channel"));
        CHECK(f.count("prepare") == 0);
    }

    // ---- the accept reply comes at once, and the result file decides the state
    {
        FakeController f;
        f.sock = sock;
        f.key_ms = 1500;  // a key longer than the core's timeout below
        Station s(base / "K", "KKK", sock, nullptr, 500);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel(f, "P1");
        const std::string id = s.written("FARSTN", "A LONG KEY");
        const std::string pid = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})), "prepare_id");
        CHECK(!pid.empty());
        s.now = s.now + 2;
        auto t0 = std::chrono::steady_clock::now();
        std::string rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}});
        const auto took =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        JsonValue v = parsed(rep);
        // Answered and accepted well within the 500 ms timeout, long before the key ends.
        CHECK(flag(v, "ok") && flag(sub(v, "controller"), "answered") && flag(sub(v, "controller"), "accepted"));
        CHECK(took < 500);
        // The reply never changes the state: still written until the result file.
        CHECK(s.state(id) == "written");
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        CHECK(s.state(id) == "written");
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));

        // Not accepted: no result file will come, so the burst may be prepared again.
        f.refuse_key = "the radio was not free";
        const std::string id2 = s.written("FARSTN", "NOT ACCEPTED");
        const std::string p2 = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id2}})), "prepare_id");
        s.now = s.now + 2;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p2}, {"confirm", "Y"}});
        CHECK(!ok(rep) && has(str(parsed(rep), "error"), "the radio was not free") &&
              has(str(parsed(rep), "error"), "stays written"));
        CHECK(s.state(id2) == "written");
        f.refuse_key.clear();
        const std::string p3 = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id2}})), "prepare_id");
        CHECK(!p3.empty());
        s.now = s.now + 2;
        CHECK(ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p3}, {"confirm", "Y"}})));
        CHECK(wait_for([&] { return s.state(id2) == "sent"; }, 5000));
        // A refused result file fails the message, with the reason.
        {
            std::lock_guard<std::mutex> g(f.mu);
            f.live.clear();  // the next token is spent before it comes: gale-key refuses the key
        }
        const std::string id3 = s.written("FARSTN", "REFUSED BY GALE-KEY");
        const std::string p4 = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id3}})), "prepare_id");
        {
            std::lock_guard<std::mutex> g(f.mu);
            f.live.clear();
        }
        s.now = s.now + 2;
        CHECK(ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p4}, {"confirm", "Y"}})));
        CHECK(wait_for([&] { return s.state(id3) == "failed"; }, 5000));
        CHECK(s.core->get(id3) && has(s.core->get(id3)->last_error, "the token is not one I gave"));
    }

    // ---- key_prepare is bound to one written burst: its WAV, its sha256, its
    // service and channel, and the connection that opened the dialog
    {
        FakeController f;
        f.sock = sock;
        Station s(base / "E", "EEE", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel(f, "P3");
        auto err_of = [](const std::string& r) { return str(parsed(r), "error"); };

        // 1. Empty, unbound, or not a written burst: refused, the controller not asked.
        std::string rep = call(*s.core, {{"cmd", "key_prepare"}});
        CHECK(!ok(rep) && has(err_of(rep), "msg_id or a burst"));
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"burst", "NOSUCH.wav"}});
        CHECK(!ok(rep) && has(err_of(rep), "no burst of a message of ours"));
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", "NOSUCH-1"}});
        CHECK(!ok(rep) && has(err_of(rep), "no outgoing message"));
        std::string e;
        SendOpts hold = SendOpts::in("mars");
        hold.held = true;
        const std::string held = s.core->submit("FARSTN", 'R', "HELD, NOT WRITTEN", &e, hold);
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", held}});
        CHECK(!ok(rep) && has(err_of(rep), "is held, not a burst written"));
        const std::string gone = s.written("FARSTN", "WAV REMOVED");
        std::string gb;
        for (auto& ev : s.core->events(0, 5000))
            if (ev.msg_id == gone && ev.text.rfind("written to the outbox as ", 0) == 0)
                gb = ev.text.substr(25, ev.text.find(';') - 25);
        CHECK(!gb.empty() && fs::remove(fs::path(s.gc.outbox_dir) / gb));
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", gone}});
        CHECK(!ok(rep) && has(err_of(rep), "is not in the outbox"));
        rep = call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", gone}, {"burst", "OTHER.wav"}});
        CHECK(!ok(rep) && has(err_of(rep), "is not " + gone + "'s burst"));
        CHECK(f.count("prepare") == 0);

        // 3. The burst changed after the prepare: rewritten, removed, or no longer written.
        auto prep = [&](const std::string& id, std::string* burst) {
            JsonValue v = parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}}));
            if (burst) *burst = str(sub(v, "confirm"), "burst");
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
        CHECK(!ok(rep) && has(err_of(rep), "changed since the y dialog opened"));
        std::string b2;
        const std::string m2 = s.written("FARSTN", "REMOVED UNDER THE DIALOG");
        std::string p2 = prep(m2, &b2);
        fs::remove(fs::path(s.gc.outbox_dir) / b2);
        s.now = s.now + 2;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p2}, {"confirm", "Y"}});
        CHECK(!ok(rep) && has(err_of(rep), "no longer in the outbox"));
        const std::string m3 = s.written("FARSTN", "CANCELLED UNDER THE DIALOG");
        std::string p3 = prep(m3, nullptr);
        CHECK(s.core->cancel(m3, &e));
        s.now = s.now + 2;
        rep = call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p3}, {"confirm", "Y"}});
        CHECK(!ok(rep) && has(err_of(rep), "is cancelled, not a burst written"));
        CHECK(f.count("key") == 0);
        CHECK(s.state(m1) == "written" && s.state(m2) == "written");

        // 4. Never confirmed nor cancelled: dropped by the core at the token's 120 s.
        const std::string m4 = s.written("FARSTN", "LEFT OPEN");
        std::string p4 = prep(m4, nullptr);
        CHECK(!p4.empty() && s.gk->held() == 1);
        s.now = s.now + 121;
        JsonValue st = parsed(call(*s.core, {{"cmd", "status"}}));
        CHECK(sub(st, "keying").get("held") && sub(st, "keying").get("held")->n == 0);
        CHECK(!ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", p4}, {"confirm", "Y"}})));

        // 5. A connection that closes takes its dialogs with it; another
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
            CHECK(!ok(rep) && has(err_of(rep), "another connection"));
            CHECK(s.gk->held() == 1);
            ::close(a1);  // the page reloads with the dialog open
            CHECK(wait_for([&] { return s.gk->held() == 0; }, 3000));
            rep = line(a2, R"({"cmd":"key_confirm","prepare_id":")" + pa + R"(","confirm":"Y"})");
            CHECK(!ok(rep) && has(err_of(rep), "no such y dialog"));
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

    // ---- received traffic takes the service of the channel it was heard on;
    // ACK and orderwire bursts key under standing machine only, in the service
    // of what they answer (MARS only until stage 5), on the controller's
    // channel; their result is logged only
    {
        FakeController f;
        f.sock = sock;
        Station s(base / "F", "FFF", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel(f, "P1");
        auto answers = [&] {
            JsonValue v = parsed(call(*s.core, {{"cmd", "status"}}));
            const JsonValue* n = sub(v, "keying").get("answers_written");
            return n && n->type == JsonValue::Type::Number ? static_cast<int>(n->n) : -1;
        };
        auto grant = [&](const std::string& mode) {
            std::string pr = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", mode}});
            const std::string id = str(parsed(pr), "prepare_id");
            s.now = s.now + 2;
            return ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", id}, {"confirm", "Y"}}));
        };
        auto hear = [&](const std::string& name, const Frame& in) {
            const std::string bytes = encode(in);
            {
                std::ofstream b(fs::path(s.gc.captures_dir) / (name + ".m110a.0.bin"), std::ios::binary);
                b << bytes;
            }
            std::ofstream l(fs::path(s.gc.captures_dir) / (name + ".modems.json"));
            l << R"({"files":[{"file":")" << name << R"(.m110a.0.bin","bytes":)" << bytes.size() << R"(,"sha256":")"
              << sha256_hex(bytes) << R"("}]})";
        };
        // FARSTN is a MARS station in the book: an orderwire to it takes its service.
        CHECK(ok(call(*s.core, {{"cmd", "book_put"}, {"name", "FARSTN"}, {"kind", "station"}, {"world", "mars"}})));
        // A message to us, heard on P1's dial (the capture's name carries it).
        Frame in;
        in.type = FrameType::Msg;
        in.id = "FARSTN-7";
        in.src = "FARSTN";
        in.dst = "FFF";
        in.prec = 'R';
        in.body = "HELLO FFF";
        hear("in_5000kHz", in);
        CHECK(wait_for([&] { return s.core->answer_bursts() == 1; }, 10000));
        {
            auto m = s.core->get("FARSTN-7");
            CHECK(m && m->world == "mars");
        }
        // One heard on an amateur channel's dial is of the amateur service, and no
        // automatic ACK answers it.
        Frame ham = in;
        ham.id = "TEST-HAM3-1";
        ham.src = "TEST-HAM3";
        ham.body = "HELLO FROM THE HAM SIDE";
        hear("ham_15000kHz", ham);
        CHECK(wait_for([&] { return s.core->get("TEST-HAM3-1").has_value(); }, 10000));
        CHECK(s.core->get("TEST-HAM3-1")->world == "amateur");
        std::string err;
        CHECK(s.core->orderwire("FARSTN", "QSL QRU", &err));
        CHECK(wait_for([&] { return s.core->answer_bursts() >= 2; }, 10000));
        const int written_answers = static_cast<int>(s.core->answer_bursts());
        CHECK(answers() == written_answers);
        // Machine off: never asked, routing on or not.
        CHECK(grant("routing"));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(f.count("key") == 0);
        // Machine on: the MARS ones keyed under it; the amateur one never.
        CHECK(grant("machine"));
        CHECK(wait_for([&] { return f.count("key") == 2; }, 5000));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(f.count("key") == 2);
        {
            std::lock_guard<std::mutex> g(f.mu);
            for (auto& r : f.got) {
                if (str(r, "cmd") != "key") continue;
                CHECK(ctl(r, "standing") == "machine" && ctl(r, "control") == "automatic" && ctl(r, "service") == "mars" &&
                      ctl(r, "channel") == "P1" && !sub(r, "control").get("token"));
            }
        }
        const std::string ev = s.events_text();
        CHECK(has(ev, "ack to FARSTN written to the outbox"));
        CHECK(has(ev, "orderwire to FARSTN written to the outbox"));
        CHECK(wait_for([&] { return has(s.events_text(), ") keyed by gale-key"); }, 5000));
        // Its result changes no message: the one acked stays as it was.
        auto heard = s.core->get("FARSTN-7");
        CHECK(heard && heard->state == "received");

        // An ACK of a message with no service (a station not in the book, heard
        // on a dial no channel of the plan is on) is never keyed, machine on.
        {
            const size_t before_n = s.core->answer_bursts();
            Frame un = in;
            un.id = "TEST-HAM5-1";
            un.src = "TEST-HAM5";
            un.body = "HEARD ON NO CHANNEL OF THE PLAN";
            hear("un_9999kHz", un);
            CHECK(wait_for([&] { return s.core->get("TEST-HAM5-1").has_value(); }, 10000));
            CHECK(s.core->get("TEST-HAM5-1")->world.empty());
            // Refused before it is even written: an ACK is MARS only until stage 5.
            CHECK(wait_for([&] { return has(s.events_text(), "not sent to TEST-HAM5: it answers a message with no service"); },
                           10000));
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            CHECK(f.count("key") == 2 && !has(s.events_text(), "ack to TEST-HAM5 written"));
            (void)before_n;
        }
        // An ACK of a MARS message written while the controller reported an
        // amateur channel: the route function refuses the crossing, never keyed.
        {
            s.set_channel(f, "AMX", "amateur");
            Frame m8 = in;
            m8.id = "FARSTN-8";
            m8.body = "HELLO AGAIN FFF";
            hear("in8_5000kHz", m8);
            CHECK(wait_for([&] { return has(s.events_text(), "ack to FARSTN written to the outbox as") &&
                                        s.core->get("FARSTN-8").has_value(); },
                           10000));
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            CHECK(f.count("key") == 2);
            CHECK(has(s.events_text(), "crossing: a mars message on an amateur channel (AMX)"));
            s.set_channel(f, "P1");
        }

        // A burst to a station the book marks amateur after it was written: never asked.
        CHECK(ok(call(*s.core, {{"cmd", "standing_off"}, {"mode", "machine"}})));
        const size_t before = s.core->answer_bursts();
        CHECK(ok(call(*s.core, {{"cmd", "book_put"}, {"name", "TEST HAM Y"}, {"call", "TEST-HAM4"}, {"world", "mars"}})));
        CHECK(s.core->orderwire("TEST-HAM4", "QRV", &err));
        CHECK(wait_for([&] { return s.core->answer_bursts() == before + 1; }, 10000));
        CHECK(ok(call(*s.core, {{"cmd", "book_put"}, {"name", "TEST HAM Y"}, {"call", "TEST-HAM4"}, {"world", "amateur"}})));
        CHECK(grant("machine"));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(f.count("key") == 2);
        CHECK(has(s.events_text(), "TEST-HAM4 is an amateur station"));
    }

    // ---- radio_channel is the controller's report, not a label the core was told
    {
        FakeController f;
        f.sock = sock;
        auto ft = std::make_shared<FakeTuner>();
        Station s(base / "G", "GGG", sock, ft);
        f.outbox = s.gc.outbox_dir;
        auto is_null = [](const JsonValue& v, const char* k) {
            const JsonValue* x = v.get(k);
            return x && x->type == JsonValue::Type::Null;
        };
        // The controller has not answered: label, service and since are null.
        JsonValue rc = sub(parsed(call(*s.core, {{"cmd", "status"}})), "radio_channel");
        CHECK(rc.is_object() && is_null(rc, "label") && is_null(rc, "service") && is_null(rc, "since") &&
              str(rc, "source") == "controller");
        f.start();
        // tuner_channel still reaches the tuner daemon, and does not set radio_channel.
        CHECK(ok(call(*s.core, {{"cmd", "tuner_channel"}, {"channel", "P1"}})));
        s.gk->refresh_radio();
        rc = sub(parsed(call(*s.core, {{"cmd", "status"}})), "radio_channel");
        CHECK(is_null(rc, "label"));
        // The controller's report: label, service, when, and where it came from.
        s.set_channel(f, "P1");
        std::string st = call(*s.core, {{"cmd", "status"}});
        rc = sub(parsed(st), "radio_channel");
        CHECK(str(rc, "label") == "P1" && str(rc, "service") == "mars" && str(rc, "world") == "mars" &&
              str(rc, "since") == "2099-01-01T00:00:00Z" && str(rc, "source") == "controller");
        CHECK(!has(st, "5000"));
        std::string ks = call(*s.core, {{"cmd", "key_status"}});
        JsonValue kc = sub(sub(parsed(ks), "station"), "channel");
        CHECK(str(kc, "label") == "P1" && str(kc, "service") == "mars" && str(kc, "source") == "controller");
        CHECK(!has(ks, "5000") && no_frequency_in(ks));
        s.set_channel(f, "AMX", "amateur");
        rc = sub(parsed(call(*s.core, {{"cmd", "status"}})), "radio_channel");
        CHECK(str(rc, "label") == "AMX" && str(rc, "service") == "amateur");
        // The poll keeps it current by itself.
        s.gk->set_poll_ms(50);
        f.set_radio("P2", "mars");
        CHECK(wait_for([&] { return str(sub(parsed(call(*s.core, {{"cmd", "status"}})), "radio_channel"), "label") == "P2"; },
                       3000));
        s.gk->set_poll_ms(0);
        // A burst written now goes on the controller's channel; the core runs
        // no tuner sequence around the key (the controller does).
        s.set_channel(f, "P1");
        const int pre = ft->count("tuner_pre_tx"), post = ft->count("tuner_post_tx");
        const std::string id = s.written("FARSTN", "CHANNEL FROM THE CONTROLLER");
        const std::string pid = str(parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}})), "prepare_id");
        CHECK(!pid.empty());
        s.now = s.now + 2;
        CHECK(ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}})));
        CHECK(f.last("key") && ctl(*f.last("key"), "channel") == "P1");
        CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));
        CHECK(ft->count("tuner_pre_tx") == pre && ft->count("tuner_post_tx") == post);
    }

    // ---- the operator's requeue or release makes an automatic send the
    // operator's own: a standing grant never keys it, only key_prepare and a y
    {
        FakeController f;
        f.sock = sock;
        Station s(base / "J", "JJJ", sock);
        f.outbox = s.gc.outbox_dir;
        f.start();
        s.set_channel(f, "P1");
        // Requeued: written by routing, put back by the operator, written again.
        const std::string rq = s.written("FARSTN", "ROUTED THEN REQUEUED");
        s.core->set_key_mode(rq, "routing");
        std::string err;
        CHECK(s.core->requeue(rq, &err));
        CHECK(wait_for([&] { return s.state(rq) == "written"; }, 10000));
        // Released: held while marked routing, then released by the operator.
        err.clear();
        SendOpts hold = SendOpts::in("mars");
        hold.held = true;
        const std::string hd = s.core->submit("FARSTN", 'R', "ROUTED THEN RELEASED", &err, hold);
        CHECK(!hd.empty());
        s.core->set_key_mode(hd, "routing");
        CHECK(s.core->release(hd, &err));
        CHECK(wait_for([&] { return s.state(hd) == "written"; }, 10000));

        // gale-web polls for written: queue keeps a written message, after
        // those still to be written, with its state; get gives the same state.
        {
            const std::string q1 = s.core->submit("FARSTN", 'R', "STILL QUEUED", &err, hold);
            s.core->set_emcon(true);
            CHECK(s.core->release(q1, &err));
            JsonValue q = parsed(call(*s.core, {{"cmd", "queue"}}));
            const JsonValue* ms = q.get("messages");
            std::vector<std::pair<std::string, std::string>> got;
            if (ms && ms->type == JsonValue::Type::Array)
                for (auto& m : ms->arr) got.emplace_back(str(m, "id"), str(m, "state"));
            CHECK(got.size() == 3);
            CHECK(got.size() == 3 && got[0] == std::make_pair(q1, std::string("queued")) &&
                  got[1] == std::make_pair(rq, std::string("written")) &&
                  got[2] == std::make_pair(hd, std::string("written")));
            JsonValue g = parsed(call(*s.core, {{"cmd", "get"}, {"id", rq}}));
            const JsonValue* gm = g.get("messages");
            CHECK(gm && gm->type == JsonValue::Type::Array && gm->arr.size() == 1 &&
                  str(gm->arr[0], "state") == "written");
            CHECK(s.core->cancel(q1, &err));
            s.core->set_emcon(false);
        }

        std::string pr = call(*s.core, {{"cmd", "standing_prepare"}, {"mode", "routing"}});
        const std::string sid = str(parsed(pr), "prepare_id");
        s.now = s.now + 2;
        CHECK(ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", sid}, {"confirm", "Y"}})));
        CHECK(s.gk->standing_on("routing"));
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        CHECK(f.count("key") == 0 && s.state(rq) == "written" && s.state(hd) == "written");

        // The operator's y keys each, as a local key with a token.
        for (const std::string& id : {rq, hd}) {
            JsonValue v = parsed(call(*s.core, {{"cmd", "key_prepare"}, {"msg_id", id}}));
            const std::string pid = str(v, "prepare_id");
            CHECK(!pid.empty());
            s.now = s.now + 2;
            CHECK(ok(call(*s.core, {{"cmd", "key_confirm"}, {"prepare_id", pid}, {"confirm", "Y"}})));
            auto k = f.last("key");
            CHECK(k && !sub(*k, "control").get("standing") && ctl(*k, "control") == "local");
            CHECK(wait_for([&] { return s.state(id) == "sent"; }, 5000));
        }
    }

    // ---- a station with no controller at all: the calls say so, status says why
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
        c.set_services(test_services());
        c.start();
        CHECK(!ok(call(c, {{"cmd", "key_status"}})));
        JsonValue v = parsed(call(c, {{"cmd", "status"}}));
        CHECK(str(sub(v, "keying"), "controller") == "none" && str(sub(v, "keying"), "gale_key") == "none");
        c.stop();
    }

    fs::remove_all(base);
    return check::report("test_gale_key");
}
