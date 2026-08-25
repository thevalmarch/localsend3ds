# Architecture

LocalSend3DS is split by responsibility and keeps protocol types independent
from libctru wherever practical.

```text
main / app scenes / input / Citro2D dual-screen UI
          |             |                 |
     discovery      HTTP server      outgoing HTTP client
          |         incoming flow      prepare/upload/cancel
          |             |                 |
     device registry    |           SD file browser + stream
          \             |                 /
       typed LocalSend JSON + bounded HTTP state machines
                         |
              POSIX-compatible libctru sockets + SD
```

## Current slice

- `main.c` owns process entry only.
- `app.c` owns lifecycle, the small scene state, input, and service ordering.
- `ui.c` owns Citro2D lifecycle, both-screen presentation, touch hit-testing,
  and friendly error presentation. It never manipulates sockets or files.
- `ui_model.c` owns platform-independent rectangle, list-window, percentage,
  and byte-size helpers used by the renderer and host tests.
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
- `outgoing_transfer.c` owns nonblocking connect/write/read states, prepare
  response credentials, 32 KiB file streaming, progress, and cancellation.

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
`sdmc:/3ds/LocalSend/settings.conf`; startup falls back to conservative defaults
if the file is absent or malformed. The editable alias is copied into the
announced `LsDevice` identity. Quick Save invokes the same secure prepare-upload
approval/session path as manual acceptance, and Auto Finish only dismisses
successful terminal states after a fixed 2.5-second display interval.

## Receive-MVP extension

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

## Outgoing-MVP extension

One `LsOutgoingTransfer` owns a copied peer identity, one selected SD path,
random file ID, bounded request/response buffers, remote session/token, file
handle, 32 KiB stream buffer, and 64-bit sent/expected counters. Prepare and
upload use separate HTTP/1.1 connections with exact Content-Length. Partial
socket writes advance only the acknowledged portion of each buffer. Incoming
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
assets/                   later icon/UI assets
```
