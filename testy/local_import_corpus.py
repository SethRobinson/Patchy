"""Make an opt-in native import manifest from the installed local fixture collections."""

from __future__ import annotations

import argparse
import json
import ntpath
import os
from pathlib import Path, PureWindowsPath
import stat
from xml.etree import ElementTree as ET
from zipfile import BadZipFile, ZipFile

from console_output import log
from rerun import write_text

ROOT = Path(__file__).resolve().parents[1]
_XCF = (".xcf", ".xcf.gz", ".xcf.bz2", ".xcf.xz", ".xcfgz", ".xcfbz2", ".xcfxz")
_COLLECTIONS = {"xcf-corpus": _XCF, "xcf": _XCF, "kra-corpus": (".kra", ".krz"), "kra": (".kra", ".krz"),
                "ora": (".ora",), "ora-corpus": (".ora",)}
_MAX_XML = 1 << 20


def _gimp_refusals(directory: Path) -> set[str]:
    """Only a recorded load refusal excludes a file. Export failures still test opening."""
    refused = set()
    marker = "/" + directory.name + "/"
    for group in ("gimp-renders", "gimp-trees"):
        log = directory / group / "failures.txt"
        if log.is_file():
            for line in log.read_text(encoding="utf-8").splitlines():
                source, _, reason = line.rpartition(": ")
                if reason == "load returned nothing" and marker in source.replace("\\", "/"):
                    refused.add(source.replace("\\", "/").rsplit(marker, 1)[1])
    return refused


def _gimp_structure(path: Path) -> tuple[dict, str]:
    """Read dump_gimp_trees.py's top-first rows in Patchy's bottom-first sibling order."""
    lines = path.read_text(encoding="utf-8").splitlines()
    tag, width, height, _, _ = lines[0].split()
    if tag != "image":
        raise ValueError(f"Invalid GIMP tree header in {path}")
    expected = {"width": int(width), "height": int(height)}
    roots = []
    siblings = [roots]
    for i, line in enumerate(lines[1:], 2):
        depth_text, kind, mode, visible, _, _, _, _, _, mask, name = line.split("\t", 10)
        if visible == "1" and mode in ("58", "60"):
            return expected, ("Canvas dimensions only. Patchy represents GIMP's visible Erase "
                              "or Split layer using a different layer tree.")
        depth = int(depth_text)
        if not 0 <= depth < len(siblings):
            raise ValueError(f"Invalid GIMP layer depth in {path} at line {i}")
        del siblings[depth + 1:]
        node = {"name": name, "visible": visible == "1"}
        if kind in ("group", "text"):
            node["kind"] = kind
        # A true source mask should survive. Clip-to-backdrop emulation may add a mask.
        if mask == "1":
            node["hasMask"] = True
        siblings[depth].append(node)
        if kind == "group":
            node["children"] = []
            siblings.append(node["children"])

    def reverse_stack(nodes):
        nodes.reverse()
        for node in nodes:
            if "children" in node:
                reverse_stack(node["children"])

    reverse_stack(roots)
    expected["layers"] = roots
    return expected, ""


