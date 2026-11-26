#include "bbs.h"

#include "acp201.h"
#include "codress.h"
#include "core.h"
#include "crypto.h"
#include "frame.h"
#include "keys.h"
#include "received.h"
#include "sqlite_stmt.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pna {
namespace bbs {

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Splits on any of seps, trims, drops empties.
std::vector<std::string> split(const std::string& s, const char* seps) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::strchr(seps, c)) {
            if (!trim(cur).empty()) out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string o;
    for (auto& x : v) o += (o.empty() ? "" : sep) + x;
    return o;
}

bool valid_token(const std::string& s) {
    if (s.empty() || s.size() > 16) return false;
    for (char c : s)
        if (!(std::isupper(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-'))
            return false;
    return true;
}

// A rule's or QRU's pattern: a token with at most one trailing '*'.
bool valid_pattern(const std::string& s) {
    std::string t = s;
    if (!t.empty() && t.back() == '*') t.pop_back();
    return valid_token(t);
}

bool as_bool(const std::string& v, bool& out) {
    std::string u = upper(trim(v));
    if (u == "TRUE" || u == "1" || u == "ON" || u == "YES" || u == "Y") return out = true, true;
    if (u == "FALSE" || u == "0" || u == "OFF" || u == "NO" || u == "N") return out = false, true;
    return false;
}

bool as_int(const std::string& v, int lo, int hi, int& out) {
    std::string t = trim(v);
    if (t.empty() || t.size() > 9) return false;
    for (char c : t)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    int n = std::stoi(t);
    if (n < lo || n > hi) return false;
    out = n;
    return true;
}

bool fail(std::string* e, const std::string& why) {
    if (e) *e = why;
    return false;
}

bool valid_sched(const std::string& s) {
    if (s.empty()) return true;
    if (s.size() != 5 || s[4] != 'Z') return false;
    for (int i = 0; i < 4; ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    int hh = std::stoi(s.substr(0, 2)), mm = std::stoi(s.substr(2, 2));
    return hh < 24 && mm < 60;
}

std::string hhmmz(int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char b[8];
    std::snprintf(b, sizeof b, "%02d%02dZ", tm.tm_hour, tm.tm_min);
    return b;
}

std::string first_word_of(const std::string& s) {
    std::istringstream in(s);
    std::string w;
    in >> w;
    return w;
}

std::string strip_cm(const std::string& call) {
    std::string c = upper(trim(call));
    if (c.size() > 3 && c.compare(c.size() - 3, 3, kSuffix) == 0) c.resize(c.size() - 3);
    return c;
}

int64_t system_now() { return static_cast<int64_t>(std::time(nullptr)); }

const char* kDuplicates =
    "an exact copy, or the same message id with the same sender and recipients (a re-file with a new FL2 is not "
    "a duplicate)";

}  // namespace

int zee_limit(const Settings& s) {
    long long n = s.zee_max_bytes > 0 ? s.zee_max_bytes : static_cast<long long>(s.data_rate_bps) * s.duty_s / 8;
    if (n > static_cast<long long>(kMaxBody)) n = kMaxBody;
    if (n < 256) n = 256;
    return static_cast<int>(n);
}

bool matches(const std::string& pattern, const std::string& recipient) {
    std::string p = upper(trim(pattern)), r = upper(trim(recipient));
    if (!p.empty() && p.back() == '*') p.pop_back();
    if (p.empty() || r.empty()) return false;
    return r.compare(0, p.size(), p) == 0;
}

std::vector<std::string> recipients(const std::string& body) {
    received::Message m;
    if (!received::parse(body, m, nullptr)) return {};
    std::vector<std::string> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& s) {
        std::istringstream in(s);
        for (std::string w; in >> w;) {
            while (!w.empty() && (w.back() == '/' || w.back() == ',' || w.back() == '.')) w.pop_back();
            w = upper(w);
            if (valid_token(w) && seen.insert(w).second) out.push_back(w);
        }
    };
    for (auto& r : m.routing) add(r);
    for (auto* list : {&m.to, &m.info, &m.xmt})
        for (auto& a : *list) {
            add(a.ri);
            add(a.plain);
        }
    return out;
}

std::string strip_ri(const std::string& body, const std::string& ri) {
    std::string target = upper(trim(ri));
    if (target.empty()) return body;
    // Word by word on every line: the routing line(s) and any address line
    // carrying it. Text lines never hold an RI on its own as a word in
    // practice; only header lines up to the first BT are touched.
    std::istringstream in(body);
    std::string out, line;
    bool header = true;
    bool first = true;
    while (std::getline(in, line)) {
        std::string cr;
        if (!line.empty() && line.back() == '\r') {
            cr = "\r";
            line.pop_back();
        }
        if (header && trim(line) == "BT") header = false;
        if (header) {
            std::vector<std::string> words;
            std::istringstream ws(line);
            bool hit = false;
            for (std::string w; ws >> w;) {
                std::string bare = w;
                while (!bare.empty() && (bare.back() == ',' || bare.back() == '.')) bare.pop_back();
                if (upper(bare) == target) {
                    hit = true;
                    continue;
                }
                words.push_back(w);
            }
            if (hit) {
                // A line that only carried the RI goes; a routing line keeps its other RIs.
                if (words.empty()) continue;
                line = join(words, " ");
            }
        }
        out += (first ? "" : "\n") + line + cr;
        first = false;
    }
    if (!body.empty() && body.back() == '\n') out += "\n";
    return out;
}

// ---------------------------------------------------------------------------

Engine::Engine(Core& core) : core_(core), clock_(system_now) {
    if (sqlite3_open(core.db_path().c_str(), &db_) != SQLITE_OK) {
        std::string e = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("bbs: cannot open " + core.db_path() + ": " + e);
    }
    // A constructor that throws runs no destructor: the connection is closed here.
    struct CloseOnThrow {
        sqlite3*& db;
        bool done = false;
        ~CloseOnThrow() {
            if (!done) {
                sqlite3_close(db);
                db = nullptr;
            }
        }
    } guard{db_};
    sqlite3_busy_timeout(db_, 2000);
    exec("CREATE TABLE IF NOT EXISTS bbs_meta (k TEXT PRIMARY KEY, v TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS bbs_guard (item TEXT PRIMARY KEY, pos INTEGER NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS bbs_alias (ri TEXT PRIMARY KEY, members TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS bbs_rule (id INTEGER PRIMARY KEY AUTOINCREMENT, pos INTEGER NOT NULL,"
         " match TEXT NOT NULL, route TEXT NOT NULL, ale TEXT NOT NULL, key TEXT NOT NULL DEFAULT '',"
         " sched TEXT NOT NULL DEFAULT '', compress INTEGER NOT NULL DEFAULT 0,"
         " mark_processed INTEGER NOT NULL DEFAULT 1)");
    // Ask 37: every rule carries its world, and only mars is ever routed.
    if (!has_column("bbs_rule", "world")) exec("ALTER TABLE bbs_rule ADD COLUMN world TEXT NOT NULL DEFAULT 'mars'");
    exec("CREATE TABLE IF NOT EXISTS bbs_routed (msg_id TEXT PRIMARY KEY, rule INTEGER NOT NULL, state TEXT NOT NULL,"
         " out_id TEXT NOT NULL DEFAULT '', due INTEGER NOT NULL, processed INTEGER NOT NULL DEFAULT 0,"
         " why TEXT NOT NULL DEFAULT '', at INTEGER NOT NULL)");
    // Audit A: which of the rule's ALE addresses a routed message is on.
    if (!has_column("bbs_routed", "ale_pos")) exec("ALTER TABLE bbs_routed ADD COLUMN ale_pos INTEGER NOT NULL DEFAULT 0");
    exec("CREATE TABLE IF NOT EXISTS bbs_seen (msg_id TEXT PRIMARY KEY)");
    exec("CREATE TABLE IF NOT EXISTS bbs_qru (requester TEXT NOT NULL, line INTEGER NOT NULL, msg_id TEXT NOT NULL,"
         " PRIMARY KEY(requester, line))");
    exec("CREATE TABLE IF NOT EXISTS bbs_zee (requester TEXT NOT NULL, page INTEGER NOT NULL, text TEXT NOT NULL,"
         " PRIMARY KEY(requester, page))");
    exec("CREATE TABLE IF NOT EXISTS bbs_qsl (msg_id TEXT NOT NULL, by TEXT NOT NULL, at INTEGER NOT NULL,"
         " PRIMARY KEY(msg_id, by))");
    exec("CREATE TABLE IF NOT EXISTS bbs_auto (at INTEGER NOT NULL, kind TEXT NOT NULL, msg_id TEXT NOT NULL)");
    // The in box's marks (api_inbox.cpp makes the same table): read here for discards.
    exec("CREATE TABLE IF NOT EXISTS inbox_flags (msg_id TEXT PRIMARY KEY, read_at INTEGER NOT NULL DEFAULT 0,"
         " done TEXT NOT NULL DEFAULT '', done_at INTEGER NOT NULL DEFAULT 0, qsl_at INTEGER NOT NULL DEFAULT 0,"
         " tag TEXT NOT NULL DEFAULT '', corrected BLOB, corrected_at INTEGER NOT NULL DEFAULT 0)");
    // A station that never ran the BBS starts reading after what it holds
    // now: nothing heard before is answered or routed by itself.
    if (meta("scan_after").empty()) {
        Stmt s(db_, "SELECT COALESCE(MAX(rowid), 0) FROM events");
        set_meta("scan_after", std::to_string(s.step() ? s.i64(0) : 0));
    }
    guard.done = true;
}

Engine::~Engine() {
    stop();
    sqlite3_close(db_);
}

// The check's statement is finished before this returns. Left open after its
// row, it keeps a read transaction on this connection, and an ALTER then has
// to upgrade it to a write: SQLite never waits for that (no busy handler, so
// no busy timeout), and fails at once with "database is locked" whenever the
// core's store holds the write lock or has written since the read began.
bool Engine::has_column(const char* table, const char* column) {
    Stmt c(db_, "SELECT COUNT(*) FROM pragma_table_info(?) WHERE name = ?");
    c.bind(1, std::string(table)).bind(2, std::string(column));
    return c.step() && c.i64(0) > 0;
}

void Engine::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string e = err ? err : "?";
        sqlite3_free(err);
        throw std::runtime_error("bbs sqlite: " + e);
    }
}

