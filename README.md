# LocalSend3DS

LocalSend3DS is an **unofficial native LocalSend-compatible client for the
Nintendo 3DS family**. It targets stable LocalSend protocol v2.2. Bidirectional
LAN discovery and HTTP single-file transfers with official LocalSend for macOS
are verified on a real New Nintendo 2DS XL. The application uses a Citro2D
graphical interface designed around the two 3DS screens.

Current code includes:

- a native libctru/Citro2D two-screen application with no terminal UI in normal
  use;
- Wi-Fi/SOC initialization and clean shutdown;
- bounded IPv4 UDP multicast discovery on `224.0.0.167:53317`;
- typed announcement parsing and a fixed-capacity device registry;
- a nonblocking HTTP server with `/register`, `/info`, `/prepare-upload`,
  `/upload`, and `/cancel`;
- a bounded SD-card file browser and discovered-device recipient picker;
- a nonblocking HTTP client using `/prepare-upload`, `/upload`, and `/cancel`;
- bounded 32 KiB SD-to-socket outgoing streaming with 64-bit progress;
- touch and physical-button navigation, approval, rejection, and cancellation;
- secure per-transfer UUID session IDs and file tokens from the 3DS PS service;
- bounded 32 KiB streaming to collision-safe `.part` files under
  `sdmc:/3ds/LocalSend/Downloads/`;
- exact 64-bit size enforcement, incremental SHA-256 verification, and final
  rename only after successful flush/close;
- strict filename/path validation and deterministic collision names;
- cryptographically random per-launch HTTP identity via the 3DS PS service;
- capped per-launch discovery diagnostics at
  `sdmc:/3ds/LocalSend/logs/latest.log`;
- graphical nearby-device, file-browser, recipient, incoming-request, progress,
  result, network-error, and scrollable Settings scenes;
- persistent device-name, Quick Save, and Auto Finish settings stored under
  `sdmc:/3ds/LocalSend/settings.conf`;
- a distinct LocalSend3DS SMDH icon and embedded title/description metadata;
- supported `.3dsx` and optional native `.cia` builds from the same application
  source, with no external `.3dsx` forwarder dependency for the CIA;
- host-side protocol, HTTP fragmentation/chunking, filesystem, session, SHA-256,
  transfer-state, outgoing partial-write/response, registry, and pure UI-model
  tests under ASan/UBSan.

The `/register` server is part of the discovery milestone because current
official LocalSend clients answer UDP announcements over HTTP. HTTPS peers are
shown from their UDP announcement but cannot yet be actively registered with;
this limitation is tracked in [protocol notes](docs/protocol-notes.md).

## Build requirements

Install devkitPro using its official macOS package installer, then install the
`3ds-dev` group:

```sh
sudo dkp-pacman -Syu
sudo dkp-pacman -S 3ds-dev
```

Start a new shell (or source `/opt/devkitpro/3dsvars.sh`) and verify:

```sh
printf '%s\n' "$DEVKITPRO" "$DEVKITARM"
command -v arm-none-eabi-gcc 3dsxtool smdhtool 3dslink
```

Build the `.3dsx`:

```sh
make
```

The optional native CIA build additionally requires compatible `bannertool`
and `makerom` executables. It is separate from the normal `make` target:

```sh
make cia BANNERTOOL=/path/to/bannertool MAKEROM=/path/to/makerom
```

The CIA launches LocalSend3DS directly from HOME Menu and does not chainload or
depend on an external `.3dsx`. Installation requires custom firmware capable of
installing and launching unsigned homebrew CIA packages.

Run platform-independent tests on macOS:

```sh
make -f Makefile.host test
```

Optional deployment is explicit and never part of the normal build:

```sh
make deploy IP=192.168.1.50
```

## Real-hardware receive test

Copy `LocalSend3DS.3dsx` to the SD card and launch it through Homebrew Launcher.
With the console and official LocalSend for macOS on the same Wi-Fi, select one
ordinary file on the Mac and send it to **LocalSend 3DS**. Confirm the request
with A, then verify the final file under `sdmc:/3ds/LocalSend/Downloads/` and
compare SHA-256 hashes. B rejects a pending request and cancels an accepted or
active transfer. See [testing.md](docs/testing.md) for the full checklist.

## Real-hardware outgoing test

TLS/HTTPS is not implemented, so first disable encryption in official
LocalSend for macOS and refresh discovery until the Mac is marked `HTTP` on the
3DS. Press A on the nearby-device screen, browse from `sdmc:/`, select one file,
select the Mac, and press A. Accept on the Mac and compare the received file's
SHA-256 with the SD source. B cancels while preparing or sending.

Received software is always treated as data. LocalSend3DS will never install or
launch transferred `.cia`, `.3dsx`, or other executable files automatically.

## Interface and controls

The top screen presents identity, Wi-Fi state, the active peer, filename,
progress, completion, and human-readable errors. The bottom touch screen owns
the Receive/Send/Settings navigation, nearby-device cards, bounded SD browser,
recipient selection, approval, rejection, and cancellation.

- D-Pad or Circle Pad: move through devices, files, and settings
- A: open, select, accept, send, or dismiss a result
- B: back, reject, or cancel
- Y: refresh discovery
- L/R: change the main Receive/Send/Settings section
- Touch: select cards, tabs, files, and actions directly
- SELECT: toggle the developer status view
- START: exit cleanly

The developer view contains bounded diagnostics; full networking details remain
in `sdmc:/3ds/LocalSend/logs/latest.log`.

The Settings screen keeps General, Network, and About information in one
scrollable list. Device name is editable with the 3DS software keyboard and is
the alias advertised over LocalSend discovery. Quick Save defaults off and can
automatically approve incoming transfers. Auto Finish defaults on and returns
to the appropriate idle screen 2.5 seconds after a successful transfer. Save
folder, HTTP connection mode, and port are intentionally read-only until their
underlying features can be changed safely.

## Branding status

The official LocalSend application and logo were studied at upstream commit
`daa652708fa44261b6805d37802a989e68ad7c7d`. Its Apache-2.0 license does not
grant trademark permission, and no separate logo/trademark policy was found.
The application therefore uses LocalSend's recognizable teal, rounded cards,
simple device-first flows, and progress language without redistributing the
official logo. The included dual-screen transfer mark is original project
artwork. See [branding.md](docs/branding.md) for the audit and source links.

## Status and scope

Bidirectional discovery and HTTP one-file transfer in both directions are
verified with official LocalSend for macOS on a real New Nintendo 2DS XL,
including correct hardware-model reporting and a byte-identical incoming
transfer verified by SHA-256. The graphical UI, Settings persistence, and
native CIA HOME Menu launch have also been tested on that console. Other
Nintendo 3DS-family models and official LocalSend clients on other operating
systems have not yet been verified. See the
[compatibility matrix](docs/compatibility.md).

Transfers are currently limited to one file per session. LocalSend3DS supports
plain HTTP only; TLS/HTTPS is not implemented, so official peers must have
encryption disabled and advertise HTTP for transfers with LocalSend3DS.

LocalSend3DS is licensed under the MIT License. LocalSend is a separate project;
LocalSend3DS is not affiliated with or endorsed by the LocalSend maintainers or
Nintendo. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
and interoperability-reference notices.
