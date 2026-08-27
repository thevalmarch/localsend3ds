# LocalSend protocol notes

## Version selection

The stable upstream specification is **LocalSend protocol v2.2** as of
2026-08-24. The protocol repository's latest inspected commit was
`62bd3406ec80d62f2ed46269cdc06c4dcc391083` (2026-08-08). A `v3/` directory
contains design diagrams, but there is no stable v3 specification; this project
therefore implements v2.2 and rejects v3 packets rather than guessing.

## Implemented discovery and registration behavior

- IPv4 UDP group `224.0.0.167`, default port `53317`, TTL 1.
- Announcement fields: alias, version, optional device model/type, fingerprint,
  server port, `http`/`https`, download capability, and `announce: true`.
- Startup/refresh announcement burst after 100 ms, then 500 ms, then 2000 ms.
- Ignore the local fingerprint, reject malformed/oversized packets, deduplicate
  by fingerprint, update changed metadata/IP/port, and expire stale entries.
- Run `POST /api/localsend/v2/register` and return the local info object.
- Run `GET /api/localsend/v2/info` for debugging.

The current official core describes UDP as announce-only: a peer answers an
announcement with an HTTP register request. This is stricter than relying on
the v2 specification's UDP fallback. Therefore `/register` was brought into the
implemented discovery path. LocalSend3DS advertises an honest plain-HTTP
receive endpoint and stores both HTTP and HTTPS peer announcements. The peer's
advertised protocol is selected later by the outgoing transport.

## Implemented receive protocol surface

1. `POST /api/localsend/v2/register` and `GET /api/localsend/v2/info`.
2. `POST /api/localsend/v2/prepare-upload` with typed sender info and a bounded
   map of file metadata. PIN support is not implemented.
3. User approval before response. `200` returns a cryptographically random
   session ID and accepted file-token map; `403` rejects; `409` protects an
   active session; `400/429/500` follow the specification.
4. `POST /api/localsend/v2/upload?sessionId=&fileId=&token=` validates all three
   values plus peer IP and requires accepted state. It accepts exact
   `Content-Length` framing or strict incremental HTTP/1.1 chunked framing,
   because the current official streaming client may use chunked encoding.
   Decoded bytes stream directly to `.part` and may never exceed metadata size.
5. Return `422` when an advertised SHA-256 does not match; otherwise close,
   flush, and rename to the collision-safe final name before `200`.
6. `POST /api/localsend/v2/cancel?sessionId=` and local B-button cancellation
   stop I/O, close handles, remove/mark partial data, invalidate all tokens, and
   emit a terminal UI event.

The current v1.0.0 transfer model is intentionally single-file. A prepare
request with more than one file receives `400`; batch support is not
implemented.

## Implemented outgoing protocol flow

1. Serialize the local typed device information and one selected SD file into
   the v2.2 prepare-upload request. The file has a secure random UUID ID,
   64-bit size, `application/octet-stream` type, `sha256: null`, and a null
   preview. Outgoing checksum generation is not implemented.
2. `POST /api/localsend/v2/prepare-upload` and keep the nonblocking request
   alive while the recipient decides. Parse bounded fragmented HTTP responses;
   `200` must contain a nonempty session ID and a token under exactly the
   requested file ID. A `401` identifies unsupported PIN protection;
   `204/403/409/429` are handled as remote rejection/busy responses.
3. URL-encode all returned credentials, then open a separate
   `POST /api/localsend/v2/upload?sessionId=&fileId=&token=` connection with an
   exact 64-bit Content-Length. Stream at most 32 KiB from SD at a time and
   count only bytes accepted by `send()`.
4. Require HTTP `200` after all declared bytes have been sent. A short SD read,
   connection loss, timeout, malformed response, or unexpected status enters a
   recoverable failure state without stopping discovery.
5. B closes a pending prepare request before credentials exist. Once a session
   exists it also sends `POST /api/localsend/v2/cancel?sessionId=` on a separate
   bounded nonblocking connection.

The same outgoing state machine supports honest HTTP and HTTPS recipients.
HTTPS performs TLS 1.2 mutual TLS, presents the persistent LocalSend3DS client
certificate, checks the peer certificate's time validity and cryptographic
self-signature, and pins the SHA-256 of the exact leaf DER to the discovered
fingerprint before any HTTP bytes are sent. A malformed fingerprint or failed
check stops the transfer; HTTPS is never downgraded to HTTP.

PIN is separate from TLS and is not implemented. A `401` prepare response
produces a specific unsupported-PIN error, and LocalSend3DS does not retry with
a PIN.

## Reverse/download API decision

`prepare-download`/`download` is designed primarily for a sender that hosts
unencrypted files for browser download when the receiver has no LocalSend
server. It is not required for the implemented 3DS receive or send flows. The
current outgoing implementation uses the normal `prepare-upload`/`upload`
sequence against a peer server. Reverse download is not implemented and remains
a possible future interoperability feature.

## Outgoing TLS identity and verification

- HTTP fingerprint: random device string, used for self-discovery/remembering.
- HTTPS fingerprint: uppercase hex SHA-256 over certificate DER.
- Native HTTPS uses a client certificate as well as a server certificate in
  current official clients. LocalSend3DS generates an RSA-2048, SHA-256,
  self-signed client certificate with `CN=LocalSend User` from PS-backed entropy.
- The identity is stored at `sdmc:/3ds/LocalSend/tls-identity.bin`, recovered
  from a valid backup when possible, and shared by the 3DSX and CIA builds.
  The file contains the private key and therefore should be protected with the
  SD card and its backups. If neither primary nor backup is usable, a new
  identity is generated.
- The discovery fingerprint pins the peer leaf certificate during each
  prepare-upload, upload, and cancel TLS handshake.
- The peer certificate's self-signature and time validity are checked; hostname
  validation is not the LocalSend identity mechanism. The console date and time
  must therefore be correct.
- The generated LocalSend3DS certificate uses a fixed 1975–4095 validity range,
  so its creation does not capture a bad first-launch clock. Local keys and
  certificates persist, and private material is never logged.

This HTTPS implementation is outgoing only. LocalSend3DS continues to advertise
and serve its incoming endpoints over HTTP.

Fingerprint pinning authenticates the certificate against the value in the
LocalSend discovery announcement. Because discovery itself is unauthenticated,
this matches LocalSend's local-network trust model but does not independently
establish the human identity of the peer.

Source of truth: [localsend/protocol](https://github.com/localsend/protocol).
Implementation behavior was cross-checked against the current
[LocalSend core HTTP and multicast modules](https://github.com/localsend/localsend/tree/main/packages/core/src).
