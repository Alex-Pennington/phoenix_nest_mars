// The greybeard-ale adapter: SHA-256, the JSON reader, payload listings, and two
// cores passing traffic through the stand-in gale-tx and the stand-in channel.
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/greybeard_modem.h"
#include "../src/json.h"
#include "../src/run_program.h"
#include "../src/sha256.h"
#include "check.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static const std::string kSrc = PNA_SOURCE_DIR;

static void write(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

static bool wait_for(const std::function<bool()>& cond, int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return cond();
}

static std::string listing(const std::string& bin, const std::string& data, bool good_digest = true) {
    return "{\"files\":[{\"file\":\"" + bin + "\",\"bytes\":" + std::to_string(data.size()) + ",\"sha256\":\"" +
           (good_digest ? sha256_hex(data) : std::string(64, '0')) + "\"}]}";
}

struct Rx {
    std::mutex mu;
    std::string got;
    std::vector<std::string> events;
    std::vector<ModemDecode> decodes;
    ModemDriver::Callbacks cb() {
        ModemDriver::Callbacks c;
        c.on_rx = [this](const std::string& b) { std::lock_guard<std::mutex> g(mu); got += b; };
        c.on_event = [this](const std::string& e) { std::lock_guard<std::mutex> g(mu); events.push_back(e); };
        c.on_tx_done = [](bool, const std::string&) {};
        c.on_decode = [this](const ModemDecode& d) { std::lock_guard<std::mutex> g(mu); decodes.push_back(d); };
        return c;
    }
    std::string text() { std::lock_guard<std::mutex> g(mu); return got; }
};

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_gb_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    // SHA-256 test vectors (FIPS 180-4 examples).
    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha256_hex(std::string(1000, 'a')) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");

    // Whole JSON values.
    {
        JsonValue v;
        CHECK(parse_json(R"({"files":[{"file":"x.bin","bytes":12,"ok":true,"n":null}],"e":-1.5e2})", v));
        CHECK(v.is_object() && v.get("files") && v.get("files")->arr.size() == 1);
        CHECK(v.get("e") && v.get("e")->n == -150);
        CHECK(!parse_json("{\"a\":1} x", v));
        CHECK(!parse_json("[1,2", v));
        CHECK(!parse_json(std::string(40, '[') + std::string(40, ']'), v));  // too deep
    }

    // Running a program.
    {
        RunResult r = run_program({"sh", "-c", "echo out; echo err >&2; exit 3"}, 5000);
        CHECK(r.started && r.exit_code == 3 && r.out == "out\n" && r.err == "err\n");
        r = run_program({"sleep", "5"}, 200);
        CHECK(r.started && r.timed_out);
        r = run_program({"/no/such/program"}, 1000);
        CHECK(!r.started);
    }

    // Receive: a listing is handled once its files are whole, skipped files never
    // reach the core, and a file name with a path is refused.
    {
        fs::path cap = dir / "cap1";
        fs::create_directories(cap);
        write(cap / "old.modems.json", listing("old.bin", "OLD"));  // there before the first start
        write(cap / "old.bin", "OLD");
        GreybeardConfig c;
        c.mode = "600S";
        c.captures_dir = cap.string();
        c.work_dir = (dir / "w1").string();
        c.outbox_dir = (dir / "o1").string();
        c.state_file = (dir / "seen1.txt").string();
        c.poll_ms = 50;
        c.settle_ms = 400;
        Rx rx;
        {
            GreybeardModem m(c);
            m.start(rx.cb());
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            CHECK(rx.text().empty());  // old traffic is not fed in again

            // Listing first, payload not yet written: wait, then take it.
            write(cap / "a.modems.json", listing("a.m110a.0.bin", "HELLO"));
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            CHECK(rx.text().empty());
            write(cap / "a.m110a.0.bin", "HELLO");
            CHECK(wait_for([&] { return rx.text() == "HELLO"; }, 2000));

            // Wrong digest: never delivered, given up after settle_ms.
            write(cap / "b.m110a.0.bin", "BAD");
            write(cap / "b.modems.json", listing("b.m110a.0.bin", "BAD", false));
            // A path in the file name is refused.
            write(cap / "c.modems.json", listing("../secret.bin", "X"));
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            CHECK(rx.text() == "HELLO");
            {
                // Each burst also comes whole, named, before the core reads it.
                std::lock_guard<std::mutex> g(rx.mu);
                CHECK(rx.decodes.size() == 1);
                CHECK(!rx.decodes.empty() && rx.decodes[0].bytes == "HELLO" &&
                      rx.decodes[0].source == "a.m110a.0.bin" && rx.decodes[0].waveform == "110A" &&
                      rx.decodes[0].sha256 == sha256_hex("HELLO"));
            }
            m.stop();
        }
        // A restart does not feed handled bursts again.
        Rx rx2;
        GreybeardModem m2(c);
        m2.start(rx2.cb());
        write(cap / "d.m110d.0.bin", "AFTER");
        write(cap / "d.modems.json", listing("d.m110d.0.bin", "AFTER"));
        CHECK(wait_for([&] { return rx2.text() == "AFTER"; }, 2000));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        CHECK(rx2.text() == "AFTER");
        {
            std::lock_guard<std::mutex> g(rx2.mu);
            CHECK(rx2.decodes.size() == 1 && rx2.decodes[0].waveform == "110D");
        }
        m2.stop();
    }

    // Send: a failing gale-tx is reported, not hidden.
    {
        GreybeardConfig c;
        c.mode = "600S";
        c.tx_cmd = {"sh", "-c", "echo broken >&2; exit 4", "gale-tx"};
        c.work_dir = (dir / "w2").string();
        c.outbox_dir = (dir / "o2").string();
        GreybeardModem m(c);
        std::mutex mu;
        bool done = false, ok = true;
        std::string why;
        ModemDriver::Callbacks cb;
        cb.on_tx_done = [&](bool o, const std::string& w) {
            std::lock_guard<std::mutex> g(mu);
            done = true, ok = o, why = w;
        };
        m.start(cb);
        CHECK(m.send("PAYLOAD"));
        CHECK(wait_for([&] { std::lock_guard<std::mutex> g(mu); return done; }, 3000));
        CHECK(!ok && why.find("exited 4") != std::string::npos);
        CHECK(fs::is_empty(c.outbox_dir));
        m.stop();
    }

    // Two cores, out and back, through the stand-in gale-tx and channel.
    {
        auto cfg = [&](const std::string& st, const std::string& their_out, const std::string& my_cap) {
            (void)their_out;
            GreybeardConfig c;
            c.mode = "600S";
            c.tx_cmd = {"python3", "-I", kSrc + "/tools/stub_gale_tx.py"};
            c.work_dir = (dir / (st + "-work")).string();
            c.outbox_dir = (dir / (st + "-outbox")).string();
            c.captures_dir = my_cap;
            c.state_file = (dir / (st + "-seen.txt")).string();
            c.poll_ms = 50;
            return c;
        };
        std::string capA = (dir / "A-cap").string(), capB = (dir / "B-cap").string();
        Core a(CoreConfig{"A", (dir / "A.db").string(), 3, 1, true},
               std::make_unique<GreybeardModem>(cfg("A", "", capA)));
        Core b(CoreConfig{"B", (dir / "B.db").string(), 3, 1, true},
               std::make_unique<GreybeardModem>(cfg("B", "", capB)));
        a.start();
        b.start();
        std::atomic<bool> run{true};
        std::thread channel([&] {
            while (run) {
                run_program({"python3", "-I", kSrc + "/tools/stub_channel.py", "--once", "--from",
                             (dir / "A-outbox").string(), "--to", capB, "--from", (dir / "B-outbox").string(),
                             "--to", capA},
                            10000);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        std::string err;
        std::string id = a.submit("B", 'P', "HELLO B THROUGH GREYBEARD\r\nLINE 2", &err);
        CHECK(!id.empty());
        CHECK(wait_for([&] { auto m = a.get(id); return m && m->state == "acked"; }, 20000));
        auto rx = b.get(id);
        CHECK(rx && rx->body == "HELLO B THROUGH GREYBEARD\r\nLINE 2" && rx->src == "A");
        std::string back = b.submit("A", 'Z', "ROGER", &err);
        CHECK(wait_for([&] { auto m = b.get(back); return m && m->state == "acked"; }, 20000));
        CHECK(a.get(back) && a.get(back)->prec == 'Z');
        run = false;
        channel.join();
        a.stop();
        b.stop();
    }

    // A decode goes into the inbox whole, flagged unverified (issue 3 ruling):
    // every byte shown, nothing acked, the same burst stored once.
    {
        std::string cap = (dir / "U-cap").string();
        fs::create_directories(cap);
        GreybeardConfig c;
        c.mode = "600S";
        c.captures_dir = cap;
        c.work_dir = (dir / "U-work").string();
        c.outbox_dir = (dir / "U-outbox").string();
        c.state_file = (dir / "U-seen.txt").string();
        c.poll_ms = 50;
        Core u(CoreConfig{"U", (dir / "U.db").string(), 3, 1, true}, std::make_unique<GreybeardModem>(c));
        u.start();
        const std::string raw = std::string("NOISE A\\B\x00\x7f\xff\tT\rR\nLINE 2", 23);
        write(fs::path(cap) / "u.m110a.0.bin", raw);
        write(fs::path(cap) / "u.modems.json", listing("u.m110a.0.bin", raw));
        const std::string id = "DEC-" + sha256_hex(raw).substr(0, 12);
        CHECK(wait_for([&] { return u.get(id).has_value(); }, 3000));
        auto m = u.get(id);
        CHECK(m && m->state == "unverified" && m->direction == "in" && m->src == "UNKNOWN");
        CHECK(m && m->body == "NOISE A\\\\B\\x00\\x7F\\xFF\\tT\\rR\nLINE 2");
        // The same bytes again (another capture of the same burst): one row.
        write(fs::path(cap) / "v.m110a.0.bin", raw);
        write(fs::path(cap) / "v.modems.json", listing("v.m110a.0.bin", raw));
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        CHECK(u.status().counts.unverified == 1);
        // The inbox list and status say so.
        std::string ls = ApiServer::handle(u, R"({"cmd":"list","dir":"in"})");
        CHECK(ls.find("\"id\":\"" + id + "\"") != std::string::npos && ls.find("\"unverified\":true") != std::string::npos);
        CHECK(ApiServer::handle(u, R"({"cmd":"status"})").find("\"unverified\":1") != std::string::npos);
        u.stop();
    }

    fs::remove_all(dir);
    return check::report("test_greybeard");
}
