#include "forms.h"

#include "channels.h"
#include "frame.h"
#include "json.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace pna {
namespace forms {

namespace fs = std::filesystem;

namespace {

const size_t kMaxTemplate = 16384;
const size_t kMaxFields = 40;

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > 24) return false;
    for (char c : id)
        if (!(std::isupper(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_'))
            return false;
    return true;
}
bool valid_key(const std::string& k) {
    if (k.empty() || k.size() > 32 || k == "cmd" || k == "form") return false;
    for (char c : k)
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_'))
            return false;
    return true;
}
std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

// A trust choice is the member's own: it is never a fixed value.
bool settable(const std::string& k) {
    static const std::set<std::string> no = {"keymaster", "accept", "replace", "fingerprint", "how", "cmd", "form"};
    return !no.count(k);
}

const std::set<std::string> kTimeMasks = {"DDHHMM", "HHMM", "YYYYMMDD", "YYYYMMDDHHMM"};
const std::set<std::string> kChecks = {"station", "designator", "frequency", "hhmm", "date", "fingerprint",
                                       "passphrase", "number", "path", "stations"};
const std::set<std::string> kPrefill = {"book", "channel", "schedule", "template", "card"};

bool set_mask(Field& f, std::string* error) {
    const std::string& m = f.mask;
    if (m.empty()) {
        *error = "the mask in () is empty";
        return false;
    }
    if (m == "X") {
        f.kind = "text";
    } else if (m == "TEXT") {
        f.kind = "textblock";
    } else if (m[0] == '@') {
        f.kind = "choice";
        std::string s = m;
        if (s.back() == '+') {
            f.multi = true;
            s.pop_back();
        }
        if (std::find(sources().begin(), sources().end(), s) == sources().end()) {
            *error = "no such list " + s + " (the lists: " + [] {
                std::string all;
                for (auto& x : sources()) all += (all.empty() ? "" : " ") + x;
                return all;
            }() + ")";
            return false;
        }
        f.source = s;
    } else if (m.find(',') != std::string::npos) {
        std::set<std::string> vals;
        std::string item;
        std::istringstream in(m);
        while (std::getline(in, item, ',')) {
            item = trim(item);
            if (item.empty()) {
                *error = "an empty choice in (" + m + ")";
                return false;
            }
            f.choices.push_back({item, item});
            vals.insert(upper(item));
        }
        if (vals == std::set<std::string>{"Y", "N"}) {
            f.kind = "yesno";
            f.choices = {{"Y", "Yes"}, {"N", "No"}};
        } else {
            f.kind = "choice";
        }
    } else if (kTimeMasks.count(m)) {
        f.kind = "time";
    } else {
        f.kind = "pattern";
    }
    return true;
}

bool parse_field(const std::string& rest, Field& f, std::string* error) {
    std::string r = trim(rest);
    size_t sp = r.find(' ');
    if (sp == std::string::npos) {
        *error = "FIELD needs a key and a Label(mask)";
        return false;
    }
    f.key = r.substr(0, sp);
    if (!valid_key(f.key)) {
        *error = "field key " + f.key + ": lower case letters, digits and _ (not cmd or form)";
        return false;
    }
    r = trim(r.substr(sp + 1));
    size_t open = r.find('('), close = open == std::string::npos ? open : r.find(')', open);
    if (open == std::string::npos || close == std::string::npos) {
        *error = "field " + f.key + ": write the label then its mask in (), as Frequency(X)";
        return false;
    }
    f.label = trim(r.substr(0, open));
    if (f.label.empty()) {
        *error = "field " + f.key + " has no label";
        return false;
    }
    f.mask = trim(r.substr(open + 1, close - open - 1));
    std::string after = r.substr(close + 1);
    size_t end = after.find(' ');
    f.suffix = after.substr(0, end);
    std::string flags = end == std::string::npos ? "" : after.substr(end + 1);
    std::string merr;
    if (!set_mask(f, &merr)) {
        *error = "field " + f.key + ": " + merr;
        return false;
    }
    // A default may have spaces (default=BY VOICE): it runs to the next flag.
    std::vector<std::string> ws;
    static const std::set<std::string> bare = {"required", "optional", "upper", "secret", "nosend"};
    for (auto& w : words(flags)) {
        bool flag = w.find('=') != std::string::npos || bare.count(lower(w));
        if (!flag && !ws.empty() && lower(ws.back()).rfind("default=", 0) == 0) ws.back() += " " + w;
        else ws.push_back(w);
    }
    for (auto& w : ws) {
        std::string k = w, v;
        size_t eq = w.find('=');
        if (eq != std::string::npos) {
            k = w.substr(0, eq);
            v = w.substr(eq + 1);
        }
        k = lower(k);
        if (k == "required") f.required = true;
        else if (k == "optional") f.required = false;
        else if (k == "upper") f.upper = true;
        else if (k == "secret") f.secret = true;
        else if (k == "nosend") f.send = false;
        else if (k == "default") f.def = v;
        else if (k == "again") f.again = v;
        else if (k == "as" && (valid_key(v) || v == "form")) f.as = v;
        else if (k == "max" && !v.empty() && std::all_of(v.begin(), v.end(), ::isdigit)) f.max = std::stoi(v);
        else if (k == "check" && kChecks.count(v)) f.check = v;
        else {
            *error = "field " + f.key + ": can't read " + w;
            return false;
        }
    }
    if (f.secret && !f.def.empty()) {
        *error = "field " + f.key + ": a secret field can't have a default";
        return false;
    }
    return true;
}

// Does the value fit a pattern mask? Each position: X anything, A a letter,
// N a letter or digit, # a digit; anything else stands for itself. Shorter
// values fit; the mask is the longest.
bool fits_pattern(const std::string& v, const std::string& mask) {
    if (v.size() > mask.size()) return false;
    bool any = false, alpha = false, digit = false;
    std::set<char> lit;
    for (char c : mask) {
        if (c == 'X') any = true;
        else if (c == 'A') alpha = true;
        else if (c == 'N') alpha = digit = true;
        else if (c == '#') digit = true;
        else lit.insert(c);
    }
    if (any) return true;
    for (char c : v) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalpha(u) && alpha) continue;
        if (std::isdigit(u) && digit) continue;
        if (lit.count(c)) continue;
        return false;
    }
    return true;
}

bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

bool fits_time(const std::string& v, const std::string& mask) {
    if (v.size() != mask.size() || !all_digits(v)) return false;
    auto two = [&](size_t p) { return std::stoi(v.substr(p, 2)); };
    int day = 1, mon = 1, hh = 0, mm = 0;
    if (mask == "HHMM") {
        hh = two(0);
        mm = two(2);
    } else if (mask == "DDHHMM") {
        day = two(0);
        hh = two(2);
        mm = two(4);
    } else {  // YYYYMMDD, YYYYMMDDHHMM
        mon = two(4);
        day = two(6);
        if (mask.size() == 12) {
            hh = two(8);
            mm = two(10);
        }
    }
    return day >= 1 && day <= 31 && mon >= 1 && mon <= 12 && hh <= 23 && mm <= 59;
}

// A named check. Empty: fine; else the problem, with no value in it.
std::string run_check(const std::string& check, const std::string& v, const std::map<std::string, std::string>& vals) {
    if (check == "station") {
        if (!valid_station(v) || is_broadcast(v)) return "a station call: letters, digits and -, up to 12";
    } else if (check == "stations") {
        auto ws = words(v);
        if (ws.empty()) return "one or more station calls";
        for (auto& w : ws)
            if (!valid_station(w) || is_broadcast(w)) return "station calls, separated by spaces";
    } else if (check == "designator") {
        if (!valid_designator(v)) return "a channel designator: 1 to 12 letters, digits or -, with a letter (P1, S1)";
    } else if (check == "frequency") {
        auto u = vals.find("unit");
        if (parse_frequency(v, u == vals.end() ? "" : u->second) <= 0)
            return "a frequency between 100 kHz and 3 GHz (in kHz unless you pick another unit)";
    } else if (check == "hhmm") {
        if (!fits_time(v, "HHMM")) return "a time as HHMM, 24 hour UTC (1900)";
    } else if (check == "date") {
        if (!(fits_time(v, "YYYYMMDD") || fits_time(v, "YYYYMMDDHHMM"))) return "a date as YYYYMMDD";
    } else if (check == "fingerprint") {
        std::string s;
        for (char c : v)
            if (c != ' ') s += c;
        if (s.size() != 16 || !std::all_of(s.begin(), s.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); }))
            return "the fingerprint as read to you: four groups of four";
    } else if (check == "passphrase") {
        if (v.size() < 12) return "at least 12 characters";
    } else if (check == "number") {
        if (!all_digits(v)) return "a whole number";
    } else if (check == "path") {
        if (v.empty() || v[0] != '/') return "a full path, starting with / (a stick is under /media)";
    }
    return "";
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool write_file(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        if (!f) return false;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}

