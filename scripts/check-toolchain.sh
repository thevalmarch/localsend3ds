#!/bin/sh
set -eu

missing=0

find_tool() {
    tool_name=$1
    if command -v "$tool_name" >/dev/null 2>&1; then
        command -v "$tool_name"
        return 0
    fi
    if [ -n "${DEVKITARM:-}" ] && [ -x "$DEVKITARM/bin/$tool_name" ]; then
        printf '%s\n' "$DEVKITARM/bin/$tool_name"
        return 0
    fi
    if [ -n "${DEVKITPRO:-}" ] && [ -x "$DEVKITPRO/tools/bin/$tool_name" ]; then
        printf '%s\n' "$DEVKITPRO/tools/bin/$tool_name"
        return 0
    fi
    return 1
}

if [ -z "${DEVKITPRO:-}" ] || [ ! -d "${DEVKITPRO:-/nonexistent}" ]; then
    echo "error: DEVKITPRO is unset or is not a directory" >&2
    missing=1
fi
if [ -z "${DEVKITARM:-}" ] || [ ! -d "${DEVKITARM:-/nonexistent}" ]; then
    echo "error: DEVKITARM is unset or is not a directory" >&2
    missing=1
fi

for tool in arm-none-eabi-gcc 3dsxtool smdhtool; do
    if ! find_tool "$tool" >/dev/null; then
        echo "error: required tool not found: $tool" >&2
        missing=1
    fi
done

if ! find_tool 3dslink >/dev/null; then
    echo "warning: optional deployment tool not found: 3dslink" >&2
fi

if [ "$missing" -ne 0 ]; then
    echo "Install devkitPro's official macOS package and the 3ds-dev group." >&2
    exit 1
fi

echo "DEVKITPRO=$DEVKITPRO"
echo "DEVKITARM=$DEVKITARM"
compiler=$(find_tool arm-none-eabi-gcc)
tool_3dsx=$(find_tool 3dsxtool)
"$compiler" --version | sed -n '1p'
"$tool_3dsx" --help >/dev/null 2>&1 || true
echo "Nintendo 3DS toolchain check passed."
