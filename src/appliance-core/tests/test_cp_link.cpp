// What the MSC Communications Processor (src/cp) does with traffic, and our
// core now does too: a message with no ack goes again, then waits as
// unacked and goes again when its station is heard; EMCON; hold, release,
// cancel and requeue; a quiet gap before keying up; garbled and overheard
// copies kept; bare text from a CP station cut into messages; CP's chat
// form answered "ack|ID". Calls are made up; nothing here is CP's cipher,
// which stays an opaque step outside the core.
#include "../src/acp201.h"
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/frame.h"
#include "../src/json.h"
#include "check.h"
#include "services_fixture.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

// A modem the test drives: it keeps what was sent, and the test says when a
// send is done and what came in.
class ScriptModem : public ModemDriver {
public:
    void start(Callbacks cb) override { cb_ = std::move(cb); }
    void stop() override {}
    bool ready() const override { return true; }
    bool busy() const override {
        std::lock_guard<std::mutex> g(mu_);
        return on_air_;
    }
    bool send(const std::string& bytes) override {
        std::lock_guard<std::mutex> g(mu_);
        if (on_air_) return false;
        on_air_ = true;
        sent_.push_back(bytes);
        return true;
    }
    std::string describe() const override { return "script"; }

    void finish(bool ok = true) {
        {
            std::lock_guard<std::mutex> g(mu_);
            if (!on_air_) return;
            on_air_ = false;
        }
        cb_.on_tx_done(ok, ok ? "" : "test failure");
    }
    void rx(const std::string& b) { cb_.on_rx(b); }
    size_t count() const {
        std::lock_guard<std::mutex> g(mu_);
        return sent_.size();
    }
    std::string last() const {
        std::lock_guard<std::mutex> g(mu_);
        return sent_.empty() ? "" : sent_.back();
    }
    bool on_air() const { return busy(); }

private:
    Callbacks cb_;
    mutable std::mutex mu_;
    bool on_air_ = false;
    std::vector<std::string> sent_;
};

static bool wait_for(const std::function<bool()>& cond, int ms = 5000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}
static std::string state_of(Core& c, const std::string& id) {
    auto m = c.get(id);
    return m ? m->state : "";
}
static JsonValue api(Core& c, const std::string& line) {
    JsonValue v;
    parse_json(ApiServer::handle(c, line), v);
    return v;
}
static std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static bool ok(const JsonValue& v) {
    const JsonValue* o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}
static Frame msg_frame(const std::string& id, const std::string& src, const std::string& dst, const std::string& body) {
    Frame f;
    f.id = id;
    f.src = src;
    f.dst = dst;
    f.prec = 'R';
    f.body = body;
    return f;
}
static Frame ack_frame(const std::string& id, const std::string& src, const std::string& dst) {
    Frame f = msg_frame(id, src, dst, "");
    f.type = FrameType::Ack;
    return f;
}
// Finds the message in `in` from src, newest first.
static std::optional<Message> inbound(Core& c, const std::string& state) {
    for (auto& m : c.list(100))
        if (m.direction == "in" && m.state == state) return m;
    return std::nullopt;
}

