#include "tuner.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace pna {

namespace {

constexpr const char* kProtocol = "ldg-tuner/1";
constexpr long long kMinHz = 1800000, kMaxHz = 54000000;  // the tuner's range (5.3)

TunerReply not_reached(const std::string& why) {
    TunerReply r;
    r.code = "no-tuner";
    r.reason = why;
    r.result = "{}";
    r.tuner = "null";
    return r;
}

TunerReply bad_line(const std::string& code, const std::string& why) {
    TunerReply r;
    r.reached = true;
    r.code = code;
    r.reason = why;
    r.result = "{}";
    r.tuner = "null";
    return r;
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

bool digits(const std::string& s) {
    return !s.empty() && s.size() <= 9 && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string args_of(std::initializer_list<std::pair<const char*, std::string>> kv) {
    // Values are JSON already (numbers, or quoted strings).
    std::string s = "{";
    bool first = true;
    for (auto& [k, v] : kv) {
        s += (first ? "\"" : ",\"") + std::string(k) + "\":" + v;
        first = false;
    }
    return s + "}";
}

std::string quoted(const std::string& s) { return "\"" + json_escape(s) + "\""; }

}  // namespace

std::string json_text(const JsonValue& v) {
    switch (v.type) {
        case JsonValue::Type::Null: return "null";
        case JsonValue::Type::Bool: return v.b ? "true" : "false";
        case JsonValue::Type::Number: {
            if (std::isfinite(v.n) && std::fabs(v.n) < 9007199254740992.0 && v.n == std::floor(v.n))
                return std::to_string(static_cast<long long>(v.n));
            if (!std::isfinite(v.n)) return "null";
            char b[40];
            std::snprintf(b, sizeof b, "%.17g", v.n);
            return b;
        }
        case JsonValue::Type::String: return quoted(v.s);
        case JsonValue::Type::Array: {
            std::string s = "[";
            for (size_t i = 0; i < v.arr.size(); ++i) s += (i ? "," : "") + json_text(v.arr[i]);
            return s + "]";
        }
        case JsonValue::Type::Object: {
            std::string s = "{";
            bool first = true;
            for (auto& [k, x] : v.obj) {
                s += (first ? "" : ",") + quoted(k) + ":" + json_text(x);
                first = false;
            }
            return s + "}";
        }
    }
    return "null";
}

TunerReply parse_tuner_reply(const std::string& line) {
    JsonValue v;
    if (!parse_json(line, v) || !v.is_object()) return bad_line("malformed", "the tuner daemon's reply isn't a JSON object");
    const JsonValue* proto = v.get("protocol");
    if (!proto || proto->type != JsonValue::Type::String || proto->s != kProtocol)
        return bad_line("malformed", std::string("the tuner daemon doesn't speak ") + kProtocol);
    const JsonValue* ok = v.get("ok");
    if (!ok || ok->type != JsonValue::Type::Bool) return bad_line("malformed", "the tuner daemon's reply has no ok");
    TunerReply r;
    r.reached = true;
    r.valid = true;
    r.ok = ok->b;
    const JsonValue* res = v.get("result");
    r.result = res && res->is_object() ? json_text(*res) : "{}";
    const JsonValue* tb = v.get("tuner");
    r.tuner = tb && tb->is_object() ? json_text(*tb) : "null";
    if (tb && tb->is_object()) {
        const JsonValue* rev = tb->get("contract_rev");
        if (rev && rev->type == JsonValue::Type::Number) r.contract_rev = static_cast<int>(rev->n);
    }
    if (!r.ok) {
        const JsonValue* e = v.get("error");
        const JsonValue* c = e && e->is_object() ? e->get("code") : nullptr;
        const JsonValue* why = e && e->is_object() ? e->get("reason") : nullptr;
        r.code = c && c->type == JsonValue::Type::String ? c->s : "malformed";
        r.reason = why && why->type == JsonValue::Type::String ? why->s : "the tuner daemon gave no reason";
    }
    return r;
}

TunerReply TunerSocket::call(const std::string& cmd, const std::string& args_json, int timeout_ms) {
    // Each call has its own connection; only the id is shared. So a status
    // read isn't held up behind a tune that takes its time.
    long long id;
    {
        std::lock_guard<std::mutex> g(mu_);
        id = next_id_++;
    }
    sockaddr_un addr{};
    if (path_.empty() || path_.size() >= sizeof(addr.sun_path)) return not_reached("bad tuner socket path");
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return not_reached(std::string("socket: ") + std::strerror(errno));
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path_.c_str(), path_.size());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        // Nothing listening: there is no tuner daemon.
        std::string why = errno == ENOENT || errno == ECONNREFUSED
                              ? "no tuner daemon is answering on its socket"
                              : std::string("connect: ") + std::strerror(errno);
        ::close(fd);
        return not_reached(why);
    }
    std::string line = "{\"id\":" + std::to_string(id) + ",\"cmd\":" + quoted(cmd);
    if (!args_json.empty()) line += ",\"args\":" + args_json;
    line += "}\n";
    // From here the daemon is there: a failure is a reply we couldn't get.
    if (::send(fd, line.data(), line.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(line.size())) {
        ::close(fd);
        return bad_line("io", "the request didn't reach the tuner daemon");
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
    if (nl == std::string::npos) return bad_line("timeout", "the tuner daemon didn't answer in time");
    std::string reply = buf.substr(0, nl);
    if (!reply.empty() && reply.back() == '\r') reply.pop_back();
    return parse_tuner_reply(reply);
}

// ------------------------------------------------------------ the sequence

TunerKeying::TunerKeying(std::shared_ptr<TunerLink> link, TunerTiming t) : link_(std::move(link)), t_(t) {}

void TunerKeying::set_pinned(bool pinned) {
    std::lock_guard<std::mutex> g(mu_);
    pinned_ = pinned;
}
bool TunerKeying::pinned() const {
    std::lock_guard<std::mutex> g(mu_);
    return pinned_;
}
void TunerKeying::set_keyer(std::shared_ptr<Keyer> k) {
    std::lock_guard<std::mutex> g(mu_);
    keyer_ = std::move(k);
}
void TunerKeying::set_log(std::function<void(const std::string&)> log) {
    std::lock_guard<std::mutex> g(mu_);
    log_ = std::move(log);
}
std::string TunerKeying::current_channel() const {
    std::lock_guard<std::mutex> g(mu_);
    return channel_;
}

std::string TunerKeying::status_block() {
    if (!link_) return "";
    std::lock_guard<std::mutex> g(status_mu_);
    auto now = std::chrono::steady_clock::now();
    if (status_read_ && now - status_at_ < std::chrono::milliseconds(t_.status_max_age_ms)) return status_;
    TunerReply r = link_->call("tuner_status", "", t_.status_ms);
    status_ = r.reached && r.valid && r.tuner != "null" && !r.tuner.empty() ? r.tuner : "";
    status_at_ = std::chrono::steady_clock::now();
    status_read_ = true;
    return status_;
}

void TunerKeying::log(const std::string& s) {
    std::function<void(const std::string&)> f;
    {
        std::lock_guard<std::mutex> g(mu_);
        f = log_;
    }
    if (f) f("tuner: " + s);
}

TunerReply TunerKeying::call(const std::string& cmd, const std::string& args) {
    if (!link_) return not_reached("no tuner is set up");
    return link_->call(cmd, args, t_.call_ms);
}

static bool keying_allowed(const Keyer& k, TunerOutcome& o) {
    std::string m = k.method();
    if (m == "cat" || m == "ptt") return true;
    o.code = "refused-keying-method";
    o.reason = "keying is by CAT or a hardware PTT line only; VOX is forbidden (this keyer says " +
               (m.empty() ? std::string("nothing") : m) + ")";
    return false;
}

TunerOutcome TunerKeying::channel(const Channel& c) {
    std::lock_guard<std::mutex> seq(seq_mu_);
    TunerOutcome o;
    {
        std::lock_guard<std::mutex> g(mu_);
        channel_ = c.name;
    }
    if (!link_) {
        o.ok = true;
        o.steps.push_back("no tuner");
        return o;
    }
    if (c.hz < kMinHz || c.hz > kMaxHz) {
        o.code = "refused-range";
        o.reason = "channel " + c.name + " is outside the tuner's range (1.8 to 54 MHz): the tuner wasn't told";
        log(o.reason);
        return o;
    }
    int rx = c.ant_rx == 2 ? 2 : 1, tx = c.ant_tx == 2 ? 2 : 1;
    o.last = call("tuner_channel", args_of({{"freq_hz", std::to_string(c.hz)},
                                            {"ant_rx", std::to_string(rx)},
                                            {"ant_tx", std::to_string(tx)}}));
    if (!o.last.reached) {
        o.ok = true;  // no daemon: nothing to tell
        o.steps.push_back("no tuner");
        log("channel " + c.name + ": " + o.last.reason);
        return o;
    }
    o.tuner = true;
    o.ok = o.last.ok;
    o.code = o.last.code;
    o.reason = o.last.reason;
    o.steps.push_back(std::string("tuner_channel ") + (o.ok ? "ok" : o.code));
    log("channel " + c.name + " (RX antenna " + std::to_string(rx) + ", TX antenna " + std::to_string(tx) + "): " +
        (o.ok ? "ok" : o.code + ": " + o.reason));
    return o;
}

TunerOutcome TunerKeying::post_tx(Keyer* k) {
    if (!k) return post_tx_when(nullptr, 0, "the keyer");
    return post_tx_when([k] { return k->idle(); }, t_.idle_wait_ms, "the keyer");
}

TunerOutcome TunerKeying::post_tx_when(const std::function<bool()>& idle, int wait_ms, const char* what) {
    TunerOutcome o;
    if (!link_) {
        o.ok = true;
        return o;
    }
    // A keying process is "keyed" to the daemon until it and its watchdog
    // have exited (5.7): wait for that first. idle() may itself take a while
    // (gale-key's status waits for its key), so the wait is by the clock.
    if (idle) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
        bool done = idle();
        while (!done && std::chrono::steady_clock::now() < end) {
            sleep_ms(t_.idle_poll_ms);
            done = idle();
        }
        if (!done)
            log(std::string(what) + " hasn't gone idle after " + std::to_string(wait_ms) + " ms; asking anyway");
    }
    int backoff = t_.post_first_ms;
    for (int i = 1; i <= t_.post_tries; ++i) {
        o.last = call("tuner_post_tx");
        if (!o.last.reached) {
            // No daemon now: nothing to switch back.
            o.ok = true;
            o.steps.push_back("tuner_post_tx: no tuner");
            log("tuner_post_tx: " + o.last.reason);
            return o;
        }
        o.tuner = true;
        if (o.last.ok) {
            o.ok = true;
            o.code.clear();
            o.reason.clear();
            o.steps.push_back("tuner_post_tx ok");
            if (i > 1) log("tuner_post_tx ok on try " + std::to_string(i));
            return o;
        }
        o.code = o.last.code;
        o.reason = o.last.reason;
        o.steps.push_back("tuner_post_tx " + o.code);
        bool again = o.code == "refused-keyed" || o.code == "busy";
        if (!again || i == t_.post_tries) break;
        log("tuner_post_tx " + o.code + " (try " + std::to_string(i) + " of " + std::to_string(t_.post_tries) +
            "): " + o.reason + "; again in " + std::to_string(backoff) + " ms");
        sleep_ms(backoff);
        backoff = std::min(backoff * 2, t_.post_max_ms);
    }
    log("tuner_post_tx not accepted (" + o.code + ": " + o.reason +
        "): the receiver may still be on the TX antenna");
    return o;
}

TunerOutcome TunerKeying::around_key(const std::function<void()>& key, const std::function<bool()>& ended,
                                     bool* asked) {
    std::lock_guard<std::mutex> seq(seq_mu_);
    TunerOutcome o;
    if (asked) *asked = false;
    bool tuner = false;
    if (link_) {
        o.last = call("tuner_pre_tx");
        if (o.last.reached) {
            tuner = true;
            o.tuner = true;
            o.steps.push_back(std::string("tuner_pre_tx ") + (o.last.ok ? "ok" : o.last.code));
            if (!o.last.ok) {
                // The antenna for this key is unknown: gale-key isn't asked.
                o.code = o.last.code;
                o.reason = o.last.reason;
                log("tuner_pre_tx " + o.code + ": " + o.reason + "; gale-key not asked to key");
                return o;
            }
        } else {
            o.steps.push_back("no tuner");
            log("tuner_pre_tx: " + o.last.reason + "; gale-key asked as without a tuner");
        }
    } else {
        o.steps.push_back("no tuner");
    }
    o.steps.push_back("key");
    if (asked) *asked = true;
    key();
    o.ok = true;
    if (tuner) {
        TunerOutcome p = post_tx_when(ended, t_.key_end_wait_ms, "gale-key's key");
        for (auto& s : p.steps) o.steps.push_back(s);
        o.last = p.last;
        if (!p.ok) {
            o.ok = false;
            o.code = p.code;
            o.reason = "the tuner didn't go back to the RX antenna after gale-key's key: " + p.reason;
        }
    }
    return o;
}

TunerOutcome TunerKeying::transmit(bool emcon) {
    std::shared_ptr<Keyer> k;
    {
        std::lock_guard<std::mutex> g(mu_);
        k = keyer_;
    }
    if (!k) {
        TunerOutcome o;
        o.code = "no-keyer";
        o.reason = "this box keys nothing: transmit is pinned to gale-tx/WAV";
        return o;
    }
    return transmit(*k, emcon);
}

TunerOutcome TunerKeying::transmit(Keyer& k, bool emcon) {
    std::lock_guard<std::mutex> seq(seq_mu_);
    TunerOutcome o;
    if (!keying_allowed(k, o)) return o;
    if (emcon) {
        o.code = "refused-emcon";
        o.reason = "EMCON is on: nothing goes on the air";
        return o;
    }
    std::string err;
    if (link_) {
        o.last = call("tuner_pre_tx");
        if (o.last.reached) {
            o.tuner = true;
            o.steps.push_back(std::string("tuner_pre_tx ") + (o.last.ok ? "ok" : o.last.code));
            if (!o.last.ok) {
                // The antenna for this transmit is unknown: don't key.
                o.code = o.last.code;
                o.reason = o.last.reason;
                log("tuner_pre_tx " + o.code + ": " + o.reason + "; not keyed");
                return o;
            }
        } else {
            o.steps.push_back("no tuner");
            log("tuner_pre_tx: " + o.last.reason + "; keying as without a tuner");
        }
    } else {
        o.steps.push_back("no tuner");
    }
    o.steps.push_back("key");
    bool sent = k.transmit(&err);
    o.steps.push_back(sent ? "unkeyed" : "transmit failed");
    if (o.tuner) {
        TunerOutcome p = post_tx(&k);
        for (auto& s : p.steps) o.steps.push_back(s);
        if (!p.ok) {
            o.code = p.code;
            o.reason = "sent, but the tuner didn't go back to the RX antenna: " + p.reason;
            o.last = p.last;
            o.ok = false;
            return o;
        }
        o.last = p.last;
    }
    o.ok = sent;
    if (!sent) {
        o.code = "transmit-failed";
        o.reason = err.empty() ? "the transmit failed" : err;
    }
    return o;
}

TunerOutcome TunerKeying::tune(const std::string& kind, const std::string& confirm, int watts, bool store, bool emcon) {
    TunerOutcome o;
    if (!link_) {
        o.code = "no-tuner";
        o.reason = "no tuner is set up on this box";
        return o;
    }
    std::shared_ptr<Keyer> k;
    bool pinned;
    {
        std::lock_guard<std::mutex> g(mu_);
        k = keyer_;
        pinned = pinned_;
    }
    if (pinned) {
        o.code = "refused-tx-locked";
        o.reason = "TX is locked: transmit is pinned to gale-tx/WAV, and a tune is a transmit";
        return o;
    }
    if (emcon) {
        o.code = "refused-emcon";
        o.reason = "EMCON is on: a tune is a transmit";
        return o;
    }
    if (confirm != "Y") {
        o.code = "refused-confirm";
        o.reason = "a tune needs the operator's keyboard y (confirm Y)";
        return o;
    }
    if (kind != "memory" && kind != "full") {
        o.code = "refused-args";
        o.reason = "kind: memory or full";
        return o;
    }
    if (watts < 1 || watts > kMaxTuneWatts) {
        o.code = "refused-power";
        o.reason = "a tune runs at 1 to " + std::to_string(kMaxTuneWatts) + " W (FT-897D roll-back unconfirmed)";
        return o;
    }
    if (!k) {
        o.code = "no-keyer";
        o.reason = "this box keys nothing: transmit is pinned to gale-tx/WAV";
        return o;
    }
    std::lock_guard<std::mutex> seq(seq_mu_);
    return tune_with(*k, kind, confirm, watts, store);
}

TunerOutcome TunerKeying::tune_with(Keyer& k, const std::string& kind, const std::string& confirm, int watts,
                                    bool store) {
    TunerOutcome o;
    if (!keying_allowed(k, o)) return o;
    o.last = call("tuner_pre_tx");
    if (!o.last.reached) {
        o.code = "tuner-absent";
        o.reason = "no tuner daemon is answering: nothing to tune";
        return o;
    }
    o.tuner = true;
    o.steps.push_back(std::string("tuner_pre_tx ") + (o.last.ok ? "ok" : o.last.code));
    if (!o.last.ok) {
        o.code = o.last.code;
        o.reason = o.last.reason;
        log("tune: tuner_pre_tx " + o.code + ": " + o.reason + "; not keyed");
        return o;
    }
    TunerReply tuned, stored;
    bool did_store = false;
    std::string err;
    o.steps.push_back("carrier " + std::to_string(watts) + " W");
    bool up = k.carrier(watts, [&] {
        tuned = call("tuner_tune", args_of({{"kind", quoted(kind)}, {"confirm", quoted(confirm)}}));
        if (tuned.ok && store) {
            stored = call("tuner_store");
            did_store = true;
        }
    }, &err);
    o.steps.push_back(up ? "carrier off" : "carrier failed");
    if (up) {
        o.steps.push_back(std::string("tuner_tune ") + (tuned.ok ? "ok" : tuned.code));
        if (did_store) o.steps.push_back(std::string("tuner_store ") + (stored.ok ? "ok" : stored.code));
    }
    TunerOutcome p = post_tx(&k);
    for (auto& s : p.steps) o.steps.push_back(s);
    if (!up) {
        o.code = "transmit-failed";
        o.reason = err.empty() ? "the tune carrier didn't come up" : err;
        o.last = p.last.reached ? p.last : o.last;
        return o;
    }
    o.last = did_store ? stored : tuned;
    if (!tuned.ok) {
        o.code = tuned.code;
        o.reason = tuned.reason;
    } else if (did_store && !stored.ok) {
        o.code = stored.code;
        o.reason = "tuned, but the store failed: " + stored.reason;
    } else if (!p.ok) {
        o.code = p.code;
        o.reason = "tuned, but the tuner didn't go back to the RX antenna: " + p.reason;
    } else {
        o.ok = true;
    }
    if (p.last.reached) o.last.tuner = p.last.tuner;  // the newest status block
    log("tune (" + kind + ", " + std::to_string(watts) + " W): " + (o.ok ? "ok" : o.code + ": " + o.reason));
    return o;
}

// ------------------------------------------------------------ the 4100 calls

namespace {

std::string reply_of(const std::string& cmd, const TunerReply& r) {
    JsonWriter w;
    w.begin_object().field("ok", r.ok).field("cmd", cmd).raw("result", r.result.empty() ? "{}" : r.result);
    if (!r.ok) w.field("code", r.code).field("error", r.reason);
    w.raw("tuner", r.tuner.empty() ? "null" : r.tuner).end_object();
    return w.str();
}

std::string reply_of(const std::string& cmd, const TunerOutcome& o, const std::string& channel = "") {
    JsonWriter w;
    w.begin_object().field("ok", o.ok).field("cmd", cmd);
    if (!channel.empty()) w.field("channel", channel);
    w.raw("result", o.last.result.empty() ? "{}" : o.last.result);
    if (!o.ok) w.field("code", o.code).field("error", o.reason);
    w.begin_array("steps");
    for (auto& s : o.steps) w.field("", s);
    w.end_array();
    w.raw("tuner", o.tuner && !o.last.tuner.empty() ? o.last.tuner : "null").end_object();
    return w.str();
}

std::string refuse(const std::string& cmd, const std::string& code, const std::string& why) {
    JsonWriter w;
    w.begin_object().field("ok", false).field("cmd", cmd).field("code", code).field("error", why).field_null("tuner").end_object();
    return w.str();
}

}  // namespace

std::optional<std::string> handle_tuner(TunerKeying* tk, const std::string& cmd,
                                        std::map<std::string, std::string>& req,
                                        const std::function<std::optional<Channel>(const std::string&)>& lookup,
                                        bool emcon) {
    static const char* kCmds[] = {"tuner_status", "tuner_channel", "tuner_recall", "tuner_bypass", "tuner_antenna",
                                  "tuner_set",    "tuner_tune",    "tuner_store",  "tuner_pre_tx", "tuner_post_tx"};
    if (std::none_of(std::begin(kCmds), std::end(kCmds), [&](const char* c) { return cmd == c; })) return std::nullopt;

    // A frequency never comes over this port: a channel goes by its designator.
    for (auto k : {"freq_hz", "frequency", "hz"})
        if (req.count(k)) return refuse(cmd, "refused-args", "name the channel by its designator (channel=P1), not a frequency");

    auto channel_of = [&](std::optional<Channel>& c) -> std::optional<std::string> {
        std::string name = req["channel"];
        if (name.empty()) name = req["name"];
        if (name.empty()) return refuse(cmd, "refused-args", "name the channel: channel=P1");
        c = lookup ? lookup(name) : std::nullopt;
        if (!c) return refuse(cmd, "refused-args", "no such channel in the plan");
        return std::nullopt;
    };

    // A channel change is recorded whether or not a tuner daemon is set up:
    // it is the channel gale-key keys on (gale_key.h). Only a daemon is told.
    if (cmd == "tuner_channel" && tk) {
        std::optional<Channel> c;
        if (auto bad = channel_of(c)) return bad;
        TunerOutcome o = tk->channel(*c);
        c->hz = 0;
        return reply_of(cmd, o, c->name);
    }

    if (!tk || !tk->configured()) {
        if (cmd == "tuner_status") {
            JsonWriter w;
            w.begin_object().field("ok", true).field("cmd", cmd).raw("result", "{}").field_null("tuner")
                .field("note", "no tuner is set up on this box (tuner-socket)").end_object();
            return w.str();
        }
        return refuse(cmd, "no-tuner", "no tuner is set up on this box (tuner-socket in the settings)");
    }

    if (cmd == "tuner_recall") {
        std::optional<Channel> c;
        if (auto bad = channel_of(c)) return bad;
        long long hz = c->hz;
        c->hz = 0;
        if (hz < kMinHz || hz > kMaxHz)
            return refuse(cmd, "refused-range", "channel " + c->name + " is outside the tuner's range (1.8 to 54 MHz)");
        return reply_of(cmd, tk->link()->call(cmd, args_of({{"freq_hz", std::to_string(hz)}}), 30000));
    }
    if (cmd == "tuner_tune") {
        std::string kind = req["kind"], confirm = req["confirm"], ws = req.count("watts") ? req["watts"] : "10";
        if (!digits(ws)) return refuse(cmd, "refused-power", "watts: a whole number, 1 to 25");
        std::string st = req.count("store") ? req["store"] : "Y";
        bool store = st == "Y" || st == "y" || st == "true" || st == "1";
        // confirm goes as the operator gave it: the core never makes up a Y.
        return reply_of(cmd, tk->tune(kind, confirm, std::stoi(ws), store, emcon));
    }
    if (cmd == "tuner_antenna") {
        std::string p = req["port"];
        if (p != "1" && p != "2") return refuse(cmd, "refused-args", "port: 1 or 2");
        return reply_of(cmd, tk->link()->call(cmd, args_of({{"port", p}}), 30000));
    }
    if (cmd == "tuner_set") {
        std::string L = req["L"], C = req["C"], hilo = req["hilo"];
        for (auto& c : hilo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!digits(L) || std::stoi(L) > 127 || !digits(C) || std::stoi(C) > 127)
            return refuse(cmd, "refused-args", "L and C: whole numbers, 0 to 127");
        if (hilo != "hi" && hilo != "lo") return refuse(cmd, "refused-args", "hilo: hi or lo");
        return reply_of(cmd, tk->link()->call(cmd, args_of({{"L", std::to_string(std::stoi(L))},
                                                           {"C", std::to_string(std::stoi(C))},
                                                           {"hilo", quoted(hilo)}}),
                                             30000));
    }
    // tuner_status, tuner_bypass, tuner_store, tuner_pre_tx, tuner_post_tx: no arguments.
    TunerReply r = tk->link()->call(cmd, "", cmd == "tuner_status" ? 3000 : 30000);
    if (!r.reached) return refuse(cmd, "no-tuner", r.reason);
    return reply_of(cmd, r);
}

}  // namespace pna
