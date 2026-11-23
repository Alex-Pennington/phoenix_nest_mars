#include "api_server.h"

#include "acp126.h"
#include "acp127.h"
#include "acp201.h"
#include "compose.h"
#include "received.h"
#include "json.h"
#include "net.h"

#include <sys/socket.h>

#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <sstream>

namespace pna {

namespace {

std::string error_reply(const std::string& why) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", why).end_object();
    return w.str();
}

void write_link(JsonWriter& w, const CoreStatus& s) {
    const LinkState& l = s.link;
    w.begin_object("link")
        .field("gate", s.link_gate)
        .field("held", s.held_for_link)
        .field("source", l.socket.empty() ? std::string() : std::string("ale-scan"))
        .field("socket", l.socket)
        .field("reachable", l.reachable)
        .field("error", l.reachable ? std::string() : l.error)
        .field("polled_at", l.polled_at)
        .field("scanner", l.scanner)
        .field("plan", l.plan)
        .field("engine", l.engine)
        .field("clock", l.clock)
        .field("last_wwv_at", l.last_wwv_at)
        .field("clock_note", clock_note(l, static_cast<long long>(std::time(nullptr))))
        .field("hold_window", l.hold_window)
        .field("links_reported", l.links_reported)
        .field("links_note", l.socket.empty() ? std::string()
                             : !l.reachable ? std::string("no link data: greybeard-ale's scanner isn't answering")
                             : !l.links_reported ? std::string("no link data from greybeard-ale yet")
                             : std::string())
        .begin_array("stations");
    for (auto& st : l.links)
        w.begin_object().field("station", st.station).field("state", st.state).field("since", st.since)
            .field("up", st.up()).end_object();
    w.end_array().end_object();
}

void write_message(JsonWriter& w, const Message& m, bool waiting_for_link = false) {
    w.begin_object()
        .field("id", m.id)
        .field("direction", m.direction)
        .field("from", m.src)
        .field("to", m.dst)
        .field("prec", std::string(1, m.prec))
        .field("precedence", prec_name(m.prec))
        .field("state", m.state)
        .field("attempts", m.attempts)
        .field("created_at", static_cast<long long>(m.created_at))
        .field("updated_at", static_cast<long long>(m.updated_at))
        .field("body", m.body);
    if (!m.last_error.empty()) w.field("last_error", m.last_error);
    if (waiting_for_link) w.field("waiting_for_link", true);
    w.end_object();
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string l; std::getline(in, l);) {
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        size_t a = l.find_first_not_of(' ');
        if (a != std::string::npos) out.push_back(l.substr(a));
    }
    return out;
}

acp127::Address to_address(const std::string& s) {
    acp127::Address a;
    size_t slash = s.find('/');
    if (slash != std::string::npos) {
        a.ri = s.substr(0, slash);
        a.plain = s.substr(slash + 1);
    } else {
        a.plain = s;
    }
    return a;
}

std::string join_addresses(const std::vector<acp127::Address>& v) {
    std::string out;
    for (auto& a : v) {
        if (!out.empty()) out += "\n";
        out += a.ri.empty() ? a.plain : a.plain.empty() ? a.ri : a.ri + "/" + a.plain;
    }
    return out;
}

