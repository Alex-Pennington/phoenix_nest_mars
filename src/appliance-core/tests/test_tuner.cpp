// The LDG tuner through the core (src/tuner.h; greybeard-ale docs/ldg-tuner.md 5.7).
//
// - each channel's antenna ports: the plan file, the CHANNEL form, channel_put;
// - forwarding on 4100: what goes to the daemon and what comes back;
// - the sequence around keying, against a stub daemon and a stub keyer:
//   pre_tx before the key and post_tx after, a pre_tx refusal that blocks the
//   key, post_tx retried, a tune refused while transmit is pinned, the 25 W
//   ceiling, no VOX, and keying as before with no tuner;
// - the same sequence around a key the core asks of gale-key (around_key);
// - tuner_channel recorded with no daemon set up.
//
// The frequencies are public time-signal ones. No hardware is touched.
#include "../src/api_server.h"
#include "../src/channels.h"
#include "../src/core.h"
#include "../src/forms.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/tuner.h"
#include "check.h"
#include "services_fixture.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

// ---------------------------------------------------------------- helpers

static JsonValue parsed(const std::string& s) {
    JsonValue v;
    parse_json(s, v);
    return v;
}
static bool okv(const JsonValue& v) {
    const JsonValue* o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}
static std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static double num(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::Number ? x->n : -1;
}
static const JsonValue& sub(const JsonValue& v, const char* k) {
    static JsonValue none;
    const JsonValue* x = v.get(k);
    return x ? *x : none;
}
static std::vector<std::string> steps_of(const JsonValue& v) {
    std::vector<std::string> out;
    for (auto& s : sub(v, "steps").arr) out.push_back(s.s);
    return out;
}
static bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

// One event list for the stub daemon and the stub keyer, so order shows.
static std::vector<std::string> g_events;
static std::mutex g_mu;
static void ev(const std::string& e) {
    std::lock_guard<std::mutex> g(g_mu);
    g_events.push_back(e);
}
static std::vector<std::string> events() {
    std::lock_guard<std::mutex> g(g_mu);
    return g_events;
}
static void clear_events() {
    std::lock_guard<std::mutex> g(g_mu);
    g_events.clear();
}

// A status block shaped like the daemon's (5.4), cut to what the core passes on.
static std::string block(int antenna, int ant_rx = 1, int ant_tx = 2) {
    return "{\"protocol\":\"ldg-tuner/1\",\"contract_rev\":2,\"channel\":{\"ant_rx\":" + std::to_string(ant_rx) +
           ",\"ant_tx\":" + std::to_string(ant_tx) + ",\"recalled_on\":" + std::to_string(antenna) +
           "},\"present\":true,\"keyed\":false,\"tx_locked\":true,\"relays\":{\"L\":30,\"C\":40,\"hilo\":\"lo\",\"antenna\":" +
           std::to_string(antenna) + ",\"bypass\":false,\"source\":\"set\"},\"meter\":{\"fwd_w\":0.0,\"ref_w\":0.0}}";
}
static std::string ok_line(const std::string& cmd, const std::string& result, int antenna = 1) {
    return "{\"protocol\":\"ldg-tuner/1\",\"id\":1,\"ok\":true,\"cmd\":\"" + cmd + "\",\"result\":" + result +
           ",\"tuner\":" + block(antenna) + "}";
}
static std::string err_line(const std::string& cmd, const std::string& code, const std::string& reason) {
    return "{\"protocol\":\"ldg-tuner/1\",\"id\":1,\"ok\":false,\"cmd\":\"" + cmd + "\",\"error\":{\"code\":\"" + code +
           "\",\"reason\":\"" + reason + "\"},\"tuner\":" + block(2) + "}";
}

// The stub daemon: scripted replies per command, every request recorded.
struct StubTuner : TunerLink {
    std::map<std::string, std::deque<std::string>> scripted;  // cmd -> reply lines, used in turn
    std::vector<std::pair<std::string, std::string>> sent;   // cmd, args
    bool absent = false;                                     // nothing listening
    TunerReply call(const std::string& cmd, const std::string& args, int) override {
        sent.push_back({cmd, args});
        if (absent) {
            TunerReply r;
            r.code = "no-tuner";
            r.reason = "no tuner daemon is answering on its socket";
            return r;
        }
        ev(cmd);
        auto& q = scripted[cmd];
        std::string line = ok_line(cmd, "{}");
        if (!q.empty()) {
            line = q.front();
            if (q.size() > 1) q.pop_front();
        }
        return parse_tuner_reply(line);
    }
    std::vector<std::string> cmds() const {
        std::vector<std::string> out;
        for (auto& [c, a] : sent) out.push_back(c);
        return out;
    }
};

