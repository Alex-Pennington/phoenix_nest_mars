// The ACP 127 handling the core does beside its traffic: orderwire, the raw
// receive log, giving up on a link after a set wait, handling times and
// expiry, and a file carried as base32 text inside a message. Calls are made
// up; nothing here touches a cipher.
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/filemsg.h"
#include "../src/frame.h"
#include "../src/json.h"
#include "check.h"
#include "services_fixture.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <sqlite3.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace pna;
namespace fs = std::filesystem;

class ScriptModem : public ModemDriver {
public:
    void start(Callbacks cb) override { cb_ = std::move(cb); }
    void stop() override {}
    bool ready() const override { return true; }
    bool busy() const override {
        std::lock_guard<std::mutex> g(mu_);
        return on_air_;
    }
    bool send(const std::string& bytes) override {
        std::lock_guard<std::mutex> g(mu_);
        if (on_air_) return false;
        on_air_ = true;
        sent_.push_back(bytes);
        return true;
    }
    std::string describe() const override { return "script"; }
    void finish(bool ok = true) {
        {
            std::lock_guard<std::mutex> g(mu_);
            if (!on_air_) return;
            on_air_ = false;
        }
        cb_.on_tx_done(ok, ok ? "" : "test failure");
    }
    void rx(const std::string& b) { cb_.on_rx(b); }
    size_t count() const {
        std::lock_guard<std::mutex> g(mu_);
        return sent_.size();
    }
    std::string last() const {
        std::lock_guard<std::mutex> g(mu_);
        return sent_.empty() ? "" : sent_.back();
    }

private:
    Callbacks cb_;
    mutable std::mutex mu_;
    bool on_air_ = false;
    std::vector<std::string> sent_;
};

static bool wait_for(const std::function<bool()>& cond, int ms = 5000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}
static std::string state_of(Core& c, const std::string& id) {
    auto m = c.get(id);
    return m ? m->state : "";
}
static bool logged(Core& c, const std::string& text) {
    for (auto& e : c.events(0, 1000))
        if (e.text.find(text) != std::string::npos) return true;
    return false;
}
static JsonValue api(Core& c, const std::string& line) {
    JsonValue v;
    parse_json(ApiServer::handle(c, line), v);
    return v;
}
static std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static bool ok(const JsonValue& v) {
    const JsonValue* o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}
