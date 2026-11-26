#include "station_view.h"

#include "channels.h"
#include "core.h"
#include "json.h"
#include "link_monitor.h"
#include "ops.h"

#include <sqlite3.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

namespace pna {
namespace sv {

namespace fs = std::filesystem;

namespace {

std::mutex g_mu;
Paths g_paths;

constexpr double kGapS = 1.2;          // lqa_db.py GAP_S
constexpr double kCloseS = 5.0;        // lqa_db.py CLOSE_S
constexpr double kMinUnix = 1.75e9;    // lqa_db.py MIN_UNIX: older stamps are replay tests
constexpr size_t kWordsBacklog = 512 * 1024;
constexpr double kWwvLiveS = 60;       // gale_term.py WWV_LOG_LIVE_S
constexpr double kClockMaxH = 8;       // gale_term.py --clock-max-h
constexpr double kGpsStaleS = 10;
constexpr double kHealthStaleS = 900;
constexpr double kEngineStaleS = 60;
constexpr double kScanStaleS = 120;

double now_d() { return static_cast<double>(std::time(nullptr)); }

std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") return name;
    return dir.back() == '/' ? dir + name : dir + "/" + name;
}

std::string error_reply(const std::string& why) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", why).end_object();
    return w.str();
}

// The last `backlog` bytes of a file, from the first whole line. False when
// it can't be read.
bool read_tail(const std::string& path, size_t backlog, std::string* out, double* mtime = nullptr,
               bool* cut = nullptr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < 0) return false;
    std::streamoff from = size > static_cast<std::streamoff>(backlog) ? size - static_cast<std::streamoff>(backlog) : 0;
    f.seekg(from);
    std::string s(static_cast<size_t>(size - from), '\0');
    f.read(&s[0], static_cast<std::streamsize>(s.size()));
    s.resize(static_cast<size_t>(f.gcount()));
    if (from > 0) {
        size_t nl = s.find('\n');
        s = nl == std::string::npos ? std::string() : s.substr(nl + 1);
    }
    if (cut) *cut = from > 0;
    if (mtime) {
        struct stat st {};
        *mtime = ::stat(path.c_str(), &st) == 0 ? static_cast<double>(st.st_mtime) : 0;
    }
    *out = std::move(s);
    return true;
}

bool read_small(const std::string& path, std::string* out, double* mtime, std::string* why) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        *why = "no file";
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        *why = "unreadable";
        return false;
    }
    std::string s(1 << 16, '\0');
    f.read(&s[0], static_cast<std::streamsize>(s.size()));
    s.resize(static_cast<size_t>(f.gcount()));
    struct stat st {};
    *mtime = ::stat(path.c_str(), &st) == 0 ? static_cast<double>(st.st_mtime) : 0;
    *out = std::move(s);
    return true;
}

// A small JSON file: the object, or false with why ("no file", "unreadable",
// "not JSON").
bool read_json_file(const std::string& path, JsonValue* v, double* mtime, std::string* why) {
    std::string s;
    if (!read_small(path, &s, mtime, why)) return false;
    if (!parse_json(s, *v) || !v->is_object()) {
        *why = "not JSON";
        return false;
    }
    return true;
}

const JsonValue* num(const JsonValue& o, const char* k) {
    const JsonValue* v = o.get(k);
    return v && v->type == JsonValue::Type::Number ? v : nullptr;
}
std::string str(const JsonValue& o, const char* k) {
    const JsonValue* v = o.get(k);
    return v && v->type == JsonValue::Type::String ? v->s : std::string();
}
bool is_true(const JsonValue& o, const char* k) {
    const JsonValue* v = o.get(k);
    return v && v->type == JsonValue::Type::Bool && v->b;
}
bool is_false(const JsonValue& o, const char* k) {
    const JsonValue* v = o.get(k);
    return v && v->type == JsonValue::Type::Bool && !v->b;
}

// Printable ASCII only, as gale_term.py's printable().
std::string printable(const std::string& s) {
    std::string o;
    for (unsigned char c : s) o += (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?';
    return o;
}

std::string num_text(double d) {
    if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) {
        char b[32];
        std::snprintf(b, sizeof b, "%.0f", d);
        return b;
    }
    char b[40];
    std::snprintf(b, sizeof b, "%.6g", d);
    return b;
}
long long rounded(double d) { return static_cast<long long>(std::llround(d)); }
std::string one_decimal(double d) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", d);
    return b;
}

bool contains_ci(const std::string& s, const char* what) {
    std::string l;
    for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return l.find(what) != std::string::npos;
}

// Text of a JsonValue, every frequency named or dropped (name_channels).
void emit(const JsonValue& v, const ChannelNames& names, std::string& o) {
    switch (v.type) {
        case JsonValue::Type::Null: o += "null"; return;
        case JsonValue::Type::Bool: o += v.b ? "true" : "false"; return;
        case JsonValue::Type::Number: o += num_text(v.n); return;
        case JsonValue::Type::String: o += "\"" + json_escape(v.s) + "\""; return;
        case JsonValue::Type::Array: {
            o += "[";
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i) o += ",";
                emit(v.arr[i], names, o);
            }
            o += "]";
            return;
        }
        case JsonValue::Type::Object: {
            o += "{";
            bool first = true;
            auto put = [&](const std::string& k) {
                if (!first) o += ",";
                first = false;
                o += "\"" + json_escape(k) + "\":";
            };
            for (auto& [k, x] : v.obj) {
                if (contains_ci(k, "khz")) {
                    std::string nk = k == "channels_khz" ? "channels" : k;
                    if (nk == k) {
                        size_t p = 0;
                        std::string l;
                        for (char c : k) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        p = l.find("khz");
                        nk = k.substr(0, p) + "label" + k.substr(p + 3);
                    }
                    if (v.obj.count(nk)) continue;  // the reply names it already
                    if (x.type == JsonValue::Type::Number) {
                        put(nk);
                        o += "\"" + json_escape(names.name(x.n)) + "\"";
                    } else if (x.is_array()) {
                        put(nk);
                        o += "[";
                        bool f2 = true;
                        for (auto& e : x.arr) {
                            if (!f2) o += ",";
                            f2 = false;
                            o += e.type == JsonValue::Type::Number ? "\"" + json_escape(names.name(e.n)) + "\""
                                                                   : std::string("null");
                        }
                        o += "]";
                    } else if (x.type == JsonValue::Type::Null) {
                        put(nk);
                        o += "null";
                    }
                    continue;  // anything else under a khz key: dropped
                }
                if (contains_ci(k, "hz") || contains_ci(k, "freq")) continue;
                put(k);
                emit(x, names, o);
            }
            o += "}";
            return;
        }
    }
}

bool valid_token(const std::string& s) {  // greybeard-ale control_socket.h
    if (s.empty() || s.size() > 64 || s[0] == '.') return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-' && c != '+') return false;
    return true;
}

// Plain decimal, as control_socket.h's parse_double takes it.
bool plain_decimal(const std::string& s, double* v) {
    if (s.empty() || s.size() > 16) return false;
    int dots = 0, digits = 0;
    for (char c : s) {
        if (c == '.') ++dots;
        else if (c >= '0' && c <= '9') ++digits;
        else return false;
    }
    if (dots > 1 || digits == 0 || s.front() == '.' || s.back() == '.') return false;
    *v = std::strtod(s.c_str(), nullptr);
    return std::isfinite(*v);
}

