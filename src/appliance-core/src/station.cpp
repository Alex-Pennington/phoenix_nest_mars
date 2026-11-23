// Station keys. See station.h and docs/crypto.md.
#include "station.h"

#include "codress.h"
#include "crypto.h"
#include "keys.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace pna {
namespace station {

namespace {

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

std::string upper(std::string s) {
    for (auto& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

std::string HEX(const std::string& b) { return upper(crypto::to_hex(b)); }

bool unhex(const std::string& h, size_t n, std::string& out) { return h.size() == 2 * n && crypto::from_hex(h, out); }

bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string simplified(const std::string& s) {
    std::string o;
    bool gap = false;
    for (char c : s) {
        if (space(c)) {
            gap = !o.empty();
        } else {
            if (gap) o += ' ';
            gap = false;
            o += c;
        }
    }
    return o;
}

std::vector<std::string> lines_of(const std::string& t) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : t) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> w;
    std::string cur;
    for (char c : s) {
        if (space(c)) {
            if (!cur.empty()) w.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

bool starts(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

bool valid_call(const std::string& s) {
    if (s.empty() || s.size() > 16) return false;
    for (char c : s)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

// The block that starts with `head` and ends with END, each line simplified.
std::vector<std::string> block(const std::string& text, const char* head) {
    std::vector<std::string> b;
    bool in = false;
    for (auto& raw : lines_of(text)) {
        std::string l = simplified(raw);
        if (!in) {
            if (l == head) {
                in = true;
                b.push_back(l);
            }
            continue;
        }
        if (l.empty()) continue;
        b.push_back(l);
        if (l == "END") return b;
    }
    return {};
}

// "KEY value" lines of a block, by key.
std::string get(const std::vector<std::string>& b, const char* key) {
    std::string k = std::string(key) + " ";
    for (auto& l : b)
        if (starts(l, k.c_str())) return l.substr(k.size());
    return "";
}

std::string all(const std::vector<std::string>& b, const char* key) {
    std::string k = std::string(key) + " ", o;
    for (auto& l : b)
        if (starts(l, k.c_str())) o += l.substr(k.size());
    return o;
}

const char* kB32 = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  // no I, O, 0, 1: read aloud without doubt

std::string card_signed_text(const Card& c) {
    return "PNA CARD 1|" + c.station + "|" + keys::time_text(c.made) + "|" + crypto::to_hex(c.sign_pub) + "|" +
           crypto::to_hex(c.seal_pub);
}

bool write_private(const std::string& path, const std::string& text, std::string* error) {
    std::string tmp = path + ".new";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return fail(error, "cannot write " + tmp + ": " + std::strerror(errno));
    size_t off = 0;
    bool ok = true;
    while (off < text.size()) {
        ssize_t n = ::write(fd, text.data() + off, text.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        off += static_cast<size_t>(n);
    }
    ok = ok && ::fsync(fd) == 0;
    ::close(fd);
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return fail(error, "cannot write " + path);
    }
    return true;
}

// What a signature covers: from the date-time group line (or the FM line,
// in the 16L forms that have no DTG line) through the closing BT, each line
// with its spacing evened out, blank lines and [DIGEST:]/[SIG:] lines left
// out. Format lines 1 to 4 are left out because relay stations rewrite them.
bool dtg_line(const std::vector<std::string>& w) {
    if (w.size() < 3 || w[0].size() != 1 || std::string("ZOPRWY").find(w[0][0]) == std::string::npos) return false;
    if (w[1].size() != 7) return false;
    for (int i = 0; i < 6; ++i)
        if (w[1][static_cast<size_t>(i)] < '0' || w[1][static_cast<size_t>(i)] > '9') return false;
    return w[2].size() == 3 && w[2].find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos;
}

std::string signed_part(const std::string& message, const std::string& signer) {
    std::vector<std::string> L;
    for (auto& raw : lines_of(message)) {
        std::string l = simplified(raw);
        if (l.empty() || starts(l, "[DIGEST:") || starts(l, "[SIG:")) continue;
        L.push_back(l);
    }
    size_t from = L.size(), last_bt = L.size();
    for (size_t i = 0; i < L.size(); ++i) {
        if (from == L.size() && (dtg_line(words(L[i])) || starts(L[i], "FM "))) from = i;
        if (L[i] == "BT" || starts(L[i], "BT ")) {
            if (from == L.size()) from = i;
            last_bt = i;
        }
    }
    std::string o = "PNA SIG 1|" + signer + "|";
    if (from == L.size()) return o;
    size_t to = last_bt == L.size() ? L.size() - 1 : last_bt;
    for (size_t i = from; i <= to && i < L.size(); ++i) o += L[i] + "\n";
    return o;
}

// The bytes of a sealed tape that the keymaster signs.
std::string sealed_signed_text(const std::vector<std::string>& b) {
    std::string o = "PNA SEALED 1|";
    for (auto& l : b) {
        if (starts(l, "SIG ") || l == "END") continue;
        o += l + "\n";
    }
    return o;
}

}  // namespace

void Identity::wipe() {
    crypto::wipe(sign_priv);
    crypto::wipe(seal_priv);
}

bool make_identity(const std::string& station, int64_t now, Identity& out, std::string* error) {
    if (!valid_call(station)) return fail(error, "a station's call is 1 to 16 of A-Z, 0-9 and -");
    out = Identity{};
    out.station = station;
    out.made = now - now % 60;
    if (!crypto::ed25519_keygen(out.sign_priv, out.sign_pub) || !crypto::x25519_keygen(out.seal_priv, out.seal_pub)) {
        out.wipe();
        return fail(error, "making the station's keys failed");
    }
    return true;
}

bool load_identity(const std::string& path, Identity& out, bool* exists, std::string* error) {
    out = Identity{};
    *exists = false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return true;
    *exists = true;
    std::string text;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    std::vector<std::string> L;
    for (auto& l : lines_of(text))
        if (!simplified(l).empty()) L.push_back(simplified(l));
    std::string body;
    for (size_t i = 0; i + 1 < L.size(); ++i) body += L[i] + "\n";
    bool ok = L.size() == 6 && L[0] == "PNA STATION IDENTITY 1" && L.back() == "end " + crypto::sha256_hex(body);
    if (ok) {
        out.station = get(L, "station");
        ok = keys::parse_time(get(L, "made"), out.made) && unhex(get(L, "sign"), 32, out.sign_priv) &&
             unhex(get(L, "seal"), 32, out.seal_priv);
    }
    crypto::wipe(text);
    crypto::wipe(body);
    for (auto& l : L) crypto::wipe(l);
    if (ok) {
        out.sign_pub = crypto::ed25519_public(out.sign_priv);
        out.seal_pub = crypto::x25519_public(out.seal_priv);
        ok = out.sign_pub.size() == 32 && out.seal_pub.size() == 32 && valid_call(out.station);
    }
    if (!ok) {
        out.wipe();
        out = Identity{};
        return fail(error, path + " is damaged: restore the station's keys from its backup, or make new ones");
    }
    return true;
}

bool save_identity(const std::string& path, const Identity& id, std::string* error) {
    std::string body = "PNA STATION IDENTITY 1\nstation " + id.station + "\nmade " + keys::time_text(id.made) +
                       "\nsign " + crypto::to_hex(id.sign_priv) + "\nseal " + crypto::to_hex(id.seal_priv) + "\n";
    std::string text = body + "end " + crypto::sha256_hex(body) + "\n";
    bool ok = write_private(path, text, error);
    crypto::wipe(body);
    crypto::wipe(text);
    return ok;
}

bool backup(const Identity& id, const std::string& passphrase, std::string& out, std::string* error) {
    if (passphrase.size() < 12) return fail(error, "the passphrase must be 12 characters or more");
    std::string salt = crypto::random_bytes(16), nonce = crypto::random_bytes(12);
    std::string key = crypto::scrypt(passphrase, salt, 32);
    if (key.empty()) return fail(error, "scrypt failed");
    std::string plain = id.sign_priv + id.seal_priv;
    std::string aad = "PNA BACKUP 1|" + id.station + "|" + keys::time_text(id.made);
    std::string sealed = crypto::aes256gcm_seal(key, nonce, aad, plain);
    crypto::wipe(key);
    crypto::wipe(plain);
    if (sealed.empty()) return fail(error, "the cipher failed");
    std::string h = HEX(sealed);
    out = "PNA STATION BACKUP 1\nSTATION " + id.station + "\nMADE " + keys::time_text(id.made) + "\nSALT " + HEX(salt) +
          "\nNONCE " + HEX(nonce) + "\n";
    for (size_t i = 0; i < h.size(); i += 64) out += "DATA " + h.substr(i, 64) + "\n";
    out += "END\n";
    return true;
}

bool restore(const std::string& text, const std::string& passphrase, Identity& out, std::string* error) {
    auto b = block(text, "PNA STATION BACKUP 1");
    if (b.empty()) return fail(error, "no station backup in the text");
    std::string salt, nonce, sealed;
    out = Identity{};
    out.station = get(b, "STATION");
    if (!valid_call(out.station) || !keys::parse_time(get(b, "MADE"), out.made) || !unhex(get(b, "SALT"), 16, salt) ||
        !unhex(get(b, "NONCE"), 12, nonce) || !crypto::from_hex(all(b, "DATA"), sealed))
        return fail(error, "the backup is damaged");
    std::string key = crypto::scrypt(passphrase, salt, 32), plain;
    bool ok = !key.empty() &&
              crypto::aes256gcm_open(key, nonce, "PNA BACKUP 1|" + out.station + "|" + keys::time_text(out.made), sealed,
                                     plain) &&
              plain.size() == 64;
    crypto::wipe(key);
    if (!ok) {
        crypto::wipe(plain);
        return fail(error, "the passphrase is wrong, or the backup was changed");
    }
    out.sign_priv = plain.substr(0, 32);
    out.seal_priv = plain.substr(32);
    crypto::wipe(plain);
    out.sign_pub = crypto::ed25519_public(out.sign_priv);
    out.seal_pub = crypto::x25519_public(out.seal_priv);
    return true;
}

Card card_of(const Identity& id) { return Card{id.station, id.made, id.sign_pub, id.seal_pub}; }

std::string fingerprint(const Card& c) {
    std::string d;
    crypto::from_hex(crypto::sha256_hex("PNA CARD 1|" + c.station + "|" + c.sign_pub + c.seal_pub), d);
    // 80 bits, 5 at a time.
    std::string o;
    uint64_t hi = 0;
    for (int i = 0; i < 5; ++i) hi = (hi << 8) | static_cast<unsigned char>(d[static_cast<size_t>(i)]);
    uint64_t lo = 0;
    for (int i = 5; i < 10; ++i) lo = (lo << 8) | static_cast<unsigned char>(d[static_cast<size_t>(i)]);
    for (int part = 0; part < 2; ++part) {
        uint64_t v = part ? lo : hi;
        for (int i = 7; i >= 0; --i) {
            o += kB32[(v >> (5 * i)) & 31];
            if (o.size() == 4 || o.size() == 9 || o.size() == 14) o += ' ';
        }
    }
    return o;
}

std::string format_card(const Identity& id) {
    Card c = card_of(id);
    std::string sig = HEX(crypto::ed25519_sign(id.sign_priv, card_signed_text(c)));
    return "PNA STATION CARD 1\nSTATION " + c.station + "\nMADE " + keys::time_text(c.made) + "\nSIGN " +
           HEX(c.sign_pub) + "\nSEAL " + HEX(c.seal_pub) + "\nSELF " + sig.substr(0, 64) + "\nSELF " + sig.substr(64) +
           "\nFP " + fingerprint(c) + "\nEND\n";
}

std::string card_text(const std::string& text) {
    std::string o;
    for (auto& l : block(upper(text), "PNA STATION CARD 1")) o += l + "\n";
    return o;
}

bool parse_card(const std::string& text, Card& out, std::string* error) {
    auto b = block(upper(text), "PNA STATION CARD 1");
    if (b.empty()) return fail(error, "no station card in the text (it starts PNA STATION CARD 1 and ends END)");
    out = Card{};
    out.station = get(b, "STATION");
    std::string sig;
    if (!valid_call(out.station) || !keys::parse_time(get(b, "MADE"), out.made) ||
        !unhex(get(b, "SIGN"), 32, out.sign_pub) || !unhex(get(b, "SEAL"), 32, out.seal_pub) ||
        !unhex(all(b, "SELF"), 64, sig))
        return fail(error, "the card is damaged or incomplete");
    if (!crypto::ed25519_verify(out.sign_pub, card_signed_text(out), sig))
        return fail(error, "the card's own signature does not check: it was changed or damaged");
    std::string fp = get(b, "FP");
    if (!fp.empty() && fp != fingerprint(out)) return fail(error, "the card's fingerprint line does not match its keys");
    return true;
}

std::string sign(const std::string& message, const Identity& id) {
    size_t nnnn = message.rfind("NNNN");
    if (nnnn == std::string::npos || message.find("[SIG:") != std::string::npos) return message;
    std::string sig = HEX(crypto::ed25519_sign(id.sign_priv, signed_part(message, id.station)));
    if (sig.empty()) return message;
    size_t p = nnnn;
    while (p > 0 && (message[p - 1] == '\r' || message[p - 1] == '\n')) --p;
    std::string eol = message.find("\r\r\n") != std::string::npos ? "\r\r\n"
                      : message.find("\r\n") != std::string::npos ? "\r\n"
                                                                  : "\n";
    return message.substr(0, p) + eol + "[SIG:" + id.station + ":" + sig + "]" + message.substr(p);
}

SigCheck check_signature(const std::string& message,
                         const std::function<std::optional<Card>(const std::string&)>& card_for) {
    SigCheck r{"none", ""};
    std::string line;
    for (auto& raw : lines_of(message)) {
        std::string l = simplified(raw);
        if (starts(l, "[SIG:") && l.back() == ']') line = l.substr(5, l.size() - 6);
    }
    if (line.empty()) return r;
    size_t colon = line.find(':');
    std::string sig;
    if (colon == std::string::npos || !unhex(line.substr(colon + 1), 64, sig)) {
        r.state = "bad";
        return r;
    }
    r.signer = line.substr(0, colon);
    auto c = card_for ? card_for(r.signer) : std::nullopt;
    if (!c) {
        r.state = "unknown";
        return r;
    }
    r.state = crypto::ed25519_verify(c->sign_pub, signed_part(message, r.signer), sig) ? "good" : "bad";
    return r;
}

// ---- sealed key tapes

bool seal_tape(const std::string& tape_text, const std::string& net, const std::string& tape_id, const Identity& from,
               const Card& to, std::string& out, std::string* error) {
    std::string eph_priv, eph_pub;
    if (!crypto::x25519_keygen(eph_priv, eph_pub)) return fail(error, "making the one-time key failed");
    std::string shared = crypto::x25519(eph_priv, to.seal_pub);
    crypto::wipe(eph_priv);
    if (shared.empty()) return fail(error, "the station's card has a bad sealing key");
    std::string info = "PNA SEALED TAPE 1|" + to.station + "|" + from.station;
    std::string key = crypto::hkdf_sha256(shared, eph_pub + to.seal_pub, info, 32);
    crypto::wipe(shared);
    std::vector<std::string> b = {"PNA SEALED TAPE 1", "TO " + to.station, "FROM " + from.station, "NET " + net,
                                  "TAPE " + tape_id, "EPH " + HEX(eph_pub)};
    std::string aad;
    for (auto& l : b) aad += l + "\n";
    std::string plain = tape_text + static_cast<char>(0x80);
    while ((12 + plain.size() + 16) % 5) plain += '\0';
    std::string nonce = crypto::random_bytes(12);
    std::string sealed = crypto::aes256gcm_seal(key, nonce, aad, plain);
    crypto::wipe(key);
    crypto::wipe(plain);
    if (sealed.empty()) return fail(error, "the cipher failed");
    auto groups = codress::to_groups(nonce + sealed);
    b.push_back("GR " + std::to_string(groups.size()));
    std::string line;
    for (size_t i = 0; i < groups.size(); ++i) {
        line += (line.empty() ? "" : " ") + groups[i];
        if ((i + 1) % codress::kGroupsPerLine == 0 || i + 1 == groups.size()) {
            b.push_back(line);
            line.clear();
        }
    }
    std::string sig = HEX(crypto::ed25519_sign(from.sign_priv, sealed_signed_text(b)));
    if (sig.empty()) return fail(error, "signing failed");
    b.push_back("SIG " + sig.substr(0, 64));
    b.push_back("SIG " + sig.substr(64));
    b.push_back("END");
    out.clear();
    for (auto& l : b) out += l + "\n";
    return true;
}

bool sealed_head(const std::string& text, SealedHead& out) {
    auto b = block(text, "PNA SEALED TAPE 1");
    if (b.empty()) return false;
    out = SealedHead{get(b, "TO"), get(b, "FROM"), get(b, "NET"), get(b, "TAPE")};
    return !out.to.empty() && !out.from.empty();
}

bool open_tape(const std::string& text, const Identity& me,
               const std::function<std::optional<Card>(const std::string&)>& card_for, std::string& tape_text,
               SealedHead& head, std::string* error) {
    tape_text.clear();
    auto b = block(text, "PNA SEALED TAPE 1");
    if (b.empty() || !sealed_head(text, head)) return fail(error, "no sealed key tape in the text");
    if (head.to != me.station) return fail(error, "this tape is sealed to " + head.to + ", not to this station");
    auto from = card_for ? card_for(head.from) : std::nullopt;
    if (!from)
        return fail(error, "the tape is signed by " + head.from +
                               ", whose station card this station doesn't hold: import that card first");
    std::string sig;
    if (!unhex(all(b, "SIG"), 64, sig) || !crypto::ed25519_verify(from->sign_pub, sealed_signed_text(b), sig))
        return fail(error, "the tape's signature does not check against " + head.from +
                               "'s card: it was changed, damaged, or not sent by that station");
    std::string eph;
    if (!unhex(get(b, "EPH"), 32, eph)) return fail(error, "the tape is damaged");
    // The groups: the lines after GR, up to the signature.
    std::vector<std::string> groups;
    bool in = false;
    size_t want = 0;
    for (auto& l : b) {
        if (starts(l, "GR ")) {
            in = true;
            want = static_cast<size_t>(std::atoi(l.c_str() + 3));
            continue;
        }
        if (starts(l, "SIG ") || l == "END") in = false;
        if (in)
            for (auto& g : words(l)) groups.push_back(g);
    }
    if (groups.size() != want || want < 7) return fail(error, "the tape's groups are incomplete");
    std::string air;
    if (!codress::from_groups(groups, air, error)) return false;
    std::string shared = crypto::x25519(me.seal_priv, eph);
    if (shared.empty()) return fail(error, "the tape is damaged");
    std::string key = crypto::hkdf_sha256(shared, eph + me.seal_pub, "PNA SEALED TAPE 1|" + head.to + "|" + head.from, 32);
    crypto::wipe(shared);
    std::string aad;
    for (size_t i = 0; i < 6 && i < b.size(); ++i) aad += b[i] + "\n";
    std::string plain;
    bool ok = crypto::aes256gcm_open(key, air.substr(0, 12), aad, air.substr(12), plain);
    crypto::wipe(key);
    crypto::wipe(air);
    if (!ok) return fail(error, "the tape doesn't open with this station's key: it was sealed to another card of " + head.to);
    while (!plain.empty() && plain.back() == '\0') plain.pop_back();
    if (plain.empty() || static_cast<unsigned char>(plain.back()) != 0x80) {
        crypto::wipe(plain);
        return fail(error, "bad padding");
    }
    plain.pop_back();
    tape_text = plain;
    crypto::wipe(plain);
    return true;
}

}  // namespace station
}  // namespace pna
