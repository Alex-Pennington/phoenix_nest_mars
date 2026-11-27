#include "gale_key.h"

#include "compose.h"
#include "frame.h"
#include "s5066.h"
#include "s5066_client.h"
#include "sha256.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <random>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace pna {

namespace fs = std::filesystem;

namespace {

GaleKeyReply not_reached(const std::string& why) {
    GaleKeyReply r;
    r.reason = why;
    return r;
}

const JsonValue* member(const JsonValue& v, const char* k) { return v.is_object() ? v.get(k) : nullptr; }

std::string text_of(const JsonValue& v, const char* k) {
    const JsonValue* x = member(v, k);
    return x && x->type == JsonValue::Type::String ? x->s : std::string();
}

bool one_of(const std::string& s, std::initializer_list<const char*> set) {
    for (const char* x : set)
        if (s == x) return true;
    return false;
}

bool read_whole(const std::string& path, std::string& out) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return static_cast<bool>(f) || f.eof();
}

// Every GaleKey alive, so a closing connection drops its dialogs in each.
// Never destroyed: a GaleKey held in a global (appliance-core's g_gale_key)
// is destroyed at exit after a function's static would be, and its
// destructor still erases itself from here.
std::mutex g_reg_mu;
std::set<GaleKey*>& registry() {
    static auto* r = new std::set<GaleKey*>;
    return *r;
}
std::atomic<unsigned long long> g_next_conn{1};
thread_local unsigned long long t_conn = 0;

}  // namespace

// ------------------------------------------------------------------ connections

KeyConnection::KeyConnection() : id_(g_next_conn++) { t_conn = id_; }

KeyConnection::~KeyConnection() {
    t_conn = 0;
    std::lock_guard<std::mutex> g(g_reg_mu);
    for (GaleKey* k : registry()) k->drop_connection(id_);
}

unsigned long long KeyConnection::current() { return t_conn; }

// ------------------------------------------------------------------ socket

GaleKeySocket::GaleKeySocket(std::string path, int timeout_ms) : path_(std::move(path)), timeout_ms_(timeout_ms) {}

GaleKeyReply GaleKeySocket::call(const std::string& request) {
    sockaddr_un addr{};
    if (path_.empty() || path_.size() >= sizeof(addr.sun_path)) return not_reached("no usable controller socket path");
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return not_reached(std::string("socket: ") + std::strerror(errno));
    timeval tv{timeout_ms_ / 1000, (timeout_ms_ % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path_.c_str(), path_.size());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        std::string why = errno == ENOENT || errno == ECONNREFUSED
                              ? "the station controller isn't running (nothing answers on " + path_ + ")"
                              : "the controller's socket: " + std::string(std::strerror(errno));
        ::close(fd);
        return not_reached(why);
    }
    const std::string line = request + "\n";
    // The controller acts only on a whole line: a request cut short is never acted on.
    if (::send(fd, line.data(), line.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(line.size())) {
        ::close(fd);
        return not_reached("the request didn't reach the controller");
    }
    GaleKeyReply r;
    r.reached = true;
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
        r.reason = "the controller didn't answer within " + std::to_string(timeout_ms_) + " ms (it answers when it accepts a send: stuck?)";
        return r;
    }
    std::string reply = buf.substr(0, nl);
    if (!reply.empty() && reply.back() == '\r') reply.pop_back();
    if (!parse_json(reply, r.body) || !r.body.is_object()) {
        r.body = JsonValue();
        r.reason = "the controller's reply isn't one JSON object";
        return r;
    }
    r.answered = true;
    const JsonValue* ok = r.body.get("ok");
    r.ok = ok && ok->type == JsonValue::Type::Bool && ok->b;
    const JsonValue* acc = r.body.get("accepted");
    // An ok reply that leaves "accepted" out is taken as accepted: the
    // result file decides what became of it.
    r.accepted = acc ? acc->type == JsonValue::Type::Bool && acc->b : r.ok;
    const JsonValue* ex = r.body.get("exit");
    if (ex && ex->type == JsonValue::Type::Number) r.exit = static_cast<int>(ex->n);
    r.reason = text_of(r.body, "reason");
    return r;
}

// ------------------------------------------------------------------ helpers

const std::vector<std::string>& core_standing_modes() {
    static const std::vector<std::string> m = {"machine", "routing", "auto_qsl"};
    return m;
}

bool core_standing_mode(const std::string& mode) {
    const auto& m = core_standing_modes();
    return std::find(m.begin(), m.end(), mode) != m.end();
}