std::string Engine::meta(const std::string& k) {
    Stmt s(db_, "SELECT v FROM bbs_meta WHERE k = ?");
    s.bind(1, k);
    return s.step() ? s.text(0) : "";
}

void Engine::set_meta(const std::string& k, const std::string& v) {
    Stmt s(db_, "INSERT INTO bbs_meta (k, v) VALUES (?, ?) ON CONFLICT(k) DO UPDATE SET v = excluded.v");
    s.bind(1, k).bind(2, v);
    s.step();
}

Settings Engine::load() {
    Settings s;
    std::map<std::string, std::string> kv;
    {
        Stmt q(db_, "SELECT k, v FROM bbs_meta");
        while (q.step()) kv[q.text(0)] = q.text(1);
    }
    auto b = [&](const char* k, bool& f) {
        if (kv.count(k)) as_bool(kv[k], f);
    };
    auto i = [&](const char* k, int& f) {
        if (kv.count(k)) as_int(kv[k], 0, 1 << 30, f);
    };
    b("machine_mode", s.machine_mode);
    if (kv.count("accept")) s.accept = kv["accept"];
    if (kv.count("ack")) s.ack = kv["ack"];
    i("ack_delay_ms", s.ack_delay_ms);
    if (kv.count("ack_to")) s.ack_to = kv["ack_to"];
    b("commands_ct_only", s.commands_ct_only);
    i("max_errors", s.max_errors);
    b("accept_malformed", s.accept_malformed);
    b("zee_multi", s.zee_multi);
    i("zee_max_bytes", s.zee_max_bytes);
    i("data_rate_bps", s.data_rate_bps);
    i("duty_s", s.duty_s);
    b("remote_sync", s.remote_sync);
    i("purge_days", s.purge_days);
    i("auto_mark_days", s.auto_mark_days);
    i("auto_per_hour", s.auto_per_hour);
    b("routing", s.routing);
    if (kv.count("broadcast_ri")) s.broadcast_ri = kv["broadcast_ri"];
    return s;
}

Settings Engine::settings() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    return load();
}

bool Engine::set(const std::map<std::string, std::string>& kv, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Settings before = load();
    std::map<std::string, std::string> put;
    for (auto& [k, v0] : kv) {
        std::string v = trim(v0);
        bool bv = false;
        int iv = 0;
        if (k == "machine_mode" || k == "commands_ct_only" || k == "accept_malformed" || k == "zee_multi" ||
            k == "remote_sync" || k == "routing") {
            if (!as_bool(v, bv)) return fail(error, k + " must be true or false");
            put[k] = bv ? "true" : "false";
        } else if (k == "accept") {
            if (v != "calls" && v != "calls_ris" && v != "all") return fail(error, "accept must be calls, calls_ris or all");
            put[k] = v;
        } else if (k == "ack") {
            if (v != "off" && v != "auto") return fail(error, "ack must be off or auto");
            put[k] = v;
        } else if (k == "ack_delay_ms") {
            if (!as_int(v, 0, 600000, iv)) return fail(error, "ack_delay_ms must be 0 to 600000");
            put[k] = std::to_string(iv);
        } else if (k == "ack_to") {
            std::string c = strip_cm(v);
            if (!c.empty() && (is_broadcast(c) || c == "ALL" || c.compare(0, 4, "ALL-") == 0))
                return fail(error, "ack_to can't be ALL: give one station, or leave it empty to answer the sender");
            if (!c.empty() && !valid_station(c)) return fail(error, "ack_to must be a call sign");
            if (std::string w = core_.world_refusal(c); !c.empty() && !w.empty())
                return fail(error, "refused: ack_to " + w);
            put[k] = c;
        } else if (k == "max_errors") {
            if (!as_int(v, 0, 100, iv)) return fail(error, "max_errors must be 0 to 100");
            put[k] = std::to_string(iv);
        } else if (k == "zee_max_bytes") {
            if (!as_int(v, 0, static_cast<int>(kMaxBody), iv) || (iv != 0 && iv < 256))
                return fail(error, "zee_max_bytes must be 0 (worked out) or 256 to " + std::to_string(kMaxBody));
            put[k] = std::to_string(iv);
        } else if (k == "data_rate_bps") {
            if (!as_int(v, 50, 9600, iv)) return fail(error, "data_rate_bps must be 50 to 9600");
            put[k] = std::to_string(iv);
        } else if (k == "duty_s") {
            if (!as_int(v, 5, 3600, iv)) return fail(error, "duty_s must be 5 to 3600");
            put[k] = std::to_string(iv);
        } else if (k == "purge_days") {
            if (!as_int(v, 1, 3650, iv)) return fail(error, "purge_days must be 1 to 3650");
            put[k] = std::to_string(iv);
        } else if (k == "auto_mark_days") {
            if (!as_int(v, 0, 3650, iv)) return fail(error, "auto_mark_days must be 0 (never) to 3650");
            put[k] = std::to_string(iv);
        } else if (k == "auto_per_hour") {
            if (!as_int(v, 0, 120, iv)) return fail(error, "auto_per_hour must be 0 to 120");
            put[k] = std::to_string(iv);
        } else if (k == "broadcast_ri") {
            std::string u = upper(v);
            if (!u.empty() && !valid_token(u)) return fail(error, "broadcast_ri must be one RI (A-Z, 0-9)");
            put[k] = u;
        } else if (k == "cmd" || k == "suffix") {
            continue;
        } else {
            return fail(error, "unknown setting " + k);
        }
    }
    for (auto& [k, v] : put) set_meta(k, v);
    Settings after = load();
    if (after.machine_mode != before.machine_mode)
        core_.note(after.machine_mode ? "BBS machine mode ON: other stations can pull traffic and this station answers "
                                        "them by itself (EMCON and the hourly cap still hold)"
                                      : "BBS machine mode OFF: no automatic answers");
    if (after.routing != before.routing)
        core_.note(after.routing ? "BBS routing ON: matching traffic is sent on by the rules (needs machine mode on)"
                                 : "BBS routing OFF: rules kept, nothing routed");
    // A mode turned off takes its standing grant with it (gale_key.h). Turning
    // one on grants nothing: that is the operator's y (standing_prepare).
    if (before.machine_mode && !after.machine_mode) core_.standing_off("machine", "BBS machine mode turned off");
    if (before.routing && !after.routing) core_.standing_off("routing", "BBS routing turned off");
    if (before.ack == "auto" && after.ack != "auto") core_.standing_off("auto_qsl", "BBS auto QSL turned off");
    return true;
}

