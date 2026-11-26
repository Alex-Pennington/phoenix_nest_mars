// The operator screen's web port: see web_server.h and docs/api.md.
#include "web_server.h"

#include "gale_key.h"

#include "json.h"
#include "net.h"

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace pna {

namespace web {

namespace {

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string canonical(const std::string& p) {
    char buf[PATH_MAX];
    if (!::realpath(p.c_str(), buf)) return "";
    return buf;
}

}  // namespace

bool ws_origin_allowed(bool has_origin, bool origin_ok, bool peer_loopback) {
    if (has_origin) return origin_ok;
    return peer_loopback;
}

bool addr_is_loopback(const sockaddr* sa) {
    if (!sa) return false;
    if (sa->sa_family == AF_INET) {
        const auto* a = reinterpret_cast<const sockaddr_in*>(sa);
        return (ntohl(a->sin_addr.s_addr) >> 24) == 127;
    }
    if (sa->sa_family == AF_INET6) {
        const auto* a = reinterpret_cast<const sockaddr_in6*>(sa);
        if (IN6_IS_ADDR_LOOPBACK(&a->sin6_addr)) return true;
        return IN6_IS_ADDR_V4MAPPED(&a->sin6_addr) && a->sin6_addr.s6_addr[12] == 127;
    }
    return false;
}

bool peer_is_loopback(int fd) {
    sockaddr_storage ss{};
    socklen_t len = sizeof ss;
    if (::getpeername(fd, reinterpret_cast<sockaddr*>(&ss), &len) != 0) return false;
    return addr_is_loopback(reinterpret_cast<const sockaddr*>(&ss));
}

bool resolve(const std::string& root_real, const std::string& target, std::string& file) {
    if (root_real.empty() || target.empty() || target[0] != '/') return false;
    std::string path = target.substr(0, target.find_first_of("?#"));
    std::string dec;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%') {
            if (i + 2 >= path.size()) return false;
            int hi = hexval(path[i + 1]), lo = hexval(path[i + 2]);
            if (hi < 0 || lo < 0) return false;
            dec += static_cast<char>(hi * 16 + lo);
            i += 2;
        } else {
            dec += path[i];
        }
    }
    std::string rel;
    size_t s = 0;
    while (s <= dec.size()) {
        size_t e = dec.find('/', s);
        std::string seg = dec.substr(s, e == std::string::npos ? std::string::npos : e - s);
        if (!seg.empty() && seg != ".") {
            // "..", hidden names (.git, .env), and anything a file name has no
            // business holding.
            if (seg[0] == '.') return false;
            for (char c : seg)
                if (c == '\0' || c == '\\' || static_cast<unsigned char>(c) < 0x20) return false;
            rel += "/" + seg;
        }
        if (e == std::string::npos) break;
        s = e + 1;
    }
    if (rel.empty()) rel = "/index.html";
    std::string real = canonical(root_real + rel);
    // A symlink may point out of the root: the real path must still be inside.
    if (real.empty() || real.compare(0, root_real.size() + 1, root_real + "/") != 0) return false;
    struct stat st {};
    if (::stat(real.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    file = real;
    return true;
}

std::string content_type(const std::string& path) {
    static const std::map<std::string, std::string> types = {
        {"html", "text/html; charset=utf-8"},  {"htm", "text/html; charset=utf-8"},
        {"js", "text/javascript; charset=utf-8"}, {"mjs", "text/javascript; charset=utf-8"},
        {"css", "text/css; charset=utf-8"},    {"json", "application/json"},
        {"map", "application/json"},           {"woff2", "font/woff2"},
        {"woff", "font/woff"},                 {"svg", "image/svg+xml"},
        {"png", "image/png"},                  {"ico", "image/x-icon"},
        {"txt", "text/plain; charset=utf-8"},  {"webmanifest", "application/manifest+json"}};
    size_t slash = path.rfind('/');
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "application/octet-stream";
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it = types.find(ext);
    return it == types.end() ? "application/octet-stream" : it->second;
}

std::string ws_accept(const std::string& key) {
    std::string in = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char md[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(in.data()), in.size(), md);
    unsigned char out[4 * ((SHA_DIGEST_LENGTH + 2) / 3) + 1];
    int n = EVP_EncodeBlock(out, md, SHA_DIGEST_LENGTH);
    return std::string(reinterpret_cast<char*>(out), static_cast<size_t>(n));
}

bool valid_utf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        int len;
        uint32_t cp;
        if (c >= 0xc2 && c <= 0xdf) len = 2, cp = c & 0x1f;
        else if (c >= 0xe0 && c <= 0xef) len = 3, cp = c & 0x0f;
        else if (c >= 0xf0 && c <= 0xf4) len = 4, cp = c & 0x07;
        else return false;
        if (i + len > n) return false;
        for (int k = 1; k < len; ++k) {
            unsigned char d = static_cast<unsigned char>(s[i + k]);
            if ((d & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3f);
        }
        if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10ffff)) ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return false;
        i += len;
    }
    return true;
}

