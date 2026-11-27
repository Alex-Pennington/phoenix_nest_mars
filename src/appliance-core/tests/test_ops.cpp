// Milestone 7: address book, a full exercise net from open to close, operator
// duty and turnover, the station log and the monthly report, all through the
// local API with two stations on the loopback modem. Calls here are made up.
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "check.h"
#include "services_fixture.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <sstream>
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

// One API call from key/value pairs.
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
static double num(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::Number ? x->n : -1;
}
static std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static const JsonValue& first(const JsonValue& v, const char* k) {
    static JsonValue none;
    const JsonValue* a = v.get(k);
    return a && a->is_array() && !a->arr.empty() ? a->arr[0] : none;
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_ops_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Pure helpers.
    CHECK(book_key("  exer   net ") == "EXER NET");
    CHECK(is_exercise_body("EXERCISE EXERCISE EXERCISE\nTEXT\n"));
    CHECK(is_exercise_body("UNCLAS\n exercise exercise\n"));
    CHECK(!is_exercise_body("NOT AN EXERCISE LINE FIRST\n"));

    // A clock the test moves: starts three hours ago, inside this month.
    std::atomic<int64_t> t{0};
    {
        std::time_t n = std::time(nullptr);
        std::tm u{};
        gmtime_r(&n, &u);
        u.tm_mday = 1;
        u.tm_hour = u.tm_min = u.tm_sec = 0;
        int64_t month_start = static_cast<int64_t>(timegm(&u));
        t = std::max<int64_t>(n - 3 * 3600, month_start);
    }
    auto clock = [&] { return t.load(); };

    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    std::string adb = (dir / "a.db").string(), bdb = (dir / "b.db").string();
    Core a(on_mars_link(CoreConfig{"NCS-A", adb, 3, 1, true}), std::move(ma));
    a.set_services(test_services());
    Core b(on_mars_link(CoreConfig{"STA-B", bdb, 3, 1, true}), std::move(mb));
    b.set_services(test_services());
    auto opsa = std::make_shared<Ops>("NCS-A", adb);
    opsa->set_clock(clock);
    a.set_ops(opsa);
    b.set_ops(std::make_shared<Ops>("STA-B", bdb));
    a.start();
    b.start();

    // Address book: import, with a personal column dropped.
    std::string csv =
        "name,kind,call,ri,ale,members,note,phone\n"
        "STA-B,station,,RXXXB,STAB,,practice station,555\n"
        "STATION C,station,STA-C,,STAC,,,\n"
        "EXER NET,collective,,,,STA-B;STATION C,the practice roster,\n"
        "LOOP ONE,collective,,,,LOOP TWO,,\n"
        "LOOP TWO,collective,,,,LOOP ONE;STA-B,,\n"
        "BAD,station,not a call!,,,,,\n";
    JsonValue r = call(a, {{"cmd", "book_import"}, {"csv", csv}, {"source", "test"}});
    CHECK(ok(r) && num(r, "imported") == 5);
    CHECK(first(r, "dropped_columns").s == "phone" && !str(r, "note").empty());
    CHECK(first(r, "errors").s.find("line 7") == 0);

    r = call(a, {{"cmd", "book_resolve"}, {"to", "exer net"}});
    CHECK(ok(r) && r.get("stations")->arr.size() == 2);
    CHECK(str(r.get("stations")->arr[1], "call") == "STA-C");
    r = call(a, {{"cmd", "book_resolve"}, {"to", "LOOP ONE, NOBODY"}});
    CHECK(r.get("stations")->arr.size() == 1 && first(r, "loops").s == "LOOP ONE" && first(r, "unknown").s == "NOBODY");
    r = call(a, {{"cmd", "book_get"}, {"name", "RXXXB"}});  // found by RI
    CHECK(ok(r) && str(first(r, "entries"), "name") == "STA-B");
    r = call(a, {{"cmd", "book_put"}, {"name", "STA-D"}, {"note", std::string(90, 'X')}});
    CHECK(!ok(r) && str(r, "error").find("80") != std::string::npos);
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "STA-D"}})));
    CHECK(call(a, {{"cmd", "book_export"}}).get("csv")->s.find("EXER NET,collective,,,,STA-B;STATION C") !=
          std::string::npos);

    // Operator on duty, then the exercise net opens on the collective.
    CHECK(ok(call(a, {{"cmd", "duty_on"}, {"operator", "op1"}})));
    CHECK(!ok(call(a, {{"cmd", "duty_on"}, {"operator", "op2"}})));
    r = call(a, {{"cmd", "net_open"}, {"name", "Practice net"}, {"roster", "EXER NET"}, {"exercise", "true"}});
    CHECK(ok(r) && str(r, "id") == "NET-1");
    const JsonValue& net = first(r, "net");
    CHECK(net.get("roster") && net.get("roster")->arr.size() == 2 && str(net, "ncs") == "NCS-A");
    CHECK(str(call(a, {{"cmd", "status"}}), "net") == "NET-1");
    CHECK(str(call(a, {{"cmd", "status"}}), "on_duty") == "OP1");

    // STA-B calls in by radio: the net hears it and checks it in.
    std::string err;
    std::string in_id = b.submit("NCS-A", 'R', "EXERCISE EXERCISE EXERCISE\nSTA-B CHECKING IN\n", &err, SendOpts::in("mars"));
    CHECK(!in_id.empty());
    CHECK(wait_for([&] { return num(first(call(a, {{"cmd", "net_status"}}), "net"), "checked_in") == 1; }));
    r = first(call(a, {{"cmd", "net_status"}}), "net");
    CHECK(str(first(r, "checkins"), "station") == "STA-B" && str(first(r, "checkins"), "via") == "radio");
    CHECK(first(r, "missing").s == "STA-C");

    // STA-C by hand with one message for the net, and a station off the roster.
    t += 600;
    CHECK(ok(call(a, {{"cmd", "net_checkin"}, {"station", "station c"}, {"traffic", "1"}, {"remark", "weak"}})));
    CHECK(ok(call(a, {{"cmd", "net_checkin"}, {"station", "STA-D"}})));
    r = first(call(a, {{"cmd", "net_status"}}), "net");
    CHECK(num(r, "checked_in") == 3 && r.get("missing")->arr.empty());
    CHECK(!r.get("checkins")->arr[2].get("in_roster")->b);

    // NCS sends to the whole roster by its collective name: one copy each.
    r = call(a, {{"cmd", "send"}, {"service", "mars"}, {"to", "EXER NET"}, {"body", "EXERCISE EXERCISE EXERCISE\nNET TRAFFIC 1\n"}});
    CHECK(ok(r) && r.get("ids")->arr.size() == 2);
    std::string to_b = r.get("ids")->arr[0].s;
    CHECK(wait_for([&] { return a.get(to_b) && a.get(to_b)->state == "acked"; }));
    CHECK(a.get(r.get("ids")->arr[1].s)->dst == "STA-C");

    CHECK(ok(call(a, {{"cmd", "log_add"}, {"kind", "comspot"}, {"text", "Band noisy, signals fair"}})));
    CHECK(!ok(call(a, {{"cmd", "log_add"}, {"kind", "chatter"}, {"text", "x"}})));

    // STA-D checks out; hearing it again doesn't put it back.
    CHECK(ok(call(a, {{"cmd", "net_checkout"}, {"station", "STA-D"}})));
    opsa->heard("STA-D");
    r = first(call(a, {{"cmd", "net_status"}}), "net");
    CHECK(r.get("checkins")->arr[2].get("out_at")->n > 0);

    // Close at 45 minutes; operator off at two hours with a turnover note.
    t += 45 * 60 - 600;
    r = call(a, {{"cmd", "net_close"}});
    CHECK(ok(r) && !first(r, "net").get("open")->b);
    CHECK(!ok(call(a, {{"cmd", "net_close"}})));
    CHECK(str(call(a, {{"cmd", "status"}}), "net").empty());
    t += 2 * 3600 - 45 * 60;
    CHECK(ok(call(a, {{"cmd", "duty_off"}, {"turnover", "Net closed, STA-C listed one message"}})));
    r = call(a, {{"cmd", "duty_status"}});
    CHECK(!r.get("on_duty")->b && str(r, "last_turnover").find("STA-C") != std::string::npos);

    // The station log tells the story in order.
    r = call(a, {{"cmd", "station_log"}, {"limit", "100"}});
    std::string story;
    for (auto& e : r.get("entries")->arr) story += str(e, "kind") + ":" + str(e, "text") + "\n";
    CHECK(story.find("duty:OP1 on duty") != std::string::npos);
    CHECK(story.find("net:exercise net PRACTICE NET opened") != std::string::npos);
    CHECK(story.find("checkin:STA-B checked in (heard)") != std::string::npos);
    CHECK(story.find("checkin:STA-D checked in, not on the roster") != std::string::npos);
    CHECK(story.find("net:net PRACTICE NET closed after 45 min, 3 checked in, 1 listed traffic") != std::string::npos);
    CHECK(story.find("duty:OP1 off duty after 2.0 h") != std::string::npos);
    CHECK(story.find("turnover:Net closed") != std::string::npos);
    CHECK(story.find("comspot:") != std::string::npos);

    // The month's report.
    r = call(a, {{"cmd", "report_monthly"}});
    CHECK(ok(r));
    CHECK(num(r, "duty_tenths") == 20);
    CHECK(num(r, "nets") == 1 && num(r, "nets_exercise") == 1 && num(r, "nets_as_ncs") == 1);
    CHECK(num(r, "checkins") == 3 && num(r, "sent") == 2 && num(r, "sent_acked") == 1 && num(r, "received") == 1);
    CHECK(num(r, "exercise_messages") == 3 && num(r, "comspots") == 1);
    std::string text = str(r, "text");
    std::istringstream lines(text);
    int n = 0;
    for (std::string l; std::getline(lines, l); ++n) CHECK(l.size() <= 69);
    CHECK(n >= 8);
    CHECK(text.rfind("MONTHLY STATION REPORT ", 0) == 0);
    CHECK(text.find("DUTY HOURS 2.0\n  OP1 2.0\n") != std::string::npos);
    CHECK(text.find("NET-1 ") != std::string::npos && text.find(" 45 MIN 3 IN EXER PRACTICE NET") != std::string::npos);
    CHECK(text.find("SENT 2: Z 0 O 0 P 0 R 2, ACKED 1, FAILED 0") != std::string::npos);
    CHECK(!ok(call(a, {{"cmd", "report_monthly"}, {"month", "2026-13"}})));
    CHECK(ok(call(a, {{"cmd", "report_monthly"}, {"month", "2001-01"}})));

    a.stop();
    b.stop();

    // Everything is still there after a restart.
    {
        Ops again("NCS-A", adb);
        CHECK(again.book_get("EXER NET") && again.book_get("EXER NET")->members.size() == 2);
        auto nets = again.net_list(10);
        CHECK(nets.size() == 1 && nets[0].checkins.size() == 3 && nets[0].closed_at > 0);
        CHECK(!again.log_list(0, INT64_MAX, 500).empty());
    }

    fs::remove_all(dir);
    return check::report("test_ops");
}
