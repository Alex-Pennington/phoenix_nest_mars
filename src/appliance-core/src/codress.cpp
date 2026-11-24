// ACP 127 codress. See codress.h and docs/crypto.md.
#include "codress.h"

#include "cipher_slot.h"
#include "crypto.h"
#include "digest.h"

#include <cstdint>
#include <vector>

namespace pna {
namespace codress {

namespace {

constexpr size_t kGroupBytes = 5;

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

struct Lines {
    std::vector<std::string> v;
    std::string eol;
};

Lines split(const std::string& t) {
    Lines L;
    L.eol = t.find("\r\r\n") != std::string::npos ? "\r\r\n" : t.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string cur;
    for (char c : t) {
        if (c == '\n') {
            while (!cur.empty() && cur.back() == '\r') cur.pop_back();
            L.v.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    while (!cur.empty() && cur.back() == '\r') cur.pop_back();
    L.v.push_back(cur);
    return L;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

bool digits(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

bool letters(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s)
        if (c < 'A' || c > 'Z') return false;
    return true;
}

// Format line 5 words: prec, DDHHMMz, MON, optional YYYY. Returns the count
// of DTG words (3 or 4), or 0 if the line is not one.
size_t dtg_words(const std::vector<std::string>& w) {
    if (w.size() < 3) return 0;
    if (w[0].size() != 1 || std::string("ZOPRWY").find(w[0][0]) == std::string::npos) return 0;
    if (w[1].size() != 7 || !digits(w[1].substr(0, 6), 6) || w[1][6] < 'A' || w[1][6] > 'Z') return 0;
    if (!letters(w[2], 3)) return 0;
    return w.size() >= 4 && digits(w[3], 4) ? 4 : 3;
}

bool is_indicator(const std::string& s) { return s.size() == 5 && letters(s.substr(0, 3), 3) && digits(s.substr(3), 2); }

std::string join(const std::vector<std::string>& w, size_t from, size_t to) {
    std::string o;
    for (size_t i = from; i < to; ++i) o += (i > from ? " " : "") + w[i];
    return o;
}

bool is_bt(const std::string& l) {
    auto w = words(l);
    return !w.empty() && w[0] == "BT";
}

// Where format line 5 is: the first line after the header that reads as a
// date-time group. Format lines 1-4 never do.
size_t find_fl5(const Lines& L) {
    for (size_t i = 0; i < L.v.size(); ++i) {
        if (is_bt(L.v[i]) || L.v[i] == "NNNN") break;
        if (dtg_words(words(L.v[i]))) return i;
    }
    return std::string::npos;
}

const char* kDigits36 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

std::string group_of(const unsigned char* b) {
    uint64_t v = 0;
    for (size_t i = 0; i < kGroupBytes; ++i) v = (v << 8) | b[i];
    std::string g(kGroupChars, 'A');
    for (int i = kGroupChars - 1; i >= 0; --i) {
        g[static_cast<size_t>(i)] = kDigits36[v % 36];
        v /= 36;
    }
    return g;
}

bool bytes_of(const std::string& g, std::string& out) {
    if (g.size() != static_cast<size_t>(kGroupChars)) return false;
    uint64_t v = 0;
    for (char c : g) {
        int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= '0' && c <= '9' ? c - '0' + 26 : -1;
        if (d < 0) return false;
        v = v * 36 + static_cast<uint64_t>(d);
    }
    if (v >> 40) return false;
    for (int i = static_cast<int>(kGroupBytes) - 1; i >= 0; --i) out += static_cast<char>((v >> (8 * i)) & 0xff);
    return true;
}

}  // namespace

std::vector<std::string> to_groups(const std::string& bytes) {
    std::vector<std::string> g;
    for (size_t i = 0; i + kGroupBytes <= bytes.size(); i += kGroupBytes)
        g.push_back(group_of(reinterpret_cast<const unsigned char*>(bytes.data()) + i));
    return g;
}

bool from_groups(const std::vector<std::string>& groups, std::string& bytes, std::string* error) {
    bytes.clear();
    for (size_t i = 0; i < groups.size(); ++i)
        if (!bytes_of(groups[i], bytes)) {
            crypto::wipe(bytes);
            return fail(error, "group " + std::to_string(i + 1) + " is garbled (" + groups[i] + ")");
        }
    return true;
}

std::string indicator_of(const std::string& text) {
    Lines L = split(text);
    size_t i = find_fl5(L);
    if (i == std::string::npos || i + 1 >= L.v.size()) return "";
    auto w = words(L.v[i]);
    size_t n = dtg_words(w);
    if (w.size() != n + 1 || !is_indicator(w[n])) return "";
    auto g = words(L.v[i + 1]);
    if (g.size() != 2 || g[0] != "GR") return "";
    return w[n];
}

bool encipher(const cipher::Slot& slot, const std::string& plaindress, const std::string& indicator,
              const std::string& key, std::string& out, std::string* error) {
    out.clear();
    if (!slot.usable()) return fail(error, slot.why_unusable());
    if (!is_indicator(indicator)) return fail(error, "bad key indicator");
    Lines L = split(plaindress);
    size_t i5 = find_fl5(L);
    if (i5 == std::string::npos) return fail(error, "no date-time group line in the message");
    auto w5 = words(L.v[i5]);
    size_t n5 = dtg_words(w5);
    if (w5.size() != n5) return fail(error, "the date-time group line already carries a key indicator");
    std::string dtg = join(w5, 0, n5);
    // The text after format line 5, up to NNNN, without the blank lines before NNNN.
    size_t end = L.v.size();
    while (end > i5 + 1 && L.v[end - 1] != "NNNN") --end;
    if (end <= i5 + 1) return fail(error, "NNNN missing");
    size_t nnnn = end - 1;
    size_t last = nnnn;
    while (last > i5 + 1 && L.v[last - 1].find_first_not_of(' ') == std::string::npos) --last;
    std::string plain;
    for (size_t i = i5 + 1; i < last; ++i) plain += L.v[i] + (i + 1 < last ? "\n" : "");

    std::string air;
    bool ok = slot.encrypt(indicator, key, dtg, plain, air, error);
    crypto::wipe(plain);
    if (!ok) return false;

    std::string body;
    size_t ngroups = 0;
    if (slot.info().text) {
        // The module's own text: its lines as they are, the count in words.
        std::string cur;
        for (char c : air + "\n") {
            if (c == '\n') {
                auto w = words(cur);
                if (!w.empty()) {
                    body += join(w, 0, w.size()) + L.eol;
                    ngroups += w.size();
                }
                cur.clear();
            } else {
                cur += c;
            }
        }
    } else {
        // Pad (0x80 then zeros) to whole groups.
        air += static_cast<char>(0x80);
        while (air.size() % kGroupBytes) air += '\0';
        ngroups = air.size() / kGroupBytes;
        for (size_t g = 0; g < ngroups; ++g) {
            body += group_of(reinterpret_cast<const unsigned char*>(air.data()) + g * kGroupBytes);
            bool line_end = (g + 1) % kGroupsPerLine == 0 || g + 1 == ngroups;
            body += line_end ? L.eol : " ";
        }
    }
    if (ngroups == 0) return fail(error, "the cipher gave no groups");

    std::string o;
    for (size_t i = 0; i < i5; ++i) o += L.v[i] + L.eol;
    o += dtg + " " + indicator + L.eol;
    o += "GR " + std::to_string(ngroups) + L.eol;
    o += "BT" + L.eol;
    o += body;
    o += "BT";
    // The same ending as the plaindress: its blank lines, then NNNN.
    o += L.eol == "\r\r\n" ? "\r\r\n\n\n\n\n\n\n\n" : L.eol;
    o += "NNNN";
    out = digest::append(o);
    return true;
}

bool encipher(const std::string& plaindress, const std::string& indicator, const std::string& key, std::string& out,
              std::string* error) {
    return encipher(*cipher::Slot::builtin(), plaindress, indicator, key, out, error);
}

bool decipher(const cipher::Slot& slot, const std::string& codress, const std::string& key, std::string& plaindress,
              std::string* error) {
    plaindress.clear();
    if (!slot.usable()) return fail(error, slot.why_unusable());
    Lines L = split(codress);
    size_t i5 = find_fl5(L);
    if (i5 == std::string::npos || i5 + 2 >= L.v.size()) return fail(error, "not a codress message");
    auto w5 = words(L.v[i5]);
    size_t n5 = dtg_words(w5);
    if (w5.size() != n5 + 1 || !is_indicator(w5[n5])) return fail(error, "no key indicator after the date-time group");
    std::string indicator = w5[n5];
    std::string dtg = join(w5, 0, n5);
    auto gr = words(L.v[i5 + 1]);
    if (gr.size() != 2 || gr[0] != "GR" || gr[1].empty() || gr[1].size() > 5 ||
        gr[1].find_first_not_of("0123456789") != std::string::npos)
        return fail(error, "no group count line");
    size_t want = static_cast<size_t>(std::stoul(gr[1]));
    size_t i = i5 + 2;
    while (i < L.v.size() && L.v[i].find_first_not_of(' ') == std::string::npos) ++i;
    if (i >= L.v.size() || !is_bt(L.v[i])) return fail(error, "no BT before the groups");
    const bool text_form = slot.info().text;
    std::string air;
    size_t have = 0;
    for (++i; i < L.v.size() && !is_bt(L.v[i]); ++i) {
        auto w = words(L.v[i]);
        if (w.empty()) continue;
        if (w[0] == "PAGE") continue;  // page heading of a long message
        if (text_form) {
            air += join(w, 0, w.size()) + "\n";
            have += w.size();
            continue;
        }
        for (auto& g : w) {
            ++have;
            if (!bytes_of(g, air)) {
                crypto::wipe(air);
                return fail(error, "group " + std::to_string(have) + " is garbled (" + g + ")");
            }
        }
    }
    if (i >= L.v.size()) return fail(error, "no BT after the groups");
    if (have != want)
        return fail(error, "the count says GR " + std::to_string(want) + " but " + std::to_string(have) + " groups came");
    if (text_form) {
        if (!air.empty()) air.pop_back();
    } else {
        while (!air.empty() && air.back() == '\0') air.pop_back();
        if (air.empty() || static_cast<unsigned char>(air.back()) != 0x80) {
            crypto::wipe(air);
            return fail(error, "the groups do not end the way they should: damaged in transit, or from another cipher");
        }
        air.pop_back();
    }
    if (air.empty()) return fail(error, "too few groups");
    std::string plain;
    bool ok = slot.decrypt(indicator, key, dtg, air, plain, error);
    crypto::wipe(air);
    if (!ok) return false;

    std::string o;
    for (size_t k = 0; k < i5; ++k) o += L.v[k] + L.eol;
    o += dtg + L.eol;
    std::string cur;
    for (char c : plain) {
        if (c == '\n') {
            o += cur + L.eol;
            cur.clear();
        } else {
            cur += c;
        }
    }
    o += cur;
    o += L.eol == "\r\r\n" ? "\r\r\n\n\n\n\n\n\n\n" : L.eol;
    o += "NNNN";
    crypto::wipe(plain);
    crypto::wipe(cur);
    plaindress = o;
    return true;
}

bool decipher(const std::string& codress, const std::string& key, std::string& plaindress, std::string* error) {
    return decipher(*cipher::Slot::builtin(), codress, key, plaindress, error);
}

}  // namespace codress
}  // namespace pna
