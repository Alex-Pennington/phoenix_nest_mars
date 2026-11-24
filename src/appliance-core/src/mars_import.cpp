#include "mars_import.h"

#include "channels.h"
#include "compose.h"
#include "ops.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
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

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
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
        {"region", {"region", "state", "st", "area", "district", "division", "territory"}},
        {"role", {"role", "position", "duty", "assignment", "title", "office", "function"}},
        {"group", {"group", "groups", "collective", "collectives", "team", "distribution", "list"}},
        // channels
        {"designator", {"designator", "des", "desig", "channel", "chan", "ch", "channelname", "chdesignator", "chan#", "channo",
                        "channelnumber", "chnum", "chname", "id"}},
        {"freq", {"frequency", "freq", "khz", "mhz", "freqkhz", "freqmhz", "frequencykhz", "frequencymhz", "qrg",
                  "dial", "dialfreq", "rxfreq", "rx", "carrier"}},
        {"mode", {"mode", "emission", "sideband", "modulation"}},
        {"use", {"use", "purpose", "usage", "net", "nets", "remarks", "notes", "comment", "comments", "note", "name"}},
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
            // "CALL SIGNS ASGN/BILLET" on a roster is the call.
            bool call_like = field == "call" && k.rfind("callsign", 0) == 0;
            if (call_like || std::find(syn.begin(), syn.end(), k) != syn.end()) {
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
    // MSC address books open with PLA (plain language address): a book, even
    // with point-of-contact columns.
    for (auto& c : r)
        if (kind == "roster" && (hkey(c) == "pla" || hkey(c) == "plainlanguageaddress")) return 0;
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

// A plain language address as a book name: other marks become spaces, and
// it is cut to the book's 64 characters.
std::string book_name(const std::string& raw) {
    std::string o;
    for (char c : up(raw))
        o += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '/' || c == '.' ? c : ' ';
    o = book_key(o);
    if (o.size() > 64) o = book_key(o.substr(0, 64));
    return o;
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
    std::set<std::string> seen;
    int vacant = 0;
    for (size_t i = head + 1; i < rows.size(); ++i) {
        const Row& row = rows[i];
        if (blank(row) || same_as(row, rows[head])) continue;
        // A line with no call and no addressing is a note or a date stamp
        // ("***UPDATED ...", "TEST_AB 01JAN2026"): not an entry.
        if (book_key(cell(row, f, "call")).empty() && book_key(cell(row, f, "ri")).empty() &&
            book_key(cell(row, f, "ale")).empty() && book_key(cell(row, f, "members")).empty())
            continue;
        std::string call = up(cell(row, f, "call"));
        // A billet not yet filled is written with underscores (ABC1__).
        if (call.find('_') != std::string::npos) {
            ++vacant;
            continue;
        }
        // A portable or mobile suffix (CALL/M) is the same station.
        size_t slash = call.find('/');
        if (slash != std::string::npos && call.size() - slash <= 3) call = call.substr(0, slash);
        ++r.rows;
        std::string where = "row " + std::to_string(i + 1) + ": ";
        BookEntry e;
        e.call = call;
        e.name = book_name(cell(row, f, "name"));
        if (e.name.empty()) e.name = e.call;
        e.ri = up(cell(row, f, "ri"));
        // A PLA with no call is reached through its routing indicator: the RI
        // is what goes on the air.
        // So is one whose "call" is a long office title (over 12 letters).
        bool by_ri = (e.call.empty() || e.call.size() > 12) && !e.ri.empty() && e.name != e.ri;
        if (by_ri) e.call = e.ri;
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
        if (by_ri) joined = "BY RI" + std::string(joined.empty() ? "" : "; ") + joined;
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
        if (!seen.insert(book_key(e.name)).second) continue;  // CALL and CALL/M: once
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
    if (vacant)
        r.problems.push_back(std::to_string(vacant) + " vacant billet" + (vacant == 1 ? "" : "s") +
                             " (a call written with _) left out");
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
        std::string fq = cell(row, f, "freq");
        Channel c;
        c.name = up(cell(row, f, "designator"));
        c.hz = parse_frequency(fq, unit);
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

// ---- Files with no header row this import knows ----

const char* const kDayCodes[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// A cell that is one day name ("SUN", "Sunday"): 0..6, else -1.
int one_day(const std::string& c) {
    std::string d = normal_days(c);
    for (int i = 0; i < 7; ++i)
        if (d == kDayCodes[i]) return i;
    return -1;
}

// A local zone named in a header, as hours behind UTC; 99 when none.
int zone_offset(const std::string& raw) {
    static const std::vector<std::pair<std::string, int>> kZones = {
        {"EDST", 4}, {"EDT", 4}, {"EST", 5}, {"CDST", 5}, {"CDT", 5}, {"CST", 6}, {"MDST", 6}, {"MDT", 6},
        {"MST", 7}, {"PDST", 7}, {"PDT", 7}, {"PST", 8}, {"AKDT", 8}, {"AKST", 9}, {"HST", 10}, {"AST", 4}};
    std::string s = up(raw), word;
    for (char c : s + " ") {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            word += c;
            continue;
        }
        for (auto& [z, h] : kZones)
            if (word == z) return h;
        word.clear();
    }
    return 99;
}

// The first clock time in a cell, in its own (local) zone: "7AM / 6AM" ->
// 0700, "0600L" -> 0600, "6:30 PM" -> 1830. -1 when there is none.
int local_minutes(const std::string& raw) {
    std::string s = up(raw);
    size_t i = 0;
    while (i < s.size() && !std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    std::string digits;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == ':')) {
        if (s[i] != ':') digits += s[i];
        ++i;
    }
    while (i < s.size() && s[i] == ' ') ++i;
    std::string ampm;
    if (i + 1 < s.size() && (s[i] == 'A' || s[i] == 'P') && s[i + 1] == 'M') ampm = s.substr(i, 2);
    int h = 0, m = 0;
    if (digits.size() <= 2 && !digits.empty()) h = std::atoi(digits.c_str());
    else if (digits.size() == 3 || digits.size() == 4) h = std::atoi(digits.substr(0, digits.size() - 2).c_str()),
                                                        m = std::atoi(digits.substr(digits.size() - 2).c_str());
    else return -1;
    if (!ampm.empty()) {
        if (h < 1 || h > 12) return -1;
        h = h % 12 + (ampm == "PM" ? 12 : 0);
    }
    if (h > 23 || m > 59) return -1;
    return h * 60 + m;
}

bool designator_shape(const std::string& t) {
    size_t i = 0;
    while (i < t.size() && std::isalpha(static_cast<unsigned char>(t[i]))) ++i;
    size_t letters = i;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) ++i;
    return letters >= 1 && letters <= 2 && i - letters >= 1 && i - letters <= 3 && i == t.size();
}

std::string hhmmz(int m) {
    char b[16];
    std::snprintf(b, sizeof b, "%02d%02dZ", (m / 60) % 24, m % 60);
    return b;
}

// A checkerboard: a row of day names across the top, a time down one side,
// and in each cell the net's designators (C11/C22/C33), sometimes with its
// name. Columns between the time and the first day (PRIMARY) are the
// channel to use each hour. Each distinct cell becomes a net schedule.
bool checkerboard(Ops& ops, const std::vector<Row>& rows, const std::string& source, SheetResult& r) {
    (void)source;  // schedules don't record a source
    size_t dayrow = rows.size();
    std::map<size_t, int> daycol;
    for (size_t i = 0; i < rows.size() && i < 20 && dayrow == rows.size(); ++i) {
        std::map<size_t, int> d;
        for (size_t c = 0; c < rows[i].size(); ++c)
            if (one_day(rows[i][c]) >= 0) d[c] = one_day(rows[i][c]);
        if (d.size() >= 5) dayrow = i, daycol = d;
    }
    if (dayrow == rows.size()) return false;
    size_t first = daycol.begin()->first;
    r.kind = "schedule";
    r.header_row = static_cast<int>(dayrow) + 1;
    // The time column: one in UTC if the sheet has it, else a local one whose
    // header names its zone.
    auto count = [&](size_t c, bool utc) {
        int n = 0;
        for (size_t i = dayrow + 1; i < rows.size(); ++i)
            if (c < rows[i].size() && (utc ? utc_minutes(rows[i][c]) : local_minutes(rows[i][c])) >= 0) ++n;
        return n;
    };
    int tcol = -1, lcol = -1, best = 0;
    for (size_t c = 0; c < first; ++c) {
        int n = count(c, true);
        if (n > best) best = n, tcol = static_cast<int>(c);
    }
    best = 0;
    for (size_t c = 0; c < first; ++c) {
        if (static_cast<int>(c) == tcol) continue;
        int n = count(c, false);
        if (n > best) best = n, lcol = static_cast<int>(c);
    }
    int offset = 99;
    if (tcol < 0) {
        if (lcol < 0) {
            r.problems.push_back("a checkerboard with no time down the side");
            return true;
        }
        for (size_t i = 0; i <= dayrow && offset == 99; ++i)
            if (static_cast<size_t>(lcol) < rows[i].size()) offset = zone_offset(rows[i][static_cast<size_t>(lcol)]);
        if (offset == 99) {
            r.problems.push_back("the times are local and the sheet doesn't say which zone. Local times aren't "
                                 "guessed at");
            return true;
        }
        r.problems.push_back("the times were local; converted to UTC as UTC-" + std::to_string(offset) +
                             " from the zone in the header. Check it against standard or daylight time");
    }
    // Each row's start in UTC, and whether that start falls on the next UTC day.
    struct Slot { size_t row; int start; int shift; };
    std::vector<Slot> slots;
    for (size_t i = dayrow + 1; i < rows.size(); ++i) {
        const Row& row = rows[i];
        int loc = lcol >= 0 && static_cast<size_t>(lcol) < row.size() ? local_minutes(row[static_cast<size_t>(lcol)]) : -1;
        if (tcol >= 0) {
            int u = static_cast<size_t>(tcol) < row.size() ? utc_minutes(row[static_cast<size_t>(tcol)]) : -1;
            if (u < 0) continue;
            slots.push_back({i, u, loc >= 0 && u < loc ? 1 : 0});
        } else {
            if (loc < 0) continue;
            int u = loc + offset * 60;
            slots.push_back({i, u % 1440, u >= 1440 ? 1 : 0});
        }
    }
    auto length = [&](size_t k) {
        for (size_t j = k + 1; j < slots.size(); ++j)
            if (slots[j].start != slots[k].start) {
                int m = (slots[j].start - slots[k].start + 1440) % 1440;
                return m >= 5 && m <= 180 ? m : 60;
            }
        return 60;
    };
    std::set<std::string> used;
    auto unique = [&](std::string name, const std::string& days) {
        name = book_key(name);
        if (used.count(name)) name += " " + days;
        used.insert(name);
        return name;
    };
    auto put = [&](NetSchedule& s, const std::string& where) {
        std::string err;
        ++r.rows;
        if (!ops.sched_put(s, &err)) {
            r.problems.push_back(where + s.name + ": " + err);
            return;
        }
        ++r.imported;
        r.names.push_back(book_key(s.name));
    };
    // Channel columns: one daily schedule per run of hours on the same channel.
    for (size_t c = 0; c < first; ++c) {
        if (static_cast<int>(c) == tcol || static_cast<int>(c) == lcol) continue;
        std::string head = c < rows[dayrow].size() ? book_key(up(rows[dayrow][c])) : "";
        if (head.empty()) continue;
        for (size_t k = 0; k < slots.size();) {
            const Row& row = rows[slots[k].row];
            std::string ch = c < row.size() ? book_key(up(row[c])) : "";
            size_t e = k + 1;
            int minutes = length(k);
            while (e < slots.size() && slots[e].start == slots[e - 1].start) ++e;
            while (e < slots.size() && !ch.empty()) {
                const Row& nx = rows[slots[e].row];
                if ((c < nx.size() ? book_key(up(nx[c])) : "") != ch) break;
                minutes += length(e);
                ++e;
                while (e < slots.size() && slots[e].start == slots[e - 1].start) ++e;
            }
            if (!ch.empty()) {
                NetSchedule s;
                s.days = "DAILY";
                s.start = slots[k].start;
                s.minutes = std::min(minutes, 1440);
                s.name = unique(head + " " + hhmmz(s.start), "DAILY");
                if (valid_designator(ch)) s.channel = ch;
                else ++r.frequencies_withheld;
                s.note = "CHANNEL PLAN";
                put(s, "row " + std::to_string(slots[k].row + 1) + ": ");
            }
            k = e;
        }
    }
    // Day cells: the same text at the same time on several days is one net.
    struct Net { int start; int minutes; std::set<int> days; std::string text; size_t row; };
    std::vector<Net> nets;
    for (size_t k = 0; k < slots.size(); ++k) {
        const Row& row = rows[slots[k].row];
        for (auto& [c, d] : daycol) {
            std::string t = c < row.size() ? book_key(up(row[c])) : "";
            if (t.empty()) continue;
            int day = (d + slots[k].shift) % 7;
            auto it = std::find_if(nets.begin(), nets.end(), [&](const Net& n) {
                return n.start == slots[k].start && n.text == t;
            });
            if (it == nets.end()) nets.push_back({slots[k].start, length(k), {day}, t, slots[k].row});
            else it->days.insert(day);
        }
    }
    for (auto& n : nets) {
        std::vector<std::string> desig, words, tags;
        std::string cur;
        bool in_tag = false;
        auto flush = [&]() {
            if (cur.empty()) return;
            if (in_tag) tags.push_back(cur);
            else if (designator_shape(cur)) desig.push_back(cur);
            else if (cur != "OR" && cur != "AND") words.push_back(cur);
            cur.clear();
        };
        for (char ch : n.text) {
            if (ch == '(') { flush(); in_tag = true; continue; }
            if (ch == ')') { flush(); in_tag = false; continue; }
            if (std::isalnum(static_cast<unsigned char>(ch))) cur += ch;
            else flush();
        }
        flush();
        std::string days;
        for (int d : n.days) days += (days.empty() ? "" : ",") + std::string(kDayCodes[d]);
        if (n.days.size() == 7) days = "DAILY";
        std::string name;
        for (auto& w : words) name += (name.empty() ? "" : " ") + w;
        if (name.size() > 40) name.resize(40);
        NetSchedule s;
        s.start = n.start;
        s.minutes = n.minutes;
        s.name = unique((name.empty() ? "NET" : book_key(name)) + " " + hhmmz(n.start), days);
        std::string canon, err;
        s.days = parse_days(days, &canon, &err) ? canon : days;
        if (!desig.empty()) s.channel = desig[0];
        std::string note;
        if (desig.size() > 1) {
            note = "ALT";
            for (size_t i = 1; i < desig.size(); ++i) note += " " + desig[i];
        }
        for (auto& t : tags) note += (note.empty() ? "" : "; ") + t;
        s.note = clip_note(note);
        put(s, "row " + std::to_string(n.row + 1) + ": ");
    }
    return true;
}

// "DES kHz" lines with no header (MSC's channels.csv): T01 5000.0
bool channel_list(Ops& ops, const std::vector<Row>& rows, const std::string& source, SheetResult& r) {
    std::vector<std::pair<std::string, std::string>> pairs;
    size_t filled = 0;
    for (auto& row : rows) {
        if (blank(row)) continue;
        ++filled;
        std::string joined;
        for (auto& c : row)
            if (!trim(c).empty()) joined += (joined.empty() ? "" : " ") + trim(c);
        size_t sp = joined.find(' ');
        if (sp == std::string::npos) continue;
        std::string des = up(joined.substr(0, sp)), fq = trim(joined.substr(sp + 1));
        bool num = !fq.empty() && std::all_of(fq.begin(), fq.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) || c == '.';
        });
        if (num && valid_designator(des)) pairs.push_back({des, fq});
    }
    if (filled == 0 || pairs.size() * 10 < filled * 8) return false;
    r.kind = "freq";
    r.problems.push_back("no header row: read as designator and frequency in kHz on each line");
    std::vector<Channel> cs;
    for (auto& [des, fq] : pairs) {
        ++r.rows;
        Channel c;
        c.name = des;
        c.hz = parse_frequency(fq, "khz");
        c.source = source;
        if (c.hz == 0) {
            r.problems.push_back("channel " + des + ": no frequency it could read");
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
    return true;
}

bool call_shape(const std::string& raw) {
    std::string s = up(raw);
    size_t slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(0, slash);
    bool digit = false, letter = false;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) digit = true;
        else if (std::isalpha(static_cast<unsigned char>(c))) letter = true;
        else return false;
    }
    return digit && letter && s.size() >= 4 && s.size() <= 8;
}

// An old address list with no header: the call is the first column.
bool call_list(Ops& ops, const std::vector<Row>& rows, const std::string& source, SheetResult& r) {
    size_t filled = 0, calls = 0;
    for (auto& row : rows) {
        if (blank(row)) continue;
        ++filled;
        if (!row.empty() && call_shape(row[0])) ++calls;
    }
    if (filled < 3 || calls * 10 < filled * 8) return false;
    Row head = {"CALLSIGN"};
    std::vector<Row> with = {head};
    with.insert(with.end(), rows.begin(), rows.end());
    Fields f = {{"call", 0}};
    r.kind = "roster";
    r.problems.push_back("no header row: read the first column as calls and left the rest out");
    stations(ops, with, 0, f, "roster", source, r);
    return true;
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
        if (got.empty() && (kind == "auto" || kind == "schedule") && checkerboard(ops, t.rows, source, r)) {
            out.push_back(r);
            continue;
        }
        if (got.empty() && (kind == "auto" || kind == "freq") && channel_list(ops, t.rows, source, r)) {
            out.push_back(r);
            continue;
        }
        if (got.empty() && (kind == "auto" || kind == "roster") && call_list(ops, t.rows, source, r)) {
            out.push_back(r);
            continue;
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
