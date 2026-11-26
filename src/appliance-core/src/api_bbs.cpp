#include "api_bbs.h"

#include "bbs.h"
#include "core.h"
#include "json.h"

#include <cstdlib>
#include <set>

namespace pna {

namespace {

std::string err(const std::string& why) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", why).end_object();
    return w.str();
}

std::string ok() {
    JsonWriter w;
    w.begin_object().field("ok", true).end_object();
    return w.str();
}

bool truthy(const std::string& v) { return v == "true" || v == "1" || v == "yes" || v == "on" || v == "Y"; }

std::string settings_reply(Core& core, bbs::Engine& e) {
    bbs::Settings s = e.settings();
    std::string gate = e.gate(false);
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("machine_mode", s.machine_mode)
        .field("suffix", bbs::kSuffix)
        .field("machine_call", core.station() + bbs::kSuffix)
        .field("accept", s.accept)
        .field("ack", s.ack)
        .field("ack_delay_ms", s.ack_delay_ms)
        .field("ack_to", s.ack_to)
        .field("commands_ct_only", s.commands_ct_only)
        .field("max_errors", s.max_errors)
        .field("accept_malformed", s.accept_malformed)
        .field("zee_multi", s.zee_multi)
        .field("zee_max_bytes", s.zee_max_bytes)
        .field("data_rate_bps", s.data_rate_bps)
        .field("duty_s", s.duty_s)
        .field("zee_limit", bbs::zee_limit(s))
        .field("remote_sync", s.remote_sync)
        .field("duplicates",
               "an exact copy, or the same message id with the same sender and recipients (a re-file with a new "
               "FL2 is not a duplicate)")
        .field("purge_days", s.purge_days)
        .field("auto_mark_days", s.auto_mark_days)
        .field("auto_per_hour", s.auto_per_hour)
        .field("auto_sent_last_hour", e.auto_sent_last_hour())
        .field("routing", s.routing)
        .field("broadcast_ri", s.broadcast_ri)
        .field("version", bbs::kVersion)
        .field("auto_send", gate.empty())
        .field("auto_send_why_not", gate);
    w.end_object();
    return w.str();
}

void write_rule(JsonWriter& w, const bbs::Rule& r) {
    w.begin_object()
        .field("id", static_cast<long long>(r.id))
        .field("pos", r.pos)
        .field("match", r.match)
        .field("route", r.route)
        .begin_array("ale");
    for (auto& a : r.ale) w.field("", a);
    w.end_array()
        .field("key", r.key)
        .field("sched", r.sched)
        .field("compress", r.compress)
        .field("mark_processed", r.mark_processed)
        .field("world", r.world)
        .end_object();
}

void write_routed(JsonWriter& w, const bbs::Routed& r) {
    w.begin_object()
        .field("msg_id", r.msg_id)
        .field("rule", static_cast<long long>(r.rule))
        .field("state", r.state)
        .field("out_id", r.out_id)
        .field("due", static_cast<long long>(r.due))
        .field("ale_pos", r.ale_pos)
        .field("processed", r.processed)
        .field("why", r.why)
        .end_object();
}

bool parse_id(const std::string& s, int64_t& id) {
    if (s.empty() || s.size() > 12) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    id = std::stoll(s);
    return id > 0;
}

}  // namespace

