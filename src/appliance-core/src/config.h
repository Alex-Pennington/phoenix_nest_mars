// The appliance's config file: one setting a line, "key = value", where the
// keys are appliance-core's long options without the dashes (station,
// tx-mode, captures, db-dir, api-port...). '#' starts a comment. A value may be
// put in double quotes to keep spaces; a leading ~/ is the home directory.
// tx-arg and advisor-arg may be given more than once. no-acks and link-gate
// take yes or no.
//
// read_config() turns the file into the same arguments the command line
// takes, so the command line can follow it and win.
#pragma once

#include <string>
#include <vector>

namespace pna {

// The keys the file may hold.
const std::vector<std::string>& config_keys();

// Appends "--key" (and its value) for each setting. False with *error, which
// names the line, on an unknown key, a missing value or a bad yes/no.
bool read_config_text(const std::string& text, std::vector<std::string>* args, std::string* error,
                      const std::string& home = "");
bool read_config(const std::string& path, std::vector<std::string>* args, std::string* error);

}  // namespace pna
