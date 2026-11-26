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

// The radio's current channel, as `key`: {"label","world","since","source"},
// each null when nothing has been set since start. Never a frequency. In
// status as "radio_channel", in key_status's station as "channel".
void write_radio_channel(JsonWriter& w, const char* key, Core& core);
}  // namespace pna
