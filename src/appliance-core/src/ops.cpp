#include "ops.h"

#include "channels.h"

#include "sqlite_stmt.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <sstream>

namespace pna {

namespace {

int64_t system_now() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::string join(const std::vector<std::string>& v, char sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

std::vector<std::string> split(const std::string& s, const std::string& seps) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (seps.find(c) != std::string::npos) {
            if (!book_key(cur).empty()) out.push_back(book_key(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!book_key(cur).empty()) out.push_back(book_key(cur));
    return out;
}

bool chars_ok(const std::string& s, const char* extra) {
    for (unsigned char c : s)
        if (!std::isupper(c) && !std::isdigit(c) && !std::strchr(extra, c)) return false;
    return true;
}

bool valid_call(const std::string& s) { return s.size() >= 2 && s.size() <= 12 && chars_ok(s, "-"); }

const char* kNetCols = "id, name, ncs, exercise, opened_at, closed_at, roster, note";

// One CSV record (quotes allowed); false at the end.
bool csv_row(std::istream& in, std::vector<std::string>* row) {
    row->clear();
    std::string line;
    if (!std::getline(in, line)) return false;
    std::string cell;
    bool q = false;
    for (size_t i = 0;; ++i) {
        if (i == line.size()) {
            if (q) {  // a quoted cell running onto the next line
                std::string more;
                if (!std::getline(in, more)) break;
                cell += '\n';
                line += '\n' + more;
                continue;
            }
            break;
        }
        char c = line[i];
        if (q) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                cell += '"';
                ++i;
            } else if (c == '"') {
                q = false;
            } else {
                cell += c;
            }
        } else if (c == '"') {
            q = true;
        } else if (c == ',') {
            row->push_back(cell);
            cell.clear();
        } else if (c != '\r') {
            cell += c;
        }
    }
    row->push_back(cell);
    return true;
}

std::string csv_cell(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

bool month_range(const std::string& m, int64_t* from, int64_t* to) {
    int y = 0, mo = 0;
    if (m.size() != 7 || m[4] != '-' || std::sscanf(m.c_str(), "%4d-%2d", &y, &mo) != 2 || mo < 1 || mo > 12)
        return false;
    std::tm t{};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = 1;
    *from = static_cast<int64_t>(timegm(&t));
    t.tm_mon += 1;
    *to = static_cast<int64_t>(timegm(&t));
    return true;
}

std::string ddhhmm(int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm u{};
    gmtime_r(&tt, &u);
    char b[16];
    std::snprintf(b, sizeof b, "%02d%02d%02dZ", u.tm_mday, u.tm_hour, u.tm_min);
    return b;
}

std::string hours(double h) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", h);
    return b;
}

int prec_index(char p) {
    switch (p) {
        case 'Z': return 0;
        case 'O': return 1;
        case 'P': return 2;
        default: return 3;
    }
}

std::string upper_line(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (s.size() > 69) s.resize(69);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

std::string book_key(const std::string& s) {
    std::string out;
    bool space = false;
    for (unsigned char c : s) {
        if (std::isspace(c)) {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += static_cast<char>(std::toupper(c));
    }
    return out;
}

bool is_exercise_body(const std::string& body) {
    std::istringstream in(body);
    for (std::string l; std::getline(in, l);)
        if (book_key(l).rfind("EXERCISE", 0) == 0) return true;
    return false;
}

std::vector<std::string> Net::missing() const {
    std::vector<std::string> out;
    for (auto& r : roster) {
        bool in = false;
        for (auto& c : checkins)
            if (c.station == r) in = true;
        if (!in) out.push_back(r);
    }
    return out;
}

Ops::Ops(std::string station, const std::string& db_path) : station_(book_key(station)), clock_(system_now) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        throw std::runtime_error("cannot open station book " + db_path + ": " + err);
    }
    sqlite3_busy_timeout(db_, 2000);
    exec("PRAGMA journal_mode=WAL");
    exec(
        "CREATE TABLE IF NOT EXISTS book (name TEXT PRIMARY KEY, kind TEXT NOT NULL, call TEXT, ri TEXT,"
        " ale TEXT, members TEXT, note TEXT, source TEXT)");
    exec(
        "CREATE TABLE IF NOT EXISTS nets (id TEXT PRIMARY KEY, name TEXT, ncs TEXT, exercise INTEGER,"
        " opened_at INTEGER, closed_at INTEGER DEFAULT 0, roster TEXT, note TEXT)");
    exec(
        "CREATE TABLE IF NOT EXISTS checkins (net TEXT, station TEXT, at INTEGER, out_at INTEGER DEFAULT 0,"
        " via TEXT, traffic INTEGER DEFAULT 0, remark TEXT, in_roster INTEGER, PRIMARY KEY(net, station))");
    exec(
        "CREATE TABLE IF NOT EXISTS duty (id INTEGER PRIMARY KEY AUTOINCREMENT, op TEXT, on_at INTEGER,"
        " off_at INTEGER DEFAULT 0, turnover TEXT DEFAULT '')");
    exec(
        "CREATE TABLE IF NOT EXISTS oplog (id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER, kind TEXT,"
        " text TEXT, net TEXT, exercise INTEGER)");
    exec("CREATE TABLE IF NOT EXISTS ops_meta (key TEXT PRIMARY KEY, value INTEGER NOT NULL)");
    exec(
        "CREATE TABLE IF NOT EXISTS schedules (name TEXT PRIMARY KEY, days TEXT, start INTEGER, minutes INTEGER,"
        " roster TEXT, exercise INTEGER, ncs TEXT, note TEXT, auto_open INTEGER, last_start INTEGER DEFAULT 0,"
        " last_net TEXT DEFAULT '')");
    {
        bool has = false;
        Stmt q(db_, "PRAGMA table_info(schedules)");
        while (q.step()) has = has || q.text(1) == "channel";
        if (!has) exec("ALTER TABLE schedules ADD COLUMN channel TEXT DEFAULT ''");
    }
    std::string base = db_path;
    if (base.size() > 3 && base.compare(base.size() - 3, 3, ".db") == 0) base.resize(base.size() - 3);
    channels_ = std::make_unique<ChannelPlan>(base + ".channels");
    std::string cerr;
    channels_->load(&cerr);
}

Ops::~Ops() { sqlite3_close(db_); }

void Ops::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string e = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("sqlite: " + e);
    }
}

