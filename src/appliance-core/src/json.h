// Just enough JSON for the local API: requests are flat objects whose values
// are strings, numbers or booleans; replies are built with JsonWriter.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pna {

// Parses {"key": "value", "n": 3, "b": true}. Every value comes back as a
// string ("3", "true"). Returns false on anything else.
bool parse_flat_object(const std::string& text, std::map<std::string, std::string>& out);

std::string json_escape(const std::string& s);

// A whole JSON value, for files other programs write (greybeard-ale's
// CAPTURE.modems.json). Numbers are kept as doubles.
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    bool is_object() const { return type == Type::Object; }
    bool is_array() const { return type == Type::Array; }
    // Member `k` of an object, or nullptr.
    const JsonValue* get(const std::string& k) const;
};

// Parses one JSON value (nesting at most 32 deep). Returns false on anything else.
bool parse_json(const std::string& text, JsonValue& out);

class JsonWriter {
public:
    JsonWriter& begin_object();
    JsonWriter& begin_object(const std::string& key);  // a member object
    JsonWriter& end_object();
    JsonWriter& begin_array(const std::string& key);
    JsonWriter& end_array();
    JsonWriter& field(const std::string& key, const std::string& v);
    JsonWriter& field(const std::string& key, const char* v) { return field(key, std::string(v)); }
    JsonWriter& field(const std::string& key, long long v);
    JsonWriter& field(const std::string& key, int v) { return field(key, static_cast<long long>(v)); }
    JsonWriter& field(const std::string& key, bool v);
    JsonWriter& field_null(const std::string& key);
    // A member whose value is JSON text already made (passed through from
    // another program's reply). The caller vouches that it is valid JSON.
    JsonWriter& raw(const std::string& key, const std::string& json);
    const std::string& str() const { return s_; }

private:
    void sep();
    void key(const std::string& k);
    std::string s_;
    bool first_ = true;
};

}  // namespace pna
