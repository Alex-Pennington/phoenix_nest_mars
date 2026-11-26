// The radio side for gale-web (station_view.h): scanner status and orders,
// word feed, station health, captures and heard stations, read from files laid
// out as greybeard-ale writes them. Every frequency here is made up for the
// test; the point of most checks is that none comes out of port 4100.
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/link_monitor.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/station_view.h"
#include "check.h"

#include <sqlite3.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

using namespace pna;
namespace fs = std::filesystem;

static bool contains(const std::string& s, const std::string& w) { return s.find(w) != std::string::npos; }
static JsonValue parsed(const std::string& s) {
    JsonValue v;
    parse_json(s, v);
    return v;
}
static bool okv(const JsonValue& v) {
    auto o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}
static std::string str(const JsonValue& v, const std::string& k) {
    auto x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static bool bl(const JsonValue& v, const std::string& k) {
    auto x = v.get(k);
    return x && x->type == JsonValue::Type::Bool && x->b;
}
static double nm(const JsonValue& v, const std::string& k) {
    auto x = v.get(k);
    return x && x->type == JsonValue::Type::Number ? x->n : -1;
}
static const JsonValue& sub(const JsonValue& v, const std::string& k) {
    static JsonValue none;
    auto x = v.get(k);
    return x ? *x : none;
}
static void write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << s;
}
// The made-up dials (kHz). None may come out of port 4100, in kHz, Hz or MHz,
// as a number or inside text, and no key may name one.
static const double kF[] = {13579.246, 17531.864, 17532.975, 4864.213, 13582.468};
static bool freq_free(const JsonValue& v) {
    switch (v.type) {
        case JsonValue::Type::Number:
            for (double f : kF)
                if (std::fabs(v.n - f) < 0.6 || std::fabs(v.n - f * 1000) < 600 || std::fabs(v.n - f / 1000) < 0.0006)
                    return false;
            return true;
        case JsonValue::Type::String:
            for (double f : kF) {
                char a[32], b[32];
                std::snprintf(a, sizeof a, "%.0f", f * 1000);
                std::snprintf(b, sizeof b, "%.1f", f);
                if (contains(v.s, a) || contains(v.s, b)) return false;
            }
            return true;
        case JsonValue::Type::Array:
            for (auto& e : v.arr)
                if (!freq_free(e)) return false;
            return true;
        case JsonValue::Type::Object:
            for (auto& [k, e] : v.obj) {
                if (contains(k, "khz") || contains(k, "hz") || contains(k, "freq") || !freq_free(e)) return false;
            }
            return true;
        default:
            return true;
    }
}
static bool no_freq(const std::string& s) {
    JsonValue v;
    return parse_json(s, v) && freq_free(v);
}

// Stands in for ale-scan --control: answers status with `status`, and each
// order with an ok order line, keeping the lines it was sent.
class Scanner {
public:
    explicit Scanner(std::string path) : path_(std::move(path)) {
        ::unlink(path_.c_str());
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        std::memcpy(a.sun_path, path_.c_str(), path_.size());
        ::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a);
        ::listen(fd_, 8);
        th_ = std::thread([this] { run(); });
    }
    ~Scanner() {
        stop_ = true;
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        th_.join();
        ::unlink(path_.c_str());
    }
    void set_status(const std::string& s) {
        std::lock_guard<std::mutex> g(mu_);
        status_ = s;
    }
    std::vector<std::string> orders() {
        std::lock_guard<std::mutex> g(mu_);
        return orders_;
    }

private:
    void run() {
        while (!stop_) {
            int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) continue;
            char buf[600];
            ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n > 0) {
                std::string line(buf, static_cast<size_t>(n));
                while (!line.empty() && line.back() == '\n') line.pop_back();
                std::string r;
                {
                    std::lock_guard<std::mutex> g(mu_);
                    if (line == "status") r = status_;
                    else {
                        orders_.push_back(line);
                        if (line.rfind("plan nope", 0) == 0) r = R"({"ok":false,"error":"plan: no plan nope in the plan directory"})";
                        else if (line.rfind("hold", 0) == 0) r = R"({"ok":true,"order":"hold window 2 for 60 s","window":2,"seconds":60})";
                        else r = R"({"ok":true,"order":")" + line.substr(0, line.find(' ')) + R"("})";
                    }
                }
                r += "\n";
                ::send(c, r.data(), r.size(), MSG_NOSIGNAL);
            }
            ::close(c);
        }
    }
    std::string path_;
    int fd_ = -1;
    std::thread th_;
    std::atomic<bool> stop_{false};
    std::mutex mu_;
    std::string status_ = R"({"ok":true,"state":"scan"})";
    std::vector<std::string> orders_;
};