// send_acp127: builds an ACP 127 message from the request, takes the next
// channel and station serials, stamps the filing time and DTG from the clock
// (UTC, zone Z, four-digit year), and queues it for to_station.
std::string send_acp127(Core& core, std::map<std::string, std::string>& req) {
    acp127::Message m;
    std::string prec = req.count("prec") ? req["prec"] : "R";
    if (prec.size() != 1) return error_reply("precedence must be Z, O, P or R");
    m.prec = prec[0];
    m.automatic = !(req["manual"] == "true" || req["manual"] == "1");
    m.channel = req["channel"];
    std::istringstream rs(req["routing"]);
    for (std::string ri; rs >> ri;) m.routing.push_back(ri);
    m.originator_ri = req["originator_ri"];
    m.transmission_instructions = req["transmission"];
    m.from = to_address(req["from"]);
    for (auto& l : split_lines(req["to"])) m.to.push_back(to_address(l));
    for (auto& l : split_lines(req["info"])) m.info.push_back(to_address(l));
    for (auto& l : split_lines(req["xmt"])) m.xmt.push_back(to_address(l));
    m.line10 = req["line10"];
    m.text = req["text"];

    std::time_t t = std::time(nullptr);
    std::tm u{};
    gmtime_r(&t, &u);
    // MARS stations write format line 3 as "DE RI #0038 3102053"; "fl3":"edition"
    // gives the ACP 127(G) form "DE RI 38 06/2053Z".
    m.mars_fl3 = req["fl3"] != "edition";
    m.filing_yday = u.tm_yday + 1;
    m.filing_day = m.dtg.day = u.tm_mday;
    m.filing_hour = m.dtg.hour = u.tm_hour;
    m.filing_minute = m.dtg.minute = u.tm_min;
    m.dtg.zone = 'Z';
    m.dtg.month = u.tm_mon + 1;
    m.dtg.year = u.tm_year + 1900;

    // Check every field before a serial number is used up.
    std::string err;
    m.channel_serial = 1;
    m.station_serial = "1";
    if (acp127::build(m, &err).empty()) return error_reply(err);
    m.channel_serial = static_cast<int>(core.next_counter("acp127.channel." + m.channel, 999));
    m.station_serial = std::to_string(core.next_counter("acp127.station." + m.originator_ri, 9999));
    std::string text = acp127::build(m, &err);
    if (text.empty()) return error_reply(err);
    std::string id = core.submit(req["to_station"], m.prec, text, &err);
    if (id.empty()) return error_reply(err);
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", id).field("edition", acp127::kEdition).field("message", text).end_object();
    return w.str();
}

// send_acp126: the call-up form "CALL DE STATION NR nnn" (see acp126.h),
// with the next channel serial and NR. "procedure" is "simple" (the default)
// or "plaindress", which adds the DTG line from the clock and needs FM and TO.
std::string send_acp126(Core& core, std::map<std::string, std::string>& req) {
    acp126::Message m;
    std::string prec = req.count("prec") ? req["prec"] : "R";
    if (prec.size() != 1) return error_reply("precedence must be Z, O, P or R");
    m.prec = prec[0];
    m.channel = req["channel"];
    std::istringstream cs(req["calls"]);
    for (std::string c; cs >> c;) m.calls.push_back(c);
    m.station = req["station"];
    if (!req["from"].empty()) m.from = to_address(req["from"]);
    for (auto& l : split_lines(req["to"])) m.to.push_back(to_address(l));
    m.unclas_line = req["unclas"] != "false";
    if (req.count("procedure") && !acp126::procedure_from(req["procedure"], m.procedure))
        return error_reply("procedure must be simple or plaindress");
    m.text = req["text"];
    std::time_t t = std::time(nullptr);
    std::tm u{};
    gmtime_r(&t, &u);
    m.dtg = acp127::Dtg{u.tm_mday, u.tm_hour, u.tm_min, 'Z', u.tm_mon + 1, u.tm_year + 1900};

    std::string err;
    if (acp126::build(m, &err).empty()) return error_reply(err);
    if (!m.channel.empty()) m.channel_serial = static_cast<int>(core.next_counter("acp127.channel." + m.channel, 999));
    m.serial = static_cast<int>(core.next_counter("acp126.nr." + m.station, 999));
    std::string text = acp126::build(m, &err);
    if (text.empty()) return error_reply(err);
    std::string id = core.submit(req["to_station"], m.prec, text, &err);
    if (id.empty()) return error_reply(err);
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", id).field("source", acp126::kSource)
        .field("procedure", acp126::procedure_name(m.procedure)).field("message", text).end_object();
    return w.str();
}

// send_chat: one chat line "<call> DE <station> // <text> // HHMMZ K"
// (acp201.h), timed from the UTC clock, sent at ROUTINE.
std::string send_chat(Core& core, std::map<std::string, std::string>& req) {
    acp201::Chat c;
    c.to = req["call"];
    c.from = req["station"];
    c.text = req["text"];
    std::time_t t = std::time(nullptr);
    std::tm u{};
    gmtime_r(&t, &u);
    c.hour = u.tm_hour;
    c.minute = u.tm_min;
    std::string err, line = acp201::build(c, &err);
    if (line.empty()) return error_reply(err);
    std::string id = core.submit(req["to_station"], 'R', line, &err);
    if (id.empty()) return error_reply(err);
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", id).field("message", line).end_object();
    return w.str();
}

