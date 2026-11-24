// Startup bundles: the operating data a net needs, so that a box brought out
// of the can doesn't have to be typed in by hand. docs/crypto.md has the
// design ("Startup bundles").
//
// A bundle carries the station book, the channel plan by designator (never a
// frequency), the net schedules and the message settings (how each station
// is reached). It is plain text in the radio's own character set, signed by
// the station that built it, with that station's card inside, so it can go
// by stick or over the air as ordinary signed plain traffic.
//
// A bundle is operating data, not trust: loading one never adds, checks or
// names a card and never touches a key. Trust and keys come from handouts,
// cards and sealed tapes (station.h), each on the member's own choice.
//
//   PNA STARTUP BUNDLE 1
//   LABEL PRACTICE REGION
//   BY NCS-A
//   MADE 202611240000Z
//   SIGNER NCS-A K7QM 2XRD 9FTA HW3C
//   STEP 1 ...
//   BOOK STA-B            a book entry, then its fields
//   KIND STATION
//   CALL STA-B
//   RI UHAAAB
//   NOTE NET CONTROL
//   CHANNEL P1            a channel designator, then its fields
//   MODE USB
//   USE PRIMARY
//   SCHEDULE PRACTICE NET a net schedule, then its fields
//   DAYS TUE,THU
//   START 1900
//   ...
//   LINK STA-D PLAIN      how a station is reached
//   (the signer's card)
//   SIG ...
//   SIG ...
//   END BUNDLE
#pragma once

#include "channels.h"
#include "ops.h"
#include "station.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pna {
namespace bundle {

struct Bundle {
    std::string label;      // what the bundle is for (a net or region), optional
    std::string by;         // the station that built and signed it
    int64_t made = 0;
    std::string signer_card;  // the signer's card text, inside the bundle
    std::string signer_fp;
    std::vector<BookEntry> book;
    std::vector<Channel> channels;  // designator, mode and use; hz is always 0
    std::vector<NetSchedule> schedules;
    std::vector<std::pair<std::string, std::string>> links;  // station, "pna1" or "plain"
    std::string tag;        // 8 characters from SHA-256 of the signed text: names its parts on the air
};

// What this station holds now, as a bundle (book, channel designators,
// schedules). links are the core's link formats. *changed names anything
// that had to be cut or left out to fit the radio's character set, a line
// of 69, or to keep a frequency out.
Bundle collect(Ops& ops, const std::vector<std::pair<std::string, std::string>>& links, const std::string& label,
               std::vector<std::string>* changed);

// The signed text. Fields are already radio-safe after collect().
std::string format(const Bundle& b, const station::Identity& signer);

// Finds a bundle in the text, checks its layout, the signer's card and the
// signature against that card. False with *error.
bool parse(const std::string& text, Bundle& out, std::string* error);

// On the air a bundle goes in parts, each under max_body bytes of text:
//   PNA BUNDLE PART 2 OF 3 TAG 4HNPQ2XR
//   (lines of the bundle)
//   END PART
std::vector<std::string> split(const std::string& bundle_text, size_t max_body);
// The part header in text, if there is one.
bool part_head(const std::string& text, std::string& tag, int& n, int& of);
// The lines of one part (between its header and END PART).
std::string part_body(const std::string& text);

// Loads a bundle into the station's book, schedules and channel plan.
// Entries are added, or replaced by name; nothing is removed. A channel
// designator the plan doesn't already hold can't be added without its
// frequency, so it is listed in need_frequency: load the frequency card
// (mars_import) for it.
struct Applied {
    int book_added = 0, book_replaced = 0, book_same = 0;
    int schedules_added = 0, schedules_replaced = 0, schedules_same = 0;
    int channels_updated = 0;
    std::vector<std::string> need_frequency;
    std::vector<std::string> problems;
};
Applied apply(const Bundle& b, Ops& ops, const std::string& source);

// Radio-safe text: capitals, A-Z 0-9 space and - ? : ( ) . , /, spaces made one.
std::string radio_text(const std::string& s);

}  // namespace bundle
}  // namespace pna
