#!/bin/sh
# Builds appliance-core and installs it for the user who runs this: no root.
#
#   packaging/install.sh             build, test, install, set up the service
#   packaging/install.sh --enable    ...and start it now and at every login
#   packaging/install.sh --uninstall remove the programs and the service
#
# Programs go in ~/.local/bin, the settings in
# ~/.config/phoenix-nest-mars/appliance.conf (never overwritten), the data in
# ~/.local/share/phoenix-nest-mars, and a systemd user service called
# appliance-core. Uninstalling keeps the settings and the data.
set -eu

PREFIX="$HOME/.local"
ENABLE=0
UNINSTALL=0
TESTS=1
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) PREFIX="$2"; shift 2 ;;
        --enable) ENABLE=1; shift ;;
        --no-tests) TESTS=0; shift ;;
        --uninstall) UNINSTALL=1; shift ;;
        -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install.sh: unknown option $1 (try --help)" >&2; exit 2 ;;
    esac
done

SRC=$(cd "$(dirname "$0")/.." && pwd)
CONF_DIR="$HOME/.config/phoenix-nest-mars"
CONF="$CONF_DIR/appliance.conf"
UNIT_DIR="$HOME/.config/systemd/user"
DATA_DIR="$HOME/.local/share/phoenix-nest-mars"
BUILD="${XDG_CACHE_HOME:-$HOME/.cache}/phoenix-nest-mars/build"
SHARE="$PREFIX/share/appliance-core"  # the example settings and the service file; never the data

say() { printf '%s\n' "$*"; }

have_user_systemd() {
    command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1
}

if [ "$UNINSTALL" = 1 ]; then
    if have_user_systemd; then
        systemctl --user disable --now appliance-core >/dev/null 2>&1 || true
    fi
    rm -f "$UNIT_DIR/appliance-core.service"
    have_user_systemd && systemctl --user daemon-reload || true
    rm -f "$PREFIX/bin/appliance-core" "$PREFIX/bin/appliance-cli"
    rm -rf "$SHARE" "$PREFIX/share/doc/phoenix-nest-mars"
    say "Removed appliance-core, appliance-cli and the service."
    say "Kept your settings in $CONF_DIR and your data in $DATA_DIR."
    exit 0
fi

for tool in cmake c++; do
    command -v "$tool" >/dev/null 2>&1 || { echo "install.sh: $tool is needed (and SQLite3's headers)" >&2; exit 1; }
done

say "Building in $BUILD"
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
cmake --build "$BUILD" -j >/dev/null
if [ "$TESTS" = 1 ]; then
    say "Testing"
    (cd "$BUILD" && ctest --output-on-failure >/dev/null) || {
        echo "install.sh: a test failed; nothing was installed. See: (cd $BUILD && ctest --output-on-failure)" >&2
        exit 1
    }
fi
cmake --install "$BUILD" >/dev/null
say "Installed appliance-core and appliance-cli in $PREFIX/bin"

mkdir -p "$CONF_DIR" "$DATA_DIR"
if [ -e "$CONF" ]; then
    say "Kept your settings: $CONF"
else
    cp "$SHARE/appliance.conf.example" "$CONF"
    chmod 600 "$CONF"
    # The service may not see this shell's PATH, so write gale-tx's full path.
    if GALE=$(command -v gale-tx 2>/dev/null); then
        sed "s|^tx-cmd = gale-tx\$|tx-cmd = $GALE|" "$CONF" > "$CONF.new" && mv "$CONF.new" "$CONF"
        chmod 600 "$CONF"
        say "Found gale-tx at $GALE"
    fi
    say "New settings file: $CONF"
fi

mkdir -p "$UNIT_DIR"
cp "$SHARE/appliance-core.service" "$UNIT_DIR/appliance-core.service"
if have_user_systemd; then
    systemctl --user daemon-reload
    say "Service set up: appliance-core (systemd, your user)"
else
    say "No systemd user session here: the service file is in $UNIT_DIR, but"
    say "start the core yourself: $PREFIX/bin/appliance-core --config $CONF"
fi

say ""
say "Checking the settings:"
if OUT=$("$PREFIX/bin/appliance-core" --config "$CONF" --check 2>&1); then READY=1; else READY=0; fi
printf '%s\n' "$OUT" | sed 's/^/  /'
if [ "$READY" = 1 ]; then
    if [ "$ENABLE" = 1 ] && have_user_systemd; then
        systemctl --user enable --now appliance-core
        say "Started. It starts again at every login."
    else
        say "Start it with: systemctl --user enable --now appliance-core"
    fi
else
    say "Edit $CONF, then run install.sh --enable again, or start it with"
    say "systemctl --user enable --now appliance-core"
fi
say "Quick start: $PREFIX/share/doc/phoenix-nest-mars/quick-start.md"
