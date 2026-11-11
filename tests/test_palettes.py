"""Check immutable palette values and Yaru text contrast without a 3DS GPU."""

from pathlib import Path
import re


ROLES = (
    "background surface surface_high primary primary_dark on_primary text "
    "muted border success error error_surface warning"
).split()

# RGB snapshots protect the established appearances and catch packed-byte errors.
EXPECTED = {
    "light": "F7FBFA E8F2F0 D9EBE8 008F88 005955 FFFFFF 181D1C "
             "5C6765 B9CCC9 2E7D32 BA1A1A FFDAD6 9A6700",
    "dark": "101919 1B2827 2B403D 63D8CA 8DE6DA 003731 E4EEEB "
            "B1C6C1 607D77 8DDA98 FFB4AB 4B2928 F2CD75",
    "oled": "000000 141918 273330 63D8CA 8DE6DA 003731 E4EEEB "
            "B1C6C1 607D77 8DDA98 FFB4AB 402322 F2CD75",
    "yaru_light": "FFFFFF F2F2F2 F6E8E3 C34113 A53610 FFFFFF 363636 "
                  "4D4D4D 808080 0B751B B52A4A F8E5EB 8A5100",
    "yaru_dark": "202020 2B2B2B 40302A E95420 FF9A70 000000 FAFAFA "
                 "E4DEDE 808080 8DDA98 F17C94 45232C F99B11",
}


def luminance(rgb):
    values = [((rgb >> shift) & 255) / 255 for shift in (16, 8, 0)]
    linear = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
              for v in values]
    return sum(w * v for w, v in zip((0.2126, 0.7152, 0.0722), linear))


def contrast(a, b):
    low, high = sorted((luminance(a), luminance(b)))
    return (high + 0.05) / (low + 0.05)


source = (Path(__file__).resolve().parents[1] / "source/ui.c").read_text()
for name, colors in EXPECTED.items():
    match = re.search(r"static const LsUiPalette " + name +
                      r"_palette = \{(.*?)\n\};", source, re.S)
    assert match, name
    entries = re.findall(r"\.(\w+) = (0x[0-9A-F]+)u", match[1])
    assert len(entries) == len(ROLES), name
    expected = dict(zip(ROLES, (int(c, 16) for c in colors.split())))
    actual = {}
    for role, value in entries:
        packed = int(value, 16)
        assert packed >> 24 == 255, (name, role)
        actual[role] = ((packed & 255) << 16) | (packed & 0xFF00) | ((packed >> 16) & 255)
    assert actual == expected, name
    if not name.startswith("yaru_"):
        continue
    for foreground in ("text", "muted", "primary_dark", "success", "error", "warning"):
        for background in ("background", "surface", "surface_high"):
            assert contrast(actual[foreground], actual[background]) >= 4.5, (
                name, foreground, background)
    for foreground, background in (("on_primary", "primary"), ("error", "error_surface")):
        assert contrast(actual[foreground], actual[background]) >= 4.5, (
            name, foreground, background)
    for foreground, background in (("border", "surface"), ("primary", "surface_high")):
        assert contrast(actual[foreground], actual[background]) >= 3, (
            name, foreground, background)

print("All five palette snapshots and Yaru contrast checks passed.")
