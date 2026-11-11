# Native LocalSend3DS CIA

This optional package contains the same LocalSend3DS executable built by the
top-level Makefile. It is a native HOME Menu application: it does not use
`hb:ldr`, APT chainloading, or an external 3DSX.

This branch targets **unreleased v1.1.0**. RC1 passed upgrade installation over
v1.0.0 on New Nintendo 2DS XL, HOME Menu launch, displayed v1.1.0, existing
settings preservation, all five themes, theme persistence after restart,
incoming/outgoing transfers, icon/banner/startup-sound observation and clean
START exit. Fresh CIA installation and RC1 sleep/lid behavior were not reported.
The published v1.0.0 package is separate; v1.1.0 is not publicly downloadable.
See the [scoped RC1 record](../../docs/testing.md#rc1-package-verification-unpublished),
including the separately tested `.3dsx` candidate.

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
  commit `e8f5f529c54ff9b22a2491a480ffa69206bf7b19`, **with the local repair below**.

The unmodified pinned makerom has a tool-version/CIA-index macro collision.
RC1 uses a locally patched makerom 0.19.0, not an unmodified upstream binary.
Apply the exact [repair patch and build instructions](patches/README.md) before
using this revision to package a CIA. No LocalSend3DS build rule applies the
patch automatically. Ctrtool from the same pinned Project_CTR commit was used
for inspection.

RC1 was built from `09d8d8bc5980fa7ae90d25831f468666226bc466`; later documentation
or external-tool patch records do not change that original build revision.

Package identity:

- Title ID: `000400000F5D5300`
- Unique ID: `0xF5D53`
- Product code: `CTR-P-LS3D`
- Process name: `LS3DS`
- Source package version: `1.1.0` (unreleased)

Packaging derives the major/minor/micro arguments from `LS3DS_APP_VERSION` in
`include/config.h` and depends on that header. Malformed or out-of-range
versions fail before packaging. Inspect the arguments without building:

```sh
make -C packaging/cia print-version
```

The supported CIA and `.3dsx` share application code and the Theme setting;
they read the same `sdmc:/3ds/LocalSend/settings.conf`. Original Light remains
the default when no theme is specified, including legacy v1.0.0 files.

Outgoing HTTPS uses Mbed TLS over the ordinary `soc:U` socket service and
PS-backed entropy through `ps:ps`. The native CIA does not request or require
the Nintendo `ssl:C` service.

The CIA is not Nintendo-signed and requires custom firmware with signature
patches. Its static HOME Menu banner is generated from the original
LocalSend3DS mark and original Light palette; runtime theme selection does not
change HOME Menu assets. The accompanying short menu chime
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
