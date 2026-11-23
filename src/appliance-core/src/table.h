// Reading a table from a file someone hands the appliance: a spreadsheet
// (.xlsx), comma-, tab-, semicolon- or bar-separated text, or a text table
// whose columns are lined up with spaces. Every cell comes back as text,
// trimmed. Nothing here knows what the table means: mars_import.h does that.
#pragma once

#include <string>
#include <vector>

namespace pna {

using Row = std::vector<std::string>;

struct Table {
    std::string sheet;          // the sheet's name, for a spreadsheet
    std::vector<Row> rows;
};

// Every sheet of an .xlsx, or the one table in a text file. False with *error
// when the bytes can't be read as either.
bool read_tables(const std::string& bytes, std::vector<Table>& out, std::string* error);

// Standard base64 (RFC 4648), padding optional. False on a bad character.
bool base64_decode(const std::string& in, std::string& out);

}  // namespace pna
