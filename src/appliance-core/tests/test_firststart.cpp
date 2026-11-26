// First start and hand-entered channels.
//
// 1. A box out of the can has no call: appliance-core answers setup calls on
//    its API port, takes the call, makes the station keys and card, and the
//    station starts. Nothing touches the settings file; a station given in
//    the settings wins; the next start skips setup.
// 2. A region with no documents enters its frequency card by hand
//    (channel_put). The frequency never comes back in a reply or the log,
//    and it rides in a member's bundle sealed to that member's card.
//
// Calls and keys are made up. The frequencies are public time-signal ones.
#include "../src/api_server.h"
#include "../src/channels.h"
#include "../src/core.h"
#include "../src/first_start.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "../src/net.h"
#include "../src/ops.h"
#include "../src/station.h"
#include "check.h"
#include "services_fixture.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>
#include <unistd.h>

using namespace pna;

// A frequency left in text shows as a number of its own (5000, 5000.0,
// 5000000). Digits inside a key, signature or sealed block don't count.
static bool leaks(const std::string& text, const std::string& needle) {
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        // A number standing alone: not the start of a longer one (a date-time
        // group such as 250012Z holds 2500).
        const size_t end = at + needle.size();
        if (std::isdigit(static_cast<unsigned char>(needle.back())) && end < text.size() &&
            std::isdigit(static_cast<unsigned char>(text[end])))
            continue;
        if (at == 0) return true;
        unsigned char before = static_cast<unsigned char>(text[at - 1]);
        if (!std::isalnum(before) && before != '+' && before != '/') return true;
    }
    return false;
}

namespace fs = std::filesystem;

static const std::string kBuild = PNA_BUILD_DIR;

static bool wait_for(const std::function<bool()>& cond, int ms = 8000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return cond();
}

static uint16_t free_port() {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    socklen_t len = sizeof sa;
    ::getsockname(s, reinterpret_cast<sockaddr*>(&sa), &len);
    uint16_t p = ntohs(sa.sin_port);
    ::close(s);
    return p;
}

static JsonValue parse(const std::string& reply) {
    JsonValue v;
    parse_json(reply, v);
    return v;
}
static bool ok(const JsonValue& v) {
    const JsonValue* o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}
