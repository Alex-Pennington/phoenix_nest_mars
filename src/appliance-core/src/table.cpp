#include "table.h"

#include <expat.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>

namespace pna {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

uint32_t le32(const std::string& s, size_t at) {
    return static_cast<uint32_t>(static_cast<unsigned char>(s[at])) |
           static_cast<uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8 |
           static_cast<uint32_t>(static_cast<unsigned char>(s[at + 2])) << 16 |
           static_cast<uint32_t>(static_cast<unsigned char>(s[at + 3])) << 24;
}
uint16_t le16(const std::string& s, size_t at) {
    return static_cast<uint16_t>(static_cast<unsigned char>(s[at]) | static_cast<unsigned char>(s[at + 1]) << 8);
}

// The files in a zip, by name: stored or deflated entries only. Limits keep a
// hostile file from taking the box's memory.
bool unzip(const std::string& z, std::map<std::string, std::string>& files, std::string* error) {
    const size_t kMaxFile = 64u << 20;
    // End of central directory: the last 22..65557 bytes.
    if (z.size() < 22) return false;
    size_t eocd = std::string::npos;
    for (size_t i = z.size() - 22 + 1; i-- > 0 && z.size() - i <= 65557;)
        if (le32(z, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd == std::string::npos) {
        if (error) *error = "the spreadsheet's zip directory is missing";
        return false;
    }
    size_t count = le16(z, eocd + 10), at = le32(z, eocd + 16);
    for (size_t n = 0; n < count; ++n) {
        if (at + 46 > z.size() || le32(z, at) != 0x02014b50) break;
        uint16_t method = le16(z, at + 10);
        uint32_t csize = le32(z, at + 20), usize = le32(z, at + 24);
        uint16_t nlen = le16(z, at + 28), xlen = le16(z, at + 30), clen = le16(z, at + 32);
        uint32_t local = le32(z, at + 42);
        if (at + 46 + nlen > z.size()) break;
        std::string name = z.substr(at + 46, nlen);
        at += 46 + nlen + xlen + clen;
        if (local + 30 > z.size() || le32(z, local) != 0x04034b50) continue;
        size_t data = local + 30 + le16(z, local + 26) + le16(z, local + 28);
        if (data + csize > z.size() || usize > kMaxFile) continue;
        if (method == 0) {
            files[name] = z.substr(data, csize);
        } else if (method == 8) {
            std::string out(usize, '\0');
            z_stream st{};
            if (inflateInit2(&st, -MAX_WBITS) != Z_OK) continue;
            st.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(z.data() + data));
            st.avail_in = csize;
            st.next_out = reinterpret_cast<Bytef*>(&out[0]);
            st.avail_out = usize;
            int rc = inflate(&st, Z_FINISH);
            inflateEnd(&st);
            if (rc == Z_STREAM_END) files[name] = out.substr(0, st.total_out);
        }
    }
    return true;
}

// A small expat walk: start, end and text callbacks with the element's local
// name (the part after any prefix).
struct Xml {
    std::function<void(const std::string&, const char**)> start;
    std::function<void(const std::string&)> end;
    std::function<void(const char*, int)> text;
    static std::string local(const char* n) {
        const char* c = std::strrchr(n, ':');
        return c ? c + 1 : n;
    }
    bool run(const std::string& doc) {
        XML_Parser p = XML_ParserCreate("UTF-8");
        XML_SetUserData(p, this);
        XML_SetElementHandler(
            p,
            [](void* u, const char* n, const char** a) {
                auto* x = static_cast<Xml*>(u);
                if (x->start) x->start(local(n), a);
            },
            [](void* u, const char* n) {
                auto* x = static_cast<Xml*>(u);
                if (x->end) x->end(local(n));
            });
        XML_SetCharacterDataHandler(p, [](void* u, const char* s, int len) {
            auto* x = static_cast<Xml*>(u);
            if (x->text) x->text(s, len);
        });
        bool ok = XML_Parse(p, doc.data(), static_cast<int>(doc.size()), 1) == XML_STATUS_OK;
        XML_ParserFree(p);
        return ok;
    }
};

const char* attr(const char** a, const char* name) {
    for (; a && *a; a += 2)
        if (Xml::local(a[0]) == name) return a[1];
    return nullptr;
}

// "C12" -> column 2 (A is 0).
int column_of(const std::string& ref) {
    int c = 0;
    bool any = false;
    for (char ch : ref) {
        if (ch < 'A' || ch > 'Z') break;
        c = c * 26 + (ch - 'A' + 1);
        any = true;
    }
    return any ? c - 1 : -1;
}

bool read_xlsx(const std::string& bytes, std::vector<Table>& out, std::string* error) {
    std::map<std::string, std::string> f;
    if (!unzip(bytes, f, error)) return false;
    if (!f.count("xl/workbook.xml")) {
        if (error) *error = "not a spreadsheet: it has no workbook";
        return false;
    }
    std::vector<std::string> shared;
    if (f.count("xl/sharedStrings.xml")) {
        Xml x;
        bool in_si = false, in_t = false;
        std::string cur;
        x.start = [&](const std::string& n, const char**) {
            if (n == "si") in_si = true, cur.clear();
            if (n == "t" && in_si) in_t = true;
        };
        x.end = [&](const std::string& n) {
            if (n == "t") in_t = false;
            if (n == "si") in_si = false, shared.push_back(cur);
        };
        x.text = [&](const char* s, int len) {
            if (in_t) cur.append(s, static_cast<size_t>(len));
        };
        x.run(f["xl/sharedStrings.xml"]);
    }
    // Sheet names and their files, in workbook order.
    std::map<std::string, std::string> rel_target;
    {
        Xml x;
        x.start = [&](const std::string& n, const char** a) {
            if (n != "Relationship") return;
            const char* id = attr(a, "Id");
            const char* t = attr(a, "Target");
            if (id && t) {
                std::string target = t;
                if (target.rfind("/xl/", 0) == 0) target = target.substr(4);
                rel_target[id] = "xl/" + target;
            }
        };
        x.run(f["xl/_rels/workbook.xml.rels"]);
    }
    std::vector<std::pair<std::string, std::string>> sheets;
    {
        Xml x;
        x.start = [&](const std::string& n, const char** a) {
            if (n != "sheet") return;
            const char* name = attr(a, "name");
            const char* rid = attr(a, "id");
            std::string file = rid && rel_target.count(rid) ? rel_target[rid] : "";
            if (file.empty()) file = "xl/worksheets/sheet" + std::to_string(sheets.size() + 1) + ".xml";
            sheets.push_back({name ? name : "", file});
        };
        x.run(f["xl/workbook.xml"]);
    }
    for (auto& [name, file] : sheets) {
        if (!f.count(file)) continue;
        Table t;
        t.sheet = name;
        Row row;
        int col = 0;
        std::string type, val;
        bool in_v = false, in_t = false;
        Xml x;
        x.start = [&](const std::string& n, const char** a) {
            if (n == "row") row.clear(), col = 0;
            if (n == "c") {
                const char* r = attr(a, "r");
                const char* ty = attr(a, "t");
                int c = r ? column_of(r) : -1;
                col = c >= 0 ? c : static_cast<int>(row.size());
                type = ty ? ty : "";
                val.clear();
            }
            if (n == "v") in_v = true;
            if (n == "t") in_t = true;
        };
        x.end = [&](const std::string& n) {
            if (n == "v") in_v = false;
            if (n == "t") in_t = false;
            if (n == "c") {
                std::string cell = val;
                if (type == "s") {
                    size_t i = static_cast<size_t>(std::strtoul(val.c_str(), nullptr, 10));
                    cell = i < shared.size() ? shared[i] : "";
                }
                if (col < 4096) {
                    if (static_cast<int>(row.size()) <= col) row.resize(static_cast<size_t>(col) + 1);
                    row[static_cast<size_t>(col)] = trim(cell);
                }
            }
            if (n == "row") t.rows.push_back(row);
        };
        x.text = [&](const char* s, int len) {
            if (in_v || in_t) val.append(s, static_cast<size_t>(len));
        };
        x.run(f[file]);
        out.push_back(std::move(t));
    }
    if (out.empty()) {
        if (error) *error = "the spreadsheet has no sheets";
        return false;
    }
    return true;
}

// One row of delimited text, with "quoted, cells" and "" for a quote.
Row split_delimited(const std::string& line, char sep) {
    Row r;
    std::string cur;
    bool q = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (q) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') cur += '"', ++i;
            else if (c == '"') q = false;
            else cur += c;
        } else if (c == '"' && trim(cur).empty()) {
            cur.clear();
            q = true;
        } else if (c == sep) {
            r.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    r.push_back(trim(cur));
    return r;
}

// Columns lined up with two or more spaces.
Row split_spaced(const std::string& line) {
    Row r;
    std::string cur;
    int spaces = 0;
    for (char c : line) {
        if (c == ' ') {
            ++spaces;
            continue;
        }
        if (spaces >= 2 && !cur.empty()) r.push_back(cur), cur.clear();
        else if (spaces == 1 && !cur.empty()) cur += ' ';
        spaces = 0;
        cur += c;
    }
    if (!cur.empty()) r.push_back(cur);
    return r;
}

bool read_text(const std::string& bytes, Table& t, std::string* error) {
    std::string s = bytes;
    if (s.rfind("\xEF\xBB\xBF", 0) == 0) s = s.substr(3);
    for (unsigned char c : s)
        if (c == 0) {
            if (error) *error = "the file isn't text or a spreadsheet";
            return false;
        }
    std::vector<std::string> lines;
    std::istringstream in(s);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    // The separator that appears most often, the same number of times, on
    // most lines.
    char best = 0;
    size_t best_score = 0;
    for (char sep : {'\t', ',', ';', '|'}) {
        std::map<size_t, size_t> counts;
        for (auto& l : lines) {
            size_t n = static_cast<size_t>(std::count(l.begin(), l.end(), sep));
            if (n) ++counts[n];
        }
        size_t score = 0;
        for (auto& [n, c] : counts) score = std::max(score, c);
        if (score > best_score) best = sep, best_score = score;
    }
    for (auto& l : lines) {
        if (trim(l).empty()) {
            t.rows.push_back({});
            continue;
        }
        t.rows.push_back(best && best_score >= 2 ? split_delimited(l, best) : split_spaced(l));
    }
    return true;
}

}  // namespace

bool read_tables(const std::string& bytes, std::vector<Table>& out, std::string* error) {
    out.clear();
    if (bytes.rfind("PK\x03\x04", 0) == 0) return read_xlsx(bytes, out, error);
    Table t;
    if (!read_text(bytes, t, error)) return false;
    out.push_back(std::move(t));
    return true;
}

bool base64_decode(const std::string& in, std::string& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        else return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return true;
}

}  // namespace pna
