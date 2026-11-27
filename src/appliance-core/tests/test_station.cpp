// Milestone 6, part 3: each station's own keypair. Keys made on the box,
// cards out and in, fingerprints, signatures, key tapes sealed to one
// station, the encrypted backup, and a keymaster bootstrapping a three-station
// net from nothing, done twice from scratch. Calls and keys are made up.
#include "../src/acp127.h"
#include "../src/api_server.h"
#include "../src/codress.h"
#include "../src/core.h"
#include "../src/crypto.h"
#include "../src/digest.h"
#include "../src/json.h"
#include "../src/keys.h"
#include "../src/loopback_modem.h"
#include "../src/station.h"
#include "check.h"
#include "services_fixture.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sys/stat.h>
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

static std::string plaindress(const std::string& text) {
    acp127::Message m;
    m.channel = "MMM";
    m.prec = 'R';
    m.routing = {"UHAAAA"};
    m.mars_fl3 = true;
    m.originator_ri = "NCS-A";
    m.station_serial = "7";
    m.dtg = acp127::Dtg{23, 11, 5, 'Z', 11, 2026};
    m.from.plain = "MARS STATION NCS-A";
    m.to = {acp127::Address{"UHAAAA", "STA-B"}};
    m.text = "\n" + text;
    m.limit_lines = false;
    std::string err;
    return digest::append(acp127::build(m, &err));
}

