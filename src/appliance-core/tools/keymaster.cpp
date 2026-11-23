// pna-keymaster: makes a key tape for one net (keys.h, docs/crypto.md).
//
//   pna-keymaster --net "PRACTICE NET" --start 20261124 [--days 31]
//                 [--period-hours 24] --out /media/usb/practice-net.tape
//
// Each period gets a fresh random 256-bit key from OpenSSL. The tape is
// written once, mode 0600, and never over an existing file. Copy it to a USB
// stick for each station of the net; each station loads it with
// "appliance-cli keys import FILE". The keymaster keeps no copy: when the
// tape is lost, make a new one.
#include "../src/crypto.h"
#include "../src/keys.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

using namespace pna;

namespace {

int usage() {
    std::cerr << "usage: pna-keymaster --net NAME --start YYYYMMDD[HHMM] [--days N | --count N]\n"
                 "                     [--period-hours H] --out FILE\n"
                 "  --net           the net's name: A-Z, 0-9, space, - and / (up to 40)\n"
                 "  --start         when the first key starts, UTC (YYYYMMDD is 0000Z that day)\n"
                 "  --days/--count  how many keys (1 to 99); a key a day by default\n"
                 "  --period-hours  how long each key lasts (default 24)\n"
                 "  --out           the tape file to write; it must not exist yet\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string net, start, out;
    int count = 31, period = 24;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        try {
            if (a == "--net") net = next();
            else if (a == "--start") start = next();
            else if (a == "--days" || a == "--count") count = std::stoi(next());
            else if (a == "--period-hours") period = std::stoi(next());
            else if (a == "--out") out = next();
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
    std::string err;
    if (!keys::make_tape(net, t0, count, period, static_cast<int64_t>(std::time(nullptr)), tape, &err)) {
        std::cerr << "pna-keymaster: " << err << "\n";
        return 2;
    }
    std::string text = keys::format_tape(tape);
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
