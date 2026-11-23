#include "mars_import.h"

#include "channels.h"
#include "compose.h"
#include "ops.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace pna {
namespace marsio {

namespace {

std::string up(const std::string& s) {
    std::string o;
    for (char c : s) o += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return book_key(o);
}

// A header as a key: lower case letters and digits only.
std::string hkey(const std::string& s) {
    std::string o;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c))) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

// Each field and the headers that mean it.
const std::map<std::string, std::vector<std::string>>& synonyms() {
    static const std::map<std::string, std::vector<std::string>> m = {
        // stations
        {"call", {"call", "callsign", "calls", "marscall", "marscallsign", "billet", "billetcall", "stationcall",
                  "station", "marsid", "mcall"}},
        {"name", {"pla", "plainlanguageaddress", "shortname", "tactical", "tacticalcall", "addressee", "entry"}},
        {"ri", {"ri", "routingindicator", "routing", "routingind"}},
        {"ale", {"ale", "aleaddress", "selfaddress", "aleself", "aleid", "aleaddr"}},
        {"members", {"members", "member", "memberstations", "includes", "stationsincluded"}},
        {"kind", {"kind", "type", "entrytype"}},
        {"region", {"region", "state", "area", "district", "division", "territory"}},
        {"role", {"role", "position", "duty", "assignment", "title", "office", "function"}},
        {"group", {"group", "groups", "collective", "collectives", "team", "distribution", "list"}},
        // channels
        {"designator", {"designator", "channel", "chan", "ch", "channelname", "chdesignator", "chan#", "channo",
                        "channelnumber", "chnum", "chname", "id"}},
        {"freq", {"frequency", "freq", "khz", "mhz", "freqkhz", "freqmhz", "frequencykhz", "frequencymhz", "qrg",
                  "dial", "dialfreq", "rxfreq", "rx", "carrier"}},
        {"mode", {"mode", "emission", "sideband", "modulation"}},
        {"use", {"use", "purpose", "usage", "net", "nets", "remarks", "notes", "comment", "comments", "note"}},
        // schedules
        {"net", {"net", "netname", "nets", "name", "activity", "event"}},
        {"days", {"day", "days", "dayofweek", "when", "schedule", "meets", "occurs"}},
        {"time", {"time", "start", "starttime", "timez", "timeutc", "utc", "zulu", "z", "starttimez", "startutc",
                  "startz"}},
        {"end", {"end", "endtime", "stop", "endz", "endutc"}},
        {"minutes", {"duration", "length", "minutes", "mins", "durationmin", "durationminutes"}},
        {"ncs", {"ncs", "netcontrol", "control", "netcontrolstation"}},
        {"roster", {"roster", "stations", "participants", "checkins", "expected"}},
        {"note", {"notes", "note", "remarks", "comments", "comment"}},
    };
    return m;
}

// Headers that carry personal details: never imported, always listed.
bool personal(const std::string& k) {
    static const std::vector<std::string> p = {
        "name", "first", "last", "fname", "lname", "firstname", "lastname", "fullname", "membername", "operator",
        "email", "mail", "phone", "cell", "mobile", "home", "work", "fax", "street", "address", "addr", "city",
        "zip", "postal", "county", "dob", "birth", "spouse", "emergency", "nextofkin", "ssn", "license",
        "amateurcall", "hamcall", "fcccall", "personalcall", "callsignamateur", "employer"};
    for (auto& w : p)
        if (k == w || (w.size() >= 4 && k.find(w) != std::string::npos)) return true;
    return false;
}

using Fields = std::map<std::string, int>;  // field -> column

// The fields a header row maps to, for one kind.
Fields map_header(const Row& h, const std::vector<std::string>& wanted, std::vector<std::string>* left_out) {
    Fields f;
    for (size_t i = 0; i < h.size(); ++i) {
        std::string k = hkey(h[i]);
        if (k.empty()) continue;
        bool used = false;
        for (auto& field : wanted) {
            if (f.count(field)) continue;
            auto& syn = synonyms().at(field);
            if (std::find(syn.begin(), syn.end(), k) != syn.end()) {
                // "name" is a person's name on a roster, the entry's name in a book.
                f[field] = static_cast<int>(i);
                used = true;
                break;
            }
        }
        if (!used && left_out) left_out->push_back(h[i]);
    }
    return f;
}

