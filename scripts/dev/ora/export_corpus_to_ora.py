"""Grows the OpenRaster corpus from the XCF and KRA corpora (docs/ora.md, "Corpus").

Usage: python scripts/dev/ora/export_corpus_to_ora.py [FIXTURES_DIR] [--only gimp|krita]
                                                       [--max-mb 64] [--limit N]

GIMP saves every file of FIXTURES_DIR/xcf-corpus and xcf as OpenRaster into
FIXTURES_DIR/ora-corpus/gimp, and Krita every file of kra-corpus and kra into
ora-corpus/krita. Each app then opens its own .ora again and renders it to <stem>.png
beside the file: the picture the layer stack means to the app that wrote it. That PNG is
the file's reference, because the composite an app stores in the archive is drawn from
the source document and can show what OpenRaster cannot hold (text, effects, filter
layers). index.json records, per file, the source, the writer and whether the stored
composite agrees with that render. Files already written are skipped.

Both apps run only through Testy's command-line drivers; nothing here reads their source.
FIXTURES_DIR defaults to the repository's local-test-fixtures. The files keep the
licences of their sources (the README.txt of xcf-corpus and kra-corpus).
"""

import argparse
import json
import sys
from pathlib import Path
from zipfile import BadZipFile, ZipFile

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "testy"))

import analyze  # noqa: E402
import config  # noqa: E402
from drivers import gimp, krita  # noqa: E402

XCF = (".xcf", ".xcf.gz", ".xcf.bz2", ".xcf.xz", ".xcfgz", ".xcfbz2", ".xcfxz")
WRITERS = {"gimp": (gimp, ("xcf-corpus", "xcf"), XCF), "krita": (krita, ("kra-corpus", "kra"), (".kra", ".krz"))}


def sources(fixtures: Path, collections, suffixes, max_bytes):
    for collection in collections:
        directory = fixtures / collection
        if not directory.is_dir():
            print(collection, "is not installed")
            continue
        for path in sorted(directory.rglob("*")):
            if path.is_file() and path.name.lower().endswith(suffixes):
                if path.stat().st_size > max_bytes:
                    print("skipped (over the size limit):", path)
                    continue
                yield collection, path, collection + "__" + path.relative_to(directory).as_posix().replace("/", "__")


def composite_agrees(ora: Path, render: Path, scratch: Path):
    """Does the archive's stored composite show what its writer renders from the stack?
    None when the archive has no readable composite."""
    try:
        with ZipFile(ora) as archive:
            scratch.write_bytes(archive.read("mergedimage.png"))
        return bool(analyze.renders_agree(scratch, render))
    except (BadZipFile, KeyError, OSError, ValueError):
        return None
    finally:
        scratch.unlink(missing_ok=True)


def export(key, editor, fixtures: Path, out: Path, max_bytes: int, limit: int, index: dict):
    driver, collections, suffixes = WRITERS[key]
    folder = out / key
    folder.mkdir(parents=True, exist_ok=True)
    written = 0
    for collection, source, stem in sources(fixtures, collections, suffixes, max_bytes):
        if limit and written >= limit:
            break
        name = f"{key}/{stem}.ora"
        ora, render = folder / (stem + ".ora"), folder / (stem + ".png")
        if name in index and ora.is_file():
            continue
        entry = {"source": f"{collection}/{source.relative_to(fixtures / collection).as_posix()}",
                 "writer": f"{editor.display_name} {editor.version}"}
        result = driver.export(editor.exe, source, ora)
        if not result.get("ok"):
            ora.unlink(missing_ok=True)
            entry["error"] = (str(result.get("stderr") or "").strip().splitlines() or ["the app wrote no file"])[-1][:300]
            print("no OpenRaster from", source.name, "-", entry["error"])
        else:
            written += 1
            if driver.export(editor.exe, ora, render).get("ok"):
                entry["compositeAgrees"] = composite_agrees(ora, render, folder / (stem + ".composite.png"))
            else:
                render.unlink(missing_ok=True)
                entry["renderError"] = "the app could not render its own file"
            print("wrote", name, "composite agrees:", entry.get("compositeAgrees"))
        index[name] = entry
        (out / "index.json").write_text(json.dumps(index, ensure_ascii=False, indent=1) + "\n", encoding="utf-8",
                                        newline="")
    return written


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("fixtures", nargs="?", type=Path, default=ROOT / "local-test-fixtures")
    parser.add_argument("--only", choices=sorted(WRITERS))
    parser.add_argument("--max-mb", type=int, default=64, help="skip sources larger than this")
    parser.add_argument("--limit", type=int, default=0, help="stop each writer after this many new files")
    args = parser.parse_args()
    fixtures = args.fixtures.resolve()
    if not fixtures.is_dir() or fixtures.name != "local-test-fixtures":
        parser.exit(1, f"{fixtures} is not a local-test-fixtures folder\n")
    out = fixtures / "ora-corpus"
    out.mkdir(exist_ok=True)
    index_path = out / "index.json"
    index = json.loads(index_path.read_text(encoding="utf-8")) if index_path.is_file() else {}
    editors = config.discover_editors("")
    for key in ([args.only] if args.only else sorted(WRITERS)):
        editor = editors.get(key)
        if editor is None or not editor.available:
            print(key, "is not installed")
            continue
        print(key, "wrote", export(key, editor, fixtures, out, args.max_mb << 20, args.limit, index), "new files")
    readme = out / "README.txt"
    if not readme.is_file():
        readme.write_text(
            "OpenRaster corpus, not committed.\n\n"
            "gimp/ and krita/: written by scripts/dev/ora/export_corpus_to_ora.py from the files of\n"
            "../xcf-corpus, ../xcf, ../kra-corpus and ../kra. Each file keeps the licence of its source:\n"
            "see the README.txt of xcf-corpus and kra-corpus; xcf and kra are Patchy's own fixtures.\n"
            "index.json names every file's source and writer. <stem>.png is the writer's own render of\n"
            "its .ora and is the reference.\n", encoding="utf-8", newline="")
    print("corpus:", out)


if __name__ == "__main__":
    main()
