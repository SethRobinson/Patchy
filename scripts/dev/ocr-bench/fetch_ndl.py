"""Fetches the NDL OCR-Lite PARSeq recognizer files for evaluation (never the layout model,
whose weights carry the DINOv3 terms).

    python -I fetch_ndl.py <models-dir>

Pinned to the repository's current master commit; commit and SHA-256 go into manifest.json.
Only api.github.com and raw.githubusercontent.com are contacted. parseq.py is fetched to be
read for its preprocessing, never run.
"""
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

REPO = "ndl-lab/ndlocr-lite"
FILES = [
    "src/model/parseq-ndl-24x256-30-tiny-189epoch-tegaki3-r8data-202604.onnx",
    "src/model/parseq-ndl-24x384-50-tiny-300epoch-tegaki3-r8data-202604.onnx",
    "src/model/parseq-ndl-24x768-100-tiny-153epoch-tegaki3-r8data-202604.onnx",
    "src/config/NDLmoji.yaml",
    "src/parseq.py",
    "README.md",
]
AGENT = {"User-Agent": "ocr-bench/1.0"}


def get(url: str) -> bytes:
    with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=600) as response:
        return response.read()


def main() -> int:
    root = Path(sys.argv[1])
    root.mkdir(parents=True, exist_ok=True)
    commit = json.loads(get(f"https://api.github.com/repos/{REPO}/commits/master"))["sha"]
    manifest = {"repository": REPO, "commit": commit, "files": {}}
    for name in FILES:
        data = get(f"https://raw.githubusercontent.com/{REPO}/{commit}/{name}")
        target = root / Path(name).name
        if target.suffix == ".py":
            target = target.with_suffix(".py.txt")  # read, never imported
        target.write_bytes(data)
        manifest["files"][name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        print(name, len(data))
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
