# Local makerom repair used for v1.1.0 RC1

This directory records an external build-tool repair. It is not LocalSend3DS
application code and is not applied by the application's Makefiles.

## Pinned source and exact patch

- Upstream: [3DSGuy/Project_CTR](https://github.com/3DSGuy/Project_CTR)
- Base commit: `e8f5f529c54ff9b22a2491a480ffa69206bf7b19`
- Tool: makerom 0.19.0
- Local patch: [makerom-tool-version-minor.patch](makerom-tool-version-minor.patch)
- Patch SHA-256: `ae306e664ee28cf068b79580210e508c65d7636aecadedbb647afaf793796056`

The pinned `makerom/src/version.h` defines `VER_MINOR` as 19. After inclusion
in `user_settings.c`, it shadows the minor-component enum index (1) in a
three-element CIA version array. The `-minor`, `-ver` and `-dver` handlers can
therefore write to index 19. `cia.c` itself uses the intended enum indexes.

The tested two-line repair renames only the tool-version minor macro to
`MAKEROM_TOOL_VERSION_MINOR` and updates its use in the version banner. The
tool still identifies as 0.19.0. CIA component indexes, array dimensions,
packaging algorithms and signing behavior are unchanged. This is a **locally
patched build**, not an unmodified upstream executable. Future upstream
revisions need a fresh review rather than blind application of this patch.

The patch is preserved byte-for-byte, including unified-diff context markers.
A generic whitespace check of the patch as ordinary newly added text can flag
its blank context lines and space-before-tab context prefixes. Do not strip
those markers to silence the check; validate it with `git apply --check`.

## Isolated source preparation and native build

Run from the LocalSend3DS repository root. The commands below create a separate
temporary source checkout and do not install anything globally. They require
Git, Make and a native C11/C++ toolchain with an archiver and platform SDK.
Makerom's library sources are already included in the pinned checkout; do not
run dependency-update or download targets.

```sh
set -eu
project_root=$(pwd)
tool_root=$(mktemp -d "${TMPDIR:-/tmp}/localsend3ds-makerom.XXXXXX")
upstream_commit=e8f5f529c54ff9b22a2491a480ffa69206bf7b19

git init "$tool_root/Project_CTR"
git -C "$tool_root/Project_CTR" remote add origin https://github.com/3DSGuy/Project_CTR.git
git -C "$tool_root/Project_CTR" fetch --depth=1 origin "$upstream_commit"
git -C "$tool_root/Project_CTR" checkout --detach "$upstream_commit"
test "$(git -C "$tool_root/Project_CTR" rev-parse HEAD)" = "$upstream_commit"
test -z "$(git -C "$tool_root/Project_CTR" status --porcelain)"

patch_file="$project_root/packaging/cia/patches/makerom-tool-version-minor.patch"
git -C "$tool_root/Project_CTR" apply --check "$patch_file"
git -C "$tool_root/Project_CTR" apply "$patch_file"
git -C "$tool_root/Project_CTR" diff --check
git -C "$tool_root/Project_CTR" diff -- makerom/src/version.h makerom/src/user_settings.c

make -C "$tool_root/Project_CTR/makerom" deps
make -C "$tool_root/Project_CTR/makerom" program
```

Stop if a command fails; do not continue with an unpatched or incomplete build.
Review the diff: only the two lines recorded in the patch should change.
`make deps` compiles bundled libraries; it does not fetch newer dependencies.
On Linux this needs the normal native compiler/runtime development environment;
on macOS it also uses the system iconv library. Choose a compiler and SDK from
the same installation. A mismatched Xcode linker and Command Line Tools SDK
failed during tool recovery; this was resolved by explicitly selecting the
installed Xcode SDK for that build, not by suppressing diagnostics or editing
upstream code.

The approved RC1 makerom was built natively for macOS arm64 with Apple Clang
21.0.0 and Make 3.81, selecting the bundled Xcode SDK via `SDKROOT` and
`-isysroot` for both compilation and linking. It was 729896 bytes with SHA-256
`a955f040c1772b6355de3de2a1f1cc4e77b37fb20310cf9857206deb6bd14625`.
A build on another platform, date, SDK or compiler need not have that hash;
makerom embeds its build date/time. Preserve the pinned source and exact patch,
review warnings and validate actual output metadata instead of assuming binary
reproducibility.

## Tool and package validation

```sh
"$tool_root/Project_CTR/makerom/bin/makerom" -help
```

The banner must identify `CTR MAKEROM v0.19.0`. This upstream command returns a
nonzero help status (254 on the validated macOS build); displaying help alone
does not verify the patch. Check the source diff and retained base commit too.
The patched preprocessing must retain the `VER_MINOR` enum index, not replace
CIA array accesses with index 19. The index enum remains major=0, minor=1,
micro=2.

A focused ASan/UBSan harness passed 20 argument cases and three encoding
assertions, including boundaries/rejection for `-major`, `-minor`, `-micro`,
`-ver` and `-dver`. It checked that successful handlers changed only expected
settings fields. Leak detection was disabled. The normal patched tool build
retained eight upstream unused-variable warnings in bundled libraries; the
three array-bounds warnings disappeared. No warning suppressions were added.

A synthetic, non-application CIA fixture with test title ID
`000400000F000100` was packaged outside LocalSend3DS. Both ctrtool from the
pinned Project_CTR commit and an independent TMD parser confirmed:

| Version arguments | Encoded TMD version |
|---|---:|
| `-major 1 -minor 1 -micro 0` | 1040 (1.1.0) |
| `-ver 1040` | 1040 (1.1.0) |
| `-major 2 -minor 3 -micro 4` | 2100 (2.3.4) |
| `-major 63 -minor 63 -micro 15` | 65535 (63.63.15) |

For any newly generated LocalSend3DS CIA, inspect its actual bytes with the
pinned ctrtool, using read-only inspection:

```sh
/path/to/ctrtool --info /path/to/generated/LocalSend3DS.cia
```

Check TMD and ticket version **1.1.0 (1040)**, Title ID and NCCH Program ID
**000400000F5D5300**, and product code **CTR-P-LS3D**. Independently parse the
TMD version when validating release artifacts; Makefile arguments alone do not
prove the encoded version.

The application Makefile emits `-major 1 -minor 1 -micro 0` in that order.
Upstream's final `-micro` handler sets `useFullTitleVer` and clears
`useNormTitleVer`, so the package retains the requested major version rather
than substituting the NCCH remaster revision. Do not reorder these arguments
without revalidating. RC1's completed CIA was checked by ctrtool and an
independent parser as 1.1.0 / 1040. Full packaging identity and hardware results
are recorded in [testing](../../../docs/testing.md#rc1-package-verification-unpublished).

## Licensing and attribution

Makerom is third-party software, copyrighted by 3DSGuy, applestash and Jakcron
under its upstream MIT license. The exact upstream notice is retained in
[MIT-makerom.txt](../../../licenses/MIT-makerom.txt); the local patch does not
replace it or claim ownership of upstream code. Keep upstream copyright and
permission notices when sharing the patched tool or its source. See also
[third-party notices](../../../THIRD_PARTY_NOTICES.md#native-cia-packaging-tools).
