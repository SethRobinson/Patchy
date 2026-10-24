"""OpenRaster corpus compatibility check (docs/ora.md, "Corpus").

Usage (repository root, through run-throttled):
    scripts\\run-throttled.bat python -I scripts\\dev\\ora\\corpus_check.py CORPUS_DIR [--patchy PATH]
        [--report DIR] [--no-apps] [--only SUBSTRING]

For every .ora under CORPUS_DIR (one file per folder is the usual layout; nothing in the
corpus is ever executed, only read as data):
  1. Patchy opens it headless (patchy.exe --headless --run-script), records its notes
     (doc.importNotes), layer tree, depth and timings, exports its composite as PNG, saves it
     as .ora, reopens that and exports the composite again.
  2. Patchy's composite is compared with the file's own mergedimage.png and, when installed,
     with Krita's and GIMP's command-line PNG exports of the original.
  3. The re-saved file is checked: the reopened tree and composite against the first open
     (exactly), and Krita and GIMP open it, export a PNG (compared with Patchy's composite)
     and re-save it as .ora, whose layer tree is compared with Patchy's.
The table goes to DIR/corpus.csv with a short DIR/summary.md (default build/ora-corpus-report).
Needs numpy and Pillow (an independent PNG decoder). Pixel differences are 8-bit units on
premultiplied RGBA; "over" is the share of pixels that
differ by more than 8 in any channel. GIMP and Krita are found in their default install places
or through PATCHY_GIMP_CONSOLE / PATCHY_KRITA.
"""

import argparse
import csv
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ElementTree
import zipfile
import zlib

import numpy
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
GIMP = os.environ.get("PATCHY_GIMP_CONSOLE",
                      os.path.expandvars(r"%LOCALAPPDATA%\Programs\GIMP 3\bin\gimp-console-3.exe"))
KRITA = os.environ.get("PATCHY_KRITA", r"C:\Program Files\Krita (x64)\bin\krita.com")
APP_TIMEOUT = 180
PATCHY_TIMEOUT = 300
OVER_THRESHOLD = 8

PATCHY_SCRIPT = r"""
var file = patchy.args.file, out = patchy.args.out;
function tree(layers) {
  var result = [];
  for (var i = 0; i < layers.length; ++i) {
    var l = layers[i], b = l.bounds;
    result.push({name: l.name, group: l.isGroup, visible: l.visible, opacity: Math.round(l.opacity * 1000) / 1000,
                 blend: l.blendMode, locked: l.locked, x: b ? b.x : 0, y: b ? b.y : 0,
                 w: b ? b.width : 0, h: b ? b.height : 0, children: l.isGroup ? tree(l.children) : []});
  }
  return result;
}
var report = {};
// The first open in a process also pays for one-time start-up work, so the file is timed on
// its second open.
var t0 = Date.now();
var doc = app.open(file);
report.coldOpenMs = Date.now() - t0;
doc.close();
t0 = Date.now();
doc = app.open(file);
report.openMs = Date.now() - t0;
report.notes = doc.importNotes;
report.width = doc.width; report.height = doc.height; report.depth = doc.bitDepth;
report.tree = tree(doc.layers);
doc.exportAs(out + "/patchy.png");
var t1 = Date.now();
doc.exportAs(out + "/resaved.ora");
report.saveMs = Date.now() - t1;
doc.close();
var again = app.open(out + "/resaved.ora");
report.reopenNotes = again.importNotes;
report.reopenTree = tree(again.layers);
again.exportAs(out + "/reopened.png");
again.close();
console.log("REPORT " + JSON.stringify(report));
"""


def decode_png(data):
    """Premultiplied RGBA float array (height, width, 4) on 0..255, through Pillow."""
    with Image.open(io.BytesIO(data)) as image:
        image.load()
        rgba = numpy.asarray(image.convert("RGBA"), dtype=numpy.float64)
    rgba[..., :3] *= rgba[..., 3:4] / 255.0
    return rgba


def compare(a, b):
    """(mean, worst, share over threshold) or None when the sizes differ."""
    if a is None or b is None or a.shape != b.shape:
        return None
    diff = numpy.abs(a - b).max(axis=2)
    if diff.size == 0:
        return (0.0, 0.0, 0.0)
    return (float(diff.mean()), float(diff.max()), float((diff > OVER_THRESHOLD).mean()))


