// The service tables the tests route by: tests/fixtures/services, in the
// format of greybeard-ale docs/services.md (amateur as it ships; mars with
// the structure and made-up values only). They are the test user's, not
// root's, so the loader's root check is lifted here only.
#pragma once

#include "../src/core.h"
#include "../src/services.h"

#include <unistd.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifndef PNA_SOURCE_DIR
#error "PNA_SOURCE_DIR is set by CMakeLists.txt for every test"
#endif

// The tables as the checkout holds them. Git keeps no mode but the exec bit, so
// a checkout made under umask 0002 has them group-writable, and the loader
// refuses such a file. The tests load a copy instead.
inline std::string test_services_source_dir() { return std::string(PNA_SOURCE_DIR) + "/tests/fixtures/services"; }

// A copy of the fixture tables, mode 0644, in this process's own temp
// directory: the tests never depend on the checkout's umask. The loader stays
// as strict as on a station. Removed when the process ends.
inline std::string test_services_dir() {
    namespace fs = std::filesystem;
    struct Copy {
        fs::path dir;
        Copy() {
            dir = fs::temp_directory_path() / ("pna-svc-" + std::to_string(::getpid()));
            std::error_code ec;
            fs::remove_all(dir, ec);
            fs::create_directories(dir);
            for (auto& e : fs::directory_iterator(test_services_source_dir())) {
                if (e.path().extension() != ".json") continue;
                fs::path to = dir / e.path().filename();
                fs::copy_file(e.path(), to, fs::copy_options::overwrite_existing);
                fs::permissions(to, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read |
                                        fs::perms::others_read);
            }
        }
        ~Copy() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    };
    static Copy c;
    return c.dir.string();
}

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