bool is_mountpoint(const std::string& p) {
    struct stat a {}, b {};
    if (::stat(p.c_str(), &a) != 0) return false;
    std::string parent = (fs::path(p) / "..").string();
    if (::stat(parent.c_str(), &b) != 0) return false;
    return a.st_dev != b.st_dev || a.st_ino == b.st_ino;
}

void write_store(JsonWriter& w, const Paths& p) {
    if (p.store_root.empty()) return;
    std::error_code ec;
    bool there = fs::is_directory(p.store_root, ec);
    bool mounted = there && is_mountpoint(p.store_root);
    w.begin_object("store").field("root", p.store_root).field("mounted", mounted);
    if (!mounted)
        w.field("why", there ? "the store's folder is there but no drive is mounted on it: what it shows is not the "
                               "store"
                             : "no store folder");
    w.end_object();
}

std::map<std::string, std::string> load_addresses(const std::string& path, bool* found) {
    std::map<std::string, std::string> out;
    std::ifstream f(path);
    *found = static_cast<bool>(f);
    std::string line;
    while (std::getline(f, line)) {
        size_t h = line.find('#');
        if (h != std::string::npos) line.resize(h);
        std::istringstream in(line);
        std::string a;
        if (!(in >> a)) continue;
        for (auto& c : a) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (a.size() > 15) continue;
        bool ok = true;
        for (char c : a)
            if (!std::isupper(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c)) && c != '@')
                ok = false;
        if (!ok) continue;
        std::string name;
        std::getline(in, name);
        size_t b = name.find_first_not_of(" \t"), e = name.find_last_not_of(" \t\r");
        out[a] = b == std::string::npos ? "" : printable(name.substr(b, e - b + 1));
    }
    return out;
}

struct WordLog {
    bool available = false;
    std::string why, file;
    bool cut = false;
    std::vector<Word> words;
};

WordLog read_word_log(const Paths& p) {
    WordLog L;
    L.file = newest_words_file(p.words);
    if (L.file.empty()) {
        L.why = "no word log at " + p.words;
        return L;
    }
    std::string text;
    if (!read_tail(L.file, kWordsBacklog, &text, nullptr, &L.cut)) {
        L.why = "can't read " + L.file;
        return L;
    }
    L.available = true;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        Word w;
        if (parse_word(line, &w)) L.words.push_back(w);
    }
    return L;
}

void write_tx(JsonWriter& w, const Transmission& t, const ChannelNames& names, double now) {
    w.begin_object()
        .field("start", rounded(t.start))
        .field("end", rounded(t.end))
        .field("channel", names.name(t.khz))
        .field("world", t.world.empty() ? "mars" : t.world)
        .field("caller", t.caller)
        .begin_array("called");
    for (auto& c : t.called) w.field("", c);
    w.end_array()
        .field("kind", t.kind)
        .field("words", t.words)
        .raw("sinad", one_decimal(t.words ? t.sinad_sum / t.words : 0))
        .field("gaps", t.gaps)
        .field("open", now - t.end <= kCloseS)
        .begin_array("amd");
    for (auto& m : t.amd) w.field("", printable(m));
    w.end_array().end_object();
}

// Each transmission's world: its words', else its channel's in the plan, else mars.
void set_worlds(std::vector<Transmission>& txs, const ChannelNames& names) {
    for (auto& t : txs)
        if (t.world.empty()) {
            t.world = names.world(t.khz);
            if (t.world.empty()) t.world = "mars";
        }
}

// A call to one of our own addresses in that world: MARS's own_address.txt for
// a MARS call, the amateur file for an amateur one (the worlds never mix).
std::optional<Transmission> find_incoming(const std::vector<Transmission>& txs,
                                          const std::map<std::string, std::string>& own, double now,
                                          double within,
                                          const std::map<std::string, std::string>& amateur_own = {}) {
    std::optional<Transmission> inc;
    if (own.empty() && amateur_own.empty()) return inc;
    for (auto& x : txs) {
        if (now - x.end > within || (x.kind != "call" && x.kind != "call-partial")) continue;
        const auto& mine = x.world == "amateur" ? amateur_own : own;
        bool to_us = false;
        for (auto& c : x.called)
            if (mine.count(c)) to_us = true;
        if (to_us && (!inc || x.end > inc->end)) inc = x;
    }
    return inc;
}

int int_arg(std::map<std::string, std::string>& req, const char* k, int def, int lo, int hi) {
    if (!req.count(k) || req[k].empty()) return def;
    try {
        int v = std::stoi(req[k]);
        return std::max(lo, std::min(hi, v));
    } catch (...) {
        return def;
    }
}

// ---- the calls --------------------------------------------------------------

std::string scanner_status(Core& core) {
    Paths p = paths();
    LinkMonitor* lm = core.link_monitor();
    JsonWriter w;
    w.begin_object().field("ok", true);
    if (!lm) {
        w.field("source", false)
            .field("view", "no scanner")
            .field("why", "no scanner socket is set (link-socket)")
            .field_null("scanner");
        write_store(w, p);
        w.end_object();
        return w.str();
    }
    LinkState s = lm->state();
    ChannelNames names(core);
    w.field("source", true)
        .field("reachable", s.reachable)
        .field("polled_at", s.polled_at)
        .field("last_ok_at", s.last_ok_at);
    if (!s.reachable) w.field("error", s.error);
    // Incoming: a call to our own address heard in the last minute, from the
    // word log (gale_term.py's Incoming).
    std::string view;
    {
        bool f = false;
        auto own = load_addresses(p.own, &f);
        bool af = false;
        auto aown = load_addresses(p.amateur_own, &af);
        // Both worlds' logs: a call to us in either is incoming.
        Paths pa = p;
        pa.words = p.amateur_words;
        WordLog L = read_word_log(p), LA = read_word_log(pa);
        std::vector<Transmission> txs;
        if (L.available) txs = group_words(L.words);
        if (LA.available)
            for (auto& t : group_words(LA.words)) {
                if (t.world.empty()) t.world = "amateur";
                txs.push_back(t);
            }
        if (!txs.empty()) {
            set_worlds(txs, names);
            auto inc = find_incoming(txs, own, now_d(), 60, aown);
            if (inc) {
                view = "incoming";
                w.begin_object("incoming")
                    .field("caller", inc->caller)
                    .field("channel", names.name(inc->khz))
                    .field("world", inc->world)
                    .field("end", rounded(inc->end))
                    .end_object();
            }
        }
    }
    if (s.raw.empty()) {
        w.field_null("scanner");
        if (view.empty()) view = "no scanner";
    } else {
        w.raw("scanner", name_channels(s.raw, names));
        if (!s.reachable) w.field("scanner_stale", true);
        if (view.empty()) {
            if (!s.reachable) view = "no scanner";
            else if (s.scanner == "held") view = "hold";
            else if (s.scanner == "scan") view = "scanning";
            else view = s.scanner;  // starting, recording, wwv, stopping
        }
    }
    w.field("view", view);
    write_store(w, p);
    w.end_object();
    return w.str();
}

std::string scanner_order(Core& core, const std::string& line) {
    LinkMonitor* lm = core.link_monitor();
    if (!lm) return error_reply("no scanner socket is set (link-socket): orders have nowhere to go");
    std::string path = lm->state().socket, reply, err;
    if (!unix_request(path, line, 3000, &reply, &err)) return error_reply("the scanner didn't answer: " + err);
    JsonValue v;
    if (!parse_json(reply, v) || !v.is_object()) return error_reply("the scanner's answer isn't JSON");
    ChannelNames names(core);
    lm->poll_once();  // so scanner_status shows the order pending at once
    std::string named = name_channels(reply, names);
    // The order as sent may hold a dial in kHz: the reply names only what the
    // scanner said, which holds none ("order" is its own words).
    return named;
}

