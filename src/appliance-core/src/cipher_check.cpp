// The cipher slot's checks. See cipher_check.h and docs/cipher-slot.md.
#include "cipher_check.h"

#include "codress.h"
#include "crypto.h"

#include <cstring>

namespace pna {
namespace cipher {

namespace {

const char* kInd = "TST01";
const char* kInd2 = "TST02";
const char* kDtg = "R 231040Z NOV 2026";

// Made-up message texts: one word, an ordinary message, a long one, and every
// character a teletype message can hold.
std::vector<std::string> sample_texts() {
    std::string ordinary =
        "FM STA-A\nTO STA-B\nINFO NCS-A\nGR 24\nBT\nUNCLAS\nSUBJ DRILL TRAFFIC\n"
        "1. THIS IS A TEST MESSAGE MADE BY THE CIPHER SLOT HARNESS.\n2. NOTHING IN IT IS REAL.\nBT";
    std::string all;
    for (int c = 32; c < 127; ++c) all += static_cast<char>(c);
    all += "\nLINE TWO\r\nLINE THREE";
    std::string longer;
    while (longer.size() < 6000) longer += "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789\n";
    return {"TEST", ordinary, all, longer};
}

std::string flip(const std::string& s, bool text) {
    std::string o = s;
    size_t i = o.size() / 2;
    if (text) {
        // Change a letter or figure to another one; leave spaces and newlines.
        for (size_t j = 0; j < o.size(); ++j) {
            size_t k = (i + j) % o.size();
            char c = o[k];
            if (c >= 'A' && c <= 'Z') {
                o[k] = c == 'Z' ? 'A' : static_cast<char>(c + 1);
                return o;
            }
            if (c >= '0' && c <= '9') {
                o[k] = c == '9' ? '0' : static_cast<char>(c + 1);
                return o;
            }
        }
        return o;
    }
    o[i] = static_cast<char>(o[i] ^ 0x01);
    return o;
}

}  // namespace

bool passed(const std::vector<Check>& checks) {
    for (auto& c : checks)
        if (c.result == Result::fail) return false;
    return true;
}

std::vector<Check> run_checks(const Slot& s) {
    std::vector<Check> out;
    auto add = [&](const std::string& what, Result r, const std::string& d = "") { out.push_back({what, r, d}); };
    if (!s.usable()) {
        add("the module loads", Result::fail, s.why_unusable());
        return out;
    }
    const Info& in = s.info();
    add("the module loads", Result::pass,
        in.name + " " + in.version + ", " + (in.text ? "its own on-air text" : "bytes written as groups") + ", " +
            std::to_string(in.key_bytes) + "-byte keys");

    const std::string k1 = crypto::random_bytes(in.key_bytes);
    const std::string k2 = crypto::random_bytes(in.key_bytes);
    std::string err, ready;

    // 1. A good key loads.
    if (!s.load_key(kInd, k1, ready, &err)) {
        add("a good key loads", Result::fail, err);
        return out;
    }
    crypto::wipe(ready);
    add("a good key loads", Result::pass);

    // 2. Bad keys are refused: none, one byte short, one byte over.
    {
        std::vector<std::pair<std::string, std::string>> bad = {{"an empty key", ""},
                                                                {"a key one byte short", k1.substr(0, in.key_bytes - 1)},
                                                                {"a key one byte over", k1 + "X"}};
        std::string took, quiet;
        for (auto& b : bad) {
            std::string e;
            if (b.second.empty()) {
                // The slot itself refuses an empty key; ask the module too.
                char eb[256] = {0};
                unsigned char rb[PNA_CIPHER_MAX_KEY];
                size_t rl = sizeof rb;
                unsigned char z = 0;
                int rc = s.table()->load_key(kInd, &z, 0, rb, &rl, eb, sizeof eb);
                std::memset(rb, 0, sizeof rb);
                if (rc == PNA_CIPHER_OK) took += (took.empty() ? "" : ", ") + b.first;
                else if (!eb[0]) quiet += (quiet.empty() ? "" : ", ") + b.first;
                continue;
            }
            if (s.load_key(kInd, b.second, ready, &e)) {
                crypto::wipe(ready);
                took += (took.empty() ? "" : ", ") + b.first;
            } else if (e.find("no reason given") != std::string::npos) {
                quiet += (quiet.empty() ? "" : ", ") + b.first;
            }
        }
        if (!took.empty()) add("bad keys are refused", Result::fail, "load_key took " + took);
        else add("bad keys are refused", Result::pass, "an empty key, one byte short, one byte over");
        if (!quiet.empty()) add("refusals say why", Result::warn, "no reason in err for " + quiet);
    }

    // 3. The key check: letters and figures, the same each time, different
    // for a different key.
    {
        std::string c1, c1b, c2, e;
        if (!s.key_check(kInd, k1, c1, &e) || !s.key_check(kInd, k1, c1b, &e) || !s.key_check(kInd, k2, c2, &e)) {
            add("the key check", Result::fail, e);
        } else if (c1 != c1b) {
            add("the key check", Result::fail, "two checks of the same key differ");
        } else if (c1 == c2) {
            add("the key check", Result::warn, "two different keys gave the same check (" + c1 + "): too short to catch a wrong key?");
        } else {
            add("the key check", Result::pass, "e.g. " + c1);
        }
    }

    // 4. Messages round-trip.
    std::string first_out;
    {
        std::string bad;
        for (auto& t : sample_texts()) {
            std::string c, back, e;
            if (!s.encrypt(kInd, k1, kDtg, t, c, &e)) {
                bad = "enciphering " + std::to_string(t.size()) + " characters: " + e;
                break;
            }
            if (first_out.empty()) first_out = c;
            if (c.find(t) != std::string::npos && t.size() > 8) {
                bad = "the text shows in the clear in the output";
                break;
            }
            if (!s.decrypt(kInd, k1, kDtg, c, back, &e)) {
                bad = "reading back " + std::to_string(t.size()) + " characters: " + e;
                break;
            }
            if (back != t) {
                bad = "a message of " + std::to_string(t.size()) + " characters came back different";
                break;
            }
        }
        if (bad.empty()) add("messages round-trip", Result::pass, "1 word to 6,000 characters, every printable character");
        else add("messages round-trip", Result::fail, bad);
    }

    // 5. A whole codress message, in the appliance's own layout.
    {
        std::string plain =
            "VZCZCTST001\nRR STA-B\nDE STA-A 001 3271040\nZNR UUUUU\nR 231040Z NOV 2026\nFM STA-A\nTO STA-B\nBT\n"
            "UNCLAS\nSUBJ HARNESS CHECK\n1. A MADE-UP MESSAGE.\nBT\n#0001\nNNNN";
        std::string air, back, e;
        if (!codress::encipher(s, plain, kInd, k1, air, &e)) add("a codress message round-trips", Result::fail, e);
        else if (!codress::decipher(s, air, k1, back, &e)) add("a codress message round-trips", Result::fail, e);
        else if (back.find("1. A MADE-UP MESSAGE.") == std::string::npos || back.find("FM STA-A") == std::string::npos)
            add("a codress message round-trips", Result::fail, "the message came back different");
        else add("a codress message round-trips", Result::pass);
    }

    // 6. Each message comes out different (a fresh nonce or message key).
    {
        std::string c, e;
        if (!first_out.empty() && s.encrypt(kInd, k1, kDtg, "TEST", c, &e)) {
            if (c == first_out) add("the same text enciphers differently each time", Result::warn, "it came out the same twice: a listener sees repeats");
            else add("the same text enciphers differently each time", Result::pass);
        }
    }

    // 7. Wrong key, changed cipher, changed DTG: refused, never the text.
    auto refusal = [&](const std::string& what, const std::string& key, const std::string& ind, const std::string& dtg,
                       const std::string& in, const std::string& text, bool must) {
        std::string back, e;
        bool refused = false;
        if (!s.decrypt(ind, key, dtg, in, back, &e, &refused)) {
            add(what, refused ? Result::pass : Result::warn,
                refused ? "" : "refused, but as a failure rather than PNA_CIPHER_REFUSED: " + e);
        } else if (back == text) {
            add(what, must ? Result::fail : Result::warn, "it read the message anyway");
        } else {
            add(what, Result::warn, "it gave garbage instead of refusing: the operator would see it as a message");
        }
        crypto::wipe(back);
    };
    {
        std::string t = "MADE-UP TEXT FOR THE HARNESS", c, e;
        if (s.encrypt(kInd, k1, kDtg, t, c, &e)) {
            refusal("a wrong key is refused", k2, kInd, kDtg, c, t, true);
            refusal("a changed cipher is refused", k1, kInd, kDtg, flip(c, in.text), t, false);
            refusal("a changed date-time group is refused", k1, kInd, "R 231041Z NOV 2026", c, t, false);
            refusal("another key indicator is refused", k1, kInd2, kDtg, c, t, false);
        }
    }

    // 8. A small out buffer gets PNA_CIPHER_SHORT and nothing written past it.
    {
        std::string ready2, e;
        if (s.load_key(kInd, k1, ready2, &e)) {
            unsigned char buf[8 + 32];
            std::memset(buf, 0xA5, sizeof buf);
            size_t len = 8;
            char eb[256] = {0};
            const char* t = "A MESSAGE LONGER THAN EIGHT BYTES";
            int rc = s.table()->encrypt(kInd, reinterpret_cast<const unsigned char*>(ready2.data()), ready2.size(), kDtg,
                                        reinterpret_cast<const unsigned char*>(t), std::strlen(t), buf, &len, eb,
                                        sizeof eb);
            bool spilled = false;
            for (size_t i = 8; i < sizeof buf; ++i) spilled = spilled || buf[i] != 0xA5;
            crypto::wipe(ready2);
            if (spilled) add("a small buffer is not overrun", Result::fail, "encrypt wrote past the room it was given");
            else if (rc != PNA_CIPHER_SHORT) add("a small buffer is not overrun", Result::fail, "encrypt did not answer PNA_CIPHER_SHORT");
            else if (len <= 8) add("a small buffer is not overrun", Result::fail, "PNA_CIPHER_SHORT without the size needed");
            else add("a small buffer is not overrun", Result::pass);
        }
    }
    return out;
}

}  // namespace cipher
}  // namespace pna