const std::vector<std::string>& wanted(const std::string& kind) {
    static const std::map<std::string, std::vector<std::string>> w = {
        {"roster", {"call", "ale", "ri", "region", "role", "group"}},
        {"book", {"name", "call", "ri", "ale", "members", "kind", "region", "role", "group"}},
        {"freq", {"designator", "freq", "mode", "use"}},
        {"schedule", {"net", "days", "time", "end", "minutes", "ncs", "designator", "freq", "roster", "note"}},
    };
    return w.at(kind);
}

bool has(const Fields& f, const char* k) { return f.count(k) > 0; }

// How well a row works as this kind's header; 0 when its key fields are missing.
int score(const Row& r, const std::string& kind) {
    Fields f = map_header(r, wanted(kind), nullptr);
    int n = static_cast<int>(f.size());
    if (kind == "roster" && !has(f, "call")) return 0;
    if (kind == "book" && !(has(f, "call") || has(f, "name")) ) return 0;
    if (kind == "book" && !(has(f, "ri") || has(f, "ale") || has(f, "members") || has(f, "name"))) return 0;
    if (kind == "freq" && !(has(f, "designator") && has(f, "freq"))) return 0;
    if (kind == "schedule" && !(has(f, "net") && has(f, "days") && has(f, "time"))) return 0;
    // A roster shows people; a book shows addressing.
    int personal_cols = 0;
    for (auto& c : r)
        if (!hkey(c).empty() && personal(hkey(c))) ++personal_cols;
    if (kind == "roster") n += personal_cols;
    if (kind == "book") n += (has(f, "ri") ? 1 : 0) + (has(f, "members") ? 1 : 0);
    return n;
}

std::string cell(const Row& r, const Fields& f, const char* k) {
    auto it = f.find(k);
    return it != f.end() && it->second < static_cast<int>(r.size()) ? r[static_cast<size_t>(it->second)] : "";
}

bool blank(const Row& r) {
    for (auto& c : r)
        if (!book_key(c).empty()) return false;
    return true;
}

// A line that repeats the header (sheets printed in pages do that).
bool same_as(const Row& r, const Row& h) {
    size_t n = 0, same = 0;
    for (size_t i = 0; i < std::min(r.size(), h.size()); ++i) {
        if (h[i].empty()) continue;
        ++n;
        same += hkey(r[i]) == hkey(h[i]);
    }
    return n && same == n;
}

std::string clip_note(std::string s) {
    if (compose::looks_like_frequency(s)) return "";
    s = up(s);
    if (s.size() > 80) s.resize(80);
    return s;
}

