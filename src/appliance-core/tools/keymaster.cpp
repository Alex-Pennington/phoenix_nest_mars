// pna-keymaster: makes a key tape for one net (keys.h, docs/crypto.md).
//
//   pna-keymaster --net "PRACTICE NET" --start 20261124 [--days 31]
//                 [--period-hours 24] [--text] --out /media/usb/practice-net.ekt
//
// Each period gets a fresh random 256-bit key from OpenSSL. The tape is
// written once, mode 0600, and never over an existing file, as a key tape
// file framed like MSC's .ekt (keys.h); --text writes the plain tape text. Copy it to a USB
// stick for each station of the net; each station loads it with
// "appliance-cli keys import FILE". The keymaster keeps no copy: when the
// tape is lost, make a new one.
//
// For a net that uses another cipher in the slot (docs/cipher-slot.md),
// --cipher FOLDER (the folder holding the net's module.so) makes the tape
// for that module: its name goes on the tape, and each key is as long as
// the module's keys.
//
//   pna-keymaster packet --handout FILE [--tape FILE] [--cipher FOLDER] --out FOLDER
//
// builds a member's handout packet (docs/cipher-slot.md): handout.txt, the
// tape if given, and for a net's own cipher, cipher/ with module.so, lib/,
// module.sha256 and ABOUT.txt. The module's SHA-256 must be the one on the
// handout's CIPHER line, or no packet is made.
#include "../src/cipher_slot.h"
#include "../src/crypto.h"
#include "../src/keys.h"
#include "../src/station.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace pna;

