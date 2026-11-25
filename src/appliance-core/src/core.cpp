#include "core.h"

#include "acp201.h"
#include "codress.h"
#include "crypto.h"
#include "received.h"
#include "filemsg.h"
#include "parts.h"

#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>

#include <chrono>

namespace pna {

Core::Core(CoreConfig cfg, std::unique_ptr<ModemDriver> modem)
    : cfg_(std::move(cfg)), modem_(std::move(modem)), store_(cfg_.db_path) {
    emcon_ = store_.flag("emcon");
}

bool valid_link_format(const std::string& f) { return f == "pna1" || f == "plain"; }

Core::~Core() { stop(); }

void Core::log(const std::string& msg_id, const std::string& text) {
    // caller holds mu_ (the store is not shared across threads otherwise)
    store_.log_event(msg_id, text);
    if (log_fn_) log_fn_(msg_id.empty() ? text : msg_id + ": " + text);
}

void Core::start() {
    {
        std::lock_guard<std::mutex> g(mu_);
        int n = store_.requeue_interrupted();
        if (n) log("", std::to_string(n) + " interrupted message(s) put back in the queue");
        log("", "station " + cfg_.station + " starting on " + modem_->describe());
        if (emcon_) log("", "EMCON is on: nothing will be sent until it is turned off");
    }
    ModemDriver::Callbacks cb;
    cb.on_rx = [this](const std::string& b) { handle_rx(b); };
    cb.on_tx_done = [this](bool ok, const std::string& e) { handle_tx_done(ok, e); };
    cb.on_event = [this](const std::string& t) {
        std::lock_guard<std::mutex> g(mu_);
        log("", t);
    };
    modem_->start(cb);
    running_ = true;
    thread_ = std::thread([this] { loop(); });
}

void Core::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    modem_->stop();
}

std::string Core::submit(const std::string& to, char prec, const std::string& body, std::string* error, bool held) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return std::string();
    };
    if (!valid_station(to)) return fail("station must be 1-16 characters of A-Z, 0-9 or '-'");
    if (to == cfg_.station) return fail("that is this station");
    if (is_broadcast(cfg_.station)) return fail("this station can't be called ALL: ALL is the broadcast address");
    if (!valid_prec(prec)) return fail("precedence must be Z, O, P or R");
    if (body.empty()) return fail("message is empty");
    if (body.size() > kMaxBody) return fail("message is longer than " + std::to_string(kMaxBody) + " bytes");

    std::lock_guard<std::mutex> g(mu_);
    Message m;
    m.id = cfg_.station + "-" + std::to_string(store_.next_seq());
    m.direction = "out";
    m.src = cfg_.station;
    m.dst = to;
    m.prec = prec;
    m.body = body;
    m.state = held ? "held" : "queued";
    m.created_at = m.updated_at = now_utc();
    store_.insert(m);
    log(m.id, std::string(held ? "held " : "queued ") + prec_name(prec) + " for " + to);
    return m.id;
}

std::string Core::submit_codress(const std::string& to, char prec, const std::string& codress,
                                 const std::string& clear, const std::string& key, std::string* error) {
    std::string id = submit(to, prec, codress, error);
    if (id.empty()) return id;
    std::lock_guard<std::mutex> g(mu_);
    store_.set_clear(id, clear, key);
    log(id, "enciphered with key " + key);
    return id;
}

bool Core::replace_identity(const station::Identity& id, std::string* error) {
    if (!station::save_identity(id_path_, id, error)) return false;
    std::lock_guard<std::mutex> g(mu_);
    if (id_) id_->wipe();
    id_ = std::make_shared<station::Identity>(id);
    log("", "station keys restored from backup: fingerprint " + station::fingerprint(station::card_of(id)));
    return true;
}

std::optional<station::Card> Core::card_for(const std::string& call) {
    std::lock_guard<std::mutex> g(mu_);
    if (id_ && call == id_->station) return station::card_of(*id_);
    auto row = store_.card(call);
    if (!row) return std::nullopt;
    station::Card c;
    if (!station::parse_card(row->card, c, nullptr) || c.station != call) return std::nullopt;
    return c;
}

bool Core::add_card(const std::string& text, const std::string& source, bool replace, station::Card& card,
                    std::string& what, std::string* error) {
    if (!station::parse_card(text, card, error)) return false;
    std::lock_guard<std::mutex> g(mu_);
    if (id_ && card.station == id_->station) {
        if (error) *error = "that is this station's own card";
        return false;
    }
    std::string fp = station::fingerprint(card);
    auto old = store_.card(card.station);
    if (old && old->fingerprint == fp) {
        what = "same";
        return true;
    }
    if (old && !replace) {
        if (error)
            *error = "this station already holds a different card for " + card.station + " (fingerprint " +
                     old->fingerprint + "); the new one is " + fp +
                     ". If that station was rebuilt, check the new fingerprint with it by voice and import with replace";
        log("", "a different station card for " + card.station + " was refused (" + fp + ")");
        return false;
    }
    Store::CardRow r;
    r.station = card.station;
    r.card = station::card_text(text);
    r.fingerprint = fp;
    r.source = source;
    r.added = now_utc();
    store_.put_card(r);
    what = old ? "replaced" : "added";
    log("", "station card for " + card.station + " " + what + ", fingerprint " + fp + " (not yet checked by voice)");
    return true;
}

std::vector<Store::CardRow> Core::cards() {
    std::lock_guard<std::mutex> g(mu_);
    return store_.cards();
}

