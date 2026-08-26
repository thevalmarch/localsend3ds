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
implemented discovery path. The current code advertises honest plain HTTP and
stores peer UDP announcements locally. HTTPS peers are not contacted because a
TLS client layer is not implemented.

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
   64-bit size, `application/octet-stream` type, and null checksum/preview.
2. `POST /api/localsend/v2/prepare-upload` and keep the nonblocking request
   alive while the recipient decides. Parse bounded fragmented HTTP responses;
   `200` must contain a nonempty session ID and a token under exactly the
   requested file ID. Treat `204/401/403/409/429` as rejection.
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

Only honest HTTP recipients are supported. HTTPS peers are not
downgraded or contacted as plaintext; the UI asks the tester to disable
encryption on the official recipient. This matches the upstream project's own
plain-HTTP sender testing mode and preserves the later certificate-pinning path.

## Reverse/download API decision

`prepare-download`/`download` is designed primarily for a sender that hosts
unencrypted files for browser download when the receiver has no LocalSend
server. It is not required for the implemented 3DS receive or send flows. The
current outgoing implementation uses the normal `prepare-upload`/`upload`
sequence against a peer server. Reverse download is not implemented and remains
a possible future interoperability feature.

## Future TLS identity and verification

TLS/HTTPS is not implemented in LocalSend3DS v1.0.0. The following notes record
the upstream behavior that a future implementation would need to preserve; they
do not describe a current feature:

- HTTP fingerprint: random device string, used for self-discovery/remembering.
- HTTPS fingerprint: uppercase hex SHA-256 over certificate DER.
- Native HTTPS uses a client certificate as well as a server certificate in
  current official clients.
- The discovery fingerprint pins the peer certificate during the handshake.
- The certificate's self-signature and time validity must be checked; hostname
  validation is not the LocalSend identity mechanism.
- Local keys/certificates persist and private material is never logged.

Source of truth: [localsend/protocol](https://github.com/localsend/protocol).
Implementation behavior was cross-checked against the current
[LocalSend core HTTP and multicast modules](https://github.com/localsend/localsend/tree/main/packages/core/src).
