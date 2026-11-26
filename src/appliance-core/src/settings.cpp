// The station's settings: see settings.h.
#include "settings.h"

#include "records.h"

#include "config.h"
#include "first_start.h"
#include "frame.h"
#include "json.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace pna {
namespace settings {

namespace {

Row op(std::string key, std::string group, std::string label, std::string type, std::string def, std::string help) {
    Row r;
    r.key = std::move(key);
    r.group = std::move(group);
    r.label = std::move(label);
    r.type = std::move(type);
    r.def = std::move(def);
    r.help = std::move(help);
    return r;
}
Row num(std::string key, std::string group, std::string label, long long lo, long long hi, std::string unit,
        std::string def, std::string help) {
    Row r = op(std::move(key), std::move(group), std::move(label), "int", std::move(def), std::move(help));
    r.lo = lo;
    r.hi = hi;
    r.unit = std::move(unit);
    return r;
}
Row pick(std::string key, std::string group, std::string label, std::vector<std::string> choices, std::string def,
         std::string help) {
    Row r = op(std::move(key), std::move(group), std::move(label), "choice", std::move(def), std::move(help));
    r.choices = std::move(choices);
    return r;
}
Row locked(std::string key, std::string group, std::string label, std::string type = "text") {
    Row r = op(std::move(key), std::move(group), std::move(label), std::move(type), "", "");
    r.cls = "locked";
    r.help = "Host plumbing: set in appliance.conf, not from the kiosk.";
    return r;
}

std::vector<Row> make_table() {
    std::vector<Row> t;
    // ---- Station ----
    {
        Row r = op("station", "Station", "Station call", "call", "",
                   "This station's call: letters A-Z, digits and -, at most 16.");
        r.gate = "confirm";
        r.warning = "A new call starts its own message store at the next start; the old one is kept. "
                    "New station keys are made for it: read the new fingerprint to your keymaster by voice.";
        t.push_back(r);
    }
    t.push_back(op("tx-mode", "Station", "Modem mode", "text", "",
                   "The gale-tx mode the station sends in, like 600S."));
    {
        Row r = op("amateur-calls", "Station", "Amateur calls", "action", "",
                   "The calls this station answers to in the amateur world, kept apart from MARS's.");
        r.call = "identity_set";
        t.push_back(r);
    }
    {
        Row r = op("cipher", "Station", "Cipher", "action", "", "Install or remove the station's cipher module.");
        r.call = "cipher_install, cipher_remove";
        t.push_back(r);
    }
    // ---- Worlds (design 7.4) ----
    {
        Row r = pick("world", "Worlds", "Worlds this station works", {"mars", "amateur", "both"}, "",
                     "MARS, amateur, or both. The two never share traffic: nothing is ever forwarded or copied "
                     "between them.");
        r.gate = "confirm";
        r.applies = "now";
        r.warning = "The scanner restarts on the new plan: about 30 seconds with no scanning.";
        t.push_back(r);
    }
    {
        Row r = pick("tx-world", "Worlds", "Transmit in", {"mars", "amateur"}, "",
                     "The world this station transmits in. It must be one of the worlds it works; the keying "
                     "service refuses a transmission from the other.");
        r.gate = "confirm";
        r.applies = "now";
        t.push_back(r);
    }
    // ---- Keying (design section 8) ----
    {
        Row r = pick("ptt-line", "Keying", "PTT line", {"RTS", "DTR"}, "",
                     "The interface's serial line that keys the radio. It must match switch S1001 in the SCU-17. "
                     "The radio isn't keyed until this is set.");
        r.applies = "now";
        t.push_back(r);
    }
    {
        Row r = op("cat-ptt", "Keying", "Key by CAT command", "bool", "no",
                   "Key the radio with a CAT command in place of the PTT line. Off unless you turn it on.");
        r.gate = "confirm";
        r.applies = "now";
        r.warning = "CAT keying goes over the CAT line: a kill switch on the PTT line won't stop it.";
        t.push_back(r);
    }
    // ---- Display (design 6.4) ----
    {
        Row r = op("engineering-readouts", "Display", "Engineering readouts", "bool", "no",
                   "Developer and engineer readouts on the operator's screens. Off unless turned on.");
        r.applies = "now";
        t.push_back(r);
    }
    // ---- Traffic ----
    t.push_back(op("no-acks", "Traffic", "Don't answer with ACKs", "bool", "no",
                   "Received messages are not answered with an ACK."));
    t.push_back(num("max-attempts", "Traffic", "Tries before a message fails", 1, 20, "", "3",
                    "Modem failures before a message is marked failed."));
    t.push_back(num("ack-timeout", "Traffic", "Wait for an ACK", 0, 3600, "s", "120",
                    "Seconds to wait for an ACK before sending again; 0 never sends again."));
    t.push_back(num("ack-retries", "Traffic", "Sends again without an ACK", 0, 10, "", "2",
                    "Sends after the first before a message waits as unacked."));
    t.push_back(num("tx-gap", "Traffic", "Quiet time before keying", 0, 120, "s", "6",
                    "Quiet seconds after any send or receive before the station sends."));
    t.push_back(op("link-gate", "Traffic", "Send only with a link", "bool", "no",
                   "Hold each message until the radio reports a link to its station."));
    t.push_back(num("link-wait", "Traffic", "Give up without a link", 0, 86400, "s", "0",
                    "With send only with a link: give up on a message after this long; 0 waits."));
    t.push_back(op("handling", "Traffic", "Handling times", "list", "10,30,180,360",
                   "Minutes before an unsent FLASH, IMMEDIATE, PRIORITY and ROUTINE message is logged as late "
                   "(four numbers; 0 turns one off)."));
    t.push_back(num("expire-h", "Traffic", "Expire after", 0, 720, "h", "0",
                    "Hours after filing that a message still queued, held or unacked expires; 0 never."));
    t.push_back(num("parts-hold-h", "Traffic", "Hold incomplete parts", 0, 720, "h", "72",
                    "Hours to hold a message sent in parts for its missing parts; 0 holds forever."));
    t.push_back(num("parts-resend-per-hour", "Traffic", "Parts sent again per hour", 0, 600, "", "12",
                    "Parts sent again from the resend queue, at most this many an hour; 0 no cap."));
    t.push_back(pick("raw-rx-log", "Traffic", "Keep every burst heard", {"on", "off"}, "on",
                     "Keep each burst as heard, one file a day."));
    // ---- Book (ask 30) ----
    {
        Row r = op("book-auto-add-heard", "Book", "Add calls heard to the book", "bool", "no",
                   "A MARS station the core hears and the book doesn't know is added to it, as a station of the "
                   "MARS world. Off unless you turn it on.");
        r.applies = "now";
        t.push_back(r);
    }
    // ---- Host plumbing: locked ----
    for (auto k : {"db-dir", "outbox", "captures", "captures-fallback", "gale-dir", "words", "scan-log", "wwv-dir",
                   "gps-status", "health-file", "engine-status", "lqa-db", "favourites", "own-address", "scan-plan",
                   "radio-file", "store-root", "amateur-own-address", "amateur-words", "keying-file", "web-root",
                   "rx-schedule-dir"})
        t.push_back(locked(k, "Paths", k));
    for (auto k : {"api-port", "web-port", "s5066-port", "appa-port"}) t.push_back(locked(k, "Ports", k, "int"));
    for (auto k : {"link", "link-socket", "link-poll-ms", "tuner-socket", "gale-key-socket", "gale-key-timeout-ms",
                   "s5066-peers", "s5066-sap", "s5066-ttl",
                   "s5066-confirm", "s5066-broadcast", "loopback-pair", "loopback-bps"})
        t.push_back(locked(k, "Link", k));
    for (auto k : {"tx-cmd", "tx-arg", "advisor", "advisor-arg"}) t.push_back(locked(k, "Programs", k));
    // Ask 26: the kinds of record the host locked (records.h).
    t.push_back(locked("lock", "Records", "Locked records", "list"));
    return t;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::string error_reply(const std::string& why) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", why).end_object();
    return w.str();
}

// Written whole: a temp file renamed over it.
bool write_whole(const std::string& path, const std::string& body, mode_t mode, std::string* err) {
    fs::path p(path);
    std::error_code ec;
    if (!p.parent_path().empty()) fs::create_directories(p.parent_path(), ec);
    std::string tmp = path + ".new";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) {
        *err = "can't write " + tmp;
        return false;
    }
    bool ok = ::write(fd, body.data(), body.size()) == static_cast<ssize_t>(body.size());
    ok = ::fsync(fd) == 0 && ok;
    ::close(fd);
    ::chmod(tmp.c_str(), mode);
    if (ok) fs::rename(tmp, path, ec);
    if (!ok || ec) {
        fs::remove(tmp, ec);
        *err = "can't write " + path;
        return false;
    }
    return true;
}

std::mutex g_mu;
Start g_start;
std::map<std::string, std::string> g_started;  // each value in force at start

const std::set<std::string>& keying_keys() {
    static const std::set<std::string> k = {"world", "tx-world", "ptt-line", "cat-ptt", "engineering-readouts"};
    return k;
}

std::string world_file() { return (fs::path(g_start.gale_dir) / "world").string(); }

// The world the scanner would pick with no setting: the first word of
// GALE-DIR/world, else mars if GALE-DIR/net_plans is there, else amateur
// (greybeard-ale docs/amateur-world.md).
std::string world_default(std::string* source) {
    std::ifstream f(world_file());
    std::string w;
    if (f >> w) {
        w = lower(w);
        if (w == "mars" || w == "amateur" || w == "both") {
            if (source) *source = "world file";
            return w;
        }
    }
    std::error_code ec;
    if (source) *source = "default";
    return fs::exists(fs::path(g_start.gale_dir) / "net_plans", ec) ? "mars" : "amateur";
}

std::string value_locked(const std::string& key, std::string* source) {
    for (auto* m : {&g_start.op, &g_start.cli, &g_start.config}) {
        auto it = m->find(key);
        if (it != m->end()) {
            if (source) *source = m == &g_start.op ? "operator" : m == &g_start.cli ? "command line" : "config";
            return it->second;
        }
    }
    if (key == "world") return world_default(source);
    if (source) *source = "default";
    if (key == "tx-world") return value_locked("world", nullptr) == "amateur" ? "amateur" : "mars";
    if (key == "keying-file") return (fs::path(g_start.gale_dir) / "keying.json").string();
    if (const Row* r = find(key)) return r->def;
    return "";
}

bool fits(const std::string& world, const std::string& tx) { return world == "both" || world == tx; }

std::string keying_json_locked(int64_t now) {
    std::string ptt = value_locked("ptt-line", nullptr), world = value_locked("world", nullptr),
                tx = value_locked("tx-world", nullptr);
    if (!fits(world, tx)) tx = world;
    JsonWriter w;
    w.begin_object().field("version", 1);
    if (ptt.empty()) w.field_null("ptt_line");
    else w.field("ptt_line", ptt);
    w.field("cat_ptt", value_locked("cat-ptt", nullptr) == "yes")
        .field("world", world)
        .field("tx_world", tx)
        .field("engineering_readouts", value_locked("engineering-readouts", nullptr) == "yes")
        .field("station", value_locked("station", nullptr))
        .field("updated_at", static_cast<long long>(now))
        .end_object();
    return w.str();
}

bool write_keying_locked(int64_t now, std::string* err) {
    std::string path = value_locked("keying-file", nullptr);
    if (path.empty()) return true;
    return write_whole(path, keying_json_locked(now) + "\n", 0644, err);
}

bool save_operator_locked(std::string* err) {
    std::string body = "# Written by appliance-core from the operator's settings (settings_set). Don't edit by hand:\n"
                       "# change them from the kiosk. appliance.conf holds the host's own settings.\n";
    for (auto& [k, v] : g_start.op) body += k + " = \"" + v + "\"\n";
    return write_whole(operator_file(g_start.db_dir), body, 0600, err);
}

void write_row(JsonWriter& w, const Row& r) {
    std::string source, v = value_locked(r.key, &source);
    w.begin_object()
        .field("key", r.key)
        .field("group", r.group)
        .field("label", r.label)
        .field("type", r.type)
        .field("class", r.cls)
        .field("gate", r.gate)
        .field("applies", r.applies);
    if (!r.choices.empty()) {
        w.begin_array("choices");
        for (auto& c : r.choices) w.field("", c);
        w.end_array();
    }
    if (r.type == "int") w.field("min", r.lo).field("max", r.hi);
    if (!r.unit.empty()) w.field("unit", r.unit);
    if (r.type == "action") {
        w.field("call", r.call);
    } else {
        w.field("value", v).field("default", r.key == "world" || r.key == "tx-world" ? "" : r.def).field("source",
                                                                                                         source);
        auto it = g_started.find(r.key);
        w.field("pending_restart", r.applies == "restart" && it != g_started.end() && it->second != v);
    }
    if (!r.help.empty()) w.field("help", r.help);
    if (!r.warning.empty()) w.field("warning", r.warning);
    w.end_object();
}

std::string settings_list(std::map<std::string, std::string>& req) {
    std::string group = req["group"];
    bool all = req["all"] == "Y" || req["all"] == "true";
    std::lock_guard<std::mutex> g(g_mu);
    JsonWriter w;
    w.begin_object().field("ok", true).begin_array("settings");
    for (auto& r : table()) {
        if (r.cls == "locked" && !all) continue;
        if (!group.empty() && lower(r.group) != lower(group)) continue;
        write_row(w, r);
    }
    w.end_array().field("file", "operator-settings");
    records::write(w);  // ask 26: each kind of record and its class
    w.end_object();
    return w.str();
}

std::string settings_set(std::map<std::string, std::string>& req, int64_t now, bool reset) {
    const Row* r = find(req["key"]);
    if (!r) return error_reply("key: no setting " + req["key"] + " (settings_list)");
    if (r->cls == "locked")
        return error_reply(r->key + " is host plumbing: it is set in appliance.conf, not from the kiosk");
    if (r->type == "action") return error_reply(r->key + " is set with " + r->call);
    std::string norm, err;
    if (!reset) {
        if (!req.count("value")) return error_reply("value: the new value (settings_list shows each one's range)");
        if (!check(*r, req["value"], &norm, &err)) return error_reply(r->key + ": " + err);
    }
    std::lock_guard<std::mutex> g(g_mu);
    Start before = g_start;
    std::string old = value_locked(r->key, nullptr);
    if (reset) g_start.op.erase(r->key);
    else g_start.op[r->key] = norm;
    std::string now_v = value_locked(r->key, nullptr);
    // The gate: a change that needs the operator's yes says why, and waits.
    bool confirmed = req["confirm"] == "Y" || req["confirm"] == "true";
    bool gated = r->gate == "confirm" && now_v != old && (r->key != "cat-ptt" || now_v == "yes");
    std::vector<std::pair<std::string, std::string>> also;
    if (r->key == "world" || r->key == "tx-world") {
        std::string world = value_locked("world", nullptr), tx = value_locked("tx-world", nullptr);
        if (!fits(world, tx)) {
            if (r->key == "tx-world") {
                g_start = before;
                return error_reply("tx-world: " + tx + " isn't a world this station works (world is " + world + ")");
            }
            g_start.op["tx-world"] = world;  // the world it now works
            also.emplace_back("tx-world", world);
        }
    }
    if (gated && !confirmed) {
        g_start = before;
        JsonWriter w;
        w.begin_object()
            .field("ok", false)
            .field("confirm_needed", true)
            .field("key", r->key)
            .field("value", now_v)
            .field("warning", r->warning.empty() ? "This change needs your yes." : r->warning)
            .field("error", "send it again with confirm=Y to set it")
            .end_object();
        return w.str();
    }
    // A new call: its keys first, so the next start has them.
    bool keys_made = false;
    std::string fingerprint;
    if (r->key == "station" && now_v != old && !now_v.empty()) {
        station::Identity id;
        if (!first_start::station_keys(g_start.db_dir, now_v, now, id, &keys_made, &err)) {
            g_start = before;
            return error_reply(err);
        }
        fingerprint = station::fingerprint(station::card_of(id));
    }
    if (!save_operator_locked(&err)) {
        g_start = before;
        return error_reply(err);
    }
    std::string note;
    if (r->key == "world" && now_v != old) {
        // The kiosk setting the scanner reads (greybeard-ale docs/amateur-world.md).
        if (!write_whole(world_file(), now_v + "\n", 0644, &err)) note = "saved, but " + err + ": the scanner keeps its world";
        else note = "The scanner picks up the new world when it restarts.";
    }
    if (keying_keys().count(r->key) && !write_keying_locked(now, &err)) note = "saved, but the keying file: " + err;
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("key", r->key)
        .field("value", now_v)
        .field("changed", now_v != old)
        .field("applies", r->applies)
        .field("restart_needed", r->applies == "restart" && g_started[r->key] != now_v);
    if (!also.empty()) {
        w.begin_array("also_set");
        for (auto& [k, v] : also) w.begin_object().field("key", k).field("value", v).end_object();
        w.end_array();
    }
    if (r->key == "station" && !fingerprint.empty()) w.field("keys_made", keys_made).field("fingerprint", fingerprint);
    if (!note.empty()) w.field("note", note);
    w.end_object();
    return w.str();
}

}  // namespace

