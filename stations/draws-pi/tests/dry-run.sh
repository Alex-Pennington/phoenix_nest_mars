#!/bin/bash
# Runs the DRAWS helpers with no Pi, no hat and no radio (DRAWS_DRY_RUN=1).
# Checks: keying refused without the enable file, a WAV converts and the
# key/play/unkey sequence runs in order, an over-long WAV is refused before
# keying, the recorder writes an 8 kHz mono file, the port finder picks the
# port with audio; the upstream helpers draws-send and draws-pull through a
# fake ssh (copy verified before keying, keying refused without the enable
# file, only verified recordings deleted); draws-setfreq against a fake
# rigctld (only F, f, m sent, read-back checked); and (tests/findcat-test.py)
# the CAT finder. Set TMPDIR first: every scratch file goes under it.
# Needs bash, sox, soxi, flock, setsid, awk, sha256sum, python3.
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

# 7. Port finder: picks the port with audio, writes DRAWS_PORT, refuses a tie.
for side in right left; do
    DRAWS_FAKE_SIDE=$side FINDPORT_SECONDS=1 "$HERE/bin/draws-findport" --write "$T/station.env" >/dev/null
    grep -qx "DRAWS_PORT=$side" "$T/station.env" || fail "findport did not pick $side"
done
if DRAWS_FAKE_SIDE=none FINDPORT_SECONDS=1 "$HERE/bin/draws-findport" --write "$T/station.env" >/dev/null 2>&1; then
    fail "findport picked a port with no audio on either"
fi
grep -qx "DRAWS_PORT=left" "$T/station.env" || fail "findport changed DRAWS_PORT on a tie"
pass "port finder picks the port with audio, refuses when it cannot tell"

# 8. Right port: PTT follows DRAWS_PORT to BCM23.
sed -i 's/^DRAWS_PORT=.*/DRAWS_PORT=right/' "$T/station.env"
[[ "$("$HERE/bin/draws-ptt" off)" == "DRY: pinctrl set 23 op dl" ]] || fail "right port ptt pin"
pass "right port: PTT on BCM23"

# The upstream helpers (draws-send, draws-pull) reach the Pi through a fake
# ssh that runs the remote command here, in a shell of its own. The fake Pi's
# side always runs dry (DRAWS_DRY_RUN=1), so its draws-ptt prints the pinctrl
# line it would run: "op dh" in any output means something keyed.
sed -i -e 's/^PTT_LEAD_MS=.*/PTT_LEAD_MS=150/' -e 's/^TX_MAX_SECONDS=.*/TX_MAX_SECONDS=5/' "$T/station.env"
mkdir -p "$T/tx" "$T/rx" "$T/up"
cat > "$T/fake-ssh" <<'FAKE'
#!/bin/bash
# Drops ssh's options, logs the host and command, runs the command here.
while [[ "${1:-}" == -* ]]; do case "$1" in -o|-i) shift 2 ;; *) shift ;; esac; done
echo "$1 :: ${*:2}" >> "$SSH_LOG"; shift
export DRAWS_DRY_RUN=1
if [[ -n "${CORRUPT_COPY:-}" && "$*" == *"cat > "* ]]; then
    head -c 100 | bash -c "$*"; exit
fi
if [[ -n "${FAIL_SEND:-}" && "$*" == *"draws-txwav"* ]]; then
    exit 255
fi
if [[ -n "${CORRUPT_CAT:-}" && "$*" == "cat -- "*"$CORRUPT_CAT" ]]; then
    bash -c "$*" | head -c 100; exit 0
fi
exec bash -c "$*"
FAKE
chmod +x "$T/fake-ssh"
cat > "$T/upstream.env" <<UP
DRAWS_PI_HOST=fake-pi
DRAWS_PI_SSH_OPTS="-i $T/fake-key -o HostKeyAlias=fake-alias"
PI_TX_SPOOL=$T/tx
PI_RX_SPOOL=$T/rx
PI_TXWAV=$HERE/bin/draws-txwav
LOCAL_RX_DIR=$T/up
UP
export DRAWS_UPSTREAM_ENV="$T/upstream.env" SSH_LOG="$T/ssh.log"
: > "$SSH_LOG"
rm -f "$T/tx-enabled"

# 9. draws-send.
out="$("$HERE/bin/draws-send" "$T/burst.wav")"
[[ "$(grep -c '^DRY: ' <<<"$out")" == 3 ]] || fail "send dry run: $out"
[[ ! -s "$SSH_LOG" && -z "$(ls -A "$T/tx")" ]] || fail "send dry run reached the Pi"
grep -q "draws-txwav send-[0-9TZ]*-[0-9]*-burst.wav" <<<"$out" || fail "send dry run does not name the file: $out"
[[ "$(grep -c -- "-i $T/fake-key -o HostKeyAlias=fake-alias fake-pi" <<<"$out")" == 3 ]] || fail "send dry run does not use the settings file's ssh options: $out"
pass "draws-send dry run prints its three ssh steps and touches nothing"

