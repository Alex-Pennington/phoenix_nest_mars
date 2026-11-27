#include "training.h"

#include "api_server.h"
#include "bbs.h"
#include "cipher_slot.h"
#include "core.h"
#include "json.h"
#include "keys.h"
#include "loopback_modem.h"
#include "ops.h"
#include "received.h"
#include "station.h"

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <thread>

namespace pna {

namespace fs = std::filesystem;

namespace {

std::string call_api(Core& c, std::initializer_list<std::pair<std::string, std::string>> kv) {
    JsonWriter w;
    w.begin_object();
    for (auto& [k, v] : kv) w.field(k, v);
    w.end_object();
    return ApiServer::handle_call(c, w.str());
}
bool reply_ok(const std::string& reply, JsonValue* out = nullptr) {
    JsonValue v;
    if (!parse_json(reply, v)) return false;
    const JsonValue* ok = v.get("ok");
    bool good = ok && ok->type == JsonValue::Type::Bool && ok->b;
    if (out) *out = v;
    return good;
}
bool wait_until(const std::function<bool()>& cond, int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}
bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

// A sample addressed to this station: the routing indicator it was sent
// to (the first one on format line 2) becomes the station's call wherever
// it stands as a word, so it comes in as received, not overheard. A sample
// that doesn't read (a garbled one) stays as it was.
std::string addressed_to(const std::string& text, const std::string& call) {
    received::Message rm;
    std::string err;
    if (!received::parse(text, rm, &err) || rm.routing.empty()) return text;
    std::istringstream in(rm.routing[0]);
    std::string ri;
    in >> ri;
    while (!ri.empty() && (ri.back() == '/' || ri.back() == ',')) ri.pop_back();
    if (ri.size() < 3 || ri == call) return text;
    auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; };
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        size_t at = text.find(ri, i);
        if (at == std::string::npos) break;
        bool whole = (at == 0 || !word(text[at - 1])) && (at + ri.size() == text.size() || !word(text[at + ri.size()]));
        out.append(text, i, at - i);
        out += whole ? call : ri;
        i = at + ri.size();
    }
    out.append(text, i, std::string::npos);
    return out;
}

// The practice net control's own messages to the member, in its words.
const char* kWelcome =
    "WELCOME TO TRAINING. THIS IS TRAIN-NCS, A PRACTICE NET CONTROL INSIDE YOUR BOX.\n"
    "NOTHING YOU SEND IN TRAINING GOES ON THE AIR, AND NONE OF IT MIXES WITH YOUR REAL TRAFFIC.\n"
    "QSL THIS MESSAGE, THEN REPLY TO IT TO PRACTICE A REPLY.";
const char* kCheckin =
    "PRACTICE CHECK-IN. SEND TRAIN-NCS A MESSAGE WITH YOUR STATION STATUS:\n"
    "POWER, ANTENNA, RADIO AND HOW LONG YOU CAN STAY ON THE AIR.\n"
    "IT WILL BE ACKNOWLEDGED LIKE A REAL ONE.";
const char* kSecret =
    "THIS MESSAGE CAME ENCIPHERED IN YOUR PRACTICE KEY. YOU ARE READING IT\n"
    "BECAUSE THE PRACTICE TAPE SEALED TO YOUR CARD IS LOADED.";

}  // namespace

Training::Training(std::string dir, std::string call, std::shared_ptr<const Services> services)
    : dir_(std::move(dir)), call_(std::move(call)), services_(std::move(services)) {}

Training::~Training() { stop(); }

// The practice net's traffic is ACP 127: it goes in the first service whose
// table offers that format (docs/api.md, Services), never a name in code.
std::string Training::practice_service() const {
    if (services_)
        for (const Service* x : services_->all())
            if (x->offers("ACP127_PLAINDRESS")) return x->name;
    return "";
}

bool Training::make(const std::string& call, std::unique_ptr<Core>& core, LoopbackModem*& modem, std::string* error) {
    CoreConfig cfg;
    cfg.rx_service = practice_service();  // the practice link carries the practice net's service
    cfg.station = call;
    cfg.db_path = (fs::path(dir_) / (call + ".db")).string();
    cfg.send_acks = true;
    cfg.tx_gap_s = 0;
    cfg.loose_quiet_s = 1;
    auto m = std::make_unique<LoopbackModem>(0);
    modem = m.get();
    core = std::make_unique<Core>(std::move(cfg), std::move(m));
    std::string base = (fs::path(dir_) / call).string();
    core->set_ops(std::make_shared<Ops>(call, base + ".db"));
    core->set_services(services_);
    auto kr = std::make_shared<keys::Keyring>(base + ".keys");
    std::string err;
    kr->load(&err);
    core->set_keyring(kr);
    core->set_cipher_dir(base + ".cipher");
    core->set_cipher(cipher::load_installed(base + ".cipher"));
    auto id = std::make_shared<station::Identity>();
    bool exists = false;
    std::string ip = base + ".id";
    if (!station::load_identity(ip, *id, &exists, &err)) return fail(error, "practice station keys: " + err);
    if (!exists || id->station != call) {
        if (!station::make_identity(call, static_cast<int64_t>(std::time(nullptr)), *id, &err) ||
            !station::save_identity(ip, *id, &err))
            return fail(error, "practice station keys: " + err);
    }
    core->set_identity(id, ip);
    return true;
}

