// Composing from templates with guidance (compose.h), and the API feeds the
// Messages panel reads.
//
// The main check is the first-time operator: start from a template's form
// with nothing filled in, and change only what the guidance points at, using
// only the value the guidance offers, until the message is ready. Every
// template in every procedure it allows must get there, and what it builds
// must read back as that procedure with no warnings.
#include "../src/acp201.h"
#include "../src/api_server.h"
#include "../src/codress.h"
#include "../src/compose.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/received.h"
#include "check.h"
#include "services_fixture.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static compose::Context ctx() {
    compose::Context c;
    c.station = "AAR4XX";
    c.now = 1795269300;  // 21 Nov 2026 13:55Z
    c.codress_keys = {{"QXR01", "PRACTICE NET"}};  // a made-up key indicator
    return c;
}

static bool has(const compose::Result& r, const std::string& field, const std::string& level,
                const std::string& words = "") {
    for (auto& i : r.issues)
        if (i.field == field && i.level == level && i.text.find(words) != std::string::npos) return true;
    return false;
}

static const compose::Issue* issue(const compose::Result& r, const std::string& field, const std::string& level) {
    for (auto& i : r.issues)
        if (i.field == field && i.level == level) return &i;
    return nullptr;
}

static void dump(const compose::Result& r) {
    for (auto& i : r.issues) std::cout << "    [" << i.level << "] " << i.field << ": " << i.text << " | " << i.fix << " | " << i.example << "\n";
}

// The first-time operator: fills only what an error names, with the example
// the error gives.
static compose::Result first_time(const std::string& tmpl, const std::string& proc, int* rounds,
                                  bool wrap = false, std::string* wrap_left = nullptr) {
    compose::Draft d;
    d.template_id = tmpl;
    d.procedure = proc;
    if (wrap) d.values["wrap"] = "Y";
    compose::Result r;
    for (*rounds = 0; *rounds < 30; ++*rounds) {
        r = compose::check(d, ctx());
        if (r.ready) {
            if (wrap_left) *wrap_left = d.values["wrap"];
            return r;
        }
        bool moved = false;
        for (auto& i : r.issues) {
            if (i.level != "error") continue;
            if (i.field.empty() || i.example.empty()) {
                std::cout << "  " << tmpl << " in " << proc << ": guidance gives no value to use: " << i.text << "\n";
                return r;
            }
            if (d.values[i.field] != i.example) {
                d.values[i.field] = i.example;
                moved = true;
            }
        }
        if (!moved) {
            std::cout << "  " << tmpl << " in " << proc << ": following the guidance doesn't clear it\n";
            dump(r);
            return r;
        }
    }
    return r;
}

// The same operator with lines broken at 69: every pair still gets ready,
// and either no line is over 69 or the guidance turned the option off.
static void test_first_time_operator_wrapped() {
    int built = 0, turned_off = 0;
    for (auto& t : compose::templates())
        for (auto& p : t.procedures) {
            const compose::Procedure* pi = compose::find_procedure(p);
            if (!pi || !pi->available || p == "ACP201_CHAT") continue;
            int rounds = 0;
            std::string left;
            compose::Result r = first_time(t.id, p, &rounds, true, &left);
            CHECK(r.ready);
            if (!r.ready) {
                std::cout << "  not ready with wrap: " << t.id << " in " << p << "\n";
                dump(r);
                continue;
            }
            ++built;
            if (left == "N") {
                ++turned_off;
                continue;
            }
            size_t at = 0;
            while (at < r.message.size()) {
                size_t e = r.message.find('\n', at);
                if (e == std::string::npos) e = r.message.size();
                std::string l = r.message.substr(at, e - at);
                while (!l.empty() && l.back() == '\r') l.pop_back();
                if (l.size() > 69) std::cout << "  " << t.id << " in " << p << ": line over 69 with wrap\n";
                CHECK(l.size() <= 69);
                at = e + 1;
            }
        }
    std::cout << "  with lines broken at 69: " << built << " pairs, " << turned_off
              << " where the guidance said to turn it off for a strip\n";
}

