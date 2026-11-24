// Milestone 6, part 1: the primitives against published known answers, the
// MSC digest against real traffic, and key tapes and the station keyring.
// Every key here is made up for the test and thrown away.
#include "../src/acp127.h"
#include "../src/crypto.h"
#include "../src/digest.h"
#include "../src/keys.h"
#include "check.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static std::string hex(const std::string& h) {
    std::string b;
    crypto::from_hex(h, b);
    return b;
}

static std::string read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

static void primitives() {
    // AES-256-GCM, the GCM specification's test cases 14 and 16
    // (McGrew and Viega, "The Galois/Counter Mode of Operation", appendix B).
    {
        std::string key(32, '\0'), iv(12, '\0'), p(16, '\0');
        std::string s = crypto::aes256gcm_seal(key, iv, "", p);
        CHECK(crypto::to_hex(s) == "cea7403d4d606b6e074ec5d3baf39d18"
                                   "d0d1c8a799996bf0265b98b5d48ab919");
        std::string back;
        CHECK(crypto::aes256gcm_open(key, iv, "", s, back) && back == p);
    }
    {
        std::string key = hex("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
        std::string iv = hex("cafebabefacedbaddecaf888");
        std::string p = hex(
            "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525"
            "b16aedf5aa0de657ba637b39");
        std::string aad = hex("feedfacedeadbeeffeedfacedeadbeefabaddad2");
        std::string s = crypto::aes256gcm_seal(key, iv, aad, p);
        CHECK(crypto::to_hex(s) ==
              "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838"
              "c5f61e6393ba7a0abcc9f662"
              "76fc6ece0f4e1768cddf8853bb2d551b");
        std::string back;
        CHECK(crypto::aes256gcm_open(key, iv, aad, s, back) && back == p);
        // One bit changed anywhere: refused.
        std::string bad = s;
        bad[5] ^= 1;
        CHECK(!crypto::aes256gcm_open(key, iv, aad, bad, back));
        bad = s;
        bad.back() ^= 0x80;
        CHECK(!crypto::aes256gcm_open(key, iv, aad, bad, back));
        CHECK(!crypto::aes256gcm_open(key, iv, aad + "x", s, back));
        std::string k2 = key;
        k2[0] ^= 1;
        CHECK(!crypto::aes256gcm_open(k2, iv, aad, s, back));
    }
    // HKDF-SHA256, RFC 5869 test case 1.
    CHECK(crypto::to_hex(crypto::hkdf_sha256(hex("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"),
                                             hex("000102030405060708090a0b0c"), hex("f0f1f2f3f4f5f6f7f8f9"), 42)) ==
          "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
    // SHA-256 and MD5 (FIPS 180-2 and RFC 1321).
    CHECK(crypto::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(crypto::md5_hex_upper("") == "D41D8CD98F00B204E9800998ECF8427E");
    CHECK(crypto::md5_hex_upper("abc") == "900150983CD24FB0D6963F7D28E17F72");
    // Random bytes differ.
    CHECK(crypto::random_bytes(32).size() == 32 && crypto::random_bytes(32) != crypto::random_bytes(32));
    std::string w = "secret";
    crypto::wipe(w);
    CHECK(w.empty());
}

static void digests() {
    // MSC's own digests, on real traffic: what MSC wrote, we compute.
    const fs::path s1 = fs::path(PNA_SOURCE_DIR) / "tests/fixtures/mars-samples";
    const fs::path s2 = fs::path(PNA_SOURCE_DIR) / "tests/fixtures/mars-samples-b2";
    struct K {
        fs::path p;
        const char* want;
    } known[] = {
        {s1 / "06_PRIORITY_RI.txt", "7563520289C79ED5D2E409DF67FE15FE"},
        {s1 / "08_PRIORITY_RI.txt", "4E64F97F64BB978F7B6E6174F64A36F0"},
        {s1 / "34_ROUTINE_ORDER.txt", "EADC49186777663FD8AEC4E3E7EF2A9C"},
        {s1 / "36_ROUTINE_GENADMIN.txt", "6AAB8D21AEBD7439EBF66CF3A7F507F5"},
        {s2 / "23_ROUTINE_ACP127-PLAINDRESS_COMSPOT.txt", "E316E1D9C6F6CFDD986A621B4A8ABB5D"},
    };
    for (auto& k : known) {
        std::string t = read(k.p);
        CHECK(!t.empty());
        CHECK(digest::written(t) == k.want);
        CHECK(digest::compute(t) == k.want);
        CHECK(digest::check(t) == digest::Check::Pass);
        // One character changed in the text: the digest fails.
        size_t at = t.find("BT");
        at = t.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ", t.find('\n', t.find('\n', at) + 1));
        std::string bad = t;
        bad[at] = bad[at] == 'X' ? 'Y' : 'X';
        CHECK(digest::check(bad) == digest::Check::Fail);
    }
    // A message we build: append, then check, then parse.
    acp127::Message m;
    m.channel = "MMM";
    m.prec = 'P';
    m.routing = {"UHAAAA"};
    m.mars_fl3 = true;
    m.originator_ri = "NCS-A";
    m.station_serial = "12";
    m.dtg = acp127::Dtg{23, 10, 40, 'Z', 11, 2026};
    m.from.plain = "MARS STATION NCS-A";
    m.to = {acp127::Address{"UHAAAA", "STA-B"}};
    m.text = "\nEXER/PRACTICE//\nTHIS IS A TEST";
    std::string err, built = acp127::build(m, &err);
    CHECK(!built.empty());
    CHECK(digest::check(built) == digest::Check::None);
    std::string d = digest::append(built);
    CHECK(d != built && d.find("[DIGEST:") != std::string::npos);
    CHECK(digest::append(d) == d);  // never twice
    CHECK(digest::check(d) == digest::Check::Pass);
    acp127::Message back;
    CHECK(acp127::parse(d, back, &err));
    CHECK(back.text == m.text);
    std::string bad = d;
    bad.replace(bad.find("A TEST"), 6, "B TEST");
    CHECK(digest::check(bad) == digest::Check::Fail);
    // Spacing and line ends don't matter to the digest (as in MSC).
    std::string loose = d;
    loose.replace(loose.find("THIS IS"), 7, "THIS   IS");
    CHECK(digest::check(loose) == digest::Check::Pass);
    // No two BT lines: nothing to digest, nothing added.
    CHECK(digest::append("ZCZC\nNNNN") == "ZCZC\nNNNN");
}

static void tapes(const fs::path& dir) {
    int64_t t0 = 0;
    CHECK(keys::parse_time("202611240000Z", t0));
    CHECK(keys::time_text(t0) == "202611240000Z");
    CHECK(!keys::parse_time("202602310000Z", t0));  // 31 February
    CHECK(keys::parse_time("202611240000Z", t0));

    keys::Tape tape;
    std::string err;
    CHECK(!keys::make_tape("practice net", t0, 3, 24, t0, tape, &err));  // lower case
    CHECK(!keys::make_tape("PRACTICE NET", t0, 100, 24, t0, tape, &err));
    CHECK(keys::make_tape("PRACTICE NET", t0, 3, 24, t0 - 3600, tape, &err));
    CHECK(tape.keys.size() == 3 && tape.tape.size() == 3);
    CHECK(tape.keys[0].indicator == tape.tape + "01" && tape.keys[2].indicator == tape.tape + "03");
    CHECK(tape.keys[1].from == t0 + 86400 && tape.keys[1].until == t0 + 2 * 86400);
    CHECK(tape.keys[0].secret != tape.keys[1].secret && tape.keys[0].secret.size() == 32);
    std::string text = keys::format_tape(tape);
    keys::Tape back;
    CHECK(keys::parse_tape(text, back, &err));
    CHECK(back.net == "PRACTICE NET" && back.keys.size() == 3 && back.keys[2].secret == tape.keys[2].secret);
    // CRLF copies read the same.
    std::string crlf;
    for (char c : text) crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    CHECK(keys::parse_tape(crlf, back, &err));
    // Any change, or a cut, is caught by the end line.
    std::string bad = text;
    size_t k = bad.find("key " + tape.tape + "02") + 30;
    bad[k] = bad[k] == '0' ? '1' : '0';
    CHECK(!keys::parse_tape(bad, back, &err) && err.find("end check") != std::string::npos);
    CHECK(!keys::parse_tape(text.substr(0, text.size() / 2), back, &err));
    // A newer version is refused by name.
    std::string v2 = text;
    v2.replace(0, 14, "PNA KEY TAPE 2");
    CHECK(!keys::parse_tape(v2, back, &err) && err.find("version 2") != std::string::npos);
    CHECK(!keys::parse_tape("hello", back, &err));
    // Each key carries MSC's five labels, as notes on the tape.
    CHECK(text.find("# KEY_CKR:" + tape.tape + "01\n") != std::string::npos);
    keys::Labels lb = keys::key_labels(tape, 1);
    CHECK(lb.ckr == tape.tape + "02" && lb.desc == "PRACTICE NET KEY 2 OF 3" && lb.interval == "Daily");
    CHECK(lb.name.size() == 5 + 1 + 3 + 4 && lb.name.rfind(tape.tape + "02 " + tape.tape, 0) == 0);
    CHECK(lb.created.size() == 18 && lb.created[9] == '@');
    CHECK(keys::label_lines(lb)[3] == "INTRVAL:Daily");
    // The key tape file: framed like an .ekt, with the tape's SHA-256 inside.
    std::string file = keys::tape_file(text);
    CHECK(file[0] == '\0' && keys::looks_framed(file) && !keys::looks_framed(text));
    CHECK(file.find("[PNA KEY TAPE]\n") != std::string::npos && file.find("<KEYTAPE>") != std::string::npos);
    CHECK(keys::parse_tape(file, back, &err) && back.keys.size() == 3 && back.keys[0].secret == tape.keys[0].secret);
    std::string opened;
    CHECK(keys::open_tape_file(file, opened, &err) && opened == text);
    CHECK(keys::open_tape_file(text, opened, &err) && opened == text);  // plain text passes through
    std::string flipped = file;
    flipped[flipped.size() - 40] ^= 1;
    CHECK(!keys::parse_tape(flipped, back, &err) && err.find("checksum") != std::string::npos);
    CHECK(!keys::parse_tape(file.substr(0, file.size() - 10), back, &err));
    // An MSC tape is refused at its head label; nothing after it is read.
    std::string msc;
    for (std::string rec : {std::string("[ENCRYPTED KEY TAPE]\n") + '\0', std::string("NOT A REAL TAPE")}) {
        uint32_t n = static_cast<uint32_t>(rec.size());
        msc += std::string{static_cast<char>(n >> 24), static_cast<char>(n >> 16), static_cast<char>(n >> 8),
                           static_cast<char>(n)} + rec;
    }
    CHECK(keys::looks_framed(msc));
    CHECK(!keys::parse_tape(msc, back, &err) && err.find("MSC key tape") != std::string::npos);

    // The keyring: import, list, choose, expire, zeroize, survive a restart.
    std::string path = (dir / "STA-B.keys").string();
    int64_t now = t0 + 3600;  // an hour into key 01
    {
        keys::Keyring kr(path);
        kr.set_clock([&] { return now; });
        CHECK(kr.load(&err));  // no file yet
        keys::ImportResult r;
        CHECK(kr.import_tape(text, r, &err));
        CHECK(r.added == 3 && r.already == 0 && r.expired == 0);
        struct stat st {};
        CHECK(::stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
        CHECK(kr.import_tape(text, r, &err) && r.added == 0 && r.already == 3);
        auto send = kr.for_sending();
        CHECK(send.size() == 1 && send[0].indicator == tape.tape + "01");
        auto all = kr.list();
        CHECK(all.size() == 3 && all[1].state == "next");
        std::string key;
        CHECK(kr.send_key(tape.tape + "01", key, &err) && key == tape.keys[0].secret);
        CHECK(!kr.send_key(tape.tape + "02", key, &err));  // not started
        CHECK(kr.receive_key(tape.tape + "01", key));
        CHECK(!kr.receive_key("ZZZ01", key));
        // The key file never has the tape's text in it whole, and is not the tape.
        std::string onfile = read(path);
        CHECK(onfile.rfind("PNA STATION KEYS 1", 0) == 0);
        // Another tape with the same indicators but other keys is refused.
        keys::Tape other = tape;
        for (auto& x : other.keys) x.secret = crypto::random_bytes(32);
        CHECK(!kr.import_tape(keys::format_tape(other), r, &err) && err.find("zeroize") != std::string::npos);
    }
    {
        // A restart: the keys come back from the file.
        keys::Keyring kr(path);
        kr.set_clock([&] { return now; });
        CHECK(kr.load(&err));
        CHECK(kr.list().size() == 3);
        // Day 2: key 01 sends no more, but reads for six hours more.
        now = t0 + 86400 + 3600;
        std::string key;
        CHECK(kr.for_sending().size() == 1 && kr.for_sending()[0].indicator == tape.tape + "02");
        CHECK(!kr.send_key(tape.tape + "01", key, &err));
        CHECK(kr.receive_key(tape.tape + "01", key));
        CHECK(kr.sweep().empty());
        // Past the hold: key 01 is zeroized and gone from the file too.
        now = t0 + 86400 + keys::kReceiveHold;
        auto lines = kr.sweep();
        CHECK(lines.size() == 1 && lines[0].find("zeroized") != std::string::npos);
        CHECK(!kr.receive_key(tape.tape + "01", key));
        CHECK(read(path).find("key " + tape.tape + "01") == std::string::npos);
        CHECK(kr.list().size() == 2);
        // The operator zeroizes the whole tape.
        CHECK(kr.zeroize(tape.tape, &err) == 2);
        CHECK(kr.list().empty() && kr.for_sending().empty());
        CHECK(kr.zeroize(tape.tape, &err) == 0);
        // A tape all past its keys loads nothing.
        now = t0 + 10 * 86400;
        keys::ImportResult r;
        CHECK(kr.import_tape(text, r, &err) && r.added == 0 && r.expired == 3);
    }
    {
        // A damaged key file is reported, not half-read.
        std::ofstream(path, std::ios::binary | std::ios::trunc) << "PNA STATION KEYS 1\nkey junk\n";
        keys::Keyring kr(path);
        CHECK(!kr.load(&err));
        CHECK(kr.list().empty());
    }
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_crypto_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    primitives();
    digests();
    tapes(dir);
    fs::remove_all(dir);
    return check::report("test_crypto");
}