rc=0; out="$(DRAWS_DRY_RUN=0 DRAWS_SSH="$T/fake-ssh" "$HERE/bin/draws-send" "$T/burst.wav" 2>&1)" || rc=$?
[[ $rc == 3 && "$out" == *"keying is off"* && "$out" != *"op dh"* ]] || fail "send without the enable file: rc=$rc $out"
[[ -z "$(ls -A "$T/tx")" ]] || fail "send left a file in the spool: $(ls -A "$T/tx")"
pass "draws-send without the enable file: draws-txwav's refusal (exit 3) comes back, nothing keyed, spool empty"

touch "$T/tx-enabled"
rc=0; out="$(CORRUPT_COPY=1 DRAWS_DRY_RUN=0 DRAWS_SSH="$T/fake-ssh" "$HERE/bin/draws-send" "$T/burst.wav" 2>&1)" || rc=$?
[[ $rc == 10 && "$out" == *"does not match"* && "$out" != *"op dh"* ]] || fail "send with a bad copy: rc=$rc $out"
[[ -z "$(ls -A "$T/tx")" ]] || fail "bad copy left in the spool: $(ls -A "$T/tx")"
pass "draws-send with a copy that does not verify: exit 10, never keyed, copy deleted"

rc=0; out="$(FAIL_SEND=1 DRAWS_DRY_RUN=0 DRAWS_SSH="$T/fake-ssh" "$HERE/bin/draws-send" "$T/burst.wav" 2>&1)" || rc=$?
[[ $rc == 11 && "$out" == *"MAY HAVE GONE OUT"* ]] || fail "send-stage ssh failure: rc=$rc $out"
rm -f "$T/tx/".send-*.part
pass "draws-send: ssh failing during the send order is exit 11 (may have keyed), not the copy stage's 10"

out="$(DRAWS_DRY_RUN=0 DRAWS_SSH="$T/fake-ssh" "$HERE/bin/draws-send" "$T/burst.wav")"
while IFS= read -r l; do echo "     $l"; done <<<"$out"
seq="$(echo "$out" | grep -n -E 'op dh|aplay|op dl' | cut -d: -f2- | sed -E 's/.*(op dh|aplay|op dl).*/\1/' | tr '\n' ' ')"
[[ "$seq" == "op dh aplay op dl " ]] || fail "send sequence was: $seq"
echo "$out" | grep -q '"file":"send-[0-9TZ]*-[0-9]*-burst.wav"' || fail "send: no tx record for the sent file"
[[ -z "$(ls -A "$T/tx")" ]] || fail "sent file left in the spool"
pass "draws-send with the enable file: copied, verified, keyed, played, unkeyed by draws-txwav; spool empty"
rm -f "$T/tx-enabled"

# 10. draws-pull.
: > "$SSH_LOG"
# The dry-run recorder is faster than real time; names are to the second.
a="$("$HERE/bin/draws-rxrec" --once)"; sleep 1.1; b="$("$HERE/bin/draws-rxrec" --once)"
[[ "$a" != "$b" ]] || fail "two recordings got one name"
touch "$T/rx/.rx-20000101T000000Z.part.wav" "$T/rx/rx-not-a-recording.wav"
out="$("$HERE/bin/draws-pull")"
[[ "$(grep -c '^DRY: ' <<<"$out")" == 3 && ! -s "$SSH_LOG" ]] || fail "pull dry run: $out"
[[ -f "$a" && -f "$b" && -z "$(ls -A "$T/up")" ]] || fail "pull dry run moved files"
pass "draws-pull dry run prints its ssh steps and moves nothing"

rc=0; out="$(CORRUPT_CAT="$(basename "$b")" DRAWS_DRY_RUN=0 DRAWS_SSH="$T/fake-ssh" "$HERE/bin/draws-pull" 2>&1)" || rc=$?
[[ $rc == 1 && ! -e "$a" && -f "$b" ]] || fail "pull with one bad copy: rc=$rc $out"
[[ -f "$T/up/$(basename "$a")" && ! -e "$T/up/$(basename "$b")" ]] || fail "pull: wrong files upstream: $(ls -A "$T/up")"
[[ -z "$(ls -A "$T/up" | grep -F .part)" ]] || fail "pull left a .part file upstream"
pass "draws-pull: the verified file moved and left the Pi; the bad copy stayed on the Pi (exit 1)"

