// The cipher slot (docs/cipher-slot.md): where the cipher that codress uses
// plugs in. One module fills it at a time. Our own pna-aes is built in and
// is the default; a net that uses another cipher hands its members a module
// in the handout packet, and the appliance loads it at startup.
//
// A module is a shared library with the C table in sdk/pna_cipher.h: a name,
// its output form, its key size, and four functions (load_key, key_check,
// encrypt, decrypt). Slot wraps the table: it owns every buffer, wipes keys
// and text after each call, calls one function at a time, and checks what a
// module hands back.
//
// An installed module lives in a folder beside the station's database
// (STATION.cipher/): module.so, any libraries it needs in lib/, and a pin
// file with the module's name and the SHA-256 of that set of files. The pin
// is written only after the set's SHA-256 matched what the handout named and
// the module passed the slot's checks. At startup the set is hashed again; a
// changed file means codress stays off (fail closed), never a quiet fall back
// to another cipher.
#pragma once

#include "../sdk/pna_cipher.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pna {
namespace cipher {

constexpr const char* kBuiltinName = "pna-aes";

struct Info {
    std::string name, version;
    bool text = false;      // PNA_CIPHER_TEXT: the module writes its own on-air text
    size_t key_bytes = 0;
    std::string source;     // "built in", or the module's path
    std::string set_hash;   // SHA-256 of the installed set (hex), "" when built in
};

class Slot {
public:
    // Our own cipher.
    static std::shared_ptr<Slot> builtin();
    // A module from a shared library. Null with *error when it won't load or
    // its table is not one the slot takes.
    static std::shared_ptr<Slot> open(const std::string& so_path, std::string* error);
    // An empty slot: codress refuses with this reason until it is filled.
    // name is the module that should have been there, when known.
    static std::shared_ptr<Slot> unusable(const std::string& why, const std::string& name = "none");
    // Any table (tests, and the built-in module).
    static std::shared_ptr<Slot> from_table(const pna_cipher_module* m, const std::string& source, std::string* error);

    ~Slot();
    bool usable() const { return m_ != nullptr; }
    const std::string& why_unusable() const { return why_; }
    const Info& info() const { return info_; }
    bool is_builtin() const { return info_.name == kBuiltinName && info_.set_hash.empty(); }
    const pna_cipher_module* table() const { return m_; }

    // The four functions, wrapped. key is the key as it is on the tape; each
    // call loads it, uses it and wipes the ready form.
    bool load_key(const std::string& indicator, const std::string& key, std::string& ready, std::string* error) const;
    bool key_check(const std::string& indicator, const std::string& key, std::string& check, std::string* error) const;
    bool encrypt(const std::string& indicator, const std::string& key, const std::string& dtg,
                 const std::string& text, std::string& out, std::string* error) const;
    // *refused is set when the module says the cipher does not check.
    bool decrypt(const std::string& indicator, const std::string& key, const std::string& dtg, const std::string& in,
                 std::string& text, std::string* error, bool* refused = nullptr) const;

    void set_hash(const std::string& h) { info_.set_hash = h; }

private:
    Slot() = default;
    const pna_cipher_module* m_ = nullptr;
    void* handle_ = nullptr;
    Info info_;
    std::string why_;
    mutable std::mutex mu_;
};

// Module names: 1 to 32 of a-z, 0-9 and '-'. Handouts are upper case, so
// names compare without case; this gives the lower-case form, or "" when
// the name is not allowed.
std::string normal_name(const std::string& name);

// The SHA-256 (hex) of a module set: module.so and every file under lib/,
// in a folder. Over "sha256  path" lines sorted by path, so it covers names
// too. "" with *error when module.so is missing or a file can't be read.
std::string set_hash(const std::string& dir, std::string* error);

// Installs the module set in `from` (a packet's cipher/ folder) into `dir`.
// Refused when its set hash isn't `expect_hash` (when given), when it won't
// load, or when it fails the slot's checks (cipher_check.h). On success the
// pin is written and the loaded slot is returned.
std::shared_ptr<Slot> install(const std::string& from, const std::string& dir, const std::string& expect_hash,
                              std::string* error);
// Removes an installed module (the built-in cipher is used again).
bool remove_installed(const std::string& dir, std::string* error);
// What fills the slot at startup: the installed module when there is a pin,
// else the built-in cipher. A pinned set that changed, or won't load, gives
// an unusable slot whose reason says so.
std::shared_ptr<Slot> load_installed(const std::string& dir);

}  // namespace cipher
}  // namespace pna
