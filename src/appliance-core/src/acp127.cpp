// ACP 127(G) message format. See acp127.h for the edition and the conventions.
#include "acp127.h"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace pna {
namespace acp127 {

namespace {

const char* kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
const char* kClassWord = "UNCLAS";  // 209.c: unclassified plain-language text begins UNCLAS

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

bool all_upper(const std::string& s) {
    for (char c : s)
        if (c < 'A' || c > 'Z') return false;
    return !s.empty();
}

bool all_digits(const std::string& s) {
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return !s.empty();
}


// 129: a station serial number, optionally with one letter suffix (107A).
bool valid_serial(const std::string& s) {
    if (s.empty() || s.size() > 5) return false;
    size_t n = s.size();
    if (std::isalpha(static_cast<unsigned char>(s.back()))) {
        if (s.back() < 'A' || s.back() > 'Z') return false;
        --n;
    }
    return n > 0 && all_digits(s.substr(0, n));
}

std::string two(int v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02d", v);
    return b;
}

std::string address_text(const Address& a) {
    if (a.ri.empty()) return a.plain;
    if (a.plain.empty()) return a.ri;
    return a.ri + "/" + a.plain;
}

bool check_address(const Address& a, const std::string& what, std::string* error) {
    if (a.ri.empty() && a.plain.empty()) return fail(error, what + ": empty address");
    if (!a.ri.empty() && !valid_ri(a.ri)) return fail(error, what + ": routing indicator must be 3 to 12 of A-Z, 0-9 and -");
    char bad = 0;
    if (!valid_chars(a.plain, &bad)) return fail(error, what + ": character '" + std::string(1, bad) + "' not allowed");
    if (a.plain.find('/') != std::string::npos) return fail(error, what + ": '/' only separates the RI");
    return true;
}

Address parse_address(const std::string& s) {
    Address a;
    size_t slash = s.find('/');
    std::string head = slash == std::string::npos ? s : s.substr(0, slash);
    if (slash != std::string::npos && valid_ri(head)) {
        a.ri = head;
        a.plain = s.substr(slash + 1);
    } else {
        a.plain = s;
    }
    return a;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

bool parse_dtg(const std::vector<std::string>& t, size_t i, Dtg& d, std::string* error) {
    if (i + 1 >= t.size()) return fail(error, "format line 5: date-time group needs DDHHMMZ and month");
    const std::string& g = t[i];
    if (g.size() != 7 || !all_digits(g.substr(0, 6)) || g[6] < 'A' || g[6] > 'Z' || g[6] == 'J')
        return fail(error, "format line 5: date-time group must be DDHHMM and a zone letter");
    d.day = std::stoi(g.substr(0, 2));
    d.hour = std::stoi(g.substr(2, 2));
    d.minute = std::stoi(g.substr(4, 2));
    d.zone = g[6];
    d.month = 0;
    for (int m = 0; m < 12; ++m)
        if (t[i + 1] == kMonths[m]) d.month = m + 1;
    if (!d.month) return fail(error, "format line 5: month must be JAN to DEC");
    d.year = 0;
    if (i + 2 < t.size() && t[i + 2].size() == 4 && all_digits(t[i + 2])) d.year = std::stoi(t[i + 2]);
    if (d.day < 1 || d.day > 31 || d.hour > 23 || d.minute > 59)
        return fail(error, "format line 5: date-time group out of range");
    return true;
}

}  // namespace

// ACP 127(G) takes routing indicators from the ACP 117 series (204.a), all
// letters. MARS stations also put call signs (AAR4MW), tactical calls
// (KAPPAMAIN), collectives (B7Q) and suffixed RIs (UHDJRKW-OP) in the same
// places (traffic samples 02, 23, 26, 33), so this follows the samples:
// 3 to 12 of A-Z, 0-9 and '-', with at least one letter, not starting or
// ending with '-'.
bool valid_ri(const std::string& s) {
    if (s.size() < 3 || s.size() > 12 || s.front() == '-' || s.back() == '-') return false;
    bool letter = false;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') letter = true;
        else if (!((c >= '0' && c <= '9') || c == '-')) return false;
    }
    return letter;
}

const char* prec_name(char p) {
    switch (p) {
        case 'Z': return "FLASH";
        case 'O': return "IMMEDIATE";
        case 'P': return "PRIORITY";
        case 'R': return "ROUTINE";
        default: return "";
    }
}

bool valid_chars(const std::string& s, char* bad) {
    for (char c : s) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '?' ||
                  c == ':' || c == '(' || c == ')' || c == '.' || c == ',' || c == '/';
        if (!ok) {
            if (bad) *bad = c;
            return false;
        }
    }
    return true;
}