// ---- compose (see compose.h and docs/api.md) ----

compose::Context compose_ctx(Core& core) {
    compose::Context c;
    c.station = core.station();
    return c;
}

// The operator's draft: every key but cmd, template and procedure is a field.
compose::Draft draft_of(std::map<std::string, std::string>& req, const std::set<std::string>& also_skip = {}) {
    compose::Draft d;
    d.template_id = req["template"];
    d.procedure = req["procedure"];
    for (auto& kv : req)
        if (kv.first != "cmd" && kv.first != "template" && kv.first != "procedure" && !also_skip.count(kv.first))
            d.values[kv.first] = kv.second;
    return d;
}

void write_issues(JsonWriter& w, const std::vector<compose::Issue>& issues) {
    w.begin_array("issues");
    for (auto& i : issues)
        w.begin_object()
            .field("field", i.field)
            .field("level", i.level)
            .field("text", i.text)
            .field("fix", i.fix)
            .field("example", i.example)
            .end_object();
    w.end_array();
}

std::string compose_templates() {
    JsonWriter w;
    w.begin_object().field("ok", true).begin_array("procedures");
    for (auto& p : compose::procedures()) {
        w.begin_object().field("id", p.id).field("title", p.title).field("summary", p.summary).field("available", p.available);
        if (!p.available) w.field("why_not", p.why_not);
        w.end_object();
    }
    w.end_array().begin_array("templates");
    for (auto& t : compose::templates()) {
        w.begin_object()
            .field("id", t.id)
            .field("title", t.title)
            .field("summary", t.summary)
            .field("procedure", t.procedure)
            .field("prec", std::string(1, t.prec))
            .begin_array("procedures");
        for (auto& p : t.procedures) w.field("", p);
        w.end_array().end_object();
    }
    w.end_array().end_object();
    return w.str();
}

std::string compose_form(Core& core, std::map<std::string, std::string>& req) {
    std::string err;
    auto fields = compose::form(req["template"], req["procedure"], compose_ctx(core), &err);
    if (fields.empty()) return error_reply(err);
    const compose::Template* t = compose::find_template(req["template"]);
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("template", t->id)
        .field("title", t->title)
        .field("summary", t->summary)
        .field("procedure", req["procedure"].empty() ? t->procedure : req["procedure"])
        .begin_array("fields");
    for (auto& f : fields) {
        w.begin_object()
            .field("key", f.key)
            .field("label", f.label)
            .field("kind", f.kind)
            .field("required", f.required)
            .field("default", f.def)
            .field("hint", f.hint)
            .field("example", f.example);
        if (!f.choices.empty()) {
            w.begin_array("choices");
            for (auto& c : f.choices) w.field("", c);
            w.end_array();
        }
        w.end_object();
    }
    w.end_array().end_object();
    return w.str();
}

// compose_check: the issues, and the message as it would go (serials shown
// as 1; they are taken only when it is sent).
std::string compose_check(Core& core, std::map<std::string, std::string>& req) {
    compose::Result r = compose::check(draft_of(req), compose_ctx(core));
    JsonWriter w;
    w.begin_object().field("ok", true).field("ready", r.ready).field("procedure", r.procedure);
    write_issues(w, r.issues);
    if (r.ready) w.field("preview", r.message).field("to_station", r.to_station).field("prec", std::string(1, r.prec));
    w.end_object();
    return w.str();
}

