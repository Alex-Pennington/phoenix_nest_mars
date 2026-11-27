// Services and the one route function (docs/api.md, "Services"; greybeard-ale
// #18 v2.2). Routing is the only guard between services: a message takes its
// destination's book service, else the one the operator picked; a crossing of
// message, destination, route or channel is refused naming both services, and
// never rerouted. One crossing test per send path, and the airtime point behind
// them. Automatic modes keep their MARS-only refusals until stage 5.
// Every call and every frequency here is made up.
#include "../src/acp127.h"
#include "../src/api_server.h"
#include "../src/bbs.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/frame.h"
#include "../src/loopback_modem.h"
#include "../src/modem.h"
#include "../src/ops.h"
#include "../src/route.h"
#include "../src/services.h"
#include "../src/station_view.h"
#include "check.h"
#include "services_fixture.h"

#include <sqlite3.h>
#include <unistd.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <thread>

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
static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (c == '\n') {
            o += "\\n";
            continue;
        }
        if (c == '\r') {
            o += "\\r";
            continue;
        }
        o += c;
    }
    return o;
}
static JsonValue call(Core& c, std::initializer_list<std::pair<std::string, std::string>> kv) {
    std::string line = "{";
    bool first = true;
    for (auto& [k, v] : kv) {
        if (!first) line += ",";
        first = false;
        line += "\"" + k + "\":\"" + esc(v) + "\"";
    }
    line += "}";
    JsonValue out;
    parse_json(ApiServer::handle(c, line), out);
    return out;
}
static bool ok(const JsonValue& v) {
    const JsonValue* o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}
