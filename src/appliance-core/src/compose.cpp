// See compose.h. The templates follow the MARS traffic samples
// (tests/fixtures/mars-samples and mars-samples-b2, and the samples' NOTES.md);
// the sample each form is drawn from is named beside it.
#include "compose.h"

#include "acp126.h"
#include "acp127.h"
#include "acp201.h"
#include "digest.h"
#include "json.h"
#include "run_program.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <mutex>
#include <set>
#include <sstream>

namespace pna {
namespace compose {

namespace {

using Values = std::map<std::string, std::string>;

const char* const kMonths[] = {"JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
                               "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
const char* const kMon3[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// ---------------------------------------------------------------- strings

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

bool has_lower(const std::string& s) {
    for (char c : s)
        if (c >= 'a' && c <= 'z') return true;
    return false;
}

bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

// Lines of a multi-line value: trailing spaces gone, blank lines at the start
// and end dropped, blank lines inside kept.
std::vector<std::string> lines_of(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string l; std::getline(in, l);) {
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
        out.push_back(l);
    }
    while (!out.empty() && trim(out.front()).empty()) out.erase(out.begin());
    while (!out.empty() && trim(out.back()).empty()) out.pop_back();
    return out;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i ? sep : "") + v[i];
    return out;
}

std::string two(int v) {
    char b[16];
    std::snprintf(b, sizeof b, "%02d", v);
    return b;
}

std::string pad(int v, int width) {
    char b[16];
    std::snprintf(b, sizeof b, "%0*d", width, v);
    return b;
}

// ---------------------------------------------------------------- time

std::tm utc(const Context& ctx) {
    std::time_t t = ctx.now ? ctx.now : std::time(nullptr);
    std::tm u{};
    gmtime_r(&t, &u);
    return u;
}

std::string now_ddhhmm(const std::tm& u) { return two(u.tm_mday) + two(u.tm_hour) + two(u.tm_min); }
std::string now_dtg(const std::tm& u) { return now_ddhhmm(u) + "Z"; }
std::string now_full(const std::tm& u) { return std::to_string(u.tm_year + 1900) + two(u.tm_mon + 1) + now_dtg(u); }

bool valid_ddhhmm(const std::string& s) {
    if (s.size() != 6 || !all_digits(s)) return false;
    int d = std::stoi(s.substr(0, 2)), h = std::stoi(s.substr(2, 2)), m = std::stoi(s.substr(4, 2));
    return d >= 1 && d <= 31 && h <= 23 && m <= 59;
}

bool valid_dtg(const std::string& s) { return s.size() == 7 && s[6] == 'Z' && valid_ddhhmm(s.substr(0, 6)); }

bool valid_datetime(const std::string& s) {
    if (s.size() != 13 || s[12] != 'Z' || !all_digits(s.substr(0, 12))) return false;
    int mo = std::stoi(s.substr(4, 2));
    return mo >= 1 && mo <= 12 && valid_ddhhmm(s.substr(6, 6));
}

}  // namespace

// ---------------------------------------------------------------- frequencies

// True when s holds something shaped like a radio frequency: a number with a
// unit (1.234 MHZ, 12345 KHZ), a number with three or more decimals (1.2345),
// a number of four or five digits with decimals (12345.0), or FREQ followed
// by a number. Channel designators (M900) and temperatures (17.0C) pass.
bool looks_like_frequency(const std::string& raw) {
    std::string s = upper(raw);
    size_t n = s.size();
    for (size_t i = 0; i < n; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) continue;
        if (i > 0 && (std::isalnum(static_cast<unsigned char>(s[i - 1])) || s[i - 1] == '.')) continue;
        size_t j = i;
        while (j < n && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
        size_t int_digits = j - i, dec = 0;
        if (j + 1 < n && s[j] == '.' && std::isdigit(static_cast<unsigned char>(s[j + 1]))) {
            size_t k = j + 1;
            while (k < n && std::isdigit(static_cast<unsigned char>(s[k]))) ++k;
            dec = k - j - 1;
            j = k;
        }
        size_t u = j;
        while (u < n && s[u] == ' ') ++u;
        auto unit = [&](const char* w) {
            size_t len = std::char_traits<char>::length(w);
            return s.compare(u, len, w) == 0 && (u + len == n || !std::isalpha(static_cast<unsigned char>(s[u + len])));
        };
        if (unit("MHZ") || unit("KHZ") || unit("HZ") || unit("MC") || unit("KC")) return true;
        if (dec >= 3 && int_digits <= 2) return true;
        if (dec >= 1 && (int_digits == 4 || int_digits == 5)) return true;
        i = j;
    }
    for (size_t p = s.find("FREQ"); p != std::string::npos; p = s.find("FREQ", p + 1)) {
        size_t q = p + 4;
        while (q < n && std::isalpha(static_cast<unsigned char>(s[q]))) ++q;
        while (q < n && (s[q] == ' ' || s[q] == ':' || s[q] == '=')) ++q;
        if (q < n && std::isdigit(static_cast<unsigned char>(s[q]))) return true;
    }
    return false;
}

namespace {

// ---------------------------------------------------------------- fields

Field F(const std::string& key, const std::string& label, const std::string& kind, bool required,
        const std::string& def, const std::string& hint, const std::string& example,
        std::vector<std::string> choices = {}) {
    Field f;
    f.key = key;
    f.label = label;
    f.kind = kind;
    f.required = required;
    f.def = def;
    f.hint = hint;
    f.example = example;
    f.choices = std::move(choices);
    return f;
}

bool multi_line(const std::string& kind) {
    return kind == "lines" || kind == "strips" || kind == "addresses" || kind == "metars" || kind == "groups";
}

bool is_prose(const std::string& kind) {
    return kind == "text" || kind == "lines" || kind == "strips" || kind == "value" || kind == "metars";
}

std::string resolve(const std::string& def, const Context& ctx) {
    if (def.find('$') == std::string::npos) return def;
    std::tm u = utc(ctx);
    std::string out = def;
    auto rep = [&](const std::string& tok, const std::string& v) {
        for (size_t p = out.find(tok); p != std::string::npos; p = out.find(tok, p + v.size())) out.replace(p, tok.size(), v);
    };
    rep("$NOW_DTG", now_dtg(u));
    rep("$NOW_DDHHMM", now_ddhhmm(u));
    rep("$NOW_FULL", now_full(u));
    rep("$LAST_MONTH", kMonths[(u.tm_mon + 11) % 12]);
    rep("$CALL", ctx.station);
    return out;
}

// ---------------------------------------------------------------- procedures

enum class Env { Acp127, L16Full, L16Abbr, L16Simple, Acp126Plain, Acp126Simple, Chat, Codress };

struct ProcInfo {
    Procedure p;
    Env env;
    bool ri = false;  // 16L RI
};

const std::vector<ProcInfo>& proc_table() {
    static const std::vector<ProcInfo> t = [] {
        auto P = [](const char* id, const char* title, const char* summary, Env env, bool ri = false,
                    bool available = true, const char* why_not = "") {
            ProcInfo i;
            i.p.id = id;
            i.p.title = title;
            i.p.summary = summary;
            i.p.available = available;
            i.p.why_not = why_not;
            i.env = env;
            i.ri = ri;
            return i;
        };
        return std::vector<ProcInfo>{
            P("16L_P2P_FULL", "16L station to station, full",
              "The usual form for a station: one heading line with precedence, addressees, your call sign, serial "
              "and time, then FM and TO lines. Mixed case is allowed.",
              Env::L16Full),
            P("16L_P2P_ABBR", "16L station to station, short",
              "As 16L full, without the FM and TO lines. Used for acknowledgements, status strips and monthly "
              "reports.",
              Env::L16Abbr),
            P("16L_P2P_SIMPLE", "16L station to station, simple",
              "As 16L short, with no precedence prosign at all. Use it only when your instructions ask for it.",
              Env::L16Simple),
            P("16L_RI_FULL", "16L from a routing indicator, full",
              "As 16L full, sent from a routing indicator (UH...) instead of a call sign: region and section "
              "traffic.",
              Env::L16Full, true),
            P("16L_RI_ABBR", "16L from a routing indicator, short", "As 16L RI full, without the FM and TO lines.",
              Env::L16Abbr, true),
            P("ACP127_PLAINDRESS", "ACP 127 plaindress",
              "Headquarters form: routing line, a DE line, ZNR UUUUU, the date-time group, then FM and TO. "
              "Capitals only, and only A-Z, 0-9, space and - ? : ( ) . , /.",
              Env::Acp127),
            P("ACP127_CODRESS", "ACP 127 codress",
              "Enciphered traffic: everything after the date-time group, addresses included, goes inside the "
              "cipher, under your net's key from its key tape.",
              Env::Codress),
            P("ACP126_PLAINDRESS", "ACP 126 call-up with FM and TO",
              "\"CALL DE STATION NR 051\", then the date-time group, FM and TO. Capitals only.", Env::Acp126Plain),
            P("ACP126_SIMPLE", "ACP 126 call-up",
              "\"NET DE STATION NR 175\", then the text. Used for net check-ins. Capitals only.", Env::Acp126Simple),
            P("ACP201_CHAT", "Chat line",
              "One line, \"CALL DE STATION // text // HHMMZ K\". For quick exchanges and strips, not formal "
              "traffic.",
              Env::Chat),
        };
    }();
    return t;
}

const ProcInfo* find_proc(const std::string& id) {
    for (auto& i : proc_table())
        if (i.p.id == id) return &i;
    return nullptr;
}

bool strict(Env e) { return e == Env::Acp127 || e == Env::Acp126Plain || e == Env::Acp126Simple; }
bool is_16l(Env e) { return e == Env::L16Full || e == Env::L16Abbr || e == Env::L16Simple; }

// Envelope fields for a procedure.
std::vector<Field> envelope(const ProcInfo& pi, char prec) {
    std::vector<Field> f;
    const std::string p(1, prec);
    const Field fprec =
        F("prec", "Precedence", "prec", true, p,
          "How urgent it is: R ROUTINE, P PRIORITY, O IMMEDIATE, Z FLASH. The template starts with what MARS "
          "stations use for this kind of message.",
          p, {"R", "P", "O", "Z"});
    const Field fto_station =
        F("to_station", "Station to send to over the air", "call", false, "",
          "The station your radio sends this to. Leave it empty to send it to the first addressee.", "AAR4XX");
    const Field ffrom = F("from", "From (FM line)", "address", true, "MARS STATION $CALL",
                          "Who the message is from, as a plain name, or a routing indicator and a plain name with a "
                          "slash between them.",
                          "MARS STATION AAR4XX");
    const Field fto = F("to", "To (TO lines)", "addresses", true, "",
                        "Who the message is for, one per line: a routing indicator, a slash, and the plain name.",
                        "UHAAAA/REGION FOUR RJMOG");
    const Field finfo = F("info", "Info copies (INFO lines)", "addresses", false, "",
                          "Anyone who gets a copy for information, one per line, written like the TO line.",
                          "UHAAAB/REGION FOUR OPS");
    switch (pi.env) {
        case Env::Codress:
            f.push_back(F("key", "Key", "choice", true, "",
                          "The key it goes in: one of your net's keys for today, from the key tape you imported. "
                          "The receiving station must hold the same tape.",
                          "QXR01"));
            [[fallthrough]];
        case Env::Acp127:
            f.push_back(fprec);
            f.push_back(F("routing", "Routing line", "stations", true, "",
                          "The routing indicators the message is relayed to, separated by spaces.", "UHAAAA"));
            f.push_back(F("originator", "Your routing indicator (DE line)", "call", true, "$CALL",
                          "The routing indicator or call sign you send from. Your serial numbers count from it.",
                          "AAR4XX"));
            f.push_back(ffrom);
            f.push_back(fto);
            f.push_back(finfo);
            f.push_back(F("channel", "Channel letters", "text", false, "MMM",
                          "The three letters after VZCZC. MMM is what MARS stations use.", "MMM"));
            f.push_back(fto_station);
            break;
        case Env::L16Full:
        case Env::L16Abbr:
        case Env::L16Simple:
            if (pi.env != Env::L16Simple) f.push_back(fprec);
            f.push_back(F("routing", "Addressees (heading line)", "stations", true, "",
                          pi.ri ? "The routing indicators this goes to, separated by spaces."
                                : "The call signs, tactical calls or routing indicators this goes to, separated "
                                  "by spaces.",
                          pi.ri ? "UHAAAA" : "AAR4XY"));
            f.push_back(F("originator", pi.ri ? "Your routing indicator (after DE)" : "Your call sign (after DE)",
                          "call", true, pi.ri ? "" : "$CALL",
                          pi.ri ? "The routing indicator you send from, starting UH. Your serial numbers count from it."
                                : "Your own call sign. Your serial numbers count from it.",
                          pi.ri ? "UHAAAC" : "AAR4XX"));
            if (pi.env == Env::L16Full) {
                f.push_back(ffrom);
                f.push_back(fto);
                f.push_back(finfo);
            }
            f.push_back(F("channel", "Channel letters", "text", false, "",
                          "Three letters after VZCZC, like MMM. Most 16L traffic leaves this empty and sends a bare "
                          "VZCZC.",
                          "MMM"));
            f.push_back(fto_station);
            break;
        case Env::Acp126Plain:
        case Env::Acp126Simple:
            if (pi.env == Env::Acp126Plain) f.push_back(fprec);
            f.push_back(F("calls", "Stations called", "stations", true, "",
                          "Who you are calling, separated by spaces: call signs, tactical calls, NET or ALL.",
                          pi.env == Env::Acp126Plain ? "AAR4XY" : "NET"));
            f.push_back(F("station", "Your call sign (after DE)", "call", true, "$CALL",
                          "Your own call sign. The NR serial counts from it.", "AAR4XX"));
            if (pi.env == Env::Acp126Plain) {
                f.push_back(ffrom);
                f.push_back(fto);
            }
            f.push_back(F("channel", "Channel letters", "text", false, "MMM",
                          "The three letters after VZCZC. MMM is what MARS stations use.", "MMM"));
            f.push_back(fto_station);
            break;
        case Env::Chat:
            f.push_back(F("call", "Station to call", "stations", true, "",
                          "Who the chat line is for: a call sign, a chat address like KAPPA71-CM, or ALL.",
                          "AAR4XY"));
            f.push_back(F("station", "Your call sign", "call", true, "$CALL", "Your own call sign.", "AAR4XX"));
            f.push_back(fto_station);
            break;
    }
    if (pi.env != Env::Chat && pi.env != Env::Codress)
        f.push_back(F("wrap", "Break lines at 69 characters", "yn", false, "N",
                      "N sends each line as you write it, the way MARS stations do. Y breaks long lines at 69 "
                      "characters, as ACP 127 asks; use it only when a station or net asks for that. A strip is "
                      "never broken, so with Y a strip must fit in 69.",
                      "N"));
    return f;
}

// ---------------------------------------------------------------- templates

// The message text before the envelope goes round it.
struct Body {
    std::vector<std::string> lines;
    std::set<size_t> keep_whole;  // strips: never wrapped
    bool svc = false;             // "UNCLAS SVC"
};

struct RenderCtx {
    std::string serial;  // MSGID serial
};

struct TemplateDef {
    Template t;
    std::vector<Field> fields;
    std::function<Body(const Values&, const RenderCtx&)> render;
};

const std::vector<std::string> kEnvelopes = {"16L_P2P_FULL", "16L_P2P_ABBR", "16L_P2P_SIMPLE",   "16L_RI_FULL",
                                             "16L_RI_ABBR",  "ACP127_PLAINDRESS", "ACP126_PLAINDRESS", "ACP126_SIMPLE"};

// Common fields.
Field f_exer() {
    return F("exer", "Exercise name", "text", false, "",
             "The exercise this belongs to, like COMEX 21-4, when it is exercise traffic. Leave it empty for real "
             "traffic.",
             "COMEX 21-4");
}
Field f_msgid_from() {
    return F("msgid_from", "Your name in the message ID", "text", true, "MARS STATION $CALL",
             "How you sign the MSGID line: usually MARS STATION and your call sign, or your office.",
             "MARS STATION AAR4XX");
}
Field f_subj(const std::string& ex) {
    return F("subj", "Subject", "text", true, "", "What the message is about, in a few words.", ex);
}
Field f_auth() {
    return F("auth", "Authentication", "text", false, "",
             "The authentication your net gave you for this event. Leave it empty if none was given.", "XXX");
}
std::vector<Field> f_ref(bool required, const std::string& def_type, std::vector<std::string> types) {
    return {
        F("ref_type", "Message you are answering: its type", "choice", required, def_type,
          "The kind of message you refer to, as its MSGID line names it.", def_type.empty() ? types[0] : def_type,
          types),
        F("ref_from", "Message you are answering: who sent it", "text", required, "",
          "The sender's name as it appears in that message's MSGID line.", "AMARS REGION FOUR OPS OFFICER"),
        F("ref_dtg", "Message you are answering: its date-time group", "dtg", required, "",
          "When that message was sent: day, hour and minute, then Z, as DDHHMMZ.", "022004Z"),
        F("ref_serial", "Message you are answering: its serial", "text", false, "-",
          "The serial at the end of that message's MSGID line. Write - if it had none.", "27"),
    };
}

std::string v(const Values& m, const std::string& k) {
    auto it = m.find(k);
    return it == m.end() ? std::string() : it->second;
}

void exer_line(const Values& m, std::vector<std::string>& L) {
    if (!v(m, "exer").empty()) L.push_back("EXER/" + v(m, "exer") + "//");
}

void ref_line(const Values& m, std::vector<std::string>& L) {
    if (v(m, "ref_from").empty()) return;
    std::string serial = v(m, "ref_serial").empty() ? "-" : v(m, "ref_serial");
    L.push_back("REF/A/" + v(m, "ref_type") + "/" + v(m, "ref_from") + "/" + v(m, "ref_dtg") + "/" + serial + "//");
}

// "SET/HEAD/" + lines + "//", the first line joined to the head.
void gentext(std::vector<std::string>& L, const std::string& head, const std::vector<std::string>& lines,
             bool head_alone = false) {
    if (lines.empty()) return;
    if (head_alone) {
        L.push_back(head);
        for (auto& l : lines) L.push_back(l);
    } else {
        L.push_back(head + lines[0]);
        for (size_t i = 1; i < lines.size(); ++i) L.push_back(lines[i]);
    }
    L.back() += "//";
}

std::vector<std::string> or_dash(const std::string& s) {
    auto l = lines_of(s);
    if (l.empty()) l.push_back("-");
    return l;
}

const std::vector<TemplateDef>& template_table() {
    static const std::vector<TemplateDef> t = [] {
        std::vector<TemplateDef> T;
        auto add = [&](const char* id, const char* title, const char* summary, const char* proc, char prec,
                       std::vector<std::string> procs, std::vector<Field> fields,
                       std::function<Body(const Values&, const RenderCtx&)> render) {
            TemplateDef d;
            d.t.id = id;
            d.t.title = title;
            d.t.summary = summary;
            d.t.procedure = proc;
            d.t.prec = prec;
            d.t.procedures = std::move(procs);
            d.fields = std::move(fields);
            d.render = std::move(render);
            T.push_back(std::move(d));
        };
        auto msgid = [](const Values& m, const char* form, const RenderCtx& r) {
            return std::string("MSGID/") + form + "/" + v(m, "msgid_from") + "/" + r.serial + "//";
        };

        // Batch 1 12, 19, 21, 23, 24, 27, 29, 30, 33, 35-37, 39, 40; batch 2 09.
        add("GENADMIN_SIMPLE", "General admin message",
            "A short administrative message: subject and remarks. The most common form in MARS traffic.",
            "16L_P2P_FULL", 'R', kEnvelopes,
            {f_exer(), f_msgid_from(), f_subj("NET SCHEDULE CHANGE"),
             F("remarks", "Remarks", "lines", true, "", "What you want to say. Each line goes as you write it.",
               "THE REGION NET MOVES TO 0100Z STARTING 1 DECEMBER.")},
            [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                exer_line(m, b.lines);
                b.lines.push_back(msgid(m, "GENADMIN", r));
                b.lines.push_back("SUBJ/" + v(m, "subj") + "//");
                gentext(b.lines, "GENTEXT/REMARKS/", lines_of(v(m, "remarks")));
                return b;
            });

        // Batch 1 13, 26, 32; batch 2 10.
        std::vector<Field> genadmin = {f_exer(), f_msgid_from(), f_subj("ACKNOWLEDGEMENT OF WARNORD COMEX 21-4")};
        for (auto& f : f_ref(false, "", {"GENADMIN", "RI", "ORDER", "WARNORD", "AKNLDG"})) genadmin.push_back(f);
        genadmin.push_back(F("ack", "Ask for an acknowledgement", "choice", false, "NO",
                             "YES asks every addressee to acknowledge; then say how in the next field.", "NO",
                             {"NO", "YES"}));
        genadmin.push_back(F("ack_how", "How to acknowledge", "text", false, "",
                             "Who to acknowledge to and how, when you asked for one.", "AKNLDG TO UHAAAA"));
        genadmin.push_back(F("remarks", "Remarks", "lines", true, "",
                             "The body of the message. Number paragraphs 1., 2., ... if there are several.",
                             "1. ALL STATIONS CHECK IN TO THE REGION NET BY 2200Z."));
        genadmin.push_back(f_auth());
        add("GENADMIN", "General admin message with references",
            "An administrative message that can refer to an earlier message and ask for an acknowledgement.",
            "16L_P2P_FULL", 'R', kEnvelopes, genadmin, [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                exer_line(m, b.lines);
                b.lines.push_back(msgid(m, "GENADMIN", r));
                b.lines.push_back("SUBJ/" + v(m, "subj") + "//");
                ref_line(m, b.lines);
                if (v(m, "ack") == "YES")
                    b.lines.push_back("AKNLDG/YES/" + (v(m, "ack_how").empty() ? "-" : v(m, "ack_how")) + "//");
                gentext(b.lines, "GENTEXT/REMARKS/", lines_of(v(m, "remarks")));
                if (!v(m, "auth").empty()) b.lines.push_back("GENTEXT/AUTHENTICATION/" + v(m, "auth") + "//");
                return b;
            });

        // Batch 1 18, 20, 45; batch 2 07, 21.
        add("GENADMIN_AMARS_MONTHLY", "AMARS member monthly report",
            "The member's monthly report to the region. The samples show its shape (numbered answers) but not the "
            "questions; answer them as your region's report form asks.",
            "16L_P2P_ABBR", 'R', kEnvelopes,
            {f_msgid_from(),
             F("region", "Region number", "number", true, "", "Your MARS region, as a number.", "4"),
             F("month", "Month reported", "choice", true, "$LAST_MONTH",
               "The month the report covers, usually the one just ended.", "MAY",
               {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER",
                "NOVEMBER", "DECEMBER"}),
             F("answers", "Answers", "lines", true, "",
               "One numbered answer per line, in the order of your region's form (1. YES, 2. YES, 3. 10, ...). "
               "Sub-items go on their own lines, like a. 0.",
               "1. YES\n2. YES\n3. 10\n4. 20"),
             F("comments", "Comments", "lines", false, "NONE", "Anything else for the region. NONE if nothing.",
               "NONE")},
            [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                b.lines.push_back(msgid(m, "GENADMIN", r));
                b.lines.push_back("SUBJ/REGION " + v(m, "region") + " AMARS MEMBER MONTHLY REPORT FOR " +
                                  v(m, "month") + "//");
                std::vector<std::string> body = lines_of(v(m, "answers"));
                for (auto& l : or_dash(v(m, "comments"))) body.push_back(l);
                gentext(b.lines, "GENTEXT/REMARKS/", body, true);
                return b;
            });

        // Batch 1 22; batch 2 22.
        add("GENADMIN_AAR", "After action report",
            "What you did in an exercise or event, in the five parts MARS uses: summary, notification, training, "
            "sustains and improves.",
            "16L_P2P_FULL", 'R', kEnvelopes,
            {f_exer(), f_msgid_from(),
             F("summary", "A. Summary", "lines", true, "", "What you did, in a sentence or two.",
               "SENT RRIS AND CHECKED INTO NETS."),
             F("notification", "B. Notification", "lines", false, "-",
               "How and when you heard the exercise had started.", "RECEIVED THE START MESSAGE AT 0051Z."),
             F("training", "C. Training", "lines", false, "-", "What you practised, one item per line.",
               "RESPONDED TO AN RI\nCHECKED INTO A NET"),
             F("sustains", "D. Sustains", "lines", false, "-", "What went well and should be kept. - if nothing.",
               "-"),
             F("improves", "E. Improves", "lines", false, "-", "What should be better next time. - if nothing.",
               "-")},
            [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                exer_line(m, b.lines);
                b.lines.push_back(msgid(m, "GENADMIN", r));
                b.lines.push_back("SUBJ/AFTER ACTION REPORT//");
                std::vector<std::string> body;
                const char* parts[][2] = {{"A. SUMMARY", "summary"},
                                          {"B. NOTIFICATION", "notification"},
                                          {"C. TRAINING", "training"},
                                          {"D. SUSTAINS", "sustains"},
                                          {"E. IMPROVES", "improves"}};
                for (auto& p : parts) {
                    body.push_back(p[0]);
                    for (auto& l : or_dash(v(m, p[1]))) body.push_back(l);
                }
                gentext(b.lines, "GENTEXT/REMARKS/", body);
                return b;
            });

        // Batch 2 04.
        add("GENADMIN_CODE_GROUPS", "Message passing on code groups",
            "Passes on a text that is in code groups, exactly as received. The groups are copied, never made here.",
            "16L_P2P_FULL", 'R', kEnvelopes,
            {f_msgid_from(), f_subj("HQ ARMY MARS MSG/131539Z OCT 2021/40"),
             F("intro", "Introduction", "text", true, "FOLLOWING IS TEXT OF SUBJECT MSG",
               "One line saying what the groups are.", "FOLLOWING IS TEXT OF SUBJECT MSG"),
             F("groups", "Code groups", "groups", true, "",
               "The groups as you received them, four characters each, several to a line.",
               "4GKD DKSG PU8O GMMP\nY4P1 JOGE")},
            [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                b.lines.push_back(msgid(m, "GENADMIN", r));
                b.lines.push_back("SUBJ/" + v(m, "subj") + "//");
                b.lines.push_back("GENTEXT/REMARKS/" + v(m, "intro"));
                for (auto& l : lines_of(v(m, "groups"))) {
                    b.keep_whole.insert(b.lines.size());
                    b.lines.push_back(join(words(l), " "));
                }
                b.lines.push_back("//");
                return b;
            });

        // Batch 1 14, 15, 31; batch 2 08, 11.
        std::vector<Field> ak = {f_msgid_from()};
        for (auto& f : f_ref(true, "GENADMIN", {"GENADMIN", "RI", "ORDER", "WARNORD"})) ak.push_back(f);
        ak.push_back(f_auth());
        add("AKNLDG", "Acknowledgement",
            "Acknowledges a message that asked for one: it names that message, and the message's authentication if "
            "it gave one.",
            "16L_P2P_ABBR", 'P', kEnvelopes, ak, [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                b.lines.push_back(msgid(m, "AKNLDG", r));
                ref_line(m, b.lines);
                b.lines.push_back("ACTAGCY/" + v(m, "msgid_from") + "/ACK/A//");
                if (!v(m, "auth").empty()) b.lines.push_back("GENTEXT/AUTHENTICATION/" + v(m, "auth") + "//");
                return b;
            });

        // Batch 1 06, 08; batch 2 01, 12, 20.
        std::vector<Field> ri = {f_exer(), f_msgid_from()};
        for (auto& f : f_ref(false, "", {"RI", "ORDER", "GENADMIN"})) ri.push_back(f);
        ri.push_back(F("datedes", "Requested at", "datetime", true, "$NOW_FULL",
                       "When the request is made, as year, month, day, hour, minute and Z.", "202111062053Z"));
        ri.push_back(F("ltiov", "Answers wanted by", "datetime", true, "",
                       "The latest time an answer is still of use (LTIOV), written like the field above.",
                       "202611231400Z"));
        ri.push_back(F("pry", "Request priority", "choice", true, "1",
                       "The request's priority as your instructions give it. The samples use 1.", "1", {"1", "2", "3"}));
        ri.push_back(F("requested", "Information requested", "lines", true, "",
                       "What you want stations to find out. Say plainly whether they may contact anyone.",
                       "EXERCISE, DO NOT CONTACT. LIST 3 PUBLIC LODGING FACILITIES WITHIN 5 MILES OF THE COUNTY "
                       "SEAT."));
        ri.push_back(F("method", "How to answer", "lines", true, "",
                       "How stations should reply: usually an RRI with the strip below, and how often.",
                       "RESPOND BY RRI USING THE STRIP BELOW, ONCE PER LOCATION."));
        ri.push_back(F("strip", "Strip to fill in", "strips", false, "",
                       "The response strip, one line, fields separated by slashes. Leave it empty if there is none.",
                       "PUBLICLODG/DDHHMM/FIPS/LOC/HOTEL1/PHONE1/HOTEL2/PHONE2//"));
        add("RI", "Request for information",
            "Asks stations to find something out and answer by RRI by a set time.", "16L_P2P_FULL", 'P', kEnvelopes,
            ri, [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                exer_line(m, b.lines);
                b.lines.push_back(msgid(m, "RI", r));
                ref_line(m, b.lines);
                b.lines.push_back("REQDAT/DATEDES:" + v(m, "datedes") + "/LTIOV:" + v(m, "ltiov") + "/PRY:" +
                                  v(m, "pry") + "//");
                gentext(b.lines, "GENTEXT/SPECIFIC INFORMATION REQUESTED/", lines_of(v(m, "requested")), true);
                std::vector<std::string> how = lines_of(v(m, "method"));
                b.lines.push_back("GENTEXT/METHOD OF RESPONSE/");
                for (auto& l : how) b.lines.push_back(l);
                for (auto& l : lines_of(v(m, "strip"))) {
                    b.keep_whole.insert(b.lines.size());
                    b.lines.push_back(l);
                }
                if (!b.keep_whole.count(b.lines.size() - 1)) b.lines.back() += "//";
                return b;
            });

        // RRI and its strip forms: batch 2 05, 06, 16, 27 / batch 1 05, 07, 43, batch 2 25, 26 / batch 2 15.
        std::vector<Field> rri = {f_exer(), f_msgid_from()};
        for (auto& f : f_ref(true, "RI", {"RI", "GENADMIN"})) rri.push_back(f);
        rri.push_back(F("response", "Your answer", "lines", true, "", "The information the RI asked for.",
                        "THREE FACILITIES FOUND; DETAILS FOLLOW."));
        add("RRI", "Response to a request for information", "Answers an RI in your own words.", "16L_P2P_FULL", 'P',
            kEnvelopes, rri, [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                exer_line(m, b.lines);
                b.lines.push_back(msgid(m, "RRI", r));
                ref_line(m, b.lines);
                gentext(b.lines, "GENTEXT/REQUEST RESPONSE/", lines_of(v(m, "response")));
                return b;
            });

        std::vector<Field> paste = {f_exer(), f_msgid_from()};
        for (auto& f : f_ref(false, "RI", {"RI", "GENADMIN"})) paste.push_back(f);
        paste.push_back(F("strips", "Strips", "strips", true, "",
                          "Each filled-in strip on a line of its own, exactly as the RI's strip asks: fields "
                          "separated by single slashes, - for a field you can't fill.",
                          "RETAILPOWER/022201/01999/SPRINGFIELD/MO/CITY UTILITIES/ON/022201/24//"));
        add("RRI_PASTE_STRIPS", "Response with strips",
            "Answers an RI with one or more filled-in strips. Each strip stays on one line so the collector can "
            "read it.",
            "16L_P2P_FULL", 'P', kEnvelopes, paste, [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                exer_line(m, b.lines);
                b.lines.push_back(msgid(m, "RRI", r));
                ref_line(m, b.lines);
                b.lines.push_back("GENTEXT/REQUEST RESPONSE/RRI STRIPS FOLLOW//");
                for (auto& l : lines_of(v(m, "strips"))) {
                    b.keep_whole.insert(b.lines.size());
                    b.lines.push_back(l);
                }
                return b;
            });

        std::vector<Field> metar = {f_exer(), f_msgid_from()};
        for (auto& f : f_ref(false, "RI", {"RI", "GENADMIN"})) metar.push_back(f);
        metar.push_back(F("metars", "Weather reports (METAR)", "metars", true, "",
                          "One airport weather report per line, as published: the airport's four-letter code, the "
                          "time as DDHHMMZ, then the report.",
                          "KAGS 071902Z 20006KT 10SM BKN070 26/16 A3005"));
        add("RRI_METAR", "Response with airport weather reports",
            "Answers a weather RI with transcribed METAR reports, one per line.", "16L_P2P_FULL", 'P', kEnvelopes,
            metar, [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                exer_line(m, b.lines);
                b.lines.push_back(msgid(m, "RRI", r));
                ref_line(m, b.lines);
                b.lines.push_back("GENTEXT/REQUEST RESPONSE/STRIP REPORTS FOLLOW//");
                for (auto& l : lines_of(v(m, "metars"))) {
                    b.keep_whole.insert(b.lines.size());
                    b.lines.push_back("METAR/" + join(words(l), " ") + "//");
                }
                return b;
            });

        // Batch 1 01-04, 09; batch 2 24, 36 (chat).
        // The samples send these as 16L and chat. A status strip is longer
        // than 69 characters, so it can go in ACP 127 or 126 only with line
        // breaking off, which is the default.
        std::vector<std::string> strip_procs = kEnvelopes;
        strip_procs.push_back("ACP201_CHAT");
        add("MARSSTATUS", "Station status strip",
            "The one-line MARSSTATUS strip for a status drill: where you are and whether you are on the air. The "
            "samples don't say what the three Y/N flags and the last number mean; fill them as your drill "
            "instructions say.",
            "16L_P2P_ABBR", 'O', strip_procs,
            {F("time", "Time of report", "ddhhmm", true, "$NOW_DDHHMM",
               "When you report, as day, hour and minute (DDHHMM), in UTC.", "082020"),
             F("town", "Town", "value", true, "", "The town your station is in.", "SPRINGFIELD"),
             F("county", "County", "value", true, "", "Your county.", "GREENE"),
             F("state", "State", "state", true, "", "Your state, as two letters.", "MO"),
             F("status_call", "Your call sign", "call", true, "$CALL", "Your own call sign.", "AAR4XX"),
             F("status", "Station status", "value", true, "FULLYOPERATIONAL",
               "Your station's state. The samples only show FULLYOPERATIONAL; use the word your drill gives if "
               "it's different.",
               "FULLYOPERATIONAL"),
             F("flag1", "First Y/N flag", "yn", true, "Y", "Y or N, as your drill instructions define it.", "Y"),
             F("flag2", "Second Y/N flag", "yn", true, "Y", "Y or N, as your drill instructions define it.", "Y"),
             F("flag3", "Third Y/N flag", "yn", true, "Y", "Y or N, as your drill instructions define it.", "Y"),
             F("count", "Last field (a number)", "number", true, "",
               "A number your drill instructions define; the samples show 12 and don't say what it counts.", "12")},
            [](const Values& m, const RenderCtx&) {
                Body b;
                b.keep_whole.insert(0);
                b.lines.push_back("MARSSTATUS/" + v(m, "time") + "/" + v(m, "town") + "/" + v(m, "county") + "/" +
                                  v(m, "state") + "/" + v(m, "status_call") + "/" + v(m, "status") + "/" + v(m, "flag1") +
                                  "/" + v(m, "flag2") + "/" + v(m, "flag3") + "/" + v(m, "count") + "//");
                return b;
            });

        add("BARE_STRIP", "Strip on its own",
            "One or more strips with nothing around them, for a net that collects strips directly.", "16L_P2P_ABBR",
            'R', strip_procs,
            {F("strips", "Strips", "strips", true, "",
               "Each strip on a line of its own, exactly as the net's instructions give it.",
               "SWEATMSO/112350/12999/SPRINGFIELD/MO/-/-/-/MARS/OBSERVED/-//")},
            [](const Values& m, const RenderCtx&) {
                Body b;
                for (auto& l : lines_of(v(m, "strips"))) {
                    b.keep_whole.insert(b.lines.size());
                    b.lines.push_back(l);
                }
                return b;
            });

        // COMSPOT and its net reports: batch 1 11, batch 2 23 / batch 1 41, batch 2 18 / batch 1 38, 42, batch 2 19.
        std::vector<Field> comev = {
            F("event", "Event", "choice", true, "ACTIVATION",
              "What kind of event this reports. The samples use ACTIVATION and OTHER.", "ACTIVATION",
              {"ACTIVATION", "OTHER"}),
            F("start", "Started", "dtg", true, "$NOW_DTG", "When it started or starts, as DDHHMMZ.", "081201Z"),
            F("end", "Ended", "dtg_or_dash", false, "-", "When it ended or ends, as DDHHMMZ, or - if open.", "081230Z"),
            F("net", "Net or event name", "text", false, "-",
              "The net or event, as your instructions name it, like REGION NET or CCN:B7Q. - if none.",
              "REGION NET"),
            F("area", "Area", "text", true, "", "Where: your region or area, like REGION 4.", "REGION 4"),
            F("country", "Country", "text", true, "US", "Two-letter country code.", "US"),
            F("grid", "Map grid", "text", false, "-", "A map grid square if your instructions ask for one, else -.",
              "-"),
        };
        auto comev_lines = [](const Values& m, std::vector<std::string>& L) {
            L.push_back("COMEV/" + v(m, "event") + "/" + v(m, "start") + "/" + (v(m, "end").empty() ? "-" : v(m, "end")) +
                        "/" + (v(m, "net").empty() ? "-" : v(m, "net")) + "//");
            L.push_back("LOCN/" + v(m, "area") + "/" + v(m, "country") + "/" + (v(m, "grid").empty() ? "-" : v(m, "grid")) +
                        "//");
        };
        std::vector<Field> comspot = {f_exer(), f_msgid_from()};
        for (auto& f : comev) comspot.push_back(f);
        comspot.push_back(F("remarks", "Plan or remarks", "lines", true, "",
                            "What you plan or report, such as which channel you will be on at what time, one line "
                            "each. Name channels by their designators, never by frequency.",
                            "TIME   CHAN   ALT\n1201Z  M900   M901"));
        add("COMSPOT", "Communications spot report",
            "Reports a communications event or your planned operations, such as your channel schedule for the next "
            "48 hours.",
            "ACP127_PLAINDRESS", 'R', kEnvelopes, comspot, [msgid, comev_lines](const Values& m, const RenderCtx& r) {
                Body b;
                exer_line(m, b.lines);
                b.lines.push_back(msgid(m, "COMSPOT", r));
                comev_lines(m, b.lines);
                gentext(b.lines, "RMKS/", lines_of(v(m, "remarks")), true);
                return b;
            });

        std::vector<Field> netrep = {f_msgid_from()};
        for (auto& f : comev) netrep.push_back(f);
        netrep[3].required = true;  // end
        netrep[3].def.clear();
        netrep[4].required = true;  // net
        netrep[4].def.clear();
        netrep.push_back(F("net_channel", "Channel the net used", "text", true, "",
                           "The channel designator from your net plan, never a frequency.", "M900"));
        netrep.push_back(F("stations", "Stations in the net", "lines", true, "",
                           "One station per line, with its role after it if it had one (NCS, ANCS1, MOBILE).",
                           "AAR4XX NCS\nAAR4XY\nAAR4XZ ANCS1"));
        netrep.push_back(F("traffic", "Traffic passed", "text", false, "NONE", "Messages passed, or NONE.", "NONE"));
        netrep.push_back(F("training", "Training", "text", false, "NONE", "Training done, or NONE.", "NONE"));
        netrep.push_back(F("net_remarks", "Remarks", "text", false, "NONE", "Anything else, or NONE.", "NONE"));
        add("COMSPOT_NET_REPORT", "Net report",
            "The net control station's report after a net: who checked in, and any traffic.", "16L_P2P_FULL", 'R',
            kEnvelopes, netrep, [msgid, comev_lines](const Values& m, const RenderCtx& r) {
                Body b;
                b.lines.push_back(msgid(m, "COMSPOT", r));
                comev_lines(m, b.lines);
                std::vector<std::string> st = lines_of(v(m, "stations"));
                size_t n = 0;
                for (auto& s : st)
                    if (!trim(s).empty()) ++n;
                b.lines.push_back("RMKS/1. FREQ: " + v(m, "net_channel"));
                b.lines.push_back("2. TOTAL STATIONS:" + std::to_string(n));
                b.lines.push_back("3. STATIONS IN THE NET:");
                for (auto& s : st) b.lines.push_back(s);
                b.lines.push_back("4. TRAFFIC: " + (v(m, "traffic").empty() ? "NONE" : v(m, "traffic")));
                b.lines.push_back("5. TRAINING: " + (v(m, "training").empty() ? "NONE" : v(m, "training")));
                b.lines.push_back("6. REMARKS: " + (v(m, "net_remarks").empty() ? "NONE" : v(m, "net_remarks")) + "//");
                return b;
            });

        std::vector<Field> gst = {f_msgid_from()};
        for (auto& f : comev) gst.push_back(f);
        gst[1].def = "OTHER";
        gst.push_back(F("team", "Net or team name", "text", true, "", "The net's name for heading 1A.",
                        "MAJOR RELAY/GATEWAY SUPPORT TEAM (GST)"));
        gst.push_back(F("roles", "Net roles", "lines", true, "",
                        "Who held each role, one per line: the role, then the station.", "NCS    AAR4XX\nROMEO  AAR4XY"));
        gst.push_back(F("acknowledged", "Stations acknowledged", "lines", true, "", "Every station heard, one per line.",
                        "AAR4XX\nAAR4XY"));
        gst.push_back(F("channels", "Channels used", "lines", false, "",
                        "The channel plan, one per line, by designator only (PRIMARY M900). Never write frequencies.",
                        "PRIMARY   M900\nSECONDARY M901"));
        gst.push_back(F("listed", "Traffic listed", "lines", false, "NONE",
                        "Messages listed in the net, one per line, or NONE.", "NONE"));
        gst.push_back(F("outstanding", "Traffic outstanding", "lines", false, "NONE",
                        "Messages still waiting, one per line, or NONE.", "NONE"));
        gst.push_back(F("broadcast", "Broadcast messages", "lines", false, "-", "Broadcasts sent, or -.", "-"));
        add("COMSPOT_GST_NET_REPORT", "Gateway support team net report",
            "The longer net report the gateway support team sends: roles, stations heard, channels and traffic.",
            "16L_P2P_FULL", 'R', kEnvelopes, gst, [msgid, comev_lines](const Values& m, const RenderCtx& r) {
                Body b;
                b.lines.push_back(msgid(m, "COMSPOT", r));
                comev_lines(m, b.lines);
                b.lines.push_back("RMKS/NET REPORT");
                b.lines.push_back("");
                b.lines.push_back("1A. " + v(m, "team"));
                for (auto& l : lines_of(v(m, "roles"))) b.lines.push_back(l);
                b.lines.push_back("");
                b.lines.push_back("1B. STATIONS ACKNOWLEDGED");
                for (auto& l : lines_of(v(m, "acknowledged"))) b.lines.push_back(l);
                auto section = [&](const std::string& head, const std::string& key) {
                    b.lines.push_back("");
                    b.lines.push_back(head);
                    for (auto& l : or_dash(v(m, key))) b.lines.push_back(l);
                };
                if (!lines_of(v(m, "channels")).empty()) section("2. CHANNELS", "channels");
                section("3. TRAFFIC LISTED", "listed");
                section("4. TRAFFIC OUTSTANDING", "outstanding");
                section("5. BROADCAST MESSAGES", "broadcast");
                b.lines.back() += "//";
                return b;
            });

        // Batch 1 34.
        std::vector<Field> order = {f_exer(), f_msgid_from()};
        for (auto& f : f_ref(false, "", {"ORDER", "OPORD", "WARNORD", "GENADMIN"})) order.push_back(f);
        order.push_back(F("ordtyp", "Kind of order", "choice", true, "WARNORD",
                          "WARNORD warns of an operation, OPORD orders it, FRAGORD changes an earlier order.",
                          "WARNORD", {"WARNORD", "OPORD", "FRAGORD"}));
        order.push_back(F("issuer", "Issued by", "text", true, "", "The office issuing the order.",
                          "REGION FOUR OPS OFFICER"));
        order.push_back(F("situation", "Situation", "lines", true, "", "What is going on and why.",
                          "REGION FOUR MARS WILL SUPPORT DOD COMEX 22-1."));
        order.push_back(F("mission", "Mission", "lines", true, "", "Who does what, when and where.",
                          "ALL STATIONS ACTIVATE FROM 1601Z 18 FEB TO 2359Z 21 FEB."));
        order.push_back(F("execution", "Execution", "lines", true, "", "The tasks, one per line if there are several.",
                          "A) MONITOR THE CALLING CHANNELS\nB) RESPOND TO RIS"));
        order.push_back(F("ack", "Ask for an acknowledgement", "choice", false, "NO",
                          "YES asks every addressee to acknowledge the order.", "NO", {"NO", "YES"}));
        add("ORDER", "Order", "A warning, operation or fragmentary order, in situation, mission and execution.",
            "16L_P2P_FULL", 'R', kEnvelopes, order, [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                exer_line(m, b.lines);
                b.lines.push_back(msgid(m, "ORDER", r));
                ref_line(m, b.lines);
                b.lines.push_back("ORDTYP/" + v(m, "ordtyp") + "/" + v(m, "issuer") + "//");
                b.lines.push_back("TIMEZONE/Z//");
                gentext(b.lines, "GENTEXT/SITUATION/", lines_of(v(m, "situation")));
                gentext(b.lines, "GENTEXT/MISSION/", lines_of(v(m, "mission")));
                gentext(b.lines, "GENTEXT/EXECUTION/", lines_of(v(m, "execution")));
                b.lines.push_back(std::string("AKNLDG/") + (v(m, "ack") == "YES" ? "YES" : "NO") + "/-//");
                return b;
            });

        // Batch 1 25.
        add("WXOBS", "Weather observation",
            "Your own weather observation: where, when, wind, visibility, cloud, temperature and pressure.",
            "ACP127_PLAINDRESS", 'R', kEnvelopes,
            {f_exer(), f_msgid_from(),
             F("location", "Where", "text", true, "",
               "Your location, as an airport code (ICAO:KCVG) or a map grid (UTMO:17SKD).", "ICAO:KCVG"),
             F("obstime", "Observed at", "dtg", true, "$NOW_DTG", "When you observed, as DDHHMMZ.", "071950Z"),
             F("wind_dir", "Wind from (degrees)", "number", true, "", "Where the wind comes from, 000 to 360.", "180"),
             F("wind_speed", "Wind speed (knots)", "number", true, "", "Wind speed in knots.", "07"),
             F("visibility", "Visibility (statute miles)", "number", true, "", "How far you can see, in miles.", "10"),
             F("cloud", "Cloud cover", "text", true, "", "FEW, SCATTERED, BROKEN or COVERED, as you see it.", "FEW"),
             F("temp", "Temperature", "text", true, "", "Air temperature with its unit, like 17.0C.", "17.0C"),
             F("temp2", "Second temperature", "text", true, "",
               "The second temperature of the TEMP line; the samples repeat the air temperature.", "17.0C"),
             F("altimeter", "Pressure (inches of mercury)", "text", true, "", "The altimeter setting, like 29.86.",
               "29.86"),
             F("wx_remarks", "Remarks", "text", false, "DP:-/SLP:NOT REPORTED",
               "Dew point and sea-level pressure if you have them.", "DP:61F/SLP:NOT REPORTED")},
            [msgid](const Values& m, const RenderCtx& r) {
                Body b;
                exer_line(m, b.lines);
                b.lines.push_back(msgid(m, "WXOBS", r));
                b.lines.push_back("WEATHLOC/" + v(m, "location") + "//");
                b.lines.push_back("OBSTIME/" + v(m, "obstime") + "//");
                b.lines.push_back("WIND/" + v(m, "wind_dir") + "/" + v(m, "wind_speed") + "//");
                b.lines.push_back("VSBY/" + v(m, "visibility") + "/SM//");
                b.lines.push_back("CLDLYR/-/" + v(m, "cloud") + "//");
                b.lines.push_back("TEMP/" + v(m, "temp") + "/" + v(m, "temp2") + "//");
                b.lines.push_back("ALTSTG/HG:" + v(m, "altimeter") + "//");
                if (!v(m, "wx_remarks").empty()) b.lines.push_back("RMKS/" + v(m, "wx_remarks") + "//");
                return b;
            });

        // Batch 1 10; batch 2 13, 14. The samples send these as ACP 127
        // plaindress with '#' in the text, which ACP 127(G) doesn't allow, so
        // this sends them as 16L, which carries '#' as MSC does.
        add("SVC", "Service message",
            "A message between operators about earlier traffic: it names each message by its serial and "
            "date-time group, then says what is wrong or needed.",
            "16L_P2P_FULL", 'P', {"16L_P2P_FULL", "16L_P2P_ABBR", "16L_RI_FULL", "16L_RI_ABBR"},
            {F("refs", "Messages referred to", "lines", true, "",
               "Each message you are writing about, one per line: its serial after # and its date-time group.",
               "#147 060426Z NOV 2026"),
             F("svc_text", "What you need to say", "lines", true, "",
               "What is wrong with those messages or what you need from their sender.",
               "1. THESE MESSAGES USE AN OLD STRIP. PLEASE RESEND WITH THE CURRENT ONE.")},
            [](const Values& m, const RenderCtx&) {
                Body b;
                b.svc = true;
                for (auto& l : lines_of(v(m, "refs"))) b.lines.push_back("REF YOUR " + trim(l));
                for (auto& l : lines_of(v(m, "svc_text"))) b.lines.push_back(l);
                return b;
            });

        // Batch 1 44, 46; batch 2 13, 14, 28-43 (chat).
        std::vector<std::string> any = kEnvelopes;
        any.push_back("ACP201_CHAT");
        any.push_back("ACP127_CODRESS");
        add("FREE_FORM", "Free text",
            "Plain text with no set format: a net check-in, a note to another operator, or a chat line.",
            "16L_P2P_FULL", 'R', any,
            {F("text", "Text", "lines", true, "", "What you want to send. A chat line takes one line only.",
               "QRU U,A")},
            [](const Values& m, const RenderCtx&) {
                Body b;
                b.lines = lines_of(v(m, "text"));
                return b;
            });
        return T;
    }();
    return t;
}

const TemplateDef* find_def(const std::string& id) {
    for (auto& d : template_table())
        if (d.t.id == id) return &d;
    return nullptr;
}

// ---------------------------------------------------------------- checks

struct Checker {
    std::vector<Issue> issues;
    void add(const std::string& field, const std::string& level, const std::string& text, const std::string& fix,
             const std::string& example = "") {
        issues.push_back(Issue{field, level, text, fix, example});
    }
    bool errors() const {
        for (auto& i : issues)
            if (i.level == "error") return true;
        return false;
    }
};

// Characters ACP 127 text may carry, as acp127::valid_chars.
bool acp_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '?' || c == ':' ||
           c == '(' || c == ')' || c == '.' || c == ',' || c == '/';
}

std::string acp_clean(const std::string& s) {
    std::string out;
    for (char c : upper(s)) {
        if (acp_char(c) || c == '\n') out += c;
        else if (c == '&') out += " AND ";
        else if (c == '#') out += "NR";
        else if (c == '"') out += "";
        else out += ' ';
    }
    return out;
}

std::string first_line(const std::string& s) {
    size_t nl = s.find('\n');
    return nl == std::string::npos ? s : s.substr(0, nl);
}

// One address line: "RI/PLAIN NAME" or a plain name alone.
void check_address(Checker& c, const Field& f, const std::string& line, bool strict_chars) {
    std::string l = trim(line);
    size_t slash = l.find('/');
    std::string head = slash == std::string::npos ? std::string() : l.substr(0, slash);
    std::string plain = slash == std::string::npos ? l : l.substr(slash + 1);
    if (slash != std::string::npos) {
        if (!acp127::valid_ri(head) || has_lower(head)) {
            c.add(f.key, "error", f.label + ": \"" + head + "\" isn't a routing indicator.",
                  "Before the slash goes a routing indicator: 3 to 12 capital letters and digits.", f.example);
            return;
        }
        if (plain.find('/') != std::string::npos) {
            c.add(f.key, "error", f.label + ": only one slash is allowed, between the routing indicator and the name.",
                  "Remove the other slashes from the name.", f.example);
            return;
        }
        if (trim(plain).empty())
            c.add(f.key, "warning", f.label + ": the routing indicator has no name after the slash.",
                  "Add the plain name after the slash so readers know who it is.", f.example);
        return;
    }
    auto w = words(l);
    if (!w.empty() && starts_with(upper(w[0]), "UH") && acp127::valid_ri(upper(w[0]))) {
        if (w.size() == 1) {
            c.add(f.key, "warning", f.label + ": " + w[0] + " is a routing indicator with no name.",
                  "Add a slash and the plain name after it.", upper(w[0]) + "/" + "REGION FOUR RJMOG");
        } else {
            std::string fixed = upper(w[0]) + "/" + join(std::vector<std::string>(w.begin() + 1, w.end()), " ");
            c.add(f.key, "error",
                  f.label + ": the routing indicator and the name need a slash between them, not a space.",
                  "Write the routing indicator, a slash, then the name.", strict_chars ? upper(fixed) : fixed);
        }
    }
}

void check_field(Checker& c, const Field& f, const std::string& val, const ProcInfo& pi) {
    const bool strict_chars = strict(pi.env);
    if (val.empty()) {
        if (f.required)
            c.add(f.key, "error", f.label + " is empty.", f.hint, f.example);
        return;
    }
    if (!multi_line(f.kind) && val.find('\n') != std::string::npos) {
        c.add(f.key, "error", f.label + " must be one line.", "Put it all on one line.", first_line(val));
        return;
    }
    // Content that never goes on the air.
    if (is_prose(f.kind) && looks_like_frequency(val)) {
        c.add(f.key, "error", f.label + " looks like it holds a frequency.",
              "MARS frequencies are controlled information and are never sent in the clear. Name the channel by "
              "its designator from your net plan instead.");
        return;
    }
    for (char ch : val)
        if (ch == '\t') {
            c.add(f.key, "error", f.label + " has a tab in it.", "Use spaces instead of tabs; tabs don't survive the radio path.");
            return;
        }
    for (unsigned char ch : val)
        if (ch >= 0x80 || (ch < 0x20 && ch != '\n')) {
            c.add(f.key, "error", f.label + " has a character the radio can't send.",
                  "Use plain letters, digits and punctuation from the keyboard: no accents, curly quotes or symbols.");
            return;
        }

    const std::string& k = f.kind;
    if (k == "call") {
        if (has_lower(val)) {
            c.add(f.key, "error", f.label + " must be in capitals.", "Write it in capital letters.", upper(val));
        } else if (!acp127::valid_ri(val)) {
            c.add(f.key, "error", f.label + " \"" + val + "\" isn't a call sign or routing indicator.",
                  "Use 3 to 12 capital letters, digits and -, with at least one letter.", f.example);
        }
    } else if (k == "stations") {
        auto w = words(val);
        for (auto& s : w) {
            if (has_lower(s)) {
                c.add(f.key, "error", f.label + " must be in capitals.", "Write them in capital letters.", upper(val));
                return;
            }
            if (!acp127::valid_ri(s) && !(pi.env == Env::Chat && s == "ALL")) {
                c.add(f.key, "error", f.label + ": \"" + s + "\" isn't a call sign or routing indicator.",
                      "Use 3 to 12 capital letters, digits and -, separated by spaces.", f.example);
                return;
            }
        }
        if (pi.ri && is_16l(pi.env))
            for (auto& s : w)
                if (!starts_with(s, "UH")) {
                    c.add(f.key, "warning", f.label + ": " + s + " isn't a routing indicator, and 16L RI messages go to RIs.",
                          "Check the addressee, or send it as 16L station to station.");
                    break;
                }
    } else if (k == "address" || k == "addresses") {
        for (auto& l : lines_of(val))
            if (!trim(l).empty()) check_address(c, f, l, strict_chars);
    } else if (k == "dtg") {
        if (!valid_dtg(val))
            c.add(f.key, "error", f.label + " \"" + val + "\" isn't a date-time group.",
                  "Write the day, hour and minute as six digits, then Z: DDHHMMZ.", f.example);
    } else if (k == "dtg_or_dash") {
        if (val != "-" && !valid_dtg(val))
            c.add(f.key, "error", f.label + " \"" + val + "\" isn't a date-time group.",
                  "Write the day, hour and minute as six digits, then Z (DDHHMMZ), or - if there is none.", f.example);
    } else if (k == "ddhhmm") {
        if (!valid_ddhhmm(val))
            c.add(f.key, "error", f.label + " \"" + val + "\" isn't a time.",
                  "Write the day, hour and minute as six digits: DDHHMM, in UTC.", f.example);
    } else if (k == "datetime") {
        if (!valid_datetime(val))
            c.add(f.key, "error", f.label + " \"" + val + "\" isn't a full date and time.",
                  "Write year, month, day, hour and minute as twelve digits, then Z.", f.example);
    } else if (k == "yn") {
        if (val != "Y" && val != "N")
            c.add(f.key, "error", f.label + " must be Y or N.", "Write a capital Y or N.",
                  upper(val).substr(0, 1) == "N" ? "N" : "Y");
    } else if (k == "number") {
        if (!all_digits(val)) c.add(f.key, "error", f.label + " must be a number.", "Use digits only.", f.example);
    } else if (k == "state") {
        if (val.size() != 2 || !std::isalpha(static_cast<unsigned char>(val[0])) ||
            !std::isalpha(static_cast<unsigned char>(val[1])) || has_lower(val))
            c.add(f.key, "error", f.label + " must be the two-letter state code.", "Write two capital letters.",
                  val.size() == 2 ? upper(val) : f.example);
    } else if (k == "choice" || k == "prec") {
        if (std::find(f.choices.begin(), f.choices.end(), val) == f.choices.end()) {
            std::string up = upper(val);
            bool near = std::find(f.choices.begin(), f.choices.end(), up) != f.choices.end();
            c.add(f.key, "error", f.label + " must be one of " + join(f.choices, ", ") + ".",
                  near ? "Write it in capitals." : "Pick one of the choices.", near ? up : f.def.empty() ? f.choices[0] : f.def);
        }
    } else if (k == "value") {
        if (val.find('/') != std::string::npos)
            c.add(f.key, "error", f.label + " has a slash in it, and a slash ends a strip field.",
                  "Leave the slash out, or use a space.", trim([&] {
                      std::string s = val;
                      std::replace(s.begin(), s.end(), '/', ' ');
                      return s;
                  }()));
    } else if (k == "groups") {
        for (auto& l : lines_of(val))
            for (auto& g : words(l)) {
                bool ok = g.size() == 4;
                for (char ch : g)
                    if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) ok = false;
                if (!ok) {
                    c.add(f.key, "error", f.label + ": \"" + g + "\" isn't a four-character group.",
                          "Copy the groups exactly as received: four capital letters or digits each.");
                    return;
                }
            }
    } else if (k == "metars") {
        for (auto& l : lines_of(val)) {
            auto w = words(l);
            if (w.empty()) continue;
            std::string first = upper(w[0]);
            if (first == "METAR" && w.size() > 1) first = upper(w[1]);
            if (first.size() != 4 || w.size() < 2) {
                c.add(f.key, "error", f.label + ": a line doesn't start with a four-letter airport code.",
                      "Start each line with the airport code, then the time and the report.", f.example);
                return;
            }
            if (upper(w[0]) == "METAR") {
                c.add(f.key, "warning", f.label + ": a line starts with the word METAR.",
                      "Leave METAR off; it is added for you.", join(std::vector<std::string>(w.begin() + 1, w.end()), " "));
                return;
            }
        }
    }

