#include "gale_key.h"

#include "compose.h"
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
    if (path_.empty() || path_.size() >= sizeof(addr.sun_path)) return not_reached("no usable gale-key socket path");
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return not_reached(std::string("socket: ") + std::strerror(errno));
    timeval tv{timeout_ms_ / 1000, (timeout_ms_ % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path_.c_str(), path_.size());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        std::string why = errno == ENOENT || errno == ECONNREFUSED
                              ? "gale-key isn't running (nothing answers on " + path_ + ")"
                              : "gale-key's socket: " + std::string(std::strerror(errno));
        ::close(fd);
        return not_reached(why);
    }
    const std::string line = request + "\n";
    // gale-key acts only on a whole line: a request cut short is never acted on.
    if (::send(fd, line.data(), line.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(line.size())) {
        ::close(fd);
        return not_reached("the request didn't reach gale-key");
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
        r.reason = "gale-key didn't answer within " + std::to_string(timeout_ms_) + " ms (busy keying, or stuck)";
        return r;
    }
    std::string reply = buf.substr(0, nl);
    if (!reply.empty() && reply.back() == '\r') reply.pop_back();
    if (!parse_json(reply, r.body) || !r.body.is_object()) {
        r.body = JsonValue();
        r.reason = "gale-key's reply isn't one JSON object";
        return r;
    }
    r.answered = true;
    const JsonValue* ok = r.body.get("ok");
    r.ok = ok && ok->type == JsonValue::Type::Bool && ok->b;
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
    }
    cv_.notify_all();
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
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

