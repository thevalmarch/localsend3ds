#!/usr/bin/env python3
"""Generate the final LocalSend3DS static banner and original menu chime."""

import binascii
import math
import struct
import sys
import wave
import zlib


WIDTH = 256
HEIGHT = 128

# These are the same colors used by the in-application Citro2D interface.
BACKGROUND = (247, 251, 250, 255)  # #F7FBFA
SURFACE = (232, 242, 240, 255)  # #E8F2F0
SURFACE_HIGH = (217, 235, 232, 255)  # #D9EBE8
PRIMARY = (0, 143, 136, 255)  # #008F88
PRIMARY_DARK = (0, 89, 85, 255)  # #005955
TEXT_MUTED = (92, 103, 101, 255)  # #5C6765
BORDER = (185, 204, 201, 255)  # #B9CCC9

SAMPLE_RATE = 44100
SOUND_DURATION_SECONDS = 0.64
CHIME_GAIN = 2.27443


FONT_5X7 = {
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "3": ("11110", "00001", "00001", "01110", "00001", "00001", "11110"),
    "o": ("00000", "00000", "01110", "10001", "10001", "10001", "01110"),
    "c": ("00000", "00000", "01111", "10000", "10000", "10000", "01111"),
    "a": ("00000", "00000", "01110", "00001", "01111", "10001", "01111"),
    "l": ("11000", "01000", "01000", "01000", "01000", "01000", "11100"),
    "e": ("00000", "00000", "01110", "10001", "11111", "10000", "01111"),
    "n": ("00000", "00000", "11110", "10001", "10001", "10001", "10001"),
    "d": ("00001", "00001", "01111", "10001", "10001", "10001", "01111"),
}

FONT_3X5 = {
    "A": ("010", "101", "111", "101", "101"),
    "D": ("110", "101", "101", "101", "110"),
    "E": ("111", "100", "110", "100", "111"),
    "I": ("111", "010", "010", "010", "111"),
    "N": ("101", "111", "111", "111", "101"),
    "O": ("010", "101", "101", "101", "010"),
    "S": ("011", "100", "010", "001", "110"),
    "T": ("111", "010", "010", "010", "010"),
    "3": ("110", "001", "010", "001", "110"),
    " ": ("000", "000", "000", "000", "000"),
}


def png_chunk(kind, payload):
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)
    )


def paeth_predictor(left, above, upper_left):
    estimate = left + above - upper_left
    left_distance = abs(estimate - left)
    above_distance = abs(estimate - above)
    upper_left_distance = abs(estimate - upper_left)
    if left_distance <= above_distance and left_distance <= upper_left_distance:
        return left
    if above_distance <= upper_left_distance:
        return above
    return upper_left


