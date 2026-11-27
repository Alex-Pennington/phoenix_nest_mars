// Audit A: a routed message calls its rule's ALE addresses in order. When the
// copy to one goes unacked (or fails), the next address is called, and the
// unacked copy is stopped so the message isn't delivered twice; when none is
// left, the routed message is failed with why. An address the core refuses
// is passed over for the next. Every call and RI here is made up.
#include "../src/api_server.h"
#include "../src/bbs.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "check.h"
#include "services_fixture.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static bool wait_for(const std::function<bool()>& cond, int ms = 5000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}
static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (c == '\n') {
            o += "\\n";
            continue;
        }
        if (c == '\r') {
            o += "\\r";
            continue;
        }
        o += c;
    }
    return o;
}
static JsonValue call(Core& c, std::initializer_list<std::pair<std::string, std::string>> kv) {
    std::string line = "{";
    bool first = true;
    for (auto& [k, v] : kv) {
        if (!first) line += ",";
        first = false;
        line += "\"" + k + "\":\"" + esc(v) + "\"";
    }
    line += "}";
    JsonValue out;
    parse_json(ApiServer::handle(c, line), out);
    return out;
}
static bool ok(const JsonValue& v) {
    const JsonValue* o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}
static std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static double num(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::Number ? x->n : -1;
}
static const JsonValue& at(const JsonValue& v, const char* k, size_t i) {
    static const JsonValue none;
    const JsonValue* x = v.get(k);
    return x && x->is_array() && i < x->arr.size() ? x->arr[i] : none;
}
static std::vector<Message> out_to(Core& c, const std::string& dst) {
    std::vector<Message> o;
    for (auto& m : c.list(1000))
        if (m.direction == "out" && m.dst == dst) o.push_back(m);
    return o;
}
static bool got(Core& c, const std::string& text) {
    for (auto& m : c.list(1000))
        if (m.direction == "in" && m.body.find(text) != std::string::npos) return true;
    return false;
}
static std::string traffic(const std::string& fl2, const std::string& de, int serial, const std::string& text) {
    char sn[8];
    std::snprintf(sn, sizeof sn, "%04d", serial);
    return "VZCZC\r\nRR " + fl2 + " DE " + de + " #" + sn + " 3291200\r\nBT\r\nUNCLAS\r\n" + text + "\r\nBT\r\nNNNN\r\n";
}
// The routed row for one held message.
static JsonValue routed_row(Core& a, const std::string& msg_id) {
    JsonValue r = call(a, {{"cmd", "route_list"}});
    for (size_t i = 0; i < 50; ++i) {
        const JsonValue& x = at(r, "routed", i);
        if (str(x, "msg_id") == msg_id) return x;
        if (!x.get("msg_id")) break;
    }
    return JsonValue();
}
static std::string in_id(Core& a, const std::string& text) {
    for (auto& m : a.list(1000))
        // A garbled partial copy cut under load is not the one routed.
        if (m.direction == "in" && m.state != "garbled" && m.body.find(text) != std::string::npos) return m.id;
    return "";
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_route_next_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    auto mc = std::make_unique<LoopbackModem>(0);
    LoopbackModem::share({ma.get(), mb.get(), mc.get()});
    CoreConfig ca{"AAM499", (dir / "a.db").string(), 3, 1, true};
    ca.rx_service = "mars";  // a channel-less test link: MARS traffic
    ca.ack_timeout_s = 1;  // a station that doesn't answer is unacked within seconds
    ca.ack_retries = 0;
    ca.rx_service = "mars";  // a loopback link: the network it belongs to
    Core a(ca, std::move(ma));
    a.set_services(test_services());
    Core b(on_mars_link(CoreConfig{"AAR9AA", (dir / "b.db").string(), 3, 1, true}), std::move(mb));
    b.set_services(test_services());
    Core c(on_mars_link(CoreConfig{"AAR7ZZ", (dir / "c.db").string(), 3, 1, true}), std::move(mc));
    c.set_services(test_services());
    a.start();
    b.start();
    c.start();
    std::string e;
    CHECK(c.set_link_format("AAM499", "plain", &e));
    bbs::Engine& bbs_a = bbs::engine(a);
    auto pump_until = [&](const std::function<bool()>& cond, int ms = 15000) {
        return wait_for(
            [&] {
                bbs_a.pump();
                return cond();
            },
            ms);
    };
    CHECK(ok(call(a, {{"cmd", "bbs_set"}, {"machine_mode", "true"}, {"routing", "true"}, {"accept", "all"}, {"ack_delay_ms", "0"},
                      {"auto_per_hour", "120"}})));
    // AAR5NO and AAR4NO are not on the air.
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"service", "mars"}, {"match", "UHW"}, {"route", "ALE WEST"}, {"ale", "AAR5NO,AAR9AA"}})));
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"service", "mars"}, {"match", "UHE"}, {"route", "ALE EAST"}, {"ale", "AAR4NO"}})));
    CHECK(ok(call(a, {{"cmd", "route_set"}, {"service", "mars"}, {"match", "UHN"}, {"route", "ALE NORTH"}, {"ale", "AAM499,AAR9AA"}})));

    // ---- the first address doesn't answer: the second is called ------------
    CHECK(!c.submit("AAM499", 'R', traffic("UHWTST", "AAR7ZZ", 1, "FOR THE WEST GATEWAY"), &e, SendOpts::in("mars")).empty());
    CHECK(pump_until([&] { return out_to(a, "AAR5NO").size() == 1; }));
    {
        const std::string id = in_id(a, "FOR THE WEST GATEWAY");
        JsonValue r = routed_row(a, id);
        CHECK(str(r, "state") == "queued" && num(r, "ale_pos") == 0);
        CHECK(pump_until([&] { return out_to(a, "AAR9AA").size() == 1; }));
        CHECK(wait_for([&] { return got(b, "FOR THE WEST GATEWAY"); }));
        r = routed_row(a, id);
        CHECK(str(r, "state") == "queued" && num(r, "ale_pos") == 1);
        auto to9 = out_to(a, "AAR9AA"), to5 = out_to(a, "AAR5NO");
        CHECK(to9.size() == 1 && str(r, "out_id") == to9[0].id);
        // The unacked copy to AAR5NO is stopped: it won't go again when AAR5NO is heard.
        CHECK(to5.size() == 1 && to5[0].state == "cancelled");
    }

    // ---- no address answers: failed, with why --------------------------------
    CHECK(!c.submit("AAM499", 'R', traffic("UHEAAA", "AAR7ZZ", 2, "FOR THE EAST GATEWAY"), &e, SendOpts::in("mars")).empty());
    CHECK(pump_until([&] { return out_to(a, "AAR4NO").size() == 1; }));
    {
        const std::string id = in_id(a, "FOR THE EAST GATEWAY");
        CHECK(pump_until([&] { return str(routed_row(a, id), "state") == "failed"; }));
        JsonValue r = routed_row(a, id);
        CHECK(str(r, "why").find("no ALE address") != std::string::npos &&
              str(r, "why").find("AAR4NO") != std::string::npos);
        CHECK(str(r, "why").find("still goes if AAR4NO is heard") != std::string::npos);
        auto to4 = out_to(a, "AAR4NO");
        CHECK(to4.size() == 1 && to4[0].state == "unacked");  // not tried again, not stopped
    }

    // ---- an address the core refuses is passed over for the next ----------
    CHECK(!c.submit("AAM499", 'R', traffic("UHNXXX", "AAR7ZZ", 3, "FOR THE NORTH GATEWAY"), &e, SendOpts::in("mars")).empty());
    CHECK(pump_until([&] { return got(b, "FOR THE NORTH GATEWAY"); }));
    {
        JsonValue r = routed_row(a, in_id(a, "FOR THE NORTH GATEWAY"));
        CHECK(str(r, "state") == "queued" && num(r, "ale_pos") == 1);
    }

    a.stop();
    b.stop();
    c.stop();
    fs::remove_all(dir);
    return check::report("test_route_next");
}
