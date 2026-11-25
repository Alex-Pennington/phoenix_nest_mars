// Long tapes and bundles in numbered parts (docs/crypto.md, Parts).
//
// The drill: net control (NCS-A) seals a 62-day key tape to STA-B. It is too
// long for one message, so it goes as numbered, signed parts. Two parts are
// lost on the way. STA-B holds the rest, won't open the tape, asks for the
// two by number, and opens it once net control sends them again. All of it
// through the forms. Then a startup bundle that has outgrown one message goes
// the same way, and on a second pair an incomplete set is dropped after the
// hold time.
//
// Then the resend queue: a rally of 30 members, where many miss parts and
// ask at once. Their asks are pooled, a part several missed goes once to the
// whole net, the operator sends it all with one press, a member who keeps
// asking is flagged, and an hourly cap and the next net slot pace the rest.
//
// Calls and data are made up. The frequencies are public time-signal ones.
#include "../src/api_server.h"
#include "../src/channels.h"
#include "../src/codress.h"
#include "../src/compose.h"
#include "../src/core.h"
#include "../src/first_start.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/parts.h"
#include "../src/station.h"
#include "check.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

// A frequency left in text shows as a number of its own (5000, 5000.0).
// Digits inside a key, signature or packed block don't count.
static bool leaks(const std::string& text, const std::string& needle) {
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        // A number standing alone: not the start of a longer one (a date-time
        // group such as 250012Z holds 2500).
        const size_t end = at + needle.size();
        if (std::isdigit(static_cast<unsigned char>(needle.back())) && end < text.size() &&
            std::isdigit(static_cast<unsigned char>(text[end])))
            continue;
        if (at == 0) return true;
        unsigned char before = static_cast<unsigned char>(text[at - 1]);
        if (!std::isalnum(before) && before != '+' && before != '/') return true;
    }
    return false;
}