const char* kB32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

std::string b32(const std::string& in) {
    std::string out;
    uint32_t buf = 0;
    int bits = 0;
    for (unsigned char c : in) {
        buf = (buf << 8) | c;
        bits += 8;
        while (bits >= 5) {
            out += kB32[(buf >> (bits - 5)) & 31];
            bits -= 5;
        }
    }
    if (bits > 0) out += kB32[(buf << (5 - bits)) & 31];
    return out;
}
bool unb32(const std::string& in, std::string& out) {
    out.clear();
    uint32_t buf = 0;
    int bits = 0;
    for (char c : in) {
        const char* p = std::strchr(kB32, c);
        if (!p || !*p) return false;
        buf = (buf << 5) | static_cast<uint32_t>(p - kB32);
        bits += 5;
        if (bits >= 8) {
            out += static_cast<char>((buf >> (bits - 8)) & 0xff);
            bits -= 8;
        }
    }
    return true;
}

void write_field(JsonWriter& w, const Field& f, const std::map<std::string, std::string>& values,
                 const std::vector<Choice>& choices) {
    w.begin_object()
        .field("key", f.key)
        .field("label", f.label)
        .field("kind", f.kind)
        .field("mask", f.mask)
        .field("suffix", f.suffix)
        .field("required", f.required)
        .field("upper", f.upper)
        .field("secret", f.secret)
        .field("multi", f.multi);
    if (!f.check.empty()) w.field("check", f.check);
    if (f.max) w.field("max", f.max);
    if (!f.again.empty()) w.field("again", f.again);
    if (!f.source.empty()) w.field("source", f.source);
    if (f.secret) {
        auto h = values.find(f.key + "?");
        w.field("has_value", h != values.end());
    } else {
        auto v = values.find(f.key);
        w.field("value", v != values.end() ? v->second : f.def);
    }
    if (!f.def.empty() && !f.secret) w.field("default", f.def);
    if (f.kind == "choice" || f.kind == "yesno") {
        w.begin_array("choices");
        for (auto& c : choices) w.begin_object().field("value", c.value).field("label", c.label).end_object();
        w.end_array();
    }
    w.field("help", f.help);
    w.end_object();
}

