// The headless core: owns the store, the send queue and the modem, and
// answers to whatever front end is attached through the local API.
#pragma once

#include <map>

#include "cipher_slot.h"
#include "frame.h"
#include "gale_key.h"
#include "keys.h"
#include "station.h"
#include "link_monitor.h"
#include "ops.h"
#include "modem.h"
#include "tuner.h"
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
    std::string station;      // our station name, e.g. "AAM499"
    std::string db_path;      // SQLite file
    int max_attempts = 3;     // modem failures before a message is marked failed
    int retry_delay_s = 10;   // first retry delay; doubles each time
    bool send_acks = true;    // answer each received message with an ACK frame
    // Hold each message until greybeard-ale reports a link to its station
    // (needs a link monitor). ACKs are not held: they answer a station just heard.
    bool link_gate = false;
    // A message that went out and got no ack goes again after this long, up
    // to ack_retries more times, then waits as unacked (as CP's chat does,
    // src/cp dialogchat.cpp). It goes again by itself when its station is
    // heard. 0: no timer.
    int ack_timeout_s = 120;
    int ack_retries = 2;
    // Seconds to wait after the last send or the last received byte before
    // keying up again, ACKs included, so a half-duplex far end has turned
    // round (CP waits 6 s). 0: none.
    int tx_gap_s = 0;
    // Bare text with no PNA1 frame (an MSC station's traffic) that stops
    // part-way is taken as it stands after this many quiet seconds.
    int loose_quiet_s = 5;
    // With link_gate: a message that has waited this long for a link to its
    // station gives up and fails (requeue sends it again). 0: waits forever.
    int link_wait_s = 0;
    // Handling times by precedence, in minutes (FLASH, IMMEDIATE, PRIORITY,
    // ROUTINE). A message still not sent past its time is said once in the
    // log and shown as overdue. 0 for a precedence: no warning.
    int handling_min[4] = {10, 30, 180, 360};
    // A message still queued or unacked this many hours after it was filed
    // is marked expired and not sent (requeue sends it again). 0: never.
    int expire_h = 0;
    // Every burst heard, as heard, goes into a day file here
    // (YYYY-MM-DD.log). Empty: no raw receive log.
    std::string raw_rx_dir{};
    // A long tape or bundle sent in numbered parts (parts.h) is held until
    // every part has come. A set still incomplete this long after a part
    // came drops that part; a repeat starts afresh. 0: held forever.
    int parts_hold_s = 72 * 3600;
    // Parts sent again from the resend queue, at most this many an hour
    // (each part counts once, however many stations asked for it). 0: no cap.
    int parts_resend_per_hour = 12;
    // A station that asks again for a part it has been sent again this many
    // times is flagged for the operator instead of being sent it once more.
    int parts_flag_after = 2;
};

