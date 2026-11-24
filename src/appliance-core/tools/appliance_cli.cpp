// appliance-cli: talk to a running appliance-core from the shell.
//
//   appliance-cli [--port 4100] status
//   appliance-cli [--port 4100] setup [CALL]    first start: what the box needs, or give it its call
//                                 (it then makes its station keys and card, and the station starts)
//   appliance-cli [--port 4100] send --to B [--prec R] "message text"
//   appliance-cli [--port 4100] list [N]
//   appliance-cli [--port 4100] get ID
//   appliance-cli [--port 4100] api CMD [key=value ...]
//   appliance-cli [--port 4100] keys import FILE     load a key tape (from its USB stick)
//   appliance-cli [--port 4100] keys list            the codress keys held (never the keys themselves)
//   appliance-cli [--port 4100] keys zeroize IND|TAPE
//   appliance-cli [--port 4100] keys open FILE|ID    load a key tape sealed to this station
//   appliance-cli [--port 4100] station card         this station's card and fingerprint
//   appliance-cli [--port 4100] cards import FILE|ID another station's card
//   appliance-cli [--port 4100] cards list
//   appliance-cli [--port 4100] cards check CALL "FINGERPRINT" [--keymaster]   after reading it back by voice
//   appliance-cli [--port 4100] cards keymaster CALL [--off]   name (or unname) a checked card as keymaster
//   appliance-cli [--port 4100] handout import FILE|FOLDER|ID [--replace]   load a member handout, stale or new;
//                                 a FOLDER is a handout packet (handout.txt, and cipher/ for a net's own cipher)
//   appliance-cli [--port 4100] keymaster list   keymasters this member accepted, refused or hasn't decided on
//   appliance-cli [--port 4100] keymaster accept CALL "FINGERPRINT" [--in-person]
//                                 the member's own choice, after reading the fingerprint back by voice
//                                 (or in person); handouts waiting on it then load
//   appliance-cli [--port 4100] keymaster refuse CALL   its tapes are not taken; its waiting handouts dropped
//   appliance-cli [--port 4100] bundle make [FILE] [--label "NET"] [--for CALL]   this box's startup bundle
//                                 (book, channel designators, schedules, message settings), signed; to FILE
//                                 for a stick. --for: that member's copy, frequencies sealed to its card
//   appliance-cli [--port 4100] bundle send CALL[,CALL...] [FILE]   over the air as signed plain traffic;
//                                 each station gets its own copy, frequencies sealed to its card
//   appliance-cli [--port 4100] bundle load FILE|ID [--yes]   load one; --yes for a signer whose card
//                                 this box hasn't checked (it is marked so)
//   appliance-cli [--port 4100] cipher           the cipher in the slot (docs/cipher-slot.md)
//   appliance-cli [--port 4100] cipher install FOLDER   install a cipher module (a folder with module.so)
//   appliance-cli [--port 4100] cipher remove    back to the built-in pna-aes
//   appliance-cli [--port 4100] rally            what this box can do now, and the next step
//   appliance-cli [--port 4100] import FILE [--kind roster|book|freq|schedule]
//                                 a roster, address book, frequency card or net schedule (.xlsx or text)
//   appliance-cli [--port 4100] channels         the channel plan, by designator (never frequencies)
//   appliance-cli [--port 4100] channels add NAME FREQ [--mode USB] [--use "PRIMARY"] [--unit khz|mhz|hz]
//   appliance-cli [--port 4100] form [list] | form show ID | form fill ID KEY=VALUE ... | form check ID ...
//   appliance-cli [--port 4100] form template ID | form edit ID FILE | form reset ID
//                                 enter one channel by hand (FREQ in kHz unless --unit says)
//   appliance-cli [--port 4100] channels remove NAME
//
// "api" sends any call in docs/api.md: appliance-cli api net_callup who=all.
// A value with spaces goes in quotes; \n in a value is a new line.
//
// Prints the core's JSON reply. Exit code 0 when the reply says ok.
#include "../src/json.h"
#include "../src/net.h"

#include "../src/crypto.h"
#include "../src/keys.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace pna;

