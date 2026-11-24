#include "config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace pna {

namespace {

const std::vector<std::string> kFlags = {"no-acks", "link-gate"};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

}  // namespace

const std::vector<std::string>& config_keys() {
    static const std::vector<std::string> k = {
        "station",     "tx-mode",     "tx-cmd",        "tx-arg",      "captures",   "outbox",
        "db-dir",      "api-port",    "loopback-pair", "loopback-bps", "no-acks",   "max-attempts",
        "advisor",     "advisor-arg", "link-socket",   "link-poll-ms", "link-gate",
        "link",        "s5066-peers", "s5066-port",    "s5066-sap",   "s5066-ttl",  "s5066-confirm",
        "appa-port",   "ack-timeout", "ack-retries",   "tx-gap"};
    return k;
}

bool read_config_text(const std::string& text, std::vector<std::string>* args, std::string* error,
                      const std::string& home) {
    std::istringstream in(text);
    std::string line;
    int n = 0;
    auto fail = [&](const std::string& why) {
        if (error) *error = "line " + std::to_string(n) + ": " + why;
        return false;
    };
    while (std::getline(in, line)) {
        ++n;
        // A '#' outside quotes starts a comment.
        bool q = false;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"') q = !q;
            if (line[i] == '#' && !q) {
                line.resize(i);
                break;
            }
        }
        line = trim(line);
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) return fail("write it as key = value");
        std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
        if (std::find(config_keys().begin(), config_keys().end(), key) == config_keys().end())
            return fail("\"" + key + "\" isn't a setting (see appliance-core --help)");
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
        else if (!val.empty() && (val.front() == '"' || val.back() == '"')) return fail("a quote isn't closed");
        if (std::find(kFlags.begin(), kFlags.end(), key) != kFlags.end()) {
            if (val == "yes" || val == "true") args->push_back("--" + key);
            else if (val != "no" && val != "false") return fail(key + " takes yes or no");
            continue;
        }
        if (val.empty()) continue;  // left blank: not set
        if (val.rfind("~/", 0) == 0) {
            std::string h = home;
            if (h.empty()) {
                const char* e = std::getenv("HOME");
                h = e ? e : "";
            }
            if (h.empty()) return fail("~ used, but HOME isn't set");
            val = h + val.substr(1);
        }
        args->push_back("--" + key);
        args->push_back(val);
    }
    return true;
}

bool read_config(const std::string& path, std::vector<std::string>* args, std::string* error) {
    std::ifstream f(path);
    if (!f) {
        if (error) *error = "can't read " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string e;
    if (!read_config_text(ss.str(), args, &e)) {
        if (error) *error = path + ", " + e;
        return false;
    }
    return true;
}

}  // namespace pna
