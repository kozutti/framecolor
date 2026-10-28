#!/bin/sh
set -eu

fc_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fc_temp=$(mktemp -d "${TMPDIR:-/tmp}/framecolor-lut-test.XXXXXX")
trap 'rm -rf "$fc_temp"' EXIT HUP INT TERM
fc_home="$fc_temp/home"
fc_xdg="$fc_temp/config"
fc_library="$fc_home/Documents/FrameColor/luts"
mkdir -p "$fc_library" "$fc_temp/input"

fc_write_cube() {
    cat > "$1" <<'CUBE'
LUT_3D_SIZE 2
DOMAIN_MIN 0 0 0
DOMAIN_MAX 1 1 1
0 0 0
1 0 0
0 1 0
1 1 0
0 0 1
1 0 1
0 1 1
1 1 1
CUBE
}

fc_write_cube "$fc_library/b.cube"
fc_write_cube "$fc_library/A.CUBE"
fc_write_cube "$fc_library/.hidden.cube"
printf 'ignore\n' > "$fc_library/ignore.txt"
fc_write_cube "$fc_temp/input/installed.cube"

fc_run() {
    HOME="$fc_home" XDG_CONFIG_HOME="$fc_xdg" sh "$fc_root/contrib/framecolor-lut" "$@"
}

fc_listing=$(fc_run list)
[ "$fc_listing" = "A.CUBE
b.cube" ]

fc_run select 'b.cube' >/dev/null
[ "$(cat "$fc_xdg/framecolor/lut.txt")" = "enabled 1
file b.cube" ]

fc_run off >/dev/null
[ "$(cat "$fc_xdg/framecolor/lut.txt")" = "enabled 0
file b.cube" ]
fc_run disable >/dev/null
fc_run on >/dev/null
[ "$(head -n 1 "$fc_xdg/framecolor/lut.txt")" = 'enabled 1' ]

printf 'state active\nfile b.cube\nmessage ready\n' > "$fc_xdg/framecolor/lut-status.txt"
fc_status=$(fc_run status)
printf '%s\n' "$fc_status" | grep -F '[lut.txt]' >/dev/null
printf '%s\n' "$fc_status" | grep -F 'state active' >/dev/null

fc_validate_output=$(fc_run validate "$fc_temp/input/installed.cube")
printf '%s\n' "$fc_validate_output" | grep -F 'Valid 2^3 LUT' >/dev/null
fc_install_output=$(fc_run install "$fc_temp/input/installed.cube")
printf '%s\n' "$fc_install_output" | grep -F 'Installed and enabled installed.cube (2^3)' >/dev/null
[ -f "$fc_library/installed.cube" ]
[ "$(cat "$fc_xdg/framecolor/lut.txt")" = "enabled 1
file installed.cube" ]

if fc_run select '../installed.cube' >/dev/null 2>&1; then
    printf 'select accepted a path traversal name\n' >&2
    exit 1
fi
printf 'not a LUT\n' > "$fc_temp/input/invalid.cube"
if fc_run validate "$fc_temp/input/invalid.cube" >/dev/null 2>&1; then
    printf 'validate accepted malformed LUT data\n' >&2
    exit 1
fi

printf 'framecolor-lut list/select/on/off/install/status tests passed\n'