bool Core::verify_card(const std::string& call, const std::string& fingerprint, std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    auto row = store_.card(call);
    if (!row) {
        if (error) *error = "no card for " + call;
        return false;
    }
    std::string a, b;
    for (char c : fingerprint)
        if (c != ' ' && c != '-') a += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char c : row->fingerprint)
        if (c != ' ') b += c;
    if (a != b) {
        if (error) *error = "that is not " + call + "'s fingerprint on the card held here (" + row->fingerprint + ")";
        return false;
    }
    store_.set_card_verified(call, true);
    log("", "station card for " + call + " checked by voice");
    return true;
}

bool Core::card_verified(const std::string& call) {
    std::lock_guard<std::mutex> g(mu_);
    auto row = store_.card(call);
    return row && row->verified;
}

bool Core::set_card_keymaster(const std::string& call, bool keymaster, std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    auto row = store_.card(call);
    if (!row) {
        if (error) *error = "no card for " + call;
        return false;
    }
    if (keymaster && !row->verified) {
        if (error) *error = "check " + call + "'s fingerprint by voice first (card_check), then name it a keymaster";
        return false;
    }
    store_.set_card_keymaster(call, keymaster);
    log("", call + (keymaster ? " named a keymaster: its sealed key tapes will open here"
                              : " is no longer a keymaster here"));
    return true;
}

bool Core::card_keymaster(const std::string& call) {
    std::lock_guard<std::mutex> g(mu_);
    auto row = store_.card(call);
    return row && row->keymaster;
}

bool Core::add_trusted_card(const std::string& text, const std::string& source, bool keymaster, bool replace,
                            station::Card& card, std::string& what, std::string* error) {
    if (!station::parse_card(text, card, error)) return false;
    std::lock_guard<std::mutex> g(mu_);
    if (id_ && card.station == id_->station) {
        what = "own";
        return true;
    }
    std::string fp = station::fingerprint(card);
    auto old = store_.card(card.station);
    if (old && old->fingerprint != fp && !replace) {
        if (error)
            *error = "this station already holds a different card for " + card.station + " (" + old->fingerprint +
                     "); the one here is " + fp + ". Check it with " + card.station + " by voice before replacing";
        log("", "a different station card for " + card.station + " was refused (" + fp + ", from " + source + ")");
        return false;
    }
    Store::CardRow r;
    r.station = card.station;
    r.card = station::card_text(text);
    r.fingerprint = fp;
    r.source = source;
    r.added = old && old->fingerprint == fp ? old->added : now_utc();
    r.verified = true;
    r.keymaster = keymaster || (old && old->fingerprint == fp && old->keymaster);
    store_.put_card(r);
    what = !old ? "added" : old->fingerprint == fp ? "same" : "replaced";
    log("", "station card for " + card.station + " " + what + " from " + source + ", fingerprint " + fp +
                (r.keymaster ? ", keymaster" : ""));
    return true;
}

bool Core::accept_keymaster(const std::string& call, const std::string& fingerprint, const std::string& how,
                            std::string* error) {
    if (!verify_card(call, fingerprint, error)) return false;
    std::lock_guard<std::mutex> g(mu_);
    store_.set_card_keymaster(call, true);
    store_.clear_keymaster_refused(call);
    log("", "the member accepted " + call + " as a keymaster" + (how.empty() ? "" : " after a check " + how) +
                ": its sealed key tapes will open here");
    return true;
}

bool Core::refuse_keymaster(const std::string& call, std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    auto row = store_.card(call);
    if (!row) {
        if (error) *error = "no card for " + call;
        return false;
    }
    store_.set_card_keymaster(call, false);
    store_.set_keymaster_refused(call, row->fingerprint, now_utc());
    log("", "the member refused " + call + " (" + row->fingerprint + ") as a keymaster: its key tapes won't be taken");
    return true;
}

std::string Core::keymaster_choice(const std::string& call, const std::string& fingerprint) {
    std::lock_guard<std::mutex> g(mu_);
    if (id_ && call == id_->station) return "accepted";
    auto row = store_.card(call);
    if (row && row->fingerprint == fingerprint && row->keymaster) return "accepted";
    auto r = store_.keymaster_refused(call);
    if (r && *r == fingerprint) return "refused";
    return "undecided";
}

void Core::put_pending_handout(const Store::PendingHandout& p) {
    std::lock_guard<std::mutex> g(mu_);
    store_.put_pending_handout(p);
}

void Core::remove_pending_handout(const std::string& key) {
    std::lock_guard<std::mutex> g(mu_);
    store_.remove_pending_handout(key);
}

std::vector<Store::PendingHandout> Core::pending_handouts() {
    std::lock_guard<std::mutex> g(mu_);
    return store_.pending_handouts();
}

void Core::note(const std::string& text) {
    std::lock_guard<std::mutex> g(mu_);
    log("", text);
}

void Core::note_on(const std::string& msg_id, const std::string& text) {
    std::lock_guard<std::mutex> g(mu_);
    log(msg_id, text);
}

std::optional<std::pair<std::string, std::string>> Core::clear(const std::string& id) {
    std::lock_guard<std::mutex> g(mu_);
    return store_.clear(id);
}

void Core::tie_cipher() {
    auto s = cipher();
    if (!keys_) return;
    std::weak_ptr<cipher::Slot> w = s;
    keys_->set_cipher(
        s->info().name.empty() ? "none" : s->info().name,
        [w](const std::string& ind, const std::string& key, std::string* e) {
            auto sl = w.lock();
            std::string ready;
            if (!sl) return false;
            bool ok = sl->load_key(ind, key, ready, e);
            crypto::wipe(ready);
            return ok;
        },
        [w](const std::string& ind, const std::string& key) {
            auto sl = w.lock();
            std::string c;
            if (!sl || !sl->key_check(ind, key, c, nullptr)) return std::string("----");
            return c;
        });
}

