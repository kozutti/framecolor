#!/bin/sh
# One-command installer and updater for Steam Frame. Run on the Frame
# (Konsole in desktop mode, or over SSH) as the normal user:
#   curl -fsSL https://github.com/kozutti/framecolor/releases/latest/download/get.sh | sh
# It downloads the latest release, checks its SHA-256, installs it into the
# home directory and, on a first install, turns the correction on.
set -eu

# Everything runs inside main so that `curl ... | sh` has read the whole
# script before any command starts (a child reading stdin cannot eat it).
main() {
    fc_base_url=${FRAMECOLOR_BASE_URL:-https://github.com/kozutti/framecolor/releases/latest/download}
    fc_archive_name=framecolor-steamframe-aarch64.tar.gz
    fc_package_dir="$HOME/.local/share/framecolor-package"

    if [ "$(id -u)" -eq 0 ]; then
        printf 'Run this as the normal Steam Frame user, without sudo.\n' >&2
        exit 1
    fi
    if [ "$(uname -m)" != aarch64 ] || [ ! -d /opt/steamvr ]; then
        printf 'This installer is for Steam Frame (SteamOS ARM64) only.\n' >&2
        exit 1
    fi
    for fc_command in curl sha256sum tar; do
        command -v "$fc_command" >/dev/null 2>&1 || { printf 'Missing required command: %s\n' "$fc_command" >&2; exit 1; }
    done

    fc_temp_dir=$(mktemp -d)
    trap 'rm -rf "$fc_temp_dir"' EXIT HUP INT TERM

    printf 'Downloading FrameColor...\n'
    curl -fsSL -o "$fc_temp_dir/$fc_archive_name" "$fc_base_url/$fc_archive_name"
    curl -fsSL -o "$fc_temp_dir/$fc_archive_name.sha256" "$fc_base_url/$fc_archive_name.sha256"

    # Compare only the hash field, so the file name inside the .sha256 does not matter.
    fc_expected=$(cut -d ' ' -f 1 "$fc_temp_dir/$fc_archive_name.sha256")
    fc_actual=$(sha256sum "$fc_temp_dir/$fc_archive_name" | cut -d ' ' -f 1)
    if [ -z "$fc_expected" ] || [ "$fc_expected" != "$fc_actual" ]; then
        printf 'Checksum mismatch; the download is incomplete or modified. Nothing was installed.\n' >&2
        exit 1
    fi

    tar -xzf "$fc_temp_dir/$fc_archive_name" -C "$fc_temp_dir"
    [ -f "$fc_temp_dir/framecolor/contrib/install.sh" ] || { printf 'Unexpected archive layout.\n' >&2; exit 1; }

    # A first install is detected before installing; updates keep the user's
    # enable/disable choice.
    fc_first_install=false
    [ -e "$HOME/.local/bin/framecolor" ] || fc_first_install=true

    # Keep the extracted package so uninstall.sh stays available.
    rm -rf "$fc_package_dir"
    mkdir -p "$(dirname "$fc_package_dir")"
    mv "$fc_temp_dir/framecolor" "$fc_package_dir"

    sh "$fc_package_dir/contrib/install.sh"
    if [ "$fc_first_install" = true ]; then
        "$HOME/.local/bin/framecolorctl" enable
    fi

    printf '\nFrameColor is installed. Open "Display Color Adjustment" in the SteamVR dashboard.\n'
    printf 'To update later, run the same command again.\n'
    printf 'To uninstall: sh %s/contrib/uninstall.sh\n' "$fc_package_dir"
}

main "$@" </dev/null
