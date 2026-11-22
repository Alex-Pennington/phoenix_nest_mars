// The headless core: owns the store, the send queue and the modem, and
// answers to whatever front end is attached through the local API.
#pragma once

#include "frame.h"
#include "link_monitor.h"
#include "modem.h"
#include "store.h"

#include <atomic>
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
    // Whether a message to `station` would wait for a link now.
    bool waits_for_link(const std::string& station) const;
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
    std::set<std::string> told_waiting_;
    std::string told_clock_;              // the clock source last logged  // messages already logged as waiting for a link
};

}  // namespace pna