Got read_frame(std::string& buf, Frame& f, size_t max, int* close_code) {
    auto fail = [&](int code) {
        if (close_code) *close_code = code;
        return Got::Error;
    };
    if (buf.size() < 2) return Got::Need;
    unsigned char b0 = static_cast<unsigned char>(buf[0]), b1 = static_cast<unsigned char>(buf[1]);
    bool fin = b0 & 0x80;
    int op = b0 & 0x0f;
    if (b0 & 0x70) return fail(1002);                           // no extensions agreed
    if (!(b1 & 0x80)) return fail(1002);                        // a client must mask
    if ((op > 2 && op < 8) || op > 10) return fail(1002);       // reserved opcodes
    uint64_t len = b1 & 0x7f;
    size_t at = 2;
    if (op >= 8 && (!fin || len > 125)) return fail(1002);      // control frames: whole and small
    if (len == 126) {
        if (buf.size() < 4) return Got::Need;
        len = (static_cast<uint64_t>(static_cast<unsigned char>(buf[2])) << 8) | static_cast<unsigned char>(buf[3]);
        at = 4;
    } else if (len == 127) {
        if (buf.size() < 10) return Got::Need;
        len = 0;
        for (int k = 0; k < 8; ++k) len = (len << 8) | static_cast<unsigned char>(buf[2 + k]);
        if (len >> 63) return fail(1002);
        at = 10;
    }
    if (len > max) return fail(1009);                            // refused before it is read
    if (buf.size() < at + 4 + len) return Got::Need;
    const unsigned char* m = reinterpret_cast<const unsigned char*>(buf.data() + at);
    f.fin = fin;
    f.op = op;
    f.payload.assign(buf, at + 4, static_cast<size_t>(len));
    for (size_t k = 0; k < f.payload.size(); ++k) f.payload[k] = static_cast<char>(f.payload[k] ^ m[k & 3]);
    buf.erase(0, at + 4 + static_cast<size_t>(len));
    return Got::Frame;
}

namespace {

std::string frame_header(int op, size_t len, bool fin, bool masked) {
    std::string h;
    h += static_cast<char>((fin ? 0x80 : 0) | (op & 0x0f));
    unsigned char mbit = masked ? 0x80 : 0;
    if (len < 126) {
        h += static_cast<char>(mbit | len);
    } else if (len <= 0xffff) {
        h += static_cast<char>(mbit | 126);
        h += static_cast<char>((len >> 8) & 0xff);
        h += static_cast<char>(len & 0xff);
    } else {
        h += static_cast<char>(mbit | 127);
        for (int k = 7; k >= 0; --k) h += static_cast<char>((static_cast<uint64_t>(len) >> (8 * k)) & 0xff);
    }
    return h;
}

}  // namespace

std::string make_frame(int op, const std::string& payload) { return frame_header(op, payload.size(), true, false) + payload; }

