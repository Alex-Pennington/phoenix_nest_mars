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
    // The LDG tuner's antenna port for this channel (Alex: "assign the ant1
    // or 2 by channel tx and rx"): 1 or 2. 0 on a channel being put means
    // "not given": the plan keeps what it held, or 1 for a new channel. A
    // record written before these fields existed reads as 1 and 1.
    int ant_rx = 0;       // the port the receiver listens on
    int ant_tx = 0;       // the port each transmit, tune and tuner memory is on
    // Station design v1.9: the world the channel belongs to, mars or amateur.
    // "" on a channel being put means "not given": the plan keeps what it
    // held, or mars for a new channel. A record written before this field
    // existed reads as mars. The core's routing and automatic sends use only
    // mars channels (ask 40); an amateur channel is there to listen on.
    std::string world;
    // The channel's HFLink label (an amateur channel's everyday name), or "".
    // Never a frequency. On a channel being put, label_given false keeps the
    // plan's label.
    std::string label;
    bool label_given = false;
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
    // The designator of a MARS channel on a frequency, when the plan holds
    // one (within 50 Hz). An amateur channel is never a MARS designator.
    std::string designator_for(int64_t hz);
    // The name to show for a frequency: a MARS channel's designator, an
    // amateur channel's HFLink label (its designator when it has none), or "".
    std::string display_name_for(int64_t hz);
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

// An HFLink label: 1 to 16 letters, digits, - / . or _, with a letter, and
// not shaped like a frequency.
bool valid_channel_label(const std::string& s);

// A frequency as a card writes it, to hertz. "7.3455" (MHz below 1000),
// "7345.5" (kHz), "7345500" (Hz), or with a unit (MHZ, KHZ). unit_hint is
// "mhz", "khz" or "hz" when the column's header says so. 0 when unreadable.
int64_t parse_frequency(const std::string& s, const std::string& unit_hint);

}  // namespace pna