struct StubKeyer : Keyer {
    std::string how = "cat";
    bool ok = true;
    int busy_polls = 0;  // idle() says no this many times after a transmit
    std::atomic<int> left{0};
    int carrier_w = 0;
    std::string method() const override { return how; }
    bool transmit(std::string* error) override {
        ev("key");
        ev("unkey");
        left = busy_polls;
        if (!ok && error) *error = "the burst didn't play";
        return ok;
    }
    bool carrier(int watts, const std::function<void()>& while_on, std::string* error) override {
        carrier_w = watts;
        ev("carrier " + std::to_string(watts));
        if (!ok) {
            if (error) *error = "no carrier";
            return false;
        }
        while_on();
        ev("carrier off");
        left = busy_polls;
        return true;
    }
    bool idle() override {
        if (left > 0) {
            --left;
            return false;
        }
        return true;
    }
};

static TunerTiming fast() {
    TunerTiming t;
    t.idle_wait_ms = 500;
    t.idle_poll_ms = 1;
    t.post_first_ms = 1;
    t.post_max_ms = 4;
    return t;
}

static Channel chan(const std::string& name, int64_t hz, int rx, int tx) {
    Channel c;
    c.name = name;
    c.hz = hz;
    c.mode = "USB";
    c.ant_rx = rx;
    c.ant_tx = tx;
    return c;
}

// ---------------------------------------------------------------- the channel plan

static void plan_file(const fs::path& dir) {
    fs::path p = dir / "STA-A.channels";
    // A plan written before the antenna fields: each channel reads as 1 and 1.
    {
        std::ofstream f(p);
        f << "PNA CHANNEL PLAN 1\nP1|5000000|USB|PRIMARY|card\nP2|10000000|USB|SECONDARY|card\n";
    }
    ChannelPlan plan(p.string());
    std::string err;
    CHECK(plan.load(&err));
    CHECK(plan.get("P1") && plan.get("P1")->ant_rx == 1 && plan.get("P1")->ant_tx == 1);
    // Put with ports given: kept, written and read back.
    CHECK(plan.put(chan("P1", 5000000, 1, 2), &err));
    // Put with ports not given (0): what was held stays.
    Channel again = chan("P1", 5000000, 0, 0);
    again.use = "PRIMARY";
    CHECK(plan.put(again, &err));
    CHECK(plan.get("P1")->ant_rx == 1 && plan.get("P1")->ant_tx == 2);
    // A fresh card import (put_all, no ports) keeps the ports too.
    std::vector<std::string> errs;
    CHECK(plan.put_all({chan("P1", 5000000, 0, 0), chan("S1", 2500000, 0, 0)}, &errs) && errs.empty());
    CHECK(plan.get("P1")->ant_tx == 2 && plan.get("S1")->ant_rx == 1 && plan.get("S1")->ant_tx == 1);
    // Anything other than 1 or 2 is refused.
    CHECK(!plan.put(chan("P2", 10000000, 3, 1), &err) && contains(err, "1 or 2"));
    CHECK(!plan.put(chan("P2", 10000000, 1, -1), &err));
    ChannelPlan reread(p.string());
    CHECK(reread.load(&err));
    CHECK(reread.get("P1")->ant_rx == 1 && reread.get("P1")->ant_tx == 2 && reread.get("P2")->ant_tx == 1);
}

// ---------------------------------------------------------------- the CHANNEL form

static JsonValue api(Core& c, const std::string& line) { return parsed(ApiServer::handle(c, line)); }

static const JsonValue* field_of(const JsonValue& form, const std::string& key) {
    for (auto& f : sub(form, "fields").arr)
        if (str(f, "key") == key) return &f;
    return nullptr;
}

