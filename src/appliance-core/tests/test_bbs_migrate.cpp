// The BBS tables of a station that ran a core from before bbs_rule.world and
// bbs_routed.ale_pos, upgraded in place.
//
// 1. The BBS's own migration adds both columns while another connection to
//    the same file holds the write lock, as the core's store does on a
//    running station. The migration waits its turn; it never fails with
//    "database is locked".
// 2. The real service starts on such a file and stops cleanly on SIGTERM.
// 3. A BBS startup that fails (and a start whose API port is taken) ends the
//    service with exit 1 and no crash.
//
// Calls are made up. Nothing here opens a radio or a real station's file.
#include "../src/bbs.h"
#include "../src/core.h"
#include "../src/loopback_modem.h"
#include "check.h"
#include "services_fixture.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sqlite3.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>
#include <unistd.h>

using namespace pna;
namespace fs = std::filesystem;

static const std::string kBuild = PNA_BUILD_DIR;

static bool wait_for(const std::function<bool()>& cond, int ms = 10000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return cond();
}

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void sql(sqlite3* db, const std::string& s) {
    char* err = nullptr;
    if (sqlite3_exec(db, s.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::cout << "sql failed: " << (err ? err : "?") << ": " << s << "\n";
        ++check::failures();
    }
    sqlite3_free(err);
}

static bool has_column(const fs::path& db, const char* table, const char* column) {
    sqlite3* c = nullptr;
    if (sqlite3_open(db.c_str(), &c) != SQLITE_OK) {
        sqlite3_close(c);
        return false;
    }
    sqlite3_stmt* s = nullptr;
    bool has = false;
    std::string q = std::string("SELECT COUNT(*) FROM pragma_table_info('") + table + "') WHERE name = '" + column + "'";
    if (sqlite3_prepare_v2(c, q.c_str(), -1, &s, nullptr) == SQLITE_OK && sqlite3_step(s) == SQLITE_ROW)
        has = sqlite3_column_int64(s, 0) == 1;
    sqlite3_finalize(s);
    sqlite3_close(c);
    return has;
}

// The BBS tables as a core from before the two columns left them, with a
// rule and a routed message in them, and the scan mark already set.
static void old_bbs_tables(const fs::path& db) {
    sqlite3* c = nullptr;
    sqlite3_open(db.c_str(), &c);
    sqlite3_busy_timeout(c, 2000);
    sql(c, "PRAGMA journal_mode=WAL");
    sql(c, "CREATE TABLE IF NOT EXISTS events (ts INTEGER NOT NULL, msg_id TEXT, text TEXT NOT NULL)");
    sql(c, "CREATE TABLE bbs_meta (k TEXT PRIMARY KEY, v TEXT NOT NULL)");
    sql(c, "CREATE TABLE bbs_guard (item TEXT PRIMARY KEY, pos INTEGER NOT NULL)");
    sql(c, "CREATE TABLE bbs_alias (ri TEXT PRIMARY KEY, members TEXT NOT NULL)");
    sql(c, "CREATE TABLE bbs_rule (id INTEGER PRIMARY KEY AUTOINCREMENT, pos INTEGER NOT NULL,"
           " match TEXT NOT NULL, route TEXT NOT NULL, ale TEXT NOT NULL, key TEXT NOT NULL DEFAULT '',"
           " sched TEXT NOT NULL DEFAULT '', compress INTEGER NOT NULL DEFAULT 0,"
           " mark_processed INTEGER NOT NULL DEFAULT 1)");
    sql(c, "CREATE TABLE bbs_routed (msg_id TEXT PRIMARY KEY, rule INTEGER NOT NULL, state TEXT NOT NULL,"
           " out_id TEXT NOT NULL DEFAULT '', due INTEGER NOT NULL, processed INTEGER NOT NULL DEFAULT 0,"
           " why TEXT NOT NULL DEFAULT '', at INTEGER NOT NULL)");
    sql(c, "CREATE TABLE bbs_seen (msg_id TEXT PRIMARY KEY)");
    sql(c, "CREATE TABLE bbs_qru (requester TEXT NOT NULL, line INTEGER NOT NULL, msg_id TEXT NOT NULL,"
           " PRIMARY KEY(requester, line))");
    sql(c, "CREATE TABLE bbs_zee (requester TEXT NOT NULL, page INTEGER NOT NULL, text TEXT NOT NULL,"
           " PRIMARY KEY(requester, page))");
    sql(c, "CREATE TABLE bbs_qsl (msg_id TEXT NOT NULL, by TEXT NOT NULL, at INTEGER NOT NULL, PRIMARY KEY(msg_id, by))");
    sql(c, "CREATE TABLE bbs_auto (at INTEGER NOT NULL, kind TEXT NOT NULL, msg_id TEXT NOT NULL)");
    sql(c, "CREATE TABLE inbox_flags (msg_id TEXT PRIMARY KEY, read_at INTEGER NOT NULL DEFAULT 0,"
           " done TEXT NOT NULL DEFAULT '', done_at INTEGER NOT NULL DEFAULT 0, qsl_at INTEGER NOT NULL DEFAULT 0,"
           " tag TEXT NOT NULL DEFAULT '', corrected BLOB, corrected_at INTEGER NOT NULL DEFAULT 0)");
    sql(c, "INSERT INTO bbs_meta (k, v) VALUES ('scan_after', '0')");
    sql(c, "INSERT INTO bbs_rule (pos, match, route, ale) VALUES (1, 'RI XYZZY', 'TSTB', 'TSTB')");
    sql(c, "INSERT INTO bbs_routed (msg_id, rule, state, due, at) VALUES ('TSTA-1', 1, 'due', 0, 0)");
    sqlite3_close(c);
}

// ------------------------------------------------------------ 1. the migration

static void migrate_under_a_writer(const fs::path& dir) {
    fs::create_directories(dir);
    const fs::path db = dir / "TSTA.db";
    Core core(CoreConfig{"TSTA", db.string(), 3, 10, true}, std::make_unique<LoopbackModem>());
    core.set_services(test_services());
    old_bbs_tables(db);
    CHECK(!has_column(db, "bbs_rule", "world") && !has_column(db, "bbs_routed", "ale_pos"));

    // Another connection, as the core's store is on a running station, holds
    // the write lock while the BBS opens and lets it go a little later.
    sqlite3* w = nullptr;
    sqlite3_open(db.c_str(), &w);
    sqlite3_busy_timeout(w, 2000);
    sql(w, "BEGIN IMMEDIATE");
    sql(w, "INSERT INTO events (ts, msg_id, text) VALUES (1, '', 'a write in flight')");
    std::thread writer([w] {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        sql(w, "COMMIT");
    });

    std::string err;
    bool opened = false;
    try {
        bbs::Engine e(core);
        opened = true;
    } catch (const std::exception& x) {
        err = x.what();
    }
    writer.join();
    sqlite3_close(w);
    if (!opened) std::cout << "BBS open failed: " << err << "\n";
    CHECK(opened);
    CHECK(err.find("locked") == std::string::npos);
    CHECK(has_column(db, "bbs_rule", "world"));
    CHECK(has_column(db, "bbs_routed", "ale_pos"));

    // The rows survive with the defaults, and the BBS opens again unchanged.
    try {
        bbs::Engine e(core);
        CHECK(e.rules().size() == 1 && e.rules()[0].world == "mars");
    } catch (const std::exception& x) {
        std::cout << "BBS reopen failed: " << x.what() << "\n";
        CHECK(false);
    }
}

// ------------------------------------------------------------ 2-3. the service

static uint16_t free_port() {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    socklen_t len = sizeof sa;
    ::getsockname(s, reinterpret_cast<sockaddr*>(&sa), &len);
    uint16_t p = ntohs(sa.sin_port);
    ::close(s);
    return p;
}

// The service as the station runs it: greybeard-ale's link with gale-key
// asked to key the outbox (here a socket nothing answers on). Receive only:
// gale-tx runs only for a send, and none is made.
static pid_t start_station(const fs::path& dir, uint16_t port, const fs::path& log) {
    fs::create_directories(dir / "captures");
    pid_t pid = ::fork();
    if (pid == 0) {
        int lf = ::open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        ::dup2(lf, 1);
        ::dup2(lf, 2);
        std::string exe = kBuild + "/appliance-core";
        std::vector<std::string> a = {exe,          "--station",        "TSTS",
                                      "--db-dir",   dir.string(),       "--api-port",
                                      std::to_string(port),             "--tx-mode",
                                      "600S",       "--captures",       (dir / "captures").string(),
                                      "--gale-key-socket",              (dir / "no-gale-key.sock").string()};
        std::vector<char*> av;
        for (auto& s : a) av.push_back(const_cast<char*>(s.c_str()));
        av.push_back(nullptr);
        ::execv(exe.c_str(), av.data());
        _exit(127);
    }
    return pid;
}

// The child's wait status, or -1 when it had to be killed.
static int reap(pid_t pid, int ms = 15000) {
    int st = 0;
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (::waitpid(pid, &st, WNOHANG) == pid) return st;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &st, 0);
    return -1;
}

