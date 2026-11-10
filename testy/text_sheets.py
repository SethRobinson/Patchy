"""Paragraph settings a type layer leaves to its sheets, read from the PSD itself.

Photoshop's scripting interface names a paragraph setting only where the layer's own
paragraph states it. Hyphenate and the line composer are usually left to the parent
sheet, and nothing reads them back without a set, which would change them
(docs/txt2-kept-settings.md, "Line composer"). The text-save leg compares exactly those
settings, so this module resolves them the way Photoshop does, from the file:

- a layer with an object in the document's `Txt2` block (addressed by the TySh
  `TextIndex`): the paragraph run's own sheet, then its parent ParagraphSheetSet entry,
  then the block's default paragraph sheet (docs/txt2.md, "Structure");
- a layer without one: the TySh EngineData paragraph run, then its normal sheet.

Only what `fill_paragraph_settings` needs is read. Layers are addressed by the same
index path Photoshop's manifest uses ("0", "2/1": top to bottom, groups counted), and a
layer whose name or paragraph count does not match the manifest is left alone, so a file
this walker misreads leaves values unknown rather than wrong.
"""

from __future__ import annotations

import struct
from pathlib import Path

from psd_sections import PsdParseError, _PSB_LONG_KEYS

_SIGNATURES = (b"8BIM", b"8B64")
_RECORD_BLOCKS = (b"Lr16", b"Lr32", b"Layr")
# Txt2 paragraph sheet keys (docs/txt2.md) and their TySh EngineData names.
_SETTINGS = (("hyphenate", "9", "AutoHyphenate"), ("everyLineComposer", "29", "EveryLineComposer"))


# ---------------------------------------------------------------- EngineData syntax

def parse_engine_data(data: bytes):
    """The PostScript-like text of a TySh EngineData or a Txt2 body as Python values:
    dicts keyed by name without the slash, lists, bools, numbers, "/name" enums and
    bytes for strings. A Txt2 body is a bare key/value sequence; EngineData is one
    dictionary."""
    position = 0
    length = len(data)

    def token():
        nonlocal position
        while position < length and data[position] in b" \t\r\n\x00":
            position += 1
        if position >= length:
            return None
        start = position
        two = data[position:position + 2]
        if two in (b"<<", b">>"):
            position += 2
            return two
        char = data[position:position + 1]
        if char in (b"[", b"]"):
            position += 1
            return char
        if char == b"(":
            position += 1
            while position < length and data[position:position + 1] != b")":
                position += 2 if data[position:position + 1] == b"\\" else 1
            position += 1
            return data[start:position]
        while position < length and data[position] not in b" \t\r\n\x00[]()<>":
            position += 1
        if position == start:
            position += 1  # a stray delimiter: skip it rather than loop forever
        return data[start:position]

    def value(first):
        if first == b"<<":
            return pairs(b">>")
        if first == b"[":
            items = []
            while True:
                item = token()
                if item is None or item == b"]":
                    return items
                items.append(value(item))
        if first.startswith(b"("):
            return first[1:-1]
        if first == b"true":
            return True
        if first == b"false":
            return False
        if first.startswith(b"/"):
            return first.decode("latin-1")
        try:
            return int(first)
        except ValueError:
            try:
                return float(first)
            except ValueError:
                return first.decode("latin-1")

    def pairs(closer):
        result = {}
        while True:
            key = token()
            if key is None or key == closer:
                return result
            following = token()
            if following is None:
                return result
            result[key.decode("latin-1").lstrip("/")] = value(following)

    start = position
    first = token()
    if first == b"<<":
        return pairs(b">>")
    position = start
    return pairs(None)


def _walk(node, *keys):
    """node[k1][k2]...: None as soon as a step is missing or of the wrong shape."""
    for key in keys:
        if isinstance(node, dict):
            node = node.get(str(key))
        elif isinstance(node, list) and isinstance(key, int) and 0 <= key < len(node):
            node = node[key]
        else:
            return None
    return node


# ---------------------------------------------------------------- PSD walk

def _blocks(data: bytes, start: int, end: int, is_psb: bool, padding: tuple[int, ...]):
    """(key, payload) for the tagged blocks in data[start:end]. Writers pad a block to 2
    or 4 bytes, or not at all; the next signature says which."""
    offset = start
    while offset + 12 <= end and data[offset:offset + 4] in _SIGNATURES:
        key = data[offset + 4:offset + 8]
        if is_psb and key in _PSB_LONG_KEYS:
            size = struct.unpack_from(">Q", data, offset + 8)[0]
            payload = offset + 16
        else:
            size = struct.unpack_from(">I", data, offset + 8)[0]
            payload = offset + 12
        if payload + size > end:
            return
        yield key, data[payload:payload + size]
        after = payload + size
        for pad in padding:
            candidate = after + (-size % pad if pad else 0)
            if candidate >= end or data[candidate:candidate + 4] in _SIGNATURES:
                after = candidate
                break
        offset = after


