// The keying calls (gale_key.h): key_prepare, standing_prepare, key_confirm,
// key_cancel, standing_off and key_status. See docs/api.md, "Keying: the core
// asks gale-key".
#pragma once

#include <map>
#include <optional>
#include <string>

namespace pna {
class Core;
class JsonWriter;

// The reply for a keying call, or nullopt when `cmd` isn't one.
std::optional<std::string> handle_key(Core& core, const std::string& cmd, std::map<std::string, std::string>& req);

// The "keying" block of status: whether gale-key is set up and answering,
// and why not; the standing modes on; the y dialogs held.
void write_keying(JsonWriter& w, Core& core);
}  // namespace pna
