"""Fetches other programs' OpenRaster files for the corpus (docs/ora.md, "Corpus").

Usage: python -I scripts/dev/ora/fetch_wild_corpus.py [OUT_DIR] [--record]

wild_corpus_sources.json, beside this script, pins every file: its public GitHub
repository, the commit, the path, the SHA-256 and the repository's stated licence. Each
file is fetched from that commit, checked against its hash and written to
OUT_DIR/<owner>__<repository>/<path with / as __>; a file already there with the right
hash is skipped. OUT_DIR defaults to local-test-fixtures/ora-corpus/wild, which is never
committed, and gets a README.txt naming every source and licence.

--record is for adding sources: an entry may then leave out "commit" (the branch tip is
pinned) and a file its "sha256" (the fetched bytes are pinned), and anything that is not
a zip archive is dropped from the list. The list is rewritten in place.

Only the .ora files are fetched. They are test data: nothing here reads the source of the
program a file came from, and the files are never linked into or shipped with Patchy.
"""

import argparse
import hashlib
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCES = Path(__file__).with_name("wild_corpus_sources.json")
LIMIT = 96 << 20


def get(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "openraster-test-corpus-fetch"})
    with urllib.request.urlopen(request, timeout=180) as response:
        data = response.read(LIMIT + 1)
    if len(data) > LIMIT:
        raise ValueError("larger than the size limit")
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", nargs="?", type=Path, default=ROOT / "local-test-fixtures/ora-corpus/wild")
    parser.add_argument("--record", action="store_true", help="pin missing commits and hashes, drop what is not a zip")
    args = parser.parse_args()
    out = args.out.resolve()
    sources = json.loads(SOURCES.read_text(encoding="utf-8"))
    fetched = kept = failed = 0
    readme = ["Other programs' OpenRaster files, not committed. Written by scripts/dev/ora/fetch_wild_corpus.py",
              "from scripts/dev/ora/wild_corpus_sources.json, which pins each file's commit and SHA-256.",
              "Each file is under its repository's licence, as that repository states it.", ""]
    for source in sources:
        repo = source["repo"]
        try:
            if not source.get("commit"):
                if not args.record:
                    raise ValueError("no commit pinned (run with --record)")
                branch = urllib.parse.quote(source.get("branch") or "HEAD")
                source["commit"] = json.loads(get(f"https://api.github.com/repos/{repo}/commits/{branch}"))["sha"]
                source.pop("branch", None)
        except (OSError, ValueError, KeyError) as error:
            print(f"{repo}: {error}")
            failed += len(source["files"])
            continue
        folder = out / repo.replace("/", "__")
        good = []
        for entry in source["files"]:
            target = folder / entry["path"].replace("/", "__")
            wanted = entry.get("sha256")
            if wanted and target.is_file() and hashlib.sha256(target.read_bytes()).hexdigest() == wanted:
                kept += 1
                good.append(entry)
                continue
            url = f"https://raw.githubusercontent.com/{repo}/{source['commit']}/{urllib.parse.quote(entry['path'])}"
            try:
                data = get(url)
            except (OSError, ValueError) as error:
                print(f"{repo}/{entry['path']}: {error}")
                failed += 1
                good.append(entry)
                continue
            digest = hashlib.sha256(data).hexdigest()
            if wanted and digest != wanted:
                print(f"{repo}/{entry['path']}: the fetched file does not match its pinned hash")
                failed += 1
                good.append(entry)
                continue
            if not data.startswith(b"PK"):
                print(f"{repo}/{entry['path']}: not a zip archive" + (", dropped" if args.record else ""))
                if not args.record:
                    failed += 1
                    good.append(entry)
                continue
            folder.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            fetched += 1
            good.append({"path": entry["path"], "size": len(data), "sha256": digest})
        source["files"] = good
        readme.append(f"{repo.replace('/', '__')}/: https://github.com/{repo}/tree/{source['commit']}"
                      f" ({source['licence']}), {len(good)} files")
    if args.record:
        sources = [source for source in sources if source["files"]]
        SOURCES.write_text(json.dumps(sources, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="")
    out.mkdir(parents=True, exist_ok=True)
    (out / "README.txt").write_text("\n".join(readme) + "\n", encoding="utf-8", newline="")
    print(f"{fetched} fetched, {kept} already there, {failed} failed; corpus: {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
