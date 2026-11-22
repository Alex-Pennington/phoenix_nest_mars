// Local API for front ends: one JSON object per line over TCP on 127.0.0.1.
// See docs/api.md.
#pragma once

#include "core.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pna {

class ApiServer {
public:
    ApiServer(Core& core, uint16_t port);
    ~ApiServer();
    bool start();  // false if the port could not be opened
    void stop();
    uint16_t port() const { return port_; }

    // Handles one request line and returns the reply line (no newline).
    static std::string handle(Core& core, const std::string& line);

private:
    void accept_loop();
    void client_loop(int fd);

    Core& core_;
    uint16_t port_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread accept_;
    std::mutex clients_mu_;
    std::vector<std::thread> clients_;
};

}  // namespace pna