static void channel_form(const fs::path& dir) {
    // The template itself: two choices, 1 or 2, no default (a blank port is
    // not sent, so the channel keeps the one it holds), with guide text.
    forms::Template t;
    std::string err;
    bool found = false;
    for (auto& [id, text] : forms::builtin_texts())
        if (id == "CHANNEL") {
            found = forms::parse(text, t, &err);
        }
    CHECK(found);
    for (auto key : {"ant_rx", "ant_tx"}) {
        auto f = std::find_if(t.fields.begin(), t.fields.end(), [&](const forms::Field& x) { return x.key == key; });
        CHECK(f != t.fields.end());
        if (f == t.fields.end()) continue;
        CHECK(f->kind == "choice" && f->choices.size() == 2 && f->choices[0].value == "1" && f->choices[1].value == "2");
        CHECK(f->def.empty() && !f->required && !f->fill);
        CHECK(contains(f->help, "1 or 2") && contains(f->help, "RF off") &&
              contains(f->help, "Blank keeps the port the channel has (1 for a new one)"));
    }

    auto core = std::make_unique<Core>(CoreConfig{"STA-A", (dir / "STA-A.db").string(), 3, 1, true},
                                       std::make_unique<LoopbackModem>(0));
    core->set_services(test_services());
    auto ops = std::make_shared<Ops>("STA-A", (dir / "STA-A.db").string());
    core->set_ops(ops);

    // The form as drawn: both fields.
    JsonValue r = api(*core, R"({"cmd":"form_get","form":"CHANNEL"})");
    CHECK(okv(r) && field_of(r, "ant_rx") && field_of(r, "ant_tx"));
    if (field_of(r, "ant_tx")) CHECK(contains(str(*field_of(r, "ant_tx"), "help"), "Tunes and tuner memories"));

    // Left out on a channel the plan holds (plan_file left P1 on RX 1, TX 2):
    // kept. Left out on a new channel: 1 and 1.
    CHECK(ops->channels().get("P1") && ops->channels().get("P1")->ant_tx == 2);
    r = api(*core, R"({"cmd":"form_submit","form":"CHANNEL","name":"P1","frequency":"5000"})");
    CHECK(okv(r));
    CHECK(ops->channels().get("P1") && ops->channels().get("P1")->ant_rx == 1 && ops->channels().get("P1")->ant_tx == 2);
    r = api(*core, R"({"cmd":"form_submit","form":"CHANNEL","name":"P5","frequency":"7000"})");
    CHECK(okv(r));
    CHECK(ops->channels().get("P5") && ops->channels().get("P5")->ant_rx == 1 && ops->channels().get("P5")->ant_tx == 1);
    // RX on 1, TX on 1, then TX on 2.
    r = api(*core, R"({"cmd":"form_submit","form":"CHANNEL","name":"P1","frequency":"5000","ant_rx":"1","ant_tx":"1"})");
    CHECK(okv(r) && num(sub(r, "result"), "ant_tx") == 1 && str(sub(r, "result"), "result") == "replaced");
    r = api(*core, R"({"cmd":"form_submit","form":"CHANNEL","name":"P1","frequency":"5000","ant_rx":"1","ant_tx":"2"})");
    CHECK(okv(r) && num(sub(r, "result"), "ant_tx") == 2 && str(sub(r, "result"), "result") == "replaced");
    CHECK(ops->channels().get("P1")->ant_tx == 2);
    // Anything but 1 or 2: the form names the field and changes nothing.
    for (auto bad : {"3", "0", "A", "12", "-1"}) {
        r = api(*core, std::string(R"({"cmd":"form_submit","form":"CHANNEL","name":"P1","frequency":"5000","ant_tx":")") +
                           bad + "\"}");
        CHECK(!okv(r) && !sub(r, "problems").arr.empty() && str(sub(r, "problems").arr[0], "field") == "ant_tx");
    }
    CHECK(ops->channels().get("P1")->ant_tx == 2);
    // Blank on the form keeps the port held.
    r = api(*core, R"({"cmd":"form_submit","form":"CHANNEL","name":"P1","frequency":"5000","ant_rx":"","ant_tx":""})");
    CHECK(okv(r) && ops->channels().get("P1")->ant_tx == 2 && ops->channels().get("P1")->ant_rx == 1);
    // Opening the channel again shows its ports, never its frequency.
    api(*core, R"({"cmd":"channel_put","name":"P2","frequency":"10000","ant_rx":"2","ant_tx":"2"})");
    r = api(*core, R"({"cmd":"form_get","form":"CHANNEL","name":"P2"})");
    CHECK(okv(r) && field_of(r, "ant_rx") && str(*field_of(r, "ant_rx"), "value") == "2");
    CHECK(!contains(ApiServer::handle(*core, R"({"cmd":"form_get","form":"CHANNEL","name":"P2"})"), "10000"));
    // channel_put straight: checked the same way; left out keeps what's held.
    r = api(*core, R"({"cmd":"channel_put","name":"P2","frequency":"10000","ant_tx":"5"})");
    CHECK(!okv(r) && contains(str(r, "error"), "1 or 2"));
    r = api(*core, R"({"cmd":"channel_put","name":"P2","frequency":"10000","use":"SECONDARY"})");
    CHECK(okv(r) && num(r, "ant_rx") == 2 && num(r, "ant_tx") == 2);
    r = api(*core, R"({"cmd":"channels_list"})");
    bool listed = false;
    for (auto& c : sub(r, "channels").arr)
        if (str(c, "name") == "P2") listed = num(c, "ant_rx") == 2 && num(c, "ant_tx") == 2;
    CHECK(listed);
}

// ---------------------------------------------------------------- forwarding

static std::optional<std::string> fwd(TunerKeying* tk, const std::string& cmd,
                                      std::map<std::string, std::string> req, bool emcon = false) {
    std::map<std::string, Channel> plan = {{"P1", chan("P1", 5000000, 1, 2)},
                                           {"P2", chan("P2", 10000000, 2, 2)},
                                           {"V1", chan("V1", 145000000, 1, 1)}};
    auto lookup = [&](const std::string& n) -> std::optional<Channel> {
        auto it = plan.find(n);
        return it == plan.end() ? std::nullopt : std::optional<Channel>(it->second);
    };
    return handle_tuner(tk, cmd, req, lookup, emcon);
}