static void chat_forms() {
    acp201::Chat c;
    c.to = "STA-B";
    c.from = "NCS-A";
    c.text = "NET OPENS AT 0200Z";
    c.hour = 1;
    c.minute = 55;
    c.day = 24;
    c.cp_id = acp201::cp_chat_id("NCS-A", 1795489200123LL);
    CHECK(c.cp_id.size() == 5 + 11 && c.cp_id.compare(0, 5, "NCS-A") == 0 && c.cp_id.substr(c.cp_id.size() - 3) == "123");
    std::string err, line = acp201::build(c, &err);
    CHECK(line.compare(0, 5 + c.cp_id.size() + 1, "chat|" + c.cp_id + "|") == 0);
    CHECK(line.find(" // \r\n\r\nNET OPENS AT 0200Z\r\n\r\n // K 24-0155Z") != std::string::npos);
    acp201::Chat back;
    CHECK(acp201::parse(line, back, &err) && back.cp_id == c.cp_id && back.text == c.text && back.to == "STA-B" &&
          back.from == "NCS-A" && back.day == 24 && back.hour == 1 && back.minute == 55 && !back.over);
    CHECK(acp201::looks_like_chat(line));
    // Instructions and AR.
    c.instructions = "QSL WHEN READY";
    c.over = true;
    line = acp201::build(c, &err);
    CHECK(line.find(" // QSL WHEN READY AR 24-0155Z") != std::string::npos);
    CHECK(acp201::parse(line, back, &err) && back.over && back.instructions == "QSL WHEN READY");
    // The older MARS line still reads.
    CHECK(acp201::parse("STA-B DE NCS-A // QRU U,A // 2001Z K", back, &err) && back.cp_id.empty() &&
          back.kind == acp201::Kind::QruUA && back.day == -1);
    // A bad ending is refused.
    CHECK(!acp201::parse("chat|NCS-A24015500123|STA-B DE NCS-A // HI // K 24-015Z", back, &err));
    // Answers.
    acp201::CpReply r;
    CHECK(acp201::parse_cp_reply("ack|NCS-A24015500123", r) && r.kind == acp201::CpReply::Kind::Ack &&
          r.id == "NCS-A24015500123" && r.from.empty());
    CHECK(acp201::parse_cp_reply("ack|NCS-A24015500123|STA-C\r\n", r) && r.from == "STA-C");
    CHECK(acp201::parse_cp_reply("nack|X1", r) && r.kind == acp201::CpReply::Kind::Nack);
    CHECK(acp201::parse_cp_reply("canx|X1", r) && r.kind == acp201::CpReply::Kind::Canx);
    CHECK(!acp201::parse_cp_reply("nack|X1|STA-C", r) && !acp201::parse_cp_reply("ack|", r) &&
          !acp201::parse_cp_reply("ackX1", r) && !acp201::parse_cp_reply("ok|X1", r));
    CHECK(acp201::build_cp_reply({acp201::CpReply::Kind::Nack, "X1", ""}) == "nack|X1");
}

static void parser_keeps_more() {
    FrameParser p;
    Frame good = msg_frame("STA-B-1", "STA-B", "NCS-A", "HELLO");
    std::string bad = encode(msg_frame("STA-B-2", "STA-B", "NCS-A", "WORLD"));
    bad[bad.find("WORLD") + 1] = '\x7f';  // the body no longer checks; the header does
    auto frames = p.feed("ZCZC SOME BARE TEXT\r\n" + encode(good) + bad + "NNNN\r\n");
    CHECK(frames.size() == 1 && frames[0].body == "HELLO");
    auto g = p.take_garbled();
    CHECK(g.size() == 1 && g[0].id == "STA-B-2" && g[0].body == "W.RLD");
    CHECK(p.take_loose() == "ZCZC SOME BARE TEXT\r\nNNNN\r\n" && p.take_loose().empty());
    CHECK(p.dropped() == 1);
    CHECK(printable(std::string("A\x01" "B\r\n", 5)) == "A.B\r\n");
}

static std::unique_ptr<Core> make_core(const fs::path& dir, const std::string& call, ScriptModem*& modem,
                                       std::function<void(CoreConfig&)> tune = nullptr) {
    fs::create_directories(dir);
    auto m = std::make_unique<ScriptModem>();
    modem = m.get();
    CoreConfig cfg{call, (dir / (call + ".db")).string(), 3, 1, true};
    cfg.ack_timeout_s = 1;
    cfg.ack_retries = 1;
    cfg.loose_quiet_s = 1;
    if (tune) tune(cfg);
    auto c = std::make_unique<Core>(cfg, std::move(m));
    c->set_services(test_services());
    c->start();
    return c;
}

