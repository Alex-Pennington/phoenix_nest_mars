// appliance-cli: talk to a running appliance-core from the shell.
//
//   appliance-cli [--port 4100] status
//   appliance-cli [--port 4100] send --to B [--prec R] "message text"
//   appliance-cli [--port 4100] list [N]
//   appliance-cli [--port 4100] get ID
//   appliance-cli [--port 4100] api CMD [key=value ...]
//   appliance-cli [--port 4100] keys import FILE     load a key tape (from its USB stick)
//   appliance-cli [--port 4100] keys list            the codress keys held (never the keys themselves)
//   appliance-cli [--port 4100] keys zeroize IND|TAPE
//
// "api" sends any call in docs/api.md: appliance-cli api net_callup who=all.
// A value with spaces goes in quotes; \n in a value is a new line.
//
// Prints the core's JSON reply. Exit code 0 when the reply says ok.
#include "../src/json.h"
#include "../src/net.h"

#include "../src/crypto.h"

#include <fstream>
#include <iostream>
#include <sstream>
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
        std::cerr << "usage: appliance-cli [--port N] status | send --to B [--prec R] TEXT | list [N] | get ID\n"
                     "                    | api CMD [key=value ...] | keys import FILE | keys list\n"
                     "                    | keys zeroize IND|TAPE\n";
        return 2;
    }
    std::string cmd = argv[i++];
    JsonWriter w;
    const bool via_api = cmd == "api";
    if (via_api) {
        if (i >= argc) {
            std::cerr << "api needs a command, e.g. appliance-cli api net_status\n";
            return 2;
        }
        cmd = argv[i++];
    }
    w.begin_object().field("cmd", cmd);
    if (via_api) {
        for (; i < argc; ++i) {
            std::string a = argv[i];
            size_t eq = a.find('=');
            if (eq == std::string::npos || eq == 0) {
                std::cerr << "write each setting as key=value, not \"" << a << "\"\n";
                return 2;
            }
            std::string v, raw = a.substr(eq + 1);
            for (size_t k = 0; k < raw.size(); ++k) {
                if (raw[k] == '\\' && k + 1 < raw.size() && raw[k + 1] == 'n') {
                    v += '\n';
                    ++k;
                } else {
                    v += raw[k];
                }
            }
            w.field(a.substr(0, eq), v);
        }
    } else if (cmd == "send") {
        std::string to, prec = "R", body;
        for (; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--to" && i + 1 < argc) to = argv[++i];
            else if (a == "--prec" && i + 1 < argc) prec = argv[++i];
            else body += (body.empty() ? "" : " ") + a;
        }
        w.field("to", to).field("prec", prec).field("body", body);
    } else if (cmd == "keys") {
        std::string sub = i < argc ? argv[i++] : "";
        if (sub == "import" && i < argc) {
            std::ifstream f(argv[i], std::ios::binary);
            if (!f) {
                std::cerr << "cannot read " << argv[i] << "\n";
                return 1;
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            std::string text = ss.str();
            w = JsonWriter();
            w.begin_object().field("cmd", "key_import").field("text", text);
            crypto::wipe(text);
        } else if (sub == "list") {
            w = JsonWriter();
            w.begin_object().field("cmd", "keys_list");
        } else if (sub == "zeroize" && i < argc) {
            w = JsonWriter();
            w.begin_object().field("cmd", "key_zeroize").field("which", std::string(argv[i]));
        } else {
            std::cerr << "keys import FILE | keys list | keys zeroize IND|TAPE\n";
            return 2;
        }
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