static bool yes(const JsonValue& v, const char* k) {
    const JsonValue* o = v.get(k);
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

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The CLI, as a member would type it.
static std::string cli(uint16_t port, const std::string& args) {
    std::string cmd = kBuild + "/appliance-cli --port " + std::to_string(port) + " " + args + " 2>&1";
    std::string out;
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    ::pclose(p);
    return out;
}

static pid_t start_core(const fs::path& dir, uint16_t port, const std::vector<std::string>& extra, const fs::path& log) {
    pid_t pid = ::fork();
    if (pid == 0) {
        int lf = ::open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        ::dup2(lf, 1);
        ::dup2(lf, 2);
        std::string exe = kBuild + "/appliance-core";
        std::vector<std::string> a = {exe, "--db-dir", dir.string(), "--api-port", std::to_string(port),
                                      "--link", "appa", "--appa-port", std::to_string(free_port())};
        a.insert(a.end(), extra.begin(), extra.end());
        std::vector<char*> av;
        for (auto& s : a) av.push_back(const_cast<char*>(s.c_str()));
        av.push_back(nullptr);
        ::execv(exe.c_str(), av.data());
        _exit(127);
    }
    return pid;
}
static void stop_core(pid_t pid) {
    if (pid <= 0) return;
    ::kill(pid, SIGTERM);
    int st = 0;
    for (int i = 0; i < 100 && ::waitpid(pid, &st, WNOHANG) == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &st, 0);
}

// ---------------------------------------------------------------- setup calls

static void setup_calls(const fs::path& dir) {
    fs::path d = dir / "unit";
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    std::string call;
    JsonValue r = parse(first_start::handle(d.string(), now, R"({"cmd":"setup_status"})", &call));
    CHECK(ok(r) && yes(r, "setup_needed") && str(r, "next").find("setup_station") != std::string::npos);
    r = parse(first_start::handle(d.string(), now, R"({"cmd":"send","service":"mars","to":"X","text":"HI"})", &call));
    CHECK(!ok(r) && str(r, "error").find("setup_station") != std::string::npos);
    for (auto* bad : {"", "ALL", "STA B", "A_B", "WAY-TOO-LONG-A-CALL"}) {
        r = parse(first_start::handle(d.string(), now, std::string(R"({"cmd":"setup_station","call":")") + bad + "\"}", &call));
        CHECK(!ok(r) && call.empty());
    }
    std::string saved;
    CHECK(!first_start::saved_call(d.string(), saved));
    r = parse(first_start::handle(d.string(), now, R"({"cmd":"setup_station","call":"sta-b"})", &call));
    CHECK(ok(r) && call == "STA-B" && str(r, "call") == "STA-B" && yes(r, "keys_made"));
    CHECK(str(r, "card").find("PNA STATION CARD 1") == 0 && str(r, "fingerprint").size() == 19);
    CHECK(first_start::saved_call(d.string(), saved) && saved == "STA-B");
    station::Identity id;
    bool exists = false;
    CHECK(station::load_identity((d / "STA-B.id").string(), id, &exists, nullptr) && exists && id.station == "STA-B");
    CHECK(station::fingerprint(station::card_of(id)) == str(r, "fingerprint"));
    // Set once: a second call is refused, and the keys stay.
    std::string again;
    r = parse(first_start::handle(d.string(), now, R"({"cmd":"setup_station","call":"STA-Z"})", &again));
    CHECK(!ok(r) && again.empty() && str(r, "error").find("already set to STA-B") != std::string::npos);
    r = parse(first_start::handle(d.string(), now, R"({"cmd":"setup_status"})", &again));
    CHECK(ok(r) && !yes(r, "setup_needed") && str(r, "call") == "STA-B");
}

// ---------------------------------------------------------------- the real service

static void first_start_service(const fs::path& dir) {
    fs::path d = dir / "can";
    fs::create_directories(d);
    fs::path log = dir / "core.log";
    uint16_t port = free_port();
    pid_t pid = start_core(d, port, {}, log);
    CHECK(wait_for([&] { return slurp(log).find("waiting for setup_station") != std::string::npos; }));

    // What the box needs, then its call, from the CLI.
    std::string out = cli(port, "setup");
    JsonValue r = parse(out);
    CHECK(ok(r) && yes(r, "setup_needed"));
    out = cli(port, "status");
    CHECK(parse(out).get("setup_needed") && yes(parse(out), "setup_needed"));
    // The same, as the setup form gale-term draws: only SETUP is open.
    r = parse(cli(port, "form"));
    const JsonValue* fl = r.get("forms");
    CHECK(ok(r) && fl && fl->is_array() && fl->arr.size() == 1 && str(fl->arr[0], "id") == "SETUP");
    r = parse(cli(port, "form check SETUP call=ALL"));
    CHECK(ok(r) && !yes(r, "ready"));
    out = cli(port, "setup sta-b");
    r = parse(out);
    CHECK(ok(r) && str(r, "call") == "STA-B" && !str(r, "fingerprint").empty());
    const std::string fp = str(r, "fingerprint");

    // The station starts on the same port with that call and those keys.
    CHECK(wait_for([&] { return str(parse(cli(port, "status")), "station") == "STA-B"; }));
    r = parse(cli(port, "station card"));
    CHECK(ok(r) && str(r, "fingerprint") == fp);

    // A frequency card by hand: no frequency in any reply or the log.
    std::string all;
    all += cli(port, "channels add p1 5000.0 --mode USB --use PRIMARY");
    all += cli(port, "channels add P2 10 --unit mhz --mode USB --use SECONDARY");
    all += cli(port, "channels add S1 2500000 --unit hz --mode ALE --use SCAN");
    // And by the channel form: the CHU time signal, 3330 kHz.
    std::string shown = cli(port, "form fill CHANNEL name=t1 frequency=3330 use=TIME");
    r = parse(cli(port, "form show CHANNEL name=T1"));
    CHECK(ok(r) && yes(r, "prefilled"));
    shown += cli(port, "form show CHANNEL name=T1");  // lists the units, so no MHZ check
    for (auto* never : {"3330", "5000"}) CHECK(!leaks(shown, never));
    r = parse(cli(port, "channels"));
    const JsonValue* chans = r.get("channels");
    CHECK(ok(r) && chans && chans->is_array() && chans->arr.size() == 4);
    all += cli(port, "channels");
for (auto* never : {"5000", "10000", "2500", "3330", "MHZ"}) CHECK(!leaks(all, never));
    stop_core(pid);
    std::string logged = slurp(log);
    CHECK(logged.find("channel P1 added by hand") != std::string::npos);
    for (auto* never : {"5000", "10000", "2500", "3330"}) CHECK(!leaks(logged, never));
    CHECK(logged.find("form CHANNEL submitted") != std::string::npos);

    // Setup wrote only its own files: the call, and the station's keys.
    CHECK(fs::exists(d / "station-call") && fs::exists(d / "STA-B.id"));
    for (auto& e : fs::directory_iterator(d)) CHECK(e.path().extension() != ".conf");

    // The next start skips setup and is the same station.
    fs::remove(log);
    pid = start_core(d, port, {}, log);
    CHECK(wait_for([&] { return str(parse(cli(port, "status")), "station") == "STA-B"; }));
    CHECK(slurp(log).find("waiting for setup_station") == std::string::npos);
    CHECK(str(parse(cli(port, "station card")), "fingerprint") == fp);
    stop_core(pid);

    // A station given in the settings (here, on the command line) wins.
    fs::remove(log);
    pid = start_core(d, port, {"--station", "STA-Q"}, log);
    CHECK(wait_for([&] { return str(parse(cli(port, "status")), "station") == "STA-Q"; }));
    stop_core(pid);

    // --check says what the first start will do, and isn't a problem.
    fs::path fresh = dir / "fresh";
    std::string chk = kBuild + "/appliance-core --check --db-dir " + fresh.string() + " --link appa 2>&1";
    FILE* p = ::popen(chk.c_str(), "r");
    std::string text;
    char buf[4096];
    size_t n;
    while (p && (n = std::fread(buf, 1, sizeof buf, p)) > 0) text.append(buf, n);
    int rc = p ? ::pclose(p) : -1;
    CHECK(text.find("first start asks for it") != std::string::npos && text.find("ready to run") != std::string::npos);
    CHECK(WIFEXITED(rc) && WEXITSTATUS(rc) == 0);
}

// ---------------------------------------------------------------- into a sealed bundle

static int64_t g_now = 0;

struct Box {
    std::unique_ptr<Core> core;
    std::shared_ptr<Ops> ops;
    std::string fp;
};

static Box open_box(const fs::path& dir, const std::string& call) {
    Box b;
    fs::create_directories(dir);
    std::string base = (dir / call).string();
    b.core = std::make_unique<Core>(CoreConfig{call, base + ".db", 3, 1, true}, std::make_unique<LoopbackModem>(0));
    b.core->set_services(test_services());
    auto keys = std::make_shared<keys::Keyring>(base + ".keys");
    keys->set_clock([] { return g_now; });
    b.core->set_keyring(keys);
    b.ops = std::make_shared<Ops>(call, base + ".db");
    b.core->set_ops(b.ops);
    auto id = std::make_shared<station::Identity>();
    CHECK(station::make_identity(call, g_now, *id, nullptr));
    b.fp = station::fingerprint(station::card_of(*id));
    b.core->set_identity(id, base + ".id");
    b.core->start();
    return b;
}

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (c == '\n') {
            o += "\\n";
            continue;
        }
        o += c;
    }
    return o;
}
static JsonValue api(Core& c, std::initializer_list<std::pair<std::string, std::string>> kv, std::string* raw = nullptr) {
    std::string line = "{";
    bool first = true;
    for (auto& [k, v] : kv) {
        if (!first) line += ",";
        first = false;
        line += "\"" + k + "\":\"" + esc(v) + "\"";
    }
    line += "}";
    std::string reply = ApiServer::handle(c, line);
    if (raw) *raw = reply;
    return parse(reply);
}

