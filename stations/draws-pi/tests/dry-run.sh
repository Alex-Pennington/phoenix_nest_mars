#!/bin/bash
# Runs the DRAWS helpers with no Pi, no hat and no radio (DRAWS_DRY_RUN=1).
# Checks: keying refused without the enable file, a WAV converts and the
# key/play/unkey sequence runs in order, an over-long WAV is refused before
# keying, and the recorder writes an 8 kHz mono file.
# Needs bash, sox, soxi, flock, setsid, awk.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT

sed -e "s|^TX_ENABLE_FILE=.*|TX_ENABLE_FILE=$T/tx-enabled|" \
    -e "s|^TX_SPOOL=.*|TX_SPOOL=$T/tx|" \
    -e "s|^RX_SPOOL=.*|RX_SPOOL=$T/rx|" \
    -e "s|^RX_FILE_SECONDS=.*|RX_FILE_SECONDS=2|" \
    -e "s|^TX_MAX_SECONDS=.*|TX_MAX_SECONDS=5|" \
    "$HERE/config/station.env" > "$T/station.env"
export DRAWS_ENV="$T/station.env" DRAWS_DRY_RUN=1 DRAWS_LOCK="$T/tx.lock" TMPDIR="$T"

fail() { echo "FAIL: $*"; exit 1; }
pass() { echo "ok   $*"; }

# A stand-in burst: 2 s of 1800 Hz at 8 kHz mono, like gale-tx's format.
sox -q -n -r 8000 -c 1 -b 16 "$T/burst.wav" synth 2 sine 1800 vol 0.5
sox -q -n -r 8000 -c 1 -b 16 "$T/long.wav" synth 9 sine 1800 vol 0.5

# 1. Keying refused without the enable file.
if "$HERE/bin/draws-ptt" on 2>/dev/null; then fail "ptt on without enable file"; fi
pass "draws-ptt on refused without the enable file"
if "$HERE/bin/draws-txwav" "$T/burst.wav" 2>/dev/null; then fail "txwav without enable file"; fi
pass "draws-txwav refused without the enable file"

# 2. Unkey always allowed.
[[ "$("$HERE/bin/draws-ptt" off)" == "DRY: pinctrl set 12 op dl" ]] || fail "ptt off"
pass "draws-ptt off drives BCM12 low"

touch "$T/tx-enabled"

# 3. Over-long WAV refused before keying.
out="$("$HERE/bin/draws-txwav" "$T/long.wav" 2>&1 || true)"
[[ "$out" == *"limit is 5s"* ]] || fail "long wav not refused: $out"
[[ "$out" != *"op dh"* ]] || fail "long wav keyed"
pass "over-long WAV refused, never keyed"

# 4. Normal send: key, play, unkey, in that order, then a JSON line.
out="$("$HERE/bin/draws-txwav" "$T/burst.wav")"
while IFS= read -r l; do echo "     $l"; done <<<"$out"
seq="$(echo "$out" | grep -n -E 'op dh|aplay|op dl' | cut -d: -f2- | sed -E 's/.*(op dh|aplay|op dl).*/\1/' | tr '\n' ' ')"
[[ "$seq" == "op dh aplay op dl " ]] || fail "sequence was: $seq"
echo "$out" | tail -1 | grep -q '"event":"tx"' || fail "no tx record"
echo "$out" | grep -q '(48000 Hz, 1 ch)' || fail "not converted to 48 kHz mono"
pass "key, play, unkey in order; tx record written"

# 4b. A second send straight after the first is not blocked by the watchdog.
"$HERE/bin/draws-txwav" "$T/burst.wav" >/dev/null || fail "back-to-back send refused"
pass "back-to-back sends allowed"

# 5. Recorder: one 2 s file, 8 kHz mono.
f="$("$HERE/bin/draws-rxrec" --once)"
[[ -f "$f" ]] || fail "no rx file"
[[ "$(soxi -r "$f")" == 8000 && "$(soxi -c "$f")" == 1 ]] || fail "rx file not 8 kHz mono"
pass "recorder wrote $(basename "$f") at 8 kHz mono"

# 6. Watchdog: kill -9 the sender while keyed; PTT must still drop at TX_MAX_SECONDS.
cat > "$T/fake-ptt" <<'FAKE'
#!/bin/bash
echo "$1" >> "$PTT_LOG"
FAKE
chmod +x "$T/fake-ptt"
export PTT_LOG="$T/ptt.log"
sed -i -e 's/^PTT_LEAD_MS=.*/PTT_LEAD_MS=4000/' -e 's/^TX_MAX_SECONDS=.*/TX_MAX_SECONDS=8/' "$T/station.env"
DRAWS_PTT="$T/fake-ptt" "$HERE/bin/draws-txwav" "$T/burst.wav" >/dev/null 2>&1 &
pid=$!
sleep 1
[[ "$(tail -1 "$PTT_LOG")" == on ]] || fail "not keyed before kill"
kill -9 "$pid"; wait "$pid" 2>/dev/null || true
sleep 8.5
[[ "$(tail -1 "$PTT_LOG")" == off ]] || fail "watchdog did not unkey after kill -9"
pass "watchdog unkeyed after the sender was killed"

echo "all dry-run checks passed"