static void forwarding() {
    // Not ours: left for the other handlers.
    CHECK(!fwd(nullptr, "status", {}));

    // No tuner set up: status says so, the rest refuse.
    JsonValue r = parsed(*fwd(nullptr, "tuner_status", {}));
    CHECK(okv(r) && sub(r, "tuner").type == JsonValue::Type::Null);
    r = parsed(*fwd(nullptr, "tuner_bypass", {}));
    CHECK(!okv(r) && str(r, "code") == "no-tuner");

    auto stub = std::make_shared<StubTuner>();
    TunerKeying tk(stub, fast());

    // Status passes the daemon's block through whole: channel, contract_rev, relays.antenna.
    r = parsed(*fwd(&tk, "tuner_status", {}));
    CHECK(okv(r));
    CHECK(num(sub(r, "tuner"), "contract_rev") == 2);
    CHECK(num(sub(sub(r, "tuner"), "channel"), "ant_tx") == 2 && num(sub(sub(r, "tuner"), "channel"), "recalled_on") == 1);
    CHECK(num(sub(sub(r, "tuner"), "relays"), "antenna") == 1);
    CHECK(sub(sub(r, "tuner"), "meter").get("fwd_w") != nullptr);

    // A channel change: by designator in, with its frequency and ports out to the daemon only.
    stub->scripted["tuner_channel"] = {ok_line(
        "tuner_channel", "{\"antenna\":1,\"role\":\"ant_rx\",\"switched\":true,\"recall\":{\"source\":\"none\",\"applied\":false}}")};
    std::string reply = *fwd(&tk, "tuner_channel", {{"channel", "P1"}});
    r = parsed(reply);
    CHECK(okv(r) && str(r, "channel") == "P1" && str(sub(r, "result"), "role") == "ant_rx");
    CHECK(!stub->sent.empty() && stub->sent.back().first == "tuner_channel");
    CHECK(stub->sent.back().second == R"({"freq_hz":5000000,"ant_rx":1,"ant_tx":2})");
    CHECK(!contains(reply, "5000000"));
    CHECK(tk.current_channel() == "P1");
    // A frequency never comes in over 4100.
    r = parsed(*fwd(&tk, "tuner_channel", {{"freq_hz", "5000000"}}));
    CHECK(!okv(r) && str(r, "code") == "refused-args");
    r = parsed(*fwd(&tk, "tuner_channel", {{"channel", "NOPE"}}));
    CHECK(!okv(r) && contains(str(r, "error"), "no such channel"));
    // Outside the tuner's range: refused, the daemon not told.
    size_t before = stub->sent.size();
    r = parsed(*fwd(&tk, "tuner_channel", {{"channel", "V1"}}));
    CHECK(!okv(r) && str(r, "code") == "refused-range" && stub->sent.size() == before);
    // Recall by designator.
    fwd(&tk, "tuner_recall", {{"channel", "P2"}});
    CHECK(stub->sent.back() == (std::pair<std::string, std::string>{"tuner_recall", R"({"freq_hz":10000000})"}));

    // The direct calls, checked here first.
    r = parsed(*fwd(&tk, "tuner_antenna", {{"port", "3"}}));
    CHECK(!okv(r) && str(r, "code") == "refused-args");
    fwd(&tk, "tuner_antenna", {{"port", "2"}});
    CHECK(stub->sent.back() == (std::pair<std::string, std::string>{"tuner_antenna", R"({"port":2})"}));
    r = parsed(*fwd(&tk, "tuner_set", {{"L", "128"}, {"C", "1"}, {"hilo", "hi"}}));
    CHECK(!okv(r));
    fwd(&tk, "tuner_set", {{"L", "12"}, {"C", "99"}, {"hilo", "LO"}});
    CHECK(stub->sent.back() == (std::pair<std::string, std::string>{"tuner_set", R"({"L":12,"C":99,"hilo":"lo"})"}));
    for (auto c : {"tuner_bypass", "tuner_store", "tuner_pre_tx", "tuner_post_tx"}) {
        fwd(&tk, c, {});
        CHECK(stub->sent.back() == (std::pair<std::string, std::string>{c, ""}));
    }

    // Refusals come through as the daemon gave them: code, reason and block.
    struct R { const char* cmd; const char* code; const char* why; };
    for (auto x : {R{"tuner_store", "refused-antenna", "store goes on the channel's ant_tx (2), and the tuner is on antenna 1"},
                   R{"tuner_pre_tx", "refused-no-channel", "send tuner_channel first"},
                   R{"tuner_post_tx", "refused-keyed", "gale-key is running: keyed; ant1 refused"},
                   R{"tuner_bypass", "refused-power", "the tuner reads 40.00 W forward"}}) {
        stub->scripted[x.cmd] = {err_line(x.cmd, x.code, x.why)};
        r = parsed(*fwd(&tk, x.cmd, {}));
        CHECK(!okv(r) && str(r, "code") == x.code && str(r, "error") == x.why);
        CHECK(num(sub(sub(r, "tuner"), "relays"), "antenna") == 2);
        stub->scripted.erase(x.cmd);
    }

    // A reply in some other protocol isn't taken.
    TunerReply bad = parse_tuner_reply(R"({"protocol":"ldg-tuner/9","ok":true})");
    CHECK(bad.reached && !bad.valid && !bad.ok && bad.code == "malformed");
    bad = parse_tuner_reply("not json");
    CHECK(!bad.ok && bad.code == "malformed");

    // Nothing listening: the call says there's no tuner.
    stub->absent = true;
    r = parsed(*fwd(&tk, "tuner_bypass", {}));
    CHECK(!okv(r) && str(r, "code") == "no-tuner");
    // ...and a channel change with no daemon is no failure: there's nothing to tell.
    r = parsed(*fwd(&tk, "tuner_channel", {{"channel", "P2"}}));
    CHECK(okv(r) && sub(r, "tuner").type == JsonValue::Type::Null && tk.current_channel() == "P2");
}