// The last hour's words, counted by the engine that decoded them (the word's
// "engine" field): "station" is the terminal's own engine and "remote" the
// overflow engine, both primary decodes; "local" is ale-scan's in-process
// fallback (phoenix_nest_mars#3, ask 12). "primary" is station + remote, and
// words with no engine named (older logs) count in "words" only.
void write_hour_counts(JsonWriter& w, const WordLog& L, double now) {
    int n = 0, g = 0, st = 0, re = 0, lo = 0;
    for (auto& x : L.words)
        if (now - x.unix <= 3600) {
            ++n;
            g += x.gap;
            st += x.engine == "station";
            re += x.engine == "remote";
            lo += x.engine == "local";
        }
    w.field("words", n).field("gaps", g).field("local", lo).field("station", st).field("remote", re).field("primary",
                                                                                                         st + re);
}

std::string word_feed(Core& core, std::map<std::string, std::string>& req) {
    Paths p = paths();
    double now = now_d();
    int limit = int_arg(req, "limit", 50, 1, 500);
    double within = int_arg(req, "incoming_s", 60, 1, 3600);
    double since = 0;
    if (req.count("since")) plain_decimal(req["since"], &since);
    // world: which world's log (mars, the default, or amateur); they never mix.
    std::string world = req.count("world") && !req["world"].empty() ? req["world"] : "mars";
    if (world != "mars" && world != "amateur") return error_reply("world: mars or amateur");
    if (world == "amateur") p.words = p.amateur_words;
    ChannelNames names(core);
    WordLog L = read_word_log(p);
    JsonWriter w;
    w.begin_object().field("ok", true).field("world", world).field("available", L.available);
    if (!L.available) {
        w.field("why", L.why).begin_array("transmissions").end_array().field_null("incoming").field_null("last_word");
        write_store(w, p);
        w.end_object();
        return w.str();
    }
    auto txs = group_words(L.words);
    for (auto& t : txs)
        if (t.world.empty() && world == "amateur") t.world = "amateur";
    set_worlds(txs, names);
    bool f = false, af = false;
    auto own = load_addresses(p.own, &f);
    auto aown = load_addresses(p.amateur_own, &af);
    w.field("source", fs::path(L.file).filename().string())
        .field("own_address_file", f)
        .field("amateur_own_address_file", af);
    w.begin_array("transmissions");
    int n = 0;
    for (auto it = txs.rbegin(); it != txs.rend() && n < limit; ++it) {
        if (it->end < since) continue;
        write_tx(w, *it, names, now);
        ++n;
    }
    w.end_array();
    auto inc = find_incoming(txs, own, now, within, aown);
    if (inc) {
        w.begin_object("incoming")
            .field("caller", inc->caller)
            .field("channel", names.name(inc->khz))
            .field("world", inc->world)
            .field("end", rounded(inc->end));
        w.begin_array("called");
        for (auto& c : inc->called) w.field("", c);
        w.end_array().end_object();
    } else {
        w.field_null("incoming");
    }
    if (L.words.empty()) {
        w.field_null("last_word");
    } else {
        const Word& lw = L.words.back();
        w.begin_object("last_word")
            .field("unix", rounded(lw.unix))
            .field("channel", names.name(lw.khz))
            .field("world", !lw.world.empty() ? lw.world
                            : world == "amateur" ? "amateur"
                            : !names.world(lw.khz).empty() ? names.world(lw.khz) : "mars")
            .field("window", lw.window)
            .raw("sinad", one_decimal(lw.sinad))
            .field("engine", lw.engine)
            .end_object();
    }
    bool partial = L.cut && !L.words.empty() && now - L.words.front().unix < 3600;
    w.begin_object("last_hour");
    write_hour_counts(w, L, now);
    w.field("partial", partial).end_object();
    write_store(w, p);
    w.end_object();
    return w.str();
}

std::string heard_stations(Core& core, std::map<std::string, std::string>& req) {
    Paths p = paths();
    double now = now_d();
    int limit = int_arg(req, "limit", 200, 1, 1000);
    ChannelNames names(core);
    bool ffound = false;
    auto favs = load_addresses(p.favourites, &ffound);
    struct Row {
        std::string addr, channel;
        double last = 0, sinad = 0;
        long long n24 = 0, n = 0, channels = 0;
        bool has_sinad = false;
    };
    std::vector<Row> rows;
    std::string source, why;
    std::error_code ec;
    if (fs::exists(p.lqa_db, ec)) {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(p.lqa_db.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
            sqlite3_busy_timeout(db, 200);
            sqlite3_stmt* st = nullptr;
            const char* sql =
                "SELECT caller, MAX(end), khz, SUM(start > ?), COUNT(*), COUNT(DISTINCT khz), MAX(sinad_mean) "
                "FROM transmissions WHERE caller IS NOT NULL AND caller != '' AND start >= ? "
                "GROUP BY caller ORDER BY MAX(end) DESC LIMIT ?";
            if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
                sqlite3_bind_double(st, 1, now - 86400);
                sqlite3_bind_double(st, 2, kMinUnix);
                sqlite3_bind_int(st, 3, limit);
                int rc;
                while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
                    Row r;
                    const unsigned char* t = sqlite3_column_text(st, 0);
                    r.addr = t ? printable(reinterpret_cast<const char*>(t)) : "";
                    r.last = sqlite3_column_double(st, 1);
                    r.channel = sqlite3_column_type(st, 2) == SQLITE_NULL ? "" : names.name(sqlite3_column_double(st, 2));
                    r.n24 = sqlite3_column_int64(st, 3);
                    r.n = sqlite3_column_int64(st, 4);
                    r.channels = sqlite3_column_int64(st, 5);
                    r.has_sinad = sqlite3_column_type(st, 6) != SQLITE_NULL;
                    r.sinad = sqlite3_column_double(st, 6);
                    rows.push_back(r);
                }
                if (rc == SQLITE_DONE) source = "lqa";
                else why = std::string("the LQA database is unreadable: ") + sqlite3_errmsg(db);
                sqlite3_finalize(st);
            } else {
                why = std::string("the LQA database is unreadable: ") + sqlite3_errmsg(db);
            }
        } else {
            why = "the LQA database can't be opened";
        }
        sqlite3_close(db);
        if (source.empty()) rows.clear();
    }
    if (source.empty()) {
        // No database (or none readable): the word log, as gale_term.py's Stations.
        WordLog L = read_word_log(p);
        if (L.available) {
            source = "word log";
            std::map<std::string, Row> by;
            std::map<std::string, std::set<long long>> chans;
            for (auto& t : group_words(L.words)) {
                if (t.caller.empty() || t.start < kMinUnix) continue;
                Row& r = by[t.caller];
                r.addr = t.caller;
                if (t.end >= r.last) {
                    r.last = t.end;
                    r.channel = names.name(t.khz);
                }
                ++r.n;
                if (t.start > now - 86400) ++r.n24;
                double s = t.words ? t.sinad_sum / t.words : 0;
                if (!r.has_sinad || s > r.sinad) r.sinad = s;
                r.has_sinad = true;
                chans[t.caller].insert(std::llround(t.khz * 1000));
            }
            for (auto& [a, r] : by) {
                r.channels = static_cast<long long>(chans[a].size());
                rows.push_back(r);
            }
            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.last > b.last; });
            if (static_cast<int>(rows.size()) > limit) rows.resize(static_cast<size_t>(limit));
        } else if (why.empty()) {
            why = "no LQA database and " + L.why;
        }
    }
    JsonWriter w;
    w.begin_object().field("ok", true).field("available", !source.empty()).field("source", source);
    if (!why.empty()) w.field("why", why);
    w.field("favourites_file", ffound).begin_array("stations");
    std::set<std::string> heard;
    for (auto& r : rows) {
        heard.insert(r.addr);
        w.begin_object()
            .field("address", r.addr)
            .field("name", favs.count(r.addr) ? favs[r.addr] : "")
            .field("favourite", favs.count(r.addr) > 0)
            .field("last_heard", rounded(r.last))
            .field("last_channel", r.channel)
            .field("heard_24h", r.n24)
            .field("heard", r.n)
            .field("channels", r.channels);
        if (r.has_sinad) w.raw("best_sinad", one_decimal(r.sinad));
        else w.field_null("best_sinad");
        w.end_object();
    }
    for (auto& [a, name] : favs)
        if (!heard.count(a))
            w.begin_object()
                .field("address", a)
                .field("name", name)
                .field("favourite", true)
                .field_null("last_heard")
                .field("last_channel", "")
                .field("heard_24h", 0)
                .field("heard", 0)
                .field("channels", 0)
                .field_null("best_sinad")
                .end_object();
    w.end_array();
    write_store(w, p);
    w.end_object();
    return w.str();
}