std::string err_reply(const std::string& e) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", e).end_object();
    return w.str();
}

}  // namespace

const std::vector<std::string>& actions() {
    static const std::vector<std::string> a = {
        "setup_station", "station_card", "book_put",       "book_remove",    "channel_put",      "channel_remove",
        "sched_put",     "sched_remove",   "card_send",      "card_import",      "card_check",
        "keymaster_accept", "keymaster_refuse", "bundle_make", "bundle_send",    "bundle_load",
        "net_tape",      "handout_import", "key_open",       "station_backup",   "station_restore",
        "form_template_put", "form_template_reset"};
    return a;
}

const std::vector<std::string>& sources() {
    static const std::vector<std::string> s = {
        "@channels", "@cards",       "@cards_unchecked", "@stations",   "@nets",        "@schedules",
        "@book",     "@keymasters_undecided", "@forms",  "@msg_cards",  "@msg_bundles", "@msg_tapes"};
    return s;
}

bool parse(const std::string& text, Template& out, std::string* error) {
    out = Template{};
    out.text = text;
    auto fail = [&](int n, const std::string& e) {
        if (error) *error = "line " + std::to_string(n) + ": " + e;
        return false;
    };
    if (text.size() > kMaxTemplate) {
        if (error) *error = "a template is at most 16 KB";
        return false;
    }
    std::istringstream in(text);
    std::string line;
    int n = 0;
    bool in_help = false, in_block = false;
    std::string block_label, block_text;
    std::map<std::string, std::string> help;
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (in_help) {
            std::string t = trim(line);
            if (!in_block) {
                if (t.empty() || t[0] == ';') continue;
                if (t == "END_HELP") {
                    in_help = false;
                    continue;
                }
                if (t.back() != ':') return fail(n, "in the help, start each block with its label and a colon");
                block_label = trim(t.substr(0, t.size() - 1));
                block_text.clear();
                in_block = true;
            } else if (t == "END") {
                help[block_label] = trim(block_text);
                in_block = false;
            } else {
                block_text += (block_text.empty() ? "" : "\n") + line;
            }
            continue;
        }
        std::string t = trim(line);
        if (t.empty() || t[0] == ';') continue;
        size_t sp = t.find(' ');
        std::string word = upper(t.substr(0, sp));
        std::string rest = sp == std::string::npos ? "" : trim(t.substr(sp + 1));
        if (word == "FORM") {
            if (!out.id.empty()) return fail(n, "one FORM per template");
            if (!valid_id(rest)) return fail(n, "the form id: capitals, digits and _, up to 24");
            out.id = rest;
        } else if (out.id.empty()) {
            return fail(n, "a template starts with FORM and its id");
        } else if (word == "TITLE") {
            out.title = rest;
        } else if (word == "SUMMARY") {
            out.summary += (out.summary.empty() ? "" : " ") + rest;
        } else if (word == "LEVEL") {
            if (upper(rest) != "ZERO") return fail(n, "LEVEL ZERO is the only level");
            out.level_zero = true;
        } else if (word == "ORDER") {
            if (!all_digits(rest) || rest.size() > 6) return fail(n, "ORDER takes a number");
            out.order = std::stoi(rest);
        } else if (word == "ACTION") {
            auto ws = words(rest);
            Action a;
            if (ws.empty()) return fail(n, "ACTION names a call");
            a.cmd = lower(ws[0]);
            if (std::find(actions().begin(), actions().end(), a.cmd) == actions().end())
                return fail(n, "a form can't run " + a.cmd);
            if (ws.size() == 3 && upper(ws[1]) == "WHEN") {
                size_t eq = ws[2].find('=');
                if (eq == std::string::npos) return fail(n, "WHEN key=VALUE");
                a.when_key = ws[2].substr(0, eq);
                a.when_value = upper(ws[2].substr(eq + 1));
            } else if (ws.size() != 1) {
                return fail(n, "ACTION call [WHEN key=VALUE]");
            }
            out.actions.push_back(a);
        } else if (word == "SET") {
            size_t s2 = rest.find(' ');
            std::string k = rest.substr(0, s2), v = s2 == std::string::npos ? "" : trim(rest.substr(s2 + 1));
            if (!valid_key(k)) return fail(n, "SET key value");
            if (!settable(k)) return fail(n, k + " is the member's own choice: make it a field");
            out.sets.push_back({k, v});
        } else if (word == "PREFILL") {
            auto ws = words(rest);
            if (ws.size() != 2 || !kPrefill.count(lower(ws[0]))) return fail(n, "PREFILL book|channel|schedule|template|card key");
            out.prefill_kind = lower(ws[0]);
            out.prefill_key = ws[1];
        } else if (word == "FIELD") {
            Field f;
            std::string e;
            if (!parse_field(rest, f, &e)) return fail(n, e);
            for (auto& g : out.fields)
                if (g.key == f.key) return fail(n, "field " + f.key + " twice");
            out.fields.push_back(f);
            if (out.fields.size() > kMaxFields) return fail(n, "at most 40 fields");
        } else if (word == "NEXT") {
            for (auto& w : words(rest)) out.next.push_back(upper(w));
        } else if (word == "BEGIN_HELP") {
            in_help = true;
        } else {
            return fail(n, "can't read " + word);
        }
    }
    if (in_block) return fail(n, "the help block " + block_label + " has no END");
    if (out.id.empty()) return fail(n, "no FORM line");
    if (out.title.empty()) out.title = out.id;
    if (out.actions.empty()) return fail(n, "no ACTION line");
    for (auto& a : out.actions) {
        if (a.when_key.empty()) continue;
        bool found = std::any_of(out.fields.begin(), out.fields.end(), [&](const Field& f) { return f.key == a.when_key; });
        if (!found) return fail(n, "WHEN names no field " + a.when_key);
    }
    for (auto& f : out.fields) {
        if (!f.again.empty() &&
            !std::any_of(out.fields.begin(), out.fields.end(), [&](const Field& g) { return g.key == f.again; }))
            return fail(n, "field " + f.key + ": again= names no field " + f.again);
        auto h = help.find(f.label);
        if (h == help.end()) h = help.find(f.label + ":");
        if (h != help.end()) f.help = h->second;
    }
    if (!out.prefill_key.empty() &&
        !std::any_of(out.fields.begin(), out.fields.end(), [&](const Field& f) { return f.key == out.prefill_key; }))
        return fail(n, "PREFILL names no field " + out.prefill_key);
    return true;
}