static std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.is_object() ? v.get(k) : nullptr;
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
// Refused as a crossing, naming both services.
static bool crossing(const std::string& why, const std::string& a, const std::string& b) {
    return has(why, "crossing") && has(why, a) && has(why, b);
}
static bool crossed(const JsonValue& v, const std::string& a, const std::string& b) {
    return !ok(v) && crossing(str(v, "error"), a, b);
}
static int out_count(Core& c) {
    int n = 0;
    for (auto& m : c.list(1000))
        if (m.direction == "out") ++n;
    return n;
}
static bool logged(Core& c, const std::string& s) {
    for (auto& e : c.events(0, 2000))
        if (has(e.text, s)) return true;
    return false;
}
static void write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << s;
}
static void set_world(const fs::path& db, const std::string& id, const std::string& world) {
    sqlite3* h = nullptr;
    sqlite3_open(db.c_str(), &h);
    // The core writes this file too. Wait for its lock: a write that met it
    // was dropped unsaid, and the checks after it then read the old service.
    sqlite3_busy_timeout(h, 5000);
    CHECK(sqlite3_exec(h, ("UPDATE messages SET world = '" + world + "' WHERE id = '" + id + "'").c_str(), nullptr,
                       nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(h);
}
static std::string chat(const std::string& to, const std::string& from, const std::string& text) {
    return to + " DE " + from + " // " + text + " // 1200Z K";
}
static CoreConfig link_in(CoreConfig c, const std::string& service) {
    c.rx_service = service;
    return c;
}

// A modem that hears bursts as the greybeard modem does: each capture's decode
// (its name carries the dial) and then the same bytes (modem.h, on_decode).
struct FakeDecoder : ModemDriver {
    Callbacks cb;
    std::vector<std::thread> done;
    void start(Callbacks c) override { cb = std::move(c); }
    void stop() override {
        for (auto& t : done) t.join();
        done.clear();
    }
    bool ready() const override { return true; }
    bool busy() const override { return false; }
    bool send(const std::string&) override {
        done.emplace_back([this] { cb.on_tx_done(true, ""); });
        return true;
    }
    std::string describe() const override { return "fake decoder"; }
    void hear(const std::string& capture, const std::string& bytes) {
        ModemDecode d;
        d.bytes = bytes;
        d.source = capture;
        d.waveform = "110A";
        if (cb.on_decode) cb.on_decode(d);
        if (cb.on_rx) cb.on_rx(bytes);
    }
};

static std::string acp127_to(const std::string& to, const std::string& text) {
    acp127::Message m;
    m.channel = "QXA";
    m.routing = {to};
    m.originator_ri = "TEST-OR";
    m.station_serial = "1";
    m.mars_fl3 = true;
    m.from = {"TEST-OR", "TEST ORIGINATOR"};
    m.to = {{to, "TEST STATION"}};
    m.text = text;
    m.dtg.year = 2099;
    std::string e;
    return acp127::build(m, &e);
}

// Received traffic takes the service of the channel it was heard on, through
// the decode path: the plan's channel on the dial the capture's name carries.
static void decode_path(const fs::path& dir) {
    auto md = std::make_unique<FakeDecoder>();
    FakeDecoder* air = md.get();
    CoreConfig cz{"STA-Z", (dir / "z.db").string(), 3, 1, true};
    cz.loose_quiet_s = 1;
    Core z(cz, std::move(md));
    z.set_services(test_services());
    auto ops = std::make_shared<Ops>("STA-Z", (dir / "z-ops.db").string());
    z.set_ops(ops);
    std::string e;
    Channel m1, h1;
    m1.name = "M1", m1.hz = 5100000, m1.world = "mars";
    h1.name = "H1", h1.hz = 5200000, h1.world = "amateur";
    CHECK(ops->channels().put(m1, &e) && ops->channels().put(h1, &e));
    z.start();
    auto frame = [](const std::string& id, const std::string& body) {
        Frame f;
        f.type = FrameType::Msg;
        f.id = id;
        f.src = "TEST-FAR";
        f.dst = "STA-Z";
        f.body = body;
        return encode(f);
    };
    air->hear("a_5100kHz.m110a.0.bin", frame("TEST-FAR-1", "ON THE MARS CHANNEL"));
    air->hear("b_5200kHz.m110a.0.bin", frame("TEST-FAR-2", "ON THE AMATEUR CHANNEL"));
    CHECK(wait_for([&] { return z.get("TEST-FAR-1") && z.get("TEST-FAR-2"); }));
    CHECK(z.get("TEST-FAR-1") && z.get("TEST-FAR-1")->world == "mars");
    CHECK(z.get("TEST-FAR-2") && z.get("TEST-FAR-2")->world == "amateur");
    // The decode's own row, kept as it came, takes its capture's channel's service too.
    for (auto& m : z.list(100))
        if (m.state == "unverified" && has(m.body, "ON THE AMATEUR CHANNEL")) CHECK(m.world == "amateur");

    // Bare text cut short in one capture, and a whole message in the next of
    // another service: the first is taken as it stands with its own capture's
    // service, never mixed into the next one's.
    const std::string whole = acp127_to("STA-Z", "WHOLE ON MARS");
    std::string part = acp127_to("STA-Z", "CUT SHORT ON THE AMATEUR CHANNEL");
    part = part.substr(0, part.find("CUT SHORT") + 9);
    air->hear("c_5200kHz.m110a.0.bin", part);
    air->hear("d_5100kHz.m110a.0.bin", whole);
    std::optional<Message> cut, full;
    CHECK(wait_for(
        [&] {
            for (auto& m : z.list(100)) {
                if (m.direction != "in" || m.state == "unverified") continue;  // not the decode's own row
                if (has(m.body, "CUT SHORT")) cut = m;
                if (has(m.body, "WHOLE ON MARS") && !has(m.body, "CUT SHORT")) full = m;
            }
            return cut && full;
        },
        8000));
    CHECK(cut && cut->world == "amateur" && !has(cut->body, "WHOLE ON MARS"));
    CHECK(full && full->world == "mars");
    z.stop();
    air->stop();

    // --rx-service is for a link that names no channel: a decode with no plan
    // channel on its dial stays of no service, whatever the link says.
    auto mw = std::make_unique<FakeDecoder>();
    FakeDecoder* air_w = mw.get();
    CoreConfig cw{"STA-W", (dir / "w.db").string(), 3, 1, true};
    cw.rx_service = "amateur";
    Core w(cw, std::move(mw));
    w.set_services(test_services());
    w.set_ops(std::make_shared<Ops>("STA-W", (dir / "w-ops.db").string()));
    CHECK(w.ops()->channels().put(m1, &e));
    w.start();
    Frame f;
    f.type = FrameType::Msg;
    f.id = "TEST-FAR-9";
    f.src = "TEST-FAR";
    f.dst = "STA-W";
    f.body = "ON NO CHANNEL OF THE PLAN";
    air_w->hear("e_9999kHz.m110a.0.bin", encode(f));
    CHECK(wait_for([&] { return w.get("TEST-FAR-9").has_value(); }));
    CHECK(w.get("TEST-FAR-9") && w.get("TEST-FAR-9")->world.empty());
    w.stop();
    air_w->stop();
}

// ---------------------------------------------------------------- the tables

static void tables() {
    // The fixtures load; valid means "has a table".
    auto s = test_services();
    CHECK(s->valid("mars") && s->valid("amateur") && !s->valid("both") && !s->valid("narnia") && !s->valid(""));
    CHECK(s->find("amateur")->offers("PLAIN") && !s->find("mars")->offers("PLAIN"));
    CHECK(s->find("amateur")->station_id && !s->find("mars")->station_id);
    CHECK(no_table("narnia") == "no table for service `narnia`");

    // The fixture tables load whatever mode the checkout gave them (a umask
    // of 0002 makes them group-writable): the tests read a 0644 copy.
    CHECK(test_services()->valid("mars") && test_services()->valid("amateur"));
    CHECK(test_services_dir() != test_services_source_dir());

    // A third service needs a table only, never code.
    std::string third;
    {
        std::ifstream f(test_services_dir() + "/amateur.json");
        std::stringstream ss;
        ss << f.rdbuf();
        third = ss.str();
    }
    auto with = [&](const std::string& from, const std::string& to) {
        std::string t = third;
        t.replace(t.find(from), from.size(), to);
        return t;
    };
    std::vector<std::string> why;
    auto x = Services::from_texts({{"drill", with("\"service\": \"amateur\"", "\"service\": \"drill\"")}}, &why);
    CHECK(x->valid("drill") && why.empty());

    // Refused whole, with the reason and never a value: the name not the stem,
    // a bad name, another version, a missing column, an unknown key, a wrong type.
    why.clear();
    x = Services::from_texts({{"other", third},
                              {"Bad", with("\"service\": \"amateur\"", "\"service\": \"Bad\"")},
                              {"v2", with("\"version\": 1", "\"version\": 2")},
                              {"nocol", with("\"transmit\": true,", "")},
                              {"extra", with("\"worked\": true,", "\"worked\": true, \"frequency\": 1,")},
                              {"wrong", with("\"encryption\": false", "\"encryption\": \"no\"")},
                              {"both", with("\"service\": \"amateur\"", "\"service\": \"both\"")}},
                             &why);
    CHECK(x->all().empty() && why.size() == 7);
    for (auto& w : why) CHECK(!has(w, "1800000") && !has(w, "14000000"));

    // All digital traffic is USB: the table has no sideband column, and a
    // table that still carries one is refused whole, as any unknown key is.
    CHECK(!has(third, "default_sideband"));
    why.clear();
    x = Services::from_texts(
        {{"amateur", with("\"passband_hz\"", "\"default_sideband\": \"usb\", \"passband_hz\"")}}, &why);
    CHECK(x->all().empty() && why.size() == 1);
    CHECK(!why.empty() && has(why[0], "amateur.json refused: unknown key radio.default_sideband"));

    // On disk: a file anyone else can write is refused, and the others still load.
    fs::path d = fs::temp_directory_path() / ("pna_services_" + std::to_string(::getpid()));
    fs::remove_all(d);
    fs::create_directories(d);
    write(d / "amateur.json", third);
    write(d / "drill.json", with("\"service\": \"amateur\"", "\"service\": \"drill\""));
    fs::permissions(d / "amateur.json", fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                                            fs::perms::others_read);
    fs::permissions(d / "drill.json", fs::perms::owner_all | fs::perms::group_write);
    Services::Options o;
    o.require_root = false;
    why.clear();
    x = Services::load(d.string(), o, &why);
    CHECK(x->valid("amateur") && !x->valid("drill") && why.size() == 1 && has(why[0], "writable by others"));
    // The station's tables are root's: the test user's file is refused there.
    why.clear();
    x = Services::load(d.string(), Services::Options(), &why);
    CHECK(::geteuid() == 0 || (!x->valid("amateur") && has(why[0], "not owned by root")));
    // No directory: no service at all, and it says why.
    why.clear();
    x = Services::load((d / "none").string(), o, &why);
    CHECK(x->all().empty() && why.size() == 1);
    fs::remove_all(d);
}

int main() {
    tables();

    fs::path dir = fs::temp_directory_path() / ("pna_worlds_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::path gale = dir / "gale";
    fs::create_directories(gale);
    sv::Paths p;
    p.gale_dir = gale.string();
    p.store_root = dir.string();
    sv::set_paths(p);

    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    auto mh = std::make_unique<LoopbackModem>(0);
    LoopbackModem::share({ma.get(), mb.get(), mh.get()});
    // STA-A's link is a MARS loopback: what it hears is MARS traffic.
    Core a(link_in(CoreConfig{"STA-A", (dir / "a.db").string(), 3, 1, true}, "mars"), std::move(ma));
    a.set_services(test_services());
    Core b(CoreConfig{"STA-B", (dir / "b.db").string(), 3, 1, true}, std::move(mb));
    b.set_services(test_services());
    // TEST-HAM1: a station the book holds in the amateur service.
    Core h(CoreConfig{"TEST-HAM1", (dir / "h.db").string(), 3, 1, true}, std::move(mh));
    h.set_services(test_services());
    auto ops = std::make_shared<Ops>("STA-A", (dir / "a-ops.db").string());
    a.set_ops(ops);
    a.start();
    b.start();
    h.start();
    std::string e;

    // ---- the book and the channel plan carry the service --------------------
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "STA-B"}, {"kind", "station"}, {"world", "mars"}})));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "HAM ONE"}, {"call", "TEST-HAM1"}, {"world", "amateur"}})));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "MIXED"}, {"kind", "collective"}, {"members", "STA-B;HAM ONE"}, {"world", "mars"}})));
    CHECK(ok(call(a, {{"cmd", "channel_put"}, {"name", "M1"}, {"frequency", "5100"}, {"world", "mars"}})));
    CHECK(ok(call(a, {{"cmd", "channel_put"}, {"name", "H1"}, {"frequency", "5200"}, {"world", "amateur"}})));
    CHECK(ops->book_find("TEST-HAM1") && ops->book_find("TEST-HAM1")->world == "amateur");
    // No service is never read as MARS: an entry or a new channel without one is refused.
    CHECK(has(str(call(a, {{"cmd", "book_put"}, {"name", "NO SERVICE"}, {"call", "TEST-NONE"}}), "error"), "service"));
    CHECK(!ops->book_find("TEST-NONE"));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "BY SERVICE"}, {"call", "TEST-SVC"}, {"service", "amateur"}})));
    CHECK(ops->book_find("TEST-SVC") && ops->book_find("TEST-SVC")->world == "amateur");
    // Every entry a reply carries says its `service` (docs/api.md, "service on
    // every record"), and `world` with the same value for one release.
    {
        auto both = [](const JsonValue& entry, const std::string& s) {
            return str(entry, "service") == s && str(entry, "world") == s;
        };
        JsonValue g = call(a, {{"cmd", "book_get"}, {"name", "TEST-SVC"}});
        const JsonValue* es = g.get("entries");
        CHECK(ok(g) && es && es->is_array() && es->arr.size() == 1 && both(es->arr[0], "amateur"));
        JsonValue l = call(a, {{"cmd", "book_list"}});
        es = l.get("entries");
        CHECK(ok(l) && es && es->is_array() && es->arr.size() == 4);
        int mars = 0, amateur = 0;
        if (es && es->is_array())
            for (auto& x : es->arr) {
                mars += both(x, "mars");
                amateur += both(x, "amateur");
            }
        CHECK(mars == 2 && amateur == 2);  // STA-B and MIXED; HAM ONE and BY SERVICE
        JsonValue rs = call(a, {{"cmd", "book_resolve"}, {"to", "STA-B"}});
        es = rs.get("stations");
        CHECK(ok(rs) && es && es->is_array() && es->arr.size() == 1 && both(es->arr[0], "mars"));
    }
    // book_export: the CSV's column is `service` (docs/api.md), and it reads
    // back with no default service given. A file of the last release, with a
    // `world` column, is still read.
    {
        const std::string csv = str(call(a, {{"cmd", "book_export"}}), "csv");
        CHECK(csv.rfind("name,kind,call,ri,ale,members,note,service\n", 0) == 0);
        CHECK(has(csv, "\nBY SERVICE,station,TEST-SVC,,,,,amateur\n") && has(csv, "\nSTA-B,station,STA-B,,,,,mars\n"));
        Ops copy("STA-X", (dir / "x-ops.db").string());
        std::vector<std::string> dropped, errors;
        CHECK(copy.book_import(csv, "", &dropped, &errors, "") == 4 && dropped.empty() && errors.empty());
        CHECK(copy.book_find("TEST-SVC") && copy.book_find("TEST-SVC")->world == "amateur");
        CHECK(copy.book_find("STA-B") && copy.book_find("STA-B")->world == "mars");
        Ops old("STA-Y", (dir / "y-ops.db").string());
        CHECK(old.book_import("name,kind,call,world\nOLD ONE,station,TEST-OLD,amateur\n", "", &dropped, &errors, "") == 1);
        CHECK(old.book_find("TEST-OLD") && old.book_find("TEST-OLD")->world == "amateur");
    }
    CHECK(has(str(call(a, {{"cmd", "channel_put"}, {"name", "N1"}, {"frequency", "5400"}}), "error"), "service"));
    CHECK(ok(call(a, {{"cmd", "channel_put"}, {"name", "M1"}, {"frequency", "5100"}})));  // an existing channel keeps its own
    CHECK(ops->channels().get("M1") && ops->channels().get("M1")->world == "mars");
    // status says when the tables are a test stand's, not root's.
    {
        JsonValue st = call(a, {{"cmd", "status"}});
        CHECK(st.get("services_unlocked") && st.get("services_unlocked")->type == JsonValue::Type::Bool &&
              st.get("services_unlocked")->b);
    }

    // ---- services_list: the tables' columns that carry no frequency ---------
    {
        JsonValue v = call(a, {{"cmd", "services_list"}});
        const JsonValue* list = v.get("services");
        CHECK(ok(v) && list && list->is_array() && list->arr.size() == 2);
        const JsonValue* am = nullptr;
        if (list && list->is_array())
            for (auto& s : list->arr)
                if (str(s, "service") == "amateur") am = &s;
        CHECK(am && str(*am, "display_name") == "Amateur" && am->get("worked") && am->get("worked")->b);
        CHECK(am && am->get("formats") && am->get("formats")->arr.size() == 1 && am->get("formats")->arr[0].s == "PLAIN");
        // No call in the amateur identity file: listen only.
        CHECK(am && am->get("listen_only") && am->get("listen_only")->b && am->get("station_id")->b);
        CHECK(!has(ApiServer::handle(a, R"({"cmd":"services_list"})"), "1800000"));
        write(gale / "amateur/own_address.txt", "# made up\nQQ0QQ\n");
        v = call(a, {{"cmd", "services_list"}});
        for (auto& s : v.get("services")->arr)
            if (str(s, "service") == "amateur") CHECK(!s.get("listen_only")->b);
    }

    // ---- the route function itself ------------------------------------------
    {
        CHECK(a.route_for("", "STA-B").ok && a.route_for("", "STA-B").service == "mars");
        CHECK(a.route_for("", "TEST-HAM1").service == "amateur");
        CHECK(crossing(a.route_for("mars", "TEST-HAM1").refusal, "a mars message", "an amateur station"));
        CHECK(crossing(a.route_for("amateur", "STA-B").refusal, "an amateur message", "a mars station"));
        CHECK(has(a.route_for("", "UNBOOKED").refusal, "no service"));
        CHECK(has(a.route_for("", "ALL").refusal, "broadcast"));
        CHECK(a.route_for("amateur", "ALL").ok && a.route_for("amateur", "UNBOOKED").ok);
        CHECK(has(a.route_for("narnia", "UNBOOKED").refusal, "no table for service `narnia`"));
        CHECK(a.route_for("mars", "STA-B", "M1").ok && a.route_for("amateur", "TEST-HAM1", "H1").ok);
        CHECK(a.route_for("", "STA-B", "M1").ok);
        CHECK(a.route_for("mars", "STA-B", "M1").refusal.empty());
        CHECK(crossing(a.route_for("amateur", "TEST-HAM1", "M1").refusal, "an amateur message", "a mars channel (M1)"));
        CHECK(crossing(a.route_for("mars", "STA-B", "H1").refusal, "a mars message", "an amateur channel (H1)"));
        CHECK(crossing(a.route_for("mars", "STA-B", "", "amateur").refusal, "a mars message", "an amateur route"));
        CHECK(has(a.route_for("mars", "STA-B", "NOPE").refusal, "no channel NOPE"));
        std::string s, why;
        CHECK(request_service("mars", "", s, &why) && s == "mars");
        CHECK(request_service("", "amateur", s, &why) && s == "amateur");  // world: the alias
        CHECK(!request_service("mars", "amateur", s, &why));
    }

    // ---- Core::submit, behind every send ------------------------------------
    CHECK(a.submit("UNBOOKED", 'R', "TEST", &e).empty() && has(e, "no service"));
    CHECK(a.submit("ALL", 'R', "TEST", &e).empty() && has(e, "broadcast"));
    CHECK(a.submit("STA-B", 'R', "TEST", &e, SendOpts::in("amateur")).empty() &&
          crossing(e, "an amateur message", "a mars station"));
    CHECK(a.submit("TEST-HAM1", 'R', "TEST", &e, SendOpts::in("mars")).empty() &&
          crossing(e, "a mars message", "an amateur station"));
    CHECK(a.submit("TEST-HAM1", 'R', "TEST", &e, SendOpts::in("", "M1")).empty() &&
          crossing(e, "an amateur message", "a mars channel (M1)"));
    CHECK(out_count(a) == 0);  // nothing queued, nothing rerouted

    // ---- send: a book name, a collective, the world alias, the gate ---------
    CHECK(crossed(call(a, {{"cmd", "send"}, {"to", "HAM ONE"}, {"body", "TEST"}, {"service", "mars"}}), "mars", "amateur"));
    CHECK(crossed(call(a, {{"cmd", "send"}, {"to", "MIXED"}, {"body", "TEST"}, {"service", "mars"}}), "mars", "amateur"));
    CHECK(crossed(call(a, {{"cmd", "send"}, {"to", "STA-B"}, {"body", "TEST"}, {"world", "amateur"}}), "amateur", "mars"));
    CHECK(crossed(call(a, {{"cmd", "send"}, {"to", "STA-B"}, {"body", "TEST"}, {"channel", "H1"}}), "mars", "amateur channel"));
    CHECK(!ok(call(a, {{"cmd", "send"}, {"to", "STA-B"}, {"body", "TEST"}, {"service", "mars"}, {"world", "amateur"}})));
    CHECK(has(str(call(a, {{"cmd", "send"}, {"to", "STA-B"}, {"body", "TEST"}, {"service", "narnia"}}), "error"),
              "no table for service `narnia`"));
    CHECK(!ok(call(a, {{"cmd", "send"}, {"to", "STA-B"}, {"body", "TEST"}, {"service", "both"}})));
    CHECK(!ok(call(a, {{"cmd", "send"}, {"to", "ALL"}, {"body", "TEST"}})));
    CHECK(out_count(a) == 0);  // not even STA-B's copy of MIXED
    {
        JsonValue wf = call(a, {{"cmd", "word_feed"}, {"world", "amateur"}});
        if (!ok(wf)) std::cerr << "word_feed: " << str(wf, "error") << "\n";
        CHECK(ok(wf));  // reading the old world marks is fine
    }

    // ---- the other operator send paths --------------------------------------
    CHECK(crossed(call(a, {{"cmd", "send_chat"}, {"to_station", "TEST-HAM1"}, {"call", "TEST-HAM1-CM"}, {"station", "STA-A"},
                           {"text", "QRU"}, {"service", "mars"}}),
                  "mars", "amateur"));
    CHECK(crossed(call(a, {{"cmd", "send_acp127"}, {"to_station", "TEST-HAM1"}, {"prec", "R"}, {"channel", "QXA"},
                           {"routing", "RXAAAA"}, {"originator_ri", "RXCCCC"}, {"from", "RXCCCC/TEST STATION ONE"},
                           {"to", "RXAAAA/TEST STATION TWO"}, {"text", "TEST"}, {"service", "mars"}}),
                  "mars", "amateur"));
    CHECK(crossed(call(a, {{"cmd", "send_acp126"}, {"to_station", "STA-B"}, {"prec", "R"}, {"calls", "STA-B"},
                           {"station", "STA-A"}, {"text", "TEST"}, {"radio_channel", "H1"}}),
                  "mars", "amateur channel (H1)"));
    auto compose16 = [&](const std::string& to, const std::string& service) {
        return call(a, {{"cmd", "compose_send"},
                        {"template", "FREE_FORM"},
                        {"procedure", "16L_P2P_FULL"},
                        {"prec", "R"},
                        {"routing", to},
                        {"originator", "STA-A"},
                        {"from", "TEST STATION ONE"},
                        {"to", "RXAAAA/TEST STATION TWO"},
                        {"to_station", to},
                        {"sign", "N"},
                        {"text", "TEST"},
                        {"service", service}});
    };
    CHECK(crossed(compose16("TEST-HAM1", "mars"), "mars", "amateur"));
    // A service offers only the formats its table lists: no 16L in amateur.
    CHECK(has(str(compose16("TEST-HAM1", ""), "error"), "not a format of the amateur service"));
    CHECK(out_count(a) == 0);

    // ---- a send's format and cipher against its service's table ------------
    {
        auto formats_refused = [&](const JsonValue& v, const std::string& fmt) {
            return !ok(v) && has(str(v, "error"), fmt + " is not a format of the amateur service");
        };
        CHECK(formats_refused(call(a, {{"cmd", "send_acp127"}, {"to_station", "TEST-HAM1"}, {"prec", "R"}, {"channel", "QXA"},
                                       {"routing", "TEST-HAM1"}, {"originator_ri", "STA-A"}, {"from", "STA-A/TEST ONE"},
                                       {"to", "TEST-HAM1/TEST HAM"}, {"text", "TEST"}}),
                              "ACP127_PLAINDRESS"));
        CHECK(formats_refused(call(a, {{"cmd", "send_acp126"}, {"to_station", "TEST-HAM1"}, {"prec", "R"}, {"calls", "TEST-HAM1"},
                                       {"station", "STA-A"}, {"text", "TEST"}}),
                              "ACP126_SIMPLE"));
        CHECK(formats_refused(call(a, {{"cmd", "send_chat"}, {"to_station", "TEST-HAM1"}, {"call", "TEST-HAM1"},
                                       {"station", "STA-A"}, {"text", "QRU"}}),
                              "ACP201_CHAT"));
        // A raw send is checked by the format its text reads as.
        CHECK(formats_refused(call(a, {{"cmd", "send"}, {"to", "TEST-HAM1"}, {"body", acp127_to("TEST-HAM1", "RAW ACP 127")}}),
                              "ACP127_PLAINDRESS"));
        // Enciphered traffic only in a service whose table allows it.
        CHECK(a.submit_codress("TEST-HAM1", 'R', "VZCZC CODRESS", "CLEAR", "QXR01", &e).empty() &&
              has(e, "the amateur service carries no enciphered traffic"));
        // One send, one service: a collective, or targets, of two services is refused whole.
        JsonValue v = call(a, {{"cmd", "send"}, {"to", "MIXED"}, {"body", "TO BOTH"}});
        CHECK(!ok(v) && has(str(v, "error"), "two services"));
        v = call(a, {{"cmd", "compose_send"}, {"template", "FREE_FORM"}, {"procedure", "16L_P2P_FULL"}, {"prec", "R"},
                     {"routing", "STA-B"}, {"originator", "STA-A"}, {"from", "TEST STATION ONE"},
                     {"to", "RXAAAA/TEST STATION TWO"}, {"to_station", "MIXED"}, {"sign", "N"}, {"text", "TEST"}});
        CHECK(!ok(v) && has(str(v, "error"), "two services"));
        CHECK(out_count(a) == 0);
    }

    // ---- PLAIN: the operator's text alone, in a service that offers it ------
    {
        JsonValue v = call(a, {{"cmd", "compose_send"}, {"template", "PLAIN"}, {"to_station", "TEST-HAM1"},
                               {"text", "QRV\nsee you on the net"}});
        CHECK(ok(v) && str(v, "procedure") == "PLAIN" && v.get("signed") && !v.get("signed")->b);
        auto m = a.get(str(v, "id"));
        CHECK(m && m->world == "amateur" && m->link == "plain" && m->body == "QRV\r\nsee you on the net");
        CHECK(has(str(call(a, {{"cmd", "compose_send"}, {"template", "PLAIN"}, {"to_station", "STA-B"}, {"text", "QRV"}}),
                      "error"),
                  "not a format of the mars service"));
        // compose_templates names each format's services.
        JsonValue t = call(a, {{"cmd", "compose_templates"}});
        bool plain_amateur = false, l16_mars = false;
        for (auto& pr : t.get("procedures")->arr) {
            const JsonValue* sv = pr.get("services");
            if (str(pr, "id") == "PLAIN")
                plain_amateur = sv && sv->arr.size() == 1 && sv->arr[0].s == "amateur";
            if (str(pr, "id") == "16L_P2P_FULL") l16_mars = sv && sv->arr.size() == 1 && sv->arr[0].s == "mars";
        }
        CHECK(plain_amateur && l16_mars);
        // It goes as the text alone, with no PNA1 frame (the link format plain).
        CHECK(wait_for([&] { auto x = a.get(str(v, "id")); return x && x->state == "sent"; }, 15000));
        CHECK(logged(a, "sent as plain text"));
    }
    const int queued_plain = out_count(a);

    // ---- retransmit and qsl carry the source message's service --------------
    CHECK(!b.submit("STA-A", 'R', "FROM B", &e, SendOpts::in("mars")).empty());
    std::string in_id;
    CHECK(wait_for([&] {
        for (auto& m : a.list(100))
            if (m.direction == "in" && m.src == "STA-B" && m.body == "FROM B") in_id = m.id;
        return !in_id.empty();
    }));
    {
        auto m = a.get(in_id);
        CHECK(m && m->world == "mars");  // the service of the link it was heard on
    }
    // A message in a reply says its `service` (docs/api.md, "service on every
    // record"), and `world` with the same value for one release.
    auto both = [](const JsonValue& row, const std::string& s) {
        return str(row, "service") == s && str(row, "world") == s;
    };
    auto row_of = [&](const JsonValue& reply, const char* key) -> const JsonValue* {
        const JsonValue* rows = reply.get(key);
        if (rows && rows->is_array())
            for (auto& x : rows->arr)
                if (str(x, "id") == in_id) return &x;
        return nullptr;
    };
    {
        JsonValue g = call(a, {{"cmd", "get"}, {"id", in_id}});
        const JsonValue* x = row_of(g, "messages");
        CHECK(ok(g) && x && both(*x, "mars"));
        JsonValue l = call(a, {{"cmd", "list"}, {"limit", "100"}});
        x = row_of(l, "messages");
        CHECK(ok(l) && x && both(*x, "mars"));
    }
    CHECK(crossed(call(a, {{"cmd", "retransmit"}, {"id", in_id}, {"to", "TEST-HAM1"}, {"confirm", "Y"}}), "mars", "amateur"));
    CHECK(crossed(call(a, {{"cmd", "retransmit"}, {"id", in_id}, {"to", "STA-B"}, {"confirm", "Y"}, {"service", "amateur"}}),
                  "amateur", "mars"));
    {
        JsonValue v = call(a, {{"cmd", "retransmit"}, {"id", in_id}, {"to", "STA-B"}, {"confirm", "Y"}});
        CHECK(ok(v));  // MARS to MARS still goes, in the source's service
        bool found = false;
        for (auto& x : a.list(100))
            if (x.direction == "out" && x.dst == "STA-B" && x.body == "FROM B") found = x.world == "mars";
        CHECK(found);
    }
    // A received message of the amateur service: its retransmit to MARS crosses.
    set_world(dir / "a.db", in_id, "amateur");
    CHECK(crossed(call(a, {{"cmd", "retransmit"}, {"id", in_id}, {"to", "STA-B"}, {"confirm", "Y"}}), "amateur", "mars"));
    // A retransmit with a key in a service that carries no enciphered traffic.
    {
        JsonValue v = call(a, {{"cmd", "retransmit"}, {"id", in_id}, {"to", "TEST-HAM1"}, {"confirm", "Y"}, {"key", "QXR01"}});
        CHECK(!ok(v) && has(str(v, "error"), "the amateur service carries no enciphered traffic"));
    }
    // qsl: a receipt for ACP 127 traffic that came as plain text.
    CHECK(b.set_link_format("STA-A", "plain", &e));
    CHECK(ok(call(b, {{"cmd", "send_acp127"}, {"to_station", "STA-A"}, {"prec", "R"}, {"channel", "QXA"},
                      {"routing", "STA-A"}, {"originator_ri", "STA-B"}, {"from", "STA-B/TEST STATION TWO"},
                      {"to", "STA-A/TEST STATION ONE"}, {"text", "FOR A RECEIPT"}, {"service", "mars"}})));
    std::string plain_id;
    CHECK(wait_for([&] {
        for (auto& m : a.list(200))
            if (m.direction == "in" && m.id.rfind("PLAIN-", 0) == 0 && has(m.body, "FOR A RECEIPT") &&
                has(m.body, "NNNN") && m.state == "received")
                plain_id = m.id;
        return !plain_id.empty();
    }, 15000));
    set_world(dir / "a.db", plain_id, "amateur");
    {
        JsonValue v = call(a, {{"cmd", "qsl"}, {"id", plain_id}, {"send", "Y"}});
        CHECK(!ok(v) && has(str(v, "error"), "receipt refused") && crossing(str(v, "error"), "amateur", "mars"));
    }
    set_world(dir / "a.db", plain_id, "mars");
    {
        JsonValue q = call(a, {{"cmd", "qsl"}, {"id", plain_id}, {"send", "Y"}});
        if (!ok(q)) std::cerr << "qsl: " << str(q, "error") << "\n";
        CHECK(ok(q));
    }

    CHECK(b.set_link_format("STA-A", "pna1", &e));

    // ---- a reply draft carries the source's service -------------------------
    {
        JsonValue v = call(a, {{"cmd", "reply_draft"}, {"id", plain_id}});
        if (!ok(v) || str(v, "service") != "mars") std::cerr << "reply_draft: " << ApiServer::handle(a, "{\"cmd\":\"reply_draft\",\"id\":\"" + plain_id + "\"}") << "\n";
        CHECK(ok(v) && str(v, "service") == "mars");
    }

    // ---- routing rules store their service, and it is the guard -------------
    CHECK(has(str(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE WEST"}, {"ale", "STA-B"}}), "error"),
              "needs its service"));
    CHECK(!ok(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE WEST"}, {"ale", "STA-B"}, {"service", "narnia"}})));
    CHECK(crossed(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE WEST"}, {"ale", "STA-B,TEST-HAM1"},
                           {"service", "mars"}}),
                  "mars", "amateur"));
    CHECK(crossed(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE WEST"}, {"ale", "STA-B"},
                           {"world", "amateur"}}),
                  "amateur", "mars"));
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"match", "UHE"}, {"route", "ALE HAM"}, {"ale", "TEST-HAM1"},
                      {"service", "amateur"}})));
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE WEST"}, {"ale", "STA-B"}, {"service", "mars"}})));
    {
        auto rules = bbs::engine(a).rules();
        CHECK(rules.size() == 2 && rules[0].world == "amateur" && rules[1].world == "mars");
        CHECK(has(ApiServer::handle(a, R"({"cmd":"route_list"})"), R"("service":"amateur")"));
    }

    // ---- the airtime point, requeue and release: the book changed under it --
    a.set_emcon(true);
    const std::string late = a.submit("LATE-1", 'R', "QUEUED BEFORE", &e, SendOpts::in("mars"));
    const std::string held = a.submit("LATE-2", 'R', "HELD BEFORE", &e, [] {
        SendOpts o = SendOpts::in("mars");
        o.held = true;
        return o;
    }());
    CHECK(!late.empty() && !held.empty());
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "LATE ONE"}, {"call", "LATE-1"}, {"world", "amateur"}})));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "LATE TWO"}, {"call", "LATE-2"}, {"world", "amateur"}})));
    CHECK(!a.release(held, &e) && crossing(e, "a mars message", "an amateur station"));
    a.set_emcon(false);
    CHECK(wait_for([&] {
        auto m = a.get(late);
        return m && m->state == "failed";
    }));
    {
        auto m = a.get(late);
        CHECK(m && crossing(m->last_error, "a mars message", "an amateur station"));
        CHECK(logged(a, "refused at the air"));
    }
    CHECK(crossed(call(a, {{"cmd", "requeue"}, {"id", late}}), "mars", "amateur"));
    CHECK(!a.requeue(late, &e) && crossing(e, "mars", "amateur"));
    {
        auto m = a.get(late);
        CHECK(m && m->state == "failed");
    }

    // ---- received traffic takes the service of the channel it was heard on --
    {
        ChannelPlan& cp = ops->channels();
        CHECK(cp.service_for(5100000) == "mars" && cp.service_for(5200000) == "amateur" && cp.service_for(9999000).empty());
        decode_path(dir);
    }

    // ---- automatic modes stay MARS only until stage 5 -----------------------
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"machine_mode", "true"}, {"ack_delay_ms", "0"}, {"auto_per_hour", "120"},
                      {"commands_ct_only", "false"}})));
    // A MARS net is open: an amateur station heard is never checked in (audit C).
    const std::string net = ops->net_open("MARS NET", {}, false, "", "", &e);
    CHECK(!net.empty());
    CHECK(!h.submit("STA-A", 'R', chat("STA-A-CM", "TEST-HAM1", "VER"), &e, SendOpts::in("amateur")).empty());
    CHECK(wait_for([&] {
        for (auto& m : a.list(100))
            if (m.direction == "in" && m.src == "TEST-HAM1") return true;
        return false;
    }));
    CHECK(wait_for([&] {
        bbs::engine(a).pump();
        return logged(a, "not sent to TEST-HAM1") && logged(a, "not answered or routed: it came from TEST-HAM1");
    }));
    for (auto& m : a.list(100))
        CHECK(!(m.direction == "out" && m.dst == "TEST-HAM1" && m.world == "mars"));
    {
        auto n = ops->net_get(net);
        CHECK(n && n->checkins.empty());
        CHECK(!b.submit("STA-A", 'R', "STA-B CHECKING IN", &e, SendOpts::in("mars")).empty());  // a MARS station still is
        CHECK(wait_for([&] {
            auto m = ops->net_get(net);
            return m && m->checkins.size() == 1 && m->checkins[0].station == "STA-B";
        }));
    }

    // ---- the direct-modem ACK: only for MARS traffic until stage 5 ------------
    // A channel-less link in the amateur service: an amateur message from a
    // station not in the book is never ACKed on the air.
    {
        auto mx = std::make_unique<LoopbackModem>(0);
        auto my = std::make_unique<LoopbackModem>(0);
        LoopbackModem::connect(*mx, *my);
        Core x(link_in(CoreConfig{"TEST-AMX", (dir / "x.db").string(), 3, 1, true}, "amateur"), std::move(mx));
        x.set_services(test_services());
        CoreConfig cy{"TEST-AMY", (dir / "y.db").string(), 3, 1, true};
        cy.ack_timeout_s = 60;
        Core y(cy, std::move(my));
        y.set_services(test_services());
        x.start();
        y.start();
        const std::string id = y.submit("TEST-AMX", 'R', "AMATEUR, NOT IN THE BOOK", &e, SendOpts::in("amateur"));
        CHECK(!id.empty());
        CHECK(wait_for([&] { auto m = x.get(id); return m && m->world == "amateur"; }));
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        CHECK(y.get(id) && y.get(id)->state != "acked");
        CHECK(logged(x, "not sent to TEST-AMY"));
        x.stop();
        y.stop();
    }

    // ---- an upgrade with no service tables yet: queued traffic waits ---------
    // (never failed for it), and goes once its table is there.
    {
        auto mq = std::make_unique<LoopbackModem>(0);
        auto mr = std::make_unique<LoopbackModem>(0);
        LoopbackModem::connect(*mq, *mr);
        Core q(on_mars_link(CoreConfig{"STA-Q", (dir / "q.db").string(), 3, 1, true}), std::move(mq));
        q.set_services(test_services());
        Core r(on_mars_link(CoreConfig{"STA-R", (dir / "r.db").string(), 3, 1, true}), std::move(mr));
        r.set_services(test_services());
        q.start();
        r.start();
        q.set_emcon(true);
        const std::string id = q.submit("STA-R", 'R', "QUEUED BEFORE THE TABLES WENT", &e, SendOpts::in("mars"));
        CHECK(!id.empty());
        q.set_services(Services::from_texts({}, nullptr));
        q.set_emcon(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        {
            auto m = q.get(id);
            CHECK(m && m->state == "queued" && has(m->last_error, "no table for service `mars`"));
            CHECK(logged(q, "waits: no table for service `mars`"));
        }
        q.set_services(test_services());
        CHECK(wait_for([&] { auto m = q.get(id); return m && m->state == "acked"; }, 40000));
        q.stop();
        r.stop();
    }

    // ---- a broadcast names its service --------------------------------------
    CHECK(!a.submit("ALL", 'R', "TO ALL STATIONS", &e, SendOpts::in("mars")).empty());
    CHECK(out_count(a) > queued_plain);

    a.stop();
    b.stop();
    h.stop();
    fs::remove_all(dir);
    return check::report("test_worlds");
}