    // USMTF sets end at "//"; one inside a value ends the set early.
    if ((k == "text" || k == "lines") && val.find("//") != std::string::npos)
        c.add(f.key, "error", f.label + " has \"//\" in it, which ends the line's set early.",
              "Remove the double slash; the template adds the ending for you.", [&] {
                  std::string s = val;
                  for (size_t p = s.find("//"); p != std::string::npos; p = s.find("//", p)) s.replace(p, 2, "/");
                  return s;
              }());

    // Characters the procedure can carry.
    if (strict_chars) {
        for (char ch : upper(val))
            if (!acp_char(ch) && ch != '\n') {
                c.add(f.key, "error",
                      f.label + std::string(": '") + ch + "' can't be sent in " + pi.p.title +
                          ", which carries only A-Z, 0-9, space and - ? : ( ) . , /.",
                      "Write AND for &, NR for #, and leave other symbols out; or send it as 16L, which carries them.",
                      acp_clean(val));
                break;
            }
    }

    if (k == "strips" || k == "metars") {
        if (pi.env == Env::Chat && lines_of(val).size() > 1)
            c.add(f.key, "error", f.label + ": a chat line carries one strip only.",
                  "Send one strip per chat line, or send them as a message.", lines_of(val)[0]);
    }
}

}  // namespace

