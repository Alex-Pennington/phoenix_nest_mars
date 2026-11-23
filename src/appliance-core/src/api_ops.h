// Local API calls for the station book (ops.h): address book, nets, duty,
// station log and the monthly report. See docs/api.md.
#pragma once

#include <map>
#include <optional>
#include <string>

namespace pna {

class Core;

// The reply for an ops call, or nullopt when `cmd` isn't one.
std::optional<std::string> handle_ops(Core& core, const std::string& cmd, std::map<std::string, std::string>& req);

}  // namespace pna
