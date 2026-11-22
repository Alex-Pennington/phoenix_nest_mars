# MARS traffic samples, batch 2 (test fixtures)

43 received messages and chat lines, taken from Alex-Pennington/mars-traffic-samples
at commit 075ece8 (batch2) and scrubbed a second time here (22 Nov 2026), the
same way as batch 1 (see ../mars-samples/README.md). These files no longer
match that repository byte for byte.

The second scrub kept every line, line ending and line length. On top of what
batch 1's scrub replaces, it replaced:

- Amateur call signs (the prefix and digit stay).
- Further tactical calls, collectives, ALE addresses and net designators.
- Anything shaped like a frequency (now zeros).
- Towns, ZIP-style codes and county FIPS codes in the station status strips
  of the chat lines and RRI responses.
- A map grid and a key indicator value.

The CODRESS bodies (02, 03) and 04's code groups were already random in the
samples. `[DIGEST:...]` values don't verify.

`TEMPLATES.csv` names, for each file, the MSC template (procedure), the form
and the composer, taken from the samples' SOURCES.csv without its source
paths. Composers in this batch: MessageMachine 17, MSCChat 13, StationMapper
6, MessageCreator 1, not recorded 6.

Sample 17 is cut off before its closing BT and is refused on purpose; sample
20 ends without NNNN after its closing BT and is taken with a warning.
`test_templates` reads every file.
