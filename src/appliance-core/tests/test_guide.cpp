// The guide built into the terminal, and training mode (docs/guide.md).
//
// A box out of the can: the guide answers in setup mode, from the built-in
// pages, and its walkthrough starts at SETUP. Once the station runs, the
// guide's pages come from editable templates like the forms: a page per
// screen, a page per form made from the form itself (so an edited form shows
// in its page), a page per message type, and context help keyed by screen.
// The walkthrough ticks off what the station holds.
//
// Then training: with no traffic yet, the member turns it on and sees a full
// in box of scrubbed sample traffic, messages from a practice net control,
// a practice key that opens enciphered traffic, and a reply that is
// acknowledged. All of it in a practice store; the real radio hears nothing
// and the real store doesn't change.
//
// Calls and data are made up. The one frequency (refused) is a public
// time-signal one.
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/first_start.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/station.h"
#include "../src/training.h"
#include "check.h"
#include "services_fixture.h"

#include <atomic>
#include <set>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

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
static JsonValue parsed(const std::string& reply) {
    if (std::getenv("GUIDE_DEBUG")) std::cerr << "  -> " << reply.substr(0, 500) << "\n";
    JsonValue v;
    parse_json(reply, v);
    return v;
}
static JsonValue call(Core& c, std::initializer_list<std::pair<std::string, std::string>> kv) {
    std::string line = line_of(kv);
    if (std::getenv("GUIDE_DEBUG")) std::cerr << line.substr(0, 200) << "\n";
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
static bool has(const JsonValue& v, const char* k) { return v.get(k) != nullptr; }
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
static const JsonValue* step(const JsonValue& start, const std::string& id) {
    for (auto& s : arr(start, "steps"))
        if (str(s, "id") == id) return &s;
    return nullptr;
}
static bool lines_fit(const JsonValue& page) {
    if (arr(page, "lines").empty()) return false;
    for (auto& l : arr(page, "lines"))
        if (l.s.size() > 80) return false;
    return true;
}

// ---------------------------------------------------------------- the guide

static void guide(const fs::path& dir) {
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    const std::string d = dir.string();
    std::string got;
    auto setup = [&](const std::string& line) { return parsed(first_start::handle(d, now, line, &got)); };

    // Out of the can: the guide answers before the call is set.
    JsonValue r = setup(R"({"cmd":"guide_start"})");
    CHECK(ok(r) && num(r, "done") == 0 && str(r, "next") == "SETUP" && num(r, "of") >= 6);
    CHECK(step(r, "SETUP") && str(*step(r, "SETUP"), "form") == "SETUP");
    r = setup(R"({"cmd":"guide_help","screen":"FORM-SETUP"})");
    CHECK(ok(r) && yes(r, "found") && lines_fit(sub(r, "page")));
    r = setup(R"({"cmd":"guide_get","id":"START"})");
    CHECK(ok(r) && yes(r, "found") && str(sub(r, "page"), "kind") == "start");
    // Setup mode changes nothing: no page edits before the station exists.
    r = setup(R"({"cmd":"guide_page_put","text":"PAGE MINE\nTITLE Mine\nBEGIN_TEXT\nx\nEND\n"})");
    CHECK(!ok(r));

    r = setup(R"({"cmd":"form_submit","form":"SETUP","call":"STA-G"})");
    CHECK(ok(r) && got == "STA-G");
    std::string base = (dir / got).string();
    auto radio = std::make_unique<LoopbackModem>(0);
    auto far = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*radio, *far);
    CoreConfig cfg{got, base + ".db", 3, 1, true};
    Core core(cfg, std::move(radio));
    core.set_services(test_services());
    auto kr = std::make_shared<keys::Keyring>(base + ".keys");
    std::string err;
    kr->load(&err);
    core.set_keyring(kr);
    core.set_ops(std::make_shared<Ops>(got, base + ".db"));
    auto id = std::make_shared<station::Identity>();
    bool exists = false;
    CHECK(station::load_identity(base + ".id", *id, &exists, &err) && exists);
    core.set_identity(id, base + ".id");
    core.start();

    // Every page: screens, forms and message types, each fits the screen.
    r = call(core, {{"cmd", "guide_list"}});
    std::set<std::string> ids;
    for (auto& p : arr(r, "pages")) ids.insert(str(p, "id"));
    for (const char* want : {"START", "MAIN", "TRAINING", "MSG-INBOX", "MSG-COMPOSE", "EMCON", "KEYS", "RESENDS",
                             "FORM-SETUP", "FORM-BOOK", "FORM-NET_TAPE", "FORM-PARTS_QUEUE", "TYPE-FREE_FORM"})
        CHECK(ids.count(want));
    for (auto& i : ids) {
        JsonValue p = call(core, {{"cmd", "guide_get"}, {"id", i}});
        CHECK(ok(p) && lines_fit(sub(p, "page")));
        if (!lines_fit(sub(p, "page"))) std::cerr << "  page doesn't fit: " << i << "\n";
    }

    // ? on a screen: the page that lists it; on compose, the type's page too.
    r = call(core, {{"cmd", "guide_help"}, {"screen", "msg-inbox"}});
    CHECK(ok(r) && yes(r, "found") && str(sub(r, "page"), "id") == "MSG-INBOX" && !arr(sub(r, "page"), "keys").empty());
    r = call(core, {{"cmd", "guide_help"}, {"screen", "MSG-COMPOSE"}, {"type", "FREE_FORM"}});
    CHECK(ok(r) && str(sub(r, "page"), "id") == "MSG-COMPOSE" && str(sub(r, "type"), "id") == "TYPE-FREE_FORM");
    r = call(core, {{"cmd", "guide_help"}, {"screen", "FORM-BOOK"}});
    CHECK(ok(r) && str(sub(r, "page"), "kind") == "form");
    // A screen with no page yet: the main screen's page, never nothing.
    r = call(core, {{"cmd", "guide_help"}, {"screen", "NO-SUCH-SCREEN"}});
    CHECK(ok(r) && !yes(r, "found") && str(sub(r, "page"), "id") == "MAIN");
    CHECK(!yes(r, "training"));

    // Ask 39: gale-web's screens find their built-in pages by SCREENS line,
    // under the GW- id and the screen's own id.
    for (const char* x : {"SCHEDULE", "CHANNELS", "BOOK", "STATION"}) {
        const std::string gw = std::string("GW-") + x;
        for (const std::string& screen : {gw, std::string(x)}) {
            r = call(core, {{"cmd", "guide_help"}, {"screen", screen}});
            CHECK(ok(r) && yes(r, "found") && str(sub(r, "page"), "id") == gw);
            if (str(sub(r, "page"), "id") != gw) std::cerr << "  no GW page for " << screen << "\n";
        }
    }
    // A page put on the box for a screen answers ? there ahead of the
    // built-in page that lists the same screen, until it is reset.
    CHECK(ok(call(core, {{"cmd", "guide_page_put"},
                         {"text", "PAGE AA-LOCAL\nTITLE Our schedule\nKIND screen\nSCREENS SCHEDULE\n"
                                  "BEGIN_TEXT\nOUR OWN SCHEDULE PAGE.\nEND\n"}})));
    r = call(core, {{"cmd", "guide_help"}, {"screen", "SCHEDULE"}});
    CHECK(ok(r) && str(sub(r, "page"), "id") == "AA-LOCAL" && str(sub(r, "page"), "origin") == "edited here");
    r = call(core, {{"cmd", "guide_help"}, {"screen", "GW-SCHEDULE"}});
    CHECK(ok(r) && str(sub(r, "page"), "id") == "GW-SCHEDULE");
    CHECK(ok(call(core, {{"cmd", "guide_page_reset"}, {"id", "AA-LOCAL"}})));
    r = call(core, {{"cmd", "guide_help"}, {"screen", "SCHEDULE"}});
    CHECK(ok(r) && str(sub(r, "page"), "id") == "GW-SCHEDULE");

    // A form's page is made from the form: edit the form, the page follows.
    r = call(core, {{"cmd", "form_template_get"}, {"form", "BOOK"}});
    std::string ft = str(r, "text");
    CHECK(ok(r) && ft.find("TITLE Station book entry") != std::string::npos);
    std::string edited = ft;
    edited.replace(edited.find("TITLE Station book entry"), 24, "TITLE Station book card");
    CHECK(ok(call(core, {{"cmd", "form_template_put"}, {"form", "BOOK"}, {"text", edited}})));
    r = call(core, {{"cmd", "guide_get"}, {"id", "FORM-BOOK"}});
    CHECK(ok(r) && str(sub(r, "page"), "title").find("Station book card") != std::string::npos);
    CHECK(ok(call(core, {{"cmd", "form_template_reset"}, {"form", "BOOK"}})));

    // A written page is editable like a form, and goes back to built in.
    r = call(core, {{"cmd", "guide_page_get"}, {"id", "EMCON"}});
    std::string pt = str(r, "text");
    CHECK(ok(r) && str(r, "origin") == "built in" && pt.find("PAGE EMCON") == 0);
    std::string mine = pt;
    mine.replace(mine.find("BEGIN_TEXT\n") + 11, 0, "OUR NET: EMCON IS CALLED BY THE NCS ONLY.\n");
    CHECK(ok(call(core, {{"cmd", "guide_page_put"}, {"text", mine}})));
    r = call(core, {{"cmd", "guide_get"}, {"id", "EMCON"}});
    CHECK(str(sub(r, "page"), "text").find("CALLED BY THE NCS ONLY") != std::string::npos &&
          str(sub(r, "page"), "origin") != "built in");
    // A page never holds a frequency, and a made page can't be written over.
    std::string freq = pt;
    freq.replace(freq.find("BEGIN_TEXT\n") + 11, 0, "NET MEETS ON 5000 KHZ.\n");
    r = call(core, {{"cmd", "guide_page_put"}, {"text", freq}});
    CHECK(!ok(r));
    r = call(core, {{"cmd", "guide_page_put"}, {"text", "PAGE FORM-BOOK\nTITLE x\nBEGIN_TEXT\nx\nEND\n"}});
    CHECK(!ok(r));
    r = call(core, {{"cmd", "guide_page_reset"}, {"id", "EMCON"}});
    CHECK(ok(r) && yes(r, "reset"));
    r = call(core, {{"cmd", "guide_get"}, {"id", "EMCON"}});
    CHECK(str(sub(r, "page"), "origin") == "built in");

    // The walkthrough ticks off what the station holds.
    r = call(core, {{"cmd", "guide_start"}});
    CHECK(ok(r) && yes(*step(r, "SETUP"), "done") && !yes(*step(r, "FIRST"), "done") && str(r, "next") == "CARD");
    CHECK(ok(call(core, {{"cmd", "send"}, {"service", "mars"}, {"to", "STA-H"}, {"body", "FIRST MESSAGE"}})));
    r = call(core, {{"cmd", "guide_start"}});
    CHECK(yes(*step(r, "FIRST"), "done") && num(r, "done") >= 2);
    core.stop();
}

