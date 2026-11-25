#include "api_inbox.h"

#include "acp127.h"
#include "acp201.h"
#include "bbs.h"
#include "cipher_slot.h"
#include "codress.h"
#include "compose.h"
#include "core.h"
#include "crypto.h"
#include "digest.h"
#include "frame.h"
#include "json.h"
#include "keys.h"
#include "ops.h"
#include "received.h"
#include "sqlite_stmt.h"
#include "store.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace pna {

namespace fs = std::filesystem;

namespace {

std::string err_reply(const std::string& why) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", why).end_object();
    return w.str();
}

bool truthy(const std::string& v) {
    return v == "true" || v == "1" || v == "yes" || v == "on" || v == "Y" || v == "y";
}

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

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s + sep) {
        if (c == sep) {
            std::string t = trim(cur);
            if (!t.empty()) out.push_back(t);
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
}

int64_t now_s() { return static_cast<int64_t>(std::time(nullptr)); }

// ---- the station's database, on a connection of our own ----

class Db {
public:
    explicit Db(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            std::string e = db_ ? sqlite3_errmsg(db_) : "out of memory";
            sqlite3_close(db_);
            db_ = nullptr;
            throw std::runtime_error("in box: cannot open the station's database: " + e);
        }
        sqlite3_busy_timeout(db_, 2000);
        exec("CREATE TABLE IF NOT EXISTS inbox_flags (msg_id TEXT PRIMARY KEY, read_at INTEGER NOT NULL DEFAULT 0,"
             " done TEXT NOT NULL DEFAULT '', done_at INTEGER NOT NULL DEFAULT 0, qsl_at INTEGER NOT NULL DEFAULT 0,"
             " tag TEXT NOT NULL DEFAULT '', corrected BLOB, corrected_at INTEGER NOT NULL DEFAULT 0)");
        exec("CREATE TABLE IF NOT EXISTS inbox_purge (at INTEGER NOT NULL, count INTEGER NOT NULL)");
    }
    ~Db() { sqlite3_close(db_); }
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;
    sqlite3* get() { return db_; }
    void exec(const char* sql) {
        char* e = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &e) != SQLITE_OK) {
            std::string m = e ? e : "error";
            sqlite3_free(e);
            throw std::runtime_error("in box: " + m);
        }
    }
    // True when a table exists (the BBS makes its own tables on first use).
    bool has(const char* table) {
        Stmt s(db_, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?");
        s.bind(1, std::string(table));
        return s.step();
    }

private:
    sqlite3* db_ = nullptr;
};

struct Flags {
    int64_t read_at = 0, done_at = 0, qsl_at = 0, corrected_at = 0;
    std::string done;  // "", "processed", "discarded"
    std::string tag;
    std::string corrected;
};

std::map<std::string, Flags> load_flags(Db& db) {
    std::map<std::string, Flags> out;
    Stmt s(db.get(), "SELECT msg_id, read_at, done, done_at, qsl_at, tag, COALESCE(corrected, ''), corrected_at"
                     " FROM inbox_flags");
    while (s.step()) {
        Flags f;
        f.read_at = s.i64(1);
        f.done = s.text(2);
        f.done_at = s.i64(3);
        f.qsl_at = s.i64(4);
        f.tag = s.text(5);
        f.corrected = s.blob(6);
        f.corrected_at = s.i64(7);
        out[s.text(0)] = f;
    }
    return out;
}

Flags flags_of(Db& db, const std::string& id) {
    Flags f;
    Stmt s(db.get(), "SELECT read_at, done, done_at, qsl_at, tag, COALESCE(corrected, ''), corrected_at"
                     " FROM inbox_flags WHERE msg_id = ?");
    s.bind(1, id);
    if (s.step()) {
        f.read_at = s.i64(0);
        f.done = s.text(1);
        f.done_at = s.i64(2);
        f.qsl_at = s.i64(3);
        f.tag = s.text(4);
        f.corrected = s.blob(5);
        f.corrected_at = s.i64(6);
    }
    return f;
}

void ensure_row(Db& db, const std::string& id) {
    Stmt s(db.get(), "INSERT OR IGNORE INTO inbox_flags (msg_id) VALUES (?)");
    s.bind(1, id);
    s.step();
}

// What the BBS knows of a message: who sent QSL for it, and whether a
// routing rule sent it on (delivered) and marked it processed.
struct BbsView {
    std::map<std::string, std::vector<std::string>> qsl;
    std::map<std::string, std::pair<std::string, bool>> routed;  // state, processed
};

BbsView bbs_view(Db& db) {
    BbsView v;
    if (db.has("bbs_qsl")) {
        Stmt s(db.get(), "SELECT msg_id, by FROM bbs_qsl ORDER BY at, by");
        while (s.step()) v.qsl[s.text(0)].push_back(s.text(1));
    }
    if (db.has("bbs_routed")) {
        Stmt s(db.get(), "SELECT msg_id, state, processed FROM bbs_routed");
        while (s.step()) v.routed[s.text(0)] = {s.text(1), s.i64(2) != 0};
    }
    return v;
}

// ---- messages, read straight from the store's table ----

struct Row {
    int64_t record = 0;  // the row's number: MM's record #
    Message m;
};

const char* kRowCols = "rowid, id, direction, src, dst, prec, body, state, created_at, updated_at, last_error";

Row read_row(Stmt& s) {
    Row r;
    r.record = s.i64(0);
    r.m.id = s.text(1);
    r.m.direction = s.text(2);
    r.m.src = s.text(3);
    r.m.dst = s.text(4);
    std::string p = s.text(5);
    r.m.prec = p.empty() ? 'R' : p[0];
    r.m.body = s.blob(6);
    r.m.state = s.text(7);
    r.m.created_at = s.i64(8);
    r.m.updated_at = s.i64(9);
    r.m.last_error = s.text(10);
    return r;
}

// Messages of one direction, newest first, below a record number (0: from
// the newest). fn returns false to stop.
void each_row(Db& db, const std::string& dir, int64_t below, const std::function<bool(const Row&)>& fn) {
    std::string sql = std::string("SELECT ") + kRowCols + " FROM messages WHERE direction = ?" +
                      (below > 0 ? " AND rowid < ?" : "") + " ORDER BY rowid DESC";
    Stmt s(db.get(), sql.c_str());
    s.bind(1, dir);
    if (below > 0) s.bind(2, below);
    // Read everything first: the callback may write (a read held open across
    // our own write would lock it).
    std::vector<Row> rows;
    while (s.step()) rows.push_back(read_row(s));
    for (auto& r : rows)
        if (!fn(r)) break;
}

std::optional<Row> row_of(Db& db, const std::string& id) {
    Stmt s(db.get(), (std::string("SELECT ") + kRowCols + " FROM messages WHERE id = ?").c_str());
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return read_row(s);
}

const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

std::string dtg_of_time(int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char b[32];
    std::snprintf(b, sizeof b, "%02d%02d%02dZ %s %02d", tm.tm_mday, tm.tm_hour, tm.tm_min, kMonths[tm.tm_mon],
                  (tm.tm_year + 1900) % 100);
    return b;
}

std::string dtg_text(const acp127::Dtg& d) {
    char b[32];
    if (d.year > 0)
        std::snprintf(b, sizeof b, "%02d%02d%02d%c %s %02d", d.day, d.hour, d.minute, d.zone,
                      kMonths[(d.month >= 1 && d.month <= 12) ? d.month - 1 : 0], d.year % 100);
    else
        std::snprintf(b, sizeof b, "%02d%02d%02d%c %s", d.day, d.hour, d.minute, d.zone,
                      kMonths[(d.month >= 1 && d.month <= 12) ? d.month - 1 : 0]);
    return b;
}

std::string first_word(const std::string& s) {
    std::istringstream in(s);
    std::string w;
    in >> w;
    while (!w.empty() && (w.back() == ',' || w.back() == '/')) w.pop_back();
    return w;
}

std::string address_text(const acp127::Address& a) {
    if (!a.plain.empty()) return a.plain;
    return a.ri;
}

// One message as the in-box shows it.
struct Item {
    Row row;
    bool parsed = false;
    std::string parse_error;
    received::Message rm;
    std::string dtg, from, to, serial, type, label, key;
    std::vector<std::string> recipients, qsl;
    bool mine = false, read = false, processed = false, discarded = false, delivered = false, format_error = false,
         enciphered = false;
    Flags f;
};

std::string usmtf_type(const std::vector<std::string>& text) {
    for (auto& l : text) {
        std::string u = upper(trim(l));
        if (u.compare(0, 6, "MSGID/") == 0) {
            size_t a = 6, b = u.find('/', a);
            return u.substr(a, b == std::string::npos ? std::string::npos : b - a);
        }
    }
    return "";
}

