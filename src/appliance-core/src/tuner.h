// The LDG AT-200PC antenna tuner, as the core sees it.
//
// The tuner's driver is greybeard-ale's: tools/ldg_tuner.py runs as a daemon on
// the radio host and answers JSON lines on a Unix socket, protocol
// "ldg-tuner/1" (greybeard-ale docs/ldg-tuner.md section 5). The core holds no
// tuner code. It does three things:
//
//  1. Forwards the operator's tuner calls from port 4100 to that socket and
//     passes the reply through: the result, the refusal code and reason as
//     they are, and the daemon's whole "tuner" status block.
//  2. Holds each channel's antenna ports (ant_rx, ant_tx: channels.h) and names
//     them, with the channel's frequency, in tuner_channel on a channel change.
//     The frequency goes only to that local socket: never into a reply, the
//     log or the air. The API names a channel by its designator.
//  3. Runs the tuner sequence around every key the core asks of gale-key
//     (section 5.7), because gale-key can't call the tuner (around_key):
//       tuner_pre_tx, RF off (switch to the channel's ant_tx); gale-key is
//         sent `key` only on ok
//       gale-key keys and transmits, and answers when its key has ended
//       once gale-key's key has ended (its reply, or its status no longer
//         keyed), tuner_post_tx (back to ant_rx), retried on refused-keyed
//         with a bounded backoff
//     A tune (a carrier of 25 W or less, tuner_tune, tuner_store, all on
//     ant_tx) runs the same sequence through a Keyer.
//
// Keying is by CAT or a hardware PTT line only. VOX is forbidden (Alex), so a
// keyer that says it keys any other way is refused before anything is sent.
//
// The core keys nothing itself: gale-key (gale_key.h) is the station's one
// keying service, so the core holds no keyer and refuses tuner_tune while
// transmit is pinned. Only Alex unpins it; no call, setting or file does. With
// no tuner daemon there at all (no socket set, or nothing listening on it),
// gale-key's keys go exactly as they would without one, and tuner_channel
// still records the channel the radio is on.
#pragma once

#include "channels.h"
#include "json.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <vector>

namespace pna {

// One reply from the tuner daemon.
struct TunerReply {
    bool reached = false;    // something answered on the socket with a line
    bool valid = false;      // that line is an ldg-tuner/1 reply
    bool ok = false;         // and it says ok
    std::string code;        // the error code when not ok ("no-tuner" when not reached)
    std::string reason;      // the reason, as the daemon gave it
    std::string result;      // the "result" object, as JSON text ("{}" when absent)
    std::string tuner;       // the "tuner" status block, as JSON text ("null" when absent)
    int contract_rev = 1;    // tuner.contract_rev; absent means 1
};

// Where the core sends tuner commands. Abstract so tests use a stub.
class TunerLink {
public:
    virtual ~TunerLink() = default;
    // Sends {"id":..,"cmd":cmd,"args":args_json} and reads one reply line.
    // args_json is a JSON object's text, or empty for none.
    virtual TunerReply call(const std::string& cmd, const std::string& args_json, int timeout_ms) = 0;
};

// The daemon's control socket (docs/ldg-tuner.md 5.1).
class TunerSocket : public TunerLink {
public:
    explicit TunerSocket(std::string path) : path_(std::move(path)) {}
    TunerReply call(const std::string& cmd, const std::string& args_json, int timeout_ms) override;
    const std::string& path() const { return path_; }

private:
    std::string path_;
    long long next_id_ = 1;
    std::mutex mu_;
};

// Reads one reply line from the daemon. Exposed for tests.
TunerReply parse_tuner_reply(const std::string& line);

// The text of a parsed JSON value, for passing a block through unchanged.
std::string json_text(const JsonValue& v);

// What keys the radio. The core holds none while transmit is pinned to
// gale-tx/WAV; tests use a stub.
class Keyer {
public:
    virtual ~Keyer() = default;
    // "cat" or "ptt". Anything else (VOX above all) is refused.
    virtual std::string method() const = 0;
    // Keys, sends the burst, unkeys. Returns when the transmit has ended.
    virtual bool transmit(std::string* error) = 0;
    // Keys a steady carrier of `watts`, runs while_on with it up, then
    // unkeys. False with *error when the carrier didn't come up.
    virtual bool carrier(int watts, const std::function<void()>& while_on, std::string* error) = 0;
    // True once the keying process and its watchdog have exited and RF is off.
    virtual bool idle() = 0;
};

struct TunerTiming {
    int call_ms = 30000;       // a client's read timeout (5.6: at least 30 s)
    int idle_wait_ms = 15000;  // how long to wait for the keyer to go idle before post_tx
    int idle_poll_ms = 100;
    int post_tries = 8;        // tuner_post_tx tries on refused-keyed or busy
    int post_first_ms = 250;   // first backoff, doubled each try
    int post_max_ms = 4000;    // backoff ceiling
    // How long to wait for gale-key's key to end before post_tx: its key
    // time is capped at 120 s (greybeard-ale docs/gale-tx.md), plus margin.
    int key_end_wait_ms = 130000;
    int status_ms = 1500;          // the core's status call: tuner_status read timeout
    int status_max_age_ms = 1000;  // and how long one read serves
};

// The most a tune's carrier may be: the FT-897D roll-back is unconfirmed.
constexpr int kMaxTuneWatts = 25;

// The outcome of one sequence step, for the API and the log.
struct TunerOutcome {
    bool ok = false;
    std::string code;          // refusal code: the daemon's, or the core's own
    std::string reason;
    TunerReply last;           // the daemon's last reply, when there was one
    std::vector<std::string> steps;  // what ran, in order: "tuner_pre_tx ok", "key", ...
    bool tuner = false;        // a tuner daemon took part
};

class TunerKeying {
public:
    // link may be null: no tuner configured.
    explicit TunerKeying(std::shared_ptr<TunerLink> link, TunerTiming t = {});