def _native_tree(stack, ora, depth=0):
    """Map plain paint nodes and editable groups. Other conversions need individual assertions."""
    if depth > 64:
        raise ValueError("the stack exceeds 64 nested groups")
    layers = []
    for element in reversed(stack):
        tag = element.tag.rsplit("}", 1)[-1]
        if ora:
            if tag not in ("layer", "stack") or (tag == "layer" and len(element)):
                raise ValueError("the OpenRaster stack contains an extension node")
            group = tag == "stack"
            subtree = element if group else None
            visible = element.get("visibility", "visible")
            if visible not in ("visible", "hidden"):
                raise ValueError("a layer has an unknown visibility value")
            shown = visible == "visible"
        else:
            kind = element.get("nodetype")
            if tag != "layer" or kind not in ("grouplayer", "paintlayer"):
                raise ValueError(f"the Krita stack contains a '{kind or tag}' node")
            if element.get("channelflags", "").endswith("0"):
                raise ValueError("Krita Inherit Alpha can regroup layers")
            group = kind == "grouplayer"
            subtree = element.find("{*}layers") if group else None
            if any(child is not subtree for child in element):
                raise ValueError("the Krita stack contains masks or layer attachments")
            visible = element.get("visible", "1")
            if visible not in ("0", "1"):
                raise ValueError("a layer has an unknown visibility value")
            shown = visible == "1"
        layer = {"kind": "group" if group else "layer", "visible": shown}
        if "opacity" in element.attrib:
            if ora:
                opacity = float(element.attrib["opacity"])
            else:
                opacity = int(element.attrib["opacity"]) / 255
            if not 0 <= opacity <= 1:
                raise ValueError("a layer has an invalid opacity")
            layer["opacity"] = opacity * 100
        # The scripting property is true for any editing lock. A source edit
        # lock is sufficient; a separate alpha lock can also make it true.
        if (ora and element.get("edit-locked") in ("true", "1")) or (not ora and element.get("locked") == "1"):
            layer["locked"] = True
        if element.get("name"):
            layer["name"] = element.get("name")
        if group:
            layer["children"] = _native_tree(subtree, ora, depth + 1) if subtree is not None else []
        layers.append(layer)
    return layers


def _companion(parent: Path, name: str) -> Path | None:
    """Only stage ordinary files below the source. Never probe an outside path or follow links."""
    name = name.replace("\\", "/")
    if not name or ":" in name or "\0" in name or name.startswith("/"):
        return None
    path = parent
    is_reserved = getattr(ntpath, "isreserved", None)
    try:
        for part in name.split("/"):
            if part in ("", "."):
                continue
            if part.endswith((" ", ".")) or (is_reserved(part) if is_reserved else PureWindowsPath(part).is_reserved()):
                return None
            path = path / part
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                return None
        if path == parent or not stat.S_ISREG(info.st_mode):
            return None
        with path.open("rb") as stream:
            header = stream.read(8)
        return path if header == b"\x89PNG\r\n\x1a\n" or header.startswith(b"\xff\xd8\xff") else None
    except OSError:
        return None


def _file_layers(image, source):
    visible = {image: True}
    missing, assets = [], []
    incomplete_render = False
    clones = any(node.get("nodetype") == "clonelayer" for node in image.iter())
    for parent in image.iter():
        for node in parent:
            visible[node] = visible[parent] and node.get("visible", "1") != "0"
            if node.get("nodetype") != "filelayer":
                continue
            asset = _companion(source.parent, node.get("source", ""))
            if asset and asset != source:
                if asset not in assets:
                    assets.append(asset)
            else:
                label = node.get("name") or "Unnamed layer"
                missing.append(f"File layer '{label}' has no available local companion ("
                               + ("visible)." if visible[node] else "hidden)."))
                incomplete_render |= visible[node] or clones
    return assets, missing, incomplete_render


def _archive_expectations(case, source, relative):
    ora = source.suffix.lower() == ".ora"
    name = "stack.xml" if ora else "maindoc.xml"
    try:
        with ZipFile(source) as archive, archive.open(name) as stream:
            data = stream.read(_MAX_XML + 1)
        if len(data) > _MAX_XML:
            # Canvas attributes precede the layer list. They remain checkable independently.
            parser = ET.XMLPullParser(events=("start",))
            parser.feed(data[:65536])
            image = next((el for _, el in parser.read_events()
                          if el.tag.rsplit("}", 1)[-1].lower() == "image"), None)
        else:
            root = ET.fromstring(data)
            image = root if ora and root.tag.rsplit("}", 1)[-1] == "image" else root.find("{*}IMAGE")
        if image is None:
            raise ValueError("canvas element missing")
        width, height = ("w", "h") if ora else ("width", "height")
        case["expect"] = {"width": int(image.attrib[width]), "height": int(image.attrib[height])}
    except (BadZipFile, KeyError, OSError, ET.ParseError, RuntimeError, ValueError) as error:
        case["referenceNote"] = f"Cannot read document properties from {name}: {error}"
        return
    if len(data) > _MAX_XML:
        case["referenceNote"] = f"Canvas dimensions only. {name} exceeds the 1 MiB layer-tree limit."
        return
    notes = []
    if not ora:
        assets, missing, incomplete = _file_layers(image, source)
        if assets:
            case["assets"] = [relative(p) for p in assets]
        notes.extend(missing)
        if incomplete:
            case.pop("reference", None)
            notes.append("Appearance is unscored because the missing file can affect the composite.")
    stack = image.find("{*}stack" if ora else "{*}layers")
    try:
        if stack is None:
            raise ValueError("the source has no layer stack")
        case["expect"]["layers"] = _native_tree(stack, ora)
    except ValueError as error:
        notes.append(f"Canvas dimensions only. No layer-tree comparison because {error}.")
    if notes:
        case["referenceNote"] = " ".join(notes)