static bool clean_exit(int st, int code) { return st >= 0 && WIFEXITED(st) && WEXITSTATUS(st) == code; }

static void say_status(const char* what, int st, const fs::path& log) {
    if (st < 0) std::cout << what << ": killed after the wait\n";
    else if (WIFSIGNALED(st)) std::cout << what << ": died on signal " << WTERMSIG(st) << "\n";
    else std::cout << what << ": exit " << WEXITSTATUS(st) << "\n";
    std::cout << slurp(log);
}

static void service_upgrades_and_stops(const fs::path& dir) {
    fs::create_directories(dir);
    old_bbs_tables(dir / "TSTS.db");
    const fs::path log = dir / "core.log";
    pid_t pid = start_station(dir, free_port(), log);
    CHECK(wait_for([&] { return slurp(log).find("API on") != std::string::npos; }));
    ::kill(pid, SIGTERM);
    int st = reap(pid);
    if (!clean_exit(st, 0)) say_status("upgrade start", st, log);
    CHECK(clean_exit(st, 0));
    CHECK(slurp(log).find("startup failed") == std::string::npos);
    CHECK(has_column(dir / "TSTS.db", "bbs_rule", "world"));
    CHECK(has_column(dir / "TSTS.db", "bbs_routed", "ale_pos"));
}

