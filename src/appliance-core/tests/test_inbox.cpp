// The in box behind the Message Machine screens (api_inbox.h): list with
// in-box states, get's extras, pending and formatting errors, marks, QSL,
// tag, corrected copy, export, retransmit (principle 5: confirm=Y, EMCON
// holds it), search with the advanced filter, the log streams, activity
// counters and the three reports. Three stations on one loopback channel:
// A holds the in box, B sends PNA1 traffic, C is an MSC-style station
// sending plain ACP 127. Every call and RI here is made up.
#include "../src/api_server.h"
#include "../src/codress.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "check.h"
#include "services_fixture.h"

#include <sqlite3.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static bool wait_for(const std::function<bool()>& cond, int ms = 6000) {
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
static bool flag(const JsonValue& v, const char* k) {
    const JsonValue* o = v.get(k);
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
static size_t len(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->is_array() ? x->arr.size() : 0;
}
static const JsonValue& at(const JsonValue& v, const char* k, size_t i) {
    static const JsonValue none;
    const JsonValue* x = v.get(k);
    return x && x->is_array() && i < x->arr.size() ? x->arr[i] : none;
}

static std::string traffic(const std::string& fl2, const std::string& de, int serial, const std::string& text) {
    char sn[8];
    std::snprintf(sn, sizeof sn, "%04d", serial);
    return "VZCZC\r\nRR " + fl2 + " DE " + de + " #" + sn + " 3291200\r\nBT\r\nUNCLAS\r\n" + text + "\r\nBT\r\nNNNN\r\n";
}

// The in-box row of a message, by id, from a list or search reply.
static const JsonValue* row(const JsonValue& v, const std::string& id) {
    const JsonValue* a = v.get("messages");
    if (!a || !a->is_array()) return nullptr;
    for (auto& m : a->arr)
        if (str(m, "id") == id) return &m;
    return nullptr;
}

static std::string id_by_serial(const JsonValue& list, const std::string& serial) {
    const JsonValue* a = list.get("messages");
    if (!a || !a->is_array()) return "";
    for (auto& m : a->arr)
        if (str(m, "serial") == serial) return str(m, "id");
    return "";
}

static std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_inbox_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    auto mc = std::make_unique<LoopbackModem>(0);
    LoopbackModem::share({ma.get(), mb.get(), mc.get()});
    Core a(CoreConfig{"AAM497", (dir / "a.db").string(), 3, 1, true}, std::move(ma));
    a.set_services(test_services());
    Core b(CoreConfig{"AAR9AA", (dir / "b.db").string(), 3, 1, true}, std::move(mb));
    b.set_services(test_services());
    Core c(CoreConfig{"AAR7ZZ", (dir / "c.db").string(), 3, 1, true}, std::move(mc));
    c.set_services(test_services());
    std::string e;
    const int64_t t0 = static_cast<int64_t>(std::time(nullptr));
    keys::Tape tape;
    CHECK(keys::make_tape("PRACTICE NET", t0 - t0 % 3600 - 86400, 3, 24, t0, tape, &e));
    auto ka = std::make_shared<keys::Keyring>((dir / "a.keys").string());
    CHECK(ka->load(&e));
    a.set_keyring(ka);
    a.start();
    b.start();
    c.start();
    CHECK(ok(call(a, {{"cmd", "key_import"}, {"text", keys::format_tape(tape)}})));
    CHECK(c.set_link_format("AAM497", "plain", &e));

    // ---- traffic in: three plain from C (one for another station), one PNA1 from B, one garbled ----
    CHECK(!c.submit("AAM497", 'P', traffic("AAM497", "AAR7ZZ", 21, "MSGID/SITREP/AAR7ZZ/21//\r\nALL QUIET"), &e, SendOpts::in("mars")).empty());
    CHECK(!c.submit("AAM497", 'R', traffic("AAM497", "AAR7ZZ", 22, "LINE ONE\r\nLINE TWO\r\nLINE THREE"), &e, SendOpts::in("mars")).empty());
    CHECK(!c.submit("AAM497", 'R', traffic("UHWTST", "AAR7ZZ", 23, "FOR THE WEST"), &e, SendOpts::in("mars")).empty());
    CHECK(!c.submit("AAM497", 'R', "VZCZC\r\nRR AAM497 DE AAR7ZZ #0024 3291200\r\nUNCLAS\r\nNO BT HERE\r\nNNNN\r\n", &e, SendOpts::in("mars")).empty());
    CHECK(!b.submit("AAM497", 'R', traffic("AAM497", "AAR9AA", 31, "FROM BRAVO"), &e, SendOpts::in("mars")).empty());
    auto in_count = [&] {
        int n = 0;
        for (auto& m : a.list(100)) n += m.direction == "in";
        return n;
    };
    CHECK(wait_for([&] { return in_count() >= 5; }));

    JsonValue all = call(a, {{"cmd", "list"}, {"dir", "in"}});
    CHECK(ok(all));
    CHECK(len(all, "messages") == 5);
    const std::string s21 = id_by_serial(all, "0021"), s22 = id_by_serial(all, "0022"), s23 = id_by_serial(all, "0023"),
                      s31 = id_by_serial(all, "0031");
    CHECK(!s21.empty() && !s22.empty() && !s23.empty() && !s31.empty());
    std::string garbled;
    for (auto& m : all.get("messages")->arr)
        if (flag(m, "format_error")) garbled = str(m, "id");
    CHECK(!garbled.empty());
    CHECK(str(*row(all, s21), "state") == "NEW");
    CHECK(str(*row(all, s21), "type") == "SITREP");
    CHECK(str(*row(all, s21), "from") == "AAR7ZZ");
    CHECK(str(*row(all, s21), "prec") == "R" || str(*row(all, s21), "prec") == "P");
    CHECK(flag(*row(all, s21), "mine"));
    CHECK(!flag(*row(all, s23), "mine"));  // for another station: overheard
    JsonValue mine = call(a, {{"cmd", "list"}, {"dir", "in"}, {"mine", "Y"}});
    CHECK(len(mine, "messages") == 3);
    JsonValue page = call(a, {{"cmd", "list"}, {"dir", "in"}, {"limit", "2"}});
    CHECK(len(page, "messages") == 2 && flag(page, "more"));
    JsonValue page2 = call(a, {{"cmd", "list"}, {"dir", "in"}, {"limit", "10"}, {"after_id", str(page, "last_id")}});
    CHECK(len(page2, "messages") == 3);
    CHECK(!ok(call(a, {{"cmd", "list"}, {"dir", "sideways"}})));
    // list without dir is the plain call, unchanged.
    CHECK(len(call(a, {{"cmd", "list"}}), "messages") >= 5);

    // ---- get: extras; read=Y marks it read ----
    JsonValue g = call(a, {{"cmd", "get"}, {"id", s21}, {"read", "Y"}});
    CHECK(ok(g) && len(g, "messages") == 1);
    CHECK(str(at(g, "inbox", 0), "state") == "READ");
    CHECK(len(g, "events") >= 1);
    CHECK(g.get("props") && str(*g.get("props"), "source") == "AAR7ZZ");
    CHECK(g.get("props") && !flag(*g.get("props"), "encrypt"));
    CHECK(str(g, "decoded").empty() && !str(g, "decoded_note").empty());

    // ---- pending ----
    JsonValue p = call(a, {{"cmd", "pending"}});
    CHECK(ok(p));
    CHECK(num(p, "pending_count") == 3);
    CHECK(num(p, "errors_count") == 1);
    CHECK(str(at(p, "errors", 0), "id") == garbled);
    CHECK(len(at(p, "errors", 0), "errors") >= 1);
    CHECK(!str(at(p, "errors", 0), "raw").empty());
    CHECK(num(call(a, {{"cmd", "pending"}, {"all", "Y"}}), "pending_count") == 4);

    // ---- mark ----
    CHECK(ok(call(a, {{"cmd", "mark"}, {"id", s22}, {"state", "processed"}})));
    JsonValue after = call(a, {{"cmd", "list"}, {"dir", "in"}});
    CHECK(row(after, s22) == nullptr);  // processed is hidden by default
    JsonValue withp = call(a, {{"cmd", "list"}, {"dir", "in"}, {"include_processed", "Y"}});
    CHECK(row(withp, s22) && str(*row(withp, s22), "state") == "PROC");
    p = call(a, {{"cmd", "pending"}});
    CHECK(num(p, "pending_count") == 2);
    CHECK(str(p, "last_processed") == s22);
    CHECK(!ok(call(a, {{"cmd", "mark"}, {"id", s22}, {"state", "lost"}})));
    CHECK(!ok(call(a, {{"cmd", "mark"}, {"id", "NOPE-1"}, {"state", "processed"}})));
    {
        std::string outid;
        for (auto& m : c.list(100))
            if (m.direction == "out") outid = m.id;
        // An id A doesn't hold is refused like any unknown one.
        CHECK(!ok(call(a, {{"cmd", "mark"}, {"id", outid + "X"}, {"state", "processed"}})));
    }
    CHECK(ok(call(a, {{"cmd", "mark"}, {"id", s22}, {"state", "new"}})));
    CHECK(num(call(a, {{"cmd", "pending"}}), "pending_count") == 3);
    CHECK(ok(call(a, {{"cmd", "mark"}, {"id", s22}, {"state", "processed"}})));

    // ---- mark_all ----
    JsonValue ma_err = call(a, {{"cmd", "mark_all"}, {"list", "errors"}});
    CHECK(ok(ma_err) && num(ma_err, "marked") == 1 && str(ma_err, "state") == "discarded");
    // The ids it marked come back (phoenix_nest_mars#3).
    CHECK(len(ma_err, "ids") == 1 && at(ma_err, "ids", 0).s == garbled);
    CHECK(num(call(a, {{"cmd", "pending"}}), "errors_count") == 0);
    CHECK(!ok(call(a, {{"cmd", "mark_all"}, {"list", "everything"}})));

    // ---- qsl ----
    JsonValue q = call(a, {{"cmd", "qsl"}, {"service", "mars"}, {"id", s21}});
    CHECK(ok(q) && !flag(q, "sent"));
    g = call(a, {{"cmd", "get"}, {"id", s21}});
    CHECK(str(at(g, "inbox", 0), "state") == "QSL");
    CHECK(len(at(g, "inbox", 0), "qsl") == 1);
    // A receipt only on send=Y, and only for plain traffic (PNA1 was acked on the air).
    JsonValue q2 = call(a, {{"cmd", "qsl"}, {"service", "mars"}, {"id", s21}, {"send", "Y"}});
    CHECK(ok(q2) && flag(q2, "sent") && !str(q2, "out_id").empty());
    CHECK(wait_for([&] {
        for (auto& m : c.list(100))
            if (m.direction == "in" && m.body.find("AAM497") != std::string::npos && m.body.find("R ") != std::string::npos)
                return true;
        return false;
    }));
    JsonValue q3 = call(a, {{"cmd", "qsl"}, {"service", "mars"}, {"id", s31}, {"send", "Y"}});
    CHECK(ok(q3) && !flag(q3, "sent") && !str(q3, "not_sent").empty());

    // ---- tag ----
    CHECK(ok(call(a, {{"cmd", "tag"}, {"id", s31}, {"text", "CHECK WITH NCS"}})));
    CHECK(!ok(call(a, {{"cmd", "tag"}, {"id", s31}, {"text", std::string(81, 'T')}})));
    CHECK(!ok(call(a, {{"cmd", "tag"}, {"id", s31}, {"text", "MOVE TO 10.000 MHZ"}})));
    CHECK(str(at(call(a, {{"cmd", "get"}, {"id", s31}}), "inbox", 0), "tag") == "CHECK WITH NCS");

    // ---- correct and export ----
    const std::string fixed =
        "VZCZC\r\nRR AAM497 DE AAR7ZZ #0022 3291200\r\nR 251200Z NOV 2026\r\nFM AAR7ZZ\r\nTO AAM497\r\nBT\r\nUNCLAS\r\n"
        "LINE ONE\r\nLINE 2 CORRECTED\r\nLINE THREE\r\nBT\r\nNNNN\r\n";
    CHECK(ok(call(a, {{"cmd", "correct"}, {"id", s22}, {"body", fixed}})));
    g = call(a, {{"cmd", "get"}, {"id", s22}});
    CHECK(str(g, "corrected_text") == fixed);
    CHECK(str(at(g, "messages", 0), "body").find("LINE TWO") != std::string::npos);  // the original stays
    JsonValue x = call(a, {{"cmd", "export"}, {"id", s22}});
    CHECK(ok(x) && len(x, "files") == 2);
    fs::path folder = str(x, "folder");
    CHECK(read_file(folder / at(x, "files", 0).s).find("LINE TWO") != std::string::npos);
    {
        const JsonValue& f1 = at(x, "files", 1);
        CHECK(f1.type == JsonValue::Type::String && read_file(folder / f1.s).find("CORRECTED") != std::string::npos);
    }
    CHECK(!ok(call(a, {{"cmd", "export"}, {"id", "NOPE-2"}})));

    // ---- retransmit: confirm=Y, sends as itself, lines, our key, EMCON ----
    CHECK(!ok(call(a, {{"cmd", "retransmit"}, {"service", "mars"}, {"id", s21}, {"to", "AAR9AA"}})));
    CHECK(!ok(call(a, {{"cmd", "retransmit"}, {"service", "mars"}, {"id", s21}, {"to", "AAR9AA"}, {"de", "AAR1XX"}, {"confirm", "Y"}})));
    CHECK(!ok(call(a, {{"cmd", "retransmit"}, {"service", "mars"}, {"id", s21}, {"confirm", "Y"}})));
    JsonValue rt = call(a, {{"cmd", "retransmit"}, {"service", "mars"}, {"id", s22}, {"to", "AAR9AA"}, {"confirm", "Y"}});
    CHECK(ok(rt) && !str(rt, "out_id").empty());
    CHECK(!str(rt, "route").empty());
    {
        auto m = a.get(str(rt, "out_id"));
        CHECK(m && m->body.find("CORRECTED") != std::string::npos);  // the corrected copy goes
    }
    JsonValue rl = call(a, {{"cmd", "retransmit"}, {"service", "mars"}, {"id", s21}, {"to", "AAR9AA"}, {"lines", "1-2"}, {"confirm", "Y"}});
    CHECK(ok(rl));
    {
        auto m = a.get(str(rl, "out_id"));
        CHECK(m && m->body.rfind("VZCZC", 0) == 0 && m->body.find("BT") == std::string::npos);
    }
    CHECK(!ok(call(a, {{"cmd", "retransmit"}, {"service", "mars"}, {"id", s21}, {"to", "AAR9AA"}, {"lines", "9-99"}, {"confirm", "Y"}})));
    JsonValue rk = call(a, {{"cmd", "retransmit"}, {"service", "mars"}, {"id", s22}, {"to", "AAR9AA"}, {"key", "PRACTICE NET"}, {"confirm", "Y"}});
    CHECK(ok(rk) && !str(rk, "key").empty());
    if (!ok(rk)) std::cout << "retransmit with key: " << str(rk, "error") << "\n";
    {
        auto m = a.get(str(rk, "out_id"));
        CHECK(m && codress::is_codress(m->body));
    }
    CHECK(!ok(call(a, {{"cmd", "retransmit"}, {"service", "mars"}, {"id", s21}, {"to", "AAR9AA"}, {"key", "NO SUCH NET"}, {"confirm", "Y"}})));
    a.set_emcon(true);
    JsonValue re = call(a, {{"cmd", "retransmit"}, {"service", "mars"}, {"id", s21}, {"to", "AAR9AA"}, {"confirm", "Y"}});
    CHECK(ok(re) && !str(re, "emcon").empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    {
        auto m = a.get(str(re, "out_id"));
        CHECK(m && m->state == "queued");  // EMCON holds it
    }
    a.set_emcon(false);
    g = call(a, {{"cmd", "get"}, {"id", s21}});
    bool logged = false;
    for (auto& ev : g.get("events")->arr)
        if (str(ev, "text").find("retransmitted by the operator") != std::string::npos) logged = true;
    CHECK(logged);

    // ---- search ----
    JsonValue s = call(a, {{"cmd", "search"}, {"sender", "aar7"}});
    CHECK(ok(s) && len(s, "messages") == 3);  // the garbled one was discarded
    CHECK(!str(s, "filter").empty());
    CHECK(len(call(a, {{"cmd", "search"}, {"serial", "0021"}}), "messages") == 1);
    CHECK(len(call(a, {{"cmd", "search"}, {"serial", "002%"}}), "messages") == 3);
    CHECK(len(call(a, {{"cmd", "search"}, {"types", "SITREP"}}), "messages") == 1);
    CHECK(len(call(a, {{"cmd", "search"}, {"recipient", "UHW"}}), "messages") == 1);
    CHECK(len(call(a, {{"cmd", "search"}, {"filters", "discarded"}}), "messages") == 1);
    CHECK(len(call(a, {{"cmd", "search"}, {"filters", "processed"}}), "messages") == 1);
    CHECK(len(call(a, {{"cmd", "search"}, {"expr", "type = SITREP"}}), "messages") == 1);
    CHECK(len(call(a, {{"cmd", "search"}, {"expr", "(serial = 0021 OR serial = 0031) AND NOT tag ~ NCS"}}), "messages") == 1);
    CHECK(len(call(a, {{"cmd", "search"}, {"expr", "text ~ \"LINE TWO\""}}), "messages") == 1);
    CHECK(len(call(a, {{"cmd", "search"}, {"expr", "from != AAR7ZZ"}}), "messages") == 1);
    CHECK(!ok(call(a, {{"cmd", "search"}, {"expr", "serial ="}})));
    CHECK(!ok(call(a, {{"cmd", "search"}, {"expr", "color = RED"}})));
    CHECK(!ok(call(a, {{"cmd", "search"}, {"expr", "(serial = 1"}})));
    CHECK(!ok(call(a, {{"cmd", "search"}, {"expr", "serial = 1; DROP TABLE messages"}})));
    CHECK(len(call(a, {{"cmd", "list"}, {"dir", "in"}, {"include_processed", "Y"}}), "messages") == 5);

    // ---- duplicates, log streams, activity counters ----
    CHECK(!c.submit("AAM497", 'P', traffic("AAM497", "AAR7ZZ", 21, "MSGID/SITREP/AAR7ZZ/21//\r\nALL QUIET"), &e, SendOpts::in("mars")).empty());
    CHECK(wait_for([&] {
        for (auto& ev : a.events(0, 1000))
            if (ev.text.find("again") != std::string::npos && ev.text.rfind("heard", 0) == 0) return true;
        return false;
    }));
    JsonValue io = call(a, {{"cmd", "log"}, {"stream", "io"}, {"limit", "500"}});
    CHECK(ok(io));
    bool in_seen = false, out_seen = false;
    for (auto& ev : io.get("events")->arr) {
        if (str(ev, "dir") == "in") in_seen = true;
        if (str(ev, "dir") == "out" && !str(ev, "route").empty()) out_seen = true;
    }
    CHECK(in_seen && out_seen);
    JsonValue app = call(a, {{"cmd", "log"}, {"stream", "app"}, {"limit", "500"}});
    CHECK(ok(app) && len(app, "events") >= 1);
    for (auto& ev : app.get("events")->arr) CHECK(str(ev, "id").empty());
    JsonValue ml = call(a, {{"cmd", "log"}, {"stream", "msg"}, {"q", "0031"}, {"limit", "500"}});
    CHECK(ok(ml) && len(ml, "events") >= 1);
    for (auto& ev : ml.get("events")->arr) CHECK(str(ev, "id") == s31 || str(ev, "text").find("0031") != std::string::npos);
    JsonValue tail = call(a, {{"cmd", "log"}, {"stream", "io"}, {"after", std::to_string(static_cast<long long>(num(io, "last")))}});
    CHECK(ok(tail) && len(tail, "events") == 0);
    CHECK(!ok(call(a, {{"cmd", "log"}, {"stream", "everything"}})));
    CHECK(ok(call(a, {{"cmd", "log"}})));  // the plain call, unchanged

    JsonValue ac = call(a, {{"cmd", "activity_counters"}});
    CHECK(ok(ac));
    CHECK(num(ac, "stored") == 5);
    CHECK(num(ac, "received_session") >= 3);
    CHECK(num(ac, "processed_session") >= 1);
    CHECK(num(ac, "duplicates_session") >= 1);
    CHECK(num(ac, "purged_session") == 0);

    // ---- reports ----
    JsonValue sl = call(a, {{"cmd", "report"}, {"kind", "station_log"}});
    CHECK(ok(sl) && str(sl, "text").find("STATION LOG AAM497") != std::string::npos);
    CHECK(str(sl, "text").find(s21) != std::string::npos);
    CHECK(len(sl, "pages") >= 1);
    {
        std::istringstream in(str(sl, "text"));
        bool narrow = true;
        for (std::string l; std::getline(in, l);) narrow = narrow && l.size() <= 69;
        CHECK(narrow);
    }
    JsonValue small = call(a, {{"cmd", "report"}, {"kind", "station_log"}, {"lines_per_page", "10"}});
    CHECK(len(small, "pages") == static_cast<size_t>((num(small, "lines") + 9) / 10));
    JsonValue cs = call(a, {{"cmd", "report"}, {"kind", "comtrafstat"}});
    CHECK(ok(cs) && str(cs, "text").find("PENDING ACTION") != std::string::npos);
    CHECK(str(cs, "text").find("OUTGOING NOT YET DELIVERED") != std::string::npos);
    JsonValue sum = call(a, {{"cmd", "report"}, {"kind", "comtrafsum"}});
    CHECK(ok(sum) && str(sum, "text").find("RECEIVED") != std::string::npos);
    CHECK(sum.get("draft") && str(*sum.get("draft"), "template") == "FREE_FORM");
    CHECK(sum.get("draft") && !str(*sum.get("draft"), "text").empty());
    CHECK(!ok(call(a, {{"cmd", "report"}, {"kind", "weather"}})));
    CHECK(!ok(call(a, {{"cmd", "report"}, {"kind", "station_log"}, {"from", "2026-02-30"}})));
    CHECK(!ok(call(a, {{"cmd", "report"}, {"kind", "station_log"}, {"from", "2026-11-10"}, {"to", "2026-11-01"}})));
    JsonValue old = call(a, {{"cmd", "report"}, {"kind", "station_log"}, {"from", "2020-01-01"}, {"to", "2020-01-02"}});
    CHECK(ok(old) && str(old, "text").find("NOTHING LOGGED") != std::string::npos);
    // report_export writes the same text to the export folder, as KIND-FROM-TO.txt.
    {
        JsonValue rx = call(a, {{"cmd", "report_export"}, {"kind", "comtrafsum"}, {"from", "2026-11-01"}, {"to", "2026-11-02"}});
        CHECK(ok(rx) && str(rx, "file") == "comtrafsum-2026-11-01-2026-11-02.txt");
        JsonValue same = call(a, {{"cmd", "report"}, {"kind", "comtrafsum"}, {"from", "2026-11-01"}, {"to", "2026-11-02"}});
        std::string want = str(same, "text");
        if (!want.empty() && want.back() != '\n') want += "\n";
        CHECK(read_file(fs::path(str(rx, "folder")) / str(rx, "file")) == want);
        CHECK(num(rx, "bytes") == static_cast<double>(want.size()));
        CHECK(!ok(call(a, {{"cmd", "report_export"}, {"kind", "weather"}})));
    }

    // ---- purge: old processed/discarded and finished outgoing; dry run first ----
    CHECK(ok(call(a, {{"cmd", "mark"}, {"id", s21}, {"state", "processed"}})));  // QSL'd above
    CHECK(wait_for([&] {
        for (auto& m : a.list(100))
            if (m.direction == "out" && (m.state == "queued" || m.state == "sending")) return false;
        return true;
    }, 15000));
    {
        sqlite3* db = nullptr;
        CHECK(sqlite3_open((dir / "a.db").string().c_str(), &db) == SQLITE_OK);
        sqlite3_busy_timeout(db, 2000);
        const std::string old = std::to_string(static_cast<long long>(std::time(nullptr)) - 40 * 86400);
        CHECK(sqlite3_exec(db, ("UPDATE inbox_flags SET done_at = " + old + " WHERE done <> ''").c_str(), nullptr,
                           nullptr, nullptr) == SQLITE_OK);
        CHECK(sqlite3_exec(db, ("UPDATE messages SET created_at = " + old + ", updated_at = " + old).c_str(), nullptr,
                           nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
    }
    int finished_out = 0, owed_out = 0;
    for (auto& m : a.list(100)) {
        if (m.direction != "out") continue;
        if (m.state == "acked" || m.state == "cancelled" || m.state == "failed" || m.state == "expired") ++finished_out;
        if (m.state == "sent") ++owed_out;
    }
    JsonValue pd = call(a, {{"cmd", "purge"}});
    CHECK(ok(pd) && flag(pd, "dry_run"));
    CHECK(num(pd, "days") == 30);
    CHECK(num(pd, "in") == 2);  // the discarded garbled one and s21 (processed, QSL'd)
    CHECK(num(pd, "out") == finished_out);
    CHECK(num(pd, "purged") == 0);
    CHECK(pd.get("kept") && num(*pd.get("kept"), "owed_qsl") == 1 + owed_out);  // s22: processed, never QSL'd
    CHECK(pd.get("kept") && num(*pd.get("kept"), "pending") >= 2);             // s23 and s31: never marked
    CHECK(ok(call(a, {{"cmd", "get"}, {"id", garbled}})));  // the dry run took nothing
    CHECK(num(call(a, {{"cmd", "purge"}, {"days", "60"}}), "count") == 0);
    CHECK(!ok(call(a, {{"cmd", "purge"}, {"days", "0"}})));
    JsonValue pr = call(a, {{"cmd", "purge"}, {"dry_run", "N"}});
    CHECK(ok(pr) && !flag(pr, "dry_run") && num(pr, "purged") == num(pd, "count"));
    CHECK(!ok(call(a, {{"cmd", "get"}, {"id", garbled}})));
    CHECK(!ok(call(a, {{"cmd", "get"}, {"id", s21}})));
    CHECK(ok(call(a, {{"cmd", "get"}, {"id", s22}})));  // owed a QSL: kept
    CHECK(ok(call(a, {{"cmd", "get"}, {"id", s31}})));  // pending: kept
    CHECK(num(call(a, {{"cmd", "activity_counters"}}), "purged_session") == num(pr, "purged"));
    CHECK(num(call(a, {{"cmd", "purge"}}), "count") == 0);
    {
        bool said = false;
        for (auto& ev : a.events(0, 2000))
            if (ev.text.rfind("purged ", 0) == 0) said = true;
        CHECK(said);
    }

    // ---- reply_draft (phoenix_nest_mars#3): to and subject filled in, nothing sent ----
    {
        const size_t out_before = c.list(1000).size() + a.list(1000).size();
        JsonValue rd = call(a, {{"cmd", "reply_draft"}, {"id", s31}});
        CHECK(ok(rd));
        CHECK(str(rd, "to_station") == "AAR9AA" && str(rd, "to") == "AAR9AA");
        CHECK(str(rd, "template") == "GENADMIN");
        CHECK(rd.get("values") && str(*rd.get("values"), "to") == "AAR9AA");
        CHECK(rd.get("in_reply_to") && str(*rd.get("in_reply_to"), "id") == s31);
        CHECK(!ok(call(a, {{"cmd", "reply_draft"}, {"id", "NOPE-1"}})));
        // A message with a subject and an FM line.
        CHECK(!c.submit("AAM497", 'P',
                        "VZCZC\r\nPP AAM497 DE AAR7ZZ #0025 3291300\r\nZNR UUUUU\r\nP 251300Z NOV 26\r\n"
                        "FM MARS STATION AAR7ZZ\r\nTO MARS STATION AAM497\r\nBT\r\nUNCLAS\r\n"
                        "MSGID/GENADMIN/AAR7ZZ/25//\r\nSUBJ/NET SCHEDULE CHANGE//\r\nRMKS/SEE YOU THERE//\r\nBT\r\n#0025\r\nNNNN\r\n",
                        &e, SendOpts::in("mars"))
                    .empty());
        std::string s25;
        CHECK(wait_for([&] {
            s25 = id_by_serial(call(a, {{"cmd", "list"}, {"dir", "in"}, {"include_processed", "Y"}}), "0025");
            return !s25.empty();
        }));
        JsonValue r2 = call(a, {{"cmd", "reply_draft"}, {"id", s25}});
        CHECK(ok(r2) && str(r2, "subject") == "NET SCHEDULE CHANGE");
        CHECK(str(r2, "to_station") == "AAR7ZZ" && str(r2, "to") == "MARS STATION AAR7ZZ");
        CHECK(r2.get("values") && str(*r2.get("values"), "subj") == "NET SCHEDULE CHANGE");
        CHECK(str(r2, "prec") == "P");
        // The draft opens in compose and checks there.
        CHECK(ok(call(a, {{"cmd", "compose_form"}, {"template", str(r2, "template")}, {"procedure", str(r2, "procedure")}})));
        CHECK(c.list(1000).size() + a.list(1000).size() == out_before + 2);  // only the one C sent, at each end
    }

    a.stop();
    b.stop();
    c.stop();
    fs::remove_all(dir);
    return check::report("test_inbox");
}
