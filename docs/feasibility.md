# Technical feasibility analysis

> Historical development snapshot: this document began as the phase-zero
> feasibility analysis and retains early constraints and then-future-looking
> TLS notes. Since this snapshot, bidirectional one-file transfer, outgoing
> HTTPS/mTLS, the Citro2D graphical UI, Settings persistence, and native CIA
> HOME Menu launch have been exercised on a real New Nintendo 2DS XL. Current
> verification status is maintained in `compatibility.md` and `testing.md`.

Research in this snapshot was refreshed on 2026-08-24.

## Host toolchain audit

The development Mac has Xcode command-line tools, GNU Make, Git, Python, and
Homebrew. It does **not** currently have a native Nintendo 3DS toolchain:

- `DEVKITPRO` and `DEVKITARM` are unset;
- `arm-none-eabi-gcc`, `3dsxtool`, `smdhtool`, `dkp-pacman`, and `3dslink` are
  not available;
- `/opt/devkitpro` is absent.

The supported remediation is the official devkitPro macOS package installer,
followed by `sudo dkp-pacman -S 3ds-dev`. This requires administrator
authentication, which was unavailable to the automated shell. A custom compiler
toolchain was not used. As an official fallback, the project was built in
`devkitpro/devkitarm:latest` with devkitARM GCC 16.1.0. The resulting 3DSX was
successfully decoded by `3dsxdump`. `scripts/check-toolchain.sh` captures the
native-host checks. Current builds additionally require the official
`3ds-mbedtls` package; see the root README rather than this historical snapshot
for current installation commands.

## Networking

libctru exposes BSD-style IPv4 TCP and UDP sockets after `socInit()` with a
page-aligned shared buffer. Its headers and implementation provide `bind`,
`listen`, `accept`, `send`, `recv`, `sendto`, `recvfrom`, `setsockopt`,
`fcntl(O_NONBLOCK)`, `IP_ADD_MEMBERSHIP`, multicast TTL/loop options, socket
timeouts, and `SOCU_GetIPInfo`. The official socket example demonstrates a
nonblocking server in the application loop.

One libctru/SOC:u compatibility exception is now explicit. A writable
nonblocking TCP socket can retain raw `SO_ERROR == -26`, SOC:u's untranslated
`EINPROGRESS` index. The outgoing client therefore uses a 3DS-only follow-up
`connect()` probe and recognizes `EISCONN` as completion; ordinary POSIX builds
continue using `SO_ERROR`. Socket creation, `fcntl`, destination, initial
connect, readiness, raw option value, and completion probe are logged with
bounded public metadata.

This makes TCP, UDP, bounded nonblocking polling, and obtaining the console's
IPv4 address technically feasible. The current implementation initializes a
1 MiB SOC buffer, joins `224.0.0.167:53317` on the active IPv4 interface, and
polls a strict maximum of eight datagrams per frame.

Joining, sending, and packet reception are verified on a real New Nintendo 2DS
XL with official LocalSend for macOS. Other access points, other 3DS-family
models, and sleep/reconnect behavior remain unverified. IPv6 discovery is not
part of the stable protocol baseline and is deferred.

## HTTP

A small server is practical because the MVP needs only a few routes and one
active transfer session. The server must remain incremental because TCP may
fragment headers and bodies. The current server:

- is nonblocking and polled from the UI loop;
- caps simultaneous clients at four;
- caps headers at 2 KiB and metadata bodies at 16 KiB;
- requires `Content-Length` for metadata POSTs and supports Content-Length or
  strict chunked framing for `/upload`;
- rejects duplicate/conflicting framing and malformed lines;
- uses stage-specific decision, transfer, and metadata timeouts;
- handles partial sends and fragmented receives.

File bodies use one separate 32 KiB BSS buffer and are never accumulated in the
metadata buffer. A worker remains a later option if hardware profiling shows
bounded SD writes cause unacceptable UI stalls.

## HTTPS and TLS (historical investigation)

libctru's SSL:C and HTTP:C services are useful client APIs but are not a clear
fit for a self-signed, mutual-TLS LocalSend server. devkitPro publishes the
`3ds-mbedtls` port (currently 2.28.8 in its package repository), which supports
both TLS roles and is the preferred investigation path.

The stable LocalSend v2.2 identity is the uppercase hexadecimal SHA-256 hash of
the DER certificate when HTTPS is used. Current official implementation
behavior additionally requires client certificates for native HTTPS peers.
The early planned identity was a persistent RSA-2048 self-signed certificate with
`CN=LocalSend User`, matching current upstream behavior, generated once and
stored under `sdmc:/3ds/LocalSend/config/`. The implemented identity instead
uses `sdmc:/3ds/LocalSend/tls-identity.bin`. The peer certificate must be
self-signature/time validated and pinned to the fingerprint learned during
discovery. Globally disabling validation is not acceptable.