// ---------------------------------------------------------------- the library

std::vector<Library::Entry> Library::list() const {
    std::map<std::string, Entry> all;
    for (auto& [id, text] : builtin_texts()) {
        Entry e;
        std::string err;
        if (!parse(text, e.t, &err)) continue;
        e.origin = "built in";
        e.builtin = true;
        all[e.t.id] = e;
    }
    std::error_code ec;
    if (!dir_.empty() && fs::is_directory(dir_, ec)) {
        for (auto& de : fs::directory_iterator(dir_, ec)) {
            if (de.path().extension() != ".form") continue;
            Template t;
            if (!parse(read_file(de.path()), t, nullptr)) continue;
            if (t.id != de.path().stem().string()) continue;
            Entry e;
            e.t = t;
            e.builtin = all.count(t.id) > 0;
            e.changed = !e.builtin || all[t.id].t.text != t.text;
            e.origin = origin_of(t.id);
            if (!e.changed) e.origin = "built in";
            all[t.id] = e;
        }
    }
    std::vector<Entry> out;
    for (auto& [id, e] : all) out.push_back(e);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        return a.t.order != b.t.order ? a.t.order < b.t.order : a.t.id < b.t.id;
    });
    return out;
}

std::optional<Library::Entry> Library::get(const std::string& id) const {
    for (auto& e : list())
        if (e.t.id == upper(id)) return e;
    return std::nullopt;
}

