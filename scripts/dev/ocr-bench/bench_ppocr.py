"""Runs the reference PP-OCR pipeline over the benchmark pages and scores it.

    python -I bench_ppocr.py <pages-dir> <models-dir> <out-json> [--variants clean300,scan300,...]
                             [--det 960,1536,2496] [--threads N] [--classify] [--only name,...]

Variants of each page: clean300 (as typeset), scan300 (a simulated scan: slight turn, blur,
noise, JPEG), clean150 and scan150 (the same at half size). The score is the character error
rate against the page's own text with all whitespace removed, lines taken in reading order.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import sys
import time
import unicodedata
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ppocr_ref  # noqa: E402

RECOGNIZER_FOR = {
    "en": ["PP-OCRv6_small_rec_onnx"],
    "latin": ["PP-OCRv6_small_rec_onnx"],
    "ja": ["PP-OCRv6_small_rec_onnx"],
    "ru": ["eslav_PP-OCRv5_mobile_rec_onnx", "cyrillic_PP-OCRv5_mobile_rec_onnx", "PP-OCRv6_small_rec_onnx"],
    "ko": ["korean_PP-OCRv5_mobile_rec_onnx", "PP-OCRv6_small_rec_onnx"],
}


def peak_memory_mb() -> float:
    class Counters(ctypes.Structure):
        _fields_ = [("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong),
                    ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                    ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                    ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]

    counters = Counters()
    counters.cb = ctypes.sizeof(Counters)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    psapi.GetProcessMemoryInfo.argtypes = [ctypes.c_void_p, ctypes.POINTER(Counters), ctypes.c_ulong]
    if not psapi.GetProcessMemoryInfo(kernel.GetCurrentProcess(), ctypes.byref(counters), counters.cb):
        return 0.0
    return counters.PeakWorkingSetSize / (1024.0 * 1024.0)


def squeeze(text: str) -> str:
    return "".join(ch for ch in unicodedata.normalize("NFC", text) if not ch.isspace())


def edit_distance(a: str, b: str) -> int:
    if not a:
        return len(b)
    if not b:
        return len(a)
    target = np.frombuffer(b.encode("utf-32-le"), dtype=np.uint32)
    previous = np.arange(len(b) + 1, dtype=np.int32)
    offsets = np.arange(len(b) + 1, dtype=np.int32)
    for i, ch in enumerate(a, start=1):
        cost = (target != ord(ch)).astype(np.int32)
        # Deletion and substitution in one pass, then insertions as a running minimum.
        current = np.empty_like(previous)
        current[0] = i
        np.minimum(previous[1:] + 1, previous[:-1] + cost, out=current[1:])
        current = np.minimum.accumulate(current - offsets) + offsets
        previous = current
    return int(previous[-1])


def scan_variant(image: np.ndarray, seed: int) -> np.ndarray:
    """A plain simulated scan: turned 0.6 degrees, slightly soft, grain, JPEG at quality 60."""
    rng = np.random.default_rng(seed)
    height, width = image.shape[:2]
    matrix = cv2.getRotationMatrix2D((width / 2.0, height / 2.0), 0.6, 1.0)
    turned = cv2.warpAffine(image, matrix, (width, height), flags=cv2.INTER_LINEAR, borderValue=(255, 255, 255))
    soft = cv2.GaussianBlur(turned, (0, 0), 0.9)
    paper = soft.astype(np.float32) * 0.94 + 8.0
    noisy = np.clip(paper + rng.normal(0.0, 6.0, soft.shape).astype(np.float32), 0, 255).astype(np.uint8)
    ok, encoded = cv2.imencode(".jpg", noisy, [cv2.IMWRITE_JPEG_QUALITY, 60])
    return cv2.imdecode(encoded, cv2.IMREAD_COLOR) if ok else noisy


def variant(image: np.ndarray, name: str, seed: int) -> np.ndarray:
    if name.endswith("150"):
        image = cv2.resize(image, (image.shape[1] // 2, image.shape[0] // 2), interpolation=cv2.INTER_AREA)
    return scan_variant(image, seed) if name.startswith("scan") else image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pages")
    parser.add_argument("models")
    parser.add_argument("out")
    parser.add_argument("--variants", default="clean300,scan300,clean150,scan150")
    parser.add_argument("--det", default="960,1536,2496")
    parser.add_argument("--threads", type=int, default=0)
    parser.add_argument("--classify", action="store_true")
    parser.add_argument("--only", default="")
    parser.add_argument("--dump", default="")
    parser.add_argument("--turn", type=int, default=1)
    parser.add_argument("--batch", type=int, default=8)
    args = parser.parse_args()

    pages_dir, models = Path(args.pages), Path(args.models)
    described = json.loads((pages_dir / "pages.json").read_text(encoding="utf-8"))["pages"]
    only = {name for name in args.only.split(",") if name}
    started = time.perf_counter()
    detector = ppocr_ref.Detector(models / "PP-OCRv6_small_det_onnx", args.threads)
    classifier = ppocr_ref.Classifier(models / "PP-LCNet_x1_0_textline_ori_onnx", args.threads) if args.classify else None
    recognizers: dict[str, ppocr_ref.Recognizer] = {}
    load_ms = (time.perf_counter() - started) * 1000.0
    rows = []
    for index, page in enumerate(described):
        if only and page["name"] not in only:
            continue
        image = cv2.imdecode(np.fromfile(str(pages_dir / (page["name"] + ".png")), dtype=np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            print("missing page image", page["name"])
            continue
        truth = squeeze("".join(page["lines"]))
        for variant_name in args.variants.split(","):
            pixels = variant(image, variant_name, seed=1000 + index)
            for recognizer_name in RECOGNIZER_FOR[page["language"]]:
                if recognizer_name not in recognizers:
                    recognizers[recognizer_name] = ppocr_ref.Recognizer(models / recognizer_name, args.threads, batch=args.batch)
                recognizer = recognizers[recognizer_name]
                missing = sorted({ch for ch in truth if ch not in set(recognizer.characters)})
                for det_side in [int(v) for v in args.det.split(",")]:
                    wall = time.perf_counter()
                    lines, timing = ppocr_ref.read_page(pixels, detector, recognizer, classifier, det_side, args.turn)
                    wall_ms = (time.perf_counter() - wall) * 1000.0
                    ordered = ppocr_ref.reading_order(lines)
                    text = squeeze("".join(line.text for line in ordered))
                    distance = edit_distance(truth, text)
                    row = {
                        "page": page["name"], "language": page["language"], "variant": variant_name,
                        "recognizer": recognizer_name, "det_side": det_side, "det_size": list(timing.det_size),
                        "truth_chars": len(truth), "read_chars": len(text), "errors": distance,
                        "cer": round(distance / max(1, len(truth)), 5), "truth_lines": len(page["lines"]),
                        "lines": timing.lines, "vertical_lines": sum(1 for l in lines if l.vertical),
                        "wall_ms": round(wall_ms), "detect_ms": round(timing.detect_ms),
                        "classify_ms": round(timing.classify_ms), "recognize_ms": round(timing.recognize_ms),
                        "other_ms": round(timing.other_ms), "not_in_dictionary": "".join(missing),
                    }
                    rows.append(row)
                    print(f'{row["page"]:16} {variant_name:9} {recognizer_name[:22]:22} det {det_side:5} '
                          f'CER {row["cer"] * 100:6.2f}%  lines {row["lines"]:3}/{row["truth_lines"]:3} '
                          f'{row["wall_ms"]:6} ms (det {row["detect_ms"]}, rec {row["recognize_ms"]})', flush=True)
                    if args.dump:
                        dump = Path(args.dump)
                        dump.mkdir(parents=True, exist_ok=True)
                        (dump / f'{page["name"]}.{variant_name}.{recognizer_name}.{det_side}.txt').write_text(
                            "\n".join(line.text for line in ordered), encoding="utf-8")
    summary = {"load_ms": round(load_ms), "peak_memory_mb": round(peak_memory_mb()), "threads": args.threads,
               "classify": args.classify, "rows": rows}
    Path(args.out).write_text(json.dumps(summary, ensure_ascii=False, indent=1), encoding="utf-8")
    print("peak memory", summary["peak_memory_mb"], "MB; model load", summary["load_ms"], "ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