void Ops::set_clock(std::function<int64_t()> now) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    clock_ = std::move(now);
}

int64_t Ops::now() const { return clock_(); }

// ---- address book ----

static BookEntry row_entry(Stmt& s) {
    BookEntry e;
    e.name = s.text(0);
    e.kind = s.text(1);
    e.call = s.text(2);
    e.ri = s.text(3);
    e.ale = s.text(4);
    e.members = split(s.text(5), "\n");
    e.note = s.text(6);
    e.source = s.text(7);
    return e;
}

bool Ops::book_put(BookEntry e, std::string* error) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    e.name = book_key(e.name);
    e.kind = e.kind.empty() ? (e.members.empty() ? "station" : "collective") : book_key(e.kind);
    for (auto& c : e.kind) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    e.call = book_key(e.call);
    e.ri = book_key(e.ri);
    e.ale = book_key(e.ale);
    for (auto& m : e.members) m = book_key(m);
    if (e.source.empty()) e.source = "local";
    if (e.name.empty() || e.name.size() > 64 || !chars_ok(e.name, " -/."))
        return fail("name: use letters, digits, spaces, - / or ., up to 64");
    if (e.note.size() > 80) return fail("note: keep it to 80 characters");
    if (e.kind == "station") {
        if (e.call.empty()) e.call = e.name;
        if (!valid_call(e.call)) return fail("call: 2 to 12 letters, digits or -");
        if (!e.members.empty()) return fail("members: only a collective has members");
    } else if (e.kind == "collective") {
        if (e.members.empty()) return fail("members: a collective needs at least one");
        for (auto& m : e.members)
            if (m == e.name) return fail("members: a collective can't hold itself");
        e.call.clear();
    } else {
        return fail("kind: station or collective");
    }
    if (!e.ri.empty() && (e.ri.size() < 3 || e.ri.size() > 12 || !chars_ok(e.ri, "-")))
        return fail("ri: 3 to 12 letters, digits or -");
    if (!e.ale.empty() && (e.ale.size() > 15 || !chars_ok(e.ale, "@?")))
        return fail("ale: up to 15 letters, digits, @ or ?");
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt(db_, "INSERT OR REPLACE INTO book VALUES(?,?,?,?,?,?,?,?)")
        .bind(1, e.name)
        .bind(2, e.kind)
        .bind(3, e.call)
        .bind(4, e.ri)
        .bind(5, e.ale)
        .bind(6, join(e.members, '\n'))
        .bind(7, e.note)
        .bind(8, e.source)
        .step();
    return true;
}

bool Ops::book_remove(const std::string& name) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt(db_, "DELETE FROM book WHERE name = ?").bind(1, book_key(name)).step();
    return sqlite3_changes(db_) > 0;
}

std::optional<BookEntry> Ops::book_get(const std::string& name) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt s(db_, "SELECT name, kind, call, ri, ale, members, note, source FROM book WHERE name = ?");
    s.bind(1, book_key(name));
    if (!s.step()) return std::nullopt;
    return row_entry(s);
}

std::optional<BookEntry> Ops::book_find(const std::string& any) {
    std::string k = book_key(any);
    if (k.empty()) return std::nullopt;
    if (auto e = book_get(k)) return e;
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt s(db_,
           "SELECT name, kind, call, ri, ale, members, note, source FROM book"
           " WHERE call = ?1 OR ri = ?1 OR ale = ?1 ORDER BY name LIMIT 1");
    s.bind(1, k);
    if (!s.step()) return std::nullopt;
    return row_entry(s);
}

std::vector<BookEntry> Ops::book_list() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt s(db_, "SELECT name, kind, call, ri, ale, members, note, source FROM book ORDER BY kind DESC, name");
    std::vector<BookEntry> out;
    while (s.step()) out.push_back(row_entry(s));
    return out;
}

