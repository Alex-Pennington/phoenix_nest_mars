// Milestone 6, part 2: codress. Encipher and decipher on their own (tampered,
// wrong key, changed DTG, lost groups), then two stations on the loopback
// modem that each import the same key tape through the API, send codress
// both ways and read it, and what happens with no key and after expiry.
// Calls and keys here are made up; every key is thrown away.
#include "../src/api_server.h"
#include "../src/codress.h"
#include "../src/core.h"
#include "../src/crypto.h"
#include "../src/digest.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/received.h"
#include "check.h"

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
static bool yes(const JsonValue& v, const char* k) {
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

static std::string plaindress() {
    acp127::Message m;
    m.channel = "MMM";
    m.prec = 'P';
    m.routing = {"UHAAAA"};
    m.mars_fl3 = true;
    m.originator_ri = "NCS-A";
    m.station_serial = "209";
    m.filing_yday = 327;
    m.dtg = acp127::Dtg{23, 10, 40, 'Z', 11, 2026};
    m.from.plain = "MARS STATION NCS-A";
    m.to = {acp127::Address{"UHAAAA", "STA-B"}};
    m.info = {acp127::Address{"", "STA-C"}};
    m.text = "\nEXER/PRACTICE//\nTHIS IS A TEST OF CODRESS\nLINE THREE";
    m.limit_lines = false;
    std::string err;
    return digest::append(acp127::build(m, &err));
}

static void unit() {
    std::string key = crypto::random_bytes(32);
    std::string p = plaindress();
    CHECK(!p.empty() && digest::check(p) == digest::Check::Pass);
    CHECK(!codress::is_codress(p));
    std::string c, err;
    CHECK(codress::encipher(p, "QXR01", key, c, &err));
    CHECK(codress::is_codress(c) && codress::indicator_of(c) == "QXR01");
    // The addresses and the text are not on the air; the header is.
    CHECK(c.find("STA-B") == std::string::npos && c.find("CODRESS") == std::string::npos);
    CHECK(c.find("VZCZCMMM") == 0 && c.find("P 231040Z NOV 2026 QXR01") != std::string::npos);
    CHECK(c.find("ZNR UUUUU") != std::string::npos);
    CHECK(digest::check(c) == digest::Check::Pass);
    // A fresh nonce every time: the same message never looks the same twice.
    std::string c2;
    CHECK(codress::encipher(p, "QXR01", key, c2, &err) && c2 != c);
    // The reader in received.cpp sees it as codress, as MARS codress reads.
    received::Message rm;
    CHECK(received::parse(c, rm, &err));
    CHECK(rm.enciphered && rm.key_indicator == "QXR01" && rm.group_count > 0);
    // Groups: 8 characters, A-Z and 0-9, 7 a line.
    size_t bt = c.find("BT\r\r\n") + 5;
    std::string first_line = c.substr(bt, c.find('\r', bt) - bt);
    CHECK(first_line.size() == 7 * 8 + 6 || first_line.size() % 9 == 8);
    for (char ch : first_line) CHECK((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ');

    // Back again, exactly.
    std::string back;
    CHECK(codress::decipher(c, key, back, &err));
    CHECK(back == p);
    CHECK(digest::check(back) == digest::Check::Pass);

    // One group character changed: refused (the outer digest also fails).
    std::string bad = c;
    size_t at = bt + 3;
    bad[at] = bad[at] == 'A' ? 'B' : 'A';
    CHECK(!codress::decipher(bad, key, back, &err) && back.empty());
    CHECK(err.find("do not check") != std::string::npos);
    CHECK(digest::check(bad) == digest::Check::Fail);
    // The wrong key: refused, never shown as garbage.
    CHECK(!codress::decipher(c, crypto::random_bytes(32), back, &err));
    // A changed date-time group or key indicator: refused.
    std::string dtg = c;
    dtg.replace(dtg.find("231040Z"), 7, "231041Z");
    CHECK(!codress::decipher(dtg, key, back, &err));
    std::string ind = c;
    ind.replace(ind.find("QXR01"), 5, "QXR02");
    CHECK(!codress::decipher(ind, key, back, &err));
    // A lost group: the count says so.
    std::string lost = c;
    lost.erase(bt, 9);
    CHECK(!codress::decipher(lost, key, back, &err) && err.find("groups came") != std::string::npos);
    // A garbled character that isn't A-Z or 0-9.
    std::string junk = c;
    junk[bt + 2] = '#';
    CHECK(!codress::decipher(junk, key, back, &err) && err.find("garbled") != std::string::npos);
    // Plain line ends survive too (a copy pasted through another program).
    std::string lf;
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i] != '\r') lf += c[i];
    CHECK(codress::decipher(lf, key, back, &err));
    // Bad inputs.
    CHECK(!codress::encipher(p, "QX01", key, c2, &err));
    CHECK(!codress::encipher(p, "QXR01", "short", c2, &err));
    CHECK(!codress::encipher(c, "QXR01", key, c2, &err));  // already codress
}

static JsonValue send_codress(Core& c, const std::string& key, const std::string& to, const std::string& text) {
    return call(c, {{"cmd", "compose_send"},
                    {"template", "FREE_FORM"},
                    {"procedure", "ACP127_CODRESS"},
                    {"key", key},
                    {"prec", "P"},
                    {"routing", "UHAAAA"},
                    {"originator", c.station()},
                    {"from", "MARS STATION " + c.station()},
                    {"to", "UHAAAA/" + to},
                    {"to_station", to},
                    {"text", text}});
}

static void stations(const fs::path& dir) {
    // A keymaster's tape: a key a day from yesterday.
    std::atomic<int64_t> t{static_cast<int64_t>(std::time(nullptr))};
    int64_t day0 = t.load() - t.load() % 3600 - 86400;  // key 01 ended within the last hour
    keys::Tape tape;
    std::string err;
    CHECK(keys::make_tape("PRACTICE NET", day0, 3, 24, t.load(), tape, &err));
    const std::string text = keys::format_tape(tape);
    const std::string today = tape.tape + "02";

    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    std::string adb = (dir / "NCS-A.db").string(), bdb = (dir / "STA-B.db").string();
    Core a(CoreConfig{"NCS-A", adb, 3, 1, true}, std::move(ma));
    Core b(CoreConfig{"STA-B", bdb, 3, 1, true}, std::move(mb));
    auto ka = std::make_shared<keys::Keyring>((dir / "NCS-A.keys").string());
    auto kb = std::make_shared<keys::Keyring>((dir / "STA-B.keys").string());
    auto clock = [&] { return t.load(); };
    ka->set_clock(clock);
    kb->set_clock(clock);
    CHECK(ka->load(&err) && kb->load(&err));
    a.set_keyring(ka);
    b.set_keyring(kb);
    a.start();
    b.start();

    // No key yet: codress is shown but can't be sent, and says why.
    JsonValue r = call(a, {{"cmd", "compose_templates"}});
    bool found = false;
    for (auto& p : r.get("procedures")->arr)
        if (str(p, "id") == "ACP127_CODRESS") {
            found = true;
            CHECK(!yes(p, "available") && str(p, "why_not").find("key") != std::string::npos);
        }
    CHECK(found);
    r = send_codress(a, today, "STA-B", "NOT YET");
    CHECK(!ok(r));

    // Each station imports the tape from its own copy (its USB stick).
    r = call(a, {{"cmd", "key_import"}, {"text", text}});
    CHECK(ok(r) && num(r, "added") == 3 && str(r, "net") == "PRACTICE NET");
    r = call(b, {{"cmd", "key_import"}, {"text", text}});
    CHECK(ok(r) && num(r, "added") == 3);
    r = call(a, {{"cmd", "keys_list"}});
    CHECK(ok(r) && r.get("keys")->arr.size() == 3);
    // The list never carries key bytes.
    CHECK(ApiServer::handle(a, "{\"cmd\":\"keys_list\"}").find(crypto::to_hex(tape.keys[1].secret)) == std::string::npos);
    // A damaged tape is refused whole.
    std::string dam = text;
    dam[dam.size() / 2] = dam[dam.size() / 2] == 'a' ? 'b' : 'a';
    CHECK(!ok(call(b, {{"cmd", "key_import"}, {"text", dam}})));

    // The form offers today's key.
    r = call(a, {{"cmd", "compose_form"}, {"template", "FREE_FORM"}, {"procedure", "ACP127_CODRESS"}});
    CHECK(ok(r));
    bool key_field = false;
    for (auto& f : r.get("fields")->arr)
        if (str(f, "key") == "key") key_field = str(f, "default") == today;
    CHECK(key_field);
    r = call(a, {{"cmd", "compose_check"},
                 {"template", "FREE_FORM"},
                 {"procedure", "ACP127_CODRESS"},
                 {"key", today},
                 {"routing", "UHAAAA"},
                 {"to", "UHAAAA/STA-B"},
                 {"to_station", "STA-B"},
                 {"text", "CHECK ONLY"}});
    CHECK(ok(r) && yes(r, "ready") && str(r, "codress_key") == today && !str(r, "note").empty());
    // Yesterday's key reads but doesn't send.
    CHECK(!ok(send_codress(a, tape.tape + "01", "STA-B", "OLD KEY")));

    // A to B, enciphered.
    r = send_codress(a, today, "STA-B", "MEET ON THE ALTERNATE AT 1900Z");
    CHECK(ok(r));
    std::string id = str(r, "id");
    std::string onair = str(r, "message");
    CHECK(codress::indicator_of(onair) == today && onair.find("ALTERNATE") == std::string::npos);
    CHECK(str(r, "clear").find("ALTERNATE") != std::string::npos);
    CHECK(wait_for([&] { return b.get(id).has_value(); }));
    auto got = b.get(id);
    CHECK(got && got->body == onair);  // stored as it came over the air
    CHECK(wait_for([&] { return a.get(id) && a.get(id)->state == "acked"; }));
    r = call(b, {{"cmd", "parse_traffic"}, {"id", id}});
    CHECK(ok(r) && yes(r, "codress") && yes(r, "deciphered"));
    CHECK(str(r, "codress_key") == today);
    CHECK(str(r, "text").find("MEET ON THE ALTERNATE AT 1900Z") != std::string::npos);
    CHECK(str(r, "to").find("STA-B") != std::string::npos);
    CHECK(str(r, "digest") == "pass" && str(r, "inner_digest") == "pass");
    CHECK(!yes(r, "possibly_garbled"));
    // A's own copy reads back too.
    r = call(a, {{"cmd", "parse_traffic"}, {"id", id}});
    CHECK(ok(r) && yes(r, "deciphered") && str(r, "text").find("ALTERNATE") != std::string::npos);

    // B answers, enciphered, the other way.
    r = send_codress(b, today, "NCS-A", "ROGER");
    CHECK(ok(r));
    std::string id2 = str(r, "id");
    CHECK(wait_for([&] { return a.get(id2).has_value(); }));
    r = call(a, {{"cmd", "parse_traffic"}, {"id", id2}});
    CHECK(ok(r) && yes(r, "deciphered") && str(r, "text").find("ROGER") != std::string::npos);

    // Plaindress traffic now carries a digest that the far end checks.
    r = call(a, {{"cmd", "compose_send"},
                 {"template", "FREE_FORM"},
                 {"procedure", "ACP127_PLAINDRESS"},
                 {"prec", "R"},
                 {"routing", "UHAAAA"},
                 {"originator", "NCS-A"},
                 {"from", "MARS STATION NCS-A"},
                 {"to", "UHAAAA/STA-B"},
                 {"to_station", "STA-B"},
                 {"text", "PLAIN WITH A DIGEST"}});
    CHECK(ok(r) && str(r, "message").find("[DIGEST:") != std::string::npos);
    std::string id3 = str(r, "id");
    CHECK(wait_for([&] { return b.get(id3).has_value(); }));
    r = call(b, {{"cmd", "parse_traffic"}, {"id", id3}});
    CHECK(ok(r) && str(r, "digest") == "pass" && !yes(r, "codress"));

    // B zeroizes the tape: new codress can't be read there, and the log says so.
    r = call(b, {{"cmd", "key_zeroize"}, {"which", tape.tape}});
    CHECK(ok(r) && num(r, "zeroized") == 3);
    r = send_codress(a, today, "STA-B", "AFTER ZEROIZE");
    CHECK(ok(r));
    std::string id4 = str(r, "id");
    CHECK(wait_for([&] { return b.get(id4).has_value(); }));
    r = call(b, {{"cmd", "parse_traffic"}, {"id", id4}});
    CHECK(ok(r) && yes(r, "codress") && !yes(r, "deciphered"));
    CHECK(str(r, "why_not_deciphered").find("no key") != std::string::npos);
    CHECK(str(r, "text").find("AFTER ZEROIZE") == std::string::npos);
    bool logged = false;
    for (auto& e : b.events(0, 1000)) logged = logged || e.text.find("holds no such key") != std::string::npos;
    CHECK(logged);
    // What B read earlier stays readable: the plain copy outlives the key.
    r = call(b, {{"cmd", "parse_traffic"}, {"id", id}});
    CHECK(ok(r) && yes(r, "deciphered"));

    // Expiry at A: the day passes, key 02 stops sending, key 03 takes over,
    // and past the hold the old keys are zeroized by the core itself.
    t = day0 + 3 * 86400 - 3600;  // last hour of key 03
    CHECK(!ok(send_codress(a, today, "STA-B", "STALE")));
    r = call(a, {{"cmd", "compose_form"}, {"template", "FREE_FORM"}, {"procedure", "ACP127_CODRESS"}});
    CHECK(ok(r));
    a.tick();
    CHECK(ka->list().size() == 1);  // 01 and 02 past their hold
    t = day0 + 3 * 86400 + keys::kReceiveHold;
    a.tick();
    CHECK(ka->list().empty());
    r = call(a, {{"cmd", "compose_form"}, {"template", "FREE_FORM"}, {"procedure", "ACP127_CODRESS"}});
    CHECK(!ok(r));
    bool zlog = false;
    for (auto& e : a.events(0, 1000)) zlog = zlog || e.text.find("zeroized") != std::string::npos;
    CHECK(zlog);

    // A digest that fails on a stored message: marked possibly garbled, with
    // a repeat request ready; the message itself still reads.
    a.stop();
    b.stop();
}

static void garbled(const fs::path& dir) {
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    Core a(CoreConfig{"NCS-A", (dir / "g-a.db").string(), 3, 1, true}, std::move(ma));
    Core b(CoreConfig{"STA-B", (dir / "g-b.db").string(), 3, 1, true}, std::move(mb));
    a.start();
    b.start();
    std::string p = plaindress();
    p.replace(p.find("LINE THREE"), 10, "LINE THREX");  // changed after the digest was made
    std::string err, id = a.submit("STA-B", 'P', p, &err);
    CHECK(!id.empty());
    CHECK(wait_for([&] { return b.get(id).has_value(); }));
    JsonValue r = call(b, {{"cmd", "parse_traffic"}, {"id", id}});
    CHECK(ok(r) && str(r, "digest") == "fail" && yes(r, "possibly_garbled"));
    CHECK(str(r, "text").find("LINE THREX") != std::string::npos);
    const JsonValue* rq = r.get("repeat_request");
    CHECK(rq && str(*rq, "template") == "SVC" && str(*rq, "to_station") == "NCS-A");
    CHECK(rq && str(*rq, "refs") == "#0209 231040Z NOV 2026");
    CHECK(rq && str(*rq, "svc_text").find("REPEAT") != std::string::npos);
    // The repeat request composes as a service message.
    if (rq) {
        r = call(b, {{"cmd", "compose_check"},
                     {"template", "SVC"},
                     {"routing", "NCS-A"},
                     {"to", "NCS-A"},
                     {"to_station", str(*rq, "to_station")},
                     {"refs", str(*rq, "refs")},
                     {"svc_text", str(*rq, "svc_text")}});
        CHECK(ok(r) && yes(r, "ready"));
    }
    a.stop();
    b.stop();
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_codress_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    unit();
    stations(dir);
    garbled(dir);
    fs::remove_all(dir);
    return check::report("test_codress");
}
