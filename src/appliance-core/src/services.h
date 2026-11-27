// The service tables, read by the core itself (Network management; greybeard-ale
// docs/services.md, "The loader", rule 7; docs/api.md, "Services").
//
// The station works one or more services. Each is one network, named by a
// table file DIR/<service>.json (default /etc/greybeard-ale/services). The core
// reads the directory once at start, under the loader's contract:
//
//  - a file is refused whole when it is not a regular file, is writable by
//    anyone but its owner, is not root's (unless the caller lifts that for a
//    test stand), or fails the format: version 1, `service` equal to the file's
//    stem, a name of 1 to 16 of a-z, 0-9 and -, starting with a letter and never
//    "both", every column written out, no unknown key, each value of its type.
//    The other files still load.
//  - a service is valid when it has a table. A name with none is refused with
//    "no table for service `NAME`", never read as MARS or any default.
//  - nothing here logs a range or a frequency.
//
// The core reads only the columns it uses (identity, messages, and whether an
// ID goes out); it checks the rest only for their shape.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pna {

struct Service {
    std::string name;  // the table's `service`
    std::string display_name;
    bool worked = false;
    bool transmit = false;
    std::vector<std::string> formats;          // messages.formats: procedure ids
    bool encryption = false;                   // messages.encryption
    std::vector<std::string> automatic_modes;  // messages.automatic_modes
    bool station_id = false;                   // keying.station_id is set: an ID goes out
    std::string identity_file;                 // keying.identity_file, "" for null
    std::string call_pattern;                  // keying.call_pattern, "" for null
    bool listen_only_without_call = false;     // keying.listen_only_without_call
    bool offers(const std::string& format) const;
};

class Services {
public:
    struct Options {
        bool require_root = true;  // a test stand's fixture tables are not root's
    };
    // Reads every *.json in dir. Each refused file adds one line to *refused
    // (the file's name and why; never a value).
    static std::shared_ptr<Services> load(const std::string& dir, const Options& opt,
                                          std::vector<std::string>* refused);
    // From texts already read (tests): file stem -> text.
    static std::shared_ptr<Services> from_texts(const std::map<std::string, std::string>& files,
                                                std::vector<std::string>* refused);

    const Service* find(const std::string& name) const;
    bool valid(const std::string& name) const { return find(name) != nullptr; }
    std::vector<const Service*> all() const;  // by name
    const std::string& dir() const { return dir_; }
    // The root check was lifted (--services-unlocked, or tables given as
    // texts): a test stand's tables, never a station's.
    bool unlocked() const { return unlocked_; }

    // Whether a service needs a call and has none: listen_only_without_call
    // and no line of identity_file (under local_dir) matches call_pattern.
    bool listen_only(const Service& s, const std::string& local_dir) const;

private:
    std::string dir_;
    bool unlocked_ = false;
    std::map<std::string, Service> by_name_;
};

// "no table for service `NAME`" (an empty name: "no service named").
std::string no_table(const std::string& name);
// A service name's shape: 1 to 16 of a-z, 0-9 and -, a letter first, not "both".
bool valid_service_name(const std::string& name);
// Parses and checks one table's text. False with *why (no value in it).
bool parse_service_table(const std::string& stem, const std::string& text, Service& out, std::string* why);

}  // namespace pna
