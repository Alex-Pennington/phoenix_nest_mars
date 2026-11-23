// MARS station files into the station's own model: a member roster, an
// address book (as an .xlsx built here), a frequency card and a net schedule.
// Everything is made up. The frequencies are public time-signal ones (2.5, 5
// and 10 MHz), standing in for a channel plan; no MARS channel appears here.
#include "../src/api_server.h"
#include "../src/channels.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/mars_import.h"
#include "../src/ops.h"
#include "../src/table.h"
#include "check.h"

#include <zlib.h>

#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

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
static JsonValue call(Core& c, std::initializer_list<std::pair<std::string, std::string>> kv, std::string* raw = nullptr) {
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
    JsonValue out;
    parse_json(reply, out);
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
static std::vector<std::string> strs(const JsonValue& v, const char* k) {
    std::vector<std::string> out;
    const JsonValue* a = v.get(k);
    if (a && a->is_array())
        for (auto& e : a->arr) out.push_back(e.s);
    return out;
}
static bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
static const JsonValue& sheet0(const JsonValue& r) {
    static JsonValue none;
    const JsonValue* a = r.get("sheets");
    return a && !a->arr.empty() ? a->arr[0] : none;
}

// ---- a minimal .xlsx writer for the test: one sheet, inline strings.
static void le(std::string& o, uint32_t v, int n) {
    for (int i = 0; i < n; ++i) o += static_cast<char>((v >> (8 * i)) & 0xFF);
}
static std::string zip(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, dir;
    for (auto& [name, data] : files) {
        // Deflate the sheet, store the rest: both paths get read.
        bool deflated = name.find("sheet1") != std::string::npos;
        std::string body = data;
        if (deflated) {
            z_stream st{};
            deflateInit2(&st, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            std::string buf(deflateBound(&st, data.size()), '\0');
            st.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
            st.avail_in = static_cast<uInt>(data.size());
            st.next_out = reinterpret_cast<Bytef*>(&buf[0]);
            st.avail_out = static_cast<uInt>(buf.size());
            deflate(&st, Z_FINISH);
            buf.resize(st.total_out);
            deflateEnd(&st);
            body = buf;
        }
        uint32_t crc = static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size())));
        uint32_t off = static_cast<uint32_t>(out.size());
        le(out, 0x04034b50, 4); le(out, 20, 2); le(out, 0, 2); le(out, deflated ? 8 : 0, 2); le(out, 0, 4);
        le(out, crc, 4); le(out, static_cast<uint32_t>(body.size()), 4); le(out, static_cast<uint32_t>(data.size()), 4);
        le(out, static_cast<uint32_t>(name.size()), 2); le(out, 0, 2);
        out += name + body;
        le(dir, 0x02014b50, 4); le(dir, 20, 2); le(dir, 20, 2); le(dir, 0, 2); le(dir, deflated ? 8 : 0, 2);
        le(dir, 0, 4); le(dir, crc, 4); le(dir, static_cast<uint32_t>(body.size()), 4);
        le(dir, static_cast<uint32_t>(data.size()), 4); le(dir, static_cast<uint32_t>(name.size()), 2);
        le(dir, 0, 2); le(dir, 0, 2); le(dir, 0, 2); le(dir, 0, 2); le(dir, 0, 4); le(dir, off, 4);
        dir += name;
    }
    uint32_t at = static_cast<uint32_t>(out.size());
    out += dir;
    le(out, 0x06054b50, 4); le(out, 0, 2); le(out, 0, 2); le(out, static_cast<uint32_t>(files.size()), 2);
    le(out, static_cast<uint32_t>(files.size()), 2); le(out, static_cast<uint32_t>(dir.size()), 4); le(out, at, 4);
    le(out, 0, 2);
    return out;
}
static std::string xlsx(const std::string& sheet_name, const std::vector<std::vector<std::string>>& rows) {
    // Shared strings for the first column, inline strings elsewhere: both get read.
    std::string ss = "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
    std::string sheet = "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>";
    int n = 0;
    for (size_t r = 0; r < rows.size(); ++r) {
        sheet += "<row r=\"" + std::to_string(r + 1) + "\">";
        for (size_t c = 0; c < rows[r].size(); ++c) {
            if (rows[r][c].empty()) continue;
            std::string ref = std::string(1, static_cast<char>('A' + c)) + std::to_string(r + 1);
            if (c == 0) {
                ss += "<si><t>" + rows[r][c] + "</t></si>";
                sheet += "<c r=\"" + ref + "\" t=\"s\"><v>" + std::to_string(n++) + "</v></c>";
            } else {
                sheet += "<c r=\"" + ref + "\" t=\"inlineStr\"><is><t>" + rows[r][c] + "</t></is></c>";
            }
        }
        sheet += "</row>";
    }
    ss += "</sst>";
    sheet += "</sheetData></worksheet>";
    return zip({{"[Content_Types].xml", "<Types/>"},
                {"xl/workbook.xml",
                 "<workbook xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>"
                 "<sheet name=\"" + sheet_name + "\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>"},
                {"xl/_rels/workbook.xml.rels",
                 "<Relationships><Relationship Id=\"rId1\" Target=\"worksheets/sheet1.xml\"/></Relationships>"},
                {"xl/sharedStrings.xml", ss},
                {"xl/worksheets/sheet1.xml", sheet}});
}
static std::string b64(const std::string& in) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : in) {
        acc = (acc << 8) | c;
        bits += 8;
        while (bits >= 6) bits -= 6, out += k[(acc >> bits) & 63];
    }
    if (bits) out += k[(acc << (6 - bits)) & 63];
    while (out.size() % 4) out += '=';
    return out;
}