std::string make_client_frame(int op, const std::string& payload, bool fin, uint32_t mask) {
    std::string out = frame_header(op, payload.size(), fin, true);
    unsigned char m[4] = {static_cast<unsigned char>(mask >> 24), static_cast<unsigned char>(mask >> 16),
                          static_cast<unsigned char>(mask >> 8), static_cast<unsigned char>(mask)};
    out.append(reinterpret_cast<char*>(m), 4);
    for (size_t k = 0; k < payload.size(); ++k) out += static_cast<char>(payload[k] ^ m[k & 3]);
    return out;
}

}  // namespace web

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// True when the comma-separated header value holds token (any case).
bool has_token(const std::string& value, const std::string& token) {
    std::stringstream in(value);
    std::string part;
    while (std::getline(in, part, ','))
        if (lower(trim(part)) == token) return true;
    return false;
}

std::string status_line(int code) {
    switch (code) {
        case 200: return "200 OK";
        case 400: return "400 Bad Request";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 426: return "426 Upgrade Required";
        case 431: return "431 Request Header Fields Too Large";
        case 503: return "503 Service Unavailable";
        default: return "500 Internal Server Error";
    }
}

void send_plain(int fd, int code, const std::string& extra_headers = "") {
    std::string body = status_line(code) + "\n";
    send_all(fd, "HTTP/1.1 " + status_line(code) +
                     "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " + std::to_string(body.size()) +
                     "\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n" + extra_headers + "\r\n" + body);
}

void set_send_timeout(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

long recv_some(int fd, std::string& out) {
    char buf[65536];
    ssize_t n;
    do {
        n = recv(fd, buf, sizeof buf, 0);
    } while (n < 0 && errno == EINTR);
    if (n > 0) out.append(buf, static_cast<size_t>(n));
    return static_cast<long>(n);
}

// A rid as it was written: a number, true or false raw, anything else as a
// JSON string. Empty when the request has none.
std::string rid_of(const std::string& frame, const std::map<std::string, std::string>* flat) {
    JsonValue v;
    if (!parse_json(frame, v) || !v.is_object()) return "";
    const JsonValue* r = v.get("rid");
    if (!r) return "";
    switch (r->type) {
        case JsonValue::Type::Number:
            if (flat) {
                auto it = flat->find("rid");
                if (it != flat->end()) return it->second;  // the digits as sent
            }
            {
                std::ostringstream o;
                o.precision(17);
                o << r->n;
                return o.str();
            }
        case JsonValue::Type::Bool: return r->b ? "true" : "false";
        case JsonValue::Type::String: return "\"" + json_escape(r->s) + "\"";
        case JsonValue::Type::Null: return "null";
        default: return "";
    }
}

std::string put_rid(const std::string& reply, const std::string& rid) {
    if (rid.empty() || reply.empty() || reply[0] != '{') return reply;
    size_t rest = reply.find_first_not_of(" \t", 1);
    bool empty = rest != std::string::npos && reply[rest] == '}';
    return "{\"rid\":" + rid + (empty ? "" : ",") + reply.substr(1);
}

}  // namespace

std::string WebServer::dispatch(const Handler& handler, const std::string& frame) {
    std::map<std::string, std::string> req;
    if (!parse_flat_object(frame, req)) {
        // Not a flat object: the handler says what's wrong, and the rid goes
        // back if one can be found, so the screen can match the error.
        return put_rid(handler(frame), rid_of(frame, nullptr));
    }
    auto it = req.find("rid");
    if (it == req.end()) return handler(frame);  // exactly what the API port gets
    std::string rid = rid_of(frame, &req);
    if (rid.empty()) rid = "\"" + json_escape(it->second) + "\"";
    req.erase(it);
    // The request again without rid. Every value reaches the handler as a
    // string either way (parse_flat_object), so nothing else changes.
    JsonWriter w;
    w.begin_object();
    for (auto& kv : req) w.field(kv.first, kv.second);
    w.end_object();
    return put_rid(handler(w.str()), rid);
}

WebServer::WebServer(uint16_t port, std::string root, Handler handler)
    : port_(port), root_(std::move(root)), handler_(std::move(handler)) {
    char buf[PATH_MAX];
    struct stat st {};
    if (!root_.empty() && ::realpath(root_.c_str(), buf) && ::stat(buf, &st) == 0 && S_ISDIR(st.st_mode))
        root_real_ = buf;
}

