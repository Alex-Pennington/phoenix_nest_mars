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

// Outgoing states: queued -> sending -> sent -> acked. Off that path:
// failed (the modem would not send it), unacked (no ack after every try),
// held (the operator holds it back), cancelled, expired (not through in
// time, core.h expire_h).
// Incoming states: received; overheard (traffic for another station, kept
// as CP keeps every burst, never acked); garbled (a damaged copy, kept so
// the operator can ask for a repeat, never acked); unverified (a modem
// decode, kept whole as it came, checked by nobody, never acked: core.cpp
// decode_in).
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
    int sends = 0;  // times it went out whole on the air (for the ack timer)
    // Every item carries its service (`world` keeps its old name until the
    // stage 2 sweep): a sent message's from its route, a received one's from
    // the channel it was heard on. "" is no service, never read as MARS.
    std::string world;
    std::string channel;  // the channel the operator picked for a send ("" none)
    std::string link;     // "plain": sent as the text alone (PLAIN); "" the station's link format
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
    int unacked = 0, held = 0, cancelled = 0, overheard = 0, garbled = 0, unverified = 0, written = 0;
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
    // Deletes a message and its plain copy (purge). Its event lines stay.
    void remove(const std::string& id);
    std::optional<Message> get(const std::string& id);
    std::vector<Message> list(int limit);
    // Outgoing messages not yet sent (queued or sending), in the order they
    // will go: precedence first, then age.
    std::vector<Message> queue(int limit);
    // Outgoing messages written to the outbox, waiting for their key, in
    // sending order.
    std::vector<Message> written_out(int limit);
    // Outgoing messages not yet through: queued, held or unacked, oldest first.
    std::vector<Message> unfinished(int limit);
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
    // Went out whole: state sent, one more send, and the time an ack is due
    // (0: none is expected).
    void mark_sent(const std::string& id, int64_t ack_due);
    // Written to the outbox as `burst`, not on the air yet (the write-back),
    // for `channel` (the designator the core sent it on; "" for none). A new
    // burst has not been asked of gale-key yet.
    void mark_written(const std::string& id, const std::string& burst, const std::string& channel = "");
    // What gale-key is asked about one written burst (gale_key.h).
    struct KeyRow {
        std::string id, dst, world, state, burst, channel;
        std::string mode;  // the standing mode of an automatic send; "" for the operator's own
        bool asked = false;
    };
    std::optional<KeyRow> key_row(const std::string& id);
    // Written bursts of automatic sends not yet asked of gale-key, oldest first.
    std::vector<KeyRow> written_automatic(int limit);
    // Marks an automatic send with its standing mode (machine, routing, auto_qsl);
    // "" makes it the operator's own again (his requeue or release).
    void set_key_mode(const std::string& id, const std::string& mode);
    // Marks the burst asked of gale-key, once: false when it already was, or the
    // message is no longer written with that burst.
    bool claim_key(const std::string& id, const std::string& burst);
    // gale-key never got the request: the burst may be asked again.
    void unclaim_key(const std::string& id, const std::string& burst);
    // The message of ours a burst file holds, if any.
    std::optional<std::string> id_by_burst(const std::string& burst);
    // Sent messages whose ack is overdue.
    std::vector<Message> overdue_acks(int64_t now);
    // Back to the queue with fresh counts (the operator's requeue, or a
    // station heard again). keep_sends keeps the ack-try count.
    void requeue(const std::string& id, bool keep_sends = false);
    // Outgoing messages to dst in one state.
    std::vector<Message> out_to(const std::string& dst, const std::string& state);
    // The outgoing message whose body starts with prefix (a CP chat's ID).
    std::optional<Message> out_with_prefix(const std::string& prefix);

    // How each station is reached (core.h LinkFormat): "pna1" (the
    // default) or "plain".
    void set_link_format(const std::string& station, const std::string& format);
    std::string link_format(const std::string& station);
    std::vector<std::pair<std::string, std::string>> link_formats();

    // A named flag in the meta table (EMCON survives a restart).
    void set_flag(const std::string& key, bool v);
    bool flag(const std::string& key);
    QueueCounts counts();

    void log_event(const std::string& msg_id, const std::string& text);

    // The plain copy of a codress message (codress.h): what the station sent
    // before it was enciphered, or what it read after deciphering, and the
    // key indicator. Kept so the operator can read it after the key is gone.
    void set_clear(const std::string& id, const std::string& text, const std::string& key);
    std::optional<std::pair<std::string, std::string>> clear(const std::string& id);  // text, key

    // Station cards (station.h) this station holds: other stations' public
    // keys, as their card text, with whether the operator checked the
    // fingerprint with that station by voice, and whether that station is
    // named as a keymaster here (only a keymaster's sealed key tapes open).
    struct CardRow {
        std::string station, card, fingerprint, source;
        bool verified = false;
        bool keymaster = false;
        int64_t added = 0;
    };
    void put_card(const CardRow& c);
    std::optional<CardRow> card(const std::string& station);
    std::vector<CardRow> cards();
    void set_card_verified(const std::string& station, bool v);
    void set_card_keymaster(const std::string& station, bool v);

    // The member's choice about a keymaster (docs/crypto.md, "Choosing your
    // keymasters"): refused, with the fingerprint refused. An accepted
    // keymaster is a card with its keymaster mark; a refusal is kept here so
    // the same keymaster isn't asked about again until the member changes
    // their mind.
    void set_keymaster_refused(const std::string& station, const std::string& fingerprint, int64_t at);
    void clear_keymaster_refused(const std::string& station);
    std::optional<std::string> keymaster_refused(const std::string& station);  // the fingerprint refused

    // Handouts waiting on the member's choice of keymaster: kept so the tape
    // they carry opens once the member accepts the issuer.
    struct PendingHandout {
        std::string key, issuer, text, packet;
        int64_t added = 0;
    };
    void put_pending_handout(const PendingHandout& p);
    void remove_pending_handout(const std::string& key);
    std::vector<PendingHandout> pending_handouts();

    // The resend queue for parts other stations asked for (docs/crypto.md,
    // Parts). One row per part asked for in one request. state: "waiting",
    // "held" (a resend made, held until `at`), "sent", "flagged" (asked for
    // again after being sent it often), "not_ours" (never sent to that
    // station from here) or "unchecked" (the request's signature didn't check).
    struct PartAsk {
        std::string request_id;
        int part = 0;
        std::string station, kind, tag, state, sent_id;
        int64_t asked = 0, at = 0;  // when asked; when the resend went or goes
    };
    void put_part_ask(const PartAsk& a);  // a new row; an existing one is kept
    void set_part_ask(const std::string& request_id, int part, const std::string& state, const std::string& sent_id,
                      int64_t at);
    std::vector<PartAsk> part_asks(const std::string& state = "");  // one state, or all

private:
    void exec(const char* sql);
    sqlite3* db_ = nullptr;
};

int64_t now_utc();

}  // namespace pna
