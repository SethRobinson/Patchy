"""Prints the structure of a GIMP .xcf file (docs/xcf.md): header, properties, parasites,
layers, channels, paths and effects. A development aid written from the published XCF
specification (developer.gimp.org/core/standards/xcf/); it reads data only.

Usage: python -I scripts/dev/xcf/xcf_dump.py FILE.xcf [--parasites]
"""

import bz2
import gzip
import lzma
import struct
import sys

PROP_NAMES = {
    0: "END", 1: "COLORMAP", 2: "ACTIVE_LAYER", 3: "ACTIVE_CHANNEL", 4: "SELECTION", 5: "FLOATING_SELECTION",
    6: "OPACITY", 7: "MODE", 8: "VISIBLE", 9: "LINKED", 10: "LOCK_ALPHA", 11: "APPLY_MASK", 12: "EDIT_MASK",
    13: "SHOW_MASK", 14: "SHOW_MASKED", 15: "OFFSETS", 16: "COLOR", 17: "COMPRESSION", 18: "GUIDES",
    19: "RESOLUTION", 20: "TATTOO", 21: "PARASITES", 22: "UNIT", 23: "PATHS", 24: "USER_UNIT", 25: "VECTORS",
    26: "TEXT_LAYER_FLAGS", 27: "OLD_SAMPLE_POINTS", 28: "LOCK_CONTENT", 29: "GROUP_ITEM", 30: "ITEM_PATH",
    31: "GROUP_ITEM_FLAGS", 32: "LOCK_POSITION", 33: "FLOAT_OPACITY", 34: "COLOR_TAG", 35: "COMPOSITE_MODE",
    36: "COMPOSITE_SPACE", 37: "BLEND_SPACE", 38: "FLOAT_COLOR", 39: "SAMPLE_POINTS", 40: "ITEM_SET",
    41: "ITEM_SET_ITEM", 42: "LOCK_VISIBILITY", 43: "SELECTED_PATH", 44: "FILTER_REGION",
    45: "FILTER_ARGUMENT", 46: "FILTER_CLIP", 47: "VECTOR_LAYER", 48: "LINK_LAYER", 49: "TRANSFORM",
}


