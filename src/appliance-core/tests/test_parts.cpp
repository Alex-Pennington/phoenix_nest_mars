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
// Calls and data are made up. The frequencies are public time-signal ones.
#include "../src/api_server.h"
#include "../src/channels.h"
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
                      int hold_s = 72 * 3600) {
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

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_parts_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    format();
    drill(dir);
    fs::remove_all(dir);
    return check::report("test_parts");
}
