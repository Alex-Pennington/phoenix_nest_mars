// The station's book: address book and collectives, nets and check-ins,
// operator duty, the station log and the monthly report. Milestone 7.
//
// Everything works offline: no lookup goes outside the appliance. The book
// keeps only what traffic needs (call, routing indicator, ALE address,
// collective members and a short note); it has no field for a person's name,
// phone or home address, and an import drops such columns and says so.
//
// Kept in the station's own SQLite file beside the message store, on its own
// connection. Times are UTC unix seconds; months are UTC calendar months.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <memory>
#include <string>
#include <vector>

struct sqlite3;

namespace pna {

struct BookEntry {
    std::string name;     // the key: a call, a tactical name or a PLA, upper case
    std::string kind;     // "station" or "collective"
    std::string call;     // station: the call the core sends to (default: name)
    std::string ri;       // routing indicator, if any
    std::string ale;      // ALE address, if any
    std::vector<std::string> members;  // collective: names of stations or collectives
    std::string note;     // short, no personal details
    std::string source;   // "local" or what the import said
    std::string world = "mars";  // mars or amateur: an amateur entry is never sent to (ask 37)
};

struct Resolved {
    std::vector<BookEntry> stations;   // in order, each once
    std::vector<std::string> unknown;  // names the book doesn't have
    std::vector<std::string> loops;    // collectives met again inside themselves
};

struct CheckIn {
    std::string station;
    int64_t at = 0;
    int64_t out_at = 0;    // 0 while checked in
    std::string via;       // "radio" (heard by the core) or "manual"
    int traffic = 0;       // messages the station says it holds for the net
    std::string remark;
    bool in_roster = false;
};

struct Net {
    std::string id;        // NET-n
    std::string name;
    std::string ncs;       // net control
    bool exercise = false;
    int64_t opened_at = 0;
    int64_t closed_at = 0;  // 0 while open
    std::vector<std::string> roster;  // station names expected
    std::string note;
    std::vector<CheckIn> checkins;
    std::vector<std::string> missing() const;  // roster stations with no check-in
};

struct Duty {
    int64_t id = 0;
    std::string op;         // operator sign or call
    int64_t on_at = 0;
    int64_t off_at = 0;     // 0 while on duty
    std::string turnover;   // notes for the next operator
};

struct LogEntry {
    int64_t id = 0;
    int64_t at = 0;
    std::string kind;   // net, checkin, duty, turnover, note, comspot, beadwindow
    std::string text;
    std::string net;    // net id, if any
    bool exercise = false;
};

struct MonthlyReport {
    std::string month;   // YYYY-MM
    double duty_hours = 0;
    std::vector<std::pair<std::string, double>> hours_by_operator;
    int nets = 0, nets_exercise = 0, nets_as_ncs = 0, checkins = 0;
    int sent = 0, sent_acked = 0, sent_failed = 0, received = 0, exercise_msgs = 0;
    int sent_by_prec[4] = {0, 0, 0, 0};      // Z O P R
    int received_by_prec[4] = {0, 0, 0, 0};
    int comspots = 0, beadwindows = 0, notes = 0;
    std::vector<Net> net_list;
    std::string text;    // plain report, upper case, lines of 69 or fewer
};

// A net that meets on a schedule. Times are UTC.
//   days: DAILY, WEEKDAYS, WEEKENDS, or a comma list of MON..SUN, each with
//         an optional week of the month in front: 1SAT is the first Saturday,
//         LSUN the last Sunday. "TUE,THU" or "1SAT,3SAT".
struct NetSchedule {
    std::string name;       // the key; the net opens under this name
    std::string days;       // as above, kept in capitals with commas
    int start = 0;          // minutes after 0000Z (written HHMM)
    int minutes = 60;       // how long the net runs
    std::vector<std::string> roster;  // book names or calls; resolved when it opens
    bool exercise = false;
    std::string ncs;        // net control; empty: this station
    std::string note;
    bool auto_open = false; // the appliance opens it at the start time
    int64_t last_start = 0; // the occurrence last opened
    std::string last_net;   // the net it opened as
    std::string channel;    // the channel's designator, never a frequency
};

// One sitting of a scheduled net.
struct NetSitting {
    NetSchedule sched;
    int64_t start = 0, end = 0;
    std::string state;      // "upcoming", "due" (started, not opened) or "open"
    std::string net;        // the open net's id when state is "open"
};

// "1900" -> 1140. False unless HHMM with HH 00-23 and MM 00-59.
bool parse_hhmm(const std::string& s, int* minutes);
// Puts days in the canonical form; false with *error when it can't.
bool parse_days(const std::string& s, std::string* canon, std::string* error);
// The first sitting of `days` at `start` (minutes) that starts at or after t.
int64_t next_start(const std::string& days, int start, int64_t t);

// True if a message body is marked as an exercise: a line that starts with
// EXERCISE (MARS practice is "EXERCISE EXERCISE EXERCISE" top and bottom).
bool is_exercise_body(const std::string& body);

// Upper case, spaces trimmed and runs of spaces made one.
std::string book_key(const std::string& s);

class ChannelPlan;

class Ops {
public:
    // station: our own call. db_path: the station's SQLite file.
    Ops(std::string station, const std::string& db_path);
    ~Ops();
    Ops(const Ops&) = delete;
    Ops& operator=(const Ops&) = delete;

