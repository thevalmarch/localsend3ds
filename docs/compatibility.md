# Compatibility matrix

No row below is marked working until tested with an official client and real
Nintendo 3DS-family hardware. Host parser/socket tests are not substitutes.

| Direction | Discovery | One file | Cancel | SHA-256 | Multi-file | HTTPS | PIN |
|---|---|---|---|---|---|---|---|
| macOS -> 3DS | **Verified on New Nintendo 2DS XL** | **Verified on New Nintendo 2DS XL** | Ready for hardware test | **Verified for tested file** | Not implemented | Not implemented | Not implemented |
| 3DS -> macOS | **Verified on New Nintendo 2DS XL** | **Verified on New Nintendo 2DS XL (HTTP)** | Ready for broader hardware test | User reports byte-identical transfer | Not implemented | Not implemented | Not implemented |
| Windows -> 3DS | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| Android -> 3DS | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| Linux -> 3DS | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| iOS -> 3DS | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| 3DS -> Windows | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| 3DS -> Android | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| 3DS -> Linux | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| 3DS -> iOS | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |

Current limitation: LocalSend3DS advertises honest HTTP mode. TLS/HTTPS and PIN
support are deferred. The receive and outgoing MVPs accept exactly one file per
session; batches and folders are not implemented. For the outgoing hardware
test, disable encryption in official LocalSend for macOS so it advertises HTTP.

## Verified discovery milestone — 2026-08-24

Bidirectional discovery was verified on a real New Nintendo 2DS XL and the
official LocalSend application for macOS on the same Wi-Fi network:

- LocalSend3DS launched, initialized Wi-Fi, obtained a LAN address, and started
  its discovery/HTTP service.
- LocalSend3DS discovered the official macOS client.
- The official macOS client discovered the alias `LocalSend 3DS`.
- The official client displayed the independently detected hardware model as
  `New Nintendo 2DS XL`.

This is a real-hardware interoperability result, not a host simulation. At this
point file receipt still required its separate test below.

## First receive attempt — 2026-08-24

Official LocalSend for macOS successfully sent the one-file prepare request to
the real New Nintendo 2DS XL, and LocalSend3DS displayed the sender, filename,
and size. After user acceptance, the old build reset the prepare socket before
sending its queued 200 response, so the official client never obtained the
session/token needed to attempt `/upload`. This was a LocalSend3DS timeout-state
bug, not a discovery regression. The replacement build subsequently passed the
single-file receive verification recorded below.

## Single-file receive milestone — verified 2026-08-24

Official LocalSend for macOS sent `QKThr.mp3` to LocalSend3DS on a real New
Nintendo 2DS XL. The user accepted on-console, both applications reached
completion, and LocalSend3DS saved the 2,100,717-byte file to
`sdmc:/3ds/LocalSend/Downloads/QKThr.mp3`.

The source and received-file SHA-256 values were identical:
`9b605de405852052f3afe49d9a4d2e914fda80d90b436c78403a63e1e5f07099`.
This verifies byte-for-byte single-file macOS-to-3DS interoperability on real
hardware. Broader receive cases such as cancellation, multiple sizes, and
malformed peers remain separate test-matrix items.

The old bottom console retained `Transfer accepted; waiting for upload` after
completion while the top console correctly showed `State: completed`. The new
graphical renderer reads the terminal transfer state directly on both screens,
removing that stale-message path; real-hardware visual confirmation is pending.

## First outgoing attempt — 2026-08-24

On a real New Nintendo 2DS XL, LocalSend3DS selected `_setIcon.png` (3,220
bytes) and the discovered official macOS peer `Test Peer` using plain
HTTP at `192.0.2.5:53317`. Discovery remained functional, but no
prepare-upload or file bytes were sent. The hardware log reported
`Connection failed (errno -26)`.

The failing value came from `getsockopt(SOL_SOCKET, SO_ERROR)`, not from the
initial `connect()`. Nintendo SOC:u uses raw error index 26 for `EINPROGRESS`,
and libctru does not translate the `SO_ERROR` payload. The replacement uses a
3DS-only follow-up `connect()` completion probe and accepts `EISCONN` as
success. Host tests and the cross-build verify the replacement logic; outgoing
interoperability was subsequently verified in the milestone below.

## Single-file outgoing milestone — verified 2026-08-24

The user verified that a real New Nintendo 2DS XL can send one file over HTTP
to official LocalSend for macOS after the SOC:u nonblocking-connect fix. The
file was streamed from SD rather than buffered in RAM, both clients completed,
and the user reports byte-identical transfer verification. Together with the
receive result above, basic bidirectional LocalSend one-file transfer is now a
real-hardware result. Broader sizes, cancellation cases, other 3DS models, and
other official-client platforms remain separate matrix items.

## Graphical UI build

The terminal interface has been replaced in normal use by a Citro2D dual-screen
UI with touch and physical controls. This UI build is host-tested and
cross-compiled but is **not yet verified on real hardware**. The verified
network and transfer results above apply to the protocol implementation that
the new UI preserves.
