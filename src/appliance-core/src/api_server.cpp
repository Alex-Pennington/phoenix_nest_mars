#include "api_server.h"

#include "api_ops.h"

#include "acp126.h"
#include "acp127.h"
#include "acp201.h"
#include "codress.h"
#include "compose.h"
#include "crypto.h"
#include "digest.h"
#include "keys.h"
#include "station.h"
#include "received.h"
#include "json.h"
#include "net.h"

#include <sys/socket.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
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
    if (keys::Keyring* kr = core.keyring())
        for (auto& k : kr->for_sending()) c.codress_keys.push_back({k.indicator, k.net});
    if (Ops* ops = core.ops()) {
        c.lookup = [ops](const std::string& name) -> std::optional<compose::BookHit> {
            auto e = ops->book_find(name);
            if (!e) return std::nullopt;
            compose::BookHit h;
            h.name = e->name;
            h.kind = e->kind;
            h.ri = e->ri;
            Resolved why;
            h.calls = ops->send_targets(e->name, &why);
            h.unknown = why.unknown;
            return h;
        };
    }
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

std::string compose_templates(Core& core) {
    // Codress can be sent only with a key current now.
    std::string codress_no = compose::codress_why_not(compose_ctx(core));
    JsonWriter w;
    w.begin_object().field("ok", true).begin_array("procedures");
    for (auto& p : compose::procedures()) {
        bool available = p.available && !(p.id == "ACP127_CODRESS" && !codress_no.empty());
        std::string why_not = p.available ? codress_no : p.why_not;
        w.begin_object().field("id", p.id).field("title", p.title).field("summary", p.summary).field("available", available);
        if (!available) w.field("why_not", why_not);
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
    compose::Result r = compose::check(draft_of(req, {"sign"}), compose_ctx(core));
    JsonWriter w;
    w.begin_object().field("ok", true).field("ready", r.ready).field("procedure", r.procedure);
    write_issues(w, r.issues);
    if (r.ready) {
        w.field("preview", r.message).field("to_station", r.to_station).field("prec", std::string(1, r.prec));
        if (!r.codress_key.empty())
            w.field("codress_key", r.codress_key)
                .field("note", "This is the message before it is enciphered. It goes enciphered in key " +
                                   r.codress_key + ": everything after the date-time group becomes cipher groups.");
        w.begin_array("to_targets");
        for (auto& t : r.to_targets) w.field("", t);
        w.end_array();
    }
    w.end_object();
    return w.str();
}

// compose_send: refused while any error stands; otherwise takes the serials
// and queues the message.
std::string compose_send(Core& core, std::map<std::string, std::string>& req) {
    compose::Draft d = draft_of(req, {"sign"});
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
    if (r.to_targets.empty()) return error_reply("no station to send to");
    compose::Serials s;
    if (!r.channel_counter.empty()) s.channel_serial = static_cast<int>(core.next_counter(r.channel_counter, 999));
    if (!r.serial_counter.empty()) {
        int n = static_cast<int>(core.next_counter(r.serial_counter, r.serial_wrap));
        s.station_serial = n;
        s.nr = n;
    }
    r = compose::check(d, ctx, s);
    if (!r.ready) return error_reply("the message changed while it was being sent; check it again");
    // Signed by this station (station.h) unless the operator says not to.
    // Codress is signed inside, before it is enciphered.
    const bool signed_msg = core.identity() && req["sign"] != "N" && r.procedure != "ACP201_CHAT";
    if (signed_msg) r.message = station::sign(r.message, *core.identity());
    // One copy for each station: a collective from the station book fans out.
    std::string err;
    std::vector<std::string> ids, refused;
    // Codress: each copy is enciphered on its own, with a fresh nonce.
    std::string codress_key;
    if (!r.codress_key.empty()) {
        keys::Keyring* kr = core.keyring();
        if (!kr) return error_reply("this station has no keyring");
        if (!kr->send_key(r.codress_key, codress_key, &err)) return error_reply(err);
    }
    std::string sent_text = r.message;
    for (auto& t : r.to_targets) {
        std::string e, id;
        if (!codress_key.empty()) {
            std::string out;
            if (!codress::encipher(r.message, r.codress_key, codress_key, out, &e)) {
                refused.push_back(t + ": " + e);
                err = e;
                continue;
            }
            sent_text = out;
            id = core.submit_codress(t, r.prec, out, r.message, r.codress_key, &e);
        } else {
            id = core.submit(t, r.prec, r.message, &e);
        }
        if (!id.empty()) {
            ids.push_back(id);
        } else {
            refused.push_back(t + ": " + e);
            err = e;
        }
    }
    crypto::wipe(codress_key);
    if (ids.empty()) return error_reply(err);
    JsonWriter w;
    w.begin_object().field("ok", true).field("id", ids[0]).field("procedure", r.procedure).field("message", sent_text);
    if (!r.codress_key.empty()) w.field("codress_key", r.codress_key).field("clear", r.message);
    w.field("signed", signed_msg);
    w.begin_array("ids");
    for (auto& i : ids) w.field("", i);
    w.end_array().begin_array("to_targets");
    for (auto& t : r.to_targets) w.field("", t);
    w.end_array();
    if (!refused.empty()) {
        w.begin_array("refused");
        for (auto& x : refused) w.field("", x);
        w.end_array();
    }
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

// How a service message names a message: "#0209 062310Z NOV 2019", as the
// SVC template asks.
std::string repeat_ref(const received::Message& m, const std::string& id) {
    static const char* kMon[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    std::string r = m.serial.empty() ? id : "#" + m.serial;
    if (m.has_dtg && m.dtg.month >= 1 && m.dtg.month <= 12) {
        char b[40];
        std::snprintf(b, sizeof b, " %02d%02d%02d%c %s", m.dtg.day, m.dtg.hour, m.dtg.minute, m.dtg.zone,
                      kMon[m.dtg.month - 1]);
        r += b;
        if (m.dtg.year) r += " " + std::to_string(m.dtg.year);
    }
    return r;
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
    // Codress: read the plain copy when this station has one (Core::read_codress).
    const bool enciphered = codress::is_codress(msg->body);
    std::string key_indicator = enciphered ? codress::indicator_of(msg->body) : "";
    auto clear = enciphered ? core.clear(msg->id) : std::nullopt;
    const std::string& text_in = clear ? clear->first : msg->body;
    // The MSC digest (digest.h): of what went over the air, and for codress
    // also of the plaindress inside it.
    digest::Check air = digest::check(msg->body);
    digest::Check inner = clear ? digest::check(clear->first) : digest::Check::None;
    received::Message m;
    if (!received::parse(text_in, m, &err)) return error_reply("not a message this reader can place: " + err);
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
        .field("digest", digest::check_name(air));
    // A station signature (station.h): on the plain copy for codress.
    station::SigCheck sc =
        station::check_signature(text_in, [&](const std::string& c) { return core.card_for(c); });
    w.field("signature", sc.state).field("signer", sc.signer);
    if (sc.state == "good") w.field("signer_checked", core.identity() && sc.signer == core.identity()->station
                                                         ? true
                                                         : core.card_verified(sc.signer));
    if (enciphered) {
        w.field("codress", true)
            .field("codress_key", key_indicator)
            .field("deciphered", static_cast<bool>(clear))
            .field("inner_digest", digest::check_name(inner));
        if (!clear) {
            std::string k;
            bool held = core.keyring() && core.keyring()->receive_key(key_indicator, k);
            crypto::wipe(k);
            w.field("why_not_deciphered",
                    held ? "its groups did not check with key " + key_indicator +
                               " (damaged, changed or another key), or it came before the key was loaded: see the log"
                         : "this station holds no key " + key_indicator);
        }
    }
    // A digest that doesn't match: the text may have been garbled on the way.
    // The message stands; offer a service message asking for a repeat.
    if (air == digest::Check::Fail || inner == digest::Check::Fail) {
        w.field("possibly_garbled", true);
        w.begin_object("repeat_request")
            .field("template", "SVC")
            .field("to_station", msg->src)
            .field("refs", repeat_ref(m, msg->id))
            .field("svc_text", "REQUEST REPEAT, DIGEST FAILED")
            .end_object();
    }
    w.end_object();
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

// Sends a block of text (a card, a sealed tape) to one station as ACP 127
// plaindress, signed. Returns the message id, or "" with *error.
std::string send_block(Core& core, const std::string& to, const std::string& text, std::string* error) {
    std::map<std::string, std::string> req = {{"template", "FREE_FORM"},
                                              {"procedure", "ACP127_PLAINDRESS"},
                                              {"prec", "R"},
                                              {"routing", to},
                                              {"originator", core.station()},
                                              {"from", "MARS STATION " + core.station()},
                                              {"to", to},
                                              {"to_station", to},
                                              {"text", text}};
    JsonValue v;
    std::string reply = compose_send(core, req);
    if (!parse_json(reply, v)) {
        if (error) *error = "could not queue it";
        return "";
    }
    const JsonValue* ok = v.get("ok");
    const JsonValue* id = v.get("id");
    if (!ok || !ok->b || !id) {
        const JsonValue* e = v.get("error");
        if (error) *error = e && e->type == JsonValue::Type::String ? e->s : "could not queue it";
        const JsonValue* is = v.get("issues");
        if (error && is && is->is_array() && !is->arr.empty()) {
            const JsonValue* t = is->arr[0].get("text");
            if (t) *error += ": " + t->s;
        }
        return "";
    }
    return id->s;
}

// Station keys (station.h): this station's card and backup, other stations'
// cards, and key tapes sealed to them.
std::optional<std::string> station_cmd(Core& core, const std::string& cmd, std::map<std::string, std::string>& req) {
    static const std::set<std::string> kCmds = {"station_card", "station_backup", "station_restore", "card_import",
                                                "cards_list",   "card_check",     "card_send",       "net_tape",
                                                "tape_seal"};
    if (!kCmds.count(cmd)) return std::nullopt;
    const station::Identity* me = core.identity();
    if (!me && cmd != "station_restore" && cmd != "cards_list") return error_reply("this station has no keys of its own");
    std::string err;
    auto card_for = [&](const std::string& c) { return core.card_for(c); };

    if (cmd == "station_card") {
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("station", me->station)
            .field("fingerprint", station::fingerprint(station::card_of(*me)))
            .field("card", station::format_card(*me))
            .end_object();
        return w.str();
    }
    if (cmd == "station_backup") {
        std::string out;
        bool ok = station::backup(*me, req["passphrase"], out, &err);
        crypto::wipe(req["passphrase"]);
        if (!ok) return error_reply(err);
        core.note("an encrypted backup of the station keys was made");
        JsonWriter w;
        w.begin_object().field("ok", true).field("backup", out).end_object();
        return w.str();
    }
    if (cmd == "station_restore") {
        station::Identity id;
        bool ok = station::restore(req["text"], req["passphrase"], id, &err);
        crypto::wipe(req["passphrase"]);
        if (!ok) return error_reply(err);
        if (id.station != core.station()) {
            id.wipe();
            return error_reply("that backup is for " + id.station + ", not this station");
        }
        ok = core.replace_identity(id, &err);
        std::string fp = station::fingerprint(station::card_of(id));
        id.wipe();
        if (!ok) return error_reply(err);
        JsonWriter w;
        w.begin_object().field("ok", true).field("fingerprint", fp).end_object();
        return w.str();
    }
    if (cmd == "card_import") {
        std::string text = req["text"], source = req["source"].empty() ? "pasted" : req["source"];
        if (!req["id"].empty()) {
            auto m = core.get(req["id"]);
            if (!m) return error_reply("no message with that id");
            text = m->body;
            source = "message " + m->id + " from " + m->src;
        }
        station::Card c;
        std::string what;
        if (!core.add_card(text, source, req["replace"] == "Y", c, what, &err)) return error_reply(err);
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("station", c.station)
            .field("fingerprint", station::fingerprint(c))
            .field("result", what)
            .field("checked", core.card_verified(c.station))
            .field("next", core.card_verified(c.station)
                               ? std::string()
                               : "Read the fingerprint back with " + c.station + " by voice, then card_check it.")
            .end_object();
        return w.str();
    }
    if (cmd == "cards_list") {
        JsonWriter w;
        w.begin_object().field("ok", true);
        if (me) w.field("station", me->station).field("fingerprint", station::fingerprint(station::card_of(*me)));
        w.begin_array("cards");
        for (auto& c : core.cards())
            w.begin_object()
                .field("station", c.station)
                .field("fingerprint", c.fingerprint)
                .field("checked", c.verified)
                .field("source", c.source)
                .field("added", static_cast<long long>(c.added))
                .end_object();
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "card_check") {
        if (!core.verify_card(req["station"], req["fingerprint"], &err)) return error_reply(err);
        JsonWriter w;
        w.begin_object().field("ok", true).field("station", req["station"]).field("checked", true).end_object();
        return w.str();
    }
    if (cmd == "card_send") {
        std::string id = send_block(core, req["to"], station::format_card(*me), &err);
        if (id.empty()) return error_reply(err);
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", id).end_object();
        return w.str();
    }
    // net_tape: the keymaster makes a net's tape, loads it here, and seals
    // one copy to each member whose card it holds; send=Y queues each copy
    // to its station over the air.
    // tape_seal: seals a tape the operator has (from pna-keymaster) to one station.
    std::string tape_text, net, tape_id;
    std::vector<std::string> members;
    if (cmd == "net_tape") {
        keys::Keyring* kr = core.keyring();
        if (!kr) return error_reply("this station has no keyring");
        net = req["net"];
        for (auto& c : net) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        int64_t now = kr->now();
        int64_t start = now - now % 86400;  // today, 0000Z
        if (!req["start"].empty()) {
            std::string st = req["start"];
            if (st.size() == 8) st += "0000";
            if (st.back() != 'Z') st += "Z";
            if (!keys::parse_time(st, start)) return error_reply("start must be YYYYMMDD or YYYYMMDDHHMM, UTC");
        }
        int days = 31, period = 24;
        try {
            if (!req["days"].empty()) days = std::stoi(req["days"]);
            if (!req["period_hours"].empty()) period = std::stoi(req["period_hours"]);
        } catch (...) {
            return error_reply("days and period_hours must be numbers");
        }
        keys::Tape t;
        if (!keys::make_tape(net, start, days, period, now, t, &err)) return error_reply(err);
        tape_text = keys::format_tape(t);
        for (auto& k : t.keys) crypto::wipe(k.secret);
        tape_id = t.tape;
        keys::ImportResult ir;
        if (!kr->import_tape(tape_text, ir, &err)) {
            crypto::wipe(tape_text);
            return error_reply("the new tape did not load here: " + err);
        }
        core.note("key tape " + tape_id + " for " + net + " made here: " + std::to_string(ir.added) + " key(s) loaded");
        std::string who = req["members"];
        std::vector<std::string> named;
        {
            std::string cur;
            for (char c : who + " ") {
                if (c == ' ' || c == ',') {
                    if (!cur.empty()) named.push_back(cur);
                    cur.clear();
                } else {
                    cur += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
            }
        }
        // A name in the station book (a collective) stands for its stations.
        for (auto& n : named) {
            std::vector<std::string> t2 = core.ops() ? core.ops()->send_targets(n) : std::vector<std::string>{};
            if (t2.empty()) t2.push_back(n);
            for (auto& x : t2)
                if (x != me->station && std::find(members.begin(), members.end(), x) == members.end())
                    members.push_back(x);
        }
    } else {  // tape_seal
        tape_text = req["text"];
        keys::Tape t;
        if (!keys::parse_tape(tape_text, t, &err)) return error_reply("not a key tape: " + err);
        for (auto& k : t.keys) crypto::wipe(k.secret);
        net = t.net;
        tape_id = t.tape;
        members.push_back(req["to"]);
    }
    JsonWriter w;
    w.begin_object().field("ok", true).field("net", net).field("tape", tape_id).begin_array("sealed");
    std::vector<std::string> no_card;
    for (auto& m : members) {
        auto c = card_for(m);
        if (!c) {
            no_card.push_back(m);
            continue;
        }
        std::string sealed;
        if (!station::seal_tape(tape_text, net, tape_id, *me, *c, sealed, &err)) {
            no_card.push_back(m + " (" + err + ")");
            continue;
        }
        w.begin_object().field("station", m).field("checked", core.card_verified(m)).field("text", sealed);
        if (req["send"] == "Y") {
            std::string e, id = send_block(core, m, sealed, &e);
            w.field("id", id);
            if (id.empty()) w.field("error", e);
        }
        w.end_object();
    }
    crypto::wipe(tape_text);
    w.end_array().begin_array("no_card");
    for (auto& n : no_card) w.field("", n);
    w.end_array().end_object();
    if (cmd == "net_tape" && !members.empty())
        core.note("key tape " + tape_id + " sealed to " + std::to_string(members.size() - no_card.size()) + " station(s)");
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
        if (Ops* ops = core.ops()) {
            auto d = ops->duty_current();
            std::string net = ops->current_net();
            w.field("on_duty", d ? d->op : std::string()).field("net", net);
            // The scheduled net that matters now: one due or open first, else the next.
            auto next = ops->sched_upcoming(1);
            if (next.empty()) {
                w.field_null("next_net");
            } else {
                auto& n = next[0];
                w.begin_object("next_net")
                    .field("name", n.sched.name)
                    .field("state", n.state)
                    .field("start", static_cast<long long>(n.start))
                    .field("end", static_cast<long long>(n.end))
                    .field("net", n.net)
                    .end_object();
            }
        }
        w.end_object();
        return w.str();
    }

    if (cmd == "send") {
        std::string prec = req.count("prec") ? req["prec"] : "R";
        if (prec.size() != 1) return error_reply("precedence must be Z, O, P or R");
        // A name in the station book goes to the stations it stands for: a
        // collective sends one copy to each.
        std::vector<std::string> to = core.ops() ? core.ops()->send_targets(req["to"]) : std::vector<std::string>{};
        if (to.empty()) to.push_back(req["to"]);
        std::string err;
        std::vector<std::string> ids;
        for (auto& t : to) {
            std::string id = core.submit(t, prec[0], req["body"], &err);
            if (id.empty()) return ids.empty() ? error_reply(err) : error_reply(err + " (sent to " + std::to_string(ids.size()) + " before this)");
            ids.push_back(id);
        }
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", ids[0]).begin_array("ids");
        for (auto& i : ids) w.field("", i);
        w.end_array().end_object();
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
    if (cmd == "compose_templates") return compose_templates(core);
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

    // Codress keys (keys.h). Key bytes never leave the core.
    if (cmd == "key_import" || cmd == "keys_list" || cmd == "key_zeroize" || cmd == "key_open") {
        keys::Keyring* kr = core.keyring();
        if (!kr) return error_reply("this station has no keyring");
        std::string err;
        if (cmd == "key_import") {
            keys::ImportResult r;
            bool ok = kr->import_tape(req["text"], r, &err);
            crypto::wipe(req["text"]);
            if (!ok) return error_reply("key tape not loaded: " + err);
            core.note("key tape " + r.tape + " for " + r.net + " loaded: " + std::to_string(r.added) + " key(s) added, " +
                      std::to_string(r.already) + " already held, " + std::to_string(r.expired) + " already ended");
            JsonWriter w;
            w.begin_object()
                .field("ok", true)
                .field("net", r.net)
                .field("tape", r.tape)
                .field("added", r.added)
                .field("already", r.already)
                .field("expired", r.expired)
                .end_object();
            return w.str();
        }
        if (cmd == "key_open") {
            // A sealed key tape (station.h), from a stored message or pasted text.
            if (!core.identity()) return error_reply("this station has no keys of its own to open a sealed tape with");
            std::string text = req["text"];
            if (!req["id"].empty()) {
                auto m = core.get(req["id"]);
                if (!m) return error_reply("no message with that id");
                text = m->body;
            }
            std::string tape;
            station::SealedHead h;
            if (!station::open_tape(text, *core.identity(), [&](const std::string& c) { return core.card_for(c); }, tape,
                                    h, &err))
                return error_reply("sealed key tape not opened: " + err);
            keys::ImportResult r;
            bool ok = kr->import_tape(tape, r, &err);
            crypto::wipe(tape);
            if (!ok) return error_reply("the sealed tape opened but did not load: " + err);
            bool checked = core.card_verified(h.from);
            core.note("sealed key tape " + r.tape + " for " + r.net + " from " + h.from + " opened and loaded: " +
                      std::to_string(r.added) + " key(s) added");
            JsonWriter w;
            w.begin_object()
                .field("ok", true)
                .field("net", r.net)
                .field("tape", r.tape)
                .field("from", h.from)
                .field("from_checked", checked)
                .field("added", r.added)
                .field("already", r.already)
                .field("expired", r.expired)
                .end_object();
            return w.str();
        }
        if (cmd == "key_zeroize") {
            int n = kr->zeroize(req["which"], &err);
            if (n <= 0) return error_reply(err);
            core.note(std::to_string(n) + " key(s) of " + req["which"] + " zeroized by the operator");
            JsonWriter w;
            w.begin_object().field("ok", true).field("zeroized", n).end_object();
            return w.str();
        }
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("keys");
        for (auto& k : kr->list())
            w.begin_object()
                .field("indicator", k.indicator)
                .field("net", k.net)
                .field("tape", k.tape)
                .field("from", keys::time_text(k.from))
                .field("until", keys::time_text(k.until))
                .field("state", k.state)
                .end_object();
        w.end_array().end_object();
        return w.str();
    }

    if (auto r = station_cmd(core, cmd, req)) return *r;

    if (auto r = handle_ops(core, cmd, req)) return *r;

    return error_reply(
        "unknown cmd; use status, send, list, get, queue, log, send_acp127, send_acp126, send_chat, parse_acp127, "
        "parse_traffic, compose_templates, compose_form, compose_check, compose_send, compose_advise, key_import, keys_list, key_zeroize, key_open, station_card, station_backup, station_restore, card_import, cards_list, card_check, card_send, net_tape, tape_seal, or the book's calls (docs/api.md): book_*, net_*, sched_*, duty_*, log_add, station_log, report_monthly");
}

}  // namespace pna