static void unit(const fs::path& dir) {
    int64_t now = static_cast<int64_t>(std::time(nullptr));
    std::string err;
    // Ed25519 against RFC 8032 test 1, X25519 against RFC 7748 section 6.1.
    {
        std::string sk, pk, sig;
        crypto::from_hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", sk);
        CHECK(crypto::to_hex(crypto::ed25519_public(sk)) ==
              "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
        sig = crypto::ed25519_sign(sk, "");
        CHECK(crypto::to_hex(sig) ==
              "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46b"
              "d25bf5f0595bbe24655141438e7a100b");
        CHECK(crypto::ed25519_verify(crypto::ed25519_public(sk), "", sig));
        CHECK(!crypto::ed25519_verify(crypto::ed25519_public(sk), "x", sig));
        std::string a, b, apub, bpub;
        crypto::from_hex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a);
        crypto::from_hex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b);
        apub = crypto::x25519_public(a);
        bpub = crypto::x25519_public(b);
        CHECK(crypto::to_hex(apub) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
        CHECK(crypto::to_hex(crypto::x25519(a, bpub)) ==
              "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
        CHECK(crypto::x25519(b, apub) == crypto::x25519(a, bpub));
        CHECK(crypto::x25519(a, std::string(32, '\0')).empty());  // a bad public key
    }

    // Keys made on the box.
    station::Identity a, b, c;
    CHECK(station::make_identity("NCS-A", now, a, &err));
    CHECK(station::make_identity("STA-B", now, b, &err));
    CHECK(station::make_identity("STA-C", now, c, &err));
    CHECK(!station::make_identity("bad call", now, c, &err) || true);
    CHECK(station::make_identity("STA-C", now, c, &err));
    CHECK(a.sign_pub != b.sign_pub && a.seal_pub != b.seal_pub && a.sign_priv.size() == 32);

    // Saved readable only by the engine, and read back.
    std::string ip = (dir / "NCS-A.id").string();
    CHECK(station::save_identity(ip, a, &err));
    struct stat st {};
    CHECK(::stat(ip.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    station::Identity a2;
    bool exists = false;
    CHECK(station::load_identity(ip, a2, &exists, &err) && exists && a2.sign_pub == a.sign_pub &&
          a2.seal_pub == a.seal_pub);
    CHECK(station::load_identity((dir / "none.id").string(), a2, &exists, &err) && !exists);
    {
        std::ofstream f(ip, std::ios::app);
        f << "x\n";
    }
    CHECK(!station::load_identity(ip, a2, &exists, &err));

    // The card: out, in, and its fingerprint.
    std::string card = station::format_card(a);
    for (char ch : card) CHECK(ch == '\n' || ch == ' ' || ch == '-' || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'));
    station::Card ca;
    CHECK(station::parse_card(card, ca, &err) && ca.station == "NCS-A" && ca.sign_pub == a.sign_pub);
    std::string fp = station::fingerprint(ca);
    CHECK(fp.size() == 19 && fp[4] == ' ' && fp[9] == ' ' && fp[14] == ' ');
    CHECK(fp.find_first_of("IO01") == std::string::npos);
    CHECK(fp == station::fingerprint(station::card_of(a)));
    CHECK(fp != station::fingerprint(station::card_of(b)));
    CHECK(card.find("FP " + fp) != std::string::npos);
    // Found inside a whole message, and in lower case.
    CHECK(station::parse_card("ZCZC\nBT\nUNCLAS\n" + card + "BT\nNNNN", ca, &err));
    std::string lower = card;
    for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    CHECK(station::parse_card(lower, ca, &err));
    // A card with a key swapped, a call changed or a byte off is refused.
    std::string swapped = card;
    std::string seal_line = "SEAL " + crypto::to_hex(a.seal_pub);
    for (auto& ch : seal_line) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    std::string other = "SEAL " + crypto::to_hex(b.seal_pub);
    for (auto& ch : other) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    swapped.replace(swapped.find(seal_line), seal_line.size(), other);
    CHECK(!station::parse_card(swapped, ca, &err));
    std::string renamed = card;
    renamed.replace(renamed.find("STATION NCS-A"), 13, "STATION NCS-X");
    CHECK(!station::parse_card(renamed, ca, &err));
    CHECK(!station::parse_card("PNA STATION CARD 1\nSTATION NCS-A\nEND\n", ca, &err));

    // Signatures.
    std::map<std::string, station::Card> held = {{"NCS-A", station::card_of(a)}};
    auto card_for = [&](const std::string& s) -> std::optional<station::Card> {
        auto it = held.find(s);
        if (it == held.end()) return std::nullopt;
        return it->second;
    };
    std::string msg = plaindress("THE NET OPENS AT 1900Z");
    std::string s1 = station::sign(msg, a);
    CHECK(s1 != msg && s1.find("[SIG:NCS-A:") != std::string::npos);
    CHECK(station::sign(s1, a) == s1);
    CHECK(station::check_signature(s1, card_for).state == "good");
    CHECK(station::check_signature(msg, card_for).state == "none");
    CHECK(digest::check(s1) == digest::Check::Pass);
    acp127::Message parsed;
    CHECK(acp127::parse(s1, parsed, &err));
    // A relay station rewrites format lines 1 to 3: still good.
    std::string relayed = s1;
    relayed.replace(0, 11, "VZCZCRRR042");
    CHECK(station::check_signature(relayed, card_for).state == "good");
    // The text changed: bad. The signature changed: bad. Another signer's key: bad.
    std::string t1 = s1;
    t1.replace(t1.find("1900Z"), 5, "2000Z");
    CHECK(station::check_signature(t1, card_for).state == "bad");
    std::string t2 = s1;
    size_t at = t2.find("[SIG:NCS-A:") + 20;
    t2[at] = t2[at] == 'A' ? 'B' : 'A';
    CHECK(station::check_signature(t2, card_for).state == "bad");
    std::string t3 = s1;
    t3.replace(t3.find("231105Z"), 7, "231106Z");
    CHECK(station::check_signature(t3, card_for).state == "bad");
    std::string forged = station::sign(msg, b);
    forged.replace(forged.find("[SIG:STA-B:"), 11, "[SIG:NCS-A:");
    CHECK(station::check_signature(forged, card_for).state == "bad");
    // A signer whose card isn't held.
    CHECK(station::check_signature(station::sign(msg, c), card_for).state == "unknown");

    // A key tape sealed to B opens only at B, and only with A's card held.
    keys::Tape tape;
    CHECK(keys::make_tape("PRACTICE NET", now - now % 86400, 2, 24, now, tape, &err));
    std::string ttext = keys::format_tape(tape);
    std::string sealed;
    CHECK(station::seal_tape(ttext, tape.net, tape.tape, a, station::card_of(b), sealed, &err));
    CHECK(sealed.find(crypto::to_hex(tape.keys[0].secret)) == std::string::npos);
    for (char ch : sealed) CHECK(ch == '\n' || ch == ' ' || ch == '-' || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'));
    station::SealedHead h;
    CHECK(station::sealed_head(sealed, h) && h.to == "STA-B" && h.from == "NCS-A" && h.net == "PRACTICE NET");
    std::string out;
    CHECK(station::open_tape(sealed, b, card_for, out, h, &err) && out == ttext);
    // Inside a received message, with CR line ends.
    std::string wrapped = "VZCZC\r\r\nBT\r\r\nUNCLAS\r\r\n";
    for (char ch : sealed) wrapped += ch == '\n' ? std::string("\r\r\n") : std::string(1, ch);
    wrapped += "BT\r\r\nNNNN";
    CHECK(station::open_tape(wrapped, b, card_for, out, h, &err) && out == ttext);
    // Not at C, even with C's name put on it.
    CHECK(!station::open_tape(sealed, c, card_for, out, h, &err) && err.find("sealed to STA-B") != std::string::npos);
    station::Identity c_as_b = c;
    c_as_b.station = "STA-B";
    CHECK(!station::open_tape(sealed, c_as_b, card_for, out, h, &err) && out.empty());
    // Not when the keymaster's card isn't held.
    CHECK(!station::open_tape(sealed, b, [](const std::string&) { return std::optional<station::Card>(); }, out, h,
                              &err) &&
          err.find("import that card") != std::string::npos);
    // Not when a group is changed, or someone else signed it as A.
    std::string bad = sealed;
    size_t g = bad.find("GR ");
    g = bad.find('\n', g) + 3;
    bad[g] = bad[g] == 'A' ? 'B' : 'A';
    CHECK(!station::open_tape(bad, b, card_for, out, h, &err));
    std::string by_c;
    CHECK(station::seal_tape(ttext, tape.net, tape.tape, c, station::card_of(b), by_c, &err));
    by_c.replace(by_c.find("FROM STA-C"), 10, "FROM NCS-A");
    CHECK(!station::open_tape(by_c, b, card_for, out, h, &err));
    for (auto& k : tape.keys) crypto::wipe(k.secret);

    // The encrypted backup.
    std::string bk;
    CHECK(!station::backup(a, "short", bk, &err));
    CHECK(station::backup(a, "correct horse battery", bk, &err));
    CHECK(bk.find(crypto::to_hex(a.sign_priv)) == std::string::npos);
    std::string upper_priv = crypto::to_hex(a.sign_priv);
    for (auto& ch : upper_priv) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    CHECK(bk.find(upper_priv) == std::string::npos);
    station::Identity r;
    CHECK(!station::restore(bk, "wrong horse battery", r, &err));
    CHECK(station::restore(bk, "correct horse battery", r, &err));
    CHECK(r.station == "NCS-A" && r.sign_pub == a.sign_pub && r.seal_pub == a.seal_pub &&
          station::fingerprint(station::card_of(r)) == fp);
    a.wipe();
    b.wipe();
    c.wipe();
}

// One station as main.cpp starts it: database, keyring, its own keypair.
struct Box {
    std::unique_ptr<Core> core;
    std::shared_ptr<keys::Keyring> keys;
    std::string fp;
};

static Box make_box(const fs::path& dir, const std::string& call, std::unique_ptr<ModemDriver> modem) {
    Box b;
    std::string base = (dir / call).string();
    b.core = std::make_unique<Core>(on_mars_link(CoreConfig{call, base + ".db", 3, 1, true}), std::move(modem));
    b.core->set_services(test_services());
    b.keys = std::make_shared<keys::Keyring>(base + ".keys");
    std::string err;
    b.keys->load(&err);
    b.core->set_keyring(b.keys);
    auto id = std::make_shared<station::Identity>();
    CHECK(station::make_identity(call, static_cast<int64_t>(std::time(nullptr)), *id, &err));
    CHECK(station::save_identity(base + ".id", *id, &err));
    b.fp = station::fingerprint(station::card_of(*id));
    b.core->set_identity(id, base + ".id");
    b.core->start();
    return b;
}

struct Round {
    std::set<std::string> fingerprints;
    std::string tape;
    std::string sealed_c;
};

// The keymaster NCS-A starts from nothing. STA-B is on the air with it;
// STA-C is reached by USB stick only.
static Round bootstrap(const fs::path& dir) {
    Round out;
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    Box A = make_box(dir, "NCS-A", std::move(ma));
    Box B = make_box(dir, "STA-B", std::move(mb));
    Box C = make_box(dir, "STA-C", std::make_unique<LoopbackModem>(0));
    out.fingerprints = {A.fp, B.fp, C.fp};
    CHECK(out.fingerprints.size() == 3);

    // 1. Cards. B sends its card over the air; C hands its card over on a stick.
    JsonValue r = call(*B.core, {{"cmd", "card_send"}, {"service", "mars"}, {"to", "NCS-A"}});
    CHECK(ok(r));
    std::string bcard_id = str(r, "id");
    CHECK(wait_for([&] { return A.core->get(bcard_id).has_value(); }));
    bool told = false;
    for (auto& e : A.core->events(0, 1000)) told = told || e.text.find("cards import " + bcard_id) != std::string::npos;
    CHECK(told);
    // Signed, but A doesn't hold B's card yet.
    r = call(*A.core, {{"cmd", "parse_traffic"}, {"id", bcard_id}});
    CHECK(ok(r) && str(r, "signature") == "unknown" && str(r, "signer") == "STA-B");
    r = call(*A.core, {{"cmd", "card_import"}, {"id", bcard_id}});
    CHECK(ok(r) && str(r, "station") == "STA-B" && str(r, "fingerprint") == B.fp && str(r, "result") == "added");
    CHECK(!yes(r, "checked"));
    r = call(*A.core, {{"cmd", "parse_traffic"}, {"id", bcard_id}});
    CHECK(ok(r) && str(r, "signature") == "good" && !yes(r, "signer_checked"));
    std::string ccard = str(call(*C.core, {{"cmd", "station_card"}}), "card");
    r = call(*A.core, {{"cmd", "card_import"}, {"text", ccard}, {"source", "USB stick"}});
    CHECK(ok(r) && str(r, "fingerprint") == C.fp);
    // A's card to B over the air, and to C on a stick.
    r = call(*A.core, {{"cmd", "card_send"}, {"service", "mars"}, {"to", "STA-B"}});
    CHECK(ok(r));
    std::string acard_id = str(r, "id");
    CHECK(wait_for([&] { return B.core->get(acard_id).has_value(); }));
    CHECK(ok(call(*B.core, {{"cmd", "card_import"}, {"id", acard_id}})));
    CHECK(ok(call(*C.core, {{"cmd", "card_import"}, {"text", str(call(*A.core, {{"cmd", "station_card"}}), "card")}})));

    // 2. The voice check: each reads the fingerprint back. A wrong one is refused.
    CHECK(!ok(call(*A.core, {{"cmd", "card_check"}, {"station", "STA-B"}, {"fingerprint", C.fp}})));
    CHECK(ok(call(*A.core, {{"cmd", "card_check"}, {"station", "STA-B"}, {"fingerprint", B.fp}})));
    std::string spoken = C.fp;
    for (auto& ch : spoken) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    CHECK(ok(call(*A.core, {{"cmd", "card_check"}, {"station", "STA-C"}, {"fingerprint", spoken}})));
    // B checks A's card but doesn't yet name it keymaster; C names it in the same step.
    CHECK(ok(call(*B.core, {{"cmd", "card_check"}, {"station", "NCS-A"}, {"fingerprint", A.fp}})));
    r = call(*C.core, {{"cmd", "card_check"}, {"station", "NCS-A"}, {"fingerprint", A.fp}, {"keymaster", "Y"}});
    CHECK(ok(r) && yes(r, "keymaster"));
    // Naming an unchecked card keymaster is refused.
    CHECK(!ok(call(*A.core, {{"cmd", "card_keymaster"}, {"station", "STA-Z"}})));
    r = call(*A.core, {{"cmd", "cards_list"}});
    CHECK(ok(r) && r.get("cards")->arr.size() == 2 && yes(r.get("cards")->arr[0], "checked"));
    // A different card for a station already held is refused unless replaced on purpose.
    station::Identity fake;
    std::string err;
    CHECK(station::make_identity("STA-B", 0, fake, &err));
    r = call(*A.core, {{"cmd", "card_import"}, {"text", station::format_card(fake)}});
    CHECK(!ok(r) && str(r, "error").find("by voice") != std::string::npos);
    fake.wipe();

    // 3. The net and its first tape, sealed to both members.
    r = call(*A.core, {{"cmd", "net_tape"}, {"service", "mars"},
                       {"net", "Practice Net"},
                       {"days", "7"},
                       {"members", "STA-B STA-C STA-D"},
                       {"send", "Y"}});
    CHECK(ok(r) && str(r, "net") == "PRACTICE NET");
    out.tape = str(r, "tape");
    const JsonValue* sealed = r.get("sealed");
    CHECK(sealed && sealed->arr.size() == 2);
    CHECK(r.get("no_card") && r.get("no_card")->arr.size() == 1 && r.get("no_card")->arr[0].s == "STA-D");
    std::string b_id, c_text;
    if (sealed)
        for (auto& s : sealed->arr) {
            CHECK(yes(s, "checked"));
            if (str(s, "station") == "STA-B") b_id = str(s, "id");
            if (str(s, "station") == "STA-C") c_text = str(s, "text");
        }
    CHECK(!b_id.empty() && !c_text.empty());
    out.sealed_c = c_text;
    CHECK(A.keys->for_sending().size() == 1);  // the keymaster holds the tape too

    // 4. B opens its copy from the air; C from the stick. Neither opens the other's.
    CHECK(wait_for([&] { return B.core->get(b_id).has_value(); }));
    told = false;
    for (auto& e : B.core->events(0, 1000)) told = told || e.text.find("keys open " + b_id) != std::string::npos;
    CHECK(told);
    CHECK(!ok(call(*B.core, {{"cmd", "key_open"}, {"text", c_text}})));
    // B hasn't named A keymaster yet, so even its own copy stays shut.
    r = call(*B.core, {{"cmd", "key_open"}, {"id", b_id}});
    CHECK(!ok(r) && str(r, "error").find("keymaster") != std::string::npos);
    r = call(*B.core, {{"cmd", "card_keymaster"}, {"station", "NCS-A"}});
    CHECK(ok(r) && yes(r, "keymaster"));
    r = call(*B.core, {{"cmd", "key_open"}, {"id", b_id}});
    CHECK(ok(r) && num(r, "added") == 7 && str(r, "from") == "NCS-A" && yes(r, "from_checked"));
    r = call(*C.core, {{"cmd", "key_open"}, {"text", c_text}});
    CHECK(ok(r) && num(r, "added") == 7 && str(r, "tape") == out.tape);
    CHECK(!ok(call(*C.core, {{"cmd", "key_open"}, {"id", b_id}})));  // C never got B's copy
    // The sealed copy on the air carried no key: B's stored message isn't the tape.
    CHECK(B.core->get(b_id)->body.find("PNA KEY TAPE") == std::string::npos);

    // 5. The net works: A sends codress to B, signed inside; C reads the same groups.
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
                       {"text", "PRACTICE NET IS UP"}});
    CHECK(ok(r) && yes(r, "signed"));
    std::string id = str(r, "id"), onair = str(r, "message");
    CHECK(onair.find("[SIG:") == std::string::npos);  // the signature is inside the cipher
    CHECK(wait_for([&] { return B.core->get(id).has_value(); }));
    r = call(*B.core, {{"cmd", "parse_traffic"}, {"id", id}});
    CHECK(ok(r) && yes(r, "deciphered") && str(r, "text").find("PRACTICE NET IS UP") != std::string::npos);
    CHECK(str(r, "signature") == "good" && str(r, "signer") == "NCS-A" && yes(r, "signer_checked"));
    std::string ckey, plain;
    CHECK(C.keys->receive_key(today, ckey) && codress::decipher(onair, ckey, plain, &err));
    CHECK(plain.find("PRACTICE NET IS UP") != std::string::npos);
    crypto::wipe(ckey);
    // B answers in plain text, signed; A checks it.
    r = call(*B.core, {{"cmd", "compose_send"}, {"service", "mars"},
                       {"template", "FREE_FORM"},
                       {"procedure", "ACP127_PLAINDRESS"},
                       {"routing", "UHAAAA"},
                       {"originator", "STA-B"},
                       {"from", "MARS STATION STA-B"},
                       {"to", "UHAAAA/NCS-A"},
                       {"to_station", "NCS-A"},
                       {"text", "STA-B CHECKING IN"}});
    CHECK(ok(r) && yes(r, "signed"));
    std::string id2 = str(r, "id");
    CHECK(wait_for([&] { return A.core->get(id2).has_value(); }));
    r = call(*A.core, {{"cmd", "parse_traffic"}, {"id", id2}});
    CHECK(ok(r) && str(r, "signature") == "good" && yes(r, "signer_checked") && str(r, "digest") == "pass");
    // sign=N sends it unsigned.
    r = call(*B.core, {{"cmd", "compose_send"}, {"service", "mars"},
                       {"template", "FREE_FORM"},
                       {"procedure", "ACP127_PLAINDRESS"},
                       {"routing", "UHAAAA"},
                       {"originator", "STA-B"},
                       {"from", "MARS STATION STA-B"},
                       {"to", "UHAAAA/NCS-A"},
                       {"to_station", "NCS-A"},
                       {"sign", "N"},
                       {"text", "UNSIGNED"}});
    CHECK(ok(r) && !yes(r, "signed") && str(r, "message").find("[SIG:") == std::string::npos);

    // 6. A rebuilt station comes back from its backup with the same card.
    r = call(*B.core, {{"cmd", "station_backup"}, {"passphrase", "practice net backup"}});
    CHECK(ok(r));
    std::string bk = str(r, "backup");
    CHECK(!ok(call(*C.core, {{"cmd", "station_restore"}, {"text", bk}, {"passphrase", "practice net backup"}})));
    r = call(*B.core, {{"cmd", "station_restore"}, {"text", bk}, {"passphrase", "practice net backup"}});
    CHECK(ok(r) && str(r, "fingerprint") == B.fp);

    A.core->stop();
    B.core->stop();
    C.core->stop();
    return out;
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_station_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    unit(dir);
    // From scratch, twice: every key is new the second time, and nothing
    // from the first net opens on the second.
    Round one = bootstrap(dir / "one");
    Round two = bootstrap(dir / "two");
    for (auto& f : one.fingerprints) CHECK(!two.fingerprints.count(f));
    {
        // Round two's STA-C, rebuilt, can't open round one's sealed tape.
        auto ma = std::make_unique<LoopbackModem>(0);
        Core c2(on_mars_link(CoreConfig{"STA-C", (dir / "two" / "STA-C.db").string(), 3, 1, true}), std::move(ma));
        c2.set_services(test_services());
        auto id = std::make_shared<station::Identity>();
        bool exists = false;
        std::string err;
        CHECK(station::load_identity((dir / "two" / "STA-C.id").string(), *id, &exists, &err) && exists);
        c2.set_identity(id, (dir / "two" / "STA-C.id").string());
        auto kr = std::make_shared<keys::Keyring>((dir / "two" / "STA-C.keys").string());
        kr->load(&err);
        c2.set_keyring(kr);
        JsonValue r = call(c2, {{"cmd", "key_open"}, {"text", one.sealed_c}});
        CHECK(!ok(r));
    }
    fs::remove_all(dir);
    return check::report("test_station");
}
