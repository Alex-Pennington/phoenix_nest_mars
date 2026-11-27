// The service tables the tests route by: tests/fixtures/services, in the
// format of greybeard-ale docs/services.md (amateur as it ships; mars with
// the structure and made-up values only). They are the test user's, not
// root's, so the loader's root check is lifted here only.
#pragma once

#include "../src/core.h"
#include "../src/services.h"

#include <memory>
#include <string>
#include <vector>

#ifndef PNA_SOURCE_DIR
#error "PNA_SOURCE_DIR is set by CMakeLists.txt for every test"
#endif

inline std::string test_services_dir() { return std::string(PNA_SOURCE_DIR) + "/tests/fixtures/services"; }

inline std::shared_ptr<const pna::Services> test_services() {
    static std::shared_ptr<const pna::Services> s = [] {
        pna::Services::Options o;
        o.require_root = false;
        std::vector<std::string> refused;
        return std::shared_ptr<const pna::Services>(pna::Services::load(test_services_dir(), o, &refused));
    }();
    return s;
}

// A channel-less test link (loopback, STANAG 5066, Appendix A) carries the
// fixture's MARS traffic: what it hears is of that service (--rx-service).
inline pna::CoreConfig on_mars_link(pna::CoreConfig c) {
    c.rx_service = "mars";
    return c;
}
