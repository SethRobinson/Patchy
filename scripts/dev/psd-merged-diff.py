"""Compares the merged images of two PSD/PSB files channel by channel.

    python scripts/dev/psd-merged-diff.py patchy.psd photoshop-flat.psd [more pairs...]

Reads each file's header and merged image data (raw or PackBits, 8 or 16 bits) and prints,
per pair, the color
mode, the largest and mean absolute difference per channel in 8-bit steps, and the share of
samples more than 2 and more than 8 steps apart. Used to compare the composite Patchy writes
with Photoshop's fresh flatten of the same file (scripts/dev/photoshop-flatten-check.ps1,
docs/cmyk.md). A file with a channel past its color channels (merged transparency) is matted
over white paper first, as Photoshop's flatten is. Standard library only.
"""

import struct
import sys

COLOR_CHANNELS = {1: 1, 3: 3, 4: 4, 9: 3}


def merged_planes(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"8BPS":
        raise ValueError(f"{path}: not a PSD")
    version, = struct.unpack(">H", data[4:6])
    channels, height, width, depth, mode = struct.unpack(">HIIHH", data[12:26])
    position = 26
    for _ in range(2):
        length, = struct.unpack(">I", data[position:position + 4])
        position += 4 + length
    if version == 2:
        length, = struct.unpack(">Q", data[position:position + 8])
        position += 8 + length
    else:
        length, = struct.unpack(">I", data[position:position + 4])
        position += 4 + length
    compression, = struct.unpack(">H", data[position:position + 2])
    position += 2
    sample_bytes = depth // 8
    row_bytes = width * sample_bytes
    color = COLOR_CHANNELS.get(mode, 3)
    planes = []
    kept = min(channels, color + 1)
    if compression == 0:
        for channel in range(kept):
            start = position + channel * row_bytes * height
            planes.append(data[start:start + row_bytes * height])
    elif compression == 1:
        count_size = 4 if version == 2 else 2
        counts = [int.from_bytes(data[position + i * count_size:position + (i + 1) * count_size], "big")
                  for i in range(channels * height)]
        position += channels * height * count_size
        for channel in range(channels):
            plane = bytearray()
            for row in range(height):
                packed = data[position:position + counts[channel * height + row]]
                position += counts[channel * height + row]
                unpacked = bytearray()
                i = 0
                while i < len(packed):
                    header = packed[i] - 256 if packed[i] > 127 else packed[i]
                    i += 1
                    if header >= 0:
                        unpacked += packed[i:i + header + 1]
                        i += header + 1
                    elif header != -128:
                        unpacked += bytes([packed[i]]) * (1 - header)
                        i += 1
                plane += unpacked
            if channel < kept:
                planes.append(bytes(plane))
    else:
        raise ValueError(f"{path}: compression {compression} not supported")
    if sample_bytes == 2:
        planes = [[int.from_bytes(plane[i:i + 2], "big") / 257.0 for i in range(0, len(plane), 2)]
                  for plane in planes]
    else:
        planes = [list(plane) for plane in planes]
    if len(planes) > color:
        # White is 255 in RGB and in PSD ink storage alike.
        alpha = planes.pop()
        planes = [[255.0 - (255.0 - value) * a / 255.0 for value, a in zip(plane, alpha)] for plane in planes]
    return mode, depth, width, height, planes


def main(arguments):
    if len(arguments) < 2 or len(arguments) % 2:
        print(__doc__)
        return 2
    for first, second in zip(arguments[0::2], arguments[1::2]):
        mode_a, depth_a, width_a, height_a, planes_a = merged_planes(first)
        mode_b, depth_b, width_b, height_b, planes_b = merged_planes(second)
        if (mode_a, width_a, height_a) != (mode_b, width_b, height_b):
            print(f"MISMATCH {first} mode={mode_a} {width_a}x{height_a} vs {second} mode={mode_b} "
                  f"{width_b}x{height_b}")
            continue
        parts = []
        for channel, (a, b) in enumerate(zip(planes_a, planes_b)):
            differences = [abs(x - y) for x, y in zip(a, b)]
            count = len(differences)
            parts.append(f"c{channel} max={max(differences):.1f} mean={sum(differences) / count:.2f} "
                         f">2={sum(d > 2 for d in differences) / count:.1%} "
                         f">8={sum(d > 8 for d in differences) / count:.1%}")
        print(f"{first} mode={mode_a} depth={depth_a}/{depth_b}: " + "; ".join(parts))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
