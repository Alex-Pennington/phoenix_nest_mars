// The MSC templates the traffic samples name, and the chat lines.
//
// Each fixture directory may hold TEMPLATES.csv (file, procedure, form,
// composer), taken from the samples' SOURCES.csv. Where it is there, the
// reader's procedure() must name every message the same way. The batch 2
// fixtures (mars-samples-b2) are kept local and may be missing; then their
// checks are skipped. The chat and ACP-126 builder checks always run.
#include "../src/acp126.h"
#include "../src/acp201.h"
#include "../src/received.h"
#include "check.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace pna;
namespace fs = std::filesystem;

static const fs::path kRoot = fs::path(PNA_SOURCE_DIR) / "tests" / "fixtures";

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool warned(const std::vector<std::string>& ws, const std::string& what) {
    for (auto& w : ws)
        if (w.find(what) != std::string::npos) return true;
    return false;
}

struct Row {
    std::string file, procedure;
};

static std::vector<Row> templates(const fs::path& dir) {
    std::vector<Row> rows;
    std::ifstream f(dir / "TEMPLATES.csv");
    std::string line;
    std::getline(f, line);  // header
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t a = line.find(','), b = line.find(',', a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        rows.push_back({line.substr(0, a), line.substr(a + 1, b - a - 1)});
    }
    return rows;
}

// Files that are refused on purpose: too garbled (batch 1 46), cut off
// before the closing BT (batch 2 17).
static bool refused_on_purpose(const std::string& dir, const std::string& file) {
    return (dir == "mars-samples" && file.rfind("46_", 0) == 0) ||
           (dir == "mars-samples-b2" && file.rfind("17_", 0) == 0);
}

static void check_dir(const std::string& name) {
    fs::path dir = kRoot / name;
    if (!fs::exists(dir / "TEMPLATES.csv")) {
        std::printf("%s/TEMPLATES.csv not here; template checks skipped\n", name.c_str());
        return;
    }
    auto rows = templates(dir);
    CHECK(rows.size() == 43);
    int envelopes = 0, chats = 0, read = 0;
    for (auto& r : rows) {
        std::string raw = slurp(dir / r.file);
        CHECK(!raw.empty());
        if (r.procedure == "ACP201_CHAT") {
            ++chats;
            acp201::Chat c;
            std::string err;
            bool ok = acp201::parse(raw, c, &err) && acp201::looks_like_chat(raw);
            CHECK(ok);
            if (!ok) std::fprintf(stderr, "  %s/%s: %s\n", name.c_str(), r.file.c_str(), err.c_str());
            if (ok) ++read;
            continue;
        }
        ++envelopes;
        received::Message m;
        std::string err;
        bool ok = received::parse(raw, m, &err);
        if (refused_on_purpose(name, r.file)) {
            CHECK(!ok);
            continue;
        }
        CHECK(ok);
        if (!ok) {
            std::fprintf(stderr, "  %s/%s: %s\n", name.c_str(), r.file.c_str(), err.c_str());
            continue;
        }
        ++read;
        bool same = received::procedure(m) == r.procedure;
        CHECK(same);
        if (!same)
            std::fprintf(stderr, "  %s/%s: read as %s, made with %s\n", name.c_str(), r.file.c_str(),
                         received::procedure(m).c_str(), r.procedure.c_str());
    }
    std::printf("%s: %d of %d read (%d envelopes, %d chat lines)\n", name.c_str(), read, envelopes + chats, envelopes,
                chats);
}

static std::map<std::string, fs::path> by_prefix(const fs::path& dir) {
    std::map<std::string, fs::path> out;
    for (auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".txt") out[e.path().filename().string().substr(0, 3)] = e.path();
    return out;
}