def describe(path):
    """What the file says about itself: writer hints, mergedimage, layer PNG depths."""
    info = {"version": "", "writer": "", "merged": False, "ops": set(), "layers": 0, "deep": False}
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        info["merged"] = "mergedimage.png" in names
        info["extra"] = sorted(n for n in names if not (n in ("mimetype", "stack.xml", "mergedimage.png") or
                                                     n.startswith("data/") or n.startswith("Thumbnails/")))[:5]
        root = ElementTree.fromstring(archive.read("stack.xml"))
        info["version"] = root.get("version", "")
        for element in root.iter():
            if element.tag == "layer":
                info["layers"] += 1
                info["ops"].add(element.get("composite-op", "svg:src-over"))
                src = element.get("src", "")
                if src in names:
                    head = archive.read(src)[:26]
                    if head[:8] == b"\x89PNG\r\n\x1a\n" and head[24] == 16:
                        info["deep"] = True
        stack = root.find("stack")
        keys = set(stack.attrib) if stack is not None else set()
        if root.get("version") == "0.0.1" and "isolation" in keys:
            info["writer"] = "Krita"
        elif not root.get("version") and any(e.get("selected") for e in root.iter("layer")):
            info["writer"] = "GIMP"
        elif any(k.startswith("mypaint") or "mypaint" in k for e in root.iter() for k in e.attrib):
            info["writer"] = "MyPaint"
        merged = archive.read("mergedimage.png") if info["merged"] else None
    info["ops"] = sorted(info["ops"])
    return info, merged


def ora_tree(path):
    """(name, is group, visible, opacity, op) rows in file order from a .ora's stack.xml."""
    rows = []

    def walk(element, depth):
        for child in element:
            if child.tag in ("layer", "stack"):
                rows.append((depth, child.tag == "stack", child.get("name", ""), child.get("visibility", "visible"),
                             round(float(child.get("opacity", "1") or 1), 2),
                             child.get("composite-op", "svg:src-over")))
                if child.tag == "stack":
                    walk(child, depth + 1)

    with zipfile.ZipFile(path) as archive:
        root = ElementTree.fromstring(archive.read("stack.xml"))
    stack = root.find("stack")
    if stack is not None:
        walk(stack, 0)
    return rows


def flatten_tree(nodes, depth=0, out=None):
    out = [] if out is None else out
    for node in nodes:
        out.append((depth, node["group"], node["name"], "visible" if node["visible"] else "hidden"))
        flatten_tree(node["children"], depth + 1, out)
    return out


def run(command, timeout):
    """(exit code or 'timeout', seconds); stops only the process it started."""
    start = time.time()
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        code = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        code = "timeout"
    return code, time.time() - start


def gimp_batch(jobs, work):
    """GIMP opens each source and saves it to each target (.png flattens, .ora re-saves)."""
    script = os.path.join(work, "gimp-jobs.py")
    with open(script, "w", encoding="utf-8") as handle:
        handle.write("import gi\ngi.require_version('Gimp', '3.0')\nfrom gi.repository import Gimp, Gio\n")
        handle.write("JOBS = %r\n" % jobs)
        handle.write("for source, targets in JOBS:\n"
                     "    try:\n"
                     "        image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(source))\n"
                     "        for target in targets:\n"
                     "            Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(target), None)\n"
                     "        image.delete()\n"
                     "    except Exception as error:\n"
                     "        open(targets[0] + '.error.txt', 'w').write(str(error))\n")
    return run([GIMP, "-i", "--batch-interpreter=python-fu-eval",
                "-b", "exec(open(r'%s', encoding='utf-8').read())" % script,
                "-b", "Gimp.get_pdb().lookup_procedure('gimp-quit').run(None)"], 3600)


def load_png(path):
    try:
        with open(path, "rb") as handle:
            return decode_png(handle.read())
    except Exception:  # noqa: BLE001 - an app may write a file Pillow refuses
        return None