// ---------------------------------------------------------------- training

static void training(const fs::path& dir) {
    std::string base = (dir / "STA-T").string();
    auto radio = std::make_unique<LoopbackModem>(0);
    auto far = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*radio, *far);
    // The real radio's far end: counts every byte the real station keys up with.
    std::atomic<size_t> heard{0};
    LoopbackModem* far_radio = far.get();
    far_radio->start({[&](const std::string& b) { heard += b.size(); }, nullptr, nullptr, nullptr});
    CoreConfig cfg{"STA-T", base + ".db", 3, 1, true};
    Core core(cfg, std::move(radio));
    core.set_services(test_services());
    auto kr = std::make_shared<keys::Keyring>(base + ".keys");
    std::string err;
    kr->load(&err);
    core.set_keyring(kr);
    core.set_ops(std::make_shared<Ops>("STA-T", base + ".db"));
    auto id = std::make_shared<station::Identity>();
    CHECK(station::make_identity("STA-T", static_cast<int64_t>(std::time(nullptr)), *id, &err) &&
          station::save_identity(base + ".id", *id, &err));
    core.set_identity(id, base + ".id");
    core.start();

    // A new member: no traffic at all.
    const size_t real_before = core.list(5000).size();
    CHECK(real_before == 0);
    JsonValue r = call(core, {{"cmd", "training_status"}});
    CHECK(ok(r) && !yes(r, "on"));

    auto t0 = std::chrono::steady_clock::now();
    r = call(core, {{"cmd", "training"}, {"on", "Y"}});
    auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cerr << "  training filled in " << took << " ms\n";
    CHECK(ok(r) && yes(r, "on") && num(r, "samples") >= 80 && str(r, "practice_station") == "TRAIN-NCS");
    CHECK(num(sub(r, "real"), "received") == 0);

    // Every reply says it's training, and it reads as this station.
    r = call(core, {{"cmd", "status"}});
    CHECK(ok(r) && yes(r, "training") && str(r, "station") == "STA-T" && num(r, "received") >= 40);
    // The in box: sample traffic addressed here, some overheard, the garbled
    // one kept for a repeat, and the practice net control's messages.
    r = call(core, {{"cmd", "list"}, {"limit", "500"}});
    int received = 0, overheard = 0, garbled = 0, from_ncs = 0;
    std::string welcome;
    for (auto& m : arr(r, "messages")) {
        received += str(m, "state") == "received" || str(m, "state") == "acked";
        overheard += str(m, "state") == "overheard";
        garbled += str(m, "state") == "garbled";
        if (str(m, "from") == "TRAIN-NCS" && str(m, "direction") == "in") {
            ++from_ncs;
            if (str(m, "body").find("WELCOME TO TRAINING") != std::string::npos) welcome = str(m, "id");
        }
    }
    std::cerr << "  in training: " << received << " received, " << overheard << " overheard, " << garbled
              << " garbled, " << from_ncs << " from the practice net control\n";
    CHECK(yes(r, "training") && received >= 40 && garbled >= 1 && from_ncs >= 3 && !welcome.empty());
    // The practice key is loaded, so enciphered traffic opens.
    r = call(core, {{"cmd", "keys_list"}});
    bool practice_key = false;
    for (auto& k : arr(r, "keys")) practice_key = practice_key || str(k, "net") == "TRAINING NET";
    CHECK(practice_key);
    bool read_secret = false;
    JsonValue all_in = call(core, {{"cmd", "list"}, {"limit", "500"}});
    for (auto& m : arr(all_in, "messages"))
        if (str(m, "from") == "TRAIN-NCS") {
            JsonValue g = call(core, {{"cmd", "parse_traffic"}, {"id", str(m, "id")}});
            std::string all = str(m, "body") + str(g, "text") + str(g, "plain");
            read_secret = read_secret || all.find("ENCIPHERED IN YOUR PRACTICE KEY") != std::string::npos;
        }
    CHECK(read_secret);

    // The member answers the practice net control: it goes, and is acknowledged.
    r = call(core, {{"cmd", "compose_send"}, {"service", "mars"},
                    {"template", "FREE_FORM"},
                    {"procedure", "ACP127_PLAINDRESS"},
                    {"prec", "R"},
                    {"routing", "TRAIN-NCS"},
                    {"originator", "STA-T"},
                    {"from", "MARS STATION STA-T"},
                    {"to", "TRAIN-NCS"},
                    {"to_station", "TRAIN-NCS"},
                    {"text", "PRACTICE CHECK-IN. POWER MAINS. ANTENNA DIPOLE. CAN STAY ON THE AIR 4 HOURS."}});
    CHECK(ok(r) && yes(r, "training"));
    std::string reply_id = str(r, "id");
    CHECK(!reply_id.empty());
    CHECK(wait_for([&] {
        auto g = call(core, {{"cmd", "get"}, {"id", reply_id}});
        auto& ms = arr(g, "messages");
        return !ms.empty() && str(ms[0], "state") == "acked";
    }));

    // Training doesn't write files out, read files in, or touch backup or cipher.
    r = call(core, {{"cmd", "station_backup"}, {"passphrase", "made up for the test"}});
    CHECK(!ok(r) && yes(r, "training"));
    r = call(core, {{"cmd", "book_export"}, {"out", (dir / "book.txt").string()}});
    CHECK(!ok(r) && !fs::exists(dir / "book.txt"));
    r = call(core, {{"cmd", "key_import"}, {"file", (dir / "nothing.txt").string()}});
    CHECK(!ok(r) && str(r, "error").find("built in") != std::string::npos);
    // The guide says it's training.
    r = call(core, {{"cmd", "guide_get"}, {"id", "MSG-INBOX"}});
    CHECK(ok(r) && yes(r, "training") && !arr(sub(r, "page"), "lines").empty() &&
          arr(sub(r, "page"), "lines")[0].s.find("TRAINING") != std::string::npos);

    // Off: the real station, untouched. Its radio heard nothing.
    r = call(core, {{"cmd", "training"}, {"on", "N"}});
    CHECK(ok(r) && !yes(r, "on"));
    r = call(core, {{"cmd", "status"}});
    CHECK(ok(r) && !has(r, "training") && num(r, "received") == 0 && num(r, "queued") == 0);
    CHECK(core.list(5000).size() == real_before);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(heard.load() == 0);

    // On again: the practice store as it was left, at once.
    t0 = std::chrono::steady_clock::now();
    r = call(core, {{"cmd", "training"}, {"on", "Y"}});
    took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ok(r) && yes(r, "on") && took < 5000 && num(r, "samples") >= 80);
    r = call(core, {{"cmd", "get"}, {"id", reply_id}});
    CHECK(!arr(r, "messages").empty());
    // Reload: as new again.
    r = call(core, {{"cmd", "training"}, {"reload", "Y"}});
    CHECK(ok(r) && yes(r, "on"));
    r = call(core, {{"cmd", "get"}, {"id", reply_id}});
    CHECK(arr(r, "messages").empty());
    r = call(core, {{"cmd", "status"}});
    CHECK(yes(r, "training") && num(r, "received") >= 40);
    CHECK(ok(call(core, {{"cmd", "training"}, {"on", "N"}})));
    // Nothing of training is in the real station's store or folder's main files.
    CHECK(core.list(5000).size() == real_before && heard.load() == 0);
    CHECK(fs::exists(dir / "STA-T.training") && fs::exists(dir / "STA-T.training" / "TRAIN-NCS.db"));
    core.stop();
    far_radio->stop();
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_guide_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    guide(dir / "g");
    fs::create_directories(dir / "t");
    training(dir / "t");
    fs::remove_all(dir);
    return check::report("test_guide");
}
