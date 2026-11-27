#include "services.h"

#include "json.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <sys/stat.h>

namespace pna {

namespace fs = std::filesystem;

namespace {

// The column kinds of docs/services.md, "The columns".
enum class K { Bool, Num, Str, StrOrNull, StrList, Ranges, RangesOrNull, Sideband, StationId };

struct Col {
    const char* key;
    K kind;
};

const std::vector<std::pair<const char*, std::vector<Col>>>& groups() {
    static const std::vector<std::pair<const char*, std::vector<Col>>> g = {
        {"radio",
         {{"bands_hz", K::RangesOrNull},
          {"data_segments_hz", K::RangesOrNull},
          {"auto_segments_hz", K::RangesOrNull},
          {"default_mode", K::Str},
          {"default_sideband", K::Sideband},
          {"passband_hz", K::Num},
          {"waveforms", K::StrList}}},
        {"keying",
         {{"station_id", K::StationId},
          {"identity_file", K::StrOrNull},
          {"call_pattern", K::StrOrNull},
          {"listen_only_without_call", K::Bool},
          {"max_key_s", K::Num},
          {"keys_per_hour", K::Num}}},
        {"priority", {{"radio", K::Num}, {"operator", K::Num}, {"automatic", K::Num}}},
        {"messages",
         {{"formats", K::StrList},
          {"encryption", K::Bool},
          {"key_source", K::StrOrNull},
          {"acks", K::Str},
          {"automatic_modes", K::StrList}}},
        {"receive",
         {{"scan_plan", K::StrOrNull},
          {"schedule_source", K::StrOrNull},
          {"park_channel", K::StrOrNull},
          {"overflow_engine", K::Bool},
          {"trim", K::StrOrNull}}},
        {"data",
         {{"word_log", K::Str}, {"captures", K::Str}, {"spool", K::Str}, {"store", K::Str}, {"fallback", K::Str}}},
    };
    return g;
}

const std::vector<Col>& station_id_cols() {
    static const std::vector<Col> c = {{"interval_s", K::Num}, {"margin_s", K::Num}, {"form", K::Str},
                                       {"wpm", K::Num},        {"tone_hz", K::Num},  {"level_dbfs", K::Num},
                                       {"gap_s", K::Num},      {"counts", K::StrList}};
    return c;
}

bool is_str_list(const JsonValue& v) {
    if (!v.is_array()) return false;
    for (auto& e : v.arr)
        if (e.type != JsonValue::Type::String) return false;
    return true;
}

// Whole Hz, [low, high], low <= high, within HF (1.5 to 30 MHz).
bool is_ranges(const JsonValue& v) {
    if (!v.is_array()) return false;
    for (auto& r : v.arr) {
        if (!r.is_array() || r.arr.size() != 2) return false;
        for (auto& x : r.arr)
            if (x.type != JsonValue::Type::Number || x.n != std::floor(x.n) || x.n < 1500000 || x.n > 30000000)
                return false;
        if (r.arr[0].n > r.arr[1].n) return false;
    }
    return true;
}

bool check_cols(const JsonValue& obj, const std::vector<Col>& cols, const std::string& where, std::string* why);

bool check_one(const JsonValue& v, K kind, const std::string& name, std::string* why) {
    bool ok = false;
    switch (kind) {
    case K::Bool: ok = v.type == JsonValue::Type::Bool; break;
    case K::Num: ok = v.type == JsonValue::Type::Number; break;
    case K::Str: ok = v.type == JsonValue::Type::String; break;
    case K::StrOrNull: ok = v.type == JsonValue::Type::String || v.type == JsonValue::Type::Null; break;
    case K::StrList: ok = is_str_list(v); break;
    case K::Ranges: ok = is_ranges(v); break;
    case K::RangesOrNull: ok = v.type == JsonValue::Type::Null || is_ranges(v); break;
    case K::Sideband: ok = v.type == JsonValue::Type::String && (v.s == "usb" || v.s == "lsb"); break;
    case K::StationId:
        if (v.type == JsonValue::Type::Null) return true;
        if (!v.is_object()) break;
        return check_cols(v, station_id_cols(), name, why);
    }
    if (!ok && why) *why = name + " is not of its type";
    return ok;
}

bool check_cols(const JsonValue& obj, const std::vector<Col>& cols, const std::string& where, std::string* why) {
    std::set<std::string> known;
    for (auto& c : cols) {
        known.insert(c.key);
        const JsonValue* v = obj.get(c.key);
        const std::string name = where + "." + c.key;
        if (!v) {
            if (why) *why = "missing column " + name;
            return false;
        }
        if (!check_one(*v, c.kind, name, why)) return false;
    }
    for (auto& [k, _] : obj.obj)
        if (!known.count(k)) {
            if (why) *why = "unknown key " + where + "." + (valid_service_name(k) ? k : std::string("(a key)"));
            return false;
        }
    return true;
}

std::vector<std::string> strs(const JsonValue& v) {
    std::vector<std::string> out;
    for (auto& e : v.arr) out.push_back(e.s);
    return out;
}

std::string str_or_empty(const JsonValue* v) {
    return v && v->type == JsonValue::Type::String ? v->s : std::string();
}

}  // namespace

bool Service::offers(const std::string& format) const {
    return std::find(formats.begin(), formats.end(), format) != formats.end();
}

std::string no_table(const std::string& name) {
    if (name.empty()) return "no service named";
    return "no table for service `" + (valid_service_name(name) ? name : std::string("(not a service name)")) + "`";
}

bool valid_service_name(const std::string& name) {
    static const std::regex re("^[a-z][a-z0-9-]{0,15}$");
    return name != "both" && std::regex_match(name, re);
}

bool parse_service_table(const std::string& stem, const std::string& text, Service& out, std::string* why) {
    auto fail = [&](const std::string& w) {
        if (why) *why = w;
        return false;
    };
    if (!valid_service_name(stem)) return fail("the file's name is not a service name");
    JsonValue v;
    if (!parse_json(text, v) || !v.is_object()) return fail("not one JSON object");
    const JsonValue* ver = v.get("version");
    if (!ver || ver->type != JsonValue::Type::Number || ver->n != 1) return fail("version is not 1");
    const JsonValue* sv = v.get("service");
    if (!sv || sv->type != JsonValue::Type::String) return fail("missing column service");
    if (sv->s != stem) return fail("service does not equal the file's name");
    std::vector<Col> top = {{"version", K::Num}, {"service", K::Str}, {"display_name", K::Str},
                            {"worked", K::Bool}, {"transmit", K::Bool}};
    std::set<std::string> known;
    for (auto& c : top) {
        known.insert(c.key);
        const JsonValue* x = v.get(c.key);
        if (!x) return fail(std::string("missing column ") + c.key);
        if (!check_one(*x, c.kind, c.key, why)) return false;
    }
    for (auto& [g, cols] : groups()) {
        known.insert(g);
        const JsonValue* x = v.get(g);
        if (!x) return fail(std::string("missing group ") + g);
        if (!x->is_object()) return fail(std::string(g) + " is not an object");
        if (!check_cols(*x, cols, g, why)) return false;
    }
    for (auto& [k, _] : v.obj)
        if (!known.count(k)) return fail("unknown key " + (valid_service_name(k) ? k : std::string("(a key)")));

    const JsonValue& keying = *v.get("keying");
    const JsonValue& msgs = *v.get("messages");
    Service s;
    s.name = stem;
    s.display_name = v.get("display_name")->s;
    s.worked = v.get("worked")->b;
    s.transmit = v.get("transmit")->b;
    s.formats = strs(*msgs.get("formats"));
    s.encryption = msgs.get("encryption")->b;
    s.automatic_modes = strs(*msgs.get("automatic_modes"));
    s.station_id = keying.get("station_id")->type != JsonValue::Type::Null;
    s.identity_file = str_or_empty(keying.get("identity_file"));
    s.call_pattern = str_or_empty(keying.get("call_pattern"));
    s.listen_only_without_call = keying.get("listen_only_without_call")->b;
    if (!s.call_pattern.empty()) {
        try {
            std::regex re(s.call_pattern);
        } catch (const std::regex_error&) {
            return fail("keying.call_pattern is not a regular expression");
        }
    }
    out = s;
    return true;
}

std::shared_ptr<Services> Services::from_texts(const std::map<std::string, std::string>& files,
                                               std::vector<std::string>* refused) {
    auto out = std::make_shared<Services>();
    out->unlocked_ = true;  // never read from root's files
    for (auto& [stem, text] : files) {
        Service s;
        std::string why;
        if (parse_service_table(stem, text, s, &why)) out->by_name_[s.name] = s;
        else if (refused) refused->push_back(stem + ".json refused: " + why);
    }
    return out;
}

std::shared_ptr<Services> Services::load(const std::string& dir, const Options& opt,
                                         std::vector<std::string>* refused) {
    std::map<std::string, std::string> texts;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        if (refused) refused->push_back("no service tables: " + dir + " is not a directory");
        auto none = std::make_shared<Services>();
        none->dir_ = dir;
        none->unlocked_ = !opt.require_root;
        return none;
    }
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (auto& p : files) {
        const std::string name = p.filename().string();
        struct stat sb {};
        if (::lstat(p.c_str(), &sb) != 0 || !S_ISREG(sb.st_mode)) {
            if (refused) refused->push_back(name + " refused: not a regular file");
            continue;
        }
        if (sb.st_mode & (S_IWGRP | S_IWOTH)) {
            if (refused) refused->push_back(name + " refused: writable by others than its owner");
            continue;
        }
        if (opt.require_root && sb.st_uid != 0) {
            if (refused) refused->push_back(name + " refused: not owned by root");
            continue;
        }
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        texts[p.stem().string()] = ss.str();
    }
    auto out = from_texts(texts, refused);
    out->dir_ = dir;
    out->unlocked_ = !opt.require_root;
    return out;
}

const Service* Services::find(const std::string& name) const {
    auto it = by_name_.find(name);
    return it == by_name_.end() ? nullptr : &it->second;
}

std::vector<const Service*> Services::all() const {
    std::vector<const Service*> out;
    for (auto& [_, s] : by_name_) out.push_back(&s);
    return out;
}

bool Services::listen_only(const Service& s, const std::string& local_dir) const {
    if (!s.listen_only_without_call) return false;
    if (s.identity_file.empty()) return true;
    std::ifstream f(fs::path(local_dir.empty() ? "." : local_dir) / s.identity_file);
    if (!f) return true;
    std::regex re;
    try {
        re = std::regex(s.call_pattern.empty() ? std::string(".+") : s.call_pattern);
    } catch (const std::regex_error&) {
        return true;
    }
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos || line[a] == '#') continue;
        std::string call = line.substr(a);
        call = call.substr(0, call.find_first_of(" \t"));
        if (std::regex_match(call, re)) return false;
    }
    return true;
}

}  // namespace pna