int Ops::book_import(const std::string& csv, const std::string& source, std::vector<std::string>* dropped,
                     std::vector<std::string>* errors) {
    std::istringstream in(csv);
    std::vector<std::string> head, row;
    if (!csv_row(in, &head)) return 0;
    std::map<std::string, int> col;
    static const std::set<std::string> kKnown = {"name", "kind", "call", "ri", "ale", "members", "note"};
    for (size_t i = 0; i < head.size(); ++i) {
        std::string h = book_key(head[i]);
        for (auto& c : h) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (kKnown.count(h))
            col[h] = static_cast<int>(i);
        else if (dropped && !h.empty())
            dropped->push_back(head[i]);
    }
    if (!col.count("name")) {
        if (errors) errors->push_back("the header has no name column");
        return 0;
    }
    auto get = [&](const char* k) {
        auto it = col.find(k);
        return it != col.end() && it->second < static_cast<int>(row.size()) ? row[it->second] : std::string();
    };
    int n = 0, line = 1;
    std::lock_guard<std::recursive_mutex> g(mu_);
    exec("BEGIN");
    while (csv_row(in, &row)) {
        ++line;
        if (row.size() == 1 && book_key(row[0]).empty()) continue;
        BookEntry e;
        e.name = get("name");
        e.kind = get("kind");
        e.call = get("call");
        e.ri = get("ri");
        e.ale = get("ale");
        e.members = split(get("members"), ";|");
        e.note = get("note");
        e.source = source.empty() ? "import" : source;
        std::string err;
        if (book_put(e, &err))
            ++n;
        else if (errors)
            errors->push_back("line " + std::to_string(line) + ": " + err);
    }
    exec("COMMIT");
    return n;
}

std::string Ops::book_export() {
    std::string out = "name,kind,call,ri,ale,members,note\n";
    for (auto& e : book_list())
        out += csv_cell(e.name) + "," + e.kind + "," + e.call + "," + e.ri + "," + e.ale + "," +
               csv_cell(join(e.members, ';')) + "," + csv_cell(e.note) + "\n";
    return out;
}

Resolved Ops::resolve(const std::string& name_or_list) {
    Resolved r;
    std::set<std::string> seen;
    std::function<void(const std::string&, std::vector<std::string>&)> walk = [&](const std::string& n,
                                                                                    std::vector<std::string>& path) {
        auto e = book_find(n);
        if (!e) {
            if (std::find(r.unknown.begin(), r.unknown.end(), n) == r.unknown.end()) r.unknown.push_back(n);
            return;
        }
        if (e->kind == "station") {
            if (seen.insert(e->call).second) r.stations.push_back(*e);
            return;
        }
        if (std::find(path.begin(), path.end(), e->name) != path.end() || path.size() > 8) {
            if (std::find(r.loops.begin(), r.loops.end(), e->name) == r.loops.end()) r.loops.push_back(e->name);
            return;
        }
        path.push_back(e->name);
        for (auto& m : e->members) walk(m, path);
        path.pop_back();
    };
    for (auto& n : split(name_or_list, ",")) {
        std::vector<std::string> path;
        walk(n, path);
    }
    return r;
}

std::vector<std::string> Ops::send_targets(const std::string& to, Resolved* why) {
    Resolved r = resolve(to);
    if (why) *why = r;
    std::vector<std::string> out;
    for (auto& s : r.stations) out.push_back(s.call);
    for (auto& u : r.unknown) out.push_back(u);  // not in the book: send as written
    return out;
}

std::string Ops::canonical(const std::string& station) {
    auto e = book_find(station);
    return e && e->kind == "station" ? e->call : book_key(station);
}

// ---- nets ----

std::optional<Net> Ops::load_net(const std::string& id) {
    Stmt s(db_, (std::string("SELECT ") + kNetCols + " FROM nets WHERE id = ?").c_str());
    s.bind(1, book_key(id));
    if (!s.step()) return std::nullopt;
    Net n;
    n.id = s.text(0);
    n.name = s.text(1);
    n.ncs = s.text(2);
    n.exercise = s.i64(3) != 0;
    n.opened_at = s.i64(4);
    n.closed_at = s.i64(5);
    n.roster = split(s.text(6), "\n");
    n.note = s.text(7);
    Stmt c(db_,
           "SELECT station, at, out_at, via, traffic, remark, in_roster FROM checkins WHERE net = ? ORDER BY at, "
           "station");
    c.bind(1, n.id);
    while (c.step()) {
        CheckIn ci;
        ci.station = c.text(0);
        ci.at = c.i64(1);
        ci.out_at = c.i64(2);
        ci.via = c.text(3);
        ci.traffic = static_cast<int>(c.i64(4));
        ci.remark = c.text(5);
        ci.in_roster = c.i64(6) != 0;
        n.checkins.push_back(ci);
    }
    return n;
}