out="$(DRAWS_DRY_RUN=0 DRAWS_SSH="$T/fake-ssh" "$HERE/bin/draws-pull" 2>&1)"
[[ ! -e "$b" && -f "$T/up/$(basename "$b")" ]] || fail "pull second run: $out"
[[ -e "$T/rx/.rx-20000101T000000Z.part.wav" && -e "$T/rx/rx-not-a-recording.wav" ]] || fail "pull took a file that is not a finished recording"
[[ "$(soxi -r "$T/up/$(basename "$b")")" == 8000 ]] || fail "pulled file is not the 8 kHz recording"
grep -q "rm -f -- .*$(basename "$a")" "$SSH_LOG" || fail "pull did not delete what it copied"
if grep -q "rm -f -- .*rx-not-a-recording" "$SSH_LOG"; then fail "pull deleted a file it did not copy"; fi
pass "draws-pull: next run takes the rest; a file being written and a stray name are left alone"

# 11. draws-setfreq. Never keys, never sets the mode, refuses a bad read-back.
out="$("$HERE/bin/draws-setfreq" 1234560)"
[[ "$out" == $'DRY: pinctrl get 23 (must read lo)\nDRY: rigctld 127.0.0.1:4532 <- F 1234560\nDRY: rigctld 127.0.0.1:4532 <- f\nDRY: rigctld 127.0.0.1:4532 <- m' ]] || fail "setfreq dry run: $out"
pass "draws-setfreq dry run: reads the port's PTT pin (BCM23, right), then F, f, m and nothing else"
for bad in 1234565 12.3456 -10 0 "1234560 USB"; do
    # shellcheck disable=SC2086  # the last case is two words on purpose
    if "$HERE/bin/draws-setfreq" $bad 2>/dev/null >/dev/null; then fail "setfreq took '$bad'"; fi
done
pass "draws-setfreq takes only a whole number of Hz on a 10 Hz step"

fake_rig() {  # fake_rig NAME [ENV=VALUE...]: start a fake rigctld, set port_NAME
    local n="$1"; shift
    env "$@" python3 -I "$HERE/tests/fake-rigctld.py" "$T/rig-$n.port" "$T/rig.log" &
    FAKES+=("$!")
    for _ in $(seq 50); do [[ -s "$T/rig-$n.port" ]] && break; sleep 0.1; done
    printf -v "port_$n" '%s' "$(cat "$T/rig-$n.port")"
}
FAKES=()
trap 'kill "${FAKES[@]}" 2>/dev/null; rm -rf "$T"' EXIT
fake_rig good; fake_rig usb FAKE_MODE=USB; fake_rig off FAKE_OFFSET=10
export DRAWS_READBACK_WAIT=0
# A fake pinctrl: prints a line in pinctrl get's format with PTT_LEVEL.
cat > "$T/fake-pinctrl" <<'FAKE'
#!/bin/bash
[[ "$1" == get ]] || { echo "fake-pinctrl: only get" >&2; exit 9; }
echo "$1 $2" >> "$PINCTRL_LOG"
case "${PTT_LEVEL:-lo}" in
    garbage) echo "nonsense" ;;
    fail) exit 1 ;;
    *) printf '%2d: op dl pd | %s // GPIO%d = output\n' "$2" "${PTT_LEVEL:-lo}" "$2" ;;
