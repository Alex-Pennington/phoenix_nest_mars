// appliance-core: the headless station service.
//
//   appliance-core --station A --tx-mode 600S --captures DIR --db-dir ./data
//   appliance-core --loopback-pair A,B --db-dir ./data --api-port 4100
//
// See README.md for every option.
#include "api_server.h"
#include "compose.h"
#include "config.h"
#include "core.h"
#include "station.h"
#include "greybeard_modem.h"
#include "link_monitor.h"
#include "ops.h"
#include "loopback_modem.h"
#include "s5066.h"
#include "s5066_client.h"
#include "appa_modem.h"

#include <csignal>
#include <filesystem>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace pna;

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

std::mutex g_out_mu;
void log_line(const std::string& station, const std::string& text) {
    std::time_t t = std::time(nullptr);
    char ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%SZ", std::gmtime(&t));
    std::lock_guard<std::mutex> g(g_out_mu);
    std::cout << ts << " [" << station << "] " << text << std::endl;
}

void usage() {
    std::cout <<
        "appliance-core: headless MARS station core\n\n"
        "  --config FILE         read settings from FILE first (key = value, the long\n"
        "                        options below without the dashes); options given\n"
        "                        on the command line win\n"
        "  --check               check the settings, say what would run, and stop\n\n"
        "One station, greybeard-ale as the modem (separate programs, files only):\n"
        "  --station NAME        this station's call (A-Z 0-9 -)\n"
        "  --tx-mode MODE        gale-tx --mode, e.g. 600S (required)\n"
        "  --tx-cmd PROG         gale-tx, or a stand-in (default gale-tx)\n"
        "  --tx-arg ARG          an argument put before gale-tx's own (repeatable)\n"
        "  --captures DIR        greybeard-ale's capture directory (watched)\n"
        "  --outbox DIR          where burst WAVs go (default DB-DIR/outbox)\n"
        "  --db-dir DIR          the store, work files and state (default .)\n"
        "  --api-port N          local API on 127.0.0.1:N (default 4100)\n\n"
        "One station, through the STANAG 5066 node (greybeard-ale's interface 7):\n"
        "  --station NAME        this station's call\n"
        "  --link s5066          send through the 5066 node instead of gale-tx\n"
        "  --s5066-peers FILE    CALL ADDRESS per line, e.g. STA-B 0.0.0.2 (required)\n"
        "  --s5066-port N        the node's SIS port on 127.0.0.1 (default 5066)\n"
        "  --s5066-sap N         SAP to bind, 0-15 (default 3, HMTP; same on every box)\n"
        "  --s5066-ttl SEC       time to live per send (default 600)\n"
        "  --s5066-confirm WHAT  node or client delivery confirmation (default node)\n\n"
        "One station, straight to the modem over 110D Appendix A (no 5066 node):\n"
        "  --station NAME        this station's call\n"
        "  --link appa           be the modem's DTE on its Appendix A TCP data socket\n"
        "  --appa-port N         the modem's data socket on 127.0.0.1 (default 3000)\n\n"
        "Two stations in one process, no modem:\n"
        "  --loopback-pair A,B   stations A and B wired back to back\n"
        "  --db-dir DIR          where their stores go (default .)\n"
        "  --api-port N          A answers on N, B on N+1\n"
        "  --loopback-bps N      simulated airtime (default 600, 0 = instant)\n\n"
        "Both:\n"
        "  --no-acks             do not answer received messages with an ACK\n"
        "  --max-attempts N      modem failures before a message fails (default 3)\n"
        "  --advisor PROG        a local program that explains compose fields and suggests\n"
        "                        wording (gets the request as one JSON argument; see\n"
        "                        docs/api.md). Without it the built-in guidance is used.\n"
        "  --advisor-arg ARG     an argument put before the request (repeatable)\n"
        "  --link-socket PATH    read link state from greybeard-ale: ale-scan --control's\n"
        "                        socket (status every --link-poll-ms, default 2000)\n"
        "  --link-gate           hold each message until greybeard-ale reports a link to\n"
        "                        its station (needs --link-socket)\n";
}