    // Tests set the clock; default is the system clock.
    void set_clock(std::function<int64_t()> now);
    int64_t now() const;
    const std::string& station() const { return station_; }
    // The channel plan (channels.h), kept in its own file beside the database.
    ChannelPlan& channels() { return *channels_; }

    // Address book.
    bool book_put(BookEntry e, std::string* error);
    bool book_remove(const std::string& name);
    std::optional<BookEntry> book_get(const std::string& name);
    // By name, call, RI or ALE address.
    std::optional<BookEntry> book_find(const std::string& any);
    std::vector<BookEntry> book_list();
    // CSV with a header row: name,kind,call,ri,ale,members,note (members split
    // on ';' or '|'). Other columns are dropped and named in *dropped.
    int book_import(const std::string& csv, const std::string& source, std::vector<std::string>* dropped,
                    std::vector<std::string>* errors);
    std::string book_export();
    Resolved resolve(const std::string& name_or_list);
    // The calls to send to for `to`: the book's resolution when it knows the
    // name, else `to` itself.
    std::vector<std::string> send_targets(const std::string& to, Resolved* why = nullptr);

    // Nets.
    std::string net_open(const std::string& name, const std::vector<std::string>& roster, bool exercise,
                         const std::string& ncs, const std::string& note, std::string* error);
    bool net_checkin(const std::string& net, const std::string& station, const std::string& via, int traffic,
                     const std::string& remark, std::string* error);
    bool net_checkout(const std::string& net, const std::string& station, std::string* error);
    bool net_close(const std::string& net, std::string* error);
    std::optional<Net> net_get(const std::string& net);
    std::vector<Net> net_list(int limit);
    // The open net, when exactly one is open; "" otherwise.
    std::string current_net();
    // The core heard a station (a message or an ACK from it): every open net
    // records it as checked in by radio, once.
    void heard(const std::string& station);

    // Net schedules.
    bool sched_put(NetSchedule s, std::string* error);
    bool sched_remove(const std::string& name);
    std::optional<NetSchedule> sched_get(const std::string& name);
    std::vector<NetSchedule> sched_list();
    // The sitting that matters now for one schedule: the one under way when
    // it hasn't been closed, else the next.
    std::optional<NetSitting> sched_sitting(const std::string& name);
    // Every schedule's sitting, soonest first.
    std::vector<NetSitting> sched_upcoming(int limit);
    // Opens the scheduled net now with its roster, net control and exercise
    // mark. Opened within 30 minutes of a sitting's start (or during it), it
    // counts as that sitting.
    std::string sched_open(const std::string& name, std::string* error);
    // Called by the core every half minute: opens due nets set to open
    // themselves, and returns what to tell the operator (each once): a net
    // that starts in 15 minutes, one that is due and not opened.
    std::vector<std::string> sched_tick();

    // Operator duty and turnover.
    bool duty_on(const std::string& op, const std::string& note, std::string* error);
    bool duty_off(const std::string& turnover, std::string* error);
    std::optional<Duty> duty_current();
    std::vector<Duty> duty_list(int64_t from, int64_t to);

    // Station log.
    int64_t log_add(const std::string& kind, const std::string& text, const std::string& net = "",
                    bool exercise = false);
    std::vector<LogEntry> log_list(int64_t from, int64_t to, int limit);

    // The month's report. Reads the message store for traffic counts.
    bool monthly(const std::string& month, MonthlyReport* out, std::string* error);

private:
    void exec(const char* sql);
    std::optional<Net> load_net(const std::string& id);
    std::optional<NetSitting> sitting_of(const NetSchedule& s, int64_t now);
    std::map<std::string, int64_t> told_;  // schedule name + what was said -> sitting start
    std::string canonical(const std::string& station);
    std::string station_;
    std::function<int64_t()> clock_;
    std::unique_ptr<ChannelPlan> channels_;
    std::recursive_mutex mu_;
    sqlite3* db_ = nullptr;
};

}  // namespace pna
