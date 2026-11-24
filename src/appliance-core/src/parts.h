// Long blocks over the air in numbered parts (docs/crypto.md, Parts).
//
// A sealed key tape of a month or more, or a startup bundle that has grown,
// is longer than one message (8192 bytes). It goes as numbered parts, in
// the ACP 127 way a long message is sent in sections: each part is its own
// signed plain message, headed and closed so a part read alone says what it
// belongs to.
//
//   PART 2 OF 5 PNA TAPE 4HNPQ2XR
//   (lines of the block)
//   END PART 2 OF 5
//
// The tag ties the parts of one block together. The receiver holds the
// parts until every one has come, from the same signer, and opens the
// block only then. It asks for the ones it lacks by number:
//
//   PNA PARTS REQUEST TAPE 4HNPQ2XR
//   MISSING 2 4
//   END REQUEST
//
// and drops a set still incomplete after the hold time.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pna {
namespace parts {

// What a set of parts carries: "TAPE" (a sealed key tape) or "BUNDLE".
// Splits text into parts, each at most max_body bytes, cut at line ends.
std::vector<std::string> split(const std::string& kind, const std::string& tag, const std::string& text,
                               size_t max_body);

struct Head {
    std::string kind, tag;
    int n = 0, of = 0;
};
// The part header in text, if there is one. Also reads the bundle parts of
// earlier builds ("PNA BUNDLE PART 2 OF 3 TAG 4HNPQ2XR").
bool head(const std::string& text, Head& out);
// The block's lines carried by one part.
std::string body(const std::string& text);
// One part, header to END line, cut out of a stored message (to send again).
std::string cut(const std::string& text);

// A tag for a block: 8 characters from its hash.
std::string tag_of(const std::string& text);

// The request for missing parts, and reading it back.
std::string request_text(const std::string& kind, const std::string& tag, const std::vector<int>& missing);
bool read_request(const std::string& text, std::string& kind, std::string& tag, std::vector<int>& missing);

}  // namespace parts
}  // namespace pna