def build_manifest(fixtures: Path, output: Path) -> tuple[dict, list[str]]:
    cases = []
    messages = []

    def relative(path):
        return Path(os.path.relpath(path, output.parent)).as_posix()

    for collection, suffixes in _COLLECTIONS.items():
        directory = fixtures / collection
        if not directory.is_dir():
            messages.append(f"{collection} is not installed")
            continue
        refusals = _gimp_refusals(directory) if collection == "xcf-corpus" else set()
        for source in sorted(directory.rglob("*")):
            if not source.name.lower().endswith(suffixes) or not source.is_file():
                continue
            within = source.relative_to(directory).as_posix()
            case = {"id": f"{collection}/{within}", "source": relative(source)}
            if collection not in ("xcf-corpus", "xcf"):
                case["reference"] = "@mergedimage"
                _archive_expectations(case, source, relative)
                render = source.with_suffix(".png")
                if collection == "ora-corpus" and render.is_file():
                    # export_corpus_to_ora.py: the writing app's own render of this file. The
                    # composite it stored was drawn from the source document, not from the stack.
                    case["reference"] = relative(render)
                # make_ora_fixtures.py exported these exact GIMP ORAs through Krita.
                # Its other Krita PNGs were exported from PSDs, not from the sibling ORAs.
                if collection == "ora" and within in ("gimp-modes.ora", "gimp-structure.ora", "gimp-structure-16.ora"):
                    png = directory / (source.stem.replace("gimp-", "krita-", 1) + ".png")
                    if png.is_file():
                        case["reopenReference"] = {"png": relative(png),
                                                   "credit": "Krita 5.3 reopening the GIMP 3.2 OpenRaster file"}
            else:
                base = within.replace("/", "__")
                png = directory / "gimp-renders" / (base + ".png")
                if collection == "xcf":
                    # make_xcf_fixtures.py saves the visible GIMP merge beside
                    # each source; the three compressed files share a reference.
                    png = source.with_name(source.name.split(".xcf", 1)[0] + ".png")
                tree = directory / "gimp-trees" / (base + ".tree.txt")
                if within in refusals and not (tree.is_file() or png.is_file()):
                    messages.append(f"Omitted {case['id']} because GIMP refused to open it")
                    continue
                if png.is_file():
                    case["reference"] = relative(png)
                if tree.is_file():
                    try:
                        case["expect"], note = _gimp_structure(tree)
                    except (ValueError, IndexError) as error:
                        raise ValueError(f"Cannot read {tree}: {error}") from error
                    if note:
                        case["referenceNote"] = note
                        messages.append(f"{case['id']}: {note}")
            cases.append(case)
    return {"name": "Local GIMP, Krita and OpenRaster corpus", "cases": cases}, messages


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixtures", type=Path, default=ROOT / "local-test-fixtures",
                        help="Parent of the local corpus directories")
    parser.add_argument("--output", type=Path, default=ROOT / "testy/corpus/native-local.json")
    args = parser.parse_args()
    manifest, messages = build_manifest(args.fixtures.resolve(), args.output.resolve())
    for message in messages:
        log(message)
    if not manifest["cases"]:
        parser.exit(1, "No native import files found in the installed local corpora.\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_text(args.output, json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    log(f"Wrote {len(manifest['cases'])} files to {args.output}")


if __name__ == "__main__":
    main()