std::string Ops::net_open(const std::string& name, const std::vector<std::string>& roster, bool exercise,
                          const std::string& ncs, const std::string& note, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::string nm = book_key(name);
    if (nm.empty()) {
        if (error) *error = "name: give the net a name";
        return "";
    }
    std::vector<std::string> calls;
    for (auto& r : roster)
        for (auto& c : send_targets(r))
            if (std::find(calls.begin(), calls.end(), c) == calls.end() && c != station_) calls.push_back(c);
    exec("INSERT OR IGNORE INTO ops_meta(key, value) VALUES('net', 0)");
    exec("UPDATE ops_meta SET value = value + 1 WHERE key = 'net'");
    Stmt q(db_, "SELECT value FROM ops_meta WHERE key = 'net'");
    q.step();
    std::string id = "NET-" + std::to_string(q.i64(0));
    std::string control = ncs.empty() ? station_ : canonical(ncs);
    Stmt(db_, "INSERT INTO nets(id, name, ncs, exercise, opened_at, roster, note) VALUES(?,?,?,?,?,?,?)")
        .bind(1, id)
        .bind(2, nm)
        .bind(3, control)
        .bind(4, static_cast<int64_t>(exercise))
        .bind(5, now())
        .bind(6, join(calls, '\n'))
        .bind(7, note)
        .step();
    log_add("net",
            std::string(exercise ? "exercise " : "") + "net " + nm + " opened, net control " + control + ", " +
                std::to_string(calls.size()) + " on the roster",
            id, exercise);
    return id;
}

bool Ops::net_checkin(const std::string& net, const std::string& station, const std::string& via, int traffic,
                      const std::string& remark, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::string id = net.empty() ? current_net() : book_key(net);
    auto n = load_net(id);
    if (!n || n->closed_at) {
        if (error) *error = n ? "that net is closed" : (net.empty() ? "no net is open, or more than one is" : "no such net");
        return false;
    }
    std::string st = canonical(station);
    if (!valid_call(st)) {
        if (error) *error = "station: 2 to 12 letters, digits or -";
        return false;
    }
    bool in_roster = std::find(n->roster.begin(), n->roster.end(), st) != n->roster.end();
    bool again = false;
    for (auto& c : n->checkins)
        if (c.station == st) again = true;
    if (again) {
        Stmt(db_, "UPDATE checkins SET out_at = 0, traffic = ?, remark = ? WHERE net = ? AND station = ?")
            .bind(1, static_cast<int64_t>(traffic))
            .bind(2, remark)
            .bind(3, id)
            .bind(4, st)
            .step();
    } else {
        Stmt(db_, "INSERT INTO checkins(net, station, at, via, traffic, remark, in_roster) VALUES(?,?,?,?,?,?,?)")
            .bind(1, id)
            .bind(2, st)
            .bind(3, now())
            .bind(4, via.empty() ? std::string("manual") : via)
            .bind(5, static_cast<int64_t>(traffic))
            .bind(6, remark)
            .bind(7, static_cast<int64_t>(in_roster))
            .step();
    }
    std::string t = st + (again ? " checked in again" : " checked in") + (via == "radio" ? " (heard)" : "");
    if (traffic) t += ", " + std::to_string(traffic) + " for the net";
    if (!in_roster) t += ", not on the roster";
    log_add("checkin", t, id, n->exercise);
    return true;
}

bool Ops::net_checkout(const std::string& net, const std::string& station, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::string id = net.empty() ? current_net() : book_key(net);
    auto n = load_net(id);
    std::string st = canonical(station);
    bool found = false;
    if (n)
        for (auto& c : n->checkins)
            if (c.station == st && !c.out_at) found = true;
    if (!found) {
        if (error) *error = n ? st + " isn't checked in to " + id : "no such net";
        return false;
    }
    Stmt(db_, "UPDATE checkins SET out_at = ? WHERE net = ? AND station = ?").bind(1, now()).bind(2, id).bind(3, st).step();
    log_add("checkin", st + " checked out", id, n->exercise);
    return true;
}

bool Ops::net_close(const std::string& net, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::string id = net.empty() ? current_net() : book_key(net);
    auto n = load_net(id);
    if (!n || n->closed_at) {
        if (error) *error = n ? "that net is already closed" : (net.empty() ? "no net is open, or more than one is" : "no such net");
        return false;
    }
    int64_t t = now();
    Stmt(db_, "UPDATE nets SET closed_at = ? WHERE id = ?").bind(1, t).bind(2, id).step();
    Stmt(db_, "UPDATE checkins SET out_at = ? WHERE net = ? AND out_at = 0").bind(1, t).bind(2, id).step();
    int traffic = 0;
    for (auto& c : n->checkins) traffic += c.traffic;
    std::string text = "net " + n->name + " closed after " + std::to_string((t - n->opened_at) / 60) + " min, " +
                       std::to_string(n->checkins.size()) + " checked in";
    auto miss = n->missing();
    if (!miss.empty()) text += ", not heard: " + join(miss, ' ');
    if (traffic) text += ", " + std::to_string(traffic) + " listed traffic";
    log_add("net", text, id, n->exercise);
    return true;
}

std::optional<Net> Ops::net_get(const std::string& net) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    return load_net(net.empty() ? current_net() : net);
}

std::vector<Net> Ops::net_list(int limit) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::vector<std::string> ids;
    {
        Stmt s(db_, "SELECT id FROM nets ORDER BY opened_at DESC, rowid DESC LIMIT ?");
        s.bind(1, static_cast<int64_t>(limit));
        while (s.step()) ids.push_back(s.text(0));
    }
    std::vector<Net> out;
    for (auto& i : ids)
        if (auto n = load_net(i)) out.push_back(*n);
    return out;
}