// ---------------------------------------------------------------- public

const std::vector<Procedure>& procedures() {
    static const std::vector<Procedure> p = [] {
        std::vector<Procedure> out;
        for (auto& i : proc_table()) out.push_back(i.p);
        return out;
    }();
    return p;
}

const std::vector<Template>& templates() {
    static const std::vector<Template> t = [] {
        std::vector<Template> out;
        for (auto& d : template_table()) out.push_back(d.t);
        return out;
    }();
    return t;
}

const Procedure* find_procedure(const std::string& id) {
    const ProcInfo* i = find_proc(id);
    return i ? &i->p : nullptr;
}

const Template* find_template(const std::string& id) {
    const TemplateDef* d = find_def(id);
    return d ? &d->t : nullptr;
}

namespace {

// The fields with defaults resolved, or empty with *error.
std::vector<Field> raw_form(const TemplateDef& d, const ProcInfo& pi) {
    std::vector<Field> out = envelope(pi, d.t.prec);
    for (auto& f : d.fields) out.push_back(f);
    return out;
}

// The key field's choices: the keys current now.
void fill_keys(std::vector<Field>& fields, const Context& ctx) {
    for (auto& f : fields)
        if (f.key == "key" && f.kind == "choice") {
            f.choices.clear();
            for (auto& k : ctx.codress_keys) f.choices.push_back(k.indicator);
            if (!f.choices.empty()) {
                f.def = f.choices[0];
                f.example = f.choices[0];
                std::string nets;
                for (auto& k : ctx.codress_keys) nets += (nets.empty() ? "" : ", ") + k.indicator + " " + k.net;
                f.hint += " Loaded now: " + nets + ".";
            }
        }
}

bool pick(const std::string& template_id, const std::string& procedure, const TemplateDef*& d, const ProcInfo*& pi,
          std::string* error) {
    d = find_def(template_id);
    if (!d) {
        if (error) *error = "There is no template called \"" + template_id + "\".";
        return false;
    }
    std::string proc = procedure.empty() ? d->t.procedure : procedure;
    pi = find_proc(proc);
    if (!pi) {
        if (error) *error = "There is no procedure called \"" + proc + "\".";
        return false;
    }
    if (std::find(d->t.procedures.begin(), d->t.procedures.end(), proc) == d->t.procedures.end()) {
        if (error) *error = d->t.title + " isn't sent as " + pi->p.title + ". Use one of: " + join(d->t.procedures, ", ") + ".";
        return false;
    }
    if (!pi->p.available) {
        if (error) *error = pi->p.why_not;
        return false;
    }
    return true;
}

}  // namespace

