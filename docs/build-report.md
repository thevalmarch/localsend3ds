# Historical cross-build report — 2026-08-25

This document records a specific development build and its environment. Its
artifact sizes and hashes are retained for engineering history; they are not
current release artifacts or v1.0.0 release checksums. Use the published
release checksums, not this report, for current artifacts.

This report predates the outgoing HTTPS/mTLS implementation and its
`3ds-mbedtls` dependency. Its binary sizes, linked-library list, HTTP-only
outgoing description, and validation results must not be used to describe the
current release candidate.

Build date: 2026-08-25 (Europe/Istanbul)

- Build environment: official `devkitpro/devkitarm:latest` container
- Container digest: `sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5`
- Compiler: `arm-none-eabi-gcc (devkitARM) 16.1.0`
- Toolchain packages: `devkitARM r68-1`, `libctru 2.7.0-1`,
  `citro2d 1.7.0-1`, `citro3d 1.7.1-2`
- Build command: `make clean && make -j2`
- Compiler policy: `-Wall -Wextra -Werror -Wstack-usage=4096`
- Output: `LocalSend3DS.3dsx`
- Size: 323,892 bytes
- SHA-256: `9b59cb4d3c08a802dabe59c7b0cf6f7245e13df1f8958df1da378e4f323f96cd`
- Host tests: passed under dedicated AddressSanitizer, dedicated
  UndefinedBehaviorSanitizer, and combined sanitizer runs
- Static analysis: Clang analyzer completed without findings
- `file` identification: `Nintendo 3DS Homebrew Application (3DSX)`
- `3dsxdump`: 65 code pages, 9 rodata pages, 2 data pages, 54 BSS pages

The 44-byte 3DSX extended header points to a 14,016-byte SMDH at byte offset
309,876. That embedded region matches `LocalSend3DS.smdh` byte-for-byte and ends
at the 3DSX EOF. Its parsed metadata is:

- short title: `LocalSend3DS`
- long description: `Unofficial LocalSend client for Nintendo 3DS`
- author: `Val March`
- icon input: generated 48x48 RGBA `icon.png`, SHA-256
  `4f697c820b21d07c9d9805b17a829f987cc8a760b1533d41adb7a34eee711839`

The UI replacement links Citro2D/Citro3D from the official `3ds-dev` package,
uses a fixed system-font glyph buffer, and draws its remaining graphics as
primitives. No runtime asset files are loaded from SD. The larger code/BSS
figures reflect the graphical renderer plus the existing singleton application
state and bounded streaming buffers. Large state remains outside the main
thread stack, and the stack-usage compiler gate remains enabled.

The approved refresh control is a 76x30 rounded secondary `Refresh` text button,
drawn with the same Citro2D primitives and system font as the application's other
buttons. Its 84x36 touch target exceeds the visual bounds; a held touch inverts
the pale-mint/dark-teal colors to teal/white. Physical Y retains the same
discovery action and is documented under Settings/About controls. The obsolete
refresh sprite sheet and texture build path have been removed. Filename labels
are measured with Citro2D's system-font metrics and ellipsized to their actual
pixel bounds, preserving the final extension where it fits. A separate fixed
measurement buffer prevents width probes from consuming the frame's draw text
buffer.

All rounded-button and bottom-navigation labels use one measured centering path.
Citro2D supplies the scaled text line-box height, which is centered within the
control's actual bounds instead of relying on a fixed top offset.
Standalone bottom action rows share an eight-pixel bottom margin; screens with
the persistent navigation bar retain their separate content-area placement.
The Settings tab is a four-row scrolling General/Network/About list. Device
name, Quick Save, and Auto Finish are persisted immediately to
`sdmc:/3ds/LocalSend/settings.conf`; save folder, HTTP mode, and port remain
explicitly read-only. Toggle activation is edge-triggered for physical A and
one-shot for touch, so a held button cannot issue repeated settings writes. A
single larger sticky heading above the list identifies the selected General,
Network, or About section. Settings replacement avoids replace-existing rename
semantics on SDMC: it writes `.tmp`, moves the prior file to `.bak`, promotes
the temporary file into the empty final path, restores the backup after a
promotion failure, and recovers a valid backup after an interrupted update.
The developer/network view is opened through Advanced.

The artifact is a normal statically linked ARM 3DSX. Docker is only the build
host environment; the application has no Docker or macOS runtime dependency.
For this build, `LocalSend3DS.3dsx` is the only file required on the SD card.
The SMDH icon/metadata are embedded, and the Downloads/log directories are
created at runtime.

Native installation of devkitPro pacman v6.0.2 could not be completed because
macOS requires an administrator password for installation under
`/opt/devkitpro`. The official container build uses the same devkitARM, libctru,
Citro2D, Citro3D, 3dsxtool, and smdhtool packages and produces a standard
hardware artifact.

The protocol behavior underneath the renderer was unchanged: startup,
discovery, hardware-model identity, incoming/outgoing HTTP sessions, token
validation, bounded file streaming, and the SOC:u nonblocking-connect workaround
remain in their existing modules. The user has verified one-file transfer in
both directions on a real New Nintendo 2DS XL. Subsequent real-hardware testing
also verified the graphical interface, Settings persistence, and native CIA
HOME Menu launch; see `compatibility.md` and `testing.md` for the current
verification record.

The application reports version `v1.0.0`. Its LocalSend identity uses the
standard protocol `desktop` device type while continuing to advertise the exact
detected Nintendo hardware model independently. Settings/About credits
`Volkan 'Val March' Söylemez`; embedded SMDH metadata intentionally retains the
short author name `Val March`.