static void test_first_time_operator() {
    int built = 0;
    for (auto& t : compose::templates()) {
        for (auto& p : t.procedures) {
            const compose::Procedure* pi = compose::find_procedure(p);
            CHECK(pi != nullptr);
            if (!pi || !pi->available) continue;
            int rounds = 0;
            compose::Result r = first_time(t.id, p, &rounds);
            CHECK(r.ready);
            if (!r.ready) {
                std::cout << "  not ready: " << t.id << " in " << p << "\n";
                dump(r);
                continue;
            }
            ++built;
            CHECK(r.procedure == p);
            CHECK(!r.to_station.empty());
            for (auto& i : r.issues)
                if (i.level == "warning") {
                    std::cout << "  " << t.id << " in " << p << " left a warning: " << i.text << "\n";
                    CHECK(false);
                }
            if (p == "PLAIN") {
                // The operator's text alone: no envelope, no digest (docs/api.md, Services).
                CHECK(r.message.find("VZCZC") == std::string::npos && r.message.find("NNNN") == std::string::npos);
                CHECK(r.message.find("BT") == std::string::npos && r.prec == 'R');
                continue;
            }
            if (p == "ACP201_CHAT") {
                acp201::Chat c;
                std::string err;
                CHECK(acp201::parse(r.message, c, &err));
                CHECK(c.warnings.empty());
                continue;
            }
            // Codress composes as the plaindress inside it; the sender
            // enciphers it (test_codress). What goes on the air reads as
            // codress; the rest of the checks read the plaindress inside.
            if (p == "ACP127_CODRESS") {
                CHECK(r.codress_key == "QXR01");
                std::string air, e;
                CHECK(codress::encipher(r.message, r.codress_key, std::string(32, 'k'), air, &e));
                received::Message cm;
                CHECK(received::parse(air, cm, &e) && received::procedure(cm) == p && cm.enciphered);
            }
            received::Message m;
            std::string err;
            bool ok = received::parse(r.message, m, &err);
            CHECK(ok);
            if (!ok) {
                std::cout << "  " << t.id << " in " << p << ": " << err << "\n" << r.message << "\n";
                continue;
            }
            const std::string want = p == "ACP127_CODRESS" ? "ACP127_PLAINDRESS" : p;
            if (received::procedure(m) != want)
                std::cout << "  " << t.id << " in " << p << " reads back as " << received::procedure(m) << "\n";
            CHECK(received::procedure(m) == want);
            // Lines aren't broken at 69 unless asked (MARS traffic isn't);
            // the reader notes a longer line, and that is all.
            if (m.warnings.size() == 1 && m.warnings[0] == "lines over 69 characters") m.warnings.clear();
            if (!m.warnings.empty()) {
                std::cout << "  " << t.id << " in " << p << " reads back with warnings:";
                for (auto& w : m.warnings) std::cout << " [" << w << "]";
                std::cout << "\n";
            }
            CHECK(m.warnings.empty());
            // The serial on the heading and in MSGID are the same.
            std::string text = received::text_of(m);
            if (text.find("MSGID/") != std::string::npos) {
                bool acp126 = p.rfind("ACP126", 0) == 0;
                CHECK(m.serial == (acp126 ? "001" : "0001"));
                CHECK(text.find("/1//") != std::string::npos);
            }
            CHECK(m.class_line == (t.id == "SVC" ? "UNCLAS SVC" : "UNCLAS") ||
                  (p == "ACP126_SIMPLE" && m.class_line == "UNCLAS"));
        }
    }
    CHECK(built >= 19 * 7);
    std::cout << "  first-time operator: " << built << " template and procedure pairs built from guidance alone\n";
}

static compose::Draft draft(const std::string& t, const std::string& p, std::map<std::string, std::string> v) {
    compose::Draft d;
    d.template_id = t;
    d.procedure = p;
    d.values = std::move(v);
    return d;
}

