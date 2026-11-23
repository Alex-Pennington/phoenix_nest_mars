// Message store: every message the station sends or receives, kept in one
// SQLite file so the queue survives a restart or power loss.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace pna {

// Outgoing states: queued -> sending -> sent -> acked, or failed.
// Incoming state: received.
struct Message {
    std::string id;
    std::string direction;  // "out" or "in"
    std::string src;
    std::string dst;
    char prec = 'R';
    std::string body;
    std::string state;
    int attempts = 0;
    int64_t created_at = 0;  // unix seconds, UTC
    int64_t updated_at = 0;
    int64_t next_try_at = 0;
    std::string last_error;
};

// One line of the event log (events table rowid is its number).
struct Event {
    int64_t seq = 0;
    int64_t ts = 0;
    std::string msg_id;
    std::string text;
};

struct QueueCounts {
    int queued = 0, sending = 0, sent = 0, acked = 0, failed = 0, received = 0;
};

class Store {
public:
    explicit Store(const std::string& path);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    // Next sequence number for this station's message ids.
    int64_t next_seq();

    // Next value of a named counter, 1..wrap, then back to 1 (format line
    // serial numbers). Kept in the meta table beside the id sequence.
    int64_t next_counter(const std::string& key, int64_t wrap);

    void insert(const Message& m);
    bool exists(const std::string& id);
    std::optional<Message> get(const std::string& id);
    std::vector<Message> list(int limit);
    // Outgoing messages not yet sent (queued or sending), in the order they
    // will go: precedence first, then age.
    std::vector<Message> queue(int limit);
    // Event lines with seq > after, oldest first, at most limit.
    std::vector<Event> events(int64_t after, int limit);

    // The outgoing message to send next: highest precedence first, then the
    // oldest, skipping any still waiting out a retry delay.
    std::optional<Message> next_to_send(int64_t now);
    // The first in sending order whose destination `may` allows.
    std::optional<Message> next_to_send(int64_t now, const std::function<bool(const std::string& dst)>& may);

    void set_state(const std::string& id, const std::string& state, const std::string& error = "");
    void mark_sending(const std::string& id);
    void schedule_retry(const std::string& id, int64_t at, const std::string& error);
    // After a crash, anything left "sending" goes back to the queue.
    int requeue_interrupted();
    QueueCounts counts();

    void log_event(const std::string& msg_id, const std::string& text);

    // The plain copy of a codress message (codress.h): what the station sent
    // before it was enciphered, or what it read after deciphering, and the
    // key indicator. Kept so the operator can read it after the key is gone.
    void set_clear(const std::string& id, const std::string& text, const std::string& key);
    std::optional<std::pair<std::string, std::string>> clear(const std::string& id);  // text, key

    // Station cards (station.h) this station holds: other stations' public
    // keys, as their card text, with whether the operator checked the
    // fingerprint with that station by voice.
    struct CardRow {
        std::string station, card, fingerprint, source;
        bool verified = false;
        int64_t added = 0;
    };
    void put_card(const CardRow& c);
    std::optional<CardRow> card(const std::string& station);
    std::vector<CardRow> cards();
    void set_card_verified(const std::string& station, bool v);

private:
    void exec(const char* sql);
    sqlite3* db_ = nullptr;
};

int64_t now_utc();

}  // namespace pna
