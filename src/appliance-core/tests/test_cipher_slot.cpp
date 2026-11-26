// The cipher slot (docs/cipher-slot.md): the harness on our own module, a
// toy text-form module, a broken one and the template; installing and
// pinning a module; tapes for another cipher; and a whole net that runs a
// module handed out in the handout packet. The toy module is a test fixture,
// not a cipher; calls and keys are made up and thrown away.
#include "../src/api_server.h"
#include "../src/cipher_check.h"
#include "../src/cipher_slot.h"
#include "../src/codress.h"
#include "../src/core.h"
#include "../src/crypto.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "../src/received.h"
#include "../src/station.h"
#include "check.h"
#include "services_fixture.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static const std::string kBuild = PNA_BUILD_DIR;

static bool wait_for(const std::function<bool()>& cond, int ms = 5000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (c == '\n') {
            o += "\\n";
            continue;
        }
        if (c == '\r') {
            o += "\\r";
            continue;
        }
        o += c;
    }
    return o;
}
static JsonValue call(Core& c, std::initializer_list<std::pair<std::string, std::string>> kv) {
    std::string line = "{";
    bool first = true;
    for (auto& [k, v] : kv) {
        if (!first) line += ",";
        first = false;
        line += "\"" + k + "\":\"" + esc(v) + "\"";
    }
    line += "}";
    JsonValue out;
    parse_json(ApiServer::handle(c, line), out);
    return out;
}
static bool ok(const JsonValue& v) {
    const JsonValue* o = v.get("ok");
    return o && o->type == JsonValue::Type::Bool && o->b;
}
static bool yes(const JsonValue& v, const char* k) {
    const JsonValue* o = v.get(k);
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

static void write_file(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}
static std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}
// A module folder: module.so copied from the build.
static fs::path module_folder(const fs::path& dir, const std::string& name, const std::string& so) {
    fs::create_directories(dir / name);
    fs::copy_file(kBuild + "/" + so, dir / name / "module.so", fs::copy_options::overwrite_existing);
    return dir / name;
}

static int count_of(const std::vector<cipher::Check>& v, cipher::Result r) {
    int n = 0;
    for (auto& c : v) n += c.result == r;
    return n;
}

// 1. The harness.
static void harness() {
    auto b = cipher::Slot::builtin();
    CHECK(b->usable() && b->is_builtin() && b->info().name == "pna-aes" && b->info().key_bytes == 32);
    auto checks = cipher::run_checks(*b);
    CHECK(cipher::passed(checks) && count_of(checks, cipher::Result::warn) == 0 && checks.size() >= 12);

    std::string err;
    auto so = cipher::Slot::open(kBuild + "/pna-aes.so", &err);
    CHECK(so && so->usable() && !so->is_builtin() == false);  // same name, no set hash: reads as our own
    CHECK(so && cipher::passed(cipher::run_checks(*so)));

    auto toy = cipher::Slot::open(kBuild + "/test_text_module.so", &err);
    CHECK(toy && toy->info().text && toy->info().key_bytes == 16 && toy->info().name == "toy-text");
    CHECK(toy && cipher::passed(cipher::run_checks(*toy)));

    auto broken = cipher::Slot::open(kBuild + "/test_broken_module.so", &err);
    CHECK(broken);
    if (broken) {
        auto bc = cipher::run_checks(*broken);
        CHECK(!cipher::passed(bc));
        bool keys = false, clear = false, wrong = false, room = false;
        for (auto& c : bc) {
            keys = keys || (c.what == "bad keys are refused" && c.result == cipher::Result::fail);
            clear = clear || (c.what == "messages round-trip" && c.result == cipher::Result::fail);
            wrong = wrong || (c.what == "a wrong key is refused" && c.result == cipher::Result::fail);
            room = room || (c.what == "a small buffer is not overrun" && c.result == cipher::Result::fail);
        }
        CHECK(keys && clear && wrong && room);
    }
    auto tmpl = cipher::Slot::open(kBuild + "/test_template_module.so", &err);
    CHECK(tmpl && tmpl->info().name == "my-cipher" && !cipher::passed(cipher::run_checks(*tmpl)));

    // Not a module at all.
    fs::path junk = fs::temp_directory_path() / ("pna_junk_" + std::to_string(::getpid()) + ".so");
    write_file(junk, "not a library");
    CHECK(!cipher::Slot::open(junk.string(), &err) && err.find("won't load") != std::string::npos);
    fs::remove(junk);
    CHECK(!cipher::Slot::open(kBuild + "/no-such.so", &err));

    // The command-line harness: exit 0 for a good module, 1 for a broken one.
    CHECK(std::system((kBuild + "/pna-cipher-check --builtin > /dev/null").c_str()) == 0);
    CHECK(std::system((kBuild + "/pna-cipher-check " + kBuild + "/test_text_module.so > /dev/null").c_str()) == 0);
    CHECK(std::system((kBuild + "/pna-cipher-check " + kBuild + "/test_broken_module.so > /dev/null").c_str()) != 0);
}

