# MARS traffic samples (test fixtures)

43 received messages, taken from Alex-Pennington/mars-traffic-samples at commit
7f8ae36 and scrubbed a second time here (22 Nov 2026), so these files no longer
match that repository byte for byte.

The second scrub kept every line, line ending and line length, and the stray
bytes of the noisy receptions, so the reader sees the same shapes. It replaced,
consistently across all files:

- Call signs: the service prefix and region digit stay (AAR4, AFA1), the
  suffix is made up.
- Routing indicators: UH stays, the rest is made up; suffixes like -OP stay.
- Tactical calls, collectives, ALE addresses and net designators.
- Channel designators (now M900 to M918). M110, the modem standard, stays.
- Names of people, towns and counties in station status strips, county FIPS
  codes, map grid squares and a station's served agency town.
- Authenticators (now XXX).
- The meeting link, meeting ID and passcode.

Kept as they were: precedence, serials, times, DTGs, states, the 555-010 phone
numbers and example-mx.com address (already fakes), public sites (dodmars.org,
connect.apan.org), public exercise names, the public city lists in RI requests,
and the digests.

Left out on purpose:
- Samples 16, 17 and 28, because they give MARS net frequencies.
- That repository's NOTES.md. What it says about format lines is summed up in
  src/received.h.

`[DIGEST:...]` values don't verify, because the scrubs changed the text they hash.
