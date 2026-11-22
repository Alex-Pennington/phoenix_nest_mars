#include "core.h"

#include <chrono>

namespace pna {

Core::Core(CoreConfig cfg, std::unique_ptr<ModemDriver> modem)
    : cfg_(std::move(cfg)), modem_(std::move(modem)), store_(cfg_.db_path) {}

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

std::string Core::submit(const std::string& to, char prec, const std::string& body, std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return std::string();
    };
    if (!valid_station(to)) return fail("station must be 1-16 characters of A-Z, 0-9 or '-'");
    if (to == cfg_.station) return fail("that is this station");
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
    m.state = "queued";
    m.created_at = m.updated_at = now_utc();
    store_.insert(m);
    log(m.id, std::string("queued ") + prec_name(prec) + " for " + to);
    return m.id;
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
    return s;
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
    if (on_air_ || !modem_->ready() || modem_->busy()) return;

    if (!acks_.empty()) {
        Frame a = acks_.front();
        if (modem_->send(encode(a))) {
            acks_.pop_front();
            on_air_ = a;
            log(a.id, "sending ack to " + a.dst);
        }
        return;
    }

    auto next = store_.next_to_send(now_utc());
    if (!next) return;
    Frame f;
    f.type = FrameType::Msg;
    f.id = next->id;
    f.src = next->src;
    f.dst = next->dst;
    f.prec = next->prec;
    f.body = next->body;
    store_.mark_sending(f.id);
    if (modem_->send(encode(f))) {
        on_air_ = f;
        log(f.id, "sending, attempt " + std::to_string(next->attempts + 1));
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
    on_air_.reset();

    if (f.type == FrameType::Ack) {
        if (!ok) log(f.id, "ack to " + f.dst + " failed: " + err);
        return;
    }
    if (ok) {
        store_.set_state(f.id, "sent");
        log(f.id, cfg_.send_acks ? "sent, waiting for ack" : "sent");
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
    std::lock_guard<std::mutex> g(mu_);
    for (const Frame& f : parser_.feed(bytes)) {
        if (f.dst != cfg_.station) {
            log(f.id, "heard traffic for " + f.dst + ", not for us");
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
            log(f.id, "duplicate from " + f.src + ", acking again");
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
            log(f.id, std::string("received ") + prec_name(f.prec) + " from " + f.src);
        }
        if (cfg_.send_acks) {
            Frame a;
            a.type = FrameType::Ack;
            a.id = f.id;
            a.src = cfg_.station;
            a.dst = f.src;
            a.prec = f.prec;
            acks_.push_back(a);
        }
    }
}

}  // namespace pna
