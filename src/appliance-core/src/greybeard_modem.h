// The core's modem is greybeard-ale, run as separate programs on the same box
// (Alex's ruling; greybeard-ale's own integration plan). Nothing goes over a
// network: the core and greybeard-ale meet only through files.
//
// Send: the frame's bytes go to a payload file, and the core runs
//   gale-tx --waveform 110a --mode MODE --in PAYLOAD.bin --out BURST.wav
// which writes an 8 kHz 16-bit mono WAV holding one whole 110A message and
// prints a one-line JSON summary. The WAV lands in the outbox, for greybeard-ale's
// transmit stage (dummy load first, on air only after the ATO). This driver never
// keys a radio.
//
// Receive: for each burst greybeard-ale writes CAPTURE.m110a.N.bin (or .m110d.N.bin)
// beside the capture, and CAPTURE.modems.json listing each payload file with its
// byte count and sha256. The driver watches the captures directory, checks every
// listed file against its count and digest, and hands the bytes to the core.
//
// The listing is read as greybeard-ale's tools/modem_verdict.py writes it
// ("verdicts", each decoded data burst with payload_file, payload_bytes and
// sha256). Tested against greybeard-ale main a5a4cda with the real 110A modem on
// audio files (see README); tools/stub_gale_tx.py stands in for tests.
#pragma once

#include "modem.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace pna {

struct GreybeardConfig {
    std::vector<std::string> tx_cmd = {"gale-tx"};  // program, plus any leading arguments
    std::string waveform = "110a";
    std::string mode;            // gale-tx --mode: 75S..4800L, 600H, 1200H. Required.
    std::string work_dir;        // payload files go here
    std::string outbox_dir;      // BURST.wav files go here, for the transmit stage
    std::string captures_dir;    // greybeard-ale's capture directory, watched
    std::string state_file;      // modems.json files already handled, one per line
    int tx_timeout_ms = 60000;
    int poll_ms = 500;
    int settle_ms = 30000;       // how long a listed file may stay missing or short
};

class GreybeardModem : public ModemDriver {
public:
    explicit GreybeardModem(GreybeardConfig cfg);
    ~GreybeardModem() override;

    void start(Callbacks cb) override;
    void stop() override;
    bool ready() const override { return running_ && !cfg_.mode.empty(); }
    bool busy() const override { return sending_; }
    bool send(const std::string& bytes) override;
    std::string describe() const override;

    // One pass over the captures directory; the watcher thread calls it, tests may too.
    void scan_captures();
    // The last burst written to the outbox (for status and tests).
    std::string last_burst() const;

private:
    void tx_loop();
    void rx_loop();
    void event(const std::string& text);
    // Returns true when the modems.json is finished with (handled or given up).
    bool handle_listing(const std::string& path);
    void mark_seen(const std::string& name);

    GreybeardConfig cfg_;
    Callbacks cb_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> tx_q_;
    std::thread tx_thread_, rx_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> sending_{false};
    std::set<std::string> seen_;
    std::map<std::string, long long> first_seen_ms_;
    std::string last_burst_;
    unsigned long long seq_ = 0;
};

}  // namespace pna