def _records(data: bytes, start: int, is_psb: bool) -> list[dict]:
    """The layer records starting at the layer count at `start`, in file order (bottom
    to top): {"name", "divider" (lsct type, 0 for none), "tysh" (payload or None)}."""
    count = abs(struct.unpack_from(">h", data, start)[0])
    cursor = start + 2
    channel_size = 8 if is_psb else 4
    records = []
    for _index in range(count):
        channel_count = struct.unpack_from(">H", data, cursor + 16)[0]
        cursor += 18 + channel_count * (2 + channel_size)
        if data[cursor:cursor + 4] != b"8BIM":
            raise PsdParseError("layer record has no blend-mode signature")
        cursor += 12
        extra_length = struct.unpack_from(">I", data, cursor)[0]
        cursor += 4
        extra_end = cursor + extra_length
        inner = cursor
        inner += 4 + struct.unpack_from(">I", data, inner)[0]   # layer mask data
        inner += 4 + struct.unpack_from(">I", data, inner)[0]   # blending ranges
        name_length = data[inner]
        name = data[inner + 1:inner + 1 + name_length].decode("latin-1", "replace")
        inner += (1 + name_length + 3) // 4 * 4
        record = {"name": name, "divider": 0, "tysh": None}
        for key, payload in _blocks(data, inner, extra_end, is_psb, (0, 2, 4)):
            if key == b"luni" and len(payload) >= 4:
                chars = struct.unpack_from(">I", payload, 0)[0]
                record["name"] = payload[4:4 + chars * 2].decode("utf-16-be", "replace").rstrip("\x00")
            elif key in (b"lsct", b"lsdk") and len(payload) >= 4:
                record["divider"] = struct.unpack_from(">I", payload, 0)[0]
            elif key == b"TySh":
                record["tysh"] = payload
        records.append(record)
        cursor = extra_end
    return records


def _document(data: bytes) -> tuple[list[dict], bytes | None]:
    """(layer records in file order, the Txt2 body or None)."""
    if len(data) < 34 or data[:4] != b"8BPS":
        raise PsdParseError("not a PSD")
    is_psb = struct.unpack_from(">H", data, 4)[0] == 2
    offset = 26
    offset += 4 + struct.unpack_from(">I", data, offset)[0]   # color mode data
    offset += 4 + struct.unpack_from(">I", data, offset)[0]   # image resources
    length_format, length_size = (">Q", 8) if is_psb else (">I", 4)
    section_length = struct.unpack_from(length_format, data, offset)[0]
    section_start = offset + length_size
    section_end = min(section_start + section_length, len(data))
    if section_length < length_size:
        return [], None
    info_length = struct.unpack_from(length_format, data, section_start)[0]
    info_start = section_start + length_size
    records: list[dict] = []
    if info_length >= 2:
        records = _records(data, info_start, is_psb)
    offset = info_start + info_length
    txt2 = None
    if offset + 4 <= section_end:
        offset += 4 + struct.unpack_from(">I", data, offset)[0]   # global layer mask info
        for key, payload in _blocks(data, offset, section_end, is_psb, (4, 2, 0)):
            if key in _RECORD_BLOCKS and not records and len(payload) >= 2:
                records = _records(payload, 0, is_psb)
            elif key == b"Txt2":
                txt2 = payload
    return records, txt2


def _tysh_fields(payload: bytes) -> tuple[int | None, bytes | None]:
    """(TextIndex, EngineData bytes) from a TySh block's text descriptor. The two items
    are found by their key and type tags, which is all this reader needs of the
    descriptor format."""
    index = None
    at = payload.find(b"\x00\x00\x00\x09TextIndexlong")
    if at >= 0 and at + 21 <= len(payload):
        index = struct.unpack_from(">i", payload, at + 17)[0]
    engine = None
    at = payload.find(b"\x00\x00\x00\x0aEngineDatatdta")
    if at >= 0 and at + 22 <= len(payload):
        size = struct.unpack_from(">I", payload, at + 18)[0]
        engine = payload[at + 22:at + 22 + size]
    return index, engine