std::optional<std::string> handle_bbs(Core& core, const std::string& cmd, std::map<std::string, std::string>& req) {
    static const std::set<std::string> kCmds = {"bbs_get",   "bbs_set",   "guard_list", "guard_set",  "alias_list",
                                                "alias_set", "alias_del", "route_list", "route_set",  "route_del",
                                                "route_move", "route_rerun", "bbs_qsl"};
    if (!kCmds.count(cmd)) return std::nullopt;
    bbs::Engine& e = bbs::engine(core);
    std::string why;

    if (cmd == "bbs_get") return settings_reply(core, e);
    if (cmd == "bbs_set") {
        std::map<std::string, std::string> kv = req;
        kv.erase("cmd");
        if (!e.set(kv, &why)) return err(why);
        return settings_reply(core, e);
    }
    if (cmd == "guard_list") {
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("items");
        for (auto& g : e.guard()) w.field("", g);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "guard_set") {
        if (!req.count("items")) return err("give items, a comma list (an empty string clears the list)");
        if (!e.set_guard(req["items"], &why)) return err(why);
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("items");
        for (auto& g : e.guard()) w.field("", g);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "alias_list") {
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("aliases");
        for (auto& a : e.aliases()) {
            w.begin_object().field("ri", a.ri).begin_array("members");
            for (auto& m : a.members) w.field("", m);
            w.end_array().end_object();
        }
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "alias_set") {
        if (!e.set_alias(req["ri"], req["members"], &why)) return err(why);
        return ok();
    }
    if (cmd == "alias_del") {
        if (!e.del_alias(req["ri"], &why)) return err(why);
        return ok();
    }
    if (cmd == "route_list") {
        bbs::Settings s = e.settings();
        std::string gate = e.gate(true);
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("routing", s.routing)
            .field("broadcast_ri", s.broadcast_ri)
            .field("auto_send", gate.empty())
            .field("auto_send_why_not", gate)
            .begin_array("rules");
        for (auto& r : e.rules()) write_rule(w, r);
        w.end_array().begin_array("routed");
        for (auto& r : e.routed(20)) write_routed(w, r);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "route_set") {
        bbs::Rule r;
        if (req.count("id") && !req["id"].empty() && !parse_id(req["id"], r.id)) return err("id must be a rule number");
        if (r.id) {
            // A change keeps what it doesn't name.
            bool found = false;
            for (auto& x : e.rules())
                if (x.id == r.id) {
                    r = x;
                    found = true;
                }
            if (!found) return err("no rule " + req["id"]);
        }
        if (req.count("match")) r.match = req["match"];
        if (req.count("route")) r.route = req["route"];
        if (req.count("ale")) {
            r.ale.clear();
            std::string cur;
            for (char c : req["ale"] + ",") {
                if (c == ',') {
                    r.ale.push_back(cur);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
        }
        if (req.count("key")) r.key = req["key"];
        if (req.count("sched")) r.sched = req["sched"];
        if (req.count("compress")) r.compress = truthy(req["compress"]);
        if (req.count("mark_processed")) r.mark_processed = truthy(req["mark_processed"]);
        if (req.count("world")) r.world = req["world"];
        int64_t id = 0;
        if (!e.set_rule(r, &id, &why)) return err(why);
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", static_cast<long long>(id)).end_object();
        return w.str();
    }
    if (cmd == "route_del" || cmd == "route_move") {
        int64_t id = 0;
        if (!parse_id(req["id"], id)) return err("give id, the rule's number");
        bool done = cmd == "route_del" ? e.del_rule(id, &why)
                                       : e.move_rule(id, req["dir"], req.count("to") ? std::atoi(req["to"].c_str()) : 0, &why);
        if (!done) return err(why);
        return ok();
    }
    if (cmd == "route_rerun") {
        // A dry run unless dry_run is false, said outright.
        const bool dry = !(req.count("dry_run") && (req["dry_run"] == "false" || req["dry_run"] == "0"));
        auto rs = e.rerun(dry, &why);
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("dry_run", dry)
            .field("count", static_cast<int>(rs.size()))
            .field("auto_send_why_not", e.gate(true))
            .begin_array("items");
        for (auto& r : rs) write_routed(w, r);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "bbs_qsl") {
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", req["id"]).begin_array("by");
        for (auto& b : e.qsl_of(req["id"])) w.field("", b);
        w.end_array().end_object();
        return w.str();
    }
    return std::nullopt;
}

}  // namespace pna
