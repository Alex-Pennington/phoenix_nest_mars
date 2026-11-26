// The greybeard-ale adapter: SHA-256, the JSON reader, payload listings, and two
// cores passing traffic through the stand-in gale-tx and the stand-in channel.
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/greybeard_modem.h"
#include "../src/json.h"
#include "../src/ops.h"
#include "../src/run_program.h"
#include "../src/sha256.h"
#include "check.h"
#include "services_fixture.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static const std::string kSrc = PNA_SOURCE_DIR;

static void write(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

static double json_num(const std::string& reply, const char* k) {
    JsonValue v;
    if (!parse_json(reply, v) || !v.is_object()) return -1;
    const JsonValue* x = v.get(k);
    if (!x) {
        const JsonValue* q = v.get("queue");
        x = q && q->is_object() ? q->get(k) : nullptr;
    }
    return x && x->type == JsonValue::Type::Number ? x->n : -1;
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

    // Two capture folders (phoenix_nest_mars#3, ask 11): the store's and the
    // local fallback the engine writes to while the store fails. Both are
    // watched; a listing is taken once by its name, whichever folder holds it.
    {
        fs::path store = dir / "store-cap", fb = dir / "fallback-cap";
        fs::create_directories(store);
        fs::create_directories(fb);
        write(fb / "old.modems.json", listing("old.bin", "OLDFB"));  // there before the first start
        write(fb / "old.bin", "OLDFB");
        GreybeardConfig c;
        c.mode = "600S";
        c.captures_dir = store.string();
        c.captures_fallback_dir = fb.string();
        c.work_dir = (dir / "w3").string();
        c.outbox_dir = (dir / "o3").string();
        c.state_file = (dir / "seen3.txt").string();
        c.poll_ms = 50;
        c.settle_ms = 400;
        Rx rx;
        GreybeardModem m(c);
        m.start(rx.cb());
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CHECK(rx.text().empty());  // old traffic in the fallback isn't fed in either
        // Written to the fallback while the store was down.
        write(fb / "e.m110a.0.bin", "FROMFB");
        write(fb / "e.modems.json", listing("e.m110a.0.bin", "FROMFB"));
        CHECK(wait_for([&] { return rx.text() == "FROMFB"; }, 2000));
        // The store is back and the catch-up moves it in: not taken again.
        fs::rename(fb / "e.m110a.0.bin", store / "e.m110a.0.bin");
        fs::rename(fb / "e.modems.json", store / "e.modems.json");
        // Half moved: the listing is in the store, its payload still in the fallback.
        write(fb / "f.m110a.0.bin", "HALF");
        write(store / "f.modems.json", listing("f.m110a.0.bin", "HALF"));
        CHECK(wait_for([&] { return rx.text() == "FROMFBHALF"; }, 2000));
        // In both folders at once: taken once.
        for (auto& d : {store, fb}) {
            write(d / "g.m110a.0.bin", "BOTH");
            write(d / "g.modems.json", listing("g.m110a.0.bin", "BOTH"));
        }
        CHECK(wait_for([&] { return rx.text() == "FROMFBHALFBOTH"; }, 2000));
        // Written to the store as usual.
        write(store / "h.m110a.0.bin", "STORE");
        write(store / "h.modems.json", listing("h.m110a.0.bin", "STORE"));
        CHECK(wait_for([&] { return rx.text() == "FROMFBHALFBOTHSTORE"; }, 2000));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(rx.text() == "FROMFBHALFBOTHSTORE");
        {
            std::lock_guard<std::mutex> g(rx.mu);
            CHECK(rx.decodes.size() == 4);
        }
        m.stop();
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
        a.set_services(test_services());
        Core b(CoreConfig{"B", (dir / "B.db").string(), 3, 1, true},
               std::make_unique<GreybeardModem>(cfg("B", "", capB)));
        b.set_services(test_services());
        a.start();
        b.start();
        std::atomic<bool> run{true};
        std::thread channel([&] {
            while (run) {
                run_program({"python3", "-I", kSrc + "/tools/stub_channel.py", "--once", "--key-results", "--from",
                             (dir / "A-outbox").string(), "--to", capB, "--from", (dir / "B-outbox").string(),
                             "--to", capA},
                            10000);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        std::string err;
        std::string id = a.submit("B", 'P', "HELLO B THROUGH GREYBEARD\r\nLINE 2", &err, SendOpts::in("mars"));
        CHECK(!id.empty());
        CHECK(wait_for([&] { auto m = a.get(id); return m && m->state == "acked"; }, 20000));
        // The channel played gale-key's part: each burst's key result came back.
        CHECK(wait_for([&] {
            for (auto& e : a.events(0, 500))
                if (e.text.find("keyed by gale-key") != std::string::npos) return true;
            return false;
        }, 5000));
        auto rx = b.get(id);
        CHECK(rx && rx->body == "HELLO B THROUGH GREYBEARD\r\nLINE 2" && rx->src == "A");
        std::string back = b.submit("A", 'Z', "ROGER", &err, SendOpts::in("mars"));
        CHECK(wait_for([&] { auto m = b.get(back); return m && m->state == "acked"; }, 20000));
        CHECK(a.get(back) && a.get(back)->prec == 'Z');
        run = false;
        channel.join();
        a.stop();
        b.stop();
    }

    // The outbox write-back: a burst in the outbox is "written", not sent.
    // gale-key's BURST.key.json says it keyed it (sent, the ack clock starts)
    // or refused (failed, with its reason); one that can't be read is kept as
    // .bad and changes nothing.
    {
        GreybeardConfig c;
        c.mode = "600S";
        c.tx_cmd = {"python3", "-I", kSrc + "/tools/stub_gale_tx.py"};
        c.captures_dir = (dir / "K-cap").string();
        c.work_dir = (dir / "K-work").string();
        c.outbox_dir = (dir / "K-outbox").string();
        c.state_file = (dir / "K-seen.txt").string();
        c.poll_ms = 50;
        CoreConfig cc{"K", (dir / "K.db").string(), 3, 1, true};
        cc.ack_timeout_s = 600;
        Core k(cc, std::make_unique<GreybeardModem>(c));
        k.set_services(test_services());
        k.start();
        auto burst_of = [&](const std::string& id) {
            for (auto& e : k.events(0, 500))
                if (e.msg_id == id && e.text.rfind("written to the outbox as ", 0) == 0) {
                    std::string b = e.text.substr(25);
                    return b.substr(0, b.find(';'));
                }
            return std::string();
        };
        auto state = [&](const std::string& id) {
            auto m = k.get(id);
            return m ? m->state : std::string();
        };
        std::string err;
        const std::string one = k.submit("FAR", 'R', "KEY ME", &err, SendOpts::in("mars")), two = k.submit("FAR", 'R', "REFUSE ME", &err, SendOpts::in("mars"));
        CHECK(wait_for([&] { return state(one) == "written" && state(two) == "written"; }, 10000));
        const std::string b1 = burst_of(one), b2 = burst_of(two);
        CHECK(!b1.empty() && fs::exists(fs::path(c.outbox_dir) / b1) && !b2.empty());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(state(one) == "written");  // nothing keyed it: still not sent
        CHECK(json_num(ApiServer::handle(k, R"({"cmd":"status"})"), "written") == 2);
        auto key = [&](const std::string& b, const std::string& body) {
            write(fs::path(c.outbox_dir) / (b.substr(0, b.size() - 4) + ".key.json"), body);
        };
        // One that names another burst is not taken.
        key(b1, R"({"version":1,"burst":")" + b2 + R"(","result":"keyed"})");
        CHECK(wait_for([&] { return fs::exists(fs::path(c.outbox_dir) / (b1.substr(0, b1.size() - 4) + ".key.json.bad")); },
                       3000));
        CHECK(state(one) == "written" && state(two) == "written");
        key(b1, R"({"version":1,"burst":")" + b1 + R"(","result":"keyed","why":""})");
        key(b2, R"({"version":1,"burst":")" + b2 + R"(","result":"refused","why":"EMCON is on"})");
        CHECK(wait_for([&] { return state(one) == "sent" && state(two) == "failed"; }, 3000));
        {
            auto m = k.get(two);
            CHECK(m && m->last_error == "not keyed: EMCON is on");
            auto s = k.get(one);
            CHECK(s && s->sends == 1 && s->next_try_at > 0);  // the ack clock runs from the key
            CHECK(!fs::exists(fs::path(c.outbox_dir) / (b1.substr(0, b1.size() - 4) + ".key.json")));
        }
        // A refused one goes again only when the operator requeues it.
        CHECK(k.requeue(two, &err));
        CHECK(wait_for([&] { return state(two) == "written"; }, 10000));
        k.stop();
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
        u.set_services(test_services());
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

    // C4: no log line quotes a capture's file name with its dial tag
    // (_<dial>kHz). The channel's designator stands in, else "a capture".
    {
        std::string cap = (dir / "K-cap").string();
        fs::create_directories(cap);
        GreybeardConfig c;
        c.mode = "600S";
        c.captures_dir = cap;
        c.work_dir = (dir / "K-work").string();
        c.outbox_dir = (dir / "K-outbox").string();
        c.state_file = (dir / "K-seen.txt").string();
        c.poll_ms = 50;
        c.settle_ms = 300;
        const std::string db = (dir / "K.db").string();
        Core k(CoreConfig{"K", db, 3, 1, true}, std::make_unique<GreybeardModem>(c));
        k.set_services(test_services());
        k.set_ops(std::make_shared<Ops>("K", db));
        CHECK(ApiServer::handle(k, R"({"cmd":"channel_put","name":"P1","frequency":"13579"})").find("\"ok\":true") !=
              std::string::npos);
        std::mutex lm;
        std::string lines;
        k.on_log([&](const std::string& t) {
            std::lock_guard<std::mutex> g(lm);
            lines += t + "\n";
        });
        k.start();
        const fs::path cp(cap);
        const std::string raw = "SOME DECODED BYTES";
        // A decode, then the same bytes from another capture.
        write(cp / "20991231T000010Z_13579kHz.m110a.0.bin", raw);
        write(cp / "20991231T000010Z_13579kHz.modems.json", listing("20991231T000010Z_13579kHz.m110a.0.bin", raw));
        CHECK(wait_for([&] { return k.status().counts.unverified == 1; }, 3000));
        write(cp / "20991231T000011Z_13579kHz.m110a.0.bin", raw);
        write(cp / "20991231T000011Z_13579kHz.modems.json", listing("20991231T000011Z_13579kHz.m110a.0.bin", raw));
        // A wrong digest, on a dial in no channel; a listing that isn't JSON;
        // one with no payloads; one with no sha256.
        write(cp / "20991231T000012Z_7345.5kHz.m110a.0.bin", "BAD");
        write(cp / "20991231T000012Z_7345.5kHz.modems.json",
              listing("20991231T000012Z_7345.5kHz.m110a.0.bin", "BAD", false));
        write(cp / "20991231T000013Z_7345kHz.modems.json", "{not json");
        write(cp / "20991231T000014Z_13579kHz.modems.json", R"({"files":[]})");
        write(cp / "20991231T000015Z_13579kHz.m110a.0.bin", "NODIGEST");
        write(cp / "20991231T000015Z_13579kHz.modems.json",
              R"({"files":[{"file":"20991231T000015Z_13579kHz.m110a.0.bin","bytes":8}]})");
        auto logged = [&] {
            std::lock_guard<std::mutex> g(lm);
            return lines;
        };
        CHECK(wait_for([&] { return logged().find("not readable JSON") != std::string::npos; }, 3000));
        CHECK(wait_for([&] { return logged().find("wrong digest") != std::string::npos; }, 3000));
        CHECK(wait_for([&] { return logged().find("lists no payload files") != std::string::npos; }, 3000));
        CHECK(wait_for([&] { return logged().find("no sha256") != std::string::npos; }, 3000));
        CHECK(wait_for([&] { return logged().find("came again") != std::string::npos; }, 3000));
        std::string all = logged();
        for (auto& e : k.events(0, 1000)) all += e.text + "\n";
        std::string low = all;
        for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        CHECK(low.find("khz") == std::string::npos);
        if (low.find("khz") != std::string::npos) std::fprintf(stderr, "%s", all.c_str());
        // The designator where the dial is a channel, "a capture" where not.
        CHECK(all.find("bytes from a capture on P1") != std::string::npos);
        CHECK(all.find("skipped a capture:") != std::string::npos ||
              all.find("skipped a capture ") != std::string::npos);
        k.stop();
    }

    fs::remove_all(dir);
    return check::report("test_greybeard");
}