// ---------------------------------------------------------------- the sequence

static void sequence() {
    std::vector<std::string> logged;
    auto stub = std::make_shared<StubTuner>();
    TunerKeying tk(stub, fast());
    tk.set_log([&](const std::string& s) { logged.push_back(s); });
    StubKeyer k;

    // In order: pre_tx with RF off, key and send, post_tx once idle.
    clear_events();
    k.busy_polls = 3;
    TunerOutcome o = tk.transmit(k, false);
    CHECK(o.ok && o.tuner);
    CHECK(events() == (std::vector<std::string>{"tuner_pre_tx", "key", "unkey", "tuner_post_tx"}));
    CHECK(k.left == 0);  // post_tx waited for the keyer to go idle

    // A pre_tx refusal blocks the key.
    for (auto code : {"refused-keyed", "tuner-absent", "timeout", "busy", "refused-no-channel"}) {
        clear_events();
        stub->scripted["tuner_pre_tx"] = {err_line("tuner_pre_tx", code, "why")};
        o = tk.transmit(k, false);
        CHECK(!o.ok && o.code == code);
        CHECK(events() == (std::vector<std::string>{"tuner_pre_tx"}));
    }
    stub->scripted.erase("tuner_pre_tx");
    // A malformed reply is no ok either.
    clear_events();
    stub->scripted["tuner_pre_tx"] = {"{\"protocol\":\"ldg-tuner/1\",\"ok\":\"yes\"}"};
    o = tk.transmit(k, false);
    CHECK(!o.ok && events() == (std::vector<std::string>{"tuner_pre_tx"}));
    stub->scripted.erase("tuner_pre_tx");

    // post_tx refused while gale-key's watchdog lingers: retried, logged, then ok.
    clear_events();
    logged.clear();
    stub->scripted["tuner_post_tx"] = {err_line("tuner_post_tx", "refused-keyed", "gale-key-watchdog is running: keyed"),
                                       err_line("tuner_post_tx", "refused-keyed", "gale-key-watchdog is running: keyed"),
                                       ok_line("tuner_post_tx", "{\"antenna\":1,\"role\":\"ant_rx\",\"switched\":true,\"recall\":null}")};
    o = tk.transmit(k, false);
    CHECK(o.ok);
    CHECK(events() == (std::vector<std::string>{"tuner_pre_tx", "key", "unkey", "tuner_post_tx", "tuner_post_tx",
                                                "tuner_post_tx"}));
    int retry_lines = 0;
    for (auto& l : logged) retry_lines += contains(l, "tuner_post_tx refused-keyed (try") ? 1 : 0;
    CHECK(retry_lines == 2);

    // Bounded: it gives up after its tries and says the receiver may be on the TX antenna.
    clear_events();
    logged.clear();
    stub->scripted["tuner_post_tx"] = {err_line("tuner_post_tx", "refused-keyed", "keyed")};
    o = tk.transmit(k, false);
    CHECK(!o.ok && o.code == "refused-keyed" && contains(o.reason, "sent, but"));
    int posts = 0;
    for (auto& e : events()) posts += e == "tuner_post_tx" ? 1 : 0;
    CHECK(posts == fast().post_tries);
    CHECK(!logged.empty() && contains(logged.back(), "TX antenna"));
    // A refusal that a retry can't mend isn't retried.
    clear_events();
    stub->scripted["tuner_post_tx"] = {err_line("tuner_post_tx", "tuner-absent", "unplugged")};
    o = tk.transmit(k, false);
    posts = 0;
    for (auto& e : events()) posts += e == "tuner_post_tx" ? 1 : 0;
    CHECK(!o.ok && posts == 1);
    stub->scripted.erase("tuner_post_tx");

    // EMCON: nothing keyed, nothing asked.
    clear_events();
    o = tk.transmit(k, true);
    CHECK(!o.ok && o.code == "refused-emcon" && events().empty());

    // No VOX: a keyer that keys any other way is refused before anything runs.
    StubKeyer vox;
    vox.how = "vox";
    clear_events();
    o = tk.transmit(vox, false);
    CHECK(!o.ok && o.code == "refused-keying-method" && contains(o.reason, "VOX") && events().empty());

    // No tuner at all: keying is exactly as before.
    TunerKeying none(nullptr, fast());
    clear_events();
    o = none.transmit(k, false);
    CHECK(o.ok && !o.tuner && events() == (std::vector<std::string>{"key", "unkey"}));
    // A socket set, but no daemon on it: the same.
    auto gone = std::make_shared<StubTuner>();
    gone->absent = true;
    TunerKeying away(gone, fast());
    clear_events();
    o = away.transmit(k, false);
    CHECK(o.ok && !o.tuner && events() == (std::vector<std::string>{"key", "unkey"}));
    CHECK(gone->cmds() == (std::vector<std::string>{"tuner_pre_tx"}));
    // The core holds no keyer today: a transmit through it says so.
    o = tk.transmit(false);
    CHECK(!o.ok && o.code == "no-keyer");
}

