# Compatibility matrix

No row below is marked working until tested with an official client and real
Nintendo 3DS-family hardware. Host parser/socket tests are not substitutes.

| Direction | Discovery | One file | Cancel | SHA-256 | Multi-file | HTTPS | PIN |
|---|---|---|---|---|---|---|---|
| macOS -> 3DS | Pending hardware test | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| 3DS -> macOS | Pending hardware test | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| Windows -> 3DS | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| Android -> 3DS | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| Linux -> 3DS | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| iOS -> 3DS | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| 3DS -> Windows | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| 3DS -> Android | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| 3DS -> Linux | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |
| 3DS -> iOS | Not tested | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented | Not implemented |

Current limitation: LocalSend3DS advertises honest HTTP mode. It can receive
official register calls and list UDP announcements, but it cannot yet actively
register with an HTTPS-only peer. Discovery success may therefore be asymmetric
until mbedTLS mutual-TLS support is added.

