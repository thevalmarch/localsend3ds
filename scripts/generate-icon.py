#!/usr/bin/env python3
"""Generate the 48x48 SMDH icon from the LocalSend3DS project mark."""

from pathlib import Path
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parent.parent
SCALE = 8
SIZE = 48


def scaled(value: int) -> int:
    return value * SCALE


def main() -> None:
    image = Image.new("RGBA", (scaled(SIZE), scaled(SIZE)), "#e8f2f0")
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle(
        (0, 0, scaled(SIZE - 1), scaled(SIZE - 1)),
        radius=scaled(10),
        fill="#e8f2f0",
    )
    draw.ellipse((scaled(5), scaled(5), scaled(43), scaled(43)), fill="#008f88")

    white = "#ffffff"
    line_width = scaled(3)
    draw.line((scaled(8), scaled(24), scaled(17), scaled(24)), fill=white,
              width=line_width)
    draw.line((scaled(14), scaled(21), scaled(17), scaled(24), scaled(14), scaled(27)),
              fill=white, width=line_width, joint="curve")
    draw.line((scaled(40), scaled(24), scaled(31), scaled(24)), fill=white,
              width=line_width)
    draw.line((scaled(34), scaled(21), scaled(31), scaled(24), scaled(34), scaled(27)),
              fill=white, width=line_width, joint="curve")

    draw.rounded_rectangle((scaled(19), scaled(13), scaled(29), scaled(21)),
                           radius=scaled(2), fill=white)
    draw.rounded_rectangle((scaled(19), scaled(27), scaled(29), scaled(35)),
                           radius=scaled(2), fill=white)
    draw.ellipse((scaled(23), scaled(23), scaled(25), scaled(25)), fill=white)

    image = image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    image.save(ROOT / "icon.png", optimize=True)


if __name__ == "__main__":
    main()
