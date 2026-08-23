# LocalSend protocol notes

## Version selection

The stable upstream specification is **LocalSend protocol v2.2** as of
2026-08-24. The protocol repository's latest inspected commit was
`62bd3406ec80d62f2ed46269cdc06c4dcc391083` (2026-08-08). A `v3/` directory
contains design diagrams, but there is no stable v3 specification; this project
therefore implements v2.2 and rejects v3 packets rather than guessing.

## Discovery behavior needed for the first milestone

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
discovery milestone. The current code advertises honest plain HTTP and stores
peer UDP announcements locally. Active register calls to HTTPS peers await the
TLS client layer.

## Exact receive-MVP protocol surface

1. `POST /api/localsend/v2/register` and `GET /api/localsend/v2/info`.
2. `POST /api/localsend/v2/prepare-upload` with typed sender info and a bounded
   map of file metadata. PIN query parsing is reserved but disabled initially.
3. User approval before response. `200` returns a cryptographically random
   session ID and accepted file-token map; `403` rejects; `409` protects an
   active session; `400/429/500` follow the specification.
4. `POST /api/localsend/v2/upload?sessionId=&fileId=&token=` validates all three
   values plus peer IP, requires accepted state, requires a bounded/valid
   `Content-Length`, and streams exactly that many bytes to `.part`.
5. Return `422` when an advertised SHA-256 does not match; otherwise close,
   flush, and rename to the collision-safe final name before `200`.
6. `POST /api/localsend/v2/cancel?sessionId=` and local B-button cancellation
   stop I/O, close handles, remove/mark partial data, invalidate all tokens, and
   emit a terminal UI event.

The data model supports multiple metadata entries, but version 0.1 accepts a
single file so real interoperability is reached before batch UI work.

## Reverse/download API decision

`prepare-download`/`download` is designed primarily for a sender that hosts
unencrypted files for browser download when the receiver has no LocalSend
server. It is not required for 3DS receiving and is not the preferred first
outgoing path. Version 0.2 should use normal prepare-upload/upload against a
peer server. Reverse download remains useful for browser interoperability and
will be reconsidered after native outgoing transfer is stable.

## TLS identity and verification

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

