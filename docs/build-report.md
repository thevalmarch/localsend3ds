# Cross-build report

Build date: 2026-08-24 (Europe/Istanbul)

- Build environment: official `devkitpro/devkitarm:latest` container
- Container digest: `sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5`
- Compiler: `arm-none-eabi-gcc (devkitARM) 16.1.0`
- Build command: `make clean && make -j2`
- Compiler policy: `-Wall -Wextra -Werror`
- Output: `LocalSend3DS.3dsx`
- Size: 224,304 bytes
- SHA-256: `5ec24c9cc39aeac32b23526cfd0d81a4d83467056644fbb22bb1625d26106daa`
- Host tests: passed under AddressSanitizer and UndefinedBehaviorSanitizer
- `file` identification: `Nintendo 3DS Homebrew Application (3DSX)`
- `3dsxdump`: 43 code pages, 7 rodata pages, 2 data pages, 3 BSS pages

Native installation of devkitPro pacman v6.0.2 could not be completed because
macOS requires an administrator password for installation under
`/opt/devkitpro`. The downloaded package came directly from the official
devkitPro GitHub release. This does not affect the container-built artifact, but
a native `make` still requires completing that administrator-authenticated
installation.
