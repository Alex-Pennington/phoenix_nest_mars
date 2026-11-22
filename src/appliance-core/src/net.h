// Small POSIX socket helpers.
#pragma once

#include <cstdint>
#include <string>

namespace pna {

// Connects to host:port, or returns -1.
int tcp_connect(const std::string& host, uint16_t port, int timeout_ms);
// Listens on 127.0.0.1:port only, or returns -1.
int tcp_listen_local(uint16_t port);
// Writes everything, or returns false.
bool send_all(int fd, const std::string& data);
// Waits up to timeout_ms for fd to be readable. Returns 1 readable, 0 timeout, -1 error.
int wait_readable(int fd, int timeout_ms);
// Reads what is there. Returns bytes read, 0 on close, -1 on error.
long read_some(int fd, std::string& out);
void close_fd(int& fd);

}  // namespace pna
