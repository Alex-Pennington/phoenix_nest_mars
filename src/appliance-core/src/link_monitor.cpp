#include "link_monitor.h"

#include "json.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace pna {

namespace {
long long now_s() { return static_cast<long long>(std::time(nullptr)); }

std::string str_of(const JsonValue* v) {
    return v && v->type == JsonValue::Type::String ? v->s : std::string();
}

bool read_link(const JsonValue& e, StationLink& out) {
    if (!e.is_object()) return false;
    for (const char* k : {"station", "with", "addr", "call"})
        if (!str_of(e.get(k)).empty()) {
            out.station = str_of(e.get(k));
            break;
        }
    out.state = str_of(e.get("state"));
    const JsonValue* t = e.get("since");
    if (t && t->type == JsonValue::Type::Number) out.since = static_cast<long long>(t->n);
    return !out.station.empty();
}
}  // namespace

std::string clock_note(const LinkState& s, long long now) {
    if (s.socket.empty()) return "";
    if (!s.reachable) return "can't tell: greybeard-ale's scanner isn't answering";
    if (s.clock == "wwv") {
        if (s.last_wwv_at <= 0 || now < s.last_wwv_at) return "set by WWV";
        long long m = (now - s.last_wwv_at) / 60;
        if (m < 1) return "set by WWV just now";
        if (m < 120) return "set by WWV " + std::to_string(m) + " min ago";
        return "set by WWV " + std::to_string(m / 60) + " h ago";
    }
    return "not set by WWV yet: times come from this computer's clock";
}

bool StationLink::up() const { return state == "linked" || state == "up" || state == "active"; }

bool parse_link_status(const std::string& line, LinkState* s) {
    JsonValue v;
    if (!parse_json(line, v) || !v.is_object()) {
        s->error = "the reply is not a JSON object";
        return false;
    }
    const JsonValue* ok = v.get("ok");
    if (!ok || ok->type != JsonValue::Type::Bool || !ok->b) {
        std::string e = str_of(v.get("error"));
        s->error = e.empty() ? "the reply is not ok" : e;
        return false;
    }
    s->scanner = str_of(v.get("state"));
    s->plan = str_of(v.get("plan"));
    const JsonValue* eng = v.get("engine");
    s->engine = eng && eng->type == JsonValue::Type::Bool && eng->b;
    s->clock = str_of(v.get("clock"));
    s->note = str_of(v.get("note"));
    const JsonValue* lw = v.get("last_wwv_unix");
    s->last_wwv_at = lw && lw->type == JsonValue::Type::Number ? static_cast<long long>(lw->n) : 0;
    s->hold_window = 0;
    const JsonValue* hold = v.get("hold");
    if (hold && hold->is_object()) {
        const JsonValue* w = hold->get("window");
        if (w && w->type == JsonValue::Type::Number) s->hold_window = static_cast<int>(w->n);
    }
    s->links.clear();
    s->links_reported = false;
    if (const JsonValue* ls = v.get("links")) {
        if (ls->is_array()) {
            s->links_reported = true;
            for (auto& e : ls->arr) {
                StationLink l;
                if (read_link(e, l)) s->links.push_back(l);
            }
        }
    } else if (const JsonValue* l1 = v.get("link")) {
        s->links_reported = l1->is_object() || l1->type == JsonValue::Type::Null;
        StationLink l;
        if (l1->is_object() && read_link(*l1, l)) s->links.push_back(l);
    }
    s->error.clear();
    return true;
}

bool unix_request(const std::string& path, const std::string& line, int timeout_ms, std::string* reply,
                  std::string* error) {
    sockaddr_un addr{};
    if (path.empty() || path.size() >= sizeof(addr.sun_path)) {
        if (error) *error = "bad socket path";
        return false;
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        if (error) *error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        if (error) *error = errno == ENOENT ? "no socket there (is ale-scan running with --control?)"
                                            : std::string("connect: ") + std::strerror(errno);
        ::close(fd);
        return false;
    }
    std::string out = line + "\n";
    if (::send(fd, out.data(), out.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(out.size())) {
        if (error) *error = std::string("send: ") + std::strerror(errno);
        ::close(fd);
        return false;
    }
    std::string buf;
    char chunk[4096];
    while (buf.find('\n') == std::string::npos && buf.size() < 64 * 1024) {
        ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
        if (n <= 0) break;
        buf.append(chunk, static_cast<size_t>(n));
    }
    ::close(fd);
    size_t nl = buf.find('\n');
    if (nl == std::string::npos) {
        if (error) *error = "no reply";
        return false;
    }
    *reply = buf.substr(0, nl);
    return true;
}

LinkMonitor::LinkMonitor(std::string socket_path, int poll_ms) : path_(std::move(socket_path)), poll_ms_(poll_ms) {
    st_.socket = path_;
    st_.error = "not read yet";
}

LinkMonitor::~LinkMonitor() { stop(); }

void LinkMonitor::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { loop(); });
}

void LinkMonitor::stop() {
    if (!running_.exchange(false)) return;
    { std::lock_guard<std::mutex> g(mu_); }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void LinkMonitor::loop() {
    while (running_) {
        poll_once();
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait_for(lk, std::chrono::milliseconds(poll_ms_), [this] { return !running_; });
    }
}

void LinkMonitor::poll_once() {
    std::string reply, err;
    LinkState next;
    {
        std::lock_guard<std::mutex> g(mu_);
        next = st_;
    }
    next.polled_at = now_s();
    bool ok = unix_request(path_, "status", 2000, &reply, &err) && parse_link_status(reply, &next);
    if (ok) {
        next.raw = reply;
        next.reachable = true;
        next.last_ok_at = next.polled_at;
    } else {
        next.reachable = false;
        if (!err.empty()) next.error = err;
        // A source we can't read reports no links: nothing counts as linked.
        next.links.clear();
        next.links_reported = false;
    }
    std::lock_guard<std::mutex> g(mu_);
    st_ = next;
}

LinkState LinkMonitor::state() const {
    std::lock_guard<std::mutex> g(mu_);
    return st_;
}

bool LinkMonitor::link_up(const std::string& station) const {
    std::lock_guard<std::mutex> g(mu_);
    if (!st_.reachable) return false;
    for (auto& l : st_.links)
        if (l.station == station) return l.up();
    return false;
}

}  // namespace pna
