// The MSC message digest: the "[DIGEST:...]" line after the closing BT.
//
// What it is (MSC's ClassSignMessage::createMessageDigest and
// checkMessageDigest, src/cp/classsignmessage.cpp in this repository): MD5 over
// the text between the two BT lines, with the first line that starts UNCLAS
// left out and all whitespace collapsed to single spaces (Qt's simplified()),
// as Latin-1, written as 32 upper-case hex digits: [DIGEST:249F5FBA...].
//
// It is an ERROR CHECK, NOT A SIGNATURE. It has no key: anyone can compute it
// for any text, so it shows a message arrived without a garbled character, never
// who sent it or that nobody changed it on purpose. Codress traffic is
// authenticated by the cipher's own tag (codress.h), and any message can carry
// its sender's signature (station.h, docs/crypto.md).
//
// Reading is a little more forgiving than MSC: lines are trimmed before BT and
// UNCLAS are looked for, so indented traffic is read too. For text without
// leading spaces the two give the same digest.
#pragma once

#include <string>

namespace pna {
namespace digest {

// The digest of a whole message (VZCZC to NNNN), computed as above.
std::string compute(const std::string& message);

// The digest written in the message, or "" when it has none.
std::string written(const std::string& message);

enum class Check { None, Pass, Fail };
const char* check_name(Check c);  // "none", "pass", "fail"
Check check(const std::string& message);

// The message with a [DIGEST:...] line put after the closing BT (and after a
// correction or a repeated serial), before NNNN, in the message's own line
// ends. A message that already has one, or has no closing BT and NNNN (a chat
// line), comes back as it was.
std::string append(const std::string& message);

}  // namespace digest
}  // namespace pna
