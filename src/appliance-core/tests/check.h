// Tiny test helper: CHECK(cond) records a failure and keeps going.
#pragma once

#include <iostream>

namespace check {
inline int& failures() {
    static int n = 0;
    return n;
}
inline int report(const char* suite) {
    if (failures()) {
        std::cout << suite << ": " << failures() << " check(s) failed\n";
        return 1;
    }
    std::cout << suite << ": all checks passed\n";
    return 0;
}
}  // namespace check

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            ++check::failures();                                                         \
            std::cout << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
        }                                                                                \
    } while (0)
