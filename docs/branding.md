# Branding and visual-language audit

Audit date: 2026-08-24. Upstream inspected commit:
`daa652708fa44261b6805d37802a989e68ad7c7d`.

## What was inspected

The current official LocalSend repository uses Flutter Material 3 with a teal
seed color, light and dark color schemes, rounded surface cards, alias-first
device rows, small protocol/model badges, a Receive/Send/Settings navigation
model, rounded progress indicators, and concise human-readable result states.
The official raster logo assets are stored under `app/assets/img/logo-*.png`.

Sources:

- [official repository and Apache-2.0 license](https://github.com/localsend/localsend)
- [theme configuration](https://github.com/localsend/localsend/blob/main/app/lib/config/theme.dart)
- [device list tile](https://github.com/localsend/localsend/blob/main/app/lib/widget/list_tile/device_list_tile.dart)
- [progress screen](https://github.com/localsend/localsend/blob/main/app/lib/pages/progress_page.dart)
- [release workflow referencing official logo assets](https://github.com/localsend/localsend/blob/main/.github/workflows/release.yml)

## License and trademark finding

The repository has an Apache License 2.0 and no root NOTICE file. No separate
asset license, branding guide, logo-use permission, or trademark policy was
found in the inspected tree or official project pages. Apache-2.0 section 6
expressly does not grant rights to use licensors' trade names, trademarks,
service marks, or product names except for reasonable descriptive use and
required attribution.

The project name is used descriptively for a compatible client, alongside the
prominent qualifier **Unofficial LocalSend client for Nintendo 3DS**. Because
permission to redistribute the official logo as this application's identity is
not explicit, the official raster logo is not copied into this repository or
artifact. Exact official-logo adoption remains paused unless upstream supplies
clear permission. This does not prevent a faithful interface adaptation.

## LocalSend3DS adaptation

The UI retains LocalSend's recognizable teal-led palette, quiet light surfaces,
rounded device cards, direct Receive/Send/Settings organization, prominent peer
names, simple approval actions, bounded progress bars, and plain-language
completion/error states. The layout is native to the 3DS rather than a reduced
desktop screen:

- top screen: identity, network state, selected peer, filename, progress,
  success, and errors;
- bottom touch screen: nearby devices, tabs, file browsing, recipient choice,
  incoming approval, cancellation, and About/Settings;
- physical controls mirror all essential touch actions.

`assets/branding/localsend3ds-icon.svg` is original project artwork combining a
dual-screen handheld and bidirectional local-transfer arrows. The generated
48x48 `icon.png` is embedded into `LocalSend3DS.smdh` together with the title
`LocalSend3DS`, description `Unofficial LocalSend client for Nintendo 3DS`, and
Homebrew/SMDH author `Val March`. The detailed application About view credits
`Volkan 'Val March' Söylemez`, which is also the intended public copyright and
Git author identity. The original LocalSend3DS artwork and project source are
covered by this repository's MIT License under that copyright owner.

The LocalSend3DS mark is the project's own identity. Official LocalSend logo
artwork is not copied or presented as LocalSend3DS branding, and descriptive
use of the LocalSend name does not imply affiliation or endorsement.