bool gale_key_holds_no_grant(const GaleKeyReply& r, const std::string& mode) {
    // greybeard-ale tools/gale_key.py, the key under a grant:
    //   raise Refused(f"no standing grant for {standing}: the operator's y when turning it on grants it")
    // answered {"ok": false, "exit": EXIT_REFUSED (2), "reason": ...}. Exit 2
    // is every refusal (docs/gale-tx.md, Command line), so the wording is the
    // only mark. Bound to the mode asked for, so gale-key's other "no standing
    // grant for amateur_auto" refusal can't turn a MARS mode off.
    constexpr int kRefused = 2;
    return r.answered && !r.ok && r.exit == kRefused && r.reason.rfind("no standing grant for " + mode, 0) == 0;
}

std::string no_frequency(const std::string& s) {
    // A bare number of six digits or more standing on its own is taken as a
    // frequency in Hz. One joined to a name (a burst's time stamp, a path's
    // number) is not: those are bounded by a letter, -, _, /, . or :.
    auto joins = [](char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '/' || c == '.' || c == ':';
    };
    bool hz = false;
    for (size_t i = 0; i < s.size() && !hz;) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
        if (j - i >= 6 && (i == 0 || !joins(s[i - 1])) && (j == s.size() || !joins(s[j]))) hz = true;
        i = j;
    }
    if (hz || compose::looks_like_frequency(s)) return "(gale-key's text named a frequency; not shown)";
    return s;
}

// ------------------------------------------------------------------ GaleKey

GaleKey::GaleKey(std::shared_ptr<GaleKeyLink> link, std::string outbox_dir)
    : link_(std::move(link)), outbox_(std::move(outbox_dir)) {
    clock_ = [] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    thread_ = std::thread([this] { worker(); });
    std::lock_guard<std::mutex> g(g_reg_mu);
    registry().insert(this);
}

GaleKey::~GaleKey() {
    {
        std::lock_guard<std::mutex> g(g_reg_mu);
        registry().erase(this);
    }
    halt();
}

void GaleKey::halt() {
    {
        std::lock_guard<std::mutex> g(mu_);
        stop_ = true;
        jobs_.clear();
        sis_result_ = nullptr;  // no SIS answer reaches a core going away
    }
    cv_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
    if (sis_) sis_->stop();
}

void GaleKey::set_clock(std::function<double()> clock) {
    std::lock_guard<std::mutex> g(mu_);
    clock_ = std::move(clock);
}

void GaleKey::set_log(std::function<void(const std::string&, const std::string&)> log) {
    std::lock_guard<std::mutex> g(mu_);
    log_ = std::move(log);
}

void GaleKey::set_around(Around around) {
    std::lock_guard<std::mutex> g(mu_);
    around_ = std::move(around);
}

GaleKeyReply GaleKey::send_key(const KeyItem& it, const std::string& request, const std::string& sha256) {
    Around a;
    {
        std::lock_guard<std::mutex> g(mu_);
        a = around_;
    }
    // link = controller: request is empty for the operator's send (SAP 13)
    // and "automatic" for an automatic one (SAP 14).
    std::function<GaleKeyReply()> send = [this, &request] { return ask(request); };
    if (sis_) send = [this, &it, &request, &sha256] { return submit_sis(it, request == "automatic", sha256); };
    if (!a) return send();
    return a(it, send);
}

void GaleKey::note(const std::string& msg_id, const std::string& text) {
    std::function<void(const std::string&, const std::string&)> f;
    {
        std::lock_guard<std::mutex> g(mu_);
        f = log_;
    }
    if (f) f(msg_id, text);
}

GaleKeyReply GaleKey::ask(const std::string& request) {
    GaleKeyReply r = link_ ? link_->call(request) : not_reached("no controller socket is set (controller-socket)");
    std::lock_guard<std::mutex> g(mu_);
    // The controller answers when it accepts: a reply later than the timeout
    // is a controller that took the request and is stuck, not one gone.
    last_ = r.answered ? "answering" : r.reached ? "busy" : "not answering";
    last_why_ = r.answered ? std::string() : no_frequency(r.reason);
    return r;
}

std::string GaleKey::last_state(std::string* why) {
    std::lock_guard<std::mutex> g(mu_);
    if (why) *why = last_why_;
    return last_;
}

std::string GaleKey::wav_path(const std::string& burst) const {
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(outbox_) / burst, ec);
    return (ec ? fs::path(outbox_) / burst : p).lexically_normal().string();
}

std::string GaleKey::air_request(const char* cmd, const KeyItem& it, const std::string& control,
                                 const std::string& extra) const {
    // D.4.2.8.3: the message, carried unopened, and its control information.
    JsonWriter m;
    m.begin_object().field("wav", wav_path(it.burst)).end_object();
    JsonWriter c;
    c.begin_object()
        .field("to", it.to)
        .field("service", it.service)
        .field("channel", it.channel)
        .field("kind", "air")
        .field("control", control)
        .field("final", false)
        .end_object();
    std::string ctl = c.str();
    if (!extra.empty()) ctl = ctl.substr(0, ctl.size() - 1) + "," + extra + "}";
    JsonWriter w;
    w.begin_object().field("cmd", cmd).raw("message", m.str()).raw("control", ctl).end_object();
    return w.str();
}