GaleKeyReply GaleKey::send_key(const KeyItem& it, const std::string& request) {
    Around a;
    {
        std::lock_guard<std::mutex> g(mu_);
        a = around_;
    }
    if (!a) return ask(request);
    return a(it, [this, &request] { return ask(request); });
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
    GaleKeyReply r = link_ ? link_->call(request) : not_reached("no gale-key socket is set (gale-key-socket)");
    std::lock_guard<std::mutex> g(mu_);
    // A key's reply comes when the key ends: a reply later than the timeout
    // is gale-key busy, not gale-key gone.
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

std::string GaleKey::air_request(const char* cmd, const KeyItem& it, const std::string& control) const {
    JsonWriter w;
    w.begin_object()
        .field("cmd", cmd)
        .field("kind", "air")
        .field("wav", wav_path(it.burst))
        .field("world", it.world)
        .field("channel", it.channel)
        .field("control", control)
        .field("final", false);
    return w.str();  // still open: the caller adds its last fields and closes it
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
    std::string req = air_request("prepare", it, "local");
    req += "}";
    GaleKeyReply r = ask(req);
    if (!r.answered) {
        p.error = "not prepared: " + no_frequency(r.reason);
        return p;
    }
    if (!r.ok) {
        p.error = "gale-key refused: " + no_frequency(r.reason.empty() ? "no reason given" : r.reason);
        return p;
    }
    const std::string token = text_of(r.body, "token");
    if (token.empty()) {
        p.error = "gale-key answered ok but gave no token: nothing to confirm";
        return p;
    }
    const JsonValue* c = member(r.body, "confirm");
    static const JsonValue none;
    const JsonValue& cf = c && c->is_object() ? *c : none;
    // The core's own channel label and world are what was asked for; gale-key's
    // summary must agree, or the y would confirm something else.
    const std::string cw = text_of(cf, "world"), cc = text_of(cf, "channel");
    if ((!cw.empty() && cw != it.world) || (!cc.empty() && cc != it.channel)) {
        p.error = "gale-key's summary names another world or channel than the core asked for: not held";
        return p;
    }
    // The summary for the kiosk: only fields it shows, only values it knows.
    JsonWriter w;
    w.begin_object().field("kind", "air").field("world", it.world).field("channel", no_frequency(it.channel));
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
    note(it.msg_id, "gale-key prepared " + it.burst + " on " + it.channel + " for the operator's y");
    return p;
}

GaleKey::Prepared GaleKey::prepare_standing(const std::string& mode) {
    Prepared p;
    if (!core_standing_mode(mode)) {
        p.error = "mode is machine, routing or auto_qsl (the core never asks for amateur_auto)";
        return p;
    }
    JsonWriter w0;
    w0.begin_object().field("cmd", "prepare").field("kind", "standing:" + mode).end_object();
    GaleKeyReply r = ask(w0.str());
    if (!r.answered) {
        p.error = "not prepared: " + no_frequency(r.reason);
        return p;
    }
    if (!r.ok) {
        p.error = "gale-key refused: " + no_frequency(r.reason.empty() ? "no reason given" : r.reason);
        return p;
    }
    const std::string token = text_of(r.body, "token");
    if (token.empty()) {
        p.error = "gale-key answered ok but gave no token: nothing to confirm";
        return p;
    }
    const JsonValue* c = member(r.body, "confirm");
    static const JsonValue none;
    const JsonValue& cf = c && c->is_object() ? *c : none;
    const std::string world = text_of(cf, "world");
    if (world != "mars") {
        p.error = "the station's transmit world is " + (world.empty() ? std::string("not given") : no_frequency(world)) +
                  ": the core turns on automatic sends in the MARS world only";
        return p;
    }
    JsonWriter w;
    w.begin_object().field("kind", "standing").field("mode", mode).field("world", world).end_object();
    double life = kGaleKeyTokenS;
    if (const JsonValue* e = member(r.body, "expires_s"); e && e->type == JsonValue::Type::Number && e->n > 0)
        life = std::min(life, e->n);
    Held h;
    h.kind = "standing";
    h.mode = mode;
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
    note("", "gale-key prepared standing " + mode + " for the operator's y");
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
    std::string req = air_request("key", h.item, "local");
    JsonWriter t;
    t.begin_object().field("token", h.token).end_object();
    req += "," + t.str().substr(1);  // ,"token":"..."}
    GaleKeyReply r = send_key(h.item, req);
    note(h.item.msg_id, "asked gale-key to key " + h.item.burst + " on the operator's y: " +
                            (r.answered ? (r.ok ? std::string("it answered ok") : "it answered " + no_frequency(r.reason))
                                        : no_frequency(r.reason)) +
                            "; its BURST.key.json decides");
    return r;
}

GaleKeyReply GaleKey::grant(const Held& h) {
    JsonWriter w;
    w.begin_object().field("cmd", "grant").field("mode", h.mode).field("token", h.token).end_object();
    GaleKeyReply r = ask(w.str());
    if (r.answered && r.ok) {
        std::lock_guard<std::mutex> g(mu_);
        standing_[h.mode] = true;
    }
    note("", r.answered && r.ok ? "standing " + h.mode + " ON: gale-key granted it on the operator's y"
                                : "standing " + h.mode + " not granted: " + no_frequency(r.reason));
    return r;
}

GaleKeyReply GaleKey::revoke(const std::string& mode, const std::string& why) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (mode == "all") standing_.clear();
        else standing_.erase(mode);
    }
    JsonWriter w;
    w.begin_object().field("cmd", "revoke").field("mode", mode).end_object();
    GaleKeyReply r = ask(w.str());
    note("", "standing " + mode + " OFF (" + why + "); gale-key " +
                 (r.answered ? (r.ok ? std::string("revoked it") : "answered " + no_frequency(r.reason))
                             : no_frequency(r.reason)));
    return r;
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
    std::string req = air_request("key", it, "automatic");
    JsonWriter t;
    t.begin_object().field("standing", mode).end_object();
    req += "," + t.str().substr(1);
    GaleKeyReply r = send_key(it, req);
    // gale-key revokes grants itself (the hourly cap, a change of transmit
    // world). Told it holds none for this mode, the core turns the mode off
    // too, so its next bursts wait for the operator's y instead of failing.
    if (gale_key_holds_no_grant(r, mode)) {
        {
            std::lock_guard<std::mutex> g(mu_);
            standing_.erase(mode);
        }
        note("", "standing " + mode + " OFF: gale-key holds no grant for it any more; the operator's y turns it on again");
    }
    note(it.msg_id, "asked gale-key to key " + it.burst + " on " + it.channel + " under standing " + mode + ": " +
                        (r.answered ? (r.ok ? std::string("it answered ok") : "it answered " + no_frequency(r.reason))
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
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) return;
            j = std::move(jobs_.front());
            jobs_.pop_front();
        }
        if (j.claim && !j.claim(j.item)) continue;
        GaleKeyReply r = key_standing_now(j.item, j.mode);
        if (j.done) j.done(j.item, r);
    }
}

GaleKeyReply GaleKey::status() { return ask(R"({"cmd":"status"})"); }

}  // namespace pna