// ---- guard list and alias map ----

std::vector<std::string> Engine::guard() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::vector<std::string> out;
    Stmt s(db_, "SELECT item FROM bbs_guard ORDER BY pos");
    while (s.step()) out.push_back(s.text(0));
    return out;
}

bool Engine::set_guard(const std::string& items, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::vector<std::string> list;
    std::set<std::string> seen;
    for (auto& t : split(items, ",; \t\n")) {
        std::string u = upper(t);
        if (!valid_token(u)) return fail(error, "guard list: " + t + " isn't a call or RI (A-Z, 0-9, -)");
        if (u.size() > 3 && u.compare(u.size() - 3, 3, kSuffix) == 0)
            return fail(error, "guard list: give calls without -CM (" + t + ")");
        if (seen.insert(u).second) list.push_back(u);
    }
    exec("BEGIN");
    try {
        exec("DELETE FROM bbs_guard");
        int pos = 0;
        for (auto& u : list) {
            Stmt s(db_, "INSERT INTO bbs_guard (item, pos) VALUES (?, ?)");
            s.bind(1, u).bind(2, static_cast<int64_t>(++pos));
            s.step();
        }
        exec("COMMIT");
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
    core_.note("BBS guard list: " + (list.empty() ? std::string("none") : join(list, " ")));
    return true;
}

std::vector<Alias> Engine::aliases() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::vector<Alias> out;
    Stmt s(db_, "SELECT ri, members FROM bbs_alias ORDER BY ri");
    while (s.step()) out.push_back({s.text(0), split(s.text(1), ";")});
    return out;
}

bool Engine::set_alias(const std::string& ri, const std::string& members, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::string r = upper(trim(ri));
    if (!valid_token(r)) return fail(error, "alias: ri must be one RI or call (A-Z, 0-9, -)");
    std::vector<std::string> ms;
    std::set<std::string> seen;
    for (auto& m : split(members, ";,")) {
        std::string u = upper(m);
        if (!valid_token(u)) return fail(error, "alias: member " + m + " isn't a call or RI");
        if (u == r) return fail(error, "alias: " + r + " can't stand for itself");
        if (seen.insert(u).second) ms.push_back(u);
    }
    if (ms.empty()) return fail(error, "alias: give at least one member (A;B;C)");
    Stmt s(db_, "INSERT INTO bbs_alias (ri, members) VALUES (?, ?) ON CONFLICT(ri) DO UPDATE SET members = excluded.members");
    s.bind(1, r).bind(2, join(ms, ";"));
    s.step();
    core_.note("BBS alias " + r + " stands for " + join(ms, " "));
    return true;
}

bool Engine::del_alias(const std::string& ri, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::string r = upper(trim(ri));
    Stmt s(db_, "DELETE FROM bbs_alias WHERE ri = ?");
    s.bind(1, r);
    s.step();
    if (sqlite3_changes(db_) == 0) return fail(error, "no alias " + r);
    core_.note("BBS alias " + r + " removed");
    return true;
}

// ---- routing rules ----

std::vector<Rule> Engine::rules() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::vector<Rule> out;
    Stmt s(db_, "SELECT id, pos, match, route, ale, key, sched, compress, mark_processed, world FROM bbs_rule ORDER BY pos, id");
    while (s.step()) {
        Rule r;
        r.id = s.i64(0);
        r.pos = static_cast<int>(s.i64(1));
        r.match = s.text(2);
        r.route = s.text(3);
        r.ale = split(s.text(4), ",");
        r.key = s.text(5);
        r.sched = s.text(6);
        r.compress = s.i64(7) != 0;
        r.mark_processed = s.i64(8) != 0;
        r.world = s.text(9);  // the rule's service, as stored
        out.push_back(r);
    }
    return out;
}

namespace {
// The key a rule names, as one indicator this station can send with now:
// the indicator itself, or the current key of the net with that name.
std::string resolve_key(keys::Keyring* kr, const std::string& name) {
    if (!kr || name.empty()) return "";
    for (auto& k : kr->list())
        if (k.indicator == name && k.usable) return k.indicator;
    for (auto& k : kr->for_sending())
        if (k.net == name && k.usable) return k.indicator;
    return "";
}
}  // namespace

