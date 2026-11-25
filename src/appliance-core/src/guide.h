// The operator guide, built into the box from day one. gale-term shows it
// with one key (?) on any screen, by that screen's id, and walks a new member
// from first start to the first message sent.
//
// Like the forms (forms.h), the guide is made of plain-text pages that can be
// changed on the box without a rebuild. A page reads:
//
//   ; The In Box.
//   PAGE MSG-INBOX
//   TITLE In Box
//   KIND screen
//   SCREENS MSG-INBOX
//   HELP Enter open  k QSL  x processed  e export  ? guide
//   KEY k QSL the message: tells the sender you have it.
//   KEY x Mark it processed: you have acted on it.
//   SEE MSG-VIEW MSG-PENDING
//   BEGIN_TEXT
//   Every message that came in, newest first. ...
//   END
//   BEGIN_EXAMPLE
//   A PRIORITY message from NCS-A arrives at 1412Z. ...
//   END
//
// Two kinds of page aren't written as pages: they are made from the
// templates they explain, so a changed template changes its page at once.
//   FORM-<ID>   one per form (forms/*.form): what it does, each field and its
//               help, what comes next. Screen id FORM-<ID>.
//   TYPE-<ID>   one per message type (compose templates): what it is for,
//               the procedures it goes in, each field and its hint.
//
// Written pages are KIND start (the first-start walkthrough), screen (one
// per gale-term screen) or topic. Lines are at most 78 characters, so a page
// fits 80 columns; a page never holds a frequency.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pna {
namespace forms {
class Library;
}
namespace guide {

struct Key {
    std::string key, what;
};

struct Page {
    std::string id, title, kind;      // start, screen, topic; form, type (made)
    std::vector<std::string> screens;  // the screen ids it answers ? on
    std::string help;                 // the screen's help line (line 24)
    std::vector<Key> keys;
    std::string text, example;
    std::vector<std::string> see;     // other pages
    std::string source;               // made pages: the template it came from
    std::string written;              // written pages: the page as written
};

constexpr size_t kWidth = 78;     // longest line
constexpr size_t kMaxPage = 16384;  // longest page, as written

// Reads a written page. False with *error naming the line.
bool parse(const std::string& text, Page& out, std::string* error);

// The pages built into this program (guide/*.page in the source tree).
const std::vector<std::pair<std::string, std::string>>& builtin_texts();

// The built-in pages, overlaid by pages kept in `dir` (changed on the box).
class Library {
public:
    explicit Library(std::string dir) : dir_(std::move(dir)) {}
    struct Entry {
        Page p;
        std::string origin;  // "built in", "edited here"
        bool builtin = false, changed = false;
    };
    std::vector<Entry> list() const;
    std::optional<Entry> get(const std::string& id) const;
    // Checks and keeps a page. result: added, replaced or same.
    bool put(const std::string& text, std::string* result, std::string* id, std::string* error);
    // Back to the built-in. False when there was no local copy.
    bool reset(const std::string& id);
    const std::string& dir() const { return dir_; }

private:
    std::string dir_;
};

// One step of the first-start walkthrough, as the station stands now.
struct Step {
    std::string id, title, form, screen, how;
    bool done = false;
};

// What the guide needs from where it runs: a running station, or setup mode
// (no station yet: built-in pages and forms only, every step after SETUP
// still to do).
struct Context {
    Library* pages = nullptr;
    forms::Library* forms = nullptr;
    // Message types: (id, title, summary, procedures) and their fields
    // (label, hint, example), from compose. Empty in setup mode.
    std::function<std::vector<std::map<std::string, std::string>>()> types;
    std::function<std::vector<std::map<std::string, std::string>>(const std::string& type)> type_fields;
    // The walkthrough's steps, done or not.
    std::function<std::vector<Step>()> steps;
    bool training = false;  // say so on every page
};

// Handles guide_list, guide_get, guide_help, guide_start, guide_page_get,
// guide_page_put and guide_page_reset. nullopt for any other cmd.
std::optional<std::string> handle(const Context& ctx, const std::string& cmd, std::map<std::string, std::string>& req);

// The walkthrough's steps with nothing known about the station: only setup.
std::vector<Step> setup_steps(bool call_set);

}  // namespace guide
}  // namespace pna
