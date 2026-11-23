// Importing the files a MARS station is handed: member rosters, address
// books, frequency cards and net schedules, as spreadsheets (.xlsx) or text
// tables (docs/formats.md says what each should look like). Each lands in
// the station's own model:
//
//   roster, address book -> the station book (ops.h): stations and
//                           collectives. Personal details are left out.
//   frequency card       -> the channel plan (channels.h). Frequencies stay
//                           in its file; replies name designators only.
//   net schedule         -> net schedules (ops.h), by channel designator.
//
// Key, cipher and passphrase files are not read: our cipher is our own.
#pragma once

#include "table.h"

#include <string>
#include <vector>

namespace pna {

class Ops;

namespace marsio {

// What a sheet turned out to be; "" when it isn't one of these.
//   roster, book, freq, schedule
struct SheetResult {
    std::string sheet;                         // the sheet's name, for a spreadsheet
    std::string kind;
    int header_row = 0;                        // 1-based
    int rows = 0;                              // data rows read
    int imported = 0;
    std::vector<std::string> names;            // book names, designators or net names, as stored
    std::vector<std::string> left_out;         // columns not imported (personal details and the like)
    std::vector<std::string> problems;         // "row 12: ...", never repeating a frequency
    int frequencies_withheld = 0;              // schedule rows that gave a frequency, not a designator
};

// kind: "auto" (the default) or one of the four, applied to every sheet.
// source: what the book and plan record as where entries came from.
std::vector<SheetResult> import_file(Ops& ops, const std::string& bytes, const std::string& kind,
                                     const std::string& source, std::string* error);

// A day string as schedules write it, to ops.h's form: "Mon-Fri" -> WEEKDAYS,
// "Tue/Thu" -> TUE,THU, "1st & 3rd Sat" -> 1SAT,3SAT, "Last Sunday" -> LSUN.
// "" when it can't.
std::string normal_days(const std::string& s);

// "1900Z", "19:00 UTC", "1900" -> minutes after 0000Z. -1 for a time in a
// local zone (ET, CST, local ...) or one it can't read.
int utc_minutes(const std::string& s);

}  // namespace marsio
}  // namespace pna
