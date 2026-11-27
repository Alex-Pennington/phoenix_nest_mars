#include "api_ops.h"

#include "acp126.h"
#include "core.h"
#include "frame.h"
#include "json.h"
#include "ops.h"
#include "channels.h"
#include "compose.h"
#include "mars_import.h"
#include "table.h"

#include <algorithm>
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
    w.field("note", e.note).field("source", e.source).field("world", e.world).end_object();
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
        .field("checked_in", static_cast<int>(n.checkins.size()))
        .field("channel", n.channel)
        .field("sitting_end", static_cast<long long>(n.sitting_end));
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

std::string hhmm(int m) {
    char b[24];
    std::snprintf(b, sizeof b, "%02d%02d", m / 60, m % 60);
    return b;
}

void write_sched(JsonWriter& w, const NetSchedule& s, const std::optional<NetSitting>& st) {
    w.begin_object()
        .field("name", s.name)
        .field("days", s.days)
        .field("time", hhmm(s.start))
        .field("minutes", s.minutes);
    strings(w, "roster", s.roster);
    w.field("exercise", s.exercise).field("ncs", s.ncs).field("note", s.note).field("auto_open", s.auto_open);
    w.field("channel", s.channel).field("plan", s.plan);
    if (s.rx_index >= 0) w.field("index", s.rx_index);
    else w.field_null("index");
    w.field("mode", s.rx_mode);
    if (st)
        w.field("state", st->state)
            .field("start", static_cast<long long>(st->start))
            .field("end", static_cast<long long>(st->end))
            .field("net", st->net);
    else
        w.field("state", "none");
    w.end_object();
}

// The call-up text: our own plain wording, in capitals, a line each.
std::string callup_text(const Net& n, const std::vector<std::string>& called, bool listed_missing,
                        const std::string& extra) {
    std::vector<std::string> L;
    if (n.exercise) L.push_back("EXERCISE EXERCISE EXERCISE");
    L.push_back("NET " + n.name + " IS OPEN. NET CONTROL IS " + n.ncs + ".");
    if (listed_missing && !called.empty()) {
        std::string line = "NOT YET CHECKED IN:";
        for (auto& c : called) line += " " + c;
        L.push_back(line);
    }
    L.push_back("CHECK IN WITH NET CONTROL.");
    if (!book_key(extra).empty()) L.push_back(book_key(extra));
    if (n.exercise) L.push_back("EXERCISE EXERCISE EXERCISE");
    std::string out;
    for (size_t i = 0; i < L.size(); ++i) out += (i ? "\n" : "") + L[i];
    return out;
}

}  // namespace

