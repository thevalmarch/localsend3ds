#!/bin/sh
set -eu

missing=0

if [ -z "${DEVKITPRO:-}" ] || [ ! -d "${DEVKITPRO:-/nonexistent}" ]; then
    echo "error: DEVKITPRO is unset or is not a directory" >&2
    missing=1
fi
if [ -z "${DEVKITARM:-}" ] || [ ! -d "${DEVKITARM:-/nonexistent}" ]; then
    echo "error: DEVKITARM is unset or is not a directory" >&2
    missing=1
fi

for tool in arm-none-eabi-gcc 3dsxtool smdhtool; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: required tool not found: $tool" >&2
        missing=1
    fi
done

if ! command -v 3dslink >/dev/null 2>&1; then
    echo "warning: optional deployment tool not found: 3dslink" >&2
fi

if [ "$missing" -ne 0 ]; then
    echo "Install devkitPro's official macOS package and the 3ds-dev group." >&2
    exit 1
fi

echo "DEVKITPRO=$DEVKITPRO"
echo "DEVKITARM=$DEVKITARM"
arm-none-eabi-gcc --version | sed -n '1p'
3dsxtool --help >/dev/null 2>&1 || true
echo "Nintendo 3DS toolchain check passed."