std::string build(const Message& m, std::string* error) {
    std::string err;
    auto bad = [&](const std::string& why) {
        if (error) *error = why;
        return std::string();
    };
    std::vector<std::string> L;

    if (m.classification != 'U')
        return bad("only UNCLAS messages are built: the appliance sends plain text (the cipher is a stand-in)");
    if (!prec_name(m.prec)[0]) return bad("precedence must be Z, O, P or R");

    // Format line 1 (202.c, 138.a).
    if (m.channel.size() != 3 || !all_upper(m.channel)) return bad("format line 1: channel must be 3 letters");
    if (m.channel_serial < 1 || m.channel_serial > 999) return bad("format line 1: channel serial must be 1 to 999");
    char serial[8];
    std::snprintf(serial, sizeof serial, "%03d", m.channel_serial);
    std::string fl1 = m.automatic ? "VZCZC" + m.channel + serial : "VV   " + m.channel + serial;
    fl1 += "     " + std::string(2, m.classification);
    L.push_back(fl1);

    // Format line 2 (151, 204).
    if (m.routing.empty()) return bad("format line 2: at least one routing indicator");
    std::string fl2(2, m.prec);
    for (auto& ri : m.routing) {
        if (!valid_ri(ri)) return bad("format line 2: routing indicator '" + ri + "' must be 3 to 12 of A-Z, 0-9 and -");
        fl2 += " " + ri;
    }
    L.push_back(fl2);

    // Format line 3.
    if (!valid_ri(m.originator_ri)) return bad("format line 3: originating routing indicator must be 3 to 12 of A-Z, 0-9 and -");
    if (m.filing_hour < 0 || m.filing_hour > 23 || m.filing_minute < 0 || m.filing_minute > 59)
        return bad("format line 3: filing time out of range");
    if (m.mars_fl3) {
        // MARS practice (samples 10, 19, 25): DE RI #nnnn DDDHHMM, the station
        // serial as four digits and the time filed as a Julian day.
        if (m.station_serial.empty() || m.station_serial.size() > 4 || !all_digits(m.station_serial))
            return bad("format line 3: station serial must be 1 to 4 digits");
        if (m.filing_yday < 1 || m.filing_yday > 366) return bad("format line 3: Julian day must be 1 to 366");
        char b[32];
        std::snprintf(b, sizeof b, "#%04d %03d%02d%02d", std::stoi(m.station_serial), m.filing_yday, m.filing_hour,
                      m.filing_minute);
        L.push_back("DE " + m.originator_ri + " " + b);
    } else {
        if (!valid_serial(m.station_serial))
            return bad("format line 3: station serial must be digits, optionally a letter");
        if (m.filing_day < 1 || m.filing_day > 31) return bad("format line 3: filing time out of range");
        L.push_back("DE " + m.originator_ri + " " + m.station_serial + " " + two(m.filing_day) + "/" +
                    two(m.filing_hour) + two(m.filing_minute) + "Z");
    }

    // Format line 4 (203.a, 124.b.(1)).
    if (m.security_line) {
        std::string fl4 = "ZNR " + std::string(5, m.classification);
        if (!m.transmission_instructions.empty()) {
            char c = 0;
            if (!valid_chars(m.transmission_instructions, &c))
                return bad("format line 4: character '" + std::string(1, c) + "' not allowed");
            fl4 += " " + m.transmission_instructions;
        }
        L.push_back(fl4);
    } else if (!m.transmission_instructions.empty()) {
        return bad("format line 4: transmission instructions need the security line");
    }

    // Format line 5 (113).
    const Dtg& d = m.dtg;
    if (d.day < 1 || d.day > 31 || d.hour < 0 || d.hour > 23 || d.minute < 0 || d.minute > 59 || d.month < 1 ||
        d.month > 12 || d.zone < 'A' || d.zone > 'Z' || d.zone == 'J' || (d.year && (d.year < 1000 || d.year > 9999)))
        return bad("format line 5: date-time group out of range");
    std::string fl5 = std::string(1, m.prec) + " " + two(d.day) + two(d.hour) + two(d.minute) + d.zone + " " +
                      kMonths[d.month - 1];
    if (d.year) fl5 += " " + std::to_string(d.year);
    L.push_back(fl5);

    // Format lines 6 to 9. Further addressees of one kind follow on lines of their own.
    auto put = [&](const char* prosign, const std::vector<Address>& list, const std::string& what) {
        for (size_t i = 0; i < list.size(); ++i) {
            if (!check_address(list[i], what, &err)) return false;
            L.push_back((i == 0 ? std::string(prosign) + " " : std::string()) + address_text(list[i]));
        }
        return true;
    };
    if (!check_address(m.from, "format line 6 (FM)", &err)) return bad(err);
    L.push_back("FM " + address_text(m.from));
    if (m.to.empty()) return bad("format line 7: at least one TO addressee");
    if (!put("TO", m.to, "format line 7 (TO)") || !put("INFO", m.info, "format line 8 (INFO)") ||
        !put("XMT", m.xmt, "format line 9 (XMT)"))
        return bad(err);

    // Format line 10: group count only (see parse()).
    if (!m.line10.empty()) {
        auto t = split_ws(m.line10);
        if (t.size() != 2 || t[0] != "GR" || !all_digits(t[1]))
            return bad("format line 10: only a group count, GR and a number, is supported");
        L.push_back("GR " + t[1]);
    }

    // Format lines 11 to 13 (214, 209.c).
    L.push_back("BT");
    std::string text;
    for (char c : m.text) text += c == '\r' ? ' ' : c;
    std::istringstream paras(text);
    bool first = true;
    size_t text_lines = 0;
    for (std::string para; std::getline(paras, para);) {
        std::string line = first ? std::string(kClassWord) : std::string();
        auto words = split_ws(para);
        if (words.empty() && !first) continue;
        for (auto& w : words) {
            char c = 0;
            if (!valid_chars(w, &c))
                return bad("format line 12: character '" + std::string(1, c) +
                           "' not allowed (write PARA, QUOTE or UNQUOTE instead of other punctuation)");
            if (m.limit_lines && w.size() > kMaxLine) return bad("format line 12: a word is longer than 69 characters");
            if (line.empty()) line = w;
            else if (!m.limit_lines || line.size() + 1 + w.size() <= kMaxLine) line += " " + w;
            else {
                L.push_back(line);
                ++text_lines;
                line = w;
            }
        }
        if (!line.empty()) {
            L.push_back(line);
            ++text_lines;
        }
        first = false;
    }
    if (first || text_lines == 0 || (text_lines == 1 && L.back() == kClassWord)) return bad("format line 12: no text");
    L.push_back("BT");

    // Format line 15 (211.c).
    if (!m.correction.empty()) {
        char c = 0;
        if (!starts_with(m.correction, "C ")) return bad("format line 15: a correction starts with C");
        if (!valid_chars(m.correction, &c)) return bad("format line 15: character '" + std::string(1, c) + "' not allowed");
        L.push_back(m.correction);
    }

    for (auto& line : L)
        if (m.limit_lines && line.size() > kMaxLine) return bad("line over 69 characters: " + line.substr(0, 20) + "...");

    // Each line ends 2CR 1LF; the last before NNNN ends 2CR 8LF (138.b, 138.d).
    std::string out;
    for (size_t i = 0; i < L.size(); ++i)
        out += L[i] + (i + 1 < L.size() ? "\r\r\n" : "\r\r\n\n\n\n\n\n\n\n");
    out += "NNNN";
    if (error) error->clear();
    return out;
}

