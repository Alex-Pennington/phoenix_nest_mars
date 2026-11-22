// The ACP 126 forms MARS stations send: the radiotelegraph call-up
// "CALL DE STATION NR nnn" inside the ACP 127 envelope.
//
// No public ACP 126 edition could be read, so this is drawn from the MARS
// traffic samples (tests/fixtures/mars-samples 43, 44 and 45), not from an
// edition, and should be checked against one when it can be:
//
//   VZCZCMMM999
//   IRONCRANE DE AAR4WS NR 051        called stations, DE, station, NR, serial
//   R 192109Z FEB 2025                optional: precedence letter and DTG
//   FM AMARS STATION AAR4WS           optional: FM and TO, as in ACP 127
//   TO UHRRRWN/IRONCRANE
//   BT
//   UNCLAS                            optional (sample 44, a net roster, has none)
//   ...text...
//   BT
//   (8 LF)
//   NNNN
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
    bool with_dtg = false;            // write the precedence letter and DTG line
    char prec = 'R';
    acp127::Dtg dtg;
    acp127::Address from;             // empty omits FM
    std::vector<acp127::Address> to;  // empty omits TO
    bool unclas_line = true;          // start the text with UNCLAS
    std::string text;                 // '\n' starts a new line; wrapped at 69
};

// Builds the whole message, VZCZC to NNNN. Returns "" and sets *error when a
// field breaks the form.
std::string build(const Message& m, std::string* error);

}  // namespace acp126
}  // namespace pna