static bool wait_for(const std::function<bool()>& cond, int ms = 30000) {
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
static std::string line_of(std::initializer_list<std::pair<std::string, std::string>> kv) {
    std::string line = "{";
    bool first = true;
    for (auto& [k, v] : kv) {
        if (!first) line += ",";
        first = false;
        line += "\"" + k + "\":\"" + esc(v) + "\"";
    }
    return line + "}";
}

static std::string g_all;  // every form reply, to look for frequencies in

static JsonValue parsed(const std::string& reply) {
    g_all += reply + "\n";
    if (std::getenv("FORMS_DEBUG")) std::cerr << "  -> " << reply.substr(0, 700) << "\n";
    JsonValue v;
    parse_json(reply, v);
    return v;
}
static JsonValue call(Core& c, std::initializer_list<std::pair<std::string, std::string>> kv) {
    std::string line = line_of(kv);
    if (std::getenv("FORMS_DEBUG")) std::cerr << line.substr(0, 200) << "\n";
    return parsed(ApiServer::handle(c, line));
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
static const JsonValue& sub(const JsonValue& v, const char* k) {
    static JsonValue none;
    const JsonValue* x = v.get(k);
    return x ? *x : none;
}
static const std::vector<JsonValue>& arr(const JsonValue& v, const char* k) {
    static const std::vector<JsonValue> none;
    const JsonValue* x = v.get(k);
    return x && x->is_array() ? x->arr : none;
}
static const JsonValue* field_of(const JsonValue& form, const std::string& key) {
    for (auto& f : arr(form, "fields"))
        if (str(f, "key") == key) return &f;
    return nullptr;
}
static std::vector<std::string> choice_values(const JsonValue& form, const std::string& key) {
    std::vector<std::string> out;
    if (auto f = field_of(form, key))
        for (auto& c : arr(*f, "choices")) out.push_back(str(c, "value"));
    return out;
}

// The member's form calls, as gale-term sends them.
static JsonValue form(Core& c, const std::string& id, std::vector<std::pair<std::string, std::string>> values) {
    std::string line = "{\"cmd\":\"form_submit\",\"form\":\"" + id + "\"";
    for (auto& [k, v] : values) line += ",\"" + k + "\":\"" + esc(v) + "\"";
    line += "}";
    if (std::getenv("FORMS_DEBUG")) std::cerr << "FORM " << id << "\n";
    return parsed(ApiServer::handle(c, line));
}
static JsonValue show(Core& c, const std::string& id, const std::string& key = "", const std::string& value = "") {
    if (key.empty()) return call(c, {{"cmd", "form_get"}, {"form", id}});
    return call(c, {{"cmd", "form_get"}, {"form", id}, {key, value}});
}

static int64_t g_now = 0;

struct Box {
    std::unique_ptr<Core> core;
    std::shared_ptr<keys::Keyring> keys;
    std::shared_ptr<Ops> ops;
    std::string call, fp;
};

// A box out of the can: it answers only the setup form until the call is
// given, then starts as that station with the keys setup made.
static Box out_of_can(const fs::path& dir, const std::string& typed, std::unique_ptr<ModemDriver> modem,
                      int hold_s = 72 * 3600, int cap = 12) {
    Box b;
    std::string got;
    JsonValue r = parsed(first_start::handle(dir.string(), g_now, R"({"cmd":"form_list"})", &got));
    CHECK(ok(r) && arr(r, "forms").size() == 1 && str(arr(r, "forms")[0], "id") == "SETUP");
    r = parsed(first_start::handle(dir.string(), g_now, R"({"cmd":"form_get","form":"BOOK"})", &got));
    CHECK(!ok(r) && str(r, "error").find("SETUP") != std::string::npos);
    r = parsed(first_start::handle(dir.string(), g_now, R"({"cmd":"form_get","form":"SETUP"})", &got));
    CHECK(ok(r) && field_of(r, "call") && yes(*field_of(r, "call"), "required") && !str(*field_of(r, "call"), "help").empty());
    // A slip first: the form says what's wrong and changes nothing.
    r = parsed(first_start::handle(dir.string(), g_now, R"({"cmd":"form_submit","form":"SETUP","call":"ALL"})", &got));
    CHECK(!ok(r) && str(arr(r, "problems")[0], "field") == "call" && got.empty());
    r = parsed(first_start::handle(dir.string(), g_now,
                                   "{\"cmd\":\"form_submit\",\"form\":\"SETUP\",\"call\":\"" + typed + "\"}", &got));
    CHECK(ok(r) && str(r, "action") == "setup_station" && !got.empty());
    b.call = got;
    b.fp = str(sub(r, "result"), "fingerprint");
    CHECK(b.fp.size() == 19);
    // The box starts as that station (what main does with the saved call).
    std::string saved;
    CHECK(first_start::saved_call(dir.string(), saved) && saved == got);
    std::string base = (dir / got).string();
    CoreConfig cfg{got, base + ".db", 3, 1, true};
    cfg.parts_hold_s = hold_s;
    cfg.parts_resend_per_hour = cap;
    b.core = std::make_unique<Core>(cfg, std::move(modem));
    b.keys = std::make_shared<keys::Keyring>(base + ".keys");
    b.keys->set_clock([] { return g_now; });
    std::string err;
    b.keys->load(&err);
    b.core->set_keyring(b.keys);
    b.ops = std::make_shared<Ops>(got, base + ".db");
    b.ops->set_clock([] { return g_now; });
    b.core->set_ops(b.ops);
    auto id = std::make_shared<station::Identity>();
    bool exists = false;
    CHECK(station::load_identity(base + ".id", *id, &exists, &err) && exists);
    b.core->set_identity(id, base + ".id");
    b.core->start();
    // SETUP is done: it isn't offered again.
    r = call(*b.core, {{"cmd", "form_list"}, {"level", "zero"}});
    bool setup_listed = false;
    for (auto& f : arr(r, "forms")) setup_listed = setup_listed || str(f, "id") == "SETUP";
    CHECK(ok(r) && !setup_listed && arr(r, "forms").size() >= 16);
    return b;
}

// The newest choice a form offers for a field (a card or bundle that came in).
static std::string pick(Core& c, const std::string& form_id, const std::string& key) {
    std::string out;
    wait_for([&] {
        auto vs = choice_values(show(c, form_id), key);
        if (vs.empty()) return false;
        out = vs.front();
        return true;
    });
    return out;
}


static std::vector<int> ints(const JsonValue& v, const char* k) {
    std::vector<int> out;
    for (auto& x : arr(v, k)) out.push_back(static_cast<int>(x.n));
    return out;
}

// ---------------------------------------------------------------- the format

static void format() {
    std::string text;
    for (int i = 0; i < 60; ++i) text += "LINE " + std::to_string(i) + " OF THE BLOCK abcdefghij\n";
    auto up = [](std::string x) {
        for (auto& c : x) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return x;
    };
    std::string tag = parts::tag_of(text);
    CHECK(tag.size() == 8 && tag == parts::tag_of(text) && tag != parts::tag_of(text + "X\n"));
    for (char c : tag) CHECK(std::string("ABCDEFGHJKLMNPQRSTUVWXYZ23456789").find(c) != std::string::npos);
    auto ps = parts::split("TAPE", tag, text, 600);
    CHECK(ps.size() >= 3);
    std::string back;
    for (size_t i = 0; i < ps.size(); ++i) {
        CHECK(ps[i].size() <= 600);
        parts::Head h;
        CHECK(parts::head(ps[i], h) && h.kind == "TAPE" && h.tag == tag && h.n == static_cast<int>(i + 1) &&
              h.of == static_cast<int>(ps.size()));
        CHECK(ps[i].rfind("PART " + std::to_string(i + 1) + " OF " + std::to_string(ps.size()) + " PNA TAPE " + tag, 0) == 0);
        // Cut out of a stored message (with a header and signature around it), it's the same part.
        CHECK(parts::cut("ZCZC NR 12\nBT\n" + ps[i] + "[SIG:ABC]\nNNNN\n") == up(ps[i]));
        back += parts::body(ps[i]);
    }
    CHECK(back == up(text));
    // A short block is one part.
    CHECK(parts::split("BUNDLE", tag, "ONE LINE\n", 6000).size() == 1);
    // The bundle parts of earlier builds still read.
    parts::Head h;
    CHECK(parts::head("PNA BUNDLE PART 2 OF 3 TAG 4HNPQ2XR\nABC\nEND PART\n", h) && h.kind == "BUNDLE" && h.n == 2 &&
          h.of == 3 && h.tag == "4HNPQ2XR");
    CHECK(parts::body("PNA BUNDLE PART 2 OF 3 TAG 4HNPQ2XR\nABC\nEND PART\n") == "ABC\n");
    // Not parts: a bad kind, tag or count.
    CHECK(!parts::head("PART 1 OF 2 PNA KEYS 4HNPQ2XR\n", h));
    CHECK(!parts::head("PART 1 OF 2 PNA TAPE 4HNPQ2X\n", h));
    CHECK(!parts::head("PART 3 OF 2 PNA TAPE 4HNPQ2XR\n", h));
    CHECK(!parts::head("PART 1 OF 100 PNA TAPE 4HNPQ2XR\n", h));
    CHECK(parts::cut("PART 1 OF 2 PNA TAPE 4HNPQ2XR\nABC\n").empty());  // no END line: not whole
    // The request, and reading it back.
    std::string rq = parts::request_text("TAPE", tag, {2, 4});
    CHECK(rq == "PNA PARTS REQUEST TAPE " + tag + "\nMISSING 2 4\nEND REQUEST\n");
    std::string kind, t2;
    std::vector<int> miss;
    CHECK(parts::read_request("ZCZC\n" + rq + "NNNN\n", kind, t2, miss) && kind == "TAPE" && t2 == tag &&
          miss == (std::vector<int>{2, 4}));
    CHECK(!parts::read_request("PNA PARTS REQUEST TAPE " + tag + "\nMISSING 2 4\n", kind, t2, miss));
    CHECK(!parts::read_request("PNA PARTS REQUEST TAPE " + tag + "\nMISSING\nEND REQUEST\n", kind, t2, miss));
}

// ---------------------------------------------------------------- the drill

// Two boxes that know each other: cards both ways, checked by voice, and the
// receiver takes the sender as its keymaster.
static void pair_up(Box& A, Box& B) {
    CHECK(ok(form(*B.core, "CARD_SEND", {{"to", A.call}})));
    std::string cid = pick(*A.core, "CARD_LOAD", "id");
    CHECK(ok(form(*A.core, "CARD_LOAD", {{"id", cid}})));
    CHECK(ok(form(*A.core, "CARD_CHECK", {{"station", B.call}, {"fingerprint", B.fp}})));
    CHECK(ok(form(*A.core, "CARD_SEND", {{"to", B.call}})));
    cid = pick(*B.core, "CARD_LOAD", "id");
    CHECK(ok(form(*B.core, "CARD_LOAD", {{"id", cid}})));
    CHECK(ok(form(*B.core, "CARD_CHECK", {{"station", A.call}, {"fingerprint", A.fp}})));
    CHECK(ok(form(*B.core, "KEYMASTER", {{"station", A.call}, {"answer", "ACCEPT"}, {"fingerprint", A.fp}})));
    CHECK(A.core->card_verified(B.call) && B.core->card_keymaster(A.call));
}

// The set waiting at a box, as parts_list shows it.
static JsonValue waiting(Core& c, const std::string& all = "") {
    JsonValue r = all.empty() ? call(c, {{"cmd", "parts_list"}}) : call(c, {{"cmd", "parts_list"}, {"all", all}});
    return arr(r, "sets").empty() ? JsonValue() : arr(r, "sets")[0];
}

// Sends with the sender's radio held, so its parts queue; the ones in lose
// are cancelled before they go, as if lost on the air. The rest go out.
static void lose(Box& A, LoopbackModem& radio, const std::vector<std::string>& ids, const std::vector<int>& lost) {
    for (int n : lost) {
        std::string e;
        CHECK(A.core->cancel(ids[n - 1], &e));
    }
    radio.hold(false);
}

static void drill(const fs::path& dir) {
    g_now = static_cast<int64_t>(std::time(nullptr));
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    LoopbackModem* radio_a = ma.get();
    Box A = out_of_can(dir / "a", "NCS-A", std::move(ma));
    Box B = out_of_can(dir / "b", "STA-B", std::move(mb));
    pair_up(A, B);
    JsonValue r;

    // A 62-day tape, sealed to B, is too long for one message.
    radio_a->hold(true);
    r = form(*A.core, "NET_TAPE", {{"net", "practice net"}, {"members", "STA-B"}, {"days", "62"}});
    CHECK(ok(r));
    const JsonValue& sent = arr(sub(r, "result"), "sealed")[0];
    int of = static_cast<int>(num(sent, "parts"));
    std::vector<std::string> ids;
    for (auto& x : arr(sent, "ids")) ids.push_back(x.s);
    CHECK(of >= 4 && static_cast<int>(ids.size()) == of);
    std::cerr << "  62-day tape: " << str(sent, "text").size() << " bytes in " << of << " parts\n";
    // Each part is its own message, under the 8192-byte limit.
    for (auto& id : ids) CHECK(A.core->get(id) && A.core->get(id)->body.size() <= 8192);
    // Parts 2 and 4 are lost on the air.
    lose(A, *radio_a, ids, {2, 4});
    CHECK(wait_for([&] { return num(waiting(*B.core), "have") == of - 2; }));

    // Each part that came is signed by net control, and checked.
    JsonValue set = waiting(*B.core);
    CHECK(str(set, "kind") == "tape" && str(set, "from") == "NCS-A" && str(set, "state") == "waiting");
    CHECK(num(set, "parts") == of && ints(set, "missing") == (std::vector<int>{2, 4}) && num(set, "drops_in_s") > 0);
    r = call(*B.core, {{"cmd", "parse_traffic"}, {"id", str(set, "id")}});
    CHECK(ok(r) && str(r, "signature") == "good" && yes(r, "signer_checked"));

    // B won't open a tape that isn't all here, and says which parts it lacks.
    std::string tid = pick(*B.core, "KEY_OPEN", "id");
    r = form(*B.core, "KEY_OPEN", {{"id", tid}});
    CHECK(!ok(r) && B.keys->for_sending().empty());
    CHECK(g_all.rfind("PARTS_ASK") != std::string::npos);
    r = call(*B.core, {{"cmd", "key_open"}, {"id", tid}});
    CHECK(!ok(r) && ints(r, "missing_parts") == (std::vector<int>{2, 4}) && num(r, "have") == of - 2);

    // B asks for parts 2 and 4, through the form.
    r = show(*B.core, "PARTS_ASK");
    CHECK(choice_values(r, "id").size() == 1);
    r = form(*B.core, "PARTS_ASK", {{"id", choice_values(r, "id")[0]}});
    CHECK(ok(r) && str(r, "action") == "parts_request");
    CHECK(ints(sub(r, "result"), "asked") == (std::vector<int>{2, 4}) && str(sub(r, "result"), "to") == "NCS-A");
    // Net control sees the request and sends the two again, through the form.
    CHECK(wait_for([&] { return !arr(call(*A.core, {{"cmd", "parts_list"}}), "requests").empty(); }));
    r = call(*A.core, {{"cmd", "parts_list"}});
    CHECK(str(arr(r, "requests")[0], "from") == "STA-B" && ints(arr(r, "requests")[0], "missing") == (std::vector<int>{2, 4}));
    r = show(*A.core, "PARTS_REPEAT");
    CHECK(choice_values(r, "id").size() == 1);
    r = form(*A.core, "PARTS_REPEAT", {{"id", choice_values(r, "id")[0]}});
    CHECK(ok(r) && arr(sub(r, "result"), "resent").size() == 2 && str(sub(r, "result"), "to") == "STA-B");

    // All here: the tape opens, every day of it.
    CHECK(wait_for([&] { return str(waiting(*B.core, "Y"), "state") == "complete"; }));
    CHECK(arr(call(*B.core, {{"cmd", "parts_list"}}), "sets").empty());  // nothing waiting
    r = form(*B.core, "KEY_OPEN", {{"id", tid}});
    CHECK(ok(r) && num(sub(r, "result"), "added") == 62);
    CHECK(!B.keys->for_sending().empty());

    // A request can't send parts to a station they never went to.
    r = call(*A.core, {{"cmd", "parts_resend"}, {"to", "STA-Q"}, {"tag", str(set, "tag")}, {"parts", "2"}});
    CHECK(!ok(r));
    // And a short tape still goes whole, as one message, for older boxes.
    r = form(*A.core, "NET_TAPE", {{"net", "short net"}, {"members", "STA-B"}, {"days", "3"}});
    CHECK(ok(r) && num(arr(sub(r, "result"), "sealed")[0], "parts") == 1);
    std::string sid = str(arr(sub(r, "result"), "sealed")[0], "id");
    CHECK(wait_for([&] { return B.core->get(sid).has_value(); }));
    parts::Head h;
    CHECK(!parts::head(B.core->get(sid)->body, h));
    CHECK(ok(form(*B.core, "KEY_OPEN", {{"id", sid}})));

    // ------------------------------------------- a startup bundle in parts

    CHECK(ok(form(*A.core, "CHANNEL", {{"name", "P1"}, {"frequency", "5000"}, {"use", "PRIMARY"}})));
    CHECK(ok(form(*A.core, "CHANNEL", {{"name", "S1"}, {"frequency", "3330"}, {"use", "TIME"}})));
    // A region's book has grown past one message.
    for (int i = 1; i <= 260; ++i) {
        char call_[16];
        std::snprintf(call_, sizeof call_, "STA-%03d", i);
        r = call(*A.core, {{"cmd", "book_put"}, {"name", call_}, {"call", call_}, {"note", "practice member, made up"}});
        CHECK(ok(r));
    }
    radio_a->hold(true);
    r = form(*A.core, "BUNDLE_SEND", {{"to", "STA-B"}, {"label", "grown region"}});
    CHECK(ok(r));
    const JsonValue& bs = arr(sub(r, "result"), "sent")[0];
    int bof = static_cast<int>(num(bs, "parts"));
    std::vector<std::string> bids;
    for (auto& x : arr(bs, "ids")) bids.push_back(x.s);
    CHECK(bof >= 2 && static_cast<int>(bids.size()) == bof && str(bs, "frequencies") == "sealed");
    std::cerr << "  bundle of 260 entries in " << bof << " parts\n";
    lose(A, *radio_a, bids, {bof});  // the last part is lost
    CHECK(wait_for([&] { return str(waiting(*B.core), "kind") == "bundle" && num(waiting(*B.core), "have") == bof - 1; }));
    std::string bid = pick(*B.core, "BUNDLE_LOAD", "id");
    r = form(*B.core, "BUNDLE_LOAD", {{"id", bid}});
    CHECK(ok(r) && !yes(sub(r, "result"), "loaded") && ints(sub(r, "result"), "missing_parts") == std::vector<int>{bof});
    CHECK(!B.ops->book_get("STA-001"));
    auto asks = choice_values(show(*B.core, "PARTS_ASK"), "id");
    CHECK(asks.size() == 1);
    std::string want = asks.empty() ? "" : asks[0];
    CHECK(ok(form(*B.core, "PARTS_ASK", {{"id", want}})));
    std::string rid;
    CHECK(wait_for([&] {
        JsonValue l = call(*A.core, {{"cmd", "parts_list"}});
        for (auto& q : arr(l, "requests"))
            if (str(q, "kind") == "bundle") rid = str(q, "id");
        return !rid.empty();
    }));
    CHECK(ok(form(*A.core, "PARTS_REPEAT", {{"id", rid}})));
    CHECK(wait_for([&] { return arr(call(*B.core, {{"cmd", "parts_list"}}), "sets").empty(); }));
    r = form(*B.core, "BUNDLE_LOAD", {{"id", bid}});
    CHECK(ok(r) && yes(sub(r, "result"), "loaded") && str(sub(r, "result"), "frequencies") == "opened");
    CHECK(B.ops->book_get("STA-001") && B.ops->book_get("STA-260"));
    CHECK(B.ops->channels().get("P1") && B.ops->channels().get("P1")->hz == 5000000);

    // ------------------------------------------- dropped after the hold time

    auto mc = std::make_unique<LoopbackModem>(0);
    auto md = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*mc, *md);
    LoopbackModem* radio_c = mc.get();
    Box C = out_of_can(dir / "c", "NCS-C", std::move(mc));
    Box D = out_of_can(dir / "d", "STA-D", std::move(md), 3);  // holds parts 3 s
    pair_up(C, D);
    radio_c->hold(true);
    r = form(*C.core, "NET_TAPE", {{"net", "drill net"}, {"members", "STA-D"}, {"days", "40"}});
    CHECK(ok(r));
    const JsonValue& cs = arr(sub(r, "result"), "sealed")[0];
    std::vector<std::string> cids;
    for (auto& x : arr(cs, "ids")) cids.push_back(x.s);
    int cof = static_cast<int>(cids.size());
    CHECK(cof >= 2);
    lose(C, *radio_c, cids, {1});
    CHECK(wait_for([&] { return num(waiting(*D.core), "have") == cof - 1; }));
    std::string dtag = str(waiting(*D.core), "tag");
    std::string did = str(waiting(*D.core), "id");
    // Past the hold time, the set is dropped: KEY_OPEN says so.
    CHECK(wait_for([&] { return str(waiting(*D.core), "state") == "dropped"; }, 10000));
    set = waiting(*D.core);
    CHECK(num(set, "have") == 0 && num(set, "dropped_parts") == cof - 1 && ints(set, "missing").size() == static_cast<size_t>(cof));
    r = call(*D.core, {{"cmd", "key_open"}, {"id", did}});
    CHECK(!ok(r) && str(r, "error").find("dropped") != std::string::npos);
    // A fresh sending of every part opens it.
    std::string all;
    for (int n = 1; n <= cof; ++n) all += (all.empty() ? "" : " ") + std::to_string(n);
    r = call(*C.core, {{"cmd", "parts_resend"}, {"to", "STA-D"}, {"tag", dtag}, {"parts", all}});
    CHECK(ok(r) && static_cast<int>(arr(r, "resent").size()) == cof);
    CHECK(wait_for([&] { return str(waiting(*D.core, "Y"), "state") == "complete"; }));
    r = call(*D.core, {{"cmd", "key_open"}, {"id", did}});
    CHECK(ok(r) && num(r, "added") == 40);

    // Not one frequency in a reply.
    for (auto* never : {"5000", "3330"}) CHECK(!leaks(g_all, never));
    for (Box* b : {&A, &B, &C, &D}) b->core->stop();
}

// ---------------------------------------------------------------- the rally

static std::string ids_of(const JsonValue& v, const char* k, std::vector<std::string>& out) {
    out.clear();
    for (auto& x : arr(v, k)) out.push_back(x.s);
    return out.empty() ? "" : out[0];
}

// A member asks for what it lacks, through the form, and net control has it
// before the next member keys up.
static int asks_from(Box& A, const std::string& st) {
    int n = 0;
    for (auto& m : A.core->list(2000))
        if (m.direction == "in" && m.src == st && m.body.find("PNA PARTS REQUEST") != std::string::npos) ++n;
    return n;
}

static void ask(Box& M, Box& A, const std::string& kind) {
    std::string want;
    JsonValue l = call(*M.core, {{"cmd", "parts_list"}});
    for (auto& e : arr(l, "sets"))
        if (str(e, "kind") == kind && str(e, "state") == "waiting") want = str(e, "id");
    if (want.empty()) {
        std::cout << M.call << " has no " << kind << " waiting: "
                  << ApiServer::handle(*M.core, line_of({{"cmd", "parts_list"}, {"all", "Y"}})).substr(0, 900) << "\n";
    }
    CHECK(!want.empty());
    int before = asks_from(A, M.call);
    CHECK(ok(form(*M.core, "PARTS_ASK", {{"id", want}})));
    CHECK(wait_for([&] { return asks_from(A, M.call) > before; }));
}

static void rally(const fs::path& dir) {
    g_now = static_cast<int64_t>(std::time(nullptr));
    fs::path stick = dir / "stick";
    fs::create_directories(stick);
    const int kMembers = 30;
    std::vector<std::unique_ptr<LoopbackModem>> radios;
    std::vector<LoopbackModem*> on_net;
    for (int i = 0; i <= kMembers; ++i) {
        radios.push_back(std::make_unique<LoopbackModem>(0));
        on_net.push_back(radios.back().get());
    }
    LoopbackModem::share(on_net);
    LoopbackModem* radio_a = on_net[0];
    Box A = out_of_can(dir / "ncs", "NCS-A", std::move(radios[0]));
    std::vector<Box> M;
    std::string everyone;
    for (int i = 1; i <= kMembers; ++i) {
        char c[16];
        std::snprintf(c, sizeof c, "STA-%02d", i);
        M.push_back(out_of_can(dir / c, c, std::move(radios[i])));
        everyone += (everyone.empty() ? "" : " ") + std::string(c);
    }
    // Cards meet in person, on a stick: each member's to net control, and
    // net control's to each member, who takes it as keymaster.
    CHECK(ok(form(*A.core, "CARD_SAVE", {{"out", (stick / "NCS-A.TXT").string()}})));
    for (auto& m : M) {
        fs::path f = stick / (m.call + ".TXT");
        CHECK(ok(form(*m.core, "CARD_SAVE", {{"out", f.string()}})));
        CHECK(ok(form(*A.core, "CARD_LOAD", {{"file", f.string()}})));
        CHECK(ok(form(*A.core, "CARD_CHECK", {{"station", m.call}, {"fingerprint", m.fp}})));
        CHECK(ok(form(*m.core, "CARD_LOAD", {{"file", (stick / "NCS-A.TXT").string()}})));
        CHECK(ok(form(*m.core, "CARD_CHECK", {{"station", "NCS-A"}, {"fingerprint", A.fp}})));
        CHECK(ok(form(*m.core, "KEYMASTER",
                      {{"station", "NCS-A"}, {"answer", "ACCEPT"}, {"fingerprint", A.fp}, {"how", "IN PERSON"}})));
    }
    JsonValue r;

    // A month's tape to every member: three parts each, each copy sealed to
    // its member. Part 2 is lost for STA-01 to STA-05.
    radio_a->hold(true);
    r = form(*A.core, "NET_TAPE", {{"net", "rally net"}, {"members", everyone}, {"days", "31"}});
    CHECK(ok(r) && arr(sub(r, "result"), "sealed").size() == static_cast<size_t>(kMembers));
    for (auto& e : arr(sub(r, "result"), "sealed")) {
        std::vector<std::string> ids;
        ids_of(e, "ids", ids);
        if (ids.size() < 2)
            std::cout << "  tape for " << str(e, "station") << ": parts " << num(e, "parts") << " checked "
                      << yes(e, "checked") << " error " << str(e, "error") << "\n";
        CHECK(ids.size() >= 2);
        std::string st = str(e, "station");
        if (st <= "STA-05") {
            std::string err;
            CHECK(A.core->cancel(ids[1], &err));
        }
    }

    // A startup bundle the region grew, one copy for all (on a stick, no
    // frequencies), sent to every member: the same block, the same tag.
    for (int i = 1; i <= 260; ++i) {
        char n[24];
        std::snprintf(n, sizeof n, "PRACTICE-%03d", i);
        CHECK(ok(call(*A.core, {{"cmd", "book_put"}, {"name", n}, {"call", n}, {"note", "made up for the drill"}})));
    }
    CHECK(ok(form(*A.core, "BUNDLE_MAKE", {{"out", (stick / "region.pnb").string()}})));
    r = call(*A.core, {{"cmd", "bundle_send"}, {"to", everyone}, {"file", (stick / "region.pnb").string()}});
    CHECK(ok(r) && arr(r, "sent").size() == static_cast<size_t>(kMembers));
    std::cerr << "  rally: shared bundle of 260 entries, " << num(r, "parts") / kMembers << " parts each\n";
    // Part 2 lost for STA-06 to STA-17, part 3 for STA-13 to STA-20.
    for (auto& e : arr(r, "sent")) {
        std::vector<std::string> ids;
        ids_of(e, "ids", ids);
        std::string st = str(e, "station"), err;
        CHECK(ids.size() >= 3 && str(e, "tag") == str(r, "tag"));
        if (st >= "STA-06" && st <= "STA-17") CHECK(A.core->cancel(ids[1], &err));
        if (st >= "STA-13" && st <= "STA-20") CHECK(A.core->cancel(ids[2], &err));
    }
    radio_a->hold(false);
    // Everything else lands.
    CHECK(wait_for([&] { return A.core->queue(10).empty(); }, 60000));
    CHECK(wait_for([&] {
        for (auto& m : M)
            if (static_cast<int>(arr(call(*m.core, {{"cmd", "parts_list"}, {"all", "Y"}}), "sets").size()) < 2) return false;
        return true;
    }, 60000));

    // Twenty members ask, one after another; STA-01 asks twice.
    for (int i = 1; i <= 5; ++i) ask(M[i - 1], A, "tape");
    ask(M[0], A, "tape");
    for (int i = 6; i <= 20; ++i) ask(M[i - 1], A, "bundle");

    // Pooled: 21 asks for 25 parts are 7 sends. Parts 2 and 3 of the bundle
    // go once each, to the whole net; the tape parts go to their members.
    r = call(*A.core, {{"cmd", "parts_queue"}});
    CHECK(ok(r) && num(r, "sends") == 7 && num(r, "stations") == 20 && arr(r, "flagged").empty());
    CHECK(str(r, "summary").find("7 sends for 20 stations") != std::string::npos);
    int to_all = 0;
    for (auto& it : arr(r, "items")) {
        if (str(it, "to") == "ALL") {
            ++to_all;
            CHECK(str(it, "kind") == "bundle" && arr(it, "asked_by").size() == (num(it, "part") == 2 ? 12u : 8u));
            CHECK(num(it, "part") == 2 || num(it, "part") == 3);
        } else {
            CHECK(str(it, "kind") == "tape" && arr(it, "asked_by").size() == 1);
        }
    }
    CHECK(to_all == 2);
    std::cerr << "  " << str(r, "summary") << "\n";
    // Nothing has gone: the box keys up only when the operator says.
    for (int i = 0; i < 5; ++i) CHECK(str(waiting(*M[i].core), "state") == "waiting");

    // Not while EMCON is on.
    CHECK(ok(call(*A.core, {{"cmd", "emcon"}, {"on", "Y"}})));
    r = form(*A.core, "PARTS_QUEUE", {{"queue", "ALL"}});
    CHECK(!ok(r) && A.core->queue(10).empty());
    CHECK(ok(call(*A.core, {{"cmd", "emcon"}, {"on", "N"}})));

    // One press.
    r = show(*A.core, "PARTS_QUEUE");
    CHECK(choice_values(r, "queue") == std::vector<std::string>{"ALL"});
    r = form(*A.core, "PARTS_QUEUE", {{"queue", "ALL"}});
    CHECK(ok(r) && num(sub(r, "result"), "sends") == 7 && num(sub(r, "result"), "stations") == 20);
    CHECK(num(call(*A.core, {{"cmd", "parts_queue"}}), "sends") == 0);
    // Every member opens its tape and loads the bundle.
    CHECK(wait_for([&] {
        for (auto& m : M)
            if (!arr(call(*m.core, {{"cmd", "parts_list"}}), "sets").empty()) return false;
        return true;
    }, 120000));
    for (auto& m : M) {
        JsonValue l = call(*m.core, {{"cmd", "parts_list"}});
        if (!arr(l, "sets").empty())
            std::cout << "  " << m.call << " still waits: " << ApiServer::handle(*m.core, line_of({{"cmd", "parts_list"}})).substr(0, 400) << "\n";
    }
    for (auto& m : M) {
        std::string tape, bundle;
        JsonValue l = call(*m.core, {{"cmd", "parts_list"}, {"all", "Y"}});
        for (auto& e : arr(l, "sets"))
            (str(e, "kind") == "tape" ? tape : bundle) = str(e, "id");
        r = call(*m.core, {{"cmd", "key_open"}, {"id", tape}});
        if (!(ok(r) && num(r, "added") == 31))
            std::cout << "  " << m.call << " key_open " << tape << ": " << ApiServer::handle(*m.core, line_of({{"cmd", "key_open"}, {"id", tape}})).substr(0, 300) << "\n";
        CHECK(ok(r) && num(r, "added") == 31);
        r = call(*m.core, {{"cmd", "bundle_load"}, {"id", bundle}});
        CHECK(ok(r) && yes(r, "loaded"));
        CHECK(m.ops->book_get("PRACTICE-001") && m.ops->book_get("PRACTICE-260"));
    }
    int bcast = 0;
    for (auto& m : A.core->list(1000))
        if (m.direction == "out" && m.dst == "ALL") ++bcast;
    CHECK(bcast == 2);

    // A member that keeps asking is flagged, not served a third time.
    Box& X = M[20];  // STA-21
    radio_a->hold(true);
    r = form(*A.core, "NET_TAPE", {{"net", "flag net"}, {"members", "STA-21"}, {"days", "31"}});
    std::vector<std::string> xids;
    ids_of(arr(sub(r, "result"), "sealed")[0], "ids", xids);
    std::string err;
    CHECK(xids.size() >= 2 && A.core->cancel(xids.back(), &err));
    radio_a->hold(false);
    CHECK(wait_for([&] { return num(waiting(*X.core), "have") == static_cast<double>(xids.size() - 1); }));
    for (int round = 1; round <= 2; ++round) {
        ask(X, A, "tape");
        radio_a->hold(true);
        r = call(*A.core, {{"cmd", "parts_queue_send"}});
        CHECK(ok(r) && num(r, "sends") == 1);
        CHECK(A.core->cancel(str(arr(r, "sent")[0], "id"), &err));  // lost on the air again
        radio_a->hold(false);
    }
    ask(X, A, "tape");
    r = call(*A.core, {{"cmd", "parts_queue"}});
    CHECK(num(r, "sends") == 0 && arr(r, "flagged").size() == 1 && str(arr(r, "flagged")[0], "station") == "STA-21");
    CHECK(!ok(call(*A.core, {{"cmd", "parts_queue_send"}})));  // nothing to send
    // RESENDS: the flagged ask is a row of its own. A fourth ask is dropped.
    {
        std::string flagged_rq = str(arr(r, "flagged")[0], "request");
        bool row = false;
        for (auto& x : arr(r, "rows"))
            if (str(x, "request") == flagged_rq) row = str(x, "state") == "FLAGGED" && str(x, "station") == "STA-21";
        CHECK(row);
        ask(X, A, "tape");
        r = call(*A.core, {{"cmd", "parts_queue"}});
        std::string again;
        for (auto& f : arr(r, "flagged"))
            if (str(f, "request") != flagged_rq) again = str(f, "request");
        CHECK(!again.empty());
        CHECK(!ok(call(*A.core, {{"cmd", "parts_queue_drop"}, {"request", "NO-SUCH"}})));
        r = call(*A.core, {{"cmd", "parts_queue_drop"}, {"request", again}});
        CHECK(ok(r) && num(r, "dropped") == 1 && str(r, "station") == "STA-21");
        CHECK(!ok(call(*A.core, {{"cmd", "parts_queue_drop"}, {"request", again}})));  // nothing left
        CHECK(!ok(call(*A.core, {{"cmd", "parts_queue_send"}, {"request", again}})));
        r = call(*A.core, {{"cmd", "parts_queue"}});
        CHECK(arr(r, "flagged").size() == 1);
        bool dropped = false;
        for (auto& x : arr(r, "rows"))
            if (str(x, "request") == again) dropped = str(x, "state") == "DROPPED";
        CHECK(dropped);
    }
    // The operator looks, and sends it by hand.
    r = show(*A.core, "PARTS_REPEAT");
    std::string flagged_id;
    if (auto f = field_of(r, "id"))
        for (auto& c : arr(*f, "choices"))
            if (str(c, "label").find("FLAGGED") == 0) flagged_id = str(c, "value");
    CHECK(!flagged_id.empty());
    CHECK(ok(form(*A.core, "PARTS_REPEAT", {{"id", flagged_id}})));
    CHECK(wait_for([&] { return arr(call(*X.core, {{"cmd", "parts_list"}}), "sets").empty(); }));
    CHECK(arr(call(*A.core, {{"cmd", "parts_queue"}}), "flagged").empty());

    for (auto& m : M) m.core->stop();
    A.core->stop();
}

// Random cipher groups can look like a number and a unit ("KC" after a
// number group). A sealed block's group lines must not trip the clear-text
// frequency guard, or a sealed tape fails to send now and then; the same
// line in clear text still does.
static bool says_frequency(Core& c, const std::string& text) {
    JsonValue r = call(c, {{"cmd", "compose_check"}, {"template", "FREE_FORM"}, {"procedure", "ACP127_PLAINDRESS"},
                            {"prec", "R"}, {"routing", "STA-D"}, {"originator", c.station()},
                            {"from", "MARS STATION " + c.station()}, {"to", "STA-D"}, {"to_station", "STA-D"},
                            {"text", text}});
    for (auto& i : arr(r, "issues"))
        if (str(i, "text").find("frequency") != std::string::npos) return true;
    return false;
}

static void groups_not_frequencies(Core& c) {
    std::string tripping;
    unsigned seed = 7;
    for (int tries = 0; tries < 200000 && tripping.empty(); ++tries) {
        std::string bytes(64, '\0');
        for (auto& b : bytes) b = static_cast<char>((seed = seed * 1103515245u + 12345u) >> 16);
        auto g = codress::to_groups(bytes);
        for (size_t i = 0; i + 7 <= g.size() && tripping.empty(); i += 7) {
            std::string line;
            for (size_t k = i; k < i + 7; ++k) line += (line.empty() ? "" : " ") + g[k];
            if (compose::looks_like_frequency(line)) tripping = line;
        }
    }
    CHECK(!tripping.empty());
    std::cerr << "  a group line the guard would trip on: found\n";
    CHECK(!says_frequency(c, "PNA SEALED TAPE 1\nTO STA-D\nGR 7\n" + tripping + "\nEND PNA SEALED TAPE"));
    CHECK(says_frequency(c, "NOTES\n" + tripping + "\nEND"));
    CHECK(says_frequency(c, "PNA SEALED TAPE 1\nGR 7\n" + tripping + "\nMEET ON 10.000 MHZ\nEND"));
}

// The cap and the net slot: at most 2 resends an hour here.
static void pacing(const fs::path& dir) {
    g_now = static_cast<int64_t>(std::time(nullptr));
    auto mc = std::make_unique<LoopbackModem>(0);
    auto md = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*mc, *md);
    LoopbackModem* radio_c = mc.get();
    Box C = out_of_can(dir / "pc", "NCS-C", std::move(mc), 72 * 3600, 2);
    Box D = out_of_can(dir / "pd", "STA-D", std::move(md));
    pair_up(C, D);
    groups_not_frequencies(*C.core);
    radio_c->hold(true);
    JsonValue r = form(*C.core, "NET_TAPE", {{"net", "slow net"}, {"members", "STA-D"}, {"days", "62"}});
    std::vector<std::string> ids;
    ids_of(arr(sub(r, "result"), "sealed")[0], "ids", ids);
    const double of = static_cast<double>(ids.size());
    CHECK(ids.size() >= 4);
    std::string err;
    for (int n : {2, 3, 4}) CHECK(C.core->cancel(ids[n - 1], &err));
    radio_c->hold(false);
    CHECK(wait_for([&] { return num(waiting(*D.core), "have") == of - 3; }));
    ask(D, C, "tape");
    r = call(*C.core, {{"cmd", "parts_queue"}});
    CHECK(num(r, "sends") == 3 && num(r, "room") == 2 && num(r, "per_hour") == 2);
    // RESENDS: one row for D's ask, the three parts, ready to go.
    CHECK(arr(r, "rows").size() == 1);
    const std::string rq = str(arr(r, "rows")[0], "request");
    {
        const JsonValue& row = arr(r, "rows")[0];
        CHECK(str(row, "station") == "STA-D" && str(row, "parts") == "2 3 4" && str(row, "state") == "WAIT");
        CHECK(str(row, "kind") == "tape" && !str(row, "message").empty() && num(row, "age_s") >= 0);
        CHECK(C.core->get(str(row, "message")) && C.core->get(str(row, "message"))->dst == "STA-D");
    }
    // The status strip's cap: nothing counted yet.
    r = call(*C.core, {{"cmd", "status"}});
    CHECK(!yes(r, "emcon"));
    CHECK(num(sub(r, "cap"), "per_hour") == 2 && num(sub(r, "cap"), "used") == 0 &&
          num(sub(r, "cap"), "room") == 2 && num(sub(r, "cap"), "resets_at") == 0);
    // Both sources are named; machine mode is off, so its cap isn't in the total.
    CHECK(arr(sub(r, "cap"), "covers").size() == 2 && arr(sub(r, "cap"), "sources").size() == 2);
    CHECK(str(arr(sub(r, "cap"), "sources")[0], "source") == "parts_resend" &&
          str(arr(sub(r, "cap"), "sources")[1], "source") == "bbs_auto" &&
          !yes(arr(sub(r, "cap"), "sources")[1], "active"));
    // Now: two go, the third waits for the cap.
    r = form(*C.core, "PARTS_QUEUE", {{"queue", "ALL"}, {"when", "NOW"}});
    CHECK(ok(r) && num(sub(r, "result"), "sends") == 2 && num(sub(r, "result"), "left_for_cap") == 1);
    CHECK(!str(sub(r, "result"), "room_at").empty());
    CHECK(wait_for([&] { return num(waiting(*D.core), "have") == of - 1; }));
    r = call(*C.core, {{"cmd", "parts_queue"}});
    CHECK(num(r, "sends") == 1 && num(r, "room") == 0);
    CHECK(str(arr(r, "rows")[0], "state") == "HELD-CAP");
    // Sending the row now obeys the cap: nothing goes.
    r = call(*C.core, {{"cmd", "parts_queue_send"}, {"request", rq}});
    CHECK(ok(r) && num(r, "sends") == 0 && num(r, "left_for_cap") == 1 && str(r, "request") == rq);
    CHECK(ok(call(*C.core, {{"cmd", "emcon"}, {"on", "Y"}})));
    CHECK(str(arr(call(*C.core, {{"cmd", "parts_queue"}}), "rows")[0], "state") == "HELD-EMCON");
    CHECK(!ok(call(*C.core, {{"cmd", "parts_queue_send"}, {"request", rq}})));
    CHECK(ok(call(*C.core, {{"cmd", "emcon"}, {"on", "N"}})));
    // Status shows the cap spent, and when the rolling hour opens room again.
    {
        const double t = static_cast<double>(std::time(nullptr));
        JsonValue st = call(*C.core, {{"cmd", "status"}});
        const double at = num(sub(st, "cap"), "resets_at");
        CHECK(num(sub(st, "cap"), "used") == 2 && num(sub(st, "cap"), "room") == 0);
        CHECK(at > t + 3500 && at <= t + 3601);
        // Machine mode on: its own cap joins the one number.
        CHECK(ok(call(*C.core, {{"cmd", "bbs_set"}, {"machine_mode", "true"}, {"auto_per_hour", "5"}})));
        JsonValue st2 = call(*C.core, {{"cmd", "status"}});
        CHECK(num(sub(st2, "cap"), "per_hour") == 7 && num(sub(st2, "cap"), "used") == 2 &&
              num(sub(st2, "cap"), "room") == 5 && num(sub(st2, "cap"), "resets_at") == at);
        CHECK(yes(arr(sub(st2, "cap"), "sources")[1], "active") && num(arr(sub(st2, "cap"), "sources")[1], "per_hour") == 5);
        CHECK(ok(call(*C.core, {{"cmd", "bbs_set"}, {"machine_mode", "false"}})));
        CHECK(ok(call(*C.core, {{"cmd", "emcon"}, {"on", "Y"}})) && yes(call(*C.core, {{"cmd", "status"}}), "emcon"));
        CHECK(ok(call(*C.core, {{"cmd", "emcon"}, {"on", "N"}})) && !yes(call(*C.core, {{"cmd", "status"}}), "emcon"));
    }
    // The rest waits for the evening net, three hours on: held till then.
    std::time_t later = std::time(nullptr) + 3 * 3600;
    std::tm tm{};
    gmtime_r(&later, &tm);
    char hhmm[8];
    std::snprintf(hhmm, sizeof hhmm, "%02d%02d", tm.tm_hour, tm.tm_min);
    CHECK(ok(form(*C.core, "SCHEDULE", {{"name", "evening net"}, {"days", "DAILY"}, {"time", hhmm}})));
    // The row, sent for the net slot (RESENDS s, then N).
    r = call(*C.core, {{"cmd", "parts_queue_send"}, {"request", rq}, {"when", "NET"}, {"net", "EVENING NET"}});
    CHECK(ok(r) && yes(r, "held") && num(r, "sends") == 1);
    std::string held_id = str(arr(r, "sent")[0], "id");
    CHECK(str(arr(call(*C.core, {{"cmd", "parts_queue"}}), "rows")[0], "state") == "HELD-NET");
    CHECK(C.core->get(held_id) && C.core->get(held_id)->state == "held");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(num(waiting(*D.core), "have") == of - 1);  // not on the air yet
    // The net slot comes: the held part goes out by itself, and D opens the tape.
    for (auto& a : C.core->part_asks())
        if (a.sent_id == held_id) C.core->set_part_ask(a.request_id, a.part, "held", a.sent_id, now_utc() - 1);
    CHECK(wait_for([&] { return arr(call(*D.core, {{"cmd", "parts_list"}}), "sets").empty(); }));
    r = call(*D.core, {{"cmd", "key_open"}, {"id", str(waiting(*D.core, "Y"), "id")}});
    CHECK(ok(r) && num(r, "added") == 62);
    C.core->stop();
    D.core->stop();
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_parts_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    format();
    drill(dir);
    rally(dir);
    pacing(dir);
    fs::remove_all(dir);
    return check::report("test_parts");
}
