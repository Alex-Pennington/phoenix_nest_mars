// Record classes (phoenix_nest_mars#3, ask 26).
//
// The station's records (the channel plan, the net schedule, the book, the
// key tape, the cipher slot and the rest) each belong to one kind, and each
// kind has a class: operator (the default: the kiosk changes it) or locked
// (the core refuses every call that would change it). A kind is locked only
// by the host, with a `lock = KIND` line in appliance.conf (or --lock KIND),
// never from the kiosk.
//
// The check is made once, in ApiServer::handle_call, before any handler
// runs. form_submit runs its action through handle_call too, so a form
// can't get round a lock. A refusal is logged.
#pragma once

#include <set>
#include <string>
#include <vector>

namespace pna {

class JsonWriter;

namespace records {

struct Kind {
    std::string key;                // channels, schedule, book, key-tape ...
    std::string label;              // as the kiosk shows it
    std::vector<std::string> calls;  // the calls that change it
};

// The compiled table, in the kiosk's order.
const std::vector<Kind>& kinds();
// The kind for a key ("key_tape" is read as "key-tape"), or nullptr.
const Kind* find(const std::string& key);

// Set once at start, from main(): the kinds the host locked. False with
// *error for a key that isn't a kind.
bool set_locked(const std::vector<std::string>& keys, std::string* error);
std::set<std::string> locked();

// Why `cmd` may not run ("" when it may): it changes a locked kind. The
// first locked kind it touches is put in *kind.
std::string refusal(const std::string& cmd, std::string* kind = nullptr);

// {"records":[{kind, label, class, calls[]}]}'s array, for settings_list.
void write(JsonWriter& w);

}  // namespace records
}  // namespace pna