bool Engine::set_rule(const Rule& r0, int64_t* id, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Rule r = r0;
    r.match = upper(trim(r.match));
    r.route = trim(r.route);
    r.key = upper(trim(r.key));
    r.sched = upper(trim(r.sched));
    if (!valid_pattern(r.match)) return fail(error, "match must be an RI, a partial RI or a call (UHE, AAR*)");
    if (r.route.empty() || r.route.size() > 40) return fail(error, "route: give the outbound stream's name");
    std::vector<std::string> ale;
    for (auto& a : r.ale) {
        std::string u = upper(trim(a));
        if (u.empty()) continue;
        if (!valid_station(u) || is_broadcast(u)) return fail(error, "ALE address " + a + " isn't one (A-Z, 0-9, -)");
        ale.push_back(u);
    }
    if (ale.empty()) return fail(error, "give at least one ALE address, in the order to call them");
    // The rule's service is stored and is the guard (docs/api.md, Services):
    // a rule with no service, or one no table names, is refused, and so is an
    // ALE address whose book entry is of another service (a crossing).
    {
        std::string w = trim(r.world);
        for (auto& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (w.empty()) return fail(error, "refused: a routing rule needs its service");
        if (!core_.services() || !core_.services()->valid(w)) return fail(error, "refused: " + no_table(w));
        r.world = w;
        for (auto& a : ale)
            if (Route rt = core_.route_for(w, a, "", w); !rt.ok) return fail(error, rt.refusal + " (ALE address " + a + ")");
    }
    r.ale = ale;
    if (!valid_sched(r.sched)) return fail(error, "schedule must be empty or HHMMZ");
    if (!r.key.empty() && resolve_key(core_.keyring(), r.key).empty())
        return fail(error, "no key " + r.key + " to send with on this station (our cipher's keys only: keys_list)");
    std::string ale_s = join(r.ale, ",");
    if (r.id == 0) {
        Stmt m(db_, "SELECT COALESCE(MAX(pos), 0) FROM bbs_rule");
        int64_t pos = m.step() ? m.i64(0) + 1 : 1;
        Stmt s(db_, "INSERT INTO bbs_rule (pos, match, route, ale, key, sched, compress, mark_processed, world)"
                    " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        s.bind(1, pos).bind(2, r.match).bind(3, r.route).bind(4, ale_s).bind(5, r.key).bind(6, r.sched)
            .bind(7, static_cast<int64_t>(r.compress)).bind(8, static_cast<int64_t>(r.mark_processed)).bind(9, r.world);
        s.step();
        r.id = sqlite3_last_insert_rowid(db_);
        core_.note("BBS route rule " + std::to_string(r.id) + " added: " + r.match + " -> " + r.route + " (" + ale_s +
                   ", " + r.world + ")");
    } else {
        Stmt s(db_, "UPDATE bbs_rule SET match = ?, route = ?, ale = ?, key = ?, sched = ?, compress = ?,"
                    " mark_processed = ?, world = ? WHERE id = ?");
        s.bind(1, r.match).bind(2, r.route).bind(3, ale_s).bind(4, r.key).bind(5, r.sched)
            .bind(6, static_cast<int64_t>(r.compress)).bind(7, static_cast<int64_t>(r.mark_processed)).bind(8, r.world)
            .bind(9, r.id);
        s.step();
        if (sqlite3_changes(db_) == 0) return fail(error, "no rule " + std::to_string(r.id));
        core_.note("BBS route rule " + std::to_string(r.id) + " changed: " + r.match + " -> " + r.route + " (" + ale_s +
                   ", " + r.world + ")");
    }
    if (id) *id = r.id;
    return true;
}

bool Engine::del_rule(int64_t id, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt s(db_, "DELETE FROM bbs_rule WHERE id = ?");
    s.bind(1, id);
    s.step();
    if (sqlite3_changes(db_) == 0) return fail(error, "no rule " + std::to_string(id));
    // Close the gap so positions stay 1..n.
    auto rs = rules();
    int pos = 0;
    for (auto& r : rs) {
        Stmt u(db_, "UPDATE bbs_rule SET pos = ? WHERE id = ?");
        u.bind(1, static_cast<int64_t>(++pos)).bind(2, r.id);
        u.step();
    }
    core_.note("BBS route rule " + std::to_string(id) + " removed");
    return true;
}

bool Engine::move_rule(int64_t id, const std::string& dir, int to, std::string* error) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    auto rs = rules();
    auto it = std::find_if(rs.begin(), rs.end(), [&](const Rule& r) { return r.id == id; });
    if (it == rs.end()) return fail(error, "no rule " + std::to_string(id));
    int from = static_cast<int>(it - rs.begin());
    int dest;
    if (to > 0) dest = std::min<int>(to, static_cast<int>(rs.size())) - 1;
    else if (dir == "up") dest = std::max(0, from - 1);
    else if (dir == "down") dest = std::min<int>(static_cast<int>(rs.size()) - 1, from + 1);
    else return fail(error, "give dir up or down, or to (a position from 1)");
    Rule moving = rs[from];
    rs.erase(rs.begin() + from);
    rs.insert(rs.begin() + dest, moving);
    int pos = 0;
    for (auto& r : rs) {
        Stmt u(db_, "UPDATE bbs_rule SET pos = ? WHERE id = ?");
        u.bind(1, static_cast<int64_t>(++pos)).bind(2, r.id);
        u.step();
    }
    core_.note("BBS route rule " + std::to_string(id) + " moved to position " + std::to_string(dest + 1));
    return true;
}

// ---- what is held here ----

std::vector<Engine::Held> Engine::pool() {
    std::vector<Held> out;
    Stmt s(db_, "SELECT id, src, dst, prec, body, state, created_at FROM messages WHERE direction = 'in' AND world = 'mars'"
                " AND state IN ('received', 'overheard')"
                // Traffic the operator discarded in the in box (api_inbox.h) isn't held.
                " AND id NOT IN (SELECT msg_id FROM inbox_flags WHERE done = 'discarded')"
                " ORDER BY created_at, rowid");
    while (s.step()) {
        Held h;
        h.id = s.text(0);
        h.src = s.text(1);
        h.dst = s.text(2);
        std::string p = s.text(3);
        h.prec = p.empty() ? 'R' : p[0];
        h.body = s.blob(4);
        h.state = s.text(5);
        h.created = s.i64(6);
        // Ask 40: traffic from a station of the amateur world is never held
        // for QRU, pulled or routed on into MARS.
        if (!core_.world_refusal(strip_cm(h.src)).empty()) continue;
        out.push_back(h);
    }
    return out;
}

std::string Engine::world_block(const std::string& msg_id, const std::string& src) {
    {
        Stmt q(db_, "SELECT world FROM messages WHERE id = ?");
        q.bind(1, msg_id);
        if (q.step() && q.text(0) != "mars")
            return "it is an item of the " + q.text(0) + " world; the MARS BBS never handles it";
    }
    if (std::string why = core_.world_refusal(strip_cm(src)); !why.empty()) return "it came from " + why;
    return "";
}

bool Engine::held_for(const Held& m, const Settings& s, const std::vector<std::string>& guard) {
    received::Message rm;
    if (!received::parse(m.body, rm, nullptr)) return false;  // chat, cards, garbled: not traffic to hold
    if (static_cast<int>(rm.warnings.size()) > s.max_errors && !s.accept_malformed) return false;
    if (m.state == "received") return true;
    if (s.accept == "all") return true;
    if (s.accept == "calls_ris") {
        for (auto& r : recipients(m.body))
            for (auto& gd : guard)
                if (matches(gd, r)) return true;
    }
    return false;
}

std::vector<std::string> Engine::qsl_of(const std::string& msg_id) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::vector<std::string> out;
    Stmt s(db_, "SELECT by FROM bbs_qsl WHERE msg_id = ? ORDER BY at, by");
    s.bind(1, msg_id);
    while (s.step()) out.push_back(s.text(0));
    return out;
}

// ---- the gate on every automatic send ----

int Engine::auto_sent_last_hour() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt s(db_, "SELECT COUNT(*) FROM bbs_auto WHERE at > ?");
    s.bind(1, clock_() - 3600);
    return s.step() ? static_cast<int>(s.i64(0)) : 0;
}

int64_t Engine::auto_oldest_last_hour() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Stmt s(db_, "SELECT MIN(at) FROM bbs_auto WHERE at > ?");
    s.bind(1, clock_() - 3600);
    return s.step() ? s.i64(0) : 0;
}

std::string Engine::gate(bool routed) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Settings s = load();
    if (!s.machine_mode) return "machine mode is off";
    if (routed && !s.routing) return "routing is off";
    if (core_.emcon()) return "EMCON is on";
    if (s.auto_per_hour <= 0) return "automatic sends are set to 0 an hour";
    if (auto_sent_last_hour() >= s.auto_per_hour)
        return "the hourly cap of " + std::to_string(s.auto_per_hour) + " automatic sends is used up";
    return "";
}

void Engine::count_auto(const std::string& kind, const std::string& id) {
    Stmt s(db_, "INSERT INTO bbs_auto (at, kind, msg_id) VALUES (?, ?, ?)");
    s.bind(1, clock_()).bind(2, kind).bind(3, id);
    s.step();
    Stmt d(db_, "DELETE FROM bbs_auto WHERE at < ?");
    d.bind(1, clock_() - 7 * 86400);
    d.step();
    // Its standing mode (gale_key.h): its burst is keyed under that grant,
    // only while the mode is on. Routed traffic is routing, a receipt is
    // auto_qsl, every other answer is machine mode's.
    core_.set_key_mode(id, kind == "route" ? "routing" : kind.compare(0, 4, "QSL ") == 0 ? "auto_qsl" : "machine");
}

