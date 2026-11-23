// The station's channel plan: each channel's designator and its frequency.
//
// Frequencies are controlled information. They live only here, in one file
// beside the database (NAME.channels, mode 0600), and nothing that leaves the
// engine carries them: the API, the log, the book and every message name a
// channel by its designator only. The plan is filled from a frequency card
// (mars_import.h) and is there for the radio side to tune by designator.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace pna {

struct Channel {
    std::string name;     // the designator, upper case: what everyone else sees
    int64_t hz = 0;       // controlled: never shown or sent
    std::string mode;     // USB, LSB, ALE, DATA ...
    std::string use;      // what the card says it's for, frequency-free
    std::string source;   // which import it came from
};

class ChannelPlan {
public:
    explicit ChannelPlan(std::string path);
    bool load(std::string* error);
    // Adds or replaces by designator and writes the file. False with *error
    // for a bad designator, or a frequency outside 100 kHz to 3 GHz.
    bool put(const Channel& c, std::string* error);
    bool put_all(const std::vector<Channel>& cs, std::vector<std::string>* errors);
    bool remove(const std::string& name);
    std::vector<Channel> list();
    std::optional<Channel> get(const std::string& name);
    // The designator for a frequency, when the plan holds it (within 50 Hz).
    std::string designator_for(int64_t hz);
    size_t size();

private:
    bool save_locked(std::string* error);
    std::string path_;
    std::vector<Channel> ch_;
    std::mutex mu_;
};

// A channel designator: 1 to 12 letters, digits or -, with at least one
// letter, and not shaped like a frequency.
bool valid_designator(const std::string& s);

// A frequency as a card writes it, to hertz. "7.3455" (MHz below 1000),
// "7345.5" (kHz), "7345500" (Hz), or with a unit (MHZ, KHZ). unit_hint is
// "mhz", "khz" or "hz" when the column's header says so. 0 when unreadable.
int64_t parse_frequency(const std::string& s, const std::string& unit_hint);

}  // namespace pna
