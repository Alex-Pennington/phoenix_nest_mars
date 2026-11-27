// Store, queue order and two cores passing traffic over the loopback modem.
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/loopback_modem.h"
#include "../src/store.h"
#include "check.h"
#include "services_fixture.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static fs::path tmpdir() {
    fs::path d = fs::temp_directory_path() / ("pna_test_" + std::to_string(::getpid()));
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

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

int main() {
    fs::path dir = tmpdir();

    // Queue order: precedence first, then age; restart puts "sending" back.
    {
        std::string db = (dir / "order.db").string();
        {
            Store s(db);
            auto add = [&](const std::string& id, char p, int64_t t) {
                Message m;
                m.id = id;
                m.direction = "out";
                m.src = "A";
                m.dst = "B";
                m.prec = p;
                m.body = id;
                m.state = "queued";
                m.created_at = m.updated_at = t;
                s.insert(m);
            };
            add("A-1", 'R', 100);
            add("A-2", 'P', 200);
            add("A-3", 'Z', 300);
            add("A-4", 'R', 50);
            auto n = s.next_to_send(1000);
            CHECK(n && n->id == "A-3");
            s.mark_sending("A-3");
            n = s.next_to_send(1000);
            CHECK(n && n->id == "A-2");
            s.set_state("A-2", "sent");
            n = s.next_to_send(1000);
            CHECK(n && n->id == "A-4");
            s.schedule_retry("A-4", 2000, "test");
            n = s.next_to_send(1000);
            CHECK(n && n->id == "A-1");
            CHECK(s.next_seq() == 1);
            CHECK(s.next_seq() == 2);
        }
        Store s(db);  // reopen, as after a power cut
        CHECK(s.requeue_interrupted() == 1);
        auto n = s.next_to_send(1000);
        CHECK(n && n->id == "A-3");
        CHECK(s.next_seq() == 3);
    }

    // Two cores, back to back.
    {
        auto ma = std::make_unique<LoopbackModem>(0);
        auto mb = std::make_unique<LoopbackModem>(0);
        LoopbackModem* pa = ma.get();
        LoopbackModem::connect(*ma, *mb);
        Core a(on_mars_link(CoreConfig{"A", (dir / "a.db").string(), 3, 1, true}), std::move(ma));
        a.set_services(test_services());
        Core b(on_mars_link(CoreConfig{"B", (dir / "b.db").string(), 3, 1, true}), std::move(mb));
        b.set_services(test_services());
        a.start();
        b.start();

        std::string err;
        CHECK(a.submit("A", 'R', "to self", &err, SendOpts::in("mars")).empty());
        CHECK(a.submit("B", 'X', "bad prec", &err, SendOpts::in("mars")).empty());
        CHECK(a.submit("B", 'R', "", &err, SendOpts::in("mars")).empty());
        CHECK(a.submit("b", 'R', "lower case", &err, SendOpts::in("mars")).empty());

        // Out and back: A sends, B receives and acks, A sees the ack.
        std::string id = a.submit("B", 'R', "HELLO B THIS IS A", &err, SendOpts::in("mars"));
        CHECK(!id.empty());
        CHECK(wait_for([&] { return state_of(a, id) == "acked"; }));
        auto rx = b.get(id);
        CHECK(rx && rx->direction == "in" && rx->body == "HELLO B THIS IS A" && rx->src == "A");

        // And the other way.
        std::string back = b.submit("A", 'P', "ROGER A", &err, SendOpts::in("mars"));
        CHECK(wait_for([&] { return state_of(b, back) == "acked"; }));
        CHECK(a.get(back) && a.get(back)->body == "ROGER A");

        // A modem failure is retried, then the message still goes.
        pa->fail_next_sends(1);
        std::string retry = a.submit("B", 'R', "AFTER ONE FAILURE", &err, SendOpts::in("mars"));
        CHECK(wait_for([&] { return state_of(a, retry) == "acked"; }, 8000));
        CHECK(a.get(retry) && a.get(retry)->attempts == 2);

        // Too many failures marks it failed.
        pa->fail_next_sends(10);
        std::string dead = a.submit("B", 'R', "NEVER ARRIVES", &err, SendOpts::in("mars"));
        CHECK(wait_for([&] { return state_of(a, dead) == "failed"; }, 15000));
        pa->fail_next_sends(0);

        // A damaged frame is dropped; nothing is stored at B, A stays "sent".
        pa->corrupt_next_deliveries(1);
        std::string hit = a.submit("B", 'R', "DAMAGED ON THE WAY", &err, SendOpts::in("mars"));
        CHECK(wait_for([&] { return state_of(a, hit) == "sent"; }));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(!b.get(hit));
        CHECK(b.status().frames_dropped >= 1);

        // FLASH jumps the queue: queue two ROUTINE and one FLASH while the
        // modem is held, then check the FLASH reached B first.
        pa->hold(true);
        std::string r1 = a.submit("B", 'R', "ROUTINE 1", &err, SendOpts::in("mars"));
        std::string r2 = a.submit("B", 'R', "ROUTINE 2", &err, SendOpts::in("mars"));
        std::string z = a.submit("B", 'Z', "FLASH", &err, SendOpts::in("mars"));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(state_of(a, z) == "queued");
        pa->hold(false);
        CHECK(wait_for([&] {
            return state_of(a, r1) == "acked" && state_of(a, r2) == "acked" && state_of(a, z) == "acked";
        }));
        std::vector<std::string> order;  // B's store lists newest first
        for (auto& m : b.list(3)) order.push_back(m.id);
        CHECK(order.size() == 3 && order[0] == r2 && order[1] == r1 && order[2] == z);

        // Local API.
        std::string s = ApiServer::handle(a, R"({"cmd":"status"})");
        CHECK(s.find("\"station\":\"A\"") != std::string::npos);
        std::string sent = ApiServer::handle(a, R"({"cmd":"send","service":"mars","to":"B","prec":"O","body":"VIA API"})");
        CHECK(sent.find("\"ok\":true") != std::string::npos);
        CHECK(ApiServer::handle(a, R"({"cmd":"nope"})").find("\"ok\":false") != std::string::npos);
        CHECK(ApiServer::handle(a, R"({"cmd":"send","service":"mars","to":"B","prec":"Q","body":"x"})").find("\"ok\":false") !=
              std::string::npos);
        CHECK(ApiServer::handle(a, R"({"cmd":"list","limit":3})").find("\"messages\":[") != std::string::npos);

        a.stop();
        b.stop();
    }

    fs::remove_all(dir);
    return check::report("test_core");
}