def load_rgba_png(path):
    """Decode the project's fixed RGBA8 icon without external dependencies."""
    with open(path, "rb") as source:
        data = source.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")

    position = 8
    compressed = bytearray()
    width = height = None
    while position < len(data):
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4 : position + 8]
        payload = data[position + 8 : position + 8 + length]
        position += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if (depth, color_type, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise ValueError(f"{path}: expected non-interlaced RGBA8 PNG")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break

    if width is None or height is None:
        raise ValueError(f"{path}: missing IHDR")
    raw = zlib.decompress(bytes(compressed))
    stride = width * 4
    if len(raw) != (stride + 1) * height:
        raise ValueError(f"{path}: unexpected decompressed size")

    pixels = bytearray(width * height * 4)
    previous = bytearray(stride)
    source_offset = 0
    for y in range(height):
        filter_type = raw[source_offset]
        source_offset += 1
        scanline = bytearray(raw[source_offset : source_offset + stride])
        source_offset += stride
        for index in range(stride):
            left = scanline[index - 4] if index >= 4 else 0
            above = previous[index]
            upper_left = previous[index - 4] if index >= 4 else 0
            if filter_type == 1:
                scanline[index] = (scanline[index] + left) & 0xFF
            elif filter_type == 2:
                scanline[index] = (scanline[index] + above) & 0xFF
            elif filter_type == 3:
                scanline[index] = (scanline[index] + ((left + above) // 2)) & 0xFF
            elif filter_type == 4:
                scanline[index] = (
                    scanline[index] + paeth_predictor(left, above, upper_left)
                ) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"{path}: unsupported PNG filter {filter_type}")
        row_offset = y * stride
        pixels[row_offset : row_offset + stride] = scanline
        previous = scanline
    return width, height, pixels


def write_rgba_png(path, width, height, pixels):
    stride = width * 4
    raw = b"".join(
        b"\x00" + bytes(pixels[y * stride : (y + 1) * stride]) for y in range(height)
    )
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    data += png_chunk(b"IDAT", zlib.compress(raw, 9))
    data += png_chunk(b"IEND", b"")
    with open(path, "wb") as output:
        output.write(data)


def make_canvas(color):
    return bytearray(color * (WIDTH * HEIGHT))


def put_pixel(canvas, x, y, color):
    if 0 <= x < WIDTH and 0 <= y < HEIGHT:
        offset = (y * WIDTH + x) * 4
        canvas[offset : offset + 4] = bytes(color)


def fill_rect(canvas, x, y, width, height, color):
    for pixel_y in range(max(0, y), min(HEIGHT, y + height)):
        for pixel_x in range(max(0, x), min(WIDTH, x + width)):
            put_pixel(canvas, pixel_x, pixel_y, color)


def fill_circle(canvas, center_x, center_y, radius, color):
    radius_squared = radius * radius
    for y in range(center_y - radius, center_y + radius + 1):
        for x in range(center_x - radius, center_x + radius + 1):
            if (x - center_x) ** 2 + (y - center_y) ** 2 <= radius_squared:
                put_pixel(canvas, x, y, color)


def fill_rounded_rect(canvas, x, y, width, height, radius, color):
    fill_rect(canvas, x + radius, y, width - 2 * radius, height, color)
    fill_rect(canvas, x, y + radius, width, height - 2 * radius, color)
    fill_circle(canvas, x + radius, y + radius, radius, color)
    fill_circle(canvas, x + width - radius - 1, y + radius, radius, color)
    fill_circle(canvas, x + radius, y + height - radius - 1, radius, color)
    fill_circle(canvas, x + width - radius - 1, y + height - radius - 1, radius, color)


def composite_rgba(canvas, source_width, source_height, source, destination_x, destination_y):
    for y in range(source_height):
        for x in range(source_width):
            source_offset = (y * source_width + x) * 4
            source_color = source[source_offset : source_offset + 4]
            alpha = source_color[3]
            target_x = destination_x + x
            target_y = destination_y + y
            if alpha == 255:
                put_pixel(canvas, target_x, target_y, source_color)
            elif alpha != 0 and 0 <= target_x < WIDTH and 0 <= target_y < HEIGHT:
                target_offset = (target_y * WIDTH + target_x) * 4
                inverse = 255 - alpha
                for channel in range(3):
                    canvas[target_offset + channel] = (
                        source_color[channel] * alpha
                        + canvas[target_offset + channel] * inverse
                        + 127
                    ) // 255
                canvas[target_offset + 3] = 255


def draw_text(canvas, x, y, text, font, scale, spacing, color):
    cursor_x = x
    for character in text:
        glyph = font.get(character)
        if glyph is None:
            raise ValueError(f"missing banner font glyph: {character!r}")
        glyph_width = len(glyph[0])
        for glyph_y, row in enumerate(glyph):
            for glyph_x, value in enumerate(row):
                if value == "1":
                    fill_rect(
                        canvas,
                        cursor_x + glyph_x * scale,
                        y + glyph_y * scale,
                        scale,
                        scale,
                        color,
                    )
        cursor_x += glyph_width * scale + spacing
    return cursor_x - spacing


def draw_text_with_keyline(
    canvas, x, y, text, font, scale, spacing, text_color, keyline_color
):
    """Draw a crisp one-pixel keyline without adding a panel or soft glow."""
    for offset_y in (-1, 0, 1):
        for offset_x in (-1, 0, 1):
            if offset_x != 0 or offset_y != 0:
                draw_text(
                    canvas,
                    x + offset_x,
                    y + offset_y,
                    text,
                    font,
                    scale,
                    spacing,
                    keyline_color,
                )
    return draw_text(canvas, x, y, text, font, scale, spacing, text_color)


def apply_rounded_icon_mask(width, height, pixels, radius):
    """Restore the transparent rounded corners described by the source SVG."""
    result = bytearray(pixels)
    for y in range(height):
        for x in range(width):
            center_x = radius if x < radius else width - radius - 1
            center_y = radius if y < radius else height - radius - 1
            in_corner = (x < radius or x >= width - radius) and (
                y < radius or y >= height - radius
            )
            if in_corner and (x - center_x) ** 2 + (y - center_y) ** 2 > radius * radius:
                result[(y * width + x) * 4 + 3] = 0
    return result


def make_png(icon_path, output_path):
    icon_width, icon_height, icon_pixels = load_rgba_png(icon_path)
    if (icon_width, icon_height) != (48, 48):
        raise ValueError(f"{icon_path}: expected the 48x48 LocalSend3DS icon")

    canvas = make_canvas((0, 0, 0, 0))

    # The banner is intentionally only the existing project logo and wordmark.
    # The icon PNG was flattened against mint when generated, so recover the
    # rounded transparent corners specified by the original SVG before placing it.
    icon_pixels = apply_rounded_icon_mask(icon_width, icon_height, icon_pixels, 10)
    composite_rgba(canvas, icon_width, icon_height, icon_pixels, 25, 40)
    draw_text_with_keyline(
        canvas,
        87,
        57,
        "LocalSend3DS",
        FONT_5X7,
        2,
        2,
        PRIMARY,
        PRIMARY_DARK,
    )

    write_rgba_png(output_path, WIDTH, HEIGHT, canvas)


def smooth_note(time_seconds, frequency, start, duration, amplitude):
    local_time = time_seconds - start
    if local_time < 0.0 or local_time >= duration:
        return 0.0
    attack = 0.032
    release = min(0.26, duration * 0.70)
    if local_time < attack:
        phase = local_time / attack
        envelope = math.sin(phase * math.pi * 0.5) ** 2
    elif local_time > duration - release:
        phase = (local_time - (duration - release)) / release
        envelope = math.cos(phase * math.pi * 0.5) ** 2
    else:
        envelope = 1.0
    envelope *= math.exp(-1.15 * local_time / duration)
    fundamental = math.sin(2.0 * math.pi * frequency * local_time)
    second_harmonic = math.sin(4.0 * math.pi * frequency * local_time)
    return amplitude * envelope * (fundamental + 0.06 * second_harmonic)


def make_wav(path):
    frame_count = round(SAMPLE_RATE * SOUND_DURATION_SECONDS)
    frames = bytearray()
    for frame_index in range(frame_count):
        time_seconds = frame_index / SAMPLE_RATE
        # Keep a quiet D4-F#4 underlay beneath the primary D5-F#5 dyad. The
        # octave voicing preserves the warm identity while moving most energy
        # into the range reproduced efficiently by the small 3DS speakers.
        sample = 0.25 * smooth_note(time_seconds, 293.66, 0.035, 0.410, 0.120)
        sample += 0.25 * smooth_note(time_seconds, 369.99, 0.080, 0.430, 0.100)
        sample += smooth_note(time_seconds, 587.32, 0.035, 0.410, 0.120)
        sample += smooth_note(time_seconds, 739.98, 0.080, 0.430, 0.100)
        sample *= CHIME_GAIN
        sample = max(-0.92, min(0.92, sample))
        encoded_sample = struct.pack("<h", round(sample * 32767.0))
        frames.extend(encoded_sample)
        frames.extend(encoded_sample)

    with wave.open(path, "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(frames)


def main():
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: generate_assets.py <LocalSend3DS-icon.png> <banner.png> <banner.wav>"
        )
    make_png(sys.argv[1], sys.argv[2])
    make_wav(sys.argv[3])


if __name__ == "__main__":
    main()