namespace {

int usage() {
    std::cerr << "usage: pna-keymaster --net NAME --start YYYYMMDD[HHMM] [--days N | --count N]\n"
                 "                     [--period-hours H] [--cipher FOLDER] [--text] --out FILE\n"
                 "       pna-keymaster packet --handout FILE [--tape FILE] [--cipher FOLDER] --out FOLDER\n"
                 "  --net           the net's name: A-Z, 0-9, space, - and / (up to 40)\n"
                 "  --start         when the first key starts, UTC (YYYYMMDD is 0000Z that day)\n"
                 "  --days/--count  how many keys (1 to 99); a key a day by default\n"
                 "  --period-hours  how long each key lasts (default 24)\n"
                 "  --cipher        the folder with the net's cipher module (module.so), when the net\n"
                 "                  doesn't use the built-in pna-aes\n"
                 "  --text          write the plain tape text, not the framed key tape file\n"
                 "  --out           the tape file to write; it must not exist yet\n";
    return 2;
}

std::string read_file(const std::string& path, bool* ok) {
    std::ifstream f(path, std::ios::binary);
    *ok = static_cast<bool>(f);
    std::ostringstream ss;
    if (f) ss << f.rdbuf();
    return ss.str();
}

// The module in a folder (or a path to module.so itself), loaded to read its
// name and key size.
std::shared_ptr<cipher::Slot> module_in(std::string folder, std::string& dir, std::string* error) {
    namespace fs = std::filesystem;
    if (fs::path(folder).filename() == "module.so") folder = fs::path(folder).parent_path().string();
    dir = folder.empty() ? "." : folder;
    return cipher::Slot::open((fs::path(dir) / "module.so").string(), error);
}

int packet(int argc, char** argv) {
    namespace fs = std::filesystem;
    std::string handout, tape, cfolder, out;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--handout") handout = next();
        else if (a == "--tape") tape = next();
        else if (a == "--cipher") cfolder = next();
        else if (a == "--out") out = next();
        else return usage();
    }
    if (handout.empty() || out.empty()) return usage();
    bool ok = false;
    std::string text = read_file(handout, &ok), err;
    if (!ok) {
        std::cerr << "pna-keymaster: cannot read " << handout << "\n";
        return 1;
    }
    station::Handout h;
    if (!station::parse_handout(text, h, &err)) {
        std::cerr << "pna-keymaster: " << handout << " is not a good handout: " << err << "\n";
        return 1;
    }
    std::string dir;
    std::string hash;
    if (!h.cipher.empty()) {
        if (cfolder.empty()) {
            std::cerr << "pna-keymaster: this handout names the " << h.cipher
                      << " cipher: give its module with --cipher FOLDER\n";
            return 2;
        }
        auto s = module_in(cfolder, dir, &err);
        if (!s) {
            std::cerr << "pna-keymaster: " << err << "\n";
            return 1;
        }
        hash = cipher::set_hash(dir, &err);
        if (hash.empty() || hash != h.cipher_hash || s->info().name != h.cipher) {
            std::cerr << "pna-keymaster: the module in " << dir << " is not the one this handout names ("
                      << h.cipher << "); no packet made\n";
            return 1;
        }
    } else if (!cfolder.empty()) {
        std::cerr << "pna-keymaster: this handout is for the built-in pna-aes; it carries no module\n";
        return 2;
    }
    std::error_code ec;
    if (fs::exists(out, ec)) {
        std::cerr << "pna-keymaster: " << out << " already exists; a packet is never written over\n";
        return 1;
    }
    fs::create_directories(out, ec);
    {
        std::ofstream f(fs::path(out) / "handout.txt", std::ios::binary);
        f << text;
        if (!f) ec = std::make_error_code(std::errc::io_error);
    }
    if (!ec && !tape.empty()) fs::copy_file(tape, fs::path(out) / "tape.ekt", ec);
    if (!ec && !h.cipher.empty()) {
        fs::path c = fs::path(out) / "cipher";
        fs::create_directories(c, ec);
        if (!ec) fs::copy_file(fs::path(dir) / "module.so", c / "module.so", ec);
        if (!ec && fs::exists(fs::path(dir) / "lib")) fs::copy(fs::path(dir) / "lib", c / "lib", fs::copy_options::recursive, ec);
        if (!ec && fs::exists(fs::path(dir) / "ABOUT.txt")) fs::copy_file(fs::path(dir) / "ABOUT.txt", c / "ABOUT.txt", ec);
        if (!ec) {
            std::ofstream f(c / "module.sha256");
            f << hash << "\n";
        }
        if (!ec && cipher::set_hash(c.string(), nullptr) != hash) ec = std::make_error_code(std::errc::io_error);
    }
    if (ec) {
        std::cerr << "pna-keymaster: writing the packet failed: " << ec.message() << "\n";
        fs::remove_all(out, ec);
        return 1;
    }
    std::cout << "handout packet for " << h.for_station << " (" << h.net << ") written to " << out
              << (h.cipher.empty() ? "" : ", with the " + h.cipher + " cipher module") << "\n"
              << "The member loads it with: appliance-cli handout import " << out << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "packet") return packet(argc, argv);
    std::string net, start, out, cfolder;
    int count = 31, period = 24;
    bool plain = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        try {
            if (a == "--net") net = next();
            else if (a == "--start") start = next();
            else if (a == "--days" || a == "--count") count = std::stoi(next());
            else if (a == "--period-hours") period = std::stoi(next());
            else if (a == "--out") out = next();
            else if (a == "--text") plain = true;
            else if (a == "--cipher") cfolder = next();
            else return usage();
        } catch (...) {
            return usage();
        }
    }
    if (net.empty() || start.empty() || out.empty()) return usage();
    for (auto& c : net) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::string st = start;
    if (st.size() == 8) st += "0000";
    if (!st.empty() && st.back() != 'Z') st += "Z";
    int64_t t0 = 0;
    if (!keys::parse_time(st, t0)) {
        std::cerr << "--start must be YYYYMMDD or YYYYMMDDHHMM, UTC\n";
        return 2;
    }
    keys::Tape tape;
    std::string err, cname;
    size_t kbytes = 32;
    if (!cfolder.empty()) {
        std::string dir;
        auto s = module_in(cfolder, dir, &err);
        if (!s) {
            std::cerr << "pna-keymaster: " << err << "\n";
            return 1;
        }
        cname = s->info().name;
        kbytes = s->info().key_bytes;
    }
    if (!keys::make_tape(net, t0, count, period, static_cast<int64_t>(std::time(nullptr)), tape, &err, cname, kbytes)) {
        std::cerr << "pna-keymaster: " << err << "\n";
        return 2;
    }
    std::string text = keys::format_tape(tape);
    if (!plain) {
        std::string framed = keys::tape_file(text);
        crypto::wipe(text);
        text = std::move(framed);
    }
    for (auto& k : tape.keys) crypto::wipe(k.secret);
    int fd = ::open(out.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        crypto::wipe(text);
        std::cerr << "pna-keymaster: cannot create " << out << ": " << std::strerror(errno)
                  << (errno == EEXIST ? " (a tape is never written over; pick a new name)" : "") << "\n";
        return 1;
    }
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
    crypto::wipe(text);
    if (!ok) {
        ::unlink(out.c_str());
        std::cerr << "pna-keymaster: writing " << out << " failed\n";
        return 1;
    }
    std::cout << "key tape " << tape.tape << " for " << tape.net << ": " << tape.keys.size() << " key(s), "
              << tape.tape << "01 from " << keys::time_text(tape.keys.front().from) << " to "
              << tape.tape << (tape.keys.size() < 10 ? "0" : "") << tape.keys.size() << " ending "
              << keys::time_text(tape.keys.back().until) << ", written to " << out << "\n"
              << "Copy it to each station of the net and load it there with: appliance-cli keys import FILE\n";
    return 0;
}