std::string Ops::current_net() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt s(db_, "SELECT id FROM nets WHERE closed_at = 0 LIMIT 2");
    std::vector<std::string> ids;
    while (s.step()) ids.push_back(s.text(0));
    return ids.size() == 1 ? ids[0] : "";
}

void Ops::heard(const std::string& station) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::string st = canonical(station);
    if (st == station_ || !valid_call(st)) return;
    std::vector<std::string> open;
    {
        Stmt s(db_, "SELECT id FROM nets WHERE closed_at = 0");
        while (s.step()) open.push_back(s.text(0));
    }
    for (auto& id : open) {
        Stmt s(db_, "SELECT 1 FROM checkins WHERE net = ? AND station = ?");
        s.bind(1, id).bind(2, st);
        if (s.step()) continue;  // already in, or checked out: radio doesn't undo a check-out
        net_checkin(id, st, "radio", 0, "", nullptr);
    }
}


// ---- net schedules ----

namespace {

const char* kDays[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

struct DayRule {
    int wday = 0;  // 0 Sunday
    int nth = 0;   // 0 every week, 1-5 that week of the month, -1 the last
};

std::vector<DayRule> day_rules(const std::string& canon) {
    std::vector<DayRule> out;
    for (auto& tok : split(canon, ",")) {
        DayRule r;
        std::string d = tok;
        if (d.size() == 4) {
            r.nth = d[0] == 'L' ? -1 : d[0] - '0';
            d = d.substr(1);
        }
        r.wday = -1;
        for (int i = 0; i < 7; ++i)
            if (d == kDays[i]) r.wday = i;
        if (r.wday >= 0) out.push_back(r);  // anything else isn't a day: no rule
    }
    return out;
}

bool day_matches(const std::vector<DayRule>& rules, int64_t day) {
    std::time_t t = static_cast<std::time_t>(day * 86400);
    std::tm u{};
    gmtime_r(&t, &u);
    std::time_t next_week = t + 7 * 86400;
    std::tm n{};
    gmtime_r(&next_week, &n);
    for (auto& r : rules) {
        if (u.tm_wday != r.wday) continue;
        if (r.nth == 0) return true;
        if (r.nth > 0 && (u.tm_mday - 1) / 7 + 1 == r.nth) return true;
        if (r.nth < 0 && n.tm_mon != u.tm_mon) return true;
    }
    return false;
}

int64_t floor_day(int64_t t) { return t >= 0 ? t / 86400 : (t - 86399) / 86400; }

}  // namespace

bool parse_hhmm(const std::string& s, int* minutes) {
    std::string t = book_key(s);
    if (!t.empty() && t.back() == 'Z') t.pop_back();
    if (t.size() != 4 || !std::all_of(t.begin(), t.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return false;
    int h = std::stoi(t.substr(0, 2)), m = std::stoi(t.substr(2));
    if (h > 23 || m > 59) return false;
    *minutes = h * 60 + m;
    return true;
}

bool parse_days(const std::string& s, std::string* canon, std::string* error) {
    std::vector<std::string> out;
    for (auto tok : split(s, ", ")) {
        if (tok == "DAILY") {
            for (auto d : kDays) out.push_back(d);
            continue;
        }
        if (tok == "WEEKDAYS") {
            for (int i = 1; i <= 5; ++i) out.push_back(kDays[i]);
            continue;
        }
        if (tok == "WEEKENDS") {
            out.push_back("SAT");
            out.push_back("SUN");
            continue;
        }
        static const char* kFull[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
        auto day_of = [](const std::string& x) -> std::string {
            for (int i = 0; i < 7; ++i)
                if (x == kDays[i] || x == kFull[i]) return kDays[i];  // MONDAY -> MON
            return "";
        };
        std::string prefix, d = day_of(tok);
        if (d.empty() && tok.size() > 3 && (tok[0] == 'L' || (tok[0] >= '1' && tok[0] <= '5'))) {
            prefix = tok.substr(0, 1);
            d = day_of(tok.substr(1));
        }
        bool ok = !d.empty();
        if (!ok) {
            if (error)
                *error = "days: \"" + tok +
                         "\" isn't a day. Write MON to SUN, DAILY, WEEKDAYS or WEEKENDS; put 1 to 5 or L in front "
                         "for that week of the month (1SAT, LSUN)";
            return false;
        }
        out.push_back(prefix + d);
    }
    if (out.empty()) {
        if (error) *error = "days: say which days the net meets, like TUE,THU or DAILY";
        return false;
    }
    std::vector<std::string> uniq;
    for (auto& d : out)
        if (std::find(uniq.begin(), uniq.end(), d) == uniq.end()) uniq.push_back(d);
    *canon = join(uniq, ',');
    return true;
}

int64_t next_start(const std::string& days, int start, int64_t t) {
    auto rules = day_rules(days);
    if (rules.empty()) return 0;
    for (int64_t day = floor_day(t) - 1, end = day + 400; day <= end; ++day) {
        int64_t at = day * 86400 + start * 60;
        if (at >= t && day_matches(rules, day)) return at;
    }
    return 0;
}

static const char* kSchedCols =
    "name, days, start, minutes, roster, exercise, ncs, note, auto_open, last_start, last_net, channel";

static NetSchedule row_sched(Stmt& q) {
    NetSchedule s;
    s.name = q.text(0);
    s.days = q.text(1);
    s.start = static_cast<int>(q.i64(2));
    s.minutes = static_cast<int>(q.i64(3));
    s.roster = split(q.text(4), "\n");
    s.exercise = q.i64(5) != 0;
    s.ncs = q.text(6);
    s.note = q.text(7);
    s.auto_open = q.i64(8) != 0;
    s.last_start = q.i64(9);
    s.last_net = q.text(10);
    s.channel = q.text(11);
    return s;
}

bool Ops::sched_put(NetSchedule s, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return false;
    };
    s.name = book_key(s.name);
    if (s.name.empty()) return fail("name: give the net a name");
    if (s.name.size() > 64 || !chars_ok(s.name, " -/.")) return fail("name: up to 64 capital letters, digits, spaces and - / .");
    std::string canon;
    if (!parse_days(s.days, &canon, error)) return false;
    s.days = canon;
    if (s.start < 0 || s.start >= 24 * 60) return fail("time: write the start as HHMM in UTC, like 1900");
    if (s.minutes < 5 || s.minutes > 24 * 60) return fail("minutes: a net runs 5 to 1440 minutes");
    if (s.note.size() > 80) return fail("note: keep it to 80 characters");
    s.channel = book_key(s.channel);
    if (!s.channel.empty() && !valid_designator(s.channel))
        return fail("channel: the channel's designator (CH01, P1), never a frequency");
    for (auto& r : s.roster) r = book_key(r);
    auto old = sched_get(s.name);
    Stmt(db_,
         "INSERT OR REPLACE INTO schedules(name, days, start, minutes, roster, exercise, ncs, note, auto_open,"
         " last_start, last_net, channel) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)")
        .bind(1, s.name)
        .bind(2, s.days)
        .bind(3, static_cast<int64_t>(s.start))
        .bind(4, static_cast<int64_t>(s.minutes))
        .bind(5, join(s.roster, '\n'))
        .bind(6, static_cast<int64_t>(s.exercise))
        .bind(7, book_key(s.ncs))
        .bind(8, s.note)
        .bind(9, static_cast<int64_t>(s.auto_open))
        .bind(10, old ? old->last_start : 0)
        .bind(11, old ? old->last_net : std::string())
        .bind(12, s.channel)
        .step();
    return true;
}

bool Ops::sched_remove(const std::string& name) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt q(db_, "DELETE FROM schedules WHERE name = ?");
    q.bind(1, book_key(name));
    q.step();
    return sqlite3_changes(db_) > 0;
}

std::optional<NetSchedule> Ops::sched_get(const std::string& name) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt q(db_, (std::string("SELECT ") + kSchedCols + " FROM schedules WHERE name = ?").c_str());
    q.bind(1, book_key(name));
    if (!q.step()) return std::nullopt;
    return row_sched(q);
}

std::vector<NetSchedule> Ops::sched_list() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt q(db_, (std::string("SELECT ") + kSchedCols + " FROM schedules ORDER BY name").c_str());
    std::vector<NetSchedule> out;
    while (q.step()) out.push_back(row_sched(q));
    return out;
}