const std::vector<Row>& table() {
    static const std::vector<Row> t = make_table();
    return t;
}

const Row* find(const std::string& key) {
    std::string k = lower(trim(key));
    std::replace(k.begin(), k.end(), '_', '-');
    for (auto& r : table())
        if (r.key == k) return &r;
    return nullptr;
}

bool check(const Row& r, const std::string& value, std::string* norm, std::string* err) {
    std::string v = trim(value);
    if (v.find_first_of("\"\n\r") != std::string::npos) {
        *err = "no quotes or line breaks";
        return false;
    }
    if (r.type == "int") {
        if (v.empty() || v.size() > 9 || v.find_first_not_of("0123456789") != std::string::npos) {
            *err = "a whole number from " + std::to_string(r.lo) + " to " + std::to_string(r.hi);
            return false;
        }
        long long n = std::stoll(v);
        if (n < r.lo || n > r.hi) {
            *err = "from " + std::to_string(r.lo) + " to " + std::to_string(r.hi);
            return false;
        }
        *norm = std::to_string(n);
        return true;
    }
    if (r.type == "bool") {
        std::string l = lower(v);
        if (l == "yes" || l == "true" || l == "y" || l == "on") *norm = "yes";
        else if (l == "no" || l == "false" || l == "n" || l == "off") *norm = "no";
        else {
            *err = "yes or no";
            return false;
        }
        return true;
    }
    if (r.type == "choice") {
        for (auto& c : r.choices)
            if (lower(c) == lower(v)) {
                *norm = c;
                return true;
            }
        std::string list;
        for (auto& c : r.choices) list += (list.empty() ? "" : ", ") + c;
        *err = "one of " + list;
        return false;
    }
    if (r.type == "call") {
        std::string c = v;
        for (auto& ch : c) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (!valid_station(c) || is_broadcast(c)) {
            *err = "letters A-Z, digits and -, at most 16, not ALL";
            return false;
        }
        *norm = c;
        return true;
    }
    if (r.key == "handling") {
        std::istringstream in(v);
        std::string part, out;
        int n = 0;
        while (std::getline(in, part, ',')) {
            part = trim(part);
            if (part.empty() || part.size() > 5 || part.find_first_not_of("0123456789") != std::string::npos) {
                *err = "four minute counts, FLASH,IMMEDIATE,PRIORITY,ROUTINE, like 10,30,180,360";
                return false;
            }
            out += (n++ ? "," : "") + std::to_string(std::stoi(part));
        }
        if (n != 4) {
            *err = "four minute counts, FLASH,IMMEDIATE,PRIORITY,ROUTINE, like 10,30,180,360";
            return false;
        }
        *norm = out;
        return true;
    }
    if (r.key == "tx-mode") {
        if (v.empty() || v.size() > 12 || v.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") !=
                                              std::string::npos) {
            *err = "the gale-tx mode: letters and digits, like 600S";
            return false;
        }
        *norm = v;
        return true;
    }
    if (r.type == "text" && r.cls == "operator") {
        *err = "not a value this setting takes";
        return false;
    }
    *norm = v;
    return true;
}

