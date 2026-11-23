// The MSC message digest. See digest.h.
#include "digest.h"

#include "crypto.h"

#include <vector>

namespace pna {
namespace digest {

namespace {

std::vector<std::string> lines_of(const std::string& t) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : t) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && space(s[a])) ++a;
    while (b > a && space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Qt's QString::simplified(): whitespace runs become one space, ends trimmed.
std::string simplified(const std::string& s) {
    std::string o;
    bool gap = false;
    for (char c : s) {
        if (space(c)) {
            gap = !o.empty();
        } else {
            if (gap) o += ' ';
            gap = false;
            o += c;
        }
    }
    return o;
}

bool starts_with(const std::string& s, const char* p) { return s.compare(0, std::char_traits<char>::length(p), p) == 0; }

}  // namespace

std::string compute(const std::string& message) {
    std::string body;
    bool in = false, cls = false;
    for (const std::string& raw : lines_of(message)) {
        std::string line = trim(raw);
        if (line == "BT" || starts_with(line, "BT ")) {
            in = !in;
            continue;
        }
        if (!in) continue;
        if (!cls && starts_with(line, "UNCLAS")) {
            cls = true;
            continue;
        }
        body += line + "\n";
    }
    return crypto::md5_hex_upper(simplified(body));
}

std::string written(const std::string& message) {
    for (const std::string& raw : lines_of(message)) {
        std::string line = simplified(raw);
        if (starts_with(line, "[DIGEST:") && line.back() == ']') return line.substr(8, line.size() - 9);
    }
    return "";
}

const char* check_name(Check c) {
    switch (c) {
        case Check::Pass: return "pass";
        case Check::Fail: return "fail";
        default: return "none";
    }
}

Check check(const std::string& message) {
    std::string w = written(message);
    if (w.empty()) return Check::None;
    for (auto& c : w)
        if (c >= 'a' && c <= 'f') c = static_cast<char>(c - 'a' + 'A');
    return w == compute(message) ? Check::Pass : Check::Fail;
}

std::string append(const std::string& message) {
    if (!written(message).empty()) return message;
    size_t nnnn = message.rfind("NNNN");
    if (nnnn == std::string::npos) return message;
    // Two BT lines before NNNN, or there is no text between BTs to digest.
    int bts = 0;
    for (const std::string& raw : lines_of(message.substr(0, nnnn))) {
        std::string l = trim(raw);
        if (l == "BT" || starts_with(l, "BT ")) ++bts;
    }
    if (bts < 2) return message;
    // The end of the last line before NNNN, and the message's own line end.
    size_t p = nnnn;
    while (p > 0 && (message[p - 1] == '\r' || message[p - 1] == '\n')) --p;
    std::string eol = message.find("\r\r\n") != std::string::npos ? "\r\r\n"
                      : message.find("\r\n") != std::string::npos ? "\r\n"
                                                                  : "\n";
    return message.substr(0, p) + eol + "[DIGEST:" + compute(message) + "]" + message.substr(p);
}

}  // namespace digest
}  // namespace pna
