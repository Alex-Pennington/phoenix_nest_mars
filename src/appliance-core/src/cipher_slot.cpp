// The cipher slot. See cipher_slot.h and docs/cipher-slot.md.
#include "cipher_slot.h"

#include "../modules/pna_aes.h"
#include "cipher_check.h"
#include "crypto.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace pna {
namespace cipher {

namespace {

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

constexpr size_t kErr = 256;

std::string reason(const char* err, const std::string& fallback) {
    std::string r(err, strnlen(err, kErr));
    for (auto& c : r)
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = ' ';
    while (!r.empty() && r.back() == ' ') r.pop_back();
    return r.empty() ? fallback : r;
}

bool printable(const char* s, size_t max) {
    if (!s) return false;
    size_t n = strnlen(s, max + 1);
    if (n == 0 || n > max) return false;
    for (size_t i = 0; i < n; ++i)
        if (static_cast<unsigned char>(s[i]) < 32 || static_cast<unsigned char>(s[i]) > 126) return false;
    return true;
}

bool on_air_text(const std::string& t) {
    for (char c : t)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '\n')) return false;
    return t.find_first_not_of(" \n") != std::string::npos;
}

// Calls fn(buffer, &len) with `first` bytes of room, and once more with what
// the module asks for when it answers PNA_CIPHER_SHORT. Every buffer that is
// let go is wiped.
template <typename Fn>
int with_room(size_t first, size_t limit, std::string& out, Fn fn) {
    std::string buf(std::min(first, limit), '\0');
    size_t len = buf.size();
    int rc = fn(reinterpret_cast<unsigned char*>(&buf[0]), &len);
    if (rc == PNA_CIPHER_SHORT) {
        if (len <= buf.size() || len > limit) {
            crypto::wipe(buf);
            return PNA_CIPHER_FAILED;
        }
        crypto::wipe(buf);
        buf.assign(len, '\0');
        rc = fn(reinterpret_cast<unsigned char*>(&buf[0]), &len);
    }
    if (rc == PNA_CIPHER_OK && len > buf.size()) rc = PNA_CIPHER_FAILED;
    if (rc == PNA_CIPHER_OK) {
        out.assign(buf.data(), len);
    }
    crypto::wipe(buf);
    return rc;
}

std::string read_small(const std::string& path, bool* exists) {
    std::ifstream f(path, std::ios::binary);
    *exists = static_cast<bool>(f);
    std::ostringstream s;
    if (f) s << f.rdbuf();
    return s.str();
}

}  // namespace

