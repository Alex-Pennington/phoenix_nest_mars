// Net keys for codress: the key tape a keymaster makes, and the station's
// keyring that imports tapes, picks the key for now and zeroizes expired keys.
//
// Simple start (docs/crypto.md): one shared key per net per period, on a dated
// key tape. A keymaster makes a tape for a net (pna-keymaster), copies it to a
// USB stick for each station, and each station imports it (appliance-cli keys
// import FILE). A key is used to send from its start time until its end time,
// and to read traffic until kReceiveHold after its end; then it is zeroized.
//
// A key tape, version 1, is text, one item a line ('#' lines are notes):
//
//   PNA KEY TAPE 1
//   net PRACTICE NET
//   tape QXR
//   made 202611231040Z
//   period-hours 24
//   key QXR01 202611240000Z 202611250000Z <64 hex digits: the 32-byte key>
//   key QXR02 202611250000Z 202611260000Z <...>
//   end <SHA-256, hex, of every byte before this line>
//
// The key indicator (QXR01) is the tape's three letters and the key's number;
// it goes in the clear after the date-time group of a codress message, the
// way MARS key indicators do (batch 2 samples 02, 03). Later tape versions
// (station public keys, keys wrapped for one station's hardware token, a
// keymaster signature) add lines and a higher version; a version 1 tape stays
// valid. A version 1 reader refuses a newer tape by name.
//
// On a USB stick the tape travels as a key tape file, framed the way MSC
// frames its .ekt tapes (a run of records, each a 4-byte big-endian length
// and its bytes): a head label, a warning, <CHECKSUM> with the SHA-256 of the
// tape, and <KEYTAPE> with the tape itself. Our head label is
// "[PNA KEY TAPE]"; an MSC tape says "[ENCRYPTED KEY TAPE]" and is refused
// at that first record: this appliance never opens MSC tapes or their keys.
//
// Each key also gets the five labels MSC's key list shows (KEY_CKR, KEYNAME,
// KEYDESC, INTRVAL, CREATED), so a keymaster used to MSC reads ours the
// same way. They are made from the tape; they are not secrets.
//
// A tape for a net that uses another cipher in the slot (cipher_slot.h) is
// version 2: it adds "cipher NAME" after the tape line, and its keys are as
// long as that cipher's keys (any whole number of bytes, up to 4096). A tape
// with no cipher line is for our own pna-aes. A station loads only tapes for
// the cipher in its slot, and sends only with keys for that cipher.
//
// Real keys never go in a repository. Tests make throwaway tapes.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace pna {
namespace keys {

constexpr int kTapeVersion = 1;         // our own cipher
constexpr int kTapeVersionCipher = 2;   // a tape that names another cipher
constexpr size_t kMaxKeyBytes = 4096;
constexpr int64_t kReceiveHold = 6 * 3600;  // a key still reads traffic this long after its end
constexpr int kMaxKeys = 99;

struct Key {
    std::string indicator;  // "QXR01"
    std::string net;
    std::string tape;       // "QXR"
    int64_t from = 0;       // unix seconds UTC; sends from here...
    int64_t until = 0;      // ...to here (exclusive)
    std::string secret;     // 32 bytes for pna-aes; the cipher's key size otherwise
    std::string cipher;     // the module name, "" for our own pna-aes
};

struct Tape {
    std::string net;
    std::string tape;
    int64_t made = 0;
    int period_hours = 24;
    std::string cipher;     // "" for our own pna-aes
    std::vector<Key> keys;
};

// "202611231040Z" and back (UTC, to the minute).
std::string time_text(int64_t t);
bool parse_time(const std::string& s, int64_t& t);

// A new tape with fresh random keys: `count` periods of `period_hours`, the
// first starting at `start`. Net names: A-Z, 0-9, space, '-' and '/', 1 to 40.
// `cipher` names the module the keys are for ("" or "pna-aes": our own, 32
// bytes); key_bytes is that module's key size.
bool make_tape(const std::string& net, int64_t start, int count, int period_hours, int64_t now, Tape& out,
               std::string* error, const std::string& cipher = "", size_t key_bytes = 32);
std::string format_tape(const Tape& t);
bool parse_tape(const std::string& text, Tape& out, std::string* error);

// The key tape file (above) around a tape's text, and back. open_tape_file
// takes either a framed file or plain tape text, and gives the text. An MSC
// tape is refused by its head label, before any other byte is read.
std::string tape_file(const std::string& tape_text);
bool open_tape_file(const std::string& bytes, std::string& tape_text, std::string* error);
// Whether bytes start the way a framed tape file does (ours or MSC's).
bool looks_framed(const std::string& bytes);

// MSC's five key labels, for one key of a tape:
//   KEY_CKR  QXR01                       the key's short title (its indicator)
//   KEYNAME  QXR01 QXR1126               short title, the tape, and MMYY it starts
//   KEYDESC  PRACTICE NET KEY 1 OF 31
//   INTRVAL  Daily                       Daily, Weekly, Monthly or Every N hours
//   CREATED  23Nov2026@10:40:00          when the tape was made, UTC
struct Labels {
    std::string ckr, name, desc, interval, created;
};
Labels key_labels(const Tape& t, size_t index);
// "KEY_CKR:QXR01" ... "CREATED:23Nov2026@10:40:00", one label a line.
std::vector<std::string> label_lines(const Labels& l);

// A key's check value: four letters and figures (no I, O, 0 or 1) from
// SHA-256 over the indicator and the key. Two stations read theirs to each
// other to confirm they hold the same key; it gives away nothing usable.
std::string key_check(const std::string& indicator, const std::string& secret);

// What a station holds. Never shows the key itself.
struct Info {
    std::string indicator, net, tape;
    int64_t from = 0, until = 0;
    std::string state;  // "current" (sends now), "next" (starts later), "receive only" (ended, still reads)
    std::string check;  // the slot's key check (key_check() for pna-aes)
    std::string cipher; // "pna-aes" or the module's name
    bool usable = true; // false when the key is for a cipher not in the slot
};

struct ImportResult {
    std::string net, tape;
    int added = 0, already = 0, expired = 0;
};

// The station's keys, kept in one file (mode 0600) beside its database. Every
// change rewrites it whole: the new file is written and synced, the old one is
// overwritten with zeros and synced, then the new one takes its name. Flash
// storage may still keep old blocks: docs/crypto.md says what that means.
class Keyring {
public:
    explicit Keyring(std::string path);
    ~Keyring();
    // Reads the file (none yet is fine). False with *error when it is damaged.
    bool load(std::string* error);
    const std::string& path() const { return path_; }

