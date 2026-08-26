# Third-party notices

LocalSend3DS's original source code and original project artwork are licensed
under the repository's MIT License. The following separate projects are used
as interoperability references, libraries, toolchains, or build-time tools.
They remain subject to their own copyright and license terms.

## LocalSend interoperability reference

[LocalSend](https://github.com/localsend/localsend) and the
[LocalSend protocol](https://github.com/localsend/protocol) are separate
upstream projects. LocalSend3DS is an unofficial, independently developed
client and is not affiliated with or endorsed by the LocalSend maintainers.

LocalSend3DS implements compatibility with stable LocalSend protocol v2.2. The
protocol documentation and official LocalSend implementation were consulted to
understand interoperable network behavior. No official LocalSend source code or
official logo artwork is included in this repository. The official LocalSend
application is distributed under Apache License 2.0. The protocol repository
did not contain a separate license file when audited, so this notice does not
assign it a license that upstream has not stated.

## Nintendo 3DS libraries and toolchain

- [devkitPro / devkitARM](https://devkitpro.org/) provides the external ARM
  cross-compilation toolchain and standard Nintendo 3DS build environment. It
  is not vendored in this repository. The devkitARM distribution contains
  multiple components; their licensing remains governed by the corresponding
  devkitPro and upstream component terms.
- devkitPro's Nintendo 3DS build tools, including `3dsxtool` and `smdhtool`,
  are invoked by the standard devkitPro Makefile rules. They are external tools
  supplied through the devkitPro package ecosystem and remain governed by
  their respective upstream terms.
- [libctru](https://github.com/devkitPro/libctru) provides Nintendo 3DS system
  APIs and runtime support. It is third-party software distributed under the
  zlib license terms published in its upstream README.
- [Citro2D](https://github.com/devkitPro/citro2d), copyright 2017–2018 fincs,
  provides the 2D graphics API. It is distributed under the zlib License.
- [Citro3D](https://github.com/devkitPro/citro3d), copyright 2014–2018 fincs,
  provides the underlying 3D graphics API used by Citro2D. It is distributed
  under the zlib License.

These libraries are obtained through the official devkitPro `3ds-dev` package
group; their source is not vendored in this repository.

## Native CIA packaging tools

- [3ds-bannertool](https://github.com/carstene1ns/3ds-bannertool), based on
  bannertool by Steveice10 and maintained by carstene1ns, creates the HOME Menu
  banner during the optional native CIA build. Its license identifies copyright
  2015–2017 Steveice10 and 2024–2026 carstene1ns; the pinned tool is distributed
  under the MIT License.
- [makerom](https://github.com/3DSGuy/Project_CTR/tree/master/makerom), from
  Project_CTR, creates the optional native CIA package. Its license identifies
  copyright 2014 3DSGuy, 2014 applestash, and 2015–2022 Jakcron; makerom is
  distributed under the MIT License.

The packaging tools are build-time dependencies and are not bundled in the
LocalSend3DS source repository.

Nintendo, Nintendo 3DS, and related names are the property of their respective
owners. LocalSend3DS is an independent homebrew project and is not affiliated
with or endorsed by Nintendo.