def _from_txt2(root, index: int) -> list[dict] | None:
    runs = _walk(root, "1", "1", index, "0", "5", "0")
    if not isinstance(runs, list) or not runs:
        return None
    sheets = _walk(root, "0", "6", "0")
    default = _walk(root, "1", "3")
    paragraphs = []
    for run in runs:
        own = _walk(run, "0", "0", "5")
        parent_index = _walk(run, "0", "0", "6")
        parent = _walk(sheets, parent_index, "0", "5") if isinstance(parent_index, int) else None
        row = {"length": _walk(run, "1")}
        for name, key, _engine_key in _SETTINGS:
            found = None
            for sheet in (own, parent, default):
                if isinstance(sheet, dict) and isinstance(sheet.get(key), bool):
                    found = sheet[key]
                    break
            row[name] = found
        paragraphs.append(row)
    return paragraphs


def _from_engine_data(engine: bytes) -> list[dict] | None:
    root = parse_engine_data(engine)
    runs = _walk(root, "EngineDict", "ParagraphRun", "RunArray")
    if not isinstance(runs, list) or not runs:
        return None
    lengths = _walk(root, "EngineDict", "ParagraphRun", "RunLengthArray") or []
    normal_index = _walk(root, "ResourceDict", "TheNormalParagraphSheet")
    normal = (_walk(root, "ResourceDict", "ParagraphSheetSet", normal_index, "Properties")
              if isinstance(normal_index, int) else None)
    paragraphs = []
    for position, run in enumerate(runs):
        own = _walk(run, "ParagraphSheet", "Properties")
        row = {"length": lengths[position] if position < len(lengths) else None}
        for name, _key, engine_key in _SETTINGS:
            found = None
            for sheet in (own, normal):
                if isinstance(sheet, dict) and isinstance(sheet.get(engine_key), bool):
                    found = sheet[engine_key]
                    break
            row[name] = found
        paragraphs.append(row)
    return paragraphs


def paragraph_sheets(data: bytes) -> dict[str, dict]:
    """Per type layer, keyed by Photoshop's index path: {"name", "source" ("Txt2" or
    "TySh"), "paragraphs": [{"length", "hyphenate", "everyLineComposer"}]}, each value
    True, False or None (no sheet names it)."""
    records, txt2 = _document(data)
    root = parse_engine_data(txt2) if txt2 else None
    result: dict[str, dict] = {}
    prefix: list[int] = []
    counters = [0]
    for record in reversed(records):          # top to bottom, as Photoshop lists layers
        if record["divider"] == 3:            # the hidden record that closes a group
            if len(counters) > 1:
                counters.pop()
                prefix.pop()
            continue
        index = counters[-1]
        counters[-1] += 1
        if record["divider"] in (1, 2):       # a group: its layers follow
            prefix.append(index)
            counters.append(0)
            continue
        if record["tysh"] is None:
            continue
        text_index, engine = _tysh_fields(record["tysh"])
        paragraphs = _from_txt2(root, text_index) if root is not None and text_index is not None else None
        source = "Txt2"
        if paragraphs is None and engine:
            paragraphs, source = _from_engine_data(engine), "TySh"
        if paragraphs:
            result["/".join(str(step) for step in prefix + [index])] = {
                "name": record["name"], "source": source, "paragraphs": paragraphs}
    return result


def fill_paragraph_settings(psd_path: Path, layers: list[dict]) -> int:
    """Give each type layer of a Photoshop manifest (drivers/photoshop.py) the Hyphenate
    and line composer values its paragraphs leave out, from the file's own sheets.
    Returns how many values were filled. A layer is matched by its index path and must
    have the same name and paragraph count in the file; anything else, and any file the
    walker cannot read, leaves the values as Photoshop gave them (None)."""
    wanted = [layer for layer in layers
              if layer.get("kind") == "TEXT" and any(
                  paragraph.get(name) is None
                  for paragraph in layer.get("paragraphs") or [] for name, _key, _engine in _SETTINGS)]
    if not wanted:
        return 0
    try:
        sheets = paragraph_sheets(Path(psd_path).read_bytes())
    except (OSError, PsdParseError, struct.error, IndexError, ValueError, TypeError):
        return 0
    filled = 0
    for layer in wanted:
        found = sheets.get(str(layer.get("path")))
        paragraphs = layer.get("paragraphs") or []
        if not found or found["name"] != layer.get("name") or len(found["paragraphs"]) != len(paragraphs):
            continue
        for stated, sheet in zip(paragraphs, found["paragraphs"]):
            for name, _key, _engine in _SETTINGS:
                if stated.get(name) is None and sheet.get(name) is not None:
                    stated[name] = sheet[name]
                    filled += 1
    return filled