std::string GaleKey::new_id() {
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::string id = "KP-";
    for (int i = 0; i < 16; ++i) id += hex[rd() & 15];
    return id;
}

void GaleKey::drop_expired() {
    const double now = clock_();
    for (auto it = held_.begin(); it != held_.end();) {
        if (now > it->second.until) it = held_.erase(it);
        else ++it;
    }
}

size_t GaleKey::held() {
    std::lock_guard<std::mutex> g(mu_);
    drop_expired();
    return held_.size();
}

GaleKey::Prepared GaleKey::prepare(const KeyItem& it) {
    if (sis_) return prepare_sis(it);
    Prepared p;
    // gale-key binds its token to the WAV's bytes, so a prepare is only for a
    // burst already whole in the outbox, never for the next one written.
    const std::string wav = wav_path(it.burst);
    std::string bytes;
    if (it.burst.empty() || !read_whole(wav, bytes) || bytes.empty()) {
        p.error = (it.burst.empty() ? std::string("no burst") : it.burst) +
                  " is not in the outbox: nothing to prepare (a prepare is only for a burst already written)";
        return p;
    }
    const std::string digest = sha256_hex(bytes);
    GaleKeyReply r = ask(air_request("prepare", it, "local", ""));
    if (!r.answered) {
        p.error = "not prepared: " + no_frequency(r.reason);
        return p;
    }
    if (!r.ok) {
        p.error = "the controller refused: " + no_frequency(r.reason.empty() ? "no reason given" : r.reason);
        return p;
    }
    const std::string token = text_of(r.body, "token");
    if (token.empty()) {
        p.error = "the controller answered ok but gave no token: nothing to confirm";
        return p;
    }
    const JsonValue* c = member(r.body, "confirm");
    static const JsonValue none;
    const JsonValue& cf = c && c->is_object() ? *c : none;
    // The core's own channel label and service are what was asked for; the
    // controller's summary must agree, or the y would confirm something else.
    // `world` is read as `service` for one release.
    std::string cs = text_of(cf, "service");
    if (cs.empty()) cs = text_of(cf, "world");
    const std::string cc = text_of(cf, "channel");
    if ((!cs.empty() && cs != it.service) || (!cc.empty() && cc != it.channel)) {
        p.error = "the controller's summary names another service or channel than the core asked for: not held";
        return p;
    }
    // The summary for the kiosk: only fields it shows, only values it knows.
    JsonWriter w;
    w.begin_object()
        .field("kind", "air")
        .field("service", it.service)
        .field("world", it.service)
        .field("channel", no_frequency(it.channel));
    const std::string control = text_of(cf, "control");
    w.field("control", one_of(control, {"local", "automatic"}) ? control : std::string("local"));
    if (const JsonValue* id = member(cf, "id"); id && id->type == JsonValue::Type::Bool) w.field("id", id->b);
    const std::string ptt = text_of(cf, "ptt");
    if (one_of(ptt, {"rts", "dtr", "cat"})) w.field("ptt", ptt);
    w.field("msg_id", it.msg_id).field("burst", it.burst).end_object();

    double life = kGaleKeyTokenS;
    if (const JsonValue* e = member(r.body, "expires_s"); e && e->type == JsonValue::Type::Number && e->n > 0)
        life = std::min(life, e->n);
    Held h;
    h.kind = "air";
    h.item = it;
    h.wav = wav;
    h.sha256 = digest;
    h.conn = KeyConnection::current();
    h.token = token;
    {
        std::lock_guard<std::mutex> g(mu_);
        drop_expired();
        for (auto x = held_.begin(); x != held_.end();) {
            if (x->second.kind == "air" && x->second.item.burst == it.burst) x = held_.erase(x);
            else ++x;
        }
        h.id = new_id();
        h.at = clock_();
        h.until = h.at + life;
        held_[h.id] = h;
    }
    p.ok = true;
    p.prepare_id = h.id;
    p.expires_s = static_cast<int>(life);
    p.confirm = w.str();
    note(it.msg_id, "the controller prepared " + it.burst + " (" + it.service + ") on " + it.channel +
                        " for the operator's y");
    return p;
}

