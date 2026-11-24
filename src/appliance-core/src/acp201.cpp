// See acp201.h.
#include "acp201.h"

#include <cstdio>
#include <ctime>
#include <sstream>

namespace pna {
namespace acp201 {

namespace {

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::string upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

bool valid_station(const std::string& s) {
    if (s.empty() || s.size() > 16) return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

bool is_prec(char c) { return c == 'Z' || c == 'O' || c == 'P' || c == 'R'; }

// "QSL _R_UHSCTZ #0025 3051616/AAR4XR" or "... 3082236/  AAR4XR AAR4XR".
bool read_receipt(const std::string& text, Chat& c) {
    auto t = words(text);
    if (t.size() < 3 || t[0] != "QSL") return false;
    const std::string& p = t[1];
    if (p.size() < 4 || p[0] != '_' || p[2] != '_' || !is_prec(p[1])) return false;
    if (t[2].size() < 2 || t[2][0] != '#' || !all_digits(t[2].substr(1))) return false;
    if (t.size() < 4) return false;
    size_t slash = t[3].find('/');
    std::string when = slash == std::string::npos ? t[3] : t[3].substr(0, slash);
    if (when.size() != 7 || !all_digits(when)) return false;
    c.prec = p[1];
    c.receipt_of = p.substr(3);
    c.receipt_serial = t[2].substr(1);
    c.receipt_time = when;
    if (slash != std::string::npos && slash + 1 < t[3].size()) c.receipt_by.push_back(t[3].substr(slash + 1));
    for (size_t i = 4; i < t.size(); ++i) c.receipt_by.push_back(t[i]);
    return true;
}

void classify(Chat& c) {
    std::string u = upper(c.text);
    auto w = words(u);
    if (u == "VER") {
        c.kind = Kind::Ver;
    } else if (u == "QRU U,A" || u == "QRU U,A [COMP]") {
        c.kind = Kind::QruUA;
        c.comp = u.size() > 7;
    } else if (w.size() == 2 && (w[0] == "ZEE" || w[0] == "QSL") && all_digits(w[1]) && w[1].size() <= 4) {
        c.kind = w[0] == "ZEE" ? Kind::Zee : Kind::Qsl;
        c.number = std::stoi(w[1]);
    } else if (u == "TRAFFIC COMPLETE") {
        c.kind = Kind::TrafficComplete;
    } else if (w.size() >= 2 && w[0] == "SQL") {
        c.kind = Kind::SqlQuery;
        c.warnings.push_back("remote SQL query; read only, never run");
    } else if (read_receipt(c.text, c)) {
        c.kind = Kind::Receipt;
    } else {
        c.kind = Kind::Text;
    }
}

// "K 24-0215Z", "AR 24-0215Z": CP's ending. Fills day, time and over.
bool cp_ending(const std::vector<std::string>& w, Chat& c) {
    if (w.size() < 2) return false;
    const std::string& t = w.back();
    const std::string& k = w[w.size() - 2];
    if (k != "K" && k != "AR") return false;
    if (t.size() != 8 || t[2] != '-' || t[7] != 'Z' || !all_digits(t.substr(0, 2)) || !all_digits(t.substr(3, 4)))
        return false;
    c.day = std::stoi(t.substr(0, 2));
    c.hour = std::stoi(t.substr(3, 2));
    c.minute = std::stoi(t.substr(5, 2));
    c.zulu = true;
    c.over = k == "AR";
    for (size_t i = 0; i + 2 < w.size(); ++i) c.instructions += (i ? " " : "") + w[i];
    return c.day >= 1 && c.day <= 31;
}

}  // namespace

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Text: return "text";
        case Kind::Ver: return "ver";
        case Kind::QruUA: return "qru";
        case Kind::Zee: return "zee";
        case Kind::Qsl: return "qsl";
        case Kind::TrafficComplete: return "traffic-complete";
        case Kind::Receipt: return "receipt";
        case Kind::SqlQuery: return "sql-query";
    }
    return "";
}

bool parse(const std::string& raw, Chat& c, std::string* error) {
    c = Chat();
    std::string line;
    std::string body = raw;
    {
        size_t a = body.find_first_not_of(" \t\r\n");
        if (a != std::string::npos && body.compare(a, 5, "chat|") == 0) {
            size_t bar = body.find('|', a + 5);
            if (bar == std::string::npos || bar == a + 5) return fail(error, "chat: no ID after \"chat|\"");
            std::string id = body.substr(a + 5, bar - a - 5);
            for (char ch : id)
                if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'))
                    return fail(error, "chat: the ID must be letters, digits and -");
            // CP's text runs over lines: read it as one.
            std::string joined;
            std::istringstream in(body.substr(bar + 1));
            for (std::string l; std::getline(in, l);) {
                l = trim(l);
                if (!l.empty()) joined += (joined.empty() ? "" : " ") + l;
            }
            Chat inner;
            if (!parse(joined, inner, error)) return false;
            c = inner;
            c.cp_id = id;
            return true;
        }
    }
    {
        std::istringstream in(raw);
        size_t lines = 0;
        for (std::string l; std::getline(in, l);) {
            if (trim(l).empty()) continue;
            if (++lines == 1) line = trim(l);
        }
        if (lines > 1) c.warnings.push_back("lines after the first ignored");
    }
    if (line.empty()) return fail(error, "chat: empty");
    size_t first = line.find("//");
    if (first == std::string::npos) return fail(error, "chat: no \"//\" after the call-up");
    size_t last = line.rfind("//");
    if (last == first) return fail(error, "chat: only one \"//\"; the time needs a second");

    auto head = words(line.substr(0, first));
    size_t de = head.size();
    for (size_t i = 0; i < head.size(); ++i)
        if (head[i] == "DE") de = i;
    if (de == head.size() || de == 0) return fail(error, "chat: no \"<to> DE <from>\" before the first \"//\"");
    if (de + 2 != head.size()) return fail(error, "chat: one calling station after DE");
    for (size_t i = 0; i < de; ++i) c.to += (i ? " " : "") + head[i];
    c.from = head[de + 1];
    if (upper(c.to) != c.to || upper(c.from) != c.from) c.warnings.push_back("lower-case station");

    c.text = trim(line.substr(first + 2, last - first - 2));

    // CP's "// <instructions> K DD-HHMMZ".
    if (cp_ending(words(line.substr(last + 2)), c)) {
        if (c.hour > 23 || c.minute > 59) return fail(error, "chat: time out of range");
        classify(c);
        if (error) error->clear();
        return true;
    }
    // "2333Z K", "2001Z K", "2339K".
    std::string tail;
    for (char ch : line.substr(last + 2))
        if (ch != ' ') tail += ch;
    if (tail.size() < 5 || tail.back() != 'K' || !all_digits(tail.substr(0, 4)) ||
        (tail.size() == 6 && tail[4] != 'Z') || tail.size() > 6)
        return fail(error, "chat: no time and K after the last \"//\"");
    c.hour = std::stoi(tail.substr(0, 2));
    c.minute = std::stoi(tail.substr(2, 2));
    c.zulu = tail.size() == 6;
    if (c.hour > 23 || c.minute > 59) return fail(error, "chat: time out of range");
    if (!c.zulu) c.warnings.push_back("time without Z");
    classify(c);
    if (error) error->clear();
    return true;
}

bool looks_like_chat(const std::string& raw) {
    if (raw.find("VZCZC") != std::string::npos) return false;
    Chat c;
    return parse(raw, c, nullptr);
}

std::string build(const Chat& c, std::string* error) {
    auto bad = [&](const std::string& why) {
        if (error) *error = why;
        return std::string();
    };
    for (auto& t : words(c.to))
        if (!valid_station(t)) return bad("chat: station to call must be A-Z, 0-9 and -");
    if (c.to.empty()) return bad("chat: station to call must be A-Z, 0-9 and -");
    if (!valid_station(c.from)) return bad("chat: calling station must be A-Z, 0-9 and -");
    if (c.hour < 0 || c.hour > 23 || c.minute < 0 || c.minute > 59) return bad("chat: time out of range");
    char hhmm[8];
    std::snprintf(hhmm, sizeof hhmm, "%02d%02d", c.hour, c.minute);

    if (c.kind == Kind::SqlQuery) return bad("chat: the appliance sends no SQL queries");
    if (c.kind == Kind::Receipt) {
        if (!is_prec(c.prec)) return bad("receipt: precedence must be Z, O, P or R");
        if (!valid_station(c.receipt_of)) return bad("receipt: originator must be A-Z, 0-9 and -");
        if (c.receipt_serial.empty() || c.receipt_serial.size() > 4 || !all_digits(c.receipt_serial))
            return bad("receipt: serial must be 1 to 4 digits");
        if (c.receipt_time.size() != 7 || !all_digits(c.receipt_time))
            return bad("receipt: time filed must be DDDHHMM");
        if (c.receipt_by.empty()) return bad("receipt: at least one receiving station");
        std::string by;
        for (auto& b : c.receipt_by) {
            if (!valid_station(b)) return bad("receipt: receiving station must be A-Z, 0-9 and -");
            by += (by.empty() ? "" : " ") + b;
        }
        std::string serial = std::string(4 - c.receipt_serial.size(), '0') + c.receipt_serial;
        if (error) error->clear();
        return c.to + " DE " + c.from + "//QSL _" + c.prec + "_" + c.receipt_of + " #" + serial + " " +
               c.receipt_time + "/" + by + "  //" + hhmm + "K";
    }

    std::string text = c.text;
    if (text.empty()) {
        switch (c.kind) {
            case Kind::Ver: text = "VER"; break;
            case Kind::QruUA: text = c.comp ? "QRU U,A [COMP]" : "QRU U,A"; break;
            case Kind::Zee:
            case Kind::Qsl:
                if (c.number < 0 || c.number > 9999) return bad("chat: message number must be 0 to 9999");
                text = std::string(c.kind == Kind::Zee ? "ZEE " : "QSL ") + std::to_string(c.number);
                break;
            case Kind::TrafficComplete: text = "TRAFFIC COMPLETE"; break;
            default: return bad("chat: no text");
        }
    }
    for (char ch : text)
        if (ch < ' ' || ch > '~') return bad("chat: text must be printable ASCII on one line");
    if (!c.cp_id.empty()) {
        for (char ch : c.cp_id)
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-'))
                return bad("chat: the ID must be A-Z, 0-9 and -");
        if (c.day < 1 || c.day > 31) return bad("chat: day out of range");
        for (char ch : c.instructions)
            if (ch < ' ' || ch > '~' || ch == '/') return bad("chat: instructions must be printable ASCII, no /");
        char end[16];
        std::snprintf(end, sizeof end, "%02d-%sZ", c.day, hhmm);
        if (error) error->clear();
        return "chat|" + c.cp_id + "|" + c.to + " DE " + c.from + " // \r\n\r\n" + text + "\r\n\r\n // " +
               (c.instructions.empty() ? "" : c.instructions + " ") + (c.over ? "AR " : "K ") + end;
    }
    if (error) error->clear();
    return c.to + " DE " + c.from + " // " + text + " // " + hhmm + (c.zulu ? "Z" : "") + " K";
}

bool parse_cp_reply(const std::string& raw, CpReply& out) {
    out = CpReply();
    std::string line = trim(raw);
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        size_t bar = line.find('|', start);
        parts.push_back(line.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    if (parts.size() < 2 || parts.size() > 3 || parts[1].empty()) return false;
    if (parts[0] == "ack") out.kind = CpReply::Kind::Ack;
    else if (parts[0] == "nack") out.kind = CpReply::Kind::Nack;
    else if (parts[0] == "canx") out.kind = CpReply::Kind::Canx;
    else return false;
    if (parts.size() == 3 && out.kind != CpReply::Kind::Ack) return false;
    for (auto& p : parts)
        for (char ch : p)
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'))
                return false;
    out.id = parts[1];
    if (parts.size() == 3) out.from = parts[2];
    return true;
}

std::string build_cp_reply(const CpReply& r) {
    const char* k = r.kind == CpReply::Kind::Ack ? "ack|" : r.kind == CpReply::Kind::Nack ? "nack|" : "canx|";
    return k + r.id + (r.from.empty() ? "" : "|" + r.from);
}

std::string cp_chat_id(const std::string& call, int64_t unix_ms) {
    std::time_t t = static_cast<std::time_t>(unix_ms / 1000);
    std::tm u{};
    gmtime_r(&t, &u);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d%02d%02d%02d%03d", u.tm_mday, u.tm_hour, u.tm_min, u.tm_sec,
                  static_cast<int>(unix_ms % 1000));
    return call + buf;
}

}  // namespace acp201
}  // namespace pna
