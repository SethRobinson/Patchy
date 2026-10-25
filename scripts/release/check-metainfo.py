#!/usr/bin/env python3
"""Checks that the Linux AppStream metainfo describes the version being released.

The AppImage and the Flatpak both install packaging/linux/com.rtsoft.patchy.metainfo.xml,
and software centers, the AppImage catalog and Flathub show its <screenshot> images. Those
are raw GitHub URLs pinned to a release tag (packaging/linux/README.md), so a pin that is
never moved keeps showing an old release's pictures: 1.17 and 1.18 shipped with the URLs
still pinned to v1.16. This script fails the release when that happens.

Checks (exit 1 on any failure):
  - every screenshot URL is a raw.githubusercontent.com URL into this repository at a tag;
  - the image file exists in the working tree and its PNG size matches the width/height
    attributes;
  - the pinned tag either is v<version> (created when this release is published, so the
    URL will resolve) or already exists with the SAME image bytes as the working tree.
    A pin that names another tag whose image differs, or a tag that does not exist,
    fails: the listing would show a stale picture or a broken link;
  - the <releases> list has an entry for <version>.

  python scripts/release/check-metainfo.py            check against CMakeLists.txt's version
  python scripts/release/check-metainfo.py --fix      repoint stale pins to v<version> and
                                                      correct width/height attributes
  python scripts/release/check-metainfo.py --version 1.19 [--fix]
                                                      the version about to be released, when
                                                      CMakeLists.txt has not been bumped yet

Run it after regenerating docs/images/screenshots/ (--fix) and at the version bump; the
release drivers run it before any package is built and again before publishing.
Stdlib and git only; run from anywhere inside the repository.
"""
import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
METAINFO = ROOT / "packaging" / "linux" / "com.rtsoft.patchy.metainfo.xml"
CMAKELISTS = ROOT / "CMakeLists.txt"
REPO = "SethRobinson/Patchy"
RAW_URL = re.compile(
    r"^https://raw\.githubusercontent\.com/" + re.escape(REPO) + r"/(?P<ref>[^/]+)/(?P<path>.+)$"
)
IMAGE_TAG = re.compile(r"(?P<open><image\b[^>]*>)(?P<url>\s*\S+\s*)(?P<close></image>)", re.S)
ATTR = re.compile(r'\b(?P<name>width|height)="(?P<value>\d+)"')


def git(*args):
    result = subprocess.run(
        ["git", "-C", str(ROOT), *args], capture_output=True, text=True, check=False
    )
    return result.returncode, result.stdout.strip()


def project_version():
    text = CMAKELISTS.read_text(encoding="utf-8")
    match = re.search(r"project\s*\([\s\S]*?\bVERSION\s+([0-9]+(?:\.[0-9]+){1,3})", text, re.I)
    if not match:
        sys.exit("error: could not read the project VERSION from CMakeLists.txt")
    return match.group(1)


def png_size(path: Path):
    with path.open("rb") as handle:
        header = handle.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", header[16:24])


def blob_at(ref: str, path: str):
    """The blob id of path at ref, '' when the ref has no such file, None when ref is unknown."""
    code, _ = git("rev-parse", "-q", "--verify", f"{ref}^{{commit}}")
    if code != 0:
        return None
    code, out = git("rev-parse", "-q", "--verify", f"{ref}:{path}")
    return out if code == 0 else ""


def blob_in_tree(path: Path):
    code, out = git("hash-object", "--", str(path))
    return out if code == 0 else None


