// Ask 40: the core's own BBS never routes, pulls, answers or QSLs traffic of
// the amateur world, and never sends to it again on its own: an item whose
// world isn't mars, or that came from (or would go to) an amateur station, is
// refused with the reason, one check per automatic path. Every send the BBS
// makes goes through Core::submit, and every resend through the airtime check
// (test_worlds). Every call and RI here is made up.
#include "../src/api_server.h"
#include "../src/bbs.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/station_view.h"
#include "check.h"

#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
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
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static double num(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::Number ? x->n : -1;
}
static const JsonValue& at(const JsonValue& v, const char* k, size_t i) {
    static const JsonValue none;
    const JsonValue* x = v.get(k);
    return x && x->is_array() && i < x->arr.size() ? x->arr[i] : none;
}
static std::vector<Message> out_to(Core& c, const std::string& dst) {
    std::vector<Message> o;
    for (auto& m : c.list(1000))
        if (m.direction == "out" && m.dst == dst) o.push_back(m);
    return o;
}
static bool got(Core& c, const std::string& text) {
    for (auto& m : c.list(1000))
        if (m.direction == "in" && m.body.find(text) != std::string::npos) return true;
    return false;
}
static std::string traffic(const std::string& fl2, const std::string& de, int serial, const std::string& text) {
    char sn[8];
    std::snprintf(sn, sizeof sn, "%04d", serial);
    return "VZCZC\r\nRR " + fl2 + " DE " + de + " #" + sn + " 3291200\r\nBT\r\nUNCLAS\r\n" + text + "\r\nBT\r\nNNNN\r\n";
}
// The routed row for one held message.
static JsonValue routed_row(Core& a, const std::string& msg_id) {
    JsonValue r = call(a, {{"cmd", "route_list"}});
    for (size_t i = 0; i < 50; ++i) {
        const JsonValue& x = at(r, "routed", i);
        if (str(x, "msg_id") == msg_id) return x;
        if (!x.get("msg_id")) break;
    }
    return JsonValue();
}
static std::string in_id(Core& a, const std::string& text) {
    for (auto& m : a.list(1000))
        if (m.direction == "in" && m.body.find(text) != std::string::npos) return m.id;
    return "";
}

