// The station as a BBS (Message Machine's machine mode and routing), the core
// side of the BBS-SETUP, BBS-GUARD and BBS-ROUTES screens
// (docs/gale-term-mm-bbs-screens.md, section 3; docs/api.md, "BBS").
//
// What it does, all of it off until the operator turns it on:
//
//  - Machine mode. Another station sends an ACP 201 command line addressed
//    to this station's call with -CM ("AAM499-CM DE AAR9AA // QRU // 1737Z K")
//    and the station answers by itself: VER (are you there, which version),
//    QRU [pattern,...] (list the traffic held here for a call or RI), ZEE n,...
//    (send me lines n of that list), ZDK n (send page n of the last ZEE answer
//    again), QSL n,... (I have line n; it is marked QSL by that station).
//    The Message Machine manual (GRSS, 12 June 2020), Machine Mode Syntax, is
//    the source for the forms. Answers are queued as outgoing messages, never
//    keyed here.
//  - Auto QSL. Plain ACP 127 traffic received for this station (which gets
//    no ack on the air) is answered with MSC's receipt line after a delay.
//  - Guard list and RI alias map: the RIs this station delivers for, and
//    collective RIs and who they stand for.
//  - Routing. A received message whose recipients match a rule goes out
//    again to the rule's ALE addresses, first match wins, with the broadcast
//    RI struck off so it can't loop.
//
// Principle 5 (nothing keys without the operator): every automatic send
// needs machine mode on (and routing on, for routed traffic), is refused or
// waits while EMCON is on, counts against auto_per_hour, and goes into the
// core's ordinary queue, so it is seen in the outbox and goes out the pinned
// transmit route like everything else. Commands heard while machine mode was
// off are never answered later.
//
// The state lives in the station's own database file, in bbs_* tables, on
// a connection of its own (as the station book does, ops.h).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct sqlite3;

namespace pna {

class Core;

namespace bbs {

constexpr const char* kSuffix = "-CM";
constexpr const char* kVersion = "PNA MESSAGE CORE BBS 1";

struct Settings {
    bool machine_mode = false;
    // Whose traffic is held here and listed to QRU:
    //   calls      messages addressed to this station's call
    //   calls_ris  those, and messages for an RI on the guard list
    //   all        everything heard (a relay node)
    std::string accept = "calls";
    // Auto QSL of plain traffic received: "off" or "auto", after ack_delay_ms.
    std::string ack = "auto";
    int ack_delay_ms = 2000;
    // Where the receipt goes: empty is the station that sent the message.
    // Never ALL (every station on the net would hear one receipt each).
    std::string ack_to;
    bool commands_ct_only = true;   // act on commands only when they came enciphered
    int max_errors = 3;             // a message with more warnings is not held for QRU
    bool accept_malformed = false;  // garbled copies listed to QRU
    bool zee_multi = false;         // ZEE may send more than one message at once
    int zee_max_bytes = 0;          // composite ZEE answer size; 0: worked out from the two below
    int data_rate_bps = 600;        // for the worked-out size
    int duty_s = 120;               // longest single transmission the radio takes, seconds
    bool remote_sync = false;
    int purge_days = 30;            // processed/discarded traffic kept this long
    int auto_mark_days = 0;         // processed marked old after this many days; 0: never
    int auto_per_hour = 12;         // automatic sends an hour, all kinds; 0: none at all
    bool routing = false;
    std::string broadcast_ri;
};

// The ZEE answer size actually used (zee_max_bytes, or rate x duty / 8, at
// most what one message can carry).
int zee_limit(const Settings& s);

struct Alias {
    std::string ri;
    std::vector<std::string> members;
};

struct Rule {
    int64_t id = 0;
    int pos = 0;
    std::string match;               // RI, partial RI ("UHE") or call with * ("AAR*")
    std::string route;               // the outbound stream's name
    std::vector<std::string> ale;    // ALE addresses, in calling order
    std::string key;                 // our cipher: a key indicator or net name; empty: as received
    std::string sched;               // "" or "HHMMZ"
    bool compress = false;
    bool mark_processed = true;
    std::string world = "mars";      // ask 37: always mars; a rule into the amateur world is refused
};

// A routed message (route_rerun and the engine).
struct Routed {
    std::string msg_id;
    int64_t rule = 0;
    // "due" (waits for its time or the gate), "queued", "refused", or
    // "failed" (no ALE address of its rule answered).
    std::string state;
    std::string out_id;  // the outgoing message, once queued
    // Audit A: which of the rule's ALE addresses it is on (0: the first). When
    // the message to one fails or goes unacked, the engine calls the next.
    int ale_pos = 0;
    int64_t due = 0;     // unix seconds
    bool processed = false;
    std::string why;
};

// Does match (a routing rule's or a QRU pattern) take this recipient? An RI
// or call matches by its start (UHE takes UHEA); a trailing * says the same.
bool matches(const std::string& pattern, const std::string& recipient);

// The recipients a message names (FL2 routing, TO, INFO, XMT, plain and RI),
// upper case, without duplicates. Empty when it isn't ACP 127 traffic.
std::vector<std::string> recipients(const std::string& body);

// The message with the broadcast RI struck from its routing line(s) and
// addressees. Unchanged when it isn't there.
std::string strip_ri(const std::string& body, const std::string& ri);

class Engine {
public:
    Engine(Core& core);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    Settings settings();
    // Applies the keys present in kv (strings, as the API gives them). False
    // with *error, changing nothing, when any value is bad.
    bool set(const std::map<std::string, std::string>& kv, std::string* error);