// 2. A module that writes its own text, through a whole codress message.
static void text_form() {
    std::string err;
    auto toy = cipher::Slot::open(kBuild + "/test_text_module.so", &err);
    CHECK(toy);
    if (!toy) return;
    std::string key = crypto::random_bytes(16);
    std::string p =
        "VZCZCMMM001\nPP UHAAAA\nDE NCS-A 001 3271040\nZNR UUUUU\nP 231040Z NOV 2026\nFM MARS STATION NCS-A\n"
        "TO STA-B\nBT\nUNCLAS\nTHIS IS A TEST OF THE SLOT\nBT\n#0001\nNNNN";
    std::string c, back;
    CHECK(codress::encipher(*toy, p, "QXR01", key, c, &err));
    CHECK(codress::indicator_of(c) == "QXR01" && c.find("STA-B") == std::string::npos);
    // The toy's groups are letters A-P, five to a group, as it wrote them.
    size_t bt = c.find("\nBT\n") + 4;
    CHECK(c.substr(bt, 5).find_first_not_of("ABCDEFGHIJKLMNOP") == std::string::npos && c[bt + 5] == ' ');
    received::Message rm;
    CHECK(received::parse(c, rm, &err) && rm.enciphered && rm.key_indicator == "QXR01");
    CHECK(codress::decipher(*toy, c, key, back, &err));
    CHECK(back.find("THIS IS A TEST OF THE SLOT") != std::string::npos && back.find("TO STA-B") != std::string::npos);
    // Our own cipher can't read it, and the toy can't read ours.
    CHECK(!codress::decipher(c, crypto::random_bytes(32), back, &err));
    std::string ours;
    CHECK(codress::encipher(p, "QXR01", crypto::random_bytes(32), ours, &err));
    CHECK(!codress::decipher(*toy, ours, key, back, &err));
    // An empty slot refuses, with its reason.
    auto none = cipher::Slot::unusable("the module changed");
    CHECK(!codress::encipher(*none, p, "QXR01", key, c, &err) && err == "the module changed");
}

// 3. Installing, pinning and loading at startup.
static void installing(const fs::path& dir) {
    std::string err;
    fs::path toy = module_folder(dir, "toy", "test_text_module.so");
    write_file(toy / "lib" / "libhelper.txt", "a file the module needs");
    std::string h = cipher::set_hash(toy.string(), &err);
    CHECK(h.size() == 64);
    // The set hash covers lib/ too, and its names.
    write_file(toy / "lib" / "libhelper.txt", "a changed file");
    CHECK(cipher::set_hash(toy.string(), &err) != h);
    write_file(toy / "lib" / "libhelper.txt", "a file the module needs");
    CHECK(cipher::set_hash(toy.string(), &err) == h);
    fs::create_symlink("/etc/hostname", toy / "lib" / "link");
    CHECK(cipher::set_hash(toy.string(), &err).empty() && err.find("not a plain file") != std::string::npos);
    fs::remove(toy / "lib" / "link");

    fs::path slot_dir = dir / "STA-B.cipher";
    // A hash that isn't the module's: refused.
    CHECK(!cipher::install(toy.string(), slot_dir.string(), std::string(64, 'a'), &err) &&
          err.find("not the one the handout names") != std::string::npos);
    // A broken module: refused by the checks.
    fs::path broken = module_folder(dir, "broken", "test_broken_module.so");
    CHECK(!cipher::install(broken.string(), slot_dir.string(), "", &err) &&
          err.find("failed the slot's checks") != std::string::npos);
    // Our own module may not be installed under its own name.
    fs::path ours = module_folder(dir, "ours", "pna-aes.so");
    CHECK(!cipher::install(ours.string(), slot_dir.string(), "", &err) && err.find("built-in") != std::string::npos);
    CHECK(!fs::exists(slot_dir));
    // No pin: the built-in cipher at startup.
    CHECK(cipher::load_installed(slot_dir.string())->is_builtin());

    auto s = cipher::install(toy.string(), slot_dir.string(), h, &err);
    CHECK(s && s->info().name == "toy-text" && s->info().set_hash == h && !s->is_builtin());
    CHECK(fs::exists(slot_dir / "pin") && fs::exists(slot_dir / "lib" / "libhelper.txt"));
    auto at_start = cipher::load_installed(slot_dir.string());
    CHECK(at_start->usable() && at_start->info().name == "toy-text" && at_start->info().set_hash == h);
    // A changed file: codress off, never a quiet fall back.
    write_file(slot_dir / "lib" / "libhelper.txt", "swapped");
    auto changed = cipher::load_installed(slot_dir.string());
    CHECK(!changed->usable() && changed->why_unusable().find("changed since it was installed") != std::string::npos);
    CHECK(changed->info().name == "toy-text");
    CHECK(cipher::remove_installed(slot_dir.string(), &err) && !fs::exists(slot_dir));
    CHECK(cipher::load_installed(slot_dir.string())->is_builtin());
}

