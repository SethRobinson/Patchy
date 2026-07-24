"""Minimal PSD/PSB section walker for Testy's trap-file generator.

Only structural navigation lives here: enough parsing to find where the four
header sections end and the merged-composite image-data section begins, so
staging.py can rewrite that trailing section with a sentinel fill while leaving
every layer byte untouched. This is deliberately not a PSD reader; Patchy's C++
codec remains the only real parser in the project.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass


class PsdParseError(Exception):
    pass


@dataclass
class PsdLayout:
    is_psb: bool
    channels: int
    height: int
    width: int
    depth: int  # bits per channel: 1, 8, 16, 32
    color_mode: int
    image_data_offset: int  # file offset of the compression u16 that starts the image-data section
    # Layer records in the file: 0 for a flattened document (the merged composite is
    # its only pixel data), None when the inner layer-info structure was unreadable.
    layer_count: int | None


# Additional-layer-info keys whose length field is 8 bytes in PSB files.
_PSB_LONG_KEYS = {b"LMsk", b"Lr16", b"Lr32", b"Layr", b"Mt16", b"Mt32", b"Mtrn",
                  b"Alph", b"FMsk", b"lnk2", b"FEid", b"FXid", b"PxSD"}
# Blocks that hold the real layer records when the standard layer info is empty.
_LAYER_RECORD_KEYS = (b"Lr16", b"Lr32", b"Layr")


def _read_layer_count(data: bytes, start: int, section_length: int, is_psb: bool) -> int | None:
    """Layer-record count inside the layer-and-mask section starting at `start`.

    0 means a genuinely flattened file. 16/32-bit documents leave the standard
    layer info empty and keep their layer records in an additional-info block
    (Lr16/Lr32), so an empty standard block alone proves nothing; the additional
    blocks are walked before concluding 0. None = structure not understood.
    """
    if section_length == 0:
        return 0
    end = min(start + section_length, len(data))
    length_size = 8 if is_psb else 4
    if section_length < length_size or start + length_size > len(data):
        return None
    info_length = struct.unpack_from(">Q" if is_psb else ">I", data, start)[0]
    if info_length >= 2:
        if start + length_size + 2 > len(data):
            return None
        # Negative means the first alpha channel holds merged transparency.
        return abs(struct.unpack_from(">h", data, start + length_size)[0])
    if info_length != 0:
        return None

    # Empty standard block: walk global mask info, then the additional blocks.
    offset = start + length_size
    if offset + 4 > end:
        return 0  # nothing after the empty block: flattened
    offset += 4 + struct.unpack_from(">I", data, offset)[0]
    while offset + 12 <= end:
        if data[offset:offset + 4] not in (b"8BIM", b"8B64"):
            return None  # lost the structure; claim nothing
        key = data[offset + 4:offset + 8]
        if is_psb and key in _PSB_LONG_KEYS:
            if offset + 16 > end:
                return None
            block_length = struct.unpack_from(">Q", data, offset + 8)[0]
            data_start = offset + 16
        else:
            block_length = struct.unpack_from(">I", data, offset + 8)[0]
            data_start = offset + 12
        if key in _LAYER_RECORD_KEYS:
            if block_length >= 2 and data_start + 2 <= len(data):
                return abs(struct.unpack_from(">h", data, data_start)[0])
            return None
        pad = 4 if is_psb else 2
        offset = data_start + block_length + (-block_length % pad)
    # Walked cleanly to the end without meeting a layer-record block: flattened.
    return 0 if end - offset < 12 else None


def read_layout(data: bytes) -> PsdLayout:
    """Walk the fixed header and the three length-prefixed sections."""
    if len(data) < 26 + 4 + 4 + 4:
        raise PsdParseError("file too small to be a PSD")
    signature, version = struct.unpack_from(">4sH", data, 0)
    if signature != b"8BPS":
        raise PsdParseError("missing 8BPS signature")
    if version not in (1, 2):
        raise PsdParseError(f"unknown PSD version {version}")
    is_psb = version == 2
    channels, height, width, depth, color_mode = struct.unpack_from(">HIIHH", data, 12)
    offset = 26

    color_mode_length = struct.unpack_from(">I", data, offset)[0]
    offset += 4 + color_mode_length

    if offset + 4 > len(data):
        raise PsdParseError("truncated before image resources")
    resources_length = struct.unpack_from(">I", data, offset)[0]
    offset += 4 + resources_length

    if is_psb:
        if offset + 8 > len(data):
            raise PsdParseError("truncated before layer info")
        layer_info_length = struct.unpack_from(">Q", data, offset)[0]
        section_start = offset + 8
    else:
        if offset + 4 > len(data):
            raise PsdParseError("truncated before layer info")
        layer_info_length = struct.unpack_from(">I", data, offset)[0]
        section_start = offset + 4
    layer_count = _read_layer_count(data, section_start, layer_info_length, is_psb)
    offset = section_start + layer_info_length

    if offset + 2 > len(data):
        raise PsdParseError("truncated before image data")
    return PsdLayout(
        is_psb=is_psb,
        channels=channels,
        height=height,
        width=width,
        depth=depth,
        color_mode=color_mode,
        image_data_offset=offset,
        layer_count=layer_count,
    )


# Magenta: maximally unlike real design content and trivially detectable in renders.
SENTINEL_RGB = (255, 0, 255)


def sentinel_composite_bytes(layout: PsdLayout) -> bytes:
    """A raw (compression 0) merged composite filled with the sentinel color.

    Channel order in the composite section is R, G, B[, extra alpha/spot planes...]
    for RGB documents; extra channels are written fully opaque. Non-RGB modes get an
    alternating light/dark per-channel fill, which is still unmistakably wrong on
    screen without pretending to know each mode's semantics.
    """
    plane_values: list[int] = []
    for channel_index in range(layout.channels):
        if layout.color_mode == 3 and channel_index < 3:  # RGB
            plane_values.append(SENTINEL_RGB[channel_index])
        else:
            plane_values.append(255)

    pixels_per_plane = layout.width * layout.height
    parts = [struct.pack(">H", 0)]  # compression 0 = raw
    for value in plane_values:
        if layout.depth == 8:
            parts.append(bytes([value]) * pixels_per_plane)
        elif layout.depth == 16:
            parts.append(struct.pack(">H", value * 257) * pixels_per_plane)
        elif layout.depth == 32:
            parts.append(struct.pack(">f", value / 255.0) * pixels_per_plane)
        elif layout.depth == 1:
            row_bytes = (layout.width + 7) // 8
            parts.append((b"\xff" if value >= 128 else b"\x00") * (row_bytes * layout.height))
        else:
            raise PsdParseError(f"unsupported depth {layout.depth}")
    return b"".join(parts)


def write_sentinel_composite(source_path: str, output_path: str) -> PsdLayout:
    """Copy source_path to output_path with the merged composite replaced by sentinel.

    Everything before the image-data section is byte-identical to the original, so the
    editors under test see the exact original layer data; only the baked flat preview
    changes. An editor whose "render" shows magenta was displaying that preview.
    """
    with open(source_path, "rb") as f:
        data = f.read()
    layout = read_layout(data)
    with open(output_path, "wb") as f:
        f.write(data[: layout.image_data_offset])
        f.write(sentinel_composite_bytes(layout))
    return layout