esac
FAKE
chmod +x "$T/fake-pinctrl"
export DRAWS_PINCTRL="$T/fake-pinctrl" PINCTRL_LOG="$T/pinctrl.log"
# shellcheck disable=SC2154  # port_* are set by fake_rig
{
out="$(DRAWS_DRY_RUN=0 DRAWS_RIGCTLD_PORT=$port_good "$HERE/bin/draws-setfreq" 1234560)"
[[ "$out" == '{"event":"freq","set_hz":1234560,"read_hz":1234560,"mode":"PKTUSB","ok":true}' ]] || fail "setfreq good: $out"
rc=0; DRAWS_DRY_RUN=0 DRAWS_RIGCTLD_PORT=$port_usb "$HERE/bin/draws-setfreq" 1234560 >/dev/null 2>&1 || rc=$?
[[ $rc == 5 ]] || fail "setfreq accepted mode USB (rc=$rc)"
rc=0; DRAWS_DRY_RUN=0 DRAWS_RIGCTLD_PORT=$port_off "$HERE/bin/draws-setfreq" 1234560 >/dev/null 2>&1 || rc=$?
[[ $rc == 4 ]] || fail "setfreq accepted a read-back 10 Hz off (rc=$rc)"
( exec 9>"$DRAWS_LOCK"; flock 9; sleep 2 ) &
sleep 0.3
rc=0; DRAWS_DRY_RUN=0 DRAWS_RIGCTLD_PORT=$port_good "$HERE/bin/draws-setfreq" 1234560 >/dev/null 2>&1 || rc=$?
wait $!
[[ $rc == 6 ]] || fail "setfreq ran during a transmission (rc=$rc)"
before="$(wc -l < "$T/rig.log")"
for lvl in hi garbage fail; do
    rc=0; out="$(PTT_LEVEL=$lvl DRAWS_DRY_RUN=0 DRAWS_RIGCTLD_PORT=$port_good "$HERE/bin/draws-setfreq" 1234560 2>&1)" || rc=$?
    [[ $rc == 7 ]] || fail "setfreq with the PTT line '$lvl': rc=$rc $out"
done
[[ "$(wc -l < "$T/rig.log")" == "$before" ]] || fail "setfreq talked to rigctld with the PTT line not low"
[[ "$(sort -u "$PINCTRL_LOG")" == "get 23" ]] || fail "setfreq ran pinctrl other than 'get 23': $(sort -u "$PINCTRL_LOG")"
}
LC_ALL=C sort -u "$T/rig.log" | tr '\n' ' ' | grep -qx 'F 1234560 f m ' || fail "rigctld got something else: $(LC_ALL=C sort -u "$T/rig.log" | tr '\n' ' ')"
pass "draws-setfreq: read-back checked (wrong mode exit 5, wrong frequency exit 4), refused during a transmission (exit 6); rigctld only ever got F, f and m"
pass "draws-setfreq: refused with the PTT line high or unreadable (exit 7), before rigctld; pinctrl only ever ran 'get 23'"

# The ALLOWED guard: loaded as a module, its rigctld client refuses any command
# outside F, f and m before anything is sent.
before="$(wc -l < "$T/rig.log")"
DRAWS_RIGCTLD_PORT=$port_good python3 -I - "$HERE/bin/draws-setfreq" <<'PY' || fail "ALLOWED guard"
import importlib.machinery, importlib.util, sys
loader = importlib.machinery.SourceFileLoader("setfreq", sys.argv[1])
spec = importlib.util.spec_from_loader("setfreq", loader)
m = importlib.util.module_from_spec(spec)
loader.exec_module(m)
rig = m.Rigctld()
for cmd, arg in (("T", 1), ("M", "USB 0"), ("w", r"\0xBE"), ("\\set_conf", None)):
    try:
        rig.ask(cmd, arg)
    except RuntimeError:
        continue
    sys.exit(f"ask({cmd!r}) was not refused")
rig.close()
PY
[[ "$(wc -l < "$T/rig.log")" == "$before" ]] || fail "a refused command reached rigctld"
pass "draws-setfreq's ALLOWED guard refuses T, M, w and set_conf before anything reaches rigctld"

# 12. No new helper keys without the enable file, and none keys by itself.
[[ ! -e "$T/tx-enabled" ]] || fail "enable file left over"
if grep -n -E 'draws-ptt|pinctrl|aplay' "$HERE/bin/draws-send" "$HERE/bin/draws-pull"; then
    fail "an upstream helper touches PTT or audio itself"
fi
# draws-setfreq reads the PTT line and nothing more: one pinctrl call, "get".
if grep -n -E 'draws-ptt|aplay|pinctrl set|"set"' "$HERE/bin/draws-setfreq"; then
    fail "draws-setfreq can drive PTT or audio"
fi
[[ "$(grep -c 'subprocess.run(\[PINCTRL, "get", pin\]' "$HERE/bin/draws-setfreq")" == 1 && "$(grep -c 'subprocess' "$HERE/bin/draws-setfreq")" == 3 ]] || fail "draws-setfreq runs something other than pinctrl get"

all="$(DRAWS_DRY_RUN=0 DRAWS_SSH="$T/fake-ssh" "$HERE/bin/draws-send" "$T/burst.wav" 2>&1 || true)
$(DRAWS_DRY_RUN=0 DRAWS_SSH="$T/fake-ssh" "$HERE/bin/draws-pull" 2>&1 || true)
$(DRAWS_DRY_RUN=0 DRAWS_RIGCTLD_PORT=$port_good "$HERE/bin/draws-setfreq" 1234560 2>&1 || true)"
[[ "$all" != *"op dh"* ]] || fail "something keyed without the enable file: $all"
pass "no new helper drives PTT or audio (draws-setfreq only reads the line), and none keys without the enable file"

# 13. CAT finder (pseudo-terminals as fake ports).
if ! python3 -I "$HERE/tests/findcat-test.py" | sed 's/^/     /'; then fail "findcat tests"; fi
pass "CAT finder tests"

echo "all dry-run checks passed"