std::string station_health(Core& core) {
    Paths p = paths();
    double now = now_d();
    JsonWriter w;
    w.begin_object().field("ok", true);
    // Clock: WWV first, then GPS (gale_term.py panel_clock, read_gps).
    JsonValue clock, gps;
    double cmt = 0, gmt = 0;
    std::string cwhy, gwhy;
    bool have_clock = read_json_file(join(p.wwv_dir, "clock.json"), &clock, &cmt, &cwhy);
    bool wwv_ok = have_clock && is_true(clock, "confirmed") && num(clock, "t_unix") && num(clock, "utc_minus_anchor_s");
    bool have_gps = read_json_file(p.gps, &gps, &gmt, &gwhy);
    double gage = 0;
    bool gfresh = false;
    if (have_gps) {
        const JsonValue* hn = num(gps, "host_ns");
        gage = now - (hn ? hn->n / 1e9 : gmt);
        gfresh = gage <= kGpsStaleS;
    }
    bool gps_time = have_gps && gfresh && !str(gps, "utc").empty();
    w.begin_object("clock").field("set_by", wwv_ok ? "wwv" : gps_time ? "gps" : "none");
    w.begin_object("wwv").field("fix", wwv_ok);
    if (wwv_ok) {
        double age = now - num(clock, "t_unix")->n;
        w.field("fixed_at", rounded(num(clock, "t_unix")->n))
            .field("age_s", rounded(age))
            .field("stale", age > kClockMaxH * 3600)
            .raw("offset_ms", one_decimal(num(clock, "utc_minus_anchor_s")->n * 1e3));
        if (auto x = num(clock, "sigma_ms")) w.raw("sigma_ms", one_decimal(x->n));
        if (auto x = num(clock, "count_ppm")) {
            char b[32];
            std::snprintf(b, sizeof b, "%.2f", x->n);
            w.raw("tcxo_ppm", b);
        }
    } else {
        w.field("why", have_clock ? "not confirmed" : cwhy);
    }
    // A block in progress: anchor.json newer than clock.json, wwv_live.log written lately.
    {
        struct stat a {}, l {};
        bool sync = ::stat(join(p.wwv_dir, "anchor.json").c_str(), &a) == 0 &&
                    ::stat(join(p.wwv_dir, "wwv_live.log").c_str(), &l) == 0 &&
                    (!have_clock || static_cast<double>(a.st_mtime) > cmt) &&
                    now - static_cast<double>(l.st_mtime) < kWwvLiveS;
        w.field("sync_running", sync);
        if (sync) w.field("sync_min", rounded((now - static_cast<double>(a.st_mtime)) / 60));
    }
    // The newest fix in clock_history.jsonl.
    {
        std::string text;
        if (read_tail(join(p.wwv_dir, "clock_history.jsonl"), 64 * 1024, &text)) {
            std::istringstream in(text);
            std::string line, last;
            int bad = 0;
            while (std::getline(in, line)) {
                JsonValue r;
                if (parse_json(line, r) && r.is_object() && num(r, "t_unix")) last = line;
                else if (!line.empty()) ++bad;
            }
            JsonValue r;
            if (!last.empty() && parse_json(last, r)) {
                w.begin_object("last_fix").field("t", rounded(num(r, "t_unix")->n));
                if (auto b = r.get("block"); b && b->type == JsonValue::Type::Number) w.field("block", rounded(b->n));
                if (auto e = num(r, "count_err_ms")) {
                    char b[32];
                    std::snprintf(b, sizeof b, "%.2f", e->n);
                    w.raw("count_err_ms", b);
                }
                if (auto h = num(r, "count_hours")) {
                    char b[32];
                    std::snprintf(b, sizeof b, "%.2f", h->n);
                    w.raw("count_hours", b);
                }
                w.end_object();
            } else {
                w.field_null("last_fix");
            }
            if (bad) w.field("history_bad_lines", bad);
        } else {
            w.field_null("last_fix");
        }
    }
    w.end_object();  // wwv
    w.begin_object("gps").field("available", have_gps);
    if (!have_gps) {
        w.field("why", gwhy);
    } else {
        w.field("fresh", gfresh).field("age_s", rounded(gage)).field("fix", is_true(gps, "fix"));
        if (auto s = gps.get("sats"); s && s->type == JsonValue::Type::Number) w.field("sats", rounded(s->n));
        else w.field_null("sats");
        w.field("source", printable(str(gps, "source")));
        if (auto x = num(gps, "host_minus_utc_ms")) {
            char b[32];
            std::snprintf(b, sizeof b, "%.3f", x->n);
            w.raw("host_minus_utc_ms", b);
        }
        if (auto x = num(gps, "uncertainty_ms")) {
            char b[32];
            std::snprintf(b, sizeof b, "%.3f", x->n);
            w.raw("uncertainty_ms", b);
        }
        if (auto x = num(gps, "pps_age_s")) w.raw("pps_age_s", one_decimal(x->n));
        const JsonValue* dev = gps.get("device");
        if (dev && dev->is_object() && is_false(*dev, "connected")) w.field("device_connected", false);
        else if (dev && dev->is_object() && is_true(*dev, "connected")) w.field("device_connected", true);
    }
    w.end_object().end_object();  // gps, clock
    // Health check (ale_health.py).
    {
        JsonValue h;
        double mt = 0;
        std::string why;
        w.begin_object("health");
        if (read_json_file(p.health, &h, &mt, &why)) {
            double age = now - (num(h, "t") ? num(h, "t")->n : mt);
            std::string state = str(h, "state");
            w.field("available", true)
                .field("state", state.empty() ? "?" : printable(state))
                .field("age_s", rounded(age))
                .field("stale", age > kHealthStaleS);
            w.begin_array("problems");
            if (const JsonValue* pr = h.get("problems"); pr && pr->is_array())
                for (auto& x : pr->arr)
                    if (x.type == JsonValue::Type::String) w.field("", printable(x.s));
            w.end_array();
        } else {
            w.field("available", false).field("why", why);
        }
        w.end_object();
    }
    // Engine (ale-engine --status).
    {
        JsonValue e;
        double mt = 0;
        std::string why;
        w.begin_object("engine");
        if (read_json_file(p.engine_status, &e, &mt, &why)) {
            double age = now - (num(e, "t") ? num(e, "t")->n : mt);
            w.field("available", true).field("age_s", rounded(age)).field("stale", age > kEngineStaleS);
            w.field("connected", is_true(e, "connected"));
            if (auto lf = num(e, "last_frame")) w.field("last_frame_age_s", rounded(now - lf->n));
            const JsonValue* fe = e.get("front_end");
            for (const char* k : {"overloads_total", "gap_visits", "local_visits"})
                if (fe && fe->is_object())
                    if (auto x = num(*fe, k)) w.field(k, rounded(x->n));
        } else {
            w.field("available", false).field("why", why);
        }
        w.end_object();
    }
    // Scanner log counts (gale_term.py ScanLog).
    {
        ScanLog sl = read_scan_log(p.scan_log);
        w.begin_object("scanner_log").field("available", sl.available);
        if (!sl.available) {
            w.field("why", sl.why);
        } else {
            double age = now - sl.mtime;
            std::string state = sl.stopped ? "stopped" : age > kScanStaleS ? "stale" : "running";
            w.field("state", state).field("age_s", rounded(age)).field("since", sl.since).field("stopped", sl.stopped);
            if (sl.link.empty()) w.field_null("link");
            else w.field("link", sl.link);
            if (sl.last_visit.empty()) w.field_null("last_visit");
            else w.field("last_visit", sl.last_visit);
            w.field("overloads", sl.overloads).field("restarts", sl.restarts);
        }
        w.end_object();
    }
    // Radio (the launcher's radio.json): why there's no radio, when there isn't.
    {
        JsonValue r;
        double mt = 0;
        std::string why;
        w.begin_object("radio");
        if (read_json_file(p.radio, &r, &mt, &why)) {
            w.field("available", true).field("radio", printable(str(r, "radio"))).field("why", printable(str(r, "why")));
            if (auto t = num(r, "t")) w.field("t", rounded(t->n));
        } else {
            w.field("available", false).field("why", why);
        }
        w.end_object();
    }
    // Words in the last hour, from the word log.
    {
        WordLog L = read_word_log(p);
        w.begin_object("words_last_hour").field("available", L.available);
        if (L.available) {
            write_hour_counts(w, L, now);
            w.field("partial", L.cut && !L.words.empty() && now - L.words.front().unix < 3600);
        } else {
            w.field("why", L.why);
        }
        w.end_object();
    }
    // The scanner's own word on its state, when there is a scanner socket.
    if (LinkMonitor* lm = core.link_monitor()) {
        LinkState s = lm->state();
        w.begin_object("scanner").field("reachable", s.reachable).field("state", s.scanner).field("engine", s.engine).field(
            "clock", s.clock);
        if (!s.reachable) w.field("why", s.error);
        w.end_object();
    } else {
        w.field_null("scanner");
    }
    w.begin_object("sources")
        .field("wwv_dir", p.wwv_dir)
        .field("gps", p.gps)
        .field("health", p.health)
        .field("engine_status", p.engine_status)
        .field("scan_log", p.scan_log)
        .field("words", p.words)
        .field("radio", p.radio)
        .end_object();
    write_store(w, p);
    w.end_object();
    return w.str();
}

