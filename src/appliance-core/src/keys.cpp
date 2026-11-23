// Key tapes and the station keyring. See keys.h and docs/crypto.md.
#include "keys.h"

#include "crypto.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace pna {
namespace keys {

namespace {

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> w;
    for (std::string t; in >> t;) w.push_back(t);
    return w;
}

bool valid_net(const std::string& s) {
    if (s.empty() || s.size() > 40 || s.front() == ' ' || s.back() == ' ') return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '/')) return false;
    return true;
}

bool valid_tape_id(const std::string& s) {
    if (s.size() != 3) return false;
    for (char c : s)
        if (c < 'A' || c > 'Z') return false;
    return true;
}

bool valid_indicator(const std::string& ind, const std::string& tape) {
    return ind.size() == 5 && ind.compare(0, 3, tape) == 0 && ind[3] >= '0' && ind[3] <= '9' && ind[4] >= '0' &&
           ind[4] <= '9' && ind.substr(3) != "00";
}

std::string two(int v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02d", v);
    return b;
}

// Lines of text, CRLF or LF, with '#' notes and blank lines dropped. Each
// kept line also carries where it starts, for the end check.
struct Line {
    std::string text;
    size_t start = 0;
};
std::vector<Line> lines_of(const std::string& t) {
    std::vector<Line> out;
    size_t i = 0;
    while (i <= t.size()) {
        size_t e = t.find('\n', i);
        if (e == std::string::npos) e = t.size();
        std::string l = t.substr(i, e - i);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t a = l.find_first_not_of(' ');
        if (a != std::string::npos && l[a] != '#') out.push_back({l, i});
        if (e == t.size()) break;
        i = e + 1;
    }
    return out;
}

// "key IND FROM UNTIL HEX" (a tape) or "key IND FROM UNTIL HEX TAPE NET..." (the keyring).
bool read_key_line(const std::vector<std::string>& w, Key& k, bool keyring, std::string* error) {
    if (w.size() < (keyring ? 7u : 5u)) return fail(error, "a key line is short");
    k.indicator = w[1];
    std::string secret;
    if (!parse_time(w[2], k.from) || !parse_time(w[3], k.until) || k.from >= k.until)
        return fail(error, "key " + k.indicator + ": bad start or end time");
    if (w[4].size() != 64 || !crypto::from_hex(w[4], secret)) return fail(error, "key " + k.indicator + ": the key is not 64 hex digits");
    k.secret = std::move(secret);
    if (keyring) {
        k.tape = w[5];
        k.net = w[6];
        for (size_t i = 7; i < w.size(); ++i) k.net += " " + w[i];
    }
    return true;
}

std::string key_line(const Key& k, bool keyring) {
    std::string l = "key " + k.indicator + " " + time_text(k.from) + " " + time_text(k.until) + " " +
                    crypto::to_hex(k.secret);
    if (keyring) l += " " + k.tape + " " + k.net;
    return l;
}

