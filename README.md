<p align="center">
  <img src="assets/branding/localsend3ds-icon.svg" width="180" alt="LocalSend3DS logo">
</p>

<h1 align="center">LocalSend3DS</h1>

<p align="center"><em>Unofficial LocalSend client for Nintendo 3DS.</em></p>

LocalSend3DS provides native LocalSend-compatible file transfer between
Nintendo 3DS-family systems and LocalSend devices on the same local network. It
uses stable LocalSend protocol v2.2 and requires no companion application,
custom server, cloud service, or account.

LocalSend3DS is an independent project and is not affiliated with or endorsed
by the LocalSend maintainers or Nintendo.

Download the latest available version from [GitHub Releases](https://github.com/thevalmarch/localsend3ds/releases).

## Features

- Automatic LocalSend discovery over the local network
- Single-file receiving over HTTP and sending to HTTP or HTTPS recipients
- TLS 1.2 mutual TLS and discovered-certificate fingerprint pinning
  for outgoing HTTPS transfers
- Native dual-screen interface with touch and physical-button controls
- Five built-in themes with a persistent selection in Settings
- SD-card file browser and discovered-device recipient selection
- Incoming-transfer approval, Quick Save, cancellation, and progress display
- Safe receive destinations, non-overwriting collision handling, and incoming
  SHA-256 verification when the sender provides a checksum
- Configurable device name and persistent Settings
- Homebrew Launcher `.3dsx` and native HOME Menu `.cia` builds

Received files are stored under:

```text
sdmc:/3ds/LocalSend/Downloads/
```

Transferred `.cia`, `.3dsx`, and other executable files are treated only as
data. LocalSend3DS never installs or launches received software automatically.

## Installation

### Homebrew Launcher (`.3dsx`)

For manual installation, rename the downloaded `.3dsx` file to
`LocalSend3DS.3dsx` and copy it to:

```text
sdmc:/3ds/LocalSend3DS/LocalSend3DS.3dsx
```

Launch LocalSend3DS from Homebrew Launcher. The application icon and metadata
are embedded; no sidecar assets are required.

Universal-Updater may install a versioned `.3dsx` file alongside an older one.
If multiple Homebrew Launcher entries appear, check the version in Settings.
Do not delete `sdmc:/3ds/LocalSend/` when cleaning up older application files;
it contains shared settings and user data.

### HOME Menu (`.cia`)

Install the downloaded `.cia` using a trusted CIA installer on a system running
compatible custom firmware. The CIA is a native build of the same application
and does not require the `.3dsx` to remain on the SD card.

Settings, logs, downloads, and the persistent outgoing-TLS identity are stored
under `sdmc:/3ds/LocalSend/`, independently of CIA installation or replacement.
Back up this directory before upgrading.

## Usage

### Receive a file

1. Launch LocalSend3DS and wait for Wi-Fi initialization.
2. On another LocalSend device, choose **LocalSend 3DS** or the configured
   device name as the recipient.
3. Review the sender, filename, and size on the 3DS, then accept with A or the
   touchscreen. Quick Save can optionally accept incoming requests
   automatically.
4. Find the completed file in `sdmc:/3ds/LocalSend/Downloads/`.

LocalSend3DS advertises an HTTP receive endpoint. An official peer may send to
that endpoint even when the peer's own receive service advertises HTTPS.

### Send a file

1. Open **Send**, browse the SD card, and select one file.
2. Select a discovered recipient. LocalSend3DS uses the HTTP or HTTPS protocol
   advertised by that device.
3. Accept the incoming request on the recipient and wait for both devices to
   report completion.

For HTTPS recipients, LocalSend3DS presents a persistent client certificate,
checks the recipient certificate's validity and self-signature, and pins its
exact SHA-256 fingerprint to the value learned during discovery before sending
HTTP metadata. It never silently downgrades an HTTPS recipient to HTTP. The 3DS
system date and time must be correct for certificate-validity checks.

PIN-protected recipients are not supported. Disable the recipient's PIN before
sending from LocalSend3DS; encryption itself can remain enabled.

### Themes

Choose a theme in **Settings → Theme**:

- **Light** (original appearance)
- **Dark**
- **OLED Black**
- **Yaru Light**
- **Yaru Dark**

Use touch or physical controls to select a theme. The selection is saved across
restarts and shared by `.3dsx` and CIA through the same SD settings file. Light
remains the default for new installations and existing v1.0.0 configurations;
other saved settings are preserved.

OLED Black changes the interface colors; the Nintendo 3DS has LCD screens, not
OLED displays. The Yaru themes use colors adapted from Ubuntu's Yaru palette.
Custom user-made themes are not supported. See the
[third-party notices](THIRD_PARTY_NOTICES.md#yaru-inspired-color-palettes)
for attribution.

### Controls

- D-Pad or Circle Pad: move through lists
- A: select, accept, send, or dismiss
- B: back, reject, or cancel
- Y: refresh nearby devices
- L/R: change Receive, Send, and Settings sections
- Touch: select tabs, cards, files, and actions
- SELECT: toggle the developer status view
- START: exit cleanly

Diagnostic logs are written to `sdmc:/3ds/LocalSend/logs/latest.log` and are
capped per launch. Logs can contain peer names, LAN IP addresses, filenames,
sizes, and certificate-fingerprint prefixes. Review them before sharing.

## Build

Install devkitPro using its supported installation method, then install the
`3ds-dev` package group and the official `3ds-mbedtls` package:

```sh
sudo dkp-pacman -Syu
sudo dkp-pacman -S 3ds-dev 3ds-mbedtls
```

Ensure `DEVKITPRO` and `DEVKITARM` are configured. The validated 3DS build uses
devkitPro's `3ds-mbedtls` 2.28.8 package.

Build the standard `.3dsx`:

```sh
make
```

For host tests, install compatible Mbed TLS 2.28 development libraries. On
macOS, for example:

```sh
brew install mbedtls@2
```

Then run the host tests and theme-palette checks:

```sh
make -f Makefile.host test
python3 -B tests/test_palettes.py
```

The host Mbed TLS dependency is used for testing, not at runtime on the 3DS.

The optional native CIA build additionally requires compatible `bannertool`
and `makerom` executables. The pinned makerom source requires the documented
[external-tool repair](packaging/cia/patches/README.md):

```sh
make cia BANNERTOOL=/path/to/bannertool MAKEROM=/path/to/makerom
```

See [packaging/cia/README.md](packaging/cia/README.md) for native CIA packaging
details and [docs/testing.md](docs/testing.md) for validation procedures.

## Compatibility and known limitations

Both `.3dsx` and native CIA builds have been tested on a New Nintendo 2DS XL,
including a CIA upgrade from v1.0.0 that preserved existing settings. The five
themes and file transfers were also verified on real hardware.

Documented interoperability with official LocalSend clients includes:

- macOS: bidirectional discovery and one-file transfer, including outgoing
  HTTPS/mTLS with fingerprint pinning and persistent client-identity reuse.
- Linux: bidirectional discovery and one-file transfer, including outgoing
  HTTPS/mTLS with fingerprint pinning.
- Android: bidirectional discovery and one-file transfer.

Other Nintendo 3DS-family models and official LocalSend peers on Windows or iOS
have not been covered by the documented hardware tests. See
[docs/compatibility.md](docs/compatibility.md) and [docs/testing.md](docs/testing.md)
for the detailed test matrix and its limitations.

Current limitations:

- LocalSend3DS's incoming server advertises HTTP. HTTPS support applies only
  when LocalSend3DS sends to an HTTPS recipient.
- PIN-protected transfers are not implemented.
- Transfers are limited to one file per session.
- Multiple files, folders, text, clipboard, and trusted-device support are
  not implemented.
- Custom user-made themes are not supported.
- A power loss or process termination can leave a collision-safe `.part` file.
  It is never presented as complete; LocalSend3DS does not delete an unknown
  stale `.part` file because it may belong to the user.
- Cancellation, network interruption, low-storage, lid/sleep, and large-file
  behavior have not received broad real-hardware coverage.

HTTPS protects outgoing transfer traffic and pins the recipient certificate to
the fingerprint in LocalSend discovery metadata. It does not independently
prove a person's identity, and incoming transfers still use HTTP. Quick Save
removes the normal approval prompt, so enable it only on a LAN you trust.

The outgoing-TLS identity includes a private key stored on the SD card at
`sdmc:/3ds/LocalSend/tls-identity.bin`. Protect the SD card and any backups as
you would other application data. If this identity is lost or corrupt beyond
recovery, LocalSend3DS generates a replacement before the next HTTPS send.

## Credits

LocalSend3DS is developed by Volkan 'Val March' Söylemez. It implements
interoperability with the separate [LocalSend](https://localsend.org/) project
and uses the devkitPro Nintendo 3DS homebrew toolchain and libraries.

The LocalSend3DS logo is original project artwork. Official LocalSend source
code and logo artwork are not bundled. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[docs/branding.md](docs/branding.md) for attribution and branding details.

## License

LocalSend3DS's original source code and project artwork are available under the
[MIT License](LICENSE).

Copyright © 2026 Volkan 'Val March' Söylemez.
