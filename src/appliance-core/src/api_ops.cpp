#include "api_ops.h"

#include "core.h"
#include "json.h"
#include "ops.h"

#include <cstdio>
#include <ctime>

namespace pna {

namespace {

std::string err_reply(const std::string& why) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", why).end_object();
    return w.str();
}

std::string ok_reply() {
    JsonWriter w;
    w.begin_object().field("ok", true).end_object();
    return w.str();
}

bool truthy(const std::string& s) { return s == "true" || s == "1" || s == "Y" || s == "y" || s == "yes"; }

std::vector<std::string> list_of(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s + sep) {
        if (c == sep) {
            if (!book_key(cur).empty()) out.push_back(book_key(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
}

void strings(JsonWriter& w, const std::string& key, const std::vector<std::string>& v) {
    w.begin_array(key);
    for (auto& s : v) w.field("", s);
    w.end_array();
}

void write_entry(JsonWriter& w, const BookEntry& e) {
    w.begin_object().field("name", e.name).field("kind", e.kind).field("call", e.call).field("ri", e.ri)
        .field("ale", e.ale);
    strings(w, "members", e.members);
    w.field("note", e.note).field("source", e.source).end_object();
}

void write_net(JsonWriter& w, const Net& n, bool with_checkins) {
    w.begin_object()
        .field("id", n.id)
        .field("name", n.name)
        .field("ncs", n.ncs)
        .field("exercise", n.exercise)
        .field("open", n.closed_at == 0)
        .field("opened_at", static_cast<long long>(n.opened_at))
        .field("closed_at", static_cast<long long>(n.closed_at))
        .field("checked_in", static_cast<int>(n.checkins.size()));
    strings(w, "roster", n.roster);
    strings(w, "missing", n.missing());
    if (with_checkins) {
        w.field("note", n.note).begin_array("checkins");
        for (auto& c : n.checkins)
            w.begin_object()
                .field("station", c.station)
                .field("at", static_cast<long long>(c.at))
                .field("out_at", static_cast<long long>(c.out_at))
                .field("via", c.via)
                .field("traffic", c.traffic)
                .field("remark", c.remark)
                .field("in_roster", c.in_roster)
                .end_object();
        w.end_array();
    }
    w.end_object();
}

bool to_int(const std::string& s, long long* v) {
    try {
        size_t n = 0;
        *v = std::stoll(s, &n);
        return n == s.size();
    } catch (...) {
        return false;
    }
}

}  // namespace

std::optional<std::string> handle_ops(Core& core, const std::string& cmd, std::map<std::string, std::string>& req) {
    static const char* kCmds[] = {"book_put", "book_remove", "book_get", "book_list", "book_import", "book_export",
                                  "book_resolve", "net_open", "net_checkin", "net_checkout", "net_close", "net_status",
                                  "net_list", "duty_on", "duty_off", "duty_status", "log_add", "station_log",
                                  "report_monthly"};
    bool ours = false;
    for (auto c : kCmds)
        if (cmd == c) ours = true;
    if (!ours) return std::nullopt;
    Ops* ops = core.ops();
    if (!ops) return err_reply("this station has no book");
    std::string err;

    if (cmd == "book_put") {
        BookEntry e;
        e.name = req["name"];
        e.kind = req["kind"];
        e.call = req["call"];
        e.ri = req["ri"];
        e.ale = req["ale"];
        e.members = list_of(req["members"], ';');
        if (e.members.empty()) e.members = list_of(req["members"], ',');
        e.note = req["note"];
        if (!ops->book_put(e, &err)) return err_reply(err);
        return ok_reply();
    }
    if (cmd == "book_remove") return ops->book_remove(req["name"]) ? ok_reply() : err_reply("no such name in the book");
    if (cmd == "book_get") {
        auto e = ops->book_find(req["name"]);
        if (!e) return err_reply("not in the book");
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("entries");
        write_entry(w, *e);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "book_list") {
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("entries");
        for (auto& e : ops->book_list()) write_entry(w, e);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "book_import") {
        std::vector<std::string> dropped, errors;
        int n = ops->book_import(req["csv"], req["source"], &dropped, &errors);
        JsonWriter w;
        w.begin_object().field("ok", errors.empty() || n > 0).field("imported", n);
        strings(w, "dropped_columns", dropped);
        strings(w, "errors", errors);
        if (!dropped.empty())
            w.field("note", "the book keeps no personal details; those columns were not imported");
        w.end_object();
        return w.str();
    }
    if (cmd == "book_export") {
        JsonWriter w;
        w.begin_object().field("ok", true).field("csv", ops->book_export()).end_object();
        return w.str();
    }
    if (cmd == "book_resolve") {
        Resolved r = ops->resolve(req["to"]);
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("stations");
        for (auto& e : r.stations) write_entry(w, e);
        w.end_array();
        strings(w, "unknown", r.unknown);
        strings(w, "loops", r.loops);
        w.end_object();
        return w.str();
    }

    if (cmd == "net_open") {
        std::string id = ops->net_open(req["name"], list_of(req["roster"], ','), truthy(req["exercise"]), req["ncs"],
                                       req["note"], &err);
        if (id.empty()) return err_reply(err);
        auto n = ops->net_get(id);
        JsonWriter out;
        out.begin_object().field("ok", true).field("id", id).begin_array("net");
        if (n) write_net(out, *n, false);
        out.end_array().end_object();
        return out.str();
    }
    if (cmd == "net_checkin") {
        long long traffic = 0;
        if (!req["traffic"].empty() && !to_int(req["traffic"], &traffic)) return err_reply("traffic must be a number");
        if (!ops->net_checkin(req["net"], req["station"], "manual", static_cast<int>(traffic), req["remark"], &err))
            return err_reply(err);
        return ok_reply();
    }
    if (cmd == "net_checkout")
        return ops->net_checkout(req["net"], req["station"], &err) ? ok_reply() : err_reply(err);
    if (cmd == "net_close") {
        std::string id = req["net"].empty() ? ops->current_net() : req["net"];
        if (!ops->net_close(id, &err)) return err_reply(err);
        auto n = ops->net_get(id);
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("net");
        if (n) write_net(w, *n, true);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "net_status") {
        auto n = ops->net_get(req["net"]);
        if (!n) return err_reply(req["net"].empty() ? "no net is open, or more than one is" : "no such net");
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("net");
        write_net(w, *n, true);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "net_list") {
        long long limit = 20;
        if (!req["limit"].empty() && !to_int(req["limit"], &limit)) return err_reply("limit must be a number");
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("nets");
        for (auto& n : ops->net_list(static_cast<int>(limit < 1 || limit > 500 ? 20 : limit))) write_net(w, n, false);
        w.end_array().end_object();
        return w.str();
    }

    if (cmd == "duty_on") return ops->duty_on(req["operator"], req["note"], &err) ? ok_reply() : err_reply(err);
    if (cmd == "duty_off") return ops->duty_off(req["turnover"], &err) ? ok_reply() : err_reply(err);
    if (cmd == "duty_status") {
        auto d = ops->duty_current();
        JsonWriter w;
        w.begin_object().field("ok", true).field("on_duty", d.has_value()).field("operator", d ? d->op : "")
            .field("since", static_cast<long long>(d ? d->on_at : 0));
        // The last turnover note, for the operator coming on.
        auto past = ops->duty_list(0, ops->now() + 1);
        std::string turnover;
        for (auto it = past.rbegin(); it != past.rend(); ++it)
            if (it->off_at && !it->turnover.empty()) {
                turnover = it->turnover;
                break;
            }
        w.field("last_turnover", turnover).end_object();
        return w.str();
    }

    if (cmd == "log_add") {
        std::string kind = req["kind"].empty() ? "note" : req["kind"];
        if (kind != "note" && kind != "comspot" && kind != "beadwindow")
            return err_reply("kind must be note, comspot or beadwindow");
        if (book_key(req["text"]).empty()) return err_reply("text: say what to log");
        std::string net = ops->current_net();
        auto n = net.empty() ? std::nullopt : ops->net_get(net);
        long long id = ops->log_add(kind, req["text"], net, n && n->exercise);
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", id).end_object();
        return w.str();
    }
    if (cmd == "station_log") {
        long long from = 0, to = 0, limit = 200;
        if (!req["from"].empty() && !to_int(req["from"], &from)) return err_reply("from must be unix seconds");
        if (!req["to"].empty() && !to_int(req["to"], &to)) return err_reply("to must be unix seconds");
        if (!req["limit"].empty() && !to_int(req["limit"], &limit)) return err_reply("limit must be a number");
        if (!to) to = ops->now() + 1;
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("entries");
        for (auto& e : ops->log_list(from, to, static_cast<int>(limit < 1 || limit > 5000 ? 200 : limit)))
            w.begin_object()
                .field("id", static_cast<long long>(e.id))
                .field("at", static_cast<long long>(e.at))
                .field("kind", e.kind)
                .field("text", e.text)
                .field("net", e.net)
                .field("exercise", e.exercise)
                .end_object();
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "report_monthly") {
        std::string month = req["month"];
        if (month.empty()) {
            std::time_t t = static_cast<std::time_t>(ops->now());
            std::tm u{};
            gmtime_r(&t, &u);
            char b[32];
            std::snprintf(b, sizeof b, "%04d-%02d", u.tm_year + 1900, u.tm_mon + 1);
            month = b;
        }
        MonthlyReport r;
        if (!ops->monthly(month, &r, &err)) return err_reply(err);
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("month", r.month)
            .field("duty_tenths", static_cast<long long>(r.duty_hours * 10 + 0.5))  // hours x 10
            .field("nets", r.nets)
            .field("nets_exercise", r.nets_exercise)
            .field("nets_as_ncs", r.nets_as_ncs)
            .field("checkins", r.checkins)
            .field("sent", r.sent)
            .field("sent_acked", r.sent_acked)
            .field("sent_failed", r.sent_failed)
            .field("received", r.received)
            .field("exercise_messages", r.exercise_msgs)
            .field("comspots", r.comspots)
            .field("beadwindows", r.beadwindows)
            .field("text", r.text)
            .end_object();
        return w.str();
    }
    return err_reply("unknown command");
}

}  // namespace pna
