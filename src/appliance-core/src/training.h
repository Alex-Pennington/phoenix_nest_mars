// Training mode: a practice copy of the station, so a member with no traffic
// yet can see every screen work (docs/guide.md, Training).
//
// While training is on, the API answers from a practice station instead of
// the real one, and every reply carries "training":true. The practice
// station:
//   - has its own store, keys and station book in DB_DIR/CALL.training/,
//     so nothing in it mixes with the real station's;
//   - has no radio. Its modem is joined only to a practice net control
//     (TRAIN-NCS) inside the program, which acknowledges what it is sent;
//     nothing it does can key the real radio;
//   - is filled, the first time, with the sample traffic built into the
//     program (scrubbed MARS samples: made-up calls and routing indicators),
//     heard as bare text the way gale-rx hands traffic to the core, a few
//     messages from the practice net control addressed to it, and a practice
//     key tape sealed to it, so enciphered traffic opens too.
// The real station keeps running underneath: it receives and acknowledges
// as always.
#pragma once

#include "services.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pna {
class Core;
class LoopbackModem;

class Training {
public:
    static constexpr const char* kNcs = "TRAIN-NCS";

    // dir: the practice station's folder. call: the station's own call
    // (the practice station uses it, so traffic reads as at home).
    // services: the tables the practice stations route by (the real core's).
    Training(std::string dir, std::string call, std::shared_ptr<const Services> services = nullptr);
    ~Training();
    // Starts the two practice stations; the first time (or after reload),
    // fills the practice store. False with *error if it couldn't.
    bool start(std::string* error);
    void stop();
    // Empties the practice store and fills it again, as new.
    bool reload(std::string* error);
    Core& core() { return *me_; }
    const std::string& dir() const { return dir_; }
    int samples_played() const { return played_; }
    long long since() const { return since_; }

private:
    bool make(const std::string& call, std::unique_ptr<Core>& core, LoopbackModem*& modem, std::string* error);
    bool fill(std::string* error);

    std::string dir_, call_;
    std::shared_ptr<const Services> services_;
    std::string practice_service() const;
    std::unique_ptr<Core> me_, ncs_;
    LoopbackModem *my_radio_ = nullptr, *ncs_radio_ = nullptr;
    int played_ = 0;
    long long since_ = 0;
};

// The sample traffic built into the program, as (name, text).
const std::vector<std::pair<std::string, std::string>>& training_samples();

}  // namespace pna
