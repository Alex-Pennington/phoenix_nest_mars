// SHA-256 (FIPS 180-4), to check greybeard-ale's payload files against the
// digest in CAPTURE.modems.json. Integrity against a half-written file, not
// security.
#pragma once

#include <string>

namespace pna {

// Lower-case hex digest of `data`.
std::string sha256_hex(const std::string& data);

}  // namespace pna