static bool logged(Core& c, const std::string& s) {
    for (auto& e : c.events(0, 5000))
        if (e.text.find(s) != std::string::npos) return true;
    return false;
}
static std::string chat(const std::string& to, const std::string& from, const std::string& text) {
    return to + " DE " + from + " // " + text + " // 1200Z K";
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_bbs_worlds_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / "gale" / "amateur");
    { std::ofstream f(dir / "gale" / "amateur" / "own_address.txt"); f << "N0CALL\n"; }
    {
        sv::Paths p;
        p.gale_dir = (dir / "gale").string();
        p.store_root = dir.string();
        sv::set_paths(p);
    }
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    auto mc = std::make_unique<LoopbackModem>(0);
    auto mk = std::make_unique<LoopbackModem>(0);
    auto md = std::make_unique<LoopbackModem>(0);
    LoopbackModem::share({ma.get(), mb.get(), mc.get(), mk.get(), md.get()});
    CoreConfig ca{"AAM499", (dir / "a.db").string(), 3, 1, true};
    ca.ack_timeout_s = 1;
    ca.ack_retries = 0;
    Core a(ca, std::move(ma));
    Core b(CoreConfig{"AAR9AA", (dir / "b.db").string(), 3, 1, true}, std::move(mb));
    Core c(CoreConfig{"AAR7ZZ", (dir / "c.db").string(), 3, 1, true}, std::move(mc));
    // K7AMA: a station the book marks amateur. AAR3HM: not on the air yet.
    Core k(CoreConfig{"K7AMA", (dir / "k.db").string(), 3, 1, true}, std::move(mk));
    Core d(CoreConfig{"AAR3HM", (dir / "d.db").string(), 3, 1, true}, std::move(md));
    auto ops = std::make_shared<Ops>("AAM499", (dir / "a-ops.db").string());
    a.set_ops(ops);
    a.start();
    b.start();
    c.start();
    k.start();
    std::string e;
    CHECK(c.set_link_format("AAM499", "plain", &e));
    CHECK(k.set_link_format("AAM499", "plain", &e));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "HAM ONE"}, {"call", "K7AMA"}, {"world", "amateur"}})));
    bbs::Engine& bbs_a = bbs::engine(a);
    std::atomic<int64_t> now{static_cast<int64_t>(std::time(nullptr))};
    bbs_a.set_clock([&] { return now.load(); });
    auto pump_until = [&](const std::function<bool()>& cond, int ms = 10000) {
        return wait_for(
            [&] {
                bbs_a.pump();
                return cond();
            },
            ms);
    };
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"machine_mode", "true"}, {"routing", "true"}, {"accept", "all"},
                      {"ack", "auto"}, {"ack_delay_ms", "0"}, {"auto_per_hour", "120"}, {"commands_ct_only", "false"}})));
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE WEST"}, {"ale", "AAR9AA"}})));

    // ---- set-up: a QSL can't be pointed at the amateur world ----------------
    {
        JsonValue r = call(a, {{"cmd", "bbs_set"}, {"ack_to", "K7AMA"}});
        CHECK(!ok(r) && str(r, "error").find("amateur") != std::string::npos);
        r = call(a, {{"cmd", "bbs_set"}, {"ack_to", "N0CALL"}});
        CHECK(!ok(r) && str(r, "error").find("amateur") != std::string::npos);
    }

    // ---- traffic from an amateur station: no QSL, no route, never held ------
    CHECK(!k.submit("AAM499", 'R', traffic("UHWTST", "K7AMA", 1, "FROM THE AMATEUR SIDE"), &e).empty());
    CHECK(wait_for([&] { return got(a, "FROM THE AMATEUR SIDE"); }));
    const std::string ham_id = in_id(a, "FROM THE AMATEUR SIDE");
    CHECK(pump_until([&] { return logged(a, "BBS: " + ham_id + " not answered or routed"); }));
    CHECK(!routed_row(a, ham_id).is_object() || str(routed_row(a, ham_id), "msg_id").empty());
    CHECK(out_to(a, "K7AMA").empty() && out_to(a, "AAR9AA").empty());
    // A re-run of the rules over held traffic doesn't pick it up either.
    {
        JsonValue r = call(a, {{"cmd", "route_rerun"}, {"dry_run", "false"}});
        CHECK(ok(r) && num(r, "count") == 0);
        CHECK(out_to(a, "AAR9AA").empty());
    }

    // ---- a machine command from an amateur station: no answer ---------------
    CHECK(!k.submit("AAM499", 'R', chat("AAM499-CM", "K7AMA", "VER"), &e).empty());
    CHECK(pump_until([&] {
        int n = 0;
        for (auto& ev : a.events(0, 5000))
            if (ev.text.find("not answered or routed: it came from K7AMA") != std::string::npos) ++n;
        return n >= 2;
    }));
    CHECK(out_to(a, "K7AMA").empty());

    // ---- an item whose world isn't mars: not answered or routed -------------
    CHECK(!c.submit("AAM499", 'R', traffic("UHWAAA", "AAR7ZZ", 2, "WORLD CHANGED UNDERFOOT"), &e).empty());
    CHECK(wait_for([&] { return got(a, "WORLD CHANGED UNDERFOOT"); }));
    const std::string flip_id = in_id(a, "WORLD CHANGED UNDERFOOT");
    {
        sqlite3* db = nullptr;
        CHECK(sqlite3_open((dir / "a.db").string().c_str(), &db) == SQLITE_OK);
        sqlite3_busy_timeout(db, 5000);
        const std::string sql = "UPDATE messages SET world = 'amateur' WHERE id = '" + flip_id + "'";
        CHECK(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
    }
    CHECK(pump_until([&] { return logged(a, "BBS: " + flip_id + " not answered or routed: it is an item of the amateur world"); }));
    CHECK(out_to(a, "AAR9AA").empty() && out_to(a, "AAR7ZZ").empty());

    // ---- a route that comes due after its sender turned out amateur ---------
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"match", "UHS"}, {"route", "ALE SOUTH"}, {"ale", "AAR9AA"}, {"sched", "2359Z"}})));
    CHECK(!c.submit("AAM499", 'R', traffic("UHSBBB", "AAR7ZZ", 3, "FOR THE SOUTH LATER"), &e).empty());
    CHECK(wait_for([&] { return got(a, "FOR THE SOUTH LATER"); }));
    const std::string later_id = in_id(a, "FOR THE SOUTH LATER");
    CHECK(pump_until([&] { return str(routed_row(a, later_id), "state") == "due"; }));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "SEVEN ZZ"}, {"call", "AAR7ZZ"}, {"world", "amateur"}})));
    now = now.load() + 2 * 86400;
    CHECK(pump_until([&] { return str(routed_row(a, later_id), "state") == "refused"; }));
    CHECK(str(routed_row(a, later_id), "why").find("amateur") != std::string::npos);
    CHECK(logged(a, "BBS: routing " + later_id + " refused: it came from AAR7ZZ is an amateur station"));
    CHECK(out_to(a, "AAR9AA").empty());

    // ---- unacked traffic, its station later known amateur: never sent again --
    const std::string un = a.submit("AAR3HM", 'R', "WAITING FOR YOU", &e);
    CHECK(!un.empty());
    CHECK(wait_for([&] {
        auto m = a.get(un);
        return m && m->state == "unacked";
    }));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "THREE HM"}, {"call", "AAR3HM"}, {"world", "amateur"}})));
    d.start();
    CHECK(!d.submit("AAM499", 'R', "HEARD NOW", &e).empty());
    CHECK(wait_for([&] {
        auto m = a.get(un);
        return m && m->state == "failed";
    }));
    {
        auto m = a.get(un);
        CHECK(m && m->last_error.find("amateur") != std::string::npos);
        CHECK(logged(a, "not sent again: AAR3HM is an amateur station"));
    }

    // ---- MARS traffic still routes ------------------------------------------
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "SEVEN ZZ"}, {"call", "AAR7ZZ"}, {"world", "mars"}})));
    CHECK(!c.submit("AAM499", 'R', traffic("UHWCCC", "AAR7ZZ", 4, "STILL GOES WEST"), &e).empty());
    CHECK(pump_until([&] { return got(b, "STILL GOES WEST"); }));

    a.stop();
    b.stop();
    c.stop();
    k.stop();
    d.stop();
    fs::remove_all(dir);
    return check::report("test_bbs_worlds");
}