GaleKey::Prepared GaleKey::prepare_standing(const std::string& mode, const std::string& service) {
    Prepared p;
    if (!core_standing_mode(mode)) {
        p.error = "mode is machine, routing or auto_qsl (the core never asks for amateur_auto)";
        return p;
    }
    // Automatic modes stay as they are until stage 5 (greybeard-ale #18): MARS
    // only, by name, until the table's automatic_modes decides them.
    if (service != "mars") {
        p.error = "service " + (service.empty() ? std::string("not given") : no_frequency(service)) +
                  ": until stage 5 the core turns on automatic sends in MARS only";
        return p;
    }
    // Also with link = controller: a grant carries the token of gale-key's
    // standing prepare, so prepare kind standing:MODE stays on the local socket
    // (greybeard-ale docs/station-controller.md, Network management).
    JsonWriter w0;
    w0.begin_object().field("cmd", "prepare").field("kind", "standing:" + mode).field("service", service).end_object();
    GaleKeyReply r = ask(w0.str());
    if (!r.answered) {
        p.error = "not prepared: " + no_frequency(r.reason);
        return p;
    }
    if (!r.ok) {
        p.error = "the controller refused: " + no_frequency(r.reason.empty() ? "no reason given" : r.reason);
        return p;
    }
    const std::string token = text_of(r.body, "token");
    if (token.empty()) {
        p.error = "the controller answered ok but gave no token: nothing to confirm";
        return p;
    }
    const JsonValue* c = member(r.body, "confirm");
    static const JsonValue none;
    const JsonValue& cf = c && c->is_object() ? *c : none;
    std::string cs = text_of(cf, "service");
    if (cs.empty()) cs = text_of(cf, "world");
    if (!cs.empty() && cs != service) {
        p.error = "the controller's summary names service " + no_frequency(cs) + ", not " + service + ": not held";
        return p;
    }
    JsonWriter w;
    w.begin_object()
        .field("kind", "standing")
        .field("mode", mode)
        .field("service", service)
        .field("world", service)
        .end_object();
    double life = kGaleKeyTokenS;
    if (const JsonValue* e = member(r.body, "expires_s"); e && e->type == JsonValue::Type::Number && e->n > 0)
        life = std::min(life, e->n);
    Held h;
    h.kind = "standing";
    h.mode = mode;
    h.service = service;
    h.conn = KeyConnection::current();
    h.token = token;
    {
        std::lock_guard<std::mutex> g(mu_);
        drop_expired();
        h.id = new_id();
        h.at = clock_();
        h.until = h.at + life;
        held_[h.id] = h;
    }
    p.ok = true;
    p.prepare_id = h.id;
    p.expires_s = static_cast<int>(life);
    p.confirm = w.str();
    note("", "the controller prepared standing " + mode + " (" + service + ") for the operator's y");
    return p;
}

bool GaleKey::take(const std::string& prepare_id, Held& out, std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = held_.find(prepare_id);
    if (it == held_.end()) {
        if (error) *error = "no such y dialog held (cancelled, used or expired): open it again";
        return false;
    }
    const double now = clock_();
    if (now > it->second.until) {
        held_.erase(it);
        if (error) *error = "that y dialog has expired (gale-key's token lasts 120 s): open it again";
        return false;
    }
    if (it->second.conn != 0 && it->second.conn != KeyConnection::current()) {
        if (error) *error = "that y dialog was opened on another connection: confirm it there, or open it again here";
        return false;
    }
    if (now - it->second.at < kGaleKeySoonestS) {
        if (error) *error = "too soon after the prompt (under 0.5 s): gale-key would refuse it; still held";
        return false;
    }
    out = it->second;
    held_.erase(it);
    return true;
}

bool GaleKey::cancel(const std::string& prepare_id) {
    std::lock_guard<std::mutex> g(mu_);
    drop_expired();
    return held_.erase(prepare_id) > 0;
}

size_t GaleKey::drop_connection(unsigned long long conn) {
    if (conn == 0) return 0;
    std::lock_guard<std::mutex> g(mu_);
    size_t n = 0;
    for (auto it = held_.begin(); it != held_.end();) {
        if (it->second.conn == conn) {
            it = held_.erase(it);
            ++n;
        } else {
            ++it;
        }
    }
    return n;
}

bool GaleKey::unchanged(const Held& h, std::string* error) const {
    std::string bytes;
    if (h.wav != wav_path(h.item.burst) || !read_whole(h.wav, bytes) || bytes.empty()) {
        if (error) *error = h.item.burst + " is no longer in the outbox: not keyed";
        return false;
    }
    if (sha256_hex(bytes) != h.sha256) {
        if (error) *error = h.item.burst + " changed since the y dialog opened (its sha256 differs): not keyed; open it again";
        return false;
    }
    return true;
}

GaleKeyReply GaleKey::key(const Held& h) {
    if (sis_) {
        // The y's sha256 goes with it: the bytes submitted are the bytes checked.
        GaleKeyReply r = send_key(h.item, "", h.sha256);
        note(h.item.msg_id, "handed " + h.item.burst + " (" + h.item.service + ") to the station controller on SAP 13 " +
                                "on the operator's y, for " + h.item.channel + ": " +
                                (r.reached ? std::string("its SIS answer decides") : no_frequency(r.reason)));
        return r;
    }
    JsonWriter t;
    t.begin_object().field("token", h.token).end_object();
    const std::string tok = t.str().substr(1, t.str().size() - 2);  // "token":"..."
    GaleKeyReply r = send_key(h.item, air_request("key", h.item, "local", tok));
    note(h.item.msg_id, "handed " + h.item.burst + " to the controller on the operator's y: " +
                            (r.answered ? (r.ok && r.accepted ? std::string("it accepted it")
                                                              : "it answered " + no_frequency(r.reason))
                                        : no_frequency(r.reason)) +
                            "; its BURST.key.json decides");
    return r;
}