static void hand_entered_into_bundle(const fs::path& dir) {
    g_now = static_cast<int64_t>(std::time(nullptr));
    Box A = open_box(dir / "hand", "NCS-A");
    Box B = open_box(dir / "hand", "STA-B");
    std::string raw;
    JsonValue r = api(*A.core, {{"cmd", "channel_put"}, {"name", "p1"}, {"frequency", "5000.0"}, {"mode", "usb"}, {"use", "Primary"}}, &raw);
    CHECK(ok(r) && str(r, "name") == "P1" && str(r, "result") == "added" && str(r, "mode") == "USB");
    CHECK(!leaks(raw, "5000"));
    auto p1 = A.ops->channels().get("P1");
    CHECK(p1 && p1->hz == 5000000 && p1->source == "ENTERED BY HAND");
    // The same again is "same"; a new frequency replaces it, keeping mode and use.
    r = api(*A.core, {{"cmd", "channel_put"}, {"name", "P1"}, {"frequency", "5000.0"}});
    CHECK(ok(r) && str(r, "result") == "same");
    r = api(*A.core, {{"cmd", "channel_put"}, {"name", "P1"}, {"frequency", "3330"}}, &raw);
    CHECK(ok(r) && str(r, "result") == "replaced" && yes(r, "frequency_changed") && str(r, "use") == "PRIMARY");
    CHECK(!leaks(raw, "3330") && A.ops->channels().get("P1")->hz == 3330000);
    CHECK(ok(api(*A.core, {{"cmd", "channel_put"}, {"name", "P1"}, {"frequency", "5"}, {"unit", "MHZ"}})));
    CHECK(A.ops->channels().get("P1")->hz == 5000000);
    // What it refuses: no frequency, one it can't read, out of range, a bad designator or unit.
    CHECK(!ok(api(*A.core, {{"cmd", "channel_put"}, {"name", "P2"}})));
    CHECK(!ok(api(*A.core, {{"cmd", "channel_put"}, {"name", "P2"}, {"frequency", "TEN"}})));
    CHECK(!ok(api(*A.core, {{"cmd", "channel_put"}, {"name", "P2"}, {"frequency", "5"}, {"unit", "hz"}})));
    CHECK(!ok(api(*A.core, {{"cmd", "channel_put"}, {"name", "P 2"}, {"frequency", "10000"}})));
    CHECK(!ok(api(*A.core, {{"cmd", "channel_put"}, {"name", "P2"}, {"frequency", "10000"}, {"unit", "ghz"}})));
    CHECK(!A.ops->channels().get("P2"));
    // A use that looks like a frequency isn't kept.
    r = api(*A.core, {{"cmd", "channel_put"}, {"name", "P2"}, {"frequency", "10000"}, {"use", "ALT 10000.0 KHZ"}}, &raw);
    CHECK(ok(r) && str(r, "use").empty() && !leaks(raw, "10000"));
    r = api(*A.core, {{"cmd", "channels_list"}}, &raw);
    CHECK(ok(r) && !leaks(raw, "5000") && !leaks(raw, "10000"));
    // A channel kept for the amateur world never goes in a MARS bundle (asks 37, 40).
    CHECK(ok(api(*A.core, {{"cmd", "channel_put"}, {"name", "A1"}, {"frequency", "15000"}, {"world", "amateur"}})));

    // Entered by hand, sealed to B in B's bundle, and B's plan fills in.
    CHECK(ok(api(*A.core, {{"cmd", "card_import"}, {"text", station::format_card(*B.core->identity())}})));
    CHECK(ok(api(*A.core, {{"cmd", "card_check"}, {"station", "STA-B"}, {"fingerprint", B.fp}})));
    CHECK(ok(api(*B.core, {{"cmd", "card_import"}, {"text", station::format_card(*A.core->identity())}})));
    CHECK(ok(api(*B.core, {{"cmd", "card_check"}, {"station", "NCS-A"}, {"fingerprint", A.fp}})));
    r = api(*A.core, {{"cmd", "bundle_make"}, {"for", "STA-B"}});
    CHECK(ok(r) && str(r, "frequencies") == "sealed" && num(r, "frequencies_sealed") == 2);
    std::string text = str(r, "text");
    CHECK(!leaks(text, "5000") && !leaks(text, "10000"));
    r = api(*B.core, {{"cmd", "bundle_load"}, {"text", text}});
    CHECK(ok(r) && str(r, "frequencies") == "opened" && num(r, "frequencies_added") == 2);
    CHECK(B.ops->channels().get("P1") && B.ops->channels().get("P1")->hz == 5000000);
    CHECK(B.ops->channels().get("P2") && B.ops->channels().get("P2")->hz == 10000000);
    CHECK(!B.ops->channels().get("A1"));
    A.core->stop();
    B.core->stop();
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_firststart_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    setup_calls(dir);
    first_start_service(dir);
    hand_entered_into_bundle(dir);
    fs::remove_all(dir);
    return check::report("test_firststart");
}