WebServer::~WebServer() { stop(); }

bool WebServer::start() {
    listen_fd_ = tcp_listen_local(port_);
    if (listen_fd_ < 0) return false;
    running_ = true;
    accept_ = std::thread([this] { accept_loop(); });
    return true;
}

void WebServer::stop() {
    if (!running_.exchange(false)) return;
    if (accept_.joinable()) accept_.join();
    close_fd(listen_fd_);
    reap(true);
}

void WebServer::reap(bool all) {
    std::lock_guard<std::mutex> g(conns_mu_);
    for (auto it = conns_.begin(); it != conns_.end();) {
        if (all || (*it)->done) {
            if ((*it)->t.joinable()) (*it)->t.join();
            it = conns_.erase(it);
        } else {
            ++it;
        }
    }
}

void WebServer::accept_loop() {
    while (running_) {
        reap(false);
        if (wait_readable(listen_fd_, 200) != 1) continue;
        int fd = accept4(listen_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) continue;
        set_send_timeout(fd, 10);
        if (active_ >= kMaxClients) {
            send_plain(fd, 503);
            close_fd(fd);
            continue;
        }
        ++active_;
        auto c = std::make_unique<Conn>();
        Conn* raw = c.get();
        std::lock_guard<std::mutex> g(conns_mu_);
        conns_.push_back(std::move(c));
        raw->t = std::thread([this, fd, raw] {
            serve(fd);
            --active_;
            raw->done = true;
        });
    }
}

bool WebServer::host_ok(const std::string& host) const {
    std::string p = std::to_string(port_);
    std::string h = lower(host);
    return h == "127.0.0.1:" + p || h == "localhost:" + p || h == "127.0.0.1" || h == "localhost";
}

bool WebServer::origin_ok(const std::string& origin) const {
    std::string p = std::to_string(port_);
    std::string o = lower(origin);
    return o == "http://127.0.0.1:" + p || o == "http://localhost:" + p;
}

void WebServer::serve(int fd) {
    // The request head: at most 16 KiB, within 10 seconds.
    std::string buf;
    size_t end = std::string::npos;
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (running_ && (end = buf.find("\r\n\r\n")) == std::string::npos) {
        if (buf.size() > 16384) {
            send_plain(fd, 431);
            close_fd(fd);
            return;
        }
        if (std::chrono::steady_clock::now() > until) break;
        int r = wait_readable(fd, 200);
        if (r < 0) break;
        if (r == 1 && recv_some(fd, buf) <= 0) break;
    }
    if (end == std::string::npos) {
        close_fd(fd);
        return;
    }
    if (end > 16384) {
        send_plain(fd, 431);
        close_fd(fd);
        return;
    }
    std::string head = buf.substr(0, end);
    std::string rest = buf.substr(end + 4);
    std::istringstream in(head);
    std::string line, method, target, version;
    std::getline(in, line);
    {
        std::istringstream rl(trim(line));
        rl >> method >> target >> version;
    }
    std::map<std::string, std::string> h;
    while (std::getline(in, line)) {
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = lower(trim(line.substr(0, c))), v = trim(line.substr(c + 1));
        h[k] = h.count(k) ? h[k] + ", " + v : v;
    }
    if (version.rfind("HTTP/1.", 0) != 0 || target.empty()) {
        send_plain(fd, 400);
    } else if (!host_ok(h["host"])) {
        // Only this box's own names: a page elsewhere that resolves to
        // 127.0.0.1 gets nothing.
        send_plain(fd, 403);
    } else if (method != "GET") {
        send_plain(fd, 404);
    } else if (target.substr(0, target.find('?')) == "/ws") {
        // A page must name this port; no Origin at all: this box only.
        if (!web::ws_origin_allowed(h.count("origin") > 0, h.count("origin") && origin_ok(h["origin"]),
                                    web::peer_is_loopback(fd)))
            send_plain(fd, 403);
        else if (lower(h["upgrade"]) != "websocket" || !has_token(h["connection"], "upgrade")) send_plain(fd, 400);
        else if (h["sec-websocket-version"] != "13") send_plain(fd, 426, "Sec-WebSocket-Version: 13\r\n");
        else {
            std::string key = h["sec-websocket-key"];
            std::string raw(32, '\0');
            int n = key.size() == 24 ? EVP_DecodeBlock(reinterpret_cast<unsigned char*>(&raw[0]),
                                                       reinterpret_cast<const unsigned char*>(key.data()), 24)
                                     : -1;
            // 16 bytes, so 18 decoded with the two padding zeros.
            if (n != 18 || key.compare(22, 2, "==") != 0) send_plain(fd, 400);
            else {
                serve_ws(fd, key, rest);
                return;
            }
        }
    } else {
        serve_file(fd, target);
    }
    close_fd(fd);
}

