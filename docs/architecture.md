# Architecture

LocalSend3DS is split by responsibility and keeps protocol types independent
from libctru wherever practical.

```text
main / app scenes / input / Citro2D dual-screen UI
          |             |                 |
     discovery      HTTP server     outgoing HTTP/HTTPS client
          |         incoming flow    transport + TLS identity
          |             |                 |
     device registry    |           SD file browser + stream
          \             |                 /
       typed LocalSend JSON + bounded HTTP state machines
                         |
              POSIX-compatible libctru sockets + SD
```

## Current implementation

This branch prepares unreleased v1.1.0. The architecture below describes its
source implementation, not completed validation of final release packages.

- `main.c` owns process entry only.
- `app.c` owns lifecycle, the small scene state, input, and service ordering.
- `ui.c` owns Citro2D lifecycle, both-screen presentation, touch hit-testing,
  and friendly error presentation. It never manipulates sockets or files.
- `ui_model.c` owns platform-independent rectangle, list-window, percentage,
  and byte-size helpers, plus theme-picker cursor/window and touch-mapping
  logic used by the renderer and host tests.
- `settings.c` owns defaults, validation, SD persistence/recovery and the stable
  theme identifiers shared by the application and renderer.
- `network.c` owns SOC shared memory and local IPv4 initialization.
- `identity.c` detects the hardware model and uses the PS service for random
  identity bytes.
- `discovery.c` owns the UDP socket, multicast membership, announcement bursts,
  receive limits, and discovery counters.
- `http_server.c` owns bounded incremental TCP connections, discovery routes,
  upload routing, strict query/framing validation, and chunked decoding. It has
  no graphics calls.
- `localsend_protocol.c` parses and serializes typed LocalSend data without any
  3DS dependency.
- `device_registry.c` owns deduplication, updates, capacity, and expiry without
  any 3DS dependency.
- `transfer.c` owns the single incoming session state machine, authorization,
  exact size/checksum accounting, partial-file cleanup, and final commit.
- `filesystem.c` owns directory creation, filename validation, and collision
  paths; `sha256.c` is an incremental platform-independent implementation.
- `secure_random.c` uses PS random bytes on 3DS and host system entropy only in
  native tests.
- `file_browser.c` owns a fixed-capacity SD directory view and path navigation.
- `http_response.c` incrementally parses bounded Content-Length, chunked, and
  connection-delimited HTTP responses.
- `outgoing_transfer.c` owns prepare/upload/cancel states, response credentials,
  32 KiB file streaming, progress, and cancellation without depending on the
  wire transport.
- `outgoing_transport.c` provides the shared nonblocking HTTP/HTTPS socket
  interface. HTTPS uses TLS 1.2 mutual TLS and accepts a peer only after
  validity, self-signature, and exact leaf-DER fingerprint checks succeed.
- `tls_identity.c` generates, validates, persists, and recovers the RSA-2048
  LocalSend3DS client certificate and private key used by both application
  package formats.

The application remains single-threaded: every socket is nonblocking and each
frame performs bounded work. The UI thread never waits for remote approval or
an entire file operation.

## Graphical UI

`LsApp` owns one small `LsUi` containing Citro2D targets and a fixed 4,096-glyph
text buffer. The renderer reads application/protocol state but cannot mutate
networking. Touch hit-testing returns semantic actions; `app.c` maps those onto
the same state transitions used by A/B/Y and D-Pad/Circle Pad input. This keeps
touch and buttons behaviorally equivalent.

Normal rendering never exposes raw socket counters or errno values. SELECT
opens a developer status view, and the capped SD log remains the source of
detailed diagnostics. The refresh action uses the same rounded primitive button
and system-font treatment as the rest of the interface; cards, device/file
icons, the LocalSend3DS mark, and progress bars remain lightweight Citro2D
primitives.

Settings are represented by a small platform-independent `LsSettings` model.
Changes are written immediately through a temporary file to
`sdmc:/3ds/LocalSend/settings.conf`; startup recovers a valid backup when the
primary file is malformed, then falls back to conservative defaults if neither
copy is valid. The editable alias is copied into the announced `LsDevice`
identity. Quick Save invokes the same secure prepare-upload approval/session
path as manual acceptance, and Auto Finish only dismisses successful terminal
states after a fixed 2.5-second display interval.

