// The config file: key = value lines turned into appliance-core's options.
#include "../src/config.h"
#include "check.h"

#include <vector>

using namespace pna;

int main() {
    std::vector<std::string> a;
    std::string err;
    CHECK(read_config_text("# a note\n"
                           "station = NCS-A   # our call\n"
                           "\n"
                           "db-dir = ~/data\n"
                           "tx-arg = --quiet\n"
                           "tx-arg = \"two words\"\n"
                           "captures =\n"
                           "captures-fallback = /x/fallback/captures\n"
                           "link-gate = yes\n"
                           "no-acks = no\n"
                           "advisor = \"/opt/my helper # not a note\"\n",
                           &a, &err, "/home/op"));
    CHECK((a == std::vector<std::string>{"--station", "NCS-A", "--db-dir", "/home/op/data", "--tx-arg", "--quiet",
                                         "--tx-arg", "two words", "--captures-fallback", "/x/fallback/captures", "--link-gate", "--advisor",
                                         "/opt/my helper # not a note"}));

    a.clear();
    CHECK(!read_config_text("station = A\nfrequency = 1\n", &a, &err) && err.find("line 2") != std::string::npos &&
          err.find("frequency") != std::string::npos);
    CHECK(!read_config_text("station A\n", &a, &err) && err.find("key = value") != std::string::npos);
    CHECK(!read_config_text("link-gate = maybe\n", &a, &err) && err.find("yes or no") != std::string::npos);
    CHECK(!read_config_text("station = \"A\n", &a, &err) && err.find("quote") != std::string::npos);
    CHECK(!read_config("/nonexistent/appliance.conf", &a, &err) && err.find("can't read") != std::string::npos);

    // The shipped example reads cleanly and sets no station.
    a.clear();
    CHECK(read_config(std::string(PNA_SOURCE_DIR) + "/packaging/appliance.conf.example", &a, &err));
    bool station = false;
    for (auto& x : a) station = station || x == "--station" || x == "--loopback-pair";
    CHECK(!station);
    return check::report("test_config");
}
