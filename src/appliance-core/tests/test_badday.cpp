// BAD_DAY: members rally from stale material alone. A normal day sets up a
// net with handouts and backups; 45 days later every box is wiped. Stations
// come back from their backups, or from nothing but an old handout, and a
// keymaster (or, with the keymaster lost, the named alternate) issues a new
// tape. Calls and keys are made up; every key here is thrown away.
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "../src/station.h"
#include "check.h"

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
static std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static double num(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::Number ? x->n : -1;
}

// The drill's clock: every box's keyring reads it.
static int64_t g_now = 0;

struct Box {
    std::unique_ptr<Core> core;
    std::shared_ptr<keys::Keyring> keys;
    std::string fp;
};

static Box make_box(const fs::path& dir, const std::string& call, std::unique_ptr<ModemDriver> modem) {
    Box b;
    fs::create_directories(dir);
    std::string base = (dir / call).string();
    b.core = std::make_unique<Core>(CoreConfig{call, base + ".db", 3, 1, true}, std::move(modem));
    b.keys = std::make_shared<keys::Keyring>(base + ".keys");
    b.keys->set_clock([] { return g_now; });
    std::string err;
    b.keys->load(&err);
    b.core->set_keyring(b.keys);
    auto id = std::make_shared<station::Identity>();
    CHECK(station::make_identity(call, g_now, *id, &err));
    CHECK(station::save_identity(base + ".id", *id, &err));
    b.fp = station::fingerprint(station::card_of(*id));
    b.core->set_identity(id, base + ".id");
    b.core->start();
    return b;
}

static std::string card_of(Box& b) { return str(call(*b.core, {{"cmd", "station_card"}}), "card"); }

static std::string handout_for(const JsonValue& r, const std::string& station) {
    const JsonValue* hs = r.get("handouts");
    if (hs)
        for (auto& h : hs->arr)
            if (str(h, "station") == station) return str(h, "text");
    return "";
}

static std::string day_of(int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y%m%d", &tm);
    return buf;
}