static const char* kPlan =
    "# test plan, made-up dials\n"
    "rate 2000000\n"
    "window 13600000 13579246 13582468\n"
    "window 17550000 17531864 17532975\n"
    "record 17532975:NETB\n";

static std::string word(double unix, double khz, const char* type, const char* chars, double sinad = 12.5,
                        int window = 1, bool gap = false, const char* engine = "local") {
    char b[400];
    std::snprintf(b, sizeof b,
                  R"({"unix":%.3f,"khz":%.3f,"type":"%s","chars":"%s","word":"a1b2c3","sinad":%.1f,"fec":0,)"
                  R"("votes":3,"engine":"%s","gap":%s,"window":%d,"tx":0,"clock":"wwv","offset_hz":12.0})",
                  unix, khz, type, chars, sinad, engine, gap ? "true" : "false", window);
    return b;
}

static void units() {
    // Defaults under gale-dir.
    sv::Paths p;
    p.gale_dir = "/x/local";
    auto r = p.resolved();
    CHECK(r.words == "/x/local/words" && r.scan_log == "/x/local/ale-scan.log" && r.wwv_dir == "/x/local/wwv");
    CHECK(r.gps == "/x/local/gps/status.json" && r.lqa_db == "/x/local/lqa.sqlite" && r.captures == "/x/local/captures");
    CHECK(r.scan_plan == "/x/local/plans/scan.plan" && r.store_root.empty());
    sv::Paths q;
    CHECK(q.resolved().words == "words" && q.resolved().gale_dir == ".");

    // A word is valid only as gale_term.py's valid_word.
    sv::Word w;
    CHECK(sv::parse_word(word(1.8e9, 10000.0, "TIS", "AB1"), &w) && w.type == "TIS" && w.window == 1 && w.engine == "local");
    CHECK(!sv::parse_word(R"({"unix":1,"khz":1,"type":"TO","chars":"X","word":"zz","sinad":1})", &w));
    CHECK(!sv::parse_word(R"({"unix":"1","khz":1,"type":"TO","chars":"X","word":"aa","sinad":1})", &w));
    CHECK(!sv::parse_word("not json", &w));

    // Words into transmissions: a sounding, then a call with an AMD text, on
    // another channel interleaved, and a gap that splits the first channel.
    std::vector<sv::Word> ws;
    double t0 = 1.8e9;
    for (auto& l : {word(t0, 10000, "TIS", "AB1"), word(t0 + 0.4, 10000, "DATA", "XY@"), word(t0 + 0.5, 12000, "TO", "CD2"),
                    word(t0 + 0.8, 12000, "DATA", "ZZ@"), word(t0 + 0.9, 12000, "CMD", "HEL"),
                    word(t0 + 1.0, 12000, "DATA", "LO "), word(t0 + 1.1, 12000, "TIS", "AB1"),
                    word(t0 + 1.2, 12000, "DATA", "XY@"), word(t0 + 5, 10000, "TWAS", "AB1"),
                    word(t0 + 5.3, 10000, "TO", "EF3")}) {
        sv::Word x;
        CHECK(sv::parse_word(l, &x));
        ws.push_back(x);
    }
    auto txs = sv::group_words(ws);
    CHECK(txs.size() == 3);
    if (txs.size() == 3) {
        CHECK(txs[0].caller == "AB1XY" && txs[0].called.empty() && txs[0].kind == "sounding" && txs[0].words == 2);
        CHECK(txs[1].caller == "AB1XY" && txs[1].called.size() == 1 && txs[1].called[0] == "CD2ZZ" && txs[1].kind == "call");
        CHECK(txs[1].amd.size() == 1 && txs[1].amd[0] == "HELLO");
        CHECK(txs[2].kind == "call-end" && txs[2].caller == "AB1");
    }
}

static std::string api(Core& c, const std::string& line) { return ApiServer::handle(c, line); }