std::string codress_why_not(const Context& ctx) {
    if (ctx.codress_keys.empty())
        return "Codress needs a key: no key for today is loaded on this station. Import your net's key tape first.";
    return "";
}

std::vector<Field> form(const std::string& template_id, const std::string& procedure, const Context& ctx,
                        std::string* error) {
    const TemplateDef* d = nullptr;
    const ProcInfo* pi = nullptr;
    if (!pick(template_id, procedure, d, pi, error)) return {};
    if (pi->env == Env::Codress && !codress_why_not(ctx).empty()) {
        if (error) *error = codress_why_not(ctx);
        return {};
    }
    std::vector<Field> out = raw_form(*d, *pi);
    fill_keys(out, ctx);
    for (auto& f : out) f.def = resolve(f.def, ctx);
    if (error) error->clear();
    return out;
}

namespace {

// The 16L forms MSC sends (see received.h): format lines 2 and 3 folded.
std::string build_16l(const ProcInfo& pi, const Values& m, const Body& body, const Serials& s, const std::tm& u,
                      char prec, bool limit) {
    std::vector<std::string> L;
    std::string chan = v(m, "channel");
    L.push_back(chan.empty() ? "VZCZC" : "VZCZC" + chan + pad(s.channel_serial, 3));
    std::string head = pi.env == Env::L16Simple ? std::string() : std::string(2, prec) + " ";
    head += join(words(v(m, "routing")), " ") + " DE " + v(m, "originator") + " #" + pad(s.station_serial, 4) + " " +
            pad(u.tm_yday + 1, 3) + two(u.tm_hour) + two(u.tm_min);
    L.push_back(head);
    if (pi.env == Env::L16Full) {
        L.push_back("FM " + trim(v(m, "from")));
        auto put = [&](const char* p, const std::string& key) {
            bool first = true;
            for (auto& l : lines_of(v(m, key))) {
                if (trim(l).empty()) continue;
                L.push_back((first ? std::string(p) + " " : std::string()) + trim(l));
                first = false;
            }
        };
        put("TO", "to");
        put("INFO", "info");
    }
    L.push_back("BT");
    L.push_back(body.svc ? "UNCLAS SVC" : "UNCLAS");
    for (size_t i = 0; i < body.lines.size(); ++i) {
        const std::string& line = body.lines[i];
        if (!limit || body.keep_whole.count(i) || line.size() <= acp127::kMaxLine) {
            L.push_back(line);
            continue;
        }
        std::string cur;
        for (auto& w : words(line)) {
            if (cur.empty()) cur = w;
            else if (cur.size() + 1 + w.size() <= acp127::kMaxLine) cur += " " + w;
            else {
                L.push_back(cur);
                cur = w;
            }
        }
        if (!cur.empty()) L.push_back(cur);
    }
    L.push_back("BT");
    std::string out;
    for (size_t i = 0; i < L.size(); ++i) out += L[i] + (i + 1 < L.size() ? "\r\r\n" : "\r\r\n\n\n\n\n\n\n\n");
    return out + "NNNN";
}

acp127::Address address(const std::string& s) {
    acp127::Address a;
    std::string t = trim(s);
    size_t slash = t.find('/');
    if (slash != std::string::npos) {
        a.ri = t.substr(0, slash);
        a.plain = t.substr(slash + 1);
    } else {
        a.plain = t;
    }
    return a;
}

std::vector<acp127::Address> addresses(const std::string& s) {
    std::vector<acp127::Address> out;
    for (auto& l : lines_of(s))
        if (!trim(l).empty()) out.push_back(address(l));
    return out;
}

// Builder errors are the format's words; say them in the operator's.
std::string plain_error(const std::string& err) {
    if (err.find("over 69") != std::string::npos || err.find("longer than 69") != std::string::npos)
        return "A line is longer than 69 characters, which can't go with lines broken at 69.";
    if (err.find("not allowed") != std::string::npos) return "The text has a character this procedure can't send.";
    if (err.find("no text") != std::string::npos) return "The message has no text.";
    return "The message couldn't be put together (" + err + ").";
}

}  // namespace