static std::unique_ptr<Core> make_core(const fs::path& dir, const std::string& call, ScriptModem*& modem,
                                       std::function<void(CoreConfig&)> tune = nullptr) {
    fs::create_directories(dir);
    auto m = std::make_unique<ScriptModem>();
    modem = m.get();
    CoreConfig cfg{call, (dir / (call + ".db")).string(), 3, 1, true};
    cfg.rx_service = "mars";  // a channel-less test link: MARS traffic
    cfg.ack_timeout_s = 0;
    if (tune) tune(cfg);
    auto c = std::make_unique<Core>(cfg, std::move(m));
    c->set_services(test_services());
    c->start();
    return c;
}
// Moves a message's filing time back, as if it had waited that long.
static void age(const fs::path& db, const std::string& id, int seconds) {
    sqlite3* h = nullptr;
    CHECK(sqlite3_open(db.string().c_str(), &h) == SQLITE_OK);
    sqlite3_busy_timeout(h, 2000);
    std::string sql = "UPDATE messages SET created_at = created_at - " + std::to_string(seconds) + " WHERE id = '" + id + "'";
    CHECK(sqlite3_exec(h, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(h);
}

static void orderwire(const fs::path& dir) {
    ScriptModem* m;
    auto c = make_core(dir, "NCS-A", m);
    std::string err;
    CHECK(!c->orderwire("STA-B", "TWO LINES\nNOT ALLOWED", &err) && !err.empty());
    CHECK(!c->orderwire("STA-B", "", &err));
    // Sent as typed, no frame, ahead of traffic.
    c->set_emcon(true);
    std::string id = c->submit("STA-B", 'R', "SOME TRAFFIC", &err, SendOpts::in("mars"));
    CHECK(ok(api(*c, R"({"cmd":"orderwire","to":"STA-B","text":"QSL QRU NCS-A"})")));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(m->count() == 0);  // EMCON holds orderwire too
    c->set_emcon(false);
    CHECK(wait_for([&] { return m->count() == 1; }));
    CHECK(m->last() == "QSL QRU NCS-A\r\n");
    m->finish();
    CHECK(logged(*c, "orderwire to STA-B: QSL QRU NCS-A"));
    CHECK(wait_for([&] { return m->count() == 2; }));  // then the traffic
    m->finish();
    CHECK(wait_for([&] { return state_of(*c, id) == "sent"; }));
    // No station: it goes to ALL.
    CHECK(c->orderwire("", "QST CHECK IN", &err));
    CHECK(wait_for([&] { return m->count() == 3; }));
    m->finish();
    CHECK(logged(*c, "orderwire to ALL: QST CHECK IN"));
}

static void raw_log(const fs::path& dir) {
    ScriptModem* m;
    auto c = make_core(dir, "NCS-A", m, [&](CoreConfig& cfg) { cfg.raw_rx_dir = (dir / "raw").string(); });
    m->rx("STA-B DE STA-C QRV\r\n");
    m->rx(std::string("NOISE\x01\x02", 7));
    auto v = api(*c, R"({"cmd":"rx_raw","limit":4000})");
    CHECK(ok(v));
    std::string t = str(v, "text");
    CHECK(t.find("STA-B DE STA-C QRV") != std::string::npos);
    CHECK(t.find("NOISE..") != std::string::npos);
    CHECK(t.find("== ") == 0);
    // Off: nothing kept.
    ScriptModem* m2;
    auto c2 = make_core(dir / "off", "NCS-A", m2);
    m2->rx("HEARD");
    CHECK(str(api(*c2, R"({"cmd":"rx_raw"})"), "text").empty());
}

static void link_wait(const fs::path& dir) {
    ScriptModem* m;
    auto c = make_core(dir, "NCS-A", m, [](CoreConfig& cfg) {
        cfg.link_gate = true;  // and no link source: no station ever has a link
        cfg.link_wait_s = 1;
    });
    std::string err, id = c->submit("STA-B", 'P', "WAITS FOR A LINK", &err, SendOpts::in("mars"));
    CHECK(wait_for([&] { return logged(*c, "waiting for a link to STA-B"); }));
    CHECK(wait_for([&] { return state_of(*c, id) == "failed"; }, 4000));
    CHECK(c->get(id)->last_error == "no link to STA-B after 1 s");
    CHECK(logged(*c, "gave up: no link to STA-B after 1 s"));
    CHECK(m->count() == 0);
    CHECK(c->requeue(id, &err) && state_of(*c, id) == "queued");
}

static void handling_and_expiry(const fs::path& dir) {
    ScriptModem* m;
    auto c = make_core(dir, "NCS-A", m, [](CoreConfig& cfg) {
        cfg.handling_min[3] = 60;  // ROUTINE: an hour
        cfg.expire_h = 2;
    });
    std::string err;
    std::string late = c->submit("STA-B", 'R', "LATE BUT NOT EXPIRED", &err, SendOpts::in("mars"));
    std::string old = c->submit("STA-B", 'R', "TOO OLD TO SEND", &err, SendOpts::in("mars"));
    std::string fresh = c->submit("STA-B", 'R', "JUST FILED", &err, SendOpts::in("mars"));
    CHECK(c->hold(late, &err) && c->hold(old, &err) && c->hold(fresh, &err));
    age(dir / "NCS-A.db", late, 90 * 60);
    age(dir / "NCS-A.db", old, 3 * 3600);
    CHECK(wait_for([&] { return state_of(*c, old) == "expired"; }));
    CHECK(wait_for([&] { return logged(*c, "past the handling time for ROUTINE (60 min) and still held"); }));
    auto ids = c->overdue();
    CHECK(ids.size() == 1 && ids[0] == late);
    CHECK(state_of(*c, late) == "held" && state_of(*c, fresh) == "held");
    CHECK(logged(*c, "expired: still held 2 h after it was filed"));
    // An expired message sent again by hand is not expired a second time.
    CHECK(c->requeue(old, &err));
    CHECK(wait_for([&] { return m->count() == 1; }));
    m->finish();
    CHECK(wait_for([&] { return state_of(*c, old) == "sent"; }));
}

static void files(const fs::path& dir) {
    // The block itself.
    std::string data;
    for (int i = 0; i < 300; ++i) data.push_back(static_cast<char>(i * 7));
    std::string err;
    std::string block = filemsg::encode("net-log 11.txt", data, &err);
    CHECK(!block.empty());
    for (char ch : block)
        CHECK(ch == '\n' || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ');
    bool present = false;
    filemsg::File f;
    CHECK(filemsg::find("SOME TEXT\n" + block + "MORE\n", &present, f, &err) && present);
    CHECK(f.name == "net-log 11.txt" && f.data == data);
    std::string bad = block;
    bad[bad.find("NAME") + 80] = bad[bad.find("NAME") + 80] == 'A' ? 'B' : 'A';
    CHECK(!filemsg::find(bad, &present, f, &err) && present && err.find("CRC") != std::string::npos);
    CHECK(!filemsg::find(block.substr(0, block.size() - 9), &present, f, &err) && present);
    CHECK(!filemsg::find("NO FILE HERE", &present, f, &err) && !present);
    CHECK(filemsg::encode("a/b", data, &err).empty());
    CHECK(filemsg::encode(std::string(41, 'x'), data, &err).empty());

    // Sent inside an ACP 127 message, read at the far end.
    ScriptModem* m;
    auto a = make_core(dir / "a", "NCS-A", m);
    ScriptModem* mb;
    auto b = make_core(dir / "b", "STA-B", mb);
    std::string b64 = filemsg::base64(data);
    auto v = api(*a, std::string(R"({"cmd":"send_file","service":"mars","to_station":"STA-B","prec":"R","channel":"QXA","routing":"RXAAAA",)") +
                         R"("originator_ri":"RXCCCC","from":"RXCCCC/TEST STATION ONE","to":"RXAAAA/TEST STATION TWO",)" +
                         R"("text":"NET LOG ATTACHED","file_name":"net-log 11.txt","file_b64":")" + b64 + R"("})");
    CHECK(ok(v));
    std::string id = str(v, "id");
    CHECK(!id.empty());
    CHECK(wait_for([&] { return m->count() == 1; }));
    std::string on_air = m->last();
    m->finish();
    mb->rx(on_air);
    CHECK(wait_for([&] { return b->get(id).has_value(); }));
    CHECK(logged(*b, "a file came in: net-log 11.txt, 300 bytes, CRC good"));
    auto g = api(*b, R"({"cmd":"get_file","id":")" + id + R"("})");
    CHECK(ok(g) && str(g, "name") == "net-log 11.txt" && str(g, "file_b64") == b64);
    CHECK(!ok(api(*a, R"({"cmd":"send_file","service":"mars","to_station":"STA-B","file_name":"x","file_b64":"!!"})")));
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_acp127_ops_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    orderwire(dir / "ow");
    raw_log(dir / "raw");
    link_wait(dir / "wait");
    handling_and_expiry(dir / "age");
    files(dir / "files");
    fs::remove_all(dir);
    return check::report("test_acp127_ops");
}
