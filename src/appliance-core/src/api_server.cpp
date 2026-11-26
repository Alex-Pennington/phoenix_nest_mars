#include "api_server.h"
#include "api_bbs.h"
#include "api_key.h"
#include "api_inbox.h"
#include "bbs.h"
#include "api_forms.h"
#include "api_guide.h"
#include "forms.h"

#include "api_ops.h"
#include "cipher_slot.h"
#include "filemsg.h"

#include "acp126.h"
#include "acp127.h"
#include "acp201.h"
#include "codress.h"
#include "compose.h"
#include "crypto.h"
#include "digest.h"
#include "keys.h"
#include "station.h"
#include "bundle.h"
#include "parts.h"
#include "ops.h"
#include "received.h"
#include "json.h"
#include "net.h"
#include "training.h"
#include "station_view.h"
#include "records.h"
#include "settings.h"

#include <sys/socket.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <chrono>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

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
        .field("sends", m.sends)
        .field("world", m.world)
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
    // A station reached as plain text is an MSC CP station: its chat window
    // takes CP's chat form, with an ID it answers "ack|ID".
    if (!req["to_station"].empty() && core.link_format(req["to_station"]) == "plain") {
        c.day = u.tm_mday;
        c.cp_id = acp201::cp_chat_id(c.from, static_cast<int64_t>(t) * 1000 +
                                                  static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      std::chrono::system_clock::now().time_since_epoch()).count() % 1000));
    }
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
    compose::Result r = compose::check(draft_of(req, {"sign", "hold"}), compose_ctx(core));
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
// Station calls in a list (space, comma or ; between them).
static std::vector<std::string> split_targets(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s + " ") {
        if (c == ' ' || c == ',' || c == ';' || c == '\n' || c == '\r' || c == '\t') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    return out;
}

