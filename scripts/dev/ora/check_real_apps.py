"""OpenRaster interchange check against GIMP 3 and Krita 5 (docs/ora.md, "Interchange").

Each app opens .ora files from the command line and exports a PNG, which is compared with
Patchy's render of the same file:

  A. Patchy's files (test-artifacts/ora/patchy-*.ora, written by
     `patchy_core_tests ora_writes_inspection_artifacts`) against Patchy's own merged image.
  B. The apps' files (local-test-fixtures/ora/*.ora) against Patchy's composite of them
     (test-artifacts/ora/from-apps/*.png, written by the same test).

Files whose layers do not overlap (the *modes* files: one tile per blend mode) are compared
per layer, so each composite-op gets its own numbers; the others as a whole. Differences are
in 8-bit units on premultiplied RGBA. An app that is not installed is skipped; the script then
exits 0 with a note. GIMP is driven through its batch Python console, Krita through
`krita.com --export`; only processes started here are stopped, by PID.

Usage (repository root, through run-throttled):
    scripts\\run-throttled.bat python scripts\\dev\\ora\\check_real_apps.py [build\\opt-dev]
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib
import xml.etree.ElementTree as ElementTree

GIMP = os.environ.get("PATCHY_GIMP_CONSOLE",
                      os.path.expandvars(r"%LOCALAPPDATA%\Programs\GIMP 3\bin\gimp-console-3.exe"))
KRITA = os.environ.get("PATCHY_KRITA", r"C:\Program Files\Krita (x64)\bin\krita.com")
KRITA_TIMEOUT = 90

# Tolerances (mean, worst) per app and composite-op, in 8-bit units on premultiplied RGBA;
# (app, None) is the app's default and also covers whole-file comparisons. Measured on the
# first run (October 24, 2026, GIMP 3.2.6 and Krita 5.3.2); docs/ora.md says why.
TOLERANCE = {
    # Krita blends in the same encoded sRGB space with the same formulas: off by rounding.
    ("krita", None): (0.5, 3),
    # A pixel whose two colors have nearly equal luminosity can pick the other one.
    ("krita", "krita:darker color"): (0.5, 128),
    ("krita", "krita:lighter color"): (0.5, 128),
    # Hard Mix thresholds at a + b = 255: pixels on the threshold flip all the way.
    ("krita", "krita:hard_mix_photoshop"): (1.0, 255),
    # GIMP 3 composites layers in linear light, so every partly covered pixel differs, and its
    # HSV/LCH-based hue, saturation, color and luminosity differ from the W3C formulas.
    ("gimp", None): (6.0, 130),
    ("gimp", "svg:plus"): (32.0, 90),
    ("gimp", "svg:hue"): (25.0, 110),
    ("gimp", "svg:color"): (12.0, 64),
    ("gimp", "svg:saturation"): (34.0, 120),
    ("gimp", "svg:luminosity"): (38.0, 150),
    ("gimp", "svg:color-dodge"): (7.0, 130),
}
# Known differences reported without failing the check: (app, op prefix) -> reason.
EXPECTED = {
    ("krita", "krita:dissolve"): "dissolve's random pattern is the app's own",
    ("gimp", "krita:"): "GIMP opens Krita's mode names as Normal",
}
# Whole files known to differ: (app, file) -> reason. Patchy's 16-bit files are checked in
# Krita too: since every PNG carries an sRGB ICC profile, Krita opens them without asking.
EXPECTED_FILES = {}


def tolerance(app, op):
    return TOLERANCE.get((app, op), TOLERANCE[(app, None)])


def expected(app, op):
    for (who, prefix), reason in EXPECTED.items():
        if who == app and op.startswith(prefix):
            return reason
    return None


def decode_png(data):
    """RGBA rows (lists of 0..255 ints, premultiplied) from 8 or 16-bit, non-interlaced PNG."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    at = 8
    idat = b""
    palette = None
    transparency = None
    width = height = depth = color = 0
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if interlace:
                raise ValueError("interlaced PNG")
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            transparency = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    bits = depth * channels
    stride = (width * bits + 7) // 8
    step = max(1, bits // 8)
    raw = zlib.decompress(idat)
    rows = []
    previous = bytearray(stride)
    at = 0
    for _ in range(height):
        kind = raw[at]
        line = bytearray(raw[at + 1:at + 1 + stride])
        at += 1 + stride
        for i in range(stride):
            left = line[i - step] if i >= step else 0
            up = previous[i]
            up_left = previous[i - step] if i >= step else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 255
            elif kind == 4:
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else up_left)) & 255
        previous = line
        samples = []
        if depth == 16:
            samples = [((line[i] << 8) | line[i + 1]) / 257.0 for i in range(0, stride, 2)]
        elif depth == 8:
            samples = list(line)
        else:
            per_byte = 8 // depth
            mask = (1 << depth) - 1
            for i in range(width):
                byte = line[i // per_byte]
                samples.append((byte >> (8 - depth * (i % per_byte + 1))) & mask)
        row = []
        for x in range(width):
            if color == 3:
                index = int(samples[x])
                r, g, b = palette[index * 3:index * 3 + 3]
                a = transparency[index] if transparency and index < len(transparency) else 255
            else:
                pixel = samples[x * channels:(x + 1) * channels]
                if channels <= 2:
                    scale = 255.0 / ((1 << depth) - 1) if depth < 8 else 1.0
                    r = g = b = pixel[0] * scale
                    a = pixel[1] if channels == 2 else 255
                else:
                    r, g, b = pixel[:3]
                    a = pixel[3] if channels == 4 else 255
            row.append((r * a / 255.0, g * a / 255.0, b * a / 255.0, float(a)))
        rows.append(row)
    return width, height, rows


def compare(ours, theirs, rect=None):
    width, height, a = ours
    width2, height2, b = theirs
    if (width, height) != (width2, height2):
        return None
    x0, y0, w, h = rect if rect else (0, 0, width, height)
    total = 0.0
    worst = 0.0
    count = 0
    for y in range(max(0, y0), min(height, y0 + h)):
        for x in range(max(0, x0), min(width, x0 + w)):
            for c in range(4):
                diff = abs(a[y][x][c] - b[y][x][c])
                total += diff
                worst = max(worst, diff)
                count += 1
    return (total / count if count else 0.0, worst)


def layer_rects(path):
    """(name, op, rect) for every top-level layer of a .ora, from its stack.xml."""
    with zipfile.ZipFile(path) as archive:
        root = ElementTree.fromstring(archive.read("stack.xml"))
        result = []
        for layer in root.find("stack"):
            if layer.tag != "layer":
                continue
            data = archive.read(layer.get("src"))
            width, height = struct.unpack(">II", data[16:24])
            x = int(float(layer.get("x", "0")))
            y = int(float(layer.get("y", "0")))
            result.append((layer.get("name", ""), layer.get("composite-op", "svg:src-over"), (x, y, width, height)))
        return result


def is_sixteen_bit(path):
    with zipfile.ZipFile(path) as archive:
        for name in archive.namelist():
            if name.startswith("data/") and name.endswith(".png"):
                if archive.read(name)[24] == 16:
                    return True
    return False


def run_krita(source, target):
    process = subprocess.Popen([KRITA, source, "--export", "--export-filename", target],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        process.wait(timeout=KRITA_TIMEOUT)
    except subprocess.TimeoutExpired:
        process.kill()  # this run's own process only
        process.wait()
        return "hang"
    return "ok" if os.path.exists(target) else "no output"


def run_gimp(jobs, work):
    script = os.path.join(work, "export.py")
    with open(script, "w", encoding="utf-8") as handle:
        handle.write("import gi\ngi.require_version('Gimp', '3.0')\nfrom gi.repository import Gimp, Gio\n")
        handle.write("JOBS = %r\n" % jobs)
        handle.write("for source, target in JOBS:\n"
                     "    image = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(source))\n"
                     "    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, image, Gio.File.new_for_path(target), None)\n"
                     "    image.delete()\n")
    process = subprocess.Popen([GIMP, "-i", "--batch-interpreter=python-fu-eval",
                                "-b", "exec(open(r'%s', encoding='utf-8').read())" % script,
                                "-b", "Gimp.get_pdb().lookup_procedure('gimp-quit').run(None)"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        process.wait(timeout=900)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def edit_locked_names(path):
    """Names of the layers and stacks a .ora marks edit-locked="true"."""
    with zipfile.ZipFile(path) as archive:
        root = ElementTree.fromstring(archive.read("stack.xml"))
    return sorted(element.get("name", "") for element in root.iter()
                  if element.tag in ("layer", "stack") and element.get("edit-locked") in ("true", "1"))


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    build = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(root, "build", "opt-dev")
    artifacts = os.path.join(build, "test-artifacts", "ora")
    fixtures = os.path.join(root, "local-test-fixtures", "ora")
    if not os.path.isdir(artifacts):
        print("no %s: run patchy_core_tests ora_writes_inspection_artifacts first" % artifacts)
        return 2
    apps = {"gimp": os.path.exists(GIMP), "krita": os.path.exists(KRITA)}
    if not any(apps.values()):
        print("SKIP: neither GIMP 3 nor Krita 5 is installed")
        return 0

    # (label, .ora path, Patchy's render as PNG bytes)
    subjects = []
    for name in sorted(os.listdir(artifacts)):
        if name.startswith("patchy-") and name.endswith(".ora"):
            path = os.path.join(artifacts, name)
            with zipfile.ZipFile(path) as archive:
                subjects.append((name, path, archive.read("mergedimage.png")))
    if os.path.isdir(fixtures):
        for name in sorted(os.listdir(fixtures)):
            ours = os.path.join(artifacts, "from-apps", name[:-4] + ".png")
            if name.endswith(".ora") and os.path.exists(ours):
                with open(ours, "rb") as handle:
                    subjects.append((name, os.path.join(fixtures, name), handle.read()))

    work = tempfile.mkdtemp(prefix="patchy-ora-check-")
    failures = []
    lines = []
    try:
        renders = {}
        if apps["gimp"]:
            jobs = [(path, os.path.join(work, "gimp-" + label[:-4] + ".png")) for label, path, _ in subjects]
            run_gimp(jobs, work)
            for (label, _, _), (_, target) in zip(subjects, jobs):
                renders[("gimp", label)] = target if os.path.exists(target) else None
        if apps["krita"]:
            for label, path, _ in subjects:
                target = os.path.join(work, "krita-" + label[:-4] + ".png")
                status = run_krita(path, target)
                renders[("krita", label)] = target if status == "ok" else status
        for label, path, ours_png in subjects:
            ours = decode_png(ours_png)
            per_layer = "modes" in label
            for app in ("gimp", "krita"):
                if not apps[app]:
                    continue
                render = renders.get((app, label))
                if render is None or render in ("hang", "no output"):
                    note = EXPECTED_FILES.get((app, label))
                    lines.append("%-6s %-34s %s%s" % (app, label, render or "no output",
                                                      " (expected: %s)" % note if note else ""))
                    if not note:
                        failures.append((app, label, render or "no output"))
                    continue
                with open(render, "rb") as handle:
                    theirs = decode_png(handle.read())
                if not per_layer:
                    result = compare(ours, theirs)
                    note = EXPECTED_FILES.get((app, label))
                    if result is None:
                        lines.append("%-6s %-34s size differs" % (app, label))
                        failures.append((app, label, "size"))
                        continue
                    mean, worst = result
                    limit = tolerance(app, None)
                    ok = mean <= limit[0] and worst <= limit[1]
                    lines.append("%-6s %-34s mean %6.2f worst %4.0f %s%s" % (
                        app, label, mean, worst, "ok" if ok else "OVER", " (expected: %s)" % note if note else ""))
                    if not ok and not note:
                        failures.append((app, label, "mean %.2f worst %.0f" % (mean, worst)))
                    continue
                for name, op, rect in layer_rects(path):
                    if rect[2] >= ours[0] and rect[3] >= ours[1]:
                        continue  # the backdrop: checked inside every tile
                    result = compare(ours, theirs, rect)
                    if result is None:
                        continue
                    mean, worst = result
                    limit = tolerance(app, op)
                    ok = mean <= limit[0] and worst <= limit[1]
                    note = expected(app, op)
                    lines.append("%-6s %-34s %-22s %-26s mean %6.2f worst %4.0f %s%s" % (
                        app, label, name, op, mean, worst, "ok" if ok else "OVER",
                        " (expected: %s)" % note if note and not ok else ""))
                    if not ok and not note:
                        failures.append((app, label + ":" + name, "%s mean %.2f worst %.0f" % (op, mean, worst)))
        # Lock read-back: each app opens Patchy's lock file and saves it as .ora again; the
        # same layers and groups must still be edit-locked.
        locks = os.path.join(artifacts, "patchy-locks.ora")
        if os.path.exists(locks):
            wanted = edit_locked_names(locks)
            resaved = {}
            if apps["gimp"]:
                target = os.path.join(work, "gimp-resave-locks.ora")
                run_gimp([(locks, target)], work)
                resaved["gimp"] = target
            if apps["krita"]:
                target = os.path.join(work, "krita-resave-locks.ora")
                resaved["krita"] = target if run_krita(locks, target) == "ok" else None
            for app, target in resaved.items():
                got = edit_locked_names(target) if target and os.path.exists(target) else None
                ok = got == wanted
                lines.append("%-6s %-34s edit-locked read back: %s %s" % (app, "patchy-locks.ora", got, "ok" if ok else
                                                                         "DIFFERS from %s" % wanted))
                if not ok:
                    failures.append((app, "patchy-locks.ora", "edit-locked %s" % got))
    finally:
        if os.path.basename(work).startswith("patchy-ora-check-"):
            shutil.rmtree(work, ignore_errors=True)
    report = "\n".join(lines)
    print(report)
    with open(os.path.join(artifacts, "real-apps-report.txt"), "w", encoding="utf-8") as handle:
        handle.write(report + "\n")
    if failures:
        print("\n%d comparisons over tolerance" % len(failures))
        return 1
    print("\nall comparisons within tolerance")
    return 0


if __name__ == "__main__":
    sys.exit(main())
