// Milestone 7, second part: net schedules, the net call-up sent to every
// station (the broadcast address ALL, which nobody acks), and compose using
// the station book. Calls and routing indicators here are made up.
#include "../src/api_server.h"
#include "../src/compose.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "check.h"
#include "services_fixture.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
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

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (c == '\n') {
            o += "\\n";
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
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static std::vector<std::string> strs(const JsonValue& v, const char* k) {
    std::vector<std::string> out;
    const JsonValue* a = v.get(k);
    if (a && a->is_array())
        for (auto& e : a->arr) out.push_back(e.s);
    return out;
}
static bool has_event(Core& c, const std::string& id, const std::string& text) {
    for (auto& e : c.events(0, 1000))
        if ((id.empty() || e.msg_id == id) && e.text.find(text) != std::string::npos) return true;
    return false;
}

constexpr int64_t kSun1 = 1793491200;  // 2026-11-01 00:00Z, a Sunday
constexpr int64_t kSat7 = 1794009600;  // 2026-11-07 00:00Z, the first Saturday
constexpr int64_t kSun29 = 1795910400; // 2026-11-29, the last Sunday
constexpr int64_t kSat5 = 1796428800;  // 2026-12-05, December's first Saturday
constexpr int64_t H = 3600, M = 60;

static void test_days() {
    int m = 0;
    CHECK(parse_hhmm("1900", &m) && m == 1140);
    CHECK(parse_hhmm("0005z", &m) && m == 5);
    CHECK(!parse_hhmm("2460", &m) && !parse_hhmm("190", &m) && !parse_hhmm("19:00", &m));
    std::string c, err;
    CHECK(parse_days("weekdays", &c, &err) && c == "MON,TUE,WED,THU,FRI");
    CHECK(parse_days("Tuesday, thu", &c, &err) && c == "TUE,THU");
    CHECK(parse_days("1sat,LSUN", &c, &err) && c == "1SAT,LSUN");
    CHECK(parse_days("DAILY", &c, &err) && c == "SUN,MON,TUE,WED,THU,FRI,SAT");
    CHECK(!parse_days("FUNDAY", &c, &err) && err.find("FUNDAY") != std::string::npos);
    CHECK(!parse_days("6SAT", &c, &err) && !parse_days("", &c, &err));

    CHECK(next_start("SAT", 1140, kSun1) == kSat7 + 19 * H);
    CHECK(next_start("SAT", 1140, kSat7 + 19 * H) == kSat7 + 19 * H);      // at the start counts
    CHECK(next_start("SAT", 1140, kSat7 + 19 * H + 1) == kSat7 + 7 * 24 * H + 19 * H);
    CHECK(next_start("1SAT", 1140, kSat7 + 20 * H) == kSat5 + 19 * H);     // next month's first
    CHECK(next_start("LSUN", 60, kSun1 + 2 * H) == kSun29 + H);
    CHECK(next_start("WEEKDAYS", 0, kSat7) == 0);  // not canonical: no rule
    CHECK(next_start("MON,TUE,WED,THU,FRI", 0, kSat7) == kSat7 + 2 * 24 * H);
}

static void test_schedules(const fs::path& dir) {
    std::string db = (dir / "s.db").string();
    std::atomic<int64_t> t{kSat7 + 18 * H + 40 * M};  // Saturday 1840Z
    {
        Ops o("NCS-A", db);
        o.set_clock([&] { return t.load(); });
        std::string err;
        NetSchedule s;
        s.name = "weekly net";
        s.days = "sat";
        s.start = 1140;
        s.roster = {"STA-B", "sta-c"};
        CHECK(o.sched_put(s, &err));
        auto g = o.sched_get("WEEKLY NET");
        CHECK(g && g->days == "SAT" && g->minutes == 60 && g->roster.size() == 2 && g->roster[1] == "STA-C");
        NetSchedule bad = s;
        bad.days = "SOMEDAY";
        CHECK(!o.sched_put(bad, &err) && err.find("SOMEDAY") != std::string::npos);
        bad = s;
        bad.minutes = 2;
        CHECK(!o.sched_put(bad, &err));

        auto st = o.sched_sitting("weekly net");
        CHECK(st && st->state == "upcoming" && st->start == kSat7 + 19 * H && st->end == kSat7 + 20 * H);
        CHECK(o.sched_tick().empty());  // 20 minutes out: too early to say
        t = kSat7 + 18 * H + 46 * M;
        auto said = o.sched_tick();
        CHECK(said.size() == 1 && said[0] == "scheduled net WEEKLY NET starts at 1900Z");
        CHECK(o.sched_tick().empty());  // said once

        t = kSat7 + 19 * H + 1 * M;
        st = o.sched_sitting("WEEKLY NET");
        CHECK(st && st->state == "due");
        said = o.sched_tick();
        CHECK(said.size() == 1 && said[0] == "scheduled net WEEKLY NET was due at 1900Z and isn't open");
        CHECK(o.sched_tick().empty());

        std::string id = o.sched_open("weekly net", &err);
        CHECK(id == "NET-1");
        auto n = o.net_get(id);
        CHECK(n && n->name == "WEEKLY NET" && n->ncs == "NCS-A" && n->roster.size() == 2 && !n->exercise);
        CHECK(n && n->note == "from schedule WEEKLY NET");
        st = o.sched_sitting("WEEKLY NET");
        CHECK(st && st->state == "open" && st->net == "NET-1");
        CHECK(o.sched_open("WEEKLY NET", &err).empty() && err.find("already open as NET-1") != std::string::npos);
        CHECK(o.sched_tick().empty());
        t = kSat7 + 19 * H + 50 * M;
        CHECK(o.net_close(id, &err));
        st = o.sched_sitting("WEEKLY NET");
        CHECK(st && st->state == "upcoming" && st->start == kSat7 + 7 * 24 * H + 19 * H);

        // A first-Saturday exercise net that opens itself.
        NetSchedule x;
        x.name = "MONTHLY EXERCISE";
        x.days = "1SAT";
        x.start = 1200;
        x.minutes = 90;
        x.exercise = true;
        x.auto_open = true;
        x.ncs = "NCS-A";
        x.roster = {"STA-D"};
        CHECK(o.sched_put(x, &err));
        auto up = o.sched_upcoming(5);
        CHECK(up.size() == 2 && up[0].sched.name == "MONTHLY EXERCISE" && up[0].start == kSat7 + 20 * H);
        t = kSat7 + 20 * H + 30;
        said = o.sched_tick();
        CHECK(said.size() == 1 && said[0] == "scheduled net MONTHLY EXERCISE opened as NET-2");
        n = o.net_get("NET-2");
        CHECK(n && n->exercise && n->closed_at == 0 && n->roster.size() == 1 && n->roster[0] == "STA-D");
        up = o.sched_upcoming(1);
        CHECK(up.size() == 1 && up[0].state == "open" && up[0].net == "NET-2");
        CHECK(o.sched_tick().empty());

        // Opened early, up to 30 minutes ahead, it counts as that sitting.
        CHECK(o.net_close("NET-2", &err));
        t = kSat7 + 7 * 24 * H + 18 * H + 40 * M;
        CHECK(o.sched_open("WEEKLY NET", &err) == "NET-3");
        st = o.sched_sitting("WEEKLY NET");
        CHECK(st && st->state == "open" && st->net == "NET-3");
        t = kSat7 + 7 * 24 * H + 19 * H + 5 * M;
        CHECK(o.sched_tick().empty());  // not "due": it is open

        // A late net that runs past midnight is under way after 0000Z.
        NetSchedule late;
        late.name = "LATE NET";
        late.days = "SAT";
        late.start = 23 * 60 + 30;
        late.minutes = 60;
        CHECK(o.sched_put(late, &err));
        t = kSat7 + 24 * H + 15 * M;  // Sunday 0015Z
        st = o.sched_sitting("LATE NET");
        CHECK(st && st->state == "due" && st->start == kSat7 + 23 * H + 30 * M);
        CHECK(o.sched_remove("late net") && !o.sched_get("LATE NET"));
    }
    // Kept across a restart.
    Ops o2("NCS-A", db);
    auto g = o2.sched_get("WEEKLY NET");
    CHECK(g && g->last_net == "NET-3" && g->last_start == kSat7 + 7 * 24 * H + 19 * H);
    CHECK(o2.sched_list().size() == 2);
}

static void test_api(const fs::path& dir) {
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    std::string adb = (dir / "a.db").string(), bdb = (dir / "b.db").string();
    Core a(on_mars_link(CoreConfig{"NCS-A", adb, 3, 1, true}), std::move(ma));
    a.set_services(test_services());
    Core b(on_mars_link(CoreConfig{"STA-B", bdb, 3, 1, true}), std::move(mb));
    b.set_services(test_services());
    auto opsa = std::make_shared<Ops>("NCS-A", adb);
    a.set_ops(opsa);
    b.set_ops(std::make_shared<Ops>("STA-B", bdb));
    a.start();
    b.start();

    // The book: a tactical name for STA-B with its RI, STA-C, and a collective.
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "IRONSIDE"}, {"call", "STA-B"}, {"ri", "RXXXB"}})));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "STA-C"}, {"ri", "RXXXC"}})));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "PRACTICE ROSTER"}, {"kind", "collective"},
                      {"members", "IRONSIDE;STA-C;STA-Z"}})));

    // Schedules through the API, and the status feed's next net.
    auto r = call(a, {{"cmd", "sched_put"}, {"name", "TUESDAY NET"}, {"days", "TUE"}, {"time", "0100"},
                      {"roster", "PRACTICE ROSTER"}});
    CHECK(ok(r) && r.get("schedules")->arr.size() == 1);
    CHECK(str(r.get("schedules")->arr[0], "days") == "TUE" && str(r.get("schedules")->arr[0], "time") == "0100");
    CHECK(!ok(call(a, {{"cmd", "sched_put"}, {"name", "X"}, {"days", "TUE"}, {"time", "25:00"}})));
    r = call(a, {{"cmd", "sched_list"}});
    CHECK(ok(r) && r.get("schedules")->arr.size() == 1);
    r = call(a, {{"cmd", "status"}});
    const JsonValue* nn = r.get("next_net");
    CHECK(nn && nn->type == JsonValue::Type::Object && str(*nn, "name") == "TUESDAY NET");
    r = call(a, {{"cmd", "sched_open"}, {"name", "TUESDAY NET"}});
    CHECK(ok(r));
    std::string net = str(r, "id");
    CHECK(!net.empty());
    const JsonValue& nj = r.get("net")->arr[0];
    // STA-Z isn't in the book: it is called as written.
    CHECK(strs(nj, "roster") == (std::vector<std::string>{"STA-B", "STA-C", "STA-Z"}));

    // Call-up of the stations not yet in, to ALL: B takes it and nobody acks it.
    r = call(a, {{"cmd", "net_callup"}, {"service", "mars"}});
    CHECK(ok(r) && str(r, "to_station") == "ALL");
    CHECK(strs(r, "called") == (std::vector<std::string>{"STA-B", "STA-C", "STA-Z"}));
    std::string msg = str(r, "message"), cid = str(r, "id");
    CHECK(msg.find("STA-B STA-C STA-Z DE NCS-A NR 001") != std::string::npos);
    CHECK(msg.find("NET TUESDAY NET IS OPEN. NET CONTROL IS NCS-A.") != std::string::npos);
    CHECK(msg.find("NOT YET CHECKED IN: STA-B STA-C STA-Z") != std::string::npos);
    CHECK(msg.find("UNCLAS") == std::string::npos && msg.find("EXERCISE") == std::string::npos);
    CHECK(wait_for([&] { return has_event(a, cid, "sent to all stations, no ack expected"); }));
    CHECK(wait_for([&] { return has_event(b, cid, "received ROUTINE from NCS-A to all stations"); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(!has_event(b, cid, "sending ack"));
    auto am = a.get(cid);
    CHECK(am && am->state == "sent" && am->dst == "ALL");
    auto bm = b.get(cid);
    CHECK(bm && bm->direction == "in" && bm->dst == "ALL");

    // B answers, so A hears it and checks it in; then only STA-C is called.
    std::string err;
    CHECK(!b.submit("NCS-A", 'R', "QNI", &err, SendOpts::in("mars")).empty());
    CHECK(wait_for([&] {
        auto n = opsa->net_get(net);
        return n && n->missing() == std::vector<std::string>{"STA-C", "STA-Z"};
    }));
    r = call(a, {{"cmd", "net_callup"}, {"service", "mars"}, {"text", "Traffic list follows"}});
    CHECK(ok(r) && strs(r, "called") == (std::vector<std::string>{"STA-C", "STA-Z"}));
    CHECK(str(r, "message").find("STA-C STA-Z DE NCS-A NR 002") != std::string::npos);
    CHECK(str(r, "message").find("TRAFFIC LIST FOLLOWS") != std::string::npos);
    r = call(a, {{"cmd", "net_callup"}, {"service", "mars"}, {"who", "all"}, {"unclas", "true"}});
    CHECK(ok(r) && strs(r, "called").size() == 3 && str(r, "message").find("UNCLAS") != std::string::npos);
    CHECK(str(r, "message").find("NOT YET CHECKED IN") == std::string::npos);
    CHECK(ok(call(a, {{"cmd", "net_checkin"}, {"station", "STA-C"}})));
    CHECK(ok(call(a, {{"cmd", "net_checkin"}, {"station", "STA-Z"}})));
    r = call(a, {{"cmd", "net_callup"}, {"service", "mars"}});
    CHECK(!ok(r) && str(r, "error") == "every station on the roster has checked in");
    CHECK(!ok(call(a, {{"cmd", "net_callup"}, {"service", "mars"}, {"who", "some"}})));

    // A long roster doesn't fit the call line: it calls the NET instead.
    std::string many;
    for (int i = 0; i < 12; ++i) many += (many.empty() ? "" : ",") + std::string("STA-") + char('D' + i) + "X";
    r = call(a, {{"cmd", "net_open"}, {"name", "BIG NET"}, {"roster", many}, {"exercise", "true"}});
    CHECK(ok(r));
    std::string big = str(r, "id");
    r = call(a, {{"cmd", "net_callup"}, {"service", "mars"}, {"net", big}});
    CHECK(ok(r) && strs(r, "called").size() == 12);
    msg = str(r, "message");
    CHECK(msg.find("NET DE NCS-A NR") != std::string::npos && msg.find("EXERCISE EXERCISE EXERCISE") != std::string::npos);
    for (auto& line : std::vector<std::string>{msg})
        for (size_t p = 0, q; (q = line.find('\n', p)) != std::string::npos; p = q + 1) CHECK(q - p <= 71);
    bool logged = false;
    for (auto& e : opsa->log_list(0, opsa->now() + 1, 500))
        if (e.text.find("call-up sent on " + big) != std::string::npos && e.exercise) logged = true;
    CHECK(logged);

    // Compose with the book: a tactical name in CALLS goes to its station.
    r = call(a, {{"cmd", "compose_check"}, {"template", "FREE_FORM"}, {"procedure", "ACP126_SIMPLE"},
                 {"calls", "IRONSIDE"}, {"text", "Test"}});
    CHECK(ok(r) && r.get("ready")->b && strs(r, "to_targets") == std::vector<std::string>{"STA-B"});
    bool tip = false;
    for (auto& i : r.get("issues")->arr)
        if (str(i, "level") == "tip" && str(i, "text").find("IRONSIDE is sent to STA-B") != std::string::npos) tip = true;
    CHECK(tip);
    // CALLS NET is the whole net: one broadcast.
    r = call(a, {{"cmd", "compose_check"}, {"template", "FREE_FORM"}, {"procedure", "ACP126_SIMPLE"},
                 {"calls", "NET"}, {"text", "Test"}});
    CHECK(ok(r) && strs(r, "to_targets") == std::vector<std::string>{"ALL"});
    // A collective as the station to send to fans out, one copy each; a plain
    // name on the TO line gets its RI from the book.
    r = call(a, {{"cmd", "compose_send"}, {"service", "mars"}, {"template", "GENADMIN_SIMPLE"}, {"procedure", "ACP127_PLAINDRESS"},
                 {"to_station", "practice roster"}, {"routing", "RXXXB RXXXC"}, {"to", "IRONSIDE\nSTA-C"},
                 {"subj", "TEST"}, {"remarks", "Book test"}});
    CHECK(ok(r) && strs(r, "ids").size() == 3 &&
          strs(r, "to_targets") == (std::vector<std::string>{"STA-B", "STA-C", "STA-Z"}));
    msg = str(r, "message");
    CHECK(msg.find("TO RXXXB/IRONSIDE") != std::string::npos && msg.find("RXXXC/STA-C") != std::string::npos);
    auto ids = strs(r, "ids");
    CHECK(a.get(ids[0])->dst == "STA-B" && a.get(ids[1])->dst == "STA-C" && a.get(ids[2])->dst == "STA-Z");
    CHECK(a.get(ids[0])->body == a.get(ids[1])->body);
    // A member the book doesn't have is named, and goes as written.
    r = call(a, {{"cmd", "compose_check"}, {"template", "FREE_FORM"}, {"procedure", "ACP126_SIMPLE"},
                 {"calls", "NET"}, {"to_station", "PRACTICE ROSTER"}, {"text", "Test"}});
    CHECK(ok(r) && r.get("ready")->b && strs(r, "to_targets").size() == 3);
    bool named = false;
    for (auto& i : r.get("issues")->arr)
        if (str(i, "level") == "warning" && str(i, "text").find("STA-Z") != std::string::npos) named = true;
    CHECK(named);
    // Without a book, compose works as before.
    compose::Context bare;
    bare.station = "NCS-A";
    auto cr = compose::check(compose::Draft{"FREE_FORM", "ACP126_SIMPLE", {{"calls", "IRONSIDE"}, {"text", "T"}}}, bare);
    CHECK(cr.ready && cr.to_targets == std::vector<std::string>{"IRONSIDE"});

    a.stop();
    b.stop();
}

// Audit C: a heard station goes only into the open net on whose channel it
// was heard, during that net's sitting.
static void test_checkin(const fs::path& dir) {
    std::atomic<int64_t> t{kSat7 + 18 * H + 55 * M};  // Saturday 1855Z
    Ops o("NCS-A", (dir / "c.db").string());
    o.set_clock([&] { return t.load(); });
    std::string err;
    NetSchedule s;
    s.name = "WEEKLY NET";
    s.days = "SAT";
    s.start = 1140;  // 1900Z, 60 minutes
    s.roster = {"STA-B", "STA-C"};
    s.channel = "CH01";
    CHECK(o.sched_put(s, &err));
    const std::string w = o.sched_open("WEEKLY NET", &err);
    CHECK(w == "NET-1");
    auto n = o.net_get(w);
    CHECK(n && n->channel == "CH01" && n->sitting_end == kSat7 + 20 * H);

    // One net, on CH01: heard there, it checks in; heard elsewhere or with the
    // channel unknown, it doesn't.
    CHECK(o.heard("STA-B", "CH02").empty());
    CHECK(o.heard("STA-B", "").empty());
    CHECK((o.heard("STA-B", "ch01") == std::vector<std::string>{"NET-1"}));
    CHECK(o.heard("STA-B", "CH01").empty());  // once

    // A second net, opened by hand on CH02: each takes only its own channel.
    const std::string e2 = o.net_open("EAST NET", {"STA-D"}, false, "", "", &err, "CH02");
    CHECK(e2 == "NET-2");
    CHECK((o.heard("STA-D", "CH02") == std::vector<std::string>{"NET-2"}));
    CHECK((o.heard("STA-C", "CH01") == std::vector<std::string>{"NET-1"}));
    n = o.net_get(w);
    CHECK(n && n->checkins.size() == 2);
    n = o.net_get(e2);
    CHECK(n && n->checkins.size() == 1 && n->checkins[0].station == "STA-D");

    // A third net that names no channel: with others open, nobody is put in it.
    const std::string x = o.net_open("NO CHANNEL NET", {}, false, "", "", &err);
    CHECK(x == "NET-3");
    CHECK(o.heard("STA-E", "").empty() && o.heard("STA-E", "CH03").empty());
    CHECK(o.net_close(x, &err) && o.net_close(e2, &err));

    // After the sitting: still open, but no more check-ins by radio.
    t = kSat7 + 20 * H + 5 * M;
    CHECK(o.heard("STA-F", "CH01").empty());
    CHECK(o.net_checkin(w, "STA-F", "hand", 0, "", &err));  // the operator still can
    CHECK(o.net_close(w, &err));

    // A lone net with no channel works as before: whoever is heard.
    const std::string y = o.net_open("AD HOC", {}, false, "", "", &err);
    CHECK((o.heard("STA-G", "") == std::vector<std::string>{y}));
    CHECK(o.net_close(y, &err));
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_sched_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    test_days();
    test_schedules(dir);
    test_checkin(dir);
    test_api(dir);
    fs::remove_all(dir);
    return check::report("test_sched");
}
