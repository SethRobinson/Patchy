"""One table for the three engines from their saved outputs, scored two ways: as read (NFC)
and with width forms folded (NFKC: full-width Latin and digits count as their plain forms).

    python -I rescore.py <pages-dir> <bench-dir>
"""
import json
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_ppocr  # noqa: E402

VARIANTS = ["clean300", "scan300", "clean150", "scan150"]
BEST_PPOCR = {"en": "PP-OCRv6_small_rec_onnx", "latin": "PP-OCRv6_small_rec_onnx", "ja": "PP-OCRv6_small_rec_onnx",
              "ru": "cyrillic_PP-OCRv5_mobile_rec_onnx", "ko": "korean_PP-OCRv5_mobile_rec_onnx"}


def fold(text: str) -> str:
    return "".join(ch for ch in unicodedata.normalize("NFKC", text) if not ch.isspace())


def cer(truth: str, read: str, folded: bool) -> float:
    a, b = (fold(truth), fold(read)) if folded else (bench_ppocr.squeeze(truth), bench_ppocr.squeeze(read))
    return 100.0 * bench_ppocr.edit_distance(a, b) / max(1, len(a))


pages_dir, bench = Path(sys.argv[1]), Path(sys.argv[2])
pages = json.loads((pages_dir / "pages.json").read_text(encoding="utf-8"))["pages"]
out = {}
for page in pages:
    truth = "".join(page["lines"])
    sources = {
        "ppocr": lambda v: bench / "dump" / f'{page["name"]}.{v}.{BEST_PPOCR[page["language"]]}.1536.txt',
        "ndl": lambda v: bench / "dump-ndl" / f'{page["name"]}.{v}.txt',
        "windows": lambda v: bench / "dump-windows" / f'{page["name"]}.{v}.txt',
    }
    for engine, path_of in sources.items():
        cells = []
        for v in VARIANTS:
            path = path_of(v)
            if not path.exists():
                cells.append(None)
                continue
            read = path.read_text(encoding="utf-8")
            cells.append((round(cer(truth, read, False), 2), round(cer(truth, read, True), 2)))
        if any(cells):
            out[f'{page["name"]}|{engine}'] = cells
            print(f'{page["name"]:16} {engine:8} ' + "  ".join(
                f"{c[0]:6.2f} / {c[1]:6.2f}" if c else " " * 15 for c in cells))
(bench / "rescore.json").write_text(json.dumps(out, indent=1), encoding="utf-8")
