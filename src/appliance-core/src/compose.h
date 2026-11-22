// Composing a message from a template, with guidance a first-time operator
// can follow.
//
// A message is a message template (what it says: GENADMIN, AKNLDG, a station
// status strip...) inside a procedure template (how it is addressed: ACP 127
// plaindress, the MSC 16L forms, ACP 126, chat). Both lists are the ones the
// MARS traffic samples show (their NOTES.md and SOURCES.csv), so every message
// composed here has the shape of real traffic; received::procedure() names it
// back the same way.
//
// The front end asks for a form (the fields of one template in one
// procedure, with defaults), sends the operator's values to check(), and
// shows the issues it returns: each says in plain words what is wrong, how to
// fix it, and gives an example. When there are no errors, check() returns the
// whole message as it will be sent. Nothing here talks to a modem or a screen.
//
// An Advisor (an AI helper or anything else) can be plugged in to explain a
// field or suggest wording. None is needed: the built-in guidance is complete
// without one, and nothing here calls an outside service.
#pragma once

#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pna {
namespace compose {

// Field kinds, so a front end can pick an input and the checks know the rules.
//   text      one line          lines     several lines (each kept as a line)
//   call      a call sign       stations  addressees, space separated
//   address   RI/PLAIN NAME     addresses one address per line
//   dtg       DDHHMMZ           ddhhmm    DDHHMM (status strips)
//   dtg_or_dash  DDHHMMZ or -
//   yn        Y or N            number    digits
//   choice    one of choices    prec      Z, O, P or R
//   strips    one strip per line, never broken     value  one strip field (no '/')
//   datetime  YYYYMMDDHHMMZ     state     two-letter state
//   metars    one METAR report per line            groups four-character code groups
struct Field {
    std::string key;
    std::string label;      // what the operator sees
    std::string kind;
    bool required = false;
    std::string def;        // default, already filled in for this station and time
    std::string hint;       // one or two sentences: what goes here and why
    std::string example;
    std::vector<std::string> choices;
};

struct Procedure {
    std::string id;         // as SOURCES.csv names it: "16L_P2P_ABBR"
    std::string title;
    std::string summary;
    bool available = true;  // false: shown, but not sendable here
    std::string why_not;    // when not available
};

struct Template {
    std::string id;         // as SOURCES.csv names the form: "GENADMIN_SIMPLE"
    std::string title;
    std::string summary;
    std::string procedure;  // the one the samples use most for it
    char prec = 'R';        // the precedence the samples use for it
    std::vector<std::string> procedures;  // the ones it may go in
};

const std::vector<Procedure>& procedures();
const std::vector<Template>& templates();
const Procedure* find_procedure(const std::string& id);
const Template* find_template(const std::string& id);

// What the defaults are filled from.
struct Context {
    std::string station;    // this station's call (the core's station name)
    std::time_t now = 0;    // UTC; 0 means the clock
};

// The fields of a template in a procedure (envelope first, then content).
// Empty with *error when the template or procedure is unknown or they don't
// go together. procedure may be empty for the template's usual one.
std::vector<Field> form(const std::string& template_id, const std::string& procedure, const Context& ctx,
                        std::string* error);

// The operator's request: template, procedure and field values by key. A key
// that is absent takes its default; a key present but empty is empty.
struct Draft {
    std::string template_id;
    std::string procedure;
    std::map<std::string, std::string> values;
};

struct Issue {
    std::string field;      // key, or empty for the message as a whole
    std::string level;      // "error" (must fix), "warning" (should look), "tip"
    std::string text;       // what is wrong, in plain words
    std::string fix;        // what to do about it
    std::string example;    // a value that would be right, when there is one
};

// Serial numbers are taken only when a message is sent; check() shows them
// as these.
struct Serials {
    int channel_serial = 1;   // VZCZCMMM001
    int station_serial = 1;   // #0001 and the MSGID serial (kept the same)
    int nr = 1;               // ACP 126 NR
};

struct Result {
    bool ready = false;              // no errors: message holds the whole message
    std::vector<Issue> issues;
    std::string procedure;           // the procedure used
    char prec = 'R';                 // for the queue
    std::string to_station;          // over the air
    std::string message;             // VZCZC to NNNN, or the chat line
    // The core counters the serials come from, so the sender can take them:
    std::string channel_counter;     // "acp127.channel.MMM" (wraps at 999); "" for bare VZCZC or chat
    std::string serial_counter;      // "acp127.station.<orig>" (9999) or "acp126.nr.<station>" (999); "" for chat
    int serial_wrap = 9999;
};

// Checks a draft and, when it has no errors, builds it with the given
// serials. Never throws; every problem comes back as an issue.
Result check(const Draft& d, const Context& ctx, const Serials& serials = Serials());

// Optional helper for explanations and suggestions. advise() fills text and
// returns true, or returns false with *error.
struct AdviceRequest {
    std::string kind;        // "explain" (a field or an issue) or "suggest" (wording)
    std::string field;       // may be empty
    std::string question;    // the operator's own words, may be empty
    Draft draft;
    std::vector<Issue> issues;  // what check() said about the draft
};

class Advisor {
public:
    virtual ~Advisor() = default;
    virtual std::string name() const = 0;
    virtual bool advise(const AdviceRequest& req, std::string& text, std::string* error) = 0;
};

// The advisor in use, or none. Thread-safe.
void set_advisor(std::shared_ptr<Advisor> a);
std::shared_ptr<Advisor> advisor();

// Runs a local program for each request: the program gets the request as one
// JSON argument and prints the answer (plain text, or {"text":"..."}). It is
// given 10 seconds. What the program does is up to it; the core sends it
// nothing else and needs no key.
std::shared_ptr<Advisor> program_advisor(const std::vector<std::string>& argv);

// The request an Advisor gets, as JSON (what program_advisor passes).
std::string advice_json(const AdviceRequest& req);

// The built-in explanation, used when there is no advisor: the field's hint
// and example, or the template's summary, and the draft's issues.
std::string builtin_advice(const AdviceRequest& req, const Context& ctx);

}  // namespace compose
}  // namespace pna