    std::vector<std::string> guard();
    bool set_guard(const std::string& items, std::string* error);
    std::vector<Alias> aliases();
    bool set_alias(const std::string& ri, const std::string& members, std::string* error);
    bool del_alias(const std::string& ri, std::string* error);

    std::vector<Rule> rules();
    // Adds (r.id 0) or changes a rule. *id is its id.
    bool set_rule(const Rule& r, int64_t* id, std::string* error);
    bool del_rule(int64_t id, std::string* error);
    // dir "up" or "down"; or to (1-based position).
    bool move_rule(int64_t id, const std::string& dir, int to, std::string* error);
    // Runs the rules over held traffic not routed yet. dry_run: only says
    // which would go where. Not dry: queues them (gate permitting) and
    // returns what was done.
    std::vector<Routed> rerun(bool dry_run, std::string* error);
    std::vector<Routed> routed(int limit);

    // Who sent QSL for a message here (QSL command or receipt).
    std::vector<std::string> qsl_of(const std::string& msg_id);

    // Automatic sends in the last hour, and what the gate says now.
    int auto_sent_last_hour();
    // When the oldest of those went (unix seconds), 0 when none: the cap
    // has room again an hour after it.
    int64_t auto_oldest_last_hour();
    // Empty when an automatic send may be queued now; otherwise why not.
    // routed: also needs routing on.
    std::string gate(bool routed);

    // Looks at what came in since the last pump, answers machine commands,
    // queues QSLs that are due and routed traffic whose time has come. The
    // engine's thread calls it each second; tests call it directly.
    void pump();
    // For tests: what pump() takes as now (unix seconds) and the delay clock.
    void set_clock(std::function<int64_t()> c) { clock_ = std::move(c); }

    void start();  // the pump thread
    void stop();

private:
    struct Held {
        std::string id, src, dst, body, state;
        char prec = 'R';
        int64_t created = 0;
    };
    void exec(const char* sql);
    std::string meta(const std::string& k);
    void set_meta(const std::string& k, const std::string& v);
    Settings load();  // caller holds mu_
    void process(const std::string& id);
    void command(const Held& m, const std::string& text, bool enciphered);
    std::vector<Held> pool();
    bool held_for(const Held& m, const Settings& s, const std::vector<std::string>& guard);
    std::string answer(const std::string& to, const std::string& body, const std::string& what);
    void count_auto(const std::string& kind, const std::string& id);
    std::optional<Rule> rule_for(const Held& m, const std::vector<Rule>& rs, const Settings& s);
    Routed route_one(const Held& m, const Rule& r, const Settings& s, bool dry_run);
    bool send_routed(Routed& rt);
    // Ask 40: why the BBS must not answer, list, pull or route a message (its
    // world isn't mars, or it came from a station of the amateur world), or "".
    std::string world_block(const std::string& msg_id, const std::string& src);
    // Routed messages whose outgoing copy failed or went unacked: on to the
    // rule's next ALE address, or failed when none is left.
    void next_ale();
    int64_t next_due(const std::string& sched, int64_t now);

    Core& core_;
    sqlite3* db_ = nullptr;
    std::recursive_mutex mu_;
    std::function<int64_t()> clock_;
    struct Qsl {
        std::string msg_id, to, text;
        std::chrono::steady_clock::time_point due;
    };
    std::vector<Qsl> qsls_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

// The engine for a core: made on first use, one per core.
Engine& engine(Core& core);
// Starts the engine's pump thread (main.cpp, after the core starts), and
// stops it before the core stops.
void start(Core& core);
void stop(Core& core);

}  // namespace bbs
}  // namespace pna
