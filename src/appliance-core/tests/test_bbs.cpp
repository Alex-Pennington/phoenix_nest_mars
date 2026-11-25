// The station as a BBS (bbs.h): machine mode (VER, QRU, ZEE, ZDK, QSL), auto
// QSL, the guard list and alias map, routing rules, and principle 5: nothing
// is sent by itself unless the operator turned machine mode (and routing) on,
// and then never under EMCON or past the hourly cap. Three stations on one
// loopback channel: A (the BBS), B (a member pulling traffic with commands),
// C (an MSC-style station sending plain ACP 127). Every call and RI here is
// made up.
#include "../src/api_server.h"
#include "../src/bbs.h"
#include "../src/codress.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "check.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
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
// Element i of array k, or an empty value (so a failed check doesn't crash).
static const JsonValue& at(const JsonValue& v, const char* k, size_t i) {
    static const JsonValue none;
    const JsonValue* x = v.get(k);
    return x && x->is_array() && i < x->arr.size() ? x->arr[i] : none;
}

// Outgoing messages of a core to one station, in the order queued (by the
// number in the id: list() is by last change).
static std::vector<Message> out_to(Core& c, const std::string& dst) {
    std::vector<Message> o;
    for (auto& m : c.list(1000))
        if (m.direction == "out" && m.dst == dst) o.push_back(m);
    auto n = [](const Message& m) { return std::stoll(m.id.substr(m.id.rfind('-') + 1)); };
    std::sort(o.begin(), o.end(), [&](const Message& x, const Message& y) { return n(x) < n(y); });
    return o;
}
// The body of the i-th outgoing message to dst ("" when there isn't one; -1: the last).
static std::string nth(Core& c, const std::string& dst, int i) {
    auto o = out_to(c, dst);
    if (i < 0) i = static_cast<int>(o.size()) - 1;
    return i >= 0 && i < static_cast<int>(o.size()) ? o[i].body : "";
}
// Bodies a core received from one station.
static std::vector<std::string> got(Core& c, const std::string& src) {
    std::vector<std::string> o;
    for (auto& m : c.list(1000))
        if (m.direction == "in" && m.src == src) o.insert(o.begin(), m.body);
    return o;
}
static bool any_has(const std::vector<std::string>& v, const std::string& s) {
    for (auto& x : v)
        if (x.find(s) != std::string::npos) return true;
    return false;
}

static std::string chat(const std::string& to, const std::string& from, const std::string& text) {
    return to + " DE " + from + " // " + text + " // 1200Z K";
}
static std::string traffic(const std::string& fl2, const std::string& de, int serial, const std::string& text) {
    char sn[8];
    std::snprintf(sn, sizeof sn, "%04d", serial);
    return "VZCZC\r\nRR " + fl2 + " DE " + de + " #" + sn + " 3291200\r\nBT\r\nUNCLAS\r\n" + text + "\r\nBT\r\nNNNN\r\n";
}