int main() {
    // The small parsers.
    CHECK(marsio::normal_days("Daily") == "SUN,MON,TUE,WED,THU,FRI,SAT");
    CHECK(marsio::normal_days("Mon-Fri") == "MON,TUE,WED,THU,FRI");
    CHECK(marsio::normal_days("Tue/Thu") == "TUE,THU");
    CHECK(marsio::normal_days("Tuesdays & Thursdays") == "TUE,THU");
    CHECK(marsio::normal_days("1st & 3rd Sat") == "1SAT,3SAT");
    CHECK(marsio::normal_days("Last Sunday of the month") == "LSUN");
    CHECK(marsio::normal_days("Weekends") == "SAT,SUN");
    CHECK(marsio::normal_days("whenever") == "");
    CHECK(marsio::utc_minutes("1900Z") == 1140);
    CHECK(marsio::utc_minutes("19:00 UTC") == 1140);
    CHECK(marsio::utc_minutes("0130") == 90);
    CHECK(marsio::utc_minutes("7:00 PM ET") == -1);
    CHECK(marsio::utc_minutes("1900 local") == -1);
    CHECK(parse_frequency("5.000", "") == 5000000);
    CHECK(parse_frequency("10000.0", "") == 10000000);
    CHECK(parse_frequency("2500", "khz") == 2500000);
    CHECK(parse_frequency("2.5 MHz", "") == 2500000);
    CHECK(parse_frequency("banana", "") == 0);
    CHECK(valid_designator("P1") && valid_designator("CH-07") && !valid_designator("5000") &&
          !valid_designator("5.000") && !valid_designator(""));
    std::string decoded;
    CHECK(base64_decode(b64("roster,data\n"), decoded) && decoded == "roster,data\n");

    fs::path dir = fs::temp_directory_path() / ("pna_import_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string db = (dir / "NCS-A.db").string();
    Core core(CoreConfig{"NCS-A", db, 3, 1, true}, std::make_unique<LoopbackModem>(0));
    auto ops = std::make_shared<Ops>("NCS-A", db);
    core.set_ops(ops);

    // 1. A member roster: a title line, then people. Personal columns stay out.
    std::string roster =
        "TEST REGION MEMBER ROSTER (MADE UP)\n"
        "\n"
        "MARS Call,First Name,Last Name,Email,Phone,State,Position,Group\n"
        "STA-B,Pat,Example,pat@example.invalid,555-0100,ZZ,Net Control,Practice Net\n"
        "STA-C,Sam,Sample,sam@example.invalid,555-0101,ZZ,Member,Practice Net\n"
        "ALT-K,Kim,Test,kim@example.invalid,555-0102,YY,Alternate Keymaster,\n"
        "MARS Call,First Name,Last Name,Email,Phone,State,Position,Group\n"
        ",,,,,,,\n";
    std::string raw;
    JsonValue r = call(core, {{"cmd", "mars_import"}, {"text", roster}, {"source", "roster test"}}, &raw);
    const JsonValue& s0 = sheet0(r);
    CHECK(ok(r) && str(s0, "kind") == "roster" && num(s0, "header_row") == 3 && num(s0, "rows") == 3);
    CHECK(contains(strs(s0, "names"), "STA-B") && contains(strs(s0, "names"), "ALT-K"));
    for (auto* col : {"First Name", "Last Name", "Email", "Phone"}) CHECK(contains(strs(s0, "left_out"), col));
    for (auto* pii : {"Pat", "Example", "example.invalid", "555-0100"}) CHECK(raw.find(pii) == std::string::npos);
    auto b = ops->book_get("STA-B");
    CHECK(b && b->kind == "station" && b->note == "NET CONTROL; ZZ" && b->source == "roster test");
    std::string book = ops->book_export();
    CHECK(book.find("EXAMPLE") == std::string::npos && book.find("@") == std::string::npos);
    // Group and state columns become collectives.
    auto pn = ops->book_get("PRACTICE NET");
    CHECK(pn && pn->kind == "collective" && pn->members.size() == 2);
    auto zz = ops->book_get("ZZ");
    CHECK(zz && zz->kind == "collective" && contains(zz->members, "STA-C"));

    // 2. An address book as a spreadsheet: addressing and a collective.
    std::string book_x = xlsx("Address Book", {{"TEST ADDRESS BOOK"},
                                               {},
                                               {"PLA", "Call Sign", "RI", "ALE Address", "Members"},
                                               {"STATION CHARLIE", "STA-C", "UHAAAC", "STAC", ""},
                                               {"TEST NET ALL", "", "", "", "STA-B; STA-C; ALT-K"}});
    r = call(core, {{"cmd", "mars_import"}, {"data", b64(book_x)}, {"source", "book test"}});
    const JsonValue& s1 = sheet0(r);
    CHECK(ok(r) && str(s1, "kind") == "book" && str(s1, "sheet") == "Address Book" && num(s1, "imported") == 2);
    auto sc = ops->book_get("STATION CHARLIE");
    CHECK(sc && sc->call == "STA-C" && sc->ri == "UHAAAC" && sc->ale == "STAC");
    auto all = ops->book_get("TEST NET ALL");
    CHECK(all && all->kind == "collective" && all->members.size() == 3);
    r = call(core, {{"cmd", "book_resolve"}, {"to", "TEST NET ALL"}});
    CHECK(ok(r));

    // 3. A frequency card, columns lined up with spaces.
    std::string card =
        "FREQUENCY CARD   TEST ONLY\n"
        "CHAN   FREQ (KHZ)   MODE   USE\n"
        "P1     5000.0       USB    PRIMARY\n"
        "P2     10000.0      USB    SECONDARY\n"
        "S1     2500.0       ALE    SCAN\n"
        "5000   2500.0       USB    BAD DESIGNATOR\n"
        "X9     LATER        USB    NO FREQUENCY YET\n";
    r = call(core, {{"cmd", "mars_import"}, {"text", card}}, &raw);
    const JsonValue& s2 = sheet0(r);
    CHECK(ok(r) && str(s2, "kind") == "freq" && num(s2, "imported") == 3 && strs(s2, "problems").size() == 2);
    CHECK(raw.find("5000.0") == std::string::npos && raw.find("10000") == std::string::npos &&
          raw.find("2500") == std::string::npos);
    CHECK(ops->channels().get("P2") && ops->channels().get("P2")->hz == 10000000);
    r = call(core, {{"cmd", "channels_list"}}, &raw);
    CHECK(ok(r) && r.get("channels")->arr.size() == 3);
    CHECK(raw.find("5000") == std::string::npos && raw.find("hz") == std::string::npos);
    struct stat st{};
    CHECK(::stat((dir / "NCS-A.channels").c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    // Nothing frequency-shaped reached the station book or the log.
    for (auto& e : core.events(0, 1000)) CHECK(e.text.find("5000") == std::string::npos);
    // A second Ops on the same database reads the plan back.
    {
        Ops again("NCS-A", db);
        CHECK(again.channels().size() == 3 && again.channels().get("S1")->mode == "ALE");
    }

    // 4. A net schedule. Days and times as people write them.
    std::string sched =
        "Net,Day,Time (UTC),Duration,NCS,Channel,Frequency,Notes\n"
        "Practice Net,Tue/Thu,1900Z,60,NCS-A,P1,,Weekly practice\n"
        "Region Net,1st & 3rd Sat,13:00 UTC,90 min,NCS-A,,10000 kHz,\n"
        "Odd Net,Mon,0200Z,1 hr,NCS-A,,20000.0,\n"
        "Local Net,Mon,7:00 PM ET,60,NCS-A,P1,,\n"
        "Bad Days Net,Someday,1900Z,60,NCS-A,P1,,\n";
    r = call(core, {{"cmd", "mars_import"}, {"text", sched}}, &raw);
    const JsonValue& s3 = sheet0(r);
    CHECK(ok(r) && str(s3, "kind") == "schedule" && num(s3, "imported") == 3 && strs(s3, "problems").size() == 2);
    CHECK(num(s3, "frequencies_withheld") == 1);
    CHECK(raw.find("20000") == std::string::npos && raw.find("10000") == std::string::npos);
    auto pr = ops->sched_get("PRACTICE NET");
    CHECK(pr && pr->days == "TUE,THU" && pr->start == 1140 && pr->minutes == 60 && pr->channel == "P1");
    auto rn = ops->sched_get("REGION NET");
    CHECK(rn && rn->days == "1SAT,3SAT" && rn->start == 780 && rn->minutes == 90 && rn->channel == "P2");
    auto on = ops->sched_get("ODD NET");
    CHECK(on && on->channel.empty() && on->minutes == 60);
    r = call(core, {{"cmd", "sched_list"}}, &raw);
    CHECK(ok(r) && raw.find("\"channel\":\"P2\"") != std::string::npos);
    // A frequency typed as a schedule's channel is refused.
    r = call(core, {{"cmd", "sched_put"}, {"name", "X NET"}, {"days", "MON"}, {"time", "1900"}, {"channel", "5.000"}});
    CHECK(!ok(r) && str(r, "error").find("designator") != std::string::npos);

    // 5. Something that's none of these.
    r = call(core, {{"cmd", "mars_import"}, {"text", "just,some\ncolumns,here\n"}});
    CHECK(!ok(r));
    r = call(core, {{"cmd", "mars_import"}, {"text", roster}, {"kind", "keys"}});
    CHECK(!ok(r));
    // The kind can be forced.
    r = call(core, {{"cmd", "mars_import"}, {"text", card}, {"kind", "roster"}});
    CHECK(!ok(r));

    fs::remove_all(dir);
    return check::report("test_import");
}
