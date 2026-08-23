# Technical feasibility analysis

Status: phase-zero source review and host tests complete; Nintendo 3DS hardware
validation pending. Research was refreshed on 2026-08-24.

## Host toolchain audit

The development Mac has Xcode command-line tools, GNU Make, Git, Python, and
Homebrew. It does **not** currently have the Nintendo 3DS toolchain:

- `DEVKITPRO` and `DEVKITARM` are unset;
- `arm-none-eabi-gcc`, `3dsxtool`, `smdhtool`, `dkp-pacman`, and `3dslink` are
  not available;
- `/opt/devkitpro` is absent.

The supported remediation is the official devkitPro macOS package installer,
followed by `sudo dkp-pacman -S 3ds-dev`. A custom compiler toolchain is not an
acceptable substitute. `scripts/check-toolchain.sh` captures the checks. The
current source therefore has host-test evidence but not a local `.3dsx` build.

## Networking

libctru exposes BSD-style IPv4 TCP and UDP sockets after `socInit()` with a
page-aligned shared buffer. Its headers and implementation provide `bind`,
`listen`, `accept`, `send`, `recv`, `sendto`, `recvfrom`, `setsockopt`,
`fcntl(O_NONBLOCK)`, `IP_ADD_MEMBERSHIP`, multicast TTL/loop options, socket
timeouts, and `SOCU_GetIPInfo`. The official socket example demonstrates a
nonblocking server in the application loop.

This makes TCP, UDP, bounded nonblocking polling, and obtaining the console's
IPv4 address technically feasible. The current implementation initializes a
1 MiB SOC buffer, joins `224.0.0.167:53317` on the active IPv4 interface, and
polls a strict maximum of eight datagrams per frame.

Source support is not evidence that multicast works on every 3DS access point.
Joining, sending, packet reception, AP multicast filtering, and behavior after
sleep must be tested on the New Nintendo 2DS XL. IPv6 discovery is not part of
the stable protocol baseline and is deferred.

## HTTP

A small server is practical because the MVP needs only a few routes and one
active transfer session. The server must remain incremental because TCP may
fragment headers and bodies. The current server:

- is nonblocking and polled from the UI loop;
- caps simultaneous clients at four;
- caps headers and metadata bodies at 2 KiB each;
- requires `Content-Length` for POST;
- rejects duplicate `Content-Length`, transfer encoding, and malformed lines;
- times out idle connections after five seconds;
- handles partial sends and fragmented receives.

Before file upload, the connection model will move to a transfer worker or a
small I/O worker with events back to the UI. File bodies must never use the
metadata buffer.

## HTTPS and TLS

libctru's SSL:C and HTTP:C services are useful client APIs but are not a clear
fit for a self-signed, mutual-TLS LocalSend server. devkitPro publishes the
`3ds-mbedtls` port (currently 2.28.8 in its package repository), which supports
both TLS roles and is the preferred investigation path.

The stable LocalSend v2.2 identity is the uppercase hexadecimal SHA-256 hash of
the DER certificate when HTTPS is used. Current official implementation
behavior additionally requires client certificates for native HTTPS peers.
The planned identity is a persistent RSA-2048 self-signed certificate with
`CN=LocalSend User`, matching current upstream behavior, generated once and
stored under `sdmc:/3ds/LocalSend/config/`. The peer certificate must be
self-signature/time validated and pinned to the fingerprint learned during
discovery. Globally disabling validation is not acceptable.

The first discovery build advertises `http`, not fake `https`. This can prove
plain-HTTP discovery behavior but is not the final security mode. TLS memory,
handshake time, entropy integration, certificate generation, persistence,
mutual authentication, and interoperability all require real-hardware tests.

## JSON

Discovery and register payloads are flat and currently use a project-owned,
bounded parser. It validates JSON syntax, UTF-8, escapes/surrogate pairs,
required and duplicate fields, ports, enum values, lengths, and protocol major
version without heap allocation.

`prepare-upload` is nested and maps arbitrary file IDs. Before that phase,
integrate a pinned copy of jsmn (MIT, allocation-free, portable C) or extend the
parser behind the existing typed protocol API. The choice must retain explicit
token and nesting limits; no unbounded DOM is planned.

## Filesystem

libctru mounts the SD archive for normal homebrew startup and supports stdio
paths such as `sdmc:/3ds/LocalSend/Downloads/`. `FSUSER_GetFreeBytes` can query
free storage. The receive design uses bounded 64 KiB chunks, incremental hash,
`filename.part`, flush/close, and atomic-style rename only after size/hash
verification. Collision names are generated within the configured directory.

Filename handling will reject absolute paths, drive-like prefixes, `.`/`..`,
slashes, backslashes, control characters, empty names, and overlong UTF-8. A
sanitized basename is never concatenated before the destination directory has
been fixed. Directory metadata and Unicode normalization remain hardware/client
compatibility questions.

## UI, concurrency, memory, and sleep

Console rendering is sufficient for the discovery proof and uses both screens.
Networking is bounded and nonblocking in the frame loop. Long uploads cannot
remain there: the planned architecture uses one network/transfer worker and a
fixed event queue, with the UI owning all graphics state.

Initial hard bounds are 16 peers, four metadata connections, 2 KiB discovery
datagrams, 2 KiB request headers, and 2 KiB metadata bodies. A transfer will use
one reusable 64 KiB buffer; 32/64/128 KiB will be benchmarked on hardware.

libctru exposes APT sleep hooks and configuration. The safe initial transfer
policy is to prevent sleep while a transfer owns an open partial file, restore
normal sleep afterward, and treat unavoidable suspension/network loss as a
failed transfer. Actual lid behavior is unresolved until hardware testing.

## Proposed dependencies

| Dependency | Phase | Reason and decision |
|---|---:|---|
| devkitARM + libctru (`3ds-dev`) | now | Official compiler/runtime and 3DS APIs; required. |
| libc/newlib socket and stdio APIs | now | Enough for discovery, HTTP, and streamed files. |
| jsmn (MIT, pinned source) | receive MVP | Likely choice for bounded nested JSON; integrate only when needed. |
| `3ds-mbedtls` 2.28.x (Apache-2.0) | TLS phase | devkitPro-supported TLS server/client and SHA-256/certificate primitives. |

No graphics framework, HTTP framework, C++ runtime, or desktop library is
needed for the first milestone. Dependency versions and licenses will be
recorded in `THIRD_PARTY_LICENSES` when code is actually linked or vendored.

## Feasibility conclusion

No source-level platform blocker has been found for IPv4 discovery, a small HTTP
server, bounded file streaming, SD storage, random tokens, or mbedTLS. The two
largest unresolved items are real 3DS multicast reliability and mutual-TLS
resource/interoperability behavior. Both have concrete hardware tests and do
not justify abandoning the protocol or inventing a companion app.

Sources: [LocalSend protocol v2.2](https://github.com/localsend/protocol),
[current LocalSend core](https://github.com/localsend/localsend/tree/main/packages/core/src),
[devkitPro setup](https://devkitpro.org/wiki/Getting_Started/devkitARM),
[libctru](https://github.com/devkitPro/libctru), and
[devkitPro 3DS mbedTLS package](https://github.com/devkitPro/pacman-packages/tree/master/3ds/mbedtls).

