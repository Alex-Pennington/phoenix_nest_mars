#include "parts.h"

#include "sha256.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace pna {
namespace parts {

namespace {

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        size_t s = 0;
        while (s < l.size() && l[s] == ' ') ++s;
        out.push_back(l.substr(s));
    }
    return out;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool good_kind(const std::string& k) { return k == "TAPE" || k == "BUNDLE"; }

bool good_tag(const std::string& t) {
    if (t.size() != 8) return false;
    for (char c : t)
        if (!std::isupper(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// "PART 2 OF 5 PNA TAPE 4HNPQ2XR", or the older "PNA BUNDLE PART 2 OF 5 TAG 4HNPQ2XR".
bool read_head(const std::string& line, Head& h) {
    std::string l = upper(line);
    char kind[16] = {0}, tag[16] = {0};
    int n = 0, of = 0;
    if (std::sscanf(l.c_str(), "PART %d OF %d PNA %15s %15s", &n, &of, kind, tag) == 4) {
        h = {kind, tag, n, of};
    } else if (std::sscanf(l.c_str(), "PNA BUNDLE PART %d OF %d TAG %15s", &n, &of, tag) == 3) {
        h = {"BUNDLE", tag, n, of};
    } else {
        return false;
    }
    return good_kind(h.kind) && good_tag(h.tag) && h.n >= 1 && h.n <= h.of && h.of <= 99;
}

bool is_end(const std::string& line, const Head& h) {
    std::string l = upper(line);
    return l == "END PART" || l == "END PART " + std::to_string(h.n) + " OF " + std::to_string(h.of);
}

}  // namespace

std::vector<std::string> split(const std::string& kind, const std::string& tag, const std::string& text,
                               size_t max_body) {
    std::vector<std::string> chunks;
    std::string cur;
    size_t room = max_body > 120 ? max_body - 80 : 40;
    for (auto& l : lines_of(text)) {
        if (l.empty()) continue;
        if (!cur.empty() && cur.size() + l.size() + 1 > room) {
            chunks.push_back(cur);
            cur.clear();
        }
        cur += l + "\n";
    }
    if (!cur.empty()) chunks.push_back(cur);
    std::vector<std::string> out;
    const std::string of = std::to_string(chunks.size());
    for (size_t i = 0; i < chunks.size(); ++i) {
        const std::string n = std::to_string(i + 1);
        out.push_back("PART " + n + " OF " + of + " PNA " + kind + " " + tag + "\n" + chunks[i] + "END PART " + n +
                      " OF " + of + "\n");
    }
    return out;
}

bool head(const std::string& text, Head& out) {
    for (auto& l : lines_of(text))
        if (read_head(l, out)) return true;
    return false;
}

std::string body(const std::string& text) {
    std::string o;
    Head h;
    bool in = false;
    for (auto& l : lines_of(text)) {
        if (!in) {
            in = read_head(l, h);
            continue;
        }
        if (is_end(l, h)) break;
        if (!l.empty()) o += upper(l) + "\n";
    }
    return o;
}

std::string cut(const std::string& text) {
    std::string o;
    Head h;
    bool in = false;
    for (auto& l : lines_of(text)) {
        if (!in) {
            if (!read_head(l, h)) continue;
            in = true;
            o = "PART " + std::to_string(h.n) + " OF " + std::to_string(h.of) + " PNA " + h.kind + " " + h.tag + "\n";
            continue;
        }
        if (is_end(l, h)) return o + "END PART " + std::to_string(h.n) + " OF " + std::to_string(h.of) + "\n";
        if (!l.empty()) o += upper(l) + "\n";
    }
    return "";
}

std::string tag_of(const std::string& text) {
    static const char* kAlpha = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::string d = sha256_hex(text);
    std::string t;
    for (size_t i = 0; i < 8; ++i) t += kAlpha[std::stoi(d.substr(i * 2, 2), nullptr, 16) % 32];
    return t;
}

std::string request_text(const std::string& kind, const std::string& tag, const std::vector<int>& missing) {
    std::string m;
    for (int n : missing) m += (m.empty() ? "" : " ") + std::to_string(n);
    return "PNA PARTS REQUEST " + kind + " " + tag + "\nMISSING " + m + "\nEND REQUEST\n";
}

bool read_request(const std::string& text, std::string& kind, std::string& tag, std::vector<int>& missing) {
    bool in = false;
    missing.clear();
    for (auto& raw : lines_of(text)) {
        std::string l = upper(raw);
        if (!in) {
            char k[16] = {0}, t[16] = {0};
            if (std::sscanf(l.c_str(), "PNA PARTS REQUEST %15s %15s", k, t) == 2 && good_kind(k) && good_tag(t)) {
                kind = k;
                tag = t;
                in = true;
            }
            continue;
        }
        if (l == "END REQUEST") return !missing.empty();
        if (l.rfind("MISSING ", 0) == 0) {
            std::istringstream in2(l.substr(8));
            int n = 0;
            while (in2 >> n)
                if (n >= 1 && n <= 99) missing.push_back(n);
        }
    }
    return false;
}

}  // namespace parts
}  // namespace pna
