"""Fetch pinned import fixtures and write Testy's native manifest and Photoshop corpus list."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
from urllib.request import Request, urlopen
from zipfile import BadZipFile, ZipFile

import config
from console_output import log


FIXTURES = config.REPO_ROOT / "local-test-fixtures" / "testy-native"
CORPORA = config.TESTY_ROOT / "corpus"
PHOTO_REPO = "storytold/photocraft-corpus"
PHOTO_PIN = "aeb450e694d0238dd317cb5c36ddca757d0acf44"
ASE_REPO = "alpine-alpaca/asefile"
ASE_PIN = "2274c354cea6764f85597252a0d2228e64709348"
ASEPRITE_REPO = "aseprite/aseprite"
ASEPRITE_PIN = "eccf4ec292f66c0b8a4f814954ce7db160a8eb43"
KRA_REPO = "KDE/krita"
KRA_PIN = "ab3502b67a5b07771d3127aeaf9cc522af5d626a"
PHOTO_DIR = FIXTURES / ("photocraft-" + PHOTO_PIN[:12])
ASE_DIR = FIXTURES / ("asefile-" + ASE_PIN[:12])
ASEPRITE_DIR = FIXTURES / ("aseprite-" + ASEPRITE_PIN[:12])
KRA_DIR = FIXTURES / ("krita-" + KRA_PIN[:12])

# Each digest pins the sorted "sha256  relative/path\n" inventory of the selected files.
# This checks missing/extra files and existing contents offline, independently of GitHub's
# archive compression. No code from an upstream project is extracted.
PHOTO_INCLUDE = re.compile(r"(?:LICENSE-(?:APACHE|MIT)|NOTICE|README\.md|SHA256SUMS|"
                           r"photoshop/(?:[\w-]+/)+[\w.-]+\.psd|"
                           r"gimp/[\w-]+\.(?:png|xcf(?:\.(?:gz|bz2|xz))?))", re.ASCII)
PHOTO_HASH = "0a5485a9ef9a886f45e10f1311d55f1221acf64fc2464428ad096f50517dc563"
ASE_INCLUDE = re.compile(r"(?:LICENSE|tests/data/[\w-]+\.(?:aseprite|png))", re.ASCII)
ASE_HASH = "080dc73d0fa09915568b0c7653951a33fe9e84495a95d9552d274273b8bea457"
KRA_FILES = {
    "libs/ui/tests/data/flood_fill_16bit.kra":
        "ff08d18a1e4f0ad56155272e066fb826e3c2185987240bafd603f7fd466ab3b1",
    "plugins/impex/libkra/tests/data/load_test_animation.kra":
        "1c457a52120b6167f1ee5a6235bcb5d9aa425a77607d2b0e8475d912f990b323",
    "plugins/impex/libkra/tests/data/single_layer_no_channel_flags_nontransp_def_pixel.kra":
        "60602d197c9c8351a3b6e0868cc1270ed6ceb9992bd99ec2135a06dc6f09c05e",
}
ASEPRITE_FILES = {
    "tests/LICENSE.txt": "19cd6155257b0d73dd531dbcf9dc5653e86a9e9920724b808e2d818c26819d56",
    "tests/sprites/README.md": "d962380166a43866d6f05e2a644902ae5dca4b57689267d82c91790bbdbcb2dd",
    "tests/sprites/1empty3.aseprite": "daf8e979ea8fcd4c1fbb7fef01fe6b5f7d2d67bcaf865c66716ad11807822fcf",
    "tests/sprites/2f-index-3x3.aseprite": "05cc2706363f7b3a0d0be280b3f3165853c8306232d5bb8129fbdeab9b1d3432",
    "tests/sprites/2x2tilemap2x2tile.aseprite": "fbd2a4805c9adcc8be0f367332186236e053170571b69cb6c49bf4108ac5996e",
    "tests/sprites/2x3tilemap-indexed.aseprite": "890124153f1ef51516bf8e4ae00cb7cdc1ef429cfcaa32226742268b48e61018",
    "tests/sprites/3x2tilemap-grayscale.aseprite": "8789ed762aab045e8466195917590d241942141a8269ede0474e32598eb609d8",
    "tests/sprites/4f-index-4x4.aseprite": "424debfcdfbb68b99ea4a5a4476a8beb9d8e47f6ae62ffa0c9a7baf4b8651338",
    "tests/sprites/abcd.aseprite": "e802c34fd630ca48a4711c6ac951b07ab0fab5fa037d74fe4f5dd2f7d44dd94b",
    "tests/sprites/bg-index-3.aseprite": "fd0a083aa13b396f6706cdc5d8567030192c829cf050543c4ac9d6d50cb772ac",
    "tests/sprites/cut_paste.aseprite": "6f44171c805b6c593f67c6e3dcfbe29e31a0b6f0fe833c1b0e3b24cadb59889b",
    "tests/sprites/file-tests-props.aseprite": "657e2549cd74a94137aec6502957d18662f4e4d50f565009d60f9a4189b22087",
    "tests/sprites/groups2.aseprite": "55ce0dd4ba59a7a8549ccd272d878a58c69aa3edf65d75e6f68ac205df21ff19",
    "tests/sprites/groups3abc.aseprite": "6111eee8c2e09dc6a1ffb5300767411470c47975ed8bc0e251dccbffe4d0673f",
    "tests/sprites/link.aseprite": "b97ffaee6943f5f9a895827925dc3bc898c1f13ae284da8aa6ccd24c7c8aa765",
    "tests/sprites/point2frames.aseprite": "f3c4593f386eabdb07e389ca3d6d91b3c28f707b28a24ab7ec08175b955020c7",
    "tests/sprites/point4frames.aseprite": "da1d4ca0dc1efb194e8bbefd15fd35d9eaeacc114f50012d56a223453ae35e85",
    "tests/sprites/slices-moving.aseprite": "1ee4025d891b7a943fb0ef98ba30e2c9fe61b7987c8b91d85ff28511da3ebe69",
    "tests/sprites/slices.aseprite": "4215a4967b83533c3bcaad8a23e76f12a6b2049081eda730d0e6d38f78f76582",
    "tests/sprites/tags3.aseprite": "d7729944e04e21c764e3791a64d4802a6a8529b5d1354f4b98cc9ad0488d8644",
    "tests/sprites/tags3x123reps.aseprite": "93c0613b9cbe784368d6ab4e914abbd9deb48599541d7fb78eaded8bb8e51272",
    "tests/sprites/z-order.aseprite": "c99a296e87d3ed1393926e2fb06a52dccf7cf09054355c4ea22c8c5ab01f0e6e",
}


def download(url):
    request = Request(url, headers={"User-Agent": "Patchy-Testy-corpus"})
    with urlopen(request, timeout=90) as source:
        return source.read()


def write_atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = tempfile.NamedTemporaryFile(dir=path.parent, delete=False)
    name = Path(tmp.name)
    try:
        with tmp:
            tmp.write(data)
        os.replace(name, path)
    finally:
        name.unlink(missing_ok=True)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inventory_hash(hashes):
    return digest("".join(f"{hashes[name]}  {name}\n" for name in sorted(hashes)).encode("utf-8"))


def fetch_pack(repo, pin, root, include, expected, offline):
    files = {p.relative_to(root).as_posix(): digest(p.read_bytes()) for p in root.rglob("*")
             if p.is_file() and include.fullmatch(p.relative_to(root).as_posix())}
    if inventory_hash(files) == expected:
        return
    if offline:
        raise ValueError(f"Missing or changed fixtures in {root}. Run once without --offline.")
    log(f"Fetching {repo} at {pin[:12]} ...")
    prefix = repo.split("/")[1] + "-" + pin + "/"
    with ZipFile(io.BytesIO(download(f"https://codeload.github.com/{repo}/zip/{pin}"))) as archive:
        names = sorted(p[len(prefix):] for p in archive.namelist()
                       if p.startswith(prefix) and include.fullmatch(p[len(prefix):]))
        hashes = {name: digest(archive.read(prefix + name)) for name in names}
        if inventory_hash(hashes) != expected:
            raise ValueError(f"Downloaded {repo} fixture inventory does not match the pin")
        for name, checksum in files.items():
            if hashes.get(name) != checksum:
                raise ValueError(f"Unexpected or modified fixture in {root}: {name}")
        for name in names:
            if name not in files:
                write_atomic(root / name, archive.read(prefix + name))


def fetch_files(repo, pin, root, files, offline):
    """Fetch only the allowlisted test data from the full editor repositories."""
    for name, checksum in files.items():
        dest = root / Path(name).name
        if dest.is_file():
            if digest(dest.read_bytes()) != checksum:
                raise ValueError(f"Modified {repo} fixture: {dest}")
            continue
        if offline:
            raise ValueError(f"Missing {dest}. Run once without --offline.")
        data = download(f"https://raw.githubusercontent.com/{repo}/{pin}/{name}")
        if digest(data) != checksum:
            raise ValueError(f"{repo} fixture checksum mismatch: {name}")
        write_atomic(dest, data)


def rel(path):
    return Path(os.path.relpath(path, CORPORA)).as_posix()


# Independent expectations from the pinned producer's tools/gimp-xcf/xcf_fixtures.py
# and historical_2_10_32.provenance.json. Layers are in bottom-to-top script order.
XCF_LAYERS = {
    "mask": [{"name": "Background", "hasMask": False}, {"name": "Masked", "hasMask": True},
             {"name": "Mask disabled", "hasMask": True}],
    "text": [{"name": "Background"}, {"name": "XCF", "kind": "text"}],
    "text-unicode-fixed-markup": [{"name": "Procedural background"}, {"kind": "text"}],
    "historical-2-10-32-legacy-text-path": [{"name": "Legacy XCF", "kind": "text"}],
    "paths-fractional-open-closed": [{"name": "Procedural background"}],
    "vector-solid-dashed-stroke": [{"name": "Procedural background"}, {"kind": "shape"}],
    "gray16": [{"name": "Background"}, {"name": "Gray alpha"}],
    "float-linear": [{"name": "Background"}, {"name": "Float alpha"}],
    "link-relative-stored-pixels": [{"name": "Unaffected sibling"}, {"name": "Relative original resource"}],
}


def xcf_layers(stem):
    base = stem.removesuffix("-perceptual")
    # GIMP's default modern Multiply blends in linear light. The paired variant
    # explicitly sets every layer's PROP_BLEND_SPACE to perceptual RGB.
    multiply = "linear-multiply" if base == stem else "multiply"
    if base == "layers-8bit":
        return [{"name": "Background"}, {"name": "Multiply 60%", "blendMode": multiply},
                {"name": "Screen off-canvas", "blendMode": "screen"}, {"name": "Hidden", "visible": False}]
    if base == "groups":
        return [{"name": "Background"}, {"name": "Group", "kind": "group", "children": [
            {"name": "Child Multiply", "blendMode": multiply},
            {"name": "Nested pass-through", "kind": "group",
             "children": [{"name": "Nested child Screen", "blendMode": "screen"}]}]}, {"name": "Top"}]
    return XCF_LAYERS.get(base)


def xcf_linear_samples(stem):
    """Canvas samples from the pinned producer's literal inputs, in linear light.

    Coordinates refer to the visible composite. The precision layers begin at (-2, 1).
    GIMP's nonlinear transfer has a linear segment below zero and a power segment above one.
    """
    if stem == "float-linear":
        samples = []
        for x, y in ((0, 0), (39, 29), (12, 7), (20, 17), (27, 22)):
            rgb = [(3 * x + 5 * y + 7 * c) / 64 for c in range(3)]
            if 12 <= x < 28 and 7 <= y < 23:
                u, v = x - 12, y - 7
                alpha = (u + v) / 30
                top = [(2 * u + 3 * v + 5 * c) / 64 for c in range(3)]
                rgb = [a * alpha + b * (1 - alpha) for a, b in zip(top, rgb)]
            samples.append({"x": x, "y": y, "rgba": [*rgb, 1]})
        return samples
    ramps = {
        "precision-half-hdr": [0, 0, 2**-24, 2**-14, -.25, .5, 1, 1.5, 65504],
        "precision-float-hdr": [0, 0, 2**-149, 2**-126, -.125, .25, 1, 2, 4],
        "precision-double-gray-boundaries": [0, 0, 0, 2**-150, 2**-149, 1+2**-24, 1+3*2**-24, 1.5, -.25, 2],
    }
    ramp = ramps.get(stem)
    if ramp is None:
        return []
    def linear(v):
        return v / 12.92 if v <= .04045 else ((v + .055) / 1.055)**2.4
    samples = []
    for x, y in ((0, 1), (1, 1), (3, 1), (4, 1), (5, 1), (3, 2), (6, 2), (1, 3)):
        u, v = x + 2, y - 1
        channels = (0, 0, 0) if stem == "precision-double-gray-boundaries" else (0, 1, 2)
        rgb = [linear(ramp[(u + 3*v + c) % len(ramp)]) for c in channels]
        alpha = (0, .25, .5, 1)[(u + v) % 4]
        samples.append({"x": x, "y": y, "rgba": [*rgb, alpha]})
    return samples


def xcf_cases():
    root = PHOTO_DIR / "gimp"
    for path in sorted(root.glob("*.xcf*")):
        if not PHOTO_INCLUDE.fullmatch(path.relative_to(PHOTO_DIR).as_posix()):
            continue
        # The three compressed wrappers have the same source document and reference.
        bare = root / (path.name.split(".xcf", 1)[0] + ".xcf")
        reference = bare.with_suffix(".png")
        with bare.open("rb") as source:
            width, height = struct.unpack_from(">II", source.read(22), 14)
        stem = bare.stem
        expect = {"width": width, "height": height}
        case = {"id": "xcf/" + path.name, "source": rel(path), "reference": rel(reference),
                "expect": expect}
        layers = xcf_layers(stem)
        if layers is not None:
            expect["layers"] = layers
        if stem == "link-relative-stored-pixels":
            case["assets"] = [rel(root / "link-source.png")]
        if stem == "historical-2-10-32-legacy-text-path":
            expect.update(depth=8, paths=[{"name": "Original legacy Bezier"}])
        if stem.startswith("historical-2-99-18-text-path-"):
            expect.update(layers=[{"kind": "text"}], paths=[{"name": "Original historical Bezier"}])
        if stem == "paths-fractional-open-closed":
            expect["paths"] = [{"name": "Closed fractional cubic"}, {"name": "Open fractional cubic"}]
        if stem in {"gray16", "float-linear"}:
            expect["depth"] = 16 if stem == "gray16" else 32
        if stem.startswith("precision-"):
            expect["layers"] = [{"name": "Stored precision samples"}]
        if stem.startswith("profile-original-"):
            expect.update(depth=32, layers=[{"name": "Literal assigned-profile samples"}])
            case["referenceNote"] = "GIMP exported an sRGB preview of the custom-profile document. The appearance score measures displayed colors. Native profile and sample preservation require separate checks."
        elif (stem.startswith("precision-") or stem == "gray16" or
              any(token in stem for token in ("float", "u16-asymmetric"))):
            case["referenceNote"] = "GIMP's PNG measures display appearance. Native sample precision and values outside the display range require separate checks."
        if stem.startswith("historical-") and "float" in stem:
            expect["depth"] = 32
        if stem.startswith("historical-") and "u16-asymmetric" in stem:
            expect["depth"] = 16
        samples = xcf_linear_samples(stem)
        if samples:
            case["linearSamples"] = samples
            case["referenceNote"] = ("The PNG measures display appearance. Linear HDR sample expectations come "
                                     "from the pinned fixture generator's pixel values and layer offsets.")
        yield case


# Frame comparisons follow the whole-frame assertions in asefile's src/tests.rs.
# Tileset atlases, individual layers/tiles and utility output also ship as PNGs.
ASE_STILL = {"256_color_old_palette_chunk", "background", "basic-16x16", "big", "grayscale",
             "tilemap", "tilemap_grayscale", "tilemap_indexed", "tilemap_multi"}
ASE_NUMBERED = {"linked_cels": 3, "layers_and_tags": 4, "transparency": 2, "indexed": 1}


def ase_metadata(data):
    """Read source dimensions, frame timing and layer headers (never cel pixels)."""
    count, width, height = struct.unpack_from("<3H", data, 6)
    durations, layers = [], []
    parents = [layers]
    pos = 128
    for frame in range(count):
        size, magic, chunks, duration = struct.unpack_from("<I3H", data, pos)
        if magic != 0xF1FA or size < 16 or pos + size > len(data):
            raise ValueError("Invalid pinned Aseprite frame header")
        durations.append(duration or struct.unpack_from("<H", data, 18)[0])
        # These pinned sprites declare their layer tree in the first frame.
        if frame == 0:
            off = pos + 16
            for _ in range(chunks):
                length, kind = struct.unpack_from("<IH", data, off)
                if length < 6 or off + length > pos + size:
                    raise ValueError("Invalid pinned Aseprite chunk")
                if kind == 0x2004:
                    flags, layer_type, level = struct.unpack_from("<3H", data, off + 6)
                    name_len = struct.unpack_from("<H", data, off + 22)[0]
                    name = data[off + 24:off + 24 + name_len].decode("utf-8")
                    # The scripting property includes a movement-only editing lock.
                    layer = {"name": name, "visible": bool(flags & 1),
                             "locked": not bool(flags & 2) or bool(flags & 4)}
                    del parents[level + 1:]
                    parents[level].append(layer)
                    if layer_type == 1:
                        layer.update(kind="group", children=[])
                        parents.append(layer["children"])
                off += length
        pos += size
    return width, height, durations, layers


def ase_expectations(path):
    width, height, durations, layers = ase_metadata(path.read_bytes())
    # One copy of the document layer tree per frame, in bottom-first script order.
    tree = [{"kind": "group", "children": layers} for _ in durations] if len(durations) > 1 else layers
    return {"width": width, "height": height, "depth": 8, "layers": tree}, durations


def ase_cases():
    for path in sorted((ASE_DIR / "tests/data").glob("*.aseprite")):
        stem = path.stem
        # This duplicate of tilemap.aseprite tests an exported tileset atlas upstream.
        if stem == "tileset" or not ASE_INCLUDE.fullmatch(path.relative_to(ASE_DIR).as_posix()):
            continue
        expect, durations = ase_expectations(path)
        case = {"id": "aseprite/" + stem, "source": rel(path)}
        refs = [None] * len(durations)
        if stem in ASE_STILL or stem.startswith("blend_"):
            refs[0] = rel(path.with_suffix(".png"))
        for i in range(ASE_NUMBERED.get(stem, 0)):
            refs[i] = rel(path.with_name(f"{stem}_{i + 1:02}.png"))
        if len(durations) > 1:
            case["durationsMs"] = durations
        if stem == "layers_and_tags":
            case["referenceNote"] = "Four frame renders, timings and the layer tree are measured. Animation tag names and playback directions are outside these checks."
        if stem in ("blend_hue", "blend_saturation", "blend_saturation_bug"):
            case["referenceNote"] = ("This March 2021 asefile PNG records historical Hue or Saturation results for "
                                     "colors with equal channels. Aseprite corrected this behavior in 1.3.13. "
                                     "The original PNG remains the reference for this case.")
        case.update(frames=refs, expect=expect)
        yield case

    # The swatches exercise solid-color ties in Hue alongside the asefile sweeps.
    swatches = config.REPO_ROOT / "test-fixtures/aseprite/aseprite-blend-modes.aseprite"
    expect, _ = ase_expectations(swatches)
    yield {"id": "aseprite/aseprite-1.3.17-blend-swatches", "source": rel(swatches), "expect": expect,
           "reference": rel(swatches.with_name("aseprite-blend-modes-reference.png")),
           "referenceNote": "Aseprite 1.3.17 exported the six opaque blend-mode swatches."}


def aseprite_upstream_cases():
    notes = {
        "1empty3": "The second frame is empty. The two named tags and cel pixels have no independent checks here.",
        "tags3": "Forward, reverse and ping-pong tag directions, ranges and linked cel relationships are unmeasured.",
        "tags3x123reps": "Tag repeat counts, ranges and playback directions are unmeasured.",
        "file-tests-props": "Layers include a tilemap. Tag data, cel pixels and custom element properties are unmeasured.",
        "slices": "The two slice rectangles are unmeasured.",
        "slices-moving": "The four animated slice bounds and linked cel relationship are unmeasured.",
        "z-order": "Per-cel stacking order is unmeasured.",
    }
    for name in sorted(ASEPRITE_FILES):
        if not name.endswith(".aseprite"):
            continue
        path = ASEPRITE_DIR / Path(name).name
        expect, durations = ase_expectations(path)
        case = {"id": "aseprite-upstream/" + path.stem, "source": rel(path), "expect": expect}
        if len(durations) > 1:
            case["durationsMs"] = durations
        if path.stem in notes:
            case["referenceNote"] = notes[path.stem]
        yield case


def kra_cases():
    # Canvas and layer metadata from these producers' maindoc.xml, converted to
    # the bottom-first scripting layer order. The animation's PNG is its saved frame.
    for stem, width, height, depth, layers, note in (
        ("flood_fill_16bit", 494, 774, 16, ["main"], "Krita stored an 8-bit preview of the 16-bit document."),
        ("load_test_animation", 1920, 1080, 8, ["Layer 1", "Layer 2"],
         "Krita's saved preview represents timeline position 19. The fixture also contains keyframes at 0 and 30."),
        ("single_layer_no_channel_flags_nontransp_def_pixel", 100, 100, 8, ["Layer 1"],
         "Krita's embedded PNG is 128 by 128 for a 100 by 100 source canvas."),
    ):
        yield {"id": "kra/" + stem, "source": rel(KRA_DIR / (stem + ".kra")),
               "reference": "@mergedimage", "referenceNote": note,
               "expect": {"width": width, "height": height, "depth": depth,
                          "layers": [{"name": name} for name in layers]}}


def corpus_credit(files):
    root = (PHOTO_DIR / "photoshop").resolve()
    if not files or not all(Path(f).resolve().is_relative_to(root) for f in files):
        return None
    return {"name": "PhotoCraft Photoshop corpus", "project": "PhotoCraft Corpus",
            "url": f"https://github.com/{PHOTO_REPO}/tree/{PHOTO_PIN}/photoshop",
            "projectUrl": f"https://github.com/{PHOTO_REPO}", "license": "MIT OR Apache-2.0",
            "commit": PHOTO_PIN}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", "--list-only", action="store_true",
                        help="verify previously fetched fixtures and regenerate lists without network access")
    args = parser.parse_args()
    fetch_pack(PHOTO_REPO, PHOTO_PIN, PHOTO_DIR, PHOTO_INCLUDE, PHOTO_HASH, args.offline)
    fetch_pack(ASE_REPO, ASE_PIN, ASE_DIR, ASE_INCLUDE, ASE_HASH, args.offline)
    fetch_files(KRA_REPO, KRA_PIN, KRA_DIR, KRA_FILES, args.offline)
    fetch_files(ASEPRITE_REPO, ASEPRITE_PIN, ASEPRITE_DIR, ASEPRITE_FILES, args.offline)
    files = sorted(p for p in (PHOTO_DIR / "photoshop").rglob("*.psd")
                   if PHOTO_INCLUDE.fullmatch(p.relative_to(PHOTO_DIR).as_posix()))
    lines = [f"# PhotoCraft Photoshop fixtures at {PHOTO_PIN} (MIT OR Apache-2.0).",
             f"# https://github.com/{PHOTO_REPO} . Paths are relative to the repository root."]
    lines.extend(p.relative_to(config.REPO_ROOT).as_posix() for p in files)
    cases = [*xcf_cases(), *ase_cases(), *kra_cases()]
    manifest = {
        "name": "PhotoCraft GIMP, asefile and Krita import fixtures",
        "sources": [
            {"url": f"https://github.com/{PHOTO_REPO}/tree/{PHOTO_PIN}/gimp",
             "license": "MIT OR Apache-2.0", "credit": "ArtCraft Team and PhotoCraft contributors"},
            {"url": f"https://github.com/{ASE_REPO}/tree/{ASE_PIN}/tests/data",
             "license": "MIT", "credit": "asefile contributors"},
            {"url": f"https://github.com/{KRA_REPO}/tree/{KRA_PIN}",
             "credit": "Krita test data", "license": "Per-image redistribution rights unverified"},
        ],
        "cases": cases,
    }
    for case in cases:
        refs = [case["source"], case.get("reference"), *case.get("frames", []), *case.get("assets", [])]
        for ref in refs:
            if ref and ref != "@mergedimage" and not (CORPORA / ref).resolve().is_file():
                raise ValueError(f"Missing fixture for {case['id']}: {ref}")
    psd_list = CORPORA / "photocraft-psd.txt"
    native_list = CORPORA / "native-imports.json"
    upstream_list = CORPORA / "native-aseprite-upstream.json"
    upstream = {"name": "Aseprite upstream test sprites",
                "sources": [{"url": f"https://github.com/{ASEPRITE_REPO}/tree/{ASEPRITE_PIN}/tests/sprites",
                             "license": "MIT (tests/LICENSE.txt)", "credit": "Igara Studio S.A. and David Capello"}],
                "cases": list(aseprite_upstream_cases())}
    write_atomic(psd_list, ("\n".join(lines) + "\n").encode("utf-8"))
    write_atomic(native_list, (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    write_atomic(upstream_list, (json.dumps(upstream, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))
    log(f"{len(files)} PSDs: {psd_list}\n{len(cases)} native cases: {native_list}"
        f"\n{len(upstream['cases'])} official Aseprite cases: {upstream_list}")


if __name__ == "__main__":
    try:
        main()
    except (BadZipFile, OSError, ValueError) as error:
        sys.exit(f"Corpus setup failed: {error}")