std::string Library::origin_of(const std::string& id) const {
    std::string o = trim(read_file(fs::path(dir_) / (id + ".origin")));
    return o.empty() ? "edited here" : o;
}

bool Library::put(const std::string& text, const std::string& origin, std::string* result, std::string* id,
                  std::string* error) {
    Template t;
    std::string norm = text;
    if (!norm.empty() && norm.back() != '\n') norm += '\n';
    if (!parse(norm, t, error)) return false;
    if (dir_.empty()) {
        if (error) *error = "templates can't be changed here";
        return false;
    }
    if (id) *id = t.id;
    auto old = get(t.id);
    if (old && old->t.text == norm) {
        if (result) *result = "same";
        return true;
    }
    if (!write_file(fs::path(dir_) / (t.id + ".form"), norm) ||
        !write_file(fs::path(dir_) / (t.id + ".origin"), origin + "\n")) {
        if (error) *error = "couldn't write the template";
        return false;
    }
    if (result) *result = old ? "replaced" : "added";
    return true;
}

bool Library::reset(const std::string& id) {
    std::error_code ec;
    bool had = fs::remove(fs::path(dir_) / (upper(id) + ".form"), ec);
    fs::remove(fs::path(dir_) / (upper(id) + ".origin"), ec);
    return had;
}

std::vector<std::pair<std::string, std::string>> Library::changed() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& e : list())
        if (e.changed) out.push_back({e.t.id, e.t.text});
    return out;
}

// ---------------------------------------------------------------- the calls

