"""Fetches PaddlePaddle's own ONNX exports of the PP-OCR small set for evaluation.

    python -I fetch_ppocr.py <models-dir>

Each repo is pinned to the commit the Hugging Face API reports now; the commit, the declared
license and the SHA-256 of every file go into <models-dir>/manifest.json. Only
huggingface.co/PaddlePaddle is contacted. Nothing downloaded is executed.
"""
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

REPOS = [
    "PP-OCRv6_small_det_onnx",
    "PP-OCRv6_small_rec_onnx",
    "PP-LCNet_x1_0_textline_ori_onnx",
    "PP-LCNet_x0_25_textline_ori_onnx",
    "PP-LCNet_x1_0_doc_ori_onnx",
    "korean_PP-OCRv5_mobile_rec_onnx",
    "cyrillic_PP-OCRv5_mobile_rec_onnx",
    "eslav_PP-OCRv5_mobile_rec_onnx",
    "el_PP-OCRv5_mobile_rec_onnx",
    "th_PP-OCRv5_mobile_rec_onnx",
    "arabic_PP-OCRv5_mobile_rec_onnx",
    "devanagari_PP-OCRv5_mobile_rec_onnx",
    "ta_PP-OCRv5_mobile_rec_onnx",
    "te_PP-OCRv5_mobile_rec_onnx",
]
FILES = ["inference.onnx", "inference.yml", "README.md"]
AGENT = {"User-Agent": "ocr-bench/1.0"}


def get(url: str) -> bytes:
    with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=300) as response:
        return response.read()


def main() -> int:
    root = Path(sys.argv[1])
    root.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for repo in REPOS:
        info = json.loads(get(f"https://huggingface.co/api/models/PaddlePaddle/{repo}"))
        commit = info["sha"]
        entry = {"commit": commit, "license": (info.get("cardData") or {}).get("license"), "files": {}}
        folder = root / repo
        folder.mkdir(exist_ok=True)
        for name in FILES:
            data = get(f"https://huggingface.co/PaddlePaddle/{repo}/resolve/{commit}/{name}")
            (folder / name).write_bytes(data)
            entry["files"][name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        manifest[repo] = entry
        print(repo, commit[:10], entry["license"], sum(f["bytes"] for f in entry["files"].values()))
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
