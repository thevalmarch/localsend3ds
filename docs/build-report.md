# Cross-build report

Build date: 2026-08-24 (Europe/Istanbul)

- Build environment: official `devkitpro/devkitarm:latest` container
- Container digest: `sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5`
- Compiler: `arm-none-eabi-gcc (devkitARM) 16.1.0`
- Build command: `make clean && make -j2`
- Compiler policy: `-Wall -Wextra -Werror -Wstack-usage=4096`
- Output: `LocalSend3DS.3dsx`
- Size: 224,300 bytes
- SHA-256: `4a6f34dfb365c6f66c3656da426329db1b56bfdc237623a7ac3800df1757d16f`
- Host tests: passed under AddressSanitizer and UndefinedBehaviorSanitizer
- `file` identification: `Nintendo 3DS Homebrew Application (3DSX)`
- `3dsxdump`: 43 code pages, 7 rodata pages, 2 data pages, 12 BSS pages

The larger BSS contains the singleton application state. It was deliberately
moved out of the constrained main-thread stack after the first hardware build
exposed a stack overflow; see `crash-2026-08-24.md`.

Native installation of devkitPro pacman v6.0.2 could not be completed because
macOS requires an administrator password for installation under
`/opt/devkitpro`. The downloaded package came directly from the official
devkitPro GitHub release. This does not affect the container-built artifact, but
a native `make` still requires completing that administrator-authenticated
installation.