std::optional<NetSitting> Ops::sitting_of(const NetSchedule& s, int64_t now) {
    // The sitting under way, if any: it started less than `minutes` ago.
    int64_t len = static_cast<int64_t>(s.minutes) * 60;
    int64_t cur = next_start(s.days, s.start, now - len + 1);
    NetSitting st;
    st.sched = s;
    if (cur && cur <= now) {
        st.start = cur;
        st.end = cur + len;
        if (s.last_start != cur) {
            st.state = "due";
            return st;
        }
        auto n = load_net(s.last_net);
        if (n && n->closed_at == 0) {
            st.state = "open";
            st.net = n->id;
            return st;
        }
        // Opened and already closed: the next one matters now.
        now = st.end;
    }
    int64_t next = next_start(s.days, s.start, now + 1);
    if (!next) return std::nullopt;
    st.start = next;
    st.end = next + len;
    st.state = "upcoming";
    st.net.clear();
    // Opened early, ahead of its start: it is under way.
    if (s.last_start == next) {
        auto n = load_net(s.last_net);
        if (n && n->closed_at == 0) {
            st.state = "open";
            st.net = n->id;
        }
    }
    return st;
}

std::optional<NetSitting> Ops::sched_sitting(const std::string& name) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    auto s = sched_get(name);
    if (!s) return std::nullopt;
    return sitting_of(*s, now());
}

std::vector<NetSitting> Ops::sched_upcoming(int limit) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::vector<NetSitting> out;
    int64_t t = now();
    for (auto& s : sched_list())
        if (auto st = sitting_of(s, t)) out.push_back(*st);
    std::stable_sort(out.begin(), out.end(), [](const NetSitting& a, const NetSitting& b) {
        auto rank = [](const NetSitting& x) { return x.state == "upcoming" ? 1 : 0; };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return a.start < b.start;
    });
    if (limit > 0 && static_cast<int>(out.size()) > limit) out.resize(limit);
    return out;
}

