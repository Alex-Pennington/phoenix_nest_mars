// First start: a box taken out of the can has no station call yet. Rather
// than have the member edit a settings file, appliance-core answers on its
// API port in setup mode until it is told the call:
//
//   {"cmd":"setup_status"}                     what the box needs
//   {"cmd":"setup_station","call":"STA-B"}     sets the call, makes the station
//                                              keys and card, then the station starts
//
// The call is kept in DB_DIR/station-call, written by the core. A station
// given in the settings file or on the command line wins over it, so the
// settings-file route keeps working, and setup never writes the settings
// file: it can't touch tx-cmd, tx-arg, advisor, link or anything else there.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "station.h"

namespace pna {
namespace first_start {

// Where the call set at first start is kept.
std::string call_file(const std::string& db_dir);
// The call set at first start, if there is one.
bool saved_call(const std::string& db_dir, std::string& call);

// Handles one setup-mode request line and returns the reply (no newline).
// On setup_station it checks the call, makes and saves the station keys at
// DB_DIR/CALL.id (where the station looks for them), then writes the call
// file; *call_set gets the call.
std::string handle(const std::string& db_dir, int64_t now, const std::string& line, std::string* call_set);

// The station keys for CALL at DB_DIR/CALL.id: read if there, made if not
// (*made). False with *err when they can't be read or made, or are another
// call's.
bool station_keys(const std::string& db_dir, const std::string& call, int64_t now, station::Identity& id, bool* made,
                  std::string* err);

// Serves setup mode on 127.0.0.1:port until a call is set (returned) or
// *stop turns true (returns ""). False in *opened when the port can't open.
std::string serve(const std::string& db_dir, uint16_t port, const std::atomic<bool>* stop, bool* opened);

}  // namespace first_start
}  // namespace pna