static void test_common_mistakes() {
    std::map<std::string, std::string> env = {{"routing", "AAR4XY"}, {"to", "UHAAAA/REGION FOUR RJMOG"}};

    // A frequency is refused, and the issue doesn't repeat it.
    {
        auto v = env;
        v["subj"] = "NET MOVES";
        v["remarks"] = "MEET ON 1.2345 TONIGHT";
        auto r = compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx());
        CHECK(!r.ready);
        const compose::Issue* i = issue(r, "remarks", "error");
        CHECK(i && i->text.find("frequency") != std::string::npos);
        CHECK(i && i->text.find("1.2345") == std::string::npos && i->example.empty());
        v["remarks"] = "MEET ON 99 MHZ";
        CHECK(has(compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx()), "remarks", "error", "frequency"));
        v["remarks"] = "MEET ON M900 AT 0100Z. TEMP 17.0C, ALTSTG 29.86.";
        CHECK(compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx()).ready);
    }
    // Lower-case RI with a space for the slash; a bare RI.
    {
        auto v = env;
        v["subj"] = "TEST";
        v["remarks"] = "TEST";
        v["to"] = "uhaaaa REGION FOUR";
        auto r = compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx());
        const compose::Issue* i = issue(r, "to", "error");
        CHECK(i && i->example == "UHAAAA/REGION FOUR");
        v["to"] = "UHAAAA";
        r = compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx());
        CHECK(r.ready && has(r, "to", "warning", "no name"));
    }
    // An empty originator.
    {
        auto v = env;
        v["subj"] = "TEST";
        v["remarks"] = "TEST";
        v["originator"] = "";
        auto r = compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx());
        CHECK(has(r, "originator", "error", "empty"));
    }
    // "//" inside a set; '/' inside a strip field.
    {
        auto v = env;
        v["subj"] = "NET//SCHEDULE";
        v["remarks"] = "TEST";
        auto r = compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx());
        const compose::Issue* i = issue(r, "subj", "error");
        CHECK(i && i->example == "NET/SCHEDULE");
        auto s = compose::check(draft("MARSSTATUS", "", {{"routing", "AAR4XY"}, {"town", "NEW/HOPE"}, {"county", "X"},
                                                          {"state", "mo"}, {"count", "12"}}),
                                ctx());
        CHECK(has(s, "town", "error", "slash"));
        CHECK(issue(s, "state", "error") && issue(s, "state", "error")->example == "MO");
    }
    // Lines over 69: sent as written by default, in every procedure, as MARS
    // stations do. Breaking at 69 is an option; with it, prose is broken and
    // a strip that can't fit is refused with the fix.
    {
        std::string strip = "RETAILPOWER/022201/01999/SPRINGFIELD/MO/CITY UTILITIES OF SPRINGFIELD/ON/022201/24//";
        CHECK(strip.size() > 69);
        for (const char* p : {"ACP127_PLAINDRESS", "16L_P2P_FULL", "ACP126_PLAINDRESS"}) {
            auto v = env;
            v["strips"] = strip;
            v["routing"] = std::string(p) == "ACP127_PLAINDRESS" ? "UHAAAA" : "AAR4XY";
            v["calls"] = "AAR4XY";
            v["station"] = "AAR4XX";
            auto r = compose::check(draft("RRI_PASTE_STRIPS", p, v), ctx());
            CHECK(r.ready);
            if (!r.ready) dump(r);
            CHECK(r.message.find(strip + "\r\r\n") != std::string::npos);
            received::Message m;
            std::string err;
            CHECK(received::parse(r.message, m, &err));
            bool found = false;
            for (auto& l : m.text) found = found || l == strip;
            CHECK(found);
            v["wrap"] = "Y";
            r = compose::check(draft("RRI_PASTE_STRIPS", p, v), ctx());
            const compose::Issue* i = issue(r, "wrap", "error");
            CHECK(!r.ready && i && i->example == "N" && has(r, "wrap", "error", "never broken"));
        }
        // Prose: one line as written; broken at spaces with the option.
        std::string longline;
        for (int k = 0; k < 20; ++k) longline += (k ? " " : "") + std::string("WORDS");
        CHECK(longline.size() > 69);
        auto longest = [](const std::string& msg) {
            size_t at = 0, n = 0;
            while (at < msg.size()) {
                size_t e = msg.find('\n', at);
                if (e == std::string::npos) e = msg.size();
                std::string l = msg.substr(at, e - at);
                while (!l.empty() && l.back() == '\r') l.pop_back();
                n = std::max(n, l.size());
                at = e + 1;
            }
            return n;
        };
        for (const char* p : {"ACP127_PLAINDRESS", "16L_P2P_FULL", "ACP126_SIMPLE"}) {
            auto v = env;
            v["routing"] = std::string(p) == "ACP127_PLAINDRESS" ? "UHAAAA" : "AAR4XY";
            v["calls"] = "NET";
            v["station"] = "AAR4XX";
            v["subj"] = "TEST";
            v["remarks"] = longline;
            auto r = compose::check(draft("GENADMIN_SIMPLE", p, v), ctx());
            CHECK(r.ready && r.message.find(longline) != std::string::npos && longest(r.message) > 69);
            if (!r.ready) dump(r);
            v["wrap"] = "Y";
            r = compose::check(draft("GENADMIN_SIMPLE", p, v), ctx());
            CHECK(r.ready && r.message.find(longline) == std::string::npos && longest(r.message) <= 69);
            if (!r.ready) dump(r);
        }
        // A status strip goes in ACP 127 too now.
        auto r = compose::check(draft("MARSSTATUS", "ACP127_PLAINDRESS",
                                      {{"routing", "UHAAAA"}, {"to", "UHAAAA/REGION FOUR RJMOG"}, {"town", "SPRINGFIELD"},
                                       {"county", "GREENE"}, {"state", "MO"}, {"count", "12"}}),
                                ctx());
        CHECK(r.ready);
        if (!r.ready) dump(r);
    }
    // ACP 127: lower case is put in capitals, with a tip; '&' is refused with a fix.
    {
        std::map<std::string, std::string> v = {{"routing", "UHAAAA"}, {"to", "UHAAAA/REGION FOUR RJMOG"},
                                                {"subj", "net schedule"}, {"remarks", "see you at 0100z"}};
        auto r = compose::check(draft("GENADMIN_SIMPLE", "ACP127_PLAINDRESS", v), ctx());
        CHECK(r.ready && has(r, "", "tip", "capitals"));
        CHECK(r.message.find("SUBJ/NET SCHEDULE//") != std::string::npos);
        v["remarks"] = "R&R AT #2";
        r = compose::check(draft("GENADMIN_SIMPLE", "ACP127_PLAINDRESS", v), ctx());
        const compose::Issue* i = issue(r, "remarks", "error");
        CHECK(i && i->example.find("AND") != std::string::npos && i->example.find("NR2") != std::string::npos);
        // 16L carries them.
        v["routing"] = "AAR4XY";
        CHECK(compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx()).ready);
    }
    // 16L RI from a call sign, and P2P from an RI.
    {
        auto v = env;
        v["subj"] = "TEST";
        v["remarks"] = "TEST";
        v["routing"] = "UHAAAA";
        v["originator"] = "AAR4XX";
        CHECK(has(compose::check(draft("GENADMIN_SIMPLE", "16L_RI_FULL", v), ctx()), "originator", "error", "isn't one"));
        v["originator"] = "UHAAAC";
        CHECK(has(compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx()), "originator", "error", "routing indicator"));
        CHECK(compose::check(draft("GENADMIN_SIMPLE", "16L_RI_FULL", v), ctx()).ready);
    }
    // Precedence: FLASH warns, an unusual one gets a tip, a bad one is refused.
    {
        auto v = env;
        v["subj"] = "TEST";
        v["remarks"] = "TEST";
        v["prec"] = "Z";
        CHECK(has(compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx()), "prec", "warning", "FLASH"));
        v["prec"] = "O";
        auto r = compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx());
        CHECK(r.ready && r.prec == 'O' && has(r, "prec", "tip", "ROUTINE"));
        CHECK(r.message.find("\r\r\nOO AAR4XY DE AAR4XX #0001 ") != std::string::npos);
        v["prec"] = "p";
        r = compose::check(draft("GENADMIN_SIMPLE", "16L_P2P_FULL", v), ctx());
        CHECK(issue(r, "prec", "error") && issue(r, "prec", "error")->example == "P");
    }
    // A reference is all or nothing; a DTG must be one.
    {
        auto v = env;
        v["subj"] = "TEST";
        v["remarks"] = "TEST";
        v["ref_from"] = "REGION FOUR";
        auto r = compose::check(draft("GENADMIN", "16L_P2P_FULL", v), ctx());
        CHECK(has(r, "ref_dtg", "error", "date-time group"));
        v["ref_dtg"] = "2004Z";
        r = compose::check(draft("GENADMIN", "16L_P2P_FULL", v), ctx());
        CHECK(issue(r, "ref_dtg", "error") && issue(r, "ref_dtg", "error")->example == "022004Z");
    }
    // A chat line takes one line.
    {
        auto r = compose::check(draft("FREE_FORM", "ACP201_CHAT", {{"call", "AAR4XY"}, {"text", "ONE\nTWO"}}), ctx());
        CHECK(has(r, "text", "error", "one line"));
    }
    // Wrong pairings and unknown names.
    {
        std::string err;
        compose::Context nokey = ctx();
        nokey.codress_keys.clear();
        CHECK(compose::form("FREE_FORM", "ACP127_CODRESS", nokey, &err).empty() && err.find("key") != std::string::npos);
        CHECK(!compose::form("FREE_FORM", "ACP127_CODRESS", ctx(), &err).empty());
        auto cr = compose::check(draft("FREE_FORM", "ACP127_CODRESS", {{"routing", "UHAAAA"}, {"to", "UHAAAA/X"},
                                                                       {"text", "HI"}, {"key", "QXR02"}}),
                                 ctx());
        CHECK(has(cr, "key", "error"));
        CHECK(compose::form("SVC", "ACP126_SIMPLE", ctx(), &err).empty() && !err.empty());
        CHECK(compose::form("NOPE", "", ctx(), &err).empty() && !err.empty());
        auto r = compose::check(draft("NOPE", "", {}), ctx());
        CHECK(!r.ready && has(r, "", "error"));
        auto u = compose::check(draft("FREE_FORM", "16L_P2P_FULL", {{"routing", "AAR4XY"}, {"text", "HI"},
                                                                     {"to", "UHAAAA/X"}, {"colour", "RED"}}),
                                ctx());
        CHECK(u.ready && has(u, "colour", "warning", "isn't a field"));
    }
}

