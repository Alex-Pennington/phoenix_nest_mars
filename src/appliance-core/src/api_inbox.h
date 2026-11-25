// The in-box calls behind the Message Machine screens MSG-INBOX, MSG-PENDING,
// MSG-VIEW, MSG-SEARCH, MSG-ACTIVITY and MSG-REPORTS
// (docs/gale-term-mm-bbs-screens.md, section 3; docs/api.md, "In box").
//
//   list {"dir":"in",...}   the in-box, with each message's in-box state
//   get                     adds the in-box state, events, props, decoded text
//                           (write_inbox_extras, called by get)
//   pending, mark, mark_all, qsl, export, retransmit, tag, correct, search,
//   log {"stream":...}, activity_counters, report
//
// The operator's marks (read, processed, discarded, QSL, tag, a corrected
// copy) live in the station's own database in inbox_flags, beside the
// messages; the message as received is never changed.
//
// Principle 5: nothing here keys the radio by itself. retransmit needs
// confirm=Y, and qsl sends a receipt only with send=Y; both go into the
// ordinary queue, so EMCON holds them and they show in the outbox.
#pragma once

#include <map>
#include <optional>
#include <string>

namespace pna {
class Core;
class JsonWriter;
struct Message;

// get's extra members for one message: "inbox" (its in-box state), "events",
// "props", "decoded" and "corrected". read: the operator opened it (MSG-VIEW),
// so it is marked read.
void write_inbox_extras(JsonWriter& w, Core& core, const Message& m, bool read);

// The reply for an in-box call, or nullopt when `cmd` isn't one (list
// without dir, get and log without stream stay with the plain calls).
std::optional<std::string> handle_inbox(Core& core, const std::string& cmd, std::map<std::string, std::string>& req);
}  // namespace pna