namespace {

std::vector<Choice> choices_of(const Field& f, const Context& ctx) {
    if (!f.source.empty()) return ctx.choices ? ctx.choices(f.source) : std::vector<Choice>{};
    return f.choices;
}

// Tidies the values the member gave and lists what's wrong. Problems never
// repeat a value.
std::vector<std::pair<std::string, std::string>> check_values(const Template& t, const Context& ctx,
                                                              std::map<std::string, std::string>& vals) {
    std::vector<std::pair<std::string, std::string>> problems;
    for (auto& f : t.fields) {
        std::string v = vals.count(f.key) ? vals[f.key] : "";
        if (f.kind != "textblock") v = trim(v);
        if (f.upper || f.kind == "choice" || f.kind == "yesno") v = upper(v);
        if (f.kind == "yesno") {
            if (v == "YES") v = "Y";
            if (v == "NO") v = "N";
        }
        if (v.empty() && !f.def.empty() && !vals.count(f.key)) v = f.def;
        vals[f.key] = v;
    }
    for (auto& f : t.fields) {
        const std::string& v = vals[f.key];
        auto bad = [&](const std::string& p) { problems.push_back({f.key, p}); };
        if (v.empty()) {
            if (f.required) bad("this is needed");
            continue;
        }
        if (f.max && static_cast<int>(v.size()) > f.max) {
            bad("at most " + std::to_string(f.max) + " characters");
            continue;
        }
        if (!f.again.empty()) {
            if (v != vals[f.again]) bad("doesn't match " + [&] {
                for (auto& g : t.fields)
                    if (g.key == f.again) return g.label;
                return f.again;
            }());
            continue;
        }
        if (f.kind == "pattern" && !fits_pattern(v, f.mask)) {
            bad("doesn't fit " + f.mask);
            continue;
        }
        if (f.kind == "time" && !fits_time(v, f.mask)) {
            bad("a time as " + f.mask);
            continue;
        }
        if (f.kind == "choice" || f.kind == "yesno") {
            auto cs = choices_of(f, ctx);
            auto in = [&](const std::string& x) {
                return std::any_of(cs.begin(), cs.end(), [&](const Choice& c) { return upper(c.value) == x; });
            };
            if (f.multi) {
                for (auto& w : words(v))
                    if (!in(w)) {
                        bad("pick from the list");
                        break;
                    }
            } else if (!in(v)) {
                bad(cs.empty() ? "there's nothing to pick from yet" : "pick from the list");
            }
            if (!problems.empty() && problems.back().first == f.key) continue;
        }
        if (!f.check.empty()) {
            std::string p = run_check(f.check, v, vals);
            if (!p.empty()) bad(p);
        }
    }
    return problems;
}

void write_problems(JsonWriter& w, const std::vector<std::pair<std::string, std::string>>& problems) {
    w.begin_array("problems");
    for (auto& [k, p] : problems) w.begin_object().field("field", k).field("problem", p).end_object();
    w.end_array();
}

bool allowed(const Context& ctx, const std::string& id) {
    return ctx.only.empty() || std::find(ctx.only.begin(), ctx.only.end(), id) != ctx.only.end();
}

}  // namespace