static void test_forms_and_serials() {
    std::string err;
    auto f = compose::form("MARSSTATUS", "", ctx(), &err);
    CHECK(!f.empty());
    std::map<std::string, compose::Field> by;
    for (auto& x : f) by[x.key] = x;
    CHECK(by["time"].def == "211355");
    CHECK(by["status_call"].def == "AAR4XX");
    CHECK(by["prec"].def == "O");
    CHECK(!by["flag1"].hint.empty() && !by["flag1"].example.empty());
    auto m = compose::form("GENADMIN_AMARS_MONTHLY", "", ctx(), &err);
    for (auto& x : m)
        if (x.key == "month") CHECK(x.def == "OCTOBER");
    // Every field the operator sees says what it is and gives an example.
    for (auto& t : compose::templates())
        for (auto& p : t.procedures) {
            auto fields = compose::form(t.id, p, ctx(), &err);
            std::set<std::string> keys;
            for (auto& x : fields) {
                if (!keys.insert(x.key).second) {
                    std::cout << "  " << t.id << " in " << p << ": field " << x.key << " twice\n";
                    CHECK(false);
                }
                CHECK(!x.label.empty() && !x.hint.empty());
                if (x.required) CHECK(!x.example.empty() || !x.def.empty());
            }
        }

    // Serials: the heading and MSGID carry the same number.
    compose::Serials s;
    s.channel_serial = 12;
    s.station_serial = 37;
    s.nr = 37;
    auto r = compose::check(draft("GENADMIN_SIMPLE", "ACP127_PLAINDRESS",
                                  {{"routing", "UHAAAA"}, {"to", "UHAAAA/REGION FOUR RJMOG"}, {"subj", "T"},
                                   {"remarks", "T"}}),
                            ctx(), s);
    CHECK(r.ready);
    CHECK(r.message.rfind("VZCZCMMM012", 0) == 0);
    CHECK(r.message.find("DE AAR4XX #0037 3251355") != std::string::npos);
    CHECK(r.message.find("MSGID/GENADMIN/MARS STATION AAR4XX/37//") != std::string::npos);
    CHECK(r.channel_counter == "acp127.channel.MMM" && r.serial_counter == "acp127.station.AAR4XX");
    auto c = compose::check(draft("FREE_FORM", "ACP126_SIMPLE", {{"calls", "NET"}, {"text", "AAR4XX-NCS"}}), ctx(), s);
    CHECK(c.ready && c.message.find("NET DE AAR4XX NR 037") != std::string::npos);
    CHECK(c.serial_counter == "acp126.nr.AAR4XX" && c.serial_wrap == 999);
}

