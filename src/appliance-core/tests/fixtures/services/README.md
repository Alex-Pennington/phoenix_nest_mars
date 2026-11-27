Service tables for the core's tests, in the format of greybeard-ale
`docs/services.md`. `amateur.json` is the public amateur table as that document
ships it. `mars.json` has the MARS table's structure with made-up values only:
the real MARS values are restricted and never enter a repo. Tests copy these
files into a temporary directory and pass it as `--services-dir`.
