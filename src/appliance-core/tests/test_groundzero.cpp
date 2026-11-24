// Ground zero: getting a net back on the air from what is in the can.
//
// MARS members keep a sealed can with a computer and this appliance, and it
// is probably never updated once sealed. Two drills here:
//
// 1. Three empty boxes. One builds a startup bundle from its importer files
//    (roster, frequency card, net schedule), and hands it out over the air
//    and on a stick. Each member chooses whether to load it. Loading changes
//    no card, key or keymaster. Then a handout: each member chooses whether
//    to accept the keymaster; a refused keymaster's tape isn't taken.
// 2. A box sealed four years ago, every tape long ended, its old keymaster
//    gone. It rallies with only what's on it: signed plain traffic first,
//    a new keymaster the member chooses, a new tape, and its old bundle
//    re-signed and handed to the new keymaster.
//
// The clock is injected: nothing waits. Calls, keys and every file here are
// made up. The frequencies are public time-signal ones (2.5, 5 and 10 MHz).
#include "../src/api_server.h"
#include "../src/bundle.h"
#include "../src/channels.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/station.h"
#include "check.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <unistd.h>

using namespace pna;

// A frequency left in text shows as a number of its own (5000, 5000.0,
// 5000000). Digits inside a key, signature or sealed block don't count.
static bool leaks(const std::string& text, const std::string& needle) {
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        if (at == 0) return true;
        unsigned char before = static_cast<unsigned char>(text[at - 1]);
        if (!std::isalnum(before) && before != '+' && before != '/') return true;
    }
    return false;
}

namespace fs = std::filesystem;

static bool wait_for(const std::function<bool()>& cond, int ms = 8000) {
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
        if (c == '\r') continue;
        o += c;
    }
    return o;
}