struct Station {
    std::unique_ptr<Core> core;
    std::unique_ptr<ApiServer> api;
};

std::shared_ptr<LinkMonitor> g_link;
bool g_link_gate = false;

bool start_station(Station& s, CoreConfig cfg, std::unique_ptr<ModemDriver> modem, uint16_t port) {
    std::string name = cfg.station;
    std::string db = cfg.db_path;
    cfg.link_gate = g_link_gate;
    s.core = std::make_unique<Core>(std::move(cfg), std::move(modem));
    // The station book (address book, nets, duty, log, reports) in the same file.
    s.core->set_ops(std::make_shared<Ops>(name, db));
    if (g_link) s.core->set_link_monitor(g_link);
    // Codress keys beside the database: STATION.keys (keys.h).
    {
        std::string kp = db.size() > 3 && db.compare(db.size() - 3, 3, ".db") == 0 ? db.substr(0, db.size() - 3) : db;
        auto kr = std::make_shared<keys::Keyring>(kp + ".keys");
        std::string err;
        if (!kr->load(&err)) log_line(name, "codress keys not loaded (" + err + "): import the key tape again");
        s.core->set_keyring(kr);
        // The cipher slot (cipher_slot.h): the net's module if one was
        // installed from a handout packet, else our own pna-aes.
        std::string cd = kp + ".cipher";
        s.core->set_cipher_dir(cd);
        auto slot = cipher::load_installed(cd);
        if (!slot->usable()) log_line(name, slot->why_unusable());
        else if (!slot->is_builtin())
            log_line(name, "cipher slot: " + slot->info().name + " " + slot->info().version + " from the net's packet");
        s.core->set_cipher(slot);
        // The station's own keypair (station.h): made here the first time.
        auto id = std::make_shared<station::Identity>();
        bool exists = false;
        std::string ip = kp + ".id";
        if (!station::load_identity(ip, *id, &exists, &err)) {
            log_line(name, "station keys not loaded (" + err + ")");
        } else if (!exists || id->station != name) {
            if (exists) log_line(name, "station keys in " + ip + " are for " + id->station + ", not " + name + ": left alone");
            else if (station::make_identity(name, static_cast<int64_t>(std::time(nullptr)), *id, &err) &&
                     station::save_identity(ip, *id, &err))
                log_line(name, "station keys made: fingerprint " + station::fingerprint(station::card_of(*id)));
            else log_line(name, "station keys not made (" + err + ")");
        }
        if (!id->station.empty() && id->station == name) s.core->set_identity(id, ip);
    }
    s.core->on_log([name](const std::string& t) { log_line(name, t); });
    s.core->start();
    s.api = std::make_unique<ApiServer>(*s.core, port);
    if (!s.api->start()) {
        log_line(name, "cannot open API port " + std::to_string(port));
        return false;
    }
    log_line(name, "API on 127.0.0.1:" + std::to_string(port));
    return true;
}

// A program named on its own is looked for on PATH, as the service will.
std::string find_program(const std::string& prog) {
    if (prog.find('/') != std::string::npos) return ::access(prog.c_str(), X_OK) == 0 ? prog : "";
    const char* path = std::getenv("PATH");
    std::string p = path ? path : "";
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find(':', start);
        std::string dir = p.substr(start, end == std::string::npos ? std::string::npos : end - start);
        std::string full = (dir.empty() ? "." : dir) + "/" + prog;
        if (::access(full.c_str(), X_OK) == 0) return full;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "";
}

}  // namespace