static void check_batch2() {
    fs::path dir = kRoot / "mars-samples-b2";
    if (!fs::exists(dir)) return;
    auto files = by_prefix(dir);
    auto msg = [&](const char* f) {
        received::Message m;
        std::string err;
        received::parse(slurp(files[f]), m, &err);
        return m;
    };
    auto chat = [&](const char* f) {
        acp201::Chat c;
        std::string err;
        acp201::parse(slurp(files[f]), c, &err);
        return c;
    };

    // A relay's second RI after DE, and an FM that runs on to the next line.
    {
        auto m = msg("01_");
        CHECK(m.originator == "UHRRRWN" && m.relay.size() == 1 && m.serial == "0718" && m.time_filed == "0492331");
        CHECK(!warned(m.warnings, "serial after BT differs") && !warned(m.warnings, "not placed"));
        CHECK(m.from.plain.find("FORWARD") != std::string::npos && m.to.size() == 1 && m.info.size() == 1);
    }
    // CODRESS: key indicator after the DTG, GR line, cipher groups not read.
    for (const char* f : {"02_", "03_"}) {
        auto m = msg(f);
        CHECK(m.enciphered && !m.key_indicator.empty() && m.group_count > 0 && m.class_line.empty());
        CHECK(m.warnings.empty());
        CHECK(received::procedure(m) == "ACP127_CODRESS");
    }
    CHECK(msg("02_").group_count == 151 && msg("03_").group_count == 234);
    // ZEK ZOB, and page headers after the closing BT.
    CHECK(msg("19_").zek && msg("19_").zek_line == "ZEK ZOB");
    CHECK(!warned(msg("15_").warnings, "not placed") && !warned(msg("19_").warnings, "not placed"));
    for (auto& l : msg("19_").text) CHECK(l.rfind("PAGE ", 0) != 0);
    // No NNNN: 20 ends after the closing BT and is taken; 17 is cut off.
    CHECK(warned(msg("20_").warnings, "no NNNN") && msg("20_").serial_again == "0103");
    {
        received::Message m;
        std::string err;
        CHECK(!received::parse(slurp(files["17_"]), m, &err) && err.find("cut off") != std::string::npos);
    }

    // Chat lines.
    {
        auto c = chat("28_");
        CHECK(c.kind == acp201::Kind::Receipt && c.prec == 'R' && c.receipt_serial == "0025");
        CHECK(c.receipt_time == "3051616" && c.receipt_by.size() == 1 && !c.zulu && c.to == "ALL-CM");
    }
    {
        auto c = chat("29_");
        CHECK(c.kind == acp201::Kind::Receipt && c.prec == 'P' && c.receipt_by.size() == 2);
    }
    CHECK(chat("30_").kind == acp201::Kind::Ver && chat("30_").hour == 23 && chat("30_").minute == 33);
    CHECK(chat("31_").kind == acp201::Kind::QruUA && !chat("31_").comp);
    CHECK(chat("37_").kind == acp201::Kind::QruUA && chat("37_").comp);
    CHECK(chat("32_").kind == acp201::Kind::Zee && chat("32_").number == 1);
    CHECK(chat("33_").kind == acp201::Kind::Qsl && chat("33_").number == 3);
    CHECK(chat("34_").kind == acp201::Kind::TrafficComplete && chat("34_").to == "ALL");
    for (const char* f : {"38_", "39_"})
        CHECK(chat(f).kind == acp201::Kind::SqlQuery && warned(chat(f).warnings, "never run"));
    {
        auto c = chat("36_");  // a MARSSTATUS strip, which itself ends in "//"
        CHECK(c.kind == acp201::Kind::Text && c.text.rfind("MARSSTATUS/", 0) == 0);
        CHECK(c.text.size() > 2 && c.text.substr(c.text.size() - 2) == "//");
    }
    for (const char* f : {"35_", "40_", "41_", "42_", "43_"}) CHECK(chat(f).kind == acp201::Kind::Text);
    for (auto& kv : files) {
        int n = std::stoi(kv.first.substr(0, 2));
        if (n < 28) CHECK(!acp201::looks_like_chat(slurp(kv.second)));
    }
}