// Queues one automatic answer if the gate allows. Returns its id, or "".
std::string Engine::answer(const std::string& to, const std::string& body, const std::string& what) {
    std::string dst = strip_cm(to);
    if (!valid_station(dst) || is_broadcast(dst) || dst.compare(0, 4, "ALL-") == 0) {
        core_.note("BBS: no answer to " + to + " (" + what + "): answers never go to ALL");
        return "";
    }
    if (std::string w = core_.world_refusal(dst); !w.empty()) {
        core_.note("BBS: not answering " + dst + " (" + what + "): " + w);
        return "";
    }
    std::string why = gate(false);
    if (!why.empty()) {
        core_.note("BBS: not answering " + dst + " (" + what + "): " + why);
        return "";
    }
    std::string err;
    // Machine answers are an automatic mode: MARS only until stage 5, as the
    // world_refusal above already holds (greybeard-ale #18).
    std::string id = core_.submit(dst, 'R', body, &err, SendOpts::in("mars"));
    if (id.empty()) {
        core_.note("BBS: answer to " + dst + " (" + what + ") refused: " + err);
        return "";
    }
    count_auto(what, id);
    core_.note("BBS: answered " + dst + " (" + what + "), queued as " + id);
    return id;
}

// ---- machine commands ----

void Engine::command(const Held& m, const std::string& text, bool enciphered) {
    Settings s = load();
    acp201::Chat c;
    if (!acp201::parse(text, c, nullptr)) return;
    const std::string me = core_.station();
    const std::string req = strip_cm(c.from);
    std::string words = upper(trim(c.text));
    bool comp = false;
    for (const char* tag : {"[COMP]", "[ COMP ]"}) {
        size_t p = words.find(tag);
        if (p != std::string::npos) {
            comp = true;
            words.erase(p, std::string(tag).size());
        }
    }
    words = trim(words);
    std::string verb = words.substr(0, words.find(' '));
    std::string params = words.size() > verb.size() ? trim(words.substr(verb.size())) : "";
    const int64_t now = clock_();
    auto block = [&](const std::string& lines) {
        return hhmmz(now) + "\nDE " + me + kSuffix + "\n" + lines + "\nAR";
    };
    core_.note("BBS: machine command from " + req + ": " + words + (enciphered ? " (enciphered)" : " (plain text)"));

    if (s.commands_ct_only && !enciphered) {
        answer(req, block("ERROR MACHINE COMMANDS ARE TAKEN ONLY ON AN ENCRYPTED CIRCUIT"), "error");
        return;
    }
    if (verb == "VER") {
        answer(req, block(std::string("VER ") + kVersion), "VER");
        return;
    }
    if (verb == "QRU") {
        // "QRU U,A" (the samples) is the plain ask: traffic for me, any kind.
        std::vector<std::string> pats;
        for (auto& p : split(params, ", ")) {
            if (p == "U" || p == "A" || p == "U,A") continue;
            pats.push_back(p);
        }
        const bool other = !pats.empty();
        if (!other) pats.push_back(req);
        for (auto& p : pats)
            if (!valid_pattern(p)) {
                answer(req, block("ERROR QRU " + p + " ISNT A CALL OR RI"), "error");
                return;
            }
        auto guard_list = guard();
        std::vector<std::pair<Held, std::vector<std::string>>> hits;
        for (auto& h : pool()) {
            if (!held_for(h, s, guard_list)) continue;
            {
                Stmt q(db_, "SELECT 1 FROM bbs_qsl WHERE msg_id = ? AND by = ?");
                q.bind(1, h.id).bind(2, req);
                if (q.step()) continue;  // that station has it
            }
            auto rs = recipients(h.body);
            bool hit = false;
            for (auto& p : pats)
                for (auto& r : rs) hit = hit || matches(p, r);
            if (hit) hits.push_back({h, rs});
        }
        {
            Stmt d(db_, "DELETE FROM bbs_qru WHERE requester = ?");
            d.bind(1, req);
            d.step();
        }
        if (hits.empty()) {
            answer(req, block("QRU"), "QRU");
            return;
        }
        std::string lines = "QTC " + std::to_string(hits.size()) + (other ? " " + join(pats, ", ") : "");
        int n = 0;
        for (auto& [h, rs] : hits) {
            ++n;
            received::Message rm;
            received::parse(h.body, rm, nullptr);
            int nlines = static_cast<int>(std::count(h.body.begin(), h.body.end(), '\n')) + 1;
            lines += "\n" + std::to_string(n) + " - " + std::to_string(nlines) + " " +
                     (rm.originator.empty() ? h.src : rm.originator) + " #" + (rm.serial.empty() ? "0000" : rm.serial) +
                     " " + (rm.time_filed.empty() ? "-" : rm.time_filed) + " ---> " + join(rs, " ");
            Stmt ins(db_, "INSERT INTO bbs_qru (requester, line, msg_id) VALUES (?, ?, ?)");
            ins.bind(1, req).bind(2, static_cast<int64_t>(n)).bind(3, h.id);
            ins.step();
        }
        answer(req, block(lines), "QRU");
        return;
    }
    // ZEE and QSL take line numbers from the last QRU list (or a message id).
    auto lines_of = [&](std::vector<std::string>& ids, std::vector<std::string>& bad) {
        for (auto& p : split(params, ", ")) {
            Stmt q(db_, "SELECT msg_id FROM bbs_qru WHERE requester = ? AND line = ?");
            int line = 0;
            if (as_int(p, 1, 9999, line)) {
                q.bind(1, req).bind(2, static_cast<int64_t>(line));
                if (q.step()) {
                    ids.push_back(q.text(0));
                    continue;
                }
            } else {
                Stmt q2(db_, "SELECT msg_id FROM bbs_qru WHERE requester = ? AND msg_id = ?");
                q2.bind(1, req).bind(2, p);
                if (q2.step()) {
                    ids.push_back(q2.text(0));
                    continue;
                }
            }
            bad.push_back(p);
        }
    };
    if (verb == "ZEE") {
        std::vector<std::string> ids, bad;
        lines_of(ids, bad);
        if (ids.empty()) {
            answer(req, block("ERROR ZEE " + (params.empty() ? std::string("NEEDS A LINE NUMBER") : params + " NOT ON YOUR LIST") +
                              " SEND QRU FIRST"),
                   "error");
            return;
        }
        std::string note;
        if (!s.zee_multi && ids.size() > 1) {
            note = "MULTI MESSAGE ZEE IS OFF HERE: SENT THE FIRST ONLY";
            ids.resize(1);
        }
        std::string all;
        for (auto& id : ids) {
            Stmt q(db_, "SELECT body FROM messages WHERE id = ?");
            q.bind(1, id);
            if (!q.step()) continue;
            std::string b = q.blob(0);
            while (!b.empty() && (b.back() == '\n' || b.back() == '\r')) b.pop_back();
            all += (all.empty() ? "" : "\n\n") + b;
        }
        if (!note.empty() || !bad.empty())
            all += "\n\n" + block((note.empty() ? "" : note) +
                                  (bad.empty() ? "" : std::string(note.empty() ? "" : "\n") + "NOT ON YOUR LIST: " + join(bad, " ")));
        // Pages no bigger than the size limit, cut at line ends.
        const int limit = zee_limit(s) - 32;
        std::vector<std::string> pages;
        std::string cur;
        std::istringstream in(all);
        for (std::string line; std::getline(in, line);) {
            if (!cur.empty() && static_cast<int>(cur.size() + line.size() + 1) > limit) {
                pages.push_back(cur);
                cur.clear();
            }
            cur += (cur.empty() ? "" : "\n") + line;
        }
        if (!cur.empty()) pages.push_back(cur);
        if (pages.size() > 1)
            for (size_t i = 0; i < pages.size(); ++i)
                pages[i] = "PAGE " + std::to_string(i + 1) + " OF " + std::to_string(pages.size()) + "\n" + pages[i];
        {
            Stmt d(db_, "DELETE FROM bbs_zee WHERE requester = ?");
            d.bind(1, req);
            d.step();
        }
        for (size_t i = 0; i < pages.size(); ++i) {
            Stmt ins(db_, "INSERT INTO bbs_zee (requester, page, text) VALUES (?, ?, ?)");
            ins.bind(1, req).bind(2, static_cast<int64_t>(i + 1)).bind(3, pages[i]);
            ins.step();
        }
        if (comp) core_.note("BBS: " + req + " asked for [COMP]; sent uncompressed (no compression on this station yet)");
        for (auto& p : pages)
            if (answer(req, p, "ZEE").empty()) break;  // the gate closed: the rest can be asked again with ZDK
        return;
    }
    if (verb == "ZDK") {
        std::vector<int> want;
        for (auto& p : split(params, ", ")) {
            int n = 0;
            if (!as_int(p, 1, 9999, n)) {
                answer(req, block("ERROR ZDK " + p + " ISNT A PAGE NUMBER"), "error");
                return;
            }
            want.push_back(n);
        }
        std::vector<std::string> pages;
        {
            Stmt q(db_, "SELECT page, text FROM bbs_zee WHERE requester = ? ORDER BY page");
            q.bind(1, req);
            while (q.step()) {
                int pg = static_cast<int>(q.i64(0));
                if (want.empty() || std::find(want.begin(), want.end(), pg) != want.end()) pages.push_back(q.text(1));
            }
        }
        if (pages.empty()) {
            answer(req, block("ERROR ZDK NO SUCH PAGE OF THE LAST ZEE TO YOU"), "error");
            return;
        }
        for (auto& p : pages)
            if (answer(req, p, "ZDK").empty()) break;
        return;
    }
    if (verb == "QSL") {
        std::vector<std::string> ids, bad;
        lines_of(ids, bad);
        for (auto& id : ids) {
            Stmt ins(db_, "INSERT OR IGNORE INTO bbs_qsl (msg_id, by, at) VALUES (?, ?, ?)");
            ins.bind(1, id).bind(2, req).bind(3, now);
            ins.step();
            core_.note("BBS: " + req + " sent QSL for " + id);
        }
        // A QSL is answered only when it named nothing we know: airtime is scarce.
        if (ids.empty()) answer(req, block("ERROR QSL " + params + " NOT ON YOUR LIST"), "error");
        return;
    }
    if (verb == "SYNC") {
        answer(req, block(s.remote_sync ? "ERROR REMOTE SYNC IS NOT BUILT ON THIS STATION" : "ERROR REMOTE SYNC NOT ALLOWED"),
               "error");
        return;
    }
    answer(req, block("ERROR UNKNOWN COMMAND " + (verb.empty() ? std::string("(NONE)") : verb) + " TRY VER QRU ZEE ZDK QSL"),
           "error");
}