GaleKeyReply GaleKey::grant(const Held& h) {
    JsonWriter w;
    w.begin_object()
        .field("cmd", "grant")
        .field("mode", h.mode)
        .field("service", h.service);
    if (!h.token.empty()) w.field("token", h.token);
    w.end_object();
    GaleKeyReply r = ask(w.str());
    if (r.answered && r.ok) {
        std::lock_guard<std::mutex> g(mu_);
        standing_[h.mode] = true;
        standing_service_[h.mode] = h.service;
    }
    note("", r.answered && r.ok ? "standing " + h.mode + " ON: the controller granted it on the operator's y"
                                : "standing " + h.mode + " not granted: " + no_frequency(r.reason));
    return r;
}

GaleKeyReply GaleKey::revoke(const std::string& mode, const std::string& why) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (mode == "all") {
            standing_.clear();
            standing_service_.clear();
        } else {
            standing_.erase(mode);
            standing_service_.erase(mode);
        }
    }
    JsonWriter w;
    w.begin_object().field("cmd", "revoke").field("mode", mode).end_object();
    GaleKeyReply r = ask(w.str());
    note("", "standing " + mode + " OFF (" + why + "); the controller " +
                 (r.answered ? (r.ok ? std::string("revoked it") : "answered " + no_frequency(r.reason))
                             : no_frequency(r.reason)));
    return r;
}

void GaleKey::forget(const std::string& mode) {
    std::lock_guard<std::mutex> g(mu_);
    standing_.erase(mode);
}

std::vector<std::string> GaleKey::forget_service(const std::string& service) {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::string> off;
    for (auto it = standing_service_.begin(); it != standing_service_.end();) {
        if (it->second == service) {
            if (standing_.erase(it->first)) off.push_back(it->first);
            it = standing_service_.erase(it);
        } else {
            ++it;
        }
    }
    return off;
}

bool GaleKey::standing_on(const std::string& mode) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = standing_.find(mode);
    return it != standing_.end() && it->second;
}

std::vector<std::string> GaleKey::standing() {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::string> out;
    for (auto& m : core_standing_modes())
        if (standing_.count(m) && standing_[m]) out.push_back(m);
    return out;
}

GaleKeyReply GaleKey::key_standing_now(const KeyItem& it, const std::string& mode) {
    if (!core_standing_mode(mode) || !standing_on(mode)) return not_reached("standing " + mode + " is off");
    if (sis_) {
        GaleKeyReply r = send_key(it, "automatic");
        note(it.msg_id, "handed " + it.burst + " (" + it.service + ") to the station controller on SAP 14 under standing " +
                            mode + ": " +
                            (r.reached ? std::string("its SIS answer decides")
                                       : no_frequency(r.reason) + "; not handed down, it stays written"));
        return r;
    }
    JsonWriter t;
    t.begin_object().field("standing", mode).end_object();
    const std::string st = t.str().substr(1, t.str().size() - 2);
    GaleKeyReply r = send_key(it, air_request("key", it, "automatic", st));
    // gale-key revokes grants itself (EMCON, the hourly cap). Told it holds
    // none for this mode, the core turns the mode off too, so its next bursts
    // wait for the operator's y instead of failing.
    if (gale_key_holds_no_grant(r, mode)) {
        {
            std::lock_guard<std::mutex> g(mu_);
            standing_.erase(mode);
        }
        note("", "standing " + mode + " OFF: gale-key holds no grant for it any more; the operator's y turns it on again");
    }
    note(it.msg_id, "handed " + it.burst + " to the controller on " + it.channel + " under standing " + mode + ": " +
                        (r.answered ? (r.ok && r.accepted ? std::string("it accepted it")
                                                          : "it answered " + no_frequency(r.reason))
                                    : no_frequency(r.reason)) +
                        (r.reached ? "; its BURST.key.json decides" : "; not asked, the burst stays written"));
    return r;
}

void GaleKey::key_standing(const KeyItem& it, const std::string& mode, std::function<bool(const KeyItem&)> claim,
                           std::function<void(const KeyItem&, const GaleKeyReply&)> done) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (stop_) return;
        jobs_.push_back(Job{it, mode, std::move(claim), std::move(done)});
    }
    cv_.notify_all();
}

