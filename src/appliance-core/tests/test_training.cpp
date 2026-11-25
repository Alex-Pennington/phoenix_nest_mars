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

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_training_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        Station s;
        make_station(dir, s);
        toggle_with_others_open(s);
        s.core->stop();
        s.far->stop();
    }
    fs::remove_all(dir);
    return check::report("test_training");
}