Item make_item(const Row& r, const std::map<std::string, Flags>& flags, const BbsView& bv, const std::string& me) {
    Item it;
    it.row = r;
    auto fi = flags.find(r.m.id);
    if (fi != flags.end()) it.f = fi->second;
    it.enciphered = codress::is_codress(r.m.body);
    if (it.enciphered) it.key = codress::indicator_of(r.m.body);
    std::string err;
    it.parsed = received::parse(r.m.body, it.rm, &err);
    it.parse_error = err;
    if (it.parsed) {
        it.dtg = it.rm.has_dtg ? dtg_text(it.rm.dtg) : (it.rm.time_filed.empty() ? dtg_of_time(r.m.created_at) : it.rm.time_filed);
        it.from = !it.rm.originator.empty() ? first_word(it.rm.originator) : r.m.src;
        if (!it.rm.to.empty()) it.to = address_text(it.rm.to.front());
        else if (!it.rm.routing.empty()) it.to = first_word(it.rm.routing.front());
        else it.to = r.m.dst;
        it.serial = it.rm.serial;
        it.type = usmtf_type(it.rm.text);
        for (auto& x : it.rm.routing)
            for (auto& w : split(x, ' ')) it.recipients.push_back(upper(first_word(w)));
        for (auto* v : {&it.rm.to, &it.rm.info, &it.rm.xmt})
            for (auto& a : *v) {
                if (!a.ri.empty()) it.recipients.push_back(upper(a.ri));
                if (!a.plain.empty()) it.recipients.push_back(upper(a.plain));
            }
    } else {
        it.dtg = dtg_of_time(r.m.created_at);
        it.from = r.m.src;
        it.to = r.m.dst;
    }
    it.recipients.push_back(upper(r.m.dst));
    it.mine = r.m.direction == "in" && r.m.state == "received";
    it.format_error = r.m.state == "garbled";
    auto qi = bv.qsl.find(r.m.id);
    if (qi != bv.qsl.end()) it.qsl = qi->second;
    if (it.f.qsl_at && std::find(it.qsl.begin(), it.qsl.end(), me) == it.qsl.end()) it.qsl.push_back(me);
    auto ri = bv.routed.find(r.m.id);
    it.delivered = ri != bv.routed.end() && ri->second.first == "queued";
    it.discarded = it.f.done == "discarded";
    it.processed = it.f.done == "processed" || (it.delivered && ri->second.second);
    it.read = it.f.read_at != 0;
    it.label = it.discarded   ? "DISC"
               : it.processed ? "PROC"
               : it.delivered ? "DELIV"
               : !it.qsl.empty() ? "QSL"
               : it.read      ? "READ"
                              : "NEW";
    return it;
}

void write_item(JsonWriter& w, const Item& it, bool with_body = false) {
    w.begin_object()
        .field("record", static_cast<long long>(it.row.record))
        .field("id", it.row.m.id)
        .field("direction", it.row.m.direction)
        .field("dtg", it.dtg)
        .field("prec", std::string(1, it.parsed && it.rm.prec ? it.rm.prec : it.row.m.prec))
        .field("from", it.from)
        .field("to", it.to)
        .field("serial", it.serial)
        .field("type", it.type)
        .field("state", it.label)
        .field("core_state", it.row.m.state)
        .field("mine", it.mine)
        .field("read", it.read)
        .field("processed", it.processed)
        .field("discarded", it.discarded)
        .field("delivered", it.delivered)
        .field("format_error", it.format_error)
        .field("enciphered", it.enciphered)
        .field("tag", it.f.tag)
        .field("corrected", !it.f.corrected.empty())
        .field("created_at", static_cast<long long>(it.row.m.created_at));
    w.begin_array("qsl");
    for (auto& q : it.qsl) w.field("", q);
    w.end_array();
    if (with_body) w.field("body", it.row.m.body);
    w.end_object();
}

// ---- matching (search and filters) ----

// Case-blind; % stands for any run of characters. Without %, `contains`
// asks for the pattern anywhere, otherwise the whole value must match.
bool like(const std::string& value, const std::string& pattern, bool contains) {
    std::string v = upper(value), p = upper(pattern);
    if (p.find('%') == std::string::npos) return contains ? v.find(p) != std::string::npos : v == p;
    // Glob match with % only.
    size_t vi = 0, pi = 0, star = std::string::npos, mark = 0;
    while (vi < v.size()) {
        if (pi < p.size() && p[pi] == '%') {
            star = pi++;
            mark = vi;
        } else if (pi < p.size() && p[pi] == v[vi]) {
            ++pi;
            ++vi;
        } else if (star != std::string::npos) {
            pi = star + 1;
            vi = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '%') ++pi;
    return pi == p.size();
}

// The values a field name stands for, for one message.
std::vector<std::string> field_values(const Item& it, const std::string& field, bool* known) {
    *known = true;
    const std::string f = upper(field);
    if (f == "FROM" || f == "SENDER") return {it.from, it.row.m.src};
    if (f == "TO" || f == "RECIPIENT") return it.recipients;
    if (f == "SERIAL") return {it.serial};
    if (f == "DTG") return {it.dtg, it.dtg.substr(0, it.dtg.find(' '))};
    if (f == "TYPE") return {it.type};
    if (f == "PREC") return {std::string(1, it.parsed && it.rm.prec ? it.rm.prec : it.row.m.prec)};
    if (f == "STATE") return {it.label, it.row.m.state};
    if (f == "TAG") return {it.f.tag};
    if (f == "ID") return {it.row.m.id};
    if (f == "KEY") return {it.key};
    if (f == "TEXT") return {it.enciphered ? std::string() : (it.parsed ? received::text_of(it.rm) : it.row.m.body)};
    *known = false;
    return {};
}

const char* kFields = "from, to, serial, dtg, type, prec, state, tag, id, key, text";

// The advanced filter: field op value, joined by AND, OR, NOT and
// parentheses. op is = (whole value, % wildcards), != or ~ (contains). It is
// read here into a tree; nothing of it reaches SQLite.
struct Expr {
    enum class K { Cond, And, Or, Not } k = K::Cond;
    std::string field, op, value;
    std::vector<std::unique_ptr<Expr>> kids;
    bool eval(const Item& it) const {
        switch (k) {
            case K::And:
                for (auto& c : kids)
                    if (!c->eval(it)) return false;
                return true;
            case K::Or:
                for (auto& c : kids)
                    if (c->eval(it)) return true;
                return false;
            case K::Not:
                return !kids[0]->eval(it);
            case K::Cond: {
                bool known = false;
                auto vals = field_values(it, field, &known);
                bool any = false;
                for (auto& v : vals)
                    if (op == "~" ? like(v, value, true) : like(v, value, false)) any = true;
                return op == "!=" ? !any : any;
            }
        }
        return false;
    }
};

class ExprParser {
public:
    explicit ExprParser(const std::string& s) : s_(s) {}
    std::unique_ptr<Expr> parse(std::string* error) {
        try {
            auto e = or_expr();
            skip();
            if (i_ < s_.size()) throw std::runtime_error("unexpected \"" + s_.substr(i_, 12) + "\"");
            return e;
        } catch (const std::exception& ex) {
            *error = std::string("filter: ") + ex.what() + " (at character " + std::to_string(i_ + 1) + ")";
            return nullptr;
        }
    }

private:
    const std::string& s_;
    size_t i_ = 0;
    void skip() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    bool keyword(const char* kw) {
        skip();
        size_t n = std::string(kw).size();
        if (i_ + n > s_.size()) return false;
        if (upper(s_.substr(i_, n)) != kw) return false;
        if (i_ + n < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_ + n])) || s_[i_ + n] == '_')) return false;
        i_ += n;
        return true;
    }
    std::string word() {
        skip();
        if (i_ < s_.size() && s_[i_] == '"') {
            size_t e = s_.find('"', i_ + 1);
            if (e == std::string::npos) throw std::runtime_error("a quote is not closed");
            std::string v = s_.substr(i_ + 1, e - i_ - 1);
            i_ = e + 1;
            return v;
        }
        size_t a = i_;
        while (i_ < s_.size() && !std::isspace(static_cast<unsigned char>(s_[i_])) && s_[i_] != '(' && s_[i_] != ')' &&
               s_[i_] != '=' && s_[i_] != '!' && s_[i_] != '~')
            ++i_;
        return s_.substr(a, i_ - a);
    }
    std::unique_ptr<Expr> or_expr() {
        auto e = std::make_unique<Expr>();
        e->k = Expr::K::Or;
        e->kids.push_back(and_expr());
        while (keyword("OR")) e->kids.push_back(and_expr());
        if (e->kids.size() == 1) return std::move(e->kids[0]);
        return e;
    }
    std::unique_ptr<Expr> and_expr() {
        auto e = std::make_unique<Expr>();
        e->k = Expr::K::And;
        e->kids.push_back(unary());
        while (keyword("AND")) e->kids.push_back(unary());
        if (e->kids.size() == 1) return std::move(e->kids[0]);
        return e;
    }
    std::unique_ptr<Expr> unary() {
        if (keyword("NOT")) {
            auto e = std::make_unique<Expr>();
            e->k = Expr::K::Not;
            e->kids.push_back(unary());
            return e;
        }
        skip();
        if (i_ < s_.size() && s_[i_] == '(') {
            ++i_;
            auto e = or_expr();
            skip();
            if (i_ >= s_.size() || s_[i_] != ')') throw std::runtime_error("a parenthesis is not closed");
            ++i_;
            return e;
        }
        auto e = std::make_unique<Expr>();
        e->field = word();
        if (e->field.empty()) throw std::runtime_error("expected a field name (" + std::string(kFields) + ")");
        Item none;
        bool known = false;
        field_values(none, e->field, &known);
        if (!known) throw std::runtime_error("no field \"" + e->field + "\" (fields: " + kFields + ")");
        skip();
        if (s_.compare(i_, 2, "!=") == 0) {
            e->op = "!=";
            i_ += 2;
        } else if (i_ < s_.size() && (s_[i_] == '=' || s_[i_] == '~')) {
            e->op = std::string(1, s_[i_]);
            ++i_;
        } else {
            throw std::runtime_error("expected =, != or ~ after " + e->field);
        }
        e->value = word();
        if (e->value.empty()) throw std::runtime_error("expected a value after " + e->field + " " + e->op);
        return e;
    }
};