void GaleKey::worker() {
    for (;;) {
        Job j;
        bool poll = false, settle = false;
        {
            std::unique_lock<std::mutex> lk(mu_);
            auto ready = [this] { return stop_ || !jobs_.empty() || settle_now_; };
            // The radio's channel every poll_ms_; while a frame is unsettled,
            // status at least every settle_retry_ms_.
            int wait_ms = poll_ms_;
            if (!unsettled_.empty()) wait_ms = wait_ms > 0 ? std::min(wait_ms, settle_retry_ms_) : settle_retry_ms_;
            bool timed_out = false;
            if (wait_ms > 0) {
                timed_out = !cv_.wait_for(lk, std::chrono::milliseconds(wait_ms), ready);
            } else {
                // A poll interval set while waiting wakes the wait too.
                cv_.wait(lk, [this] { return stop_ || !jobs_.empty() || settle_now_ || poll_ms_ > 0; });
                if (!stop_ && jobs_.empty() && !settle_now_) continue;
            }
            if (stop_) return;
            if (settle_now_ || (timed_out && !unsettled_.empty())) {
                settle = true;  // its status read is the poll's too
                settle_now_ = false;
            } else if (timed_out) {
                poll = true;
            } else {
                j = std::move(jobs_.front());
                jobs_.pop_front();
            }
        }
        if (settle) {
            settle_pass();
            continue;
        }
        if (poll) {
            refresh_radio();
            continue;
        }
        if (j.claim && !j.claim(j.item)) continue;
        GaleKeyReply r = key_standing_now(j.item, j.mode);
        if (j.done) j.done(j.item, r);
    }
}

void GaleKey::set_poll_ms(int ms) {
    {
        std::lock_guard<std::mutex> g(mu_);
        poll_ms_ = ms > 0 ? ms : 0;
    }
    cv_.notify_all();
}

void GaleKey::take_radio(const GaleKeyReply& r) {
    // station-controller.md, status: "radios":[{"radio","transmit","radioChannel","service","since",
    // "source","radioKeyed","claim"}, ...]. The transmitting radio's entry is the radio_channel. The
    // older "radio":{"channel","service","since","source"} is read too, for one release.
    RadioReport rr;
    if (r.answered && r.ok) {
        const JsonValue* x = member(r.body, "radios");
        if (x && x->is_array()) {
            for (auto& e : x->arr) {
                const JsonValue* tx = member(e, "transmit");
                if (!(tx && tx->type == JsonValue::Type::Bool && tx->b)) continue;
                rr.known = true;
                rr.label = no_frequency(text_of(e, "radioChannel"));
                rr.service = text_of(e, "service");
                rr.since = text_of(e, "since");
                rr.source = text_of(e, "source");
                break;
            }
            x = nullptr;
        } else {
            x = member(r.body, "radio");
        }
        if (x && x->is_object()) {
            rr.known = true;
            rr.label = no_frequency(text_of(*x, "channel"));
            rr.service = text_of(*x, "service");
            if (rr.service.empty()) rr.service = text_of(*x, "world");
            rr.since = text_of(*x, "since");
            rr.source = text_of(*x, "source");
        }
    }
    std::lock_guard<std::mutex> g(mu_);
    radio_ = rr;
}

RadioReport GaleKey::radio() {
    std::lock_guard<std::mutex> g(mu_);
    return radio_;
}

void GaleKey::refresh_radio() { status(); }

GaleKeyReply GaleKey::status() {
    GaleKeyReply r = ask(R"({"cmd":"status"})");
    take_radio(r);
    return r;
}

// ------------------------------------------------------------------ link = controller

void GaleKey::set_sis(std::shared_ptr<SisControllerLink> sis) {
    sis_ = std::move(sis);
    if (!sis_) return;
    {
        std::lock_guard<std::mutex> g(mu_);
        settle_wait_s_ = sis_->config().settle_wait_s;
        settle_retry_ms_ = std::max(50, sis_->config().settle_retry_ms);
    }
    sis_->start(
        [this](const SisSend& s, SisControllerLink::End end, uint8_t, const std::string& why) {
            // No answer in time, or a drop: it may have been keyed, so status.last decides.
            if (end == SisControllerLink::End::unanswered) unanswered(s.burst, s.msg_id, s.sha256, why);
            else on_sis(s.burst, s.sha256, end == SisControllerLink::End::confirmed, why);
        },
        [this](const std::string& text) { note("", text); },
        // Every bind, the first and each reconnect: what is unsettled is looked up now.
        [this](const std::string&, bool) {
            {
                std::lock_guard<std::mutex> g(mu_);
                settle_now_ = true;
            }
            cv_.notify_all();
        });
}

