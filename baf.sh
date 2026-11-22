#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

IMG=boot-uefi.img
BUILD_LOG=build.last.log
PARTITION=boot
FV_MIN_FREE=4096

red()    { printf '\033[1;31m%s\033[0m\n' "$*"; }
yellow() { printf '\033[1;33m%s\033[0m\n' "$*"; }
blue()   { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }

ask_yn() {
    [[ -t 0 ]] || return 1
    local ans
    read -r -p "$1 [y/N] " ans
    [[ "$ans" =~ ^[yY]$ ]]
}

die() {
    red "ERROR: $*" >&2
    exit 1
}

die_build() {
    red "ERROR: $*" >&2
    if [[ -s "$BUILD_LOG" ]] && ask_yn "open $BUILD_LOG via vi?"; then
        vi "$BUILD_LOG"
    fi
    exit 1
}

command -v fastboot >/dev/null || die "cant find fastboot in PATH"
[[ -x ./build.sh ]]            || die "./build.sh is not found/no +x"

blue "BUILD!!!"
rm -f "$IMG"
if ! ./build.sh 2>&1 | tee "$BUILD_LOG"; then
    die_build "build failed"
fi

[[ -s "$IMG" ]] || die_build "$IMG creation failed/corrupt"

free=$(grep -E '^FVMAIN \[' "$BUILD_LOG" \
       | sed -E 's/.* ([0-9]+) \(0x[0-9a-fA-F]+\) free.*/\1/' || true)

if [[ "$free" =~ ^[0-9]+$ ]]; then
    if (( free < FV_MIN_FREE )); then
        yellow "WARN: FVMAIN is almost full!~ $free bytes free ( $FV_MIN_FREE)"
    else
        blue "FVMAIN: $free bytes free"
    fi
else
    yellow "WARN: cant parse free FVMAIN space"
fi
blue "FASTBOOT CHECK!!!"
[[ -n "$(fastboot devices)" ]] || die "no device detected!!! (usb, mode, udev, etc.)"
if [[ "${ASSUME_YES:-0}" != 1 ]]; then
    ask_yn "Flash $IMG to '$PARTITION'?" || die "Cancel"
fi
blue "FLASH!!!"
fastboot flash "$PARTITION" "$IMG" || die "FLASH FAIL!!!"
blue "RESET!!!"
fastboot reboot
blue "======Completed!!!======"