Result check(const Draft& d, const Context& ctx, const Serials& serials) {
    Result r;
    Checker c;
    const TemplateDef* def = nullptr;
    const ProcInfo* pi = nullptr;
    std::string err;
    if (!pick(d.template_id, d.procedure, def, pi, &err)) {
        c.add("", "error", err, "Pick a template and a procedure from the lists.");
        r.issues = c.issues;
        return r;
    }
    r.procedure = pi->p.id;
    if (pi->env == Env::Codress && !codress_why_not(ctx).empty()) {
        c.add("", "error", codress_why_not(ctx),
              "Import your net's key tape from its USB stick (appliance-cli keys import FILE), or send it plaindress.");
        r.issues = c.issues;
        return r;
    }
    std::vector<Field> fields = raw_form(*def, *pi);
    fill_keys(fields, ctx);

    // Values: given, or the default. Strict procedures carry capitals only, so
    // their text is put in capitals here and the operator told once.
    Values val;
    bool upcased = false;
    for (auto& f : fields) {
        auto it = d.values.find(f.key);
        std::string s = it == d.values.end() ? resolve(f.def, ctx) : it->second;
        if (multi_line(f.kind)) s = join(lines_of(s), "\n");
        else s = trim(s);
        if (strict(pi->env) && has_lower(s) && f.kind != "call" && f.kind != "stations") {
            s = upper(s);
            upcased = true;
        }
        val[f.key] = s;
    }
    for (auto& kv : d.values)
        if (kv.first != "cmd" && !val.count(kv.first) && !kv.second.empty())
            c.add(kv.first, "warning", "\"" + kv.first + "\" isn't a field of this form and was left out.",
                  "Check the field name against the form.");

    // The station book: a plain name with no routing indicator on an
    // address line gets the book's RI, and a book name may stand where the
    // station to send to goes.
    std::optional<BookHit> to_hit;
    if (ctx.lookup) {
        for (auto& f : fields) {
            if (f.kind != "address" && f.kind != "addresses") continue;
            std::vector<std::string> out, filled;
            for (auto& l : lines_of(val[f.key])) {
                std::string t = trim(l);
                if (!t.empty() && t.find('/') == std::string::npos) {
                    auto h = ctx.lookup(t);
                    if (h && !h->ri.empty()) {
                        out.push_back(h->ri + "/" + t);
                        filled.push_back(t);
                        continue;
                    }
                }
                out.push_back(l);
            }
            if (filled.empty()) continue;
            val[f.key] = join(out, "\n");
            c.add(f.key, "tip",
                  f.label + ": the routing indicator for " + join(filled, ", ") + " came from the station book.",
                  "Nothing to do.");
        }
        if (!trim(val["to_station"]).empty()) to_hit = ctx.lookup(trim(val["to_station"]));
    }
    for (auto& f : fields) {
        if (f.key == "to_station" && to_hit) continue;  // a book name, checked below
        check_field(c, f, val[f.key], *pi);
    }
    if (upcased)
        c.add("", "tip", "Lower-case letters were changed to capitals: " + pi->p.title + " carries capitals only.",
              "Nothing to do.");

    // A reference is all or nothing.
    bool has_ref_fields = val.count("ref_from");
    if (has_ref_fields) {
        bool any = !val["ref_from"].empty() || !val["ref_dtg"].empty();
        if (any) {
            if (val["ref_from"].empty())
                c.add("ref_from", "error", "The message you refer to needs its sender.",
                      "Write the sender's name as its MSGID line has it.", "AMARS REGION FOUR OPS OFFICER");
            if (val["ref_dtg"].empty())
                c.add("ref_dtg", "error", "The message you refer to needs its date-time group.",
                      "Write when it was sent, as DDHHMMZ.", "022004Z");
            if (val["ref_type"].empty())
                c.add("ref_type", "error", "The message you refer to needs its type.", "Pick its type from the list.",
                      "RI");
        }
    }
    if (val.count("ack") && val["ack"] == "YES" && val.count("ack_how") && val["ack_how"].empty() &&
        def->t.id == "GENADMIN")
        c.add("ack_how", "warning", "You ask for an acknowledgement without saying how.",
              "Say who to acknowledge to, like AKNLDG TO and your routing indicator.", "AKNLDG TO UHAAAA");

    // Precedence.
    char prec = pi->env == Env::L16Simple || pi->env == Env::Acp126Simple || pi->env == Env::Chat ? 'R' : def->t.prec;
    if (val.count("prec") && val["prec"].size() == 1) prec = val["prec"][0];
    if (val.count("prec")) {
        if (prec == 'Z')
            c.add("prec", "warning", "FLASH is the highest precedence, and no MARS sample uses it.",
                  "Use FLASH only when your instructions call for it; otherwise IMMEDIATE (O) or lower.",
                  std::string(1, def->t.prec));
        else if (prec != def->t.prec && (prec == 'R' || prec == 'P' || prec == 'O'))
            c.add("prec", "tip",
                  std::string("MARS stations usually send ") + def->t.title + " at " + acp127::prec_name(def->t.prec) +
                      "; this is " + acp127::prec_name(prec) + ".",
                  "Keep it if your instructions say so.", std::string(1, def->t.prec));
    }

    // Who the 16L forms come from.
    if (is_16l(pi->env) && !val["originator"].empty()) {
        bool ri = starts_with(val["originator"], "UH");
        if (pi->ri && !ri)
            c.add("originator", "error", "16L RI messages are sent from a routing indicator (UH...), and " +
                                             val["originator"] + " isn't one.",
                  "If you send from your call sign, use 16L station to station (16L_P2P_" +
                      std::string(pi->env == Env::L16Full ? "FULL" : "ABBR") + ").",
                  "UHAAAC");
        if (!pi->ri && ri)
            c.add("originator", "error", val["originator"] + " is a routing indicator, and 16L station to station "
                                         "is sent from a call sign.",
                  std::string("Use 16L RI (16L_RI_") + (pi->env == Env::L16Full ? "FULL" : "ABBR") +
                      ") to send from a routing indicator.",
                  ctx.station.empty() ? "AAR4XX" : ctx.station);
    }
    if (val.count("channel") && !val["channel"].empty()) {
        const std::string& ch = val["channel"];
        if (ch.size() != 3 || !std::isalpha(static_cast<unsigned char>(ch[0])) ||
            !std::isalpha(static_cast<unsigned char>(ch[1])) || !std::isalpha(static_cast<unsigned char>(ch[2])) ||
            has_lower(ch))
            c.add("channel", "error", "Channel letters must be three capital letters.",
                  "Use MMM unless your instructions give others, or leave it empty for a bare VZCZC.", "MMM");
    }
    if (pi->env == Env::Chat) {
        Body b0 = def->render(val, RenderCtx{"1"});
        if (b0.lines.size() > 1)
            c.add(def->t.id == "FREE_FORM" ? "text" : "", "error", "A chat line carries one line of text.",
                  "Put it on one line, or send it as a message (16L_P2P_FULL).", b0.lines.empty() ? "" : b0.lines[0]);
    }

    // Breaking lines at 69 is an option (Message Creator's), off by default:
    // MARS traffic carries longer lines. With it on, a strip that can't fit
    // and a word too long to break are refused; prose is broken for you.
    const bool limit = val.count("wrap") && val["wrap"] == "Y";
    if (limit && !c.errors()) {
        Body b0 = def->render(val, RenderCtx{"1"});
        for (size_t i = 0; i < b0.lines.size(); ++i) {
            size_t n = b0.lines[i].size();
            bool strip = b0.keep_whole.count(i) > 0;
            size_t longest = 0;
            for (auto& w : words(b0.lines[i])) longest = std::max(longest, w.size());
            if (strip && n > acp127::kMaxLine) {
                c.add("wrap", "error",
                      "A strip is " + std::to_string(n) +
                          " characters. Strips are never broken, so it can't go with lines broken at 69.",
                      "Set Break lines at 69 characters to N, or shorten the strip.", "N");
                break;
            }
            if (longest > acp127::kMaxLine) {
                c.add("wrap", "error",
                      "A line has " + std::to_string(longest) +
                          " characters with no space, so it can't be broken at 69.",
                      "Set Break lines at 69 characters to N, or put spaces in it.", "N");
                break;
            }
        }
    }

    std::string to_station = trim(val["to_station"]);
    if (to_station.empty()) {
        auto w = words(val.count("routing") ? val["routing"] : val.count("calls") ? val["calls"] : val["call"]);
        if (!w.empty()) {
            to_station = w[0];
            if (ctx.lookup) to_hit = ctx.lookup(to_station);
        }
    }
    r.to_station = to_station;
    r.prec = prec;
    if (to_hit) {
        r.to_targets = to_hit->calls;
        if (r.to_targets.empty()) {
            c.add("to_station", "error",
                  "The station book has " + to_hit->name + " but no station in it to send to.",
                  "Add stations to " + to_hit->name + " in the station book, or write a call sign.");
        } else if (!(r.to_targets.size() == 1 && r.to_targets[0] == upper(to_station))) {
            c.add("to_station", "tip",
                  to_hit->kind == "collective"
                      ? "From the station book: " + to_hit->name + " goes to " + std::to_string(r.to_targets.size()) +
                            " stations, one copy each: " + join(r.to_targets, ", ") + "."
                      : "From the station book: " + to_hit->name + " is sent to " + r.to_targets[0] + ".",
                  "Nothing to do.");
        }
        if (!to_hit->unknown.empty())
            c.add("to_station", "warning",
                  to_hit->name + " lists " + join(to_hit->unknown, ", ") +
                      ", which the station book doesn't have: the copy goes to that name as written.",
                  "Check the spelling, or add it to the station book with its call.");
    } else if (upper(to_station) == "NET" || upper(to_station) == "ALL") {
        // Calling the whole net: one broadcast that every station takes and none acks.
        r.to_targets = {"ALL"};
    } else if (!to_station.empty()) {
        r.to_targets = {to_station};
    }

    if (c.errors()) {
        r.issues = c.issues;
        return r;
    }

    // Build.
    std::tm u = utc(ctx);
    std::string msg;
    switch (pi->env) {
        case Env::Codress:
            r.codress_key = val["key"];
            [[fallthrough]];
        case Env::Acp127: {
            Body b = def->render(val, RenderCtx{std::to_string(serials.station_serial)});
            acp127::Message m;
            m.prec = prec;
            m.channel = val["channel"];
            m.channel_serial = serials.channel_serial;
            m.routing = words(val["routing"]);
            m.mars_fl3 = true;
            m.originator_ri = val["originator"];
            m.station_serial = std::to_string(serials.station_serial);
            m.filing_yday = u.tm_yday + 1;
            m.filing_day = m.dtg.day = u.tm_mday;
            m.filing_hour = m.dtg.hour = u.tm_hour;
            m.filing_minute = m.dtg.minute = u.tm_min;
            m.dtg.zone = 'Z';
            m.dtg.month = u.tm_mon + 1;
            m.dtg.year = u.tm_year + 1900;
            m.from = address(val["from"]);
            m.to = addresses(val["to"]);
            m.info = addresses(val["info"]);
            m.text = (b.svc ? "SVC\n" : "\n") + join(b.lines, "\n");
            m.limit_lines = limit;
            msg = acp127::build(m, &err);
            r.channel_counter = m.channel.empty() ? "" : "acp127.channel." + m.channel;
            r.serial_counter = "acp127.station." + m.originator_ri;
            r.serial_wrap = 9999;
            break;
        }
        case Env::L16Full:
        case Env::L16Abbr:
        case Env::L16Simple: {
            Body b = def->render(val, RenderCtx{std::to_string(serials.station_serial)});
            msg = build_16l(*pi, val, b, serials, u, prec, limit);
            r.channel_counter = val["channel"].empty() ? "" : "acp127.channel." + val["channel"];
            r.serial_counter = "acp127.station." + val["originator"];
            r.serial_wrap = 9999;
            break;
        }
        case Env::Acp126Plain:
        case Env::Acp126Simple: {
            Body b = def->render(val, RenderCtx{std::to_string(serials.nr)});
            acp126::Message m;
            m.procedure = pi->env == Env::Acp126Plain ? acp126::Message::Procedure::Plaindress
                                                      : acp126::Message::Procedure::Simple;
            m.channel = val["channel"];
            m.channel_serial = serials.channel_serial;
            m.calls = words(val["calls"]);
            m.station = val["station"];
            m.serial = serials.nr;
            m.prec = prec;
            m.dtg = acp127::Dtg{u.tm_mday, u.tm_hour, u.tm_min, 'Z', u.tm_mon + 1, u.tm_year + 1900};
            if (pi->env == Env::Acp126Plain) {
                m.from = address(val["from"]);
                m.to = addresses(val["to"]);
            }
            m.text = join(b.lines, "\n");
            m.limit_lines = limit;
            msg = acp126::build(m, &err);
            r.channel_counter = m.channel.empty() ? "" : "acp127.channel." + m.channel;
            r.serial_counter = "acp126.nr." + m.station;
            r.serial_wrap = 999;
            break;
        }
        case Env::Chat: {
            Body b = def->render(val, RenderCtx{"1"});
            acp201::Chat ch;
            ch.to = join(words(val["call"]), " ");
            ch.from = val["station"];
            ch.text = b.lines.empty() ? "" : b.lines[0];
            ch.hour = u.tm_hour;
            ch.minute = u.tm_min;
            msg = acp201::build(ch, &err);
            break;
        }
    }
    // Every message but a chat line carries the MSC digest (digest.h).
    if (!msg.empty() && pi->env != Env::Chat) msg = digest::append(msg);
    if (msg.empty()) {
        c.add("", "error", plain_error(err), "Check the fields marked above, or send it as 16L, which is less strict.");
        r.issues = c.issues;
        return r;
    }
    r.message = msg;
    r.ready = true;
    r.issues = c.issues;
    return r;
}