// Checks the "end SHA256" line: the hash of every byte before it.
bool check_end(const std::string& text, const std::vector<Line>& L, std::string* error) {
    if (L.empty()) return fail(error, "empty");
    auto w = split_ws(L.back().text);
    if (w.size() != 2 || w[0] != "end") return fail(error, "no end line: the file is cut off");
    // Over the text with plain line ends, so a copy that gained CRs still reads.
    std::string body;
    for (size_t i = 0; i < L.back().start; ++i)
        if (!(text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')) body += text[i];
    if (crypto::sha256_hex(body) != w[1])
        return fail(error, "the end check does not match: the file was changed or damaged");
    return true;
}

std::string with_end(const std::string& body) { return body + "end " + crypto::sha256_hex(body) + "\n"; }

bool write_all(int fd, const std::string& s) {
    size_t off = 0;
    while (off < s.size()) {
        ssize_t n = ::write(fd, s.data() + off, s.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

std::string read_file(const std::string& path, bool* exists) {
    *exists = false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "";
    *exists = true;
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

const char* kKeyringHead = "PNA STATION KEYS 1";

bool parse_keyring(const std::string& text, std::vector<Key>& out, std::string* error) {
    auto L = lines_of(text);
    if (L.empty() || L[0].text != kKeyringHead) return fail(error, "not a station key file");
    if (!check_end(text, L, error)) return false;
    for (size_t i = 1; i + 1 < L.size(); ++i) {
        auto w = split_ws(L[i].text);
        if (w.empty() || w[0] != "key") return fail(error, "unknown line in the station key file");
        Key k;
        if (!read_key_line(w, k, true, error)) return false;
        out.push_back(std::move(k));
    }
    return true;
}

void wipe_keys(std::vector<Key>& v) {
    for (auto& k : v) crypto::wipe(k.secret);
    v.clear();
}

}  // namespace

std::string time_text(int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm u{};
    gmtime_r(&tt, &u);
    return std::to_string(u.tm_year + 1900) + two(u.tm_mon + 1) + two(u.tm_mday) + two(u.tm_hour) + two(u.tm_min) + "Z";
}

bool parse_time(const std::string& s, int64_t& t) {
    if (s.size() != 13 || s[12] != 'Z') return false;
    for (int i = 0; i < 12; ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    std::tm u{};
    u.tm_year = std::stoi(s.substr(0, 4)) - 1900;
    u.tm_mon = std::stoi(s.substr(4, 2)) - 1;
    u.tm_mday = std::stoi(s.substr(6, 2));
    u.tm_hour = std::stoi(s.substr(8, 2));
    u.tm_min = std::stoi(s.substr(10, 2));
    if (u.tm_mon < 0 || u.tm_mon > 11 || u.tm_mday < 1 || u.tm_mday > 31 || u.tm_hour > 23 || u.tm_min > 59) return false;
    t = static_cast<int64_t>(timegm(&u));
    return time_text(t) == s;  // refuses 31 FEB and the like
}

bool make_tape(const std::string& net, int64_t start, int count, int period_hours, int64_t now, Tape& out,
               std::string* error) {
    if (!valid_net(net)) return fail(error, "net name: 1 to 40 of A-Z, 0-9, space, - and /");
    if (count < 1 || count > kMaxKeys) return fail(error, "a tape holds 1 to 99 keys");
    if (period_hours < 1 || period_hours > 24 * 31) return fail(error, "period: 1 hour to 31 days");
    if (start % 60) return fail(error, "the start time must be on the minute");
    out = Tape{};
    out.net = net;
    out.made = now - now % 60;
    out.period_hours = period_hours;
    // Three random letters, without bias (rejection sampling).
    while (out.tape.size() < 3) {
        unsigned char c = static_cast<unsigned char>(crypto::random_bytes(1)[0]);
        if (c < 26 * 9) out.tape += static_cast<char>('A' + c % 26);
    }
    for (int i = 0; i < count; ++i) {
        Key k;
        k.indicator = out.tape + two(i + 1);
        k.net = net;
        k.tape = out.tape;
        k.from = start + static_cast<int64_t>(i) * period_hours * 3600;
        k.until = k.from + static_cast<int64_t>(period_hours) * 3600;
        k.secret = crypto::random_bytes(32);
        out.keys.push_back(std::move(k));
    }
    return true;
}

std::string format_tape(const Tape& t) {
    std::string b = "PNA KEY TAPE " + std::to_string(kTapeVersion) + "\n";
    b += "# Key tape for one net. Handle it like the keys it holds: copy it only to the\n";
    b += "# stations of this net, and destroy every copy when its last key has ended.\n";
    b += "net " + t.net + "\n";
    b += "tape " + t.tape + "\n";
    b += "made " + time_text(t.made) + "\n";
    b += "period-hours " + std::to_string(t.period_hours) + "\n";
    for (auto& k : t.keys) b += key_line(k, false) + "\n";
    return with_end(b);
}

bool parse_tape(const std::string& text, Tape& out, std::string* error) {
    out = Tape{};
    auto L = lines_of(text);
    if (L.empty()) return fail(error, "not a key tape: it is empty");
    auto head = split_ws(L[0].text);
    if (head.size() != 4 || head[0] != "PNA" || head[1] != "KEY" || head[2] != "TAPE")
        return fail(error, "not a key tape: the first line must be PNA KEY TAPE 1");
    if (head[3] != std::to_string(kTapeVersion))
        return fail(error, "a key tape version " + head[3] + " needs a newer appliance; this one reads version 1");
    if (!check_end(text, L, error)) return false;
    for (size_t i = 1; i + 1 < L.size(); ++i) {
        const std::string& l = L[i].text;
        auto w = split_ws(l);
        if (w[0] == "net") {
            out.net = l.substr(l.find("net") + 4);
            while (!out.net.empty() && out.net.back() == ' ') out.net.pop_back();
        } else if (w[0] == "tape" && w.size() == 2) {
            out.tape = w[1];
        } else if (w[0] == "made" && w.size() == 2) {
            if (!parse_time(w[1], out.made)) return fail(error, "bad made time");
        } else if (w[0] == "period-hours" && w.size() == 2) {
            out.period_hours = std::atoi(w[1].c_str());
        } else if (w[0] == "key") {
            Key k;
            if (!read_key_line(w, k, false, error)) return false;
            out.keys.push_back(std::move(k));
        } else {
            return fail(error, "unknown line on the key tape: " + w[0]);
        }
    }
    if (!valid_net(out.net)) return fail(error, "the tape's net name is missing or not allowed");
    if (!valid_tape_id(out.tape)) return fail(error, "the tape's id must be three letters");
    if (out.keys.empty() || out.keys.size() > static_cast<size_t>(kMaxKeys)) return fail(error, "a tape holds 1 to 99 keys");
    for (size_t i = 0; i < out.keys.size(); ++i) {
        auto& k = out.keys[i];
        k.net = out.net;
        k.tape = out.tape;
        if (!valid_indicator(k.indicator, out.tape))
            return fail(error, "key indicator " + k.indicator + " must be the tape's letters and two digits");
        for (size_t j = 0; j < i; ++j)
            if (out.keys[j].indicator == k.indicator) return fail(error, "key " + k.indicator + " is on the tape twice");
    }
    return true;
}

// ---- Keyring

Keyring::Keyring(std::string path) : path_(std::move(path)) {}
Keyring::~Keyring() { wipe_keys(keys_); }

int64_t Keyring::now() const { return clock_ ? clock_() : static_cast<int64_t>(std::time(nullptr)); }

bool Keyring::load(std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    wipe_keys(keys_);
    bool exists = false;
    std::string text = read_file(path_, &exists);
    std::string err;
    if (exists && parse_keyring(text, keys_, &err)) {
        crypto::wipe(text);
        return true;
    }
    crypto::wipe(text);
    wipe_keys(keys_);
    // A save cut off between zeroing the old file and the rename leaves the
    // new file beside it: take it.
    bool tmp_exists = false;
    std::string tmp = read_file(path_ + ".new", &tmp_exists);
    std::string terr;
    if (tmp_exists && parse_keyring(tmp, keys_, &terr)) {
        crypto::wipe(tmp);
        if (std::rename((path_ + ".new").c_str(), path_.c_str()) != 0) return fail(error, "cannot rename " + path_ + ".new");
        return true;
    }
    crypto::wipe(tmp);
    wipe_keys(keys_);
    if (!exists) return true;  // no keys yet
    return fail(error, path_ + ": " + err);
}

bool Keyring::save_locked(std::string* error) {
    std::string body = std::string(kKeyringHead) + "\n";
    for (auto& k : keys_) body += key_line(k, true) + "\n";
    std::string text = with_end(body);
    crypto::wipe(body);
    std::string tmp = path_ + ".new";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        crypto::wipe(text);
        return fail(error, "cannot write " + tmp + ": " + std::strerror(errno));
    }
    bool ok = write_all(fd, text) && ::fsync(fd) == 0;
    ::close(fd);
    crypto::wipe(text);
    if (!ok) {
        ::unlink(tmp.c_str());
        return fail(error, "cannot write " + tmp);
    }
    // Zero the old file in place before the new one takes its name.
    int old = ::open(path_.c_str(), O_WRONLY | O_CLOEXEC);
    if (old >= 0) {
        struct stat st {};
        if (::fstat(old, &st) == 0 && st.st_size > 0) {
            std::string zeros(static_cast<size_t>(st.st_size), '\0');
            if (write_all(old, zeros)) ::fsync(old);
        }
        ::close(old);
    }
    if (std::rename(tmp.c_str(), path_.c_str()) != 0) return fail(error, "cannot rename " + tmp);
    std::string dir = path_.substr(0, path_.find_last_of('/') == std::string::npos ? 0 : path_.find_last_of('/'));
    int dfd = ::open(dir.empty() ? "." : dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
    return true;
}

bool Keyring::import_tape(const std::string& text, ImportResult& r, std::string* error) {
    Tape t;
    if (!parse_tape(text, t, error)) {
        wipe_keys(t.keys);
        return false;
    }
    r = ImportResult{};
    r.net = t.net;
    r.tape = t.tape;
    const int64_t n = now();
    std::lock_guard<std::mutex> g(mu_);
    for (auto& k : t.keys)
        for (auto& h : keys_)
            if (h.indicator == k.indicator && (h.secret != k.secret || h.net != k.net)) {
                wipe_keys(t.keys);
                return fail(error, "key " + k.indicator + " is already held from another tape: zeroize tape " + h.tape +
                                       " first if this one replaces it");
            }
    std::vector<Key> before = keys_;
    for (auto& k : t.keys) {
        if (k.until + kReceiveHold <= n) {
            ++r.expired;
            continue;
        }
        bool held = false;
        for (auto& h : keys_) held = held || h.indicator == k.indicator;
        if (held) {
            ++r.already;
            continue;
        }
        keys_.push_back(k);
        ++r.added;
    }
    wipe_keys(t.keys);
    std::sort(keys_.begin(), keys_.end(), [](const Key& a, const Key& b) {
        return a.from != b.from ? a.from < b.from : a.indicator < b.indicator;
    });
    if (r.added && !save_locked(error)) {
        wipe_keys(keys_);
        keys_ = before;
        wipe_keys(before);
        return false;
    }
    wipe_keys(before);
    return true;
}

std::string key_check(const std::string& indicator, const std::string& secret) {
    static const char* b32 = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::string d;
    crypto::from_hex(crypto::sha256_hex("PNA KEY CHECK 1|" + indicator + "|" + secret), d);
    uint32_t v = (static_cast<uint32_t>(static_cast<unsigned char>(d[0])) << 16) |
                 (static_cast<uint32_t>(static_cast<unsigned char>(d[1])) << 8) | static_cast<unsigned char>(d[2]);
    std::string o;
    for (int i = 3; i >= 0; --i) o += b32[(v >> (4 + 5 * i)) & 31];
    return o;
}

std::vector<Info> Keyring::list() {
    const int64_t n = now();
    std::lock_guard<std::mutex> g(mu_);
    std::vector<Info> out;
    for (auto& k : keys_) {
        Info i{k.indicator, k.net, k.tape, k.from, k.until, "", key_check(k.indicator, k.secret)};
        i.state = n < k.from ? "next" : n < k.until ? "current" : "receive only";
        out.push_back(i);
    }
    return out;
}

std::vector<Info> Keyring::for_sending() {
    std::vector<Info> out;
    for (auto& i : list())
        if (i.state == "current") out.push_back(i);
    return out;
}

bool Keyring::send_key(const std::string& indicator, std::string& secret, std::string* error) {
    const int64_t n = now();
    std::lock_guard<std::mutex> g(mu_);
    for (auto& k : keys_)
        if (k.indicator == indicator) {
            if (n < k.from) return fail(error, "key " + indicator + " starts at " + time_text(k.from));
            if (n >= k.until) return fail(error, "key " + indicator + " ended at " + time_text(k.until));
            secret = k.secret;
            return true;
        }
    return fail(error, "no key " + indicator + " is loaded");
}

bool Keyring::receive_key(const std::string& indicator, std::string& secret) {
    const int64_t n = now();
    std::lock_guard<std::mutex> g(mu_);
    for (auto& k : keys_)
        if (k.indicator == indicator && n < k.until + kReceiveHold && n >= k.from - kReceiveHold) {
            secret = k.secret;
            return true;
        }
    return false;
}

std::vector<std::string> Keyring::sweep() {
    const int64_t n = now();
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::string> lines;
    std::vector<Key> keep;
    for (auto& k : keys_) {
        if (k.until + kReceiveHold <= n) {
            lines.push_back("key " + k.indicator + " (" + k.net + ") ended " + time_text(k.until) + ": zeroized");
            crypto::wipe(k.secret);
        } else {
            keep.push_back(std::move(k));
        }
    }
    keys_ = std::move(keep);
    if (!lines.empty()) {
        std::string err;
        if (!save_locked(&err)) lines.push_back("could not rewrite the key file: " + err);
    }
    return lines;
}

int Keyring::zeroize(const std::string& which, std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<Key> keep;
    int n = 0;
    for (auto& k : keys_) {
        if (k.indicator == which || k.tape == which) {
            crypto::wipe(k.secret);
            ++n;
        } else {
            keep.push_back(std::move(k));
        }
    }
    keys_ = std::move(keep);
    if (n && !save_locked(error)) return -1;
    if (!n && error) *error = "no key or tape " + which + " is held";
    return n;
}

}  // namespace keys
}  // namespace pna
