#include "api_key.h"

#include "core.h"
#include "gale_key.h"
#include "json.h"

#include <filesystem>

namespace pna {

namespace {

std::string err(const std::string& why) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", no_frequency(why)).end_object();
    return w.str();
}

// gale-key's answer, as the kiosk may see it: never a token, never a frequency.
void write_reply(JsonWriter& w, const GaleKeyReply& r) {
    w.begin_object("gale_key")
        .field("reached", r.reached)
        .field("answered", r.answered)
        .field("ok", r.ok)
        .field("exit", r.exit)
        .field("reason", no_frequency(r.reason))
        .end_object();
}

bool is_y(const std::string& v) { return v == "Y" || v == "y"; }

}  // namespace

void write_keying(JsonWriter& w, Core& core) {
    GaleKey* gk = core.gale_key();
    w.begin_object("keying");
    if (!gk) {
        w.field("gale_key", "none").field("why", "no gale-key is set up for this station: nothing is keyed");
        w.begin_array("standing").end_array().field("held", 0).end_object();
        return;
    }
    std::string why;
    std::string state = gk->last_state(&why);
    std::error_code ec;
    const std::string where = gk->where();
    if (!where.empty() && !std::filesystem::exists(where, ec)) {
        state = "not answering";
        why = "gale-key isn't running (no socket at " + where + "): nothing is keyed, bursts stay written";
    } else if (state.empty()) {
        state = "not asked yet";
    }
    w.field("gale_key", state).field("socket", where).field("why", no_frequency(why));
    w.begin_array("standing");
    for (auto& m : gk->standing()) w.field("", m);
    w.end_array().field("held", static_cast<long long>(gk->held())).end_object();
}

std::optional<std::string> handle_key(Core& core, const std::string& cmd, std::map<std::string, std::string>& req) {
    if (cmd != "key_prepare" && cmd != "standing_prepare" && cmd != "key_confirm" && cmd != "key_cancel" &&
        cmd != "standing_off" && cmd != "key_status")
        return std::nullopt;
    GaleKey* gk = core.gale_key();
    if (!gk) return err("no gale-key is set up for this station (gale-key-socket): nothing is keyed");

    if (cmd == "key_prepare") {
        KeyItem it;
        std::string why;
        if (!core.key_item(req["msg_id"], req["burst"], it, &why)) return err(why);
        GaleKey::Prepared p = gk->prepare(it);
        if (!p.ok) return err(p.error);
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("prepare_id", p.prepare_id)
            .field("expires_s", p.expires_s)
            .raw("confirm", p.confirm)
            .end_object();
        return w.str();
    }
    if (cmd == "standing_prepare") {
        const std::string mode = req["mode"];
        if (core.emcon()) return err("EMCON is on: no standing mode is turned on until it is off");
        GaleKey::Prepared p = gk->prepare_standing(mode);
        if (!p.ok) return err(p.error);
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("prepare_id", p.prepare_id)
            .field("expires_s", p.expires_s)
            .raw("confirm", p.confirm)
            .end_object();
        return w.str();
    }
    if (cmd == "key_cancel") {
        const std::string id = req["prepare_id"];
        if (id.empty()) return err("give prepare_id");
        JsonWriter w;
        w.begin_object().field("ok", true).field("prepare_id", id).field("dropped", gk->cancel(id)).end_object();
        return w.str();
    }
    if (cmd == "key_confirm") {
        const std::string id = req["prepare_id"];
        if (id.empty()) return err("give prepare_id");
        // The core never makes up a Y: confirm goes as the operator gave it.
        if (!is_y(req["confirm"])) return err("confirm=Y is the operator's y; nothing sent, the dialog is still held");
        GaleKey::Held h;
        std::string why;
        if (!gk->take(id, h, &why)) return err(why);
        JsonWriter w;
        if (h.kind == "standing") {
            if (core.emcon()) return err("EMCON is on: standing " + h.mode + " not granted");
            GaleKeyReply r = gk->grant(h);
            if (!(r.answered && r.ok))
                return err("standing " + h.mode + " not granted: " + (r.reason.empty() ? "no reason given" : r.reason));
            w.begin_object().field("ok", true).field("kind", "standing").field("mode", h.mode).field("on", true);
            write_reply(w, r);
            w.end_object();
            return w.str();
        }
        if (!core.claim_key(h.item, &why)) return err(why);
        GaleKeyReply r = gk->key(h);
        if (!r.reached) {
            core.unclaim_key(h.item);
            return err("not keyed: " + r.reason + "; " + h.item.burst + " stays written");
        }
        w.begin_object()
            .field("ok", true)
            .field("kind", "air")
            .field("msg_id", h.item.msg_id)
            .field("burst", h.item.burst)
            .field("asked", true);
        write_reply(w, r);
        w.field("note", "the message stays written until gale-key's BURST.key.json says keyed (sent) or refused (failed)")
            .end_object();
        return w.str();
    }
    if (cmd == "standing_off") {
        const std::string mode = req["mode"];
        if (mode != "all" && !core_standing_mode(mode)) return err("mode is machine, routing, auto_qsl or all");
        GaleKeyReply r = gk->revoke(mode, "turned off by the operator");
        JsonWriter w;
        w.begin_object().field("ok", true).field("mode", mode).field("on", false);
        write_reply(w, r);
        w.end_object();
        return w.str();
    }
    // key_status: gale-key's own status, the parts the kiosk shows.
    GaleKeyReply r = gk->status();
    JsonWriter w;
    w.begin_object().field("ok", true);
    write_keying(w, core);
    write_reply(w, r);
    if (r.answered) {
        w.begin_object("station");
        if (const JsonValue* x = r.body.get("ready"); x && x->type == JsonValue::Type::Bool) w.field("ready", x->b);
        if (const JsonValue* x = r.body.get("keyed"); x && x->type == JsonValue::Type::Bool) w.field("keyed", x->b);
        if (const JsonValue* x = r.body.get("ptt"); x && x->type == JsonValue::Type::String &&
                                                    (x->s == "rts" || x->s == "dtr" || x->s == "cat"))
            w.field("ptt", x->s);
        if (const JsonValue* x = r.body.get("transmit_world");
            x && x->type == JsonValue::Type::String && (x->s == "mars" || x->s == "amateur"))
            w.field("transmit_world", x->s);
        w.begin_array("why_not");
        if (const JsonValue* x = r.body.get("why_not"); x && x->is_array())
            for (auto& e : x->arr)
                if (e.type == JsonValue::Type::String) w.field("", no_frequency(e.s));
        w.end_array();
        w.begin_array("grants");
        if (const JsonValue* x = r.body.get("grants"); x && x->is_object())
            for (auto& [m, v] : x->obj)
                if (m == "machine" || m == "routing" || m == "auto_qsl" || m == "amateur_auto") w.field("", m);
        w.end_array().end_object();
    }
    w.end_object();
    return w.str();
}

}  // namespace pna