class FakeAdvisor : public compose::Advisor {
public:
    std::string name() const override { return "fake"; }
    bool advise(const compose::AdviceRequest& req, std::string& text, std::string*) override {
        last = req;
        text = "fake says " + req.field;
        return true;
    }
    compose::AdviceRequest last;
};

static void test_advice() {
    compose::AdviceRequest a;
    a.kind = "explain";
    a.field = "ref_dtg";
    a.draft = draft("AKNLDG", "", {{"ref_dtg", "2004Z"}});
    a.issues = compose::check(a.draft, ctx()).issues;
    std::string t = compose::builtin_advice(a, ctx());
    CHECK(t.find("DDHHMMZ") != std::string::npos && t.find("022004Z") != std::string::npos);
    CHECK(t.find("isn't a date-time group") != std::string::npos);
    a.field.clear();
    t = compose::builtin_advice(a, ctx());
    CHECK(t.find("Acknowledgement") != std::string::npos && t.find("To finish it") != std::string::npos);

    // The request an advisor gets names the field and carries the issues.
    std::string j = compose::advice_json(a);
    JsonValue v;
    CHECK(parse_json(j, v) && v.is_object() && v.get("issues") && v.get("issues")->is_array());

    // A program advisor: reads {"text":...} back; failures are reported.
    auto p = compose::program_advisor({"/bin/sh", "-c", "printf '%s' '{\"text\":\"use the DTG of the RI\"}'"});
    std::string out, err;
    CHECK(p->advise(a, out, &err) && out == "use the DTG of the RI");
    auto bad = compose::program_advisor({"/bin/sh", "-c", "exit 3"});
    CHECK(!bad->advise(a, out, &err) && err.find("exit 3") != std::string::npos);
    auto plain = compose::program_advisor({"/bin/sh", "-c", "echo plain words"});
    CHECK(plain->advise(a, out, &err) && out == "plain words");
}

