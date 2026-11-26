#include "channels.h"

#include "compose.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace pna {

namespace {

const char* kHead = "PNA CHANNEL PLAN 1";

std::string up(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string clean(std::string s) {
    for (auto& c : s)
        if (c == '|' || c == '\n' || c == '\r') c = ' ';
    return up(s);
}

}  // namespace

bool valid_designator(const std::string& s) {
    if (s.empty() || s.size() > 12) return false;
    bool letter = false;
    for (char c : s) {
        if (std::isalpha(static_cast<unsigned char>(c))) letter = true;
        else if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') return false;
    }
    return letter && !compose::looks_like_frequency(s);
}

bool valid_channel_label(const std::string& s) {
    if (s.empty() || s.size() > 16) return false;
    bool letter = false;
    for (char c : s) {
        if (std::isalpha(static_cast<unsigned char>(c))) letter = true;
        else if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-' && c != '/' && c != '.' && c != '_')
            return false;
    }
    return letter && !compose::looks_like_frequency(s);
}

int64_t parse_frequency(const std::string& raw, const std::string& unit_hint) {
    std::string s = up(raw), num, unit;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') num += c;
        else if (c == ',' && unit.empty()) continue;  // 7,345 as thousands
        else if (std::isalpha(static_cast<unsigned char>(c))) unit += c;
        else if (!num.empty() && c != ' ') break;
    }
    if (num.empty() || std::count(num.begin(), num.end(), '.') > 1) return 0;
    double v = std::strtod(num.c_str(), nullptr);
    std::string u = unit.empty() ? up(unit_hint) : unit;
    double hz;
    if (u == "MHZ" || u == "MC") hz = v * 1e6;
    else if (u == "KHZ" || u == "KC") hz = v * 1e3;
    else if (u == "HZ") hz = v;
    else if (v < 1000) hz = v * 1e6;          // 7.3455: MHz
    else if (v < 1000000) hz = v * 1e3;       // 7345.5: kHz
    else hz = v;                              // 7345500: Hz
    if (hz < 100e3 || hz > 3e9) return 0;
    return static_cast<int64_t>(std::llround(hz));
}

ChannelPlan::ChannelPlan(std::string path) : path_(std::move(path)) {}

bool ChannelPlan::load(std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    ch_.clear();
    std::ifstream f(path_);
    if (!f) return true;  // none yet
    std::string line;
    if (!std::getline(f, line) || line != kHead) {
        if (error) *error = "the channel plan file isn't one";
        return false;
    }
    while (std::getline(f, line)) {
        std::vector<std::string> p;
        std::string cur;
        for (char c : line + "|") {
            if (c == '|') p.push_back(cur), cur.clear();
            else cur += c;
        }
        if (p.size() < 5) continue;
        Channel c{p[0], std::atoll(p[1].c_str()), p[2], p[3], p[4]};
        // Antenna ports came later: a record without them is 1 and 1.
        c.ant_rx = p.size() > 5 && p[5] == "2" ? 2 : 1;
        c.ant_tx = p.size() > 6 && p[6] == "2" ? 2 : 1;
        // The world and HFLink label came later still: mars and none.
        c.world = p.size() > 7 && p[7] == "amateur" ? "amateur" : "mars";
        c.label = p.size() > 8 && valid_channel_label(p[8]) ? p[8] : "";
        if (valid_designator(c.name) && c.hz > 0) ch_.push_back(c);
    }
    return true;
}

bool ChannelPlan::save_locked(std::string* error) {
    std::string tmp = path_ + ".new";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        if (error) *error = "cannot write " + tmp;
        return false;
    }
    std::string out = std::string(kHead) + "\n";
    for (auto& c : ch_)
        out += c.name + "|" + std::to_string(c.hz) + "|" + c.mode + "|" + c.use + "|" + c.source + "|" +
               std::to_string(c.ant_rx == 2 ? 2 : 1) + "|" + std::to_string(c.ant_tx == 2 ? 2 : 1) + "|" +
               (c.world == "amateur" ? "amateur" : "mars") + "|" + c.label + "\n";
    bool ok = ::write(fd, out.data(), out.size()) == static_cast<ssize_t>(out.size()) && ::fsync(fd) == 0;
    ::fchmod(fd, 0600);
    ::close(fd);
    if (!ok || std::rename(tmp.c_str(), path_.c_str()) != 0) {
        ::unlink(tmp.c_str());
        if (error) *error = "cannot write " + path_;
        return false;
    }
    return true;
}

