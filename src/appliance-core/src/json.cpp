#include "json.h"

#include <cctype>
#include <cstdio>
#include <string>

namespace pna {

namespace {

void skip_ws(const std::string& t, size_t& i) {
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) ++i;
}

void put_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool parse_string(const std::string& t, size_t& i, std::string& out) {
    if (i >= t.size() || t[i] != '"') return false;
    ++i;
    out.clear();
    while (i < t.size()) {
        char c = t[i++];
        if (c == '"') return true;
        if (c != '\\') {
            out += c;
            continue;
        }
        if (i >= t.size()) return false;
        char e = t[i++];
        switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (i + 4 > t.size()) return false;
                unsigned cp = 0;
                for (int k = 0; k < 4; ++k) {
                    char h = t[i++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= h - '0';
                    else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                    else return false;
                }
                put_utf8(out, cp);
                break;
            }
            default: return false;
        }
    }
    return false;
}

bool parse_value(const std::string& t, size_t& i, JsonValue& v, int depth) {
    if (depth > 32) return false;
    skip_ws(t, i);
    if (i >= t.size()) return false;
    char c = t[i];
    if (c == '"') {
        v.type = JsonValue::Type::String;
        return parse_string(t, i, v.s);
    }
    if (c == '{') {
        v.type = JsonValue::Type::Object;
        ++i;
        skip_ws(t, i);
        if (i < t.size() && t[i] == '}') return ++i, true;
        for (;;) {
            std::string k;
            skip_ws(t, i);
            if (!parse_string(t, i, k)) return false;
            skip_ws(t, i);
            if (i >= t.size() || t[i] != ':') return false;
            ++i;
            if (!parse_value(t, i, v.obj[k], depth + 1)) return false;
            skip_ws(t, i);
            if (i < t.size() && t[i] == ',') { ++i; continue; }
            if (i < t.size() && t[i] == '}') return ++i, true;
            return false;
        }
    }
    if (c == '[') {
        v.type = JsonValue::Type::Array;
        ++i;
        skip_ws(t, i);
        if (i < t.size() && t[i] == ']') return ++i, true;
        for (;;) {
            v.arr.emplace_back();
            if (!parse_value(t, i, v.arr.back(), depth + 1)) return false;
            skip_ws(t, i);
            if (i < t.size() && t[i] == ',') { ++i; continue; }
            if (i < t.size() && t[i] == ']') return ++i, true;
            return false;
        }
    }
    if (t.compare(i, 4, "true") == 0) { v.type = JsonValue::Type::Bool; v.b = true; i += 4; return true; }
    if (t.compare(i, 5, "false") == 0) { v.type = JsonValue::Type::Bool; i += 5; return true; }
    if (t.compare(i, 4, "null") == 0) { v.type = JsonValue::Type::Null; i += 4; return true; }
    size_t s = i;
    while (i < t.size() && (std::isdigit(static_cast<unsigned char>(t[i])) || t[i] == '-' || t[i] == '+' ||
                            t[i] == '.' || t[i] == 'e' || t[i] == 'E'))
        ++i;
    if (s == i) return false;
    try {
        size_t used = 0;
        v.n = std::stod(t.substr(s, i - s), &used);
        if (used != i - s) return false;
    } catch (...) {
        return false;
    }
    v.type = JsonValue::Type::Number;
    return true;
}

}  // namespace

const JsonValue* JsonValue::get(const std::string& k) const {
    if (type != Type::Object) return nullptr;
    auto it = obj.find(k);
    return it == obj.end() ? nullptr : &it->second;
}

bool parse_json(const std::string& t, JsonValue& out) {
    size_t i = 0;
    out = JsonValue();
    if (!parse_value(t, i, out, 0)) return false;
    skip_ws(t, i);
    return i == t.size();
}

bool parse_flat_object(const std::string& t, std::map<std::string, std::string>& out) {
    size_t i = 0;
    skip_ws(t, i);
    if (i >= t.size() || t[i] != '{') return false;
    ++i;
    skip_ws(t, i);
    if (i < t.size() && t[i] == '}') return true;
    for (;;) {
        std::string k, v;
        skip_ws(t, i);
        if (!parse_string(t, i, k)) return false;
        skip_ws(t, i);
        if (i >= t.size() || t[i] != ':') return false;
        ++i;
        skip_ws(t, i);
        if (i < t.size() && t[i] == '"') {
            if (!parse_string(t, i, v)) return false;
        } else {
            size_t s = i;
            while (i < t.size() && t[i] != ',' && t[i] != '}' && t[i] != ' ' && t[i] != '\n') ++i;
            v = t.substr(s, i - s);
            if (v.empty()) return false;
            if (v != "true" && v != "false") {
                for (char c : v)
                    if (!((c >= '0' && c <= '9') || c == '-' || c == '.')) return false;
            }
        }
        out[k] = v;
        skip_ws(t, i);
        if (i < t.size() && t[i] == ',') {
            ++i;
            continue;
        }
        if (i < t.size() && t[i] == '}') {
            ++i;
            skip_ws(t, i);
            return i == t.size();
        }
        return false;
    }
}

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o;
}

void JsonWriter::sep() {
    if (!first_) s_ += ',';
    first_ = false;
}

void JsonWriter::key(const std::string& k) {
    sep();
    if (!k.empty()) s_ += "\"" + json_escape(k) + "\":";
}

JsonWriter& JsonWriter::begin_object() {
    sep();
    s_ += '{';
    first_ = true;
    return *this;
}

JsonWriter& JsonWriter::begin_object(const std::string& k) {
    key(k);
    s_ += '{';
    first_ = true;
    return *this;
}

JsonWriter& JsonWriter::end_object() {
    s_ += '}';
    first_ = false;
    return *this;
}

JsonWriter& JsonWriter::begin_array(const std::string& k) {
    key(k);
    s_ += '[';
    first_ = true;
    return *this;
}

JsonWriter& JsonWriter::end_array() {
    s_ += ']';
    first_ = false;
    return *this;
}

JsonWriter& JsonWriter::field(const std::string& k, const std::string& v) {
    key(k);
    s_ += "\"" + json_escape(v) + "\"";
    return *this;
}

JsonWriter& JsonWriter::field(const std::string& k, long long v) {
    key(k);
    s_ += std::to_string(v);
    return *this;
}

JsonWriter& JsonWriter::field(const std::string& k, bool v) {
    key(k);
    s_ += v ? "true" : "false";
    return *this;
}

JsonWriter& JsonWriter::field_null(const std::string& k) {
    key(k);
    s_ += "null";
    return *this;
}

}  // namespace pna
