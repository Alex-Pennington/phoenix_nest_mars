#include "store.h"

#include "sqlite_stmt.h"

#include <sqlite3.h>

#include <chrono>
#include <stdexcept>

namespace pna {

int64_t now_utc() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

namespace {



constexpr const char* kCols =
    "id, direction, src, dst, prec, body, state, attempts, created_at, updated_at, next_try_at, last_error";

Message row(Stmt& s) {
    Message m;
    m.id = s.text(0);
    m.direction = s.text(1);
    m.src = s.text(2);
    m.dst = s.text(3);
    std::string p = s.text(4);
    m.prec = p.empty() ? 'R' : p[0];
    m.body = s.blob(5);
    m.state = s.text(6);
    m.attempts = static_cast<int>(s.i64(7));
    m.created_at = s.i64(8);
    m.updated_at = s.i64(9);
    m.next_try_at = s.i64(10);
    m.last_error = s.text(11);
    return m;
}

}  // namespace

Store::Store(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        throw std::runtime_error("cannot open store " + path + ": " + err);
    }
    sqlite3_busy_timeout(db_, 2000);
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=FULL");
    exec(
        "CREATE TABLE IF NOT EXISTS messages ("
        " id TEXT PRIMARY KEY, direction TEXT NOT NULL, src TEXT NOT NULL, dst TEXT NOT NULL,"
        " prec TEXT NOT NULL, body BLOB NOT NULL, state TEXT NOT NULL, attempts INTEGER DEFAULT 0,"
        " created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, next_try_at INTEGER DEFAULT 0,"
        " last_error TEXT DEFAULT '')");
    exec(
        "CREATE TABLE IF NOT EXISTS events ("
        " ts INTEGER NOT NULL, msg_id TEXT, text TEXT NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value INTEGER NOT NULL)");
    exec("CREATE TABLE IF NOT EXISTS clear (id TEXT PRIMARY KEY, text BLOB NOT NULL, key TEXT NOT NULL)");
    exec(
        "CREATE TABLE IF NOT EXISTS cards (station TEXT PRIMARY KEY, card TEXT NOT NULL, fingerprint TEXT NOT NULL,"
        " verified INTEGER NOT NULL DEFAULT 0, added INTEGER NOT NULL, source TEXT NOT NULL DEFAULT '')");
    exec("CREATE INDEX IF NOT EXISTS messages_queue ON messages(direction, state, prec, created_at)");
}

Store::~Store() { sqlite3_close(db_); }

void Store::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string e = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("sqlite: " + e);
    }
}

int64_t Store::next_seq() {
    exec("INSERT OR IGNORE INTO meta(key, value) VALUES('seq', 0)");
    exec("UPDATE meta SET value = value + 1 WHERE key = 'seq'");
    Stmt s(db_, "SELECT value FROM meta WHERE key = 'seq'");
    s.step();
    return s.i64(0);
}

int64_t Store::next_counter(const std::string& key, int64_t wrap) {
    std::string k = "counter:" + key;
    Stmt(db_, "INSERT OR IGNORE INTO meta(key, value) VALUES(?, 0)").bind(1, k).step();
    Stmt(db_, "UPDATE meta SET value = CASE WHEN value >= ? THEN 1 ELSE value + 1 END WHERE key = ?")
        .bind(1, wrap)
        .bind(2, k)
        .step();
    Stmt s(db_, "SELECT value FROM meta WHERE key = ?");
    s.bind(1, k);
    s.step();
    return s.i64(0);
}