### Runtime themes (unreleased v1.1.0)

`LsSettings.theme` is the authoritative theme ID. The renderer resolves it once
per frame to an immutable `LsUiPalette` and uses that palette for both screens.
The 13 roles cover backgrounds, normal/selected surfaces, text, controls,
borders and status colors. Light, Dark, OLED Black, Yaru Light and Yaru Dark
use the same component geometry and rendering paths, without new textures,
allocations, external theme files or per-frame SD reads. The former compile-time
preview flags are rejected so they cannot override saved settings.

The Settings picker displays three entries at a time. Its cursor determines a
bounded window; rendering and touch mapping share the row geometry and window
calculation. A separate Saved marker identifies the active theme. A/touch
confirmation applies a candidate only after the existing SD-safe save succeeds;
B/Back discards the candidate, and a save failure retains the prior theme.

Configuration remains `version=1`. The optional `theme` field accepts `light`,
`dark`, `oled`, `yaru_light` or `yaru_dark`. Missing/unknown values select the
original Light palette while preserving other valid preferences. Duplicate
theme fields are structural corruption and use the existing backup/default
recovery policy. v1.0.0 ignores the unknown field when reading a newer file;
saving with v1.0.0 drops the theme field. No explicit migration is required.

Yaru-inspired palettes adapt verified color references to the shared roles;
they do not import Flutter/Yaru implementations or assets. See
[third-party notices](../THIRD_PARTY_NOTICES.md#yaru-inspired-color-palettes).

The persistent outgoing TLS identity is stored separately at
`sdmc:/3ds/LocalSend/tls-identity.bin`. It uses the same SD path from the 3DSX
and CIA builds and survives same-title CIA updates. Its key material is never
written to the diagnostic log. A valid backup is recovered when possible;
otherwise a missing or corrupt identity is regenerated. Identity unavailability
disables HTTPS sending without changing the plain-HTTP path.

## Incoming transfer path

The first receive slice remains single-threaded and nonblocking at the socket
layer. At most one 32 KiB body chunk is received and written during a connection
update; all large buffers live in the BSS-owned `LsApp`, not on the constrained
main-thread stack. This avoids synchronization complexity while bounding work
and RAM. Hardware measurements will determine whether later transfers benefit
from a dedicated worker.

One `LsIncomingTransfer` owns one untrusted file ID, random token, original
display name, sanitized final path, exclusive-created `.part` path,
expected/received 64-bit sizes, and incremental SHA-256 context. Only an
accepted session with matching session/file/token and peer IP may receive bytes.

## Outgoing transfer path

One `LsOutgoingTransfer` owns a copied peer identity, one selected SD path,
random file ID, bounded request/response buffers, remote session/token, file
handle, 32 KiB stream buffer, and 64-bit sent/expected counters. Prepare and
upload use separate HTTP/1.1 connections over the peer's advertised HTTP or
HTTPS transport with exact Content-Length. HTTPS connections independently
perform mutual TLS and fingerprint pinning before any HTTP bytes are sent.
Partial writes advance only the acknowledged portion of each buffer. Incoming
prepare requests receive `409` while an outgoing transfer is active, preventing
two constrained transfer paths from running concurrently while discovery and
registration continue.

## Limits and ownership

All network-provided strings have capacities, registries have fixed maximums,
and request parsers receive explicit byte lengths. Socket/file handles have one
owner and close on every terminal path. UI state is only changed on the main
thread. File contents never enter metadata structures.

## Repository structure

```text
README.md                 current behavior, build, hardware test
LICENSE                   MIT project license
Makefile                  devkitPro 3DSX build
Makefile.host             native pure/portable tests
include/                  public module contracts and central limits
source/                   module implementations
tests/                    host unit/integration tests
docs/                     feasibility, protocol, testing, compatibility
scripts/                  developer environment checks
.github/workflows/        host and cross-build CI
assets/                   canonical original branding source assets
packaging/cia/            optional native CIA configuration and asset tools
```