int main() {
    units();
    fs::path dir = fs::temp_directory_path() / ("pna_sv_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::path gale = dir / "local";
    fs::create_directories(gale);
    auto core = std::make_unique<Core>(CoreConfig{"STA-A", (dir / "STA-A.db").string(), 3, 1, true},
                                       std::make_unique<LoopbackModem>(0));
    auto ops = std::make_shared<Ops>("STA-A", (dir / "STA-A.db").string());
    core->set_ops(ops);
    {
        Channel c;
        c.name = "P1";
        c.hz = 13579246;
        c.mode = "USB";
        std::string err;
        CHECK(ops->channels().put(c, &err));
    }

    // ---- nothing there at all: the store not mounted ----------------------
    sv::Paths p;
    p.gale_dir = (dir / "no-store").string();
    p.store_root = (dir / "no-store").string();
    sv::set_paths(p);
    for (const char* cmd : {"scanner_status", "word_feed", "station_health", "captures_list", "heard_stations"}) {
        std::string r = api(*core, std::string(R"({"cmd":")") + cmd + "\"}");
        JsonValue v = parsed(r);
        CHECK(okv(v));
        CHECK(sub(v, "store").is_object() && !bl(sub(v, "store"), "mounted") && contains(str(sub(v, "store"), "why"), "no store"));
        if (std::string(cmd) != "scanner_status" && std::string(cmd) != "station_health") CHECK(!bl(v, "available"));
    }
    {
        JsonValue v = parsed(api(*core, R"({"cmd":"station_health"})"));
        CHECK(!bl(sub(v, "health"), "available") && str(sub(v, "health"), "why") == "no file");
        CHECK(!bl(sub(v, "scanner_log"), "available") && str(sub(v, "clock"), "set_by") == "none");
        v = parsed(api(*core, R"({"cmd":"scanner_status"})"));
        CHECK(!bl(v, "source") && str(v, "view") == "no scanner");
        v = parsed(api(*core, R"({"cmd":"scanner_hold","window":"1"})"));
        CHECK(!okv(v) && contains(str(v, "error"), "link-socket"));
    }
    // A store folder with no drive on it says so.
    fs::create_directories(dir / "store");
    p.store_root = (dir / "store").string();
    sv::set_paths(p);
    {
        JsonValue v = parsed(api(*core, R"({"cmd":"captures_list"})"));
        CHECK(!bl(sub(v, "store"), "mounted") && contains(str(sub(v, "store"), "why"), "no drive is mounted"));
    }

    // ---- the files, as greybeard-ale writes them -------------------------
    p = sv::Paths{};
    p.gale_dir = gale.string();
    sv::set_paths(p);
    write(gale / "plans/scan.plan", kPlan);
    write(gale / "own_address.txt", "# ours\nCD2ZZ\n");
    write(gale / "favourites.txt", "AB1XY Bob\nQQ9 Far away\nbad/address\n");
    double now = static_cast<double>(std::time(nullptr));
    std::string words;
    // An older day file, then today's: the newest is read.
    write(gale / "words-20200101.jsonl", word(now - 99999, 13579.246, "TIS", "OLD") + "\n");
    for (auto& l : {word(now - 30, 13579.246, "TIS", "AB1"), word(now - 29.6, 13579.246, "DATA", "XY@"),
                    // The terminal's own engine decoded this call, the overflow engine the
                    // gap word (phoenix_nest_mars#3, ask 12); the rest are ale-scan's fallback.
                    word(now - 20, 17531.864, "TO", "CD2", 9.0, 2, false, "station"),
                    word(now - 19.6, 17531.864, "DATA", "ZZ@", 9.0, 2, false, "station"),
                    word(now - 19.5, 17531.864, "TIS", "AB1", 9.0, 2, false, "station"),
                    word(now - 19.2, 17531.864, "DATA", "XY@", 9.0, 2, false, "station"),
                    word(now - 10, 4864.213, "TIS", "GH4", 5.0, 3, true, "remote"), word(now - 2, 17532.975, "TIS", "AB1", 20.0, 2),
                    word(now - 1.8, 17532.975, "DATA", "XY@", 20.0, 2)})
        words += l + "\n";
    words += "garbage line\n";
    write(gale / "words-20991231.jsonl", words);

    // Channel names: designator, scanner label, place, unlisted.
    {
        sv::ChannelNames n(*core);
        CHECK(n.name(13579.246) == "P1");
        CHECK(n.name(17532.975) == "NETB");
        CHECK(n.name(17531.864) == "W2C1");
        CHECK(n.name(13582.468) == "W1C2");
        CHECK(n.name(4864.213) == "unlisted");
        std::string named = sv::name_channels(
            R"({"ok":true,"channels_khz":[13579.246,13582.468],"hold":{"window":1,"record_khz":13579.246,"record_label":"x"},"offset_hz":3,"peak_khz":17532.975,"freq_mhz":13.579246,"note":"hi"})",
            n);
        JsonValue v = parsed(named);
        CHECK(no_freq(named));
        CHECK(sub(v, "channels").is_array() && sub(v, "channels").arr.size() == 2 && sub(v, "channels").arr[1].s == "W1C2");
        CHECK(str(sub(v, "hold"), "record_label") == "x" && str(v, "peak_label") == "NETB" && str(v, "note") == "hi");
    }

    // ---- word feed ----------------------------------------------------------
    {
        std::string r = api(*core, R"({"cmd":"word_feed"})");
        JsonValue v = parsed(r);
        CHECK(okv(v) && bl(v, "available") && str(v, "source") == "words-20991231.jsonl" && no_freq(r));
        JsonValue tx = sub(v, "transmissions");
        CHECK(tx.is_array() && tx.arr.size() == 4);
        if (tx.arr.size() == 4) {
            CHECK(str(tx.arr[0], "channel") == "NETB" && str(tx.arr[0], "kind") == "sounding" && bl(tx.arr[0], "open"));
            CHECK(str(tx.arr[1], "channel") == "unlisted" && nm(tx.arr[1], "gaps") == 1);
            CHECK(str(tx.arr[2], "channel") == "W2C1" && str(tx.arr[2], "kind") == "call" && str(tx.arr[2], "caller") == "AB1XY");
            CHECK(str(tx.arr[3], "channel") == "P1" && str(tx.arr[3], "kind") == "sounding" && !bl(tx.arr[3], "open"));
        }
        CHECK(str(sub(v, "incoming"), "caller") == "AB1XY" && str(sub(v, "incoming"), "channel") == "W2C1");
        CHECK(str(sub(v, "last_word"), "channel") == "NETB" && nm(sub(v, "last_word"), "window") == 2);
        JsonValue lh = sub(v, "last_hour");
        CHECK(nm(lh, "words") == 9 && nm(lh, "gaps") == 1 && nm(lh, "local") == 4);
        CHECK(nm(lh, "station") == 4 && nm(lh, "remote") == 1 && nm(lh, "primary") == 5);
        v = parsed(api(*core, R"({"cmd":"word_feed","limit":"1"})"));
        CHECK(sub(v, "transmissions").arr.size() == 1);
        // Not ours: no incoming.
        write(gale / "own_address.txt", "ZZZ\n");
        v = parsed(api(*core, R"({"cmd":"word_feed"})"));
        CHECK(sub(v, "incoming").type == JsonValue::Type::Null);
        write(gale / "own_address.txt", "CD2ZZ\n");
    }

    // ---- worlds (ask 38): channel labels, chan lines, amateur words -----------
    {
        // A newer scanner's status: a count, labels (null: none) and worlds, no kHz.
        sv::ChannelNames n(*core);
        std::string named = sv::name_channels(
            R"({"ok":true,"window":2,"channels":3,"channel_labels":["NETB",null,"07DALE"],)"
            R"("channel_worlds":["mars","mars","amateur"],"world":"both","hold":{"window":2,"record_channel":1,"record_label":"NETB"}})",
            n);
        JsonValue v = parsed(named);
        CHECK(no_freq(named));
        JsonValue ch = sub(v, "channels"), lb = sub(v, "channel_labels");
        CHECK(ch.is_array() && ch.arr.size() == 3 && ch.arr[0].s == "NETB" && ch.arr[1].s == "W2C2" && ch.arr[2].s == "07DALE");
        CHECK(lb.is_array() && lb.arr.size() == 3 && lb.arr[1].type == JsonValue::Type::Null && lb.arr[2].s == "07DALE");
        CHECK(nm(v, "channel_count") == 3 && str(v, "world") == "both" && sub(v, "channel_worlds").arr.size() == 3);
        CHECK(nm(sub(v, "hold"), "record_channel") == 1 && str(sub(v, "hold"), "record_label") == "NETB");
        // An older one's channels_khz comes out the same way.
        v = parsed(sv::name_channels(R"({"ok":true,"window":1,"channels_khz":[13579.246,13582.468]})", n));
        CHECK(sub(v, "channels").arr.size() == 2 && sub(v, "channel_labels").arr[0].s == "P1" && nm(v, "channel_count") == 2);
        CHECK(!v.get("channels_khz"));

        // chan lines name a channel and give its world.
        write(gale / "plans/scan.plan", std::string(kPlan) + "chan 4864213 07DALE amateur plus auto\n");
        sv::ChannelNames n2(*core);
        CHECK(n2.name(4864.213) == "07DALE" && n2.world(4864.213) == "amateur" && n2.world(17532.975).empty());

        // The amateur world's words: their own log and their own addresses.
        fs::create_directories(gale / "amateur");
        std::string aw;
        for (auto& l : {word(now - 8, 4864.213, "TO", "N0C", 9.0, 1), word(now - 7.6, 4864.213, "DATA", "ALL", 9.0, 1),
                        word(now - 7.4, 4864.213, "TIS", "K0T", 9.0, 1), word(now - 7.2, 4864.213, "DATA", "EST", 9.0, 1)})
            aw += l + "\n";
        write(gale / "amateur/words-20991231.jsonl", aw);
        write(gale / "amateur/own_address.txt", "N0CALL\n");
        v = parsed(api(*core, R"({"cmd":"word_feed","world":"amateur"})"));
        CHECK(okv(v) && str(v, "world") == "amateur" && bl(v, "amateur_own_address_file"));
        CHECK(sub(v, "transmissions").arr.size() == 1);
        if (sub(v, "transmissions").arr.size() == 1) {
            JsonValue t = sub(v, "transmissions").arr[0];
            CHECK(str(t, "world") == "amateur" && str(t, "channel") == "07DALE" && str(t, "caller") == "K0TEST");
        }
        CHECK(str(sub(v, "incoming"), "caller") == "K0TEST" && str(sub(v, "incoming"), "world") == "amateur");
        CHECK(str(sub(v, "last_word"), "world") == "amateur");
        // MARS's log stays MARS's: its rows say mars, and the amateur address
        // isn't ours there.
        v = parsed(api(*core, R"({"cmd":"word_feed"})"));
        CHECK(str(v, "world") == "mars" && str(sub(v, "transmissions").arr[0], "world") == "mars");
        CHECK(!okv(parsed(api(*core, R"({"cmd":"word_feed","world":"hfn"})"))));
        write(gale / "plans/scan.plan", kPlan);
        fs::remove_all(gale / "amateur");
    }

    // ---- heard stations: from the word log, then from the LQA database ------
    {
        std::string r = api(*core, R"({"cmd":"heard_stations"})");
        JsonValue v = parsed(r);
        CHECK(okv(v) && str(v, "source") == "word log" && no_freq(r));
        JsonValue st = sub(v, "stations");
        // AB1XY (heard on 3 channels, a favourite), GH4, then QQ9 (favourite, not heard).
        CHECK(st.arr.size() == 3);
        if (st.arr.size() == 3) {
            CHECK(str(st.arr[0], "address") == "AB1XY" && bl(st.arr[0], "favourite") && str(st.arr[0], "name") == "Bob");
            CHECK(nm(st.arr[0], "heard") == 3 && nm(st.arr[0], "channels") == 3 && str(st.arr[0], "last_channel") == "NETB");
            CHECK(str(st.arr[1], "address") == "GH4" && !bl(st.arr[1], "favourite"));
            CHECK(str(st.arr[2], "address") == "QQ9" && bl(st.arr[2], "favourite") && nm(st.arr[2], "heard") == 0);
        }
        sqlite3* db = nullptr;
        CHECK(sqlite3_open((gale / "lqa.sqlite").c_str(), &db) == SQLITE_OK);
        std::string sql = "CREATE TABLE transmissions (id INTEGER PRIMARY KEY, khz REAL, start REAL, end REAL, caller TEXT, "
                          "called TEXT, kind TEXT, words INT, sinad_mean REAL);"
                          "INSERT INTO transmissions (khz,start,end,caller,sinad_mean) VALUES "
                          "(13579.246," + std::to_string(now - 100) + "," + std::to_string(now - 99) + ",'JK5',7.5),"
                          "(17532.975," + std::to_string(now - 50) + "," + std::to_string(now - 49) + ",'JK5',11.0),"
                          "(13579.246,1600000000,1600000001,'OLDTEST',1.0);";
        CHECK(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(db);
        r = api(*core, R"({"cmd":"heard_stations"})");
        v = parsed(r);
        CHECK(str(v, "source") == "lqa" && no_freq(r));
        JsonValue s2 = sub(v, "stations");
        CHECK(s2.arr.size() == 3);  // JK5, then the two favourites
        if (!s2.arr.empty()) {
            CHECK(str(s2.arr[0], "address") == "JK5" && nm(s2.arr[0], "heard") == 2 && nm(s2.arr[0], "heard_24h") == 2);
            CHECK(str(s2.arr[0], "last_channel") == "NETB" && nm(s2.arr[0], "best_sinad") == 11.0);
        }
        CHECK(!contains(r, "OLDTEST"));
        fs::remove(gale / "lqa.sqlite");
    }

    // ---- station health -------------------------------------------------------
    {
        char b[600];
        std::snprintf(b, sizeof b,
                      R"({"confirmed":true,"t_unix":%.0f,"utc_minus_anchor_s":0.0021,"sigma_ms":0.4,"band_khz":13579.246,"count_ppm":-1.234})",
                      now - 600);
        write(gale / "wwv/clock.json", b);
        std::snprintf(b, sizeof b, R"({"t_unix":%.0f,"block":7,"count_err_ms":0.12,"count_hours":1.5})", now - 600);
        write(gale / "wwv/clock_history.jsonl", std::string("junk\n") + b + "\n");
        std::snprintf(b, sizeof b,
                      R"({"fix":true,"sats":9,"utc":"x","source":"pps","host_ns":%.0f,"host_minus_utc_ms":0.0123,"device":{"connected":true}})",
                      now * 1e9);
        write(gale / "gps/status.json", b);
        std::snprintf(b, sizeof b, R"({"state":"warn","problems":["disk 91%% full"],"t":%.0f})", now - 5);
        write(gale / "health.json", b);
        std::snprintf(b, sizeof b,
                      R"({"connected":true,"last_frame":%.0f,"t":%.0f,"front_end":{"overloads_total":2,"gap_visits":1,"local_visits":4}})",
                      now - 1, now - 1);
        write(gale / "engine_status.json", b);
        write(gale / "radio.json", R"({"t":1,"radio":"no radio","why":"no RSP2 found","commit":"x"})");
        write(gale / "ale-scan.log",
              "[ale_scan_gangleri] 2026-11-25T10:00:00Z starting ale-scan: plan x\n"
              "ADC overload DETECTED\n"
              "ale-scan: decoding on engine 127.0.0.1:7373\n"
              "rsp2 timeline restart\n"
              "visit w2, engine, peak -12.5 dBFS\n");
        std::string r = api(*core, R"({"cmd":"station_health"})");
        JsonValue v = parsed(r);
        CHECK(okv(v) && no_freq(r));
        JsonValue ck = sub(v, "clock");
        CHECK(str(ck, "set_by") == "wwv" && bl(sub(ck, "wwv"), "fix") && !bl(sub(ck, "wwv"), "stale"));
        CHECK(nm(sub(ck, "wwv"), "offset_ms") == 2.1 && nm(sub(ck, "wwv"), "tcxo_ppm") == -1.23);
        CHECK(nm(sub(sub(ck, "wwv"), "last_fix"), "block") == 7 && nm(sub(ck, "wwv"), "history_bad_lines") == 1);
        CHECK(bl(sub(ck, "gps"), "fresh") && bl(sub(ck, "gps"), "fix") && nm(sub(ck, "gps"), "sats") == 9 &&
              bl(sub(ck, "gps"), "device_connected"));
        CHECK(str(sub(v, "health"), "state") == "warn" && sub(sub(v, "health"), "problems").arr.size() == 1);
        CHECK(bl(sub(v, "engine"), "connected") && nm(sub(v, "engine"), "overloads_total") == 2);
        JsonValue sl = sub(v, "scanner_log");
        CHECK(bl(sl, "available") && str(sl, "state") == "running" && str(sl, "link") == "up");
        CHECK(nm(sl, "overloads") == 1 && nm(sl, "restarts") == 1 && str(sl, "last_visit") == "engine");
        CHECK(str(sub(v, "radio"), "radio") == "no radio");
        JsonValue wh = sub(v, "words_last_hour");
        CHECK(nm(wh, "words") == 9 && nm(wh, "local") == 4 && nm(wh, "station") == 4 && nm(wh, "remote") == 1 &&
              nm(wh, "primary") == 5);
        // A stop and a stale clock.
        write(gale / "ale-scan.log", "[ale_scan_gangleri] 2026-11-25T10:00:00Z starting ale-scan\n"
                                     "[ale_scan_gangleri] 2026-11-25T11:00:00Z stopped; no plan: x\n");
        std::snprintf(b, sizeof b, R"({"confirmed":true,"t_unix":%.0f,"utc_minus_anchor_s":0.0})", now - 9 * 3600);
        write(gale / "wwv/clock.json", b);
        v = parsed(api(*core, R"({"cmd":"station_health"})"));
        CHECK(str(sub(v, "scanner_log"), "state") == "stopped" && bl(sub(sub(v, "clock"), "wwv"), "stale"));
    }

    // ---- captures --------------------------------------------------------------
    {
        fs::path cap = gale / "captures";
        write(cap / "20991231T000001Z_data.wav", std::string(1000, 'x'));
        write(cap / "20991231T000001Z_data.json",
              R"({"utc_start_unix":4102444801.5,"seconds":12.34,"trigger":"data","khz":17531.864,"window":2,"clock":"wwv","lost_s":0.0,"ale_offset_hz":30})");
        write(cap / "20991231T000001Z_data.modems.json", "{}");
        write(cap / "20991231T000001Z_data.m110a.0.bin", "x");
        write(cap / "20991231T000001Z_data.m110a.1.bin", "x");
        write(cap / "20991231T000002Z_net.wav", std::string(10, 'x'));
        write(cap / "20991231T000002Z_net.json", R"({"seconds":3,"trigger":"net","label":"NETB","khz":17532.975})");
        write(cap / "20991231T000003Z_open.wav", std::string(44, 'x'));
        std::string r = api(*core, R"({"cmd":"captures_list"})");
        JsonValue v = parsed(r);
        CHECK(okv(v) && bl(v, "available") && nm(v, "total") == 3 && no_freq(r));
        JsonValue c = sub(v, "captures");
        CHECK(c.arr.size() == 3);
        if (c.arr.size() == 3) {
            CHECK(str(c.arr[0], "name") == "20991231T000003Z_open" && bl(c.arr[0], "open"));
            CHECK(str(c.arr[1], "channel") == "NETB" && str(c.arr[1], "trigger") == "net");
            CHECK(str(c.arr[2], "channel") == "W2C1" && nm(c.arr[2], "window") == 2 && bl(c.arr[2], "modems"));
            CHECK(nm(c.arr[2], "payloads") == 2 && nm(c.arr[2], "seconds") == 12.3 && nm(c.arr[2], "size") == 1000);
        }
        v = parsed(api(*core, R"({"cmd":"captures_list","limit":"1"})"));
        CHECK(sub(v, "captures").arr.size() == 1);
        CHECK(!v.get("fallback_folder") && !sub(v, "captures").arr[0].get("fallback"));  // no fallback set

        // The engine's fallback folder too (phoenix_nest_mars#3, ask 11): a
        // capture only there is listed from it; one in both, once, from the store.
        fs::path fb = gale / "fallback/captures";
        write(fb / "20991231T000004Z_down.wav", std::string(500, 'x'));
        write(fb / "20991231T000004Z_down.json", R"({"seconds":2,"trigger":"data","label":"NETB"})");
        write(fb / "20991231T000004Z_down.modems.json", "{}");
        write(fb / "20991231T000004Z_down.m110a.0.bin", "x");
        write(fb / "20991231T000002Z_net.wav", std::string(10, 'x'));
        sv::Paths pf = p;
        pf.captures_fallback = fb.string();
        sv::set_paths(pf);
        r = api(*core, R"({"cmd":"captures_list"})");
        v = parsed(r);
        CHECK(okv(v) && bl(v, "available") && nm(v, "total") == 4 && no_freq(r));
        CHECK(bl(v, "store_folder") && bl(v, "fallback_folder"));
        c = sub(v, "captures");
        CHECK(c.arr.size() == 4);
        if (c.arr.size() == 4) {
            CHECK(str(c.arr[0], "name") == "20991231T000004Z_down" && bl(c.arr[0], "fallback"));
            CHECK(!bl(c.arr[0], "open") && bl(c.arr[0], "modems") && nm(c.arr[0], "payloads") == 1);
            CHECK(str(c.arr[2], "name") == "20991231T000002Z_net" && !bl(c.arr[2], "fallback"));
            CHECK(str(c.arr[2], "trigger") == "net");  // the store's copy, with its sidecar
        }
        // The store folder gone: the fallback alone is listed.
        pf.captures = (gale / "no-such-captures").string();
        sv::set_paths(pf);
        v = parsed(api(*core, R"({"cmd":"captures_list"})"));
        CHECK(okv(v) && bl(v, "available") && nm(v, "total") == 2 && !bl(v, "store_folder"));
        sv::set_paths(p);
    }

    // ---- the scanner: status and orders ----------------------------------------
    {
        std::string sock = (dir / "scan.sock").string();
        Scanner sc(sock);
        sc.set_status(
            R"({"ok":true,"state":"held","plan":"scan","windows":2,"window":2,"channels_khz":[17531.864,17532.975],"visit":3,"visit_s":1.25,"visits":40,"stream_unix":1.8e9,"hold":{"window":2,"left_s":55.5,"record_khz":17532.975,"record_label":"operator-NETB"},"order_pending":false,"plan_pending":false,"engine":true,"visit_remote":true,"clock":"wwv","clock_counter":true,"wwv_blocks":3,"last_wwv_unix":1.8e9,"plan_switches":0,"note":""})");
        auto lm = std::make_shared<LinkMonitor>(sock, 60000);
        lm->poll_once();
        core->set_link_monitor(lm);
        std::string r = api(*core, R"({"cmd":"scanner_status"})");
        JsonValue v = parsed(r);
        CHECK(okv(v) && bl(v, "source") && bl(v, "reachable") && no_freq(r));
        // The call to CD2ZZ 20 s ago is ours: incoming wins over hold.
        CHECK(str(v, "view") == "incoming" && str(sub(v, "incoming"), "caller") == "AB1XY");
        JsonValue s = sub(v, "scanner");
        CHECK(str(s, "state") == "held" && nm(s, "windows") == 2 && bl(s, "engine") && str(s, "clock") == "wwv");
        CHECK(sub(s, "channels").arr.size() == 2 && sub(s, "channels").arr[0].s == "W2C1" && sub(s, "channels").arr[1].s == "NETB");
        CHECK(str(sub(s, "hold"), "record_label") == "operator-NETB" && nm(sub(s, "hold"), "left_s") == 55.5);
        write(gale / "own_address.txt", "ZZZ\n");
        v = parsed(api(*core, R"({"cmd":"scanner_status"})"));
        CHECK(str(v, "view") == "hold");

        // Orders.
        CHECK(okv(parsed(api(*core, R"({"cmd":"scanner_hold","window":"2","seconds":"60"})"))));
        CHECK(okv(parsed(api(*core, R"({"cmd":"scanner_hold","window":"1"})"))));
        CHECK(okv(parsed(api(*core, R"({"cmd":"scanner_release"})"))));
        r = api(*core, R"({"cmd":"scanner_record","channel":"P1","seconds":"30"})");
        CHECK(okv(parsed(r)) && no_freq(r));
        CHECK(okv(parsed(api(*core, R"({"cmd":"scanner_record","channel":"W2C1","seconds":"30"})"))));
        CHECK(okv(parsed(api(*core, R"({"cmd":"scanner_record","channel":"NETB","seconds":"900"})"))));
        CHECK(okv(parsed(api(*core, R"({"cmd":"scanner_plan","name":"night.plan"})"))));
        v = parsed(api(*core, R"({"cmd":"scanner_plan","name":"nope"})"));
        CHECK(!okv(v) && contains(str(v, "error"), "no plan nope"));
        auto o = sc.orders();
        CHECK(o.size() == 8);
        if (o.size() == 8) {
            CHECK(o[0] == "hold 2 60" && o[1] == "hold 1" && o[2] == "release");
            CHECK(o[3] == "record 13579.246 30" && o[4] == "record 17531.864 30" && o[5] == "record NETB 900");
            CHECK(o[6] == "plan night.plan" && o[7] == "plan nope");
        }
        // Refused here, never sent.
        for (const char* bad : {R"({"cmd":"scanner_hold","window":"0"})", R"({"cmd":"scanner_hold","window":"x"})",
                                R"({"cmd":"scanner_hold","window":"1","seconds":"901"})",
                                R"({"cmd":"scanner_hold","window":"1","seconds":"-5"})",
                                R"({"cmd":"scanner_record","channel":"NETB","seconds":"0"})",
                                R"({"cmd":"scanner_record","channel":"","seconds":"5"})",
                                R"({"cmd":"scanner_record","channel":"a b\nrelease","seconds":"5"})",
                                R"({"cmd":"scanner_plan","name":"../etc/passwd"})", R"({"cmd":"scanner_plan","name":"a b"})",
                                R"({"cmd":"scanner_plan"})"})
            CHECK(!okv(parsed(api(*core, bad))));
        CHECK(sc.orders().size() == 8);
    }
    // The scanner gone: the last status stays, marked stale; orders fail.
    {
        JsonValue v = parsed(api(*core, R"({"cmd":"scanner_status"})"));
        auto lm = core->link_monitor();
        lm->poll_once();
        v = parsed(api(*core, R"({"cmd":"scanner_status"})"));
        CHECK(okv(v) && !bl(v, "reachable") && bl(v, "scanner_stale") && str(v, "view") == "no scanner");
        CHECK(str(sub(v, "scanner"), "state") == "held");
        v = parsed(api(*core, R"({"cmd":"scanner_release"})"));
        CHECK(!okv(v) && contains(str(v, "error"), "didn't answer"));
    }
    // status carries what gale-web's status bar reads (issue 3 item 9).
    {
        JsonValue v = parsed(api(*core, R"({"cmd":"status"})"));
        for (const char* k : {"net", "next_net", "acked", "received", "unacked", "queued", "held", "failed"})
            CHECK(okv(v) && v.get(k) != nullptr);
    }
    // An unknown cmd still names the new calls.
    CHECK(contains(api(*core, R"({"cmd":"nope"})"), "scanner_status"));

    core.reset();
    fs::remove_all(dir);
    return check::report("test_station_view");
}