static std::string base64(const std::string& in) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : in) {
        acc = (acc << 8) | c;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out += k[(acc >> bits) & 63];
        }
    }
    if (bits) out += k[(acc << (6 - bits)) & 63];
    while (out.size() % 4) out += '=';
    return out;
}

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
                     "                    | keys zeroize IND|TAPE | keys open FILE|ID\n"
                     "                    | station card | cards import FILE|ID [--replace] | cards list\n"
                     "                    | cards check CALL FINGERPRINT [--keymaster] | cards keymaster CALL [--off]\n"
                     "                    | handout import FILE|FOLDER|ID [--replace] | rally\n"
                     "                    | keymaster list | keymaster accept CALL FINGERPRINT [--in-person]\n"
                     "                    | keymaster refuse CALL | bundle make [FILE] [--label NET] [--for CALL]\n"
                     "                    | bundle send CALL[,CALL...] [FILE] | bundle load FILE|ID [--yes]\n"
                     "                    | cipher | cipher install FOLDER | cipher remove\n"
                     "                    | import FILE [--kind roster|book|freq|schedule] | channels\n"
                     "                    | channels add NAME FREQ [--mode M] [--use TEXT] [--unit khz|mhz|hz]\n"
                     "                    | form [list] | form show ID | form fill ID KEY=VALUE ... | form check ID ...\n"
                     "                    | form template ID | form edit ID FILE | form reset ID\n"
                     "                    | channels remove NAME | setup [CALL]\n";
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
            // A framed key tape file is opened here, so plain text goes to
            // the core; an MSC tape stops here, unopened.
            if (keys::looks_framed(text)) {
                std::string plain, err;
                bool ok = keys::open_tape_file(text, plain, &err);
                crypto::wipe(text);
                if (!ok) {
                    std::cerr << argv[i] << ": " << err << "\n";
                    return 1;
                }
                text = std::move(plain);
            }
            w = JsonWriter();
            w.begin_object().field("cmd", "key_import").field("text", text);
            crypto::wipe(text);
        } else if (sub == "list") {
            w = JsonWriter();
            w.begin_object().field("cmd", "keys_list");
        } else if (sub == "zeroize" && i < argc) {
            w = JsonWriter();
            w.begin_object().field("cmd", "key_zeroize").field("which", std::string(argv[i]));
        } else if (sub == "open" && i < argc) {
            w = JsonWriter();
            w.begin_object().field("cmd", "key_open");
            std::ifstream f(argv[i], std::ios::binary);
            if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                w.field("text", ss.str());
            } else {
                w.field("id", std::string(argv[i]));
            }
        } else {
            std::cerr << "keys import FILE | keys list | keys zeroize IND|TAPE | keys open FILE|ID\n";
            return 2;
        }
    } else if (cmd == "station") {
        w = JsonWriter();
        w.begin_object().field("cmd", "station_card");
    } else if (cmd == "cards") {
        std::string sub = i < argc ? argv[i++] : "";
        w = JsonWriter();
        if (sub == "import" && i < argc) {
            w.begin_object().field("cmd", "card_import");
            std::ifstream f(argv[i], std::ios::binary);
            if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                w.field("text", ss.str()).field("source", std::string("file ") + argv[i]);
            } else {
                w.field("id", std::string(argv[i]));
            }
            if (i + 1 < argc && std::string(argv[i + 1]) == "--replace") w.field("replace", "Y");
        } else if (sub == "list") {
            w.begin_object().field("cmd", "cards_list");
        } else if (sub == "check" && i + 1 < argc) {
            w.begin_object().field("cmd", "card_check").field("station", std::string(argv[i])).field(
                "fingerprint", std::string(argv[i + 1]));
            if (i + 2 < argc && std::string(argv[i + 2]) == "--keymaster") w.field("keymaster", "Y");
        } else if (sub == "keymaster" && i < argc) {
            bool off = i + 1 < argc && std::string(argv[i + 1]) == "--off";
            w.begin_object()
                .field("cmd", "card_keymaster")
                .field("station", std::string(argv[i]))
                .field("keymaster", off ? "N" : "Y");
        } else {
            std::cerr << "cards import FILE|ID [--replace] | cards list | cards check CALL FINGERPRINT [--keymaster]"
                         " | cards keymaster CALL [--off]\n";
            return 2;
        }
    } else if (cmd == "handout") {
        std::string sub = i < argc ? argv[i++] : "";
        w = JsonWriter();
        if (sub == "import" && i < argc) {
            w.begin_object().field("cmd", "handout_import");
            std::error_code ec;
            std::ifstream f(argv[i], std::ios::binary);
            if (std::filesystem::is_directory(argv[i], ec)) {
                // A handout packet: the core reads it from where it is.
                w.field("packet", std::filesystem::absolute(argv[i], ec).string());
            } else if (f) {
                std::ostringstream ss;
                ss << f.rdbuf();
                w.field("text", ss.str());
            } else {
                w.field("id", std::string(argv[i]));
            }
            if (i + 1 < argc && std::string(argv[i + 1]) == "--replace") w.field("replace", "Y");
        } else {
            std::cerr << "handout import FILE|FOLDER|ID [--replace]\n";
            return 2;
        }
    } else if (cmd == "keymaster") {
        std::string sub = i < argc ? argv[i++] : "";
        w = JsonWriter();
        if (sub.empty() || sub == "list") {
            w.begin_object().field("cmd", "keymaster_list");
        } else if (sub == "accept" && i + 1 < argc) {
            bool in_person = i + 2 < argc && std::string(argv[i + 2]) == "--in-person";
            w.begin_object()
                .field("cmd", "keymaster_accept")
                .field("station", std::string(argv[i]))
                .field("fingerprint", std::string(argv[i + 1]))
                .field("how", in_person ? "in person" : "by voice");
        } else if (sub == "refuse" && i < argc) {
            w.begin_object().field("cmd", "keymaster_refuse").field("station", std::string(argv[i]));
        } else {
            std::cerr << "keymaster list | keymaster accept CALL FINGERPRINT [--in-person] | keymaster refuse CALL\n";
            return 2;
        }
    } else if (cmd == "bundle") {
        std::string sub = i < argc ? argv[i++] : "";
        w = JsonWriter();
        std::error_code ec;
        std::string file, label, to, member;
        bool yes = false;
        if (sub == "send" && i < argc) to = argv[i++];
        for (; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--label" && i + 1 < argc) label = argv[++i];
            else if (a == "--for" && i + 1 < argc) member = argv[++i];
            else if (a == "--yes") yes = true;
            else if (file.empty()) file = a;
        }
        if (sub == "make") {
            w.begin_object().field("cmd", "bundle_make");
            if (!label.empty()) w.field("label", label);
            if (!file.empty()) w.field("out", std::filesystem::absolute(file, ec).string());
            if (!member.empty()) w.field("for", member);
        } else if (sub == "send" && !to.empty()) {
            w.begin_object().field("cmd", "bundle_send").field("to", to);
            if (!label.empty()) w.field("label", label);
            if (!file.empty()) w.field("file", std::filesystem::absolute(file, ec).string());
        } else if (sub == "load" && !file.empty()) {
            w.begin_object().field("cmd", "bundle_load");
            if (std::filesystem::is_regular_file(file, ec)) w.field("file", std::filesystem::absolute(file, ec).string());
            else w.field("id", file);
            if (yes) w.field("accept", "Y");
        } else {
            std::cerr << "bundle make [FILE] [--label NET] [--for CALL] | bundle send CALL[,CALL...] [FILE] | bundle load FILE|ID [--yes]\n";
            return 2;
        }
    } else if (cmd == "cipher") {
        std::string sub = i < argc ? argv[i++] : "";
        w = JsonWriter();
        std::error_code ec;
        if (sub.empty() || sub == "status") {
            w.begin_object().field("cmd", "cipher_status");
        } else if (sub == "install" && i < argc) {
            w.begin_object().field("cmd", "cipher_install").field("path", std::filesystem::absolute(argv[i], ec).string());
        } else if (sub == "remove") {
            w.begin_object().field("cmd", "cipher_remove");
        } else {
            std::cerr << "cipher | cipher install FOLDER | cipher remove\n";
            return 2;
        }
    } else if (cmd == "import") {
        if (i >= argc) {
            std::cerr << "import FILE [--kind roster|book|freq|schedule]\n";
            return 2;
        }
        std::ifstream f(argv[i], std::ios::binary);
        if (!f) {
            std::cerr << "cannot read " << argv[i] << "\n";
            return 1;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        std::string path = argv[i++], kind = "auto";
        if (i + 1 < argc && std::string(argv[i]) == "--kind") kind = argv[i + 1];
        std::string name = path.substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
        w = JsonWriter();
        w.begin_object()
            .field("cmd", "mars_import")
            .field("data", base64(ss.str()))
            .field("kind", kind)
            .field("source", "file " + name);
    } else if (cmd == "setup") {
        w = JsonWriter();
        if (i < argc) w.begin_object().field("cmd", "setup_station").field("call", std::string(argv[i]));
        else w.begin_object().field("cmd", "setup_status");
    } else if (cmd == "channels") {
        std::string sub = i < argc ? argv[i++] : "";
        w = JsonWriter();
        if (sub.empty() || sub == "list") {
            w.begin_object().field("cmd", "channels_list");
        } else if (sub == "add" && i + 1 < argc) {
            w.begin_object().field("cmd", "channel_put").field("name", std::string(argv[i])).field(
                "frequency", std::string(argv[i + 1]));
            for (i += 2; i + 1 < argc; i += 2) {
                std::string a = argv[i];
                if (a == "--mode") w.field("mode", std::string(argv[i + 1]));
                else if (a == "--use") w.field("use", std::string(argv[i + 1]));
                else if (a == "--unit") w.field("unit", std::string(argv[i + 1]));
            }
        } else if (sub == "remove" && i < argc) {
            w.begin_object().field("cmd", "channel_remove").field("name", std::string(argv[i]));
        } else {
            std::cerr << "channels | channels add NAME FREQ [--mode M] [--use TEXT] [--unit khz|mhz|hz]"
                         " | channels remove NAME\n";
            return 2;
        }
    } else if (cmd == "form") {
        // Forms from templates (docs/forms.md): what gale-term draws.
        std::string sub = i < argc ? argv[i++] : "";
        w = JsonWriter();
        auto pairs = [&](int from) {
            for (int k = from; k < argc; ++k) {
                std::string a = argv[k];
                size_t eq = a.find('=');
                if (eq != std::string::npos) w.field(a.substr(0, eq), a.substr(eq + 1));
            }
        };
        if (sub.empty() || sub == "list") {
            w.begin_object().field("cmd", "form_list");
        } else if (sub == "show" && i < argc) {
            w.begin_object().field("cmd", "form_get").field("form", std::string(argv[i]));
            pairs(i + 1);
        } else if ((sub == "fill" || sub == "check") && i < argc) {
            w.begin_object().field("cmd", sub == "fill" ? "form_submit" : "form_check").field("form", std::string(argv[i]));
            pairs(i + 1);
        } else if (sub == "template" && i < argc) {
            w.begin_object().field("cmd", "form_template_get").field("form", std::string(argv[i]));
        } else if (sub == "edit" && i + 1 < argc) {
            std::ifstream f(argv[i + 1], std::ios::binary);
            if (!f) {
                std::cerr << "can't read " << argv[i + 1] << "\n";
                return 1;
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            w.begin_object().field("cmd", "form_template_put").field("form", std::string(argv[i])).field("text", ss.str());
        } else if (sub == "reset" && i < argc) {
            w.begin_object().field("cmd", "form_template_reset").field("form", std::string(argv[i]));
        } else {
            std::cerr << "form | form show ID [KEY=VALUE] | form fill ID KEY=VALUE ... | form check ID KEY=VALUE ...\n"
                         "     | form template ID | form edit ID FILE | form reset ID\n";
            return 2;
        }
    } else if (cmd == "rally") {
        w = JsonWriter();
        w.begin_object().field("cmd", "rally_status");
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
