#!/usr/bin/env python3
"""Validate the CWAV embedded by bannertool against its source WAV."""

import struct
import sys
import wave


EXPECTED_SAMPLE_RATE = 44100
EXPECTED_CHANNELS = 2
EXPECTED_SAMPLE_WIDTH = 2


def unpack_from(fmt, data, offset, label):
    size = struct.calcsize(fmt)
    if offset < 0 or offset + size > len(data):
        raise ValueError(f"{label} lies outside the banner")
    return struct.unpack_from(fmt, data, offset)


def validate(banner_path, wav_path):
    with open(banner_path, "rb") as source:
        banner = source.read()
    if banner[:4] != b"CBMD":
        raise ValueError("banner is not CBMD")

    (cwav_offset,) = unpack_from("<I", banner, 0x84, "CBMD CWAV offset")
    (
        magic,
        endianness,
        header_size,
        version,
        file_size,
        block_count,
        _,
    ) = unpack_from("<4sHHIIHH", banner, cwav_offset, "CWAV header")
    if magic != b"CWAV" or endianness != 0xFEFF:
        raise ValueError("embedded audio is not a little-endian CWAV")
    if header_size != 64 or version != 0x02010000 or block_count != 2:
        raise ValueError("unexpected CWAV header layout")
    if cwav_offset + file_size != len(banner):
        raise ValueError("CWAV size does not reach the end of the banner")

    info_type, _, info_offset, _ = unpack_from(
        "<HHII", banner, cwav_offset + 0x14, "CWAV INFO reference"
    )
    data_type, _, data_offset, _ = unpack_from(
        "<HHII", banner, cwav_offset + 0x20, "CWAV DATA reference"
    )
    if info_type != 0x7000 or data_type != 0x7001:
        raise ValueError("unexpected CWAV block references")

    info = cwav_offset + info_offset
    (
        info_magic,
        _,
        encoding,
        loop,
        _,
        sample_rate,
        loop_start,
        loop_end,
        _,
    ) = unpack_from("<4sIBBHIIII", banner, info, "CWAV INFO block")
    if info_magic != b"INFO" or encoding != 1:
        raise ValueError("embedded audio is not PCM16")
    if loop != 0 or loop_start != 0:
        raise ValueError("banner chime must be non-looping")

    channel_table = info + 0x1C
    (channel_count,) = unpack_from("<I", banner, channel_table, "channel count")
    if channel_count != EXPECTED_CHANNELS:
        raise ValueError("banner chime must contain exactly two PCM channels")

    data_block = cwav_offset + data_offset
    data_magic, _ = unpack_from("<4sI", banner, data_block, "CWAV DATA block")
    if data_magic != b"DATA":
        raise ValueError("missing CWAV DATA block")
    with wave.open(wav_path, "rb") as source:
        if source.getnchannels() != EXPECTED_CHANNELS:
            raise ValueError("source WAV must be PCM16 stereo (2 channels)")
        if source.getsampwidth() != EXPECTED_SAMPLE_WIDTH:
            raise ValueError("source WAV must be PCM16")
        if source.getframerate() != EXPECTED_SAMPLE_RATE:
            raise ValueError("source WAV has an unexpected sample rate")
        if source.getnframes() != loop_end:
            raise ValueError("CWAV frame count does not match source WAV")
        source_samples = source.readframes(source.getnframes())

    if sample_rate != EXPECTED_SAMPLE_RATE:
        raise ValueError("embedded CWAV has an unexpected sample rate")

    source_channels = []
    frame_width = EXPECTED_CHANNELS * EXPECTED_SAMPLE_WIDTH
    for channel in range(EXPECTED_CHANNELS):
        channel_start = channel * EXPECTED_SAMPLE_WIDTH
        source_channels.append(
            b"".join(
                source_samples[offset + channel_start : offset + channel_start + EXPECTED_SAMPLE_WIDTH]
                for offset in range(0, len(source_samples), frame_width)
            )
        )

    embedded_channels = []
    for channel in range(EXPECTED_CHANNELS):
        channel_type, _, channel_offset = unpack_from(
            "<HHI", banner, channel_table + 4 + channel * 8, "channel reference"
        )
        if channel_type != 0x7100:
            raise ValueError("unexpected CWAV channel reference")
        channel_info = channel_table + channel_offset
        sample_type, _, sample_offset = unpack_from(
            "<HHI", banner, channel_info, "sample reference"
        )
        if sample_type != 0x1F00:
            raise ValueError("unexpected CWAV sample reference")
        sample_start = data_block + 8 + sample_offset
        sample_size = loop_end * EXPECTED_SAMPLE_WIDTH
        sample_end = sample_start + sample_size
        if sample_end > len(banner):
            raise ValueError("CWAV sample data lies outside the banner")
        embedded_channels.append(banner[sample_start:sample_end])

    if embedded_channels != source_channels:
        raise ValueError("embedded CWAV PCM differs from source WAV")
    if embedded_channels[0] != embedded_channels[1]:
        raise ValueError("banner chime must be centered with identical stereo channels")

    print(
        "validated banner CWAV: "
        f"PCM16 stereo {sample_rate} Hz, {loop_end} frames, non-looping, PCM identical"
    )


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: validate_banner.py <banner.bnr> <source.wav>")
    validate(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()