static bool check(Channel& c, std::string* error) {
    c.name = clean(c.name);
    c.mode = clean(c.mode);
    c.use = clean(c.use);
    c.source = clean(c.source);
    if (!valid_designator(c.name)) {
        if (error) *error = "designator: 1 to 12 letters, digits or -, with a letter (CH01, P1)";
        return false;
    }
    if (c.hz < 100000 || c.hz > 3000000000LL) {
        if (error) *error = "channel " + c.name + ": no frequency it could read";
        return false;
    }
    if (compose::looks_like_frequency(c.use)) c.use.clear();
    if (c.use.size() > 40) c.use.resize(40);
    if (c.mode.size() > 12) c.mode.resize(12);
    if (c.ant_rx < 0 || c.ant_rx > 2 || c.ant_tx < 0 || c.ant_tx > 2) {
        if (error) *error = "channel " + c.name + ": the tuner antenna is 1 or 2";
        return false;
    }
    for (auto& ch : c.world) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (!c.world.empty() && c.world != "mars" && c.world != "amateur") {
        if (error) *error = "channel " + c.name + ": world is mars or amateur";
        return false;
    }
    c.label = clean(c.label);
    if (!c.label.empty() && !valid_channel_label(c.label)) {
        if (error) *error = "channel " + c.name + ": the HFLink label is 1 to 16 letters, digits or - / . _, never a frequency";
        return false;
    }
    return true;
}

// An antenna port not given (0) keeps what the plan held, or 1.
// So are a world not given ("": what the plan held, or mars) and a label
// not given.
static void keep_antennas(Channel& c, const Channel* old) {
    if (c.ant_rx == 0) c.ant_rx = old ? old->ant_rx : 1;
    if (c.ant_tx == 0) c.ant_tx = old ? old->ant_tx : 1;
    if (c.world.empty()) c.world = old ? old->world : "mars";
    if (!c.label_given) c.label = old ? old->label : "";
    c.label_given = false;
}

bool ChannelPlan::put(const Channel& in, std::string* error) {
    Channel c = in;
    if (!check(c, error)) return false;
    std::lock_guard<std::mutex> g(mu_);
    auto it = std::find_if(ch_.begin(), ch_.end(), [&](const Channel& x) { return x.name == c.name; });
    keep_antennas(c, it != ch_.end() ? &*it : nullptr);
    if (it != ch_.end()) *it = c;
    else ch_.push_back(c);
    return save_locked(error);
}

bool ChannelPlan::put_all(const std::vector<Channel>& cs, std::vector<std::string>* errors) {
    std::lock_guard<std::mutex> g(mu_);
    for (auto c : cs) {
        std::string e;
        if (!check(c, &e)) {
            if (errors) errors->push_back(e);
            continue;
        }
        auto it = std::find_if(ch_.begin(), ch_.end(), [&](const Channel& x) { return x.name == c.name; });
        keep_antennas(c, it != ch_.end() ? &*it : nullptr);
        if (it != ch_.end()) *it = c;
        else ch_.push_back(c);
    }
    std::string e;
    if (!save_locked(&e)) {
        if (errors) errors->push_back(e);
        return false;
    }
    return true;
}

bool ChannelPlan::remove(const std::string& name) {
    std::lock_guard<std::mutex> g(mu_);
    std::string n = clean(name);
    auto it = std::find_if(ch_.begin(), ch_.end(), [&](const Channel& x) { return x.name == n; });
    if (it == ch_.end()) return false;
    ch_.erase(it);
    return save_locked(nullptr);
}

std::vector<Channel> ChannelPlan::list() {
    std::lock_guard<std::mutex> g(mu_);
    auto v = ch_;
    std::sort(v.begin(), v.end(), [](const Channel& a, const Channel& b) { return a.name < b.name; });
    return v;
}

std::optional<Channel> ChannelPlan::get(const std::string& name) {
    std::lock_guard<std::mutex> g(mu_);
    std::string n = clean(name);
    for (auto& c : ch_)
        if (c.name == n) return c;
    return std::nullopt;
}

std::string ChannelPlan::designator_for(int64_t hz) {
    std::lock_guard<std::mutex> g(mu_);
    for (auto& c : ch_)
        if (c.world != "amateur" && std::llabs(c.hz - hz) <= 50) return c.name;
    return "";
}

std::string ChannelPlan::display_name_for(int64_t hz) {
    std::lock_guard<std::mutex> g(mu_);
    std::string amateur;
    for (auto& c : ch_) {
        if (std::llabs(c.hz - hz) > 50) continue;
        if (c.world != "amateur") return c.name;
        if (amateur.empty()) amateur = c.label.empty() ? c.name : c.label;
    }
    return amateur;
}

std::string ChannelPlan::service_for(int64_t hz) {
    std::lock_guard<std::mutex> g(mu_);
    std::string svc;
    for (auto& c : ch_) {
        if (std::llabs(c.hz - hz) > 50) continue;
        if (!svc.empty() && svc != c.world) return "";
        svc = c.world;
    }
    return svc;
}

size_t ChannelPlan::size() {
    std::lock_guard<std::mutex> g(mu_);
    return ch_.size();
}

}  // namespace pna
