// The station's settings, as the operator sees them (phoenix_nest_mars#3,
// asks 24, 34, 35, 36).
//
// Every setting is a row of one compiled table: its key (the config key, the
// long option without dashes), group, label, type, range or choices, unit,
// class and gate. Alex's ruling (2026-11-26): "Only host plumbing" is kept
// from the operator. The operator edits every other setting, station call
// included; store paths, ports, programs and link plumbing are "locked" and
// stay in appliance.conf.
//
// What the operator sets is kept in DB_DIR/operator-settings ("key = value",
// written whole by the core, mode 600). The core never writes appliance.conf.
// At start the operator's values are read last, after appliance.conf and the
// command line, so the kiosk's word stands; a line that is no longer valid is
// skipped with a log line, never a reason not to start. Most rows apply at
// the next start ("applies":"restart"); the world and keying rows apply now.
//
// The keying rows (ptt-line, cat-ptt, world,
// engineering-readouts) are for gale-key, greybeard-ale's host keying
// service: the core writes them to the keying file (keying-file, default
// GALE-DIR/keying.json) whole, at start and on every change, and answers
// keying_settings with the same object. The core never keys the radio.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pna {
namespace settings {

struct Row {
    std::string key, group, label;
    std::string type;                  // text, int, bool, choice, call, list, action
    std::vector<std::string> choices;  // choice
    long long lo = 0, hi = 0;          // int
    std::string unit;
    std::string cls = "operator";      // operator or locked (host plumbing)
    std::string gate = "none";         // none or confirm (the reply asks; confirm=Y sets)
    std::string applies = "restart";   // now or restart
    std::string def;                   // shown as the default ("" for none)
    std::string help, warning;
    std::string call;                  // action rows: the call that sets it (identity_set...)
};

// The whole table, in the kiosk's order.
const std::vector<Row>& table();
// The row for a key ("ptt_line" is read as "ptt-line"), or nullptr.
const Row* find(const std::string& key);
// A value checked against its row: true with *norm (case and spacing as the
// core keeps it), false with *err.
bool check(const Row& r, const std::string& value, std::string* norm, std::string* err);

// "--key value" arguments, as read_config gives them, turned into key ->
// value (a flag such as no-acks is "yes"; a repeated key keeps the last,
// list keys join with a space).
std::map<std::string, std::string> args_map(const std::vector<std::string>& args);

// DB_DIR/operator-settings.
std::string operator_file(const std::string& db_dir);
// Its operator rows that are still valid; each skipped line goes in *skipped.
std::map<std::string, std::string> load_operator(const std::string& db_dir, std::vector<std::string>* skipped);
// The arguments that put the operator's values into force at start (put
// after appliance.conf and the command line).
std::vector<std::string> overlay_args(const std::map<std::string, std::string>& op);

// Set once at start, from main(): where each value came from.
struct Start {
    std::string db_dir = ".", gale_dir = ".";
    std::map<std::string, std::string> config, cli, op;
};
void init(const Start& s);

// The value in force now, and where it came from (operator, command line,
// config, default).
std::string value(const std::string& key, std::string* source = nullptr);

// The keying object gale-key reads, and writing it to the keying file.
std::string keying_json(int64_t now);
bool write_keying(int64_t now, std::string* err);
// At start: a world set in the settings (not read from it) written to
// GALE-DIR/world, the file the scanner picks its world from, when it differs.
bool sync_world(std::string* err);

// The calls: settings_list, settings_set, settings_reset, keying_settings.
// nullopt for any other cmd.
std::optional<std::string> handle(const std::string& cmd, std::map<std::string, std::string>& req, int64_t now);

}  // namespace settings
}  // namespace pna
