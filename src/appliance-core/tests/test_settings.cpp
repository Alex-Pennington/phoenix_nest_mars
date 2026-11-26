// The operator's settings (asks 34, 35, 36): the table, the operator file,
// the gates, and the keying file gale-key reads.
#include "../src/json.h"
#include "../src/settings.h"
#include "check.h"

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace pna;
namespace fs = std::filesystem;

static JsonValue call(std::map<std::string, std::string> req) {
    std::string cmd = req["cmd"];
    auto r = settings::handle(cmd, req, 1800000000);
    JsonValue v;
    if (r) parse_json(*r, v);
    return v;
}
static bool ok(const JsonValue& v) {
    const JsonValue* o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}
static std::string str(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::String ? x->s : "";
}
static bool bl(const JsonValue& v, const char* k) {
    const JsonValue* x = v.get(k);
    return x && x->type == JsonValue::Type::Bool && x->b;
}
static std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static const JsonValue* row(const JsonValue& list, const std::string& key) {
    const JsonValue* s = list.get("settings");
    if (!s) return nullptr;
    for (auto& r : s->arr)
        if (str(r, "key") == key) return &r;
    return nullptr;
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_settings_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::path db = dir / "db", gale = dir / "gale";
    fs::create_directories(db);
    fs::create_directories(gale);

    // ---- the table: every config key is a row; host plumbing is locked ------
    for (const char* k : {"db-dir", "api-port", "web-port", "link-socket", "tx-cmd", "gale-dir", "keying-file"})
        CHECK(settings::find(k) && settings::find(k)->cls == "locked");
    for (const char* k : {"station", "tx-mode", "world", "ptt_line", "cat_ptt", "engineering_readouts",
                          "no-acks", "max-attempts", "handling", "raw-rx-log"})
        CHECK(settings::find(k) && settings::find(k)->cls == "operator");
    CHECK(settings::find("ptt_line")->key == "ptt-line" && !settings::find("frequency"));
    // tx-world retired (greybeard-ale #18 v2.2): a send carries its own service.
    CHECK(!settings::find("tx-world") && !settings::find("tx_world"));

    // ---- values checked ----------------------------------------------------
    std::string n, e;
    CHECK(settings::check(*settings::find("ptt-line"), "rts", &n, &e) && n == "RTS");
    CHECK(!settings::check(*settings::find("ptt-line"), "VOX", &n, &e));  // never VOX
    CHECK(settings::check(*settings::find("cat-ptt"), "on", &n, &e) && n == "yes");
    CHECK(!settings::check(*settings::find("max-attempts"), "0", &n, &e) && !settings::check(*settings::find("max-attempts"), "x", &n, &e));
    CHECK(settings::check(*settings::find("handling"), "5, 30,180,0", &n, &e) && n == "5,30,180,0");
    CHECK(!settings::check(*settings::find("handling"), "5,30", &n, &e));
    CHECK(settings::check(*settings::find("station"), "ab1-x", &n, &e) && n == "AB1-X");
    CHECK(!settings::check(*settings::find("station"), "ALL", &n, &e));

    // ---- args, the operator file and its overlay -------------------------------
    auto m = settings::args_map({"--station", "CFG", "--no-acks", "--tx-arg", "a", "--tx-arg", "b", "--check"});
    CHECK(m["station"] == "CFG" && m["no-acks"] == "yes" && m["tx-arg"] == "a b" && !m.count("check"));
    {
        std::ofstream f(settings::operator_file(db.string()));
        f << "# ours\nmax-attempts = \"5\"\nno-acks = no\ndb-dir = /elsewhere\nworld = narnia\nfrequency = 1\nnot a line\n";
    }
    std::vector<std::string> skipped;
    auto op = settings::load_operator(db.string(), &skipped);
    CHECK(op.size() == 2 && op["max-attempts"] == "5" && op["no-acks"] == "no");
    CHECK(skipped.size() == 4);  // a locked key, a bad value, no such key, no "="
    auto ov = settings::overlay_args(op);
    CHECK((ov == std::vector<std::string>{"--max-attempts", "5", "--acks"}));

    // ---- the calls ------------------------------------------------------------
    settings::Start st;
    st.db_dir = db.string();
    st.gale_dir = gale.string();
    st.config = {{"station", "CFG"}, {"api-port", "4100"}};
    st.op = op;
    settings::init(st);
    JsonValue v = call({{"cmd", "settings_list"}});
    CHECK(ok(v) && row(v, "max-attempts") && str(*row(v, "max-attempts"), "source") == "operator");
    CHECK(str(*row(v, "station"), "value") == "CFG" && str(*row(v, "station"), "source") == "config");
    CHECK(!row(v, "api-port"));  // locked rows only with all=Y
    CHECK(row(v, "amateur-calls") && str(*row(v, "amateur-calls"), "call") == "identity_set");
    // No net_plans and no world file: an amateur station. No tx-world row.
    CHECK(str(*row(v, "world"), "value") == "amateur" && !row(v, "tx-world"));
    CHECK(str(*row(v, "cat-ptt"), "value") == "no" && str(*row(v, "engineering-readouts"), "value") == "no");
    v = call({{"cmd", "settings_list"}, {"all", "Y"}});
    CHECK(row(v, "api-port") && str(*row(v, "api-port"), "class") == "locked");
    v = call({{"cmd", "settings_list"}, {"group", "keying"}});
    CHECK(v.get("settings") && v.get("settings")->arr.size() == 2);

    // Host plumbing can't be set from the kiosk.
    v = call({{"cmd", "settings_set"}, {"key", "db-dir"}, {"value", "/tmp"}});
    CHECK(!ok(v) && str(v, "error").find("appliance.conf") != std::string::npos);
    CHECK(!ok(call({{"cmd", "settings_set"}, {"key", "nope"}, {"value", "1"}})));
    CHECK(!ok(call({{"cmd", "settings_set"}, {"key", "tx-gap"}, {"value", "999"}})));

    // A plain one: saved, applies at restart.
    v = call({{"cmd", "settings_set"}, {"key", "tx_gap"}, {"value", "9"}});
    CHECK(ok(v) && str(v, "value") == "9" && str(v, "applies") == "restart" && bl(v, "restart_needed"));
    CHECK(slurp(settings::operator_file(db.string())).find("tx-gap = \"9\"") != std::string::npos);
    {
        struct stat sb {};
        CHECK(::stat(settings::operator_file(db.string()).c_str(), &sb) == 0 && (sb.st_mode & 0777) == 0600);
    }
    v = call({{"cmd", "settings_reset"}, {"key", "tx-gap"}});
    CHECK(ok(v) && str(v, "value") == "6" && !bl(v, "restart_needed"));

    // ptt-line: applies now, into the keying file.
    v = call({{"cmd", "settings_set"}, {"key", "ptt_line"}, {"value", "dtr"}});
    CHECK(ok(v) && str(v, "value") == "DTR" && str(v, "applies") == "now");
    JsonValue k;
    CHECK(parse_json(slurp(gale / "keying.json"), k) && str(k, "ptt_line") == "DTR" && !bl(k, "cat_ptt"));
    CHECK(str(k, "world") == "amateur" && !k.get("tx_world") && str(k, "station") == "CFG");

    // cat-ptt on: the kill-switch warning, and the operator's yes.
    v = call({{"cmd", "settings_set"}, {"key", "cat_ptt"}, {"value", "yes"}});
    CHECK(!ok(v) && bl(v, "confirm_needed") && str(v, "warning").find("kill switch") != std::string::npos);
    CHECK(settings::value("cat-ptt") == "no");
    v = call({{"cmd", "settings_set"}, {"key", "cat_ptt"}, {"value", "yes"}, {"confirm", "Y"}});
    CHECK(ok(v) && settings::value("cat-ptt") == "yes");
    CHECK(parse_json(slurp(gale / "keying.json"), k) && bl(k, "cat_ptt"));
    CHECK(ok(call({{"cmd", "settings_set"}, {"key", "cat_ptt"}, {"value", "no"}})));  // off needs no yes

    // world: needs a yes, writes the scanner's world file. No tx-world follows it.
    v = call({{"cmd", "settings_set"}, {"key", "world"}, {"value", "both"}});
    CHECK(!ok(v) && bl(v, "confirm_needed"));
    v = call({{"cmd", "settings_set"}, {"key", "world"}, {"value", "both"}, {"confirm", "Y"}});
    CHECK(ok(v) && slurp(gale / "world") == "both\n");
    v = call({{"cmd", "settings_set"}, {"key", "world"}, {"value", "amateur"}, {"confirm", "Y"}});
    CHECK(ok(v) && !v.get("also_set"));
    v = call({{"cmd", "settings_set"}, {"key", "tx-world"}, {"value", "mars"}, {"confirm", "Y"}});
    CHECK(!ok(v));  // retired: no such setting
    CHECK(parse_json(slurp(gale / "keying.json"), k) && !k.get("tx_world"));

    // The keying call: the same object as the file.
    v = call({{"cmd", "keying_settings"}});
    CHECK(ok(v) && v.get("keying") && str(*v.get("keying"), "ptt_line") == "DTR" && str(*v.get("keying"), "world") == "amateur");

    // The station call: a yes, new keys, applies at the next start.
    v = call({{"cmd", "settings_set"}, {"key", "station"}, {"value", "new-1"}});
    CHECK(!ok(v) && bl(v, "confirm_needed"));
    v = call({{"cmd", "settings_set"}, {"key", "station"}, {"value", "new-1"}, {"confirm", "Y"}});
    CHECK(ok(v) && str(v, "value") == "NEW-1" && bl(v, "keys_made") && !str(v, "fingerprint").empty());
    CHECK(fs::exists(db / "NEW-1.id") && bl(v, "restart_needed"));

    // The whole file read back at the next start.
    skipped.clear();
    op = settings::load_operator(db.string(), &skipped);
    CHECK(skipped.empty() && op["station"] == "NEW-1" && op["world"] == "amateur" && op["ptt-line"] == "DTR");
    ov = settings::overlay_args(op);
    bool has_station = false;
    for (size_t i = 0; i + 1 < ov.size(); ++i) has_station = has_station || (ov[i] == "--station" && ov[i + 1] == "NEW-1");
    CHECK(has_station);

    fs::remove_all(dir);
    return check::report("test_settings");
}