// compose_send: refused while any error stands; otherwise takes the serials
// and queues the message.
std::string compose_send(Core& core, std::map<std::string, std::string>& req) {
    compose::Draft d = draft_of(req);
    compose::Context ctx = compose_ctx(core);
    ctx.now = std::time(nullptr);
    compose::Result r = compose::check(d, ctx);
    if (!r.ready) {
        JsonWriter w;
        w.begin_object().field("ok", false).field("error", "the message has errors to fix first").field("ready", false);
        write_issues(w, r.issues);
        w.end_object();
        return w.str();
    }
    if (r.to_station.empty()) return error_reply("no station to send to");
    compose::Serials s;
    if (!r.channel_counter.empty()) s.channel_serial = static_cast<int>(core.next_counter(r.channel_counter, 999));
    if (!r.serial_counter.empty()) {
        int n = static_cast<int>(core.next_counter(r.serial_counter, r.serial_wrap));
        s.station_serial = n;
        s.nr = n;
    }
    r = compose::check(d, ctx, s);
    if (!r.ready) return error_reply("the message changed while it was being sent; check it again");
    std::string err;
    std::string id = core.submit(r.to_station, r.prec, r.message, &err);
    if (id.empty()) return error_reply(err);
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", id).field("procedure", r.procedure).field("message", r.message);
    write_issues(w, r.issues);
    w.end_object();
    return w.str();
}

// compose_advise: the advisor's answer when one is set and answers, else the
// built-in guidance. "source" says which.
std::string compose_advise(Core& core, std::map<std::string, std::string>& req) {
    compose::AdviceRequest a;
    a.kind = req["kind"].empty() ? "explain" : req["kind"];
    if (a.kind != "explain" && a.kind != "suggest") return error_reply("kind must be explain or suggest");
    a.field = req["field"];
    a.question = req["question"];
    a.draft = draft_of(req, {"kind", "field", "question"});
    compose::Context ctx = compose_ctx(core);
    a.issues = compose::check(a.draft, ctx).issues;
    std::string text, source = "builtin", note;
    if (auto adv = compose::advisor()) {
        std::string err;
        if (adv->advise(a, text, &err)) source = adv->name();
        else note = err;
    }
    if (source == "builtin") text = compose::builtin_advice(a, ctx);
    JsonWriter w;
    w.begin_object().field("ok", true).field("source", source).field("text", text);
    if (!note.empty()) w.field("advisor_error", note);
    w.end_object();
    return w.str();
}

int limit_of(std::map<std::string, std::string>& req, int def, int max, bool* bad) {
    int limit = def;
    try {
        if (req.count("limit")) limit = std::stoi(req["limit"]);
    } catch (...) {
        *bad = true;
    }
    if (limit < 1 || limit > max) limit = def;
    return limit;
}

// parse_traffic: reads a stored message in any form MARS stations send
// (received.h), or a chat line (acp201.h), and lists what it had to tolerate.
std::string parse_traffic(Core& core, std::map<std::string, std::string>& req) {
    auto msg = core.get(req["id"]);
    if (!msg) return error_reply("no message with that id");
    std::string err;
    if (acp201::looks_like_chat(msg->body)) {
        acp201::Chat c;
        acp201::parse(msg->body, c, &err);
        std::string by, warnings;
        for (auto& b : c.receipt_by) by += (by.empty() ? "" : " ") + b;
        for (auto& x : c.warnings) warnings += (warnings.empty() ? "" : "\n") + x;
        char hhmm[8];
        std::snprintf(hhmm, sizeof hhmm, "%02d%02d", c.hour, c.minute);
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("id", msg->id)
            .field("form", "acp201-chat")
            .field("procedure", "ACP201_CHAT")
            .field("to", c.to)
            .field("from", c.from)
            .field("kind", acp201::kind_name(c.kind))
            .field("text", c.text)
            .field("time", std::string(hhmm) + (c.zulu ? "Z" : ""))
            .field("number", c.number)
            .field("prec", c.prec ? std::string(1, c.prec) : "")
            .field("receipt_of", c.receipt_of)
            .field("receipt_serial", c.receipt_serial)
            .field("receipt_time", c.receipt_time)
            .field("receipt_by", by)
            .field("warnings", warnings)
            .end_object();
        return w.str();
    }
    received::Message m;
    if (!received::parse(msg->body, m, &err)) return error_reply("not a message this reader can place: " + err);
    std::string routing, warnings;
    for (auto& r : m.routing) routing += (routing.empty() ? "" : " ") + r;
    std::string relay;
    for (auto& r : m.relay) relay += (relay.empty() ? "" : " ") + r;
    for (auto& x : m.warnings) warnings += (warnings.empty() ? "" : "\n") + x;
    std::string prec = m.prec ? std::string(1, m.prec) : "";
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("id", msg->id)
        .field("form", received::form_name(m.form))
        .field("procedure", received::procedure(m))
        .field("channel", m.channel)
        .field("channel_serial", m.channel_serial)
        .field("prec", prec)
        .field("precedence", m.prec ? acp127::prec_name(m.prec) : "")
        .field("routing", routing)
        .field("originator", m.originator)
        .field("relay", relay)
        .field("serial", m.serial)
        .field("time_filed", m.time_filed)
        .field("security", m.security)
        .field("zek", m.zek_line)
        .field("key_indicator", m.key_indicator)
        .field("group_count", m.group_count)
        .field("enciphered", m.enciphered)
        .field("from", join_addresses({m.from}))
        .field("to", join_addresses(m.to))
        .field("info", join_addresses(m.info))
        .field("class_line", m.class_line)
        .field("text", received::text_of(m))
        .field("warnings", warnings)
        .end_object();
    return w.str();
}

