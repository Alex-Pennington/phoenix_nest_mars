// Reading traffic as MARS stations actually send it.
//
// acp127::parse() reads ACP 127(G) to the letter, which is what this
// appliance sends. Traffic from other stations departs from the edition in
// ways the MARS traffic samples show (tests/fixtures/mars-samples), and this
// reader takes all of them. It is drawn from those samples, not from an ACP
// edition:
//
//  - Text before VZCZC (a receive time like "2035Z", an ALE link tag, noise)
//    and after NNNN is ignored. Lines may end CRLF or LF, carry trailing
//    spaces, or all be indented.
//  - Format line 1 is VZCZC, with or without the channel and serial.
//  - Three header shapes:
//      Acp127        "PP RI RI" then "DE RI #0038 3102053" (HQ traffic)
//      Acp127Folded  "PP RI RI DE RI #0038 3102053" on one line (region and
//                    station traffic); the time filed may spill to the next line
//      Acp126        "CALL DE STATION NR 051", the radiotelegraph call-up with
//                    the ACP 127 envelope around it (samples 43, 44, 45). The
//                    samples hold no other ACP 126 form.
//  - The precedence prosign may be missing, the originator may be empty
//    ("DE #0547", "ALL DE NR 001"), and call signs, tactical calls or
//    lower-case RIs may stand where RIs go.
//  - Format line 4 (ZNR UUUUU, ZNY EEEEE, a ZEK line), the DTG and FM/TO/INFO/
//    XMT are all optional; a further addressee of one kind is on a line of its
//    own; an address may be "RI/PLAIN", "RI PLAIN" or plain alone.
//  - The text may or may not start with an UNCLAS or UNCLAS SVC line. Page
//    headers ("PAGE 2 RI #0129 UNCLAS") are dropped.
//  - After the closing BT: the station serial again (#0212), an MSC
//    [DIGEST:...] line, or a correction line.
//
// Anything outside the edition that was taken anyway is listed in warnings.
// Only unclassified traffic is accepted.
#pragma once

#include "acp127.h"

#include <string>
#include <vector>

namespace pna {
namespace received {

enum class Form { Acp127, Acp127Folded, Acp126 };
const char* form_name(Form f);

struct Message {
    Form form = Form::Acp127;
    // Format line 1.
    std::string channel;
    int channel_serial = 0;          // 0 when bare VZCZC
    // Header.
    char prec = 0;                   // Z O P R, or 0 when no prosign was sent
    std::vector<std::string> routing;  // FL2 addressees, or the called stations (Acp126)
    std::string originator;          // after DE; may be empty
    std::string serial;              // after '#' or NR, digits as sent
    std::string time_filed;          // "3102053" (Julian DDDHHMM) or "02/0009Z"; may be empty
    std::string security;            // "ZNR UUUUU", "ZNY EEEEE" or empty
    bool zek = false;
    bool has_dtg = false;
    acp127::Dtg dtg;
    acp127::Address from;
    std::vector<acp127::Address> to, info, xmt;
    // Text.
    std::string class_line;          // "UNCLAS", "UNCLAS SVC" or empty when none was sent
    std::vector<std::string> text;   // lines after the class line, trailing spaces removed
    // After the closing BT.
    std::string serial_again;        // "0212" when the serial is repeated
    std::string digest;              // MSC [DIGEST:...] value, if any
    std::string correction;
    std::vector<std::string> warnings;
};

// Reads one message. Returns false and sets *error when it can't find the
// envelope (VZCZC, a DE line, BT, text, BT, NNNN) or the message isn't
// unclassified.
bool parse(const std::string& raw, Message& out, std::string* error);

// The text as one string, lines joined with '\n'.
std::string text_of(const Message& m);

}  // namespace received
}  // namespace pna