std::string normal_name(const std::string& name) {
    if (name.empty() || name.size() > 32) return "";
    std::string o;
    for (char c : name) {
        char l = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (!((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '-')) return "";
        o += l;
    }
    return o;
}

std::shared_ptr<Slot> Slot::from_table(const pna_cipher_module* m, const std::string& source, std::string* error) {
    if (!m) return fail(error, "the module gave no table"), nullptr;
    if (m->abi != PNA_CIPHER_ABI)
        return fail(error, "the module was built for slot version " + std::to_string(m->abi) + "; this appliance takes version " +
                               std::to_string(PNA_CIPHER_ABI)),
               nullptr;
    if (!printable(m->name, 32) || normal_name(m->name) != m->name)
        return fail(error, "the module's name must be 1 to 32 of a-z, 0-9 and -"), nullptr;
    if (!printable(m->version, 32)) return fail(error, "the module's version must be 1 to 32 printable characters"), nullptr;
    if (m->output != PNA_CIPHER_BYTES && m->output != PNA_CIPHER_TEXT)
        return fail(error, "the module's output form must be PNA_CIPHER_BYTES or PNA_CIPHER_TEXT"), nullptr;
    if (m->key_bytes < 1 || m->key_bytes > PNA_CIPHER_MAX_KEY)
        return fail(error, "the module's key size must be 1 to " + std::to_string(PNA_CIPHER_MAX_KEY) + " bytes"), nullptr;
    if (!m->load_key || !m->key_check || !m->encrypt || !m->decrypt)
        return fail(error, "the module's table is missing one of load_key, key_check, encrypt, decrypt"), nullptr;
    std::shared_ptr<Slot> s(new Slot());
    s->m_ = m;
    s->info_.name = m->name;
    s->info_.version = m->version;
    s->info_.text = m->output == PNA_CIPHER_TEXT;
    s->info_.key_bytes = m->key_bytes;
    s->info_.source = source;
    return s;
}

std::shared_ptr<Slot> Slot::builtin() {
    static std::shared_ptr<Slot> b = from_table(pna_aes_module(), "built in", nullptr);
    return b;
}

// The loader hands back a library it already holds when the path matches, so
// a module put in place where another one once sat would read as the old
// one. Each load goes through a hard link with a name of its own, beside
// module.so so that $ORIGIN/lib still finds the module's own libraries; the
// link goes once the library is mapped.
static void* open_fresh(const std::string& so_path) {
    static std::atomic<unsigned> n{0};
    fs::path p(so_path);
    fs::path link = p.parent_path() / (".load-" + std::to_string(::getpid()) + "-" + std::to_string(++n) + ".so");
    std::error_code ec;
    fs::create_hard_link(p, link, ec);
    if (ec) return ::dlopen(so_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    void* h = ::dlopen(link.c_str(), RTLD_NOW | RTLD_LOCAL);
    fs::remove(link, ec);
    return h;
}

std::shared_ptr<Slot> Slot::open(const std::string& so_path, std::string* error) {
    void* h = open_fresh(so_path);
    if (!h) {
        const char* e = ::dlerror();
        return fail(error, "the module won't load: " + std::string(e ? e : so_path)), nullptr;
    }
    auto entry = reinterpret_cast<pna_cipher_entry>(::dlsym(h, PNA_CIPHER_ENTRY));
    if (!entry) {
        ::dlclose(h);
        return fail(error, std::string("the library has no ") + PNA_CIPHER_ENTRY + "(): it is not a cipher module"), nullptr;
    }
    auto s = from_table(entry(), so_path, error);
    if (!s) {
        ::dlclose(h);
        return nullptr;
    }
    s->handle_ = h;
    return s;
}

std::shared_ptr<Slot> Slot::unusable(const std::string& why, const std::string& name) {
    std::shared_ptr<Slot> s(new Slot());
    s->why_ = why;
    s->info_.name = normal_name(name).empty() ? "none" : normal_name(name);
    s->info_.source = "none";
    return s;
}

Slot::~Slot() {
    // A module's code may still be referenced by a static it set up; leaving
    // the library mapped is the safe choice for a long-running appliance.
    (void)handle_;
}

bool Slot::load_key(const std::string& indicator, const std::string& key, std::string& ready, std::string* error) const {
    ready.clear();
    if (!m_) return fail(error, why_);
    if (key.empty() || key.size() > PNA_CIPHER_MAX_KEY) return fail(error, "the key is empty or too long");
    std::lock_guard<std::mutex> g(mu_);
    char err[kErr] = {0};
    int rc = with_room(PNA_CIPHER_MAX_KEY, PNA_CIPHER_MAX_KEY, ready, [&](unsigned char* b, size_t* n) {
        return m_->load_key(indicator.c_str(), reinterpret_cast<const unsigned char*>(key.data()), key.size(), b, n,
                            err, kErr);
    });
    err[kErr - 1] = '\0';
    if (rc == PNA_CIPHER_OK && ready.empty()) rc = PNA_CIPHER_FAILED;
    if (rc == PNA_CIPHER_OK) return true;
    crypto::wipe(ready);
    return fail(error, std::string(rc == PNA_CIPHER_BAD_KEY ? "key " + indicator + " refused by " + info_.name + ": "
                                                           : info_.name + " could not load key " + indicator + ": ") +
                           reason(err, "no reason given"));
}

bool Slot::key_check(const std::string& indicator, const std::string& key, std::string& check, std::string* error) const {
    check.clear();
    std::string ready;
    if (!load_key(indicator, key, ready, error)) return false;
    std::lock_guard<std::mutex> g(mu_);
    char err[kErr] = {0};
    int rc = with_room(PNA_CIPHER_MAX_CHECK, PNA_CIPHER_MAX_CHECK, check, [&](unsigned char* b, size_t* n) {
        return m_->key_check(indicator.c_str(), reinterpret_cast<const unsigned char*>(ready.data()), ready.size(),
                             reinterpret_cast<char*>(b), n, err, kErr);
    });
    crypto::wipe(ready);
    err[kErr - 1] = '\0';
    if (rc != PNA_CIPHER_OK) return fail(error, info_.name + " gave no key check: " + reason(err, "no reason given"));
    bool ok = !check.empty();
    for (char c : check) ok = ok && ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
    if (!ok) {
        check.clear();
        return fail(error, info_.name + "'s key check must be 1 to 16 of A-Z and 0-9");
    }
    return true;
}

bool Slot::encrypt(const std::string& indicator, const std::string& key, const std::string& dtg,
                   const std::string& text, std::string& out, std::string* error) const {
    out.clear();
    if (text.size() > PNA_CIPHER_MAX_TEXT) return fail(error, "the message is too long for the cipher slot");
    std::string ready;
    if (!load_key(indicator, key, ready, error)) return false;
    std::lock_guard<std::mutex> g(mu_);
    char err[kErr] = {0};
    int rc = with_room(text.size() * 2 + 256, PNA_CIPHER_MAX_TEXT * 4, out, [&](unsigned char* b, size_t* n) {
        return m_->encrypt(indicator.c_str(), reinterpret_cast<const unsigned char*>(ready.data()), ready.size(),
                           dtg.c_str(), reinterpret_cast<const unsigned char*>(text.data()), text.size(), b, n, err,
                           kErr);
    });
    crypto::wipe(ready);
    err[kErr - 1] = '\0';
    if (rc != PNA_CIPHER_OK) return fail(error, info_.name + " could not encipher: " + reason(err, "no reason given"));
    if (out.empty()) return fail(error, info_.name + " gave nothing back");
    if (info_.text && !on_air_text(out)) {
        out.clear();
        return fail(error, info_.name + " gave text with characters other than A-Z, 0-9, space and newline");
    }
    return true;
}

bool Slot::decrypt(const std::string& indicator, const std::string& key, const std::string& dtg, const std::string& in,
                   std::string& text, std::string* error, bool* refused) const {
    text.clear();
    if (refused) *refused = false;
    if (in.empty() || in.size() > PNA_CIPHER_MAX_TEXT * 4) return fail(error, "nothing to read, or too much");
    std::string ready;
    if (!load_key(indicator, key, ready, error)) return false;
    std::lock_guard<std::mutex> g(mu_);
    char err[kErr] = {0};
    int rc = with_room(in.size() + 256, PNA_CIPHER_MAX_TEXT, text, [&](unsigned char* b, size_t* n) {
        return m_->decrypt(indicator.c_str(), reinterpret_cast<const unsigned char*>(ready.data()), ready.size(),
                           dtg.c_str(), reinterpret_cast<const unsigned char*>(in.data()), in.size(), b, n, err, kErr);
    });
    crypto::wipe(ready);
    err[kErr - 1] = '\0';
    if (rc == PNA_CIPHER_OK) return true;
    crypto::wipe(text);
    if (rc == PNA_CIPHER_REFUSED) {
        if (refused) *refused = true;
        return fail(error, "the groups do not check with key " + indicator + ": " +
                               reason(err, "damaged in transit, changed, or a different key"));
    }
    return fail(error, info_.name + " could not read it: " + reason(err, "no reason given"));
}

// ---- Installed modules

std::string set_hash(const std::string& dir, std::string* error) {
    std::vector<std::pair<std::string, std::string>> files;
    auto add = [&](const fs::path& p, const std::string& rel) -> bool {
        std::error_code ec;
        auto st = fs::symlink_status(p, ec);
        if (ec || !fs::is_regular_file(st)) return fail(error, rel + " is not a plain file");
        bool exists = false;
        std::string bytes = read_small(p.string(), &exists);
        if (!exists) return fail(error, "cannot read " + rel);
        files.emplace_back(rel, crypto::sha256_hex(bytes));
        return true;
    };
    if (!add(fs::path(dir) / "module.so", "module.so")) {
        if (error) *error = "no module.so in " + dir;
        return "";
    }
    fs::path lib = fs::path(dir) / "lib";
    std::error_code ec;
    if (fs::exists(fs::symlink_status(lib, ec))) {
        if (!fs::is_directory(fs::symlink_status(lib, ec))) return fail(error, "lib is not a folder"), "";
        for (auto it = fs::recursive_directory_iterator(lib, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            auto st = it->symlink_status(ec);
            if (fs::is_directory(st)) continue;
            std::string rel = "lib/" + fs::relative(it->path(), lib, ec).generic_string();
            if (!add(it->path(), rel)) return "";
        }
        if (ec) return fail(error, "cannot list " + lib.string()), "";
    }
    std::sort(files.begin(), files.end());
    std::string list;
    for (auto& f : files) list += f.second + "  " + f.first + "\n";
    return crypto::sha256_hex(list);
}

std::shared_ptr<Slot> install(const std::string& from, const std::string& dir, const std::string& expect_hash,
                              std::string* error) {
    std::string h = set_hash(from, error);
    if (h.empty()) return nullptr;
    std::string want = expect_hash;
    for (auto& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!want.empty() && want != h)
        return fail(error, "the cipher module in the packet is not the one the handout names (its SHA-256 differs): "
                           "it was changed, or it belongs to another handout"),
               nullptr;
    std::error_code ec;
    std::string tmp = dir + ".new";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    if (ec) return fail(error, "cannot make " + tmp + ": " + ec.message()), nullptr;
    fs::copy(fs::path(from) / "module.so", fs::path(tmp) / "module.so", ec);
    if (!ec && fs::exists(fs::path(from) / "lib"))
        fs::copy(fs::path(from) / "lib", fs::path(tmp) / "lib", fs::copy_options::recursive, ec);
    if (ec || set_hash(tmp, nullptr) != h) {
        fs::remove_all(tmp, ec);
        return fail(error, "the module did not copy cleanly"), nullptr;
    }
    std::string err;
    auto s = Slot::open((fs::path(tmp) / "module.so").string(), &err);
    if (!s) {
        fs::remove_all(tmp, ec);
        return fail(error, err), nullptr;
    }
    if (s->info().name == kBuiltinName) {
        fs::remove_all(tmp, ec);
        return fail(error, std::string("a module may not take the built-in cipher's name (") + kBuiltinName + ")"), nullptr;
    }
    for (auto& c : run_checks(*s))
        if (c.result == Result::fail) {
            fs::remove_all(tmp, ec);
            return fail(error, s->info().name + " failed the slot's checks: " + c.what + ": " + c.detail +
                                   " (run pna-cipher-check on it for the whole list)"),
                   nullptr;
        }
    {
        std::ofstream p(fs::path(tmp) / "pin", std::ios::trunc);
        p << s->info().name << " " << h << "\n";
        if (!p) {
            fs::remove_all(tmp, ec);
            return fail(error, "cannot write the pin"), nullptr;
        }
    }
    fs::remove_all(dir, ec);
    fs::rename(tmp, dir, ec);
    if (ec) return fail(error, "cannot put the module in place: " + ec.message()), nullptr;
    // Load it again from where it now lives, so the source is right.
    auto placed = load_installed(dir);
    if (!placed->usable()) return fail(error, placed->why_unusable()), nullptr;
    return placed;
}

bool remove_installed(const std::string& dir, std::string* error) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    if (ec) return fail(error, "cannot remove " + dir + ": " + ec.message());
    return true;
}

std::shared_ptr<Slot> load_installed(const std::string& dir) {
    bool exists = false;
    std::string pin = read_small((fs::path(dir) / "pin").string(), &exists);
    if (!exists) return Slot::builtin();
    std::istringstream in(pin);
    std::string name, hash;
    in >> name >> hash;
    if (normal_name(name).empty() || hash.size() != 64)
        return Slot::unusable("the cipher module's pin in " + dir + " is damaged: codress is off until the net's packet is loaded again");
    std::string err;
    std::string now = set_hash(dir, &err);
    if (now != hash)
        return Slot::unusable("the installed cipher module " + name + " changed since it was installed" +
                              (now.empty() ? " (" + err + ")" : "") +
                              ": codress is off until the net's packet is loaded again",
                              name);
    auto s = Slot::open((fs::path(dir) / "module.so").string(), &err);
    if (!s) return Slot::unusable("the installed cipher module " + name + " won't load (" + err + "): codress is off", name);
    if (s->info().name != name)
        return Slot::unusable("the installed cipher module calls itself " + s->info().name + ", but was installed as " +
                              name + ": codress is off",
                              name);
    s->set_hash(hash);
    return s;
}

}  // namespace cipher
}  // namespace pna
