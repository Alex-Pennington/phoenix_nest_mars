// Link state for the core, read from greybeard-ale. The core does no ALE:
// scanning, calling and linking are greybeard-ale's (its scope, Alex's
// ruling). The core only asks.
//
// The source is a local Unix socket that takes "status" and answers one JSON
// line: ale-scan --control PATH (greybeard-ale src/control_socket.h). From that
// reply the core reads the scanner's state, plan, hold, engine link and clock
// source ("wwv" when WWV has set the clock, "host" when not).
//
// Per-station link state: greybeard-ale has no link control yet (its stage 6),
// so ale-scan's status has no such field today. The core reads, when present,
//   "links": [{"station": "AAR4XY", "state": "linked", "since": UNIX}, ...]
// (or one such object as "link"). That shape is the core's proposal to
// greybeard-ale, not its interface; tools/stub_ale_scan.py answers with it.
// Until a station is reported linked, link_up() is false for it.
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pna {

struct StationLink {
    std::string station;
    std::string state;      // as reported: "linked", "calling", "down", ...
    long long since = 0;    // UNIX time, 0 if not given
    bool up() const;        // "linked", "up" or "active"
};

struct LinkState {
    std::string socket;         // where it is read from; empty: no link source
    bool reachable = false;     // the last poll got a good reply
    std::string error;          // why not, when not
    long long polled_at = 0;    // UNIX time of the last poll
    long long last_ok_at = 0;   // and of the last good one
    // From ale-scan's status.
    std::string scanner;        // its "state"
    std::string plan;
    bool engine = false;
    std::string clock;          // "wwv", "host" or ""
    long long last_wwv_at = 0;  // UNIX time WWV last set the clock, 0 if never
    int hold_window = 0;        // 0: scanning, not held
    std::string note;
    // Per-station links, when the source reports them.
    bool links_reported = false;
    std::vector<StationLink> links;
};

// Reads one status reply into *s (keeps socket and times). False, with
// s->error set, when it isn't a good reply.
bool parse_link_status(const std::string& line, LinkState* s);

// The clock check, in words for the operator: whether WWV set the clock and
// how long ago. Empty when there is no link source.
std::string clock_note(const LinkState& s, long long now);

// One request on a Unix stream socket: sends line + "\n", returns the first
// line of the answer. False with *error on anything else.
bool unix_request(const std::string& path, const std::string& line, int timeout_ms, std::string* reply,
                  std::string* error);

class LinkMonitor {
public:
    explicit LinkMonitor(std::string socket_path, int poll_ms = 2000);
    ~LinkMonitor();
    void start();
    void stop();
    void poll_once();           // the thread calls it; tests may too
    LinkState state() const;
    bool link_up(const std::string& station) const;

private:
    void loop();
    const std::string path_;
    const int poll_ms_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    LinkState st_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace pna
