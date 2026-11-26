// The radio side of the station, for gale-web: what greybeard-ale's scanner,
// engine, clock and logs say, read from their files and the scanner's control
// socket and passed on through port 4100 (issue 3). The core does no ALE and
// no modem work here: it reads what greybeard-ale writes, names channels, and
// forwards the operator's scanner orders.
//
// Channels are named, never given by frequency. A frequency read from a file
// or the scanner is turned into a channel name here and goes no further: not
// into a reply, not into the log. A name is, in order:
//   1. the channel's designator in the station's channel plan (channels.h),
//   2. its label in the scanner's plan (record/voice HZ:LABEL),
//   3. its place in the scanner's plan, "W<window>C<channel>" (1-based),
//   4. "unlisted".
//
// Where the files are (settings, each optional; config keys in brackets):
//   gale-dir       base for the defaults below (default "."; on the terminal
//                  image the core runs in /var/lib/gale/local, so "." is it)
//   words          word log prefix: the file itself, else the newest
//                  PREFIX-YYYYMMDD.jsonl, else PREFIX.jsonl (default GALE/words)
//   scan-log       ale-scan.log (default GALE/ale-scan.log)
//   wwv-dir        clock.json, clock_history.jsonl, anchor.json, wwv_live.log
//                  (default GALE/wwv)
//   gps-status     gps_pps.py's status.json (default GALE/gps/status.json)
//   health-file    ale_health.py's health.json (default GALE/health.json)
//   engine-status  ale-engine --status (default GALE/engine_status.json)
//   lqa-db         lqa_db.py's database, read only (default GALE/lqa.sqlite)
//   favourites     ADDRESS [name] per line (default GALE/favourites.txt)
//   own-address    our own ALE address(es) (default GALE/own_address.txt)
//   scan-plan      the scanner's plan, for channel labels (default GALE/plans/scan.plan)
//   radio-file     the launcher's radio.json (default GALE/radio.json)
//   captures       the engine's capture folder (the core's --captures, else GALE/captures)
//   captures-fallback  where the engine writes captures while the store fails
//                  (the core's --captures-fallback; no default: not read)
//   store-root     the data store's mount point; when set, every reply says
//                  whether it is mounted (no default: not checked)
// On Gangleri they point under /srv/greybeard-store/greybeard-ale/gangleri/.
// A file or folder that isn't there (the store not mounted) makes the call
// say so ("available": false with why) and never fails the call.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pna {

class Core;

namespace sv {

struct Paths {
    std::string gale_dir;
    std::string words, scan_log, wwv_dir, gps, health, engine_status, lqa_db, favourites, own, scan_plan, radio,
        captures, captures_fallback, store_root;
    // Each empty one filled with its default under gale_dir ("." when empty).
    Paths resolved() const;
};

void set_paths(const Paths& p);
Paths paths();  // resolved

// One ALE word from the word log.
struct Word {
    double unix = 0, khz = 0, sinad = 0;
    std::string type, chars, engine;
    bool gap = false;
    int window = 0;  // 0: not given
};
// A word log line, valid as gale_term.py's valid_word: unix, khz, sinad
// numbers; type and chars strings; word hex.
bool parse_word(const std::string& line, Word* w);

// Words heard on one channel close together, put back together as
// lqa_db.py does: a gap over 1.2 s starts a new one.
struct Transmission {
    double khz = 0, start = 0, end = 0, sinad_sum = 0;
    int words = 0, gaps = 0;
    std::vector<std::pair<std::string, std::string>> groups;  // (type, address)
    std::string caller;
    std::vector<std::string> called;
    std::string kind;              // sounding, call, call-partial, call-end, unknown
    std::vector<std::string> amd;  // AMD texts
};
std::vector<Transmission> group_words(const std::vector<Word>& words);

// The newest word log for a prefix (see the top of this file); empty if none.
std::string newest_words_file(const std::string& prefix);

// Scanner log state, as gale_term.py's ScanLog reads it.
struct ScanLog {
    bool available = false;
    std::string why;
    double mtime = 0;
    long long since = 0, stopped = 0;
    std::string link;        // "up", "down" or ""
    std::string last_visit;  // "engine", "local" or ""
    int overloads = 0, restarts = 0;
};
ScanLog read_scan_log(const std::string& path, size_t backlog = 256 * 1024);

// Channel names (see the top of this file).
class ChannelNames {
public:
    explicit ChannelNames(Core& core);
    std::string name(double khz) const;
    std::string name_hz(int64_t hz) const;

private:
    Core& core_;
    std::map<int64_t, std::string> labels_;              // scanner plan labels, by Hz
    std::vector<std::vector<int64_t>> windows_;          // scanner plan dials, Hz
};

// Copies a scanner reply object with every frequency named or dropped: a
// member whose key holds "khz" becomes the same key with "label" for "khz"
// (numbers and arrays of numbers named), one holding "hz" or "freq" goes.
std::string name_channels(const std::string& reply_json, const ChannelNames& names);

// The calls: scanner_status, scanner_hold, scanner_release, scanner_record,
// scanner_plan, word_feed, station_health, captures_list, heard_stations.
// nullopt for any other cmd.
std::optional<std::string> handle(Core& core, const std::string& cmd, std::map<std::string, std::string>& req);

}  // namespace sv
}  // namespace pna