std::optional<std::string> handle(Library& lib_in, const Context& ctx, const std::string& cmd,
                                  std::map<std::string, std::string>& req) {
    static const std::set<std::string> kCmds = {"form_list", "form_get", "form_check", "form_submit",
                                                "form_template_get", "form_template_put", "form_template_reset"};
    if (!kCmds.count(cmd)) return std::nullopt;
    Library builtin_lib("");
    Library& lib = ctx.builtin_only ? builtin_lib : lib_in;

    if (cmd == "form_list") {
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("forms");
        bool zero_only = upper(req["level"]) == "ZERO";
        for (auto& e : lib.list()) {
            if (!allowed(ctx, e.t.id) || (zero_only && !e.t.level_zero)) continue;
            w.begin_object()
                .field("id", e.t.id)
                .field("title", e.t.title)
                .field("summary", e.t.summary)
                .field("level_zero", e.t.level_zero)
                .field("order", e.t.order)
                .field("origin", e.origin)
                .field("changed", e.changed)
                .end_object();
        }
        w.end_array().end_object();
        return w.str();
    }

    std::string id = upper(req["form"]);
    if (id.empty()) return err_reply("name the form (form=CHANNEL; form_list lists them)");
    if (!allowed(ctx, id)) {
        if (!ctx.builtin_only && !lib.get(id)) return err_reply("no form " + id + " (form_list lists them)");
        return err_reply("form " + id + " isn't open yet: set up the station first (form SETUP)");
    }

    if (cmd == "form_template_put") {
        if (ctx.builtin_only) return err_reply("templates can't be changed before the station is set up");
        std::string text = req["text"];
        if (text.empty()) return err_reply("give the template as text");
        std::string result, got, err;
        if (!lib.put(text, "edited here", &result, &got, &err)) return err_reply(err);
        if (got != id && !(id == "NEW")) {
            lib.reset(got);
            return err_reply("the template says FORM " + got + ", not " + id);
        }
        JsonWriter w;
        w.begin_object().field("ok", true).field("form", got).field("result", result).end_object();
        return w.str();
    }
    if (cmd == "form_template_reset") {
        if (ctx.builtin_only) return err_reply("templates can't be changed before the station is set up");
        auto before = lib.get(id);
        if (!lib.reset(id)) return err_reply("form " + id + " has no copy of its own here");
        auto after = lib.get(id);
        JsonWriter w;
        w.begin_object().field("ok", true).field("form", id).field("result", after ? "built in again" : "removed").end_object();
        (void)before;
        return w.str();
    }

    auto e = lib.get(id);
    if (!e) return err_reply("no form " + id + " (form_list lists them)");
    const Template& t = e->t;

    if (cmd == "form_template_get") {
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("form", t.id)
            .field("origin", e->origin)
            .field("builtin", e->builtin)
            .field("changed", e->changed)
            .field("text", t.text)
            .end_object();
        return w.str();
    }

    if (cmd == "form_get") {
        std::map<std::string, std::string> values;
        if (!t.prefill_kind.empty() && !req[t.prefill_key].empty() && ctx.prefill)
            values = ctx.prefill(t.prefill_kind, req[t.prefill_key]);
        for (auto& f : t.fields)
            if (f.secret) values.erase(f.key);
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("form", t.id)
            .field("title", t.title)
            .field("summary", t.summary)
            .field("origin", e->origin)
            .field("level_zero", t.level_zero)
            .field("prefilled", !values.empty());
        w.begin_array("fields");
        for (auto& f : t.fields) write_field(w, f, values, choices_of(f, ctx));
        w.end_array();
        w.begin_array("fixed");
        for (auto& [k, v] : t.sets) w.begin_object().field("key", k).field("value", v).end_object();
        w.end_array();
        w.begin_array("actions");
        for (auto& a : t.actions) {
            w.begin_object().field("cmd", a.cmd);
            if (!a.when_key.empty()) w.field("when", a.when_key + "=" + a.when_value);
            w.end_object();
        }
        w.end_array();
        w.begin_array("next_forms");
        for (auto& nx : t.next) w.begin_object().field("form", nx).end_object();
        w.end_array();
        w.end_object();
        return w.str();
    }

    // form_check and form_submit: the values are the request's other keys.
    std::map<std::string, std::string> vals;
    for (auto& f : t.fields)
        if (req.count(f.key)) vals[f.key] = req[f.key];
    auto problems = check_values(t, ctx, vals);

    if (cmd == "form_check") {
        JsonWriter w;
        w.begin_object().field("ok", true).field("form", t.id).field("ready", problems.empty());
        write_problems(w, problems);
        w.end_object();
        return w.str();
    }

    // form_submit
    if (!problems.empty()) {
        JsonWriter w;
        w.begin_object().field("ok", false).field("form", t.id).field("error", "some fields need a look");
        write_problems(w, problems);
        w.end_object();
        return w.str();
    }
    const Action* act = nullptr;
    for (auto& a : t.actions)
        if (a.when_key.empty() || upper(vals[a.when_key]) == a.when_value) {
            act = &a;
            break;
        }
    if (!act) return err_reply("this form has nothing to do for those answers");
    if (!ctx.run) return err_reply("forms can't run here");
    JsonWriter rq;
    rq.begin_object().field("cmd", act->cmd);
    std::set<std::string> done = {"cmd"};
    for (auto& [k, v] : t.sets)
        if (done.insert(k).second) rq.field(k, v);
    for (auto& f : t.fields) {
        if (!f.send || !f.again.empty()) continue;
        if (!act->when_key.empty() && f.key == act->when_key) continue;
        const std::string& v = vals[f.key];
        if (v.empty()) continue;
        std::string k = f.as.empty() ? f.key : f.as;
        if (done.insert(k).second) rq.field(k, v);
    }
    rq.end_object();
    std::string line = rq.str();
    std::string raw = ctx.run(line);
    // Secrets (a frequency, a passphrase) don't stay about in memory.
    auto wipe = [](std::string& x) {
        volatile char* p = &x[0];
        for (size_t i = 0; i < x.size(); ++i) p[i] = 0;
    };
    wipe(line);
    for (auto& f : t.fields)
        if (f.secret) {
            if (!vals[f.key].empty()) wipe(vals[f.key]);
            if (req.count(f.key) && !req[f.key].empty()) wipe(req[f.key]);
        }
    JsonValue r;
    bool good = parse_json(raw, r) && r.get("ok") && r.get("ok")->type == JsonValue::Type::Bool && r.get("ok")->b;
    // A send that reached some stations and not others names the ones it
    // missed, so the member doesn't take the form's ok for all of them.
    std::string missed;
    if (const JsonValue* fl = r.get("failed"); fl && fl->is_array())
        for (auto& e : fl->arr)
            if (e.type == JsonValue::Type::String) missed += (missed.empty() ? "" : "; ") + e.s;
    JsonWriter w;
    w.begin_object().field("ok", good).field("form", t.id).field("action", act->cmd);
    if (!good) {
        const JsonValue* er = r.get("error");
        w.field("error", er && er->type == JsonValue::Type::String ? er->s : std::string("the call failed"));
    } else if (!missed.empty()) {
        w.field("not_done", missed);
    }
    const JsonValue* nx = r.get("next");
    if (nx && nx->type == JsonValue::Type::String) w.field("next", nx->s);
    w.begin_array("next_forms");
    if (good)
        for (auto& f : t.next)
            if (allowed(ctx, f) && lib.get(f)) w.begin_object().field("form", f).end_object();
    w.end_array();
    w.end_object();
    std::string out = w.str();
    out.pop_back();
    out += ",\"result\":" + (raw.empty() ? std::string("null") : raw) + "}";
    return out;
}

