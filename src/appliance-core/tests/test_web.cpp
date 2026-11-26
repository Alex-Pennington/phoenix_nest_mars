// The web port (web_server.h): static files, path escapes, the WebSocket
// handshake and frames, rid, origin and size limits, and the same replies as
// the API port.
#include "../src/api_server.h"
#include "../src/core.h"
#include "../src/json.h"
#include "../src/loopback_modem.h"
#include "../src/net.h"
#include "../src/web_server.h"
#include "check.h"
#include "services_fixture.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

using namespace pna;
namespace fs = std::filesystem;

static void write_file(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// Reads until the peer closes or nothing comes for timeout_ms.
static std::string read_all(int fd, int timeout_ms = 3000) {
    std::string out;
    for (;;) {
        int r = wait_readable(fd, timeout_ms);
        if (r != 1 || read_some(fd, out) <= 0) break;
    }
    return out;
}

static std::string http(uint16_t port, const std::string& raw) {
    int fd = tcp_connect("127.0.0.1", port, 2000);
    if (fd < 0) return "";
    send_all(fd, raw);
    std::string r = read_all(fd);
    close_fd(fd);
    return r;
}

static std::string get(uint16_t port, const std::string& target, const std::string& host = "") {
    std::string h = host.empty() ? "127.0.0.1:" + std::to_string(port) : host;
    return http(port, "GET " + target + " HTTP/1.1\r\nHost: " + h + "\r\n\r\n");
}

static int code_of(const std::string& resp) {
    if (resp.size() < 12 || resp.compare(0, 9, "HTTP/1.1 ") != 0) return 0;
    return std::stoi(resp.substr(9, 3));
}

static std::string body_of(const std::string& resp) {
    size_t e = resp.find("\r\n\r\n");
    return e == std::string::npos ? "" : resp.substr(e + 4);
}

static const char* kKey = "dGhlIHNhbXBsZSBub25jZQ==";

struct Ws {
    int fd = -1;
    std::string buf;
    std::string head;
};

static Ws ws_open(uint16_t port, const std::string& extra = "Origin: http://127.0.0.1:%P\r\n") {
    Ws w;
    w.fd = tcp_connect("127.0.0.1", port, 2000);
    std::string ex = extra;
    size_t p = ex.find("%P");
    if (p != std::string::npos) ex.replace(p, 2, std::to_string(port));
    send_all(w.fd, "GET /ws HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                       "\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                       "Sec-WebSocket-Key: " + std::string(kKey) + "\r\n" + ex + "\r\n");
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (w.buf.find("\r\n\r\n") == std::string::npos && std::chrono::steady_clock::now() < until)
        if (wait_readable(w.fd, 200) == 1 && read_some(w.fd, w.buf) <= 0) break;
    size_t e = w.buf.find("\r\n\r\n");
    if (e != std::string::npos) {
        w.head = w.buf.substr(0, e);
        w.buf.erase(0, e + 4);
    }
    return w;
}

// One server frame (unmasked). op -1 when none came.
static std::pair<int, std::string> ws_read(Ws& w, int timeout_ms = 3000) {
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        if (w.buf.size() >= 2) {
            int op = static_cast<unsigned char>(w.buf[0]) & 0x0f;
            uint64_t len = static_cast<unsigned char>(w.buf[1]) & 0x7f;
            size_t at = 2;
            bool have = true;
            if (len == 126) {
                have = w.buf.size() >= 4;
                if (have) len = (static_cast<unsigned char>(w.buf[2]) << 8) | static_cast<unsigned char>(w.buf[3]);
                at = 4;
            } else if (len == 127) {
                have = w.buf.size() >= 10;
                if (have) {
                    len = 0;
                    for (int k = 0; k < 8; ++k) len = (len << 8) | static_cast<unsigned char>(w.buf[2 + k]);
                }
                at = 10;
            }
            if (have && w.buf.size() >= at + len) {
                std::string p = w.buf.substr(at, len);
                w.buf.erase(0, at + len);
                return {op, p};
            }
        }
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        if (left.count() <= 0) return {-1, ""};
        int r = wait_readable(w.fd, static_cast<int>(left.count()));
        if (r != 1 || read_some(w.fd, w.buf) <= 0) return {-1, ""};
    }
}

