// Forms from templates (docs/forms.md).
//
// The drill: three boxes out of the can, a first-time operator at each, and
// nothing but form calls from the first start to a working net. Net control
// (NCS-A) and STA-B are on the air; STA-C comes in person, with a stick. No
// file is edited: the only files are the ones the forms write to the stick.
// Then a form is changed on a box, takes effect at once, and travels signed
// in a bundle.
//
// Calls and data are made up. The frequencies are public time-signal ones.
#include "../src/api_server.h"
#include "../src/channels.h"
#include "../src/core.h"
#include "../src/first_start.h"
#include "../src/forms.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/station.h"
#include "check.h"
#include "services_fixture.h"

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
static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The member's form calls, as gale-term sends them.
static JsonValue form(Core& c, const std::string& id, std::vector<std::pair<std::string, std::string>> values) {
    std::string line = "{\"cmd\":\"form_submit\",\"service\":\"mars\",\"form\":\"" + id + "\"";
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
static Box out_of_can(const fs::path& dir, const std::string& typed, std::unique_ptr<ModemDriver> modem) {
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
    b.core = std::make_unique<Core>(CoreConfig{got, base + ".db", 3, 1, true}, std::move(modem));
    b.core->set_services(test_services());
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

// ---------------------------------------------------------------- the templates

static void builtins() {
    std::set<std::string> ids;
    for (auto& [id, text] : forms::builtin_texts()) {
        forms::Template t;
        std::string err;
        bool parsed_ok = forms::parse(text, t, &err);
        if (!parsed_ok) std::cerr << id << ": " << err << "\n";
        CHECK(parsed_ok && t.id == id && !t.title.empty() && !t.summary.empty());
        for (auto& f : t.fields) CHECK(!f.help.empty() || f.kind != "textblock");
        ids.insert(id);
    }
    // Every document a box needs from level zero has a form.
    for (auto* need : {"SETUP", "CARD_SEND", "CARD_SAVE", "CARD_LOAD", "CARD_CHECK", "BOOK", "CHANNEL", "SCHEDULE",
                       "BUNDLE_MAKE", "BUNDLE_SEND", "BUNDLE_LOAD", "KEYMASTER", "NET_TAPE", "KEY_OPEN",
                       "HANDOUT_LOAD", "BACKUP", "RESTORE", "FORM_EDIT", "FORM_RESET"})
        CHECK(ids.count(need));
}

static void language() {
    forms::Template t;
    std::string err;
    const std::string head = "FORM T\nTITLE T\n";
    // The masks, as Response Collector writes them.
    CHECK(forms::parse(head +
                           "ACTION book_put\n"
                           "FIELD a Free(X)\nFIELD b Block(TEXT)\nFIELD c Yes or no(Y,N) default=N\n"
                           "FIELD d Pick(ONE,TWO)\nFIELD e Cards(@cards+)\nFIELD f Start(HHMM)Z\nFIELD g Count(###)\n"
                           "FIELD h Day(DDHHMM)\n",
                       t, &err));
    CHECK(t.fields.size() == 8 && t.fields[0].kind == "text" && t.fields[1].kind == "textblock" &&
          t.fields[2].kind == "yesno" && t.fields[3].kind == "choice" && t.fields[3].choices.size() == 2 &&
          t.fields[4].source == "@cards" && t.fields[4].multi && t.fields[5].kind == "time" &&
          t.fields[5].suffix == "Z" && t.fields[6].kind == "pattern" && t.fields[7].kind == "time");
    // What a template can't do.
    struct Bad {
        std::string text, says;
    };
    for (auto& b : std::vector<Bad>{
             {head + "ACTION key_zeroize\n", "can't run key_zeroize"},
             {head + "ACTION send\n", "can't run send"},
             {head + "ACTION card_check\nSET keymaster Y\nFIELD station S(X)\n", "member's own choice"},
             {head + "ACTION keymaster_accept\nSET fingerprint X\n", "member's own choice"},
             {head + "ACTION book_put\nFIELD a A(@everything)\n", "no such list"},
             {head + "ACTION book_put WHEN z=Y\nFIELD a A(X)\n", "WHEN names no field"},
             {head + "ACTION book_put\nFIELD a A(X) again=q\n", "again= names no field"},
             {head + "ACTION book_put\nFIELD a A(X) secret default=1\n", "secret field"},
             {head + "ACTION book_put\nFIELD cmd A(X)\n", "field key"},
             {head + "FIELD a A(X)\n", "no ACTION"},
             {"TITLE T\nFORM T\n", "starts with FORM"},
             {head + "ACTION book_put\nFIELD a A\n", "mask in ()"},
             {head + "ACTION book_put\nBEGIN_HELP\nA:\nno end\n", "has no END"}}) {
        err.clear();
        CHECK(!forms::parse(b.text, t, &err) && err.find(b.says) != std::string::npos);
        if (err.find(b.says) == std::string::npos) std::cerr << "said: " << err << " wanted: " << b.says << "\n";
    }
    // Packed for a bundle: radio-safe lines, and back.
    std::string text = forms::builtin_texts()[0].second;
    auto lines = forms::pack(text);
    CHECK(!lines.empty());
    for (auto& l : lines) {
        CHECK(l.size() <= 56);
        for (char c : l) CHECK(std::isupper(static_cast<unsigned char>(c)) || (c >= '2' && c <= '7'));
    }
    std::string back;
    CHECK(forms::unpack(lines, back) && back == text);
    lines.back()[0] = lines.back()[0] == 'A' ? 'B' : 'A';
    CHECK(!forms::unpack(lines, back) || back != text);
}

// ---------------------------------------------------------------- the drill

// How each answer reaches the call, shown before form_submit runs it: the
// field words nosend, fill and as= on form_get, and the defaults form_check
// says it will fill in. form_submit then sends exactly that.
static void fill_rules(const fs::path& dir) {
    fs::create_directories(dir);
    forms::Library lib(dir.string());
    std::string result, got, err;
    CHECK(lib.put("FORM FILLS\nTITLE Fill rules\nSUMMARY The field words a screen shows.\n"
                  "ACTION book_put\n"
                  "FIELD name Name(X) required upper\n"
                  "FIELD kind Kind(STATION,COLLECTIVE) default=STATION\n"
                  "FIELD port Port(1,2) default=1 fill\n"
                  "FIELD sure Sure(Y,N) nosend\n"
                  "FIELD label Label(X) upper as=note\n"
                  "FIELD word Word(X) secret\n",
                  "edited here", &result, &got, &err));
    std::vector<std::string> sent;
    forms::Context ctx;
    ctx.run = [&](const std::string& line) {
        sent.push_back(line);
        return std::string(R"({"ok":true})");
    };
    auto run = [&](std::map<std::string, std::string> req) {
        std::string cmd = req["cmd"];
        req.erase("cmd");
        return parsed(*forms::handle(lib, ctx, cmd, req));
    };

    // form_get: every field says whether it's sent, whether a blank takes the
    // default, and the name it goes under when that isn't its key.
    JsonValue r = run({{"cmd", "form_get"}, {"form", "FILLS"}});
    CHECK(ok(r) && arr(r, "fields").size() == 6);
    const JsonValue* name = field_of(r, "name");
    const JsonValue* kind = field_of(r, "kind");
    const JsonValue* port = field_of(r, "port");
    const JsonValue* sure = field_of(r, "sure");
    const JsonValue* label = field_of(r, "label");
    const JsonValue* word = field_of(r, "word");
    CHECK(name && kind && port && sure && label && word);
    if (!(name && kind && port && sure && label && word)) return;
    for (auto* f : {name, kind, port, sure, label, word}) {
        CHECK(f->get("nosend") && f->get("nosend")->type == JsonValue::Type::Bool);
        CHECK(f->get("fill") && f->get("fill")->type == JsonValue::Type::Bool);
    }
    CHECK(!yes(*name, "nosend") && !yes(*name, "fill") && !name->get("as"));
    CHECK(!yes(*kind, "fill") && str(*kind, "default") == "STATION");
    CHECK(yes(*port, "fill") && str(*port, "default") == "1");
    CHECK(yes(*sure, "nosend") && !yes(*sure, "fill"));
    CHECK(str(*label, "as") == "note" && !yes(*label, "nosend"));
    CHECK(!yes(*word, "fill") && !word->get("default") && !word->get("value"));
    // The keys that were there before are all still there.
    for (auto* k : {"key", "label", "kind", "mask", "suffix", "required", "upper", "secret", "multi", "value", "help"})
        CHECK(name->get(k));
    CHECK(arr(r, "actions").size() == 1 && str(arr(r, "actions")[0], "cmd") == "book_put");

    // form_check: kind left out and port sent blank both take their default;
    // label sent blank has no default, so it stays blank.
    r = run({{"cmd", "form_check"}, {"form", "FILLS"}, {"name", "sta-b"}, {"port", ""}, {"sure", "y"},
             {"label", ""}});
    CHECK(ok(r) && yes(r, "ready") && arr(r, "problems").empty());
    std::map<std::string, std::string> filled;
    for (auto& f : arr(r, "filled")) filled[str(f, "field")] = str(f, "value");
    CHECK(filled.size() == 2 && filled["kind"] == "STATION" && filled["port"] == "1");
    CHECK(sent.empty());  // form_check runs nothing

    // A blank on a field without `fill` is left as is, not filled.
    r = run({{"cmd", "form_check"}, {"form", "FILLS"}, {"name", "sta-b"}, {"kind", ""}, {"port", "2"}});
    filled.clear();
    for (auto& f : arr(r, "filled")) filled[str(f, "field")] = str(f, "value");
    CHECK(filled.empty());

    // form_submit sends what form_check said: the defaults filled in, sure
    // kept back, label under its other name.
    r = run({{"cmd", "form_submit"}, {"form", "FILLS"}, {"name", "sta-b"}, {"port", ""}, {"sure", "y"},
             {"label", "practice"}, {"word", "lima"}});
    CHECK(ok(r) && str(r, "action") == "book_put" && sent.size() == 1);
    if (sent.size() == 1) {
        JsonValue q;
        CHECK(parse_json(sent[0], q));
        CHECK(str(q, "cmd") == "book_put" && str(q, "name") == "STA-B" && str(q, "kind") == "STATION" &&
              str(q, "port") == "1" && !q.get("sure") && !q.get("label") && str(q, "note") == "PRACTICE" &&
              str(q, "word") == "lima");
    }
}

static void drill(const fs::path& dir) {
    g_now = static_cast<int64_t>(std::time(nullptr));
    fs::path stick = dir / "stick";
    fs::create_directories(stick);  // the stick, as the box mounts it
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    Box A = out_of_can(dir / "a", "ncs-a", std::move(ma));
    Box B = out_of_can(dir / "b", "STA-B", std::move(mb));
    Box C = out_of_can(dir / "c", " sta-c ", std::make_unique<LoopbackModem>(0));
    CHECK(A.call == "NCS-A" && C.call == "STA-C");
    JsonValue r;

    // Net control enters its frequency card by hand, then the book and the net.
    r = form(*A.core, "CHANNEL", {{"name", "p1"}, {"frequency", "5000.0"}, {"use", "PRIMARY"}});
    CHECK(ok(r) && str(sub(r, "result"), "result") == "added" && str(sub(r, "result"), "mode") == "USB");
    CHECK(arr(r, "next_forms").size() == 2 && str(arr(r, "next_forms")[0], "form") == "CHANNEL");
    r = form(*A.core, "CHANNEL", {{"name", "P2"}, {"frequency", "10"}, {"unit", "mhz"}, {"use", "SECONDARY"}});
    CHECK(ok(r));
    r = form(*A.core, "CHANNEL", {{"name", "S1"}, {"frequency", "2500"}, {"mode", "ale"}, {"use", "SCAN"}});
    CHECK(ok(r));
    // A slip: the form names the field and repeats no value.
    r = form(*A.core, "CHANNEL", {{"name", "P3"}, {"frequency", "99"}, {"unit", "KHZ"}});
    CHECK(!ok(r) && str(arr(r, "problems")[0], "field") == "frequency" && !A.ops->channels().get("P3"));
    r = form(*A.core, "CHANNEL", {{"name", "P 3"}, {"frequency", "7000"}, {"mode", "SSB"}});
    CHECK(!ok(r) && arr(r, "problems").size() == 2);
    // A frequency left blank keeps the one a channel holds (asks 27, 29), so
    // the form doesn't ask for it; a new channel without one is refused by
    // channel_put itself, and nothing is added.
    r = call(*A.core, {{"cmd", "form_check"}, {"form", "CHANNEL"}, {"name", "P4"}});
    CHECK(ok(r) && yes(r, "ready"));
    r = form(*A.core, "CHANNEL", {{"name", "P4"}});
    CHECK(!ok(r) && str(r, "error").find("frequency") != std::string::npos && !A.ops->channels().get("P4"));
    // Opening P1 again fills in what's held, never its frequency.
    r = show(*A.core, "CHANNEL", "name", "P1");
    CHECK(ok(r) && yes(r, "prefilled") && str(*field_of(r, "use"), "value") == "PRIMARY");
    CHECK(yes(*field_of(r, "frequency"), "has_value") && !field_of(r, "frequency")->get("value"));
    CHECK(A.ops->channels().get("P2")->hz == 10000000);

    for (auto& [name, call] : std::vector<std::pair<std::string, std::string>>{
             {"NCS-A", "NCS-A"}, {"STA-B", "STA-B"}, {"STA-C", "STA-C"}}) {
        r = form(*A.core, "BOOK", {{"name", name}, {"call", call}, {"note", "practice"}});
        CHECK(ok(r));
    }
    r = form(*A.core, "BOOK", {{"name", "PRACTICE NET"}, {"kind", "collective"}, {"members", "STA-B, STA-C"}});
    CHECK(ok(r) && A.ops->book_get("PRACTICE NET") && A.ops->book_get("PRACTICE NET")->members.size() == 2);
    r = show(*A.core, "BOOK", "name", "PRACTICE NET");
    CHECK(str(*field_of(r, "kind"), "value") == "COLLECTIVE" && str(*field_of(r, "members"), "value") == "STA-B, STA-C");
    r = form(*A.core, "SCHEDULE", {{"name", "PRACTICE NET"}, {"days", "TUE,THU"}, {"time", "2460"}});
    CHECK(!ok(r) && str(arr(r, "problems")[0], "field") == "time");
    r = show(*A.core, "SCHEDULE");
    CHECK(choice_values(r, "channel") == (std::vector<std::string>{"P1", "P2", "S1"}));
    r = form(*A.core, "SCHEDULE", {{"name", "Practice Net"},
                                    {"days", "tue,thu"},
                                    {"time", "1900"},
                                    {"channel", "p1"},
                                    {"ncs", "NCS-A"},
                                    {"roster", "PRACTICE NET"}});
    CHECK(ok(r) && A.ops->sched_list().size() == 1 && A.ops->sched_list()[0].channel == "P1");

    // Cards. B sends its card over the air; A takes it and checks it by voice.
    r = form(*B.core, "CARD_SEND", {{"to", "NCS-A"}});
    CHECK(ok(r));
    std::string cid = pick(*A.core, "CARD_LOAD", "id");
    CHECK(!cid.empty());
    r = form(*A.core, "CARD_LOAD", {{"id", cid}});
    CHECK(ok(r) && str(sub(r, "result"), "station") == "STA-B");
    r = show(*A.core, "CARD_CHECK");
    CHECK(choice_values(r, "station") == std::vector<std::string>{"STA-B"});
    r = form(*A.core, "CARD_CHECK", {{"station", "STA-B"}, {"fingerprint", "AAAA BBBB CCCC DDDD"}});
    CHECK(!ok(r) && !A.core->card_verified("STA-B"));
    r = form(*A.core, "CARD_CHECK", {{"station", "STA-B"}, {"fingerprint", B.fp}});
    CHECK(ok(r) && A.core->card_verified("STA-B") && !A.core->card_keymaster("STA-B"));
    // And back: A's card to B, which checks it by voice.
    CHECK(ok(form(*A.core, "CARD_SEND", {{"to", "STA-B"}})));
    cid = pick(*B.core, "CARD_LOAD", "id");
    CHECK(ok(form(*B.core, "CARD_LOAD", {{"id", cid}})));
    CHECK(ok(form(*B.core, "CARD_CHECK", {{"station", "NCS-A"}, {"fingerprint", A.fp}})));
    // C meets net control in person: its card goes on the stick.
    r = form(*C.core, "CARD_SAVE", {{"out", (stick / "STA-C-CARD.TXT").string()}});
    CHECK(ok(r) && fs::exists(stick / "STA-C-CARD.TXT"));
    CHECK(ok(form(*A.core, "CARD_LOAD", {{"file", (stick / "STA-C-CARD.TXT").string()}})));
    CHECK(ok(form(*A.core, "CARD_CHECK", {{"station", "STA-C"}, {"fingerprint", C.fp}})));

    // The startup bundle: B's own copy over the air, frequencies sealed to it.
    r = form(*A.core, "BUNDLE_SEND", {{"to", "STA-B"}, {"label", "practice region"}});
    CHECK(ok(r) && str(arr(sub(r, "result"), "sent")[0], "frequencies") == "sealed");
    std::string bid = pick(*B.core, "BUNDLE_LOAD", "id");
    CHECK(!bid.empty());
    r = form(*B.core, "BUNDLE_LOAD", {{"id", bid}});
    CHECK(ok(r) && yes(sub(r, "result"), "loaded") && str(sub(r, "result"), "frequencies") == "opened");
    CHECK(num(sub(r, "result"), "frequencies_added") == 3);
    CHECK(B.ops->channels().get("P1") && B.ops->channels().get("P1")->hz == 5000000);
    CHECK(B.ops->sched_list().size() == 1 && B.ops->book_get("PRACTICE NET"));
    // C's copy on the stick, made for C: C's box asks first, as it hasn't checked A.
    r = form(*A.core, "BUNDLE_MAKE", {{"for", "STA-C"}, {"out", (stick / "region.pnb").string()}});
    CHECK(ok(r) && str(sub(r, "result"), "frequencies") == "sealed");
    r = form(*C.core, "BUNDLE_LOAD", {{"file", (stick / "region.pnb").string()}});
    CHECK(ok(r) && yes(sub(r, "result"), "needs_yes") && C.ops->book_list().empty());
    r = form(*C.core, "BUNDLE_LOAD", {{"file", (stick / "region.pnb").string()}, {"accept", "Y"}});
    CHECK(ok(r) && str(sub(r, "result"), "frequencies") == "opened" && C.ops->channels().get("S1"));

    // Key tape: sealed to B over the air, and handouts on the stick.
    r = form(*A.core, "NET_TAPE", {{"net", "practice net"},
                                    {"members", "STA-B STA-C"},
                                    {"days", "7"},
                                    {"handouts", "Y"},
                                    {"out", stick.string()}});
    CHECK(ok(r) && arr(sub(r, "result"), "no_card").empty());
    CHECK(fs::exists(stick / "HANDOUT-STA-C.TXT") && fs::exists(stick / "HANDOUT-STA-B.TXT"));
    // B hasn't chosen a keymaster: the tape that came in doesn't open yet.
    std::string tid = pick(*B.core, "KEY_OPEN", "id");
    CHECK(!tid.empty());
    r = form(*B.core, "KEY_OPEN", {{"id", tid}});
    CHECK(!ok(r) && B.keys->for_sending().empty());
    // B's own choice, with the fingerprint read by voice.
    r = form(*B.core, "KEYMASTER", {{"station", "NCS-A"}, {"answer", "accept"}});
    CHECK(!ok(r) && !B.core->card_keymaster("NCS-A"));
    r = form(*B.core, "KEYMASTER", {{"station", "NCS-A"}, {"answer", "ACCEPT"}, {"fingerprint", A.fp}});
    CHECK(ok(r) && str(r, "action") == "keymaster_accept" && B.core->card_keymaster("NCS-A"));
    r = form(*B.core, "KEY_OPEN", {{"id", tid}});
    CHECK(ok(r) && num(sub(r, "result"), "added") == 7);
    // C loads its handout from the stick; nothing opens until C chooses.
    r = form(*C.core, "HANDOUT_LOAD", {{"file", (stick / "HANDOUT-STA-C.TXT").string()}});
    CHECK(ok(r) && yes(sub(r, "result"), "waiting_on_member") && C.keys->for_sending().empty());
    r = show(*C.core, "KEYMASTER");
    CHECK(choice_values(r, "station") == std::vector<std::string>{"NCS-A"});
    r = form(*C.core, "KEYMASTER", {{"station", "NCS-A"}, {"answer", "ACCEPT"}, {"fingerprint", A.fp}, {"how", "IN PERSON"}});
    CHECK(ok(r) && C.core->card_keymaster("NCS-A") && !C.keys->for_sending().empty());
    // A member may refuse, and the form runs the other call.
    r = form(*B.core, "KEYMASTER", {{"station", "NCS-A"}, {"answer", "REFUSE"}});
    CHECK(ok(r) && str(r, "action") == "keymaster_refuse" && !B.core->card_keymaster("NCS-A"));
    CHECK(ok(form(*B.core, "KEYMASTER", {{"station", "NCS-A"}, {"answer", "ACCEPT"}, {"fingerprint", A.fp}})));

    // Backups, to the stick, and one put back.
    r = form(*B.core, "BACKUP", {{"passphrase", "correct horse battery"}, {"again", "correct horse batterY"},
                                  {"out", (stick / "STA-B.BAK").string()}});
    CHECK(!ok(r) && str(arr(r, "problems")[0], "field") == "again" && !fs::exists(stick / "STA-B.BAK"));
    r = form(*B.core, "BACKUP", {{"passphrase", "short"}, {"again", "short"}, {"out", (stick / "STA-B.BAK").string()}});
    CHECK(!ok(r) && str(arr(r, "problems")[0], "field") == "passphrase");
    r = form(*B.core, "BACKUP", {{"passphrase", "correct horse battery"}, {"again", "correct horse battery"},
                                  {"out", (stick / "STA-B.BAK").string()}});
    CHECK(ok(r) && fs::exists(stick / "STA-B.BAK"));
    r = form(*B.core, "RESTORE", {{"file", (stick / "STA-B.BAK").string()}, {"passphrase", "wrong horse battery"}});
    CHECK(!ok(r));
    r = form(*B.core, "RESTORE", {{"file", (stick / "STA-B.BAK").string()}, {"passphrase", "correct horse battery"}});
    CHECK(ok(r) && str(sub(r, "result"), "fingerprint") == B.fp);

    // A working net: all three hold today's key, B and C the frequencies,
    // and B reads A's signed traffic.
    auto today = [](Box& b) {
        JsonValue k = call(*b.core, {{"cmd", "keys_list"}});
        for (auto& e : arr(k, "keys"))
            if (str(e, "state") == "current") return str(e, "check");
        return std::string();
    };
    CHECK(!today(A).empty() && today(A) == today(B) && today(A) == today(C));
    r = call(*A.core, {{"cmd", "compose_send"}, {"service", "mars"},
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

    // ---------------------------------------------------------- changing a form

    // Net control adds a use for the channel and a fixed net length, on the box.
    r = call(*A.core, {{"cmd", "form_template_get"}, {"form", "SCHEDULE"}});
    std::string text = str(r, "text");
    CHECK(ok(r) && yes(r, "builtin") && !yes(r, "changed") && str(r, "origin") == "built in");
    std::string mine = text;
    mine.replace(mine.find("TITLE Net schedule"), 18, "TITLE Region net schedule");
    mine.replace(mine.find("FIELD minutes"), 0, "SET exercise N\n");
    // Through the form, with a slip first.
    r = form(*A.core, "FORM_EDIT", {{"template", "SCHEDULE"}, {"text", "FORM SCHEDULE\nACTION key_zeroize\n"}});
    CHECK(!ok(r) && str(r, "error").find("key_zeroize") != std::string::npos);
    r = show(*A.core, "FORM_EDIT", "template", "SCHEDULE");
    CHECK(str(*field_of(r, "text"), "value") == text);
    r = form(*A.core, "FORM_EDIT", {{"template", "SCHEDULE"}, {"text", mine}});
    CHECK(ok(r) && str(sub(r, "result"), "result") == "replaced");
    // In use at once: no rebuild, no restart.
    r = show(*A.core, "SCHEDULE");
    CHECK(str(r, "title") == "Region net schedule" && str(r, "origin") == "edited here");
    CHECK(arr(r, "fixed").size() == 1 && str(arr(r, "fixed")[0], "key") == "exercise");
    // A new form of the region's own.
    std::string wx = "FORM WX_NET\nTITLE Weather net\nSUMMARY The weather net, every day at 1300Z.\nORDER 55\n"
                     "ACTION sched_put\nSET days DAILY\nSET time 1300\nFIELD name Net name(X) required upper\n"
                     "FIELD channel Channel(@channels) required\n";
    r = form(*A.core, "FORM_EDIT", {{"template", "WX_NET"}, {"text", wx}});
    CHECK(ok(r) && str(sub(r, "result"), "result") == "added");
    r = form(*A.core, "WX_NET", {{"name", "weather net"}, {"channel", "P2"}});
    CHECK(ok(r) && A.ops->sched_list().size() == 2);
    // The changed forms ride in the next bundle, signed.
    r = form(*A.core, "BUNDLE_SEND", {{"to", "STA-B"}});
    CHECK(ok(r));
    std::string bid2;
    CHECK(wait_for([&] {
        auto vs = choice_values(show(*B.core, "BUNDLE_LOAD"), "id");
        for (auto& v : vs)
            if (v != bid) bid2 = v;
        return !bid2.empty();
    }));
    r = form(*B.core, "BUNDLE_LOAD", {{"id", bid2}});
    CHECK(ok(r) && num(sub(r, "result"), "forms_added") == 1 && num(sub(r, "result"), "forms_replaced") == 1 &&
          num(sub(r, "result"), "forms_held") == 0);
    r = show(*B.core, "WX_NET");
    CHECK(ok(r) && str(r, "origin") == "from bundle NCS-A");
    CHECK(str(show(*B.core, "SCHEDULE"), "title") == "Region net schedule");
    // A member's own change stays: B edits WX_NET, and the next bundle keeps it.
    std::string bwx = wx;
    bwx.replace(bwx.find("TITLE Weather net"), 17, "TITLE Our weather net");
    CHECK(ok(form(*B.core, "FORM_EDIT", {{"template", "WX_NET"}, {"text", bwx}})));
    r = form(*A.core, "BUNDLE_MAKE", {{"out", (stick / "region2.pnb").string()}});
    CHECK(ok(r) && num(sub(r, "result"), "forms") == 2);
    r = form(*B.core, "BUNDLE_LOAD", {{"file", (stick / "region2.pnb").string()}});
    CHECK(ok(r) && num(sub(r, "result"), "forms_kept") == 1 && num(sub(r, "result"), "forms_same") == 1);
    CHECK(str(show(*B.core, "WX_NET"), "title") == "Our weather net");
    // A box that hasn't checked the signer takes no forms from it.
    Box D = out_of_can(dir / "d", "STA-D", std::make_unique<LoopbackModem>(0));
    r = form(*D.core, "BUNDLE_LOAD", {{"file", (stick / "region2.pnb").string()}, {"accept", "Y"}});
    CHECK(ok(r) && num(sub(r, "result"), "forms_held") == 2 && num(sub(r, "result"), "forms_added") == 0);
    CHECK(str(show(*D.core, "SCHEDULE"), "title") == "Net schedule" && !ok(show(*D.core, "WX_NET")));
    // Put back as it came.
    r = form(*A.core, "FORM_RESET", {{"template", "SCHEDULE"}});
    CHECK(ok(r) && str(sub(r, "result"), "result") == "built in again");
    CHECK(str(show(*A.core, "SCHEDULE"), "title") == "Net schedule");
    r = form(*A.core, "FORM_RESET", {{"template", "WX_NET"}});
    CHECK(ok(r) && !ok(show(*A.core, "WX_NET")));
    // Templates live beside the station's database, not in the settings.
    CHECK(fs::exists(dir / "b" / "STA-B.forms" / "WX_NET.form"));

    // Not one frequency in a form reply.
    for (auto* never : {"5000", "10000", "2500"}) CHECK(!leaks(g_all, never));
    for (Box* b : {&A, &B, &C, &D}) b->core->stop();
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_forms_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    builtins();
    language();
    fill_rules(dir / "fills");
    drill(dir);
    fs::remove_all(dir);
    return check::report("test_forms");
}
