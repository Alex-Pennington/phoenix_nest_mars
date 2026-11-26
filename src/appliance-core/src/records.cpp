// Record classes: see records.h.
#include "records.h"

#include "json.h"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace pna {
namespace records {

namespace {

std::mutex g_mu;
std::set<std::string> g_locked;

std::vector<Kind> make_kinds() {
    // station_restore puts back every kind, so it is in each list. An import
    // or a bundle that can change a kind is in that kind's list.
    return {
        {"channels", "Channel plan", {"channel_put", "channel_remove", "mars_import", "bundle_load", "station_restore"}},
        {"schedule", "Net schedule", {"sched_put", "sched_remove", "mars_import", "bundle_load", "station_restore"}},
        {"book", "Station book", {"book_put", "book_remove", "book_import", "mars_import", "bundle_load", "station_restore"}},
        {"key-tape", "Key tape", {"key_import", "key_open", "key_zeroize", "net_tape", "station_restore"}},
        {"cipher", "Cipher module", {"cipher_install", "cipher_remove", "handout_import", "station_restore"}},
        {"cards", "Cards and keymasters",
         {"card_import", "card_check", "card_keymaster", "keymaster_accept", "keymaster_refuse", "station_restore"}},
        {"handouts", "Member handouts", {"handout_import", "station_restore"}},
        {"forms", "Form templates", {"form_template_put", "form_template_reset", "station_restore"}},
        {"guide", "Guide pages", {"guide_page_put", "guide_page_reset", "station_restore"}},
        {"bbs-rules", "BBS guard list, aliases and routes",
         {"guard_set", "alias_set", "alias_del", "route_set", "route_del", "route_move", "station_restore"}},
    };
}

std::string norm(std::string k) {
    for (auto& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::replace(k.begin(), k.end(), '_', '-');
    size_t a = k.find_first_not_of(' '), b = k.find_last_not_of(' ');
    return a == std::string::npos ? "" : k.substr(a, b - a + 1);
}

}  // namespace

const std::vector<Kind>& kinds() {
    static const std::vector<Kind> k = make_kinds();
    return k;
}

const Kind* find(const std::string& key) {
    std::string k = norm(key);
    for (auto& x : kinds())
        if (x.key == k) return &x;
    return nullptr;
}

bool set_locked(const std::vector<std::string>& keys, std::string* error) {
    std::set<std::string> out;
    for (auto& k : keys) {
        const Kind* x = find(k);
        if (!x) {
            if (error) {
                std::string list;
                for (auto& y : kinds()) list += (list.empty() ? "" : ", ") + y.key;
                *error = "lock: \"" + k + "\" isn't a kind of record (" + list + ")";
            }
            return false;
        }
        out.insert(x->key);
    }
    std::lock_guard<std::mutex> g(g_mu);
    g_locked = out;
    return true;
}

std::set<std::string> locked() {
    std::lock_guard<std::mutex> g(g_mu);
    return g_locked;
}

std::string refusal(const std::string& cmd, std::string* kind) {
    std::lock_guard<std::mutex> g(g_mu);
    if (g_locked.empty()) return "";
    for (auto& x : kinds()) {
        if (!g_locked.count(x.key)) continue;
        if (std::find(x.calls.begin(), x.calls.end(), cmd) == x.calls.end()) continue;
        if (kind) *kind = x.key;
        return "locked: " + x.label + " (" + x.key + ") is locked in appliance.conf on this box: " + cmd +
               " can't change it from here";
    }
    return "";
}

void write(JsonWriter& w) {
    auto lk = locked();
    w.begin_array("records");
    for (auto& x : kinds()) {
        w.begin_object()
            .field("kind", x.key)
            .field("label", x.label)
            .field("class", lk.count(x.key) ? "locked" : "operator")
            .begin_array("calls");
        for (auto& c : x.calls) w.field("", c);
        w.end_array().end_object();
    }
    w.end_array();
}

}  // namespace records
}  // namespace pna
