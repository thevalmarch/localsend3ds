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
- an HTTP `/register` request fragmented across request line, headers, and body;
- valid and malformed single-file prepare-upload metadata, missing/duplicate
  fields, map/file-ID mismatch, u64 overflow, and multi-file rejection;
- filename UTF-8/path safety and collision selection that accounts for `.part`;
- secure-session state transitions and session/file/token/IP validation;
- exact-size, oversized, short, cancelled, and checksum-mismatch transfers;
- SHA-256 test vectors and completed/cleaned temporary-file behavior;
- a fragmented HTTP/1.1 chunked prepare/accept/upload/finalize integration flow;
- delayed prepare approval beyond the ordinary HTTP idle timeout, matching the
  first real-hardware receive failure;
- prepare rejection and invalid-token HTTP responses;
- outgoing metadata generation with JSON escaping and a 5,000,000,000-byte
  64-bit size value;
- fragmented Content-Length and chunked HTTP response parsing plus malformed,
  ambiguous, oversized/incomplete response rejection;
- an end-to-end outgoing prepare/upload socket flow with every client write
  artificially limited to three bytes;
- recipient rejection, malformed prepare response, interrupted upload, pending
  cancellation, active-session `/cancel`, and refusal to downgrade HTTPS;
- Nintendo 3DS nonblocking-connect classification, including the raw SOC:u
  `SO_ERROR == -26` regression and the `EISCONN` completion probe;
- fixed-capacity file-browser navigation and file-size selection;
- pure UI hit-boundary, bounded-list-window, overflow-safe progress percentage,
  byte-size formatting, and rendered-width filename ellipsis behavior using a
  proportional-font measurement callback, including extension preservation;
- settings defaults, alias/UTF-8 validation, persistence round-trip, malformed
  file fallback, and Quick Save prepare-upload acceptance through the normal
  secure session/token path;
- 10,000 randomized inputs each for device and prepare-upload parsers.

## Cross-build gate

Run `scripts/check-toolchain.sh`, then `make`. The required artifact is
`LocalSend3DS.3dsx`. Treat every warning as an error. The build retains
`-Wstack-usage=4096`, generates the SMDH from `icon.png`, and links only the
official devkitPro libctru/Citro2D/Citro3D stack. CI independently runs host
tests and builds in the official devkitPro devkitARM container.

## Verified real-device discovery test

On 2026-08-24, bidirectional discovery passed on a real New Nintendo 2DS XL,
with the console and official LocalSend for macOS on the same Wi-Fi network.
The application launched without the previous ARM11 fault, initialized Wi-Fi,
obtained a valid LAN address, started its discovery/HTTP service, discovered the
Mac, and was discovered by the Mac as `LocalSend 3DS`. The Mac also displayed
the correct model, `New Nintendo 2DS XL`.

The repeatable procedure is retained below as a discovery regression test:

1. Record toolchain versions and build commit/hash.
2. Copy or explicitly deploy `LocalSend3DS.3dsx`; launch it.
3. Confirm the UI shows the expected hardware model, nonzero LAN IP, HTTP port,
   and no socket error.
4. Open official LocalSend on macOS and refresh both apps (Y on 3DS).
5. Confirm **LocalSend 3DS** appears in official LocalSend and the 3DS HTTP
   handled-request counter increases.
6. Confirm the Mac alias appears on the 3DS and metadata matches.
7. Capture official-client version, router/AP model, encryption setting, both
   IPs, counters, packet trace if available, and result in compatibility.md.
8. Repeat after refresh, app restart, peer rename, Wi-Fi disconnect/reconnect,
   and lid close/open. Confirm no duplicate or stale UI entries.

If discovery fails, capture UDP 53317 and TCP 53317 traffic on the Mac. Separate
"no multicast received", "register not attempted", "TCP connect failed",
"HTTP response rejected", and "HTTP mode refused" rather than treating all as
one failure.

## On-device discovery log

Each launch replaces `sdmc:/3ds/LocalSend/logs/latest.log`; output is capped at
256 KiB and flushed line-by-line. Copy this file from the SD card after a failed
test. It records public peer aliases/IPs and protocol metadata, but not the local
fingerprint, private key material, or transfer tokens.

Read the sequence in order:

1. `network: SOC ready` proves SOC initialization and local IPv4 lookup.
2. `discovery: joined multicast group` proves UDP bind and membership setup.
3. Three `announcement sent` entries prove the startup burst reached `sendto`.
4. `peer announcement` proves inbound multicast and JSON parsing.
5. `http: accepted connection` proves the Mac reached TCP port 53317.
6. `http: request ... /register` proves a complete HTTP header/body arrived.
7. `register accepted` and `register response ready` prove protocol parsing and
   response generation; `response sent` proves the socket write completed.

For receive tests, the same log records `prepare-upload received`, the public
sender alias/file/count/size, the user decision, session creation (without the
session ID or token), upload start, byte counts, completion/cancellation/failure,
filesystem errno values, and protocol validation failures. It never logs raw
file data, raw request bodies, tokens, or cryptographic private material.