    bool configured() const { return link_ != nullptr; }
    TunerLink* link() const { return link_.get(); }

    // Transmit pinned to gale-tx/WAV. True by default; only a build that Alex
    // unpins clears it. No API call or setting reaches this.
    void set_pinned(bool pinned);
    bool pinned() const;

    // The keyer, or none. Set by the program, never by a call.
    void set_keyer(std::shared_ptr<Keyer> k);

    // Logs each step (designators only, never a frequency).
    void set_log(std::function<void(const std::string&)> log);

    // A channel change: tuner_channel {freq_hz, ant_rx, ant_tx}. Remembers the
    // channel's designator. With no tuner, ok with tuner=false.
    TunerOutcome channel(const Channel& c);
    std::string current_channel() const;
    // The channel the radio was last set to, for clients that show it (status
    // radio_channel): the designator and when (UTC, ISO 8601 Z); world is
    // left "" for the caller to take from the channel plan. Never a
    // frequency. nullopt when none has been set since start.
    struct RadioChannel {
        std::string label, world, since;
    };
    std::optional<RadioChannel> radio_channel() const;

    // The daemon's status block, as JSON text, for the core's status call:
    // tuner_status read at most once a second, passed through unchanged.
    // Empty when no tuner is set up or nothing answers.
    std::string status_block();

    // One keyed transmit through the keyer, with the tuner around it.
    TunerOutcome transmit(Keyer& k, bool emcon);
    // The program's keyer, if any.
    TunerOutcome transmit(bool emcon);

    // A tune: refused while pinned, under EMCON, without the operator's "Y",
    // or above 25 W. Then pre_tx, a carrier of `watts`, tuner_tune, tuner_store
    // (when store), carrier off, post_tx.
    TunerOutcome tune(const std::string& kind, const std::string& confirm, int watts, bool store, bool emcon);

    // After a transmit: wait for the keyer to go idle, then tuner_post_tx,
    // retried with a bounded backoff on refused-keyed and busy.
    TunerOutcome post_tx(Keyer* k);

    // Around one key the core asks of gale-key. With a tuner daemon:
    // tuner_pre_tx, and key() is called only on ok; then, once ended() says
    // gale-key's key is over (or key_end_wait_ms passes), tuner_post_tx with
    // the bounded retry. key() runs whether gale-key keys or refuses; post_tx
    // follows either way. With no daemon (none set, or none answering), key()
    // is called as it would be without a tuner. *asked says whether key() ran.
    TunerOutcome around_key(const std::function<void()>& key, const std::function<bool()>& ended, bool* asked);

private:
    TunerOutcome tune_with(Keyer& k, const std::string& kind, const std::string& confirm, int watts, bool store);
    TunerReply call(const std::string& cmd, const std::string& args = "");
    TunerOutcome post_tx_when(const std::function<bool()>& idle, int wait_ms, const char* what);
    void log(const std::string& s);

    std::shared_ptr<TunerLink> link_;
    TunerTiming t_;
    mutable std::mutex mu_;
    bool pinned_ = true;
    std::shared_ptr<Keyer> keyer_;
    std::string channel_;
    std::string channel_since_;  // when channel_ was set, ISO 8601 Z; "" none
    std::function<void(const std::string&)> log_;
    std::mutex seq_mu_;  // one sequence at a time
    std::mutex status_mu_;
    bool status_read_ = false;
    std::string status_;
    std::chrono::steady_clock::time_point status_at_;
};

// The 4100 calls: tuner_status, tuner_channel, tuner_recall, tuner_bypass,
// tuner_antenna, tuner_set, tuner_tune, tuner_store, tuner_pre_tx,
// tuner_post_tx. `lookup` finds a channel by designator; `emcon` is the
// core's transmit hold. nullopt for any other cmd.
std::optional<std::string> handle_tuner(TunerKeying* tk, const std::string& cmd,
                                        std::map<std::string, std::string>& req,
                                        const std::function<std::optional<Channel>(const std::string&)>& lookup,
                                        bool emcon);

}  // namespace pna
