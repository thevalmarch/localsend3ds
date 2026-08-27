# Compatibility matrix

No row below is marked working until tested with an official client and real
Nintendo 3DS-family hardware. Host parser/socket tests are not substitutes.

| Direction | Discovery | One file | Transport verified | Independent integrity check | Cancel | Multi-file | PIN |
|---|---|---|---|---|---|---|---|
| macOS -> 3DS | **Verified on New Nintendo 2DS XL** | **Verified on New Nintendo 2DS XL** | HTTP | **SHA-256 matched for one tested file** | Implemented; not hardware-verified | Not implemented | Not implemented |
| 3DS -> macOS | **Verified on New Nintendo 2DS XL** | **Verified on New Nintendo 2DS XL** | HTTP and HTTPS/mTLS | **SHA-256 matched for an earlier HTTP test; HTTPS not independently checked** | Implemented; not hardware-verified | Not implemented | Not implemented |
| Linux -> 3DS | **Verified on New Nintendo 2DS XL** | **Verified on New Nintendo 2DS XL** | HTTP | Not tested | Not tested | Not implemented | Not implemented |
| 3DS -> Linux | **Verified on New Nintendo 2DS XL** | **Verified on New Nintendo 2DS XL** | HTTPS/mTLS | Not tested | Not tested | Not implemented | Not implemented |
| Android -> 3DS | **Verified on New Nintendo 2DS XL** | **Verified on New Nintendo 2DS XL** | HTTP | Not independently checked | Not reported | Not implemented | Not implemented |
| 3DS -> Android | **Verified on New Nintendo 2DS XL** | **Verified on New Nintendo 2DS XL** | Not separately recorded | Not independently checked | Not reported | Not implemented | Not implemented |
| Windows -> 3DS | Not tested | Not tested | Not tested | Not tested | Not tested | Not implemented | Not implemented |
| 3DS -> Windows | Not tested | Not tested | Not tested | Not tested | Not tested | Not implemented | Not implemented |
| iOS -> 3DS | Not tested | Not tested | Not tested | Not tested | Not tested | Not implemented | Not implemented |
| 3DS -> iOS | Not tested | Not tested | Not tested | Not tested | Not tested | Not implemented | Not implemented |

LocalSend3DS advertises an HTTP receive endpoint. Official peers can send to
that endpoint regardless of whether their own receive service advertises HTTP
or HTTPS. When LocalSend3DS sends, it follows the recipient's advertised
protocol: plain HTTP remains supported, while HTTPS uses TLS 1.2 mutual TLS,
certificate validity and self-signature checks, and exact leaf-DER SHA-256
fingerprint pinning. There is no HTTPS-to-HTTP downgrade.

PIN support is not implemented. Receive and outgoing transfers accept exactly
one file per session; batches, folders, text, and clipboard transfers are not
implemented. Sleep/lid interruption, cancellation, low-storage, and large-file
behavior have not received broad real-hardware coverage.

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
completion while the top console correctly showed `State: completed`. The
terminal interface was subsequently replaced by the graphical renderer and is
no longer the normal application UI.

## First outgoing attempt — 2026-08-24

On a real New Nintendo 2DS XL, LocalSend3DS selected a 3,220-byte PNG and a
discovered official macOS peer using plain HTTP. Discovery remained functional,
but no prepare-upload or file bytes were sent. The hardware log reported
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

## Linux interoperability — verified in stages 2026-08-26 to 2026-08-27

On a real New Nintendo 2DS XL and official LocalSend on Linux on the same LAN,
the two applications discovered each other. This verifies discovery in both
directions for the tested Linux peer.

The Linux peer successfully sent one file to LocalSend3DS. Completion was
observed, but no independent SHA-256 comparison or Linux cancellation test was
reported, so neither is marked verified.

The first reverse-direction attempt correctly stopped before transfer because
the then-current LocalSend3DS build did not support the HTTPS protocol advertised
by the Linux peer. After outgoing HTTPS/mTLS was implemented, LocalSend3DS sent
one file successfully to the normally encrypted Linux peer on the same real
hardware. Both sides reached completion. No independent destination size or
SHA-256 comparison was reported for that HTTPS transfer.

## Outgoing HTTPS/mTLS milestone — verified 2026-08-27

LocalSend3DS sent one file from a real New Nintendo 2DS XL to normally encrypted
official LocalSend recipients on macOS and Linux using the native CIA. On
macOS, diagnostic logs for both the prepare-upload and upload connections
confirmed:

- TLS 1.2 handshakes with a configured LocalSend3DS client certificate;
- peer-certificate time validity and cryptographic self-signature checks;
- exact leaf-certificate SHA-256 matches against the discovered fingerprint;
- no HTTP metadata before the recipient security checks passed.

The application was restarted and a second macOS HTTPS transfer completed with
the same persistent client-identity fingerprint prefix, verifying identity
reuse from SD. Incoming macOS-to-3DS transfer still worked after the transport
refactor. The console clock had to be corrected before peer-certificate
validity could pass, as intended. The completed HTTPS transfers have not yet
received independent destination file-size or SHA-256 comparisons.

The final TLS-enabled `.3dsx` artifact launches successfully through Homebrew
Launcher on the same real hardware. This verifies package-format startup, not a
separate `.3dsx`-specific transfer run.

## Android interoperability — verified 2026-08-27

On a real New Nintendo 2DS XL and official LocalSend on Android on the same
LAN, bidirectional discovery and one-file transfer in both directions
completed successfully. No independent checksum comparison, cancellation
coverage, exact outgoing transport record, or Android-specific failure-path
coverage was reported, so those properties are not inferred from completion.

## Graphical UI, Settings, and native CIA

The Citro2D dual-screen UI has been exercised on a real New Nintendo 2DS XL,
including its Receive, Send, Settings, file-browser, approval, progress, and
result views. Touch and physical-button interaction were used during iterative
real-hardware testing. Device-name, Quick Save, and Auto Finish Settings
persistence was also tested on the console, including the SDMC-safe replacement
fix.

The native CIA has been installed and launched directly from HOME Menu on the
same console. It runs the shared LocalSend3DS application rather than a
forwarder and does not depend on an external `.3dsx`. HOME Menu banner rendering
and chime playback were visually and audibly checked on hardware. This does not
constitute coverage of other 3DS-family models, every HOME Menu theme, sleep/lid
transitions, or every failure path.

## Final v1.0.0 package and Settings smoke — verified 2026-08-27

The final native CIA and final TLS-enabled `.3dsx` both launched successfully
on the real New Nintendo 2DS XL, respectively from HOME Menu and Homebrew
Launcher. The application exited cleanly. The final graphical Settings screen
was visually checked on hardware: the Connection value displayed
`Receive: HTTP`; the Quick Save trusted-network warning, v1.0.0/PIN detail,
compact and detailed Author text, and Settings status feedback all rendered
correctly. Settings persistence survived an application restart.

Across the real-hardware release testing, bidirectional discovery,
computer-to-3DS receive, and 3DS-to-computer HTTPS/mTLS send remained working.
The final `.3dsx` result above is deliberately limited to launch because the
transfer record was not separately attributed to that package format.