void GaleKey::settle(const std::vector<KeyItem>& asked, const std::string& because) {
    if (!sis_ || asked.empty()) return;
    {
        std::lock_guard<std::mutex> g(mu_);
        if (stop_) return;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(settle_wait_s_);
        for (auto& it : asked) {
            if (it.burst.empty()) continue;
            if (std::any_of(unsettled_.begin(), unsettled_.end(),
                            [&](const Unsettled& u) { return u.burst == it.burst; }))
                continue;
            unsettled_.push_back(Unsettled{it.burst, it.msg_id, "", because, until});
        }
        settle_now_ = true;
    }
    cv_.notify_all();
}

void GaleKey::unanswered(const std::string& burst, const std::string& msg_id, const std::string& sha256,
                         const std::string& because) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (stop_) return;
        if (std::none_of(unsettled_.begin(), unsettled_.end(), [&](const Unsettled& u) { return u.burst == burst; }))
            unsettled_.push_back(Unsettled{burst, msg_id, sha256, because,
                                           std::chrono::steady_clock::now() + std::chrono::seconds(settle_wait_s_)});
        settle_now_ = true;
    }
    cv_.notify_all();
}

size_t GaleKey::unsettled() {
    std::lock_guard<std::mutex> g(mu_);
    return unsettled_.size();
}

void GaleKey::settle_pass() {
    std::vector<Unsettled> items;
    {
        std::lock_guard<std::mutex> g(mu_);
        items.assign(unsettled_.begin(), unsettled_.end());
    }
    if (items.empty()) return;
    // station-controller.md, status: "last":[{"send","sha256","service","channel",
    // "result":"keyed"|"refused"|"unknown","why"}, ...], the last 20 sends, newest first.
    GaleKeyReply st = status();
    const JsonValue* last = st.answered ? member(st.body, "last") : nullptr;
    const auto now = std::chrono::steady_clock::now();
    struct Verdict {
        Unsettled u;
        SisOutcome outcome;
        std::string why, how;
    };
    std::vector<Verdict> out;
    for (auto& u : items) {
        if (u.sha256.empty()) {
            std::string bytes;
            if (read_whole(wav_path(u.burst), bytes) && !bytes.empty()) u.sha256 = sha256_hex(bytes);
        }
        bool decided = false;
        Verdict v{u, SisOutcome::unknown, kSisResultUnknown, ""};
        if (st.answered) {
            const JsonValue* e = nullptr;
            if (!u.sha256.empty() && last && last->is_array())
                for (auto& x : last->arr)
                    if (text_of(x, "sha256") == u.sha256) {
                        e = &x;  // the newest under this sha256
                        break;
                    }
            const std::string result = e ? text_of(*e, "result") : std::string();
            if (result == "keyed") {
                decided = true;
                v.outcome = SisOutcome::keyed;
                v.why = no_frequency(text_of(*e, "channel"));
                v.how = "its status says keyed";
            } else if (result == "refused") {
                decided = true;
                v.outcome = SisOutcome::refused;
                v.why = no_frequency(text_of(*e, "why"));
                v.how = "its status says refused";
            } else if (!e) {
                decided = true;
                v.how = u.sha256.empty() ? "the frame is no longer in the outbox, so its sha256 can't be looked up"
                                         : "its status does not name the frame's sha256";
            }
            // Otherwise gale-key has the order and has not answered: its result comes.
        }
        if (!decided && now >= u.until) {
            decided = true;
            v.how = std::string(st.answered ? "its status gave no result" : "its status did not answer") + " within " +
                    std::to_string(settle_wait_s_) + " s";
        }
        if (decided) out.push_back(std::move(v));
    }
    SisResult f;
    {
        std::lock_guard<std::mutex> g(mu_);
        for (auto& v : out)
            unsettled_.erase(std::remove_if(unsettled_.begin(), unsettled_.end(),
                                            [&](const Unsettled& u) { return u.burst == v.u.burst; }),
                             unsettled_.end());
        f = sis_result_;
    }
    for (auto& v : out) {
        note(v.u.msg_id, v.u.burst + ": no SIS answer is waited for (" + v.u.because +
                             "); settled from the station controller's status: " + v.how);
        if (f) f(v.u.burst, v.outcome, v.why);
    }
}

void GaleKey::set_sis_result(SisResult r) {
    std::lock_guard<std::mutex> g(mu_);
    sis_result_ = std::move(r);
}

std::string GaleKey::send_channel(const std::string& service, std::string* why) {
    // station-controller.md, status: "send_channel":{"S":"<LABEL>"|null}.
    GaleKeyReply r = status();
    if (!r.answered) {
        if (why) *why = "the station controller's status: " + no_frequency(r.reason);
        return "";
    }
    const JsonValue* sc = member(r.body, "send_channel");
    if (sc && sc->is_object())
        if (const JsonValue* c = sc->get(service); c && c->type == JsonValue::Type::String && !c->s.empty())
            return no_frequency(c->s);
    std::string w;
    if (const JsonValue* x = member(r.body, "waiting"); x && x->is_array())
        for (auto& e : x->arr)
            if (text_of(e, "service") == service && !text_of(e, "why").empty()) {
                w = no_frequency(text_of(e, "why"));
                break;
            }
    if (why) *why = w.empty() ? "no channel for service " + service : w;
    return "";
}

