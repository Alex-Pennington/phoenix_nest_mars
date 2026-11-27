// The core as the station controller's caller (greybeard-ale
// docs/station-controller.md; docs/api.md, "Keying: the core asks the station
// controller"). The file and class keep gale-key's name for one release; the
// requests are the controller's.
//
// The core is the networking controller and the station controller (gale-rx)
// is the link controller for the one radio (MIL-STD-188-141B D.4.2.8). The core
// never talks to gale-key: the controller orders it. Over the controller's
// socket (default /run/gale-rx/control.sock; one JSON object on one line per
// connection, one line back) each burst goes down as a network message in two
// parts: `message` ({"wav": ABS}), carried unopened, and `control` (the
// link-level addressee `to`, the `service`, the operator's `channel` label,
// kind, control, final, and the token or a standing mode).
//
//  - An operator's send: key_prepare asks the controller to `prepare`; the core
//    holds the one-shot token against that dialog and gives the kiosk the
//    confirm summary, with no token and no frequency. On the operator's Y
//    (key_confirm) the core sends `key` with the token in `control`.
//  - An automatic send (BBS routing, machine answers, auto-QSL): once its burst
//    is written, `key` with "standing": MODE, only while the core holds that
//    mode on. A mode goes on only through `prepare kind standing:MODE` and the
//    y (`grant`), and off with `revoke`. Until stage 5 the core asks for
//    automatic modes in MARS only, as it did before (greybeard-ale #18).
//  - The core's ACK and orderwire bursts key the same way, under "machine".
//
// The controller answers a `key` when it accepts the send, not when the key
// ends; the timeout bounds only that reply. The reply never changes a
// message's state: the result file BURST.key.json beside the burst is the one
// source of truth (docs/api.md, "Transmit: written, then sent").
//
// `status` gives the radio's channel and its service; that report is the core's
// radio_channel. It is read on this object's thread every poll interval, and
// whenever the core asks.
#pragma once

#include "json.h"

#include <atomic>
#include <chrono>
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

constexpr const char* kControllerSocket = "/run/gale-rx/control.sock";
constexpr int kControllerTimeoutMs = 5000;  // bounds the accept reply, never the key
constexpr const char* kGaleKeySocket = kControllerSocket;  // the old name, one release
constexpr int kGaleKeyTimeoutMs = kControllerTimeoutMs;
constexpr double kGaleKeyTokenS = 120.0;  // gale-key's token life, at most
constexpr double kGaleKeySoonestS = 0.5;  // gale-key refuses a key sooner than this after the prompt

// One reply from the controller.
struct GaleKeyReply {
    bool reached = false;   // the controller took the whole request line
    bool answered = false;  // and a reply line came back in time
    bool ok = false;
    bool accepted = false;  // a `key`: the controller took custody of the burst
    int exit = -1;          // gale-key's exit code, when the controller passes one on; -1 none
    std::string reason;     // the controller's reason, or ours when it wasn't reached or didn't answer
    JsonValue body;         // the whole reply, when answered
};

// The controller's report of where the radio is: a label and its service,
// never a frequency. known is false until the controller has answered.
struct RadioReport {
    bool known = false;
    std::string label, service, since, source;
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

// gale-key refused a key under a standing mode because it holds no grant for
// that mode (it never gave one, or revoked it). gale-key has no exit code of
// its own for this: it is exit 2, as every refusal is, so the reason's
// wording is read here and nowhere else.
bool gale_key_holds_no_grant(const GaleKeyReply& r, const std::string& mode);

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

// A burst the core may hand the controller: the message it carries, the
// burst's file name in the outbox, the message's service, the channel label
// it goes on, and the link-level addressee.
struct KeyItem {
    std::string msg_id, burst, service, channel, to;
    bool station_id = false;  // the service's table sends a station ID (the y shows it)
};

class SisControllerLink;

// link = controller: what became of a frame handed down over SIS.
//  keyed    S_UNIDATA_REQUEST_CONFIRM, or status.last says keyed
//  refused  S_UNIDATA_REQUEST_REJECTED, or status.last says refused
//  unknown  nobody is waiting on its answer and the controller's status does
//           not say: it may or may not have been keyed
enum class SisOutcome { keyed, refused, unknown };
constexpr const char* kSisResultUnknown = "result unknown: check the log before resending";

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

    // Runs around every `key` request (the operator's and the standing ones):
    // it is given the burst and a function that sends the key and returns
    // gale-key's reply. It returns that reply, or, when it did not send the
    // key, a reply with reached false and the reason.
    using Around = std::function<GaleKeyReply(const KeyItem&, const std::function<GaleKeyReply()>& send)>;
    void set_around(Around around);

    // A token held against one y dialog. The token never leaves this object.
    struct Held {
        std::string id;     // the prepare_id the kiosk holds
        std::string kind;   // "air" (one burst) or "standing" (a mode)
        KeyItem item;       // air
        std::string wav, sha256;  // air: the WAV gale-key bound the token to, and its bytes' sha256
        std::string mode, service;  // standing
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
    // prepare kind standing:MODE in one service, for the y that turns a mode on.
    Prepared prepare_standing(const std::string& mode, const std::string& service);

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
    // The mode goes off here only (gale-key already dropped it); nothing is sent.
    void forget(const std::string& mode);
    // Every mode granted for this service goes off here only; nothing is sent.
    // The controller's standing gate is per service (station-controller.md,
    // part 3). Returns the modes turned off.
    std::vector<std::string> forget_service(const std::string& service);
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