std::string Ops::sched_open(const std::string& name, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    auto s = sched_get(name);
    if (!s) {
        if (error) *error = "no schedule called " + book_key(name);
        return "";
    }
    auto st = sitting_of(*s, now());
    if (st && st->state == "open") {
        if (error) *error = s->name + " is already open as " + st->net;
        return "";
    }
    std::string note = "from schedule " + s->name + (s->note.empty() ? "" : ": " + s->note);
    if (note.size() > 80) note = note.substr(0, 80);
    std::string id = net_open(s->name, s->roster, s->exercise, s->ncs, note, error);
    if (id.empty()) return "";
    // Opened during a sitting or up to 30 minutes ahead of it: that sitting.
    int64_t sitting = 0;
    if (st && (st->state == "due" || st->start - now() <= 30 * 60)) sitting = st->start;
    Stmt(db_, "UPDATE schedules SET last_start = ?, last_net = ? WHERE name = ?")
        .bind(1, sitting ? sitting : s->last_start)
        .bind(2, id)
        .bind(3, s->name)
        .step();
    return id;
}

static std::string hhmmz(int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm u{};
    gmtime_r(&tt, &u);
    char b[16];
    std::snprintf(b, sizeof b, "%02d%02dZ", u.tm_hour, u.tm_min);
    return b;
}

std::vector<std::string> Ops::sched_tick() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::vector<std::string> said;
    int64_t t = now();
    auto once = [&](const std::string& key, int64_t sitting) {
        auto it = told_.find(key);
        if (it != told_.end() && it->second == sitting) return false;
        told_[key] = sitting;
        return true;
    };
    for (auto& s : sched_list()) {
        auto st = sitting_of(s, t);
        if (!st) continue;
        if (st->state == "due") {
            if (s.auto_open) {
                std::string err, id = sched_open(s.name, &err);
                if (!id.empty())
                    said.push_back("scheduled net " + s.name + " opened as " + id);
                else if (once(s.name + "|fail", st->start))
                    said.push_back("scheduled net " + s.name + " didn't open: " + err);
            } else if (once(s.name + "|due", st->start)) {
                said.push_back("scheduled net " + s.name + " was due at " + hhmmz(st->start) + " and isn't open");
            }
        } else if (st->state == "upcoming" && st->start - t <= 15 * 60 && once(s.name + "|soon", st->start)) {
            said.push_back("scheduled net " + s.name + " starts at " + hhmmz(st->start) +
                           (s.auto_open ? " and opens itself" : ""));
        }
    }
    return said;
}

// ---- duty ----

bool Ops::duty_on(const std::string& op, const std::string& note, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::string o = book_key(op);
    if (o.empty() || o.size() > 20) {
        if (error) *error = "operator: a sign or call, up to 20 characters";
        return false;
    }
    if (auto d = duty_current()) {
        if (error) *error = d->op + " is on duty; go off duty first";
        return false;
    }
    Stmt(db_, "INSERT INTO duty(op, on_at) VALUES(?, ?)").bind(1, o).bind(2, now()).step();
    log_add("duty", o + " on duty" + (note.empty() ? "" : ": " + note));
    return true;
}

bool Ops::duty_off(const std::string& turnover, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    auto d = duty_current();
    if (!d) {
        if (error) *error = "nobody is on duty";
        return false;
    }
    int64_t t = now();
    Stmt(db_, "UPDATE duty SET off_at = ?, turnover = ? WHERE id = ?").bind(1, t).bind(2, turnover).bind(3, d->id).step();
    log_add("duty", d->op + " off duty after " + hours((t - d->on_at) / 3600.0) + " h");
    if (!turnover.empty()) log_add("turnover", turnover);
    return true;
}

std::optional<Duty> Ops::duty_current() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt s(db_, "SELECT id, op, on_at, off_at, turnover FROM duty WHERE off_at = 0 ORDER BY id DESC LIMIT 1");
    if (!s.step()) return std::nullopt;
    return Duty{s.i64(0), s.text(1), s.i64(2), s.i64(3), s.text(4)};
}

std::vector<Duty> Ops::duty_list(int64_t from, int64_t to) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt s(db_,
           "SELECT id, op, on_at, off_at, turnover FROM duty WHERE on_at < ? AND (off_at = 0 OR off_at > ?) ORDER BY "
           "on_at");
    s.bind(1, to).bind(2, from);
    std::vector<Duty> out;
    while (s.step()) out.push_back(Duty{s.i64(0), s.text(1), s.i64(2), s.i64(3), s.text(4)});
    return out;
}

// ---- log ----

int64_t Ops::log_add(const std::string& kind, const std::string& text, const std::string& net, bool exercise) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt(db_, "INSERT INTO oplog(at, kind, text, net, exercise) VALUES(?,?,?,?,?)")
        .bind(1, now())
        .bind(2, kind)
        .bind(3, text)
        .bind(4, net)
        .bind(5, static_cast<int64_t>(exercise))
        .step();
    return sqlite3_last_insert_rowid(db_);
}

