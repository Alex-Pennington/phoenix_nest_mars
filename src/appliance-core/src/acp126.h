// The ACP 126 forms MARS stations send: the radiotelegraph call-up
// "CALL DE STATION NR nnn" inside the ACP 127 envelope.
//
// No public ACP 126 edition could be read, so this is drawn from the MARS
// traffic samples (tests/fixtures/mars-samples 43, 44 and 45), not from an
// edition, and should be checked against one when it can be. The samples'
// notes name two MSC templates, and this builds exactly those two:
//
//   PLAINDRESS (sample 43)                SIMPLE (samples 44, 45)
//   VZCZCMMM999                           VZCZC
//   IRONCRANE DE AAR4WS NR 051            NET DE AAR4WS NR 175
//   R 192109Z FEB 2025                    BT
//   FM AMARS STATION AAR4WS               ...text...
//   TO UHRRRWN/IRONCRANE                  BT
//   BT                                    (8 LF)
//   UNCLAS                                NNNN
//   ...text...
//   BT
//   (8 LF)
//   NNNN
//
// UNCLAS is optional in both (sample 44, a net roster, has none). MSC sent
// sample 45 as "ALL DE NR 001" when the station's own call was not set; this
// refuses an empty calling station instead.
//
// The serial is three digits (051, 175, 001). Line ends, the 69-character
// limit and the character set follow acp127.h. received::parse() reads it.
#pragma once

#include "acp127.h"

#include <string>
#include <vector>

namespace pna {
namespace acp126 {

constexpr const char* kSource =
    "MARS traffic samples (call-up form of samples 43, 44, 45); no ACP 126 edition was available";

struct Message {
    std::string channel;              // three letters after VZCZC; empty for bare VZCZC
    int channel_serial = 1;           // 001..999
    std::vector<std::string> calls;   // called stations: call signs, tactical calls, NET, ALL
    std::string station;              // calling station after DE; may not be empty here
    int serial = 1;                   // NR, 1..999
    enum class Procedure { Simple, Plaindress };
    Procedure procedure = Procedure::Simple;
    char prec = 'R';                  // PLAINDRESS: the DTG line's precedence
    acp127::Dtg dtg;                  // PLAINDRESS only
    acp127::Address from;             // PLAINDRESS only, required
    std::vector<acp127::Address> to;  // PLAINDRESS only, at least one
    bool unclas_line = true;          // start the text with UNCLAS
    std::string text;                 // '\n' starts a new line
    bool limit_lines = true;          // wrap at 69 and refuse longer lines (see acp127.h)
};

// Builds the whole message, VZCZC to NNNN. Returns "" and sets *error when a
// field breaks the form, including FM, TO or a DTG asked of SIMPLE, or any
// of them missing from PLAINDRESS.
std::string build(const Message& m, std::string* error);

// "ACP126_PLAINDRESS" or "ACP126_SIMPLE", as SOURCES.csv names them.
const char* procedure_name(Message::Procedure p);
// Reads "plaindress" or "simple" (any case). False for anything else.
bool procedure_from(const std::string& s, Message::Procedure& p);

}  // namespace acp126
}  // namespace pna