// ---- routing ----

int64_t Engine::next_due(const std::string& sched, int64_t now) {
    if (sched.empty()) return now;
    int hh = std::stoi(sched.substr(0, 2)), mm = std::stoi(sched.substr(2, 2));
    int64_t day = now - now % 86400;
    int64_t t = day + hh * 3600 + mm * 60;
    if (t <= now) t += 86400;  // passed today: same time tomorrow
    return t;
}

std::optional<Rule> Engine::rule_for(const Held& m, const std::vector<Rule>& rs, const Settings& s) {
    auto rec = recipients(m.body);
    // The broadcast RI is struck off before a routed message goes, so it
    // doesn't pick the route either (UHW would otherwise take UHWHHCF).
    for (auto& r : rs)
        for (auto& x : rec)
            if (x != s.broadcast_ri && matches(r.match, x)) return r;
    return std::nullopt;
}

Routed Engine::route_one(const Held& m, const Rule& r, const Settings&, bool dry_run) {
    Routed rt;
    rt.msg_id = m.id;
    rt.rule = r.id;
    rt.state = "due";
    rt.due = next_due(r.sched, clock_());
    rt.why = r.sched.empty() ? "" : "waits for " + r.sched;
    if (dry_run) return rt;
    Stmt s(db_, "INSERT OR IGNORE INTO bbs_routed (msg_id, rule, state, due, at, why) VALUES (?, ?, 'due', ?, ?, ?)");
    s.bind(1, m.id).bind(2, r.id).bind(3, rt.due).bind(4, clock_()).bind(5, rt.why);
    s.step();
    if (rt.due <= clock_()) send_routed(rt);
    return rt;
}

bool Engine::send_routed(Routed& rt) {
    std::string why = gate(true);
    auto save = [&]() {
        Stmt u(db_, "UPDATE bbs_routed SET state = ?, out_id = ?, processed = ?, why = ?, ale_pos = ? WHERE msg_id = ?");
        u.bind(1, rt.state).bind(2, rt.out_id).bind(3, static_cast<int64_t>(rt.processed)).bind(4, rt.why)
            .bind(5, static_cast<int64_t>(rt.ale_pos)).bind(6, rt.msg_id);
        u.step();
    };
    if (!why.empty()) {
        rt.why = "waits: " + why;
        save();
        return false;
    }
    std::optional<Rule> rule;
    for (auto& r : rules())
        if (r.id == rt.rule) rule = r;
    // Read and let go of the row before anything writes: a read left open
    // across the core's write would pin an old snapshot and lock our own.
    bool found = false;
    std::string p, raw, src;
    {
        Stmt q(db_, "SELECT src, prec, body FROM messages WHERE id = ?");
        q.bind(1, rt.msg_id);
        if (q.step()) {
            found = true;
            src = q.text(0);
            p = q.text(1);
            raw = q.blob(2);
        }
    }
    if (!rule || !found) {
        rt.state = "refused";
        rt.why = !rule ? "its rule was removed" : "the message is gone";
        save();
        return false;
    }
    // Ask 40: checked again when it goes, so a rule, a re-run or a sitting
    // that comes due later can't carry amateur traffic into MARS. Routing is
    // an automatic mode: MARS only until stage 5 (greybeard-ale #18).
    std::string why_block = world_block(rt.msg_id, src);
    if (why_block.empty() && rule->world != "mars")
        why_block = "its rule is of the " + (rule->world.empty() ? std::string("(no)") : rule->world) +
                    " service: until stage 5 the BBS routes MARS traffic only";
    if (std::string why = why_block; !why.empty()) {
        rt.state = "refused";
        rt.why = why;
        save();
        core_.note("BBS: routing " + rt.msg_id + " refused: " + why);
        return false;
    }
    char prec = p.empty() ? 'R' : p[0];
    std::string body = strip_ri(raw, load().broadcast_ri);
    // The rule's ALE addresses in calling order, from the one it is on (audit
    // A): an address the core refuses is passed over for the next.
    std::string err, id, dst, refusals;
    for (int pos = std::max(0, rt.ale_pos); pos < static_cast<int>(rule->ale.size()) && id.empty(); ++pos) {
        dst = rule->ale[pos];
        err.clear();
        if (!rule->key.empty() && !codress::is_codress(body)) {
            // Our own cipher only (the cipher slot), as compose_send does.
            std::string ind = resolve_key(core_.keyring(), rule->key), secret, out;
            if (ind.empty() || !core_.keyring()->send_key(ind, secret, &err)) {
                if (err.empty()) err = "no key " + rule->key + " to send with";
            } else if (!codress::encipher(*core_.cipher(), body, ind, secret, out, &err)) {
                // err says why
            } else {
                SendOpts o = SendOpts::in(rule->world);
                o.route_service = rule->world;
                id = core_.submit_codress(dst, prec, out, body, ind, &err, o);
            }
            crypto::wipe(secret);
        } else {
            SendOpts o = SendOpts::in(rule->world);
            o.route_service = rule->world;
            id = core_.submit(dst, prec, body, &err, o);
        }
        if (id.empty()) {
            refusals += (refusals.empty() ? "" : "; ") + dst + ": " + err;
            if (pos + 1 < static_cast<int>(rule->ale.size()))
                core_.note("BBS: routing " + rt.msg_id + " to " + dst + " refused (" + err + "); calling " +
                           rule->ale[pos + 1] + " next");
        } else {
            rt.ale_pos = pos;
        }
    }
    if (id.empty()) {
        rt.state = "refused";
        rt.why = refusals.empty() ? "the rule has no ALE address left to call" : refusals;
        save();
        core_.note("BBS: routing " + rt.msg_id + " by rule " + std::to_string(rule->id) + " refused: " + rt.why);
        return false;
    }
    count_auto("route", id);
    rt.state = "queued";
    rt.out_id = id;
    rt.processed = rule->mark_processed;
    rt.why = "";
    save();
    core_.note("BBS: routed " + rt.msg_id + " by rule " + std::to_string(rule->id) + " (" + rule->route +
               "), queued as " + id + " for " + dst + " (ALE address " + std::to_string(rt.ale_pos + 1) + " of " +
               std::to_string(rule->ale.size()) + "); ALE call order " + join(rule->ale, ",") +
               (rule->compress ? " (compress asked; sent uncompressed)" : ""));
    return true;
}