std::map<std::string, std::string> args_map(const std::vector<std::string>& args) {
    static const std::set<std::string> flags = {"no-acks", "link-gate"}, lists = {"tx-arg", "advisor-arg", "lock"};
    const auto& keys = config_keys();
    std::map<std::string, std::string> out;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a.rfind("--", 0) != 0) continue;
        std::string k = a.substr(2);
        if (k == "acks") {
            out["no-acks"] = "no";
            continue;
        }
        if (k == "no-link-gate") {
            out["link-gate"] = "no";
            continue;
        }
        if (flags.count(k)) {
            out[k] = "yes";
            continue;
        }
        if (std::find(keys.begin(), keys.end(), k) == keys.end() || i + 1 >= args.size()) continue;
        std::string v = args[++i];
        if (lists.count(k) && out.count(k)) out[k] += " " + v;
        else out[k] = v;
    }
    return out;
}

std::string operator_file(const std::string& db_dir) {
    return (fs::path(db_dir.empty() ? "." : db_dir) / "operator-settings").string();
}

std::map<std::string, std::string> load_operator(const std::string& db_dir, std::vector<std::string>* skipped) {
    std::map<std::string, std::string> out;
    std::ifstream f(operator_file(db_dir));
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        std::string why;
        const Row* r = nullptr;
        std::string k, v, norm;
        if (eq == std::string::npos) why = "not key = value";
        else {
            k = trim(t.substr(0, eq));
            v = trim(t.substr(eq + 1));
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
            r = find(k);
            if (!r) why = "no setting " + k;
            else if (r->cls != "operator" || r->type == "action") why = k + " isn't an operator setting";
            else if (!check(*r, v, &norm, &why)) why = k + ": " + why;
        }
        if (!why.empty()) {
            if (skipped) skipped->push_back("line " + std::to_string(n) + ": " + why);
            continue;
        }
        out[r->key] = norm;
    }
    return out;
}