std::string captures_list(Core& core, std::map<std::string, std::string>& req) {
    Paths p = paths();
    int limit = int_arg(req, "limit", 20, 1, 200);
    ChannelNames names(core);
    JsonWriter w;
    w.begin_object().field("ok", true);
    std::error_code ec;
    // The store's folder, then the fallback the engine writes to while the
    // store fails (phoenix_nest_mars#3, ask 11). A capture is listed once, by
    // name, from the store's folder when it is in both.
    const bool store_there = fs::is_directory(p.captures, ec);
    const bool fb_set = !p.captures_fallback.empty() && p.captures_fallback != p.captures;
    const bool fb_there = fb_set && fs::is_directory(p.captures_fallback, ec);
    if (!store_there && !fb_there) {
        w.field("available", false).field("why", "no capture folder at " + p.captures).begin_array("captures").end_array();
        write_store(w, p);
        w.end_object();
        return w.str();
    }
    std::map<std::string, std::string> wav_dir;           // capture file -> its folder
    std::map<std::string, std::set<std::string>> files;   // folder -> its files
    for (auto* d : {&p.captures, &p.captures_fallback}) {
        if (d == &p.captures ? !store_there : !fb_there) continue;
        auto& here = files[*d];
        for (auto& e : fs::directory_iterator(*d, ec)) {
            std::string n = e.path().filename().string();
            here.insert(n);
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".wav") == 0 && e.is_regular_file(ec)) wav_dir.emplace(n, *d);
        }
    }
    std::vector<std::string> wavs;
    for (auto& kv : wav_dir) wavs.push_back(kv.first);
    std::sort(wavs.rbegin(), wavs.rend());
    w.field("available", true).field("total", static_cast<long long>(wavs.size()));
    if (fb_set) w.field("store_folder", store_there).field("fallback_folder", fb_there);
    w.begin_array("captures");
    for (size_t i = 0; i < wavs.size() && static_cast<int>(i) < limit; ++i) {
        const std::string& n = wavs[i];
        const std::string& dir = wav_dir[n];
        const std::set<std::string>& here = files[dir];
        std::string base = n.substr(0, n.size() - 4);
        w.begin_object().field("name", base);
        if (fb_set) w.field("fallback", dir == p.captures_fallback);
        auto sz = fs::file_size(fs::path(dir) / n, ec);
        if (ec) w.field_null("size");
        else w.field("size", static_cast<long long>(sz));
        JsonValue d;
        double mt = 0;
        std::string why;
        if (read_json_file((fs::path(dir) / (base + ".json")).string(), &d, &mt, &why)) {
            w.field("open", false);
            if (auto t = num(d, "utc_start_unix")) w.field("start", rounded(t->n));
            if (auto s = num(d, "seconds")) w.raw("seconds", one_decimal(s->n));
            w.field("trigger", printable(str(d, "trigger")));
            std::string label = str(d, "label");
            if (label.empty())
                if (auto k = num(d, "khz")) label = names.name(k->n);
            w.field("channel", printable(label));
            if (auto x = d.get("window"); x && x->type == JsonValue::Type::Number) w.field("window", rounded(x->n));
            std::string ck = str(d, "clock");
            if (ck == "wwv" || ck == "host") w.field("clock", ck);
            if (auto x = num(d, "lost_s")) w.raw("lost_s", one_decimal(x->n));
        } else {
            w.field("open", true);  // no sidecar yet: still being written, or cut off
        }
        w.field("modems", here.count(base + ".modems.json") > 0);
        int payloads = 0;
        for (auto it = here.lower_bound(base + "."); it != here.end() && it->rfind(base + ".", 0) == 0; ++it)
            if (it->size() > 4 && it->compare(it->size() - 4, 4, ".bin") == 0) ++payloads;
        w.field("payloads", payloads).end_object();
    }
    w.end_array();
    write_store(w, p);
    w.end_object();
    return w.str();
}

}  // namespace

// ---- public -------------------------------------------------------------------

