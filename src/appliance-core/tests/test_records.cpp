// The station's records from the kiosk (phoenix_nest_mars#3).
//
// 1. CHANNELS (Alex's ruling 4 of 2026-11-26, asks 27 and 29): channels_get_full
//    is the one reply with a frequency; channels_list and every other reply
//    stay frequency-free. channel_put keeps a frequency left out and refuses
//    one sent empty.
// 2. Each channel's world (mars or amateur) and HFLink label (v1.9). The core's
//    nets, call-ups and keying use only MARS channels (ask 40).
// 3. Ask 26: a kind of record the host locked is refused at every call that
//    changes it, a form's action included.
// 4. Ask 28: gale-rx's schedule files, written from the net schedules.
// 5. Ask 30: calls heard added to the book, when the operator turns it on.
//
// Every frequency is a public time-signal one or made up; every call is made up.
#include "../src/api_server.h"
#include "../src/channels.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/ops.h"
#include "../src/records.h"
#include "../src/settings.h"
#include "check.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>

using namespace pna;
namespace fs = std::filesystem;

namespace {

// A frequency left in text as a number of its own.
bool leaks(const std::string& text, const std::string& needle) {
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        const size_t end = at + needle.size();
        if (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) continue;
        if (at == 0) return true;
        unsigned char before = static_cast<unsigned char>(text[at - 1]);
        if (!std::isalnum(before) && before != '+' && before != '/') return true;
    }
    return false;
}