def fmt(result):
    return "" if result is None else "%.2f/%.0f/%.1f%%" % (result[0], result[1], result[2] * 100)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("corpus")
    parser.add_argument("--patchy", default=os.path.join(ROOT, "build", "opt-dev", "patchy.exe"))
    parser.add_argument("--report", default=os.path.join(ROOT, "build", "ora-corpus-report"))
    parser.add_argument("--no-apps", action="store_true")
    parser.add_argument("--only", default="")
    args = parser.parse_args()
    apps = {"krita": not args.no_apps and os.path.exists(KRITA), "gimp": not args.no_apps and os.path.exists(GIMP)}
    files = []
    for folder, _, names in os.walk(args.corpus):
        files += [os.path.join(folder, n) for n in sorted(names) if n.lower().endswith(".ora") and args.only in n]
    files.sort()
    os.makedirs(args.report, exist_ok=True)
    work = tempfile.mkdtemp(prefix="patchy-ora-corpus-")
    script = os.path.join(work, "probe.js")
    with open(script, "w", encoding="utf-8") as handle:
        handle.write(PATCHY_SCRIPT)
    settings = os.path.join(work, "settings")
    os.makedirs(settings)
    rows = []
    gimp_jobs = []
    try:
        for index, path in enumerate(files):
            label = os.path.relpath(path, args.corpus)
            out = os.path.join(work, "f%03d" % index)
            os.makedirs(out)
            row = {"file": label, "bytes": os.path.getsize(path)}
            try:
                info, merged_bytes = describe(path)
                row.update({"writer": info["writer"], "version": info["version"], "layers": info["layers"],
                            "deep_png": info["deep"], "merged": info["merged"], "ops": " ".join(info["ops"]),
                            "extra": " ".join(info["extra"])})
            except Exception as error:  # noqa: BLE001 - corpus files are untrusted
                merged_bytes = None
                row["describe_error"] = str(error)[:120]
            output = os.path.join(out, "patchy.txt")
            env = dict(os.environ, PATCHY_SETTINGS_DIR=settings)
            start = time.time()
            process = subprocess.Popen([args.patchy, "--headless", "--run-script", script, "--script-output", output,
                                        "--script-arg", "file=" + path, "--script-arg", "out=" + out.replace("\\", "/")],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
            try:
                code = process.wait(timeout=PATCHY_TIMEOUT)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                code = "timeout"
            row["patchy_s"] = round(time.time() - start, 2)
            row["patchy_exit"] = code
            report = None
            text = open(output, encoding="utf-8", errors="replace").read() if os.path.exists(output) else ""
            for line in text.splitlines():
                if line.startswith("REPORT "):
                    report = json.loads(line[7:])
                elif line.startswith("[error]") and "patchy_error" not in row:
                    row["patchy_error"] = line[8:200]
            ours = load_png(os.path.join(out, "patchy.png"))
            if report:
                row.update({"cold_open_ms": report["coldOpenMs"], "open_ms": report["openMs"], "save_ms": report["saveMs"], "depth": report["depth"],
                            "size": "%dx%d" % (report["width"], report["height"]),
                            "notes": " | ".join(report.get("notes") or [])[:300],
                            "reopen_notes": " | ".join(report.get("reopenNotes") or [])[:200],
                            "roundtrip_tree": "same" if report["tree"] == report["reopenTree"] else "DIFFERS"})
                reopened = load_png(os.path.join(out, "reopened.png"))
                same = compare(ours, reopened)
                row["roundtrip_pixels"] = "same" if same and same[1] == 0 else ("DIFFERS " + fmt(same))
                row["patchy_tree"] = report["tree"]
            if merged_bytes:
                try:
                    row["vs_merged"] = fmt(compare(ours, decode_png(merged_bytes)))
                except Exception as error:  # noqa: BLE001
                    row["vs_merged"] = "merged unreadable: %s" % error
            resaved = os.path.join(out, "resaved.ora")
            if apps["krita"]:
                code, seconds = run([KRITA, path, "--export", "--export-filename", os.path.join(out, "krita.png")],
                                    APP_TIMEOUT)
                row["krita_orig"] = fmt(compare(ours, load_png(os.path.join(out, "krita.png")))) if code == 0 else code
                if os.path.exists(resaved):
                    code, _ = run([KRITA, resaved, "--export", "--export-filename",
                                   os.path.join(out, "krita-resaved.png")], APP_TIMEOUT)
                    row["krita_resaved"] = (fmt(compare(ours, load_png(os.path.join(out, "krita-resaved.png"))))
                                            if code == 0 else code)
                    code, _ = run([KRITA, resaved, "--export", "--export-filename",
                                   os.path.join(out, "krita-resaved.ora")], APP_TIMEOUT)
                    if code == 0 and os.path.exists(os.path.join(out, "krita-resaved.ora")):
                        theirs = [(d, g, n, v) for d, g, n, v, _, _ in ora_tree(os.path.join(out, "krita-resaved.ora"))]
                        mine = [(d, g, n, v) for d, g, n, v, _, _ in ora_tree(resaved)]
                        row["krita_tree"] = "same" if theirs == mine else "DIFFERS"
            if apps["gimp"]:
                gimp_jobs.append((path, [os.path.join(out, "gimp.png")]))
                if os.path.exists(resaved):
                    gimp_jobs.append((resaved, [os.path.join(out, "gimp-resaved.png"),
                                                os.path.join(out, "gimp-resaved.ora")]))
            rows.append((row, out, ours, resaved))
            print("%3d/%d %s %s" % (index + 1, len(files), label, row.get("patchy_error", "")), flush=True)
        if gimp_jobs:
            gimp_batch(gimp_jobs, work)
            for row, out, ours, resaved in rows:
                if os.path.exists(os.path.join(out, "gimp.png")):
                    row["gimp_orig"] = fmt(compare(ours, load_png(os.path.join(out, "gimp.png"))))
                if os.path.exists(os.path.join(out, "gimp-resaved.png")):
                    row["gimp_resaved"] = fmt(compare(ours, load_png(os.path.join(out, "gimp-resaved.png"))))
                if os.path.exists(os.path.join(out, "gimp-resaved.ora")):
                    # GIMP makes layer names unique by adding " #1", " #2" to repeats.
                    theirs = [(d, g, re.sub(r" #\d+$", "", n))
                              for d, g, n, _, _, _ in ora_tree(os.path.join(out, "gimp-resaved.ora"))]
                    mine = [(d, g, re.sub(r" #\d+$", "", n)) for d, g, n, _, _, _ in ora_tree(resaved)]
                    row["gimp_tree"] = "same" if theirs == mine else "DIFFERS"
        # Keep Patchy's outputs of anything that differs, for triage.
        keep = os.path.join(args.report, "outputs")
        for row, out, _, _ in rows:
            flagged = any(str(v).startswith(("DIFFERS", "timeout")) for v in row.values()) or row.get("patchy_error")
            if flagged:
                target = os.path.join(keep, os.path.basename(out))
                if os.path.isdir(target) and os.path.basename(target).startswith("f"):
                    shutil.rmtree(target)
                shutil.copytree(out, target)
                row["outputs"] = os.path.basename(out)
    finally:
        if os.path.basename(work).startswith("patchy-ora-corpus-"):
            shutil.rmtree(work, ignore_errors=True)
    columns = ["file", "bytes", "writer", "version", "size", "depth", "layers", "deep_png", "merged", "ops", "extra",
               "patchy_exit", "patchy_error", "patchy_s", "cold_open_ms", "open_ms", "save_ms", "notes", "vs_merged", "krita_orig",
               "gimp_orig", "roundtrip_tree", "roundtrip_pixels", "reopen_notes", "krita_resaved", "krita_tree",
               "gimp_resaved", "gimp_tree", "describe_error", "outputs"]
    with open(os.path.join(args.report, "corpus.csv"), "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        for row, _, _, _ in rows:
            writer.writerow(row)
    with open(os.path.join(args.report, "trees.json"), "w", encoding="utf-8") as handle:
        json.dump({row["file"]: row.get("patchy_tree") for row, _, _, _ in rows}, handle, indent=1)
    opened = sum(1 for row, _, _, _ in rows if row.get("open_ms") is not None)
    roundtrip = sum(1 for row, _, _, _ in rows if row.get("roundtrip_tree") == "same" and row.get("roundtrip_pixels") == "same")
    slow = [row["file"] for row, _, _, _ in rows if (row.get("open_ms") or 0) > 2000 or (row.get("save_ms") or 0) > 2000]
    lines = ["# OpenRaster corpus check", "",
             "%d files; Patchy opened %d; save and reopen identical for %d." % (len(rows), opened, roundtrip),
             "Columns of pixel comparisons: mean/worst/share of pixels over %d." % OVER_THRESHOLD, "",
             "Slow (over 2 s to open or save): %s" % (", ".join(slow) or "none"), ""]
    for row, _, _, _ in rows:
        problems = [k for k in ("patchy_error", "describe_error") if row.get(k)]
        problems += [k for k, v in row.items() if str(v).startswith(("DIFFERS", "timeout"))]
        if row.get("notes"):
            problems.append("notes")
        if problems:
            lines.append("- %s: %s" % (row["file"], ", ".join(problems)))
    with open(os.path.join(args.report, "summary.md"), "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