void Core::read_codress(const std::string& id, const std::string& body) {
    std::string ind = codress::indicator_of(body);
    if (ind.empty()) return;
    auto slot = cipher();
    if (!slot->usable()) {
        log(id, "codress in key " + ind + " not read: " + slot->why_unusable());
        return;
    }
    std::string key;
    if (!keys_ || !keys_->receive_key(ind, key)) {
        log(id, "codress in key " + ind + ": this station holds no such key, so it can't be read here");
        return;
    }
    // ---- CIPHER HAND-OFF (receive) -----------------------------------------
    // The text as heard goes to the cipher here, at the same point where MSC's
    // CP hands received traffic to its cipher library, and comes back as the
    // clear message. Do not move this line. The enciphered-traffic header, its
    // checksum and compression are the library's side and are not read here.
    // docs/cp-link.md, "The cipher hand-off".
    // --------------------------------------------------------------------------
    std::string plain, err;
    bool ok = codress::decipher(*slot, body, key, plain, &err);
    crypto::wipe(key);
    if (!ok) {
        log(id, "codress in key " + ind + " could not be read: " + err);
        return;
    }
    store_.set_clear(id, plain, ind);
    log(id, "deciphered with key " + ind);
}

int64_t Core::next_counter(const std::string& key, int64_t wrap) {
    std::lock_guard<std::mutex> g(mu_);
    return store_.next_counter(key, wrap);
}

CoreStatus Core::status() {
    std::lock_guard<std::mutex> g(mu_);
    CoreStatus s;
    s.station = cfg_.station;
    s.modem = modem_->describe();
    s.modem_ready = modem_->ready();
    s.modem_busy = modem_->busy();
    s.sending = on_air_ ? on_air_->id + (on_air_->type == FrameType::Ack ? " (ack)" : "") : "";
    s.counts = store_.counts();
    s.frames_dropped = parser_.dropped();
    s.acks_waiting = static_cast<int>(acks_.size());
    s.link_gate = cfg_.link_gate;
    s.emcon = emcon_;
    if (link_) {
        s.link = link_->state();
        if (cfg_.link_gate)
            for (auto& m : store_.queue(1000))
                if (m.state == "queued" && !is_broadcast(m.dst) && !link_->link_up(m.dst)) ++s.held_for_link;
    }
    return s;
}

bool Core::waits_for_link(const std::string& station) const {
    if (is_broadcast(station)) return false;
    return cfg_.link_gate && (!link_ || !link_->link_up(station));
}

std::vector<Message> Core::list(int limit) {
    std::lock_guard<std::mutex> g(mu_);
    return store_.list(limit);
}

std::vector<Message> Core::queue(int limit) {
    std::lock_guard<std::mutex> g(mu_);
    return store_.queue(limit);
}

std::vector<Event> Core::events(int64_t after, int limit) {
    std::lock_guard<std::mutex> g(mu_);
    return store_.events(after, limit);
}

std::optional<Message> Core::get(const std::string& id) {
    std::lock_guard<std::mutex> g(mu_);
    return store_.get(id);
}

void Core::tick() {
    std::lock_guard<std::mutex> g(mu_);
    if (link_) {
        // Log when the clock source changes, so the log shows when message
        // times stopped (or started) coming from WWV.
        LinkState ls = link_->state();
        std::string c = ls.reachable ? (ls.clock == "wwv" ? "wwv" : "host") : "";
        if (!c.empty() && c != told_clock_) {
            log("", c == "wwv" ? "clock set by WWV" : "clock not set by WWV: times come from this computer's clock");
            told_clock_ = c;
        }
    }
    // Keys past their receive hold are zeroized (keys.h).
    if (keys_)
        for (auto& line : keys_->sweep()) log("", line);
    if (ops_ && std::chrono::steady_clock::now() >= next_sched_) {
        // Scheduled nets: open the ones set to open themselves, say the rest once.
        next_sched_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        for (auto& line : ops_->sched_tick()) log("", line);
    }
    // Messages whose ack is overdue go again, or wait as unacked.
    if (cfg_.ack_timeout_s > 0)
        for (auto& m : store_.overdue_acks(now_utc()))
            no_ack(m, "no ack from " + m.dst + " after " + std::to_string(cfg_.ack_timeout_s) + " s");
    // Resends held for a net slot go to the queue at its start.
    release_due_parts();
    // Handling times, expiry.
    age_queue();
    // Bare text that stopped part-way is taken as it stands.
    if (!loose_.empty() &&
        std::chrono::steady_clock::now() - last_loose_ >= std::chrono::seconds(cfg_.loose_quiet_s))
        cut_loose(true);

    if (emcon_) return;
    if (on_air_ || !modem_->ready() || modem_->busy()) return;
    if (cfg_.tx_gap_s > 0 && std::chrono::steady_clock::now() - last_air_ < std::chrono::seconds(cfg_.tx_gap_s))
        return;

    if (!acks_.empty()) {
        Answer a = acks_.front();
        if (modem_->send(a.raw.empty() ? encode(a.f) : a.raw + "\r\n")) {
            acks_.pop_front();
            on_air_ = a.f;
            on_air_plain_ = !a.raw.empty();
            if (a.orderwire) log("", "orderwire to " + a.f.dst + ": " + a.raw);
            else
                log(a.f.id, (a.raw.empty() ? "sending ack to " : "sending " + a.raw.substr(0, a.raw.find('|')) + " to ") +
                                a.f.dst);
        }
        return;
    }

    std::optional<Message> next;
    if (cfg_.link_gate) {
        next = store_.next_to_send(now_utc(), [this](const std::string& dst) {
            return !waits_for_link(dst);
        });
        // Say once per message that it waits, and for whom. One that has
        // waited link_wait_s gives up.
        auto now_s = std::chrono::steady_clock::now();
        for (auto& m : store_.queue(1000)) {
            if (m.state != "queued" || !waits_for_link(m.dst)) {
                waiting_since_.erase(m.id);
                continue;
            }
            auto since = waiting_since_.emplace(m.id, now_s).first->second;
            if (told_waiting_.insert(m.id).second) log(m.id, "waiting for a link to " + m.dst);
            if (cfg_.link_wait_s > 0 && now_s - since >= std::chrono::seconds(cfg_.link_wait_s)) {
                std::string why = "no link to " + m.dst + " after " + std::to_string(cfg_.link_wait_s) + " s";
                store_.set_state(m.id, "failed", why);
                log(m.id, "gave up: " + why + "; requeue to try again");
                waiting_since_.erase(m.id);
                told_waiting_.erase(m.id);
            }
        }
    } else {
        next = store_.next_to_send(now_utc());
    }
    if (!next) return;
    waiting_since_.erase(next->id);
    if (told_waiting_.erase(next->id)) log(next->id, "link to " + next->dst + " is up");
    Frame f;
    f.type = FrameType::Msg;
    f.id = next->id;
    f.src = next->src;
    f.dst = next->dst;
    f.prec = next->prec;
    f.body = next->body;
    const bool plain = !is_broadcast(f.dst) && store_.link_format(f.dst) == "plain";
    store_.mark_sending(f.id);
    if (modem_->send(plain ? f.body : encode(f))) {
        on_air_ = f;
        on_air_plain_ = plain;
        log(f.id, std::string(plain ? "sending as plain text" : "sending") + ", attempt " +
                      std::to_string(next->attempts + 1) +
                      (next->sends ? " (sent " + std::to_string(next->sends) + " time(s) before)" : ""));
    } else {
        store_.schedule_retry(f.id, now_utc() + 2, "modem would not start the send");
    }
}