// ---------------------------------------------------------------- advisor

namespace {
std::mutex g_advisor_mu;
std::shared_ptr<Advisor> g_advisor;

class ProgramAdvisor : public Advisor {
public:
    explicit ProgramAdvisor(std::vector<std::string> argv) : argv_(std::move(argv)) {}
    std::string name() const override { return argv_.empty() ? "program" : "program " + argv_[0]; }
    bool advise(const AdviceRequest& req, std::string& text, std::string* error) override {
        if (argv_.empty()) {
            if (error) *error = "no advisor program set";
            return false;
        }
        std::vector<std::string> a = argv_;
        a.push_back(advice_json(req));
        RunResult rr = run_program(a, 10000);
        if (!rr.started) {
            if (error) *error = "the advisor program couldn't be started";
            return false;
        }
        if (rr.timed_out) {
            if (error) *error = "the advisor program took longer than 10 seconds";
            return false;
        }
        if (rr.exit_code != 0) {
            if (error) *error = "the advisor program failed (exit " + std::to_string(rr.exit_code) + ")";
            return false;
        }
        std::string out = trim(rr.out);
        JsonValue j;
        if (!out.empty() && out[0] == '{' && parse_json(out, j) && j.is_object()) {
            const JsonValue* t = j.get("text");
            if (!t || t->type != JsonValue::Type::String) {
                if (error) *error = "the advisor program's reply has no text";
                return false;
            }
            out = trim(t->s);
        }
        if (out.empty()) {
            if (error) *error = "the advisor program gave no answer";
            return false;
        }
        if (out.size() > 4000) out = out.substr(0, 4000);
        text = out;
        return true;
    }

private:
    std::vector<std::string> argv_;
};
}  // namespace