// How a station is reached on the air.
//  pna1   our own frame and ACK (appliance to appliance; the default)
//  plain  the message text alone, as an MSC CP station sends it. Chat goes
//         in CP's chat form and is answered "ack|ID"; other traffic gets no
//         ack on the air.
bool valid_link_format(const std::string& f);

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
    bool emcon = false;       // transmit hold: nothing goes on the air
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
    // held: queued as held, so it waits until released.
    std::string submit(const std::string& to, char prec, const std::string& body, std::string* error,
                       bool held = false);

    // Ask 37, Alex's hard rule: no route between MARS and amateur, ever.
    // Why a send to `call` would cross into the amateur world ("" when it
    // would not): it is one of this station's own amateur addresses, it has
    // been heard on the amateur channels (station_view.h amateur_calls), or
    // the station book marks it amateur. Checked in code at every send path
    // and again at the airtime point, never left to configuration. A refused
    // send is an error; it is never rerouted.
    std::string world_refusal(const std::string& call) const;

    // Next value of a named counter, 1..wrap (see Store::next_counter).
    int64_t next_counter(const std::string& key, int64_t wrap);

    const std::string& station() const { return cfg_.station; }
    const std::string& db_path() const { return cfg_.db_path; }
    int parts_hold_s() const { return cfg_.parts_hold_s; }
    int parts_resend_per_hour() const { return cfg_.parts_resend_per_hour; }
    int parts_flag_after() const { return cfg_.parts_flag_after; }
    // The resend queue (Store::PartAsk). Held resends go to the queue by
    // themselves at their time (the next net slot); EMCON still holds them.
    void put_part_ask(const Store::PartAsk& a);
    void set_part_ask(const std::string& request_id, int part, const std::string& state, const std::string& sent_id,
                      int64_t at);
    std::vector<Store::PartAsk> part_asks();

    CoreStatus status();
    // greybeard-ale's link state (see link_monitor.h). Set before start().
    void set_link_monitor(std::shared_ptr<LinkMonitor> m) { link_ = std::move(m); }
    LinkMonitor* link_monitor() const { return link_.get(); }
    // The LDG tuner and the keying sequence around it (tuner.h). Set before
    // start(). Without one, or with one holding no daemon, the core still
    // records the channel tuner_channel names (gale-key keys on it); the
    // other tuner calls answer that no tuner is set up.
    void set_tuner(std::shared_ptr<TunerKeying> t) {
        tuner_ = t ? std::move(t) : std::make_shared<TunerKeying>(nullptr);
    }
    TunerKeying* tuner() const { return tuner_.get(); }
    // Whether a message to `station` would wait for a link now. A broadcast
    // (ALL) never waits: it goes out for whoever is listening.
    bool waits_for_link(const std::string& station) const;
    // The station book (ops.h): nets hear stations through it. Set before start().
    void set_ops(std::shared_ptr<Ops> ops) { ops_ = std::move(ops); }
    Ops* ops() const { return ops_.get(); }
    // The station's codress keys (keys.h). Set before start(); none: codress
    // is neither sent nor read.
    void set_keyring(std::shared_ptr<keys::Keyring> k) {
        keys_ = std::move(k);
        tie_cipher();
    }
    keys::Keyring* keyring() const { return keys_.get(); }
    // The cipher slot (cipher_slot.h): built-in pna-aes until a module is
    // installed. dir is where an installed module lives (STATION.cipher).
    // A new slot takes effect at once; the keyring follows it.
    void set_cipher(std::shared_ptr<cipher::Slot> s) {
        {
            std::lock_guard<std::mutex> g(cipher_mu_);
            cipher_ = s ? std::move(s) : cipher::Slot::builtin();
        }
        tie_cipher();
    }
    std::shared_ptr<cipher::Slot> cipher() const {
        std::lock_guard<std::mutex> g(cipher_mu_);
        return cipher_;
    }
    void set_cipher_dir(std::string d) { cipher_dir_ = std::move(d); }
    const std::string& cipher_dir() const { return cipher_dir_; }
    // Queues a codress copy and keeps its plain copy beside it (Store::set_clear).
    std::string submit_codress(const std::string& to, char prec, const std::string& codress, const std::string& clear,
                               const std::string& key, std::string* error);
    std::optional<std::pair<std::string, std::string>> clear(const std::string& id);
    // Writes a line to the station's event log (key loads and zeroizes).
    void note(const std::string& text);
    // The same, against one message (its EVENTS tab): operator marks, QSLs, retransmits.
    void note_on(const std::string& msg_id, const std::string& text);
    // Deletes these messages (the in box's purge, api_inbox.cpp, which picks
    // them) and writes one line saying so. Returns how many went.
    int purge(const std::vector<std::string>& ids, const std::string& why);

    // The station's own keypair (station.h). Set before start(); none: no
    // signing, no cards, no sealed tapes.
    void set_identity(std::shared_ptr<station::Identity> id, std::string path) {
        id_ = std::move(id);
        id_path_ = std::move(path);
    }
    const station::Identity* identity() const { return id_.get(); }
    // Replaces the keypair (a restore from backup) and saves it.
    bool replace_identity(const station::Identity& id, std::string* error);
    // Cards of other stations. card_for() gives this station's own card too.
    std::optional<station::Card> card_for(const std::string& call);
    // Adds a card. A station already held with other keys is refused unless
    // replace is set (a new card for a rebuilt station); then it must be
    // checked again. *what is "added", "same" or "replaced".
    bool add_card(const std::string& text, const std::string& source, bool replace, station::Card& card,
                  std::string& what, std::string* error);
    std::vector<Store::CardRow> cards();
    // The operator read the fingerprint back with the station by voice.
    bool verify_card(const std::string& call, const std::string& fingerprint, std::string* error);
    bool card_verified(const std::string& call);
    // Names a station as a keymaster (its sealed key tapes open here) or
    // takes that away. Naming needs the card checked by voice first.
    bool set_card_keymaster(const std::string& call, bool keymaster, std::string* error);
    bool card_keymaster(const std::string& call);
    // A card that arrived on a trusted path (a handout carried by hand, or
    // this station's own backup): held as checked, keymaster as given. A
    // different card already held for that station is refused unless
    // replace is set. *what as add_card's.
    bool add_trusted_card(const std::string& text, const std::string& source, bool keymaster, bool replace,
                          station::Card& card, std::string& what, std::string* error);
    // The member's choice of keymasters (docs/crypto.md). accept: the
    // fingerprint is checked against the card held, the card is marked
    // checked and keymaster, and any refusal is taken back; how says how it
    // was confirmed ("voice", "in person"). refuse: the keymaster mark comes
    // off and the refusal is kept with the card's fingerprint. Neither
    // removes the card: signed plain traffic still checks.
    bool accept_keymaster(const std::string& call, const std::string& fingerprint, const std::string& how,
                          std::string* error);
    bool refuse_keymaster(const std::string& call, std::string* error);
    // "accepted", "refused" (this fingerprint was refused) or "undecided".
    std::string keymaster_choice(const std::string& call, const std::string& fingerprint);
    void put_pending_handout(const Store::PendingHandout& p);
    void remove_pending_handout(const std::string& key);
    std::vector<Store::PendingHandout> pending_handouts();
    std::vector<Message> list(int limit);
    std::optional<Message> get(const std::string& id);
    std::vector<Message> queue(int limit);
    std::vector<Event> events(int64_t after, int limit);

    // Called for each event line (also written to the store's event table).
    void on_log(std::function<void(const std::string&)> fn) { log_fn_ = std::move(fn); }

    // Transmit hold (EMCON): nothing goes on the air, ACKs included, until
    // it is turned off. Kept across a restart.
    void set_emcon(bool on);
    bool emcon();
    // The operator's hand on the queue. Each returns false with *error when
    // the message isn't one of ours or isn't in a state it can leave.
    bool cancel(const std::string& id, std::string* error,     // not while on the air
                const std::string& by = "the operator");
    bool hold(const std::string& id, std::string* error);     // queued -> held
    bool release(const std::string& id, std::string* error);  // held -> queued
    bool requeue(const std::string& id, std::string* error);  // failed, unacked, cancelled, expired -> queued

    // Orderwire: operator-to-operator text, sent as typed with no frame, no
    // serial and no ack, ahead of traffic. Not kept as a message; the log
    // holds it. Held by EMCON like everything else.
    bool orderwire(const std::string& to, const std::string& text, std::string* error);
    // The end of today's raw receive log, at most max bytes, non-printing
    // bytes as dots. Empty when there is no raw log.
    std::string raw_rx_tail(size_t max);
    // Messages past their handling time and still not sent or acked.
    std::vector<std::string> overdue();
    // How a station is reached (LinkFormat above).
    bool set_link_format(const std::string& station, const std::string& format, std::string* error);
    std::string link_format(const std::string& station);
    std::vector<std::pair<std::string, std::string>> link_formats();

    // gale-key's caller (gale_key.h). Set before start(); none: nothing is
    // ever asked of gale-key and every burst stays written.
    void set_gale_key(std::shared_ptr<GaleKey> g);
    GaleKey* gale_key() const { return gk_.get(); }
    // The burst an operator's y would key: by message id, or by burst file
    // name. False with *error when gale-key may not be asked to key it: not a
    // written burst of ours, already asked, no world or not MARS (ask 40), a
    // station of the amateur world, no channel, or EMCON on.
    bool key_item(const std::string& msg_id, const std::string& burst, KeyItem& it, std::string* error);
    // The same checks again, then the burst is marked asked (once) just
    // before the key goes: false with *error when it may not go now.
    bool claim_key(const KeyItem& it, std::string* error);
    // gale-key never got the key: the burst may be asked again.
    void unclaim_key(const KeyItem& it);
    // The BBS marks each automatic send with its standing mode (machine,
    // routing, auto_qsl): once written, its burst is keyed under that
    // standing grant, only while the mode is on.
    void set_key_mode(const std::string& msg_id, const std::string& mode);
    // A mode turned off (BBS-SETUP, or the operator's standing_off): off here,
    // and gale-key is told to revoke it.
    void standing_off(const std::string& mode, const std::string& why);
    // ACK and orderwire bursts written to the outbox and not yet answered by
    // gale-key's result: keyed under standing "machine" only, MARS only.
    size_t answer_bursts();

    // For tests: run one scheduler pass now.
    void tick();

