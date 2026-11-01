#!/usr/bin/env python3
"""Print a GIMP 3 plug-in wire log as messages (docs/gimp-plugins.md, development tools).

Reads the binary logs gimp-wire-tap.exe (scripts/dev/gimp/wire_tap.cpp) or patchy-gimp-host
(PATCHY_GIMP_WIRE_LOG) write: records of u8 direction ('H' host to plug-in, 'P' plug-in to
host), u32 length, u64 microseconds, bytes. The message grammar here is the one observed in
real GIMP 3.2.6 traffic (docs/gimp-plugins-provenance.md) and mirrors
src/plugins/gimp/gimp_wire_codec.cpp; a log that does not parse means one of them is wrong.

    python scripts/dev/gimp/wire_dump.py <log.bin> [--raw] [--tiles]
"""

import struct
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

MESSAGES = ["QUIT", "CONFIG", "TILE_REQ", "TILE_ACK", "TILE_DATA", "PROC_RUN", "PROC_RETURN",
            "TEMP_PROC_RUN", "TEMP_PROC_RETURN", "PROC_INSTALL", "PROC_UNINSTALL",
            "EXTENSION_ACK", "HAS_INIT"]
PARAM_DEF = ["DEFAULT", "INT", "UNIT", "ENUM", "CHOICE", "BOOLEAN", "DOUBLE", "STRING",
             "GEGL_COLOR", "ID", "ID_ARRAY", "EXPORT_OPTIONS", "RESOURCE", "FILE", "CURVE"]
PARAM = ["INT", "DOUBLE", "STRING", "STRV", "BYTES", "FILE", "BABL_FORMAT", "GEGL_COLOR",
         "COLOR_ARRAY", "PARASITE", "ARRAY", "ID_ARRAY", "EXPORT_OPTIONS", "PARAM_DEF",
         "VALUE_ARRAY", "CURVE"]


class Reader:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def left(self):
        return len(self.data) - self.at

    def take(self, n):
        if self.at + n > len(self.data):
            raise EOFError
        out = self.data[self.at:self.at + n]
        self.at += n
        return out

    def u8(self):
        return self.take(1)[0]

    def u32(self):
        return struct.unpack(">I", self.take(4))[0]

    def i32(self):
        return struct.unpack(">i", self.take(4))[0]

    def u64(self):
        return struct.unpack(">Q", self.take(8))[0]

    def i64(self):
        return struct.unpack(">q", self.take(8))[0]

    def f64(self):
        return struct.unpack(">d", self.take(8))[0]

    def string(self):
        n = self.u32()
        if n == 0:
            return None
        raw = self.take(n)
        return raw[:-1].decode("utf-8", "replace")

    def blob(self):
        n = self.u32()
        return self.take(n)


def read_color(r):
    size = r.u32()
    data = r.take(size)
    encoding = r.string()
    profile = r.blob()
    return {"bytes": data.hex(), "encoding": encoding, "profile": len(profile)}


def read_param_def(r):
    kind = r.u32()
    d = {"def": PARAM_DEF[kind] if kind < len(PARAM_DEF) else kind,
         "type": r.string(), "value_type": r.string(), "name": r.string(), "nick": r.string(),
         "blurb": r.string(), "flags": r.u32()}
    k = d["def"]
    if k == "INT":
        d.update(min=r.i64(), max=r.i64(), default=r.i64())
    elif k == "UNIT":
        d.update(allow_pixels=r.i32(), allow_percent=r.i32(), default=r.i32())
    elif k in ("ENUM", "BOOLEAN"):
        d.update(default=r.i32())
    elif k == "DOUBLE":
        d.update(min=r.f64(), max=r.f64(), default=r.f64())
    elif k == "STRING":
        d.update(default=r.string())
    elif k == "CHOICE":
        default = r.string()
        n = r.u32()
        choices = []
        for _ in range(n):
            choices.append({"nick": r.string(), "id": r.i32(), "label": r.string(), "help": r.string()})
        d.update(default=default, choices=choices)
    elif k == "GEGL_COLOR":
        d.update(has_alpha=r.i32(), default=read_color(r))
    elif k == "ID":
        d.update(none_ok=r.i32())
    elif k == "ID_ARRAY":
        d.update(item_type=r.string())
    elif k == "RESOURCE":
        d.update(none_ok=r.i32(), default_to_context=r.i32(), default_id=r.i32())
    elif k == "FILE":
        d.update(action=r.i32(), none_ok=r.i32(), default=r.string())
    elif k == "CURVE":
        d.update(none_ok=r.i32())
    return d