int main(int argc, char** argv) {
    std::string pair, db_dir = ".", station, tx_mode, captures, outbox;
    std::string tx_prog = "gale-tx";
    std::vector<std::string> tx_args;
    std::string advisor_prog;
    std::vector<std::string> advisor_args;
    uint16_t api_port = 4100;
    int loop_bps = 600;
    bool acks = true;
    int max_attempts = 3;
    std::string link_socket;
    int link_poll_ms = 2000;
    bool link_gate = false;
    std::string link_kind = "burst";
    S5066Config s5066;
    AppaConfig appa;

    bool check_only = false;
    std::string config_path;
    // The config file's settings come first, so the command line wins.
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--config" && i + 1 < argc) config_path = argv[i + 1];
    if (!config_path.empty()) {
        std::string err;
        if (!read_config(config_path, &args, &err)) {
            std::cerr << "config: " << err << "\n";
            return 2;
        }
    }
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

    for (size_t i = 0; i < args.size(); ++i) {
        std::string a = args[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= args.size()) {
                std::cerr << a << " needs a value\n";
                std::exit(2);
            }
            return args[++i];
        };
        try {
            if (a == "--help" || a == "-h") {
                usage();
                return 0;
            } else if (a == "--config") next();
            else if (a == "--check") check_only = true;
            else if (a == "--api-port") api_port = static_cast<uint16_t>(std::stoi(next()));
            else if (a == "--loopback-pair") pair = next();
            else if (a == "--station") station = next();
            else if (a == "--tx-mode") tx_mode = next();
            else if (a == "--tx-cmd") tx_prog = next();
            else if (a == "--tx-arg") tx_args.push_back(next());
            else if (a == "--captures") captures = next();
            else if (a == "--outbox") outbox = next();
            else if (a == "--db-dir") db_dir = next();
            else if (a == "--loopback-bps") loop_bps = std::stoi(next());
            else if (a == "--no-acks") acks = false;
            else if (a == "--max-attempts") max_attempts = std::stoi(next());
            else if (a == "--advisor") advisor_prog = next();
            else if (a == "--advisor-arg") advisor_args.push_back(next());
            else if (a == "--link-socket") link_socket = next();
            else if (a == "--link-poll-ms") link_poll_ms = std::stoi(next());
            else if (a == "--link-gate") link_gate = true;
            else if (a == "--link") link_kind = next();
            else if (a == "--s5066-peers") s5066.peers_file = next();
            else if (a == "--appa-port") appa.port = static_cast<uint16_t>(std::stoi(next()));
            else if (a == "--s5066-port") s5066.port = static_cast<uint16_t>(std::stoi(next()));
            else if (a == "--s5066-sap") {
                int v = std::stoi(next());
                if (v < 0 || v > 15) {
                    std::cerr << "--s5066-sap: 0 to 15\n";
                    return 2;
                }
                s5066.sap = static_cast<uint8_t>(v);
            } else if (a == "--s5066-ttl") s5066.ttl_seconds = static_cast<uint32_t>(std::stoul(next()));
            else if (a == "--s5066-confirm") {
                std::string v = next();
                if (v == "node") s5066.confirm = s5066::kNodeConfirm;
                else if (v == "client") s5066.confirm = s5066::kClientConfirm;
                else {
                    std::cerr << "--s5066-confirm: node or client\n";
                    return 2;
                }
            }
            else {
                std::cerr << "unknown option " << a << " (try --help)\n";
                return 2;
            }
        } catch (const std::exception&) {
            std::cerr << a << " needs a number\n";
            return 2;
        }
    }

    if (link_kind != "burst" && link_kind != "s5066" && link_kind != "appa") {
        std::cerr << "--link: burst, s5066 or appa\n";
        return 2;
    }
    if (link_gate && link_socket.empty()) {
        std::cerr << "--link-gate needs --link-socket\n";
        return 2;
    }
    if (!link_socket.empty()) {
        g_link = std::make_shared<LinkMonitor>(link_socket, link_poll_ms < 200 ? 200 : link_poll_ms);
        g_link->start();
        g_link_gate = link_gate;
    }
    if (!advisor_prog.empty()) {
        std::vector<std::string> argv_adv{advisor_prog};
        argv_adv.insert(argv_adv.end(), advisor_args.begin(), advisor_args.end());
        pna::compose::set_advisor(pna::compose::program_advisor(argv_adv));
    } else if (!advisor_args.empty()) {
        std::cerr << "--advisor-arg needs --advisor\n";
        return 2;
    }

    if (check_only) {
        // Say what would run and what stands in the way, then stop.
        namespace fs = std::filesystem;
        std::vector<std::string> problems, notes;
        std::cout << "appliance-core settings" << (config_path.empty() ? "" : " from " + config_path) << "\n";
        if (!pair.empty()) {
            std::cout << "  practice pair  " << pair << " on the loopback modem, no radio\n"
                      << "  API            127.0.0.1:" << api_port << " and " << api_port + 1 << "\n";
        } else if (!station.empty()) {
            if (!valid_station(station) || is_broadcast(station))
                problems.push_back("station \"" + station + "\": use A-Z, digits and -, at most 16, not ALL");
            std::cout << "  station        " << station << "\n";
            if (link_kind == "s5066") {
                std::cout << "  link           STANAG 5066 node at 127.0.0.1:" << s5066.port << ", SAP "
                          << int(s5066.sap) << "\n";
                std::map<std::string, uint32_t> peers;
                std::string perr;
                if (s5066.peers_file.empty()) problems.push_back("s5066-peers isn't set (CALL ADDRESS per line)");
                else if (!s5066::read_peers(s5066.peers_file, peers, &perr)) problems.push_back("s5066-peers: " + perr);
                else std::cout << "  5066 peers     " << peers.size() << " stations in " << s5066.peers_file << "\n";
                notes.push_back("the 5066 node is looked for at start and every few seconds after; it needn't be up now");
            } else if (link_kind == "appa") {
                std::cout << "  link           110D Appendix A modem at 127.0.0.1:" << appa.port
                          << " (the waveform is the modem's setting)\n";
                notes.push_back("the modem is looked for at start and every few seconds after; it needn't be up now");
            } else {
                std::cout << "  modem          greybeard-ale, gale-tx mode " << (tx_mode.empty() ? "(not set)" : tx_mode)
                          << "\n";
                if (tx_mode.empty()) problems.push_back("tx-mode isn't set (the gale-tx mode, like 600S)");
                std::string tx = find_program(tx_prog);
                std::cout << "  gale-tx        " << (tx.empty() ? tx_prog + " (not found)" : tx) << "\n";
                if (tx.empty())
                    problems.push_back("tx-cmd: " + tx_prog + " isn't found; give its full path, as the service " +
                                       "may not have your PATH");
                std::cout << "  captures       " << (captures.empty() ? "(not set)" : captures) << "\n";
                std::error_code ec;
                if (captures.empty()) problems.push_back("captures isn't set (greybeard-ale's capture directory)");
                else if (!fs::is_directory(captures, ec)) problems.push_back("captures: " + captures + " isn't a directory");
            }
            std::cout << "  API            127.0.0.1:" << api_port << "\n";
        } else {
            problems.push_back("set station (this station's call), or loopback-pair to practise with no radio");
        }
        std::error_code ec;
        std::cout << "  data           " << db_dir << "\n";
        if (fs::exists(db_dir, ec)) {
            if (!fs::is_directory(db_dir, ec)) problems.push_back("db-dir: " + db_dir + " isn't a directory");
            else if (::access(db_dir.c_str(), W_OK) != 0) problems.push_back("db-dir: " + db_dir + " can't be written");
        } else {
            notes.push_back("db-dir " + db_dir + " doesn't exist yet; it is made at the first start");
        }
        if (!link_socket.empty()) {
            std::cout << "  link state     " << link_socket << (link_gate ? ", messages wait for a link" : "") << "\n";
            if (!fs::exists(link_socket, ec))
                notes.push_back("the link socket isn't there yet: start greybeard-ale's ale-scan with --control");
        } else {
            std::cout << "  link state     not read (no link-socket)\n";
        }
        if (!advisor_prog.empty()) {
            std::string adv = find_program(advisor_prog);
            std::cout << "  advisor        " << (adv.empty() ? advisor_prog + " (not found)" : adv) << "\n";
            if (adv.empty()) problems.push_back("advisor: " + advisor_prog + " isn't found");
        }
        for (auto& n : notes) std::cout << "note: " << n << "\n";
        for (auto& p : problems) std::cout << "problem: " << p << "\n";
        std::cout << (problems.empty() ? "ready to run\n" : "fix the problems above, then check again\n");
        return problems.empty() ? 0 : 1;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::vector<Station> stations;

    try {
        std::filesystem::create_directories(db_dir);
        if (!pair.empty()) {
            size_t comma = pair.find(',');
            std::string a = pair.substr(0, comma), b = comma == std::string::npos ? "" : pair.substr(comma + 1);
            if (!valid_station(a) || !valid_station(b) || a == b) {
                std::cerr << "--loopback-pair needs two different station names, e.g. A,B\n";
                return 2;
            }
            auto ma = std::make_unique<LoopbackModem>(loop_bps);
            auto mb = std::make_unique<LoopbackModem>(loop_bps);
            LoopbackModem::connect(*ma, *mb);
            stations.resize(2);
            CoreConfig ca{a, db_dir + "/" + a + ".db", max_attempts, 10, acks};
            CoreConfig cb{b, db_dir + "/" + b + ".db", max_attempts, 10, acks};
            if (!start_station(stations[0], ca, std::move(ma), api_port)) return 1;
            if (!start_station(stations[1], cb, std::move(mb), api_port + 1)) return 1;
        } else if (!station.empty()) {
            if (!valid_station(station)) {
                std::cerr << "--station: letters A-Z, digits and -, at most 16\n";
                return 2;
            }
            stations.resize(1);
            CoreConfig c{station, db_dir + "/" + station + ".db", max_attempts, 30, acks};
            std::unique_ptr<ModemDriver> modem;
            if (link_kind == "s5066") {
                if (s5066.peers_file.empty()) {
                    std::cerr << "--link s5066 needs --s5066-peers (try --help)\n";
                    return 2;
                }
                modem = std::make_unique<S5066Client>(s5066);
            } else if (link_kind == "appa") {
                modem = std::make_unique<AppaModem>(appa);
            } else {
                if (tx_mode.empty() || captures.empty()) {
                    std::cerr << "--station needs --tx-mode and --captures (try --help)\n";
                    return 2;
                }
                GreybeardConfig g;
                g.tx_cmd = {tx_prog};
                g.tx_cmd.insert(g.tx_cmd.end(), tx_args.begin(), tx_args.end());
                g.mode = tx_mode;
                g.captures_dir = captures;
                g.work_dir = db_dir + "/" + station + "-work";
                g.outbox_dir = outbox.empty() ? db_dir + "/" + station + "-outbox" : outbox;
                g.state_file = db_dir + "/" + station + "-captures-seen.txt";
                modem = std::make_unique<GreybeardModem>(g);
            }
            if (!start_station(stations[0], c, std::move(modem), api_port)) return 1;
        } else {
            std::cerr << "give --station (greybeard-ale) or --loopback-pair (try --help)\n";
            return 2;
        }
    } catch (const std::exception& e) {
        std::cerr << "startup failed: " << e.what() << "\n";
        return 1;
    }

    while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    for (auto& s : stations) {
        if (s.api) s.api->stop();
        if (s.core) s.core->stop();
    }
    if (g_link) g_link->stop();
    return 0;
}