bool parse(const std::string& raw, Message& m, std::string* error) {
    m = Message();
    // Lines, with machine functions removed: CRs, BELs, blank lines (the 8 LFs).
    std::vector<std::string> L;
    {
        std::string cur;
        auto flush = [&] {
            while (!cur.empty() && cur.back() == ' ') cur.pop_back();
            if (!cur.empty()) L.push_back(cur);
            cur.clear();
        };
        for (char c : raw) {
            if (c == '\n') flush();
            else if (c != '\r' && c != '\a') cur += c;
        }
        flush();
    }
    size_t i = 0;
    auto at = [&](size_t k) -> const std::string& {
        static const std::string empty;
        return k < L.size() ? L[k] : empty;
    };

    // Format line 1.
    std::string fl1 = at(i++);
    std::string ti;
    if (starts_with(fl1, "VZCZC")) {
        m.automatic = true;
        ti = fl1.substr(5);
    } else if (starts_with(fl1, "VV")) {
        m.automatic = false;
        ti = fl1.substr(2);
        size_t s = ti.find_first_not_of(' ');
        ti = s == std::string::npos ? "" : ti.substr(s);
    } else {
        return fail(error, "format line 1: must start VZCZC or VV");
    }
    if (ti.size() < 6 || !all_upper(ti.substr(0, 3)) || !all_digits(ti.substr(3, 3)))
        return fail(error, "format line 1: transmission identification must be 3 letters and 3 digits");
    m.channel = ti.substr(0, 3);
    m.channel_serial = std::stoi(ti.substr(3, 3));
    auto rest = split_ws(ti.substr(6));
    if (!rest.empty()) {
        if (rest.size() != 1 || rest[0].size() != 2 || rest[0][0] != rest[0][1] || !all_upper(rest[0]))
            return fail(error, "format line 1: security warning must be one letter twice");
        m.classification = rest[0][0];
    }

    // Format line 2.
    auto t2 = split_ws(at(i++));
    if (t2.empty() || t2[0].size() != 2 || t2[0][0] != t2[0][1] || !prec_name(t2[0][0])[0])
        return fail(error, "format line 2: must start ZZ, OO, PP or RR");
    m.prec = t2[0][0];
    for (size_t k = 1; k < t2.size(); ++k) {
        if (!valid_ri(t2[k])) return fail(error, "format line 2: '" + t2[k] + "' is not a routing indicator");
        m.routing.push_back(t2[k]);
    }
    if (m.routing.empty()) return fail(error, "format line 2: no routing indicator");

    // Format line 3.
    auto t3 = split_ws(at(i++));
    if (t3.size() != 4 || t3[0] != "DE") return fail(error, "format line 3: must be DE, RI, serial, filing time");
    if (!valid_ri(t3[1])) return fail(error, "format line 3: bad originating routing indicator");
    m.originator_ri = t3[1];
    const std::string& ft = t3[3];
    if (t3[2].size() == 5 && t3[2][0] == '#' && all_digits(t3[2].substr(1))) {
        if (ft.size() != 7 || !all_digits(ft)) return fail(error, "format line 3: time filed must be DDDHHMM");
        m.mars_fl3 = true;
        m.station_serial = t3[2].substr(1);
        m.filing_yday = std::stoi(ft.substr(0, 3));
        m.filing_hour = std::stoi(ft.substr(3, 2));
        m.filing_minute = std::stoi(ft.substr(5, 2));
        if (m.filing_yday < 1 || m.filing_yday > 366 || m.filing_hour > 23 || m.filing_minute > 59)
            return fail(error, "format line 3: time filed out of range");
    } else {
        if (!valid_serial(t3[2])) return fail(error, "format line 3: bad station serial");
        if (ft.size() != 8 || ft[2] != '/' || ft[7] != 'Z' || !all_digits(ft.substr(0, 2)) ||
            !all_digits(ft.substr(3, 4)))
            return fail(error, "format line 3: filing time must be DD/HHMMZ");
        m.mars_fl3 = false;
        m.station_serial = t3[2];
        m.filing_day = std::stoi(ft.substr(0, 2));
        m.filing_hour = std::stoi(ft.substr(3, 2));
        m.filing_minute = std::stoi(ft.substr(5, 2));
    }

    // Format line 4 (optional).
    m.security_line = false;
    if (starts_with(at(i), "ZNR ") || starts_with(at(i), "ZNY ")) {
        auto t4 = split_ws(at(i));
        if (t4.size() < 2 || t4[1].size() != 5 || t4[1] != std::string(5, t4[1][0]))
            return fail(error, "format line 4: classification must be one letter five times");
        if (t4[1][0] != m.classification) return fail(error, "format line 4: classification differs from line 1");
        m.security_line = true;
        std::string instr;
        for (size_t k = 2; k < t4.size(); ++k) instr += (instr.empty() ? "" : " ") + t4[k];
        m.transmission_instructions = instr;
        ++i;
    }

    // Format line 5.
    auto t5 = split_ws(at(i++));
    if (t5.empty() || t5[0].size() != 1 || t5[0][0] != m.prec)
        return fail(error, "format line 5: must start with the precedence of format line 2");
    if (!parse_dtg(t5, 1, m.dtg, error)) return false;

    // Format lines 6 to 9.
    if (!starts_with(at(i), "FM ")) return fail(error, "format line 6: FM missing");
    m.from = parse_address(at(i++).substr(3));
    if (!starts_with(at(i), "TO ")) return fail(error, "format line 7: TO missing");
    std::vector<Address>* list = &m.to;
    m.to.push_back(parse_address(at(i++).substr(3)));
    for (;; ++i) {
        const std::string& l = at(i);
        if (i >= L.size()) return fail(error, "message ends before BT");
        if (l == "BT") break;
        if (starts_with(l, "INFO ")) {
            if (list != &m.to) return fail(error, "format line 8: INFO out of order");
            list = &m.info;
            list->push_back(parse_address(l.substr(5)));
        } else if (starts_with(l, "XMT ")) {
            if (list == &m.xmt) return fail(error, "format line 9: XMT twice");
            list = &m.xmt;
            list->push_back(parse_address(l.substr(4)));
        } else if (starts_with(l, "GR ") && all_digits(l.substr(3)) && at(i + 1) == "BT") {
            m.line10 = l;
        } else {
            list->push_back(parse_address(l));
        }
    }
    ++i;  // BT, format line 11

    // Format line 12, up to the BT of format line 13.
    std::string text;
    bool first = true;
    for (;; ++i) {
        if (i >= L.size()) return fail(error, "format line 13: closing BT missing");
        const std::string& l = at(i);
        if (l == "BT") break;
        if (first) {
            if (l != kClassWord && !starts_with(l, std::string(kClassWord) + " "))
                return fail(error, "format line 12: text must start UNCLAS");
            std::string after = l.size() > 6 ? l.substr(7) : "";
            text = after;
            first = false;
        } else {
            text += "\n" + l;
        }
    }
    if (first) return fail(error, "format line 12: no text");
    if (m.classification != 'U') return fail(error, "only UNCLAS messages are accepted");
    m.text = text;
    ++i;

    // Format line 15 (optional), then NNNN. MSC stations (and this engine)
    // also put a [DIGEST:...] line before NNNN: an error check over the
    // text (digest.h), not part of ACP 127, read by digest::check. This
    // engine may add a [SIG:...] line after it (station.h).
    if (starts_with(at(i), "C ")) m.correction = at(i++);
    if (starts_with(at(i), "[DIGEST:")) ++i;
    if (starts_with(at(i), "[SIG:")) ++i;  // a station signature (station.h)
    if (at(i) != "NNNN") return fail(error, "format line 16: NNNN missing");
    if (i + 1 != L.size()) return fail(error, "text after NNNN");
    if (error) error->clear();
    return true;
}

}  // namespace acp127
}  // namespace pna
