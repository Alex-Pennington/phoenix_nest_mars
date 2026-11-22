// See acp126.h.
#include "acp126.h"

#include <cstdio>
#include <sstream>

namespace pna {
namespace acp126 {

namespace {

const char* kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

std::string address_text(const acp127::Address& a) {
    if (a.ri.empty()) return a.plain;
    if (a.plain.empty()) return a.ri;
    return a.ri + "/" + a.plain;
}

bool check_address(const acp127::Address& a, const std::string& what, std::string* error) {
    char bad = 0;
    if (a.ri.empty() && a.plain.empty()) return error && (*error = what + ": empty address", false);
    if (!a.ri.empty() && !acp127::valid_ri(a.ri)) return error && (*error = what + ": bad routing indicator", false);
    if (!acp127::valid_chars(a.plain, &bad) || a.plain.find('/') != std::string::npos) {
        if (error) *error = what + ": character not allowed in the address";
        return false;
    }
    return true;
}

}  // namespace

std::string build(const Message& m, std::string* error) {
    auto bad = [&](const std::string& why) {
        if (error) *error = why;
        return std::string();
    };
    std::vector<std::string> L;
    char b[64];

    if (m.channel.empty()) {
        L.push_back("VZCZC");
    } else {
        if (m.channel.size() != 3) return bad("format line 1: channel must be 3 letters");
        for (char c : m.channel)
            if (c < 'A' || c > 'Z') return bad("format line 1: channel must be 3 letters");
        if (m.channel_serial < 1 || m.channel_serial > 999) return bad("format line 1: channel serial must be 1 to 999");
        std::snprintf(b, sizeof b, "VZCZC%s%03d", m.channel.c_str(), m.channel_serial);
        L.push_back(b);
    }

    if (m.calls.empty()) return bad("call-up: at least one called station");
    std::string call;
    for (auto& c : m.calls) {
        if (!acp127::valid_ri(c)) return bad("call-up: '" + c + "' must be 3 to 12 of A-Z, 0-9 and -");
        call += c + " ";
    }
    if (!acp127::valid_ri(m.station)) return bad("call-up: calling station must be 3 to 12 of A-Z, 0-9 and -");
    if (m.serial < 1 || m.serial > 999) return bad("call-up: NR must be 1 to 999");
    std::snprintf(b, sizeof b, "DE %s NR %03d", m.station.c_str(), m.serial);
    L.push_back(call + b);

    bool plain = m.procedure == Message::Procedure::Plaindress;
    bool has_fm = !m.from.ri.empty() || !m.from.plain.empty();
    if (!plain && (has_fm || !m.to.empty()))
        return bad("SIMPLE has no FM or TO; use PLAINDRESS for an addressed message");
    if (plain && (!has_fm || m.to.empty())) return bad("PLAINDRESS needs FM and at least one TO");
    if (plain) {
        const acp127::Dtg& d = m.dtg;
        if (!acp127::prec_name(m.prec)[0]) return bad("precedence must be Z, O, P or R");
        if (d.day < 1 || d.day > 31 || d.hour < 0 || d.hour > 23 || d.minute < 0 || d.minute > 59 || d.month < 1 ||
            d.month > 12 || d.zone < 'A' || d.zone > 'Z' || d.zone == 'J' || (d.year && (d.year < 1000 || d.year > 9999)))
            return bad("date-time group out of range");
        std::snprintf(b, sizeof b, "%c %02d%02d%02d%c %s", m.prec, d.day, d.hour, d.minute, d.zone, kMonths[d.month - 1]);
        std::string l = b;
        if (d.year) l += " " + std::to_string(d.year);
        L.push_back(l);
    }
    std::string err;
    if (has_fm) {
        if (!check_address(m.from, "FM", &err)) return bad(err);
        L.push_back("FM " + address_text(m.from));
    }
    for (size_t i = 0; i < m.to.size(); ++i) {
        if (!check_address(m.to[i], "TO", &err)) return bad(err);
        L.push_back((i ? "" : "TO ") + address_text(m.to[i]));
    }

    L.push_back("BT");
    if (m.unclas_line) L.push_back("UNCLAS");
    size_t text_start = L.size();
    std::istringstream paras(m.text);
    for (std::string para; std::getline(paras, para);) {
        std::istringstream ws(para);
        std::string line;
        for (std::string w; ws >> w;) {
            char c = 0;
            if (!acp127::valid_chars(w, &c))
                return bad("text: character '" + std::string(1, c) + "' not allowed");
            if (w.size() > acp127::kMaxLine) return bad("text: a word is longer than 69 characters");
            if (line.empty()) line = w;
            else if (line.size() + 1 + w.size() <= acp127::kMaxLine) line += " " + w;
            else {
                L.push_back(line);
                line = w;
            }
        }
        if (!line.empty()) L.push_back(line);
    }
    if (L.size() == text_start) return bad("text: no text");
    L.push_back("BT");
    for (auto& l : L)
        if (l.size() > acp127::kMaxLine) return bad("line over 69 characters: " + l.substr(0, 20) + "...");

    std::string out;
    for (size_t i = 0; i < L.size(); ++i) out += L[i] + (i + 1 < L.size() ? "\r\r\n" : "\r\r\n\n\n\n\n\n\n\n");
    out += "NNNN";
    if (error) error->clear();
    return out;
}

const char* procedure_name(Message::Procedure p) {
    return p == Message::Procedure::Plaindress ? "ACP126_PLAINDRESS" : "ACP126_SIMPLE";
}

bool procedure_from(const std::string& s, Message::Procedure& p) {
    std::string u;
    for (char c : s) u += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    if (u == "PLAINDRESS" || u == "ACP126_PLAINDRESS") p = Message::Procedure::Plaindress;
    else if (u == "SIMPLE" || u == "ACP126_SIMPLE") p = Message::Procedure::Simple;
    else return false;
    return true;
}

}  // namespace acp126
}  // namespace pna