Paths Paths::resolved() const {
    Paths r = *this;
    std::string g = gale_dir.empty() ? "." : gale_dir;
    r.gale_dir = g;
    auto def = [&](std::string& v, const std::string& name) {
        if (v.empty()) v = join(g, name);
    };
    def(r.words, "words");
    def(r.scan_log, "ale-scan.log");
    def(r.wwv_dir, "wwv");
    def(r.gps, "gps/status.json");
    def(r.health, "health.json");
    def(r.engine_status, "engine_status.json");
    def(r.lqa_db, "lqa.sqlite");
    def(r.favourites, "favourites.txt");
    def(r.own, "own_address.txt");
    def(r.amateur_own, "amateur/own_address.txt");
    def(r.amateur_words, "amateur/words");
    def(r.scan_plan, "plans/scan.plan");
    def(r.radio, "radio.json");
    def(r.captures, "captures");
    return r;
}

void set_paths(const Paths& p) {
    std::lock_guard<std::mutex> g(g_mu);
    g_paths = p;
}

Paths paths() {
    std::lock_guard<std::mutex> g(g_mu);
    return g_paths.resolved();
}

bool parse_word(const std::string& line, Word* w) {
    JsonValue v;
    if (!parse_json(line, v) || !v.is_object()) return false;
    const JsonValue *u = num(v, "unix"), *k = num(v, "khz"), *s = num(v, "sinad");
    const JsonValue *t = v.get("type"), *c = v.get("chars"), *word = v.get("word");
    if (!u || !k || !s || !t || t->type != JsonValue::Type::String || !c || c->type != JsonValue::Type::String) return false;
    if (!word || word->type != JsonValue::Type::String || word->s.empty()) return false;
    std::string hx = word->s;
    if (hx.size() > 2 && hx[0] == '0' && (hx[1] == 'x' || hx[1] == 'X')) hx = hx.substr(2);
    if (hx.empty()) return false;
    for (char ch : hx)
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    w->unix = u->n;
    w->khz = k->n;
    w->sinad = s->n;
    w->type = t->s;
    w->chars = c->s;
    w->engine = str(v, "engine");
    w->world = str(v, "world");
    if (w->world != "mars" && w->world != "amateur") w->world.clear();
    w->gap = is_true(v, "gap");
    const JsonValue* win = num(v, "window");
    w->window = win ? static_cast<int>(win->n) : 0;
    return true;
}

namespace {

bool is_ascii64(const std::string& c) {
    for (unsigned char ch : c)
        if (ch < 0x20 || ch > 0x5F) return false;
    return true;
}

std::string clean_amd(const std::string& m) {
    std::string o;
    for (char c : m)
        if (c != '@') o += c;
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

// lqa_db.py AddressScan, MessageScan (AMD texts only) and classify().
void analyse(Transmission& t, const std::vector<Word>& ws) {
    static const std::set<std::string> addr_types = {"TO", "TIS", "TWAS", "FROM", "THRU"}, ext = {"DATA", "REP"};
    // Addresses.
    std::optional<std::pair<std::string, std::string>> cur, prev;
    auto close = [&](const std::pair<std::string, std::string>& g) {
        std::string a = g.second;
        while (!a.empty() && a.back() == '@') a.pop_back();
        a.erase(std::remove(a.begin(), a.end(), '@'), a.end());
        if (!a.empty() && (t.groups.empty() || t.groups.back() != std::make_pair(g.first, a))) t.groups.emplace_back(g.first, a);
    };
    for (auto& w : ws) {
        std::pair<std::string, std::string> tc{w.type, w.chars};
        if (ext.count(w.type) && prev == tc) continue;
        prev = tc;
        if (addr_types.count(w.type)) {
            if (cur) close(*cur);
            cur = tc;
        } else if (ext.count(w.type) && cur) {
            cur->second += w.chars;
        } else {
            if (cur) close(*cur);
            cur.reset();
        }
    }
    if (cur) close(*cur);
    // AMD texts.
    std::optional<std::string> m;
    std::set<std::string> seen;
    auto close_m = [&](const std::string& x) {
        std::string c = clean_amd(x);
        if (!c.empty() && seen.insert(c).second) t.amd.push_back(c);
    };
    for (auto& w : ws) {
        if (w.type == "CMD") {
            if (m) close_m(*m);
            m.reset();
            if (w.chars.size() == 3 && is_ascii64(w.chars)) m = w.chars;
        } else if (ext.count(w.type) && m) {
            *m += w.chars;
        } else {
            if (m) close_m(*m);
            m.reset();
        }
    }
    if (m) close_m(*m);
    // classify().
    for (auto it = t.groups.rbegin(); it != t.groups.rend(); ++it)
        if (it->first == "TIS" || it->first == "TWAS" || it->first == "FROM") {
            t.caller = it->second;
            break;
        }
    std::set<std::string> called;
    bool twas = false;
    for (auto& g : t.groups) {
        if (g.first == "TO") called.insert(g.second);
        if (g.first == "TWAS") twas = true;
    }
    for (auto& a : called) {
        bool prefix = false;
        for (auto& b : called)
            if (b != a && b.rfind(a, 0) == 0) prefix = true;
        if (!prefix) t.called.push_back(a);
    }
    if (!t.caller.empty() && t.called.empty()) t.kind = "sounding";
    else if (!t.called.empty() && !t.caller.empty()) t.kind = "call";
    else if (!t.called.empty()) t.kind = "call-partial";
    else t.kind = "unknown";
    if (twas && !t.called.empty()) t.kind = "call-end";
}

}  // namespace

std::vector<Transmission> group_words(const std::vector<Word>& words) {
    std::vector<Transmission> out;
    std::map<long long, std::vector<Word>> open;  // by channel (Hz)
    auto close = [&](long long k) {
        auto it = open.find(k);
        if (it == open.end() || it->second.empty()) return;
        auto& ws = it->second;
        Transmission t;
        t.khz = ws.front().khz;
        t.start = ws.front().unix;
        t.end = ws.back().unix;
        t.words = static_cast<int>(ws.size());
        for (auto& w : ws) {
            t.sinad_sum += w.sinad;
            t.gaps += w.gap;
            if (t.world.empty()) t.world = w.world;
        }
        analyse(t, ws);
        out.push_back(std::move(t));
        open.erase(it);
    };
    for (auto& w : words) {
        long long k = std::llround(w.khz * 1000);
        auto it = open.find(k);
        if (it != open.end() && !it->second.empty() && w.unix - it->second.back().unix > kGapS) close(k);
        open[k].push_back(w);
    }
    std::vector<long long> ks;
    for (auto& [k, v] : open) ks.push_back(k);
    for (auto k : ks) close(k);
    std::stable_sort(out.begin(), out.end(), [](const Transmission& a, const Transmission& b) { return a.start < b.start; });
    return out;
}

std::string newest_words_file(const std::string& prefix) {
    std::error_code ec;
    if (fs::is_regular_file(prefix, ec)) return prefix;
    fs::path pp(prefix);
    fs::path dir = pp.has_parent_path() ? pp.parent_path() : fs::path(".");
    std::string stem = pp.filename().string() + "-";
    std::string best;
    if (fs::is_directory(dir, ec))
        for (auto& e : fs::directory_iterator(dir, ec)) {
            std::string n = e.path().filename().string();
            if (n.rfind(stem, 0) == 0 && n.size() > stem.size() + 6 && n.compare(n.size() - 6, 6, ".jsonl") == 0 &&
                e.is_regular_file(ec) && n > best)
                best = n;
        }
    if (!best.empty()) return (dir / best).string();
    if (fs::is_regular_file(prefix + ".jsonl", ec)) return prefix + ".jsonl";
    return "";
}

ScanLog read_scan_log(const std::string& path, size_t backlog) {
    ScanLog s;
    std::string text;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        s.why = "no scanner log at " + path;
        return s;
    }
    if (!read_tail(path, backlog, &text, &s.mtime)) {
        s.why = "can't read " + path;
        return s;
    }
    s.available = true;
    std::istringstream in(text);
    std::string line;
    const std::string say = "[ale_scan_gangleri] ";
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(say, 0) == 0 && line.size() > say.size() + 21 && line[say.size() + 19] == 'Z' &&
            line[say.size() + 20] == ' ') {
            std::tm tm{};
            if (strptime(line.substr(say.size(), 20).c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm)) {
                long long t = static_cast<long long>(timegm(&tm));
                std::string msg = line.substr(say.size() + 21);
                if (msg.rfind("starting ale-scan", 0) == 0 || msg.rfind("schedule mode", 0) == 0) {
                    s.since = t;
                    s.stopped = 0;
                    s.overloads = s.restarts = 0;
                    s.link.clear();
                    s.last_visit.clear();
                } else if (msg.rfind("stopped", 0) == 0) {
                    s.stopped = t;
                }
                continue;
            }
        }
        if (line.find("ale-scan: decoding on engine") != std::string::npos) s.link = "up";
        else if (line.find("; decoding here") != std::string::npos ||
                 line.find("WWV block: engine link lost") != std::string::npos)
            s.link = "down";
        else if (line.find("ADC overload DETECTED") != std::string::npos) ++s.overloads;
        else if (line.find("rsp2 timeline restart") != std::string::npos) ++s.restarts;
        else {
            // ", (engine|local), peak -?[0-9.]+ dBFS" at the end of the line.
            const std::string tail = " dBFS";
            if (line.size() > tail.size() && line.compare(line.size() - tail.size(), tail.size(), tail) == 0) {
                for (const char* kind : {"engine", "local"}) {
                    std::string pre = std::string(", ") + kind + ", peak ";
                    size_t at = line.rfind(pre);
                    if (at == std::string::npos) continue;
                    std::string v = line.substr(at + pre.size(), line.size() - tail.size() - at - pre.size());
                    if (!v.empty() && v[0] == '-') v = v.substr(1);
                    bool ok = !v.empty() && v.find_first_not_of("0123456789.") == std::string::npos;
                    if (ok) s.last_visit = kind;
                }
            }
        }
    }
    return s;
}

