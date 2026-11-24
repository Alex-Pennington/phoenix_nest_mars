// First start: see first_start.h and docs/quick-start.md.
#include "first_start.h"

#include "frame.h"
#include "json.h"
#include "net.h"
#include "station.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <sys/socket.h>

namespace pna {
namespace first_start {

namespace {

std::string error_reply(const std::string& why) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("error", why).end_object();
    return w.str();
}

std::string str_of(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}

}  // namespace

std::string call_file(const std::string& db_dir) { return (db_dir.empty() ? std::string(".") : db_dir) + "/station-call"; }

bool saved_call(const std::string& db_dir, std::string& call) {
    call.clear();
    std::ifstream f(call_file(db_dir));
    if (!f) return false;
    std::getline(f, call);
    while (!call.empty() && (call.back() == '\r' || call.back() == ' ')) call.pop_back();
    if (!valid_station(call) || is_broadcast(call)) {
        call.clear();
        return false;
    }
    return true;
}

std::string handle(const std::string& db_dir, int64_t now, const std::string& line, std::string* call_set) {
    JsonValue req;
    if (!parse_json(line, req) || !req.get("cmd")) return error_reply("send one JSON object with \"cmd\"");
    std::string cmd = str_of(req, "cmd");
    std::string have;
    bool set = saved_call(db_dir, have);
    if (cmd == "setup_status" || cmd == "status") {
        JsonWriter w;
        w.begin_object().field("ok", true).field("setup_needed", !set);
        if (set) w.field("call", have);
        w.field("next", set ? "The call is set. The station is starting."
                            : "This box has no station call yet. Give it with setup_station call=YOUR-CALL "
                              "(letters A-Z, digits and -, at most 16). The box then makes its station keys and card.");
        w.end_object();
        return w.str();
    }
    if (cmd != "setup_station")
        return error_reply("this box has no station call yet: run setup_station with its call first");
    if (set) return error_reply("this box's call is already set to " + have);
    std::string call = str_of(req, "call");
    for (auto& c : call) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    while (!call.empty() && call.back() == ' ') call.pop_back();
    while (!call.empty() && call.front() == ' ') call.erase(call.begin());
    if (!valid_station(call) || is_broadcast(call))
        return error_reply("\"" + call + "\" isn't a call this box can use: letters A-Z, digits and -, at most 16, not ALL");
    std::error_code ec;
    std::filesystem::create_directories(db_dir.empty() ? "." : db_dir, ec);
    // The station keys, where the station looks for them (DB_DIR/CALL.id).
    std::string ip = (db_dir.empty() ? std::string(".") : db_dir) + "/" + call + ".id";
    station::Identity id;
    bool exists = false;
    std::string err;
    if (!station::load_identity(ip, id, &exists, &err)) return error_reply("station keys there can't be read: " + err);
    bool made = false;
    if (!exists || id.station != call) {
        if (exists) return error_reply("the station keys at " + ip + " are for " + id.station + ", not " + call);
        if (!station::make_identity(call, now, id, &err) || !station::save_identity(ip, id, &err))
            return error_reply("station keys not made: " + err);
        made = true;
    }
    // The call last, written whole, so a cut-off write leaves setup to run again.
    std::string cf = call_file(db_dir), tmp = cf + ".new";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << call << "\n";
        if (!f) return error_reply("can't write " + tmp);
    }
    std::filesystem::rename(tmp, cf, ec);
    if (ec) return error_reply("can't write " + cf + ": " + ec.message());
    if (call_set) *call_set = call;
    station::Card card = station::card_of(id);
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("call", call)
        .field("keys_made", made)
        .field("fingerprint", station::fingerprint(card))
        .field("card", station::format_card(id))
        .field("next", "Station " + call + " is starting. Read your fingerprint to your keymaster by voice, "
                       "and send your card (card_send).")
        .end_object();
    return w.str();
}

std::string serve(const std::string& db_dir, uint16_t port, const std::atomic<bool>* stop, bool* opened) {
    int lfd = tcp_listen_local(port);
    if (opened) *opened = lfd >= 0;
    if (lfd < 0) return "";
    std::string call;
    while (call.empty() && !(stop && *stop)) {
        if (wait_readable(lfd, 200) != 1) continue;
        int fd = ::accept(lfd, nullptr, nullptr);
        if (fd < 0) continue;
        std::string buf;
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (call.empty() && std::chrono::steady_clock::now() < until && !(stop && *stop)) {
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                if (line.find_first_not_of(" \r\t") == std::string::npos) continue;
                std::string reply = handle(db_dir, static_cast<int64_t>(std::time(nullptr)), line, &call);
                send_all(fd, reply + "\n");
                until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            }
            int r = wait_readable(fd, 200);
            if (r < 0) break;
            if (r == 1 && read_some(fd, buf) <= 0) break;
        }
        close_fd(fd);
    }
    close_fd(lfd);
    return call;
}

}  // namespace first_start
}  // namespace pna