private:
    void loop();
    void handle_rx(const std::string& bytes);
    void decode_in(const ModemDecode& d);
    void handle_tx_done(bool ok, const std::string& err);
    // The outbox write-back: gale-key keyed a burst (sent from now) or
    // refused it (failed, with why).
    void handle_key_result(const std::string& burst, bool keyed, const std::string& why);
    void log(const std::string& msg_id, const std::string& text);
    void release_due_parts();  // caller holds mu_
    void read_codress(const std::string& id, const std::string& body);  // caller holds mu_
    // caller holds mu_ for these:
    void heard(const std::string& src);
    void no_ack(const Message& m, const std::string& why);
    void loose_in(const std::string& bytes);
    void cut_loose(bool flush);
    void plain_unit(const std::string& unit, bool complete);
    void plain_chat(const std::string& text);
    bool for_us(const std::string& call) const;
    void store_in(const std::string& id, const std::string& src, const std::string& dst, char prec,
                  const std::string& body, const std::string& state, const std::string& what);

    CoreConfig cfg_;
    std::unique_ptr<ModemDriver> modem_;
    Store store_;
    FrameParser parser_;

    std::mutex mu_;
    // An answer waiting to go out: a PNA1 ACK frame, or (raw set) CP's
    // "ack|ID" text for a plain station.
    struct Answer {
        Frame f;
        std::string raw;
        bool orderwire = false;
    };
    std::deque<Answer> acks_;
    std::optional<Frame> on_air_;      // what the modem is sending now
    bool on_air_plain_ = false;        // ... as bare text, no PNA1 frame
    std::string written_burst_;        // the send ended as this outbox file, not on the air yet
    bool emcon_ = false;
    std::chrono::steady_clock::time_point last_air_{};    // last send done or byte heard
    std::string loose_;                                   // bare text not yet cut into messages
    std::chrono::steady_clock::time_point last_loose_{};
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::function<void(const std::string&)> log_fn_;
    std::shared_ptr<LinkMonitor> link_;
    std::shared_ptr<TunerKeying> tuner_ = std::make_shared<TunerKeying>(nullptr);
    std::shared_ptr<Ops> ops_;
    std::shared_ptr<keys::Keyring> keys_;
    void tie_cipher();
    mutable std::mutex cipher_mu_;
    std::shared_ptr<cipher::Slot> cipher_ = cipher::Slot::builtin();
    std::string cipher_dir_;
    std::shared_ptr<station::Identity> id_;
    std::string id_path_;
    std::set<std::string> told_waiting_;  // messages already logged as waiting for a link
    std::map<std::string, std::chrono::steady_clock::time_point> waiting_since_;
    std::set<std::string> overdue_;       // messages logged as past their handling time
    std::set<std::string> no_expire_;     // expired, then requeued by the operator: not expired again
    void age_queue();                     // caller holds mu_
    // Automatic sends whose burst is written: asked of gale-key under their
    // standing mode, once each (caller holds mu_).
    void key_automatic();
    // The checks key_item and claim_key share (caller holds mu_).
    bool key_checks(const Store::KeyRow& r, std::string* error);
    std::shared_ptr<GaleKey> gk_;
    std::set<std::string> key_queued_;    // automatic keys handed to gale-key's thread, not yet claimed
    std::map<std::string, std::chrono::steady_clock::time_point> key_retry_;  // gale-key wasn't reached: not before
    std::set<std::string> key_told_;      // bursts already logged as never to be asked of gale-key
    // An ACK or orderwire written to the outbox (not a message: memory only).
    struct AnswerBurst {
        std::string about;    // the message it answers ("" for an orderwire)
        std::string what;     // "ack to STA-B", "orderwire to STA-B"
        std::string dst, world, channel;
        bool queued = false;  // handed to gale-key's thread, not yet claimed
        bool asked = false;   // gale-key was sent its key
        std::chrono::steady_clock::time_point retry{};  // gale-key wasn't reached: not before
    };
    std::map<std::string, AnswerBurst> answer_bursts_;  // by burst file name
    void key_answers(bool machine_on);  // caller holds mu_
    void raw_rx(const std::string& bytes);
    void note_file(const std::string& id, const std::string& body);
    std::mutex raw_mu_;
    int orderwire_seq_ = 0;
    std::string told_clock_;              // the clock source last logged
    std::chrono::steady_clock::time_point next_sched_{};  // when to look at net schedules again
};

}  // namespace pna
