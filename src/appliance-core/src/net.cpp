#include "net.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace pna {

int tcp_connect(const std::string& host, uint16_t port, int timeout_ms) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return -1;
    int fd = -1;
    for (addrinfo* a = res; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = connect(fd, a->ai_addr, a->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            pollfd p{fd, POLLOUT, 0};
            rc = poll(&p, 1, timeout_ms) == 1 ? 0 : -1;
            if (rc == 0) {
                int err = 0;
                socklen_t len = sizeof err;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                rc = err == 0 ? 0 : -1;
            }
        }
        if (rc == 0) {
            fcntl(fd, F_SETFL, flags);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

int tcp_listen_local(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(fd, 8) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool send_all(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

int wait_readable(int fd, int timeout_ms) {
    pollfd p{fd, POLLIN, 0};
    int rc = poll(&p, 1, timeout_ms);
    if (rc < 0) return errno == EINTR ? 0 : -1;
    if (rc == 0) return 0;
    return 1;
}

long read_some(int fd, std::string& out) {
    char buf[4096];
    ssize_t n;
    do {
        n = recv(fd, buf, sizeof buf, 0);
    } while (n < 0 && errno == EINTR);
    if (n > 0) out.append(buf, static_cast<size_t>(n));
    return static_cast<long>(n);
}

void close_fd(int& fd) {
    if (fd >= 0) {
        shutdown(fd, SHUT_RDWR);
        close(fd);
        fd = -1;
    }
}

}  // namespace pna