static void tune() {
    auto stub = std::make_shared<StubTuner>();
    TunerKeying tk(stub, fast());
    auto k = std::make_shared<StubKeyer>();

    // Pinned (the default, and today): refused, nothing sent, nothing keyed.
    CHECK(tk.pinned());
    clear_events();
    TunerOutcome o = tk.tune("full", "Y", 10, true, false);
    CHECK(!o.ok && o.code == "refused-tx-locked" && events().empty() && stub->sent.empty());
    tk.set_keyer(k);
    o = tk.tune("full", "Y", 10, true, false);
    CHECK(!o.ok && o.code == "refused-tx-locked" && events().empty() && stub->sent.empty());
    // ...and through 4100 the same, whatever is asked.
    JsonValue r = parsed(*fwd(&tk, "tuner_tune", {{"kind", "full"}, {"confirm", "Y"}, {"watts", "5"}}));
    CHECK(!okv(r) && str(r, "code") == "refused-tx-locked" && stub->sent.empty());

    // Unpinned (a test only: no call or setting does this).
    tk.set_pinned(false);
    // The core holds 25 W: above it, refused before anything keys.
    clear_events();
    o = tk.tune("full", "Y", 26, true, false);
    CHECK(!o.ok && o.code == "refused-power" && events().empty());
    r = parsed(*fwd(&tk, "tuner_tune", {{"kind", "full"}, {"confirm", "Y"}, {"watts", "100"}}));
    CHECK(!okv(r) && str(r, "code") == "refused-power" && events().empty());
    // Only the operator's Y; the core never makes one up.
    o = tk.tune("full", "y", 10, true, false);
    CHECK(!o.ok && o.code == "refused-confirm" && events().empty());
    r = parsed(*fwd(&tk, "tuner_tune", {{"kind", "full"}}));
    CHECK(!okv(r) && str(r, "code") == "refused-confirm" && events().empty());
    o = tk.tune("full", "Y", 10, true, true);
    CHECK(!o.ok && o.code == "refused-emcon" && events().empty());

    // In order: pre_tx, a carrier of 25 W or less, tune, store, carrier off, post_tx.
    clear_events();
    o = tk.tune("full", "Y", 25, true, false);
    CHECK(o.ok && k->carrier_w == 25);
    CHECK(events() == (std::vector<std::string>{"tuner_pre_tx", "carrier 25", "tuner_tune", "tuner_store", "carrier off",
                                                "tuner_post_tx"}));
    bool confirm_sent = false;
    for (auto& [c, a] : stub->sent)
        if (c == "tuner_tune") confirm_sent = a == R"({"kind":"full","confirm":"Y"})";
    CHECK(confirm_sent);
    // The daemon's refused-power (it read more than 25 W): passed on, no store, back to RX.
    clear_events();
    stub->scripted["tuner_tune"] = {err_line("tuner_tune", "refused-power", "the tuner reads 40.00 W forward")};
    o = tk.tune("memory", "Y", 20, true, false);
    CHECK(!o.ok && o.code == "refused-power");
    CHECK(events() == (std::vector<std::string>{"tuner_pre_tx", "carrier 20", "tuner_tune", "carrier off", "tuner_post_tx"}));
    stub->scripted.erase("tuner_tune");
    // pre_tx refused (not on ant_tx): no carrier at all.
    clear_events();
    stub->scripted["tuner_pre_tx"] = {err_line("tuner_pre_tx", "refused-keyed", "keyed")};
    o = tk.tune("full", "Y", 10, true, false);
    CHECK(!o.ok && o.code == "refused-keyed" && events() == (std::vector<std::string>{"tuner_pre_tx"}));
    stub->scripted.erase("tuner_pre_tx");
    // No daemon: nothing to tune, nothing keyed.
    auto gone = std::make_shared<StubTuner>();
    gone->absent = true;
    TunerKeying away(gone, fast());
    away.set_keyer(k);
    away.set_pinned(false);
    clear_events();
    o = away.tune("full", "Y", 10, true, false);
    CHECK(!o.ok && o.code == "tuner-absent" && events().empty());
}

