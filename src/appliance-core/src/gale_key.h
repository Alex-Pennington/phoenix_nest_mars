// The core as gale-key's caller (mars-suite issue #7; greybeard-ale
// docs/gale-tx.md, "The operator's y" and "The service's interface").
//
// gale-key is greybeard-ale's one keying service: nothing else on the station
// keys the radio, and the core never does. The core only asks it to key a
// burst that is already in the outbox, over gale-key's socket
// (default /run/gale-key/key.sock): one JSON object on one line per
// connection, one line back.
//
//  - An operator's send: the kiosk opens its y dialog with key_prepare. The
//    core asks gale-key to `prepare` the key, holds the one-shot token against
//    that dialog, and gives the kiosk gale-key's confirm summary, with no
//    token and no frequency. On the operator's Y (key_confirm) the core sends
//    `key` with the token. key_cancel drops a held token; a held token dies
//    with gale-key's 120 s.
//  - An automatic send (BBS routing, machine mode answers, auto-QSL): once its
//    burst is written, the core sends `key` with "standing": MODE, only while
//    the core holds that mode on. A mode is turned on only through the same
//    prepare and y path (`prepare kind standing:MODE`, then `grant`), and off
//    with `revoke`. amateur_auto is never asked for (ask 40).
//
// The socket's reply never changes a message's state. gale-key writes
// BURST.key.json beside every burst in the outbox it is asked to key (keyed or
// refused), and that file is the one source of truth (docs/api.md, "Transmit:
// written, then sent"). With no gale-key answering, nothing keys: the burst
// stays written and the status says why.
#pragma once

#include "json.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace pna {

constexpr const char* kGaleKeySocket = "/run/gale-key/key.sock";
constexpr int kGaleKeyTimeoutMs = 5000;  // a reply this late is "not answered", never a result
constexpr double kGaleKeyTokenS = 120.0;  // gale-key's token life, at most
constexpr double kGaleKeySoonestS = 0.5;  // gale-key refuses a key sooner than this after the prompt

// One reply from gale-key.
struct GaleKeyReply {
    bool reached = false;   // gale-key took the whole request line
    bool answered = false;  // and a reply line came back in time
    bool ok = false;
    int exit = -1;          // gale-key's exit code (docs/gale-tx.md, Command line); -1 none
    std::string reason;     // gale-key's reason, or ours when it wasn't reached or didn't answer
    JsonValue body;         // the whole reply, when answered
};

// Where the core sends gale-key requests. Abstract so tests use a fake.
class GaleKeyLink {
public:
    virtual ~GaleKeyLink() = default;
    // request: one JSON object's text, no newline.
    virtual GaleKeyReply call(const std::string& request) = 0;
    virtual std::string where() const = 0;
};

// gale-key's Unix socket.
class GaleKeySocket : public GaleKeyLink {
public:
    GaleKeySocket(std::string path, int timeout_ms);
    GaleKeyReply call(const std::string& request) override;
    std::string where() const override { return path_; }

private:
    std::string path_;
    int timeout_ms_;
};

// The standing modes the core asks for: machine, routing, auto_qsl. Never
// amateur_auto (ask 40): the core sends nothing automatic into the amateur world.
bool core_standing_mode(const std::string& mode);
const std::vector<std::string>& core_standing_modes();

// Text for the kiosk with no frequency in it: s itself, or a fixed line when
// s names a frequency (compose::looks_like_frequency, or six digits or more in
// a row, a frequency in Hz).
std::string no_frequency(const std::string& s);

// The connection a request comes on. The API and web servers run one thread
// per connection and make one of these at its start. A y dialog belongs to
// the connection that opened it: it is confirmed only on that connection, and
// dropped when that connection closes (a page reloaded with the dialog open).
class KeyConnection {
public:
    KeyConnection();
    ~KeyConnection();  // drops this connection's held y dialogs, in every GaleKey
    KeyConnection(const KeyConnection&) = delete;
    KeyConnection& operator=(const KeyConnection&) = delete;
    // This thread's connection; 0 for none (a call the core makes itself, a test).
    static unsigned long long current();

private:
    unsigned long long id_;
};

// A burst the core may ask gale-key to key: the message it carries, the
// burst's file name in the outbox, the message's world and the channel the
// core sent it on.
struct KeyItem {
    std::string msg_id, burst, world, channel;
};

class GaleKey {
public:
    // outbox_dir: where the bursts are (gale-key is given absolute paths).
    GaleKey(std::shared_ptr<GaleKeyLink> link, std::string outbox_dir);
    ~GaleKey();
    GaleKey(const GaleKey&) = delete;
    GaleKey& operator=(const GaleKey&) = delete;