Warnings include the peer IP, stage, errno where applicable, bounded byte
counts, and parser rejection reason. The raw JSON and fingerprints are not
logged.


## Receive-MVP hardware tests

The first real-hardware receive attempt on 2026-08-24 reached prepare-upload and
user approval, but the Mac received a connection reset before `/upload`. The
hardware log showed that the 200 prepare response had been queued with zero
bytes sent, then was immediately expired because its activity timestamp still
referred to request receipt roughly 43 seconds earlier. No upload connection
was accepted. The timeout transition has been fixed and covered by a host
integration regression test.

The replacement build subsequently passed its real-hardware receive test on a
New Nintendo 2DS XL. Official LocalSend for macOS sent the 2,100,717-byte
`QKThr.mp3`; both sides completed, no misleading final result was reported, and
the source and received-file SHA-256 values both equalled
`9b605de405852052f3afe49d9a4d2e914fda80d90b436c78403a63e1e5f07099`.
For the first test, use one ordinary file, accept with A, watch byte/percentage
progress, and confirm the final path under
`sdmc:/3ds/LocalSend/Downloads/`. Reject a separate request with B and cancel an
active test with B to confirm discovery remains usable afterward.

For every size (1 KiB, 1 MiB, 10 MiB, 100 MiB, 500 MiB, then larger where
practical), record source/destination SHA-256, elapsed time, transfer rate,
available memory, and SD free bytes. Test accept, reject, local cancel, remote
cancel, peer loss, lid close, SD full/write error, collision, malicious names,
wrong size, early close, invalid token/IP, and intentional checksum corruption.
Only a byte-identical final file with no misleading `.part` result is success.

## Outgoing-MVP hardware test

The first real-hardware attempt on 2026-08-24 selected `_setIcon.png` (3,220
bytes) and the HTTP peer `Test Peer` at `192.0.2.5:53317`. It failed
before sending the prepare request because libctru returned raw SOC:u
`EINPROGRESS` (`-26`) through `SO_ERROR` after `select()` reported the socket
writable. LocalSend3DS incorrectly treated that stale raw value as a terminal
connection error. The replacement uses the documented 3DS workaround: reissue
`connect()` and treat `EISCONN` as established, while preserving pending and
real failure states. The user subsequently verified one HTTP single-file send
to official LocalSend for macOS, including byte-identical output.

1. In official LocalSend for macOS, disable encryption for this HTTP-only MVP.
2. Refresh both applications and confirm the Mac entry on the 3DS says `HTTP`.
3. On the nearby-device screen press A, browse from `sdmc:/`, select one
   ordinary file, select the Mac, and press A again.
4. Confirm the Mac shows the LocalSend3DS alias, filename, and size; accept it.
5. Confirm both sides reach completion and the 3DS shows 100% with identical
   sent/total byte counts.
6. Compare SHA-256 for the SD source and Mac result. Only identical hashes pass.
7. Repeat once with B while the Mac approval dialog is pending and once during
   upload; verify cancellation is clean and discovery still refreshes.
8. If anything fails, copy `sdmc:/3ds/LocalSend/logs/latest.log` before the next
   launch. Relevant entries use the `outgoing` component and never contain full
   session IDs or file tokens. The replacement log records socket creation,
   `fcntl` setup, destination, initial `connect`, `select`, raw `SO_ERROR`, and
   the 3DS `connect` completion probe.

## Graphical-UI hardware test

The graphical UI is not yet verified on hardware. Use a small ordinary file in
each direction so protocol regressions can be separated from rendering/input
issues:

1. Replace the prior SD-card `.3dsx` with the reported artifact. Keep no asset
   sidecar files; the icon and metadata are embedded.
2. In Homebrew Launcher, confirm the custom teal dual-screen icon, title
   `LocalSend3DS`, and unofficial-client description appear.
3. Launch on a New Nintendo 2DS XL. Confirm neither screen shows a text console:
   the top should show branding/network status and the bottom should show the
   Nearby devices/Receive/Settings interface.
4. Exercise bottom-screen tabs and buttons by touch, then repeat navigation with
   L/R, D-Pad/Circle Pad, A, B, and Y. Press SELECT twice and confirm the
   developer view appears and returns without interrupting discovery.
5. Confirm `Test Peer` appears as a device card and official LocalSend for
   macOS still discovers `LocalSend 3DS` with model `New Nintendo 2DS XL`.
6. Send one file from macOS. Confirm the graphical sender/filename/size approval
   view, accept by touch or A, observe progress on top, and verify completion on
   both screens. Compare SHA-256 with the SD result.
7. From Send, browse the SD using touch and buttons, verify folders precede
   files and long lists scroll around the selection, choose one file and the
   HTTP Mac card, accept on macOS, and compare SHA-256 at completion.
8. Repeat receive rejection and one active cancellation. Confirm friendly UI
   results, no crash/stale console text, and discovery remains functional.
9. Press START from an idle screen and confirm a clean return to Homebrew
   Launcher. If anything fails, copy
   `sdmc:/3ds/LocalSend/logs/latest.log` before relaunching and photograph both
   screens.