std::string compose_send(Core& core, std::map<std::string, std::string>& req) {
    // Ask 37: a station of the amateur world is refused before the form is checked.
    for (auto& t : split_targets(req["to_station"]))
        if (std::string why = core.world_refusal(t); !why.empty()) return error_reply(why);
    compose::Draft d = draft_of(req, {"sign", "hold"});
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
    for (auto& t : r.to_targets)  // the whole send, before any copy is queued
        if (std::string why = core.world_refusal(t); !why.empty()) return error_reply(why);
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
            // ---- CIPHER HAND-OFF (send) ------------------------------------
            // The finished message text leaves our flow here, at the same
            // point where MSC's CP hands a message to its cipher library. Do
            // not move this line. Everything CP leaves to its library stays
            // on the far side: the enciphered-traffic header, its checksum
            // and compression. None of that is built here. Our own cipher
            // fills the slot; docs/cp-link.md, "The cipher hand-off".
            // ------------------------------------------------------------------
            std::string out;
            if (!codress::encipher(*core.cipher(), r.message, r.codress_key, codress_key, out, &e)) {
                refused.push_back(t + ": " + e);
                err = e;
                continue;
            }
            sent_text = out;
            id = core.submit_codress(t, r.prec, out, r.message, r.codress_key, &e);
        } else {
            id = core.submit(t, r.prec, r.message, &e, req["hold"] == "Y");
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
std::string send_block(Core& core, const std::string& to, const std::string& text, std::string* error,
                       bool held = false) {
    std::map<std::string, std::string> req = {{"hold", held ? "Y" : "N"},{"template", "FREE_FORM"},
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
        // The first error says why; a warning beside it doesn't.
        if (error && is && is->is_array())
            for (auto& i : is->arr) {
                const JsonValue* lv = i.get("level");
                const JsonValue* t = i.get("text");
                if (t && lv && lv->s == "error") {
                    *error += ": " + t->s;
                    break;
                }
            }
        return "";
    }
    return id->s;
}

// Station keys (station.h): this station's card and backup, other stations'
// cards, and key tapes sealed to them.
// Calls in a list written with spaces or commas, in capitals.
std::vector<std::string> call_list(const std::string& who) {
    std::vector<std::string> named;
    std::string cur;
    for (char c : who + " ") {
        if (c == ' ' || c == ',') {
            if (!cur.empty()) named.push_back(cur);
            cur.clear();
        } else {
            cur += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return named;
}

// The check value of a tape's first key, from the cipher in the slot when
// the tape is for it.
std::string first_key_check(Core& core, const keys::Tape& t) {
    if (t.keys.empty()) return "";
    auto slot = core.cipher();
    std::string c;
    if (slot->usable() && keys::cipher_id(slot->info().name) == t.cipher &&
        slot->key_check(t.keys.front().indicator, t.keys.front().secret, c, nullptr))
        return c;
    return t.cipher.empty() ? keys::key_check(t.keys.front().indicator, t.keys.front().secret) : "----";
}

std::string read_whole(const std::string& path, bool* ok) {
    std::ifstream f(path, std::ios::binary);
    *ok = static_cast<bool>(f);
    std::ostringstream ss;
    if (f) ss << f.rdbuf();
    return ss.str();
}

// handout_import: a member handout (station.h), from a file or a stored
// message. The member decides whose authority they accept (docs/crypto.md,
// "Choosing your keymasters"), so loading a handout names nobody a
// keymaster. Each keymaster it lists is:
//   accepted   already a keymaster here, with the same card (a station
//              restored from its backup, or one the member accepted before):
//              not asked again;
//   refused    the member refused this card before: its tapes aren't taken;
//   undecided  its card is held unchecked, and the reply asks the member to
//              check the fingerprint by voice or in person and then
//              keymaster_accept or keymaster_refuse it.
// The handout's sealed tape opens, and the other cards in it are held as
// checked, only once its issuer is accepted. Until then the handout is kept,
// and keymaster_accept opens it. Signed plain traffic works either way.
//
// packet: a handout packet folder (docs/cipher-slot.md): handout.txt, and,
// for a net that uses another cipher, cipher/ with its module. The module is
// installed only when the issuer is accepted, its SHA-256 matches the CIPHER
// line the keymaster signed into the handout, and it passes the slot's checks.
std::string handout_key(const station::Handout& h) {
    return h.by + " " + keys::time_text(h.issued) + " " + h.for_station + " " + h.tape;
}

void handout_reply(JsonWriter& w, Core& core, const station::Identity& me, const station::Handout& h,
                   const std::string& text, const std::string& packet, bool replace) {
    std::string issuer_fp = h.keymasters.empty() ? "" : h.keymasters[0].second;
    const std::string issuer_choice = core.keymaster_choice(h.by, issuer_fp);
    const bool issuer_ok = issuer_choice == "accepted";
    std::string err;
    // The net's cipher, when it isn't our own, from an accepted issuer only.
    std::string cipher_next;
    bool cipher_installed = false;
    if (!h.cipher.empty() && issuer_ok) {
        auto cur = core.cipher();
        bool have = cur->usable() && cur->info().name == h.cipher && cur->info().set_hash == h.cipher_hash;
        namespace fs = std::filesystem;
        std::error_code ec;
        if (have) {
        } else if (packet.empty() || !fs::exists(fs::path(packet) / "cipher" / "module.so", ec)) {
            cipher_next = "this net uses the " + h.cipher +
                          " cipher: load the whole packet folder (handout import FOLDER), which carries its module";
        } else if (core.cipher_dir().empty()) {
            cipher_next = "this station has no place to keep a cipher module";
        } else {
            std::string e;
            auto slot = cipher::install((fs::path(packet) / "cipher").string(), core.cipher_dir(), h.cipher_hash, &e);
            if (!slot) {
                cipher_next = "the net's cipher module was not installed: " + e;
            } else if (slot->info().name != h.cipher) {
                cipher::remove_installed(core.cipher_dir(), nullptr);
                core.set_cipher(cipher::Slot::builtin());
                cipher_next = "the packet's cipher module calls itself " + slot->info().name + ", but the handout names " +
                              h.cipher + ": not installed";
            } else {
                core.set_cipher(slot);
                cipher_installed = true;
                core.note("cipher module " + h.cipher + " " + slot->info().version + " installed from " + h.by +
                          "'s handout packet");
            }
        }
    }
    std::string source = "handout from " + h.by + " issued " + keys::time_text(h.issued);
    std::vector<std::string> refused;
    int loaded = 0;
    for (auto& ct : h.cards) {
        station::Card c;
        if (!station::parse_card(ct, c, nullptr)) continue;
        std::string fp = station::fingerprint(c);
        bool km = false;
        for (auto& k : h.keymasters) km = km || k.first == c.station;
        // A keymaster's card is trusted only once the member accepted it; a
        // member's card, once the issuer who vouches for it is accepted.
        bool trusted = km ? core.keymaster_choice(c.station, fp) == "accepted" : issuer_ok;
        std::string what, e;
        bool good = trusted ? core.add_trusted_card(ct, source, km, replace, c, what, &e)
                            : core.add_card(ct, source + " (not yet accepted)", replace, c, what, &e);
        if (!good && c.station == me.station) good = true, what = "own";
        if (good) {
            if (what != "own") ++loaded;
        } else {
            refused.push_back(e);
        }
    }
    // Ask, or keep, the member's choice of each keymaster.
    std::vector<std::pair<std::string, std::string>> undecided;
    w.begin_array("keymasters");
    for (auto& k : h.keymasters) w.field("", k.first);
    w.end_array();
    w.begin_array("keymaster_choices");
    for (auto& k : h.keymasters) {
        std::string ch = core.keymaster_choice(k.first, k.second);
        w.begin_object().field("station", k.first).field("fingerprint", k.second).field("choice", ch);
        if (ch == "undecided") {
            undecided.push_back(k);
            w.field("question", k.first + " " + k.second + " wants to be your keymaster. Accept?");
        }
        w.end_object();
    }
    w.end_array();
    int64_t now = core.keyring() ? core.keyring()->now() : static_cast<int64_t>(std::time(nullptr));
    w.field("net", h.net)
        .field("by", h.by)
        .field("issued", keys::time_text(h.issued))
        .field("cards", loaded)
        .field("stale", h.valid_until > 0 && h.valid_until <= now)
        .field("cipher", h.cipher.empty() ? std::string(cipher::kBuiltinName) : h.cipher)
        .field("cipher_installed", cipher_installed)
        .field("waiting_on_member", !undecided.empty());
    bool opened = false;
    std::string why;
    keys::ImportResult r;
    if (issuer_choice == "undecided") {
        why = h.by + " " + issuer_fp + " wants to be your keymaster. Check that fingerprint with " + h.by +
              " by voice or in person, then accept it (keymaster_accept) or refuse it (keymaster_refuse). " +
              (h.sealed.empty() ? "" : "Its tape opens once you accept. ") + "Signed plain traffic works meanwhile.";
    } else if (issuer_choice == "refused") {
        why = "you refused " + h.by + " as a keymaster, so this handout's tape was not taken. Signed plain traffic "
              "still works.";
    } else if (!cipher_next.empty()) {
        why = cipher_next;
    } else if (h.sealed.empty()) {
        why = "this handout has no tape: send your card to a keymaster (card_send) and read the fingerprint by voice";
    } else if (!core.keyring()) {
        why = "this station has no keyring";
    } else {
        std::string tape;
        station::SealedHead sh;
        if (!station::open_tape(h.sealed, me, [&](const std::string& c) { return core.card_for(c); }, tape, sh, &err)) {
            why = err.find("doesn't open with this station's key") != std::string::npos
                      ? "the tape here was sealed to an older key of this station: send your card to a keymaster "
                        "(card_send) for a new tape"
                      : err;
        } else {
            opened = core.keyring()->import_tape(tape, r, &err);
            crypto::wipe(tape);
            if (!opened) why = err;
            else
                core.note("key tape " + r.tape + " from a handout by " + h.by + " loaded: " + std::to_string(r.added) +
                          " key(s) added, " + std::to_string(r.expired) + " already ended");
        }
    }
    // Kept until the member decides about its issuer.
    std::string key = handout_key(h);
    if (issuer_choice == "undecided") {
        Store::PendingHandout p;
        p.key = key;
        p.issuer = h.by;
        p.text = text;
        p.packet = packet;
        p.added = now;
        core.put_pending_handout(p);
    } else {
        core.remove_pending_handout(key);
    }
    w.field("tape_opened", opened);
    if (opened) w.field("added", r.added).field("already", r.already).field("expired", r.expired);
    if (opened && r.added == 0) why = "every key on this tape has ended: ask a keymaster for a new tape";
    if (!why.empty()) w.field("next", why);
    w.begin_array("refused");
    for (auto& e : refused) w.field("", e);
    w.end_array();
    core.note("handout from " + h.by + " for " + h.net + " loaded: " + std::to_string(loaded) + " card(s), issuer " +
              issuer_choice);
}

std::string handout_import(Core& core, const station::Identity& me, std::map<std::string, std::string>& req) {
    std::string text = req["text"], err;
    const std::string packet = req["packet"];
    if (!packet.empty()) {
        bool ok = false;
        text = read_whole(packet + "/handout.txt", &ok);
        if (!ok) return error_reply("no handout.txt in the packet folder " + packet);
    }
    if (!req["id"].empty()) {
        auto m = core.get(req["id"]);
        if (!m) return error_reply("no message with that id");
        text = m->body;
    }
    if (!req["file"].empty()) {
        bool ok = false;
        text = read_whole(req["file"], &ok);
        if (!ok) return error_reply("can't read " + req["file"]);
    }
    station::Handout h;
    if (!station::parse_handout(text, h, &err)) return error_reply("handout not loaded: " + err);
    if (h.for_station != me.station)
        return error_reply("this handout is for " + h.for_station + ", not this station");
    // A different card already held for the issuer: stop before anything is kept.
    if (auto held = core.card_for(h.by); held && h.by != me.station && !h.keymasters.empty() &&
        station::fingerprint(*held) != h.keymasters[0].second && req["replace"] != "Y")
        return error_reply("handout not loaded: this station already holds a different card for " + h.by + " (" +
                           station::fingerprint(*held) + "); the handout's is " + h.keymasters[0].second +
                           ". Check it with " + h.by + " by voice before replacing");
    JsonWriter w;
    w.begin_object().field("ok", true);
    handout_reply(w, core, me, h, text, packet, req["replace"] == "Y");
    w.end_object();
    return w.str();
}

// After the member accepts a keymaster: every handout kept for it opens.
void reapply_pending(JsonWriter& w, Core& core, const station::Identity& me, const std::string& issuer) {
    w.begin_array("handouts");
    for (auto& p : core.pending_handouts()) {
        if (p.issuer != issuer) continue;
        station::Handout h;
        std::string err;
        if (!station::parse_handout(p.text, h, &err) || h.for_station != me.station) {
            core.remove_pending_handout(p.key);
            continue;
        }
        w.begin_object();
        handout_reply(w, core, me, h, p.text, p.packet, false);
        w.end_object();
    }
    w.end_array();
}

// keymaster_accept / keymaster_refuse / keymaster_list: the member's choice.
std::string keymaster_cmd(Core& core, const station::Identity& me, const std::string& cmd,
                          std::map<std::string, std::string>& req) {
    std::string err, call = req["station"];
    for (auto& c : call) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (cmd == "keymaster_accept") {
        if (req["fingerprint"].empty())
            return error_reply("give the fingerprint you read back with " + call + " by voice or in person");
        std::string how = req["how"].empty() ? "by voice" : req["how"];
        if (!core.accept_keymaster(call, req["fingerprint"], how, &err)) return error_reply(err);
        JsonWriter w;
        w.begin_object().field("ok", true).field("station", call).field("keymaster", true);
        reapply_pending(w, core, me, call);
        w.end_object();
        return w.str();
    }
    if (cmd == "keymaster_refuse") {
        if (!core.refuse_keymaster(call, &err)) return error_reply(err);
        int dropped = 0;
        for (auto& p : core.pending_handouts())
            if (p.issuer == call) {
                core.remove_pending_handout(p.key);
                ++dropped;
            }
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("station", call)
            .field("keymaster", false)
            .field("handouts_dropped", dropped)
            .field("next", "Signed plain traffic from " + call + " still checks. Its key tapes won't be taken.")
            .end_object();
        return w.str();
    }
    // keymaster_list: who the member accepted, refused, and who still waits.
    JsonWriter w;
    w.begin_object().field("ok", true);
    w.begin_array("accepted");
    for (auto& c : core.cards())
        if (c.keymaster)
            w.begin_object().field("station", c.station).field("fingerprint", c.fingerprint).field("source", c.source).end_object();
    w.end_array();
    w.begin_array("refused");
    for (auto& c : core.cards())
        if (core.keymaster_choice(c.station, c.fingerprint) == "refused")
            w.begin_object().field("station", c.station).field("fingerprint", c.fingerprint).end_object();
    w.end_array();
    std::set<std::string> seen;
    w.begin_array("undecided");
    for (auto& p : core.pending_handouts()) {
        station::Handout h;
        if (!station::parse_handout(p.text, h, nullptr)) continue;
        for (auto& k : h.keymasters)
            if (core.keymaster_choice(k.first, k.second) == "undecided" && seen.insert(k.first).second)
                w.begin_object()
                    .field("station", k.first)
                    .field("fingerprint", k.second)
                    .field("from_handout_by", h.by)
                    .field("question", k.first + " " + k.second + " wants to be your keymaster. Accept?")
                    .end_object();
    }
    w.end_array().end_object();
    return w.str();
}

// Startup bundles (bundle.h): the operating data a net needs, signed by the
// box that built it. bundle_make builds one from this station's book,
// channel designators, schedules and link formats. bundle_load loads one
// from text, a file, or a message received over the air (its parts are
// gathered by tag). bundle_send sends one, in parts, to each station named.
// A bundle changes no card, key or keymaster here. From a signer whose card
// this station doesn't hold checked, it loads only with accept=Y, the
// member's own yes, and is marked as from that signer.
constexpr size_t kBundlePart = 6000;  // text bytes a part, well under a message's 8192

// Parts of one long block (parts.h) that came in to this station: a sealed
// key tape or a startup bundle sent in numbered parts. A set is the parts
// with one kind and tag, signed by one station. Each part is its own signed
// message: a part whose signature is bad, or that was signed by a station
// other than the set's, is not taken. A set still incomplete after the hold
// time is dropped: its old parts are no longer counted.
struct PartSet {
    parts::Head h;
    std::string from;                   // the signer
    std::map<int, std::string> got;     // part number -> its lines
    std::map<int, std::string> ids;     // part number -> message id
    std::vector<int> missing;
    int64_t first = 0, last = 0;        // when the first and last parts came
    bool complete = false, dropped = false;
    bool direct = false;                // a part came addressed to this station
    int dropped_parts = 0, refused = 0;  // parts too old; parts not taken
    std::string text() const {
        std::string t;
        for (auto& [n, b] : got) t += b;
        return t;
    }
};

// A part's signer, when its signature can be taken: good, or from a station
// whose card isn't held here (a bundle carries its signer's card and is
// checked whole once complete). Empty when it can't.
std::string part_signer(Core& core, const Message& m) {
    auto sc = station::check_signature(m.body, [&](const std::string& c) { return core.card_for(c); });
    if (sc.state == "good") return sc.signer;
    if (sc.state == "unknown" && !core.card_for(sc.signer)) return sc.signer;
    return "";
}

std::vector<PartSet> part_sets(Core& core) {
    std::map<std::string, PartSet> sets;  // kind|tag|signer
    const int64_t now = now_utc();
    const int64_t hold = core.parts_hold_s();
    for (auto& m : core.list(5000)) {
        if (m.direction != "in" || m.state == "overheard") continue;
        parts::Head h;
        if (!parts::head(m.body, h)) continue;
        std::string from = part_signer(core, m);
        std::string key = h.kind + "|" + h.tag + "|" + (from.empty() ? "?" + m.src : from);
        PartSet& ps = sets[key];
        if (ps.h.of == 0) {
            ps.h = h;
            ps.from = from.empty() ? m.src : from;
        }
        if (from.empty() || h.of != ps.h.of) {
            ++ps.refused;
            continue;
        }
        // A part sent again: the newest copy counts.
        if (ps.ids.count(h.n)) {
            auto had = core.get(ps.ids[h.n]);
            if (had && had->created_at > m.created_at) continue;
        }
        ps.got[h.n] = parts::body(m.body);
        ps.ids[h.n] = m.id;
        if (m.dst != "ALL") ps.direct = true;
    }
    std::vector<PartSet> out;
    for (auto& [k, ps] : sets) {
        if (ps.got.empty()) continue;  // only refused parts
        // Parts sent again to the whole net reach stations whose block it
        // isn't: a set counts here only when part of it came to this station.
        if (!ps.direct) continue;
        std::map<int, int64_t> when;
        for (auto& [n, id] : ps.ids)
            if (auto m = core.get(id)) when[n] = m->created_at;
        auto span = [&] {
            ps.first = ps.last = 0;
            for (auto& [n, t] : when) {
                ps.first = ps.first ? std::min(ps.first, t) : t;
                ps.last = std::max(ps.last, t);
            }
        };
        span();
        ps.complete = static_cast<int>(ps.got.size()) == ps.h.of;
        if (hold > 0) {
            // A set waits at most the hold time. Parts that waited longer
            // before the set was whole (or until now, while it isn't) are
            // dropped; parts sent again start it afresh.
            auto drop_before = [&](int64_t until) {
                for (auto it = ps.got.begin(); it != ps.got.end();) {
                    if (until - when[it->first] > hold) {
                        ++ps.dropped_parts;
                        ps.ids.erase(it->first);
                        when.erase(it->first);
                        it = ps.got.erase(it);
                    } else {
                        ++it;
                    }
                }
                ps.complete = static_cast<int>(ps.got.size()) == ps.h.of;
            };
            if (ps.complete) drop_before(ps.last);
            if (!ps.complete) drop_before(now);
            ps.dropped = ps.got.empty();
            span();
        }
        for (int n = 1; n <= ps.h.of; ++n)
            if (!ps.got.count(n)) ps.missing.push_back(n);
        out.push_back(ps);
    }
    return out;
}

// The set a stored part belongs to.
std::optional<PartSet> part_set_of(Core& core, const Message& m) {
    parts::Head h;
    if (!parts::head(m.body, h)) return std::nullopt;
    std::string from = part_signer(core, m);
    if (from.empty()) return std::nullopt;
    for (auto& ps : part_sets(core))
        if (ps.h.kind == h.kind && ps.h.tag == h.tag && ps.from == from) return ps;
    return std::nullopt;
}

void write_set(JsonWriter& w, Core& core, const PartSet& ps) {
    const int64_t hold = core.parts_hold_s();
    w.begin_object()
        .field("kind", ps.h.kind == "TAPE" ? std::string("tape") : std::string("bundle"))
        .field("tag", ps.h.tag)
        .field("from", ps.from)
        .field("parts", ps.h.of)
        .field("have", static_cast<int>(ps.got.size()))
        .field("state", ps.complete ? std::string("complete") : ps.dropped ? std::string("dropped") : std::string("waiting"));
    w.begin_array("missing");
    for (int n : ps.missing) w.field("", n);
    w.end_array();
    if (!ps.ids.empty()) w.field("id", ps.ids.begin()->second);
    if (!ps.complete && !ps.dropped && hold > 0)
        w.field("drops_in_s", static_cast<long long>(std::max<int64_t>(0, ps.first + hold - now_utc())));
    if (ps.dropped_parts) w.field("dropped_parts", ps.dropped_parts);
    if (ps.refused) w.field("refused_parts", ps.refused);
    w.end_object();
}

// What a reply says when a set isn't all here.
std::string incomplete_reply(Core& core, const PartSet& ps, const std::string& what) {
    JsonWriter w;
    w.begin_object().field("ok", false);
    std::string have = std::to_string(ps.got.size()) + " of " + std::to_string(ps.h.of);
    if (ps.dropped)
        w.field("error", what + " not opened: its parts waited past the hold time and were dropped. Ask " + ps.from +
                             " to send it again.");
    else
        w.field("error", what + " not opened yet: " + have + " parts are here. Ask " + ps.from +
                             " for the rest (form PARTS_ASK, or parts_request).");
    w.field("tag", ps.h.tag).field("from", ps.from).field("parts", ps.h.of).field("have", static_cast<int>(ps.got.size()));
    w.begin_array("missing_parts");
    for (int n : ps.missing) w.field("", n);
    w.end_array();
    if (!ps.ids.empty()) w.field("id", ps.ids.begin()->second);
    w.end_object();
    (void)core;
    return w.str();
}

// Sends a block to one station, in parts when it is long. The ids sent, or
// empty with *error.
std::vector<std::string> send_in_parts(Core& core, const std::string& to, const std::string& kind,
                                       const std::string& tag, const std::string& text, std::string* error,
                                       int* parts_n = nullptr) {
    std::vector<std::string> ids;
    std::vector<std::string> pieces;
    if (text.size() <= kBundlePart && kind == "TAPE")
        pieces.push_back(text);
    else
        pieces = parts::split(kind, tag, text, kBundlePart);
    if (parts_n) *parts_n = static_cast<int>(pieces.size());
    if (pieces.size() > 99) {
        if (error) *error = "too long to send in parts (more than 99): put it on a stick";
        return {};
    }
    for (auto& p : pieces) {
        std::string id = send_block(core, to, p, error);
        if (id.empty()) return {};
        ids.push_back(id);
    }
    return ids;
}

// ---- The resend queue (docs/crypto.md, Parts) ----
//
// Requests for missing parts are pooled: each part asked for once per
// request is a row (Store::PartAsk). Rows asking for the same part of the
// same block are one send: to the station, or to ALL when several stations
// asked for a block that went to each of them. Nothing goes out until the
// operator sends the queue (parts_queue_send), now or at the next net slot,
// and then at most parts_resend_per_hour parts an hour.

// The part's text as first sent to station (header to END line), or empty.
std::string part_text_sent(Core& core, const std::string& station, const std::string& tag, int n,
                           const std::vector<Message>& msgs) {
    for (auto& m : msgs) {
        parts::Head h;
        if (m.direction != "out" || m.dst != station || !parts::head(m.body, h) || h.tag != tag || h.n != n) continue;
        std::string t = parts::cut(m.body);
        if (!t.empty()) return t;
    }
    return "";
}

// Takes requests that came in into the queue, once each.
void sync_asks(Core& core, const std::vector<Message>& msgs) {
    std::set<std::string> known;
    std::map<std::string, std::set<std::string>> served;  // station|tag|part -> resends made
    for (auto& a : core.part_asks()) {
        known.insert(a.request_id);
        if ((a.state == "sent" || a.state == "held") && !a.sent_id.empty())
            served[a.station + "|" + a.tag + "|" + std::to_string(a.part)].insert(a.sent_id);
    }
    // Oldest first, so a later repeat is weighed against what was sent before it.
    for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
        const Message& m = *it;
        if (m.direction != "in" || m.state == "overheard" || known.count(m.id)) continue;
        std::string kind, tag;
        std::vector<int> missing;
        if (!parts::read_request(m.body, kind, tag, missing)) continue;
        auto sc = station::check_signature(m.body, [&](const std::string& c) { return core.card_for(c); });
        std::string from = sc.state == "good" ? sc.signer : m.src;
        for (int n : missing) {
            Store::PartAsk a;
            a.request_id = m.id;
            a.part = n;
            a.station = from;
            a.kind = kind;
            a.tag = tag;
            a.asked = m.created_at;
            if (sc.state != "good")
                a.state = "unchecked";
            else if (part_text_sent(core, from, tag, n, msgs).empty())
                a.state = "not_ours";
            else if (core.parts_flag_after() > 0 &&
                     static_cast<int>(served[from + "|" + tag + "|" + std::to_string(n)].size()) >= core.parts_flag_after())
                a.state = "flagged";
            else
                a.state = "waiting";
            core.put_part_ask(a);
        }
        if (sc.state == "good")
            core.note(from + " asks for " + std::to_string(missing.size()) + " part(s) of " + tag +
                      ": in the resend queue (parts_queue)");
    }
}

struct ResendGroup {
    std::string kind, tag;
    int part = 0;
    std::set<std::string> stations;
    std::vector<std::pair<std::string, int>> rows;  // request id, part
    int64_t first_asked = 0;
    std::string to() const { return stations.size() == 1 ? *stations.begin() : std::string("ALL"); }
};

std::vector<ResendGroup> resend_groups(const std::vector<Store::PartAsk>& asks) {
    std::map<std::string, ResendGroup> g;
    for (auto& a : asks) {
        if (a.state != "waiting") continue;
        auto& r = g[a.kind + "|" + a.tag + "|" + std::to_string(1000 + a.part)];
        r.kind = a.kind;
        r.tag = a.tag;
        r.part = a.part;
        r.stations.insert(a.station);
        r.rows.push_back({a.request_id, a.part});
        r.first_asked = r.first_asked ? std::min(r.first_asked, a.asked) : a.asked;
    }
    std::vector<ResendGroup> out;
    for (auto& [k, r] : g) out.push_back(r);
    std::stable_sort(out.begin(), out.end(),
                     [](const ResendGroup& a, const ResendGroup& b) { return a.first_asked < b.first_asked; });
    return out;
}

// Resends that go out within the hour before t (each send counted once).
int resends_near(const std::vector<Store::PartAsk>& asks, int64_t t, int64_t* oldest) {
    std::map<std::string, int64_t> sends;
    for (auto& a : asks)
        if ((a.state == "sent" || a.state == "held") && !a.sent_id.empty() && a.at > t - 3600 && a.at <= t)
            sends[a.sent_id] = a.at;
    if (oldest) {
        *oldest = 0;
        for (auto& [id, at] : sends) *oldest = *oldest ? std::min(*oldest, at) : at;
    }
    return static_cast<int>(sends.size());
}

std::string queue_summary(const std::vector<ResendGroup>& gs) {
    if (gs.empty()) return "Resend queue: empty";
    std::set<std::string> who;
    std::map<std::string, std::vector<int>> by_tag;
    std::map<std::string, std::string> kind_of;
    for (auto& g : gs) {
        who.insert(g.stations.begin(), g.stations.end());
        by_tag[g.tag].push_back(g.part);
        kind_of[g.tag] = g.kind == "TAPE" ? "tape" : "bundle";
    }
    std::string list;
    for (auto& [tag, ps] : by_tag) {
        std::string nums;
        for (size_t i = 0; i < ps.size(); ++i)
            nums += (i == 0 ? "" : i + 1 == ps.size() ? " and " : ", ") + std::to_string(ps[i]);
        list += (list.empty() ? "" : "; ") + kind_of[tag] + " " + tag + (ps.size() == 1 ? " part " : " parts ") + nums;
    }
    return "Resend queue: " + std::to_string(gs.size()) + (gs.size() == 1 ? " send" : " sends") + " for " +
           std::to_string(who.size()) + (who.size() == 1 ? " station" : " stations") + " (" + list + ")";
}

// The RESENDS screen's rows: one per request a station sent, with the parts
// it asked for and where they stand. state, first that applies: WAIT (can
// go now), HELD-EMCON, HELD-CAP (past this hour's room), FLAGGED (asked again
// after two resends: the operator's call), HELD-NET (sent into the outbox,
// held for a net slot), SENT, DROPPED. Requests that weren't signed, or ask
// for parts this station never sent, aren't rows (unchecked, not_ours).
void write_resend_rows(JsonWriter& w, Core& core, const std::vector<Store::PartAsk>& asks,
                       const std::vector<ResendGroup>& groups, int room, const std::vector<Message>& msgs,
                       int64_t now, int limit) {
    std::set<std::string> in_room;  // request|part inside this hour's room
    for (size_t i = 0; i < groups.size(); ++i)
        if (room < 0 || static_cast<int>(i) < room)
            for (auto& [rid, n] : groups[i].rows) in_room.insert(rid + "|" + std::to_string(n));
    struct Row {
        std::string request, station, kind, tag;
        std::vector<int> parts;
        std::set<std::string> states;
        bool any_in_room = false;
        int64_t asked = 0, at = 0;
    };
    std::map<std::string, Row> by;
    std::vector<std::string> order;
    for (auto& a : asks) {
        if (a.state == "unchecked" || a.state == "not_ours") continue;
        auto [it, fresh] = by.try_emplace(a.request_id);
        if (fresh) order.push_back(a.request_id);
        Row& r = it->second;
        r.request = a.request_id;
        r.station = a.station;
        r.kind = a.kind;
        r.tag = a.tag;
        r.asked = a.asked;
        r.at = std::max(r.at, a.at);
        r.parts.push_back(a.part);
        r.states.insert(a.state);
        if (a.state == "waiting" && in_room.count(a.request_id + "|" + std::to_string(a.part))) r.any_in_room = true;
    }
    auto label = [&](const Row& r) -> std::string {
        if (r.states.count("waiting")) return core.emcon() ? "HELD-EMCON" : r.any_in_room ? "WAIT" : "HELD-CAP";
        if (r.states.count("flagged")) return "FLAGGED";
        if (r.states.count("held")) return "HELD-NET";
        if (r.states.count("sent")) return "SENT";
        return "DROPPED";
    };
    // Open rows first, oldest ask first; then finished ones, latest first.
    std::vector<const Row*> open, done;
    for (auto& id : order) {
        const Row& r = by[id];
        std::string l = label(r);
        (l == "SENT" || l == "DROPPED" ? done : open).push_back(&r);
    }
    std::stable_sort(done.begin(), done.end(), [](const Row* a, const Row* b) { return a->at > b->at; });
    open.insert(open.end(), done.begin(), done.end());
    w.begin_array("rows");
    int shown = 0;
    for (const Row* r : open) {
        if (limit > 0 && shown >= limit) break;
        ++shown;
        // The message it's about: the first part this station was sent of that block.
        std::string message;
        int first = 1 << 30;
        for (auto& m : msgs) {
            parts::Head h;
            if (m.direction == "out" && m.dst == r->station && parts::head(m.body, h) && h.tag == r->tag && h.n < first) {
                first = h.n;
                message = m.id;
            }
        }
        std::string nums;
        for (int n : r->parts) nums += (nums.empty() ? "" : " ") + std::to_string(n);
        const std::string kind = r->kind == "TAPE" ? "tape" : "bundle";
        w.begin_object().field("request", r->request).field("message", message).field("kind", kind)
            .field("tag", r->tag).field("title", kind + " " + r->tag).field("station", r->station)
            .field("parts", nums).field("asked", keys::time_text(r->asked))
            .field("age_s", static_cast<long long>(std::max<int64_t>(0, now - r->asked)))
            .field("state", label(*r)).end_object();
    }
    w.end_array();
}

std::optional<std::string> parts_cmd(Core& core, const station::Identity& me, const std::string& cmd,
                                     std::map<std::string, std::string>& req) {
    if (cmd == "parts_list") {
        JsonWriter w;
        w.begin_object().field("ok", true).field("hold_s", static_cast<long long>(core.parts_hold_s())).begin_array("sets");
        for (auto& ps : part_sets(core))
            if (req["all"] == "Y" || !ps.complete) write_set(w, core, ps);
        w.end_array().begin_array("requests");
        auto msgs = core.list(5000);
        sync_asks(core, msgs);
        std::map<std::string, std::vector<Store::PartAsk>> by_req;
        std::vector<std::string> order;
        for (auto& a : core.part_asks()) {
            if (!by_req.count(a.request_id)) order.push_back(a.request_id);
            by_req[a.request_id].push_back(a);
        }
        for (auto& id : order) {
            auto& rows = by_req[id];
            bool open = false;
            for (auto& a : rows) open = open || a.state == "waiting" || a.state == "flagged" || a.state == "held";
            if (!open && req["all"] != "Y") continue;
            w.begin_object().field("id", id).field("from", rows[0].station)
                .field("kind", rows[0].kind == "TAPE" ? std::string("tape") : std::string("bundle")).field("tag", rows[0].tag);
            w.begin_array("missing");
            for (auto& a : rows) w.field("", a.part);
            w.end_array().begin_array("states");
            for (auto& a : rows) w.field("", a.state);
            w.end_array().end_object();
        }
        w.end_array().end_object();
        return w.str();
    }
    // parts_queue_drop: the operator drops one station's request (RESENDS d):
    // its parts still waiting, or flagged, won't be sent. part= drops one.
    if (cmd == "parts_queue_drop") {
        const std::string rid = req["request"];
        if (rid.empty()) return error_reply("give the request to drop (parts_queue rows[].request)");
        int dropped = 0;
        std::string station;
        for (auto& a : core.part_asks()) {
            if (a.request_id != rid) continue;
            station = a.station;
            if (!req["part"].empty() && std::to_string(a.part) != req["part"]) continue;
            if (a.state != "waiting" && a.state != "flagged" && a.state != "unchecked" && a.state != "not_ours")
                continue;
            core.set_part_ask(a.request_id, a.part, "dropped", "", now_utc());
            ++dropped;
        }
        if (station.empty()) return error_reply("no request " + rid + " in the resend queue");
        if (dropped == 0) return error_reply("nothing left to drop in that request: its parts were sent or dropped");
        core.note("dropped " + std::to_string(dropped) + " part(s) " + station + " asked for (request " + rid +
                  "): they won't be resent");
        JsonWriter w;
        w.begin_object().field("ok", true).field("request", rid).field("station", station).field("dropped", dropped)
            .end_object();
        return w.str();
    }
    if (cmd == "parts_queue" || cmd == "parts_queue_send") {
        auto msgs = core.list(5000);
        sync_asks(core, msgs);
        auto asks = core.part_asks();
        // RESENDS s: send one request's parts. Its flagged parts go too (the
        // operator chose to), pooled with anyone else asking for the same part.
        const std::string only = cmd == "parts_queue_send" ? req["request"] : std::string();
        if (!only.empty()) {
            bool found = false, open = false;
            for (auto& a : asks)
                if (a.request_id == only) {
                    found = true;
                    if (a.state == "flagged") a.state = "waiting";
                    open = open || a.state == "waiting";
                }
            if (!found) return error_reply("no request " + only + " in the resend queue");
            if (!open) return error_reply("nothing left to send in that request: its parts were sent or dropped");
        }
        auto groups = resend_groups(asks);
        if (!only.empty()) {
            std::vector<ResendGroup> keep;
            for (auto& g : groups)
                for (auto& [rid, n] : g.rows)
                    if (rid == only) {
                        keep.push_back(g);
                        break;
                    }
            groups = keep;
        }
        const int cap = core.parts_resend_per_hour();
        const int64_t now = now_utc();
        auto write_queue = [&](JsonWriter& w, const std::vector<ResendGroup>& gs) {
            w.field("summary", queue_summary(gs));
            std::set<std::string> who;
            for (auto& g : gs) who.insert(g.stations.begin(), g.stations.end());
            w.field("sends", static_cast<int>(gs.size())).field("stations", static_cast<int>(who.size()));
            w.begin_array("items");
            for (auto& g : gs) {
                w.begin_object().field("kind", g.kind == "TAPE" ? std::string("tape") : std::string("bundle"))
                    .field("tag", g.tag).field("part", g.part).field("to", g.to());
                w.begin_array("asked_by");
                for (auto& st : g.stations) w.field("", st);
                w.end_array().end_object();
            }
            w.end_array().begin_array("flagged");
            for (auto& a : core.part_asks())
                if (a.state == "flagged")
                    w.begin_object().field("station", a.station).field("tag", a.tag).field("part", a.part)
                        .field("request", a.request_id).end_object();
            w.end_array().begin_array("held");
            std::set<std::string> seen;
            for (auto& a : core.part_asks())
                if (a.state == "held" && seen.insert(a.sent_id).second)
                    w.begin_object().field("id", a.sent_id).field("tag", a.tag).field("part", a.part)
                        .field("goes_at", keys::time_text(a.at)).end_object();
            w.end_array();
            int unchecked = 0, not_ours = 0;
            for (auto& a : core.part_asks()) {
                unchecked += a.state == "unchecked";
                not_ours += a.state == "not_ours";
            }
            w.field("unchecked", unchecked).field("not_ours", not_ours);
        };
        if (cmd == "parts_queue") {
            int64_t oldest = 0;
            int used = resends_near(asks, now, &oldest);
            JsonWriter w;
            w.begin_object().field("ok", true);
            write_queue(w, groups);
            const int room = cap > 0 ? std::max(0, cap - used) : -1;
            w.field("per_hour", cap).field("sent_last_hour", used).field("room", room);
            w.field("emcon", core.emcon());
            int lim = 100;
            if (!req["limit"].empty()) lim = std::atoi(req["limit"].c_str());
            write_resend_rows(w, core, asks, groups, room, msgs, now, lim);
            w.end_object();
            return w.str();
        }
        // parts_queue_send: the operator's one press. when=NOW (default) or
        // NET: held for the next net slot (net= a schedule, else the soonest).
        std::string when = req["when"];
        for (auto& c : when) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (when.empty()) when = "NOW";
        if (when != "NOW" && when != "NET") return error_reply("when is NOW or NET");
        if (groups.empty()) return error_reply("the resend queue is empty");
        int64_t go_at = now;
        std::string slot;
        if (when == "NET") {
            Ops* ops = core.ops();
            std::optional<NetSitting> sit;
            if (ops && !req["net"].empty()) sit = ops->sched_sitting(req["net"]);
            else if (ops) {
                auto up = ops->sched_upcoming(1);
                if (!up.empty()) sit = up[0];
            }
            if (!sit) return error_reply(req["net"].empty() ? "no net is scheduled: send now, or add a net schedule"
                                                            : "no net schedule named " + req["net"]);
            go_at = std::max(now, sit->start);
            slot = sit->sched.name;
        } else if (core.emcon()) {
            return error_reply("EMCON is on, so nothing was sent. Send at the next net slot (when=NET), or when EMCON is lifted.");
        }
        int64_t oldest = 0;
        int used = resends_near(asks, go_at, &oldest);
        int room = cap > 0 ? std::max(0, cap - used) : static_cast<int>(groups.size());
        const bool held = go_at > now;
        JsonWriter w;
        w.begin_object().field("ok", true).field("when", when);
        if (!only.empty()) w.field("request", only);
        if (!slot.empty()) w.field("net", slot);
        w.field("goes_at", keys::time_text(go_at)).begin_array("sent");
        std::vector<ResendGroup> left;
        std::vector<std::string> failed;
        int done = 0;
        std::set<std::string> who;
        for (auto& g : groups) {
            if (done >= room) {
                left.push_back(g);
                continue;
            }
            std::string text;
            for (auto& st : g.stations)
                if (text.empty()) text = part_text_sent(core, st, g.tag, g.part, msgs);
            std::string err, id = text.empty() ? std::string() : send_block(core, g.to(), text, &err, held);
            if (id.empty()) {
                failed.push_back(g.tag + " part " + std::to_string(g.part) + ": " + (text.empty() ? "not found" : err));
                left.push_back(g);
                continue;
            }
            ++done;
            for (auto& [rid, n] : g.rows) core.set_part_ask(rid, n, held ? "held" : "sent", id, go_at);
            who.insert(g.stations.begin(), g.stations.end());
            w.begin_object().field("id", id).field("tag", g.tag).field("part", g.part).field("to", g.to())
                .field("stations", static_cast<int>(g.stations.size())).end_object();
        }
        w.end_array();
        w.field("sends", done).field("stations", static_cast<int>(who.size())).field("held", held);
        w.field("left_for_cap", static_cast<int>(left.size()) - static_cast<int>(failed.size()));
        if (!left.empty() && cap > 0 && room == done)
            w.field("room_at", keys::time_text((oldest ? oldest : go_at) + 3600));
        w.begin_array("failed");
        for (auto& f : failed) w.field("", f);
        w.end_array();
        w.field("left", queue_summary(left));
        w.end_object();
        core.note(std::to_string(done) + " part(s) from the resend queue " +
                  (held ? "held for the net slot at " + keys::time_text(go_at) : std::string("sent")) + " for " +
                  std::to_string(who.size()) + " station(s)");
        return w.str();
    }
    if (cmd == "parts_request") {
        // The receiver asks the sender for the parts it lacks, by number.
        auto m = core.get(req["id"]);
        if (!m) return error_reply("give the id of a part that came in (parts_list lists them)");
        auto ps = part_set_of(core, *m);
        if (!ps) return error_reply("that message isn't a part this station can take");
        if (ps->complete) return error_reply("every part is here: open it (key_open or bundle_load)");
        std::vector<int> missing = ps->missing;
        std::string err;
        std::string id = send_block(core, ps->from, parts::request_text(ps->h.kind, ps->h.tag, missing), &err);
        if (id.empty()) return error_reply("the request wasn't sent: " + err);
        core.note("asked " + ps->from + " for parts of " + ps->h.kind + " " + ps->h.tag);
        JsonWriter w;
        w.begin_object().field("ok", true).field("to", ps->from).field("tag", ps->h.tag).field("id", id);
        w.begin_array("asked");
        for (int n : missing) w.field("", n);
        w.end_array().end_object();
        return w.str();
    }
    // parts_resend: sends parts again, as a station asked (id of its request),
    // or as named (to, tag, parts). Only to a station they were sent to.
    std::string to = req["to"], tag = req["tag"];
    std::vector<int> want;
    if (!req["id"].empty()) {
        auto m = core.get(req["id"]);
        std::string kind;
        if (!m || m->direction != "in" || !parts::read_request(m->body, kind, tag, want))
            return error_reply("that message isn't a request for parts");
        auto sc = station::check_signature(m->body, [&](const std::string& c) { return core.card_for(c); });
        if (sc.state != "good") return error_reply("the request's signature can't be checked here, so nothing was sent");
        to = sc.signer;
    } else {
        for (auto& c : to) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        for (auto& c : tag) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::istringstream in(req["parts"]);
        std::string tok;
        while (std::getline(in, tok, ' '))
            for (auto& x : call_list(tok))
                if (!x.empty() && std::all_of(x.begin(), x.end(), ::isdigit)) want.push_back(std::stoi(x));
        if (to.empty() || tag.empty() || want.empty())
            return error_reply("give the request's id, or to, tag and the part numbers (parts=\"2 4\")");
    }
    std::map<int, std::string> sent_before;
    for (auto& m : core.list(5000)) {
        parts::Head h;
        if (m.direction != "out" || m.dst != to || !parts::head(m.body, h) || h.tag != tag) continue;
        if (!sent_before.count(h.n)) sent_before[h.n] = parts::cut(m.body);
    }
    if (sent_before.empty()) return error_reply("no parts with tag " + tag + " were sent to " + to + " from here");
    JsonWriter w;
    w.begin_object().field("ok", true).field("to", to).field("tag", tag).begin_array("resent");
    std::vector<int> not_found;
    std::vector<std::string> failed;
    std::sort(want.begin(), want.end());
    want.erase(std::unique(want.begin(), want.end()), want.end());
    for (int n : want) {
        auto it = sent_before.find(n);
        if (it == sent_before.end() || it->second.empty()) {
            not_found.push_back(n);
            continue;
        }
        std::string err, id = send_block(core, to, it->second, &err);
        if (id.empty()) {
            failed.push_back(std::to_string(n) + ": " + err);
            continue;
        }
        w.begin_object().field("part", n).field("id", id).end_object();
        // Asks from that station for this part are answered (flagged ones too).
        for (auto& a : core.part_asks())
            if (a.station == to && a.tag == tag && a.part == n && (a.state == "waiting" || a.state == "flagged"))
                core.set_part_ask(a.request_id, a.part, "sent", id, now_utc());
    }
    w.end_array().begin_array("not_found");
    for (int n : not_found) w.field("", n);
    w.end_array().begin_array("failed");
    for (auto& f : failed) w.field("", f);
    w.end_array().end_object();
    core.note("parts of " + tag + " sent again to " + to);
    (void)me;
    return w.str();
}


std::string bundle_cmd(Core& core, const station::Identity& me, const std::string& cmd,
                       std::map<std::string, std::string>& req) {
    Ops* ops = core.ops();
    if (!ops) return error_reply("this station has no station book");
    std::string err;
    // A bundle from what this box holds. For a member (`member`), the
    // frequencies go sealed to its card, and only to a card checked here by
    // voice: sealing to an unchecked card could hand them to whoever made it.
    // *freqs says what happened: "sealed", "no_card", "card_not_checked",
    // "none_held" (the plan has none), or "not_sealed" (no member named).
    auto make = [&](std::vector<std::string>* changed, const std::string& member, std::string* freqs, int* sealed_n) {
        bundle::Bundle b = bundle::collect(*ops, core.link_formats(), req["label"], changed);
        b.forms = forms::Library(forms_dir(core)).changed();
        b.made = ops->now();
        std::string state = "not_sealed";
        std::string text;
        if (member.empty()) {
            text = bundle::format(b, me);
        } else {
            auto card = member == me.station ? std::optional<station::Card>(station::card_of(me)) : core.card_for(member);
            bool checked = member == me.station || core.card_verified(member);
            if (card && checked && !b.freqs.empty()) {
                state = "sealed";
                if (sealed_n) *sealed_n = static_cast<int>(b.freqs.size());
                text = bundle::format(b, me, &*card);
            } else {
                state = !card ? "no_card" : !checked ? "card_not_checked" : "none_held";
                b.for_station = member;
                text = bundle::format(b, me);
            }
        }
        for (auto& f : b.freqs) f.second = 0;
        if (freqs) *freqs = state;
        return text;
    };
    // The bundle's text: given, from a file, or (bundle_make/send) built here.
    auto given = [&](std::string& text) -> std::string {
        if (!req["file"].empty()) {
            bool ok = false;
            text = read_whole(req["file"], &ok);
            if (!ok) return "can't read " + req["file"];
        } else if (!req["text"].empty()) {
            text = req["text"];
        }
        return "";
    };
    if (cmd == "bundle_make") {
        std::vector<std::string> changed;
        std::string member = req["for"];
        for (auto& c : member) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::string freqs;
        int sealed_n = 0;
        std::string text = make(&changed, member, &freqs, &sealed_n);
        bundle::Bundle b;
        if (!bundle::parse(text, b, &err)) return error_reply("the bundle didn't check after it was made: " + err);
        if (!req["out"].empty()) {
            std::ofstream f(req["out"], std::ios::binary | std::ios::trunc);
            f << text;
            if (!f) return error_reply("can't write " + req["out"]);
        }
        core.note("startup bundle " + b.tag + " made: " + std::to_string(b.book.size()) + " book entries, " +
                  std::to_string(b.channels.size()) + " channels, " + std::to_string(b.schedules.size()) + " schedules");
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("tag", b.tag)
            .field("by", b.by)
            .field("fingerprint", b.signer_fp)
            .field("made", keys::time_text(b.made))
            .field("book", static_cast<int>(b.book.size()))
            .field("channels", static_cast<int>(b.channels.size()))
            .field("schedules", static_cast<int>(b.schedules.size()))
            .field("links", static_cast<int>(b.links.size()))
            .field("forms", static_cast<int>(b.forms.size()))
            .field("bytes", static_cast<int>(text.size()))
            .field("parts", static_cast<int>(bundle::split(text, kBundlePart).size()))
            .field("for", b.for_station)
            .field("frequencies", freqs)
            .field("frequencies_sealed", sealed_n)
            .field("text", text);
        if (!req["out"].empty()) w.field("written", req["out"]);
        if (freqs == "no_card" || freqs == "card_not_checked")
            w.field("next", "This copy carries no frequencies: " + member +
                                (freqs == "no_card" ? "'s card isn't held here." : "'s card isn't checked by voice yet.") +
                                " Get its card and check it, then make the bundle again.");
        w.begin_array("changed");
        for (auto& c : changed) w.field("", c);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "bundle_send") {
        // A bundle given as text or file goes as it is. Otherwise each
        // station gets its own copy, with the frequencies sealed to its card.
        std::string given_text;
        std::string e = given(given_text);
        if (!e.empty()) return error_reply(e);
        bundle::Bundle b;
        if (!given_text.empty() && !bundle::parse(given_text, b, &err)) return error_reply("bundle not sent: " + err);
        auto to = call_list(req["to"]);
        if (to.empty()) return error_reply("name the stations to send it to (to=\"STA-B STA-C\")");
        JsonWriter w;
        w.begin_object().field("ok", true).field("by", me.station);
        if (!given_text.empty()) w.field("tag", b.tag);
        w.begin_array("sent");
        std::vector<std::string> failed;
        int total_parts = 0;
        for (auto& st : to) {
            std::string text = given_text, freqs = "as_given";
            if (text.empty()) text = make(nullptr, st, &freqs, nullptr);
            bundle::Bundle mine;
            if (!bundle::parse(text, mine, &err)) {
                failed.push_back(st + ": " + err);
                continue;
            }
            auto parts = bundle::split(text, kBundlePart);  // in PART n OF m form (parts.h)
            if (parts.size() > 99) {
                failed.push_back(st + ": too long to send in parts (more than 99): put it on a stick");
                continue;
            }
            total_parts += static_cast<int>(parts.size());
            w.begin_object()
                .field("station", st)
                .field("tag", mine.tag)
                .field("parts", static_cast<int>(parts.size()))
                .field("frequencies", freqs);
            w.begin_array("ids");
            for (auto& p : parts) {
                std::string id = send_block(core, st, p, &err);
                if (id.empty()) {
                    failed.push_back(st + ": " + err);
                    break;
                }
                w.field("", id);
            }
            w.end_array().end_object();
        }
        w.end_array();
        w.field("parts", total_parts);
        w.begin_array("failed");
        for (auto& f : failed) w.field("", f);
        w.end_array().end_object();
        core.note("startup bundle by " + me.station + " sent to " + req["to"] + " in " + std::to_string(total_parts) +
                  " part(s)");
        return w.str();
    }
    // bundle_load
    std::string text;
    std::string e = given(text);
    if (!e.empty()) return error_reply(e);
    if (!req["id"].empty()) {
        auto m = core.get(req["id"]);
        if (!m) return error_reply("no message with that id");
        text = m->body;
        parts::Head ph;
        if (parts::head(text, ph) && ph.kind == "BUNDLE") {
            // Every part with this tag, from the same signer; opened only when all are here.
            auto ps = part_set_of(core, *m);
            if (!ps) return error_reply("that part's signature can't be taken here: it is bad or from another station");
            if (!ps->complete) {
                JsonWriter w;
                w.begin_object().field("ok", true).field("loaded", false).field("tag", ps->h.tag).field("from", ps->from);
                w.field("parts", ps->h.of).field("have", static_cast<int>(ps->got.size())).field("dropped", ps->dropped);
                w.begin_array("missing_parts");
                for (int k : ps->missing) w.field("", k);
                w.end_array();
                w.field("next", ps->dropped ? "Its parts waited past the hold time and were dropped. Ask " + ps->from +
                                                  " to send the bundle again."
                                            : "Not every part has arrived. Ask " + ps->from +
                                                  " for the missing ones (form PARTS_ASK, or parts_request).");
                w.end_object();
                return w.str();
            }
            text = ps->text();
        }
    }
    if (text.empty()) return error_reply("give the bundle as text, file or the id of a message that carries it");
    bundle::Bundle b;
    if (!bundle::parse(text, b, &err)) return error_reply("bundle not loaded: " + err);
    // Who signed it, as this station knows them.
    std::string signer;
    if (b.by == me.station) {
        signer = station::fingerprint(station::card_of(me)) == b.signer_fp ? "own" : "different";
    } else if (auto held = core.card_for(b.by)) {
        signer = station::fingerprint(*held) != b.signer_fp ? "different"
                 : core.card_verified(b.by)                ? "checked"
                                                           : "unchecked";
    } else {
        signer = "unknown";
    }
    bool trusted = signer == "own" || signer == "checked";
    auto summary = [&](JsonWriter& w) {
        w.field("tag", b.tag)
            .field("by", b.by)
            .field("fingerprint", b.signer_fp)
            .field("signer", signer)
            .field("label", b.label)
            .field("made", keys::time_text(b.made))
            .field("book", static_cast<int>(b.book.size()))
            .field("channels", static_cast<int>(b.channels.size()))
            .field("schedules", static_cast<int>(b.schedules.size()))
            .field("links", static_cast<int>(b.links.size()))
            .field("forms", static_cast<int>(b.forms.size()));
    };
    if (!trusted && req["accept"] != "Y") {
        std::string what = std::to_string(b.book.size()) + " book entries, " + std::to_string(b.channels.size()) +
                           " channels, " + std::to_string(b.schedules.size()) + " net schedules";
        std::string q = b.by + " " + b.signer_fp + " signed this startup bundle (" + what + "). Load it?";
        if (signer == "different")
            q = "This bundle is signed by a card for " + b.by + " (" + b.signer_fp +
                ") that is NOT the one this station holds for " + b.by + ". Load it anyway?";
        JsonWriter w;
        w.begin_object().field("ok", true).field("loaded", false).field("needs_yes", true);
        summary(w);
        w.field("question", q)
            .field("next", "Check the fingerprint with " + b.by +
                               " by voice or in person. Load it with accept=Y if you choose to. Nothing has changed.")
            .end_object();
        return w.str();
    }
    std::string source = "bundle from " + b.by + " " + b.signer_fp + " made " + keys::time_text(b.made) +
                         (trusted ? "" : " (signer not checked: loaded on the member's yes)");
    bundle::Applied a = bundle::apply(b, *ops, source);
    // The frequencies, when this copy was sealed to this station.
    std::string freq_state = "none";
    bundle::FreqApplied fa;
    if (!b.sealed_freqs.empty()) {
        if (b.for_station != me.station) {
            freq_state = "sealed_to_other";
        } else {
            std::vector<std::pair<std::string, int64_t>> fs;
            std::string ferr;
            if (bundle::open_freqs(b, me, fs, &ferr)) {
                freq_state = "opened";
                fa = bundle::apply_freqs(b, fs, ops->channels(), trusted, source);
                for (auto& f : fs) f.second = 0;
            } else {
                freq_state = "not_opened";
                a.problems.push_back("frequencies not opened: " + ferr);
            }
        }
    } else if (!b.for_station.empty()) {
        freq_state = b.for_station == me.station ? "none_for_you" : "none";
    }
    for (auto& p : fa.problems) a.problems.push_back("frequency: " + p);
    {
        std::vector<std::string> still;
        for (auto& c : a.need_frequency)
            if (!ops->channels().get(c)) still.push_back(c);
        a.need_frequency = still;
    }
    // Forms: only from a signer checked here, and never over a form this
    // member changed on the box.
    int forms_added = 0, forms_replaced = 0, forms_same = 0, forms_kept = 0, forms_held = 0;
    {
        forms::Library lib(forms_dir(core));
        for (auto& [fid, ftext] : b.forms) {
            if (!trusted) {
                ++forms_held;
                continue;
            }
            auto have = lib.get(fid);
            if (have && have->changed && have->origin == "edited here") {
                ++forms_kept;
                continue;
            }
            std::string res, got, ferr;
            if (!lib.put(ftext, "from bundle " + b.by, &res, &got, &ferr)) {
                a.problems.push_back("form " + fid + ": " + ferr);
                continue;
            }
            if (res == "added") ++forms_added;
            else if (res == "replaced") ++forms_replaced;
            else ++forms_same;
        }
    }
    int links = 0;
    for (auto& l : b.links) {
        if (l.first == me.station) continue;
        if (core.set_link_format(l.first, l.second, &err)) ++links;
        else a.problems.push_back("link " + l.first + ": " + err);
    }
    core.note("startup bundle " + b.tag + " from " + b.by + " loaded (" + signer + " signer): " +
              std::to_string(a.book_added + a.book_replaced) + " book entries, " +
              std::to_string(a.schedules_added + a.schedules_replaced) + " schedules" +
              (freq_state == "opened" ? ", " + std::to_string(fa.added + fa.changed) + " frequencies (sealed to this station)"
                                      : ""));
    JsonWriter w;
    w.begin_object().field("ok", true).field("loaded", true);
    summary(w);
    w.field("signer_checked", trusted)
        .field("book_added", a.book_added)
        .field("book_replaced", a.book_replaced)
        .field("book_same", a.book_same)
        .field("schedules_added", a.schedules_added)
        .field("schedules_replaced", a.schedules_replaced)
        .field("schedules_same", a.schedules_same)
        .field("channels_updated", a.channels_updated)
        .field("links_set", links)
        .field("for", b.for_station)
        .field("frequencies", freq_state)
        .field("frequencies_added", fa.added)
        .field("frequencies_changed", fa.changed)
        .field("frequencies_same", fa.same)
        .field("frequencies_kept", fa.kept)
        .field("forms_added", forms_added)
        .field("forms_replaced", forms_replaced)
        .field("forms_same", forms_same)
        .field("forms_kept", forms_kept)
        .field("forms_held", forms_held);
    w.begin_array("need_frequency");
    for (auto& c : a.need_frequency) w.field("", c);
    w.end_array();
    w.begin_array("problems");
    for (auto& p : a.problems) w.field("", p);
    w.end_array();
    if (fa.kept > 0)
        w.field("next", std::to_string(fa.kept) + " channel(s) here have a different frequency from the bundle and were "
                        "kept: " + b.by + "'s card isn't checked on this box. Check it by voice and load the bundle again "
                        "to take them.");
    else if (!a.need_frequency.empty())
        w.field("next", "This box has no frequency for " + std::to_string(a.need_frequency.size()) +
                            " channel(s) in the bundle. Load your frequency card (mars_import), or send your card to " +
                            b.by + " and check it by voice, for a bundle with them sealed to you.");
    w.end_object();
    return w.str();
}

// rally_status: what this box can do now, and the next step, for a station
// coming back on a BAD_DAY.
std::string rally_status(Core& core, const station::Identity& me) {
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("station", me.station)
        .field("fingerprint", station::fingerprint(station::card_of(me)));
    int cards = 0, keymasters = 0;
    w.begin_array("keymasters");
    for (auto& c : core.cards()) {
        ++cards;
        if (!c.keymaster) continue;
        ++keymasters;
        w.begin_object().field("station", c.station).field("fingerprint", c.fingerprint).end_object();
    }
    w.end_array().field("cards", cards);
    std::string current;
    w.begin_array("keys_now");
    if (core.keyring())
        for (auto& k : core.keyring()->for_sending()) {
            if (current.empty()) current = k.indicator;
            w.begin_object().field("indicator", k.indicator).field("net", k.net).field("check", k.check).end_object();
        }
    w.end_array();
    std::vector<std::pair<std::string, std::string>> ask;
    std::set<std::string> seen;
    for (auto& p : core.pending_handouts()) {
        station::Handout h;
        if (!station::parse_handout(p.text, h, nullptr)) continue;
        for (auto& k : h.keymasters)
            if (core.keymaster_choice(k.first, k.second) == "undecided" && seen.insert(k.first).second)
                ask.push_back(k);
    }
    w.begin_array("undecided");
    for (auto& k : ask) w.begin_object().field("station", k.first).field("fingerprint", k.second).end_object();
    w.end_array();
    std::string next;
    if (!ask.empty() && (keymasters == 0 || current.empty()))
        next = ask[0].first + " " + ask[0].second + " wants to be your keymaster. Check that fingerprint by voice or "
               "in person, then keymaster_accept or keymaster_refuse. Signed plain traffic works meanwhile.";
    else if (keymasters == 0)
        next = "Load your handout (handout_import), stale is fine, or check a keymaster's card by voice and run "
               "card_check with keymaster=Y.";
    else if (current.empty())
        next = "No key for now. Send your card to a keymaster (card_send) and ask for a new tape. Signed plain "
               "traffic works meanwhile.";
    else
        next = "Ready: key " + current + " is current. Read its check to the keymaster by voice.";
    w.field("next", next).end_object();
    return w.str();
}

std::optional<std::string> station_cmd(Core& core, const std::string& cmd, std::map<std::string, std::string>& req) {
    static const std::set<std::string> kCmds = {"station_card", "station_backup", "station_restore", "card_import",
                                                "cards_list",   "card_check",     "card_send",       "net_tape",
                                                "tape_seal",    "card_keymaster", "handout_import",  "rally_status",
                                                "keymaster_accept", "keymaster_refuse", "keymaster_list",
                                                "bundle_make", "bundle_load", "bundle_send",
                                                "parts_list", "parts_request", "parts_resend", "parts_queue",
                                                "parts_queue_send", "parts_queue_drop"};
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
            .field("card", station::format_card(*me));
        if (!req["out"].empty()) {
            // On a stick, for a keymaster met in person.
            std::ofstream f(req["out"], std::ios::binary | std::ios::trunc);
            f << station::format_card(*me);
            if (!f) return error_reply("can't write " + req["out"]);
            w.field("written", req["out"]);
        }
        w.end_object();
        return w.str();
    }
    if (cmd == "station_backup") {
        std::string out;
        std::vector<station::HeldCard> held;
        for (auto& c : core.cards()) held.push_back(station::HeldCard{c.card, c.verified, c.keymaster, c.source});
        int64_t now = core.keyring() ? core.keyring()->now() : static_cast<int64_t>(std::time(nullptr));
        bool ok = station::backup(*me, held, now, req["passphrase"], out, &err);
        crypto::wipe(req["passphrase"]);
        if (!ok) return error_reply(err);
        core.note("an encrypted backup of the station keys and " + std::to_string(held.size()) + " held card(s) was made");
        JsonWriter w;
        w.begin_object().field("ok", true).field("backup", out).field("cards", static_cast<int>(held.size()));
        if (!req["out"].empty()) {
            std::ofstream f(req["out"], std::ios::binary | std::ios::trunc);
            f << out;
            if (!f) return error_reply("the backup was made but can't be written to " + req["out"]);
            w.field("written", req["out"]);
        }
        w.end_object();
        return w.str();
    }
    if (cmd == "station_restore") {
        station::Identity id;
        std::vector<station::HeldCard> held;
        int64_t saved = 0;
        std::string btext = req["text"];
        if (!req["file"].empty()) {
            bool read = false;
            btext = read_whole(req["file"], &read);
            if (!read) {
                crypto::wipe(req["passphrase"]);
                return error_reply("can't read " + req["file"]);
            }
        }
        bool ok = station::restore(btext, req["passphrase"], id, &held, &saved, &err);
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
        // The cards it held, trusted as they were when the backup was made.
        int restored = 0;
        std::vector<std::string> refused;
        for (auto& h : held) {
            station::Card c;
            std::string what, e;
            bool good = h.checked ? core.add_trusted_card(h.card, "backup (" + h.source + ")", h.keymaster, false, c, what, &e)
                                  : core.add_card(h.card, "backup (" + h.source + ")", false, c, what, &e);
            if (good) {
                ++restored;
            } else {
                refused.push_back(e);
            }
        }
        int64_t now = core.keyring() ? core.keyring()->now() : static_cast<int64_t>(std::time(nullptr));
        JsonWriter w;
        w.begin_object().field("ok", true).field("fingerprint", fp).field("cards", restored);
        if (saved > 0)
            w.field("saved", keys::time_text(saved)).field("age_days", static_cast<int>((now - saved) / 86400));
        w.begin_array("refused");
        for (auto& r : refused) w.field("", r);
        w.end_array().end_object();
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
        if (!req["file"].empty()) {
            bool read = false;
            text = read_whole(req["file"], &read);
            if (!read) return error_reply("can't read " + req["file"]);
            if (req["source"].empty()) source = "file " + req["file"];
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
                .field("keymaster", c.keymaster)
                .field("source", c.source)
                .field("added", static_cast<long long>(c.added))
                .end_object();
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "card_check") {
        // keymaster=Y is the member accepting that station as a keymaster.
        bool ok = req["keymaster"] == "Y" ? core.accept_keymaster(req["station"], req["fingerprint"], "by voice", &err)
                                          : core.verify_card(req["station"], req["fingerprint"], &err);
        if (!ok) return error_reply(err);
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("station", req["station"])
            .field("checked", true)
            .field("keymaster", core.card_keymaster(req["station"]));
        if (req["keymaster"] == "Y") reapply_pending(w, core, *me, req["station"]);
        w.end_object();
        return w.str();
    }
    if (cmd == "card_keymaster") {
        bool on = req["keymaster"] != "N";
        auto row = core.card_for(req["station"]);
        if (on && row && core.card_verified(req["station"])) {
            if (!core.accept_keymaster(req["station"], station::fingerprint(*row), "earlier", &err))
                return error_reply(err);
        } else if (!core.set_card_keymaster(req["station"], on, &err)) {
            return error_reply(err);
        }
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("station", req["station"])
            .field("keymaster", core.card_keymaster(req["station"]));
        if (on) reapply_pending(w, core, *me, req["station"]);
        w.end_object();
        return w.str();
    }
    if (cmd == "handout_import") return handout_import(core, *me, req);
    if (cmd == "bundle_make" || cmd == "bundle_load" || cmd == "bundle_send") return bundle_cmd(core, *me, cmd, req);
    if (cmd == "parts_list" || cmd == "parts_request" || cmd == "parts_resend" || cmd == "parts_queue" ||
        cmd == "parts_queue_send" || cmd == "parts_queue_drop")
        return *parts_cmd(core, *me, cmd, req);
    if (cmd == "keymaster_accept" || cmd == "keymaster_refuse" || cmd == "keymaster_list")
        return keymaster_cmd(core, *me, cmd, req);
    if (cmd == "rally_status") return rally_status(core, *me);
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
    std::string tape_text, net, tape_id, first_key, first_check;
    int64_t valid_from = 0, valid_until = 0;
    std::vector<std::string> members, alternates;
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
        auto slot = core.cipher();
        if (!slot->usable()) return error_reply(slot->why_unusable());
        if (!keys::make_tape(net, start, days, period, now, t, &err, slot->info().name, slot->info().key_bytes))
            return error_reply(err);
        tape_text = keys::format_tape(t);
        first_key = t.keys.front().indicator;
        first_check = first_key_check(core, t);
        valid_from = t.keys.front().from;
        valid_until = t.keys.back().until;
        for (auto& k : t.keys) crypto::wipe(k.secret);
        tape_id = t.tape;
        keys::ImportResult ir;
        if (!kr->import_tape(tape_text, ir, &err)) {
            crypto::wipe(tape_text);
            return error_reply("the new tape did not load here: " + err);
        }
        core.note("key tape " + tape_id + " for " + net + " made here: " + std::to_string(ir.added) + " key(s) loaded");
        std::vector<std::string> named = call_list(req["members"]);
        // Alternate keymasters get the tape too, and a handout with every
        // member's card so they can issue tapes if this station is lost.
        for (auto& a : call_list(req["keymasters"]))
            if (a != me->station) {
                alternates.push_back(a);
                if (std::find(named.begin(), named.end(), a) == named.end()) named.push_back(a);
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
        if (!t.keys.empty()) {
            first_key = t.keys.front().indicator;
            first_check = first_key_check(core, t);
            valid_from = t.keys.front().from;
            valid_until = t.keys.back().until;
        }
        for (auto& k : t.keys) crypto::wipe(k.secret);
        net = t.net;
        tape_id = t.tape;
        members.push_back(req["to"]);
    }
    JsonWriter w;
    w.begin_object().field("ok", true).field("net", net).field("tape", tape_id).begin_array("sealed");
    std::vector<std::string> no_card;
    std::map<std::string, std::string> sealed_for;
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
        sealed_for[m] = sealed;
        w.begin_object().field("station", m).field("checked", core.card_verified(m)).field("text", sealed);
        if (req["send"] == "Y") {
            // A long tape (a month or more) goes in numbered parts.
            std::string e;
            int n_parts = 0;
            auto ids = send_in_parts(core, m, "TAPE", parts::tag_of(sealed), sealed, &e, &n_parts);
            w.field("id", ids.empty() ? std::string() : ids.front()).field("parts", n_parts);
            w.begin_array("ids");
            for (auto& x : ids) w.field("", x);
            w.end_array();
            if (ids.empty()) w.field("error", e);
        }
        w.end_object();
    }
    crypto::wipe(tape_text);
    w.end_array().begin_array("no_card");
    for (auto& n : no_card) w.field("", n);
    w.end_array();
    const std::string handout_dir = req["out"];
    if (req["handouts"] == "Y" || !handout_dir.empty()) {
        // One handout per member: no secret in it, only cards, fingerprints,
        // the tape sealed to that member and the steps.
        station::Handout base;
        base.net = net;
        base.by = me->station;
        base.issued = core.keyring() ? core.keyring()->now() : static_cast<int64_t>(std::time(nullptr));
        base.tape = tape_id;
        base.valid_from = valid_from;
        base.valid_until = valid_until;
        base.first_key = first_key;
        base.first_check = first_check;
        {
            auto slot = core.cipher();
            if (slot->usable() && !slot->is_builtin()) {
                base.cipher = slot->info().name;
                base.cipher_hash = slot->info().set_hash;
            }
        }
        station::Card mine = station::card_of(*me);
        base.keymasters.push_back({me->station, station::fingerprint(mine)});
        base.cards.push_back(station::format_card(*me));
        std::vector<std::string> member_cards;
        for (auto& a : alternates) {
            auto row = core.cards();
            for (auto& r : row)
                if (r.station == a) {
                    base.keymasters.push_back({a, r.fingerprint});
                    base.cards.push_back(r.card);
                    if (r.verified) core.set_card_keymaster(a, true, nullptr);
                }
        }
        for (auto& m : members)
            for (auto& r : core.cards())
                if (r.station == m) {
                    base.members.push_back({m, r.fingerprint});
                    member_cards.push_back(r.card);
                }
        w.begin_array("handouts");
        for (auto& m : members) {
            station::Handout h = base;
            h.for_station = m;
            auto it = sealed_for.find(m);
            if (it != sealed_for.end()) h.sealed = it->second;
            bool alt = std::find(alternates.begin(), alternates.end(), m) != alternates.end();
            if (alt)
                for (auto& c : member_cards)
                    if (std::find(h.cards.begin(), h.cards.end(), c) == h.cards.end()) h.cards.push_back(c);
            std::string text = station::format_handout(h, *me);
            w.begin_object()
                .field("station", m)
                .field("has_tape", !h.sealed.empty())
                .field("alternate", alt)
                .field("text", text);
            if (!handout_dir.empty()) {
                // On a stick: one file per member, HANDOUT-STA-B.TXT.
                std::string path = handout_dir + "/HANDOUT-" + m + ".TXT";
                std::ofstream f(path, std::ios::binary | std::ios::trunc);
                f << text;
                w.field("file", f ? path : std::string());
                if (!f) w.field("problem", "can't write " + path);
            }
            w.end_object();
        }
        w.end_array();
    }
    w.end_object();
    if (cmd == "net_tape" && !members.empty())
        core.note("key tape " + tape_id + " sealed to " + std::to_string(members.size() - no_card.size()) + " station(s)");
    return w.str();
}


// ---- Training (src/training.h, docs/guide.md) ----
//
// One practice station per real station while training is on. The real
// station keeps receiving underneath; the API answers from the practice one.

std::mutex g_training_mu;
std::map<const Core*, std::shared_ptr<Training>> g_training;

// One turn on, off or reload at a time, whichever connection asks.
std::mutex g_training_toggle_mu;

std::shared_ptr<Training> training_for(const Core& core) {
    std::lock_guard<std::mutex> g(g_training_mu);
    auto it = g_training.find(&core);
    return it == g_training.end() ? nullptr : it->second;
}

// Takes a practice station out of use. A call another connection is still
// running on it holds its own reference, so the station is stopped only once
// that call has finished: tearing it down under a running call crashed the
// core (phoenix_nest_mars#3). Called with g_training_toggle_mu held and the
// station already out of g_training, so no new call can reach it. False, with
// the station put back as it was, if a call is still on it after `wait_ms`.
bool retire_training(const Core& core, std::shared_ptr<Training>& t, int wait_ms, std::string* err) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    while (t.use_count() > 1) {
        if (std::chrono::steady_clock::now() >= end) {
            std::lock_guard<std::mutex> g(g_training_mu);
            g_training[&core] = t;
            *err = "a call on another connection is still running in training: try again";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    t.reset();  // the last reference: ~Training stops the practice stations
    return true;
}

std::string training_dir(const Core& core) {
    std::string db = core.db_path();
    if (db.size() > 3 && db.compare(db.size() - 3, 3, ".db") == 0) db.resize(db.size() - 3);
    return db + ".training";
}

// Marks a reply as from the practice station.
std::string mark_training(const std::string& reply) {
    if (reply.size() < 2 || reply[0] != '{') return reply;
    return reply == "{}" ? std::string("{\"training\":true}") : "{\"training\":true," + reply.substr(1);
}

// What training doesn't do: write files out of the box, read files in (its
// material is built in), or change the real station's backup or cipher.
std::string training_refuses(const std::string& cmd, std::map<std::string, std::string>& req) {
    static const std::set<std::string> never = {"station_backup", "station_restore", "cipher_install", "cipher_remove",
                                                "settings_set", "settings_reset",
                                                // gale-key keys the real station's radio, never practice traffic
                                                "key_prepare", "standing_prepare", "key_confirm", "key_cancel",
                                                "standing_off", "key_status"};
    if (never.count(cmd)) return cmd + " isn't done in training: turn training off first";
    if (!req["out"].empty()) return "training doesn't write files: turn training off first";
    if (!req["file"].empty()) return "training doesn't read files in: its traffic is built in";
    return "";
}

void write_training(JsonWriter& w, Core& core, const std::shared_ptr<Training>& t) {
    w.field("on", t != nullptr)
        .field("since", static_cast<long long>(t ? t->since() : 0))
        .field("samples", t ? t->samples_played() : 0)
        .field("practice_station", std::string(Training::kNcs));
    // The real station's traffic, so the operator knows what waits outside.
    CoreStatus s = core.status();
    w.begin_object("real")
        .field("received", s.counts.received)
        .field("queued", s.counts.queued)
        .field("unacked", s.counts.unacked)
        .field("emcon", s.emcon)
        .end_object();
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
    KeyConnection conn;  // y dialogs opened here are dropped when it closes (gale_key.h)
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

// The store a reply came from, put last in it (ok stays first).
static std::string with_store_mode(const std::string& reply, const char* mode) {
    if (reply.size() < 2 || reply[0] != '{' || reply.back() != '}') return reply;
    std::string f = std::string("\"store_mode\":\"") + mode + "\"";
    return reply == "{}" ? "{" + f + "}" : reply.substr(0, reply.size() - 1) + "," + f + "}";
}

std::string ApiServer::handle(Core& core, const std::string& line) {
    std::map<std::string, std::string> req;
    if (!parse_flat_object(line, req)) return with_store_mode(error_reply("request must be one flat JSON object"), "real");
    const std::string cmd = req["cmd"];
    // Turning training on or off is what changes the store, so it isn't held
    // to the store a front end expects.
    const bool toggle = cmd == "training" || cmd == "training_status";
    const char* mode = training_for(core) ? "training" : "real";
    auto it = req.find("expect_store");
    if (!toggle && it != req.end() && !it->second.empty()) {
        if (it->second != "real" && it->second != "training")
            return with_store_mode(error_reply("expect_store is real or training"), mode);
        if (it->second != mode) {
            JsonWriter w;
            w.begin_object()
                .field("ok", false)
                .field("error", std::string("training is ") + (std::string(mode) == "training" ? "on" : "off") +
                                    " now, so this was answered from the " + mode +
                                    " store: nothing was done. Look again and send it once more.")
                .field("store_mismatch", true)
                .field("expected", it->second)
                .end_object();
            return with_store_mode(w.str(), mode);
        }
    }
    // The stamp is for this check only: no call sees it. Handlers that take
    // every field they're sent (bbs_set's settings, a compose draft's fields)
    // would otherwise refuse it or warn about it (phoenix_nest_mars#3). Every
    // value reaches handle_call as a string either way (parse_flat_object),
    // so writing the rest back out changes nothing else.
    std::string call_line = line;
    if (it != req.end()) {
        req.erase(it);
        JsonWriter w;
        w.begin_object();
        for (auto& kv : req) w.field(kv.first, kv.second);
        w.end_object();
        call_line = w.str();
    }
    std::string reply = handle_call(core, call_line);
    // Where it was answered: a call the practice station answered is marked
    // so (mark_training), even if training changed while it ran. The
    // toggle's own reply names the store now in force.
    if (toggle) mode = training_for(core) ? "training" : "real";
    else mode = reply.rfind("{\"training\":true", 0) == 0 ? "training" : "real";
    return with_store_mode(reply, mode);
}

std::string ApiServer::handle_call(Core& core, const std::string& line) {
    std::map<std::string, std::string> req;
    if (!parse_flat_object(line, req)) return error_reply("request must be one flat JSON object");
    const std::string cmd = req["cmd"];

    // Ask 37: the core is the MARS message center. A call that carries a
    // world other than mars is refused, unless it only reads or sets up
    // (the word feed, the settings, the book's world mark): nothing is ever
    // queued, copied or forwarded into the amateur world from here.
    if (req.count("world")) {
        std::string wv = req["world"];
        for (auto& c : wv) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        static const std::set<std::string> reads = {
            "word_feed",      "heard_stations", "scanner_status", "settings_list", "settings_set", "settings_reset",
            "keying_settings", "identity_get",  "identity_set",   "book_put",      "book_get",     "book_list",
            "station_health", "captures_list",  "status",
            // A channel's world is set up on CHANNELS (station design v1.9).
            "channel_put", "channels_list", "channels_get_full",
            // A form's action comes back through here and is checked itself.
            "form_get", "form_check", "form_submit"};
        if (!wv.empty() && wv != "mars" && !reads.count(cmd))
            return error_reply("refused: world " + wv +
                               ": the MARS message center never sends, copies or forwards into the amateur world");
    }

    // Ask 26: a call that changes a kind of record the host locked is
    // refused here, before any handler (form_submit's action included).
    {
        std::string kind, why = records::refusal(cmd, &kind);
        if (!why.empty()) {
            core.note("refused " + cmd + ": " + kind + " is locked in appliance.conf");
            JsonWriter w;
            w.begin_object().field("ok", false).field("error", why).field("refused", "locked").field("record", kind)
                .end_object();
            return w.str();
        }
    }

    // Training: turned on and off here, on the real station.
    if (cmd == "training" || cmd == "training_status") {
        auto t = training_for(core);
        if (cmd == "training") {
            std::string on = req["on"], reload = req["reload"];
            for (auto* x : {&on, &reload})
                for (auto& c : *x) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!on.empty() && on != "Y" && on != "N") return error_reply("on is Y or N");
            std::string err;
            if (on == "N" || on == "Y" || reload == "Y") {
                std::lock_guard<std::mutex> toggle(g_training_toggle_mu);
                t = training_for(core);  // as it is now that this connection has the toggle
                // Off, or a reload: the practice station in use is retired first.
                if (t && (on == "N" || reload == "Y")) {
                    {
                        std::lock_guard<std::mutex> g(g_training_mu);
                        g_training.erase(&core);
                    }
                    if (!retire_training(core, t, 30000, &err)) return error_reply(err);
                }
                if (on != "N" && !t) {
                    t = std::make_shared<Training>(training_dir(core), core.station());
                    bool good = reload == "Y" ? t->reload(&err) : t->start(&err);
                    if (!good) return error_reply(std::string("training didn't ") + (reload == "Y" ? "reload: " : "start: ") + err);
                    std::lock_guard<std::mutex> g(g_training_mu);
                    g_training[&core] = t;
                }
            }
        }
        JsonWriter w;
        w.begin_object().field("ok", true);
        write_training(w, core, t);
        w.end_object();
        return w.str();
    }
    if (auto t = training_for(core)) {
        // The guide speaks of training while it is on.
        if (cmd.rfind("guide_", 0) == 0)
            if (auto r = handle_guide(core, cmd, req, true)) return mark_training(*r);
        std::string why = training_refuses(cmd, req);
        if (!why.empty()) return mark_training(error_reply(why));
        // Ask 39: the amateur addresses are the whole box's, never the
        // practice station's: checked, never written.
        if (cmd == "identity_set") return mark_training(sv::identity_check(req));
        return mark_training(handle_call(t->core(), line));
    }

    if (auto r = handle_forms(core, cmd, req)) return *r;
    if (auto r = handle_guide(core, cmd, req)) return *r;
    if (auto r = settings::handle(cmd, req, static_cast<int64_t>(std::time(nullptr)))) return *r;
    if (auto r = handle_bbs(core, cmd, req)) return *r;
    if (auto r = handle_key(core, cmd, req)) return *r;
    if (auto r = handle_inbox(core, cmd, req)) return *r;
    // The tuner (tuner.h): forwarded to greybeard-ale's ldg-tuner daemon. A
    // channel goes by its designator; its frequency is looked up here and
    // goes only to that local socket.
    {
        Ops* ops = core.ops();
        auto lookup = [ops](const std::string& name) -> std::optional<Channel> {
            return ops ? ops->channels().get(name) : std::nullopt;
        };
        if (auto r = handle_tuner(core.tuner(), cmd, req, lookup, core.emcon())) return *r;
    }
    // The radio side for gale-web (station_view.h): scanner status and orders,
    // the word feed, station health, captures and heard stations.
    if (auto r = sv::handle(core, cmd, req)) return *r;

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
            .field("unacked", s.counts.unacked)
            .field("held", s.counts.held)
            .field("cancelled", s.counts.cancelled)
            .field("overheard", s.counts.overheard)
            .field("garbled", s.counts.garbled)
            .field("unverified", s.counts.unverified)
            .field("written", s.counts.written)
            .field("emcon", s.emcon)
            .field("acks_waiting", s.acks_waiting)
            .field("frames_dropped", static_cast<long long>(s.frames_dropped));
        // The per-hour airtime cap, for the status strip: every source of
        // transmissions the operator didn't press for one by one, in one
        // number. Each source keeps its own rolling hour:
        //   parts_resend  the resend queue (parts_resend_per_hour; 0: no cap)
        //   bbs_auto      machine mode's automatic sends (auto_per_hour);
        //                 counted in the total only while machine mode is on
        // resets_at: when the oldest counted send ages out and room opens
        // again (0 when nothing counts). per_hour 0 with room -1: no cap.
        {
            struct Src {
                std::string name;
                bool active;
                int cap;  // -1: no cap
                int used;
                int64_t oldest;
            };
            std::vector<Src> srcs;
            int64_t oldest = 0;
            const int pc = core.parts_resend_per_hour();
            const int pu = resends_near(core.part_asks(), now_utc(), &oldest);
            srcs.push_back({"parts_resend", true, pc > 0 ? pc : -1, pu, oldest});
            bbs::Engine& be = bbs::engine(core);
            const bbs::Settings bs = be.settings();
            srcs.push_back({"bbs_auto", bs.machine_mode, std::max(0, bs.auto_per_hour), be.auto_sent_last_hour(),
                            be.auto_oldest_last_hour()});
            int per = 0, used = 0, room = 0;
            bool uncapped = false;
            int64_t resets = 0;
            for (auto& x : srcs) {
                if (!x.active) continue;
                used += x.used;
                if (x.cap < 0) uncapped = true;
                else {
                    per += x.cap;
                    room += std::max(0, x.cap - x.used);
                }
            }
            for (auto& x : srcs)
                if (x.used > 0 && x.oldest > 0) resets = resets ? std::min(resets, x.oldest + 3600) : x.oldest + 3600;
            w.begin_object("cap")
                .field("per_hour", uncapped ? 0 : per)
                .field("used", used)
                .field("room", uncapped ? -1 : room)
                .field("resets_at", static_cast<long long>(resets));
            w.begin_array("covers");
            for (auto& x : srcs) w.field("", x.name);
            w.end_array().begin_array("sources");
            for (auto& x : srcs)
                w.begin_object()
                    .field("source", x.name)
                    .field("active", x.active)
                    .field("per_hour", x.cap < 0 ? 0 : x.cap)
                    .field("used", x.used)
                    .field("room", x.cap < 0 ? -1 : std::max(0, x.cap - x.used))
                    .field("resets_at", static_cast<long long>(x.used > 0 && x.oldest > 0 ? x.oldest + 3600 : 0))
                    .end_object();
            w.end_array().end_object();
        }
        auto adv = compose::advisor();
        w.field("advisor", adv ? adv->name() : std::string());
        // Link state from greybeard-ale (link_monitor.h); "link": null with no source.
        write_link(w, s);
        // gale-key (gale_key.h): answering or not and why, standing modes on.
        write_keying(w, core);
        // The radio's current channel, as tuner_channel last set it (no frequency).
        write_radio_channel(w, "radio_channel", core);
        // The tuner daemon's status block, passed through unchanged: channel
        // {ant_rx, ant_tx, recalled_on}, contract_rev, relays.antenna, ...
        // Left out when no tuner is set up or nothing answers.
        if (TunerKeying* tk = core.tuner()) {
            std::string tb = tk->status_block();
            if (!tb.empty()) w.raw("tuner", tb);
        }
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
        } else {
            // No station book: the fields gale-web reads are still there.
            w.field("on_duty", std::string()).field("net", std::string()).field_null("next_net");
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
        // Ask 37: one station of the amateur world refuses the whole send, before any copy is queued.
        for (auto& t : to)
            if (std::string why = core.world_refusal(t); !why.empty())
                return error_reply(to.size() > 1 ? "refused for " + req["to"] + ": " + why : why);
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
        w.end_array();
        // MSG-VIEW: in-box state, events, props, decoded text (api_inbox.h).
        const std::string rd = req["read"];
        write_inbox_extras(w, core, *m, rd == "Y" || rd == "y" || rd == "true" || rd == "1");
        w.end_object();
        return w.str();
    }

    if (cmd == "send_acp127") return send_acp127(core, req);
    // A file as base32 text inside an ACP 127 message (filemsg.h).
    if (cmd == "send_file") {
        bool ok = false;
        std::string data = filemsg::base64_decode_or_empty(req["file_b64"], &ok);
        if (!ok) return error_reply("file_b64 isn't base64");
        if (data.empty()) return error_reply("the file is empty");
        std::string err;
        std::string block = filemsg::encode(req["file_name"], data, &err);
        if (block.empty()) return error_reply(err);
        std::string lead = req["text"];
        req["text"] = lead.empty() ? block : lead + "\n" + block;
        return send_acp127(core, req);
    }
    if (cmd == "get_file") {
        auto m = core.get(req["id"]);
        if (!m) return error_reply("no message " + req["id"]);
        bool present = false;
        filemsg::File f;
        std::string err;
        if (!filemsg::find(m->body, &present, f, &err)) return error_reply(err);
        JsonWriter w;
        w.begin_object().field("ok", true).field("name", f.name).field("size", static_cast<long long>(f.data.size()))
            .field("file_b64", filemsg::base64(f.data)).end_object();
        return w.str();
    }
    if (cmd == "orderwire") {
        std::string err;
        if (!core.orderwire(req["to"], req["text"], &err)) return error_reply(err);
        JsonWriter w;
        w.begin_object().field("ok", true).end_object();
        return w.str();
    }
    if (cmd == "rx_raw") {
        size_t limit = 4000;
        if (req.count("limit")) limit = static_cast<size_t>(std::max(1, std::min(200000, std::atoi(req["limit"].c_str()))));
        JsonWriter w;
        w.begin_object().field("ok", true).field("text", core.raw_rx_tail(limit)).end_object();
        return w.str();
    }
    if (cmd == "overdue") {
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("ids");
        for (auto& id : core.overdue()) w.field("", id);
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "parse_acp127") return parse_acp127(core, req);
    if (cmd == "send_acp126") return send_acp126(core, req);
    if (cmd == "parse_traffic") return parse_traffic(core, req);
    if (cmd == "send_chat") return send_chat(core, req);
    if (cmd == "compose_templates") return compose_templates(core);
    if (cmd == "compose_form") return compose_form(core, req);
    if (cmd == "compose_check") return compose_check(core, req);
    if (cmd == "compose_send") return compose_send(core, req);
    if (cmd == "compose_advise") return compose_advise(core, req);

    // The operator's hand on the queue, EMCON and link formats (docs/api.md).
    if (cmd == "emcon") {
        const std::string v = req["on"];
        if (v == "Y" || v == "y" || v == "on" || v == "1") core.set_emcon(true);
        else if (v == "N" || v == "n" || v == "off" || v == "0") core.set_emcon(false);
        else if (!v.empty()) return error_reply("on must be Y or N");
        JsonWriter w;
        w.begin_object().field("ok", true).field("emcon", core.emcon()).end_object();
        return w.str();
    }
    if (cmd == "cancel" || cmd == "hold" || cmd == "release" || cmd == "requeue") {
        const std::string id = req["id"];
        if (id.empty()) return error_reply("give the message id");
        std::string err;
        bool done = cmd == "cancel"    ? core.cancel(id, &err)
                    : cmd == "hold"    ? core.hold(id, &err)
                    : cmd == "release" ? core.release(id, &err)
                                       : core.requeue(id, &err);
        if (!done) return error_reply(err);
        auto m = core.get(id);
        JsonWriter w;
        w.begin_object().field("ok", true).field("id", id).field("state", m ? m->state : "").end_object();
        return w.str();
    }
    if (cmd == "link_format") {
        std::string err;
        if (!req["station"].empty() && !req["format"].empty() &&
            !core.set_link_format(req["station"], req["format"], &err))
            return error_reply(err);
        JsonWriter w;
        w.begin_object().field("ok", true);
        if (!req["station"].empty()) w.field("station", req["station"]).field("format", core.link_format(req["station"]));
        w.begin_array("links");
        for (auto& [st, f] : core.link_formats()) w.begin_object().field("station", st).field("format", f).end_object();
        w.end_array().end_object();
        return w.str();
    }
    if (cmd == "queue") {
        bool bad = false;
        int limit = limit_of(req, 100, 1000, &bad);
        if (bad) return error_reply("limit must be a number");
        JsonWriter w;
        w.begin_object().field("ok", true).begin_array("messages");
        auto q = core.queue(limit);
        for (const Message& m : q) write_message(w, m, m.state == "queued" && core.waits_for_link(m.dst));
        // Then the bursts written to the outbox, waiting for their key.
        if (static_cast<int>(q.size()) < limit)
            for (const Message& m : core.written_out(limit - static_cast<int>(q.size()))) write_message(w, m);
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
    if (cmd == "cipher_status" || cmd == "cipher_install" || cmd == "cipher_remove") {
        // The cipher slot (docs/cipher-slot.md).
        std::string err;
        if (cmd == "cipher_install") {
            // A folder holding module.so (and lib/), or a packet folder with cipher/ in it.
            namespace fs = std::filesystem;
            std::string from = req["path"];
            std::error_code ec;
            if (from.empty()) return error_reply("give the folder that holds module.so");
            if (!fs::exists(fs::path(from) / "module.so", ec) && fs::exists(fs::path(from) / "cipher" / "module.so", ec))
                from = (fs::path(from) / "cipher").string();
            if (core.cipher_dir().empty()) return error_reply("this station has no place to keep a cipher module");
            auto slot = cipher::install(from, core.cipher_dir(), "", &err);
            if (!slot) return error_reply("not installed: " + err);
            core.set_cipher(slot);
            core.note("cipher module " + slot->info().name + " " + slot->info().version + " installed by the operator");
        } else if (cmd == "cipher_remove") {
            if (core.cipher_dir().empty() || !cipher::remove_installed(core.cipher_dir(), &err))
                return error_reply(err.empty() ? "no installed cipher module" : err);
            core.set_cipher(cipher::Slot::builtin());
            core.note("cipher module removed: the built-in pna-aes is in the slot");
        }
        auto slot = core.cipher();
        const auto& in = slot->info();
        JsonWriter w;
        w.begin_object()
            .field("ok", true)
            .field("usable", slot->usable())
            .field("name", in.name)
            .field("version", in.version)
            .field("builtin", slot->is_builtin())
            .field("output", in.text ? "text" : "bytes")
            .field("key_bytes", static_cast<long long>(in.key_bytes))
            .field("source", in.source)
            .field("set_hash", in.set_hash);
        if (!slot->usable()) w.field("why", slot->why_unusable());
        w.end_object();
        return w.str();
    }
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
                parts::Head ph;
                if (parts::head(text, ph) && ph.kind == "TAPE") {
                    // A long tape in parts: open it only when every part is here.
                    auto ps = part_set_of(core, *m);
                    if (!ps) return error_reply("sealed key tape not opened: that part's signature is bad or can't be checked here");
                    if (!ps->complete) return incomplete_reply(core, *ps, "sealed key tape");
                    text = ps->text();
                }
            }
            std::string tape;
            station::SealedHead h;
            if (station::sealed_head(text, h) && core.card_for(h.from) && !core.card_keymaster(h.from))
                return error_reply("sealed key tape not opened: " + h.from +
                                   " isn't named as a keymaster on this station. Load a handout that lists it, or "
                                   "check its fingerprint by voice and run card_check with keymaster=Y");
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
                .field("check", k.check)
                .field("cipher", k.cipher)
                .field("usable", k.usable)
                .end_object();
        w.end_array().end_object();
        return w.str();
    }

    if (auto r = station_cmd(core, cmd, req)) return *r;

    if (auto r = handle_ops(core, cmd, req)) return *r;

    return error_reply(
        "unknown cmd; use status, key_prepare, standing_prepare, key_confirm, key_cancel, standing_off, key_status, scanner_status, scanner_hold, scanner_release, scanner_record, scanner_plan, word_feed, identity_get, identity_set, settings_list, settings_set, settings_reset, keying_settings, station_health, captures_list, heard_stations, tuner_status, tuner_channel, send, list, get, queue, log, emcon, cancel, hold, release, requeue, link_format, orderwire, rx_raw, overdue, send_file, get_file, send_acp127, send_acp126, send_chat, parse_acp127, "
        "parse_traffic, compose_templates, compose_form, compose_check, compose_send, compose_advise, key_import, keys_list, key_zeroize, key_open, cipher_status, cipher_install, cipher_remove, station_card, station_backup, station_restore, card_import, cards_list, card_check, card_send, net_tape, tape_seal, or the book's calls (docs/api.md): book_*, net_*, sched_*, duty_*, log_add, station_log, report_monthly");
}

}  // namespace pna