void WebServer::serve_file(int fd, const std::string& target) {
    std::string file;
    if (!web::resolve(root_real_, target, file)) {
        send_plain(fd, 404);
        return;
    }
    std::ifstream f(file, std::ios::binary);
    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        send_plain(fd, 404);
        return;
    }
    send_all(fd, "HTTP/1.1 200 OK\r\nContent-Type: " + web::content_type(file) +
                     "\r\nContent-Length: " + std::to_string(body.size()) +
                     "\r\nCache-Control: no-cache\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n" +
                     body);
}

void WebServer::serve_ws(int fd, const std::string& key, std::string buf) {
    KeyConnection conn;  // gale-web's y dialogs are dropped when its socket closes (gale_key.h)
    if (!send_all(fd, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Accept: " + web::ws_accept(key) + "\r\n\r\n")) {
        close_fd(fd);
        return;
    }
    auto close_with = [&](int code) {
        std::string p;
        p += static_cast<char>((code >> 8) & 0xff);
        p += static_cast<char>(code & 0xff);
        send_all(fd, web::make_frame(8, p));
    };
    std::string message;
    bool in_message = false;
    for (;;) {
        web::Frame f;
        int code = 1002;
        web::Got got = web::read_frame(buf, f, kMaxMessage, &code);
        if (got == web::Got::Error) {
            close_with(code);
            break;
        }
        if (got == web::Got::Need) {
            if (!running_) {
                close_with(1001);  // going away
                break;
            }
            int r = wait_readable(fd, 200);
            if (r < 0) break;
            if (r == 1 && recv_some(fd, buf) <= 0) break;
            continue;
        }
        if (f.op == 9) {
            if (!send_all(fd, web::make_frame(10, f.payload))) break;
            continue;
        }
        if (f.op == 10) continue;
        if (f.op == 8) {
            if (f.payload.size() == 1) {
                close_with(1002);
                break;
            }
            if (f.payload.size() >= 2) {
                int c = (static_cast<unsigned char>(f.payload[0]) << 8) | static_cast<unsigned char>(f.payload[1]);
                bool ok = (c >= 1000 && c <= 1003) || (c >= 1007 && c <= 1011) || (c >= 3000 && c <= 4999);
                if (!ok) {
                    close_with(1002);
                    break;
                }
                if (!web::valid_utf8(f.payload.substr(2))) {
                    close_with(1007);
                    break;
                }
                close_with(c);
            } else {
                send_all(fd, web::make_frame(8, ""));
            }
            break;
        }
        if (f.op == 0) {
            if (!in_message) {
                close_with(1002);
                break;
            }
            if (message.size() + f.payload.size() > kMaxMessage) {
                close_with(1009);
                break;
            }
            message += f.payload;
        } else {  // 1 text, 2 binary
            if (in_message) {
                close_with(1002);
                break;
            }
            if (f.op == 2) {
                close_with(1003);  // requests are text
                break;
            }
            message = std::move(f.payload);
            in_message = true;
        }
        if (!f.fin) continue;
        in_message = false;
        if (!web::valid_utf8(message)) {
            close_with(1007);
            break;
        }
        if (!send_all(fd, web::make_frame(1, dispatch(handler_, message)))) break;
        message.clear();
    }
    close_fd(fd);
}

}  // namespace pna