def read_param(r):
    kind = r.u32()
    p = {"t": PARAM[kind] if kind < len(PARAM) else kind, "type": r.string()}
    k = p["t"]
    if k == "INT":
        p["v"] = r.i32()
    elif k == "DOUBLE":
        p["v"] = r.f64()
    elif k in ("STRING", "FILE"):
        p["v"] = r.string()
    elif k == "STRV":
        n = r.u32()
        p["v"] = [r.string() for _ in range(n)]
    elif k == "BYTES":
        b = r.blob()
        p["v"] = b.hex() if len(b) <= 64 else f"{len(b)} bytes"
    elif k == "BABL_FORMAT":
        p["v"] = {"encoding": r.string(), "profile": len(r.blob())}
    elif k == "GEGL_COLOR":
        p["v"] = read_color(r)
    elif k == "COLOR_ARRAY":
        n = r.u32()
        p["v"] = [read_color(r) for _ in range(n)]
    elif k == "PARASITE":
        name = r.string()
        if name is None:
            p["v"] = None
        else:
            flags = r.u32()
            p["v"] = {"name": name, "flags": flags, "data": r.blob().hex()}
    elif k == "ARRAY":
        b = r.blob()
        p["v"] = b.hex() if len(b) <= 64 else f"{len(b)} bytes"
    elif k == "ID_ARRAY":
        item = r.string()
        n = r.u32()
        p["v"] = {"item_type": item, "ids": [r.i32() for _ in range(n)]}
    elif k == "EXPORT_OPTIONS":
        p["v"] = {}
    elif k == "PARAM_DEF":
        p["v"] = read_param_def(r)
    elif k == "VALUE_ARRAY":
        n = r.u32()
        p["v"] = [read_param(r) for _ in range(n)]
    elif k == "CURVE":
        curve_type = r.u32()
        n = r.u32()
        points = [r.f64() for _ in range(n * 2)]
        types = [r.u32() for _ in range(n)]
        m = r.u32()
        samples = [r.f64() for _ in range(m)]
        p["v"] = {"curve_type": curve_type, "points": points, "types": types, "samples": len(samples)}
    return p


def read_message(r, show_tiles):
    t = r.u32()
    name = MESSAGES[t] if t < len(MESSAGES) else f"type {t}"
    if name in ("QUIT", "TILE_ACK", "EXTENSION_ACK", "HAS_INIT"):
        return name, None
    if name == "CONFIG":
        # Wire order as observed: the two check colors come right after check_type,
        # ahead of the flag bytes (they were added in protocol 0x0111).
        c = {"tile_width": r.u32(), "tile_height": r.u32(), "shm_id": r.i32(),
             "check_size": r.u8(), "check_type": r.u8()}
        for i in (1, 2):
            c[f"check_custom_color{i}"] = r.blob().hex()
            c[f"check_custom_encoding{i}"] = r.string()
            c[f"check_custom_icc{i}"] = len(r.blob())
        for f in ("show_help_button", "use_cpu_accel", "use_opencl",
                  "export_color_profile", "export_comment", "export_exif", "export_xmp",
                  "export_iptc", "update_metadata"):
            c[f] = r.u8()
        c["default_display_id"] = r.i32()
        for f in ("app_name", "wm_class", "display_name"):
            c[f] = r.string()
        c["monitor_number"] = r.i32()
        c["timestamp"] = r.u32()
        c["icon_theme_dir"] = r.string()
        c["tile_cache_size"] = r.u64()
        c["swap_path"] = r.string()
        c["swap_compression"] = r.string()
        c["num_processors"] = r.i32()
        return name, c
    if name == "TILE_REQ":
        return name, {"drawable": r.i32(), "tile": r.u32(), "shadow": r.u32()}
    if name == "TILE_DATA":
        d = {"drawable": r.i32(), "tile": r.u32(), "shadow": r.u32(), "bpp": r.u32(),
             "width": r.u32(), "height": r.u32(), "use_shm": r.u32()}
        if not d["use_shm"]:
            n = d["width"] * d["height"] * d["bpp"]
            if n:
                data = r.take(n)
                d["data"] = data[:16].hex() + ("..." if n > 16 else "") if show_tiles else f"{n} bytes"
        return name, d
    if name in ("PROC_RUN", "PROC_RETURN", "TEMP_PROC_RUN", "TEMP_PROC_RETURN"):
        proc = r.string()
        n = r.u32()
        return name, {"name": proc, "params": [read_param(r) for _ in range(n)]}
    if name == "PROC_INSTALL":
        proc = r.string()
        kind = r.u32()
        n = r.u32()
        m = r.u32()
        return name, {"name": proc, "type": kind,
                      "params": [read_param_def(r) for _ in range(n)],
                      "returns": [read_param_def(r) for _ in range(m)]}
    if name == "PROC_UNINSTALL":
        return name, {"name": r.string()}
    raise ValueError(f"unknown message type {t}")


def records(path):
    data = open(path, "rb").read()
    at = 0
    while at < len(data):
        direction = chr(data[at])
        n, us = struct.unpack_from("<IQ", data, at + 1)
        yield direction, us, data[at + 13:at + 13 + n]
        at += 13 + n


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    raw = "--raw" in sys.argv
    tiles = "--tiles" in sys.argv
    streams = {"H": bytearray(), "P": bytearray()}
    order = []
    for direction, us, chunk in records(args[0]):
        streams[direction] += chunk
        order.append((direction, us, len(chunk)))
    # Messages may span records, so parse each direction as one stream and order them by
    # the record that completed each message.
    out = []
    for direction in ("H", "P"):
        r = Reader(bytes(streams[direction]))
        ends = []
        total = 0
        for d, us, n in order:
            if d == direction:
                total += n
                ends.append((total, us))
        while r.left():
            start = r.at
            try:
                name, body = read_message(r, tiles)
            except (EOFError, ValueError, IndexError) as e:
                out.append((10**18, direction, f"PARSE ERROR at byte {start}: {e!r}; next "
                            f"{bytes(r.data[start:start + 48]).hex(' ')}", None))
                break
            us = next((t for end, t in ends if end >= r.at), 0)
            out.append((us, direction, name, body if not raw else bytes(r.data[start:r.at]).hex(' ')))
    for us, direction, name, body in sorted(out, key=lambda x: x[0]):
        arrow = "host->plug-in" if direction == "H" else "plug-in->host"
        print(f"{us / 1000:10.1f} ms {arrow} {name}" + (f" {body}" if body is not None else ""))


if __name__ == "__main__":
    main()