static std::string ask(Core& core, const std::string& line) { return ApiServer::handle(core, line); }

static void test_api() {
    fs::path dir = fs::temp_directory_path() / ("pna_compose_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        Core core(on_mars_link(CoreConfig{"AAR4XX", (dir / "a.db").string(), 3, 1, true}), std::make_unique<LoopbackModem>(0));
        core.set_services(test_services());
        JsonValue v;

        CHECK(parse_json(ask(core, R"({"cmd":"compose_templates"})"), v));
        CHECK(v.get("templates") && v.get("templates")->arr.size() == compose::templates().size());
        CHECK(v.get("procedures") && v.get("procedures")->arr.size() == compose::procedures().size());

        CHECK(parse_json(ask(core, R"({"cmd":"compose_form","template":"AKNLDG"})"), v));
        CHECK(v.get("ok")->b && v.get("procedure")->s == "16L_P2P_ABBR" && !v.get("fields")->arr.empty());
        CHECK(parse_json(ask(core, R"({"cmd":"compose_form","template":"FREE_FORM","procedure":"ACP127_CODRESS"})"), v));
        CHECK(!v.get("ok")->b);

        // Check, then send: refused with errors, queued without.
        CHECK(parse_json(ask(core, R"({"cmd":"compose_check","template":"GENADMIN_SIMPLE","routing":"AAR4XY"})"), v));
        CHECK(v.get("ok")->b && !v.get("ready")->b && !v.get("issues")->arr.empty() && !v.get("preview"));
        CHECK(parse_json(ask(core, R"({"cmd":"compose_send","service":"mars","template":"GENADMIN_SIMPLE","routing":"AAR4XY"})"), v));
        CHECK(!v.get("ok")->b && v.get("issues"));
        std::string good =
            R"("template":"GENADMIN_SIMPLE","routing":"AAR4XY","to":"UHAAAA/REGION FOUR RJMOG","subj":"TEST","remarks":"Line one.\nLine two.")";
        CHECK(parse_json(ask(core, "{\"cmd\":\"compose_check\"," + good + "}"), v));
        CHECK(v.get("ready")->b && v.get("preview")->s.find("#0001") != std::string::npos);
        CHECK(parse_json(ask(core, "{\"cmd\":\"compose_send\",\"service\":\"mars\"," + good + "}"), v));
        CHECK(v.get("ok")->b && !v.get("id")->s.empty());
        std::string first = v.get("message")->s;
        CHECK(first.find("DE AAR4XX #0001 ") != std::string::npos && first.find("/1//") != std::string::npos);
        CHECK(parse_json(ask(core, "{\"cmd\":\"compose_send\",\"service\":\"mars\"," + good + ",\"prec\":\"O\"}"), v));
        CHECK(v.get("ok")->b && v.get("message")->s.find("DE AAR4XX #0002 ") != std::string::npos);
        CHECK(v.get("message")->s.find("/2//") != std::string::npos);
        std::string second = v.get("id")->s;

        // The queue: the IMMEDIATE one first.
        CHECK(parse_json(ask(core, R"({"cmd":"queue"})"), v));
        CHECK(v.get("messages")->arr.size() == 2);
        CHECK(v.get("messages")->arr[0].get("id")->s == second);
        CHECK(v.get("messages")->arr[0].get("prec")->s == "O");

        // The log: oldest first, and "after" gives only what's new.
        CHECK(parse_json(ask(core, R"({"cmd":"log"})"), v));
        CHECK(v.get("events")->arr.size() >= 2);
        long long last = static_cast<long long>(v.get("last")->n);
        CHECK(last > 0);
        CHECK(v.get("events")->arr.back().get("text")->s.find("queued") != std::string::npos);
        CHECK(parse_json(ask(core, "{\"cmd\":\"log\",\"after\":" + std::to_string(last) + "}"), v));
        CHECK(v.get("events")->arr.empty() && static_cast<long long>(v.get("last")->n) == last);
        CHECK(parse_json(ask(core, R"({"cmd":"log","after":"x"})"), v) && !v.get("ok")->b);

        // Advice: built in without an advisor, the advisor's with one.
        CHECK(parse_json(ask(core, R"({"cmd":"compose_advise","template":"MARSSTATUS","field":"count"})"), v));
        CHECK(v.get("source")->s == "builtin" && v.get("text")->s.find("12") != std::string::npos);
        auto fake = std::make_shared<FakeAdvisor>();
        compose::set_advisor(fake);
        CHECK(parse_json(ask(core, R"({"cmd":"compose_advise","template":"MARSSTATUS","field":"count","question":"what is this?"})"), v));
        CHECK(v.get("source")->s == "fake" && v.get("text")->s == "fake says count");
        CHECK(fake->last.question == "what is this?" && fake->last.draft.values.count("question") == 0);
        CHECK(parse_json(ask(core, R"({"cmd":"status"})"), v) && v.get("advisor")->s == "fake");
        compose::set_advisor(compose::program_advisor({"/bin/sh", "-c", "exit 1"}));
        CHECK(parse_json(ask(core, R"({"cmd":"compose_advise","template":"MARSSTATUS"})"), v));
        CHECK(v.get("source")->s == "builtin" && v.get("advisor_error"));
        compose::set_advisor(nullptr);
        CHECK(parse_json(ask(core, R"({"cmd":"status"})"), v) && v.get("advisor")->s.empty());
    }
    fs::remove_all(dir);
}

// PLAIN (docs/api.md, Services): the operator's text alone, as written,
// lower case kept, no envelope, no digest; a frequency is still refused.
static void test_plain() {
    compose::Draft d;
    d.template_id = "PLAIN";
    d.values["to_station"] = "TEST-1";
    d.values["text"] = "QRV ON THE NET\nsee you there";
    compose::Result r = compose::check(d, ctx());
    CHECK(r.ready && r.procedure == "PLAIN" && r.to_targets.size() == 1 && r.to_targets[0] == "TEST-1");
    CHECK(r.message == "QRV ON THE NET\r\nsee you there");
    CHECK(compose::find_procedure("PLAIN") && compose::find_template("PLAIN"));
    d.values["text"] = "";
    CHECK(!compose::check(d, ctx()).ready);
    d.values["text"] = "MEET ON 12.345 MHZ";
    CHECK(!compose::check(d, ctx()).ready);
}

int main() {
    test_plain();
    test_first_time_operator();
    test_first_time_operator_wrapped();
    test_common_mistakes();
    test_forms_and_serials();
    test_advice();
    test_api();
    return check::report("test_compose");
}