// 4. Tapes and the keyring for another cipher.
static void tapes(const fs::path& dir) {
    std::string err;
    keys::Tape t;
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    CHECK(keys::make_tape("TOY NET", now - now % 86400, 5, 24, now, t, &err, "toy-text", 16));
    CHECK(t.cipher == "toy-text" && t.keys.size() == 5 && t.keys[0].secret.size() == 16);
    std::string text = keys::format_tape(t);
    CHECK(text.find("PNA KEY TAPE 2\n") == 0 && text.find("\ncipher toy-text\n") != std::string::npos);
    keys::Tape back;
    CHECK(keys::parse_tape(keys::tape_file(text), back, &err) && back.cipher == "toy-text" &&
          back.keys[1].secret == t.keys[1].secret && back.keys[1].cipher == "toy-text");
    // Our own tapes stay version 1, with no cipher line.
    keys::Tape ours;
    CHECK(keys::make_tape("PRACTICE NET", now - now % 86400, 2, 24, now, ours, &err, "PNA-AES", 16));
    CHECK(ours.cipher.empty() && ours.keys[0].secret.size() == 32);
    CHECK(keys::format_tape(ours).find("PNA KEY TAPE 1\n") == 0);
    // A version 2 tape without its cipher line is refused.
    std::string bare = text;
    bare.erase(bare.find("cipher toy-text\n"), 16);
    CHECK(!keys::parse_tape(bare, back, &err));

    std::string err2;
    auto toy = cipher::Slot::open(kBuild + "/test_text_module.so", &err2);
    CHECK(toy);
    if (!toy) return;
    auto kr = std::make_shared<keys::Keyring>((dir / "TST.keys").string());
    keys::ImportResult r;
    // A keyring on our own cipher refuses the toy's tape, by name.
    CHECK(!kr->import_tape(text, r, &err) && err.find("toy-text") != std::string::npos &&
          err.find("pna-aes") != std::string::npos);
    auto accept = [toy](const std::string& i, const std::string& k, std::string* e) {
        std::string ready;
        bool okk = toy->load_key(i, k, ready, e);
        crypto::wipe(ready);
        return okk;
    };
    auto check = [toy](const std::string& i, const std::string& k) {
        std::string c;
        toy->key_check(i, k, c, nullptr);
        return c;
    };
    kr->set_cipher("toy-text", accept, check);
    CHECK(kr->import_tape(text, r, &err) && r.added == 5);
    auto l = kr->list();
    CHECK(l.size() == 5 && l[0].cipher == "toy-text" && l[0].usable && l[0].check.size() == 6);
    std::string key;
    CHECK(kr->send_key(l[0].indicator, key, &err) && key.size() == 16);
    // The cipher is kept with each key across a restart.
    auto kr2 = std::make_shared<keys::Keyring>((dir / "TST.keys").string());
    CHECK(kr2->load(&err));
    kr2->set_cipher("toy-text", accept, check);
    CHECK(kr2->list().size() == 5 && kr2->send_key(l[0].indicator, key, &err));
    // Back on our own cipher: the toy's keys are held but not used.
    kr2->set_cipher("pna-aes", nullptr, nullptr);
    CHECK(!kr2->send_key(l[0].indicator, key, &err) && err.find("not in this appliance's cipher slot") != std::string::npos);
    CHECK(!kr2->receive_key(l[0].indicator, key) && kr2->for_sending().empty() && !kr2->list()[0].usable);
    // A key the module refuses keeps the whole tape out.
    keys::Tape wrong;
    CHECK(keys::make_tape("TOY NET", now - now % 86400, 2, 24, now, wrong, &err, "toy-text", 17));
    auto kr3 = std::make_shared<keys::Keyring>((dir / "TST3.keys").string());
    kr3->set_cipher("toy-text", accept, check);
    CHECK(!kr3->import_tape(keys::format_tape(wrong), r, &err) && err.find("refused") != std::string::npos);
    for (auto& k : t.keys) crypto::wipe(k.secret);
}