std::optional<std::string> handle_ops(Core& core, const std::string& cmd, std::map<std::string, std::string>& req) {
    static const char* kCmds[] = {"book_put", "book_remove", "book_get", "book_list", "book_import", "book_export",
                                  "book_resolve", "net_open", "net_checkin", "net_checkout", "net_close", "net_status",
                                  "net_list", "duty_on", "duty_off", "duty_status", "log_add", "station_log",
                                  "report_monthly", "sched_put", "sched_remove", "sched_list", "sched_next",
                                  "sched_open", "net_callup", "mars_import", "channels_list", "channel_remove",
                                  "channel_put", "channels_get_full"};
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
        // Members split on ; or , (STA-B;STA-C or STA-B, STA-C).
        std::string members = req["members"];
        std::replace(members.begin(), members.end(), ',', ';');
        e.members = list_of(members, ';');
        e.note = req["note"];
        e.world = req["world"];
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
    if (cmd == "mars_import") {
        // A roster, address book, frequency card or net schedule: data is the
        // file in base64 (a spreadsheet or text), or text is the text itself.
        std::string bytes = req["text"];
        if (!req["data"].empty() && !base64_decode(req["data"], bytes)) return err_reply("data isn't base64");
        if (bytes.empty()) return err_reply("give the file as data (base64) or text");
        if (bytes.size() > (16u << 20)) return err_reply("the file is over 16 MB");
        auto res = marsio::import_file(*ops, bytes, req["kind"], req["source"], &err);
        if (res.empty()) return err_reply(err.empty() ? "nothing in the file to read" : err);
        int total = 0;
        JsonWriter w;
        w.begin_object().begin_array("sheets");
        for (auto& r : res) {
            total += r.imported;
            w.begin_object()
                .field("sheet", r.sheet)
                .field("kind", r.kind)
                .field("header_row", r.header_row)
                .field("rows", r.rows)
                .field("imported", r.imported);
            strings(w, "names", r.names);
            strings(w, "left_out", r.left_out);
            strings(w, "problems", r.problems);
            if (r.frequencies_withheld)
                w.field("frequencies_withheld", r.frequencies_withheld)
                    .field("note", "some rows gave a frequency the channel plan doesn't hold; load the frequency "
                                   "card first, or give the channel's designator");
            w.end_object();
        }
        w.end_array().field("ok", total > 0).field("imported", total);
        if (total == 0) w.field("error", "nothing was imported");
        w.end_object();
        core.note("import" + (req["source"].empty() ? std::string() : " from " + req["source"]) + ": " +
                  std::to_string(total) + " entries");
        return w.str();
    }
    if (cmd == "channels_list" || cmd == "channels_get_full") {
        // channels_list: designators only; frequencies stay in the plan's
        // file. channels_get_full is the one reply that carries a frequency
        // (Alex's ruling 4 of 2026-11-26, asks 27 and 29): it is for the
        // CHANNELS screen only, and every read of it is logged, never with a
        // frequency.
        const bool full = cmd == "channels_get_full";
        auto list = ops->channels().list();
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("channels");
        for (auto& c : list) {
            w.begin_object()
                .field("name", c.name)
                .field("world", c.world == "amateur" ? "amateur" : "mars")
                .field("label", c.label);
            if (full) w.field("frequency_hz", static_cast<long long>(c.hz));
            w.field("mode", c.mode)
                .field("use", c.use)
                .field("source", c.source)
                .field("ant_rx", c.ant_rx)
                .field("ant_tx", c.ant_tx)
                .end_object();
        }
        w.end_array().end_object();
        if (full) core.note("channels_get_full: the channel plan read with its frequencies (" +
                            std::to_string(list.size()) + " channels)");
        return w.str();
    }
    if (cmd == "channel_put") {
        // One channel entered by hand: a region with no documents makes its
        // first frequency card this way. The frequency is kept in the plan
        // like an imported one and never comes back in a reply or the log.
        const bool has_use = req.count("use") > 0;
        Channel c;
        c.name = req["name"];
        for (auto& ch : c.name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        std::string unit = req["unit"];
        for (auto& ch : unit) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (!unit.empty() && unit != "khz" && unit != "mhz" && unit != "hz")
            return err_reply("unit: khz, mhz or hz (or leave it out: 7345.5 reads as kHz)");
        auto old = ops->channels().get(c.name);
        // Left out, a channel keeps its frequency, so a change to its port,
        // mode, world or label needs no retyping. Sent empty it is refused.
        if (!req.count("frequency") && old) {
            c.hz = old->hz;
        } else {
            if (req["frequency"].empty()) return err_reply("give the channel's frequency (frequency=7345.5, in kHz)");
            c.hz = parse_frequency(req["frequency"], unit);
            if (c.hz <= 0) return err_reply("the frequency can't be read: give it in kHz (7345.5), MHz or Hz");
        }
        // Station design v1.9: the channel's world and HFLink label. Left
        // out, each keeps what the channel had (mars and none for a new one);
        // an empty label clears it.
        if (req.count("world")) {
            c.world = req["world"];
            for (auto& ch : c.world) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (c.world != "mars" && c.world != "amateur") return err_reply("world: mars or amateur");
        }
        if (req.count("label")) {
            c.label = req["label"];
            for (auto& ch : c.label) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (!c.label.empty() && !valid_channel_label(c.label))
                return err_reply("label: the HFLink label, 1 to 16 letters, digits or - / . _, never a frequency");
            c.label_given = true;
        }
        c.mode = req["mode"];
        for (auto& ch : c.mode) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        c.use = req["use"];
        for (auto& ch : c.use) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (compose::looks_like_frequency(c.use)) c.use.clear();
        c.source = "entered by hand";
        // The tuner's antenna ports (docs/forms.md, CHANNEL): 1 or 2. Left
        // out, the channel keeps what it had (1 for a new one); blank is 1.
        for (auto [key, port] : {std::pair<const char*, int*>{"ant_rx", &c.ant_rx}, {"ant_tx", &c.ant_tx}}) {
            if (!req.count(key)) continue;
            const std::string& v = req[key];
            if (v.empty() || v == "1") *port = 1;
            else if (v == "2") *port = 2;
            else return err_reply(std::string(key) + ": the tuner port, 1 or 2");
        }
        if (old) {
            if (c.mode.empty()) c.mode = old->mode;
            if (!has_use) c.use = old->use;
        }
        if (c.mode.empty()) c.mode = "USB";  // a new channel with none given
        if (c.ant_rx == 0) c.ant_rx = old ? old->ant_rx : 1;
        if (c.ant_tx == 0) c.ant_tx = old ? old->ant_tx : 1;
        if (c.world.empty()) c.world = old ? old->world : "mars";
        if (!c.label_given) c.label = old ? old->label : "";
        c.label_given = true;
        bool same = old && old->hz == c.hz && old->mode == c.mode && old->use == c.use && old->ant_rx == c.ant_rx &&
                    old->ant_tx == c.ant_tx && old->world == c.world && old->label == c.label;
        if (!same && !ops->channels().put(c, &err)) {
            c.hz = 0;
            return err_reply(err);
        }
        auto now = ops->channels().get(c.name);
        c.hz = 0;
        std::string result = same ? "same" : old ? "replaced" : "added";
        if (!same) core.note("channel " + c.name + " " + result + " by hand");
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("name", now ? now->name : c.name)
            .field("result", result)
            .field("frequency_changed", old && !same && old->hz != (now ? now->hz : 0))
            .field("mode", now ? now->mode : c.mode)
            .field("use", now ? now->use : c.use)
            .field("ant_rx", now ? now->ant_rx : c.ant_rx)
            .field("ant_tx", now ? now->ant_tx : c.ant_tx)
            .field("world", now ? now->world : c.world)
            .field("label", now ? now->label : c.label)
            .end_object();
        return w.str();
    }
    if (cmd == "channel_remove")
        return ops->channels().remove(req["name"]) ? ok_reply() : err_reply("no such channel in the plan");
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
        const std::string ch = book_key(req["channel"]);
        auto plan_ch = ch.empty() ? std::nullopt : ops->channels().get(ch);
        if (!ch.empty() && !plan_ch) return err_reply("no channel " + ch + " in the channel plan");
        if (plan_ch && plan_ch->world == "amateur")
            return err_reply("channel: " + ch + " is an amateur channel; a MARS net meets on a MARS channel (ask 40)");
        std::string id = ops->net_open(req["name"], list_of(req["roster"], ','), truthy(req["exercise"]), req["ncs"],
                                       req["note"], &err, ch);
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

    if (cmd == "sched_put") {
        NetSchedule sc;
        sc.name = req["name"];
        sc.days = req["days"];
        sc.channel = req["channel"];
        if (!parse_hhmm(req["time"], &sc.start)) return err_reply("time: write the start as HHMM in UTC, like 1900");
        long long minutes = 60;
        if (!req["minutes"].empty() && !to_int(req["minutes"], &minutes)) return err_reply("minutes must be a number");
        sc.minutes = static_cast<int>(minutes);
        sc.roster = list_of(req["roster"], ',');
        sc.exercise = truthy(req["exercise"]);
        sc.ncs = req["ncs"];
        sc.note = req["note"];
        sc.auto_open = truthy(req["auto_open"]);
        // gale-rx's fields (ask 28). Each left out keeps what the schedule
        // had; sent empty, it is cleared.
        auto had = ops->sched_get(sc.name);
        sc.plan = req.count("plan") ? req["plan"] : had ? had->plan : "";
        sc.rx_mode = req.count("mode") ? req["mode"] : had ? had->rx_mode : "";
        sc.rx_index = had ? had->rx_index : -1;
        if (req.count("index")) {
            long long ix = -1;
            if (!req["index"].empty() && !to_int(req["index"], &ix)) return err_reply("index: a number, the plan's channel from 0");
            sc.rx_index = static_cast<int>(ix < -1 || ix > 999 ? -2 : ix);
        }
        if (!ops->sched_put(sc, &err)) return err_reply(err);
        auto saved = ops->sched_get(sc.name);
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("schedules");
        if (saved) write_sched(w, *saved, ops->sched_sitting(saved->name));
        w.end_array().field("rx_file", ops->write_rx_schedule(true)).end_object();
        return w.str();
    }
    if (cmd == "sched_remove") {
        if (!ops->sched_remove(req["name"])) return err_reply("no schedule by that name");
        JsonWriter w;
        w.begin_object().field("ok", true).field("rx_file", ops->write_rx_schedule(true)).end_object();
        return w.str();
    }
    if (cmd == "sched_list") {
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("schedules");
        for (auto& sc : ops->sched_list()) write_sched(w, sc, ops->sched_sitting(sc.name));
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "sched_next") {
        long long limit = 5;
        if (!req["limit"].empty() && !to_int(req["limit"], &limit)) return err_reply("limit must be a number");
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("sittings");
        for (auto& st : ops->sched_upcoming(static_cast<int>(limit < 1 || limit > 100 ? 5 : limit)))
            write_sched(w, st.sched, st);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "sched_open") {
        std::string id = ops->sched_open(req["name"], &err);
        if (id.empty()) return err_reply(err);
        auto n = ops->net_get(id);
        JsonWriter out;
        out.begin_object().field("ok", true).field("id", id).begin_array("net");
        if (n) write_net(out, *n, false);
        out.end_array().end_object();
        return out.str();
    }

    // net_callup: one ACP 126 SIMPLE call-up from this station to every
    // station listening (the broadcast address ALL; nobody acks it). It calls
    // the roster stations not yet checked in ("who":"missing", the default),
    // or the whole roster ("who":"all").
    if (cmd == "net_callup") {
        std::string id = req["net"].empty() ? ops->current_net() : book_key(req["net"]);
        auto n = id.empty() ? std::nullopt : ops->net_get(id);
        if (!n) return err_reply(req["net"].empty() ? "no net is open, or more than one is: say which" : "no such net");
        if (n->closed_at) return err_reply(n->id + " is closed");
        std::string who = req["who"].empty() ? "missing" : req["who"];
        if (who != "missing" && who != "all") return err_reply("who must be missing or all");
        std::vector<std::string> called = who == "all" ? n->roster : n->missing();
        if (who == "missing" && called.empty())
            return err_reply(n->roster.empty() ? n->id + " has no roster: send who all to call the whole net"
                                               : "every station on the roster has checked in");
        std::string prec = req["prec"].empty() ? "R" : req["prec"];
        if (prec.size() != 1 || !valid_prec(prec[0])) return err_reply("precedence must be Z, O, P or R");

        acp126::Message m;
        m.station = core.station();
        m.prec = prec[0];
        m.channel = book_key(req["channel"]);
        if (auto pc = m.channel.empty() ? std::nullopt : ops->channels().get(m.channel); pc && pc->world == "amateur")
            return err_reply("channel: " + m.channel + " is an amateur channel; a MARS call-up goes on a MARS channel (ask 40)");
        m.unclas_line = truthy(req["unclas"]);
        // Call the stations by name when they fit on the call line, else the NET.
        std::string line;
        for (auto& c : called) line += (line.empty() ? "" : " ") + c;
        if (!called.empty() && line.size() + 4 + m.station.size() + 7 <= 69) m.calls = called;
        else m.calls = {"NET"};
        m.text = callup_text(*n, called, who == "missing", req["text"]);
        if (acp126::build(m, &err).empty()) return err_reply(err);
        if (!m.channel.empty())
            m.channel_serial = static_cast<int>(core.next_counter("acp127.channel." + m.channel, 999));
        m.serial = static_cast<int>(core.next_counter("acp126.nr." + m.station, 999));
        std::string text = acp126::build(m, &err);
        if (text.empty()) return err_reply(err);
        // A broadcast needs its service named: the request's, else its channel's.
        std::string svc = req["service"];
        if (svc.empty() && !m.channel.empty())
            if (auto pc = ops->channels().get(m.channel)) svc = pc->world;
        const std::string format =
            m.procedure == acp126::Message::Procedure::Plaindress ? "ACP126_PLAINDRESS" : "ACP126_SIMPLE";
        std::string msg = core.submit(kBroadcast, m.prec, text, &err, SendOpts::in(svc, "", format));
        if (msg.empty()) return err_reply(err);
        ops->log_add("net",
                     "call-up sent on " + n->id + " to " +
                         (who == "all" ? "the whole roster" : std::to_string(called.size()) + " not checked in"),
                     n->id, n->exercise);
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", msg).field("net", n->id).field("to_station", kBroadcast);
        strings(w, "called", called);
        w.field("message", text).end_object();
        return w.str();
    }
    return err_reply("unknown command");
}

}  // namespace pna