static const char* kPass = "practice net backup";

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_badday_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    const int64_t drill = static_cast<int64_t>(std::time(nullptr));
    const int64_t t0 = drill - 45 * 86400;

    // ---- Normal day, 45 days before the drill. ----
    g_now = t0;
    std::string hand_b, hand_c, hand_k, back_a, back_b, back_k, old_c_fp;
    {
        auto ma = std::make_unique<LoopbackModem>(0);
        auto mb = std::make_unique<LoopbackModem>(0);
        LoopbackModem::connect(*ma, *mb);
        Box A = make_box(dir / "day", "NCS-A", std::move(ma));
        Box B = make_box(dir / "day", "STA-B", std::move(mb));
        Box C = make_box(dir / "day", "STA-C", std::make_unique<LoopbackModem>(0));
        Box K = make_box(dir / "day", "ALT-K", std::make_unique<LoopbackModem>(0));
        old_c_fp = C.fp;

        // Members' cards to the keymaster by stick, each read back by voice.
        for (Box* m : {&B, &C, &K}) {
            CHECK(ok(call(*A.core, {{"cmd", "card_import"}, {"text", card_of(*m)}, {"source", "USB stick"}})));
            CHECK(ok(call(*A.core,
                          {{"cmd", "card_check"}, {"station", m->core->station()}, {"fingerprint", m->fp}})));
        }
        // An unchecked card can't be named keymaster.
        CHECK(ok(call(*B.core, {{"cmd", "card_import"}, {"text", card_of(K)}})));
        JsonValue r = call(*B.core, {{"cmd", "card_keymaster"}, {"station", "ALT-K"}});
        CHECK(!ok(r) && str(r, "error").find("voice") != std::string::npos);

        // The keymaster issues the tape and a handout per member; ALT-K is the alternate.
        r = call(*A.core, {{"cmd", "net_tape"},
                           {"net", "Practice Net"},
                           {"days", "31"},
                           {"start", day_of(t0)},
                           {"members", "STA-B STA-C"},
                           {"keymasters", "ALT-K"},
                           {"handouts", "Y"}});
        CHECK(ok(r) && r.get("handouts") && r.get("handouts")->arr.size() == 3);
        hand_b = handout_for(r, "STA-B");
        hand_c = handout_for(r, "STA-C");
        hand_k = handout_for(r, "ALT-K");
        CHECK(!hand_b.empty() && !hand_c.empty() && !hand_k.empty());
        // No secret in the clear: the tape is only there sealed.
        for (auto* h : {&hand_b, &hand_c, &hand_k}) {
            CHECK(h->find("PNA MEMBER HANDOUT 1") == 0);
            CHECK(h->find("PNA KEY TAPE") == std::string::npos);
            CHECK(h->find("KEYMASTER NCS-A " + A.fp) != std::string::npos);
            CHECK(h->find("KEYMASTER ALT-K " + K.fp) != std::string::npos);
            // The block leadership's passphrase list uses, with no passphrase in it.
            CHECK(h->find("\nKEY_CKR:") != std::string::npos && h->find("\nKEYNAME:") != std::string::npos &&
                  h->find("\nKEYPASS:NONE.") != std::string::npos);
            CHECK(h->find("\n" + std::string(43, '=') + "\n") != std::string::npos);
        }
        // The alternate's handout carries every member's card; a member's doesn't.
        CHECK(hand_k.find("MEMBER STA-C " + C.fp) != std::string::npos);
        CHECK(std::count(hand_k.begin(), hand_k.end(), '\n') > std::count(hand_b.begin(), hand_b.end(), '\n'));

        // A handout is for one station only, and a changed one is refused.
        r = call(*C.core, {{"cmd", "handout_import"}, {"text", hand_b}});
        CHECK(!ok(r) && str(r, "error").find("STA-B") != std::string::npos);
        std::string bent = hand_c;
        bent.replace(bent.find("PRACTICE NET"), 12, "PRACTICE NUT");
        CHECK(!ok(call(*C.core, {{"cmd", "handout_import"}, {"text", bent}})));

        // Each member loads its handout: keymasters named, tape opened.
        for (auto [box, text] : {std::pair<Box*, std::string*>{&B, &hand_b}, {&C, &hand_c}, {&K, &hand_k}}) {
            r = call(*box->core, {{"cmd", "handout_import"}, {"text", *text}});
            CHECK(ok(r) && yes(r, "tape_opened") && num(r, "added") == 31 && !yes(r, "stale"));
            r = call(*box->core, {{"cmd", "rally_status"}});
            CHECK(ok(r) && str(r, "next").find("Ready") == 0);
        }
        r = call(*B.core, {{"cmd", "cards_list"}});
        int km = 0;
        for (auto& c : r.get("cards")->arr) km += yes(c, "keymaster") ? 1 : 0;
        CHECK(km == 2);
        // The checks match: B reads its first key's check to the keymaster.
        CHECK(B.keys->for_sending().at(0).check == A.keys->for_sending().at(0).check);

        // Backups, with the cards each holds. STA-C never makes one.
        r = call(*A.core, {{"cmd", "station_backup"}, {"passphrase", kPass}});
        CHECK(ok(r) && num(r, "cards") == 3);
        back_a = str(r, "backup");
        back_b = str(call(*B.core, {{"cmd", "station_backup"}, {"passphrase", kPass}}), "backup");
        back_k = str(call(*K.core, {{"cmd", "station_backup"}, {"passphrase", kPass}}), "backup");
        CHECK(back_b.find("PNA STATION BACKUP 2") != std::string::npos);
        CHECK(back_b.find("PNA KEY TAPE") == std::string::npos);
        for (Box* b : {&A, &B, &C, &K}) b->core->stop();
    }
    fs::remove_all(dir / "day");  // every box wiped

    // ---- Drill 1, 45 days on: the old tape has ended. ----
    g_now = drill;
    {
        auto ma = std::make_unique<LoopbackModem>(0);
        auto mb = std::make_unique<LoopbackModem>(0);
        LoopbackModem::connect(*ma, *mb);
        Box A = make_box(dir / "drill1", "NCS-A", std::move(ma));
        Box B = make_box(dir / "drill1", "STA-B", std::move(mb));
        Box C = make_box(dir / "drill1", "STA-C", std::make_unique<LoopbackModem>(0));

        // A and B come back from their backups, cards and keymaster marks included.
        JsonValue r = call(*A.core, {{"cmd", "station_restore"}, {"text", back_a}, {"passphrase", kPass}});
        CHECK(ok(r) && num(r, "cards") == 3 && num(r, "age_days") == 45);
        r = call(*B.core, {{"cmd", "station_restore"}, {"text", back_b}, {"passphrase", kPass}});
        CHECK(ok(r) && num(r, "cards") >= 2);
        r = call(*B.core, {{"cmd", "rally_status"}});
        CHECK(ok(r) && str(r, "next").find("No key for now") == 0);

        // C has only its stale handout: the keymasters come back, the tape doesn't.
        r = call(*C.core, {{"cmd", "rally_status"}});
        CHECK(ok(r) && str(r, "next").find("Load your handout") == 0);
        r = call(*C.core, {{"cmd", "handout_import"}, {"text", hand_c}});
        CHECK(ok(r) && yes(r, "stale") && !yes(r, "tape_opened") &&
              str(r, "next").find("older key") != std::string::npos);
        r = call(*C.core, {{"cmd", "rally_status"}});
        CHECK(ok(r) && str(r, "next").find("No key for now") == 0);
        CHECK(C.keys->for_sending().empty());

        // Signed plain traffic needs no tape: B's card goes over the air and checks good.
        r = call(*B.core, {{"cmd", "card_send"}, {"to", "NCS-A"}});
        CHECK(ok(r));
        std::string bid = str(r, "id");
        CHECK(wait_for([&] { return A.core->get(bid).has_value(); }));
        r = call(*A.core, {{"cmd", "parse_traffic"}, {"id", bid}});
        CHECK(ok(r) && str(r, "signature") == "good" && yes(r, "signer_checked"));

        // C's new card: refused until the keymaster hears it by voice and replaces it.
        CHECK(C.fp != old_c_fp);
        r = call(*A.core, {{"cmd", "card_import"}, {"text", card_of(C)}});
        CHECK(!ok(r));
        CHECK(ok(call(*A.core, {{"cmd", "card_import"}, {"text", card_of(C)}, {"replace", "Y"}})));
        CHECK(ok(call(*A.core, {{"cmd", "card_check"}, {"station", "STA-C"}, {"fingerprint", C.fp}})));

        // A new tape: B opens its copy from the air, C loads its new handout.
        r = call(*A.core, {{"cmd", "net_tape"},
                           {"net", "Practice Net"},
                           {"days", "7"},
                           {"members", "STA-B STA-C"},
                           {"send", "Y"},
                           {"handouts", "Y"}});
        CHECK(ok(r));
        std::string b_id;
        for (auto& s : r.get("sealed")->arr)
            if (str(s, "station") == "STA-B") b_id = str(s, "id");
        std::string new_c = handout_for(r, "STA-C");
        CHECK(wait_for([&] { return !b_id.empty() && B.core->get(b_id).has_value(); }));
        r = call(*B.core, {{"cmd", "key_open"}, {"id", b_id}});
        CHECK(ok(r) && num(r, "added") == 7 && str(r, "from") == "NCS-A");
        r = call(*C.core, {{"cmd", "handout_import"}, {"text", new_c}});
        CHECK(ok(r) && yes(r, "tape_opened") && num(r, "added") == 7);
        r = call(*C.core, {{"cmd", "rally_status"}});
        CHECK(ok(r) && str(r, "next").find("Ready") == 0);
        std::string today = B.keys->for_sending().at(0).indicator;
        CHECK(C.keys->for_sending().at(0).indicator == today);

        // Codress works again, A to B.
        r = call(*A.core, {{"cmd", "compose_send"},
                           {"template", "FREE_FORM"},
                           {"procedure", "ACP127_CODRESS"},
                           {"key", today},
                           {"prec", "P"},
                           {"routing", "UHAAAA"},
                           {"originator", "NCS-A"},
                           {"from", "MARS STATION NCS-A"},
                           {"to", "UHAAAA/STA-B"},
                           {"to_station", "STA-B"},
                           {"text", "NET IS BACK UP"}});
        CHECK(ok(r) && yes(r, "signed"));
        std::string mid = str(r, "id");
        CHECK(wait_for([&] { return B.core->get(mid).has_value(); }));
        r = call(*B.core, {{"cmd", "parse_traffic"}, {"id", mid}});
        CHECK(ok(r) && str(r, "signature") == "good");
        for (Box* b : {&A, &B, &C}) b->core->stop();
    }

    // ---- Drill 2: NCS-A is lost. The alternate takes over. ----
    {
        Box K = make_box(dir / "drill2", "ALT-K", std::make_unique<LoopbackModem>(0));
        Box B = make_box(dir / "drill2", "STA-B", std::make_unique<LoopbackModem>(0));
        Box C = make_box(dir / "drill2", "STA-C", std::make_unique<LoopbackModem>(0));
        JsonValue r = call(*K.core, {{"cmd", "station_restore"}, {"text", back_k}, {"passphrase", kPass}});
        CHECK(ok(r) && num(r, "cards") >= 3);
        // K's own stale handout opens, but every key on it has ended.
        r = call(*K.core, {{"cmd", "handout_import"}, {"text", hand_k}});
        CHECK(ok(r) && yes(r, "tape_opened") && num(r, "added") == 0 && num(r, "expired") == 31 &&
              str(r, "next").find("ended") != std::string::npos);
        CHECK(K.keys->for_sending().empty());

        CHECK(ok(call(*B.core, {{"cmd", "station_restore"}, {"text", back_b}, {"passphrase", kPass}})));
        CHECK(ok(call(*C.core, {{"cmd", "handout_import"}, {"text", hand_c}})));
        // C rebuilt again with new keys: the alternate hears its card by voice.
        CHECK(ok(call(*K.core, {{"cmd", "card_import"}, {"text", card_of(C)}, {"replace", "Y"}})));
        CHECK(ok(call(*K.core, {{"cmd", "card_check"}, {"station", "STA-C"}, {"fingerprint", C.fp}})));

        r = call(*K.core, {{"cmd", "net_tape"},
                           {"net", "Practice Net"},
                           {"days", "7"},
                           {"members", "STA-B STA-C"},
                           {"handouts", "Y"}});
        CHECK(ok(r) && r.get("no_card")->arr.empty());
        r = call(*B.core, {{"cmd", "handout_import"}, {"text", handout_for(r, "STA-B")}});
        CHECK(ok(r) && yes(r, "tape_opened") && num(r, "added") == 7 && str(r, "by") == "ALT-K");
        r = call(*K.core, {{"cmd", "net_tape"},
                           {"net", "Practice Net"},
                           {"days", "7"},
                           {"members", "STA-C"},
                           {"handouts", "Y"}});
        r = call(*C.core, {{"cmd", "handout_import"}, {"text", handout_for(r, "STA-C")}});
        CHECK(ok(r) && yes(r, "tape_opened") && num(r, "added") == 7);

        // A station that never named ALT-K keymaster won't take its tape.
        Box D = make_box(dir / "drill2", "STA-D", std::make_unique<LoopbackModem>(0));
        CHECK(ok(call(*K.core, {{"cmd", "card_import"}, {"text", card_of(D)}})));
        CHECK(ok(call(*K.core, {{"cmd", "card_check"}, {"station", "STA-D"}, {"fingerprint", D.fp}})));
        CHECK(ok(call(*D.core, {{"cmd", "card_import"}, {"text", card_of(K)}})));
        r = call(*K.core, {{"cmd", "net_tape"}, {"net", "Practice Net"}, {"days", "7"}, {"members", "STA-D"}});
        std::string d_text;
        for (auto& s : r.get("sealed")->arr) d_text = str(s, "text");
        r = call(*D.core, {{"cmd", "key_open"}, {"text", d_text}});
        CHECK(!ok(r) && str(r, "error").find("keymaster") != std::string::npos);
        for (Box* b : {&K, &B, &C, &D}) b->core->stop();
    }
    fs::remove_all(dir);
    return check::report("test_badday");
}
