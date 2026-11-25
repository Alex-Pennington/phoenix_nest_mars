// The guide's calls on a running station (guide.h): pages from the
// station's own copy of the guide, form pages from its forms, message-type
// pages from compose, and the walkthrough's steps from what it holds.
#include "api_guide.h"

#include "api_forms.h"
#include "api_server.h"
#include "channels.h"
#include "compose.h"
#include "core.h"
#include "forms.h"
#include "guide.h"
#include "json.h"
#include "keys.h"
#include "ops.h"
#include "parts.h"

namespace pna {

std::string guide_dir(const Core& core) {
    std::string db = core.db_path();
    if (db.size() > 3 && db.compare(db.size() - 3, 3, ".db") == 0) db.resize(db.size() - 3);
    return db + ".guide";
}

namespace {

// A message an operator wrote, not one the box sends for itself (a card,
// a tape, a bundle or a part of one, a request for parts).
bool operator_message(const Message& m) {
    const std::string& b = m.body;
    parts::Head h;
    if (parts::head(b, h)) return false;
    for (const char* head : {"PNA STARTUP BUNDLE 1", "PNA SEALED TAPE 1", "PNA STATION CARD 1", "PNA PARTS REQUEST"})
        if (b.find(head) != std::string::npos) return false;
    return true;
}

std::vector<guide::Step> steps_of(Core& core) {
    auto s = guide::setup_steps(!core.station().empty() && core.identity() != nullptr);
    bool checked = false, keymaster = false;
    for (auto& c : core.cards()) {
        checked = checked || c.verified;
        keymaster = keymaster || c.keymaster;
    }
    auto set = [&](const char* id, bool done) {
        for (auto& x : s)
            if (x.id == id) x.done = done;
    };
    set("CARD", checked || keymaster);
    set("KEYMASTER", keymaster);
    if (Ops* ops = core.ops()) {
        set("CHANNELS", !ops->channels().list().empty());
        set("SCHEDULE", !ops->sched_list().empty());
    }
    if (keys::Keyring* kr = core.keyring()) set("KEYS", !kr->for_sending().empty());
    bool sent = false;
    for (auto& m : core.list(500))
        if (m.direction == "out" && m.state != "cancelled" && operator_message(m)) {
            sent = true;
            break;
        }
    set("FIRST", sent);
    return s;
}

}  // namespace

std::optional<std::string> handle_guide(Core& core, const std::string& cmd, std::map<std::string, std::string>& req,
                                        bool training) {
    if (cmd.rfind("guide_", 0) != 0) return std::nullopt;
    guide::Library pages(guide_dir(core));
    forms::Library forms(forms_dir(core));
    guide::Context ctx;
    ctx.pages = &pages;
    ctx.forms = &forms;
    ctx.training = training;
    ctx.types = [] {
        std::vector<std::map<std::string, std::string>> out;
        for (auto& t : compose::templates()) {
            std::string procs;
            for (auto& p : t.procedures) {
                const compose::Procedure* pr = compose::find_procedure(p);
                procs += (procs.empty() ? "" : ", ") + (pr ? pr->title : p);
            }
            out.push_back({{"id", t.id}, {"title", t.title}, {"summary", t.summary}, {"procedures", procs}});
        }
        return out;
    };
    ctx.type_fields = [&core](const std::string& id) {
        std::vector<std::map<std::string, std::string>> out;
        JsonWriter q;
        q.begin_object().field("cmd", "compose_form").field("template", id).end_object();
        JsonValue v;
        if (!parse_json(ApiServer::handle_call(core, q.str()), v)) return out;
        const JsonValue* f = v.get("fields");
        if (!f || !f->is_array()) return out;
        for (auto& x : f->arr) {
            auto s = [&](const char* k) {
                const JsonValue* y = x.get(k);
                return y && y->type == JsonValue::Type::String ? y->s : std::string();
            };
            const JsonValue* r = x.get("required");
            out.push_back({{"label", s("label")},
                           {"hint", s("hint")},
                           {"example", s("example")},
                           {"required", r && r->type == JsonValue::Type::Bool && r->b ? "Y" : "N"}});
        }
        return out;
    };
    ctx.steps = [&core] { return steps_of(core); };
    return guide::handle(ctx, cmd, req);
}

}  // namespace pna