ChannelNames::ChannelNames(Core& core) : core_(core) {
    Paths p = paths();
    std::ifstream f(p.scan_plan);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream s(line);
        std::string key;
        s >> key;
        if (key == "record" || key == "voice") {
            std::string tok;
            while (s >> tok) {
                size_t c = tok.find(':');
                if (c == std::string::npos || c + 1 >= tok.size()) continue;
                int64_t hz = std::atoll(tok.substr(0, c).c_str());
                if (hz > 0) labels_[hz] = printable(tok.substr(c + 1));
            }
        } else if (key == "chan") {
            // chan DIAL_HZ LABEL WORLD [plus] [auto] (greybeard-ale docs/amateur-world.md)
            double hz = 0;
            std::string label, world;
            s >> hz >> label >> world;
            int64_t h = std::llround(hz);
            if (h <= 0) continue;
            if (!label.empty() && label != "-" && !labels_.count(h)) labels_[h] = printable(label);
            if (world == "mars" || world == "amateur") worlds_[h] = world;
        } else if (key == "window") {
            double center = 0, d;
            s >> center;
            std::vector<int64_t> dials;
            while (s >> d) dials.push_back(std::llround(d));
            windows_.push_back(dials);
        }
    }
}

std::string ChannelNames::name(double khz) const { return name_hz(std::llround(khz * 1000)); }

std::string ChannelNames::name_hz(int64_t hz) const {
    if (Ops* ops = core_.ops()) {
        std::string d = ops->channels().designator_for(hz);
        if (!d.empty()) return d;
    }
    for (auto& [h, l] : labels_)
        if (std::llabs(h - hz) <= 1) return l;
    for (size_t w = 0; w < windows_.size(); ++w)
        for (size_t c = 0; c < windows_[w].size(); ++c)
            if (std::llabs(windows_[w][c] - hz) <= 1) return "W" + std::to_string(w + 1) + "C" + std::to_string(c + 1);
    return "unlisted";
}

std::string ChannelNames::world(double khz) const {
    int64_t hz = std::llround(khz * 1000);
    for (auto& [h, w] : worlds_)
        if (std::llabs(h - hz) <= 1) return w;
    return "";
}

namespace {

// The channels of a status reply, the same from either scanner (ask 38):
// "channels" the names, "channel_labels" the labels, "channel_count" how many.
// An older scanner's channels_khz goes; a newer one's channel_labels stays.
void same_channels(JsonValue& v, const ChannelNames& names) {
    if (!v.is_object()) return;
    auto lab = v.obj.find("channel_labels");
    auto khz = v.obj.find("channels_khz");
    JsonValue out, labels;
    out.type = labels.type = JsonValue::Type::Array;
    auto text = [](const std::string& s) {
        JsonValue x;
        x.type = JsonValue::Type::String;
        x.s = s;
        return x;
    };
    if (lab != v.obj.end() && lab->second.is_array()) {
        const JsonValue* win = v.get("window");
        long long w = win && win->type == JsonValue::Type::Number ? std::llround(win->n) : 0;
        for (size_t i = 0; i < lab->second.arr.size(); ++i) {
            const JsonValue& l = lab->second.arr[i];
            if (l.type == JsonValue::Type::String && !l.s.empty()) {
                out.arr.push_back(text(printable(l.s)));
                labels.arr.push_back(text(printable(l.s)));
            } else {
                out.arr.push_back(text("W" + std::to_string(w) + "C" + std::to_string(i + 1)));
                labels.arr.push_back(JsonValue{});
            }
        }
    } else if (khz != v.obj.end() && khz->second.is_array()) {
        for (auto& e : khz->second.arr) {
            JsonValue x = e.type == JsonValue::Type::Number ? text(names.name(e.n)) : JsonValue{};
            out.arr.push_back(x);
            labels.arr.push_back(x);
        }
    } else {
        return;
    }
    JsonValue count;
    count.type = JsonValue::Type::Number;
    count.n = static_cast<double>(out.arr.size());
    v.obj.erase("channels_khz");
    v.obj["channels"] = out;
    v.obj["channel_labels"] = labels;
    v.obj["channel_count"] = count;
}

}  // namespace

std::string name_channels(const std::string& reply_json, const ChannelNames& names) {
    JsonValue v;
    if (!parse_json(reply_json, v)) return "null";
    same_channels(v, names);
    std::string o;
    emit(v, names, o);
    return o;
}

