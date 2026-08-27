# Native LocalSend3DS CIA

This optional package contains the same LocalSend3DS executable built by the
top-level Makefile. It is a native HOME Menu application: it does not use
`hb:ldr`, APT chainloading, or an external 3DSX.

The normal top-level `make` remains the supported 3DSX build. Build both the
3DSX and the optional CIA with the `3ds-dev` and `3ds-mbedtls` packages
installed:

```sh
make cia \
  BANNERTOOL=/absolute/path/to/bannertool \
  MAKEROM=/absolute/path/to/makerom
```

The output is `packaging/cia/LocalSend3DS.cia`.

The native packaging pipeline has been validated with:

- [`carstene1ns/3ds-bannertool`](https://github.com/carstene1ns/3ds-bannertool/tree/734d33be79fd3f8c29c6296158f06ac7c5ca9dcb)
  commit `734d33be79fd3f8c29c6296158f06ac7c5ca9dcb`
- [`3DSGuy/Project_CTR` makerom](https://github.com/3DSGuy/Project_CTR/tree/e8f5f529c54ff9b22a2491a480ffa69206bf7b19/makerom)
  commit `e8f5f529c54ff9b22a2491a480ffa69206bf7b19`

Package identity:

- Title ID: `000400000F5D5300`
- Unique ID: `0xF5D53`
- Product code: `CTR-P-LS3D`
- Process name: `LS3DS`
- Version: `1.0.0`

Outgoing HTTPS uses Mbed TLS over the ordinary `soc:U` socket service and
PS-backed entropy through `ps:ps`. The native CIA does not request or require
the Nintendo `ssl:C` service.

The CIA is not Nintendo-signed and requires custom firmware with signature
patches. Its static HOME Menu banner is generated from the original
LocalSend3DS mark and in-application palette. The accompanying short menu chime
is an original, deterministically synthesized, non-looping PCM16 sound. The
build validates that the CWAV embedded by `bannertool` is byte-identical to the
source WAV samples before invoking `makerom`.

No title-managed save data is allocated. Settings, logs, and downloads remain
in their existing direct-SD paths under `sdmc:/3ds/LocalSend`, independently of
CIA installation or replacement.

The final v1.0.0 native package with main-thread `Priority: 16` (encoded as
priority `0x30`) has been verified on a real New Nintendo 2DS XL for direct
HOME Menu launch, normal LocalSend3DS operation, Settings persistence after
restart, and clean exit. Future rebuilt release artifacts should still receive
a brief real-hardware regression test.

The earlier generic 3DSX-forwarder experiment was removed after its cross-title
APT handoff crashed HOME Menu on real hardware. It is not a supported launch
method.