    // Monotonic seconds; tests set it to run a token past its life.
    void set_clock(std::function<double()> clock);
    // Log lines, against a message ("" for none). Never given a token.
    void set_log(std::function<void(const std::string& msg_id, const std::string& text)> log);
    std::string where() const { return link_ ? link_->where() : std::string(); }

    // A token held against one y dialog. The token never leaves this object.
    struct Held {
        std::string id;     // the prepare_id the kiosk holds
        std::string kind;   // "air" (one burst) or "standing" (a mode)
        KeyItem item;       // air
        std::string wav, sha256;  // air: the WAV gale-key bound the token to, and its bytes' sha256
        std::string mode;   // standing
        double at = 0, until = 0;
        unsigned long long conn = 0;  // the connection that opened it (KeyConnection)
        std::string token;
    };

    // What the kiosk is given for its y dialog.
    struct Prepared {
        bool ok = false;
        std::string error;       // frequency-free
        std::string prepare_id;
        int expires_s = 0;
        std::string confirm;     // JSON object text: gale-key's summary, frequency-free
    };
    // prepare an operator's key (control local). A new prepare for a burst
    // drops any older token held for that burst.
    Prepared prepare(const KeyItem& it);
    // prepare kind standing:MODE, for the y that turns a mode on.
    Prepared prepare_standing(const std::string& mode);

    // Takes a held token out for its y. False with *error, and the token still
    // held, when it is sooner than 0.5 s after its prepare. False with *error
    // when there is no such prepare, or it has expired (then it is gone).
    bool take(const std::string& prepare_id, Held& out, std::string* error);
    // Drops a held token. False when there was none (unknown, spent or expired).
    bool cancel(const std::string& prepare_id);
    // Drops every token held for a connection (it closed). Returns how many.
    size_t drop_connection(unsigned long long conn);
    // An air token's burst is the very one it was prepared for: the same WAV,
    // still there, with the same sha256. False with *error otherwise.
    bool unchanged(const Held& h, std::string* error) const;
    // How many tokens are held now (expired ones dropped first).
    size_t held();

    // `key` with a taken token (control local).
    GaleKeyReply key(const Held& h);
    // `grant` with a taken standing token; on gale-key's ok the mode is on here.
    GaleKeyReply grant(const Held& h);
    // The mode ("all" for every one) goes off here first, then `revoke` is sent.
    GaleKeyReply revoke(const std::string& mode, const std::string& why);
    bool standing_on(const std::string& mode);
    std::vector<std::string> standing();

    // An automatic key under a standing grant (control automatic), run on this
    // object's own thread. There, claim is called first and the key is sent
    // only when it returns true (the core marks the burst asked, once); then
    // done is called with gale-key's reply. A job still waiting when the
    // object halts is never sent and never claimed.
    void key_standing(const KeyItem& it, const std::string& mode, std::function<bool(const KeyItem&)> claim,
                      std::function<void(const KeyItem&, const GaleKeyReply&)> done);
    // Stops the thread; jobs not yet started are dropped. Called by the core's
    // stop() so no callback reaches a core that is going away.
    void halt();
    GaleKeyReply key_standing_now(const KeyItem& it, const std::string& mode);

    // `status`, as gale-key answers it.
    GaleKeyReply status();

    // How the last request went, for the core's status: "" (nothing asked
    // yet), "answering", "busy" (it took the request and answered after the
    // timeout: a key runs until it ends) or "not answering", and why
    // (frequency-free).
    std::string last_state(std::string* why);

private:
    GaleKeyReply ask(const std::string& request);
    std::string air_request(const char* cmd, const KeyItem& it, const std::string& control) const;
    std::string wav_path(const std::string& burst) const;
    std::string new_id();
    void drop_expired();  // caller holds mu_
    void note(const std::string& msg_id, const std::string& text);
    void worker();

    std::shared_ptr<GaleKeyLink> link_;
    std::string outbox_;
    std::function<double()> clock_;
    std::function<void(const std::string&, const std::string&)> log_;
    std::mutex mu_;
    std::map<std::string, Held> held_;
    std::map<std::string, bool> standing_;
    std::string last_, last_why_;

    struct Job {
        KeyItem item;
        std::string mode;
        std::function<bool(const KeyItem&)> claim;
        std::function<void(const KeyItem&, const GaleKeyReply&)> done;
    };
    std::deque<Job> jobs_;
    std::condition_variable cv_;
    std::thread thread_;
    bool stop_ = false;
};

}  // namespace pna