namespace {

// ---- the amateur world's identity (ask 33) ----------------------------------
// greybeard-ale docs/amateur-world.md: one address per line, 1-15 of A-Z 0-9 @,
// written whole (temp file, rename), file 600 and folder 700; then the
// scanner's control socket is told "reload-identity". No address: the scanner
// listens only. It is never MARS's own_address.txt.

bool same_file(const std::string& a, const std::string& b) {
    std::error_code e1, e2;
    fs::path x = fs::weakly_canonical(a, e1), y = fs::weakly_canonical(b, e2);
    return !e1 && !e2 && x == y;
}

std::string identity_get(Core&) {
    Paths p = paths();
    bool found = false;
    auto own = load_addresses(p.amateur_own, &found);
    JsonWriter w;
    w.begin_object().field("ok", true).field("world", "amateur").field("file_found", found).begin_array("addresses");
    for (auto& [a, n] : own) w.field("", a);
    w.end_array().field("listen_only", own.empty()).end_object();
    return w.str();
}

std::string identity_set(Core& core, std::map<std::string, std::string>& req) {
    if (!req.count("addresses")) return error_reply("addresses: the amateur calls this station answers to, "
                                                    "separated by spaces or commas (empty: listen only)");
    std::vector<std::string> got;
    std::set<std::string> seen;
    std::string text = req["addresses"], tok;
    auto take = [&]() -> std::string {
        if (tok.empty()) return "";
        std::string a = tok;
        tok.clear();
        for (auto& c : a) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        bool ok = a.size() <= 15;
        for (char c : a)
            if (!std::isupper(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c)) && c != '@')
                ok = false;
        if (!ok) return "\"" + printable(a) + "\" isn't an address: 1 to 15 of A-Z, 0-9 and @";
        if (seen.insert(a).second) got.push_back(a);
        return "";
    };
    for (char c : text) {
        if (c == ' ' || c == ',' || c == '\n' || c == '\r' || c == '\t' || c == ';') {
            std::string e = take();
            if (!e.empty()) return error_reply(e);
        } else {
            tok += c;
        }
    }
    if (std::string e = take(); !e.empty()) return error_reply(e);
    if (got.size() > 32) return error_reply("at most 32 addresses");
    Paths p = paths();
    if (same_file(p.amateur_own, p.own))
        return error_reply("the amateur address file is set to MARS's own-address file: the worlds keep separate "
                           "files (amateur-own-address)");
    fs::path file(p.amateur_own), dir = file.parent_path();
    std::error_code ec;
    if (!dir.empty()) {
        fs::create_directories(dir, ec);
        if (ec) return error_reply("can't make " + dir.string() + ": " + ec.message());
        ::chmod(dir.c_str(), 0700);
    }
    std::string tmp = file.string() + ".new";
    {
        int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) return error_reply("can't write " + tmp);
        std::string body = "# The amateur world's own addresses, one per line (written by the station's settings).\n";
        for (auto& a : got) body += a + "\n";
        bool ok = ::write(fd, body.data(), body.size()) == static_cast<ssize_t>(body.size());
        ok = ::fsync(fd) == 0 && ok;
        ::close(fd);
        ::chmod(tmp.c_str(), 0600);
        if (!ok) {
            fs::remove(tmp, ec);
            return error_reply("can't write " + tmp);
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) return error_reply("can't write " + file.string() + ": " + ec.message());
    JsonWriter w;
    w.begin_object().field("ok", true).field("world", "amateur").begin_array("addresses");
    for (auto& a : got) w.field("", a);
    w.end_array().field("listen_only", got.empty());
    // The scanner reads it again now, or at its next start.
    LinkMonitor* lm = core.link_monitor();
    std::string reply, err;
    JsonValue v;
    if (lm && unix_request(lm->state().socket, "reload-identity", 3000, &reply, &err) && parse_json(reply, v) &&
        v.is_object()) {
        const JsonValue* ok = v.get("ok");
        bool took = ok && ok->type == JsonValue::Type::Bool && ok->b;
        w.field("scanner_reloaded", took);
        if (took) {
            w.begin_array("scanner_own");
            if (const JsonValue* o = v.get("own"); o && o->is_array())
                for (auto& e : o->arr)
                    if (e.type == JsonValue::Type::String) w.field("", printable(e.s));
            w.end_array();
            const JsonValue* oe = v.get("own_error");
            if (oe && oe->type == JsonValue::Type::String) w.field("scanner_own_error", printable(oe->s));
            else w.field_null("scanner_own_error");
        } else {
            w.field("note", "saved; the scanner refused the reload (" +
                                printable(str(v, "error")) + "), so it reads the file at its next start");
        }
        lm->poll_once();
    } else {
        w.field("scanner_reloaded", false)
            .field("note", lm ? "saved; the scanner didn't answer, so it reads the file at its next start"
                              : "saved; no scanner socket is set, so the scanner reads the file at its next start");
    }
    w.end_object();
    return w.str();
}

}  // namespace

std::optional<std::string> handle(Core& core, const std::string& cmd, std::map<std::string, std::string>& req) {
    if (cmd == "scanner_status") return scanner_status(core);
    if (cmd == "identity_get") return identity_get(core);
    if (cmd == "identity_set") return identity_set(core, req);
    if (cmd == "scanner_release") return scanner_order(core, "release");
    if (cmd == "scanner_hold") {
        double win = 0;
        if (!plain_decimal(req["window"], &win) || win < 1 || win != std::floor(win) || win > 999999)
            return error_reply("window: the plan window to hold, a whole number from 1 (scanner_status windows)");
        std::string line = "hold " + num_text(win);
        if (req.count("seconds") && !req["seconds"].empty()) {
            double s = 0;
            if (!plain_decimal(req["seconds"], &s) || s <= 0 || s > 900)
                return error_reply("seconds: above 0 and at most 900 (every hold ends; without it, 600)");
            line += " " + req["seconds"];
        }
        return scanner_order(core, line);
    }
    if (cmd == "scanner_record") {
        std::string ch = req["channel"];
        double s = 0;
        if (!plain_decimal(req["seconds"], &s) || s <= 0 || s > 900)
            return error_reply("seconds: above 0 and at most 900");
        if (ch.empty()) return error_reply("channel: the channel to record, by its name");
        // The scanner takes its own label, or a dial in kHz. A designator in
        // the station's channel plan, or a "W<w>C<c>" place, is looked up here
        // and goes to the local socket as kHz only; any other name goes to the
        // scanner as its own label.
        Paths p = paths();
        std::string target;
        // A designator in the station's channel plan, or a "W<w>C<c>" place:
        // the kHz go to the local socket only.
        std::optional<int64_t> want;
        if (Ops* ops = core.ops())
            if (auto c = ops->channels().get(ch)) want = c->hz;
        if (!want && ch.size() >= 4 && (ch[0] == 'W' || ch[0] == 'w')) {
            size_t cpos = ch.find_first_of("Cc");
            try {
                if (cpos != std::string::npos && cpos > 1) {
                    int w = std::stoi(ch.substr(1, cpos - 1)), c = std::stoi(ch.substr(cpos + 1));
                    std::ifstream f(p.scan_plan);
                    std::string line;
                    int wi = 0;
                    while (std::getline(f, line)) {
                        std::istringstream ls(line);
                        std::string key;
                        ls >> key;
                        if (key != "window") continue;
                        ++wi;
                        if (wi != w) continue;
                        double center, d;
                        ls >> center;
                        int ci = 0;
                        while (ls >> d)
                            if (++ci == c) want = std::llround(d);
                    }
                }
            } catch (...) {
            }
        }
        if (want) {
            char b[32];
            std::snprintf(b, sizeof b, "%.3f", static_cast<double>(*want) / 1000.0);
            target = b;
        } else if (valid_token(ch)) {
            target = ch;
        }
        if (target.empty()) return error_reply("channel: no channel named " + printable(ch));
        return scanner_order(core, "record " + target + " " + req["seconds"]);
    }
    if (cmd == "scanner_plan") {
        if (!valid_token(req["name"]))
            return error_reply("name: a plan file name in the scanner's plan folder ([A-Za-z0-9._+-], no path)");
        return scanner_order(core, "plan " + req["name"]);
    }
    if (cmd == "word_feed") return word_feed(core, req);
    if (cmd == "station_health") return station_health(core);
    if (cmd == "captures_list") return captures_list(core, req);
    if (cmd == "heard_stations") return heard_stations(core, req);
    return std::nullopt;
}

}  // namespace sv
}  // namespace pna