std::vector<std::string> overlay_args(const std::map<std::string, std::string>& op) {
    std::vector<std::string> a;
    const auto& keys = config_keys();
    for (auto& [k, v] : op) {
        if (k == "no-acks") a.push_back(v == "yes" ? "--no-acks" : "--acks");
        else if (k == "link-gate") a.push_back(v == "yes" ? "--link-gate" : "--no-link-gate");
        else if (std::find(keys.begin(), keys.end(), k) != keys.end()) {
            a.push_back("--" + k);
            a.push_back(v);
        }
    }
    return a;
}

void init(const Start& s) {
    std::lock_guard<std::mutex> g(g_mu);
    g_start = s;
    g_started.clear();
    for (auto& r : table())
        if (r.type != "action") g_started[r.key] = value_locked(r.key, nullptr);
}

std::string value(const std::string& key, std::string* source) {
    std::lock_guard<std::mutex> g(g_mu);
    return value_locked(key, source);
}

std::string keying_json(int64_t now) {
    std::lock_guard<std::mutex> g(g_mu);
    return keying_json_locked(now);
}

bool write_keying(int64_t now, std::string* err) {
    std::lock_guard<std::mutex> g(g_mu);
    return write_keying_locked(now, err);
}

bool sync_world(std::string* err) {
    std::lock_guard<std::mutex> g(g_mu);
    std::string source, w = value_locked("world", &source);
    if (source == "world file" || source == "default") return true;  // the file's own, or none set
    std::ifstream f(world_file());
    std::string have;
    f >> have;
    if (lower(have) == w) return true;
    return write_whole(world_file(), w + "\n", 0644, err);
}

std::optional<std::string> handle(const std::string& cmd, std::map<std::string, std::string>& req, int64_t now) {
    if (cmd == "settings_list") return settings_list(req);
    if (cmd == "settings_set") return settings_set(req, now, false);
    if (cmd == "settings_reset") return settings_set(req, now, true);
    if (cmd == "keying_settings") {
        std::string k = keying_json(now);
        return "{\"ok\":true,\"keying\":" + k + ",\"file\":" + "\"" + json_escape(value("keying-file")) + "\"}";
    }
    return std::nullopt;
}

}  // namespace settings
}  // namespace pna