def inspect(text: str, version: str):
    """Returns (errors, repoint, size_fixes, image_count) for one metainfo text."""
    release_tag = f"v{version}"
    errors = []
    repoint = False
    size_fixes = {}
    images = list(IMAGE_TAG.finditer(text))
    if not images:
        errors.append("no <screenshot><image> entries found")
    for match in images:
        url = match.group("url").strip()
        parsed = RAW_URL.match(url)
        if not parsed:
            errors.append(f"{url}: not a raw.githubusercontent.com URL into {REPO}")
            continue
        ref, rel_path = parsed.group("ref"), parsed.group("path")
        file = ROOT / Path(rel_path)
        if not file.is_file():
            errors.append(f"{url}: {rel_path} does not exist in the working tree")
            continue

        declared = {m.group("name"): int(m.group("value")) for m in ATTR.finditer(match.group("open"))}
        actual = png_size(file)
        if actual is None:
            errors.append(f"{rel_path}: not a PNG file")
        elif (declared.get("width"), declared.get("height")) != actual:
            size_fixes[match.start()] = actual
            errors.append(
                f"{rel_path}: metainfo says {declared.get('width')}x{declared.get('height')}, "
                f"the file is {actual[0]}x{actual[1]}"
            )

        tree_blob = blob_in_tree(file)
        pinned_blob = blob_at(ref, rel_path)
        if ref == release_tag:
            if pinned_blob is not None and pinned_blob != tree_blob:
                errors.append(
                    f"{rel_path}: pinned to {ref}, which is already tagged with a different image. "
                    f"A public tag is never moved: bump the version (CMakeLists.txt) and rerun with --fix"
                )
        elif pinned_blob is None:
            repoint = True
            errors.append(
                f"{rel_path}: pinned to {ref}, which is not a tag or branch here and is not {release_tag}. "
                f"If {ref} is the version about to be released, bump CMakeLists.txt first"
            )
        elif pinned_blob != tree_blob:
            repoint = True
            errors.append(
                f"{rel_path}: pinned to {ref}, but the image changed since then. "
                f"Point it at {release_tag} (--fix) so the listing shows this release"
            )

    if not re.search(r'<release\s+version="' + re.escape(version) + r'"', text):
        errors.append(f'no <release version="{version}"> entry in the <releases> list')
    return errors, repoint, size_fixes, len(images)


def rewrite(text: str, release_tag: str, repoint: bool, size_fixes: dict) -> str:
    """Repoints every screenshot URL to release_tag and corrects the listed sizes."""

    def replace(match):
        open_tag = match.group("open")
        if match.start() in size_fixes:
            width, height = size_fixes[match.start()]
            open_tag = ATTR.sub(
                lambda m: f'{m.group("name")}="{width if m.group("name") == "width" else height}"',
                open_tag,
            )
        url = match.group("url")
        if repoint:
            bare = url.strip()
            pinned = RAW_URL.sub(
                lambda m: f"https://raw.githubusercontent.com/{REPO}/{release_tag}/{m.group('path')}",
                bare,
            )
            url = url.replace(bare, pinned)
        return open_tag + url + match.group("close")

    return IMAGE_TAG.sub(replace, text)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--fix", action="store_true", help="repoint stale pins and fix image sizes")
    parser.add_argument("--version", help="the version being released (default: CMakeLists.txt)")
    args = parser.parse_args()

    version = args.version or project_version()
    release_tag = f"v{version}"
    text = METAINFO.read_text(encoding="utf-8")
    newline = "\r\n" if "\r\n" in text else "\n"
    errors, repoint, size_fixes, count = inspect(text, version)

    if args.fix and (repoint or size_fixes):
        fixed = rewrite(text, release_tag, repoint, size_fixes)
        if fixed != text:
            METAINFO.write_text(
                fixed.replace("\r\n", "\n").replace("\n", newline), encoding="utf-8", newline=""
            )
            print(
                f"fixed: screenshot entries in {METAINFO.relative_to(ROOT)}"
                + (f" now pin {release_tag}" if repoint else "")
            )
            errors, repoint, size_fixes, count = inspect(fixed, version)

    for error in errors:
        print(f"error: {error}")
    status = "FAILED" if errors else "passed"
    print(f"[SUMMARY] metainfo check for {release_tag}: {count} screenshots, {len(errors)} errors, {status}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