void Core::loop() {
    while (running_) {
        tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Core::handle_tx_done(bool ok, const std::string& err) {
    std::lock_guard<std::mutex> g(mu_);
    if (!on_air_) return;
    Frame f = *on_air_;
    const bool plain = on_air_plain_;
    on_air_.reset();
    on_air_plain_ = false;
    last_air_ = std::chrono::steady_clock::now();

    if (f.type == FrameType::Ack) {
        const bool ow = f.id.compare(0, 3, "OW-") == 0;
        if (!ok) log(ow ? "" : f.id, (ow ? "orderwire to " : "ack to ") + f.dst + " failed: " + err);
        return;
    }
    if (ok) {
        auto now_m = store_.get(f.id);
        if (now_m && now_m->state == "acked") {
            log(f.id, "sent again, but it was acked meanwhile");
            return;
        }
        if (now_m && now_m->state == "cancelled") return;
        // Only our own frames and CP's chat form are answered on the air.
        const bool cp_chat = plain && f.body.compare(0, 5, "chat|") == 0;
        const bool awaits = !is_broadcast(f.dst) && cfg_.send_acks && (!plain || cp_chat);
        store_.mark_sent(f.id, awaits && cfg_.ack_timeout_s > 0 ? now_utc() + cfg_.ack_timeout_s : 0);
        if (is_broadcast(f.dst)) log(f.id, "sent to all stations, no ack expected");
        else if (plain && !cp_chat) log(f.id, "sent as plain text; this link has no ack on the air");
        else log(f.id, awaits ? "sent, waiting for ack" : "sent");
        return;
    }
    auto m = store_.get(f.id);
    int attempts = m ? m->attempts : cfg_.max_attempts;
    if (attempts >= cfg_.max_attempts) {
        store_.set_state(f.id, "failed", err);
        log(f.id, "failed after " + std::to_string(attempts) + " attempts: " + err);
    } else {
        int delay = cfg_.retry_delay_s << (attempts - 1);
        store_.schedule_retry(f.id, now_utc() + delay, err);
        log(f.id, "send failed (" + err + "), retry in " + std::to_string(delay) + " s");
    }
}

void Core::handle_rx(const std::string& bytes) {
    raw_rx(bytes);
    std::lock_guard<std::mutex> g(mu_);
    if (!bytes.empty()) last_air_ = std::chrono::steady_clock::now();
    auto frames = parser_.feed(bytes);
    for (const Frame& f : parser_.take_garbled()) {
        // The header checked, the body didn't: keep what came, ack nothing,
        // so the sender's ack timer sends it again.
        if (f.dst != cfg_.station && !is_broadcast(f.dst)) continue;
        heard(f.src);
        if (store_.exists(f.id)) continue;
        store_in(f.id + "-G" + std::to_string(now_utc()), f.src, f.dst, f.prec, f.body, "garbled",
                 "garbled copy of " + f.id + " from " + f.src + ": not acked, so it should come again; or ask for a repeat");
    }
    loose_in(parser_.take_loose());
    for (const Frame& f : frames) {
        // Any frame from a station puts it on frequency: open nets check it in,
        // and its unacked traffic goes again.
        heard(f.src);
        const bool to_all = is_broadcast(f.dst);
        if (f.dst != cfg_.station && !to_all) {
            if (f.type == FrameType::Msg && !store_.exists(f.id))
                store_in(f.id, f.src, f.dst, f.prec, f.body, "overheard",
                         std::string("overheard ") + prec_name(f.prec) + " from " + f.src + " for " + f.dst);
            else
                log(f.id, "heard traffic for " + f.dst + ", not for us");
            continue;
        }
        if (to_all && f.type == FrameType::Ack) {
            log(f.id, "heard an ack to ALL from " + f.src + ", ignored");
            continue;
        }
        if (f.type == FrameType::Ack) {
            auto m = store_.get(f.id);
            if (m && m->direction == "out" && m->dst == f.src) {
                if (m->state != "acked") {
                    store_.set_state(f.id, "acked");
                    log(f.id, "acked by " + f.src);
                }
            } else {
                log(f.id, "ack from " + f.src + " for a message we did not send");
            }
            continue;
        }
        if (store_.exists(f.id)) {
            log(f.id, to_all ? "heard the broadcast from " + f.src + " again"
                             : "duplicate from " + f.src + ", acking again");
        } else {
            Message m;
            m.id = f.id;
            m.direction = "in";
            m.src = f.src;
            m.dst = f.dst;
            m.prec = f.prec;
            m.body = f.body;
            m.state = "received";
            m.created_at = m.updated_at = now_utc();
            store_.insert(m);
            log(f.id, std::string("received ") + prec_name(f.prec) + " from " + f.src +
                          (to_all ? " to all stations" : ""));
            read_codress(f.id, f.body);
            note_file(f.id, f.body);
            // Station keys that came over the air wait for the operator.
            station::SealedHead sh;
            if (f.body.find("PNA STATION CARD 1") != std::string::npos)
                log(f.id, "a station card came in: load it with cards import " + f.id);
            parts::Head ph;
            std::string pk, ptag;
            std::vector<int> pmissing;
            if (parts::head(f.body, ph))
                log(f.id, "part " + std::to_string(ph.n) + " of " + std::to_string(ph.of) + " of " +
                              (ph.kind == "TAPE" ? "a sealed key tape" : "a startup bundle") + " (" + ph.tag + ") from " +
                              f.src + " came in: parts_list shows what is still missing");
            else if (parts::read_request(f.body, pk, ptag, pmissing))
                log(f.id, f.src + " asks for " + std::to_string(pmissing.size()) + " missing part(s) of " + ptag +
                              ": send them again with parts_resend " + f.id);
            if (station::sealed_head(f.body, sh) && id_ && sh.to == id_->station)
                log(f.id, "a sealed key tape for " + sh.net + " from " + sh.from + " came in: open it with keys open " + f.id);
        }
        // Nobody acks a broadcast: every station on the net would answer at once.
        if (cfg_.send_acks && !to_all) {
            Answer a;
            a.f.type = FrameType::Ack;
            a.f.id = f.id;
            a.f.src = cfg_.station;
            a.f.dst = f.src;
            a.f.prec = f.prec;
            acks_.push_back(a);
        }
    }
}

// ---- the operator's hand on the queue, EMCON and link formats ----

void Core::set_emcon(bool on) {
    std::lock_guard<std::mutex> g(mu_);
    if (emcon_ == on) return;
    emcon_ = on;
    store_.set_flag("emcon", on);
    log("", on ? "EMCON on: nothing is sent, acks included, until it is turned off"
               : "EMCON off: the queue goes out again");
}

bool Core::emcon() {
    std::lock_guard<std::mutex> g(mu_);
    return emcon_;
}

namespace {
bool refuse(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}
}  // namespace

bool Core::cancel(const std::string& id, std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    auto m = store_.get(id);
    if (!m || m->direction != "out") return refuse(error, "no message " + id + " of ours");
    if (on_air_ && on_air_->id == id && on_air_->type == FrameType::Msg)
        return refuse(error, id + " is on the air now; cancel it when the send ends");
    if (m->state != "queued" && m->state != "held" && m->state != "sent" && m->state != "unacked" &&
        m->state != "failed")
        return refuse(error, id + " is " + m->state + "; only queued, held, sent, unacked or failed traffic can be cancelled");
    store_.set_state(id, "cancelled", "cancelled by the operator");
    log(id, "cancelled by the operator (was " + m->state + ")");
    return true;
}

bool Core::hold(const std::string& id, std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    auto m = store_.get(id);
    if (!m || m->direction != "out") return refuse(error, "no message " + id + " of ours");
    if (m->state != "queued") return refuse(error, id + " is " + m->state + "; only queued traffic can be held");
    store_.set_state(id, "held", "held by the operator");
    log(id, "held by the operator");
    return true;
}

void Core::put_part_ask(const Store::PartAsk& a) {
    std::lock_guard<std::mutex> g(mu_);
    store_.put_part_ask(a);
}

void Core::set_part_ask(const std::string& request_id, int part, const std::string& state, const std::string& sent_id,
                        int64_t at) {
    std::lock_guard<std::mutex> g(mu_);
    store_.set_part_ask(request_id, part, state, sent_id, at);
}

std::vector<Store::PartAsk> Core::part_asks() {
    std::lock_guard<std::mutex> g(mu_);
    return store_.part_asks();
}

// Caller holds mu_.
void Core::release_due_parts() {
    const int64_t now = now_utc();
    std::set<std::string> done;
    for (auto& a : store_.part_asks("held")) {
        if (a.state != "held" || a.at > now) continue;
        if (!done.count(a.sent_id)) {
            auto m = store_.get(a.sent_id);
            if (m && m->state == "held") {
                store_.set_state(a.sent_id, "queued");
                log(a.sent_id, "resent part " + std::to_string(a.part) + " of " + a.tag + " released for the net slot");
            }
            done.insert(a.sent_id);
        }
        store_.set_part_ask(a.request_id, a.part, "sent", a.sent_id, a.at);
    }
}

bool Core::release(const std::string& id, std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    auto m = store_.get(id);
    if (!m || m->direction != "out") return refuse(error, "no message " + id + " of ours");
    if (m->state != "held") return refuse(error, id + " is " + m->state + ", not held");
    store_.set_state(id, "queued");
    log(id, "released to the queue");
    return true;
}

bool Core::requeue(const std::string& id, std::string* error) {
    std::lock_guard<std::mutex> g(mu_);
    auto m = store_.get(id);
    if (!m || m->direction != "out") return refuse(error, "no message " + id + " of ours");
    if (m->state != "failed" && m->state != "unacked" && m->state != "cancelled" && m->state != "held" &&
        m->state != "expired")
        return refuse(error, id + " is " + m->state +
                                 "; only failed, unacked, cancelled, expired or held traffic can be requeued");
    overdue_.erase(id);
    if (m->state == "expired") no_expire_.insert(id);
    store_.requeue(id, false);
    log(id, "put back in the queue by the operator (was " + m->state + ")");
    return true;
}

bool Core::set_link_format(const std::string& station, const std::string& format, std::string* error) {
    if (!valid_station(station) || is_broadcast(station)) return refuse(error, "give one station, not ALL");
    if (!valid_link_format(format)) return refuse(error, "link format must be pna1 or plain");
    std::lock_guard<std::mutex> g(mu_);
    store_.set_link_format(station, format);
    log("", station + (format == "plain" ? " is reached as plain text (an MSC CP station)" : " is reached with PNA1 frames"));
    return true;
}

std::string Core::link_format(const std::string& station) {
    std::lock_guard<std::mutex> g(mu_);
    return store_.link_format(station);
}

std::vector<std::pair<std::string, std::string>> Core::link_formats() {
    std::lock_guard<std::mutex> g(mu_);
    return store_.link_formats();
}

// ---- receiving: what helps the far station and the operator ----

void Core::heard(const std::string& who) {
    std::string src = who;
    if (src.size() > 3 && src.compare(src.size() - 3, 3, "-CM") == 0) src.resize(src.size() - 3);
    if (src.empty() || src == cfg_.station || is_broadcast(src)) return;
    if (ops_) ops_->heard(src);
    // Traffic that went unanswered goes again now the station is on frequency.
    auto un = store_.out_to(src, "unacked");
    if (un.empty()) return;
    for (auto& m : un) store_.requeue(m.id, false);
    log("", "heard " + src + ": sending its " + std::to_string(un.size()) + " unacked message(s) again");
}

void Core::no_ack(const Message& m, const std::string& why) {
    if (m.sends <= cfg_.ack_retries) {
        store_.requeue(m.id, true);
        log(m.id, why + ", sending again (" + std::to_string(m.sends + 1) + " of " +
                      std::to_string(cfg_.ack_retries + 1) + ")");
    } else {
        store_.set_state(m.id, "unacked", why);
        log(m.id, why + ", after " + std::to_string(m.sends) + " sends: unacked. It goes again when " + m.dst +
                      " is heard, or requeue it");
    }
}

bool Core::for_us(const std::string& call) const {
    return call == cfg_.station || (call.size() > cfg_.station.size() && call.compare(0, cfg_.station.size(), cfg_.station) == 0 &&
                                    call[cfg_.station.size()] == '-');
}

void Core::store_in(const std::string& id, const std::string& src, const std::string& dst, char prec,
                    const std::string& body, const std::string& state, const std::string& what) {
    Message m;
    m.id = id;
    m.direction = "in";
    m.src = src.empty() ? "UNKNOWN" : src;
    m.dst = dst.empty() ? "UNKNOWN" : dst;
    m.prec = valid_prec(prec) ? prec : 'R';
    m.body = body;
    m.state = state;
    m.created_at = m.updated_at = now_utc();
    store_.insert(m);
    log(id, what);
    if (state == "received") note_file(id, body);
}

namespace {
const char* const kStarts[] = {"chat|", "nack|", "ack|", "canx|", "ZCZC"};

std::string crc_id(const std::string& prefix, const std::string& text) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08X", crc32(text.data(), text.size()));
    return prefix + buf;
}

std::string first_word(const std::string& s) {
    std::istringstream in(s);
    std::string w;
    in >> w;
    return w;
}
}  // namespace

void Core::loose_in(const std::string& bytes) {
    if (bytes.empty()) return;
    loose_ += bytes;
    last_loose_ = std::chrono::steady_clock::now();
    if (loose_.size() > 2 * kMaxBody) loose_.erase(0, loose_.size() - 2 * kMaxBody);
    cut_loose(false);
}

// Cuts bare text into messages, as an MSC CP station sends them: ACP 127
// traffic from ZCZC to NNNN (several may come in one burst), CP chat from
// "chat|" to its "K DD-HHMMZ", CP's "ack|ID" answers, and older MARS chat
// lines. Anything else is noise between them.
void Core::cut_loose(bool flush) {
    static const std::regex cp_end(R"((K|AR) \d\d-\d{4}Z)");
    for (;;) {
        size_t best = std::string::npos;
        int which = -1;
        for (int i = 0; i < 5; ++i) {
            size_t p = loose_.find(kStarts[i]);
            if (p < best) best = p, which = i;
        }
        if (which == 4 && best > 0 && loose_[best - 1] == 'V') --best;
        // Whole lines before the first start: older MARS chat lines are chat.
        std::string head = loose_.substr(0, best == std::string::npos ? loose_.size() : best);
        size_t used = 0;
        for (size_t nl; (nl = head.find('\n', used)) != std::string::npos; used = nl + 1) {
            std::string line = head.substr(used, nl - used);
            acp201::Chat c;
            if (acp201::parse(line, c, nullptr)) plain_chat(line);
        }
        if (best == std::string::npos) {
            if (flush) {
                acp201::Chat c;
                if (acp201::parse(head.substr(used), c, nullptr)) plain_chat(head.substr(used));
                loose_.clear();
            } else {
                loose_.erase(0, used);
            }
            return;
        }
        loose_.erase(0, best);
        size_t end = std::string::npos;
        if (which == 0) {
            std::smatch mm;
            if (std::regex_search(loose_, mm, cp_end)) end = static_cast<size_t>(mm.position(0) + mm.length(0));
        } else if (which <= 3) {
            end = loose_.find_first_of("\r\n");
        } else {
            size_t n = loose_.find("NNNN");
            if (n != std::string::npos) end = n + 4;
        }
        // Another start before this one's end: this one was cut off.
        size_t next = std::string::npos;
        for (auto* m : kStarts) {
            size_t p = loose_.find(m, 6);
            if (p != std::string::npos && std::string(m) == "ZCZC" && loose_[p - 1] == 'V') --p;
            if (p < next && p > 0) next = p;
        }
        if (next != std::string::npos && (end == std::string::npos || next < end)) {
            plain_unit(loose_.substr(0, next), false);
            loose_.erase(0, next);
            continue;
        }
        if (end == std::string::npos) {
            if (!flush) return;
            plain_unit(loose_, which >= 1 && which <= 3);
            loose_.clear();
            return;
        }
        plain_unit(loose_.substr(0, end), true);
        loose_.erase(0, end);
    }
}

void Core::plain_chat(const std::string& text) {
    acp201::Chat c;
    if (!acp201::parse(text, c, nullptr)) return;
    std::string dst = first_word(c.to);
    const bool mine = for_us(dst) || is_broadcast(dst) || dst.compare(0, 4, "ALL-") == 0;
    std::string id = c.cp_id.empty() ? crc_id("CHAT-", text) : "CHAT-" + c.cp_id;
    if (store_.exists(id)) {
        log(id, "heard the chat from " + c.from + " again");
    } else {
        store_in(id, c.from, dst, 'R', text, mine ? "received" : "overheard",
                 std::string(mine ? "received chat from " : "overheard chat from ") + c.from +
                     (mine ? "" : " for " + dst) + (c.cp_id.empty() ? "" : " (CP chat form)"));
    }
    heard(c.from);
    // CP answers chat to its own call, never to ALL, and answers a repeat again.
    if (!c.cp_id.empty() && dst == cfg_.station && cfg_.send_acks) {
        Answer a;
        a.f.type = FrameType::Ack;
        a.f.id = id;
        a.f.src = cfg_.station;
        a.f.dst = c.from;
        a.f.prec = 'P';
        a.raw = acp201::build_cp_reply({acp201::CpReply::Kind::Ack, c.cp_id, ""});
        acks_.push_back(a);
    }
}

void Core::plain_unit(const std::string& raw, bool complete) {
    std::string u = printable(raw);
    size_t a = u.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return;
    u.erase(0, a);

    if (u.compare(0, 4, "ack|") == 0 || u.compare(0, 5, "nack|") == 0 || u.compare(0, 5, "canx|") == 0) {
        acp201::CpReply r;
        if (!acp201::parse_cp_reply(u.substr(0, u.find_first_of("\r\n")), r)) return;
        auto m = store_.out_with_prefix("chat|" + r.id + "|");
        const char* what = r.kind == acp201::CpReply::Kind::Ack ? "ack" : r.kind == acp201::CpReply::Kind::Nack ? "nack" : "canx";
        if (!m) {
            log("", std::string("heard ") + what + " for chat " + r.id + ", which we did not send");
            return;
        }
        heard(m->dst);
        if (r.kind == acp201::CpReply::Kind::Ack) {
            if (m->state != "acked") {
                store_.set_state(m->id, "acked");
                log(m->id, "acked by " + m->dst + (r.from.empty() ? "" : " via " + r.from));
            }
        } else if (r.kind == acp201::CpReply::Kind::Nack) {
            if (m->state == "sent") no_ack(*m, m->dst + " answered nack: its copy came garbled");
            else log(m->id, m->dst + " answered nack");
        } else {
            log(m->id, m->dst + " sent canx for this chat");
        }
        return;
    }

    if (u.compare(0, 5, "chat|") == 0) {
        acp201::Chat c;
        if (complete && acp201::parse(u, c, nullptr)) {
            plain_chat(u);
            return;
        }
        // A chat cut short: keep it, and answer nack if it was for us.
        size_t bar = u.find('|', 5);
        std::string cp_id = bar == std::string::npos ? "" : u.substr(5, bar - 5);
        size_t de = u.find(" DE ");
        std::string dst = bar != std::string::npos && de != std::string::npos && de > bar ? first_word(u.substr(bar + 1, de - bar - 1)) : "";
        std::string src = de != std::string::npos ? first_word(u.substr(de + 4)) : "";
        store_in(crc_id("CHAT-G", u), src, dst, 'R', u, "garbled",
                 "garbled chat" + (src.empty() ? std::string() : " from " + src) + ": ask for a repeat");
        if (!cp_id.empty() && dst == cfg_.station && !src.empty() && cfg_.send_acks) {
            Answer n;
            n.f.type = FrameType::Ack;
            n.f.id = "CHAT-" + cp_id;
            n.f.src = cfg_.station;
            n.f.dst = src;
            n.f.prec = 'P';
            n.raw = acp201::build_cp_reply({acp201::CpReply::Kind::Nack, cp_id, ""});
            acks_.push_back(n);
        }
        return;
    }

    // ACP 127 traffic, ZCZC to NNNN.
    received::Message rm;
    std::string err;
    const bool ok = complete && received::parse(u, rm, &err);
    std::string src = rm.originator;
    std::string id = crc_id("PLAIN-", u);
    if (store_.exists(id)) {
        log(id, "heard the plain traffic from " + (src.empty() ? std::string("UNKNOWN") : src) + " again");
        return;
    }
    if (!ok) {
        store_in(id, src, "", 'R', u, "garbled",
                 "garbled plain traffic" + (src.empty() ? std::string() : " from " + src) + " (" +
                     (complete ? err : "it stopped before NNNN") + "): ask for a repeat");
        return;
    }
    // Ours when any address on it names this station.
    auto names_us = [&](const std::string& s) {
        std::istringstream in(s);
        for (std::string w; in >> w;) {
            while (!w.empty() && (w.back() == '/' || w.back() == ',' || w.back() == '.')) w.pop_back();
            if (for_us(w)) return true;
        }
        return false;
    };
    bool mine = false;
    std::string dst;
    for (auto& r : rm.routing) mine = mine || names_us(r);
    for (auto* list : {&rm.to, &rm.info, &rm.xmt})
        for (auto& ad : *list) {
            if (dst.empty()) dst = first_word(ad.plain.empty() ? ad.ri : ad.plain);
            mine = mine || names_us(ad.ri) || names_us(ad.plain);
        }
    if (mine) dst = cfg_.station;
    char prec = rm.prec ? rm.prec : 'R';
    store_in(id, src, dst, prec, u, mine ? "received" : "overheard",
             std::string(mine ? "received " : "overheard ") + prec_name(prec) + " plain traffic from " +
                 (src.empty() ? std::string("UNKNOWN") : src));
    if (mine) read_codress(id, u);
    if (!src.empty()) heard(src);
}

// ---- ACP 127 handling: orderwire, handling times, expiry, raw receive log, files ----

bool Core::orderwire(const std::string& to, const std::string& text, std::string* error) {
    std::string dst = to.empty() ? std::string(kBroadcast) : to;
    if (!is_broadcast(dst) && !valid_station(dst)) return refuse(error, "station must be 1-16 characters of A-Z, 0-9 or '-'");
    if (text.empty()) return refuse(error, "orderwire is empty");
    if (text.size() > 512) return refuse(error, "orderwire is longer than 512 characters; send a message instead");
    for (char c : text)
        if (c == '\r' || c == '\n' || static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E)
            return refuse(error, "orderwire is one line of printable text");
    std::lock_guard<std::mutex> g(mu_);
    Answer a;
    a.f.type = FrameType::Ack;
    a.f.id = "OW-" + std::to_string(++orderwire_seq_);
    a.f.src = cfg_.station;
    a.f.dst = dst;
    a.f.prec = 'Z';
    a.raw = text;
    a.orderwire = true;
    acks_.push_back(a);
    log("", "orderwire to " + dst + " queued" + (emcon_ ? " (EMCON is on: it waits)" : ""));
    return true;
}

// Caller holds mu_.
void Core::age_queue() {
    int64_t now = now_utc();
    for (auto& m : store_.unfinished(1000)) {
        int64_t age = now - m.created_at;
        if (cfg_.expire_h > 0 && age >= int64_t(cfg_.expire_h) * 3600 && !no_expire_.count(m.id)) {
            std::string was = m.state;
            store_.set_state(m.id, "expired", "not through " + std::to_string(cfg_.expire_h) + " h after it was filed");
            overdue_.erase(m.id);
            log(m.id, "expired: still " + was + " " + std::to_string(cfg_.expire_h) +
                          " h after it was filed; requeue to send it again");
            continue;
        }
        int r = prec_rank(m.prec);
        int lim = (r >= 0 && r < 4) ? cfg_.handling_min[r] : 0;
        if (lim > 0 && age >= int64_t(lim) * 60 && overdue_.insert(m.id).second)
            log(m.id, std::string("past the handling time for ") + prec_name(m.prec) + " (" + std::to_string(lim) +
                          " min) and still " + m.state);
    }
}

std::vector<std::string> Core::overdue() {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::string> out;
    for (auto& m : store_.unfinished(1000))
        if (overdue_.count(m.id)) out.push_back(m.id);
    return out;
}

namespace {
std::string utc_stamp(bool day_only) {
    std::time_t t = std::time(nullptr);
    std::tm u{};
    gmtime_r(&t, &u);
    char buf[32];
    std::strftime(buf, sizeof buf, day_only ? "%Y-%m-%d" : "%Y-%m-%dT%H:%M:%SZ", &u);
    return buf;
}
}  // namespace

void Core::raw_rx(const std::string& bytes) {
    if (cfg_.raw_rx_dir.empty() || bytes.empty()) return;
    std::lock_guard<std::mutex> g(raw_mu_);
    std::error_code ec;
    std::filesystem::create_directories(cfg_.raw_rx_dir, ec);
    std::ofstream f(cfg_.raw_rx_dir + "/" + utc_stamp(true) + ".log", std::ios::app | std::ios::binary);
    if (!f) return;
    f << "== " << utc_stamp(false) << " " << bytes.size() << " bytes\n" << bytes << "\n";
}

std::string Core::raw_rx_tail(size_t max) {
    if (cfg_.raw_rx_dir.empty()) return "";
    std::lock_guard<std::mutex> g(raw_mu_);
    std::ifstream f(cfg_.raw_rx_dir + "/" + utc_stamp(true) + ".log", std::ios::binary);
    if (!f) return "";
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (all.size() > max) all.erase(0, all.size() - max);
    for (auto& c : all)
        if (c != '\n' && (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E)) c = '.';
    return all;
}

// Caller holds mu_.
void Core::note_file(const std::string& id, const std::string& body) {
    bool present = false;
    filemsg::File file;
    std::string err;
    if (filemsg::find(body, &present, file, &err))
        log(id, "a file came in: " + file.name + ", " + std::to_string(file.data.size()) + " bytes, CRC good");
    else if (present)
        log(id, "a file came in but can't be read: " + err);
}

}  // namespace pna
