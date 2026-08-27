# Branding assets

`localsend3ds-icon.svg` and the generated root `icon.png` are original
LocalSend3DS project artwork. The mark combines a dual-screen handheld with
two-way local transfer arrows and uses the teal-centered visual language of
LocalSend without copying the official LocalSend logo.

Regenerate the 48x48 SMDH input with:

```sh
python3 -m pip install Pillow
python3 scripts/generate-icon.py
```

Pillow is needed only to regenerate the tracked `icon.png`; it is not required
by the normal 3DSX or CIA build.

The official LocalSend repository is Apache-2.0, but that license's section 6
does not grant trademark permission. No separate NOTICE, asset license,
branding policy, or trademark permission was present in the upstream repository
at commit `daa652708fa44261b6805d37802a989e68ad7c7d`. Consequently, the official
logo was studied but is not redistributed here. If upstream later grants clear
permission, this project-specific mark can be revisited.