void set_advisor(std::shared_ptr<Advisor> a) {
    std::lock_guard<std::mutex> lk(g_advisor_mu);
    g_advisor = std::move(a);
}

std::shared_ptr<Advisor> advisor() {
    std::lock_guard<std::mutex> lk(g_advisor_mu);
    return g_advisor;
}

std::shared_ptr<Advisor> program_advisor(const std::vector<std::string>& argv) {
    return std::make_shared<ProgramAdvisor>(argv);
}

std::string advice_json(const AdviceRequest& req) {
    JsonWriter w;
    w.begin_object()
        .field("kind", req.kind)
        .field("field", req.field)
        .field("question", req.question)
        .field("template", req.draft.template_id)
        .field("procedure", req.draft.procedure);
    if (const TemplateDef* d = find_def(req.draft.template_id)) {
        w.field("template_title", d->t.title).field("template_summary", d->t.summary);
        for (auto& f : d->fields)
            if (f.key == req.field) w.field("field_label", f.label).field("field_hint", f.hint).field("field_example", f.example);
    }
    w.begin_array("values");
    for (auto& kv : req.draft.values) w.begin_object().field("key", kv.first).field("value", kv.second).end_object();
    w.end_array();
    w.begin_array("issues");
    for (auto& i : req.issues)
        w.begin_object()
            .field("field", i.field)
            .field("level", i.level)
            .field("text", i.text)
            .field("fix", i.fix)
            .field("example", i.example)
            .end_object();
    w.end_array().end_object();
    return w.str();
}