GaleKey::Prepared GaleKey::prepare_sis(const KeyItem& it) {
    Prepared p;
    const std::string path = wav_path(it.burst);
    std::string bytes;
    if (it.burst.empty() || !read_whole(path, bytes) || bytes.empty()) {
        p.error = (it.burst.empty() ? std::string("no frame") : it.burst) +
                  " is not in the outbox: nothing to prepare (a prepare is only for a frame already written)";
        return p;
    }
    // Annex A A.2.1.3: no U_PDU over the MTU. Refused before the y.
    if (bytes.size() > kSisMtu) {
        p.error = "too long for one burst: " + std::to_string(bytes.size()) + " bytes, the station controller takes " +
                  std::to_string(kSisMtu);
        return p;
    }
    std::string why;
    const std::string ch = send_channel(it.service, &why);
    if (ch.empty()) {
        p.error = "not prepared: " + why;
        return p;
    }
    KeyItem item = it;
    item.channel = ch;
    JsonWriter w;
    w.begin_object()
        .field("kind", "air")
        .field("service", it.service)
        .field("world", it.service)
        .field("channel", ch)
        .field("id", it.station_id)
        .field("msg_id", it.msg_id)
        .field("burst", it.burst)
        .end_object();
    Held h;
    h.kind = "air";
    h.item = item;
    h.wav = path;
    h.sha256 = sha256_hex(bytes);
    h.conn = KeyConnection::current();
    {
        std::lock_guard<std::mutex> g(mu_);
        drop_expired();
        for (auto x = held_.begin(); x != held_.end();) {
            if (x->second.kind == "air" && x->second.item.burst == it.burst) x = held_.erase(x);
            else ++x;
        }
        h.id = new_id();
        h.at = clock_();
        h.until = h.at + kGaleKeyTokenS;
        held_[h.id] = h;
    }
    p.ok = true;
    p.prepare_id = h.id;
    p.expires_s = static_cast<int>(kGaleKeyTokenS);
    p.confirm = w.str();
    note(it.msg_id, it.burst + " (" + it.service + ") on " + ch + " waits for the operator's y");
    return p;
}

GaleKeyReply GaleKey::submit_sis(const KeyItem& it, bool automatic, const std::string& sha256) {
    GaleKeyReply r;
    // Read once. These bytes are checked against the y's sha256, and these
    // bytes are submitted: the file is not read again in between.
    std::string bytes;
    if (!read_whole(wav_path(it.burst), bytes) || bytes.empty()) {
        r.reason = it.burst + " is no longer in the outbox";
        return r;
    }
    const std::string digest = sha256_hex(bytes);
    if (!sha256.empty() && digest != sha256) {
        r.reason = it.burst + " changed since the y dialog opened (its sha256 differs): not keyed; open it again";
        return r;
    }
    SisSend s;
    s.service = it.service;
    s.burst = it.burst;
    s.msg_id = it.msg_id;
    s.frame = bytes;
    s.sha256 = digest;
    s.automatic = automatic;
    // The 5066 priority from the precedence the frame's header carries (Z 15,
    // O 10, P 5, R 0); a plain frame, with no header, goes at 0.
    {
        FrameParser fp;
        auto frames = fp.feed(bytes);
        s.priority = frames.size() == 1 ? s5066::priority_for(frames.front().prec) : 0;
    }
    std::string why;
    if (!sis_->submit(s, &why)) {
        r.reason = why;
        return r;
    }
    // Handed down: only its SIS answer decides what became of it.
    r.reached = r.answered = r.ok = r.accepted = true;
    return r;
}

void GaleKey::on_sis(const std::string& burst, const std::string& sha256, bool confirmed, const std::string& why) {
    // The controller's sentence is in status.last under the frame's sha256,
    // because SIS carries only a reason code (station-controller.md, part 3).
    std::string said = confirmed ? std::string() : why, channel;
    GaleKeyReply st = status();
    if (const JsonValue* last = member(st.body, "last"); st.answered && last && last->is_array())
        for (auto& e : last->arr)
            if (text_of(e, "sha256") == sha256) {
                channel = no_frequency(text_of(e, "channel"));
                if (!confirmed && !text_of(e, "why").empty()) said = no_frequency(text_of(e, "why"));
                break;
            }
    SisResult f;
    {
        std::lock_guard<std::mutex> g(mu_);
        f = sis_result_;
    }
    if (f) f(burst, confirmed ? SisOutcome::keyed : SisOutcome::refused, confirmed ? channel : said);
}

}  // namespace pna