// ---------------------------------------------------------------- packing

std::vector<std::string> pack(const std::string& text) {
    uLongf n = compressBound(text.size());
    std::string z(n, '\0');
    if (compress2(reinterpret_cast<Bytef*>(&z[0]), &n, reinterpret_cast<const Bytef*>(text.data()), text.size(), 9) != Z_OK)
        return {};
    z.resize(n);
    uint32_t len = static_cast<uint32_t>(text.size());
    std::string head = {static_cast<char>(len >> 24), static_cast<char>(len >> 16), static_cast<char>(len >> 8),
                        static_cast<char>(len)};
    std::string all = b32(head + z);
    std::vector<std::string> out;
    for (size_t i = 0; i < all.size(); i += 56) out.push_back(all.substr(i, 56));
    return out;
}

bool unpack(const std::vector<std::string>& lines, std::string& text) {
    std::string all, bytes;
    for (auto& l : lines) all += trim(l);
    if (!unb32(all, bytes) || bytes.size() < 4) return false;
    uLongf len = (static_cast<uint32_t>(static_cast<unsigned char>(bytes[0])) << 24) |
                 (static_cast<uint32_t>(static_cast<unsigned char>(bytes[1])) << 16) |
                 (static_cast<uint32_t>(static_cast<unsigned char>(bytes[2])) << 8) |
                 static_cast<uint32_t>(static_cast<unsigned char>(bytes[3]));
    if (len > kMaxTemplate) return false;
    text.assign(len, '\0');
    uLongf got = len;
    if (uncompress(reinterpret_cast<Bytef*>(&text[0]), &got, reinterpret_cast<const Bytef*>(bytes.data() + 4),
                   bytes.size() - 4) != Z_OK ||
        got != len)
        return false;
    return true;
}

}  // namespace forms
}  // namespace pna
