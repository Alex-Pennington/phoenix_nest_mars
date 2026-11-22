#include "api_server.h"

#include "acp126.h"
#include "acp127.h"
#include "received.h"
#include "json.h"
#include "net.h"

#include <sys/socket.h>

#include <cstdio>
#include <ctime>
#include <map>
#include <sstream>

namespace pna {

namespace {

std::string error_reply(const std::string& why) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", why).end_object();
    return w.str();
}

void write_message(JsonWriter& w, const Message& m) {
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
// with the next channel serial and NR, and the DTG line if "dtg":"true".
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
    m.with_dtg = req["dtg"] == "true" || req["dtg"] == "1";
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
    w.begin_object().field("ok", true).field("id", id).field("source", acp126::kSource).field("message", text).end_object();
    return w.str();
}

// parse_traffic: reads a stored message in any form MARS stations send
// (received.h) and lists what it had to tolerate.
std::string parse_traffic(Core& core, std::map<std::string, std::string>& req) {
    auto msg = core.get(req["id"]);
    if (!msg) return error_reply("no message with that id");
    received::Message m;
    std::string err;
    if (!received::parse(msg->body, m, &err)) return error_reply("not a message this reader can place: " + err);
    std::string routing, warnings;
    for (auto& r : m.routing) routing += (routing.empty() ? "" : " ") + r;
    for (auto& x : m.warnings) warnings += (warnings.empty() ? "" : "\n") + x;
    std::string prec = m.prec ? std::string(1, m.prec) : "";
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("id", msg->id)
        .field("form", received::form_name(m.form))
        .field("channel", m.channel)
        .field("channel_serial", m.channel_serial)
        .field("prec", prec)
        .field("precedence", m.prec ? acp127::prec_name(m.prec) : "")
        .field("routing", routing)
        .field("originator", m.originator)
        .field("serial", m.serial)
        .field("time_filed", m.time_filed)
        .field("security", m.security)
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
            .field("frames_dropped", static_cast<long long>(s.frames_dropped))
            .end_object();
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

    return error_reply("unknown cmd; use status, send, send_acp127, send_acp126, parse_acp127, parse_traffic, list or get");
}

}  // namespace pna