static void check_chat_builder() {
    std::string err;
    acp201::Chat c;
    c.to = "KAPPA71-CM";
    c.from = "AAR4XX";
    c.hour = 20, c.minute = 1;
    c.kind = acp201::Kind::QruUA;
    std::string line = acp201::build(c, &err);
    CHECK(line == "KAPPA71-CM DE AAR4XX // QRU U,A // 2001Z K");
    acp201::Chat r;
    CHECK(acp201::parse(line + "\r\n", r, &err) && r.kind == acp201::Kind::QruUA && r.from == "AAR4XX" &&
          r.warnings.empty());

    c.kind = acp201::Kind::Zee;
    c.number = 2;
    CHECK(acp201::build(c, &err) == "KAPPA71-CM DE AAR4XX // ZEE 2 // 2001Z K");

    // Text holding "//" reads back whole.
    c.kind = acp201::Kind::Text;
    c.text = "MARSSTATUS/182300/TEST/TEST/KY/AAR4XX/FULLYOPERATIONAL/Y/Y/Y/3//";
    line = acp201::build(c, &err);
    CHECK(acp201::parse(line, r, &err) && r.text == c.text);

    // The receipt, in MSC's compact spacing.
    acp201::Chat q;
    q.to = "ALL-CM";
    q.from = "AAR4XX-CM";
    q.hour = 23, q.minute = 39;
    q.zulu = false;
    q.kind = acp201::Kind::Receipt;
    q.prec = 'R';
    q.receipt_of = "UHXXXX";
    q.receipt_serial = "25";
    q.receipt_time = "3051616";
    q.receipt_by = {"AAR4XX"};
    line = acp201::build(q, &err);
    CHECK(line == "ALL-CM DE AAR4XX-CM//QSL _R_UHXXXX #0025 3051616/AAR4XX  //2339K");
    CHECK(acp201::parse(line, r, &err) && r.kind == acp201::Kind::Receipt && r.receipt_of == "UHXXXX" &&
          r.receipt_serial == "0025" && r.receipt_by == q.receipt_by);

    // What the chat reader and builder refuse.
    CHECK(!acp201::parse("KAPPA71-CM DE AAR4XX QRU U,A 2001Z K", r, &err));
    CHECK(!acp201::parse("KAPPA71-CM DE AAR4XX // QRU U,A", r, &err));
    CHECK(!acp201::parse("KAPPA71-CM AAR4XX // QRU U,A // 2001Z K", r, &err));
    CHECK(!acp201::parse("KAPPA71-CM DE AAR4XX // QRU U,A // 2501Z K", r, &err));
    CHECK(!acp201::looks_like_chat("VZCZC\r\nRR A DE B // x // 2001Z K\r\n"));
    acp201::Chat x = c;
    x.kind = acp201::Kind::SqlQuery;
    x.text = "SQL SELECT 1";
    CHECK(acp201::build(x, &err).empty() && err.find("SQL") != std::string::npos);
    x = c;
    x.from = "aar4xx";
    CHECK(acp201::build(x, &err).empty());
    x = c;
    x.hour = -1;
    CHECK(acp201::build(x, &err).empty());
    x = c;
    x.text = "two\nlines";
    CHECK(acp201::build(x, &err).empty());
}

static void check_acp126_procedures() {
    acp126::Message m;
    m.calls = {"IRONCRANE"};
    m.station = "AAR4XX";
    m.serial = 51;
    m.text = "TEST";
    std::string err;
    received::Message r;

    CHECK(received::parse(acp126::build(m, &err), r, &err) && received::procedure(r) == "ACP126_SIMPLE");
    acp126::Message s = m;
    s.from = {"", "TEST STATION ONE"};
    CHECK(acp126::build(s, &err).empty() && err.find("SIMPLE") != std::string::npos);

    acp126::Message p = m;
    p.procedure = acp126::Message::Procedure::Plaindress;
    p.dtg = acp127::Dtg{19, 21, 9, 'Z', 2, 2025};
    p.from = {"", "TEST STATION ONE"};
    CHECK(acp126::build(p, &err).empty() && err.find("PLAINDRESS") != std::string::npos);  // no TO
    p.to = {{"UHXXXX", "IRONCRANE"}};
    CHECK(received::parse(acp126::build(p, &err), r, &err) && received::procedure(r) == "ACP126_PLAINDRESS");

    acp126::Message::Procedure proc;
    CHECK(acp126::procedure_from("plaindress", proc) && proc == acp126::Message::Procedure::Plaindress);
    CHECK(acp126::procedure_from("SIMPLE", proc) && proc == acp126::Message::Procedure::Simple);
    CHECK(!acp126::procedure_from("codress", proc));
}

int main() {
    check_dir("mars-samples");
    check_dir("mars-samples-b2");
    check_batch2();
    check_chat_builder();
    check_acp126_procedures();
    return check::report("test_templates");
}
