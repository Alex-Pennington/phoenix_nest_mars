// Reading real MARS traffic: the 43 scrubbed samples in tests/fixtures/
// mars-samples (see the README there), the ACP-126 call-up form drawn from
// them, and what the reader refuses.
#include "../src/acp126.h"
#include "../src/acp127.h"
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/received.h"
#include "check.h"
#include "services_fixture.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static const fs::path kFixtures = fs::path(PNA_SOURCE_DIR) / "tests" / "fixtures" / "mars-samples";

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool warned(const received::Message& m, const std::string& what) {
    for (auto& w : m.warnings)
        if (w.find(what) != std::string::npos) return true;
    return false;
}

static std::string field(const std::string& reply, const std::string& key) {
    JsonValue v;
    if (!parse_json(reply, v) || !v.get(key)) return "";
    return v.get(key)->s;
}

static bool wait_for(const std::function<bool()>& cond, int ms = 5000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}

struct Expect {
    const char* file;  // name prefix, "01_"
    received::Form form;
    char prec;         // '-' for none
    const char* originator;
    const char* serial;
    const char* time_filed;
    size_t routing, to, info;
    const char* class_line;
    bool dtg;
};

int main() {
    const auto A127 = received::Form::Acp127, FOLD = received::Form::Acp127Folded, A126 = received::Form::Acp126;
    // What each sample's header says. Taken from the reader's first pass and
    // checked against each file's header by eye; a change here is a change in reading.
    const std::vector<Expect> expect = {
        {"01_", FOLD, 'O', "UHPSFFJ", "0044", "0492019", 1, 0, 0, "UNCLAS", 0},
        {"02_", FOLD, 'O', "AAR4HR", "0062", "0492122", 1, 0, 0, "UNCLAS", 0},
        {"03_", FOLD, 'O', "", "0547", "0492144", 1, 0, 0, "UNCLAS", 0},
        {"04_", FOLD, '-', "AAR4BZ", "0088", "0491951", 1, 0, 0, "", 0},
        {"05_", FOLD, 'O', "AAR4MW", "0003", "0492241", 1, 1, 0, "UNCLAS", 0},
        {"06_", FOLD, 'P', "UHSCTZ", "0038", "3102053", 1, 1, 0, "UNCLAS", 0},
        {"07_", FOLD, 'P', "AAR4BN", "0067", "3072025", 1, 1, 0, "UNCLAS", 0},
        {"08_", FOLD, 'P', "UHSCTZ", "0401", "3122049", 1, 1, 0, "UNCLAS", 0},
        {"09_", FOLD, 'P', "UHFGPSB", "0037", "3091249", 1, 1, 0, "UNCLAS", 0},
        {"10_", A127, 'P', "UHBQKL", "0212", "3111401", 1, 1, 0, "UNCLAS SVC", 1},
        {"11_", A127, 'P', "UHBQKL", "0187", "3101143", 1, 1, 0, "UNCLAS", 1},
        {"12_", A127, 'P', "UHZNFCJ", "0402", "3090001", 1, 1, 0, "UNCLAS", 1},
        {"13_", A127, 'P', "UHBQKL", "0087", "2700517", 1, 1, 0, "UNCLAS", 1},
        {"14_", FOLD, 'P', "AAR4VT", "0012", "2731725", 1, 0, 0, "UNCLAS", 0},
        {"15_", FOLD, 'P', "AAR4ZC", "0021", "2741650", 1, 1, 0, "UNCLAS", 0},
        {"18_", FOLD, 'R', "AAR4XR", "0007", "1571747", 1, 0, 0, "UNCLAS", 0},
        {"19_", A127, 'R', "UHSWSLC", "0030", "1761527", 2, 1, 1, "UNCLAS", 1},
        {"20_", FOLD, 'R', "AAR4XR", "0026", "3510146", 2, 1, 1, "UNCLAS", 0},
        {"21_", FOLD, 'R', "AAA4C2", "0099", "2991219", 1, 1, 0, "UNCLAS", 0},
        {"22_", FOLD, 'R', "AAR4XR", "0024", "3191148", 2, 0, 0, "UNCLAS", 0},
        {"23_", FOLD, 'R', "UHSWSLC", "0831", "3141932", 6, 1, 4, "UNCLAS", 0},
        {"24_", FOLD, 'R', "UHSWSLC", "0823", "2631947", 2, 1, 0, "UNCLAS", 0},
        {"25_", A127, 'R', "UHWGHZB", "0129", "0381959", 1, 1, 1, "UNCLAS", 1},
        {"26_", FOLD, 'R', "AAA4C2", "0106", "3212041", 1, 1, 0, "UNCLAS", 0},
        {"27_", FOLD, 'R', "UHSCTZ", "0025", "3051616", 1, 1, 0, "UNCLAS", 0},
        {"29_", A127, 'R', "UHZNFCJ", "0415", "3111640", 1, 2, 0, "UNCLAS", 1},
        {"30_", FOLD, 'R', "UHWMSTL", "0009", "0090108", 6, 2, 4, "UNCLAS", 0},
        {"31_", A127, 'R', "UHBQKL", "0213", "3111947", 1, 1, 0, "UNCLAS", 1},
        {"32_", FOLD, 'R', "AAA4C2", "0018", "0261451", 1, 1, 0, "UNCLAS", 0},
        {"33_", FOLD, 'R', "AAR4XS", "0276", "0380623", 1, 1, 0, "UNCLAS", 0},
        {"34_", FOLD, 'R', "AAA4X9", "0402", "0402045", 1, 1, 0, "UNCLAS", 0},
        {"35_", FOLD, 'R', "UHSWSLC", "0836", "0421830", 4, 1, 3, "UNCLAS", 0},
        {"36_", A127, 'R', "UHLLGR", "0208", "2431609", 1, 1, 0, "UNCLAS", 1},
        {"37_", A127, 'R', "UHKWCQM", "0007", "0081620", 3, 3, 0, "UNCLAS", 1},
        {"38_", A127, 'R', "UHKWCQM", "0009", "0081656", 3, 3, 0, "UNCLAS", 1},
        {"39_", FOLD, 'R', "AAD4XR", "0311", "0081229", 1, 0, 0, "UNCLAS", 0},
        {"40_", FOLD, 'R', "AAD4FZ", "0703", "0380029", 1, 0, 0, "UNCLAS", 0},
        {"41_", FOLD, 'R', "AAR4KZ", "0341", "0391152", 3, 1, 2, "UNCLAS", 0},
        {"42_", A127, 'R', "UHFCWMD", "0116", "0422056", 3, 3, 0, "UNCLAS", 1},
        {"43_", A126, 'R', "AAR4WS", "051", "", 1, 1, 0, "UNCLAS", 1},
        {"44_", A126, '-', "AAR4KW", "175", "", 1, 0, 0, "", 0},
        {"45_", A126, '-', "", "001", "", 1, 0, 0, "", 0},
        // 46_NOPREC_GARBLED.txt: format line 1: no line starts with VZCZC
    };

    // Every fixture is read, and reads as the table says.
    std::map<std::string, fs::path> files;
    for (auto& e : fs::directory_iterator(kFixtures))
        if (e.path().extension() == ".txt") files[e.path().filename().string().substr(0, 3)] = e.path();
    CHECK(files.size() == 43);
    CHECK(!files.count("16_") && !files.count("17_") && !files.count("28_"));
    std::set<std::string> seen;
    for (const Expect& x : expect) {
        CHECK(files.count(x.file));
        if (!files.count(x.file)) continue;
        seen.insert(x.file);
        received::Message m;
        std::string err;
        bool ok = received::parse(slurp(files[x.file]), m, &err);
        CHECK(ok);
        if (!ok) {
            std::fprintf(stderr, "  %s: %s\n", x.file, err.c_str());
            continue;
        }
        bool same = m.form == x.form && (m.prec ? m.prec : '-') == x.prec && m.originator == x.originator &&
                    m.serial == x.serial && m.time_filed == x.time_filed && m.routing.size() == x.routing &&
                    m.to.size() == x.to && m.info.size() == x.info && m.class_line == x.class_line &&
                    m.has_dtg == x.dtg && !m.text.empty();
        CHECK(same);
        if (!same) std::fprintf(stderr, "  %s differs\n", x.file);
        for (auto& l : m.text) CHECK(l.find("PAGE ") != 0 || l.find(" UNCLAS") == std::string::npos);
    }
    CHECK(seen.size() == 42);  // all but 46, which is too garbled to read

    // The operator mistakes the samples hold are taken and named.
    auto read = [&](const char* f) {
        received::Message m;
        std::string err;
        received::parse(slurp(files[f]), m, &err);
        return m;
    };
    CHECK(warned(read("03_"), "no originator") && warned(read("03_"), "indented"));
    // 04 is the 16L SIMPLE template, which sends no prosign by design.
    CHECK(!warned(read("04_"), "no precedence") && received::procedure(read("04_")) == "16L_P2P_SIMPLE");
    CHECK(warned(read("04_"), "no UNCLAS") && warned(read("04_"), "before VZCZC") && warned(read("04_"), "after NNNN"));
    CHECK(warned(read("10_"), "ZNY EEEEE") && read("10_").serial_again == "0212");
    CHECK(read("12_").zek && read("29_").zek);
    CHECK(warned(read("23_"), "time filed on a line of its own") && read("23_").routing[2] == "UHDJRKW-OP");
    CHECK(read("23_").info.size() == 4 && read("23_").info[1].ri == "UHDJRKW-OP");
    {
        auto m = read("43_");
        CHECK(m.routing.size() == 1 && m.routing[0] == "IRONCRANE" && m.to.size() == 1 && m.to[0].ri == "UHRRRWN");
        CHECK(m.to[0].plain == "IRONCRANE" && warned(m, "lower case"));
    }
    CHECK(read("36_").from.ri.empty() && read("36_").from.plain == "2nd AFMARS COMM WNG WC");
    CHECK(read("44_").routing[0] == "NET" && read("44_").text.size() == 17);
    CHECK(read("45_").routing[0] == "ALL" && read("45_").originator.empty() && read("45_").serial == "001");
    {
        auto m = read("25_");  // six pages; page headers dropped
        CHECK(m.text.size() > 100);
        for (auto& l : m.text) CHECK(l.rfind("PAGE ", 0) != 0);
    }
    CHECK(read("10_").class_line == "UNCLAS SVC");
    CHECK(read("01_").digest.size() == 32);
    {
        received::Message m;
        std::string err;
        CHECK(!received::parse(slurp(files["46_"]), m, &err) && !err.empty());
    }

    // What the reader refuses.
    {
        received::Message m;
        std::string err;
        std::string ok = "VZCZC\r\nRR AAR4XX DE AAR4YY #0001 0010101\r\nBT\r\nUNCLAS\r\nTEXT\r\nBT\r\nNNNN\r\n";
        CHECK(received::parse(ok, m, &err));
        auto swap = [&](const std::string& from, const std::string& to) {
            std::string s = ok;
            s.replace(s.find(from), from.size(), to);
            return s;
        };
        CHECK(!received::parse(swap("UNCLAS", "CONFIDENTIAL"), m, &err));
        CHECK(!received::parse(swap("0010101\r\nBT", "0010101\r\nZNY CCCCC\r\nBT"), m, &err));
        // No NNNN: taken with a warning after a closing BT, refused when cut off before it.
        CHECK(received::parse(swap("NNNN", "NNN"), m, &err) && warned(m, "no NNNN"));
        CHECK(!received::parse(swap("TEXT\r\nBT\r\nNNNN", "TEXT\r\n"), m, &err) &&
              err.find("format line 16") != std::string::npos);
        CHECK(!received::parse(swap(" DE ", " XX "), m, &err) && err.find("format line 3") != std::string::npos);
        CHECK(!received::parse(swap("VZCZC", "VZC"), m, &err));
        CHECK(!received::parse(swap("TEXT\r\nBT\r\n", "TEXT\r\n"), m, &err));
        CHECK(!received::parse("", m, &err));
        // Station Mapper saves the ALE link ahead of VZCZC on the same line.
        CHECK(received::parse("STA-A.ALL." + ok, m, &err) && warned(m, "ALE link prefix"));
    }

    // The appliance's own ACP 127 messages read back the same way.
    {
        acp127::Message m;
        m.channel = "QXA";
        m.prec = 'O';
        m.routing = {"KAPPAMAIN"};
        m.originator_ri = "AAR4XX";
        m.mars_fl3 = true;
        m.station_serial = "7";
        m.filing_yday = 49, m.filing_hour = 20, m.filing_minute = 19;
        m.dtg = acp127::Dtg{18, 20, 19, 'Z', 2, 2026};
        m.from = {"", "TEST STATION ONE"};
        m.to = {{"UHXXXX", "TEST STATION TWO"}};
        m.text = "MARSSTATUS/182019/TEST/TEST/KY/AAR4XX/FULLYOPERATIONAL/Y/Y/Y/12//";
        std::string err, out = acp127::build(m, &err);
        CHECK(!out.empty());
        received::Message r;
        CHECK(received::parse(out, r, &err) && r.form == received::Form::Acp127 && r.prec == 'O');
        CHECK(r.originator == "AAR4XX" && r.serial == "0007" && r.time_filed == "0492019" && r.has_dtg);
        CHECK(r.security == "ZNR UUUUU" && r.class_line == "UNCLAS" && r.text.size() == 1 && r.text[0] == m.text);
        CHECK(r.warnings.empty());
        for (auto& w : r.warnings) std::fprintf(stderr, "  warning: %s\n", w.c_str());
    }

    // ACP-126 call-up form, built and read back (shape of sample 43).
    {
        acp126::Message m;
        m.channel = "QXA";
        m.channel_serial = 12;
        m.calls = {"IRONCRANE"};
        m.station = "AAR4XX";
        m.serial = 51;
        m.procedure = acp126::Message::Procedure::Plaindress;
        m.prec = 'R';
        m.dtg = acp127::Dtg{19, 21, 9, 'Z', 2, 2025};
        m.from = {"", "TEST STATION ONE"};
        m.to = {{"UHXXXX", "IRONCRANE"}};
        m.text = "MSGID/RRI/TEST STATION ONE/51//\nGENTEXT/REQUEST RESPONSE/RRI STRIPS FOLLOW//";
        std::string err, out = acp126::build(m, &err);
        CHECK(!out.empty());
        CHECK(out.rfind("VZCZCQXA012\r\r\nIRONCRANE DE AAR4XX NR 051\r\r\nR 192109Z FEB 2025\r\r\n", 0) == 0);
        CHECK(out.find("BT\r\r\n\n\n\n\n\n\n\nNNNN") != std::string::npos);
        received::Message r;
        CHECK(received::parse(out, r, &err) && r.form == received::Form::Acp126);
        CHECK(r.routing == m.calls && r.originator == "AAR4XX" && r.serial == "051" && r.prec == 'R' && r.has_dtg);
        CHECK(r.to.size() == 1 && r.to[0].ri == "UHXXXX" && r.class_line == "UNCLAS" && r.text.size() == 2);
        CHECK(r.warnings.empty());

        // Net roster shape (sample 44): no DTG, no FM/TO, no UNCLAS line.
        acp126::Message n;
        n.calls = {"NET"};
        n.station = "AAR4XX";
        n.serial = 175;
        n.unclas_line = false;
        n.text = "AAR4XX-NCS\nAAR4YY-1P\nAAR4ZZ";
        out = acp126::build(n, &err);
        CHECK(out.rfind("VZCZC\r\r\nNET DE AAR4XX NR 175\r\r\nBT\r\r\nAAR4XX-NCS\r\r\n", 0) == 0);
        CHECK(received::parse(out, r, &err) && r.form == received::Form::Acp126 && r.text.size() == 3);

        auto refused = [&](std::function<void(acp126::Message&)> change) {
            acp126::Message x = m;
            change(x);
            std::string e;
            return acp126::build(x, &e).empty() && !e.empty();
        };
        CHECK(refused([](acp126::Message& x) { x.calls.clear(); }));
        CHECK(refused([](acp126::Message& x) { x.station = ""; }));
        CHECK(refused([](acp126::Message& x) { x.serial = 1000; }));
        CHECK(refused([](acp126::Message& x) { x.text = "lower"; }));
        CHECK(refused([](acp126::Message& x) { x.text = ""; }));
        CHECK(refused([](acp126::Message& x) { x.channel = "QX"; }));
        CHECK(refused([](acp126::Message& x) { x.prec = 'X'; }));
    }

    // Through the core: an ACP-126 message out and back, then read with parse_traffic.
    {
        fs::path dir = fs::temp_directory_path() / ("pna_rx_" + std::to_string(::getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        {
            auto ma = std::make_unique<LoopbackModem>(0);
            auto mb = std::make_unique<LoopbackModem>(0);
            LoopbackModem::connect(*ma, *mb);
            Core a(on_mars_link(CoreConfig{"A", (dir / "a.db").string(), 3, 1, true}), std::move(ma));
            a.set_services(test_services());
            Core b(on_mars_link(CoreConfig{"B", (dir / "b.db").string(), 3, 1, true}), std::move(mb));
            b.set_services(test_services());
            a.start();
            b.start();
            std::string reply = ApiServer::handle(
                a, R"({"cmd":"send_acp126","service":"mars","to_station":"B","calls":"NET","station":"AAR4XX","unclas":"false",)"
                   R"("text":"AAR4XX-NCS\nAAR4YY"})");
            std::string id = field(reply, "id");
            CHECK(!id.empty() && field(reply, "source") == acp126::kSource);
            CHECK(wait_for([&] { auto m = a.get(id); return m && m->state == "acked"; }));
            std::string r = ApiServer::handle(b, R"({"cmd":"parse_traffic","id":")" + id + R"("})");
            CHECK(field(r, "form") == "acp126" && field(r, "routing") == "NET" && field(r, "serial") == "001");
            CHECK(field(r, "text") == "AAR4XX-NCS\nAAR4YY" && field(r, "warnings") == "no UNCLAS line");
            std::string again = ApiServer::handle(
                a, R"({"cmd":"send_acp126","service":"mars","to_station":"B","calls":"NET","station":"AAR4XX","text":"X"})");
            CHECK(field(again, "message").find("NET DE AAR4XX NR 002") != std::string::npos);
            CHECK(field(r, "procedure") == "ACP126_SIMPLE" && field(again, "procedure") == "ACP126_SIMPLE");
            CHECK(field(ApiServer::handle(a, R"({"cmd":"send_acp126","service":"mars","to_station":"B","calls":"NET","station":"AAR4XX",)"
                                             R"("procedure":"plaindress","from":"TEST ONE","text":"X"})"),
                        "error").find("PLAINDRESS") != std::string::npos);

            // A chat line out and back.
            std::string chat = ApiServer::handle(
                a, R"({"cmd":"send_chat","service":"mars","to_station":"B","call":"KAPPA71-CM","station":"AAR4XX","text":"QRU U,A"})");
            std::string cid = field(chat, "id");
            CHECK(!cid.empty() && field(chat, "message").rfind("KAPPA71-CM DE AAR4XX // QRU U,A // ", 0) == 0);
            CHECK(wait_for([&] { auto m = a.get(cid); return m && m->state == "acked"; }));
            std::string cr = ApiServer::handle(b, R"({"cmd":"parse_traffic","id":")" + cid + R"("})");
            CHECK(field(cr, "form") == "acp201-chat" && field(cr, "kind") == "qru" && field(cr, "from") == "AAR4XX");
            CHECK(field(ApiServer::handle(a, R"({"cmd":"send_acp126","service":"mars","to_station":"B","calls":"","station":"AAR4XX","text":"X"})"),
                        "error").find("called station") != std::string::npos);
            a.stop();
            b.stop();
        }
        fs::remove_all(dir);
    }

    return check::report("test_received");
}
