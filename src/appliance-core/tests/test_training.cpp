// Training mode with more than one connection open (phoenix_nest_mars#3,
// carried asks).
//
// gale-web and the kiosk can each hold a connection while the operator turns
// training on, off or reloads it. Before this test, a call still running on
// the practice station while another connection turned training off found
// that station torn down under it, and the core crashed.
//
// Calls and data are made up.
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/station.h"
#include "check.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace pna;
namespace fs = std::filesystem;

static std::string line_of(std::initializer_list<std::pair<std::string, std::string>> kv) {
    std::string line = "{";
    bool first = true;
    for (auto& [k, v] : kv) {
        if (!first) line += ",";
        first = false;
        line += "\"" + k + "\":\"" + v + "\"";
    }
    return line + "}";
}
static JsonValue call(Core& c, std::initializer_list<std::pair<std::string, std::string>> kv) {
    JsonValue v;
    parse_json(ApiServer::handle(c, line_of(kv)), v);
    return v;
}
static bool ok(const JsonValue& v) {
    const JsonValue* o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}

struct Station {
    std::unique_ptr<Core> core;
    LoopbackModem* far = nullptr;
    std::unique_ptr<LoopbackModem> far_owner;
    std::atomic<size_t> heard{0};
};

static void make_station(const fs::path& dir, Station& s) {
    std::string base = (dir / "STA-T").string();
    auto radio = std::make_unique<LoopbackModem>(0);
    s.far_owner = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*radio, *s.far_owner);
    s.far = s.far_owner.get();
    s.far->start({[&s](const std::string& b) { s.heard += b.size(); }, nullptr, nullptr});
    CoreConfig cfg{"STA-T", base + ".db", 3, 1, true};
    s.core = std::make_unique<Core>(cfg, std::move(radio));
    auto kr = std::make_shared<keys::Keyring>(base + ".keys");
    std::string err;
    kr->load(&err);
    s.core->set_keyring(kr);
    s.core->set_ops(std::make_shared<Ops>("STA-T", base + ".db"));
    auto id = std::make_shared<station::Identity>();
    CHECK(station::make_identity("STA-T", static_cast<int64_t>(std::time(nullptr)), *id, &err) &&
          station::save_identity(base + ".id", *id, &err));
    s.core->set_identity(id, base + ".id");
    s.core->start();
}

// Other connections keep calling while one turns training on, off and
// reloads it. Nothing may crash, and every reply must be a whole one.
static void toggle_with_others_open(Station& s) {
    Core& core = *s.core;
    CHECK(ok(call(core, {{"cmd", "training"}, {"on", "Y"}})));  // fills the practice store once
    std::atomic<bool> run{true};
    std::atomic<long> calls{0}, bad{0};
    std::vector<std::thread> others;
    for (int i = 0; i < 3; ++i)
        others.emplace_back([&, i] {
            const char* cmds[] = {"status", "list", "pending"};
            while (run) {
                JsonValue r = call(core, {{"cmd", cmds[i % 3]}});
                if (!r.is_object() || !r.get("ok")) ++bad;
                ++calls;
            }
        });
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    int toggles = 0;
    while (std::chrono::steady_clock::now() < end) {
        CHECK(ok(call(core, {{"cmd", "training"}, {"on", "N"}})));
        CHECK(ok(call(core, {{"cmd", "training"}, {"on", "Y"}})));
        if (toggles % 5 == 4) CHECK(ok(call(core, {{"cmd", "training"}, {"reload", "Y"}})));
        ++toggles;
    }
    run = false;
    for (auto& t : others) t.join();
    std::cerr << "  " << toggles << " toggles, " << calls.load() << " calls from other connections\n";
    CHECK(toggles > 3 && calls.load() > 0 && bad.load() == 0);
    CHECK(ok(call(core, {{"cmd", "training"}, {"on", "N"}})));
}

static std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : std::string();
}