std::string parse_acp127(Core& core, std::map<std::string, std::string>& req) {
    auto msg = core.get(req["id"]);
    if (!msg) return error_reply("no message with that id");
    acp127::Message m;
    std::string err;
    if (!acp127::parse(msg->body, m, &err)) return error_reply("not an ACP 127 message: " + err);
    char dtg[32];
    std::snprintf(dtg, sizeof dtg, "%02d%02d%02d%c", m.dtg.day, m.dtg.hour, m.dtg.minute, m.dtg.zone);
    std::string routing;
    for (auto& ri : m.routing) routing += (routing.empty() ? "" : " ") + ri;
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("id", msg->id)
        .field("edition", acp127::kEdition)
        .field("automatic", m.automatic)
        .field("channel", m.channel)
        .field("channel_serial", m.channel_serial)
        .field("prec", std::string(1, m.prec))
        .field("precedence", acp127::prec_name(m.prec))
        .field("routing", routing)
        .field("originator_ri", m.originator_ri)
        .field("station_serial", m.station_serial)
        .field("dtg", dtg)
        .field("month", m.dtg.month)
        .field("year", m.dtg.year)
        .field("from", join_addresses({m.from}))
        .field("to", join_addresses(m.to))
        .field("info", join_addresses(m.info))
        .field("xmt", join_addresses(m.xmt))
        .field("text", m.text)
        .end_object();
    return w.str();
}

}  // namespace

ApiServer::ApiServer(Core& core, uint16_t port) : core_(core), port_(port) {}

ApiServer::~ApiServer() { stop(); }

bool ApiServer::start() {
    listen_fd_ = tcp_listen_local(port_);
    if (listen_fd_ < 0) return false;
    running_ = true;
    accept_ = std::thread([this] { accept_loop(); });
    return true;
}

void ApiServer::stop() {
    if (!running_.exchange(false)) return;
    if (accept_.joinable()) accept_.join();
    close_fd(listen_fd_);
    std::lock_guard<std::mutex> g(clients_mu_);
    for (auto& t : clients_)
        if (t.joinable()) t.join();
    clients_.clear();
}

void ApiServer::accept_loop() {
    while (running_) {
        if (wait_readable(listen_fd_, 200) != 1) continue;
        int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) continue;
        std::lock_guard<std::mutex> g(clients_mu_);
        clients_.emplace_back([this, fd] { client_loop(fd); });
    }
}

void ApiServer::client_loop(int fd) {
    std::string buf;
    while (running_) {
        int r = wait_readable(fd, 200);
        if (r < 0) break;
        if (r == 0) continue;
        if (read_some(fd, buf) <= 0) break;
        if (buf.size() > 65536) {
            send_all(fd, error_reply("request too long") + "\n");
            break;
        }
        size_t nl;
        bool ok = true;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (line.find_first_not_of(" \r\t") == std::string::npos) continue;
            if (!send_all(fd, handle(core_, line) + "\n")) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
    }
    close_fd(fd);
}