// ---------------------------------------------------------------- the real socket

// A one-shot Unix socket server standing in for the daemon: checks the line
// the core sends and answers it.
static void socket_framing(const fs::path& dir) {
    std::string path = (dir / "tuner.sock").string();
    int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::memcpy(a.sun_path, path.c_str(), path.size());
    CHECK(::bind(srv, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0 && ::listen(srv, 4) == 0);
    std::string got;
    std::thread t([&] {
        int c = ::accept(srv, nullptr, nullptr);
        char buf[4096];
        ssize_t n = ::recv(c, buf, sizeof buf, 0);
        if (n > 0) got.assign(buf, static_cast<size_t>(n));
        std::string reply = ok_line("tuner_channel", "{\"antenna\":1,\"role\":\"ant_rx\",\"switched\":false,\"recall\":null}") + "\n";
        ::send(c, reply.data(), reply.size(), MSG_NOSIGNAL);
        ::close(c);
    });
    TunerSocket s(path);
    TunerReply r = s.call("tuner_channel", R"({"freq_hz":5000000,"ant_rx":1,"ant_tx":2})", 5000);
    t.join();
    ::close(srv);
    CHECK(got == "{\"id\":1,\"cmd\":\"tuner_channel\",\"args\":{\"freq_hz\":5000000,\"ant_rx\":1,\"ant_tx\":2}}\n");
    CHECK(r.reached && r.valid && r.ok && r.contract_rev == 2);
    CHECK(contains(r.tuner, "\"recalled_on\":1") && contains(r.result, "\"role\":\"ant_rx\""));
    // Nothing listening there now: not reached, so no tuner.
    ::unlink(path.c_str());
    r = s.call("tuner_status", "", 1000);
    CHECK(!r.reached && r.code == "no-tuner");
}

// ---------------------------------------------------------------- status and the guide page

static void status_and_guide(const fs::path& dir) {
    auto core = std::make_unique<Core>(CoreConfig{"STA-B", (dir / "STA-B.db").string(), 3, 1, true},
                                       std::make_unique<LoopbackModem>(0));
    core->set_services(test_services());
    core->set_ops(std::make_shared<Ops>("STA-B", (dir / "STA-B.db").string()));

    // No tuner set up: status carries no tuner section.
    JsonValue r = api(*core, R"({"cmd":"status"})");
    CHECK(okv(r) && r.get("tuner") == nullptr);

    // A tuner answering: its block, passed through whole.
    auto stub = std::make_shared<StubTuner>();
    auto tk = std::make_shared<TunerKeying>(stub, fast());
    core->set_tuner(tk);
    r = api(*core, R"({"cmd":"status"})");
    CHECK(okv(r) && r.get("tuner") != nullptr);
    const JsonValue& t = sub(r, "tuner");
    CHECK(num(t, "contract_rev") == 2 && str(t, "protocol") == "ldg-tuner/1");
    CHECK(num(sub(t, "channel"), "ant_rx") == 1 && num(sub(t, "channel"), "ant_tx") == 2 &&
          num(sub(t, "channel"), "recalled_on") == 1);
    CHECK(num(sub(t, "relays"), "antenna") == 1 && sub(t, "meter").get("ref_w") != nullptr);
    // Read at most once a second: a second status within that reuses the read.
    size_t reads = stub->sent.size();
    api(*core, R"({"cmd":"status"})");
    CHECK(stub->sent.size() == reads);
    // The rest of status is as it was.
    CHECK(r.get("station") && r.get("emcon") && r.get("cap"));

    // The daemon gone: the section is left out, not an error.
    auto gone = std::make_shared<StubTuner>();
    gone->absent = true;
    core->set_tuner(std::make_shared<TunerKeying>(gone, fast()));
    r = api(*core, R"({"cmd":"status"})");
    CHECK(okv(r) && r.get("tuner") == nullptr);
    // An answer that isn't ldg-tuner/1: left out too.
    auto odd = std::make_shared<StubTuner>();
    odd->scripted["tuner_status"] = {R"({"protocol":"other","ok":true,"tuner":{"x":1}})"};
    core->set_tuner(std::make_shared<TunerKeying>(odd, fast()));
    r = api(*core, R"({"cmd":"status"})");
    CHECK(okv(r) && r.get("tuner") == nullptr);

    // The TUNER guide page: per-channel ports, RF-off switching, 25 W, refused while pinned.
    r = api(*core, R"({"cmd":"guide_get","id":"TUNER"})");
    CHECK(okv(r));
    std::string text;
    for (auto& l : sub(sub(r, "page"), "lines").arr) text += l.s + " ";
    for (auto want : {"RX antenna", "TX antenna", "port 1", "RF on", "RF off", "25 W", "pinned", "refused", "VOX",
                      "With no tuner"})
        CHECK(contains(text, want));
    bool see_channel = false;
    for (auto& x : sub(sub(r, "page"), "see").arr) see_channel = see_channel || x.s == "FORM-CHANNEL";
    CHECK(see_channel);
}

// ---------------------------------------------------------------- around gale-key's key

static void around_gale_key() {
    std::vector<std::string> logged;
    auto stub = std::make_shared<StubTuner>();
    TunerKeying tk(stub, fast());
    tk.set_log([&](const std::string& s) { logged.push_back(s); });

    // pre_tx, then gale-key's key, then (once it has ended) post_tx.
    clear_events();
    bool asked = false;
    int ended_polls = 0;
    TunerOutcome o = tk.around_key([] { ev("gale-key key"); },
                                   [&] {
                                       ev("ended?");
                                       return ++ended_polls >= 3;  // gale-key still keyed twice
                                   },
                                   &asked);
    CHECK(o.ok && o.tuner && asked);
    std::vector<std::string> want = {"tuner_pre_tx", "gale-key key", "ended?", "ended?", "ended?", "tuner_post_tx"};
    CHECK(events() == want);

    // pre_tx refused: gale-key is never asked, and the reason comes back.
    clear_events();
    stub->scripted["tuner_pre_tx"] = {err_line("tuner_pre_tx", "refused-no-channel", "send tuner_channel first")};
    bool called = false;
    o = tk.around_key([&] { called = true; }, [] { return true; }, &asked);
    CHECK(!o.ok && !asked && !called && o.code == "refused-no-channel" && contains(o.reason, "tuner_channel first"));
    CHECK(events() == std::vector<std::string>{"tuner_pre_tx"});
    stub->scripted.erase("tuner_pre_tx");

    // post_tx refused-keyed: retried with the backoff until it is taken.
    clear_events();
    stub->scripted["tuner_post_tx"] = {err_line("tuner_post_tx", "refused-keyed", "gale-key is running: keyed"),
                                       err_line("tuner_post_tx", "refused-keyed", "gale-key is running: keyed"),
                                       ok_line("tuner_post_tx", "{}")};
    o = tk.around_key([] { ev("gale-key key"); }, [] { return true; }, &asked);
    CHECK(o.ok && asked);
    want = {"tuner_pre_tx", "gale-key key", "tuner_post_tx", "tuner_post_tx", "tuner_post_tx"};
    CHECK(events() == want);
    // Never taken: the key still happened; the outcome says the antenna didn't go back.
    stub->scripted["tuner_post_tx"] = {err_line("tuner_post_tx", "refused-keyed", "gale-key is running: keyed")};
    o = tk.around_key([] {}, [] { return true; }, &asked);
    CHECK(!o.ok && asked && o.code == "refused-keyed" && contains(o.reason, "RX antenna"));
    stub->scripted.erase("tuner_post_tx");

    // A daemon set up but not answering: gale-key is asked as without a tuner.
    stub->absent = true;
    called = false;
    o = tk.around_key([&] { called = true; }, [] { return true; }, &asked);
    CHECK(o.ok && asked && called && !o.tuner);
    stub->absent = false;

    // No daemon set up at all: the key goes as is, nothing else runs.
    TunerKeying none(nullptr);
    called = false;
    int polls = 0;
    o = none.around_key([&] { called = true; }, [&] { ++polls; return true; }, &asked);
    CHECK(o.ok && asked && called && polls == 0 && !o.tuner);

    // tuner_channel with no daemon set up: ok, and the channel is recorded.
    std::map<std::string, Channel> plan = {{"P1", chan("P1", 5000000, 1, 2)}};
    auto lookup = [&](const std::string& n) -> std::optional<Channel> {
        auto it = plan.find(n);
        return it == plan.end() ? std::nullopt : std::optional<Channel>(it->second);
    };
    std::map<std::string, std::string> req = {{"channel", "P1"}};
    std::string reply = *handle_tuner(&none, "tuner_channel", req, lookup, false);
    JsonValue r = parsed(reply);
    CHECK(okv(r) && str(r, "channel") == "P1" && none.current_channel() == "P1" && !contains(reply, "5000000"));
    req = {{"channel", "NOPE"}};
    CHECK(!okv(parsed(*handle_tuner(&none, "tuner_channel", req, lookup, false))));
    CHECK(none.current_channel() == "P1");
    // The other calls still say there is no tuner.
    req.clear();
    CHECK(str(parsed(*handle_tuner(&none, "tuner_pre_tx", req, lookup, false)), "code") == "no-tuner");
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_tuner_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    plan_file(dir);
    channel_form(dir);
    forwarding();
    sequence();
    tune();
    around_gale_key();
    socket_framing(dir);
    status_and_guide(dir);
    fs::remove_all(dir);
    return check::report("test_tuner");
}