// ---- helpers for the calls ----

std::string station_of(Core& core) { return core.station(); }

std::vector<Item> items(Core& core, Db& db, const std::string& dir, int64_t below,
                        const std::function<bool(const Item&)>& keep, int limit) {
    auto flags = load_flags(db);
    BbsView bv = bbs_view(db);
    std::vector<Item> out;
    each_row(db, dir, below, [&](const Row& r) {
        Item it = make_item(r, flags, bv, station_of(core));
        if (keep(it)) out.push_back(std::move(it));
        return static_cast<int>(out.size()) < limit;
    });
    return out;
}

int int_of(std::map<std::string, std::string>& req, const char* k, int def, int lo, int hi, bool* bad) {
    if (!req.count(k) || req[k].empty()) return def;
    try {
        int v = std::stoi(req[k]);
        if (v < lo || v > hi) *bad = true;
        return std::clamp(v, lo, hi);
    } catch (...) {
        *bad = true;
        return def;
    }
}

int64_t record_of(Db& db, const std::string& id) {
    if (id.empty()) return 0;
    Stmt s(db.get(), "SELECT rowid FROM messages WHERE id = ?");
    s.bind(1, id);
    return s.step() ? s.i64(0) : -1;
}

std::optional<Row> incoming(Db& db, const std::string& id, std::string* why) {
    if (id.empty()) {
        *why = "give the message id";
        return std::nullopt;
    }
    auto r = row_of(db, id);
    if (!r) {
        *why = "no message with that id";
        return std::nullopt;
    }
    if (r->m.direction != "in") {
        *why = "that message is one this station sent; the in-box marks are for messages received";
        return std::nullopt;
    }
    return r;
}

void set_done(Db& db, const std::string& id, const std::string& done) {
    ensure_row(db, id);
    Stmt s(db.get(), "UPDATE inbox_flags SET done = ?, done_at = ? WHERE msg_id = ?");
    s.bind(1, done).bind(2, done.empty() ? 0 : now_s()).bind(3, id);
    s.step();
}

void set_read(Db& db, const std::string& id, bool read) {
    ensure_row(db, id);
    Stmt s(db.get(), read ? "UPDATE inbox_flags SET read_at = ? WHERE msg_id = ? AND read_at = 0"
                          : "UPDATE inbox_flags SET read_at = ? WHERE msg_id = ?");
    s.bind(1, read ? now_s() : 0).bind(2, id);
    s.step();
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        out.push_back(l);
    }
    return out;
}