static std::string ws_call(Ws& w, const std::string& text) {
    send_all(w.fd, web::make_client_frame(1, text));
    auto f = ws_read(w);
    return f.first == 1 ? f.second : "";
}

static int close_code(const std::string& p) {
    return p.size() >= 2 ? (static_cast<unsigned char>(p[0]) << 8) | static_cast<unsigned char>(p[1]) : 0;
}

static std::string api_call(uint16_t port, const std::string& line) {
    int fd = tcp_connect("127.0.0.1", port, 2000);
    send_all(fd, line + "\n");
    std::string buf;
    while (buf.find('\n') == std::string::npos)
        if (wait_readable(fd, 3000) != 1 || read_some(fd, buf) <= 0) break;
    close_fd(fd);
    return buf.substr(0, buf.find('\n'));
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_test_web_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::path root = dir / "web";
    write_file(root / "index.html", "<!doctype html><title>gale-web</title>");
    write_file(root / "assets" / "app.js", "console.log(1)");
    write_file(root / "assets" / "app.css", "body{}");
    write_file(root / "assets" / "font.woff2", std::string("wOF2\0\1", 6));
    write_file(root / "icon.svg", "<svg/>");
    write_file(root / "data.json", "{}");
    write_file(root / "logo.png", "\x89PNG");
    write_file(root / ".env", "hidden");
    write_file(dir / "secret.txt", "outside the root");
    fs::create_symlink(dir / "secret.txt", root / "link.txt");
    fs::create_symlink(dir, root / "updir");
    std::string real = fs::canonical(root).string();

    // ---- paths ----
    {
        std::string f;
        CHECK(web::resolve(real, "/", f) && f == real + "/index.html");
        CHECK(web::resolve(real, "/?stub=1", f) && f == real + "/index.html");
        CHECK(web::resolve(real, "/assets/app.js", f) && f == real + "/assets/app.js");
        CHECK(web::resolve(real, "/assets/app.js?v=3#x", f));
        CHECK(web::resolve(real, "//assets/./app.js", f) && f == real + "/assets/app.js");
        CHECK(web::resolve(real, "/assets/app%2ejs", f) && f == real + "/assets/app.js");
        for (const char* bad : {"/../secret.txt", "/assets/../../secret.txt", "/%2e%2e/secret.txt", "/%2E%2E/secret.txt",
                                "/%2e%2e%2fsecret.txt", "/assets/..%2f..%2fsecret.txt", "/.%2e/secret.txt",
                                "/assets%2f..%2f..%2fsecret.txt", "/..%5csecret.txt", "/a%00b", "/%zz", "/%2",
                                "/.env", "/%2eenv", "/link.txt", "/updir/secret.txt", "/assets", "/assets/",
                                "/nope.js", "assets/app.js", "", "http://127.0.0.1/index.html", "/%252e%252e/secret.txt"})
            CHECK(!web::resolve(real, bad, f));
        CHECK(!web::resolve("", "/", f));  // no web root: everything is 404
    }

    // ---- content types ----
    CHECK(web::content_type("/x/index.html") == "text/html; charset=utf-8");
    CHECK(web::content_type("/x/a.js") == "text/javascript; charset=utf-8");
    CHECK(web::content_type("/x/a.CSS") == "text/css; charset=utf-8");
    CHECK(web::content_type("/x/a.woff2") == "font/woff2");
    CHECK(web::content_type("/x/a.svg") == "image/svg+xml");
    CHECK(web::content_type("/x/a.json") == "application/json");
    CHECK(web::content_type("/x/a.png") == "image/png");
    CHECK(web::content_type("/x.d/noext") == "application/octet-stream");

    // ---- handshake key (RFC 6455 section 1.3's example) and UTF-8 ----
    CHECK(web::ws_accept(kKey) == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    CHECK(web::valid_utf8("plain") && web::valid_utf8("caf\xc3\xa9") && web::valid_utf8("\xf0\x9f\x93\xbb"));
    CHECK(!web::valid_utf8("\xc3") && !web::valid_utf8("\xc0\xaf") && !web::valid_utf8("\xed\xa0\x80") &&
          !web::valid_utf8("\xf5\x80\x80\x80") && !web::valid_utf8("\xe0\x80\xaf"));

    // ---- frames ----
    {
        web::Frame f;
        int code = 0;
        std::string b = web::make_client_frame(1, "hello");
        std::string half = b.substr(0, 4);
        CHECK(web::read_frame(half, f, 1024, &code) == web::Got::Need);
        CHECK(web::read_frame(b, f, 1024, &code) == web::Got::Frame && f.fin && f.op == 1 && f.payload == "hello" &&
              b.empty());
        std::string big(70000, 'x');
        b = web::make_client_frame(1, big);
        CHECK(web::read_frame(b, f, 1u << 20, &code) == web::Got::Frame && f.payload == big);
        b = web::make_frame(1, "unmasked");
        CHECK(web::read_frame(b, f, 1024, &code) == web::Got::Error && code == 1002);
        b = web::make_client_frame(1, "x");
        b[0] = static_cast<char>(b[0] | 0x40);  // RSV1
        CHECK(web::read_frame(b, f, 1024, &code) == web::Got::Error && code == 1002);
        b = web::make_client_frame(3, "x");
        CHECK(web::read_frame(b, f, 1024, &code) == web::Got::Error && code == 1002);
        b = web::make_client_frame(9, "x", false);  // a fragmented ping
        CHECK(web::read_frame(b, f, 1024, &code) == web::Got::Error && code == 1002);
        b = web::make_client_frame(9, std::string(126, 'p'));
        CHECK(web::read_frame(b, f, 1024, &code) == web::Got::Error && code == 1002);
        // Too big is refused from the header alone: 2 MiB said, 10 bytes sent.
        std::string head = web::make_client_frame(1, std::string(2u << 20, 'x')).substr(0, 14);
        CHECK(web::read_frame(head, f, 1u << 20, &code) == web::Got::Error && code == 1009);
    }

    // ---- rid ----
    {
        std::string seen;
        WebServer::Handler echo = [&](const std::string& l) {
            seen = l;
            return std::string("{\"ok\":true}");
        };
        CHECK(WebServer::dispatch(echo, "{\"cmd\":\"status\",\"rid\":7}") == "{\"rid\":7,\"ok\":true}");
        CHECK(seen.find("rid") == std::string::npos && seen.find("\"cmd\":\"status\"") != std::string::npos);
        CHECK(WebServer::dispatch(echo, "{\"rid\":\"a\\\"b\",\"cmd\":\"x\"}") == "{\"rid\":\"a\\\"b\",\"ok\":true}");
        CHECK(WebServer::dispatch(echo, "{\"cmd\":\"x\",\"rid\":-12}") == "{\"rid\":-12,\"ok\":true}");
        const std::string plain = "{\"cmd\": \"status\"}";
        CHECK(WebServer::dispatch(echo, plain) == "{\"ok\":true}" && seen == plain);  // untouched without rid
        WebServer::Handler empty = [](const std::string&) { return std::string("{}"); };
        CHECK(WebServer::dispatch(empty, "{\"rid\":1}") == "{\"rid\":1}");
        // Not flat: the handler's error still carries the rid.
        WebServer::Handler strict = [](const std::string& l) {
            std::map<std::string, std::string> m;
            return std::string(parse_flat_object(l, m) ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"flat\"}");
        };
        CHECK(WebServer::dispatch(strict, "{\"rid\":5,\"x\":{\"y\":1}}") == "{\"rid\":5,\"ok\":false,\"error\":\"flat\"}");
        // Values keep their text when the request is rebuilt without rid.
        CHECK(WebServer::dispatch(strict, "{\"rid\":5,\"t\":\"line1\\nline2 caf\\u00e9\",\"n\":3,\"b\":true}") ==
              "{\"rid\":5,\"ok\":true}");
        std::map<std::string, std::string> m;
        WebServer::dispatch(echo, "{\"rid\":5,\"t\":\"line1\\nline2\",\"n\":3,\"b\":true}");
        CHECK(parse_flat_object(seen, m) && m.size() == 3 && m["t"] == "line1\nline2" && m["n"] == "3" && m["b"] == "true");
    }

    // ---- the server, on a real core, beside the API port ----
    uint16_t api_port = static_cast<uint16_t>(41000 + ::getpid() % 2000);
    uint16_t port = static_cast<uint16_t>(api_port + 2500);
    auto ma = std::make_unique<LoopbackModem>(0);
    auto mb = std::make_unique<LoopbackModem>(0);
    LoopbackModem::connect(*ma, *mb);
    Core core(CoreConfig{"STA-A", (dir / "a.db").string(), 3, 1, true}, std::move(ma));
    core.set_services(test_services());
    core.start();
    ApiServer api(core, api_port);
    CHECK(api.start());
    WebServer web(port, root.string(), [&core](const std::string& l) { return ApiServer::handle(core, l); });
    CHECK(web.root_ok());
    CHECK(web.start());
    {
        WebServer twice(port, root.string(), [](const std::string&) { return std::string("{}"); });
        CHECK(!twice.start());  // the port is taken
    }

    // Static files.
    std::string r = get(port, "/");
    CHECK(code_of(r) == 200 && body_of(r) == "<!doctype html><title>gale-web</title>" &&
          r.find("Content-Type: text/html; charset=utf-8\r\n") != std::string::npos &&
          r.find("Content-Length: 38\r\n") != std::string::npos);
    r = get(port, "/assets/app.js");
    CHECK(code_of(r) == 200 && r.find("Content-Type: text/javascript") != std::string::npos);
    r = get(port, "/assets/font.woff2");
    CHECK(code_of(r) == 200 && r.find("Content-Type: font/woff2") != std::string::npos && body_of(r).size() == 6);
    r = get(port, "/logo.png", "localhost:" + std::to_string(port));
    CHECK(code_of(r) == 200 && r.find("image/png") != std::string::npos);
    for (const char* bad : {"/../secret.txt", "/%2e%2e/secret.txt", "/assets/..%2f..%2fsecret.txt", "/link.txt",
                            "/.env", "/assets/", "/missing.html", "/updir/secret.txt"}) {
        r = get(port, bad);
        CHECK(code_of(r) == 404 && r.find("outside the root") == std::string::npos);
    }
    CHECK(code_of(http(port, "POST / HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                                 "\r\nContent-Length: 0\r\n\r\n")) == 404);
    CHECK(code_of(get(port, "/", "evil.example:" + std::to_string(port))) == 403);
    CHECK(code_of(http(port, "nonsense\r\n\r\n")) == 400);
    CHECK(code_of(http(port, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nX: " + std::string(20000, 'a') + "\r\n\r\n")) == 431);
    {
        WebServer noroot(static_cast<uint16_t>(port + 1), (dir / "nothere").string(),
                         [](const std::string&) { return std::string("{\"ok\":true}"); });
        CHECK(!noroot.root_ok() && noroot.start());
        CHECK(code_of(get(static_cast<uint16_t>(port + 1), "/")) == 404);
        Ws w = ws_open(static_cast<uint16_t>(port + 1));
        CHECK(code_of(w.head) == 101 && ws_call(w, "{\"cmd\":\"x\"}") == "{\"ok\":true}");
        close_fd(w.fd);
    }

    // The Origin rule (phoenix_nest_mars#3): no Origin header is taken from
    // this box only. The port is bound to 127.0.0.1, so a peer from elsewhere
    // can't be made here; the rule and the peer test are checked on their own.
    {
        CHECK(web::ws_origin_allowed(false, false, true));    // no Origin, from this box: taken
        CHECK(!web::ws_origin_allowed(false, false, false));  // no Origin, from elsewhere: refused
        CHECK(web::ws_origin_allowed(true, true, false));     // a page naming this port
        CHECK(!web::ws_origin_allowed(true, false, true));    // a page elsewhere, even from this box
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(0x7f000001);
        CHECK(web::addr_is_loopback(reinterpret_cast<sockaddr*>(&a)));
        a.sin_addr.s_addr = htonl(0x7f0a0b0c);
        CHECK(web::addr_is_loopback(reinterpret_cast<sockaddr*>(&a)));
        a.sin_addr.s_addr = htonl(0x0a4d4d17);  // a LAN address
        CHECK(!web::addr_is_loopback(reinterpret_cast<sockaddr*>(&a)));
        a.sin_addr.s_addr = htonl(0xc0a80105);
        CHECK(!web::addr_is_loopback(reinterpret_cast<sockaddr*>(&a)));
        sockaddr_in6 b{};
        b.sin6_family = AF_INET6;
        b.sin6_addr = in6addr_loopback;
        CHECK(web::addr_is_loopback(reinterpret_cast<sockaddr*>(&b)));
        b.sin6_addr = in6addr_any;
        b.sin6_addr.s6_addr[0] = 0xfe;
        b.sin6_addr.s6_addr[1] = 0x80;
        CHECK(!web::addr_is_loopback(reinterpret_cast<sockaddr*>(&b)));
        CHECK(!web::addr_is_loopback(nullptr));
        int sv[2];
        CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        CHECK(!web::peer_is_loopback(sv[0]));  // not an IP peer: not this box's loopback
        ::close(sv[0]);
        ::close(sv[1]);
        int fd = tcp_connect("127.0.0.1", port, 2000);
        CHECK(fd >= 0 && web::peer_is_loopback(fd));
        close_fd(fd);
    }

    // Handshake.
    {
        Ws w = ws_open(port);
        CHECK(code_of(w.head) == 101 && w.head.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
        close_fd(w.fd);
        w = ws_open(port, "Origin: http://localhost:%P\r\n");
        CHECK(code_of(w.head) == 101);
        close_fd(w.fd);
        w = ws_open(port, "");  // no Origin: a local program on this box, not a browser page
        CHECK(code_of(w.head) == 101);
        close_fd(w.fd);
        for (const char* o : {"Origin: http://evil.example\r\n", "Origin: http://127.0.0.1:1\r\n",
                              "Origin: https://127.0.0.1:%P\r\n", "Origin: null\r\n", "Origin: http://127.0.0.1:%P.evil\r\n"}) {
            w = ws_open(port, o);
            CHECK(code_of(w.head) == 403);
            close_fd(w.fd);
        }
        std::string p = std::to_string(port);
        CHECK(code_of(http(port, "GET /ws HTTP/1.1\r\nHost: 127.0.0.1:" + p +
                                     "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 8\r\n"
                                     "Sec-WebSocket-Key: " + std::string(kKey) + "\r\n\r\n")) == 426);
        CHECK(code_of(http(port, "GET /ws HTTP/1.1\r\nHost: 127.0.0.1:" + p +
                                     "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
                                     "Sec-WebSocket-Key: short\r\n\r\n")) == 400);
        CHECK(code_of(get(port, "/ws")) == 400);  // no upgrade asked
    }

    // The same calls as the API port, with rid.
    {
        Ws w = ws_open(port);
        for (const char* line : {"{\"cmd\":\"list\"}", "{\"cmd\":\"bbs_get\"}", "{\"cmd\":\"nope\"}", "{\"cmd\":\"guard_list\"}",
                                 "not json"}) {
            std::string via_api = api_call(api_port, line);
            CHECK(!via_api.empty());
            CHECK(ws_call(w, line) == via_api);
        }
        std::string via_api = api_call(api_port, "{\"cmd\":\"list\"}");
        std::string via_ws = ws_call(w, "{\"cmd\":\"list\",\"rid\":41}");
        CHECK(via_ws == "{\"rid\":41," + via_api.substr(1));

        // Fragmented, with a ping in the middle.
        send_all(w.fd, web::make_client_frame(1, "{\"cmd\":\"li", false) + web::make_client_frame(9, "are you there") +
                           web::make_client_frame(0, "st\",\"rid\"", false) + web::make_client_frame(0, ":42}", true));
        auto f = ws_read(w);
        CHECK(f.first == 10 && f.second == "are you there");
        f = ws_read(w);
        CHECK(f.first == 1 && f.second == "{\"rid\":42," + via_api.substr(1));

        // A big request is fine up to the limit (the core says what's wrong with it).
        std::string bigtext(900000, 'a');
        std::string reply = ws_call(w, "{\"cmd\":\"nope\",\"rid\":43,\"text\":\"" + bigtext + "\"}");
        CHECK(reply.rfind("{\"rid\":43,\"ok\":false", 0) == 0);

        // Close: answered with the same code.
        send_all(w.fd, web::make_client_frame(8, std::string("\x03\xe8", 2)));
        f = ws_read(w);
        CHECK(f.first == 8 && close_code(f.second) == 1000);
        CHECK(ws_read(w, 1000).first == -1);  // and the socket closes
        close_fd(w.fd);
    }
    // Too big: one frame over 1 MiB, refused before it is sent.
    {
        Ws w = ws_open(port);
        std::string frame = web::make_client_frame(1, std::string(WebServer::kMaxMessage + 1, 'x'));
        send_all(w.fd, frame.substr(0, 14));
        auto f = ws_read(w);
        CHECK(f.first == 8 && close_code(f.second) == 1009);
        close_fd(w.fd);
    }
    // Too big in pieces, each under the limit.
    {
        Ws w = ws_open(port);
        std::string piece(700000, 'x');
        send_all(w.fd, web::make_client_frame(1, piece, false) + web::make_client_frame(0, piece, true));
        auto f = ws_read(w);
        CHECK(f.first == 8 && close_code(f.second) == 1009);
        close_fd(w.fd);
    }
    // Binary, unmasked, bad UTF-8, a stray continuation: each closes with its code.
    {
        struct Case {
            std::string bytes;
            int code;
        };
        for (auto& c : {Case{web::make_client_frame(2, "{}"), 1003}, Case{web::make_frame(1, "{}"), 1002},
                        Case{web::make_client_frame(1, "\xc3("), 1007}, Case{web::make_client_frame(0, "x"), 1002},
                        Case{web::make_client_frame(1, "a", false) + web::make_client_frame(1, "b"), 1002}}) {
            Ws w = ws_open(port);
            send_all(w.fd, c.bytes);
            auto f = ws_read(w);
            CHECK(f.first == 8 && close_code(f.second) == c.code);
            close_fd(w.fd);
        }
    }
    // Many clients at once, and they all go away cleanly.
    {
        std::vector<std::thread> ts;
        std::atomic<int> good{0};
        for (int i = 0; i < 8; ++i)
            ts.emplace_back([&, i] {
                Ws w = ws_open(port);
                bool ok = code_of(w.head) == 101;
                for (int k = 0; k < 20 && ok; ++k) {
                    std::string rid = std::to_string(i * 100 + k);
                    ok = ws_call(w, "{\"cmd\":\"list\",\"rid\":" + rid + "}").rfind("{\"rid\":" + rid + ",", 0) == 0;
                }
                if (ok) ++good;
                close_fd(w.fd);
            });
        for (auto& t : ts) t.join();
        CHECK(good == 8);
    }
    // stop() with a socket still open: the client is told the server is going away.
    {
        Ws w = ws_open(port);
        CHECK(code_of(w.head) == 101);
        web.stop();
        auto f = ws_read(w);
        CHECK(f.first == 8 && close_code(f.second) == 1001);
        close_fd(w.fd);
        CHECK(code_of(get(port, "/")) == 0);  // nothing listening
    }

    api.stop();
    core.stop();
    fs::remove_all(dir);
    return check::report("test_web");
}