std::string builtin_advice(const AdviceRequest& req, const Context& ctx) {
    std::string out;
    if (req.kind == "suggest")
        out = "No helper is set up to suggest wording, so here is what the form asks for. ";
    std::string err;
    std::vector<Field> fields = form(req.draft.template_id, req.draft.procedure, ctx, &err);
    if (fields.empty()) return out + err;
    if (!req.field.empty()) {
        for (auto& f : fields) {
            if (f.key != req.field) continue;
            out += f.label + ": " + f.hint;
            if (!f.example.empty()) out += " For example: " + f.example + ".";
            if (!f.choices.empty()) out += " Choices: " + join(f.choices, ", ") + ".";
            if (!f.required) out += " You can leave it empty.";
            for (auto& i : req.issues)
                if (i.field == f.key) out += " " + i.text + " " + i.fix;
            return out;
        }
        return out + "This form has no field called \"" + req.field + "\".";
    }
    const Template* t = find_template(req.draft.template_id);
    out += t->title + ": " + t->summary;
    std::vector<const Issue*> errors;
    for (auto& i : req.issues)
        if (i.level == "error") errors.push_back(&i);
    if (errors.empty()) {
        if (!req.issues.empty() || !req.draft.values.empty()) out += " Nothing stops it being sent.";
    } else {
        out += " To finish it:";
        for (size_t i = 0; i < errors.size(); ++i)
            out += " " + std::to_string(i + 1) + ". " + errors[i]->text + " " + errors[i]->fix;
    }
    return out;
}

}  // namespace compose
}  // namespace pna