// 5. A whole net on a module from the handout packet.
struct Box {
    std::unique_ptr<Core> core;
    std::shared_ptr<keys::Keyring> keys;
    std::string fp;
};
static Box make_box(const fs::path& dir, const std::string& call, std::unique_ptr<ModemDriver> modem) {
    Box b;
    fs::create_directories(dir);
    std::string base = (dir / call).string();
    b.core = std::make_unique<Core>(CoreConfig{call, base + ".db", 3, 1, true}, std::move(modem));
    b.core->set_services(test_services());
    b.keys = std::make_shared<keys::Keyring>(base + ".keys");
    std::string err;
    b.keys->load(&err);
    b.core->set_keyring(b.keys);
    b.core->set_cipher_dir(base + ".cipher");
    b.core->set_cipher(cipher::load_installed(base + ".cipher"));
    auto id = std::make_shared<station::Identity>();
    CHECK(station::make_identity(call, static_cast<int64_t>(std::time(nullptr)), *id, &err));
    CHECK(station::save_identity(base + ".id", *id, &err));
    b.fp = station::fingerprint(station::card_of(*id));
    b.core->set_identity(id, base + ".id");
    b.core->start();
    return b;
}

static void net(const fs::path& dir) {
    fs::path toy = module_folder(dir, "toy", "test_text_module.so");
    write_file(toy / "ABOUT.txt", "TOY TEXT MODULE FOR THE TESTS. NOT A CIPHER.\n");
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    Box A = make_box(dir / "net", "NCS-A", std::move(ma));
    Box B = make_box(dir / "net", "STA-B", std::move(mb));

    // The keymaster puts the net's module in its slot.
    JsonValue r = call(*A.core, {{"cmd", "cipher_status"}});
    CHECK(ok(r) && str(r, "name") == "pna-aes" && yes(r, "builtin"));
    r = call(*A.core, {{"cmd", "cipher_install"}, {"path", (dir / "broken-nothing").string()}});
    CHECK(!ok(r));
    r = call(*A.core, {{"cmd", "cipher_install"}, {"path", toy.string()}});
    CHECK(ok(r) && str(r, "name") == "toy-text" && str(r, "output") == "text" && num(r, "key_bytes") == 16);
    const std::string hash = str(r, "set_hash");
    CHECK(hash.size() == 64);

    // B's card to the keymaster, read back by voice; then the tape and handout.
    CHECK(ok(call(*A.core, {{"cmd", "card_import"}, {"text", str(call(*B.core, {{"cmd", "station_card"}}), "card")}})));
    CHECK(ok(call(*A.core, {{"cmd", "card_check"}, {"station", "STA-B"}, {"fingerprint", B.fp}})));
    r = call(*A.core, {{"cmd", "net_tape"}, {"service", "mars"}, {"net", "Toy Net"}, {"days", "7"}, {"members", "STA-B"}, {"handouts", "Y"}});
    CHECK(ok(r) && r.get("handouts") && r.get("handouts")->arr.size() == 1);
    std::string hand = r.get("handouts") ? str(r.get("handouts")->arr[0], "text") : "";
    CHECK(hand.find("\nCIPHER TOY-TEXT ") != std::string::npos);
    CHECK(hand.find("THIS NET USES THE TOY-TEXT CIPHER") != std::string::npos);
    std::string upper_hash = hash;
    for (auto& c : upper_hash) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    CHECK(hand.find(upper_hash) != std::string::npos);
    write_file(dir / "hand-b.txt", hand);

    // The keymaster tool builds the packet; it refuses another module.
    fs::path packet = dir / "packet-sta-b";
    fs::path other = module_folder(dir, "other", "pna-aes.so");
    CHECK(std::system((kBuild + "/pna-keymaster packet --handout " + (dir / "hand-b.txt").string() + " --cipher " +
                       other.string() + " --out " + (dir / "bad-packet").string() + " > /dev/null 2>&1")
                          .c_str()) != 0);
    CHECK(!fs::exists(dir / "bad-packet"));
    CHECK(std::system((kBuild + "/pna-keymaster packet --handout " + (dir / "hand-b.txt").string() + " --cipher " +
                       toy.string() + " --out " + packet.string() + " > /dev/null")
                          .c_str()) == 0);
    CHECK(fs::exists(packet / "handout.txt") && fs::exists(packet / "cipher" / "module.so") &&
          fs::exists(packet / "cipher" / "ABOUT.txt"));
    CHECK(read_file(packet / "cipher" / "module.sha256") == hash + "\n");

    // Until B accepts NCS-A as its keymaster, nothing from the handout is
    // taken: not the tape, not the cipher module.
    r = call(*B.core, {{"cmd", "handout_import"}, {"packet", packet.string()}});
    CHECK(ok(r) && yes(r, "waiting_on_member") && !yes(r, "cipher_installed") && !yes(r, "tape_opened"));
    CHECK(B.core->cipher()->is_builtin());
    r = call(*B.core, {{"cmd", "handout_import"}, {"text", hand}});
    CHECK(ok(r) && yes(r, "waiting_on_member"));
    r = call(*B.core, {{"cmd", "keymaster_accept"}, {"station", "NCS-A"}, {"fingerprint", A.fp}});
    CHECK(ok(r) && r.get("handouts")->arr.size() == 1 &&
          str(r.get("handouts")->arr[0], "next").find("whole packet folder") != std::string::npos);
    CHECK(B.core->cipher()->is_builtin());

    // The page alone: B is told to load the whole packet; nothing installed.
    r = call(*B.core, {{"cmd", "handout_import"}, {"text", hand}});
    CHECK(ok(r) && !yes(r, "tape_opened") && !yes(r, "cipher_installed") &&
          str(r, "next").find("whole packet folder") != std::string::npos);
    // A packet whose module was swapped: refused, the slot stays as it was.
    fs::path swapped = dir / "packet-swapped";
    fs::copy(packet, swapped, fs::copy_options::recursive);
    fs::copy_file(kBuild + "/pna-aes.so", swapped / "cipher" / "module.so", fs::copy_options::overwrite_existing);
    r = call(*B.core, {{"cmd", "handout_import"}, {"packet", swapped.string()}});
    CHECK(ok(r) && !yes(r, "cipher_installed") && !yes(r, "tape_opened") &&
          str(r, "next").find("not the one the handout names") != std::string::npos);
    CHECK(B.core->cipher()->is_builtin());
    // The real packet: module installed, tape opened, one load.
    r = call(*B.core, {{"cmd", "handout_import"}, {"packet", packet.string()}});
    CHECK(ok(r) && yes(r, "cipher_installed") && yes(r, "tape_opened") && num(r, "added") == 7);
    CHECK(str(r, "cipher") == "toy-text" && B.core->cipher()->info().name == "toy-text");
    // The checks both stations read by voice match.
    CHECK(!B.keys->for_sending().empty() && B.keys->for_sending().at(0).check == A.keys->for_sending().at(0).check);
    CHECK(B.keys->for_sending().at(0).check.size() == 6);

    // Codress on the toy cipher, over the air.
    std::string today = B.keys->for_sending().at(0).indicator;
    r = call(*A.core, {{"cmd", "compose_send"}, {"service", "mars"},
                       {"template", "FREE_FORM"},
                       {"procedure", "ACP127_CODRESS"},
                       {"key", today},
                       {"prec", "P"},
                       {"routing", "UHAAAA"},
                       {"originator", "NCS-A"},
                       {"from", "MARS STATION NCS-A"},
                       {"to", "UHAAAA/STA-B"},
                       {"to_station", "STA-B"},
                       {"text", "TOY NET IS UP"}});
    CHECK(ok(r));
    std::string id = str(r, "id"), onair = str(r, "message");
    CHECK(onair.find("TOY NET IS UP") == std::string::npos && onair.find(today) != std::string::npos);
    CHECK(wait_for([&] { return B.core->get(id).has_value(); }));
    r = call(*B.core, {{"cmd", "parse_traffic"}, {"id", id}});
    CHECK(ok(r) && yes(r, "deciphered") && str(r, "text").find("TOY NET IS UP") != std::string::npos);

    // Startup on B: the installed module fills the slot again.
    auto again = cipher::load_installed((dir / "net" / "STA-B.cipher").string());
    CHECK(again->usable() && again->info().name == "toy-text" && again->info().set_hash == hash);
    // Removing it puts our own cipher back; the toy's keys stay held but unused.
    r = call(*B.core, {{"cmd", "cipher_remove"}});
    CHECK(ok(r) && yes(r, "builtin"));
    CHECK(B.keys->for_sending().empty());
    r = call(*B.core, {{"cmd", "keys_list"}});
    CHECK(ok(r) && r.get("keys") && !r.get("keys")->arr.empty() && !yes(r.get("keys")->arr[0], "usable"));
    A.core->stop();
    B.core->stop();
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_cipher_slot_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    harness();
    text_form();
    installing(dir / "install");
    tapes(dir);
    net(dir / "whole");
    fs::remove_all(dir);
    return check::report("test_cipher_slot");
}