bool Training::start(std::string* error) {
    if (me_) return true;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec) return fail(error, "can't make the practice folder");
    if (call_.empty() || call_ == kNcs) call_ = "TRAINEE";
    if (!make(call_, me_, my_radio_, error) || !make(kNcs, ncs_, ncs_radio_, error)) {
        me_.reset();
        ncs_.reset();
        return false;
    }
    // Joined to each other only: no radio of the real station is on this channel.
    LoopbackModem::connect(*my_radio_, *ncs_radio_);
    me_->start();
    ncs_->start();
    since_ = static_cast<long long>(std::time(nullptr));
    fs::path marker = fs::path(dir_) / "filled";
    if (fs::exists(marker)) {
        std::ifstream in(marker);
        in >> played_;
        return true;
    }
    if (!fill(error)) {
        stop();
        return false;
    }
    std::ofstream(marker) << played_ << "\n";
    return true;
}

bool Training::fill(std::string* error) {
    // Cards, trusted as if met in person: the practice net control is this
    // station's keymaster in training only.
    station::Card card;
    std::string what, err;
    if (!me_->add_trusted_card(station::format_card(*ncs_->identity()), "training", true, true, card, what, &err) ||
        !ncs_->add_trusted_card(station::format_card(*me_->identity()), "training", false, true, card, what, &err))
        return fail(error, "practice cards: " + err);

    // A practice key tape, sealed to this station and sent over the practice
    // channel, so enciphered traffic opens.
    const std::string svc = practice_service();
    if (svc.empty()) return fail(error, "no service table offers ACP 127 plaindress: the practice net can't send");
    const std::string made = call_api(*ncs_, {{"cmd", "net_tape"}, {"net", "TRAINING NET"}, {"members", call_}, {"days", "31"}, {"send", "Y"}, {"service", svc}});
    if (!reply_ok(made)) return fail(error, "practice key tape not made");
    std::string opened;
    wait_until(
        [&] {
            for (auto& m : me_->list(50))
                if (m.direction == "in" && m.src == kNcs && reply_ok(call_api(*me_, {{"cmd", "key_open"}, {"id", m.id}}))) {
                    opened = m.id;
                    return true;
                }
            return false;
        },
        20000);
    if (opened.empty()) return fail(error, "the practice key tape didn't arrive");

    // The sample traffic, heard as bare text the way gale-rx hands it over.
    played_ = 0;
    for (auto& [name, text] : training_samples()) {
        std::string t = addressed_to(text, call_);
        if (!t.empty() && t.back() != '\n') t += "\r\n";
        const size_t before = me_->list(5000).size();
        ncs_radio_->play(t);
        // One at a time, as heard on the air: the next comes once this one is in.
        wait_until([&] { return me_->list(5000).size() > before; }, 2500);
        ++played_;
    }

    // Three messages to this station: two in the clear, one enciphered.
    auto send = [&](const char* prec, const std::string& text, const std::string& key) {
        std::map<std::string, std::string> r = {{"cmd", "compose_send"},
                                                {"service", svc},
                                                {"template", "FREE_FORM"},
                                                {"procedure", key.empty() ? "ACP127_PLAINDRESS" : "ACP127_CODRESS"},
                                                {"prec", prec},
                                                {"routing", call_},
                                                {"originator", kNcs},
                                                {"from", "TRAINING NET CONTROL " + std::string(kNcs)},
                                                {"to", call_},
                                                {"to_station", call_},
                                                {"text", text}};
        if (!key.empty()) r["key"] = key;
        JsonWriter w;
        w.begin_object();
        for (auto& [k, x] : r) w.field(k, x);
        w.end_object();
        return reply_ok(ApiServer::handle_call(*ncs_, w.str()));
    };
    auto from_ncs = [&] {
        int got = 0;
        for (auto& m : me_->list(5000)) got += m.direction == "in" && m.src == kNcs;
        return got;
    };
    const int had = from_ncs();
    int sent = 0;
    sent += send("P", kWelcome, "");
    sent += send("R", kCheckin, "");
    std::string key;
    if (keys::Keyring* kr = ncs_->keyring())
        for (auto& k : kr->for_sending())
            if (k.net == "TRAINING NET") key = k.indicator;
    if (!key.empty()) sent += send("R", kSecret, key);
    wait_until([&] { return from_ncs() >= had + sent; }, 20000);
    return true;
}

void Training::stop() {
    // A BBS engine made for a practice core goes with it (one per core, keyed
    // by the core's address, which a later core may reuse).
    if (me_) bbs::stop(*me_);
    if (ncs_) bbs::stop(*ncs_);
    if (me_) me_->stop();
    if (ncs_) ncs_->stop();
    me_.reset();
    ncs_.reset();
    my_radio_ = ncs_radio_ = nullptr;
}

bool Training::reload(std::string* error) {
    stop();
    std::error_code ec;
    fs::remove_all(dir_, ec);
    played_ = 0;
    return start(error);
}

}  // namespace pna