    // link = controller (docs/api.md, "Keying"): frames go down over STANAG 5066
    // SIS on this link, never as prepare, key or cancel on the local socket,
    // and their SIS answer is the result. The local socket keeps status, grant
    // and revoke. Set before the core's set_gale_key; this starts the link.
    void set_sis(std::shared_ptr<SisControllerLink> sis);
    bool controller_link() const { return sis_ != nullptr; }
    SisControllerLink* sis() const { return sis_.get(); }
    // Called once per frame handed down over SIS, from the link's thread or
    // this object's: keyed (S_UNIDATA_REQUEST_CONFIRM), refused (REJECTED) or,
    // for a frame settled from status.last (settle, below; no answer in time
    // and a dropped connection are settled so too), any of the three, with
    // why: the controller's
    // sentence from status.last under the frame's sha256, else the Annex A
    // reason. For a keyed frame, why names the channel status.last gives (""
    // when it gives none). For unknown it is kSisResultUnknown.
    using SisResult = std::function<void(const std::string& burst, SisOutcome outcome, const std::string& why)>;
    void set_sis_result(SisResult r);
    // Frames handed down whose answer nobody is waiting for (the core stopped
    // before it came): each is settled from the controller's status.last
    // under the frame's sha256, on this object's thread, through the SIS
    // result. keyed and refused are the controller's word. A frame status.last
    // does not name is unknown. One it names with no result yet (gale-key has
    // the order) waits, as does every one while status does not answer; the
    // wait ends as unknown after settle_wait_s. Status is read at once, again
    // at every bind of the link, and every settle_retry_ms while any waits.
    // because: said in the log, for example "the core restarted".
    void settle(const std::vector<KeyItem>& asked, const std::string& because);
    size_t unsettled();
    // The controller's status.send_channel for a service: the label it would
    // send on now. "" with *why (the controller's) when it names none.
    std::string send_channel(const std::string& service, std::string* why);

    // `status`, as the controller answers it. Every status read updates radio().
    GaleKeyReply status();
    // The last report of the radio's channel (status's `radio`).
    RadioReport radio();
    // Reads status now (blocking, up to the timeout).
    void refresh_radio();
    // Reads status on this object's thread every ms (0: only when asked).
    void set_poll_ms(int ms);

    // How the last request went, for the core's status: "" (nothing asked
    // yet), "answering", "busy" (it took the request and answered after the
    // timeout: a key runs until it ends) or "not answering", and why
    // (frequency-free).
    std::string last_state(std::string* why);

private:
    GaleKeyReply ask(const std::string& request);
    // Through around_. sha256: link = controller, the operator's y; the frame's
    // sha256 the y was given for ("" for an automatic send, which has no y).
    GaleKeyReply send_key(const KeyItem& it, const std::string& request, const std::string& sha256 = "");
    // {"cmd":CMD,"message":{"wav":ABS},"control":{to, service, channel, kind, control, final, EXTRA}}.
    // extra: members for `control`, as JSON text "\"k\":v,..." ("" none).
    std::string air_request(const char* cmd, const KeyItem& it, const std::string& control,
                            const std::string& extra) const;
    void take_radio(const GaleKeyReply& r);
    std::string wav_path(const std::string& burst) const;
    std::string new_id();
    void drop_expired();  // caller holds mu_
    void note(const std::string& msg_id, const std::string& text);
    void worker();

    // link = controller
    Prepared prepare_sis(const KeyItem& it);
    // Reads the frame once and submits those bytes. With sha256 given (the
    // y's), bytes of another sha256 are not submitted.
    GaleKeyReply submit_sis(const KeyItem& it, bool automatic, const std::string& sha256);
    // late: the answer of a request that had ended unanswered. It settles that
    // frame, waiting on status or already settled, and no other.
    void on_sis(const std::string& burst, const std::string& msg_id, const std::string& sha256, bool confirmed,
                const std::string& why, bool late);
    // A request that ended with no answer: its frame joins the unsettled.
    void unanswered(const std::string& burst, const std::string& msg_id, const std::string& sha256,
                    const std::string& because);
    std::shared_ptr<SisControllerLink> sis_;
    SisResult sis_result_;
    struct Unsettled {
        std::string burst, msg_id;
        std::string sha256;   // "" until read from the frame in the outbox
        std::string because;
        std::chrono::steady_clock::time_point until;
    };
    std::deque<Unsettled> unsettled_;
    bool settle_now_ = false;
    int settle_wait_s_ = 270, settle_retry_ms_ = 2000;  // the link's (SisLinkConfig)
    void settle_pass();  // on the worker thread

    std::shared_ptr<GaleKeyLink> link_;
    std::string outbox_;
    std::function<double()> clock_;
    std::function<void(const std::string&, const std::string&)> log_;
    Around around_;
    std::mutex mu_;
    std::map<std::string, Held> held_;
    std::map<std::string, bool> standing_;
    std::map<std::string, std::string> standing_service_;  // mode -> the service it was granted for
    std::string last_, last_why_;
    RadioReport radio_;
    int poll_ms_ = 0;

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
