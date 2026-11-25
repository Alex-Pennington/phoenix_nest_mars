// The BBS calls on a running station (bbs.h): bbs_get, bbs_set, guard_*,
// alias_* and route_*. See docs/api.md, "BBS".
#pragma once

#include <map>
#include <optional>
#include <string>

namespace pna {
class Core;

// The reply for a BBS call, or nullopt when `cmd` isn't one.
std::optional<std::string> handle_bbs(Core& core, const std::string& cmd, std::map<std::string, std::string>& req);
}  // namespace pna