std::vector<LogEntry> Ops::log_list(int64_t from, int64_t to, int limit) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt s(db_,
           "SELECT id, at, kind, text, net, exercise FROM oplog WHERE at >= ? AND at < ? ORDER BY id DESC LIMIT ?");
    s.bind(1, from).bind(2, to).bind(3, static_cast<int64_t>(limit));
    std::vector<LogEntry> out;
    while (s.step()) out.push_back(LogEntry{s.i64(0), s.i64(1), s.text(2), s.text(3), s.text(4), s.i64(5) != 0});
    std::reverse(out.begin(), out.end());
    return out;
}

// ---- monthly report ----

bool Ops::monthly(const std::string& month, MonthlyReport* r, std::string* error) {
    int64_t from = 0, to = 0;
    if (!month_range(month, &from, &to)) {
        if (error) *error = "month: YYYY-MM";
        return false;
    }
    std::lock_guard<std::recursive_mutex> g(mu_);
    *r = MonthlyReport{};
    r->month = month;
    int64_t t_now = now();

    std::map<std::string, double> by_op;
    for (auto& d : duty_list(from, to)) {
        int64_t a = std::max(d.on_at, from), b = std::min(d.off_at ? d.off_at : t_now, to);
        if (b > a) by_op[d.op] += (b - a) / 3600.0;
    }
    for (auto& [op, h] : by_op) {
        r->hours_by_operator.push_back({op, h});
        r->duty_hours += h;
    }

    {
        std::vector<std::string> ids;
        Stmt s(db_, "SELECT id FROM nets WHERE opened_at >= ? AND opened_at < ? ORDER BY opened_at");
        s.bind(1, from).bind(2, to);
        while (s.step()) ids.push_back(s.text(0));
        for (auto& i : ids)
            if (auto n = load_net(i)) {
                ++r->nets;
                if (n->exercise) ++r->nets_exercise;
                if (n->ncs == station_) ++r->nets_as_ncs;
                r->checkins += static_cast<int>(n->checkins.size());
                r->net_list.push_back(*n);
            }
    }

    // Traffic, from the message store in the same file. A station with no
    // store yet has no traffic.
    sqlite3_stmt* probe = nullptr;
    bool have_messages =
        sqlite3_prepare_v2(db_, "SELECT 1 FROM messages LIMIT 1", -1, &probe, nullptr) == SQLITE_OK;
    sqlite3_finalize(probe);
    if (have_messages) {
        Stmt s(db_, "SELECT direction, prec, state, body FROM messages WHERE created_at >= ? AND created_at < ?");
        s.bind(1, from).bind(2, to);
        while (s.step()) {
            std::string dir = s.text(0), p = s.text(1), st = s.text(2), body = s.blob(3);
            int pi = prec_index(p.empty() ? 'R' : p[0]);
            if (dir == "out") {
                ++r->sent;
                ++r->sent_by_prec[pi];
                if (st == "acked") ++r->sent_acked;
                if (st == "failed") ++r->sent_failed;
            } else {
                ++r->received;
                ++r->received_by_prec[pi];
            }
            if (is_exercise_body(body)) ++r->exercise_msgs;
        }
    }

    for (auto& e : log_list(from, to, 100000)) {
        if (e.kind == "comspot") ++r->comspots;
        if (e.kind == "beadwindow") ++r->beadwindows;
        if (e.kind == "note") ++r->notes;
    }

    auto prec_line = [](const int* v) {
        return "Z " + std::to_string(v[0]) + " O " + std::to_string(v[1]) + " P " + std::to_string(v[2]) + " R " +
               std::to_string(v[3]);
    };
    std::vector<std::string> L;
    L.push_back("MONTHLY STATION REPORT " + month);
    L.push_back("STATION " + station_);
    L.push_back("DUTY HOURS " + hours(r->duty_hours));
    for (auto& [op, h] : r->hours_by_operator) L.push_back("  " + op + " " + hours(h));
    L.push_back("NETS " + std::to_string(r->nets) + ", EXERCISE " + std::to_string(r->nets_exercise) +
                ", AS NET CONTROL " + std::to_string(r->nets_as_ncs) + ", CHECK-INS " + std::to_string(r->checkins));
    for (auto& n : r->net_list) {
        int64_t end = n.closed_at ? n.closed_at : t_now;
        L.push_back("  " + n.id + " " + ddhhmm(n.opened_at) + " " + std::to_string((end - n.opened_at) / 60) +
                    " MIN " + std::to_string(n.checkins.size()) + " IN" + (n.exercise ? " EXER " : " ") + n.name);
    }
    L.push_back("SENT " + std::to_string(r->sent) + ": " + prec_line(r->sent_by_prec) + ", ACKED " +
                std::to_string(r->sent_acked) + ", FAILED " + std::to_string(r->sent_failed));
    L.push_back("RECEIVED " + std::to_string(r->received) + ": " + prec_line(r->received_by_prec));
    L.push_back("EXERCISE MESSAGES " + std::to_string(r->exercise_msgs));
    L.push_back("COMSPOT " + std::to_string(r->comspots) + ", BEADWINDOW " + std::to_string(r->beadwindows) +
                ", LOG NOTES " + std::to_string(r->notes));
    for (auto& l : L) r->text += upper_line(l) + "\n";
    return true;
}

}  // namespace pna