std::vector<Routed> Engine::rerun(bool dry_run, std::string*) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    Settings s = load();
    auto rs = rules();
    auto guard_list = guard();
    std::vector<Routed> out;
    for (auto& h : pool()) {
        if (h.src == core_.station()) continue;
        {
            Stmt q(db_, "SELECT 1 FROM bbs_routed WHERE msg_id = ?");
            q.bind(1, h.id);
            if (q.step()) continue;  // already routed (or waiting)
        }
        if (!held_for(h, s, guard_list)) continue;
        auto r = rule_for(h, rs, s);
        if (!r) continue;
        out.push_back(route_one(h, *r, s, dry_run));
    }
    if (!dry_run) {
        // route_one may have sent: read the rows back as they stand.
        for (auto& o : out) {
            Stmt q(db_, "SELECT state, out_id, processed, why FROM bbs_routed WHERE msg_id = ?");
            q.bind(1, o.msg_id);
            if (q.step()) {
                o.state = q.text(0);
                o.out_id = q.text(1);
                o.processed = q.i64(2) != 0;
                o.why = q.text(3);
            }
        }
        core_.note("BBS: rules re-run over held traffic: " + std::to_string(out.size()) + " matched");
    }
    return out;
}

std::vector<Routed> Engine::routed(int limit) {
    std::lock_guard<std::recursive_mutex> g(mu_);
    std::vector<Routed> out;
    Stmt s(db_, "SELECT msg_id, rule, state, out_id, due, processed, why, ale_pos FROM bbs_routed"
                " ORDER BY at DESC, rowid DESC LIMIT ?");
    s.bind(1, static_cast<int64_t>(limit));
    while (s.step()) {
        Routed r;
        r.msg_id = s.text(0);
        r.rule = s.i64(1);
        r.state = s.text(2);
        r.out_id = s.text(3);
        r.due = s.i64(4);
        r.processed = s.i64(5) != 0;
        r.why = s.text(6);
        r.ale_pos = static_cast<int>(s.i64(7));
        out.push_back(r);
    }
    return out;
}

// ---- the pump ----

void Engine::process(const std::string& id) {
    {
        Stmt q(db_, "INSERT OR IGNORE INTO bbs_seen (msg_id) VALUES (?)");
        q.bind(1, id);
        q.step();
        if (sqlite3_changes(db_) == 0) return;  // looked at already
    }
    Held h;
    {
        Stmt q(db_, "SELECT src, dst, prec, body, state, created_at FROM messages WHERE id = ? AND direction = 'in'");
        q.bind(1, id);
        if (!q.step()) return;
        h.id = id;
        h.src = q.text(0);
        h.dst = q.text(1);
        std::string p = q.text(2);
        h.prec = p.empty() ? 'R' : p[0];
        h.body = q.blob(3);
        h.state = q.text(4);
        h.created = q.i64(5);
    }
    if (h.state != "received" && h.state != "overheard") return;
    // Ask 40: no machine answer, QSL or route for amateur-world traffic.
    if (std::string why = world_block(id, h.src); !why.empty()) {
        core_.note("BBS: " + id + " not answered or routed: " + why);
        return;
    }
    Settings s = load();
    const std::string me = core_.station();

    // A machine command: an ACP 201 line to our call with -CM.
    std::string text = h.body;
    bool enciphered = false;
    if (auto c = core_.clear(id)) {
        text = c->first;
        enciphered = true;
    } else if (codress::is_codress(h.body)) {
        // Enciphered and not read yet: the core deciphers it as it stores it
        // and logs either "deciphered" or why not. Until one of those is in,
        // look again on that event rather than take it as unreadable.
        bool settled = false;
        {
            Stmt q(db_, "SELECT 1 FROM events WHERE msg_id = ? AND (text LIKE 'deciphered%' OR text LIKE 'codress in key%')"
                        " LIMIT 1");
            q.bind(1, id);
            settled = q.step();
        }
        if (!settled) {
            Stmt d(db_, "DELETE FROM bbs_seen WHERE msg_id = ?");
            d.bind(1, id);
            d.step();
            return;
        }
    }
    {
        std::string first = trim(text.substr(0, text.find_first_of("\r\n")));
        if (enciphered) {
            // A command sent enciphered is a codress message whose text
            // (after the class line) starts with the -CM line.
            received::Message cm;
            if (received::parse(text, cm, nullptr))
                for (auto& l : cm.text) {
                    std::string t = trim(l);
                    if (t.empty()) continue;
                    // The compose form won't take "//" in text, so a command
                    // written there uses single slashes:
                    // "AAM499-CM DE AAR9AA / QRU / 1737Z K".
                    if (upper(first_word_of(t)) == me + kSuffix && t.find("//") == std::string::npos) {
                        std::string n;
                        for (size_t k = 0; k < t.size(); ++k) {
                            if (t[k] == '/' && k > 0 && k + 1 < t.size() && t[k - 1] == ' ' && t[k + 1] == ' ')
                                n += "//";
                            else
                                n += t[k];
                        }
                        t = n;
                    }
                    first = t;
                    break;
                }
        }
        acp201::Chat c;
        if (h.state == "received" && acp201::parse(first, c, nullptr) && upper(first_word_of(c.to)) == me + kSuffix &&
            c.kind != acp201::Kind::Receipt) {
            if (!s.machine_mode) {
                core_.note("BBS: machine command from " + strip_cm(c.from) + " not answered: machine mode is off");
                return;
            }
            command(h, first, enciphered);
            return;
        }
    }

    // Traffic: auto QSL for plain traffic (it gets no ack on the air), then routing.
    received::Message rm;
    if (!received::parse(h.body, rm, nullptr)) return;
    if (h.state == "received" && s.machine_mode && s.ack == "auto" && id.compare(0, 6, "PLAIN-") == 0) {
        acp201::Chat r;
        std::string to = s.ack_to.empty() ? strip_cm(rm.originator.empty() ? h.src : rm.originator) : s.ack_to;
        int64_t now = clock_();
        std::time_t tt = static_cast<std::time_t>(now);
        std::tm tm{};
        gmtime_r(&tt, &tm);
        r.to = to + kSuffix;
        r.from = me + kSuffix;
        r.kind = acp201::Kind::Receipt;
        r.prec = rm.prec ? rm.prec : h.prec;
        r.receipt_of = rm.originator.empty() ? h.src : rm.originator;
        r.receipt_serial = rm.serial.empty() ? "0" : rm.serial;
        r.receipt_time = rm.time_filed;
        r.receipt_by.push_back(me);
        for (auto& gd : guard()) r.receipt_by.push_back(gd);
        r.hour = tm.tm_hour;
        r.minute = tm.tm_min;
        std::string err;
        std::string line = acp201::build(r, &err);
        if (line.empty()) {
            core_.note("BBS: no QSL for " + id + ": " + err);
        } else {
            qsls_.push_back({id, to, line, std::chrono::steady_clock::now() + std::chrono::milliseconds(s.ack_delay_ms)});
        }
    }
    if (s.routing && h.src != me) {
        if (!held_for(h, s, guard())) return;
        auto rs = rules();
        if (auto r = rule_for(h, rs, s)) route_one(h, *r, s, false);
    }
}

