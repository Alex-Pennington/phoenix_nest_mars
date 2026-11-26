// One interface for every modem the core can drive. The core never talks to a
// modem any other way, so swapping the MS-DMT modem for another (or for the
// loopback used in tests) changes no other part.
#pragma once

#include <functional>
#include <string>

namespace pna {

// One decoded burst: its bytes, where they came from (the payload file's
// name), the waveform ("110A", "110D"...), and its sha256 when the modem gave one.
struct ModemDecode {
    std::string bytes, source, waveform, sha256;
};

class ModemDriver {
public:
    struct Callbacks {
        // Decoded bytes from the far station, in whatever chunks they arrive.
        std::function<void(const std::string& bytes)> on_rx;
        // The last send() finished. Called once per send, never from inside send().
        std::function<void(bool ok, const std::string& error)> on_tx_done;
        // Human-readable state changes for the log.
        std::function<void(const std::string& text)> on_event;
        // One whole decoded burst, as the modem gave it, before the core reads
        // it (issue 3 ruling: decodes go into the inbox flagged UNVERIFIED,
        // with no bytes hidden). Called just before on_rx gets the same bytes.
        // Optional: a modem that has no bursts never calls it.
        std::function<void(const ModemDecode& d)> on_decode;
        // The send ended as an audio file in the outbox that is not on the air
        // yet (greybeard-ale's gale-key keys it). Called just before
        // on_tx_done(true) for that send, with the burst's file name. A modem
        // that keys itself never calls it.
        std::function<void(const std::string& burst)> on_tx_written;
        // What gale-key did with a burst it was written: keyed on the air, or
        // refused (why says why). Called once per result, from the modem's
        // own thread, at any later time.
        std::function<void(const std::string& burst, bool keyed, const std::string& why)> on_key_result;
    };

    virtual ~ModemDriver() = default;
    virtual void start(Callbacks cb) = 0;
    virtual void stop() = 0;
    // Connected and configured, so a send can start.
    virtual bool ready() const = 0;
    // Sending, or busy receiving, so the core should hold its next send.
    virtual bool busy() const = 0;
    // Starts transmitting bytes. Returns false if it could not start; on true,
    // on_tx_done follows later.
    virtual bool send(const std::string& bytes) = 0;
    // Short text for status, e.g. "MS-DMT 127.0.0.1:4998/4999 600S".
    virtual std::string describe() const = 0;
};

}  // namespace pna
