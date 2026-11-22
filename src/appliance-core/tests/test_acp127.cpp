// The message engine's ACP 127(G) format, its serial counters, and the
// milestone 3 check: an IMMEDIATE ACP 127 message goes ahead of ROUTINE
// traffic and arrives with correct format lines. Every station, routing
// indicator and text here is made up, except the 214.a worked example,
// which is ACP 127(G)'s own.
#include "../src/acp127.h"
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/store.h"
#include "check.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <sstream>
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

static std::vector<std::string> lines_of(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static std::string field(const std::string& reply, const std::string& key) {
    JsonValue v;
    if (!parse_json(reply, v) || !v.get(key)) return "";
    return v.get(key)->s;
}

static acp127::Message sample() {
    acp127::Message m;
    m.automatic = true;
    m.channel = "QXA";
    m.channel_serial = 7;
    m.prec = 'P';
    m.routing = {"RXAAAA", "RXBBBB"};
    m.originator_ri = "RXCCCC";
    m.station_serial = "42B";
    m.filing_day = 22, m.filing_hour = 14, m.filing_minute = 5;
    m.transmission_instructions = "T RXDDDD";
    m.dtg = acp127::Dtg{22, 14, 3, 'Z', 11, 2026};
    m.from = {"RXCCCC", "TEST STATION ONE"};
    m.to = {{"RXAAAA", "TEST STATION TWO"}, {"", "TEST STATION THREE"}};
    m.info = {{"RXBBBB", "TEST STATION FOUR"}};
    m.xmt = {{"", "TEST STATION FIVE"}};
    m.line10 = "GR 12";
    m.text = "THIS IS A MADE UP TEST MESSAGE.\nSECOND LINE: (ONE, TWO/THREE) - FOUR?";
    m.correction = "C LINE 12 WA TWO";
    return m;
}

int main() {
    std::string err;

    // ACP 127(G) 214.a, the abbreviated manual example, line for line.
    {
        acp127::Message m;
        m.automatic = false;
        m.channel = "PCB";
        m.channel_serial = 111;
        m.prec = 'R';
        m.routing = {"RCWNDB"};
        m.originator_ri = "RCCIC";
        m.station_serial = "134";
        m.filing_day = 2, m.filing_hour = 0, m.filing_minute = 9;
        m.security_line = false;
        m.dtg = acp127::Dtg{1, 23, 45, 'Z', 4, 0};
        m.from = {"", "CANAVHED"};
        m.to = {{"", "NAVAL RESERVE DIV WINNIPEG"}};
        m.text =
            "FOR SHIPPING DEPARTMENT FROM\nPROCUREMENT LIAISON SECTION YOUR\n291318Z MAR PD ADVISE WHEN MATERIAL\n"
            "LISTED MY 160322Z MAR WILL BE READY\nFOR SHIPMENT";
        std::string out = acp127::build(m, &err);
        CHECK(err.empty());
        std::string want =
            "VV   PCB111     UU\r\r\n"
            "RR RCWNDB\r\r\n"
            "DE RCCIC 134 02/0009Z\r\r\n"
            "R 012345Z APR\r\r\n"
            "FM CANAVHED\r\r\n"
            "TO NAVAL RESERVE DIV WINNIPEG\r\r\n"
            "BT\r\r\n"
            "UNCLAS FOR SHIPPING DEPARTMENT FROM\r\r\n"
            "PROCUREMENT LIAISON SECTION YOUR\r\r\n"
            "291318Z MAR PD ADVISE WHEN MATERIAL\r\r\n"
            "LISTED MY 160322Z MAR WILL BE READY\r\r\n"
            "FOR SHIPMENT\r\r\n"
            "BT\r\r\n\n\n\n\n\n\n\n"
            "NNNN";
        CHECK(out == want);
        acp127::Message p;
        CHECK(acp127::parse(out, p, &err));
        CHECK(!p.automatic && p.channel == "PCB" && p.channel_serial == 111 && p.prec == 'R');
        CHECK(p.originator_ri == "RCCIC" && p.station_serial == "134" && !p.security_line);
        CHECK(p.from.plain == "CANAVHED" && p.to.size() == 1 && p.dtg == m.dtg);
        CHECK(acp127::build(p, &err) == out);
    }

    // Every format line, built and parsed back.
    {
        acp127::Message m = sample();
        std::string out = acp127::build(m, &err);
        CHECK(!out.empty());
        auto L = lines_of(out);
        CHECK(L.size() == 17);
        CHECK(L[0] == "VZCZCQXA007     UU");
        CHECK(L[1] == "PP RXAAAA RXBBBB");
        CHECK(L[2] == "DE RXCCCC 42B 22/1405Z");
        CHECK(L[3] == "ZNR UUUUU T RXDDDD");
        CHECK(L[4] == "P 221403Z NOV 2026");
        CHECK(L[5] == "FM RXCCCC/TEST STATION ONE");
        CHECK(L[6] == "TO RXAAAA/TEST STATION TWO");
        CHECK(L[7] == "TEST STATION THREE");
        CHECK(L[8] == "INFO RXBBBB/TEST STATION FOUR");
        CHECK(L[9] == "XMT TEST STATION FIVE");
        CHECK(L[10] == "GR 12");
        CHECK(L[11] == "BT");
        CHECK(L[12] == "UNCLAS THIS IS A MADE UP TEST MESSAGE.");
        CHECK(L[14] == "BT");
        CHECK(L[15] == "C LINE 12 WA TWO");
        CHECK(L[16] == "NNNN");
        CHECK(out.find("BT\r\r\n\n\n\n\n\n\n\nC LINE") == std::string::npos);  // 8 LF only before NNNN
        CHECK(out.find("TWO\r\r\n\n\n\n\n\n\n\nNNNN") != std::string::npos);
        acp127::Message p;
        CHECK(acp127::parse(out, p, &err));
        CHECK(p.routing == m.routing && p.station_serial == "42B" && p.transmission_instructions == "T RXDDDD");
        CHECK(p.dtg == m.dtg && p.from == m.from && p.to == m.to && p.info == m.info && p.xmt == m.xmt);
        CHECK(p.line10 == "GR 12" && p.text == m.text && p.correction == m.correction);
        CHECK(acp127::build(p, &err) == out);
        // Other line ends and a bell signal are read the same way.
        std::string lf;
        for (char c : out)
            if (c != '\r') lf += c;
        acp127::Message q;
        CHECK(acp127::parse("\a" + lf, q, &err) && acp127::build(q, &err) == out);
    }

    // MARS routing indicators and the MARS form of format line 3 (samples 02, 23, 26, 10).
    {
        acp127::Message m = sample();
        m.routing = {"KAPPAMAIN", "AAR4MW", "B7Q", "UHDJRKW-OP"};
        m.mars_fl3 = true;
        m.station_serial = "38";
        m.filing_yday = 310, m.filing_hour = 20, m.filing_minute = 53;
        std::string out = acp127::build(m, &err);
        CHECK(!out.empty());
        auto L = lines_of(out);
        CHECK(L.size() > 2 && L[1] == "PP KAPPAMAIN AAR4MW B7Q UHDJRKW-OP" && L[2] == "DE RXCCCC #0038 3102053");
        acp127::Message p;
        CHECK(acp127::parse(out, p, &err) && p.mars_fl3 && p.station_serial == "0038" && p.filing_yday == 310);
        CHECK(p.routing == m.routing && acp127::build(p, &err) == out);
        m.station_serial = "12345";
        CHECK(acp127::build(m, &err).empty());
    }

    // Lines are wrapped to 69 characters at spaces.
    {
        acp127::Message m = sample();
        m.text.clear();
        for (int i = 0; i < 40; ++i) m.text += "WORD" + std::to_string(i) + " ";
        std::string out = acp127::build(m, &err);
        CHECK(!out.empty());
        size_t longest = 0;
        for (auto& l : lines_of(out)) longest = std::max(longest, l.size());
        CHECK(longest <= acp127::kMaxLine && longest > 60);
        acp127::Message p;
        CHECK(acp127::parse(out, p, &err) && p.text.find("WORD39") != std::string::npos);
    }

    // What the builder refuses.
    {
        auto refused = [&](std::function<void(acp127::Message&)> change) {
            acp127::Message m = sample();
            change(m);
            std::string e;
            return acp127::build(m, &e).empty() && !e.empty();
        };
        CHECK(refused([](acp127::Message& m) { m.classification = 'C'; }));
        CHECK(refused([](acp127::Message& m) { m.prec = 'X'; }));
        CHECK(refused([](acp127::Message& m) { m.text = "lower case"; }));
        CHECK(refused([](acp127::Message& m) { m.text = "AT @ SIGN"; }));
        CHECK(refused([](acp127::Message& m) { m.text = ""; }));
        CHECK(refused([](acp127::Message& m) { m.channel = "QX"; }));
        CHECK(refused([](acp127::Message& m) { m.channel_serial = 1000; }));
        CHECK(refused([](acp127::Message& m) { m.routing = {"RX"}; }));
        CHECK(refused([](acp127::Message& m) { m.routing.clear(); }));
        CHECK(refused([](acp127::Message& m) { m.routing = {"-RXA"}; }));
        CHECK(refused([](acp127::Message& m) { m.routing = {"4444"}; }));
        CHECK(refused([](acp127::Message& m) { m.routing = {"rxaaaa"}; }));
        CHECK(refused([](acp127::Message& m) { m.to.clear(); }));
        CHECK(refused([](acp127::Message& m) { m.station_serial = "4A2"; }));
        CHECK(refused([](acp127::Message& m) { m.dtg.month = 13; }));
        CHECK(refused([](acp127::Message& m) { m.dtg.zone = 'J'; }));
        CHECK(refused([](acp127::Message& m) { m.to[0].plain = std::string(70, 'A'); }));
        CHECK(refused([](acp127::Message& m) { m.text = std::string(70, 'A'); }));
        CHECK(refused([](acp127::Message& m) { m.correction = "LINE 12"; }));
    }

    // What the parser refuses, naming the format line.
    {
        std::string good = acp127::build(sample(), &err);
        acp127::Message p;
        auto bad = [&](const std::string& from, const std::string& to, const std::string& expect) {
            std::string s = good;
            size_t at = s.find(from);
            if (at == std::string::npos) return false;
            s.replace(at, from.size(), to);
            std::string e;
            return !acp127::parse(s, p, &e) && e.find(expect) != std::string::npos;
        };
        CHECK(bad("NNNN", "", "format line 16"));
        CHECK(bad("PP RXAAAA", "PX RXAAAA", "format line 2"));
        CHECK(bad("P 221403Z", "R 221403Z", "format line 5"));
        CHECK(bad("NOV 2026", "NOB 2026", "format line 5"));
        CHECK(bad("UNCLAS THIS", "THIS", "format line 12"));
        CHECK(bad("ZNR UUUUU", "ZNR CCCCC", "format line 4"));
        CHECK(bad("DE RXCCCC", "DX RXCCCC", "format line 3"));
        CHECK(!acp127::parse("", p, &err));
        CHECK(!acp127::parse(good + "\r\r\nMORE", p, &err));
    }

    fs::path dir = fs::temp_directory_path() / ("pna_acp_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Serial counters run 1..wrap and then start again, per key, across restarts.
    {
        {
            Store s((dir / "count.db").string());
            CHECK(s.next_counter("x", 3) == 1 && s.next_counter("x", 3) == 2 && s.next_counter("y", 3) == 1);
        }
        Store s((dir / "count.db").string());
        CHECK(s.next_counter("x", 3) == 3 && s.next_counter("x", 3) == 1 && s.next_seq() == 1);
    }

    // Milestone 3: ROUTINE ACP 127 traffic is queued while the modem is held,
    // then one IMMEDIATE; it reaches B first, with correct format lines.
    {
        auto ma = std::make_unique<LoopbackModem>(0);
        auto mb = std::make_unique<LoopbackModem>(0);
        LoopbackModem* pa = ma.get();
        LoopbackModem::connect(*ma, *mb);
        Core a(CoreConfig{"A", (dir / "a.db").string(), 3, 1, true}, std::move(ma));
        Core b(CoreConfig{"B", (dir / "b.db").string(), 3, 1, true}, std::move(mb));
        a.start();
        b.start();

        auto send = [&](char prec, const std::string& text) {
            std::string req = std::string(R"({"cmd":"send_acp127","to_station":"B","prec":")") + prec +
                              R"(","channel":"QXA","routing":"RXAAAA","originator_ri":"RXCCCC",)"
                              R"("from":"RXCCCC/TEST STATION ONE","to":"RXAAAA/TEST STATION TWO\nTEST STATION THREE",)"
                              R"("text":")" + text + R"("})";
            return ApiServer::handle(a, req);
        };
        pa->hold(true);
        std::string r1 = field(send('R', "ROUTINE ONE"), "id");
        std::string r2 = field(send('R', "ROUTINE TWO"), "id");
        std::string imm_reply = send('O', "IMMEDIATE ONE");
        std::string o = field(imm_reply, "id");
        CHECK(!r1.empty() && !r2.empty() && !o.empty());
        CHECK(field(imm_reply, "edition") == acp127::kEdition);
        CHECK(field(send('R', "lower case"), "error").find("format line 12") != std::string::npos);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(a.get(o) && a.get(o)->state == "queued");
        pa->hold(false);
        CHECK(wait_for([&] {
            for (auto& id : {r1, r2, o}) {
                auto m = a.get(id);
                if (!m || m->state != "acked") return false;
            }
            return true;
        }));
        std::vector<std::string> order;  // B's store lists newest first
        for (auto& m : b.list(3)) order.push_back(m.id);
        CHECK(order.size() == 3 && order[2] == o && order[1] == r1 && order[0] == r2);

        auto rx = b.get(o);
        CHECK(rx && rx->prec == 'O');
        auto L = lines_of(rx ? rx->body : "");
        CHECK(L.size() >= 12);
        if (L.size() >= 12) {
            CHECK(L[0] == "VZCZCQXA003     UU");  // the third message on channel QXA
            CHECK(L[1] == "OO RXAAAA");
            CHECK(L[2].rfind("DE RXCCCC #0003 ", 0) == 0 && L[2].size() == 23);  // MARS form, Julian time filed
            CHECK(L[3] == "ZNR UUUUU");
            std::istringstream fl5(L[4]);
            std::string p5, dtg, mon, year;
            fl5 >> p5 >> dtg >> mon >> year;
            CHECK(p5 == "O" && dtg.size() == 7 && dtg.back() == 'Z' && mon.size() == 3 && year.size() == 4);
            CHECK(L[5] == "FM RXCCCC/TEST STATION ONE");
            CHECK(L[6] == "TO RXAAAA/TEST STATION TWO" && L[7] == "TEST STATION THREE");
            CHECK(L[8] == "BT" && L[9] == "UNCLAS IMMEDIATE ONE" && L[10] == "BT" && L.back() == "NNNN");
        }
        std::string parsed = ApiServer::handle(b, R"({"cmd":"parse_acp127","id":")" + o + R"("})");
        CHECK(field(parsed, "precedence") == "IMMEDIATE" && field(parsed, "text") == "IMMEDIATE ONE");
        CHECK(field(parsed, "to") == "RXAAAA/TEST STATION TWO\nTEST STATION THREE");
        // A plain message is not taken for ACP 127.
        std::string plain = a.submit("B", 'R', "NOT ACP 127", &err);
        CHECK(wait_for([&] { return b.get(plain).has_value(); }));
        CHECK(field(ApiServer::handle(b, R"({"cmd":"parse_acp127","id":")" + plain + R"("})"), "error")
                  .find("not an ACP 127 message") != std::string::npos);
        a.stop();
        b.stop();
    }

    fs::remove_all(dir);
    return check::report("test_acp127");
}
