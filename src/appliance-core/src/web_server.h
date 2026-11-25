// The operator screen's web port (docs/api.md, "Web port"; docs/gale-web-screens.md
// section 5). On 127.0.0.1 only:
//
//   GET /...   the screen's files from the web root (index.html for /)
//   GET /ws    a WebSocket: each text frame is one request, answered by the
//              same handler as the line API on the API port
//
// A request with a "rid" gets it back unchanged in its reply. No TLS: the
// port is loopback only, like the API port.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <sys/socket.h>

namespace pna {

class WebServer {
public:
    // One request line in, one reply object out (ApiServer::handle).
    using Handler = std::function<std::string(const std::string&)>;

    static constexpr size_t kMaxMessage = 1u << 20;  // per frame and per whole message
    static constexpr int kMaxClients = 32;

    WebServer(uint16_t port, std::string root, Handler handler);
    ~WebServer();
    bool start();  // false if the port could not be opened
    void stop();
    uint16_t port() const { return port_; }
    // False when the web root isn't a directory: the WebSocket still works,
    // every file is 404.
    bool root_ok() const { return !root_real_.empty(); }

    // Runs one request through the handler with "rid" taken out and put back
    // into the reply as it came (a number stays a number).
    static std::string dispatch(const Handler& handler, const std::string& frame);

private:
    struct Conn {
        std::thread t;
        std::atomic<bool> done{false};
    };
    void accept_loop();
    void serve(int fd);
    void serve_file(int fd, const std::string& target);
    void serve_ws(int fd, const std::string& key, std::string pending);
    bool host_ok(const std::string& host) const;
    bool origin_ok(const std::string& origin) const;
    void reap(bool all);

    uint16_t port_;
    std::string root_;
    std::string root_real_;
    Handler handler_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<int> active_{0};
    std::thread accept_;
    std::mutex conns_mu_;
    std::list<std::unique_ptr<Conn>> conns_;
};

// The parts, on their own for the tests.
namespace web {

// Maps a request target to a file under root_real (already canonical).
// False for anything that isn't a regular file inside it: "..", encoded or
// not, hidden names, bad %-escapes, NUL, backslash, symlinks out of the root.
bool resolve(const std::string& root_real, const std::string& target, std::string& file);
std::string content_type(const std::string& path);
// Sec-WebSocket-Accept for a Sec-WebSocket-Key.
std::string ws_accept(const std::string& key);
bool valid_utf8(const std::string& s);

// One frame read from the front of buf (a client's frames must be masked).
struct Frame {
    bool fin = false;
    int op = 0;
    std::string payload;
};
enum class Got { Need, Frame, Error };
// Need: more bytes. Error: *close_code says why (1002 protocol, 1009 too big).
Got read_frame(std::string& buf, Frame& f, size_t max, int* close_code);
// A server frame: never masked.
std::string make_frame(int op, const std::string& payload);
// The WebSocket's Origin rule (phoenix_nest_mars#3): a browser page must
// send an Origin naming this port on 127.0.0.1 or localhost. A request with
// no Origin header at all is not a browser page; it is taken only when it
// comes from this box itself (a loopback peer), and refused from anywhere
// else, whatever address the port is later bound to.
bool ws_origin_allowed(bool has_origin, bool origin_ok, bool peer_loopback);
// True when the socket's peer is 127.0.0.0/8 or ::1 (an IPv4-mapped
// 127.x address too).
bool peer_is_loopback(int fd);
bool addr_is_loopback(const ::sockaddr* sa);

// A client frame (masked), for the tests.
std::string make_client_frame(int op, const std::string& payload, bool fin = true, uint32_t mask = 0x1a2b3c4d);

}  // namespace web
}  // namespace pna
