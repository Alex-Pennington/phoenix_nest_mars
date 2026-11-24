// pna-cipher-check: checks a cipher module against the cipher slot
// (docs/cipher-slot.md) and prints PASS, WARN or FAIL for each check.
//
//   pna-cipher-check MODULE.so      a module (an adapter author's build)
//   pna-cipher-check --builtin      the appliance's own pna-aes
//
// Exit status 0 when nothing failed (warnings allowed), 1 when something
// failed, 2 on a usage error. It uses made-up keys and text only.
#include "../src/cipher_check.h"
#include "../src/cipher_slot.h"

#include <iostream>
#include <string>

using namespace pna;

int main(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        std::cerr << "usage: pna-cipher-check MODULE.so | --builtin\n";
        return 2;
    }
    std::string arg = argv[1], err;
    std::shared_ptr<cipher::Slot> s;
    if (arg == "--builtin") {
        s = cipher::Slot::builtin();
    } else {
        s = cipher::Slot::open(arg.find('/') == std::string::npos ? "./" + arg : arg, &err);
        if (!s) s = cipher::Slot::unusable(err);
    }
    std::cout << "pna-cipher-check: " << (s->usable() ? s->info().name + " " + s->info().version : arg) << " ("
              << (s->usable() ? s->info().source : "not loaded") << ")\n";
    auto checks = cipher::run_checks(*s);
    int pass = 0, warn = 0, fail = 0;
    for (auto& c : checks) {
        const char* tag = c.result == cipher::Result::pass ? "PASS" : c.result == cipher::Result::warn ? "WARN" : "FAIL";
        (c.result == cipher::Result::pass ? pass : c.result == cipher::Result::warn ? warn : fail)++;
        std::cout << tag << "  " << c.what << (c.detail.empty() ? "" : ": " + c.detail) << "\n";
    }
    std::cout << "RESULT: " << (fail ? "FAIL" : "PASS") << " (" << pass << " passed, " << warn << " warning"
              << (warn == 1 ? "" : "s") << ", " << fail << " failed)\n";
    return fail ? 1 : 0;
}
