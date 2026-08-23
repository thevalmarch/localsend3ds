# Architecture

LocalSend3DS is split by responsibility and keeps protocol types independent
from libctru wherever practical.

```text
main / app state / input / UI
              |
              v
       fixed application events       (introduced with transfer worker)
          /             \
discovery + HTTP       transfer session manager
server/client          /        |        \
       |          incoming   outgoing   cancellation
       v              |          |
typed LocalSend protocol + HTTP parser/state
       |              |          |
device registry    filesystem   incremental SHA-256
       \              |          /
        network sockets / TLS abstraction
                   |
             libctru + SD archive
```

## Current slice

- `main.c` owns process entry only.
- `app.c` owns lifecycle, the small scene state, input, and service ordering.
- `ui.c` owns both-screen presentation and never manipulates sockets.
- `network.c` owns SOC shared memory and local IPv4 initialization.
- `identity.c` detects the hardware model and uses the PS service for random
  identity bytes.
- `discovery.c` owns the UDP socket, multicast membership, announcement bursts,
  receive limits, and discovery counters.
- `http_server.c` owns bounded incremental TCP connections and the two discovery
  routes. It has no graphics calls.
- `localsend_protocol.c` parses and serializes typed LocalSend data without any
  3DS dependency.
- `device_registry.c` owns deduplication, updates, capacity, and expiry without
  any 3DS dependency.

The discovery milestone deliberately stays single-threaded: every socket is
nonblocking and every frame has a strict work bound. This is simpler and easier
to validate than introducing synchronization before transfers exist.

## Receive-MVP extension

Add `http_parser`, `transfer`, `incoming_transfer`, `filesystem`, `crypto`,
`event_queue`, `settings`, and `logger` modules. One transfer worker may block
with timeouts while streaming, but it communicates only immutable request data
and bounded progress/error events. The UI transitions explicitly through
`INCOMING_REQUEST`, `TRANSFER_RECEIVING`, and terminal states.

One `TransferSession` owns a bounded file array. Each entry owns its untrusted
ID, random token, original display name, sanitized final path, `.part` path,
expected/received 64-bit sizes, and optional incremental SHA-256 context. Only
an accepted session from the same peer IP may enter `TRANSFERRING`.

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