static void bbs_failure_exits_cleanly(const fs::path& dir) {
    fs::create_directories(dir);
    // A view where the BBS keeps its routed table: its migration can't add the
    // column, so the BBS fails to open after the core has started.
    {
        sqlite3* c = nullptr;
        sqlite3_open((dir / "TSTS.db").c_str(), &c);
        sql(c, "CREATE VIEW bbs_routed AS SELECT 'X' AS msg_id");
        sqlite3_close(c);
    }
    const fs::path log = dir / "core.log";
    pid_t pid = start_station(dir, free_port(), log);
    int st = reap(pid);
    if (!clean_exit(st, 1)) say_status("bbs failure", st, log);
    CHECK(clean_exit(st, 1));
    CHECK(slurp(log).find("startup failed: bbs sqlite") != std::string::npos);
}

static void api_port_taken_exits_cleanly(const fs::path& dir) {
    fs::create_directories(dir);
    // The API port already held: start_station fails after the core and the
    // BBS are running.
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa);
    ::listen(s, 1);
    socklen_t len = sizeof sa;
    ::getsockname(s, reinterpret_cast<sockaddr*>(&sa), &len);
    const fs::path log = dir / "core.log";
    pid_t pid = start_station(dir, ntohs(sa.sin_port), log);
    int st = reap(pid);
    ::close(s);
    if (!clean_exit(st, 1)) say_status("API port taken", st, log);
    CHECK(clean_exit(st, 1));
    CHECK(slurp(log).find("cannot open API port") != std::string::npos);
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("pna_bbs_migrate_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    migrate_under_a_writer(dir / "unit");
    service_upgrades_and_stops(dir / "upgrade");
    bbs_failure_exits_cleanly(dir / "bbsfail");
    api_port_taken_exits_cleanly(dir / "porttaken");
    if (!check::failures()) fs::remove_all(dir);
    return check::report("test_bbs_migrate");
}