The first discovery build advertised `http`, not fake `https`. The eventual
implementation retained that truthful HTTP receive advertisement and added a
separate outgoing TLS-over-SOC transport. It uses the official devkitPro
`3ds-mbedtls` 2.28.8 package, PS-backed entropy, a persistent RSA-2048 client
identity, peer self-signature/time validation, and exact leaf-DER fingerprint
pinning. Outgoing HTTPS/mTLS interoperability was later verified on real
hardware with official LocalSend on macOS and Linux.

## JSON

Discovery and register payloads are flat and currently use a project-owned,
bounded parser. It validates JSON syntax, UTF-8, escapes/surrogate pairs,
required and duplicate fields, ports, enum values, lengths, and protocol major
version without heap allocation.

The project-owned parser now handles one bounded `prepare-upload` file entry,
nested unknown values, u64 sizes, optional SHA-256, duplicate/missing fields,
ID-map consistency, UTF-8, and depth limits without heap allocation. No JSON
dependency was added.

## Filesystem

libctru mounts the SD archive for normal homebrew startup and supports stdio
paths such as `sdmc:/3ds/LocalSend/Downloads/`. The receive implementation uses
bounded 32 KiB chunks, incremental hash, exclusive-created `filename.part`,
flush/close, and final rename only after exact size/hash verification. Collision
names are generated within the fixed directory. Preflight free-space display is
still deferred; write/flush failures are handled and never finalize the file.

Filename handling rejects absolute paths, drive-like prefixes, `.`/`..`,
slashes, backslashes, control characters, empty names, and overlong UTF-8. A
sanitized basename is never concatenated before the destination directory has
been fixed. Directory metadata and Unicode normalization remain hardware/client
compatibility questions.

## UI, concurrency, memory, and sleep

Citro2D now renders a normal graphical interface on both screens. The system
font and primitive shapes avoid texture-cache growth; UI model helpers remain
host-testable. Networking is bounded and nonblocking in the frame loop. The
receive MVP does at most one bounded socket read and SD write per connection
update; hardware profiling will decide whether a worker is needed.

Initial hard bounds are 16 peers, four connections, 2 KiB discovery datagrams,
2 KiB request headers, 16 KiB metadata bodies, and one reusable 32 KiB transfer
buffer. Buffer-size benchmarking remains a hardware task.

libctru exposes APT sleep hooks and configuration. The safe initial transfer
policy is to prevent sleep while a transfer owns an open partial file, restore
normal sleep afterward, and treat unavoidable suspension/network loss as a
failed transfer. Actual lid behavior is unresolved until hardware testing.

## Proposed dependencies

| Dependency | Phase | Reason and decision |
|---|---:|---|
| devkitARM + libctru (`3ds-dev`) | now | Official compiler/runtime and 3DS APIs; required. |
| Citro2D/Citro3D (`3ds-dev`) | now | Lightweight hardware-accelerated 2D UI, system-font text, and primitive drawing. |
| libc/newlib socket and stdio APIs | now | Enough for discovery, HTTP, and streamed files. |
| `3ds-mbedtls` 2.28.8 (Apache-2.0) | now | Official devkitPro TLS/X.509/RSA dependency used by the outgoing HTTPS client. |

No HTTP framework, C++ runtime, or desktop library is linked. Citro2D/Citro3D
come from the official devkitPro `3ds-dev` group; no graphics code is vendored.

## Feasibility conclusion

No platform blocker was found for IPv4 discovery, a small HTTP server, bounded
file streaming, SD storage, random tokens, or a Citro2D interface. Discovery,
one-file transfer in both directions, the graphical shell, and outgoing mutual
TLS were subsequently exercised on real hardware. Incoming HTTPS serving,
PIN-protected transfers, broader hardware coverage, and the stress/failure
matrix remain outside this historical feasibility milestone.

Sources: [LocalSend protocol v2.2](https://github.com/localsend/protocol),
[current LocalSend core](https://github.com/localsend/localsend/tree/main/packages/core/src),
[devkitPro setup](https://devkitpro.org/wiki/Getting_Started/devkitARM),
[libctru](https://github.com/devkitPro/libctru), and
[devkitPro 3DS mbedTLS package](https://github.com/devkitPro/pacman-packages/tree/master/3ds/mbedtls).
