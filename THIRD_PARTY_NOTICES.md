# Third-party notices

LocalSend3DS's original source code and original project artwork are licensed
under the repository's MIT License. The components below remain subject to
their own copyright and license terms. Full license texts that apply to the
distributed 3DSX/CIA binaries are collected in [`licenses/`](licenses/).

## LocalSend interoperability reference

[LocalSend](https://github.com/localsend/localsend) and the
[LocalSend protocol](https://github.com/localsend/protocol) are separate
upstream projects. LocalSend3DS is an unofficial, independently developed
client and is not affiliated with or endorsed by the LocalSend maintainers.

LocalSend3DS implements compatibility with stable LocalSend protocol v2.2. The
protocol documentation and official LocalSend implementation were consulted to
understand interoperable network behavior. No official LocalSend source code or
official logo artwork is included in LocalSend3DS. The official LocalSend
application is distributed under Apache License 2.0. The protocol repository
did not contain a separate license file when audited, so this notice does not
assign it a license that upstream has not stated.

## Components present in release binaries

The versions below are those used by the current validated devkitPro build.
They are external packages, not vendored source dependencies.

- [Mbed TLS 2.28.8](https://github.com/Mbed-TLS/mbedtls/tree/v2.28.8),
  packaged by devkitPro as `3ds-mbedtls` 2.28.8-1: the static `mbedtls`,
  `mbedx509`, and `mbedcrypto` libraries provide TLS 1.2, X.509, RSA,
  SHA-256, and CTR-DRBG functionality. Upstream offers Mbed TLS 2.28.8 under
  Apache-2.0 or GPL-2.0-or-later; devkitPro's
  [package recipe](https://github.com/devkitPro/pacman-packages/blob/master/3ds/mbedtls/PKGBUILD)
  selects the Apache-2.0 option. LocalSend3DS redistributes it under that
  option: [`Apache-2.0.txt`](licenses/Apache-2.0.txt).
- [libctru 2.7.0](https://github.com/devkitPro/libctru/tree/v2.7.0),
  [Citro2D 1.7.0](https://github.com/devkitPro/citro2d/tree/v1.7.0), and
  [Citro3D 1.7.1](https://github.com/devkitPro/citro3d/tree/v1.7.1): these
  static libraries provide Nintendo 3DS system/runtime and graphics support.
  They use the zlib license terms published by their upstream projects:
  [`zlib-devkitPro-libraries.txt`](licenses/zlib-devkitPro-libraries.txt).
- [Newlib 4.6.0.20260123](https://sourceware.org/newlib/), packaged as
  `devkitarm-newlib` 4.6.0.20260123-5: the C library, math library, and
  libgloss/libsysbase runtime objects are linked into the executable. Newlib
  and libgloss are multi-license collections; their complete upstream notice
  compilations are reproduced rather than reduced to a single SPDX label:
  [`Newlib-COPYING.txt`](licenses/Newlib-COPYING.txt) and
  [`Libgloss-COPYING.txt`](licenses/Libgloss-COPYING.txt).
- [GCC 16.1.0](https://gcc.gnu.org/gcc-16/), packaged as `devkitarm-gcc`
  16.1.0-1: `libgcc` and GCC CRT runtime objects are linked into the
  executable. They are covered by GPL-3.0-or-later with the GCC Runtime
  Library Exception 3.1. The exception permits eligible compiled target code
  to be distributed under the application's chosen terms. See
  [`GPL-3.0.txt`](licenses/GPL-3.0.txt) and
  [`GCC-Runtime-Library-Exception-3.1.txt`](licenses/GCC-Runtime-Library-Exception-3.1.txt).
- [devkitarm-crtls 1.2.6](https://github.com/devkitPro/devkitarm-crtls/tree/v1.2.6),
  packaged as `devkitarm-crtls` 1.2.6-1: its 3DSX startup object is present in
  the linked executable and is licensed under MPL-2.0. The exact MPL-covered
  Source Code Form used to build that object is
  [`3dsx_crt0.s`](https://github.com/devkitPro/devkitarm-crtls/blob/v1.2.6/3dsx_crt0.s),
  available without charge from the tagged upstream source. This source-access
  notice accompanies the Executable Form as required by MPL-2.0 section 3.2;
  see [`MPL-2.0.txt`](licenses/MPL-2.0.txt).

The normal build obtains these components through the official devkitPro
package ecosystem (`3ds-dev` plus `3ds-mbedtls`). Their source trees are not
copied into this repository.

## Native CIA packaging tools

- [3ds-bannertool](https://github.com/carstene1ns/3ds-bannertool/tree/734d33be79fd3f8c29c6296158f06ac7c5ca9dcb)
  is a build-time tool used to create the HOME Menu banner. The tool itself is
  not distributed, but its MIT notice is bundled conservatively because the
  generated banner incorporates tool-provided banner template data:
  [`MIT-3ds-bannertool.txt`](licenses/MIT-3ds-bannertool.txt).
- [makerom](https://github.com/3DSGuy/Project_CTR/tree/e8f5f529c54ff9b22a2491a480ffa69206bf7b19/makerom)
  is a build-time tool used to create the native CIA. The tool itself is not
  distributed, but the build uses `-exefslogo`, which places makerom's
  tool-provided homebrew logo data in the package. Its MIT notice is therefore
  included: [`MIT-makerom.txt`](licenses/MIT-makerom.txt).

Other devkitPro executables invoked while building, such as `3dsxtool` and
`smdhtool`, are not distributed with LocalSend3DS and do not contribute linked
runtime code identified by the final link map.

The optional icon-regeneration script can use
[Pillow](https://python-pillow.github.io/). Pillow is not required by the
normal 3DSX or CIA build, is not linked into either artifact, and is not
distributed with LocalSend3DS; no Pillow license copy is included in the binary
license bundle.

Nintendo, Nintendo 3DS, and related names are the property of their respective
owners. LocalSend3DS is an independent homebrew project and is not affiliated
with or endorsed by Nintendo.