int main() {
    // ---- pure helpers ----
    CHECK(bbs::matches("UHE", "UHEA"));
    CHECK(bbs::matches("UHE*", "UHEB"));
    CHECK(!bbs::matches("UHE", "UHW"));
    CHECK(bbs::matches("AAR*", "AAR9AA"));
    CHECK(!bbs::matches("", "AAR9AA"));
    {
        auto r = bbs::recipients(traffic("UHWTST AAR9AA", "AAR7ZZ", 1, "TEST"));
        CHECK(r.size() == 2 && r[0] == "UHWTST" && r[1] == "AAR9AA");
        CHECK(bbs::recipients("AAM499-CM DE AAR9AA // VER // 1200Z K").empty());
        std::string b = traffic("UHWTST UHWHHCF", "AAR7ZZ", 2, "UHWHHCF IN TEXT STAYS");
        std::string s = bbs::strip_ri(b, "UHWHHCF");
        CHECK(s.find("RR UHWTST DE AAR7ZZ") != std::string::npos);
        CHECK(s.find("UHWHHCF IN TEXT STAYS") != std::string::npos);  // only the header is touched
        CHECK(bbs::strip_ri(b, "") == b);
        bbs::Settings st;
        CHECK(bbs::zee_limit(st) == 8192);  // 600 bps x 120 s / 8 = 9000, at most one message
        st.data_rate_bps = 75;
        CHECK(bbs::zee_limit(st) == 1125);
        st.zee_max_bytes = 300;
        CHECK(bbs::zee_limit(st) == 300);
    }

    fs::path dir = fs::temp_directory_path() / ("pna_bbs_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    auto mc = std::make_unique<LoopbackModem>(0);
    LoopbackModem::share({ma.get(), mb.get(), mc.get()});
    Core a(CoreConfig{"AAM499", (dir / "a.db").string(), 3, 1, true}, std::move(ma));
    Core b(CoreConfig{"AAR9AA", (dir / "b.db").string(), 3, 1, true}, std::move(mb));
    Core c(CoreConfig{"AAR7ZZ", (dir / "c.db").string(), 3, 1, true}, std::move(mc));
    // A practice key tape (made here, thrown away) for A and B: our own cipher.
    std::string e;
    const int64_t t0 = static_cast<int64_t>(std::time(nullptr));
    keys::Tape tape;
    CHECK(keys::make_tape("PRACTICE NET", t0 - t0 % 3600 - 86400, 3, 24, t0, tape, &e));
    const std::string today = tape.tape + "02";
    auto ka = std::make_shared<keys::Keyring>((dir / "a.keys").string());
    auto kb = std::make_shared<keys::Keyring>((dir / "b.keys").string());
    CHECK(ka->load(&e) && kb->load(&e));
    a.set_keyring(ka);
    b.set_keyring(kb);
    a.start();
    b.start();
    c.start();
    CHECK(ok(call(a, {{"cmd", "key_import"}, {"text", keys::format_tape(tape)}})));
    CHECK(ok(call(b, {{"cmd", "key_import"}, {"text", keys::format_tape(tape)}})));
    CHECK(c.set_link_format("AAM499", "plain", &e));  // C is an MSC station: bare ACP 127
    bbs::Engine& bbs_a = bbs::engine(a);
    auto pump_until = [&](const std::function<bool()>& cond) {
        return wait_for([&] {
            bbs_a.pump();
            return cond();
        });
    };
    auto settle = [&] {
        for (int i = 0; i < 10; ++i) {
            bbs_a.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
    };
    auto a_answers = [&] { return out_to(a, "AAR9AA").size(); };

    // ---- defaults: everything off ----
    JsonValue r = call(a, {{"cmd", "bbs_get"}});
    CHECK(ok(r) && !flag(r, "machine_mode") && !flag(r, "routing"));
    CHECK(str(r, "suffix") == "-CM" && str(r, "machine_call") == "AAM499-CM");
    CHECK(str(r, "accept") == "calls" && str(r, "ack") == "auto" && num(r, "ack_delay_ms") == 2000);
    CHECK(flag(r, "commands_ct_only") && num(r, "max_errors") == 3 && !flag(r, "accept_malformed"));
    CHECK(num(r, "zee_limit") == 8192 && num(r, "purge_days") == 30 && !flag(r, "remote_sync"));
    CHECK(!flag(r, "auto_send") && str(r, "auto_send_why_not") == "machine mode is off");
    CHECK(!str(r, "duplicates").empty());

    // ---- settings refused whole ----
    r = call(a, {{"cmd", "bbs_set"}, {"ack_to", "ALL"}});
    CHECK(!ok(r) && str(r, "error").find("ALL") != std::string::npos);
    r = call(a, {{"cmd", "bbs_set"}, {"ack_to", "ALL-CM"}});
    CHECK(!ok(r));
    r = call(a, {{"cmd", "bbs_set"}, {"machine_mode", "true"}, {"accept", "everyone"}});
    CHECK(!ok(r) && !flag(call(a, {{"cmd", "bbs_get"}}), "machine_mode"));  // nothing changed
    r = call(a, {{"cmd", "bbs_set"}, {"bogus", "1"}});
    CHECK(!ok(r));

    // ---- principle 5: a command heard while machine mode is off is never answered ----
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "VER"), &e).empty());
    CHECK(wait_for([&] { return got(a, "AAR9AA").size() == 1; }));
    settle();
    CHECK(a_answers() == 0);
    // Turning it on later doesn't answer the old one.
    r = call(a, {{"cmd", "bbs_set"}, {"machine_mode", "true"}, {"ack_delay_ms", "0"}, {"auto_per_hour", "120"}});
    CHECK(ok(r) && flag(r, "machine_mode") && flag(r, "auto_send"));
    settle();
    CHECK(a_answers() == 0);

    // ---- plain-text command with commands_ct_only on (the default): an error answer ----
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "VER "), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 1; }));
    CHECK(nth(a, "AAR9AA", 0).find("ENCRYPTED CIRCUIT") != std::string::npos);

    r = call(a, {{"cmd", "bbs_set"}, {"commands_ct_only", "false"}});
    CHECK(ok(r));

    // ---- VER ----
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "VER  "), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 2; }));
    CHECK(nth(a, "AAR9AA", 1).find(std::string("VER ") + bbs::kVersion) != std::string::npos);
    CHECK(nth(a, "AAR9AA", 1).find("DE AAM499-CM") != std::string::npos);
    CHECK(wait_for([&] { return any_has(got(b, "AAM499"), bbs::kVersion); }));

    // A command for another station's machine is left alone.
    CHECK(!b.submit("AAM499", 'R', chat("AAR1QQ-CM", "AAR9AA", "VER"), &e).empty());
    settle();
    CHECK(a_answers() == 2);

    // ---- traffic from C: one for B (via A's call), one overheard for UHWTST ----
    CHECK(!c.submit("AAM499", 'R', traffic("AAR9AA AAM499", "AAR7ZZ", 11, "FOR BRAVO"), &e).empty());
    CHECK(wait_for([&] { return got(a, "AAR7ZZ").size() == 1; }));
    // Auto QSL (plain traffic gets no ack on the air): MSC's receipt, to the
    // originator, never ALL, with the station's call and guard RIs.
    CHECK(pump_until([&] { return out_to(a, "AAR7ZZ").size() == 1; }));
    {
        std::string q = nth(a, "AAR7ZZ", 0);
        CHECK(q.find("AAR7ZZ-CM DE AAM499-CM//QSL _R_AAR7ZZ #0011 3291200/AAM499") == 0);
    }
    CHECK(!c.submit("AAM499", 'R', traffic("UHWTST", "AAR7ZZ", 12, "FOR THE WEST GATEWAY"), &e).empty());
    CHECK(wait_for([&] {
        for (auto& m : a.list(100))
            if (m.state == "overheard") return true;
        return false;
    }));
    settle();
    CHECK(out_to(a, "AAR7ZZ").size() == 1);  // overheard traffic isn't QSL'd

    // ---- QRU / ZEE / QSL / ZDK ----
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "QRU U,A"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 3; }));
    {
        std::string q = nth(a, "AAR9AA", 2);
        CHECK(q.find("QTC 1\n1 - ") != std::string::npos);
        CHECK(q.find("AAR7ZZ #0011 3291200 ---> AAR9AA AAM499") != std::string::npos);
        CHECK(q.find("UHWTST") == std::string::npos);  // accept=calls: overheard traffic isn't held
    }
    // QRU UHW: nothing until the guard list and accept say so.
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "QRU UHW"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 4; }));
    CHECK(nth(a, "AAR9AA", 3).find("\nQRU\nAR") != std::string::npos);
    r = call(a, {{"cmd", "guard_set"}, {"items", "AAM499-CM"}});
    CHECK(!ok(r));
    r = call(a, {{"cmd", "guard_set"}, {"items", "uhw, UHE;UHW"}});
    CHECK(ok(r) && len(r, "items") == 2);
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"accept", "calls_ris"}})));
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "QRU UHW"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 5; }));
    CHECK(nth(a, "AAR9AA", 4).find("QTC 1 UHW\n1 - ") != std::string::npos);
    CHECK(nth(a, "AAR9AA", 4).find("---> UHWTST") != std::string::npos);

    // ZEE 1 sends line 1 of B's last list (the UHW one).
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "ZEE 1"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 6; }));
    CHECK(nth(a, "AAR9AA", 5).find("FOR THE WEST GATEWAY") != std::string::npos);
    CHECK(wait_for([&] { return any_has(got(b, "AAM499"), "FOR THE WEST GATEWAY"); }));
    // ZEE of a line not on the list: an error, not a guess.
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "ZEE 7"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 7; }));
    CHECK(nth(a, "AAR9AA", 6).find("ERROR ZEE 7 NOT ON YOUR LIST") != std::string::npos);
    // QSL 1: marked, not answered, and not listed to B again.
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "QSL 1"), &e).empty());
    std::string west_id;
    for (auto& m : a.list(100))
        if (m.state == "overheard" && m.body.find("WEST GATEWAY") != std::string::npos) west_id = m.id;
    CHECK(pump_until([&] { return len(call(a, {{"cmd", "bbs_qsl"}, {"id", west_id}}), "by") == 1; }));
    settle();
    CHECK(a_answers() == 7);
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "QRU UHW,UHE"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 8; }));
    CHECK(nth(a, "AAR9AA", 7).find("\nQRU\nAR") != std::string::npos);
    // ZDK 1: page 1 of the last ZEE answer again.
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "ZDK 1"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 9; }));
    CHECK(nth(a, "AAR9AA", 8).find("FOR THE WEST GATEWAY") != std::string::npos);

    // Multi-message ZEE off (the default): the first only, and it says so.
    // The ZEE answer is paged to the size limit.
    CHECK(!c.submit("AAM499", 'R', traffic("AAR9AA AAM499", "AAR7ZZ", 13, std::string(700, 'X')), &e).empty());
    CHECK(wait_for([&] { return got(a, "AAR7ZZ").size() == 3; }));
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"zee_max_bytes", "300"}, {"ack", "off"}})));
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "QRU"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 10; }));
    CHECK(nth(a, "AAR9AA", 9).find("QTC 2\n") != std::string::npos);
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "ZEE 1,2"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == 11; }));
    CHECK(nth(a, "AAR9AA", 10).find("MULTI MESSAGE ZEE IS OFF") != std::string::npos);
    CHECK(nth(a, "AAR9AA", 10).find("XXXX") == std::string::npos);
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"zee_multi", "true"}})));
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "ZEE 2 [COMP]"), &e).empty());
    // 700 X's on one line can't be cut: one page holds it, the rest page.
    CHECK(pump_until([&] { return a_answers() >= 12; }));
    settle();
    {
        auto o = out_to(a, "AAR9AA");
        CHECK(o.size() >= 12 && o[11].body.find("PAGE 1 OF ") == 0);
        for (size_t i = 11; i < o.size(); ++i) CHECK(o[i].body.find("PAGE ") == 0);
    }
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"zee_max_bytes", "0"}})));

    // Unknown command, and remote sync (not allowed by default).
    size_t n0 = a_answers();
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "SYNC"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == n0 + 1; }));
    CHECK(nth(a, "AAR9AA", -1).find("REMOTE SYNC NOT ALLOWED") != std::string::npos);
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "DIR"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == n0 + 2; }));
    CHECK(nth(a, "AAR9AA", -1).find("UNKNOWN COMMAND DIR") != std::string::npos);

    // ---- principle 5: EMCON and the hourly cap ----
    a.set_emcon(true);
    r = call(a, {{"cmd", "bbs_get"}});
    CHECK(!flag(r, "auto_send") && str(r, "auto_send_why_not") == "EMCON is on");
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "VER"), &e).empty());
    CHECK(wait_for([&] {
        int n = 0;
        for (auto& m : a.list(1000))
            if (m.direction == "in" && m.body.find("// VER //") != std::string::npos) ++n;
        return n >= 3;  // VER, the one for AAR1QQ, this one
    }));
    settle();
    CHECK(a_answers() == n0 + 2);  // not queued, not answered later either
    a.set_emcon(false);
    settle();
    CHECK(a_answers() == n0 + 2);
    int used = static_cast<int>(num(call(a, {{"cmd", "bbs_get"}}), "auto_sent_last_hour"));
    CHECK(used >= 14);
    // The status strip's one airtime number counts them, with the resend cap.
    {
        JsonValue st = call(a, {{"cmd", "status"}});
        const JsonValue* cap = st.get("cap");
        CHECK(cap && num(*cap, "used") == used && num(*cap, "per_hour") == 120 + 12 && num(*cap, "resets_at") > 0);
    }
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"auto_per_hour", std::to_string(used)}})));
    r = call(a, {{"cmd", "bbs_get"}});
    CHECK(!flag(r, "auto_send") && str(r, "auto_send_why_not").find("hourly cap") != std::string::npos);
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "VER"), &e).empty());
    settle();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    settle();
    CHECK(a_answers() == n0 + 2);
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"auto_per_hour", "120"}})));
    // Answers never go to ALL, even when asked from ALL.
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"ack_to", ""}})));

    // ---- alias map ----
    CHECK(!ok(call(a, {{"cmd", "alias_set"}, {"ri", "RJMOG"}, {"members", ""}})));
    CHECK(!ok(call(a, {{"cmd", "alias_set"}, {"ri", "RJMOG"}, {"members", "RJMOG"}})));
    CHECK(ok(call(a, {{"cmd", "alias_set"}, {"ri", "rjmog"}, {"members", "AAR9AA;AAR7ZZ;aar9aa"}})));
    r = call(a, {{"cmd", "alias_list"}});
    CHECK(ok(r) && len(r, "aliases") == 1 && len(at(r, "aliases", 0), "members") == 2);
    CHECK(ok(call(a, {{"cmd", "alias_del"}, {"ri", "RJMOG"}})));
    CHECK(!ok(call(a, {{"cmd", "alias_del"}, {"ri", "RJMOG"}})));

    // ---- routing rules ----
    CHECK(!ok(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE West"}})));  // no ALE address
    CHECK(!ok(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE West"}, {"ale", "WAB"}, {"sched", "2500Z"}})));
    CHECK(!ok(call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE West"}, {"ale", "WAB"}, {"key", "QXR01"}})));
    CHECK(!ok(call(a, {{"cmd", "route_set"}, {"match", "U H W"}, {"route", "ALE West"}, {"ale", "WAB"}})));
    r = call(a, {{"cmd", "route_set"}, {"match", "uhe"}, {"route", "ALE East"}, {"ale", "EAB"}});
    CHECK(ok(r) && num(r, "id") > 0);
    int64_t east = static_cast<int64_t>(num(r, "id"));
    r = call(a, {{"cmd", "route_set"}, {"match", "UHW"}, {"route", "ALE West"}, {"ale", "wab, WAC"}, {"mark_processed", "true"}});
    CHECK(ok(r));
    int64_t west = static_cast<int64_t>(num(r, "id"));
    r = call(a, {{"cmd", "route_list"}});
    CHECK(ok(r) && len(r, "rules") == 2 && !flag(r, "routing"));
    CHECK(len(at(r, "rules", 1), "ale") == 2 && at(at(r, "rules", 1), "ale", 0).s == "WAB");
    CHECK(str(r, "auto_send_why_not") == "routing is off");
    CHECK(ok(call(a, {{"cmd", "route_move"}, {"id", std::to_string(west)}, {"dir", "up"}})));
    r = call(a, {{"cmd", "route_list"}});
    CHECK(num(at(r, "rules", 0), "id") == west && num(at(r, "rules", 0), "pos") == 1);
    CHECK(!ok(call(a, {{"cmd", "route_move"}, {"id", "999"}, {"dir", "up"}})));
    // A change keeps what it doesn't name.
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"id", std::to_string(east)}, {"route", "ALE East 2"}})));
    r = call(a, {{"cmd", "route_list"}});
    CHECK(str(at(r, "rules", 1), "route") == "ALE East 2" && str(at(r, "rules", 1), "match") == "UHE");

    // Routing off: held traffic for UHW isn't sent on. A dry re-run says what would.
    CHECK(out_to(a, "WAB").empty());
    r = call(a, {{"cmd", "route_rerun"}});
    CHECK(ok(r) && flag(r, "dry_run") && num(r, "count") == 1);
    CHECK(str(at(r, "items", 0), "msg_id") == west_id && num(at(r, "items", 0), "rule") == west);
    settle();
    CHECK(out_to(a, "WAB").empty());
    // Not dry while routing is off: recorded, and it waits for the gate.
    r = call(a, {{"cmd", "route_rerun"}, {"dry_run", "false"}});
    CHECK(ok(r) && num(r, "count") == 1 && str(at(r, "items", 0), "state") == "due");
    CHECK(str(at(r, "items", 0), "why").find("routing is off") != std::string::npos);
    settle();
    CHECK(out_to(a, "WAB").empty());
    // Routing on, but machine mode off: still nothing.
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"machine_mode", "false"}, {"routing", "true"}})));
    settle();
    CHECK(out_to(a, "WAB").empty());
    // Both on: the waiting one goes, to the first ALE address.
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"machine_mode", "true"}})));
    CHECK(pump_until([&] { return out_to(a, "WAB").size() == 1; }));
    CHECK(nth(a, "WAB", 0).find("FOR THE WEST GATEWAY") != std::string::npos);
    r = call(a, {{"cmd", "route_list"}});
    CHECK(len(r, "routed") == 1 && str(at(r, "routed", 0), "state") == "queued" &&
          flag(at(r, "routed", 0), "processed"));
    // Not routed twice.
    CHECK(num(call(a, {{"cmd", "route_rerun"}}), "count") == 0);

    // New traffic routes as it comes, broadcast RI struck off.
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"broadcast_ri", "uhwhhcf"}})));
    CHECK(!c.submit("AAM499", 'R', traffic("UHWHHCF UHEAAA", "AAR7ZZ", 14, "BROADCAST FOR EAST"), &e).empty());
    CHECK(pump_until([&] { return out_to(a, "EAB").size() == 1; }));
    {
        std::string body = nth(a, "EAB", 0);
        CHECK(body.find("RR UHEAAA DE AAR7ZZ") != std::string::npos);
        CHECK(body.find("UHWHHCF") == std::string::npos);
    }
    // A scheduled rule holds its traffic until its time.
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"id", std::to_string(east)}, {"sched", "0000Z"}})));
    int64_t fake = static_cast<int64_t>(std::time(nullptr));
    fake = fake - fake % 86400 + 3600;  // 0100Z today: 0000Z is tomorrow
    bbs_a.set_clock([&] { return fake; });
    CHECK(!c.submit("AAM499", 'R', traffic("UHEBBB", "AAR7ZZ", 15, "LATER FOR EAST"), &e).empty());
    CHECK(wait_for([&] { return got(a, "AAR7ZZ").size() == 5; }));
    settle();
    CHECK(out_to(a, "EAB").size() == 1);
    r = call(a, {{"cmd", "route_list"}});
    CHECK(str(at(r, "routed", 0), "state") == "due" &&
          num(at(r, "routed", 0), "due") == static_cast<double>(fake - 3600 + 86400));
    fake += 86400;  // the next day
    CHECK(pump_until([&] { return out_to(a, "EAB").size() == 2; }));
    bbs_a.set_clock([] { return static_cast<int64_t>(std::time(nullptr)); });

    // EMCON holds routed traffic back until it is off.
    a.set_emcon(true);
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"id", std::to_string(east)}, {"sched", ""}})));
    CHECK(!c.submit("AAM499", 'R', traffic("UHECCC", "AAR7ZZ", 16, "EAST UNDER EMCON"), &e).empty());
    CHECK(wait_for([&] { return got(a, "AAR7ZZ").size() == 6; }));
    settle();
    CHECK(out_to(a, "EAB").size() == 2);
    a.set_emcon(false);
    CHECK(pump_until([&] { return out_to(a, "EAB").size() == 3; }));

    // Removing a rule closes the gap.
    CHECK(ok(call(a, {{"cmd", "route_del"}, {"id", std::to_string(west)}})));
    r = call(a, {{"cmd", "route_list"}});
    CHECK(len(r, "rules") == 1 && num(at(r, "rules", 0), "pos") == 1);
    CHECK(!ok(call(a, {{"cmd", "route_del"}, {"id", std::to_string(west)}})));

    // Commands taken only enciphered: plain gets the error, codress is answered.
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"commands_ct_only", "true"}})));
    size_t n1 = a_answers();
    r = call(b, {{"cmd", "compose_send"},
                 {"template", "FREE_FORM"},
                 {"procedure", "ACP127_CODRESS"},
                 {"key", today},
                 {"prec", "R"},
                 {"routing", "UHAAAA"},
                 {"originator", "AAR9AA"},
                 {"from", "MARS STATION AAR9AA"},
                 {"to", "UHAAAA/AAM499"},
                 {"to_station", "AAM499"},
                 {"text", "AAM499-CM DE AAR9AA / VER / 1200Z K"}});  // the form takes no "//"
    CHECK(ok(r));
    CHECK(pump_until([&] { return a_answers() == n1 + 1; }));
    CHECK(nth(a, "AAR9AA", -1).find(std::string("VER ") + bbs::kVersion) != std::string::npos);
    CHECK(!b.submit("AAM499", 'R', chat("AAM499-CM", "AAR9AA", "VER"), &e).empty());
    CHECK(pump_until([&] { return a_answers() == n1 + 2; }));
    CHECK(nth(a, "AAR9AA", -1).find("ENCRYPTED CIRCUIT") != std::string::npos);

    // A rule with a key sends on in our own cipher.
    CHECK(!ok(call(a, {{"cmd", "route_set"}, {"id", std::to_string(east)}, {"key", "NOSUCHKEY"}})));
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"id", std::to_string(east)}, {"key", today}})));
    CHECK(!c.submit("AAM499", 'R', "VZCZC\r\nRR UHEDDD DE AAR7ZZ #0017 3291200\r\nR 251200Z NOV 2026\r\nFM AAR7ZZ\r\nTO UHEDDD\r\nBT\r\nUNCLAS\r\nEAST IN OUR CIPHER\r\nBT\r\nNNNN\r\n", &e).empty());
    CHECK(pump_until([&] { return out_to(a, "EAB").size() == 4; }));
    CHECK(codress::is_codress(nth(a, "EAB", 3)) && nth(a, "EAB", 3).find("EAST IN OUR CIPHER") == std::string::npos);
    {
        auto cl = a.clear(out_to(a, "EAB")[3].id);
        CHECK(cl && cl->first.find("EAST IN OUR CIPHER") != std::string::npos && cl->second == today);
    }

    // Settings survive a restart of the engine.
    bbs::stop(a);
    r = call(a, {{"cmd", "bbs_get"}});
    CHECK(flag(r, "machine_mode") && flag(r, "routing") && str(r, "broadcast_ri") == "UHWHHCF" &&
          str(r, "accept") == "calls_ris");
    CHECK(len(call(a, {{"cmd", "guard_list"}}), "items") == 2);

    bbs::stop(a);
    a.stop();
    b.stop();
    c.stop();
    fs::remove_all(dir);
    return check::report("test_bbs");
}