std::string ApiServer::handle(Core& core, const std::string& line) {
    std::map<std::string, std::string> req;
    if (!parse_flat_object(line, req)) return error_reply("request must be one flat JSON object");
    const std::string cmd = req["cmd"];

    if (cmd == "status") {
        CoreStatus s = core.status();
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("station", s.station)
            .field("modem", s.modem)
            .field("modem_ready", s.modem_ready)
            .field("modem_busy", s.modem_busy)
            .field("sending", s.sending)
            .field("queued", s.counts.queued)
            .field("sent", s.counts.sent)
            .field("acked", s.counts.acked)
            .field("failed", s.counts.failed)
            .field("received", s.counts.received)
            .field("acks_waiting", s.acks_waiting)
            .field("frames_dropped", static_cast<long long>(s.frames_dropped));
        auto adv = compose::advisor();
        w.field("advisor", adv ? adv->name() : std::string());
        // Link state from greybeard-ale (link_monitor.h); "link": null with no source.
        write_link(w, s);
        w.end_object();
        return w.str();
    }

    if (cmd == "send") {
        std::string prec = req.count("prec") ? req["prec"] : "R";
        if (prec.size() != 1) return error_reply("precedence must be Z, O, P or R");
        std::string err;
        std::string id = core.submit(req["to"], prec[0], req["body"], &err);
        if (id.empty()) return error_reply(err);
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", id).end_object();
        return w.str();
    }

    if (cmd == "list") {
        int limit = 50;
        try {
            if (req.count("limit")) limit = std::stoi(req["limit"]);
        } catch (...) {
            return error_reply("limit must be a number");
        }
        if (limit < 1 || limit > 1000) limit = 50;
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("messages");
        for (const Message& m : core.list(limit)) write_message(w, m);
        w.end_array().end_object();
        return w.str();
    }

    if (cmd == "get") {
        auto m = core.get(req["id"]);
        if (!m) return error_reply("no message with that id");
        JsonWriter w;
        w.begin_object().field("ok", true);
        w.begin_array("messages");
        write_message(w, *m);
        w.end_array().end_object();
        return w.str();
    }

    if (cmd == "send_acp127") return send_acp127(core, req);
    if (cmd == "parse_acp127") return parse_acp127(core, req);
    if (cmd == "send_acp126") return send_acp126(core, req);
    if (cmd == "parse_traffic") return parse_traffic(core, req);
    if (cmd == "send_chat") return send_chat(core, req);
    if (cmd == "compose_templates") return compose_templates();
    if (cmd == "compose_form") return compose_form(core, req);
    if (cmd == "compose_check") return compose_check(core, req);
    if (cmd == "compose_send") return compose_send(core, req);
    if (cmd == "compose_advise") return compose_advise(core, req);

    if (cmd == "queue") {
        bool bad = false;
        int limit = limit_of(req, 100, 1000, &bad);
        if (bad) return error_reply("limit must be a number");
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("messages");
        for (const Message& m : core.queue(limit))
            write_message(w, m, m.state == "queued" && core.waits_for_link(m.dst));
        w.end_array().end_object();
        return w.str();
    }

    if (cmd == "log") {
        bool bad = false;
        int limit = limit_of(req, 100, 1000, &bad);
        long long after = 0;
        try {
            if (req.count("after")) after = std::stoll(req["after"]);
        } catch (...) {
            bad = true;
        }
        if (bad) return error_reply("after and limit must be numbers");
        auto ev = core.events(after, limit);
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("events");
        for (auto& e : ev)
            w.begin_object()
                .field("seq", static_cast<long long>(e.seq))
                .field("ts", static_cast<long long>(e.ts))
                .field("id", e.msg_id)
                .field("text", e.text)
                .end_object();
        w.end_array().field("last", static_cast<long long>(ev.empty() ? after : ev.back().seq)).end_object();
        return w.str();
    }

    return error_reply(
        "unknown cmd; use status, send, list, get, queue, log, send_acp127, send_acp126, send_chat, parse_acp127, "
        "parse_traffic, compose_templates, compose_form, compose_check, compose_send or compose_advise");
}

}  // namespace pna