void Engine::pump() {
    std::lock_guard<std::recursive_mutex> g(mu_);
    int64_t after = 0;
    {
        std::string a = meta("scan_after");
        if (!a.empty()) after = std::stoll(a);
    }
    // New messages since the last look, by the event log.
    std::vector<std::pair<int64_t, std::string>> ev;
    {
        Stmt s(db_, "SELECT rowid, COALESCE(msg_id, '') FROM events WHERE rowid > ? ORDER BY rowid LIMIT 500");
        s.bind(1, after);
        while (s.step()) ev.push_back({s.i64(0), s.text(1)});
    }
    for (auto& [seq, id] : ev) {
        after = seq;
        if (!id.empty()) {
            try {
                process(id);
            } catch (const std::exception& e) {
                core_.note(std::string("BBS: ") + e.what() + " on " + id);
            }
        }
    }
    if (!ev.empty()) set_meta("scan_after", std::to_string(after));

    // QSLs whose delay is up.
    auto now = std::chrono::steady_clock::now();
    for (auto it = qsls_.begin(); it != qsls_.end();) {
        if (it->due > now) {
            ++it;
            continue;
        }
        answer(it->to, it->text, "QSL " + it->msg_id);
        it = qsls_.erase(it);
    }
    // Routed traffic whose time has come and whose gate is open.
    std::vector<Routed> due;
    {
        Stmt s(db_, "SELECT msg_id, rule, due, ale_pos FROM bbs_routed WHERE state = 'due' AND due <= ?"
                    " ORDER BY due, rowid");
        s.bind(1, clock_());
        while (s.step()) {
            Routed r;
            r.msg_id = s.text(0);
            r.rule = s.i64(1);
            r.due = s.i64(2);
            r.ale_pos = static_cast<int>(s.i64(3));
            r.state = "due";
            due.push_back(r);
        }
    }
    for (auto& r : due)
        if (!send_routed(r) && r.state == "due") break;  // gate shut: the rest wait too
    next_ale();
}

void Engine::next_ale() {
    struct Q {
        std::string msg_id, out_id;
        int64_t rule = 0;
        int ale_pos = 0;
        bool processed = false;
    };
    std::vector<Q> qs;
    {
        Stmt s(db_, "SELECT msg_id, out_id, rule, ale_pos, processed FROM bbs_routed WHERE state = 'queued' AND out_id != ''");
        while (s.step()) qs.push_back({s.text(0), s.text(1), s.i64(2), static_cast<int>(s.i64(3)), s.i64(4) != 0});
    }
    for (auto& q : qs) {
        auto m = core_.get(q.out_id);
        if (!m || (m->state != "failed" && m->state != "unacked" && m->state != "expired")) continue;
        std::optional<Rule> rule;
        for (auto& r : rules())
            if (r.id == q.rule) rule = r;
        Routed rt;
        rt.msg_id = q.msg_id;
        rt.rule = q.rule;
        rt.processed = q.processed;
        rt.out_id = q.out_id;
        const std::string what = m->dst + " (" + m->state + (m->last_error.empty() ? "" : ": " + m->last_error) + ")";
        const int next = q.ale_pos + 1;
        if (!rule || next >= static_cast<int>(rule->ale.size())) {
            rt.state = "failed";
            rt.ale_pos = q.ale_pos;
            rt.why = !rule ? "its rule was removed; last tried " + what
                           : "no ALE address of rule " + std::to_string(rule->id) + " answered; last tried " + what;
            if (m->state == "unacked") rt.why += "; that copy still goes if " + m->dst + " is heard";
            Stmt u(db_, "UPDATE bbs_routed SET state = ?, why = ? WHERE msg_id = ?");
            u.bind(1, rt.state).bind(2, rt.why).bind(3, rt.msg_id);
            u.step();
            core_.note("BBS: routing " + rt.msg_id + " failed: " + rt.why);
            continue;
        }
        // An unacked copy would go again by itself when its station is heard:
        // it is stopped, so the message isn't delivered twice.
        if (m->state == "unacked") core_.cancel(q.out_id, nullptr, "the BBS, calling the rule's next ALE address");
        core_.note("BBS: routed " + rt.msg_id + " got no answer from " + what + "; calling " + rule->ale[next] +
                   ", ALE address " + std::to_string(next + 1) + " of " + std::to_string(rule->ale.size()));
        rt.state = "due";
        rt.ale_pos = next;
        rt.due = clock_();
        rt.why = "calling " + rule->ale[next] + " next";
        {
            Stmt u(db_, "UPDATE bbs_routed SET state = 'due', due = ?, ale_pos = ?, why = ? WHERE msg_id = ?");
            u.bind(1, rt.due).bind(2, static_cast<int64_t>(rt.ale_pos)).bind(3, rt.why).bind(4, rt.msg_id);
            u.step();
        }
        send_routed(rt);
    }
}

void Engine::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] {
        while (running_) {
            try {
                pump();
            } catch (const std::exception& e) {
                core_.note(std::string("BBS pump: ") + e.what());
            }
            for (int i = 0; i < 10 && running_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void Engine::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

// ---- one engine per core ----

namespace {
std::mutex g_mu;
std::map<Core*, std::unique_ptr<Engine>>& engines() {
    static std::map<Core*, std::unique_ptr<Engine>> m;
    return m;
}
}  // namespace

Engine& engine(Core& core) {
    std::lock_guard<std::mutex> g(g_mu);
    auto& e = engines()[&core];
    if (!e) e = std::make_unique<Engine>(core);
    return *e;
}

void start(Core& core) { engine(core).start(); }

void stop(Core& core) {
    std::lock_guard<std::mutex> g(g_mu);
    auto it = engines().find(&core);
    if (it == engines().end()) return;
    // An engine whose open threw leaves its slot empty.
    if (it->second) it->second->stop();
    engines().erase(it);
}

}  // namespace bbs
}  // namespace pna
