#include "api_forms.h"

#include "api_server.h"
#include "bundle.h"
#include "core.h"
#include "forms.h"
#include "json.h"
#include "parts.h"

#include <algorithm>
#include <memory>
#include <set>

namespace pna {

namespace {

JsonValue call(Core& core, const std::string& line) {
    JsonValue v;
    parse_json(ApiServer::handle(core, line), v);
    return v;
}
// A reply kept for the length of a loop over it (a temporary would be gone
// before the loop reads it).
const JsonValue& held(Core& core, const std::string& line) {
    thread_local std::vector<std::unique_ptr<JsonValue>> keep;
    if (keep.size() > 16) keep.erase(keep.begin());
    keep.push_back(std::make_unique<JsonValue>(call(core, line)));
    return *keep.back();
}
std::string s_of(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
bool b_of(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::Bool && x->b;
}
const std::vector<JsonValue>& arr(const JsonValue& v, const char* k) {
    static const std::vector<JsonValue> none;
    const JsonValue* x = v.get(k);
    return x && x->is_array() ? x->arr : none;
}
std::string joined(const JsonValue& v, const char* k) {
    std::string out;
    for (auto& e : arr(v, k))
        if (e.type == JsonValue::Type::String) out += (out.empty() ? "" : ", ") + e.s;
    return out;
}
std::string flat(std::initializer_list<std::pair<std::string, std::string>> kv) {
    JsonWriter w;
    w.begin_object();
    for (auto& [k, v] : kv) w.field(k, v);
    w.end_object();
    return w.str();
}

// What kind of thing a received message carries.
std::string carries(const std::string& body) {
    parts::Head h;
    if (parts::head(body, h)) return h.kind == "TAPE" ? "tape" : "bundle";
    if (body.find("PNA STARTUP BUNDLE 1") != std::string::npos) return "bundle";
    if (body.find("PNA SEALED TAPE 1") != std::string::npos) return "tape";
    if (body.find("PNA STATION CARD 1") != std::string::npos) return "card";
    return "";
}

std::vector<forms::Choice> choices(Core& core, const forms::Library& lib, const std::string& src) {
    std::vector<forms::Choice> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& value, const std::string& label) {
        if (!value.empty() && seen.insert(value).second) out.push_back({value, label});
    };
    if (src == "@channels") {
        for (auto& c : arr(held(core, flat({{"cmd", "channels_list"}})), "channels")) {
            std::string use = s_of(c, "use");
            add(s_of(c, "name"), s_of(c, "name") + (use.empty() ? "" : " " + use));
        }
    } else if (src == "@cards" || src == "@cards_unchecked") {
        for (auto& c : arr(held(core, flat({{"cmd", "cards_list"}})), "cards")) {
            bool checked = b_of(c, "checked");
            if (src == "@cards_unchecked" && checked) continue;
            add(s_of(c, "station"), s_of(c, "station") + (checked ? " (checked)" : " (not checked)"));
        }
    } else if (src == "@stations" || src == "@nets" || src == "@book") {
        for (auto& e : arr(held(core, flat({{"cmd", "book_list"}})), "entries")) {
            std::string kind = s_of(e, "kind");
            if (src == "@stations" && kind == "collective") continue;
            if (src == "@nets" && kind != "collective") continue;
            std::string v = src == "@stations" && !s_of(e, "call").empty() ? s_of(e, "call") : s_of(e, "name");
            add(v, s_of(e, "name") == v ? v : v + " " + s_of(e, "name"));
        }
        if (src == "@stations")
            for (auto& c : arr(held(core, flat({{"cmd", "cards_list"}})), "cards")) add(s_of(c, "station"), s_of(c, "station"));
    } else if (src == "@schedules") {
        for (auto& sc : arr(held(core, flat({{"cmd", "sched_list"}})), "schedules")) add(s_of(sc, "name"), s_of(sc, "name"));
    } else if (src == "@keymasters_undecided") {
        for (auto& k : arr(held(core, flat({{"cmd", "keymaster_list"}})), "undecided"))
            add(s_of(k, "station"), s_of(k, "question"));
    } else if (src == "@forms") {
        for (auto& e : lib.list()) add(e.t.id, e.t.id + " " + e.t.title);
    } else if (src == "@parts_waiting" || src == "@parts_requests") {
        JsonValue r = call(core, flat({{"cmd", "parts_list"}}));
        const char* list = src == "@parts_waiting" ? "sets" : "requests";
        for (auto& e : arr(r, list)) {
            std::string missing;
            for (auto& n : arr(e, "missing"))
                if (n.type == JsonValue::Type::Number) missing += (missing.empty() ? "" : " ") + std::to_string(static_cast<int>(n.n));
            if (src == "@parts_waiting") {
                if (s_of(e, "state") != "waiting") continue;
                add(s_of(e, "id"), s_of(e, "kind") + " from " + s_of(e, "from") + ", tag " + s_of(e, "tag") +
                                       ", missing parts " + missing);
            } else {
                add(s_of(e, "id"), s_of(e, "from") + " asks for parts " + missing + " of " + s_of(e, "kind") + " " +
                                       s_of(e, "tag"));
            }
        }
    } else if (src == "@msg_cards" || src == "@msg_bundles" || src == "@msg_tapes") {
        std::string want = src == "@msg_cards" ? "card" : src == "@msg_bundles" ? "bundle" : "tape";
        // A set of parts is one choice, under the id of its first part to
        // come, so the choice stays the same as more parts (or repeats) arrive.
        struct Group {
            std::string id, src, tag;
            int of = 0;
            std::set<int> have;
        };
        std::vector<Group> groups;  // newest first
        auto msgs = core.list(300);
        for (auto& m : msgs) {
            if (m.direction != "in" || carries(m.body) != want) continue;
            parts::Head ph;
            if (!parts::head(m.body, ph)) {
                groups.push_back({m.id, m.src, "", 0, {}});
                continue;
            }
            auto g = std::find_if(groups.begin(), groups.end(),
                                  [&](const Group& x) { return x.tag == ph.tag && x.src == m.src; });
            if (g == groups.end()) {
                groups.push_back({m.id, m.src, ph.tag, ph.of, {}});
                g = groups.end() - 1;
            }
            g->id = m.id;  // the list is newest first: this ends on the first to come
            g->have.insert(ph.n);
        }
        for (auto& g : groups) {
            std::string label = want + " from " + g.src;
            if (!g.tag.empty())
                label += ", tag " + g.tag + ", " + std::to_string(std::min<int>(static_cast<int>(g.have.size()), g.of)) +
                         " of " + std::to_string(g.of) + " parts in";
            add(g.id, label);
        }
    }
    return out;
}

std::map<std::string, std::string> prefill(Core& core, const forms::Library& lib, const std::string& kind,
                                           const std::string& key) {
    std::map<std::string, std::string> v;
    std::string k = key;
    for (auto& c : k) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (kind == "book") {
        JsonValue r = call(core, flat({{"cmd", "book_get"}, {"name", key}}));
        auto& es = arr(r, "entries");
        if (es.empty()) return v;
        auto& e = es[0];
        for (auto f : {"name", "kind", "call", "ri", "ale", "note"}) v[f] = s_of(e, f);
        for (auto& c : v["kind"]) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        v["members"] = joined(e, "members");
    } else if (kind == "channel") {
        for (auto& c : arr(held(core, flat({{"cmd", "channels_list"}})), "channels"))
            if (s_of(c, "name") == k) {
                v["name"] = k;
                v["mode"] = s_of(c, "mode");
                v["use"] = s_of(c, "use");
                v["frequency?"] = "held";  // held here; never shown
            }
    } else if (kind == "schedule") {
        for (auto& sc : arr(held(core, flat({{"cmd", "sched_list"}})), "schedules"))
            if (s_of(sc, "name") == k || s_of(sc, "name") == key) {
                for (auto f : {"name", "days", "time", "ncs", "note", "channel"}) v[f] = s_of(sc, f);
                const JsonValue* m = sc.get("minutes");
                if (m && m->type == JsonValue::Type::Number) v["minutes"] = std::to_string(static_cast<long long>(m->n));
                v["roster"] = joined(sc, "roster");
            }
    } else if (kind == "template") {
        if (auto e = lib.get(k)) {
            v["template"] = e->t.id;
            v["text"] = e->t.text;
        }
    }
    return v;
}

}  // namespace

std::string forms_dir(const Core& core) {
    std::string db = core.db_path();
    if (db.size() > 3 && db.compare(db.size() - 3, 3, ".db") == 0) db.resize(db.size() - 3);
    return db + ".forms";
}

std::optional<std::string> handle_forms(Core& core, const std::string& cmd, std::map<std::string, std::string>& req) {
    if (cmd.rfind("form_", 0) != 0) return std::nullopt;
    forms::Library lib(forms_dir(core));
    forms::Context ctx;
    ctx.choices = [&](const std::string& src) { return choices(core, lib, src); };
    ctx.prefill = [&](const std::string& kind, const std::string& key) { return prefill(core, lib, kind, key); };
    ctx.run = [&](const std::string& line) { return ApiServer::handle(core, line); };
    // The station is set up: SETUP is done.
    std::vector<std::string> ids;
    for (auto& e : lib.list())
        if (e.t.id != "SETUP") ids.push_back(e.t.id);
    ctx.only = ids.empty() ? std::vector<std::string>{"-"} : ids;
    if (cmd == "form_template_put" || cmd == "form_template_reset") ctx.only.clear();
    if (cmd == "form_template_put" || cmd == "form_template_get" || cmd == "form_template_reset") ctx.only.clear();
    auto out = forms::handle(lib, ctx, cmd, req);
    if (out && cmd == "form_submit") {
        auto f = req.find("form");
        core.note("form " + (f == req.end() ? std::string("?") : f->second) + " submitted");
    }
    return out;
}

}  // namespace pna
