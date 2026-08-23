# Testing strategy

## Automated host tests

Run `make -f Makefile.host test`. AddressSanitizer and UndefinedBehaviorSanitizer
cover:

- valid v2.2 discovery parsing and typed fields;
- optional/null/future fields and UTF-8 escape decoding;
- duplicate keys, missing fields, invalid ports/UTF-8, v3 rejection, and length
  limits;
- announcement serialization and parse round-trip;
- registry add/update/deduplicate/expire/capacity behavior;
- an HTTP `/register` request fragmented across request line, headers, and body.

Planned pure tests add prepare-upload maps, HTTP edge cases, filename safety,
collision selection, transfer-state transitions, token validation, SHA-256 test
vectors, premature EOF, and cancellation.

## Cross-build gate

Run `scripts/check-toolchain.sh`, then `make`. The required artifact is
`LocalSend3DS.3dsx`. Treat every warning as an error. CI independently runs host
tests and builds in the official devkitPro devkitARM container.

## First real-device discovery test

Equipment: New Nintendo 2DS XL, SD card, current Homebrew Launcher, Mac and 3DS
on the same Wi-Fi, and current official LocalSend for macOS.

1. Record toolchain versions and build commit/hash.
2. Copy or explicitly deploy `LocalSend3DS.3dsx`; launch it.
3. Confirm the UI shows the expected hardware model, nonzero LAN IP, HTTP port,
   and no socket error.
4. Open official LocalSend on macOS and refresh both apps (Y on 3DS).
5. Confirm **LocalSend 3DS** appears in official LocalSend. This is the primary
   milestone, and requires the 3DS HTTP handled-request counter to increase.
6. Confirm the Mac alias appears on the 3DS and metadata matches.
7. Capture official-client version, router/AP model, encryption setting, both
   IPs, counters, packet trace if available, and result in compatibility.md.
8. Repeat after refresh, app restart, peer rename, Wi-Fi disconnect/reconnect,
   and lid close/open. Confirm no duplicate or stale UI entries.

If discovery fails, capture UDP 53317 and TCP 53317 traffic on the Mac. Separate
"no multicast received", "register not attempted", "TCP connect failed",
"HTTP response rejected", and "HTTP mode refused" rather than treating all as
one failure.

## Receive-MVP hardware tests

For every size (1 KiB, 1 MiB, 10 MiB, 100 MiB, 500 MiB, then larger where
practical), record source/destination SHA-256, elapsed time, transfer rate,
available memory, and SD free bytes. Test accept, reject, local cancel, remote
cancel, peer loss, lid close, SD full/write error, collision, malicious names,
wrong size, early close, invalid token/IP, and intentional checksum corruption.
Only a byte-identical final file with no misleading `.part` result is success.

