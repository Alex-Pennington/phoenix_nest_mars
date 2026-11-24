// Chat lines as MARS stations send them over the same modem path as traffic.
//
// Drawn from the MARS traffic samples (tests/fixtures/mars-samples-b2, 28 to
// 43), not from an ACP 201 edition, which could not be read:
//
//   KAPPA71-CM DE AAR4XR // QRU U,A // 2001Z K
//   <to>       DE <from> // <text>  // <HHMM>[Z] K
//
// The text may itself hold "//" (a MARSSTATUS strip ends in "//"), so it is
// everything between the first and the last "//". The separators may come
// with or without spaces, and the time with or without Z: the MSC automatic
// receipt is "ALL-CM DE AAR4XR-CM//QSL _R_UHSCTZ #0025 3051616/AAR4XR  //2339K".
//
// What the samples' chat lines say, by kind:
//   Ver              VER: are you there
//   QruUA            QRU U,A (have you traffic for me, unclassified, any
//                    precedence), optionally "QRU U,A [COMP]"
//   Zee              ZEE n: I have n messages for you
//   Qsl              QSL n: I received your message n
//   TrafficComplete  TRAFFIC COMPLETE
//   Receipt          MSC's automatic receipt: QSL _<prec>_<originator>
//                    #<serial> <DDDHHMM>/<receiving calls>
//   SqlQuery         "SQL SELECT ...": a remote query of another station's
//                    MSC database. Read and named, never run.
//   Text             anything else (a strip, a checklist, a test, plain chat)
//
// The MSC Communications Processor's own chat window (src/cp, dialogchat.cpp
// and classipchatxml.cpp) sends person-to-person chat in a longer form, with
// an ID so the far end can answer it:
//
//   chat|<ID>|<to> DE <from> // CRLF CRLF <text> CRLF CRLF // <instr> K DD-HHMMZ
//
// The ID is the sender's call and DDHHMMSSzzz; the ending is K (or AR) and
// day-hour-minute. The far end answers "ack|<ID>" (or "ack|<ID>|<from>" when
// it relays) or "nack|<ID>" for a damaged copy; "canx|<ID>" cancels one. The
// text may run over several lines.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pna {
namespace acp201 {

enum class Kind { Text, Ver, QruUA, Zee, Qsl, TrafficComplete, Receipt, SqlQuery };
const char* kind_name(Kind k);

struct Chat {
    std::string to;                 // "KAPPA71-CM", "ALL", "AXQ1"
    std::string from;               // "AAR4XR" or "AAR4XR-CM"
    std::string text;               // between the separators, trimmed
    int hour = -1, minute = -1;     // the time at the end
    bool zulu = true;               // the time carried Z
    Kind kind = Kind::Text;
    int number = -1;                // Zee and Qsl: the count or message number
    bool comp = false;              // QruUA: "[COMP]" (a compressed reply wanted)
    // Receipt.
    char prec = 0;                  // Z O P R
    std::string receipt_of;         // originator of the message received (RI or call)
    std::string receipt_serial;     // "0025"
    std::string receipt_time;       // "3051616", Julian DDDHHMM
    std::vector<std::string> receipt_by;  // receiving station calls
    std::vector<std::string> warnings;
    // CP's chat form.
    std::string cp_id;              // non-empty: "chat|<ID>|..."
    std::string instructions;       // CP's operator instructions before K
    bool over = false;              // ended AR rather than K
    int day = -1;                   // CP: the day of the month before the time
};

// A CP chat answer: "ack|ID", "ack|ID|FROM", "nack|ID" or "canx|ID".
struct CpReply {
    enum class Kind { Ack, Nack, Canx } kind = Kind::Ack;
    std::string id;
    std::string from;  // ack via a relay only
};
bool parse_cp_reply(const std::string& line, CpReply& out);
std::string build_cp_reply(const CpReply& r);

// A CP chat ID: the call and DDHHMMSSzzz from unix milliseconds, UTC.
std::string cp_chat_id(const std::string& call, int64_t unix_ms);

// Reads one chat line (trailing CR/LF and spaces allowed). False with *error
// when it has no " DE ", fewer than two "//", or no time and K at the end.
bool parse(const std::string& line, Chat& out, std::string* error);

// True when raw has no VZCZC envelope and its first non-empty line reads as
// chat. parse_traffic uses this to choose the reader.
bool looks_like_chat(const std::string& raw);

// Writes "<to> DE <from> // <text> // HHMMZ K" (no line end). For a Receipt
// the text is made from the receipt fields, in MSC's compact spacing. With
// cp_id set it writes CP's chat form instead (above; day must be set), with
// CRLF line ends.
// Returns "" and sets *error when a field breaks the form.
std::string build(const Chat& c, std::string* error);

}  // namespace acp201
}  // namespace pna
