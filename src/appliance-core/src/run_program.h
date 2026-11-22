// Runs a program with arguments (no shell), with a time limit, and returns its
// exit status and output. Used to call greybeard-ale's gale-tx.
#pragma once

#include <string>
#include <vector>

namespace pna {

struct RunResult {
    bool started = false;     // the program could be started at all
    bool timed_out = false;   // killed at the time limit
    int exit_code = -1;       // when it exited normally
    std::string out;          // stdout, at most 64 KiB kept
    std::string err;          // stderr, at most 64 KiB kept
};

RunResult run_program(const std::vector<std::string>& argv, int timeout_ms);

}  // namespace pna
