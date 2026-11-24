// Reading traffic as MARS stations send it. See received.h.
#include "received.h"

#include <cctype>
#include <sstream>

namespace pna {
namespace received {

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

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

std::string rtrim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    return a == std::string::npos ? std::string() : rtrim(s.substr(a));
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool has_lower(const std::string& s) {
    for (char c : s)
        if (c >= 'a' && c <= 'z') return true;
    return false;
}

bool is_page_header(const std::vector<std::string>& w) {
    // "PAGE 2 RI #0129 UNCLAS", "PAGE 2 RI 0163 UNCLAS", "PAGE 2 RI #0209".
    if (w.size() < 4 || w.size() > 5 || w[0] != "PAGE" || !all_digits(w[1])) return false;
    const std::string& s = w[3];
    if (!all_digits(s[0] == '#' ? s.substr(1) : s)) return false;
    return w.size() == 4 || w[4] == "UNCLAS";
}

bool is_prec_prosign(const std::string& t) {
    return t.size() == 2 && t[0] == t[1] && (t[0] == 'Z' || t[0] == 'O' || t[0] == 'P' || t[0] == 'R');
}

const char* kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// "R 192109Z FEB 2025": precedence letter, DDHHMM and zone, month, optional year.
// A CODRESS DTG carries the key indicator after it (batch 2 samples 02, 03).
bool read_dtg(const std::vector<std::string>& t, char& prec, acp127::Dtg& d, std::string& key) {
    if (t.size() < 3 || t[0].size() != 1 || std::string("ZOPR").find(t[0][0]) == std::string::npos) return false;
    const std::string& g = t[1];
    if (g.size() != 7 || !all_digits(g.substr(0, 6)) || g[6] < 'A' || g[6] > 'Z') return false;
    int month = 0;
    for (int m = 0; m < 12; ++m)
        if (t[2] == kMonths[m]) month = m + 1;
    if (!month) return false;
    prec = t[0][0];
    d.day = std::stoi(g.substr(0, 2));
    d.hour = std::stoi(g.substr(2, 2));
    d.minute = std::stoi(g.substr(4, 2));
    d.zone = g[6];
    d.month = month;
    d.year = t.size() > 3 && t[3].size() == 4 && all_digits(t[3]) ? std::stoi(t[3]) : 0;
    size_t k = d.year ? 4 : 3;
    key = k < t.size() ? t[k] : std::string();
    return true;
}

acp127::Address read_address(const std::string& s, std::vector<std::string>& warn) {
    acp127::Address a;
    size_t slash = s.find('/');
    if (slash != std::string::npos) {
        std::string head = trim(s.substr(0, slash));
        if (!head.empty() && head.find(' ') == std::string::npos) {
            if (has_lower(head)) warn.push_back("lower-case routing indicator " + head);
            a.ri = upper(head);
            a.plain = trim(s.substr(slash + 1));
            return a;
        }
    }
    auto w = words(s);
    if (w.size() == 1 && acp127::valid_ri(upper(w[0]))) {
        a.ri = upper(w[0]);
        if (has_lower(w[0])) warn.push_back("lower-case routing indicator " + w[0]);
        return a;
    }
    // "uhrrrwn IRONCRANE": a lower-case RI and a space for the slash (sample 43).
    if (w.size() > 1) {
        bool lower_ri = w[0].size() >= 4;  // all lower-case letters, so "2nd AFMARS" stays plain
        for (char c : w[0])
            if (c < 'a' || c > 'z') lower_ri = false;
        if (lower_ri && acp127::valid_ri(upper(w[0]))) {
            warn.push_back("routing indicator " + w[0] + " in lower case with a space for the slash");
            a.ri = upper(w[0]);
            a.plain = trim(s.substr(s.find(w[0]) + w[0].size()));
            return a;
        }
    }
    a.plain = trim(s);
    return a;
}

}  // namespace

const char* form_name(Form f) {
    switch (f) {
        case Form::Acp127: return "acp127";
        case Form::Acp127Folded: return "acp127-folded";
        case Form::Acp126: return "acp126";
    }
    return "";
}

std::string procedure(const Message& m) {
    switch (m.form) {
        case Form::Acp126: return m.has_dtg ? "ACP126_PLAINDRESS" : "ACP126_SIMPLE";
        case Form::Acp127: return m.enciphered ? "ACP127_CODRESS" : "ACP127_PLAINDRESS";
        case Form::Acp127Folded: break;
    }
    bool ri = starts_with(m.originator, "UH");
    if (m.originator.empty())
        for (auto& r : m.routing)
            if (starts_with(r, "UH")) ri = true;
    std::string p = ri ? "16L_RI_" : "16L_P2P_";
    if (!m.prec) return p + "SIMPLE";
    bool fm_to = !m.from.ri.empty() || !m.from.plain.empty() || !m.to.empty();
    return p + (fm_to ? "FULL" : "ABBR");
}

std::string text_of(const Message& m) {
    std::string out;
    for (size_t i = 0; i < m.text.size(); ++i) out += (i ? "\n" : "") + m.text[i];
    return out;
}

bool parse(const std::string& raw, Message& m, std::string* error) {
    m = Message();
    auto& warn = m.warnings;

    std::vector<std::string> L;
    {
        std::string cur;
        for (char c : raw) {
            if (c == '\n') {
                L.push_back(rtrim(cur));
                cur.clear();
            } else if (c != '\r') {
                cur += c;
            }
        }
        L.push_back(rtrim(cur));
    }

    // Format line 1: the first line that starts with VZCZC.
    size_t v = 0;
    while (v < L.size() && !starts_with(trim(L[v]), "VZCZC")) ++v;
    if (v == L.size()) {
        // Station Mapper saves an ALE link prefix ("FROM.TO.") on the same
        // line, run straight into VZCZC: "STA-A.ALL.VZCZC".
        for (v = 0; v < L.size(); ++v) {
            size_t at = L[v].find(".VZCZC");
            if (at == std::string::npos) continue;
            L[v] = L[v].substr(at + 1);
            warn.push_back("an ALE link prefix before VZCZC on the same line ignored");
            break;
        }
    }
    if (v == L.size()) return fail(error, "format line 1: no line starts with VZCZC");
    for (size_t i = 0; i < v; ++i)
        if (!trim(L[i]).empty()) {
            warn.push_back("text before VZCZC ignored");
            break;
        }
    size_t n = v + 1;
    while (n < L.size() && trim(L[n]) != "NNNN") ++n;
    // A message may end without NNNN (batch 2 sample 20). It is taken if the
    // closing BT came; one cut off before that (sample 17) is refused below.
    bool no_nnnn = n == L.size();
    if (no_nnnn) {
        L.push_back(std::string());
        warn.push_back("format line 16: no NNNN");
    }
    for (size_t i = n + 1; i < L.size(); ++i)
        if (!trim(L[i]).empty()) {
            warn.push_back("text after NNNN ignored");
            break;
        }

    // Remove an indent common to every line of the message (sample 03).
    size_t indent = std::string::npos;
    for (size_t i = v; i <= n; ++i) {
        if (L[i].empty()) continue;
        size_t a = L[i].find_first_not_of(' ');
        indent = std::min(indent, a == std::string::npos ? L[i].size() : a);
    }
    if (indent && indent != std::string::npos) {
        warn.push_back("every line indented");
        for (size_t i = v; i <= n; ++i) L[i] = L[i].size() > indent ? L[i].substr(indent) : std::string();
    }

    // Format line 1: VZCZC, the channel and serial, then (ACP 127(G) 138.a)
    // the security warning, e.g. "VZCZCQXA007     UU".
    {
        auto w = words(trim(L[v]).substr(5));
        size_t i = 0;
        if (i < w.size() && w[i].size() == 6 && std::isupper(static_cast<unsigned char>(w[i][0])) &&
            std::isupper(static_cast<unsigned char>(w[i][1])) && std::isupper(static_cast<unsigned char>(w[i][2])) &&
            all_digits(w[i].substr(3))) {
            m.channel = w[i].substr(0, 3);
            m.channel_serial = std::stoi(w[i].substr(3));
            ++i;
        }
        if (i < w.size() && w[i].size() == 2 && w[i][0] == w[i][1] && std::isupper(static_cast<unsigned char>(w[i][0]))) {
            if (w[i][0] != 'U') return fail(error, "format line 1: security warning " + w[i] + "; only UNCLAS is accepted");
            ++i;
        }
        if (i < w.size()) warn.push_back("format line 1: unread text after VZCZC");
    }

    // Header: the lines up to the first BT.
    std::vector<std::string> H;
    size_t b = v + 1;
    for (; b < n; ++b) {
        std::string t = trim(L[b]);
        if (t == "BT") break;
        if (!t.empty()) H.push_back(t);
    }
    if (b == n) return fail(error, no_nnnn ? "format line 16: no NNNN, and no BT: the message is cut off"
                                           : "format line 11: no BT before NNNN");

    // Format lines 2 and 3: everything up to and including the DE line.
    size_t de_line = 0;
    std::vector<std::string> before, after;
    bool found = false;
    for (; de_line < H.size() && !found; ++de_line) {
        auto w = words(H[de_line]);
        for (size_t k = 0; k < w.size(); ++k) {
            if (w[k] == "DE") {
                found = true;
                before.insert(before.end(), w.begin(), w.begin() + static_cast<long>(k));
                after.assign(w.begin() + static_cast<long>(k) + 1, w.end());
                break;
            }
        }
        if (!found) before.insert(before.end(), w.begin(), w.end());
    }
    if (!found) return fail(error, "format line 3: no DE");
    bool de_starts_line = words(H[de_line - 1])[0] == "DE";
    if (de_line > 2 || (de_line == 2 && !de_starts_line)) warn.push_back("format line 2 runs over more than one line");

    size_t k = 0;
    if (!before.empty() && is_prec_prosign(before[0])) {
        m.prec = before[0][0];
        k = 1;
    }
    for (; k < before.size(); ++k) {
        std::string a = upper(before[k]);
        if (a != before[k]) warn.push_back("lower-case address " + before[k]);
        m.routing.push_back(a);
    }

    size_t a = 0;
    if (a < after.size() && after[a] != "NR" && after[a][0] != '#') m.originator = upper(after[a++]);
    // "DE UHRRRWN UHXXZ #0718 0492331": a relay names a second RI (batch 2 sample 01).
    {
        size_t hash = a;
        while (hash < after.size() && after[hash][0] != '#') ++hash;
        for (; hash < after.size() && a < hash && acp127::valid_ri(upper(after[a])) && !all_digits(after[a]) &&
               after[a] != "NR";)
            m.relay.push_back(upper(after[a++]));
    }
    if (a < after.size() && after[a] == "NR") {
        m.form = Form::Acp126;
        if (a + 1 >= after.size() || !all_digits(after[a + 1])) return fail(error, "format line 3: NR without a number");
        m.serial = after[a + 1];
        a += 2;
    } else {
        m.form = de_starts_line ? Form::Acp127 : Form::Acp127Folded;
        if (a < after.size() && after[a][0] == '#') {
            if (!all_digits(after[a].substr(1))) return fail(error, "format line 3: bad station serial");
            m.serial = after[a++].substr(1);
        } else if (a < after.size()) {
            m.serial = after[a++];
        }
        if (a < after.size()) m.time_filed = after[a++];
        // A long address list pushes the time filed onto the next line (23, 30).
        if (m.time_filed.empty() && de_line < H.size() && H[de_line].size() == 7 && all_digits(H[de_line])) {
            m.time_filed = H[de_line++];
            warn.push_back("time filed on a line of its own");
        }
    }
    if (a < after.size()) warn.push_back("format line 3: extra text ignored");
    if (m.originator.empty()) warn.push_back("no originator after DE");
    // The 16L SIMPLE template sends no prosign by design (batch 1 sample 04),
    // and ACP 126 carries precedence on the DTG line if anywhere.
    if (!m.prec && m.form == Form::Acp127) warn.push_back("no precedence prosign");
    if (m.routing.empty()) warn.push_back("no addressee before DE");

    // Format lines 4 to 10.
    std::vector<acp127::Address>* list = nullptr;
    bool from_open = false;  // a long FM runs on to the next line (batch 2 sample 01)
    for (size_t i = de_line; i < H.size(); ++i) {
        const std::string& l = H[i];
        auto w = words(l);
        char p = 0;
        acp127::Dtg d;
        if ((w[0] == "ZNR" || w[0] == "ZNY") && w.size() >= 2 && w[1].size() == 5 &&
            w[1] == std::string(5, w[1][0])) {
            m.security = w[0] + " " + w[1];
            if (w[1][0] != 'U' && w[1][0] != 'E')
                return fail(error, "format line 4: " + w[1] + " is not unclassified; only UNCLAS is accepted");
        } else if (w[0] == "ZEK") {
            m.zek = true;
            m.zek_line = l;
        } else if (!m.has_dtg && !list && read_dtg(w, p, d, m.key_indicator)) {
            m.has_dtg = true;
            m.dtg = d;
            if (m.prec && p != m.prec) warn.push_back("format line 5 precedence differs from format line 2");
            if (!m.prec) m.prec = p;
        } else if (w.size() == 2 && w[0] == "GR" && all_digits(w[1])) {
            m.group_count = std::stoi(w[1]);
            list = nullptr;
            from_open = false;
        } else if (starts_with(l, "FM ")) {
            m.from = read_address(l.substr(3), warn);
            list = nullptr;
            from_open = true;
            continue;
        } else if (starts_with(l, "TO ")) {
            list = &m.to;
            list->push_back(read_address(l.substr(3), warn));
        } else if (starts_with(l, "INFO ")) {
            list = &m.info;
            list->push_back(read_address(l.substr(5), warn));
        } else if (starts_with(l, "XMT ")) {
            list = &m.xmt;
            list->push_back(read_address(l.substr(4), warn));
        } else if (from_open) {
            m.from.plain += (m.from.plain.empty() ? "" : " ") + l;
            warn.push_back("FM runs on to a second line");
            continue;
        } else if (list) {
            list->push_back(read_address(l, warn));
        } else {
            warn.push_back("header line not placed: " + l.substr(0, 20));
        }
        from_open = false;
    }

    // Format line 12, up to the closing BT.
    size_t e = b + 1;
    while (e < n && trim(L[e]) != "BT") ++e;
    if (e == n)
        return fail(error, no_nnnn ? "format line 16: no NNNN, and no closing BT: the message is cut off"
                                   : "format line 13: no closing BT");
    std::vector<std::string> T;
    for (size_t i = b + 1; i < e; ++i) {
        std::string t = L[i];
        auto w = words(t);
        // Page headers of a long message (sample 25).
        if (is_page_header(w)) continue;
        T.push_back(t);
    }
    while (!T.empty() && trim(T.front()).empty()) T.erase(T.begin());
    while (!T.empty() && trim(T.back()).empty()) T.pop_back();
    if (T.empty()) return fail(error, "format line 12: no text");
    // CODRESS: a key indicator and a group count, and cipher groups for text.
    // The body is kept as received and not read (the cipher is not here).
    if (m.form == Form::Acp127 && m.group_count >= 0 && !m.key_indicator.empty()) {
        m.enciphered = true;
        size_t groups = 0;
        for (auto& t : T) groups += words(t).size();
        if (static_cast<int>(groups) != m.group_count)
            warn.push_back("GR " + std::to_string(m.group_count) + " but " + std::to_string(groups) + " groups received");
    } else {
        if (m.group_count >= 0) warn.push_back("GR line on a plain-text message");
        if (!m.key_indicator.empty()) warn.push_back("format line 5: unread text after the date-time group");
        auto w = words(T[0]);
        static const char* kClassified[] = {"CONFIDENTIAL", "SECRET", "TOP", "CUI", "FOUO", "EFTO", "CONTROLLED"};
        for (const char* c : kClassified)
            if (upper(w[0]) == c) return fail(error, "format line 12: marked " + w[0] + "; only UNCLAS is accepted");
        if (w[0] == "UNCLAS") {
            size_t take = 1;
            m.class_line = "UNCLAS";
            if (w.size() > 1 && w[1] == "SVC") {
                m.class_line += " SVC";
                take = 2;
            }
            std::string rest;
            for (size_t i = take; i < w.size(); ++i) rest += (rest.empty() ? "" : " ") + w[i];
            if (rest.empty()) T.erase(T.begin());
            else T[0] = rest;
        } else {
            warn.push_back("no UNCLAS line");
        }
    }
    if (m.security == "ZNY EEEEE") warn.push_back("format line 4 is ZNY EEEEE over unclassified text");
    bool long_line = false;
    for (size_t i = v; i <= n; ++i)
        if (L[i].size() > acp127::kMaxLine && !starts_with(trim(L[i]), "[SIG:")) long_line = true;
    if (long_line) warn.push_back("lines over 69 characters");
    m.text = T;

    // After the closing BT.
    for (size_t i = e + 1; i < n; ++i) {
        std::string t = trim(L[i]);
        if (t.empty()) continue;
        if (t[0] == '#' && all_digits(t.substr(1))) {
            m.serial_again = t.substr(1);
            if (m.serial_again != m.serial) warn.push_back("serial after BT differs from format line 3");
        } else if (starts_with(t, "[DIGEST:") && t.back() == ']') {
            m.digest = t.substr(8, t.size() - 9);
        } else if (starts_with(t, "[SIG:") && t.back() == ']') {
            m.signature = t.substr(5, t.size() - 6);
        } else if (starts_with(t, "C ")) {
            m.correction = t;
        } else if (is_page_header(words(t))) {
            // A page header can follow the closing BT (batch 2 samples 15, 19).
        } else {
            warn.push_back("line after the closing BT not placed");
        }
    }
    if (error) error->clear();
    return true;
}

}  // namespace received
}  // namespace pna