// No ack: again, then unacked, then again when the station is heard.
static void no_ack(const fs::path& dir) {
    ScriptModem* m = nullptr;
    auto c = make_core(dir, "NCS-A", m);
    std::string err, id = c->submit("STA-B", 'P', "TEST ONE", &err, SendOpts::in("mars"));
    CHECK(wait_for([&] { return m->count() == 1; }));
    m->finish();
    CHECK(state_of(*c, id) == "sent" && c->get(id)->sends == 1);
    // No ack in a second: it goes again.
    CHECK(wait_for([&] { return m->count() == 2; }, 4000));
    m->finish();
    // Still none: it waits as unacked.
    CHECK(wait_for([&] { return state_of(*c, id) == "unacked"; }, 4000));
    CHECK(c->get(id)->sends == 2 && m->count() == 2);
    CHECK(c->status().counts.unacked == 1);
    // STA-B comes up with traffic of its own: we ack it, and ours goes again.
    m->rx(encode(msg_frame("STA-B-7", "STA-B", "NCS-A", "ARE YOU THERE")));
    CHECK(wait_for([&] { return m->count() == 3; }));
    CHECK(m->last().find("PNA1 ACK STA-B-7") != std::string::npos);
    m->finish();
    CHECK(wait_for([&] { return m->count() == 4; }));
    CHECK(m->last().find("PNA1 MSG " + id) != std::string::npos);
    m->finish();
    // Its ack arrives late: acked, and nothing more goes.
    m->rx(encode(ack_frame(id, "STA-B", "NCS-A")));
    CHECK(wait_for([&] { return state_of(*c, id) == "acked"; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    CHECK(m->count() == 4);
    // An ack while it is queued for another try stops the try.
    std::string id2 = c->submit("STA-B", 'R', "TEST TWO", &err, SendOpts::in("mars"));
    CHECK(wait_for([&] { return m->count() == 5; }));
    m->finish();
    CHECK(wait_for([&] { return m->count() == 6; }, 4000));
    m->rx(encode(ack_frame(id2, "STA-B", "NCS-A")));
    m->finish();
    CHECK(wait_for([&] { return state_of(*c, id2) == "acked"; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    CHECK(state_of(*c, id2) == "acked" && m->count() == 6);
    c->stop();
}

// EMCON, and the operator's hand on the queue.
static void operator_hand(const fs::path& dir) {
    ScriptModem* m = nullptr;
    auto c = make_core(dir, "NCS-A", m, [](CoreConfig& cfg) { cfg.ack_timeout_s = 0; });
    std::string err;
    CHECK(ok(api(*c, R"({"cmd":"emcon","on":"Y"})")));
    CHECK(c->emcon() && c->status().emcon);
    std::string id = c->submit("STA-B", 'Z', "FLASH TEST", &err, SendOpts::in("mars"));
    // A station heard while in EMCON is not acked on the air either.
    m->rx(encode(msg_frame("STA-B-9", "STA-B", "NCS-A", "CHECK")));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    CHECK(m->count() == 0 && state_of(*c, id) == "queued");
    // EMCON holds across a restart.
    c->stop();
    c.reset();
    c = make_core(dir, "NCS-A", m, [](CoreConfig& cfg) { cfg.ack_timeout_s = 0; });
    CHECK(c->emcon());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(m->count() == 0);
    // Hold it, lift EMCON: it stays.
    JsonValue r = api(*c, "{\"cmd\":\"hold\",\"id\":\"" + id + "\"}");
    CHECK(ok(r) && str(r, "state") == "held");
    CHECK(!ok(api(*c, "{\"cmd\":\"hold\",\"id\":\"" + id + "\"}")));
    CHECK(ok(api(*c, R"({"cmd":"emcon","on":"N"})")) && !c->emcon());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(m->count() == 0 && state_of(*c, id) == "held");
    // Release: it goes.
    CHECK(ok(api(*c, "{\"cmd\":\"release\",\"id\":\"" + id + "\"}")));
    CHECK(wait_for([&] { return m->count() == 1; }));
    // Not cancelled while on the air.
    r = api(*c, "{\"cmd\":\"cancel\",\"id\":\"" + id + "\"}");
    CHECK(!ok(r) && str(r, "error").find("on the air") != std::string::npos);
    m->finish(false);
    CHECK(wait_for([&] { return state_of(*c, id) == "queued"; }));
    CHECK(ok(api(*c, "{\"cmd\":\"cancel\",\"id\":\"" + id + "\"}")) && state_of(*c, id) == "cancelled");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(m->count() == 1);
    CHECK(!ok(api(*c, "{\"cmd\":\"release\",\"id\":\"" + id + "\"}")));
    // Requeue: fresh counts, and it goes.
    r = api(*c, "{\"cmd\":\"requeue\",\"id\":\"" + id + "\"}");
    CHECK(ok(r) && str(r, "state") == "queued" && c->get(id)->attempts == 0);
    CHECK(wait_for([&] { return m->count() == 2; }));
    m->finish();
    CHECK(!ok(api(*c, R"({"cmd":"cancel","id":"NOPE-1"})")) && !ok(api(*c, R"({"cmd":"requeue"})")));
    c->stop();
}

// The quiet gap before keying up.
static void tx_gap(const fs::path& dir) {
    ScriptModem* m = nullptr;
    auto c = make_core(dir, "NCS-A", m, [](CoreConfig& cfg) {
        cfg.tx_gap_s = 2;
        cfg.ack_timeout_s = 0;
    });
    std::string err;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    c->submit("STA-B", 'R', "ONE", &err, SendOpts::in("mars"));
    CHECK(wait_for([&] { return m->count() == 1; }));
    m->finish();
    auto done = std::chrono::steady_clock::now();
    c->submit("STA-B", 'R', "TWO", &err, SendOpts::in("mars"));
    CHECK(wait_for([&] { return m->count() == 2; }, 5000));
    CHECK(std::chrono::steady_clock::now() - done >= std::chrono::milliseconds(1900));
    m->finish();
    // A byte heard restarts the gap.
    m->rx("noise");
    auto heard = std::chrono::steady_clock::now();
    c->submit("STA-B", 'R', "THREE", &err, SendOpts::in("mars"));
    CHECK(wait_for([&] { return m->count() == 3; }, 5000));
    CHECK(std::chrono::steady_clock::now() - heard >= std::chrono::milliseconds(1900));
    m->finish();
    c->stop();
}

// Garbled and overheard copies are kept; neither is acked.
static void keeps_copies(const fs::path& dir) {
    ScriptModem* m = nullptr;
    auto c = make_core(dir, "NCS-A", m, [](CoreConfig& cfg) { cfg.ack_timeout_s = 0; });
    std::string bad = encode(msg_frame("STA-B-3", "STA-B", "NCS-A", "PLEASE REPEAT"));
    bad[bad.find("REPEAT")] = '\x02';
    m->rx(bad);
    m->rx(encode(msg_frame("STA-B-4", "STA-B", "STA-C", "FOR STATION C")));
    CHECK(wait_for([&] { return inbound(*c, "garbled").has_value() && inbound(*c, "overheard").has_value(); }));
    auto g = inbound(*c, "garbled");
    CHECK(g && g->body == "PLEASE .EPEAT" && g->src == "STA-B");
    auto o = inbound(*c, "overheard");
    CHECK(o && o->id == "STA-B-4" && o->dst == "STA-C" && o->body == "FOR STATION C");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(m->count() == 0);
    // The good copy, when it comes again, is received and acked.
    m->rx(encode(msg_frame("STA-B-3", "STA-B", "NCS-A", "PLEASE REPEAT")));
    CHECK(wait_for([&] { return state_of(*c, "STA-B-3") == "received" && m->count() == 1; }));
    m->finish();
    CHECK(c->status().counts.garbled == 1 && c->status().counts.overheard == 1);
    c->stop();
}

// A CP station: plain text both ways, CP chat with its ack.
static void cp_station(const fs::path& dir) {
    ScriptModem* m = nullptr;
    auto c = make_core(dir, "NCS-A", m);
    JsonValue r = api(*c, R"({"cmd":"link_format","station":"STA-C","format":"plain"})");
    CHECK(ok(r) && str(r, "format") == "plain");
    CHECK(!ok(api(*c, R"({"cmd":"link_format","station":"STA-C","format":"morse"})")));
    CHECK(!ok(api(*c, R"({"cmd":"link_format","station":"ALL","format":"plain"})")));
    CHECK(c->link_format("STA-B") == "pna1");

    // Formal traffic goes as its text alone, with no ack expected.
    std::string text = "VZCZC\r\nRR STA-C DE NCS-A #0001 3280155\r\nBT\r\nUNCLAS\r\nTEST\r\nBT\r\nNNNN\r\n";
    std::string err, id = c->submit("STA-C", 'R', text, &err, SendOpts::in("mars"));
    CHECK(wait_for([&] { return m->count() == 1; }));
    CHECK(m->last() == text);
    m->finish();
    CHECK(state_of(*c, id) == "sent");
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    CHECK(state_of(*c, id) == "sent" && m->count() == 1);

    // Chat goes in CP's form, and CP's ack|ID answers it.
    r = api(*c, R"({"cmd":"send_chat","service":"mars","call":"STA-C","station":"NCS-A","text":"QSL TEST","to_station":"STA-C"})");
    CHECK(ok(r) && str(r, "message").compare(0, 10, "chat|NCS-A") == 0);
    std::string chat_id = str(r, "id"), body = str(r, "message");
    std::string cp_id = body.substr(5, body.find('|', 5) - 5);
    CHECK(wait_for([&] { return m->count() == 2; }));
    CHECK(m->last() == body);
    m->finish();
    CHECK(state_of(*c, chat_id) == "sent");
    m->rx("..noise..ack|" + cp_id + "\r\n");
    CHECK(wait_for([&] { return state_of(*c, chat_id) == "acked"; }));

    // A burst from STA-C: two messages, then a CP chat to us.
    std::string in1 = "VZCZC\r\nRR NCS-A DE STA-C #0011 3280200\r\nBT\r\nUNCLAS\r\nFIRST\r\nBT\r\nNNNN\r\n";
    std::string in2 = "VZCZC\r\nRR STA-D DE STA-C #0012 3280201\r\nBT\r\nUNCLAS\r\nSECOND\r\nBT\r\nNNNN\r\n";
    std::string cp_chat = "chat|STA-C24020200555|NCS-A DE STA-C // \r\n\r\nHOW COPY\r\n\r\n // K 24-0202Z";
    m->rx("\x7f" + in1.substr(0, 20));
    m->rx(in1.substr(20) + in2 + cp_chat + "\r\n");
    CHECK(wait_for([&] { return m->count() == 3; }));
    CHECK(m->last() == "ack|STA-C24020200555\r\n");
    m->finish();
    int recv = 0, over = 0;
    bool chat_in = false;
    for (auto& x : c->list(100)) {
        if (x.direction != "in") continue;
        if (x.state == "received" && x.body.find("FIRST") != std::string::npos && x.src == "STA-C" && x.dst == "NCS-A") ++recv;
        if (x.state == "overheard" && x.body.find("SECOND") != std::string::npos) ++over;
        if (x.id == "CHAT-STA-C24020200555" && x.state == "received" && x.src == "STA-C") chat_in = true;
    }
    CHECK(recv == 1 && over == 1 && chat_in);
    // The same chat again is acked again but kept once.
    m->rx(cp_chat + "\r\n");
    CHECK(wait_for([&] { return m->count() == 4; }));
    m->finish();
    // A message cut short is kept as garbled once the channel goes quiet.
    m->rx("VZCZC\r\nRR NCS-A DE STA-C #0013 3280203\r\nBT\r\nUNCLAS\r\nTHIRD AND");
    CHECK(wait_for([&] { return inbound(*c, "garbled").has_value(); }, 4000));
    auto g = inbound(*c, "garbled");
    CHECK(g && g->body.find("THIRD AND") != std::string::npos);
    // A CP chat to us cut short gets nack|ID.
    m->rx("chat|STA-C24020400001|NCS-A DE STA-C // \r\n\r\nWHAT IS YOUR");
    CHECK(wait_for([&] { return m->count() == 5; }, 4000));
    CHECK(m->last() == "nack|STA-C24020400001\r\n");
    m->finish();
    // CP's nack for our chat sends it again.
    r = api(*c, R"({"cmd":"send_chat","service":"mars","call":"STA-C","station":"NCS-A","text":"AGAIN","to_station":"STA-C"})");
    std::string id3 = str(r, "id"), body3 = str(r, "message");
    CHECK(wait_for([&] { return m->count() == 6; }));
    m->finish();
    m->rx("nack|" + body3.substr(5, body3.find('|', 5) - 5) + "\r\n");
    CHECK(wait_for([&] { return m->count() == 7; }));
    CHECK(m->last() == body3);
    m->finish();
    c->stop();
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_cp_link_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    chat_forms();
    parser_keeps_more();
    no_ack(dir / "noack");
    operator_hand(dir / "hand");
    tx_gap(dir / "gap");
    keeps_copies(dir / "copies");
    cp_station(dir / "cp");
    fs::remove_all(dir);
    return check::report("test_cp_link");
}
