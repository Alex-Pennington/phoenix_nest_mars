// appliance-core: the headless station service.
//
//   appliance-core --station A --tx-mode 600S --captures DIR --db-dir ./data
//   appliance-core --loopback-pair A,B --db-dir ./data --api-port 4100
//
// See README.md for every option.
#include "api_server.h"
#include "core.h"
#include "greybeard_modem.h"
#include "loopback_modem.h"

#include <csignal>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
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
        "appliance-core: headless MARS station core (milestone 1)\n\n"
        "One station, greybeard-ale as the modem (separate programs, files only):\n"
        "  --station NAME        this station's call (A-Z 0-9 -)\n"
        "  --tx-mode MODE        gale-tx --mode, e.g. 600S (required)\n"
        "  --tx-cmd PROG         gale-tx, or a stand-in (default gale-tx)\n"
        "  --tx-arg ARG          an argument put before gale-tx's own (repeatable)\n"
        "  --captures DIR        greybeard-ale's capture directory (watched)\n"
        "  --outbox DIR          where burst WAVs go (default DB-DIR/outbox)\n"
        "  --db-dir DIR          the store, work files and state (default .)\n"
        "  --api-port N          local API on 127.0.0.1:N (default 4100)\n\n"
        "Two stations in one process, no modem:\n"
        "  --loopback-pair A,B   stations A and B wired back to back\n"
        "  --db-dir DIR          where their stores go (default .)\n"
        "  --api-port N          A answers on N, B on N+1\n"
        "  --loopback-bps N      simulated airtime (default 600, 0 = instant)\n\n"
        "Both:\n"
        "  --no-acks             do not answer received messages with an ACK\n"
        "  --max-attempts N      modem failures before a message fails (default 3)\n";
}

struct Station {
    std::unique_ptr<Core> core;
    std::unique_ptr<ApiServer> api;
};

bool start_station(Station& s, CoreConfig cfg, std::unique_ptr<ModemDriver> modem, uint16_t port) {
    std::string name = cfg.station;
    s.core = std::make_unique<Core>(std::move(cfg), std::move(modem));
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

}  // namespace

int main(int argc, char** argv) {
    std::string pair, db_dir = ".", station, tx_mode, captures, outbox;
    std::string tx_prog = "gale-tx";
    std::vector<std::string> tx_args;
    uint16_t api_port = 4100;
    int loop_bps = 600;
    bool acks = true;
    int max_attempts = 3;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << a << " needs a value\n";
                std::exit(2);
            }
            return argv[++i];
        };
        try {
            if (a == "--help" || a == "-h") {
                usage();
                return 0;
            } else if (a == "--api-port") api_port = static_cast<uint16_t>(std::stoi(next()));
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
            else {
                std::cerr << "unknown option " << a << " (try --help)\n";
                return 2;
            }
        } catch (const std::exception&) {
            std::cerr << a << " needs a number\n";
            return 2;
        }
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::vector<Station> stations;

    try {
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
            stations.resize(1);
            CoreConfig c{station, db_dir + "/" + station + ".db", max_attempts, 30, acks};
            if (!start_station(stations[0], c, std::make_unique<GreybeardModem>(g), api_port)) return 1;
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
    return 0;
}