static JsonValue call(Core& c, std::initializer_list<std::pair<std::string, std::string>> kv, std::string* raw = nullptr) {
    std::string line = "{";
    bool first = true;
    for (auto& [k, v] : kv) {
        if (!first) line += ",";
        first = false;
        line += "\"" + k + "\":\"" + esc(v) + "\"";
    }
    line += "}";
    std::string reply = ApiServer::handle(c, line);
    if (raw) *raw = reply;
    if (std::getenv("GZ_DEBUG")) std::cerr << line.substr(0, 200) << "\n  -> " << (reply.size() > 900 ? reply.substr(0, 450) + " ... " + reply.substr(reply.size() - 450) : reply) << "\n";
    JsonValue out;
    parse_json(reply, out);
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
static size_t count(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->is_array() ? x->arr.size() : 0;
}
static const JsonValue& item(const JsonValue& v, const char* k, size_t i) {
    static JsonValue none;
    const JsonValue* x = v.get(k);
    return x && x->is_array() && i < x->arr.size() ? x->arr[i] : none;
}
static std::vector<std::string> ids(const JsonValue& r, const std::string& station) {
    std::vector<std::string> out;
    const JsonValue* s = r.get("sent");
    if (s && s->is_array())
        for (auto& e : s->arr)
            if (str(e, "station") == station && e.get("ids"))
                for (auto& i : e.get("ids")->arr) out.push_back(i.s);
    return out;
}

// Every box reads this clock: its keyring and its book.
static int64_t g_now = 0;

struct Box {
    std::unique_ptr<Core> core;
    std::shared_ptr<keys::Keyring> keys;
    std::shared_ptr<Ops> ops;
    std::string fp;
};

// A box in dir. A box that was there before (taken out of the can) keeps
// its station keys, keyring, cards, book and plan.
static Box open_box(const fs::path& dir, const std::string& call, std::unique_ptr<ModemDriver> modem) {
    Box b;
    fs::create_directories(dir);
    std::string base = (dir / call).string();
    b.core = std::make_unique<Core>(CoreConfig{call, base + ".db", 3, 1, true}, std::move(modem));
    b.keys = std::make_shared<keys::Keyring>(base + ".keys");
    b.keys->set_clock([] { return g_now; });
    std::string err;
    b.keys->load(&err);
    b.core->set_keyring(b.keys);
    b.ops = std::make_shared<Ops>(call, base + ".db");
    b.ops->set_clock([] { return g_now; });
    b.core->set_ops(b.ops);
    auto id = std::make_shared<station::Identity>();
    bool exists = false;
    if (!station::load_identity(base + ".id", *id, &exists, &err) || !exists) {
        CHECK(station::make_identity(call, g_now, *id, &err));
        CHECK(station::save_identity(base + ".id", *id, &err));
    }
    b.fp = station::fingerprint(station::card_of(*id));
    b.core->set_identity(id, base + ".id");
    b.core->start();
    return b;
}

static std::string card_of(Box& b) { return str(call(*b.core, {{"cmd", "station_card"}}), "card"); }

static std::string day_of(int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y%m%d", &tm);
    return buf;
}

static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Leadership's documents, made up: a roster with personal columns, a
// frequency card and a net schedule.
static const char* kRoster =
    "PRACTICE REGION MEMBER ROSTER (MADE UP)\n"
    "\n"
    "MARS Call,First Name,Last Name,Email,Phone,State,Position,Group\n"
    "NCS-A,Lee,Sample,lee@example.invalid,555-0110,ZZ,Net Control,Practice Net\n"
    "STA-B,Pat,Example,pat@example.invalid,555-0100,ZZ,Member,Practice Net\n"
    "STA-C,Sam,Sample,sam@example.invalid,555-0101,YY,Member,Practice Net\n";
static const char* kFreqCard =
    "FREQUENCY CARD   TEST ONLY\n"
    "CHAN   FREQ (KHZ)   MODE   USE\n"
    "P1     5000.0       USB    PRIMARY\n"
    "P2     10000.0      USB    SECONDARY\n"
    "S1     2500.0       ALE    SCAN\n";
static const char* kSchedule =
    "Net,Day,Time (UTC),Duration,NCS,Channel,Frequency,Notes\n"
    "Practice Net,Tue/Thu,1900Z,60,NCS-A,P1,,Weekly practice\n"
    "Region Net,1st & 3rd Sat,13:00 UTC,90 min,NCS-A,P2,,\n"
    "Late Net,Mon,0200Z,1 hr,NCS-A,S1,,\n";

static void import_documents(Box& b) {
    for (auto* doc : {kRoster, kFreqCard, kSchedule})
        CHECK(ok(call(*b.core, {{"cmd", "mars_import"}, {"text", doc}, {"source", "leadership documents"}})));
}

// ---------------------------------------------------------------- the format

static void format_checks(const fs::path& dir) {
    g_now = static_cast<int64_t>(std::time(nullptr));
    Box A = open_box(dir / "fmt", "NCS-A", std::make_unique<LoopbackModem>(0));
    import_documents(A);
    // A note with characters the radio can't send, and one shaped like a frequency.
    BookEntry e;
    e.name = "EOC ALPHA";
    e.kind = "station";
    e.call = "EOC-A";
    e.ale = "EOC@";
    e.note = "county eoc; callup #1";
    CHECK(A.ops->book_put(e, nullptr));
    BookEntry f;
    f.name = "STA-F";
    f.note = "monitors 7.3455";
    CHECK(A.ops->book_put(f, nullptr));
    std::string raw;
    JsonValue r = call(*A.core, {{"cmd", "bundle_make"}, {"label", "Practice Region"}, {"out", (dir / "fmt.pnb").string()}}, &raw);
    CHECK(ok(r) && num(r, "channels") == 3 && num(r, "schedules") == 3 && num(r, "parts") == 1);
    std::string text = str(r, "text");
    CHECK(read_file(dir / "fmt.pnb") == text);
    // No frequency and no personal detail leaves the box; every line fits the radio.
    for (auto* never : {"5000", "10000", "2500", "7.3455", "EXAMPLE", "@", "555-", "PAT", "LEE "})
        CHECK(!leaks(text, never));
    std::istringstream ls(text);
    for (std::string l; std::getline(ls, l);) {
        CHECK(l.size() <= 69);
        CHECK(bundle::radio_text(l) == l);
    }
    CHECK(count(r, "changed") >= 2);
    CHECK(text.find("\nLABEL PRACTICE REGION\n") != std::string::npos);
    CHECK(text.find("\nSIGNER NCS-A " + A.fp + "\n") != std::string::npos);
    CHECK(text.find("\nCHANNEL P1\nMODE USB\nUSE PRIMARY\n") != std::string::npos);
    CHECK(text.find("\nALE EOC/\n") != std::string::npos);

    bundle::Bundle b;
    std::string err;
    CHECK(bundle::parse(text, b, &err));
    CHECK(b.by == "NCS-A" && b.signer_fp == A.fp && b.label == "PRACTICE REGION" && b.tag.size() == 8);
    bool eoc = false;
    for (auto& x : b.book)
        if (x.name == "EOC ALPHA") eoc = x.ale == "EOC@" && x.call == "EOC-A" && x.note == "COUNTY EOC CALLUP 1";
    CHECK(eoc);
    bool pn = false;
    for (auto& s : b.schedules)
        if (s.name == "PRACTICE NET") pn = s.days == "TUE,THU" && s.start == 1140 && s.channel == "P1";
    CHECK(pn);
    for (auto& c : b.channels) CHECK(c.hz == 0);

    // A changed line, a swapped card, a cut-off bundle: refused.
    std::string bent = text;
    bent.replace(bent.find("CHANNEL P2"), 10, "CHANNEL P9");
    CHECK(!bundle::parse(bent, b, &err) && err.find("signature") != std::string::npos);
    CHECK(!bundle::parse(text.substr(0, text.size() / 2), b, &err));
    Box X = open_box(dir / "fmt", "NCS-X", std::make_unique<LoopbackModem>(0));
    std::string swapped = text;
    size_t at = swapped.find("PNA STATION CARD 1");
    swapped = swapped.substr(0, at) + card_of(X) + swapped.substr(swapped.find("SIG "));
    CHECK(!bundle::parse(swapped, b, &err));

    // Parts: a large book goes in several, and comes back whole.
    for (int i = 0; i < 160; ++i) {
        BookEntry s;
        char name[16];
        std::snprintf(name, sizeof name, "STA-%03d", i);
        s.name = name;
        s.ri = "UHA" + std::string(name).substr(4);
        s.note = "PRACTICE MEMBER NUMBER " + std::to_string(i) + " WITH A LONGER NOTE";
        CHECK(A.ops->book_put(s, nullptr));
    }
    r = call(*A.core, {{"cmd", "bundle_make"}});
    std::string big = str(r, "text");
    CHECK(ok(r) && num(r, "parts") >= 2);
    auto parts = bundle::split(big, 6000);
    CHECK(parts.size() == static_cast<size_t>(num(r, "parts")));
    std::string joined, tag;
    for (size_t i = 0; i < parts.size(); ++i) {
        int n = 0, of = 0;
        CHECK(parts[i].size() < 6000);
        CHECK(bundle::part_head("ZCZC\nBT\n" + parts[i] + "BT\nNNNN\n", tag, n, of));
        CHECK(n == static_cast<int>(i + 1) && of == static_cast<int>(parts.size()) && tag == str(r, "tag"));
        joined += bundle::part_body(parts[i]);
    }
    CHECK(bundle::parse(joined, b, &err) && b.book.size() >= 160);
    A.core->stop();
    X.core->stop();
}

// ---------------------------------------------------------------- drill 1

static void three_empty_boxes(const fs::path& dir) {
    g_now = static_cast<int64_t>(std::time(nullptr));
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    Box A = open_box(dir / "zero", "NCS-A", std::move(ma));
    Box B = open_box(dir / "zero", "STA-B", std::move(mb));
    Box C = open_box(dir / "zero", "STA-C", std::make_unique<LoopbackModem>(0));

    // A builds the startup data from leadership's documents, and a bundle from it.
    import_documents(A);
    CHECK(ok(call(*A.core, {{"cmd", "link_format"}, {"station", "STA-D"}, {"format", "plain"}})));
    JsonValue r = call(*A.core, {{"cmd", "bundle_make"}, {"label", "Practice Region"}, {"out", (dir / "stick.pnb").string()}});
    CHECK(ok(r) && num(r, "book") >= 4 && num(r, "schedules") == 3 && num(r, "links") == 1);
    const std::string tag = str(r, "tag");

    // Over the air to B, as signed plain traffic.
    r = call(*A.core, {{"cmd", "bundle_send"}, {"to", "STA-B"}});
    CHECK(ok(r) && num(r, "parts") == 1 && count(r, "failed") == 0);
    // A holds no card for B yet: B's copy carries no frequencies, and says to send the card.
    CHECK(str(item(r, "sent", 0), "frequencies") == "no_card");
    const std::string air_tag = str(item(r, "sent", 0), "tag");
    auto sent = ids(r, "STA-B");
    CHECK(sent.size() == 1);
    CHECK(wait_for([&] { return !sent.empty() && B.core->get(sent[0]).has_value(); }));
    r = call(*B.core, {{"cmd", "parse_traffic"}, {"id", sent.at(0)}});
    CHECK(ok(r) && str(r, "signature") == "unknown" && str(r, "digest") == "pass");

    // B holds no card for A: the bundle waits for B's yes, and nothing changes.
    r = call(*B.core, {{"cmd", "bundle_load"}, {"id", sent.at(0)}});
    CHECK(ok(r) && !yes(r, "loaded") && yes(r, "needs_yes") && str(r, "signer") == "unknown" && str(r, "tag") == air_tag);
    CHECK(str(r, "question").find("NCS-A " + A.fp + " signed this startup bundle") == 0);
    CHECK(B.ops->book_list().empty() && B.ops->sched_list().empty() && B.core->cards().empty());

    // B gets A's card on the air and reads the fingerprint back by voice.
    r = call(*A.core, {{"cmd", "card_send"}, {"to", "STA-B"}});
    std::string cid = str(r, "id");
    CHECK(wait_for([&] { return B.core->get(cid).has_value(); }));
    CHECK(ok(call(*B.core, {{"cmd", "card_import"}, {"id", cid}})));
    CHECK(ok(call(*B.core, {{"cmd", "card_check"}, {"station", "NCS-A"}, {"fingerprint", A.fp}})));
    r = call(*B.core, {{"cmd", "bundle_load"}, {"id", sent.at(0)}});
    CHECK(ok(r) && yes(r, "loaded") && yes(r, "signer_checked") && str(r, "signer") == "checked");
    CHECK(num(r, "book_added") >= 4 && num(r, "schedules_added") == 3 && num(r, "links_set") == 1);
    CHECK(count(r, "need_frequency") == 3 && str(r, "next").find("frequency card") != std::string::npos);
    CHECK(str(r, "for") == "STA-B" && str(r, "frequencies") == "none_for_you" &&
          str(r, "next").find("send your card to NCS-A") != std::string::npos);
    auto pn = B.ops->sched_get("PRACTICE NET");
    CHECK(pn && pn->channel == "P1" && pn->start == 1140 && pn->days == "TUE,THU");
    auto sb = B.ops->book_get("STA-C");
    CHECK(sb && sb->source.find("bundle from NCS-A " + A.fp) == 0);
    CHECK(B.core->link_format("STA-D") == "plain");
    // Loading it named nobody a keymaster and took no key.
    CHECK(!B.core->card_keymaster("NCS-A") && B.keys->for_sending().empty() && B.core->cards().size() == 1);
    // B loads its own frequency card (from the paper in its can): the plan fills in.
    CHECK(ok(call(*B.core, {{"cmd", "mars_import"}, {"text", kFreqCard}})));
    CHECK(B.ops->channels().get("P1") && B.ops->channels().get("P1")->hz == 5000000);
    r = call(*B.core, {{"cmd", "bundle_load"}, {"id", sent.at(0)}});
    CHECK(ok(r) && yes(r, "loaded") && count(r, "need_frequency") == 0 && num(r, "book_same") >= 4 &&
          num(r, "schedules_same") == 3 && num(r, "schedules_replaced") == 0);

    // C, on a stick, from a signer it has never met: loads only on C's yes.
    r = call(*C.core, {{"cmd", "bundle_load"}, {"file", (dir / "stick.pnb").string()}});
    CHECK(ok(r) && yes(r, "needs_yes") && str(r, "tag") == tag && C.ops->book_list().empty());
    r = call(*C.core, {{"cmd", "bundle_load"}, {"file", (dir / "stick.pnb").string()}, {"accept", "Y"}});
    CHECK(ok(r) && yes(r, "loaded") && !yes(r, "signer_checked") && num(r, "schedules_added") == 3);
    auto cb = C.ops->book_get("NCS-A");
    CHECK(cb && cb->source.find("signer not checked") != std::string::npos);
    CHECK(C.core->cards().empty() && C.keys->for_sending().empty());

    // A bundle changed on the way is refused, even with a yes.
    std::string bent = read_file(dir / "stick.pnb");
    bent.replace(bent.find("SCHEDULE LATE NET"), 17, "SCHEDULE LAKE NET");
    r = call(*C.core, {{"cmd", "bundle_load"}, {"text", bent}, {"accept", "Y"}});
    CHECK(!ok(r) && str(r, "error").find("signature") != std::string::npos);

    // Now trust and keys, on each member's own choice. B's card to A by air.
    r = call(*B.core, {{"cmd", "card_send"}, {"to", "NCS-A"}});
    cid = str(r, "id");
    CHECK(ok(r) && !cid.empty());
    bool arrived = wait_for([&] { return A.core->get(cid).has_value(); }, 30000);
    if (!arrived) {
        std::string raw;
        call(*B.core, {{"cmd", "get"}, {"id", cid}}, &raw);
        std::fprintf(stderr, "card_send not arrived: %s\n", raw.substr(0, 600).c_str());
    }
    CHECK(arrived);
    CHECK(ok(call(*A.core, {{"cmd", "card_import"}, {"id", cid}})));
    CHECK(ok(call(*A.core, {{"cmd", "card_check"}, {"station", "STA-B"}, {"fingerprint", B.fp}})));
    r = call(*A.core, {{"cmd", "net_tape"}, {"net", "Practice Net"}, {"days", "7"}, {"members", "STA-B STA-C"}, {"handouts", "Y"}});
    CHECK(ok(r));
    std::string hand_b, hand_c;
    for (auto& h : r.get("handouts")->arr) {
        if (str(h, "station") == "STA-B") hand_b = str(h, "text");
        if (str(h, "station") == "STA-C") hand_c = str(h, "text");
    }
    CHECK(!hand_b.empty() && !hand_c.empty());

    // B has checked A's card already, but checking a card isn't choosing an
    // authority: the handout still asks.
    r = call(*B.core, {{"cmd", "handout_import"}, {"text", hand_b}});
    CHECK(ok(r) && yes(r, "waiting_on_member") && !yes(r, "tape_opened"));
    CHECK(str(item(r, "keymaster_choices", 0), "question") == "NCS-A " + A.fp + " wants to be your keymaster. Accept?");
    // B refuses at first: the tape isn't taken, the handout isn't kept.
    r = call(*B.core, {{"cmd", "keymaster_refuse"}, {"station", "NCS-A"}});
    CHECK(ok(r) && num(r, "handouts_dropped") == 1);
    CHECK(B.keys->for_sending().empty());
    r = call(*B.core, {{"cmd", "handout_import"}, {"text", hand_b}});
    CHECK(ok(r) && !yes(r, "tape_opened") && !yes(r, "waiting_on_member") &&
          str(item(r, "keymaster_choices", 0), "choice") == "refused" &&
          str(r, "next").find("you refused NCS-A") == 0);
    r = call(*B.core, {{"cmd", "keymaster_list"}});
    CHECK(ok(r) && count(r, "refused") == 1 && count(r, "accepted") == 0);
    // Signed plain traffic still works with a refused keymaster.
    r = call(*A.core, {{"cmd", "compose_send"},
                       {"template", "FREE_FORM"},
                       {"procedure", "ACP127_PLAINDRESS"},
                       {"prec", "R"},
                       {"routing", "UHAAAB"},
                       {"originator", "NCS-A"},
                       {"from", "MARS STATION NCS-A"},
                       {"to", "UHAAAB/STA-B"},
                       {"to_station", "STA-B"},
                       {"text", "NET OPENS AT 1900Z ON P1"}});
    CHECK(ok(r));
    std::string mid = str(r, "id");
    CHECK(wait_for([&] { return B.core->get(mid).has_value(); }));
    r = call(*B.core, {{"cmd", "parse_traffic"}, {"id", mid}});
    CHECK(ok(r) && str(r, "signature") == "good" && yes(r, "signer_checked"));
    // A wrong fingerprint is not an acceptance.
    r = call(*B.core, {{"cmd", "keymaster_accept"}, {"station", "NCS-A"}, {"fingerprint", "AAAA BBBB CCCC DDDD"}});
    CHECK(!ok(r) && !B.core->card_keymaster("NCS-A"));
    // B changes its mind after a second voice check: the handout loaded again opens.
    r = call(*B.core, {{"cmd", "keymaster_accept"}, {"station", "NCS-A"}, {"fingerprint", A.fp}, {"how", "by voice"}});
    CHECK(ok(r) && count(r, "handouts") == 0);
    r = call(*B.core, {{"cmd", "handout_import"}, {"text", hand_b}});
    CHECK(ok(r) && yes(r, "tape_opened") && num(r, "added") == 7 &&
          str(item(r, "keymaster_choices", 0), "choice") == "accepted");

    // C, met in person at the rally point, accepts straight away.
    r = call(*C.core, {{"cmd", "handout_import"}, {"text", hand_c}});
    CHECK(ok(r) && yes(r, "waiting_on_member"));
    r = call(*C.core, {{"cmd", "rally_status"}});
    CHECK(ok(r) && str(r, "next").find("NCS-A " + A.fp + " wants to be your keymaster") == 0);
    r = call(*C.core, {{"cmd", "keymaster_accept"}, {"station", "NCS-A"}, {"fingerprint", A.fp}, {"how", "in person"}});
    CHECK(ok(r) && count(r, "handouts") == 1);
    // C has no card at A yet, so its handout carries no tape.
    CHECK(str(item(r, "handouts", 0), "next").find("no tape") != std::string::npos);
    CHECK(C.core->card_keymaster("NCS-A"));
    // The bundle C loaded on its own yes is still marked so; accepting the keymaster didn't change it.
    cb = C.ops->book_get("NCS-A");
    CHECK(cb && cb->source.find("signer not checked") != std::string::npos);
    for (Box* b : {&A, &B, &C}) b->core->stop();
}

// ---------------------------------------------------------------- sealed frequencies

// Frequencies ride in a member's own copy of the bundle, sealed to its card.
static void sealed_frequencies(const fs::path& dir) {
    g_now = static_cast<int64_t>(std::time(nullptr));
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    Box A = open_box(dir / "freq", "NCS-A", std::move(ma));
    Box B = open_box(dir / "freq", "STA-B", std::move(mb));
    Box C = open_box(dir / "freq", "STA-C", std::make_unique<LoopbackModem>(0));
    Box E = open_box(dir / "freq", "STA-E", std::make_unique<LoopbackModem>(0));
    import_documents(A);
    // A has checked B's and E's cards by voice; it holds C's card unchecked, and none for D.
    for (Box* m : {&B, &E}) {
        CHECK(ok(call(*A.core, {{"cmd", "card_import"}, {"text", card_of(*m)}})));
        CHECK(ok(call(*A.core, {{"cmd", "card_check"}, {"station", m == &B ? "STA-B" : "STA-E"}, {"fingerprint", m->fp}})));
    }
    CHECK(ok(call(*A.core, {{"cmd", "card_import"}, {"text", card_of(C)}})));
    CHECK(ok(call(*B.core, {{"cmd", "card_import"}, {"text", card_of(A)}})));
    CHECK(ok(call(*B.core, {{"cmd", "card_check"}, {"station", "NCS-A"}, {"fingerprint", A.fp}})));

    std::string raw;
    JsonValue r = call(*A.core, {{"cmd", "bundle_send"}, {"to", "STA-B STA-C STA-D"}}, &raw);
    CHECK(ok(r) && count(r, "sent") == 3);
    CHECK(str(item(r, "sent", 0), "frequencies") == "sealed");
    CHECK(str(item(r, "sent", 1), "frequencies") == "card_not_checked");
    CHECK(str(item(r, "sent", 2), "frequencies") == "no_card");
    auto sent = ids(r, "STA-B");
    CHECK(sent.size() == 1);
    CHECK(wait_for([&] { return !sent.empty() && B.core->get(sent[0]).has_value(); }));

    // The copy on the air: the frequencies are sealed, never in the clear.
    std::string air = sent.empty() ? "" : B.core->get(sent[0])->body;
    CHECK(air.find("PNA SEALED PLAN 1") != std::string::npos && air.find("FOR STA-B") != std::string::npos);
    CHECK(air.find("STEP 4 THE FREQUENCIES ARE SEALED BELOW TO STA-B.") != std::string::npos);
    for (auto* never : {"5000", "10000", "2500", "KHZ", "MHZ"}) CHECK(!leaks(air, never));

    // B loads it: the plan fills in, with no frequency card on B's box.
    r = call(*B.core, {{"cmd", "bundle_load"}, {"id", sent.at(0)}}, &raw);
    CHECK(ok(r) && yes(r, "loaded") && str(r, "frequencies") == "opened" && num(r, "frequencies_added") == 3);
    CHECK(count(r, "need_frequency") == 0 && str(r, "next").empty());
    for (auto* never : {"5000", "10000", "2500"}) CHECK(!leaks(raw, never));
    auto p1 = B.ops->channels().get("P1");
    CHECK(p1 && p1->hz == 5000000 && p1->mode == "USB" && p1->use == "PRIMARY");
    CHECK(B.ops->channels().get("S1") && B.ops->channels().get("S1")->hz == 2500000);
    // Loading it again changes nothing.
    r = call(*B.core, {{"cmd", "bundle_load"}, {"id", sent.at(0)}});
    CHECK(ok(r) && num(r, "frequencies_same") == 3 && num(r, "frequencies_added") == 0);
    // Still no keymaster and no key from it.
    CHECK(!B.core->card_keymaster("NCS-A") && B.keys->for_sending().empty());

    // C's own copy (its card isn't checked at A) carries none and says what to do.
    r = call(*A.core, {{"cmd", "bundle_make"}, {"for", "STA-C"}});
    CHECK(ok(r) && str(r, "frequencies") == "card_not_checked" && str(r, "next").find("check it") != std::string::npos);
    std::string c_copy = str(r, "text");
    CHECK(c_copy.find("PNA SEALED PLAN") == std::string::npos && c_copy.find("STEP 4 NO FREQUENCIES") != std::string::npos);
    r = call(*C.core, {{"cmd", "bundle_load"}, {"text", c_copy}, {"accept", "Y"}});
    CHECK(ok(r) && str(r, "frequencies") == "none_for_you" && count(r, "need_frequency") == 3);
    // B's copy reaching C opens nothing on C.
    r = call(*C.core, {{"cmd", "bundle_load"}, {"text", air}, {"accept", "Y"}});
    CHECK(ok(r) && yes(r, "loaded") && str(r, "frequencies") == "sealed_to_other" && !C.ops->channels().get("P1"));
    // A sealed line changed on the way: the whole bundle is refused.
    std::string bent = air;
    size_t g = bent.find("\nGR ");
    g = bent.find('\n', g + 1) + 1;
    bent[g] = bent[g] == 'A' ? 'B' : 'A';
    r = call(*B.core, {{"cmd", "bundle_load"}, {"text", bent}});
    CHECK(!ok(r) && str(r, "error").find("signature") != std::string::npos);

    // E already has P1 on another frequency, and hasn't checked A's card.
    Channel mine;
    mine.name = "P1";
    mine.hz = 3330000;
    mine.mode = "USB";
    CHECK(E.ops->channels().put(mine, nullptr));
    r = call(*A.core, {{"cmd", "bundle_make"}, {"for", "STA-E"}});
    CHECK(ok(r) && str(r, "frequencies") == "sealed" && num(r, "frequencies_sealed") == 3);
    std::string e_copy = str(r, "text");
    // On E's yes, the missing channels are added, but E's own P1 is kept.
    r = call(*E.core, {{"cmd", "bundle_load"}, {"text", e_copy}, {"accept", "Y"}});
    CHECK(ok(r) && num(r, "frequencies_added") == 2 && num(r, "frequencies_kept") == 1 &&
          str(r, "next").find("kept") != std::string::npos);
    CHECK(E.ops->channels().get("P1")->hz == 3330000);
    // Once E checks A's card by voice, the bundle's P1 is taken.
    CHECK(ok(call(*E.core, {{"cmd", "card_import"}, {"text", card_of(A)}})));
    CHECK(ok(call(*E.core, {{"cmd", "card_check"}, {"station", "NCS-A"}, {"fingerprint", A.fp}})));
    r = call(*E.core, {{"cmd", "bundle_load"}, {"text", e_copy}});
    CHECK(ok(r) && num(r, "frequencies_changed") == 1 && num(r, "frequencies_same") == 2);
    CHECK(E.ops->channels().get("P1")->hz == 5000000);
    // A sealed frequency block is not a key tape.
    r = call(*E.core, {{"cmd", "key_open"}, {"text", e_copy}});
    CHECK(!ok(r));
    for (Box* b : {&A, &B, &C, &E}) b->core->stop();
}

// ---------------------------------------------------------------- drill 2

static void stale_box(const fs::path& dir) {
    const int64_t today = static_cast<int64_t>(std::time(nullptr));
    const int64_t sealed = today - 4 * 365 * 86400;  // four years in the can
    std::string old_bundle;

    // ---- The day the can was sealed. ----
    g_now = sealed;
    {
        auto mk = std::make_unique<LoopbackModem>(0);
        auto mb = std::make_unique<LoopbackModem>(0);
        LoopbackModem::connect(*mk, *mb);
        Box K = open_box(dir / "old-k", "OLD-K", std::move(mk));
        Box B = open_box(dir / "can", "STA-B", std::move(mb));
        import_documents(K);
        CHECK(ok(call(*K.core, {{"cmd", "card_import"}, {"text", card_of(B)}})));
        CHECK(ok(call(*K.core, {{"cmd", "card_check"}, {"station", "STA-B"}, {"fingerprint", B.fp}})));
        JsonValue r = call(*K.core, {{"cmd", "net_tape"},
                                     {"net", "Practice Net"},
                                     {"days", "31"},
                                     {"start", day_of(sealed)},
                                     {"members", "STA-B"},
                                     {"handouts", "Y"}});
        CHECK(ok(r));
        std::string hand = str(item(r, "handouts", 0), "text");
        r = call(*B.core, {{"cmd", "handout_import"}, {"text", hand}});
        CHECK(ok(r) && yes(r, "waiting_on_member"));
        r = call(*B.core, {{"cmd", "keymaster_accept"}, {"station", "OLD-K"}, {"fingerprint", K.fp}, {"how", "in person"}});
        CHECK(ok(r) && num(item(r, "handouts", 0), "added") == 31);
        r = call(*K.core, {{"cmd", "bundle_make"}, {"label", "Practice Region"}});
        old_bundle = str(r, "text");
        r = call(*B.core, {{"cmd", "bundle_load"}, {"text", old_bundle}});
        CHECK(ok(r) && yes(r, "loaded") && str(r, "signer") == "checked");
        CHECK(ok(call(*B.core, {{"cmd", "mars_import"}, {"text", kFreqCard}})));
        CHECK(!B.keys->for_sending().empty());
        K.core->stop();
        B.core->stop();
    }
    fs::remove_all(dir / "old-k");  // the old keymaster is gone for good
    // STA-B's box goes into the can as it is, and the can is taped shut.

    // ---- Four years on. The can is opened. ----
    g_now = today;
    auto mb = std::make_unique<LoopbackModem>(0);
    auto mn = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*mb, *mn);
    Box B = open_box(dir / "can", "STA-B", std::move(mb));
    // Everything on the box is still there, and every key has long ended.
    CHECK(B.ops->sched_get("PRACTICE NET").has_value() && B.ops->book_get("STA-C").has_value());
    CHECK(B.ops->channels().get("P2") && B.ops->channels().get("P2")->hz == 10000000);
    CHECK(B.keys->for_sending().empty());
    JsonValue r = call(*B.core, {{"cmd", "rally_status"}});
    CHECK(ok(r) && str(r, "next").find("No key for now") == 0 && count(r, "keys_now") == 0);
    // The old handout and bundle still load: cards and data stay good, the tape doesn't.
    r = call(*B.core, {{"cmd", "bundle_load"}, {"text", old_bundle}});
    CHECK(ok(r) && yes(r, "loaded") && num(r, "book_same") >= 4);

    // A new keymaster: a fresh box nobody has met on this net.
    Box N = open_box(dir / "new-k", "NEW-K", std::move(mn));

    // 1. Signed plain traffic first. B's card goes out over the air.
    r = call(*B.core, {{"cmd", "card_send"}, {"to", "NEW-K"}});
    std::string cid = str(r, "id");
    CHECK(wait_for([&] { return N.core->get(cid).has_value(); }));
    r = call(*N.core, {{"cmd", "parse_traffic"}, {"id", cid}});
    CHECK(ok(r) && str(r, "signature") == "unknown");
    CHECK(ok(call(*N.core, {{"cmd", "card_import"}, {"id", cid}})));
    r = call(*N.core, {{"cmd", "parse_traffic"}, {"id", cid}});
    CHECK(ok(r) && str(r, "signature") == "good" && !yes(r, "signer_checked"));
    CHECK(ok(call(*N.core, {{"cmd", "card_check"}, {"station", "STA-B"}, {"fingerprint", B.fp}})));
    r = call(*N.core, {{"cmd", "card_send"}, {"to", "STA-B"}});
    cid = str(r, "id");
    CHECK(wait_for([&] { return B.core->get(cid).has_value(); }));
    CHECK(ok(call(*B.core, {{"cmd", "card_import"}, {"id", cid}})));
    r = call(*B.core, {{"cmd", "parse_traffic"}, {"id", cid}});
    CHECK(ok(r) && str(r, "signature") == "good" && !yes(r, "signer_checked"));

    // 2. NEW-K sends a tape before B has chosen it: not taken.
    r = call(*N.core, {{"cmd", "net_tape"}, {"net", "Practice Net"}, {"days", "7"}, {"members", "STA-B"}, {"send", "Y"}});
    CHECK(ok(r));
    std::string tid = str(item(r, "sealed", 0), "id");
    CHECK(wait_for([&] { return !tid.empty() && B.core->get(tid).has_value(); }));
    r = call(*B.core, {{"cmd", "key_open"}, {"id", tid}});
    CHECK(!ok(r) && str(r, "error").find("keymaster") != std::string::npos);
    CHECK(B.keys->for_sending().empty());

    // A stranger on a stick wants to be B's keymaster too. B refuses.
    Box X = open_box(dir / "stranger", "ROGUE-X", std::make_unique<LoopbackModem>(0));
    CHECK(ok(call(*X.core, {{"cmd", "card_import"}, {"text", card_of(B)}})));
    CHECK(ok(call(*X.core, {{"cmd", "card_check"}, {"station", "STA-B"}, {"fingerprint", B.fp}})));
    r = call(*X.core, {{"cmd", "net_tape"}, {"net", "Practice Net"}, {"days", "31"}, {"members", "STA-B"}, {"handouts", "Y"}});
    std::string rogue = str(item(r, "handouts", 0), "text");
    r = call(*B.core, {{"cmd", "handout_import"}, {"text", rogue}});
    CHECK(ok(r) && yes(r, "waiting_on_member") && !yes(r, "tape_opened"));
    r = call(*B.core, {{"cmd", "keymaster_refuse"}, {"station", "ROGUE-X"}});
    CHECK(ok(r) && num(r, "handouts_dropped") == 1 && B.keys->for_sending().empty());

    // 3. B knows NEW-K's voice, reads the fingerprint back, and accepts.
    r = call(*B.core, {{"cmd", "keymaster_accept"}, {"station", "NEW-K"}, {"fingerprint", N.fp}, {"how", "by voice"}});
    CHECK(ok(r) && yes(r, "keymaster"));
    r = call(*B.core, {{"cmd", "key_open"}, {"id", tid}});
    CHECK(ok(r) && num(r, "added") == 7 && str(r, "from") == "NEW-K");
    r = call(*B.core, {{"cmd", "rally_status"}});
    CHECK(ok(r) && str(r, "next").find("Ready") == 0);
    r = call(*B.core, {{"cmd", "keymaster_list"}});
    CHECK(ok(r) && count(r, "accepted") == 2 && count(r, "refused") == 1 && count(r, "undecided") == 0);
    CHECK(B.keys->for_sending().at(0).check == N.keys->for_sending().at(0).check);

    // 4. Codress works again.
    std::string key = B.keys->for_sending().at(0).indicator;
    r = call(*N.core, {{"cmd", "compose_send"},
                       {"template", "FREE_FORM"},
                       {"procedure", "ACP127_CODRESS"},
                       {"key", key},
                       {"prec", "P"},
                       {"routing", "UHAAAB"},
                       {"originator", "NEW-K"},
                       {"from", "MARS STATION NEW-K"},
                       {"to", "UHAAAB/STA-B"},
                       {"to_station", "STA-B"},
                       {"text", "NET IS BACK UP"}});
    CHECK(ok(r));
    std::string mid = str(r, "id");
    CHECK(wait_for([&] { return B.core->get(mid).has_value(); }));
    r = call(*B.core, {{"cmd", "parse_traffic"}, {"id", mid}});
    CHECK(ok(r) && yes(r, "deciphered") && str(r, "signature") == "good" && yes(r, "signer_checked"));

    // 5. B hands the net's startup data to the new keymaster: its old book
    //    and schedules, re-signed by B, over the air.
    r = call(*B.core, {{"cmd", "bundle_send"}, {"to", "NEW-K"}, {"label", "Practice Region"}});
    CHECK(ok(r) && str(r, "by") == "STA-B");
    auto sent = ids(r, "NEW-K");
    CHECK(wait_for([&] { return !sent.empty() && N.core->get(sent.back()).has_value(); }));
    r = call(*N.core, {{"cmd", "bundle_load"}, {"id", sent.at(0)}});
    CHECK(ok(r) && yes(r, "loaded") && str(r, "signer") == "checked" && num(r, "schedules_added") == 3);
    CHECK(N.ops->sched_get("REGION NET").has_value() && N.ops->book_get("STA-C").has_value());
    // NEW-K's trust didn't change by loading it: B is still only a checked member.
    CHECK(!N.core->card_keymaster("STA-B"));
    // The old keymaster's bundle, relayed as it was: signed by OLD-K, whom
    // NEW-K never met. It waits for NEW-K's own yes.
    r = call(*N.core, {{"cmd", "bundle_load"}, {"text", old_bundle}});
    CHECK(ok(r) && yes(r, "needs_yes") && str(r, "by") == "OLD-K");
    for (Box* b : {&B, &N, &X}) b->core->stop();
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_groundzero_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    format_checks(dir);
    three_empty_boxes(dir);
    sealed_frequencies(dir);
    stale_box(dir);
    fs::remove_all(dir);
    return check::report("test_groundzero");
}