class Reader:
    def __init__(self, data):
        self.data = data
        self.version = 0

    def u32(self, at):
        return struct.unpack_from(">I", self.data, at)[0]

    def i32(self, at):
        return struct.unpack_from(">i", self.data, at)[0]

    def f32(self, at):
        return struct.unpack_from(">f", self.data, at)[0]

    def ptr(self, at):
        if self.version >= 11:
            return struct.unpack_from(">Q", self.data, at)[0], at + 8
        return self.u32(at), at + 4

    def string(self, at):
        n = self.u32(at)
        if n == 0:
            return "", at + 4
        raw = self.data[at + 4:at + 4 + n]
        return raw.rstrip(b"\0").decode("utf-8", "replace"), at + 4 + n

    def props(self, at, indent, show_parasites):
        out = {}
        while True:
            kind = self.u32(at)
            length = self.u32(at + 4)
            payload_at = at + 8
            name = PROP_NAMES.get(kind, str(kind))
            if kind == 0:
                return out, at + 8
            # Properties whose stored length is known to be wrong.
            if kind == 1 and self.u32(payload_at) * 3 + 4 != length:
                length = self.u32(payload_at) * 3 + 4
            payload = self.data[payload_at:payload_at + length]
            text = ""
            if length == 4 and kind not in (33,):
                text = str(struct.unpack(">i", payload)[0])
            elif kind == 33 or kind == 19 or kind == 38:
                text = " ".join(f"{v:.5g}" for v in struct.unpack(f">{length // 4}f", payload))
            elif kind == 15:
                text = str(struct.unpack(">ii", payload))
            elif kind == 17:
                text = str(payload[0])
            elif kind == 21:
                text = self.parasites(payload, indent + "    ", show_parasites)
            elif kind == 18:
                text = str([(struct.unpack_from(">i", payload, i)[0], payload[i + 4]) for i in range(0, length, 5)])
            elif kind == 45:
                n, rest = self.string_in(payload, 0)
                text = f"{n} type={struct.unpack_from('>I', payload, rest)[0]} data={payload[rest + 4:rest + 40].hex()}"
            else:
                text = f"[{length} bytes] {payload[:48].hex()}"
            print(f"{indent}{name}: {text}")
            out.setdefault(kind, []).append(payload)
            at = payload_at + length

    def string_in(self, buf, at):
        n = struct.unpack_from(">I", buf, at)[0]
        return buf[at + 4:at + 4 + n].rstrip(b"\0").decode("utf-8", "replace"), at + 4 + n

    def parasites(self, payload, indent, show):
        names = []
        at = 0
        while at < len(payload):
            name, at = self.string_in(payload, at)
            flags, size = struct.unpack_from(">II", payload, at)
            at += 8
            body = payload[at:at + size]
            at += size
            names.append(f"{name}({size})")
            if show:
                print(f"{indent}parasite {name} flags={flags}:")
                try:
                    print(indent + "  " + body.decode("utf-8").replace("\n", "\n" + indent + "  "))
                except UnicodeDecodeError:
                    print(indent + "  " + body[:200].hex())
        return ", ".join(names)

    def hierarchy(self, at, indent):
        width, height, bpp = self.u32(at), self.u32(at + 4), self.u32(at + 8)
        level, _ = self.ptr(at + 12)
        lw, lh = self.u32(level), self.u32(level + 4)
        first, _ = self.ptr(level + 8)
        print(f"{indent}hierarchy {width}x{height} bpp={bpp} level {lw}x{lh} first tile @{first}")

    def channel(self, at, indent, show):
        width, height = self.u32(at), self.u32(at + 4)
        name, at = self.string(at + 8)
        print(f"{indent}channel '{name}' {width}x{height}")
        _, at = self.props(at, indent + "  ", show)
        hptr, at = self.ptr(at)
        self.hierarchy(hptr, indent + "  ")

    def layer(self, at, indent, show):
        width, height, kind = self.u32(at), self.u32(at + 4), self.u32(at + 8)
        name, at = self.string(at + 12)
        print(f"{indent}layer '{name}' {width}x{height} type={kind}")
        _, at = self.props(at, indent + "  ", show)
        hptr, at = self.ptr(at)
        mptr, at = self.ptr(at)
        if hptr:
            self.hierarchy(hptr, indent + "  ")
        if mptr:
            print(f"{indent}  mask:")
            self.channel(mptr, indent + "    ", show)
        if self.version >= 20:
            while True:
                eptr, at = self.ptr(at)
                if eptr == 0:
                    break
                self.effect(eptr, indent + "  ", show)

    def effect(self, at, indent, show):
        name, at = self.string(at)
        icon, at = self.string(at)
        op, at = self.string(at)
        version = ""
        if self.version >= 22:
            version, at = self.string(at)
        print(f"{indent}effect '{name}' op={op} version={version} icon={icon}")
        _, at = self.props(at, indent + "  ", show)
        mptr, at = self.ptr(at)
        if mptr:
            self.channel(mptr, indent + "    ", show)

    def vectors(self, at, indent, show):
        name, at = self.string(at)
        plength = self.u32(at)
        print(f"{indent}path '{name}' plength={plength}")
        _, at = self.props(at + 4, indent + "  ", show)
        version, strokes = self.u32(at), self.u32(at + 4)
        at += 8
        for _ in range(strokes):
            kind, closed, nf, np = (self.u32(at + i * 4) for i in range(4))
            at += 16
            points = []
            for _ in range(np):
                points.append((self.u32(at), self.f32(at + 4), self.f32(at + 8)))
                at += 4 + nf * 4
            print(f"{indent}  stroke closed={closed} nf={nf} points={points[:6]}{'...' if np > 6 else ''}")


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    path = sys.argv[1]
    show = "--parasites" in sys.argv
    data = open(path, "rb").read()
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)
    elif data[:3] == b"BZh":
        data = bz2.decompress(data)
    elif data[:6] == b"\xfd7zXZ\0":
        data = lzma.decompress(data)
    reader = Reader(data)
    tag = data[9:13].decode("ascii", "replace")
    reader.version = 0 if tag == "file" else int(tag[1:])
    width, height, base = reader.u32(14), reader.u32(18), reader.u32(22)
    at = 26
    precision = None
    if reader.version >= 4:
        precision = reader.u32(26)
        at = 30
    print(f"version {reader.version} ({tag}) {width}x{height} base={base} precision={precision}")
    _, at = reader.props(at, "  ", show)
    layers = []
    while True:
        p, at = reader.ptr(at)
        if p == 0:
            break
        layers.append(p)
    channels = []
    while True:
        p, at = reader.ptr(at)
        if p == 0:
            break
        channels.append(p)
    paths = []
    if reader.version >= 18:
        while True:
            p, at = reader.ptr(at)
            if p == 0:
                break
            paths.append(p)
    for p in layers:
        reader.layer(p, "  ", show)
    for p in channels:
        reader.channel(p, "  ", show)
    for p in paths:
        reader.vectors(p, "  ", show)


if __name__ == "__main__":
    main()
