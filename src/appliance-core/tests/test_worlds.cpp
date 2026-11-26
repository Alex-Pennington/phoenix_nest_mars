// Ask 37 and audit B: no route between MARS and amateur, ever (Alex's hard
// rule). Every item, rule and book entry carries its world, and a send to a
// station of the amateur world is refused in code with an error, never
// rerouted. One check per send path, and the airtime point behind them all.
// Every call here is made up (N0CALL, K0TEST and the like).
#include "../src/api_server.h"
#include "../src/bbs.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/station_view.h"
#include "check.h"

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
// Refused with an error that says why: the amateur world.
static bool refused(const JsonValue& v) {
    return !ok(v) && str(v, "error").find("amateur") != std::string::npos;
}
static int out_count(Core& c) {
    int n = 0;
    for (auto& m : c.list(1000))
        if (m.direction == "out") ++n;
    return n;
}
static bool logged(Core& c, const std::string& s) {
    for (auto& e : c.events(0, 2000))
        if (e.text.find(s) != std::string::npos) return true;
    return false;
}
static void write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << s;
}
static std::string word(double unix, const char* type, const char* chars) {
    char b[300];
    std::snprintf(b, sizeof b,
                  R"({"unix":%.3f,"khz":4864.213,"type":"%s","chars":"%s","word":"a1b2c3","sinad":9.0,"fec":0,)"
                  R"("votes":3,"engine":"local","gap":false,"window":1,"tx":0,"clock":"wwv","world":"amateur"})",
                  unix, type, chars);
    return b;
}
static std::string chat(const std::string& to, const std::string& from, const std::string& text) {
    return to + " DE " + from + " // " + text + " // 1200Z K";
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_worlds_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::path gale = dir / "gale";
    fs::create_directories(gale);
    // The amateur world as the scanner leaves it: its own address file, and a
    // word log where K0TEST called N0CALL on an amateur channel.
    write(gale / "amateur/own_address.txt", "N0CALL\n");
    const double now = static_cast<double>(std::time(nullptr));
    write(gale / "amateur/words-20991231.jsonl", word(now - 8, "TO", "N0C") + "\n" + word(now - 7.6, "DATA", "ALL") +
                                                     "\n" + word(now - 7.4, "TIS", "K0T") + "\n" +
                                                     word(now - 7.2, "DATA", "EST") + "\n");
    sv::Paths p;
    p.gale_dir = gale.string();
    p.store_root = dir.string();
    sv::set_paths(p);
    {
        auto am = sv::amateur_calls();
        CHECK(am.count("N0CALL") && am.count("K0TEST") && !am.count("STA-B"));
    }

    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    auto mh = std::make_unique<LoopbackModem>(0);
    LoopbackModem* air_a = ma.get();
    LoopbackModem::share({ma.get(), mb.get(), mh.get()});
    Core a(CoreConfig{"STA-A", (dir / "a.db").string(), 3, 1, true}, std::move(ma));
    Core b(CoreConfig{"STA-B", (dir / "b.db").string(), 3, 1, true}, std::move(mb));
    // K7AMA: a station the book marks amateur, on the same test channel.
    Core h(CoreConfig{"K7AMA", (dir / "h.db").string(), 3, 1, true}, std::move(mh));
    auto ops = std::make_shared<Ops>("STA-A", (dir / "a-ops.db").string());
    a.set_ops(ops);
    a.start();
    b.start();
    h.start();
    std::string e;

    // ---- the book: every entry carries its world ----------------------------
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "STA-B"}, {"kind", "station"}})));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "HAM ONE"}, {"call", "K7AMA"}, {"world", "amateur"}})));
    CHECK(!ok(call(a, {{"cmd", "book_put"}, {"name", "HAM TWO"}, {"call", "K6AMA"}, {"world", "narnia"}})));
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "MIXED"}, {"kind", "collective"}, {"members", "STA-B;HAM ONE"}})));
    CHECK(ops->book_find("K7AMA") && ops->book_find("K7AMA")->world == "amateur");
    CHECK(ops->book_find("STA-B") && ops->book_find("STA-B")->world == "mars");
    CHECK(ops->book_export().find(",amateur\n") != std::string::npos);

    // ---- the reason, in code ------------------------------------------------
    CHECK(a.world_refusal("STA-B").empty() && a.world_refusal("ALL").empty() && a.world_refusal("").empty());
    CHECK(a.world_refusal("N0CALL").find("own amateur address") != std::string::npos);
    CHECK(a.world_refusal("k0test").find("heard on amateur") != std::string::npos);
    CHECK(a.world_refusal("@K0TEST@").find("amateur") != std::string::npos);
    CHECK(a.world_refusal("K7AMA").find("station book") != std::string::npos);

    // ---- Core::submit, behind every send ------------------------------------
    CHECK(a.submit("K0TEST", 'R', "TEST", &e).empty() && e.find("amateur") != std::string::npos);
    CHECK(a.submit("N0CALL", 'R', "TEST", &e).empty());
    CHECK(a.submit("K7AMA", 'R', "TEST", &e).empty());
    CHECK(out_count(a) == 0);  // nothing queued, nothing rerouted

    // ---- send (and a book name, and a collective holding an amateur) --------
    CHECK(refused(call(a, {{"cmd", "send"}, {"to", "K0TEST"}, {"body", "TEST"}})));
    CHECK(refused(call(a, {{"cmd", "send"}, {"to", "HAM ONE"}, {"body", "TEST"}})));
    CHECK(refused(call(a, {{"cmd", "send"}, {"to", "MIXED"}, {"body", "TEST"}})));
    CHECK(out_count(a) == 0);  // not even STA-B's copy of MIXED

    // ---- a call that names the amateur world --------------------------------
    CHECK(refused(call(a, {{"cmd", "send"}, {"to", "STA-B"}, {"body", "TEST"}, {"world", "amateur"}})));
    CHECK(refused(call(a, {{"cmd", "send_chat"}, {"to_station", "STA-B"}, {"call", "STA-B-CM"}, {"station", "STA-A"},
                           {"text", "TEST"}, {"world", "both"}})));
    CHECK(out_count(a) == 0);
    CHECK(ok(call(a, {{"cmd", "word_feed"}, {"world", "amateur"}})));  // reading it is fine

    // ---- the other send paths -----------------------------------------------
    CHECK(refused(call(a, {{"cmd", "send_chat"}, {"to_station", "K0TEST"}, {"call", "K0TEST-CM"}, {"station", "STA-A"},
                           {"text", "QRU"}})));
    CHECK(refused(call(a, {{"cmd", "send_acp127"}, {"to_station", "K0TEST"}, {"prec", "R"}, {"channel", "QXA"}, {"routing", "RXAAAA"},
                           {"originator_ri", "RXCCCC"}, {"from", "RXCCCC/TEST STATION ONE"},
                           {"to", "RXAAAA/TEST STATION TWO"}, {"text", "TEST"}})));
    CHECK(refused(call(a, {{"cmd", "send_acp126"}, {"to_station", "N0CALL"}, {"prec", "R"}, {"calls", "N0CALL"},
                           {"station", "STA-A"}, {"text", "TEST"}})));
    CHECK(refused(call(a, {{"cmd", "compose_send"},
                           {"template", "FREE_FORM"},
                           {"procedure", "ACP127"},
                           {"prec", "R"},
                           {"routing", "RXAAAA"},
                           {"originator", "STA-A"},
                           {"from", "TEST STATION ONE"},
                           {"to", "RXAAAA/TEST STATION TWO"},
                           {"to_station", "K7AMA"},
                           {"sign", "N"},
                           {"text", "TEST"}})));
    CHECK(out_count(a) == 0);

    // ---- retransmit, from the in box ----------------------------------------
    CHECK(!b.submit("STA-A", 'R', "FROM B", &e).empty());
    std::string in_id;
    CHECK(wait_for([&] {
        for (auto& m : a.list(100))
            if (m.direction == "in" && m.src == "STA-B") in_id = m.id;
        return !in_id.empty();
    }));
    {
        auto m = a.get(in_id);
        CHECK(m && m->world == "mars");
    }
    CHECK(refused(call(a, {{"cmd", "retransmit"}, {"id", in_id}, {"to", "K0TEST"}, {"confirm", "Y"}})));
    CHECK(ok(call(a, {{"cmd", "retransmit"}, {"id", in_id}, {"to", "STA-B"}, {"confirm", "Y"}})));  // MARS still goes

    // ---- routing rules (the BBS) --------------------------------------------
    CHECK(refused(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE WEST"}, {"ale", "K0TEST"}})));
    CHECK(refused(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE WEST"}, {"ale", "STA-B,K7AMA"}})));
    CHECK(refused(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE WEST"}, {"ale", "STA-B"},
                           {"world", "amateur"}})));
    {
        bbs::Rule r;
        r.match = "UHW";
        r.route = "ALE WEST";
        r.ale = {"STA-B"};
        r.world = "amateur";
        CHECK(!bbs::engine(a).set_rule(r, nullptr, &e) && e.find("amateur") != std::string::npos);
        r.world = "mars";
        CHECK(bbs::engine(a).set_rule(r, nullptr, &e));
        CHECK(!bbs::engine(a).rules().empty() && bbs::engine(a).rules()[0].world == "mars");
    }

    // ---- the airtime point: an item that became amateur after it was queued --
    a.set_emcon(true);
    const std::string late = a.submit("K9LATE", 'R', "QUEUED BEFORE", &e);
    CHECK(!late.empty());
    CHECK(ok(call(a, {{"cmd", "book_put"}, {"name", "LATE HAM"}, {"call", "K9LATE"}, {"world", "amateur"}})));
    a.set_emcon(false);
    CHECK(wait_for([&] {
        auto m = a.get(late);
        return m && m->state == "failed";
    }));
    {
        auto m = a.get(late);
        CHECK(m && m->last_error.find("amateur") != std::string::npos);
        CHECK(logged(a, "refused at the air"));
    }
    // requeue and release refuse it too.
    CHECK(refused(call(a, {{"cmd", "requeue"}, {"id", late}})));
    CHECK(!a.requeue(late, &e) && e.find("amateur") != std::string::npos);
    {
        auto m = a.get(late);
        CHECK(m && m->state == "failed");
    }

    // ---- acks and machine answers to an amateur station ---------------------
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"machine_mode", "true"}, {"ack_delay_ms", "0"}, {"auto_per_hour", "120"},
                      {"commands_ct_only", "false"}})));
    // A MARS net is open: an amateur station heard is never checked in (audit C).
    const std::string net = ops->net_open("MARS NET", {}, false, "", "", &e);
    CHECK(!net.empty());
    CHECK(!h.submit("STA-A", 'R', chat("STA-A-CM", "K7AMA", "VER"), &e).empty());
    CHECK(wait_for([&] {
        for (auto& m : a.list(100))
            if (m.direction == "in" && m.src == "K7AMA") return true;
        return false;
    }));
    CHECK(wait_for([&] {
        bbs::engine(a).pump();
        return logged(a, "not sent to K7AMA") && logged(a, "not answered or routed: it came from K7AMA");
    }));
    for (auto& m : a.list(100)) CHECK(!(m.direction == "out" && m.dst == "K7AMA"));
    {
        auto n = ops->net_get(net);
        CHECK(n && n->checkins.empty());
        CHECK(!b.submit("STA-A", 'R', "STA-B CHECKING IN", &e).empty());  // a MARS station still is
        CHECK(wait_for([&] {
            auto m = ops->net_get(net);
            return m && m->checkins.size() == 1 && m->checkins[0].station == "STA-B";
        }));
    }

    // ---- a broadcast is still a broadcast -----------------------------------
    CHECK(!a.submit("ALL", 'R', "TO ALL STATIONS", &e).empty());
    (void)air_a;

    a.stop();
    b.stop();
    h.stop();
    fs::remove_all(dir);
    return check::report("test_worlds");
}