// Every reply names its store; a front end that expected the other store is
// refused and nothing is done (phoenix_nest_mars#3, per-connection training).
static void store_mode(Station& s) {
    Core& core = *s.core;
    JsonValue st = call(core, {{"cmd", "status"}});
    CHECK(ok(st) && str(st, "store_mode") == "real");
    CHECK(ok(call(core, {{"cmd", "status"}, {"expect_store", "real"}})));
    JsonValue bad = call(core, {{"cmd", "status"}, {"expect_store", "sideways"}});
    CHECK(!ok(bad) && str(bad, "store_mode") == "real");
    // Another connection turns training on.
    JsonValue on = call(core, {{"cmd", "training"}, {"on", "Y"}});
    CHECK(ok(on) && str(on, "store_mode") == "training");
    // This one still thinks it is on the real store: its send is refused.
    const size_t real_before = core.list(1000).size();
    JsonValue r = call(core, {{"cmd", "send"}, {"to", "STA-X"}, {"body", "MEANT FOR THE AIR"}, {"expect_store", "real"}});
    CHECK(!ok(r) && str(r, "store_mode") == "training");
    CHECK(r.get("store_mismatch") && r.get("store_mismatch")->b);
    CHECK(core.list(1000).size() == real_before);
    JsonValue tl = call(core, {{"cmd", "list"}, {"expect_store", "training"}});
    CHECK(ok(tl) && str(tl, "store_mode") == "training");
    // Guide replies too.
    CHECK(str(call(core, {{"cmd", "status"}}), "store_mode") == "training");
    JsonValue off = call(core, {{"cmd", "training"}, {"on", "N"}});
    CHECK(ok(off) && str(off, "store_mode") == "real");
    r = call(core, {{"cmd", "list"}, {"expect_store", "training"}});
    CHECK(!ok(r) && str(r, "store_mode") == "real");
}

// Resend while training is on reaches the training store only: the real
// radio's far end hears nothing and the real store doesn't change
// (phoenix_nest_mars#3).
static void resend_in_training(Station& s) {
    Core& core = *s.core;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const size_t heard_before = s.heard.load();
    auto real_rows = [&] {
        std::string all;
        for (auto& m : core.list(1000)) all += m.id + ":" + m.state + ":" + std::to_string(m.attempts) + ";";
        return all;
    };
    const std::string real_before = real_rows();
    CHECK(ok(call(core, {{"cmd", "training"}, {"on", "Y"}})));
    JsonValue in = call(core, {{"cmd", "list"}, {"dir", "in"}, {"include_processed", "Y"}});
    std::vector<std::string> ids;
    if (const JsonValue* a = in.get("messages"))
        for (auto& m : a->arr) ids.push_back(str(m, "id"));
    CHECK(!ids.empty());
    int tried = 0;
    for (size_t i = 0; i < ids.size() && i < 3; ++i) {
        JsonValue r = call(core, {{"cmd", "retransmit"}, {"id", ids[i]}, {"to", "STA-X"}, {"confirm", "Y"}});
        CHECK(str(r, "store_mode") == "training");
        tried += ok(r);
    }
    CHECK(tried > 0);
    // The out box's own resends, in training.
    JsonValue out = call(core, {{"cmd", "list"}});
    if (const JsonValue* a = out.get("messages"))
        for (auto& m : a->arr) call(core, {{"cmd", "requeue"}, {"id", str(m, "id")}});
    call(core, {{"cmd", "parts_queue_send"}, {"confirm", "Y"}});
    std::this_thread::sleep_for(std::chrono::seconds(2));
    CHECK(s.heard.load() == heard_before);  // 0 bytes on the real radio
    CHECK(real_rows() == real_before);      // the real store as it was
    CHECK(ok(call(core, {{"cmd", "training"}, {"on", "N"}})));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(s.heard.load() == heard_before);
    CHECK(real_rows() == real_before);
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_training_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        Station s;
        make_station(dir, s);
        toggle_with_others_open(s);
        store_mode(s);
        resend_in_training(s);
        s.core->stop();
        s.far->stop();
    }
    fs::remove_all(dir);
    return check::report("test_training");
}