std::vector<std::string> list_of(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s + ";") {
        if (c == ';' || c == ',' || c == '|' || c == '\n' || c == '/') {
            if (!up(cur).empty()) out.push_back(up(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
}

// A group name as a collective: letters, digits, spaces, - / .
std::string group_name(const std::string& field, const std::string& v) {
    std::string g = up(v);
    std::string o;
    for (char c : g)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '.') o += c;
    o = book_key(o);
    if (o.empty()) return "";
    if (field == "region" && std::all_of(o.begin(), o.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        o = "REGION " + o;
    return o.size() <= 64 ? o : "";
}

void stations(Ops& ops, const std::vector<Row>& rows, size_t head, const Fields& f, const std::string& kind,
              const std::string& source, SheetResult& r) {
    std::map<std::string, std::vector<std::string>> groups;
    std::vector<BookEntry> collectives;
    for (size_t i = head + 1; i < rows.size(); ++i) {
        const Row& row = rows[i];
        if (blank(row) || same_as(row, rows[head])) continue;
        ++r.rows;
        std::string where = "row " + std::to_string(i + 1) + ": ";
        BookEntry e;
        e.call = up(cell(row, f, "call"));
        e.name = up(cell(row, f, "name"));
        if (e.name.empty()) e.name = e.call;
        e.ri = up(cell(row, f, "ri"));
        e.ale = up(cell(row, f, "ale"));
        e.members = list_of(cell(row, f, "members"));
        std::string k = hkey(cell(row, f, "kind"));
        if (k.find("collect") != std::string::npos || k.find("group") != std::string::npos || k == "cad" ||
            k == "aig")
            e.kind = "collective";
        else if (!e.members.empty())
            e.kind = "collective";
        else
            e.kind = "station";
        std::vector<std::string> note;
        std::string role = cell(row, f, "role"), region = cell(row, f, "region");
        if (!role.empty()) note.push_back(up(role));
        if (!region.empty()) note.push_back(up(region));
        std::string joined;
        for (auto& n : note) joined += (joined.empty() ? "" : "; ") + n;
        e.note = clip_note(joined);
        e.source = source;
        if (e.name.empty()) {
            r.problems.push_back(where + "no call or name");
            continue;
        }
        if (e.kind == "collective") {
            collectives.push_back(e);  // after the stations they name
            continue;
        }
        std::string err;
        if (!ops.book_put(e, &err)) {
            r.problems.push_back(where + err);
            continue;
        }
        ++r.imported;
        r.names.push_back(book_key(e.name));
        for (auto* fld : {"group", "region"}) {
            for (auto& g : (std::string(fld) == "group" ? list_of(cell(row, f, fld))
                                                         : std::vector<std::string>{cell(row, f, fld)})) {
                std::string gn = group_name(fld, g);
                if (!gn.empty() && gn != book_key(e.name)) groups[gn].push_back(book_key(e.name));
            }
        }
    }
    // Collectives named in the file, then one per group or region column value.
    for (auto& c : collectives) {
        std::string err;
        if (ops.book_put(c, &err)) {
            ++r.imported;
            r.names.push_back(book_key(c.name));
        } else {
            r.problems.push_back(c.name + ": " + err);
        }
    }
    for (auto& [g, members] : groups) {
        auto old = ops.book_get(g);
        if (old && old->kind != "collective") continue;  // a station by that name stays
        BookEntry e;
        e.name = g;
        e.kind = "collective";
        e.members = members;
        if (old)
            for (auto& m : old->members)
                if (std::find(e.members.begin(), e.members.end(), m) == e.members.end()) e.members.push_back(m);
        e.note = kind == "roster" ? "FROM THE ROSTER" : "FROM THE ADDRESS BOOK";
        e.source = source;
        std::string err;
        if (ops.book_put(e, &err)) r.names.push_back(g);
    }
}

std::string unit_of(const Row& h, const Fields& f) {
    std::string k = hkey(cell(h, f, "freq"));
    if (k.find("mhz") != std::string::npos) return "mhz";
    if (k.find("khz") != std::string::npos) return "khz";
    return "";
}

void freqs(Ops& ops, const std::vector<Row>& rows, size_t head, const Fields& f, const std::string& source,
           SheetResult& r) {
    std::string unit = unit_of(rows[head], f);
    std::vector<Channel> cs;
    for (size_t i = head + 1; i < rows.size(); ++i) {
        const Row& row = rows[i];
        if (blank(row) || same_as(row, rows[head])) continue;
        ++r.rows;
        std::string where = "row " + std::to_string(i + 1) + ": ";
        Channel c;
        c.name = up(cell(row, f, "designator"));
        c.hz = parse_frequency(cell(row, f, "freq"), unit);
        c.mode = up(cell(row, f, "mode"));
        c.use = up(cell(row, f, "use"));
        c.source = source;
        if (c.name.empty()) {
            r.problems.push_back(where + "no channel designator");
            continue;
        }
        if (!valid_designator(c.name)) {
            r.problems.push_back(where + "the designator isn't 1 to 12 letters, digits or - with a letter");
            continue;
        }
        if (c.hz == 0) {
            r.problems.push_back(where + "channel " + c.name + ": no frequency it could read");
            continue;
        }
        cs.push_back(c);
    }
    std::vector<std::string> errs;
    ops.channels().put_all(cs, &errs);
    for (auto& e : errs) r.problems.push_back(e);
    for (auto& c : cs)
        if (ops.channels().get(c.name)) {
            ++r.imported;
            r.names.push_back(c.name);
        }
}

void schedules(Ops& ops, const std::vector<Row>& rows, size_t head, const Fields& f, const std::string& source,
               SheetResult& r) {
    std::string unit = unit_of(rows[head], f);
    (void)source;
    for (size_t i = head + 1; i < rows.size(); ++i) {
        const Row& row = rows[i];
        if (blank(row) || same_as(row, rows[head])) continue;
        ++r.rows;
        std::string where = "row " + std::to_string(i + 1) + ": ";
        NetSchedule s;
        s.name = up(cell(row, f, "net"));
        s.days = normal_days(cell(row, f, "days"));
        if (s.days.empty()) {
            r.problems.push_back(where + s.name + ": couldn't read the days");
            continue;
        }
        s.start = utc_minutes(cell(row, f, "time"));
        if (s.start < 0) {
            r.problems.push_back(where + s.name +
                                 ": the start isn't a UTC time (1900Z). Local times aren't guessed at");
            continue;
        }
        int end = has(f, "end") ? utc_minutes(cell(row, f, "end")) : -1;
        std::string mins = cell(row, f, "minutes");
        if (end >= 0) {
            s.minutes = (end - s.start + 24 * 60) % (24 * 60);
            if (s.minutes == 0) s.minutes = 60;
        } else if (!mins.empty()) {
            int n = std::atoi(mins.c_str());
            std::string m = hkey(mins);
            if (m.find("h") != std::string::npos && m.find("min") == std::string::npos) n *= 60;
            if (n > 0) s.minutes = n;
        }
        s.ncs = up(cell(row, f, "ncs"));
        s.roster = list_of(cell(row, f, "roster"));
        s.note = clip_note(cell(row, f, "note"));
        std::string ch = up(cell(row, f, "designator"));
        if (ch.empty() && has(f, "freq") && !cell(row, f, "freq").empty()) {
            int64_t hz = parse_frequency(cell(row, f, "freq"), unit);
            ch = hz ? ops.channels().designator_for(hz) : "";
            if (ch.empty()) ++r.frequencies_withheld;
        }
        s.channel = valid_designator(ch) ? ch : "";
        if (!ch.empty() && s.channel.empty()) ++r.frequencies_withheld;
        std::string err;
        if (!ops.sched_put(s, &err)) {
            r.problems.push_back(where + err);
            continue;
        }
        ++r.imported;
        r.names.push_back(book_key(s.name));
    }
}

bool is_number(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

}  // namespace

std::string normal_days(const std::string& raw) {
    std::string s = up(raw);
    for (auto& c : s)
        if (c == '/' || c == '&' || c == ';' || c == '+') c = ',';
    // Words that join, and "EVERY".
    auto words = [](const std::string& x) {
        std::vector<std::string> w;
        std::string cur;
        for (char c : x + " ") {
            if (c == ' ' || c == ',') {
                if (!cur.empty()) w.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        return w;
    };
    static const char* kDays[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    auto day = [&](std::string w) -> int {
        while (!w.empty() && (w.back() == '.' || w.back() == 'S')) {
            if (w.back() == 'S' && w.size() <= 3) break;
            w.pop_back();
        }
        if (w == "TUES" || w == "TU") w = "TUE";
        if (w == "THUR" || w == "THURS" || w == "TH") w = "THU";
        if (w == "WEDS") w = "WED";
        for (int i = 0; i < 7; ++i)
            if (w.compare(0, 3, kDays[i]) == 0 && w.size() >= 3) return i;
        return -1;
    };
    std::vector<std::string> out, weeks;
    std::vector<std::string> w = words(s);
    for (size_t i = 0; i < w.size(); ++i) {
        std::string t = w[i];
        if (t == "EVERY" || t == "AND" || t == "OF" || t == "THE" || t == "EACH" || t == "MONTH" || t == "ON")
            continue;
        if (t == "DAILY" || t == "EVERYDAY") { out.push_back("DAILY"); continue; }
        if (t == "WEEKDAYS" || t == "WEEKDAY") { out.push_back("WEEKDAYS"); continue; }
        if (t == "WEEKENDS" || t == "WEEKEND") { out.push_back("WEEKENDS"); continue; }
        static const std::map<std::string, std::string> kWeek = {
            {"1ST", "1"}, {"FIRST", "1"}, {"2ND", "2"}, {"SECOND", "2"}, {"3RD", "3"}, {"THIRD", "3"},
            {"4TH", "4"}, {"FOURTH", "4"}, {"5TH", "5"}, {"FIFTH", "5"}, {"LAST", "L"}};
        if (kWeek.count(t)) { weeks.push_back(kWeek.at(t)); continue; }
        // MON-FRI, MON-THU
        size_t dash = t.find('-');
        if (dash != std::string::npos) {
            int a = day(t.substr(0, dash)), b = day(t.substr(dash + 1));
            if (a < 0 || b < 0) return "";
            if (a == 1 && b == 5) { out.push_back("WEEKDAYS"); continue; }
            for (int d = a;; d = (d + 1) % 7) {
                out.push_back(kDays[d]);
                if (d == b) break;
            }
            continue;
        }
        int d = day(t);
        if (d < 0) return "";
        if (weeks.empty()) {
            out.push_back(kDays[d]);
        } else {
            for (auto& wk : weeks) out.push_back(wk + kDays[d]);
            weeks.clear();
        }
    }
    if (!weeks.empty() || out.empty()) return "";
    std::string canon, err;
    std::string joined;
    for (auto& o : out) joined += (joined.empty() ? "" : ",") + o;
    return parse_days(joined, &canon, &err) ? canon : "";
}

int utc_minutes(const std::string& raw) {
    std::string s = up(raw), digits, rest;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            if (!rest.empty() && rest != ":" && rest != " ") break;
            digits += c;
            if (rest == ":") rest.clear();
        } else if (c == ':' && !digits.empty()) {
            rest = ":";
        } else {
            rest += c;
        }
    }
    std::string zone = book_key(rest);
    static const std::set<std::string> kUtc = {"", "Z", "UTC", "GMT", "ZULU", "Z UTC", "UTC Z"};
    if (!kUtc.count(zone)) return -1;
    if (digits.size() == 3) digits = "0" + digits;
    if (digits.size() == 1 || digits.size() == 2) digits += "00";
    if (digits.size() != 4) return -1;
    int m = 0;
    return parse_hhmm(digits, &m) ? m : -1;
}

std::vector<SheetResult> import_file(Ops& ops, const std::string& bytes, const std::string& kind_in,
                                     const std::string& source_in, std::string* error) {
    std::vector<SheetResult> out;
    std::vector<Table> tables;
    if (!read_tables(bytes, tables, error)) return out;
    std::string kind = kind_in.empty() ? "auto" : kind_in;
    static const std::vector<std::string> kKinds = {"roster", "book", "freq", "schedule"};
    if (kind != "auto" && std::find(kKinds.begin(), kKinds.end(), kind) == kKinds.end()) {
        if (error) *error = "kind: auto, roster, book, freq or schedule";
        return out;
    }
    std::string source = source_in.empty() ? "import" : source_in;
    for (auto& t : tables) {
        SheetResult r;
        r.sheet = t.sheet;
        // The header is the best-scoring row among the first 20.
        int best = 0;
        size_t head = 0;
        std::string got;
        for (size_t i = 0; i < t.rows.size() && i < 20; ++i)
            for (auto& k : kKinds) {
                if (kind != "auto" && k != kind) continue;
                int sc = score(t.rows[i], k);
                if (sc > best) best = sc, head = i, got = k;
            }
        if (got.empty()) {
            r.problems.push_back("no header row this import knows (docs/formats.md)");
            out.push_back(r);
            continue;
        }
        // A sheet of calls with no addressing and no personal columns is still a roster.
        r.kind = got;
        r.header_row = static_cast<int>(head) + 1;
        Fields f = map_header(t.rows[head], wanted(got), &r.left_out);
        // On a roster, a column headed "name" is a person's name: never kept.
        if (got == "freq") freqs(ops, t.rows, head, f, source, r);
        else if (got == "schedule") schedules(ops, t.rows, head, f, source, r);
        else stations(ops, t.rows, head, f, got, source, r);
        (void)is_number;
        out.push_back(r);
    }
    return out;
}

}  // namespace marsio
}  // namespace pna
