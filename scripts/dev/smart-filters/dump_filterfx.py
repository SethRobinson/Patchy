"""Prints the filterFX descriptor of every SoLd block in a PSD/PSB.

usage: python dump_filterfx.py file.psd [...]

A minimal Action Manager descriptor reader (enough for Smart Filter
records); unknown value types stop the dump of that block with a note.
"""

import struct
import sys


class Reader:
    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def u32(self):
        v = struct.unpack_from(">I", self.data, self.pos)[0]
        self.pos += 4
        return v

    def i32(self):
        v = struct.unpack_from(">i", self.data, self.pos)[0]
        self.pos += 4
        return v

    def f64(self):
        v = struct.unpack_from(">d", self.data, self.pos)[0]
        self.pos += 8
        return v

    def raw(self, n):
        v = self.data[self.pos:self.pos + n]
        self.pos += n
        return v

    def unicode(self):
        n = self.u32()
        return self.raw(2 * n).decode("utf-16-be").rstrip("\x00")

    def ident(self):
        n = self.u32()
        if n == 0:
            return self.raw(4).decode("latin-1")
        return "$" + self.raw(n).decode("latin-1")


def read_value(r, kind):
    if kind in ("Objc", "GlbO"):
        return read_descriptor(r)
    if kind == "VlLs":
        count = r.u32()
        return [read_value(r, r.raw(4).decode("latin-1")) for _ in range(count)]
    if kind == "doub":
        return r.f64()
    if kind == "UntF":
        unit = r.raw(4).decode("latin-1")
        return (unit, r.f64())
    if kind == "TEXT":
        return r.unicode()
    if kind == "enum":
        return ("enum", r.ident(), r.ident())
    if kind == "long":
        return r.i32()
    if kind == "comp":
        return struct.unpack(">q", r.raw(8))[0]
    if kind == "bool":
        return r.raw(1)[0] != 0
    if kind == "type" or kind == "GlbC":
        return ("class", r.unicode(), r.ident())
    if kind == "tdta":
        n = r.u32()
        return ("tdta", len(r.raw(n)))
    if kind == "UnFl":
        unit = r.raw(4).decode("latin-1")
        count = r.u32()
        return (unit, [r.f64() for _ in range(count)])
    raise ValueError("unhandled type " + kind)


def read_descriptor(r):
    name = r.unicode()
    cls = r.ident()
    count = r.u32()
    items = []
    for _ in range(count):
        key = r.ident()
        kind = r.raw(4).decode("latin-1")
        items.append((key, kind, read_value(r, kind)))
    return {"name": name, "class": cls, "items": items}


def show(desc, indent=0):
    pad = "  " * indent
    print(f"{pad}<{desc['class']}> {desc['name']!r}")
    for key, kind, value in desc["items"]:
        if isinstance(value, dict):
            print(f"{pad}  {key!r} {kind}:")
            show(value, indent + 2)
        elif isinstance(value, list):
            print(f"{pad}  {key!r} {kind} [{len(value)}]:")
            for item in value:
                if isinstance(item, dict):
                    show(item, indent + 2)
                else:
                    print(f"{pad}    {item!r}")
        else:
            print(f"{pad}  {key!r} {kind} = {value!r}")


def find_key(desc, key):
    for k, kind, value in desc["items"]:
        if k == key:
            return value
    return None


def main():
    for path in sys.argv[1:]:
        data = open(path, "rb").read()
        print("==", path)
        pos = 0
        while True:
            i = data.find(b"8BIMSoLd", pos)
            if i < 0:
                break
            pos = i + 8
            r = Reader(data, i + 12)
            tag = r.raw(4)
            version = r.u32()
            desc_version = r.u32()
            try:
                desc = read_descriptor(r)
            except Exception as error:  # noqa: BLE001
                print("  parse stopped:", error)
                continue
            fx = find_key(desc, "$filterFX")
            if fx is None:
                print("  (no filterFX)")
                continue
            show(fx, 1)


if __name__ == "__main__":
    main()
