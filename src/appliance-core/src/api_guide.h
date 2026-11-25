// The guide's calls on a running station (guide.h, docs/guide.md):
// guide_list, guide_get, guide_help, guide_start and the guide_page_* calls.
#pragma once

#include <map>
#include <optional>
#include <string>

namespace pna {
class Core;

// This station's own copies of guide pages: DB_DIR/CALL.guide.
std::string guide_dir(const Core& core);

std::optional<std::string> handle_guide(Core& core, const std::string& cmd, std::map<std::string, std::string>& req,
                                        bool training = false);
}  // namespace pna