void Store::insert(const Message& m) {
    Stmt s(db_,
           "INSERT INTO messages(id, direction, src, dst, prec, body, state, attempts, created_at,"
           " updated_at, next_try_at, last_error) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
    s.bind(1, m.id).bind(2, m.direction).bind(3, m.src).bind(4, m.dst).bind(5, std::string(1, m.prec));
    s.bind_blob(6, m.body).bind(7, m.state).bind(8, static_cast<int64_t>(m.attempts));
    s.bind(9, m.created_at).bind(10, m.updated_at).bind(11, m.next_try_at).bind(12, m.last_error);
    s.step();
}

bool Store::exists(const std::string& id) {
    Stmt s(db_, "SELECT 1 FROM messages WHERE id = ?");
    s.bind(1, id);
    return s.step();
}

std::optional<Message> Store::get(const std::string& id) {
    Stmt s(db_, (std::string("SELECT ") + kCols + " FROM messages WHERE id = ?").c_str());
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return row(s);
}

std::vector<Message> Store::list(int limit) {
    Stmt s(db_, (std::string("SELECT ") + kCols +
                 " FROM messages ORDER BY updated_at DESC, rowid DESC LIMIT ?")
                    .c_str());
    s.bind(1, static_cast<int64_t>(limit));
    std::vector<Message> out;
    while (s.step()) out.push_back(row(s));
    return out;
}

std::vector<Message> Store::queue(int limit) {
    Stmt s(db_, (std::string("SELECT ") + kCols +
                 " FROM messages WHERE direction = 'out' AND state IN ('queued', 'sending')"
                 " ORDER BY state = 'sending' DESC,"
                 " CASE prec WHEN 'Z' THEN 0 WHEN 'O' THEN 1 WHEN 'P' THEN 2 ELSE 3 END, created_at, rowid LIMIT ?")
                    .c_str());
    s.bind(1, static_cast<int64_t>(limit));
    std::vector<Message> out;
    while (s.step()) out.push_back(row(s));
    return out;
}

std::vector<Event> Store::events(int64_t after, int limit) {
    Stmt s(db_, "SELECT rowid, ts, COALESCE(msg_id, ''), text FROM events WHERE rowid > ? ORDER BY rowid LIMIT ?");
    s.bind(1, after).bind(2, static_cast<int64_t>(limit));
    std::vector<Event> out;
    while (s.step()) out.push_back(Event{s.i64(0), s.i64(1), s.text(2), s.text(3)});
    return out;
}

std::optional<Message> Store::next_to_send(int64_t now) {
    Stmt s(db_, (std::string("SELECT ") + kCols +
                 " FROM messages WHERE direction = 'out' AND state = 'queued' AND next_try_at <= ?"
                 " ORDER BY CASE prec WHEN 'Z' THEN 0 WHEN 'O' THEN 1 WHEN 'P' THEN 2 ELSE 3 END,"
                 " created_at, rowid LIMIT 1")
                    .c_str());
    s.bind(1, now);
    if (!s.step()) return std::nullopt;
    return row(s);
}

std::optional<Message> Store::next_to_send(int64_t now, const std::function<bool(const std::string&)>& may) {
    Stmt s(db_, (std::string("SELECT ") + kCols +
                 " FROM messages WHERE direction = 'out' AND state = 'queued' AND next_try_at <= ?"
                 " ORDER BY CASE prec WHEN 'Z' THEN 0 WHEN 'O' THEN 1 WHEN 'P' THEN 2 ELSE 3 END,"
                 " created_at, rowid")
                    .c_str());
    s.bind(1, now);
    while (s.step()) {
        Message m = row(s);
        if (may(m.dst)) return m;
    }
    return std::nullopt;
}

void Store::set_state(const std::string& id, const std::string& state, const std::string& error) {
    Stmt s(db_, "UPDATE messages SET state = ?, last_error = ?, updated_at = ? WHERE id = ?");
    s.bind(1, state).bind(2, error).bind(3, now_utc()).bind(4, id);
    s.step();
}

void Store::mark_sending(const std::string& id) {
    Stmt s(db_,
           "UPDATE messages SET state = 'sending', attempts = attempts + 1, updated_at = ? WHERE id = ?");
    s.bind(1, now_utc()).bind(2, id);
    s.step();
}

void Store::schedule_retry(const std::string& id, int64_t at, const std::string& error) {
    Stmt s(db_,
           "UPDATE messages SET state = 'queued', next_try_at = ?, last_error = ?, updated_at = ?"
           " WHERE id = ?");
    s.bind(1, at).bind(2, error).bind(3, now_utc()).bind(4, id);
    s.step();
}

int Store::requeue_interrupted() {
    exec("UPDATE messages SET state = 'queued' WHERE direction = 'out' AND state = 'sending'");
    return sqlite3_changes(db_);
}

QueueCounts Store::counts() {
    QueueCounts c;
    Stmt s(db_, "SELECT state, COUNT(*) FROM messages GROUP BY state");
    while (s.step()) {
        std::string st = s.text(0);
        int n = static_cast<int>(s.i64(1));
        if (st == "queued") c.queued = n;
        else if (st == "sending") c.sending = n;
        else if (st == "sent") c.sent = n;
        else if (st == "acked") c.acked = n;
        else if (st == "failed") c.failed = n;
        else if (st == "received") c.received = n;
    }
    return c;
}

void Store::log_event(const std::string& msg_id, const std::string& text) {
    Stmt s(db_, "INSERT INTO events(ts, msg_id, text) VALUES(?,?,?)");
    s.bind(1, now_utc()).bind(2, msg_id).bind(3, text);
    s.step();
}

void Store::set_clear(const std::string& id, const std::string& text, const std::string& key) {
    Stmt s(db_, "INSERT OR REPLACE INTO clear(id, text, key) VALUES(?,?,?)");
    s.bind(1, id).bind_blob(2, text).bind(3, key);
    s.step();
}

std::optional<std::pair<std::string, std::string>> Store::clear(const std::string& id) {
    Stmt s(db_, "SELECT text, key FROM clear WHERE id = ?");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return std::make_pair(s.blob(0), s.text(1));
}

void Store::put_card(const CardRow& c) {
    Stmt s(db_, "INSERT OR REPLACE INTO cards(station, card, fingerprint, verified, added, source) VALUES(?,?,?,?,?,?)");
    s.bind(1, c.station).bind(2, c.card).bind(3, c.fingerprint).bind(4, static_cast<int64_t>(c.verified ? 1 : 0));
    s.bind(5, c.added).bind(6, c.source);
    s.step();
}

namespace {
Store::CardRow card_row(Stmt& s) {
    Store::CardRow c;
    c.station = s.text(0);
    c.card = s.text(1);
    c.fingerprint = s.text(2);
    c.verified = s.i64(3) != 0;
    c.added = s.i64(4);
    c.source = s.text(5);
    return c;
}
}  // namespace

std::optional<Store::CardRow> Store::card(const std::string& station) {
    Stmt s(db_, "SELECT station, card, fingerprint, verified, added, source FROM cards WHERE station = ?");
    s.bind(1, station);
    if (!s.step()) return std::nullopt;
    return card_row(s);
}

std::vector<Store::CardRow> Store::cards() {
    Stmt s(db_, "SELECT station, card, fingerprint, verified, added, source FROM cards ORDER BY station");
    std::vector<CardRow> out;
    while (s.step()) out.push_back(card_row(s));
    return out;
}

void Store::set_card_verified(const std::string& station, bool v) {
    Stmt s(db_, "UPDATE cards SET verified = ? WHERE station = ?");
    s.bind(1, static_cast<int64_t>(v ? 1 : 0)).bind(2, station);
    s.step();
}

}  // namespace pna