bool wait_for(const std::function<bool()>& cond, int ms = 10000) {
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

JsonValue api(Core& c, std::vector<std::pair<std::string, std::string>> kv, std::string* raw = nullptr) {
    std::string line = "{";
    bool first = true;
    for (auto& [k, v] : kv) {
        line += (first ? "\"" : ",\"") + k + "\":\"" + esc(v) + "\"";
        first = false;
    }
    line += "}";
    std::string reply = ApiServer::handle(c, line);
    if (std::getenv("REC_DEBUG")) std::cerr << line.substr(0, 160) << "\n  -> " << reply.substr(0, 600) << "\n";
    if (raw) *raw = reply;
    JsonValue v;
    parse_json(reply, v);
    return v;
}

bool ok(const JsonValue& v) {
    const JsonValue* o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}
bool yes(const JsonValue& v, const char* k) {
    const JsonValue* o = v.get(k);
    return o && o->type == JsonValue::Type::Bool && o->b;
}
std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
double num(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::Number ? x->n : -1;
}
const std::vector<JsonValue>& arr(const JsonValue& v, const char* k) {
    static const std::vector<JsonValue> none;
    const JsonValue* x = v.get(k);
    return x && x->is_array() ? x->arr : none;
}
const JsonValue* row(const JsonValue& v, const char* list, const char* key, const std::string& name) {
    for (auto& r : arr(v, list))
        if (str(r, key) == name) return &r;
    return nullptr;
}
std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

struct Box {
    std::unique_ptr<Core> core;
    std::shared_ptr<Ops> ops;
};

Box open_box(const fs::path& dir, const std::string& call) {
    Box b;
    fs::create_directories(dir);
    std::string base = (dir / call).string();
    b.core = std::make_unique<Core>(CoreConfig{call, base + ".db", 3, 1, true}, std::make_unique<LoopbackModem>(0));
    b.ops = std::make_shared<Ops>(call, base + ".db");
    b.core->set_ops(b.ops);
    b.core->start();
    return b;
}

// ---- 1 and 2: channels ----

void channels(const fs::path& dir) {
    Box A = open_box(dir / "ch", "NCS-A");
    Core& c = *A.core;
    std::string raw, all;
    JsonValue r = api(c, {{"cmd", "channel_put"}, {"name", "p1"}, {"frequency", "5000"}, {"use", "primary"}}, &raw);
    all += raw;
    CHECK(ok(r) && str(r, "result") == "added" && str(r, "world") == "mars" && str(r, "label").empty());

    // A change to the port alone keeps the frequency: no retyping.
    r = api(c, {{"cmd", "channel_put"}, {"name", "P1"}, {"ant_tx", "2"}}, &raw);
    all += raw;
    CHECK(ok(r) && str(r, "result") == "replaced" && !yes(r, "frequency_changed") && num(r, "ant_tx") == 2);
    CHECK(A.ops->channels().get("P1")->hz == 5000000 && A.ops->channels().get("P1")->use == "PRIMARY");
    // The same again is "same".
    CHECK(str(api(c, {{"cmd", "channel_put"}, {"name", "P1"}, {"ant_tx", "2"}}), "result") == "same");
    // Sent empty, it is refused, and the channel is as it was.
    r = api(c, {{"cmd", "channel_put"}, {"name", "P1"}, {"frequency", ""}, {"ant_tx", "1"}});
    CHECK(!ok(r) && A.ops->channels().get("P1")->hz == 5000000 && A.ops->channels().get("P1")->ant_tx == 2);
    // A new channel needs one.
    CHECK(!ok(api(c, {{"cmd", "channel_put"}, {"name", "P2"}, {"mode", "USB"}})) && !A.ops->channels().get("P2"));

    // An amateur channel with its HFLink label.
    r = api(c, {{"cmd", "channel_put"}, {"name", "A1"}, {"frequency", "15000"}, {"world", "Amateur"},
                {"label", "hfl-x1"}}, &raw);
    all += raw;
    CHECK(ok(r) && str(r, "world") == "amateur" && str(r, "label") == "HFL-X1");
    // Left out, world and label are kept; a mode change alone keeps both.
    r = api(c, {{"cmd", "channel_put"}, {"name", "A1"}, {"mode", "usb"}}, &raw);
    all += raw;
    CHECK(ok(r) && str(r, "world") == "amateur" && str(r, "label") == "HFL-X1" && !yes(r, "frequency_changed"));
    CHECK(A.ops->channels().get("A1")->hz == 15000000);
    // What it refuses: a world it doesn't know, a label shaped like a frequency.
    CHECK(!ok(api(c, {{"cmd", "channel_put"}, {"name", "A1"}, {"world", "cb"}})));
    CHECK(!ok(api(c, {{"cmd", "channel_put"}, {"name", "A1"}, {"label", "15.000"}})));
    CHECK(!ok(api(c, {{"cmd", "channel_put"}, {"name", "A1"}, {"label", "HFL X"}})));
    CHECK(A.ops->channels().get("A1")->label == "HFL-X1");
    // A label sent empty is cleared, and set again.
    CHECK(api(c, {{"cmd", "channel_put"}, {"name", "A1"}, {"label", ""}}).get("label")->s.empty());
    CHECK(ok(api(c, {{"cmd", "channel_put"}, {"name", "A1"}, {"label", "HFL-X1"}})));

    // channels_list: world and label, never a frequency.
    r = api(c, {{"cmd", "channels_list"}}, &raw);
    all += raw;
    const JsonValue* a1 = row(r, "channels", "name", "A1");
    CHECK(ok(r) && a1 && str(*a1, "world") == "amateur" && str(*a1, "label") == "HFL-X1" && !a1->get("frequency_hz"));
    CHECK(!leaks(all, "5000") && !leaks(all, "15000") && !leaks(all, "5000000") && !leaks(all, "15000000"));

    // channels_get_full: the one reply with each frequency.
    r = api(c, {{"cmd", "channels_get_full"}}, &raw);
    const JsonValue* p1 = row(r, "channels", "name", "P1");
    a1 = row(r, "channels", "name", "A1");
    CHECK(ok(r) && p1 && a1 && num(*p1, "frequency_hz") == 5000000 && num(*a1, "frequency_hz") == 15000000);
    CHECK(str(*p1, "world") == "mars" && str(*a1, "label") == "HFL-X1" && num(*p1, "ant_tx") == 2);
    // Its read is logged, with no frequency.
    std::string log;
    JsonValue events = api(c, {{"cmd", "log"}, {"after", "0"}, {"limit", "1000"}});
    for (auto& e : arr(events, "events")) log += str(e, "text") + "\n";
    CHECK(log.find("channels_get_full") != std::string::npos && !leaks(log, "5000000") && !leaks(log, "15000000"));

    // The plan's file keeps the world and label; a record written before
    // them reads as mars with no label.
    {
        std::ofstream f(dir / "ch" / "old.channels");
        f << "PNA CHANNEL PLAN 1\nP9|5000000|USB|PRIMARY|CARD|1|2\n";
    }
    ChannelPlan old((dir / "ch" / "old.channels").string());
    CHECK(old.load(nullptr) && old.get("P9") && old.get("P9")->world == "mars" && old.get("P9")->label.empty() &&
          old.get("P9")->ant_tx == 2);
    ChannelPlan again((dir / "ch" / "NCS-A.channels").string());
    CHECK(again.load(nullptr) && again.get("A1")->world == "amateur" && again.get("A1")->label == "HFL-X1");
    // An amateur channel is never a MARS designator; it is shown by its label.
    CHECK(A.ops->channels().designator_for(15000000).empty() && A.ops->channels().designator_for(5000000) == "P1");
    CHECK(A.ops->channels().display_name_for(15000000) == "HFL-X1" &&
          A.ops->channels().display_name_for(5000000) == "P1");

    // The CHANNEL form: an amateur channel opens with its world and label,
    // and a change to the port alone keeps the frequency.
    r = api(c, {{"cmd", "form_get"}, {"form", "CHANNEL"}, {"name", "A1"}}, &raw);
    std::string world, label;
    for (auto& f : arr(r, "fields")) {
        if (str(f, "key") == "world") world = str(f, "value");
        if (str(f, "key") == "label") label = str(f, "value");
    }
    CHECK(ok(r) && world == "AMATEUR" && label == "HFL-X1" && !leaks(raw, "15000"));
    r = api(c, {{"cmd", "form_submit"}, {"form", "CHANNEL"}, {"name", "A1"}, {"ant_rx", "2"}});
    CHECK(ok(r) && A.ops->channels().get("A1")->hz == 15000000 && A.ops->channels().get("A1")->ant_rx == 2 &&
          A.ops->channels().get("A1")->world == "amateur");
    r = api(c, {{"cmd", "form_submit"}, {"form", "CHANNEL"}, {"name", "P5"}});
    CHECK(!ok(r) && !A.ops->channels().get("P5"));

    // Ask 40: a MARS net, its schedule and its call-up go on MARS channels only.
    CHECK(!ok(api(c, {{"cmd", "net_open"}, {"name", "TEST NET"}, {"channel", "A1"}})));
    CHECK(!ok(api(c, {{"cmd", "sched_put"}, {"name", "TEST NET"}, {"days", "TUE"}, {"time", "1900"}, {"channel", "A1"}})));
    CHECK(A.ops->sched_list().empty());
    r = api(c, {{"cmd", "net_open"}, {"name", "TEST NET"}, {"channel", "P1"}, {"roster", "STA-B"}});
    CHECK(ok(r));
    // A call-up names ACP 126's three-letter channel.
    CHECK(ok(api(c, {{"cmd", "channel_put"}, {"name", "AMX"}, {"frequency", "15000"}, {"world", "amateur"}})));
    CHECK(ok(api(c, {{"cmd", "channel_put"}, {"name", "QXA"}, {"frequency", "5000"}})));
    r = api(c, {{"cmd", "net_callup"}, {"channel", "AMX"}});
    CHECK(!ok(r) && str(r, "error").find("amateur channel") != std::string::npos);
    CHECK(ok(api(c, {{"cmd", "net_callup"}, {"channel", "QXA"}})));
    A.core->stop();
}

// ---- 3: record classes (ask 26) ----

void locks(const fs::path& dir) {
    Box A = open_box(dir / "lock", "NCS-A");
    Core& c = *A.core;
    CHECK(ok(api(c, {{"cmd", "channel_put"}, {"name", "P1"}, {"frequency", "5000"}})));
    std::string err;
    CHECK(!records::set_locked({"channels", "nothing"}, &err) && err.find("nothing") != std::string::npos);
    CHECK(records::locked().empty());
    CHECK(records::set_locked({"channels", "key_tape"}, &err));

    // Every call that changes the plan is refused, with why, and logged.
    JsonValue r = api(c, {{"cmd", "channel_put"}, {"name", "P1"}, {"ant_tx", "2"}});
    CHECK(!ok(r) && str(r, "refused") == "locked" && str(r, "record") == "channels" &&
          str(r, "error").find("locked") != std::string::npos);
    CHECK(!ok(api(c, {{"cmd", "channel_remove"}, {"name", "P1"}})) && A.ops->channels().get("P1"));
    CHECK(str(api(c, {{"cmd", "key_zeroize"}, {"which", "ALL"}}), "record") == "key-tape");
    CHECK(str(api(c, {{"cmd", "station_restore"}}), "refused") == "locked");
    // A form can't get round it: its action goes through the same door.
    r = api(c, {{"cmd", "form_submit"}, {"form", "CHANNEL"}, {"name", "P1"}, {"ant_tx", "2"}});
    CHECK(!ok(r) && A.ops->channels().get("P1")->ant_tx == 1);
    std::string log;
    JsonValue events = api(c, {{"cmd", "log"}, {"after", "0"}, {"limit", "1000"}});
    for (auto& e : arr(events, "events")) log += str(e, "text") + "\n";
    CHECK(log.find("refused channel_put: channels is locked") != std::string::npos);

    // Reading is never refused, and other kinds still change.
    CHECK(ok(api(c, {{"cmd", "channels_list"}})) && ok(api(c, {{"cmd", "channels_get_full"}})));
    CHECK(ok(api(c, {{"cmd", "sched_put"}, {"name", "TEST NET"}, {"days", "TUE"}, {"time", "1900"}})));
    CHECK(ok(api(c, {{"cmd", "book_put"}, {"name", "STA-B"}})));

    // settings_list names each kind and its class.
    r = api(c, {{"cmd", "settings_list"}});
    const JsonValue* ch = row(r, "records", "kind", "channels");
    const JsonValue* bk = row(r, "records", "kind", "book");
    CHECK(ch && str(*ch, "class") == "locked" && bk && str(*bk, "class") == "operator");
    CHECK(records::find("KEY_TAPE") && records::find("key_tape")->key == "key-tape");

    CHECK(records::set_locked({}, &err));
    CHECK(ok(api(c, {{"cmd", "channel_put"}, {"name", "P1"}, {"ant_tx", "2"}})));
    A.core->stop();
}

// ---- 4: gale-rx's schedule files (ask 28) ----

NetSchedule sched(const std::string& name, const std::string& days, int start, int minutes, const std::string& plan,
                  int index = -1, const std::string& mode = "", const std::string& channel = "") {
    NetSchedule s;
    s.name = name;
    s.days = days;
    s.start = start;
    s.minutes = minutes;
    s.plan = plan;
    s.rx_index = index;
    s.rx_mode = mode;
    s.channel = channel;
    return s;
}

void rx_files(const fs::path& dir) {
    const int64_t mon_23 = 1795392000;  // Monday 2026-11-23 0000Z
    const int64_t mon_30 = 1795996800;  // Monday 2026-11-30 0000Z: the Saturday after is the month's first
    std::vector<NetSchedule> v = {
        sched("TUESDAY NET", "TUE", 19 * 60, 60, "net03", -1, "", "P1"),
        sched("LATE NET", "SUN", 23 * 60 + 30, 60, "net01", 2, "LSB"),
        sched("NO PLAN", "WED", 12 * 60, 60, ""),
        sched("TUESDAY TOO", "TUE", 19 * 60, 30, "net05"),
        sched("FIRST SAT", "1SAT", 12 * 60, 30, "net02"),
        sched("LONG", "THU", 10 * 60, 120, "net04"),
        sched("INSIDE", "THU", 11 * 60, 120, "net06"),
    };
    RxSchedule r = rx_schedule(v, mon_23);
    const std::string& ns = r.net_schedule;
    const std::string& sc = r.slot_channels;
    auto has = [](const std::string& text, const std::string& line) { return text.find(line + "\n") != std::string::npos; };
    CHECK(ns.rfind("# DAY HHMM(Zulu) PLAN MODE", 0) == 0);
    CHECK(has(ns, "TUE 1900 net_plans/net03.plan net  # TUESDAY-NET + P1"));
    CHECK(has(ns, "TUE 2000 net_plans/net03.plan scan  # end of TUESDAY-NET") && has(sc, "TUE 2000 park"));
    // Across midnight and round the week: Sunday 2330 ends Monday 0030.
    CHECK(has(ns, "SUN 2330 net_plans/net01.plan net  # LATE-NET") && has(sc, "SUN 2330 2 LSB"));
    CHECK(has(ns, "MON 0030 net_plans/net01.plan scan  # end of LATE-NET") && has(sc, "MON 0030 park"));
    // A sitting ending inside another gets no end of its own.
    CHECK(has(ns, "THU 1000 net_plans/net04.plan net  # LONG") && has(ns, "THU 1100 net_plans/net06.plan net  # INSIDE"));
    CHECK(ns.find("THU 1200") == std::string::npos && has(ns, "THU 1300 net_plans/net06.plan scan  # end of INSIDE"));
    // No plan, or the same start as another: left out, with why.
    CHECK(ns.find("NO-PLAN") == std::string::npos && ns.find("TUESDAY-TOO") == std::string::npos);
    bool no_plan = false, clash = false;
    for (auto& s : r.skipped) {
        no_plan = no_plan || s == "NO PLAN: no plan";
        clash = clash || s.find("TUESDAY TOO: starts at the same time as TUESDAY NET on TUE") == 0;
    }
    CHECK(no_plan && clash);
    // A week-of-month day is in only in the week it falls.
    CHECK(ns.find("FIRST-SAT") == std::string::npos);
    CHECK(rx_schedule(v, mon_30).net_schedule.find("SAT 1200 net_plans/net02.plan net  # FIRST-SAT") != std::string::npos);
    // Slots in week order: Monday first.
    CHECK(ns.find("MON 0030") < ns.find("TUE 1900") && ns.find("TUE 1900") < ns.find("SUN 2330"));

    // Through the API: the fields, their checks, and the files on every change.
    Box A = open_box(dir / "rx", "NCS-A");
    Core& c = *A.core;
    auto put = [&](std::vector<std::pair<std::string, std::string>> extra) {
        std::vector<std::pair<std::string, std::string>> kv = {
            {"cmd", "sched_put"}, {"name", "TUESDAY NET"}, {"days", "TUE"}, {"time", "1900"}};
        kv.insert(kv.end(), extra.begin(), extra.end());
        return api(c, kv);
    };
    JsonValue j = put({{"plan", "net03"}});
    CHECK(ok(j) && str(j, "rx_file") == "off");
    const JsonValue& s0 = arr(j, "schedules")[0];
    CHECK(str(s0, "plan") == "net03" && s0.get("index") && s0.get("index")->type == JsonValue::Type::Null &&
          str(s0, "mode").empty());
    CHECK(!ok(put({{"plan", "7102"}})) && !ok(put({{"plan", "../x"}})));
    CHECK(!ok(put({{"index", "1"}, {"plan", ""}})) && !ok(put({{"mode", "LSB"}})));
    CHECK(!ok(put({{"index", "1"}, {"mode", "FM"}})) && !ok(put({{"index", "x"}})));
    fs::path rx = dir / "rx" / "gale-rx";
    A.ops->set_rx_schedule_dir(rx.string());
    j = put({{"index", "1"}, {"mode", "lsb"}});
    CHECK(ok(j) && str(j, "rx_file").rfind("not written: no folder", 0) == 0);
    fs::create_directories(rx);
    j = put({{"index", "1"}, {"mode", "lsb"}});
    CHECK(ok(j) && str(j, "rx_file") == "written");
    // Left out, the plan, index and mode are kept.
    j = put({{"note", "WEEKLY"}});
    const JsonValue& s1 = arr(j, "schedules")[0];
    CHECK(ok(j) && str(s1, "plan") == "net03" && num(s1, "index") == 1 && str(s1, "mode") == "LSB");
    CHECK(slurp(rx / "slot-channels.txt").find("TUE 1900 1 LSB\n") != std::string::npos);
    CHECK(slurp(rx / "net_schedule.txt").find("TUE 1900 net_plans/net03.plan net  # TUESDAY-NET\n") !=
          std::string::npos);
    struct stat st{};
    CHECK(::stat((rx / "net_schedule.txt").c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    // The SCHEDULE form opens it with them.
    JsonValue f = api(c, {{"cmd", "form_get"}, {"form", "SCHEDULE"}, {"name", "TUESDAY NET"}});
    std::string plan, index;
    for (auto& x : arr(f, "fields")) {
        if (str(x, "key") == "plan") plan = str(x, "value");
        if (str(x, "key") == "index") index = str(x, "value");
    }
    CHECK(plan == "net03" && index == "1");
    // Removed: written again, without it.
    j = api(c, {{"cmd", "sched_remove"}, {"name", "TUESDAY NET"}});
    CHECK(ok(j) && str(j, "rx_file") == "written" && slurp(rx / "net_schedule.txt").find("TUESDAY") == std::string::npos);
    // The tick writes only a change.
    CHECK(A.ops->write_rx_schedule() == "unchanged");
    A.core->stop();
}

// ---- 5: calls heard added to the book (ask 30) ----

void add_heard(const fs::path& dir) {
    fs::create_directories(dir / "heard");
    settings::Start st;
    st.db_dir = (dir / "heard").string();
    st.gale_dir = (dir / "heard").string();
    settings::init(st);

    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    Core a(CoreConfig{"STA-A", (dir / "heard" / "a.db").string(), 3, 1, true}, std::move(ma));
    Core b(CoreConfig{"STA-B", (dir / "heard" / "b.db").string(), 3, 1, true}, std::move(mb));
    auto ob = std::make_shared<Ops>("STA-B", (dir / "heard" / "b.db").string());
    b.set_ops(ob);
    a.start();
    b.start();
    auto send = [&](const std::string& text) {
        std::string err, id = a.submit("STA-B", 'R', text, &err);
        CHECK(!id.empty());
        CHECK(wait_for([&] { return a.get(id) && a.get(id)->state == "acked"; }));
    };

    JsonValue r = api(b, {{"cmd", "settings_list"}, {"group", "Book"}});
    const JsonValue* rowp = row(r, "settings", "key", "book-auto-add-heard");
    CHECK(rowp && str(*rowp, "class") == "operator" && str(*rowp, "type") == "bool" && str(*rowp, "value") == "no" &&
          str(*rowp, "applies") == "now");
    // Off: a station heard isn't added.
    send("FIRST");
    CHECK(!ob->book_find("STA-A"));
    // On: it is, as a MARS station heard.
    CHECK(ok(api(b, {{"cmd", "settings_set"}, {"key", "book_auto_add_heard"}, {"value", "yes"}})));
    send("SECOND");
    CHECK(wait_for([&] { return ob->book_find("STA-A").has_value(); }));
    auto e = ob->book_find("STA-A");
    CHECK(e && e->call == "STA-A" && e->source == "heard" && e->world == "mars" && e->kind == "station");
    // An entry already there is left as it is.
    BookEntry mine;
    mine.name = "STA-A";
    mine.note = "KEPT";
    std::string err;
    CHECK(ob->book_put(mine, &err));
    send("THIRD");
    CHECK(ob->book_find("STA-A")->note == "KEPT" && ob->book_find("STA-A")->source == "local");
    CHECK(ok(api(b, {{"cmd", "settings_reset"}, {"key", "book-auto-add-heard"}})));
    a.stop();
    b.stop();
}

}  // namespace

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_records_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    channels(dir);
    locks(dir);
    rx_files(dir);
    add_heard(dir);
    fs::remove_all(dir);
    return check::report("test_records");
}
