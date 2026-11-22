// The message engine's ACP 127 format: builds and parses messages in the
// tape relay format of
//
//   ACP 127(G), "Communications Instructions Tape Relay Procedures",
//   November 1988 (frozen by the CCEB on 10 December 1997), as published at
//   commsmuseum.co.uk. Paragraph numbers below are from that edition.
//
// The current ACP series is not used. Only PLAINDRESS messages in the
// automatic (VZCZC) or manual (VV) form are built. Only UNCLAS is accepted:
// the appliance sends plain text (the cipher is a stand-in), so the builder
// refuses any other classification.
//
// The text form is ASCII, not ITA2. Machine functions are shown as:
//   2CR 1LF (end of each format line, 138)       "\r\r\n"
//   2CR 8LF (before NNNN, 138.b and 138.d)        "\r\r\n\n\n\n\n\n\n\n"
// LTRS and FIGS shifts (format line 1, 202.c; 12 LTRS after NNNN, 138.d) and
// the FLASH bell signal (151.b) have no ASCII character and are not written;
// the parser ignores BEL characters if a sender includes them.
//
// This sits above the core's link framing (frame.h), which carries a built
// message as its body; the two are independent.
#pragma once

#include <string>
#include <vector>

namespace pna {
namespace acp127 {

constexpr const char* kEdition = "ACP 127(G), November 1988";
constexpr size_t kMaxLine = 69;  // 138.f: no line over 69 characters, spaces included

// An address in format lines 6 to 9: a routing indicator and/or a plain
// language address, written "RI/PLAIN" (215) or either alone.
struct Address {
    std::string ri;
    std::string plain;
    bool operator==(const Address& o) const { return ri == o.ri && plain == o.plain; }
};

// Date-time group (113): DDHHMM, zone letter, month, and the year in four
// digits (from 1 January 2006). year 0 omits it, as older examples do.
struct Dtg {
    int day = 1, hour = 0, minute = 0;
    char zone = 'Z';
    int month = 1;  // 1..12
    int year = 0;
    bool operator==(const Dtg& o) const {
        return day == o.day && hour == o.hour && minute == o.minute && zone == o.zone && month == o.month &&
               year == o.year;
    }
};

struct Message {
    // Format line 1 (202.c): transmission identification.
    bool automatic = true;        // VZCZC (automatic relay) or VV (manual)
    std::string channel;          // three station and channel letters
    int channel_serial = 1;       // 001..999 (112)
    // Format line 2 (151): precedence prosign, twice, then routing indicators.
    char prec = 'R';              // Z FLASH, O IMMEDIATE, P PRIORITY, R ROUTINE (150.b)
    std::vector<std::string> routing;
    // Format line 3: DE, originating station's RI, station serial (129), filing time (115).
    // The edition writes "DE RI 134 02/0009Z". MARS stations write
    // "DE RI #0134 0330009": the serial as four digits after '#' and the
    // time filed as Julian day, hour and minute (traffic samples).
    bool mars_fl3 = false;
    std::string originator_ri;
    std::string station_serial;   // edition: digits, optionally one letter suffix (107A); MARS: 1 to 4 digits
    int filing_day = 1, filing_hour = 0, filing_minute = 0;  // edition form DD/HHMMZ
    int filing_yday = 1;          // MARS form: day of the year, 1..366
    // Format line 4 (203): ZNR UUUUU, then any transmission instructions.
    bool security_line = true;
    std::string transmission_instructions;
    // Format line 5: precedence prosign and date-time group.
    Dtg dtg;
    // Format lines 6-9.
    Address from;
    std::vector<Address> to, info, xmt;
    // Format line 10: accounting symbol or group count (e.g. "GR 37"); empty omits it.
    std::string line10;
    // Format line 12: the text after UNCLAS. '\n' starts a new line; long
    // lines are wrapped at spaces to fit 69 characters.
    std::string text;
    // Format line 15 (211.c): correction, e.g. "C LINE 8 WA ...". Empty omits it.
    std::string correction;
    // Classification letter from format lines 1, 4 and 12. Only 'U' is built.
    char classification = 'U';
};

// Builds the whole message, start to NNNN. Returns "" and sets *error when a
// field breaks the format (bad character, line over 69, missing field).
std::string build(const Message& m, std::string* error);

// Parses a message built by build() or by another station that follows the
// same edition. Accepts "\r\r\n", "\r\n" or "\n" line ends. Returns false and
// sets *error (naming the format line) on anything it cannot place.
bool parse(const std::string& text, Message& out, std::string* error);

// Name of a precedence prosign letter ("IMMEDIATE"), or "" if not one.
const char* prec_name(char p);

// True if s can stand where a routing indicator goes: 3 to 12 of A-Z, 0-9
// and '-' with at least one letter. Wider than ACP 127(G), to take the call
// signs and tactical calls MARS stations use (see acp127.cpp).
bool valid_ri(const std::string& s);

// True if every character is one ACP 127 text may carry (134, 140): A-Z,
// 0-9, space and - ? : ( ) . , /. Lower case is not allowed; upcase first.
bool valid_chars(const std::string& s, char* bad = nullptr);

}  // namespace acp127
}  // namespace pna
