// Forms from templates: everything a member enters on a box with no shell
// (the call, the station book, the frequency card, schedules, bundles,
// keymasters, tapes, backups) goes in through a form, and every form is a
// plain-text template that can be changed on the box, or carried signed in a
// startup bundle, without a rebuild. docs/forms.md has the design and the
// template language.
//
// The idea is MSC's. Response Collector keeps each of its forms as a table
// template: a name and an ordered list of "LABEL(mask)" columns, where the
// mask both says what goes in and checks it (X free text, AA two letters,
// ###.## a number, DDHHMM a time, a comma list a choice, Y,N yes or no).
// Message Creator adds labelled help blocks and fields filled from the
// station (FETCH). A template here is the same idea in a file of its own:
//
//   ; Channel and frequency, entered by hand.
//   FORM CHANNEL
//   TITLE Channel and frequency
//   SUMMARY Enter one channel of your frequency card.
//   LEVEL ZERO
//   ORDER 30
//   ACTION channel_put
//   PREFILL channel name
//   FIELD name Designator(NNNNNNNNNNNN) required upper
//   FIELD frequency Frequency(X)kHz required secret check=frequency
//   FIELD mode Mode(USB,LSB,ALE,DATA,AM,FM,CW) default=USB
//   NEXT CHANNEL SCHEDULE
//   BEGIN_HELP
//   Designator:
//   The name everyone uses for the channel, as on the card: P1, S1.
//   END
//
// A form runs one core call (its ACTION) with the fields as that call's
// parameters, so a form can do nothing a member couldn't do by hand, and
// only the calls in actions() can be named.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pna {
namespace forms {

struct Choice {
    std::string value, label;
};

struct Field {
    std::string key;     // the action's parameter
    std::string label;   // what the member sees
    std::string mask;    // as written in the template
    std::string suffix;  // shown after the box ("kHz", "Z")
    // text, textblock, choice, yesno, time, pattern
    std::string kind = "text";
    std::vector<Choice> choices;  // a fixed list (choice, yesno)
    std::string source;           // "@channels": choices from the station
    bool multi = false;           // "@cards+": more than one, joined by spaces
    bool required = false, upper = false, secret = false, send = true;
    bool fill = false;  // a blank answer takes the default, not "leave as is"
    std::string def;    // default
    std::string check;  // a named check: station, designator, frequency, ...
    std::string again;  // must equal this other field (a passphrase twice)
    std::string as;     // send under this parameter name instead of key
    int max = 0;        // longest value; 0: no limit but the mask's
    std::string help;   // from the help block with this label
};

struct Action {
    std::string cmd, when_key, when_value;  // when_key empty: always
};

struct Template {
    std::string id, title, summary;
    bool level_zero = false;
    int order = 1000;
    std::vector<Action> actions;
    std::vector<std::pair<std::string, std::string>> sets;  // fixed values, shown to the member
    std::string prefill_kind, prefill_key;                  // fill from what the station holds
    std::vector<Field> fields;
    std::vector<std::string> next;  // forms to offer after this one
    std::string text;               // the template as written
};

// The core calls a form may run.
const std::vector<std::string>& actions();
// The live choice sources a field may name.
const std::vector<std::string>& sources();

// Reads a template. False with *error naming the line.
bool parse(const std::string& text, Template& out, std::string* error);

// The templates built into this program (forms/*.form in the source tree).
const std::vector<std::pair<std::string, std::string>>& builtin_texts();

// The built-ins, overlaid by templates kept in `dir` (edited on the box, or
// from a bundle). The folder is read on every call, so a new or changed
// template is in use at once.
class Library {
public:
    explicit Library(std::string dir) : dir_(std::move(dir)) {}
    struct Entry {
        Template t;
        std::string origin;  // "built in", "edited here", "from bundle NCS-A"
        bool builtin = false;   // a built-in of that id exists
        bool changed = false;   // a local copy replaces or adds to the built-ins
    };
    std::vector<Entry> list() const;
    std::optional<Entry> get(const std::string& id) const;
    // Checks and keeps a template. result: added, replaced or same.
    bool put(const std::string& text, const std::string& origin, std::string* result, std::string* id,
             std::string* error);
    // Drops the local copy: back to the built-in (or gone, for one that
    // isn't built in). False when there was no local copy.
    bool reset(const std::string& id);
    // The templates that differ from the built-ins, as (id, text), for a bundle.
    std::vector<std::pair<std::string, std::string>> changed() const;
    std::string origin_of(const std::string& id) const;
    const std::string& dir() const { return dir_; }

private:
    std::string dir_;
};

// What the forms need from the station they run on.
struct Context {
    // Choices for a source such as "@channels".
    std::function<std::vector<Choice>(const std::string& source)> choices;
    // Values to start a form with: kind (book, channel, schedule, template)
    // and the key's value. Never a secret.
    std::function<std::map<std::string, std::string>(const std::string& kind, const std::string& key)> prefill;
    // Runs one core call (a flat JSON object) and returns its reply.
    std::function<std::string(const std::string& request)> run;
    // Only these form ids (setup mode); empty: all.
    std::vector<std::string> only;
    // No local templates (setup mode): built-ins only.
    bool builtin_only = false;
};

// Handles form_list, form_get, form_check, form_submit, form_template_get,
// form_template_put and form_template_reset. nullopt for any other cmd.
std::optional<std::string> handle(Library& lib, const Context& ctx, const std::string& cmd,
                                  std::map<std::string, std::string>& req);

// For bundles: a template as radio-safe lines of at most 69 characters
// (compressed, base32), and back.
std::vector<std::string> pack(const std::string& text);
bool unpack(const std::vector<std::string>& lines, std::string& text);

}  // namespace forms
}  // namespace pna
