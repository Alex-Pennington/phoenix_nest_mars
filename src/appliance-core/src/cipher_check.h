// The cipher slot's checks (docs/cipher-slot.md, "The harness"). Any module
// must pass them before the appliance installs it, and pna-cipher-check runs
// them from the command line. They use made-up keys and text only.
#pragma once

#include "cipher_slot.h"

#include <string>
#include <vector>

namespace pna {
namespace cipher {

enum class Result { pass, warn, fail };

struct Check {
    std::string what;
    Result result = Result::pass;
    std::string detail;
};

std::vector<Check> run_checks(const Slot& s);
// True when nothing failed (warnings allowed).
bool passed(const std::vector<Check>& checks);

}  // namespace cipher
}  // namespace pna