    // Adds a tape's keys. Keys already past their receive hold are skipped
    // (counted as expired); a key already held is skipped; an indicator held
    // with a different key refuses the whole tape.
    bool import_tape(const std::string& text, ImportResult& r, std::string* error);

    std::vector<Info> list();
    std::vector<Info> for_sending();  // the keys current now
    // The key to send with now, or false with *error.
    bool send_key(const std::string& indicator, std::string& secret, std::string* error);
    // The key to read traffic sent with this indicator, if held and not past its hold.
    bool receive_key(const std::string& indicator, std::string& secret);

    // Zeroizes keys past their receive hold. Returns one log line per key.
    std::vector<std::string> sweep();
    // Zeroizes by the operator's word: one indicator ("QXR01") or a whole tape ("QXR").
    int zeroize(const std::string& which, std::string* error);

    void set_clock(std::function<int64_t()> c) { clock_ = std::move(c); }

    // The cipher in the slot: its name, a test that a key loads in it, and
    // its key check. Until set, our own pna-aes and key_check().
    using KeyTest = std::function<bool(const std::string& indicator, const std::string& key, std::string* error)>;
    using CheckFn = std::function<std::string(const std::string& indicator, const std::string& key)>;
    void set_cipher(const std::string& name, KeyTest accept, CheckFn check);
    std::string cipher() const;
    int64_t now() const;

private:
    bool save_locked(std::string* error);
    std::string path_;
    std::mutex mu_;
    std::vector<Key> keys_;
    std::function<int64_t()> clock_;
    std::string cipher_;  // "" = pna-aes
    KeyTest accept_;
    CheckFn check_;
};

// "" for our own cipher (any spelling of pna-aes, or none), else the
// module's name in lower case.
std::string cipher_id(const std::string& name);

}  // namespace keys
}  // namespace pna
