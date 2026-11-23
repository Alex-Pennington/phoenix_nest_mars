// The headless core: owns the store, the send queue and the modem, and
// answers to whatever front end is attached through the local API.
#pragma once

#include "frame.h"
#include "keys.h"
#include "link_monitor.h"
#include "ops.h"
#include "modem.h"
#include "store.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <set>
#include <thread>

namespace pna {

struct CoreConfig {
    std::string station;      // our station name, e.g. "AAM402"
    std::string db_path;      // SQLite file
    int max_attempts = 3;     // modem failures before a message is marked failed
    int retry_delay_s = 10;   // first retry delay; doubles each time
    bool send_acks = true;    // answer each received message with an ACK frame
    // Hold each message until greybeard-ale reports a link to its station
    // (needs a link monitor). ACKs are not held: they answer a station just heard.
    bool link_gate = false;
};

struct CoreStatus {
    std::string station;
    std::string modem;
    bool modem_ready = false;
    bool modem_busy = false;
    std::string sending;  // id on the air now, empty if none
    QueueCounts counts;
    size_t frames_dropped = 0;
    int acks_waiting = 0;
    bool link_gate = false;
    int held_for_link = 0;    // queued messages waiting for a link
    LinkState link;           // socket empty: no link source
};

class Core {
public:
    Core(CoreConfig cfg, std::unique_ptr<ModemDriver> modem);
    ~Core();

    void start();
    void stop();

    // Queues a message. Returns its id, or an empty string with *error set.
    std::string submit(const std::string& to, char prec, const std::string& body, std::string* error);

    // Next value of a named counter, 1..wrap (see Store::next_counter).
    int64_t next_counter(const std::string& key, int64_t wrap);

    const std::string& station() const { return cfg_.station; }

    CoreStatus status();
    // greybeard-ale's link state (see link_monitor.h). Set before start().
    void set_link_monitor(std::shared_ptr<LinkMonitor> m) { link_ = std::move(m); }
    // Whether a message to `station` would wait for a link now. A broadcast
    // (ALL) never waits: it goes out for whoever is listening.
    bool waits_for_link(const std::string& station) const;
    // The station book (ops.h): nets hear stations through it. Set before start().
    void set_ops(std::shared_ptr<Ops> ops) { ops_ = std::move(ops); }
    Ops* ops() const { return ops_.get(); }
    // The station's codress keys (keys.h). Set before start(); none: codress
    // is neither sent nor read.
    void set_keyring(std::shared_ptr<keys::Keyring> k) { keys_ = std::move(k); }
    keys::Keyring* keyring() const { return keys_.get(); }
    // Queues a codress copy and keeps its plain copy beside it (Store::set_clear).
    std::string submit_codress(const std::string& to, char prec, const std::string& codress, const std::string& clear,
                               const std::string& key, std::string* error);
    std::optional<std::pair<std::string, std::string>> clear(const std::string& id);
    // Writes a line to the station's event log (key loads and zeroizes).
    void note(const std::string& text);
    std::vector<Message> list(int limit);
    std::optional<Message> get(const std::string& id);
    std::vector<Message> queue(int limit);
    std::vector<Event> events(int64_t after, int limit);

    // Called for each event line (also written to the store's event table).
    void on_log(std::function<void(const std::string&)> fn) { log_fn_ = std::move(fn); }

    // For tests: run one scheduler pass now.
    void tick();

private:
    void loop();
    void handle_rx(const std::string& bytes);
    void handle_tx_done(bool ok, const std::string& err);
    void log(const std::string& msg_id, const std::string& text);
    void read_codress(const std::string& id, const std::string& body);  // caller holds mu_

    CoreConfig cfg_;
    std::unique_ptr<ModemDriver> modem_;
    Store store_;
    FrameParser parser_;

    std::mutex mu_;
    std::deque<Frame> acks_;           // ACK frames waiting to go out
    std::optional<Frame> on_air_;      // what the modem is sending now
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::function<void(const std::string&)> log_fn_;
    std::shared_ptr<LinkMonitor> link_;
    std::shared_ptr<Ops> ops_;
    std::shared_ptr<keys::Keyring> keys_;
    std::set<std::string> told_waiting_;  // messages already logged as waiting for a link
    std::string told_clock_;              // the clock source last logged
    std::chrono::steady_clock::time_point next_sched_{};  // when to look at net schedules again
};

}  // namespace pna
