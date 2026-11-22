// appliance-cli: talk to a running appliance-core from the shell.
//
//   appliance-cli [--port 4100] status
//   appliance-cli [--port 4100] send --to B [--prec R] "message text"
//   appliance-cli [--port 4100] list [N]
//   appliance-cli [--port 4100] get ID
//
// Prints the core's JSON reply. Exit code 0 when the reply says ok.
#include "../src/json.h"
#include "../src/net.h"

#include <iostream>
#include <string>

using namespace pna;

int main(int argc, char** argv) {
    uint16_t port = 4100;
    int i = 1;
    if (i + 1 < argc && std::string(argv[i]) == "--port") {
        port = static_cast<uint16_t>(std::stoi(argv[i + 1]));
        i += 2;
    }
    if (i >= argc) {
        std::cerr << "usage: appliance-cli [--port N] status | send --to B [--prec R] TEXT | list [N] | get ID\n";
        return 2;
    }
    std::string cmd = argv[i++];
    JsonWriter w;
    w.begin_object().field("cmd", cmd);
    if (cmd == "send") {
        std::string to, prec = "R", body;
        for (; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--to" && i + 1 < argc) to = argv[++i];
            else if (a == "--prec" && i + 1 < argc) prec = argv[++i];
            else body += (body.empty() ? "" : " ") + a;
        }
        w.field("to", to).field("prec", prec).field("body", body);
    } else if (cmd == "list" && i < argc) {
        w.field("limit", std::stoi(argv[i]));
    } else if (cmd == "get" && i < argc) {
        w.field("id", std::string(argv[i]));
    }
    w.end_object();

    int fd = tcp_connect("127.0.0.1", port, 2000);
    if (fd < 0) {
        std::cerr << "no appliance-core answering on 127.0.0.1:" << port << "\n";
        return 1;
    }
    if (!send_all(fd, w.str() + "\n")) return 1;
    std::string reply;
    while (reply.find('\n') == std::string::npos) {
        if (wait_readable(fd, 5000) != 1 || read_some(fd, reply) <= 0) break;
    }
    close_fd(fd);
    if (reply.empty()) {
        std::cerr << "no reply\n";
        return 1;
    }
    std::cout << reply;
    return reply.find("\"ok\":true") != std::string::npos ? 0 : 1;
}