// "3-5,9" (1-based) to the set of line numbers. False on a bad range.
bool pick_lines(const std::string& spec, size_t count, std::vector<size_t>& out, std::string* why) {
    for (auto& part : split(spec, ',')) {
        size_t dash = part.find('-');
        try {
            size_t a = std::stoul(part.substr(0, dash));
            size_t b = dash == std::string::npos ? a : std::stoul(part.substr(dash + 1));
            if (a < 1 || b < a || b > count) {
                *why = "lines: " + part + " is not within 1-" + std::to_string(count);
                return false;
            }
            for (size_t i = a; i <= b; ++i) out.push_back(i);
        } catch (...) {
            *why = "lines: give numbers and ranges such as 3-5,9";
            return false;
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.empty()) {
        *why = "lines: none picked";
        return false;
    }
    return true;
}

// The key a name stands for, as one indicator this station can send with:
// the indicator itself or the current key of the net with that name.
std::string resolve_key(keys::Keyring* kr, const std::string& name) {
    if (!kr || name.empty()) return "";
    for (auto& k : kr->list())
        if (k.indicator == name && k.usable) return k.indicator;
    for (auto& k : kr->for_sending())
        if (k.net == name && k.usable) return k.indicator;
    return "";
}

// Lines of 69 or fewer, broken at spaces (the station log's width).
void wrap_into(std::vector<std::string>& out, const std::string& line, size_t width = 69) {
    std::string rest = line;
    while (rest.size() > width) {
        size_t cut = rest.rfind(' ', width);
        if (cut == std::string::npos || cut < 10) cut = width;
        out.push_back(rest.substr(0, cut));
        rest = "        " + trim(rest.substr(cut));
    }
    out.push_back(rest);
}

bool parse_day(const std::string& s, int64_t* t) {
    int y = 0, m = 0, d = 0;
    char extra = 0;
    if (std::sscanf(s.c_str(), "%4d-%2d-%2d%c", &y, &m, &d, &extra) != 3) return false;
    if (y < 2000 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    *t = static_cast<int64_t>(timegm(&tm));
    std::tm back{};
    std::time_t tt = static_cast<std::time_t>(*t);
    gmtime_r(&tt, &back);
    return back.tm_mday == d;  // 2026-02-30 is refused
}

// The event that opened this run of the core: "station X starting on ...".
std::pair<int64_t, int64_t> session_start(Db& db) {
    Stmt s(db.get(), "SELECT rowid, ts FROM events WHERE COALESCE(msg_id, '') = '' AND text LIKE 'station % starting on %'"
                     " ORDER BY rowid DESC LIMIT 1");
    if (s.step()) return {s.i64(0), s.i64(1)};
    return {0, 0};
}

// in/out for the IN/OUT tab: what an event line says of traffic, or "".
std::string io_dir(const Event& e) {
    auto starts = [&](const char* p) { return e.text.compare(0, std::string(p).size(), p) == 0; };
    if (!e.msg_id.empty()) {
        if (starts("received ") || starts("heard ")) return "in";
        if (starts("sent") || starts("sending") || starts("acked by") || starts("queued ") || starts("failed after") ||
            starts("resent ") || starts("retransmitted "))
            return "out";
        return "";
    }
    if (starts("BBS: answered ")) return "out";
    if (starts("BBS: ") && (e.text.find(" sent QSL for ") != std::string::npos ||
                            e.text.find("machine command") != std::string::npos))
        return "in";
    return "";
}

// ---- the calls ----

std::string inbox_list(Core& core, Db& db, std::map<std::string, std::string>& req) {
    const std::string dir = req["dir"];
    if (dir != "in" && dir != "out") return err_reply("dir is in or out");
    bool bad = false;
    int limit = int_of(req, "limit", 50, 1, 1000, &bad);
    if (bad) return err_reply("limit must be 1 to 1000");
    int64_t below = record_of(db, req["after_id"]);
    if (below < 0) return err_reply("after_id: no message with that id");
    const bool mine = truthy(req["mine"]), hide_delivered = truthy(req["hide_delivered"]),
               include_processed = truthy(req["include_processed"]);
    auto got = items(core, db, dir, below,
                     [&](const Item& it) {
                         if (dir == "out") return true;
                         if (mine && !it.mine) return false;
                         if (hide_delivered && it.delivered) return false;
                         if (!include_processed && (it.processed || it.discarded)) return false;
                         return true;
                     },
                     limit + 1);
    const bool more = static_cast<int>(got.size()) > limit;
    if (more) got.resize(limit);
    JsonWriter w;
    w.begin_object().field("ok", true).field("dir", dir).begin_array("messages");
    for (auto& it : got) write_item(w, it);
    w.end_array().field("more", more).field("last_id", got.empty() ? "" : got.back().row.m.id).end_object();
    return w.str();
}

std::string last_processed(Db& db) {
    Stmt s(db.get(), "SELECT msg_id FROM inbox_flags WHERE done = 'processed' ORDER BY done_at DESC, rowid DESC LIMIT 1");
    return s.step() ? s.text(0) : "";
}

bool pending_item(const Item& it, bool all) {
    if (it.processed || it.discarded || it.format_error) return false;
    if (it.row.m.state == "received") return true;
    return all && it.row.m.state == "overheard";
}

bool error_item(const Item& it) { return it.format_error && !it.discarded && !it.processed; }

std::string pending(Core& core, Db& db, std::map<std::string, std::string>& req) {
    bool bad = false;
    int limit = int_of(req, "limit", 200, 1, 5000, &bad);
    if (bad) return err_reply("limit must be 1 to 5000");
    const bool all = truthy(req["all"]);
    auto p = items(core, db, "in", 0, [&](const Item& it) { return pending_item(it, all); }, 100000);
    auto e = items(core, db, "in", 0, error_item, 100000);
    JsonWriter w;
    w.begin_object().field("ok", true).field("pending_count", static_cast<int>(p.size()))
        .field("errors_count", static_cast<int>(e.size())).field("last_processed", last_processed(db));
    w.begin_array("pending");
    for (size_t i = 0; i < p.size() && static_cast<int>(i) < limit; ++i) write_item(w, p[i]);
    w.end_array().begin_array("errors");
    for (size_t i = 0; i < e.size() && static_cast<int>(i) < limit; ++i) {
        const Item& it = e[i];
        w.begin_object()
            .field("record", static_cast<long long>(it.row.record))
            .field("id", it.row.m.id)
            .field("dtg", it.dtg)
            .field("from", it.from)
            .field("raw", it.row.m.body);
        w.begin_array("errors");
        if (!it.parsed && !it.parse_error.empty()) w.field("", it.parse_error);
        if (!it.row.m.last_error.empty()) w.field("", it.row.m.last_error);
        if (it.parsed)
            for (auto& x : it.rm.warnings) w.field("", x);
        if (it.parsed && it.rm.warnings.empty() && it.row.m.last_error.empty())
            w.field("", "heard damaged: the frame check failed");
        w.end_array().end_object();
    }
    w.end_array().end_object();
    return w.str();
}

std::string mark(Core& core, Db& db, std::map<std::string, std::string>& req) {
    std::string why;
    auto r = incoming(db, req["id"], &why);
    if (!r) return err_reply(why);
    const std::string st = req["state"];
    if (st == "processed" || st == "discarded") {
        set_done(db, r->m.id, st);
    } else if (st == "reviewed" || st == "read") {
        set_read(db, r->m.id, true);
    } else if (st == "new") {
        set_done(db, r->m.id, "");
        set_read(db, r->m.id, false);
    } else {
        return err_reply("state is processed, discarded, reviewed or new");
    }
    core.note_on(r->m.id, "marked " + st + " by the operator");
    auto flags = load_flags(db);
    Item it = make_item(*r, flags, bbs_view(db), core.station());
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", r->m.id).field("state", it.label).end_object();
    return w.str();
}

std::string mark_all(Core& core, Db& db, std::map<std::string, std::string>& req) {
    const std::string list = req["list"];
    std::string st = req["state"];
    if (list != "pending" && list != "errors") return err_reply("list is pending or errors");
    if (st.empty()) st = list == "errors" ? "discarded" : "processed";
    if (st != "processed" && st != "discarded") return err_reply("state is processed or discarded");
    const bool all = truthy(req["all"]);
    auto got = list == "pending" ? items(core, db, "in", 0, [&](const Item& it) { return pending_item(it, all); }, 100000)
                                 : items(core, db, "in", 0, error_item, 100000);
    db.exec("BEGIN");
    try {
        for (auto& it : got) set_done(db, it.row.m.id, st);
        db.exec("COMMIT");
    } catch (...) {
        db.exec("ROLLBACK");
        throw;
    }
    core.note("in box: " + std::to_string(got.size()) + " message(s) on the " + list + " list marked " + st +
              " by the operator");
    JsonWriter w;
    // The ids marked, so a screen can update its rows without reading the list again.
    w.begin_object().field("ok", true).field("marked", static_cast<int>(got.size())).field("state", st).begin_array("ids");
    for (auto& it : got) w.field("", it.row.m.id);
    w.end_array().end_object();
    return w.str();
}

std::string qsl(Core& core, Db& db, std::map<std::string, std::string>& req) {
    std::string why;
    auto r = incoming(db, req["id"], &why);
    if (!r) return err_reply(why);
    const bool send = truthy(req["send"]);
    ensure_row(db, r->m.id);
    {
        Stmt s(db.get(), "UPDATE inbox_flags SET qsl_at = ? WHERE msg_id = ? AND qsl_at = 0");
        s.bind(1, now_s()).bind(2, r->m.id);
        s.step();
    }
    std::string out_id, not_sent;
    if (send) {
        received::Message rm;
        if (!received::parse(r->m.body, rm, nullptr)) {
            not_sent = "only ACP 127 traffic takes a QSL receipt";
        } else if (r->m.id.compare(0, 6, "PLAIN-") != 0) {
            not_sent = "it was acknowledged on the air when it came in, so no receipt is needed";
        } else {
            const std::string me = core.station();
            std::string to = first_word(rm.originator.empty() ? r->m.src : rm.originator);
            if (to.size() > 3 && to.compare(to.size() - 3, 3, "-CM") == 0) to.resize(to.size() - 3);
            acp201::Chat c;
            int64_t now = now_s();
            std::time_t tt = static_cast<std::time_t>(now);
            std::tm tm{};
            gmtime_r(&tt, &tm);
            c.to = to + "-CM";
            c.from = me + "-CM";
            c.kind = acp201::Kind::Receipt;
            c.prec = rm.prec ? rm.prec : r->m.prec;
            c.receipt_of = rm.originator.empty() ? r->m.src : first_word(rm.originator);
            c.receipt_serial = rm.serial.empty() ? "0" : rm.serial;
            c.receipt_time = rm.time_filed;
            c.receipt_by.push_back(me);
            c.hour = tm.tm_hour;
            c.minute = tm.tm_min;
            std::string err;
            std::string line = acp201::build(c, &err);
            if (line.empty()) return err_reply("no receipt could be made: " + err);
            out_id = core.submit(to, 'R', line, &err);
            if (out_id.empty()) return err_reply("receipt refused: " + err);
        }
    }
    core.note_on(r->m.id, out_id.empty() ? "QSL by the operator"
                                         : "QSL by the operator; receipt queued as " + out_id);
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", r->m.id).field("sent", !out_id.empty()).field("out_id", out_id);
    if (!not_sent.empty()) w.field("not_sent", not_sent);
    if (!out_id.empty() && core.emcon()) w.field("emcon", "EMCON is on: the receipt waits in the outbox");
    w.end_object();
    return w.str();
}

std::string safe_name(const std::string& id) {
    std::string n;
    for (char c : id) n += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return n;
}

std::string export_msg(Core& core, Db& db, std::map<std::string, std::string>& req) {
    auto r = row_of(db, req["id"]);
    if (req["id"].empty()) return err_reply("give the message id");
    if (!r) return err_reply("no message with that id");
    fs::path dir = fs::path(core.db_path()).parent_path() / "export";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return err_reply("can't make the export folder");
    Flags f = flags_of(db, r->m.id);
    // As received: an enciphered message stays enciphered on disk.
    std::vector<std::string> files;
    auto put = [&](const std::string& name, const std::string& text) {
        fs::path p = dir / name;
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out << text;
        if (!text.empty() && text.back() != '\n') out << "\n";
        if (!out) return false;
        files.push_back(name);
        return true;
    };
    if (!put(safe_name(r->m.id) + ".txt", r->m.body)) return err_reply("the export file couldn't be written");
    if (!f.corrected.empty() && !put(safe_name(r->m.id) + ".corrected.txt", f.corrected))
        return err_reply("the corrected copy couldn't be written");
    core.note_on(r->m.id, "exported by the operator");
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", r->m.id).field("folder", dir.string()).begin_array("files");
    for (auto& n : files) w.field("", n);
    w.end_array().end_object();
    return w.str();
}

std::string retransmit(Core& core, Db& db, std::map<std::string, std::string>& req) {
    auto r = row_of(db, req["id"]);
    if (req["id"].empty()) return err_reply("give the message id");
    if (!r) return err_reply("no message with that id");
    if (!truthy(req["confirm"]))
        return err_reply("retransmit keys the radio: send confirm=Y once the operator has said yes");
    const std::string me = core.station();
    std::string de = upper(trim(req["de"]));
    if (!de.empty() && de != me) return err_reply("this station sends as " + me + " only");
    std::string dst = upper(trim(req["ale"]));
    if (dst.empty()) dst = upper(trim(req["to"]));
    if (dst.empty()) return err_reply("give the station to send to (to, or ale for its ALE address)");
    if (!valid_station(dst) && !is_broadcast(dst)) return err_reply("not a station call or ALE address: " + dst);
    char prec = r->m.prec;
    if (!req["prec"].empty()) {
        std::string p = upper(req["prec"]);
        if (p.size() != 1 || std::string("ZOPR").find(p[0]) == std::string::npos)
            return err_reply("precedence must be Z, O, P or R");
        prec = p[0];
    }
    Flags f = flags_of(db, r->m.id);
    std::string body = f.corrected.empty() ? r->m.body : f.corrected;
    const bool enciphered = codress::is_codress(body);
    if (!req["lines"].empty()) {
        if (enciphered) return err_reply("pick lines from a message in the clear; an enciphered one goes whole");
        auto all = lines_of(body);
        std::vector<size_t> pick;
        std::string why;
        if (!pick_lines(req["lines"], all.size(), pick, &why)) return err_reply(why);
        std::string part;
        for (size_t n : pick) part += all[n - 1] + "\r\n";
        body = part;
    }
    const std::string key_name = upper(trim(req["key"]));
    std::string err, id, ind;
    if (!key_name.empty()) {
        if (enciphered) return err_reply("it is enciphered already; send it without a key");
        ind = resolve_key(core.keyring(), key_name);
        std::string secret, out;
        if (ind.empty() || !core.keyring()->send_key(ind, secret, &err)) {
            crypto::wipe(secret);
            return err_reply(err.empty() ? "no key " + key_name + " to send with on this station" : err);
        }
        bool ok = core.cipher() && codress::encipher(*core.cipher(), body, ind, secret, out, &err);
        crypto::wipe(secret);
        if (!ok) return err_reply(err.empty() ? "no cipher installed" : err);
        id = core.submit_codress(dst, prec, out, body, ind, &err);
    } else {
        id = core.submit(dst, prec, body, &err);
    }
    if (id.empty()) return err_reply(err);
    const bool compress = truthy(req["compress"]);
    std::string route = trim(req["route"]);
    core.note_on(r->m.id, "retransmitted by the operator as " + id + " to " + dst +
                              (route.empty() ? "" : " (route " + route + ")") +
                              (compress ? "; compress asked, sent uncompressed" : ""));
    CoreStatus s = core.status();
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", r->m.id).field("out_id", id).field("to", dst)
        .field("key", ind).field("route", s.modem)
        .field("route_note", "everything goes out the station's one transmit route until transmit is unlocked");
    if (compress) w.field("compress", "no compression on this station yet: sent uncompressed");
    if (core.emcon()) w.field("emcon", "EMCON is on: it waits in the outbox");
    w.end_object();
    return w.str();
}

std::string tag(Core& core, Db& db, std::map<std::string, std::string>& req) {
    std::string why;
    auto r = incoming(db, req["id"], &why);
    if (!r) return err_reply(why);
    std::string text = trim(req["text"]);
    if (text.size() > 80) return err_reply("a tag is 80 characters or fewer");
    if (text.find_first_of("\r\n") != std::string::npos) return err_reply("a tag is one line");
    if (compose::looks_like_frequency(text)) return err_reply("a tag can't hold a frequency: name the channel instead");
    ensure_row(db, r->m.id);
    Stmt s(db.get(), "UPDATE inbox_flags SET tag = ? WHERE msg_id = ?");
    s.bind(1, text).bind(2, r->m.id);
    s.step();
    core.note_on(r->m.id, text.empty() ? "tag cleared by the operator" : "tagged by the operator");
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", r->m.id).field("tag", text).end_object();
    return w.str();
}

std::string correct(Core& core, Db& db, std::map<std::string, std::string>& req) {
    std::string why;
    auto r = incoming(db, req["id"], &why);
    if (!r) return err_reply(why);
    const std::string body = req["body"];
    if (body.size() > 65536) return err_reply("the corrected copy is too long (64 KB at most)");
    ensure_row(db, r->m.id);
    Stmt s(db.get(), "UPDATE inbox_flags SET corrected = ?, corrected_at = ? WHERE msg_id = ?");
    if (trim(body).empty()) s.bind(1, std::string()).bind(2, static_cast<int64_t>(0));
    else s.bind_blob(1, body).bind(2, now_s());
    s.bind(3, r->m.id);
    s.step();
    core.note_on(r->m.id, trim(body).empty() ? "corrected copy removed by the operator"
                                             : "corrected copy saved by the operator; the message as received is kept");
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", r->m.id).field("corrected", !trim(body).empty()).end_object();
    return w.str();
}

// reply_draft: a reply to a message received, filled in for MSG-COMPOSE.
// Nothing is sent and nothing is stored: the screen opens the compose form
// with these values and the operator writes the text.
std::string strip_cm(std::string c) {
    c = upper(first_word(c));
    if (c.size() > 3 && c.compare(c.size() - 3, 3, "-CM") == 0) c.resize(c.size() - 3);
    return c;
}

std::string subject_of(const std::vector<std::string>& text) {
    for (auto& l : text) {
        std::string u = trim(l);
        std::string up = upper(u);
        if (up.compare(0, 5, "SUBJ/") == 0 || up.compare(0, 5, "SUBJ:") == 0) {
            std::string v = trim(u.substr(5));
            while (!v.empty() && v.back() == '/') v.pop_back();
            return trim(v);
        }
    }
    return "";
}

std::string reply_draft(Core& core, Db& db, std::map<std::string, std::string>& req) {
    std::string why;
    auto r = incoming(db, req["id"], &why);
    if (!r) return err_reply(why);
    std::map<std::string, std::string> v;
    std::string tmpl = "GENADMIN", proc, subject, to_station;
    received::Message rm;
    acp201::Chat chat;
    const std::string first_line = r->m.body.substr(0, r->m.body.find_first_of("\r\n"));
    if (received::parse(r->m.body, rm, nullptr)) {
        to_station = strip_cm(rm.originator.empty() ? r->m.src : rm.originator);
        std::string to;
        if (!rm.from.ri.empty() && !rm.from.plain.empty()) to = rm.from.ri + "/" + rm.from.plain;
        else to = !rm.from.ri.empty() ? rm.from.ri : rm.from.plain;
        if (to.empty()) to = to_station;
        proc = received::procedure(rm);
        if (proc == "ACP127_CODRESS") proc.clear();  // a reply goes in the clear unless the operator picks a key
        subject = subject_of(rm.text);
        v["to"] = to;
        v["to_station"] = to_station;
        if (proc == "ACP127_PLAINDRESS" && !rm.originator.empty()) v["routing"] = first_word(rm.originator);
        if (rm.prec) v["prec"] = std::string(1, rm.prec);
    } else if (acp201::parse(first_line, chat, nullptr)) {
        tmpl = "FREE_FORM";
        proc = "ACP201_CHAT";
        to_station = strip_cm(chat.from.empty() ? r->m.src : chat.from);
        v["to"] = to_station;
        v["to_station"] = to_station;
    } else {
        tmpl = "FREE_FORM";
        to_station = strip_cm(r->m.src);
        v["to"] = to_station;
        v["to_station"] = to_station;
    }
    if (v["prec"].empty()) v["prec"] = std::string(1, r->m.prec ? r->m.prec : 'R');
    // Only a procedure the template takes; else the template's own.
    compose::Context ctx;
    ctx.station = core.station();
    std::string err;
    if (!proc.empty() && compose::form(tmpl, proc, ctx, &err).empty()) proc.clear();
    if (tmpl == "GENADMIN") v["subj"] = subject;
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("id", r->m.id)
        .field("template", tmpl)
        .field("procedure", proc)
        .field("to", v["to"])
        .field("to_station", to_station)
        .field("subject", subject)
        .field("prec", v["prec"]);
    w.begin_object("values");
    for (auto& [k, x] : v) w.field(k, x);
    w.end_object();
    w.begin_object("in_reply_to")
        .field("id", r->m.id)
        .field("from", to_station)
        .field("subject", subject)
        .field("serial", rm.serial)
        .end_object();
    w.end_object();
    return w.str();
}

std::string search(Core& core, Db& db, std::map<std::string, std::string>& req) {
    bool bad = false;
    int limit = int_of(req, "limit", 200, 1, 5000, &bad);
    if (bad) return err_reply("limit must be 1 to 5000");
    std::vector<std::string> said;
    const std::string sender = trim(req["sender"]), serial = trim(req["serial"]), dtg = trim(req["dtg"]),
                      recipient = trim(req["recipient"]);
    std::vector<std::string> types;
    for (auto& t : split(req["types"], ',')) types.push_back(upper(t));
    std::set<std::string> filters;
    for (auto& f : split(req["filters"], ',')) {
        std::string x = upper(f);
        if (x != "DELIVERED" && x != "UNPROCESSED" && x != "PROCESSED" && x != "DISCARDED")
            return err_reply("filters: delivered, unprocessed, processed, discarded");
        filters.insert(x);
    }
    std::unique_ptr<Expr> expr;
    if (!trim(req["expr"]).empty()) {
        std::string e;
        const std::string text = req["expr"];
        ExprParser p(text);
        expr = p.parse(&e);
        if (!expr) return err_reply(e);
    }
    if (!sender.empty()) said.push_back("SENDER CONTAINS " + upper(sender));
    if (!serial.empty()) said.push_back("SERIAL = " + upper(serial));
    if (!dtg.empty()) said.push_back("DTG " + std::string(dtg.find('%') == std::string::npos ? "CONTAINS " : "= ") + upper(dtg));
    if (!recipient.empty()) said.push_back("RECIPIENT CONTAINS " + upper(recipient));
    if (!types.empty()) {
        std::string t;
        for (auto& x : types) t += (t.empty() ? "" : ",") + x;
        said.push_back("TYPE IN " + t);
    }
    std::string fs_text;
    for (auto& f : filters) fs_text += (fs_text.empty() ? "" : " OR ") + f;
    said.push_back(filters.empty() ? "NOT DISCARDED" : "(" + fs_text + ")");
    if (expr) said.push_back("(" + trim(req["expr"]) + ")");
    std::string effective;
    for (auto& s : said) effective += (effective.empty() ? "" : " AND ") + s;

    auto any_like = [](const std::vector<std::string>& vals, const std::string& p, bool contains) {
        for (auto& v : vals)
            if (like(v, p, contains)) return true;
        return false;
    };
    auto got = items(core, db, "in", 0,
                     [&](const Item& it) {
                         if (!sender.empty() && !any_like({it.from, it.row.m.src}, sender, true)) return false;
                         if (!serial.empty() && !like(it.serial, serial, false)) return false;
                         if (!dtg.empty() && !any_like({it.dtg}, dtg, true)) return false;
                         if (!recipient.empty() && !any_like(it.recipients, recipient, true)) return false;
                         if (!types.empty() && std::find(types.begin(), types.end(), upper(it.type)) == types.end())
                             return false;
                         if (filters.empty()) {
                             if (it.discarded) return false;
                         } else {
                             bool ok = (filters.count("DELIVERED") && it.delivered) ||
                                       (filters.count("UNPROCESSED") && !it.processed && !it.discarded) ||
                                       (filters.count("PROCESSED") && it.processed) ||
                                       (filters.count("DISCARDED") && it.discarded);
                             if (!ok) return false;
                         }
                         if (expr && !expr->eval(it)) return false;
                         return true;
                     },
                     limit + 1);
    const bool more = static_cast<int>(got.size()) > limit;
    if (more) got.resize(limit);
    JsonWriter w;
    w.begin_object().field("ok", true).field("filter", effective).field("more", more).begin_array("messages");
    for (auto& it : got) write_item(w, it);
    w.end_array().end_object();
    return w.str();
}

std::string log_stream(Core& core, Db& db, std::map<std::string, std::string>& req) {
    const std::string stream = req["stream"];
    if (stream != "io" && stream != "app" && stream != "msg") return err_reply("stream is io, app or msg");
    bool bad = false;
    int limit = int_of(req, "limit", 100, 1, 1000, &bad);
    long long after = 0;
    try {
        if (!req["after"].empty()) after = std::stoll(req["after"]);
    } catch (...) {
        bad = true;
    }
    if (bad) return err_reply("after and limit must be numbers (limit 1 to 1000)");
    const std::string q = upper(trim(req["q"]));
    std::string out_route;
    if (stream == "io") out_route = core.status().modem;
    // For EVENTS with a search: the messages whose id or fields match.
    std::set<std::string> ids;
    if (stream == "msg" && !q.empty()) {
        auto got = items(core, db, "in", 0,
                         [&](const Item& it) {
                             return like(it.row.m.id, q, true) || like(it.from, q, true) || like(it.serial, q, true) ||
                                    like(it.f.tag, q, true);
                         },
                         5000);
        for (auto& it : got) ids.insert(it.row.m.id);
        for (auto& it : items(core, db, "out", 0, [&](const Item& it) { return like(it.row.m.id, q, true); }, 5000))
            ids.insert(it.row.m.id);
    }
    JsonWriter w;
    w.begin_object().field("ok", true).field("stream", stream).begin_array("events");
    int n = 0;
    long long last = after;
    // Walk the log in pages until enough lines of this stream are found.
    for (long long from = after; n < limit;) {
        auto ev = core.events(from, 1000);
        if (ev.empty()) break;
        for (auto& e : ev) {
            from = e.seq;
            if (n >= limit) break;
            last = e.seq;
            std::string dir;
            if (stream == "app") {
                if (!e.msg_id.empty()) continue;
                if (!q.empty() && upper(e.text).find(q) == std::string::npos) continue;
            } else if (stream == "msg") {
                if (e.msg_id.empty()) continue;
                if (!q.empty() && !ids.count(e.msg_id) && upper(e.text).find(q) == std::string::npos) continue;
            } else {
                dir = io_dir(e);
                if (dir.empty()) continue;
                if (!q.empty() && upper(e.text).find(q) == std::string::npos && upper(e.msg_id).find(q) == std::string::npos)
                    continue;
            }
            w.begin_object()
                .field("seq", static_cast<long long>(e.seq))
                .field("ts", static_cast<long long>(e.ts))
                .field("id", e.msg_id)
                .field("text", e.text);
            if (!dir.empty()) {
                w.field("dir", dir);
                if (dir == "out") w.field("route", out_route);
            }
            w.end_object();
            ++n;
        }
        if (ev.size() < 1000) break;
    }
    w.end_array().field("last", last).end_object();
    return w.str();
}

std::string activity_counters(Core&, Db& db) {
    auto [seq, ts] = session_start(db);
    auto count = [&](const char* sql, std::function<void(Stmt&)> bind) {
        Stmt s(db.get(), sql);
        bind(s);
        return s.step() ? s.i64(0) : 0;
    };
    int64_t stored = count("SELECT COUNT(*) FROM messages WHERE direction = 'in'", [](Stmt&) {});
    int64_t received = count("SELECT COUNT(*) FROM events WHERE rowid > ? AND COALESCE(msg_id, '') <> ''"
                             " AND text LIKE 'received %'",
                             [&](Stmt& s) { s.bind(1, seq); });
    int64_t dups = count("SELECT COUNT(*) FROM events WHERE rowid > ? AND COALESCE(msg_id, '') <> ''"
                         " AND text LIKE 'heard % again'",
                         [&](Stmt& s) { s.bind(1, seq); });
    int64_t processed = count("SELECT COUNT(*) FROM inbox_flags WHERE done = 'processed' AND done_at >= ?",
                              [&](Stmt& s) { s.bind(1, ts); });
    int64_t purged = count("SELECT COALESCE(SUM(count), 0) FROM inbox_purge WHERE at >= ?",
                           [&](Stmt& s) { s.bind(1, ts); });
    int64_t routes = db.has("bbs_rule") ? count("SELECT COUNT(*) FROM bbs_rule", [](Stmt&) {}) : 0;
    bool routing = false;
    if (db.has("bbs_meta")) {
        Stmt s(db.get(), "SELECT v FROM bbs_meta WHERE k = 'routing'");
        if (s.step()) routing = truthy(s.text(0));
    }
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("session_since", static_cast<long long>(ts))
        .field("watched_routes", static_cast<int>(routes))
        .field("routing", routing)
        .field("stored", static_cast<int>(stored))
        .field("received_session", static_cast<int>(received))
        .field("processed_session", static_cast<int>(processed))
        .field("duplicates_session", static_cast<int>(dups))
        .field("purged_session", static_cast<int>(purged))
        .end_object();
    return w.str();
}

std::string report(Core& core, Db& db, std::map<std::string, std::string>& req) {
    const std::string kind = req["kind"];
    if (kind != "station_log" && kind != "comtrafstat" && kind != "comtrafsum")
        return err_reply("kind is station_log, comtrafstat or comtrafsum");
    int64_t now = now_s();
    int64_t from = now - now % 86400, to = from;
    if (!req["from"].empty() && !parse_day(req["from"], &from)) return err_reply("from: a UTC day as YYYY-MM-DD");
    if (!req["to"].empty() && !parse_day(req["to"], &to)) return err_reply("to: a UTC day as YYYY-MM-DD");
    if (req["to"].empty() && !req["from"].empty()) to = from;
    if (to < from) return err_reply("to is before from");
    const int64_t end = to + 86400;  // the whole last day
    bool bad = false;
    int per_page = int_of(req, "lines_per_page", 60, 10, 200, &bad);
    if (bad) return err_reply("lines_per_page must be 10 to 200");
    const std::string me = core.station();
    auto day = [](int64_t t) {
        std::time_t tt = static_cast<std::time_t>(t);
        std::tm tm{};
        gmtime_r(&tt, &tm);
        char b[16];
        std::snprintf(b, sizeof b, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        return std::string(b);
    };
    std::vector<std::string> lines;
    bool has_draft = false;
    if (kind == "station_log") {
        wrap_into(lines, "STATION LOG " + me + " " + day(from) + " TO " + day(to));
        std::vector<std::pair<int64_t, std::string>> rows;
        if (Ops* ops = core.ops())
            for (auto& e : ops->log_list(from, end, 10000))
                rows.push_back({e.at, dtg_of_time(e.at).substr(0, 7) + " " + upper(e.kind) + " " + e.text});
        for (const char* dir : {"in", "out"})
            each_row(db, dir, 0, [&](const Row& r) {
                if (r.m.created_at >= from && r.m.created_at < end)
                    rows.push_back({r.m.created_at, dtg_of_time(r.m.created_at).substr(0, 7) + " " +
                                                        (std::string(dir) == "in" ? "IN  " : "OUT ") +
                                                        std::string(1, r.m.prec) + " " + r.m.src + " TO " + r.m.dst +
                                                        " " + upper(r.m.state) + " " + r.m.id});
                return true;
            });
        std::stable_sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& r : rows) wrap_into(lines, r.second);
        if (rows.empty()) lines.push_back("NOTHING LOGGED IN THESE DAYS");
    } else if (kind == "comtrafstat") {
        wrap_into(lines, "COMTRAFSTAT " + me + " AS OF " + dtg_of_time(now));
        lines.push_back("PENDING ACTION");
        auto p = items(core, db, "in", 0, [](const Item& it) { return pending_item(it, false); }, 100000);
        for (auto& it : p)
            wrap_into(lines, "  " + it.dtg + " " +
                                 std::string(1, it.parsed && it.rm.prec ? it.rm.prec : it.row.m.prec) + " " + it.from +
                                 (it.serial.empty() ? "" : " NR " + it.serial) + " " + it.row.m.id);
        if (p.empty()) lines.push_back("  NONE");
        lines.push_back("OUTGOING NOT YET DELIVERED");
        int out = 0;
        each_row(db, "out", 0, [&](const Row& r) {
            const auto& st = r.m.state;
            if (st == "queued" || st == "sending" || st == "held" || st == "failed" || st == "sent" || st == "expired") {
                wrap_into(lines, "  " + dtg_of_time(r.m.created_at) + " " + std::string(1, r.m.prec) + " TO " + r.m.dst +
                                     " " + upper(st) + " " + r.m.id);
                ++out;
            }
            return true;
        });
        if (!out) lines.push_back("  NONE");
        auto e = items(core, db, "in", 0, error_item, 100000);
        lines.push_back("FORMATTING ERRORS " + std::to_string(e.size()));
    } else {
        int in_by[4] = {0, 0, 0, 0}, out_by[4] = {0, 0, 0, 0};
        int overheard = 0, garbled = 0, acked = 0, failed = 0;
        auto pi = [](char p) { return p == 'Z' ? 0 : p == 'O' ? 1 : p == 'P' ? 2 : 3; };
        each_row(db, "in", 0, [&](const Row& r) {
            if (r.m.created_at < from || r.m.created_at >= end) return true;
            if (r.m.state == "received") ++in_by[pi(r.m.prec)];
            else if (r.m.state == "overheard") ++overheard;
            else if (r.m.state == "garbled") ++garbled;
            return true;
        });
        each_row(db, "out", 0, [&](const Row& r) {
            if (r.m.created_at < from || r.m.created_at >= end) return true;
            if (r.m.state == "cancelled") return true;
            ++out_by[pi(r.m.prec)];
            if (r.m.state == "acked") ++acked;
            if (r.m.state == "failed" || r.m.state == "expired") ++failed;
            return true;
        });
        int processed = 0, discarded = 0;
        {
            Stmt s(db.get(), "SELECT done, COUNT(*) FROM inbox_flags WHERE done_at >= ? AND done_at < ? GROUP BY done");
            s.bind(1, from).bind(2, end);
            while (s.step()) {
                if (s.text(0) == "processed") processed = static_cast<int>(s.i64(1));
                if (s.text(0) == "discarded") discarded = static_cast<int>(s.i64(1));
            }
        }
        auto row4 = [](const char* name, const int* v) {
            char b[96];
            std::snprintf(b, sizeof b, "%-9s Z %-4d O %-4d P %-4d R %-4d TOTAL %d", name, v[0], v[1], v[2], v[3],
                          v[0] + v[1] + v[2] + v[3]);
            return std::string(b);
        };
        wrap_into(lines, "COMTRAFSUM " + me + " " + day(from) + " TO " + day(to));
        lines.push_back(row4("RECEIVED", in_by));
        lines.push_back(row4("SENT", out_by));
        lines.push_back("SENT AND ACKNOWLEDGED " + std::to_string(acked) + "  NOT DELIVERED " + std::to_string(failed));
        lines.push_back("OVERHEARD " + std::to_string(overheard) + "  FORMATTING ERRORS " + std::to_string(garbled));
        lines.push_back("PROCESSED " + std::to_string(processed) + "  DISCARDED " + std::to_string(discarded));
        auto p = items(core, db, "in", 0, [](const Item& it) { return pending_item(it, false); }, 100000);
        lines.push_back("PENDING ACTION NOW " + std::to_string(p.size()));
        has_draft = true;
    }
    std::string text;
    for (auto& l : lines) text += l + "\n";
    JsonWriter w;
    w.begin_object().field("ok", true).field("kind", kind).field("from", day(from)).field("to", day(to))
        .field("lines", static_cast<int>(lines.size())).field("text", text);
    w.begin_array("pages");
    for (size_t i = 0; i < lines.size(); i += static_cast<size_t>(per_page)) {
        std::string pg;
        for (size_t j = i; j < lines.size() && j < i + static_cast<size_t>(per_page); ++j) pg += lines[j] + "\n";
        w.field("", pg);
    }
    w.end_array();
    if (has_draft) {
        // Opens in MSG-COMPOSE as a draft; nothing is sent from here.
        std::string body;
        for (size_t i = 1; i < lines.size(); ++i) body += lines[i] + "\n";
        w.begin_object("draft")
            .field("template", "FREE_FORM")
            .field("prec", "R")
            .field("subject", "COMTRAFSUM " + day(from) + " TO " + day(to))
            .field("text", body)
            .end_object();
    }
    w.end_object();
    return w.str();
}


// Old traffic out of the store (BBS-SETUP: purge processed/discarded after N
// days). In box: processed or discarded, marked longer ago than the age.
// Out box: acked, cancelled, failed or expired, last changed longer ago.
// Never: anything still pending, a sent message still waiting for its ack or
// QSL, plain traffic this station hasn't QSL'd, routed traffic not yet sent,
// a message on a QRU list just given, or a sent part a resend may need.
// A dry run (the default) only says what would go.
std::string purge(Core& core, Db& db, std::map<std::string, std::string>& req) {
    const bool dry_run = req["dry_run"].empty() || truthy(req["dry_run"]);
    int days = bbs::engine(core).settings().purge_days;
    bool bad = false;
    days = int_of(req, "days", days, 1, 3650, &bad);
    if (bad) return err_reply("days must be 1 to 3650");
    const int64_t now = now_s(), cutoff = now - static_cast<int64_t>(days) * 86400;
    auto flags = load_flags(db);
    std::set<std::string> routing_due, on_qru, part_sent;
    if (db.has("bbs_routed")) {
        Stmt s(db.get(), "SELECT msg_id FROM bbs_routed WHERE state = 'due'");
        while (s.step()) routing_due.insert(s.text(0));
    }
    if (db.has("bbs_qru")) {
        Stmt s(db.get(), "SELECT msg_id FROM bbs_qru");
        while (s.step()) on_qru.insert(s.text(0));
    }
    if (db.has("part_asks")) {
        Stmt s(db.get(), "SELECT sent_id FROM part_asks WHERE sent_id <> ''");
        while (s.step()) part_sent.insert(s.text(0));
    }
    auto qsl_answered = [&](const std::string& id) {
        Stmt s(db.get(), "SELECT 1 FROM events WHERE text LIKE ? LIMIT 1");
        s.bind(1, "%(QSL " + id + ")%");
        return s.step();
    };
    struct Pick {
        std::string id, dir, state;
        int64_t age_days;
    };
    std::vector<Pick> picks;
    std::map<std::string, int> kept;
    each_row(db, "in", 0, [&](const Row& r) {
        auto fi = flags.find(r.m.id);
        const Flags f = fi == flags.end() ? Flags{} : fi->second;
        if (f.done.empty()) {
            if (r.m.created_at < cutoff) ++kept["pending"];
            return true;
        }
        if (f.done_at >= cutoff) return true;
        if (routing_due.count(r.m.id)) {
            ++kept["routing_due"];
            return true;
        }
        if (on_qru.count(r.m.id)) {
            ++kept["on_qru"];
            return true;
        }
        if (f.done == "processed" && r.m.state == "received" && r.m.id.compare(0, 6, "PLAIN-") == 0 && !f.qsl_at &&
            !qsl_answered(r.m.id)) {
            ++kept["owed_qsl"];
            return true;
        }
        picks.push_back({r.m.id, "in", f.done, (now - f.done_at) / 86400});
        return true;
    });
    each_row(db, "out", 0, [&](const Row& r) {
        if (r.m.updated_at >= cutoff) return true;
        const auto& st = r.m.state;
        if (st == "sent") {
            ++kept["owed_qsl"];
            return true;
        }
        if (st != "acked" && st != "cancelled" && st != "failed" && st != "expired") {
            ++kept["pending"];
            return true;
        }
        if (part_sent.count(r.m.id)) {
            ++kept["part_resend"];
            return true;
        }
        picks.push_back({r.m.id, "out", st, (now - r.m.updated_at) / 86400});
        return true;
    });
    int n_in = 0, n_out = 0;
    for (auto& p : picks) (p.dir == "in" ? n_in : n_out)++;
    int purged = 0;
    if (!dry_run && !picks.empty()) {
        std::vector<std::string> ids;
        for (auto& p : picks) ids.push_back(p.id);
        purged = core.purge(ids, "older than " + std::to_string(days) + " days (in box " + std::to_string(n_in) +
                                     ", out box " + std::to_string(n_out) + "), by the operator");
        db.exec("BEGIN");
        try {
            for (auto& id : ids) {
                Stmt(db.get(), "DELETE FROM inbox_flags WHERE msg_id = ?").bind(1, id).step();
                if (db.has("bbs_routed")) Stmt(db.get(), "DELETE FROM bbs_routed WHERE msg_id = ?").bind(1, id).step();
            }
            Stmt(db.get(), "INSERT INTO inbox_purge (at, count) VALUES (?, ?)").bind(1, now).bind(2, static_cast<int64_t>(purged)).step();
            db.exec("COMMIT");
        } catch (...) {
            db.exec("ROLLBACK");
            throw;
        }
    }
    bool bad_limit = false;
    int limit = int_of(req, "limit", 200, 1, 5000, &bad_limit);
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("dry_run", dry_run)
        .field("days", days)
        .field("count", static_cast<int>(picks.size()))
        .field("in", n_in)
        .field("out", n_out)
        .field("purged", purged);
    w.begin_object("kept");
    for (const char* k : {"pending", "owed_qsl", "routing_due", "on_qru", "part_resend"}) w.field(k, kept[k]);
    w.end_object();
    w.begin_array("items");
    for (size_t i = 0; i < picks.size() && static_cast<int>(i) < limit; ++i)
        w.begin_object()
            .field("id", picks[i].id)
            .field("direction", picks[i].dir)
            .field("state", picks[i].state)
            .field("age_days", static_cast<long long>(picks[i].age_days))
            .end_object();
    w.end_array().end_object();
    return w.str();
}

}  // namespace

void write_inbox_extras(JsonWriter& w, Core& core, const Message& m, bool read) {
    try {
        Db db(core.db_path());
        if (read && m.direction == "in") set_read(db, m.id, true);
        auto r = row_of(db, m.id);
        if (!r) return;
        auto flags = load_flags(db);
        Item it = make_item(*r, flags, bbs_view(db), core.station());
        w.begin_array("inbox");
        write_item(w, it);
        w.end_array();
        w.begin_array("events");
        {
            Stmt s(db.get(), "SELECT rowid, ts, text FROM events WHERE msg_id = ? ORDER BY rowid");
            s.bind(1, m.id);
            while (s.step())
                w.begin_object()
                    .field("seq", static_cast<long long>(s.i64(0)))
                    .field("ts", static_cast<long long>(s.i64(1)))
                    .field("text", s.text(2))
                    .end_object();
        }
        w.end_array();
        const bool enc = codress::is_codress(m.body);
        std::string decoded, note;
        if (enc) {
            if (auto c = core.clear(m.id)) decoded = c->first;
            else note = "No key for this message on this station.";
        } else {
            note = "Sent in the clear: nothing to decode.";
        }
        w.begin_object("props")
            .field("precedence", acp127::prec_name(it.parsed && it.rm.prec ? it.rm.prec : m.prec))
            .field("compress", false)
            .field("encrypt", enc)
            .field("key", enc ? codress::indicator_of(m.body) : "")
            .field("source", m.src)
            .field("destination", m.dst)
            .field("ale", m.direction == "in" ? m.src : m.dst)
            .field("data_rate", "")
            .field("interleave", "")
            .field("checksum", digest::check_name(digest::check(m.body)))
            .field("props_note", "data rate and interleave belong to the modem; the core doesn't see them")
            .end_object();
        w.field("decoded", decoded).field("decoded_note", note).field("corrected_text", it.f.corrected);
    } catch (const std::exception& e) {
        w.field("inbox_error", e.what());
    }
}

// report_export: the same report as `report`, written to the station's export
// folder as KIND-FROM-TO.txt (gale-term's name), the way export writes a message.
std::string report_export(Core& core, Db& db, std::map<std::string, std::string>& req) {
    std::string rep = report(core, db, req);
    JsonValue v;
    if (!parse_json(rep, v) || !v.is_object()) return err_reply("the report couldn't be read back");
    const JsonValue* ok = v.get("ok");
    if (!ok || ok->type != JsonValue::Type::Bool || !ok->b) return rep;  // report's own error
    auto str = [&](const char* k) {
        const JsonValue* x = v.get(k);
        return x && x->type == JsonValue::Type::String ? x->s : std::string();
    };
    std::string text = str("text");
    fs::path dir = fs::path(core.db_path()).parent_path() / "export";
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return err_reply("can't make the export folder");
    std::string name;
    for (char c : str("kind") + "-" + str("from") + "-" + str("to") + ".txt")
        name += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') ? c : '_';
    std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
    out << text;
    if (!text.empty() && text.back() != '\n') out << "\n";
    out.close();
    if (!out) return err_reply("the report file couldn't be written");
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("kind", str("kind"))
        .field("from", str("from"))
        .field("to", str("to"))
        .field("folder", dir.string())
        .field("file", name)
        .field("bytes", static_cast<long long>(text.size() + (text.empty() || text.back() == '\n' ? 0 : 1)))
        .end_object();
    return w.str();
}

std::optional<std::string> handle_inbox(Core& core, const std::string& cmd, std::map<std::string, std::string>& req) {
    static const std::set<std::string> calls = {"pending", "mark",   "mark_all", "qsl",    "export", "retransmit",
                                                "tag",     "correct", "search",  "report", "activity_counters", "purge",
                                                "reply_draft", "report_export"};
    const bool list_in = cmd == "list" && req.count("dir");
    const bool log_stream_call = cmd == "log" && req.count("stream");
    if (!calls.count(cmd) && !list_in && !log_stream_call) return std::nullopt;
    try {
        Db db(core.db_path());
        if (list_in) return inbox_list(core, db, req);
        if (log_stream_call) return log_stream(core, db, req);
        if (cmd == "pending") return pending(core, db, req);
        if (cmd == "mark") return mark(core, db, req);
        if (cmd == "mark_all") return mark_all(core, db, req);
        if (cmd == "qsl") return qsl(core, db, req);
        if (cmd == "export") return export_msg(core, db, req);
        if (cmd == "retransmit") return retransmit(core, db, req);
        if (cmd == "tag") return tag(core, db, req);
        if (cmd == "correct") return correct(core, db, req);
        if (cmd == "search") return search(core, db, req);
        if (cmd == "activity_counters") return activity_counters(core, db);
        if (cmd == "purge") return purge(core, db, req);
        if (cmd == "reply_draft") return reply_draft(core, db, req);
        if (cmd == "report_export") return report_export(core, db, req);
        return report(core, db, req);
    } catch (const std::exception& e) {
        return err_reply(e.what());
    }
}

}  // namespace pna
