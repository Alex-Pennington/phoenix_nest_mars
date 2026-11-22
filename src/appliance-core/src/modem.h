// One interface for every modem the core can drive. The core never talks to a
// modem any other way, so swapping the MS-DMT modem for another (or for the
// loopback used in tests) changes no other part.
#pragma once

#include <functional>
#include <string>

namespace pna {

class ModemDriver {
public:
    struct Callbacks {
        // Decoded bytes from the far station, in whatever chunks they arrive.
        std::function<void(const std::string& bytes)> on_rx;
        // The last send() finished. Called once per send, never from inside send().
        std::function<void(bool ok, const std::string& error)> on_tx_done;
        // Human-readable state changes for the log.
        std::function<void(const std::string& text)> on_event;
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
