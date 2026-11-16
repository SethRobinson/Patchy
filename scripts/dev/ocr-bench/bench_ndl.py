"""Reads the Japanese benchmark pages with the NDL OCR-Lite PARSeq recognizer, on line
boxes found by PP-OCR's detector (NDL's own layout model is not used).

    python -I bench_ndl.py <pages-dir> <ppocr-models> <ndl-models> <out-json> [--det 1536] [--threads N]

Each line goes to one of NDL's three recognizers by its length (about 30, 50 or 100
characters), prepared as NDL's own code does: a tall crop is turned a quarter turn
counterclockwise, stretched to the model's input size, RGB, scaled to -1..1.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import cv2
import numpy as np
import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_ppocr  # noqa: E402
import ppocr_ref  # noqa: E402

MODELS = [
    (25.0, "parseq-ndl-24x256-30-tiny-189epoch-tegaki3-r8data-202604.onnx"),
    (42.0, "parseq-ndl-24x384-50-tiny-300epoch-tegaki3-r8data-202604.onnx"),
    (1e9, "parseq-ndl-24x768-100-tiny-153epoch-tegaki3-r8data-202604.onnx"),
]


class NdlRecognizer:
    mode = "100"

    def __init__(self, folder: Path, threads: int):
        charset = yaml.safe_load((folder / "NDLmoji.yaml").read_text(encoding="utf-8"))["model"]["charset_train"]
        self.characters = list(charset)
        self.sessions = []
        for limit, name in MODELS:
            session = ppocr_ref.session(folder / name, threads)
            shape = session.get_inputs()[0].shape
            self.sessions.append((limit, session, session.get_inputs()[0].name, int(shape[2]), int(shape[3])))

    def read(self, crop: np.ndarray) -> str:
        height, width = crop.shape[:2]
        if height > width * 0.8:
            crop = cv2.rotate(crop, cv2.ROTATE_90_COUNTERCLOCKWISE)
        # NDL's cascade: the 30-character model first; a result that nearly fills a model
        # goes to the next larger one (25 or more characters, then 45 or more).
        text = ""
        if self.mode == "100":
            # NDL's own fallback when its layout model gave no character count.
            _, session, input_name, in_h, in_w = self.sessions[2]
            return self._run(crop, session, input_name, in_h, in_w)
        if self.mode == "aspect":
            aspect = crop.shape[1] / max(1, crop.shape[0])
            for limit, session, input_name, in_h, in_w in self.sessions:
                if aspect <= limit:
                    break
            return self._run(crop, session, input_name, in_h, in_w)
        for (limit, session, input_name, in_h, in_w), full in zip(self.sessions, (25, 45, 10 ** 9)):
            text = self._run(crop, session, input_name, in_h, in_w)
            if len(text) < full:
                break
        return text

    def _run(self, crop: np.ndarray, session, input_name: str, in_h: int, in_w: int) -> str:
        resized = cv2.resize(crop, (in_w, in_h), interpolation=cv2.INTER_LINEAR)
        tensor = (resized[:, :, ::-1].astype(np.float32) / 127.5 - 1.0).transpose(2, 0, 1)[None]
        output = session.run(None, {input_name: np.ascontiguousarray(tensor)})[0]
        indices = output[0].argmax(axis=1)
        stops = np.nonzero(indices == 0)[0]
        end = int(stops[0]) if stops.size else len(indices)
        return "".join(self.characters[i - 1] for i in indices[:end].tolist() if 0 < i <= len(self.characters))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pages")
    parser.add_argument("ppocr")
    parser.add_argument("ndl")
    parser.add_argument("out")
    parser.add_argument("--variants", default="clean300,scan300,clean150,scan150")
    parser.add_argument("--det", type=int, default=1536)
    parser.add_argument("--threads", type=int, default=0)
    parser.add_argument("--dump", default="")
    parser.add_argument("--unclip", type=float, default=1.4)
    parser.add_argument("--only", default="")
    parser.add_argument("--mode", default="100", choices=["100", "aspect", "cascade"])
    args = parser.parse_args()

    pages_dir = Path(args.pages)
    described = json.loads((pages_dir / "pages.json").read_text(encoding="utf-8"))["pages"]
    detector = ppocr_ref.Detector(Path(args.ppocr) / "PP-OCRv6_small_det_onnx", args.threads,
                                  unclip_ratio=args.unclip)
    only = {name for name in args.only.split(",") if name}
    recognizer = NdlRecognizer(Path(args.ndl), args.threads)
    recognizer.mode = args.mode
    rows = []
    for index, page in enumerate(described):
        if page["language"] != "ja" or (only and page["name"] not in only):
            continue
        image = cv2.imdecode(np.fromfile(str(pages_dir / (page["name"] + ".png")), dtype=np.uint8), cv2.IMREAD_COLOR)
        truth = bench_ppocr.squeeze("".join(page["lines"]))
        missing = sorted({ch for ch in truth if ch not in set(recognizer.characters)})
        for variant_name in args.variants.split(","):
            pixels = bench_ppocr.variant(image, variant_name, seed=1000 + index)
            wall = time.perf_counter()
            timing = ppocr_ref.Timing()
            lines = detector.detect(pixels, args.det, timing)
            started = time.perf_counter()
            for line in lines:
                crop = ppocr_ref.crop_line(pixels, line.quad)
                line.vertical = crop.shape[0] >= 1.5 * crop.shape[1]
                line.text = recognizer.read(crop)
            recognize_ms = (time.perf_counter() - started) * 1000.0
            wall_ms = (time.perf_counter() - wall) * 1000.0
            ordered = ppocr_ref.reading_order(lines)
            text = bench_ppocr.squeeze("".join(line.text for line in ordered))
            distance = bench_ppocr.edit_distance(truth, text)
            row = {"page": page["name"], "language": "ja", "variant": variant_name, "det_side": args.det,
                   "truth_chars": len(truth), "read_chars": len(text), "errors": distance,
                   "cer": round(distance / max(1, len(truth)), 5), "truth_lines": len(page["lines"]),
                   "lines": len(lines), "wall_ms": round(wall_ms), "detect_ms": round(timing.detect_ms),
                   "recognize_ms": round(recognize_ms), "not_in_dictionary": "".join(missing)}
            rows.append(row)
            print(f'{row["page"]:16} {variant_name:9} CER {row["cer"] * 100:6.2f}%  lines {row["lines"]:3}/'
                  f'{row["truth_lines"]:3} {row["wall_ms"]:6} ms (det {row["detect_ms"]}, rec {row["recognize_ms"]})',
                  flush=True)
            if args.dump:
                dump = Path(args.dump)
                dump.mkdir(parents=True, exist_ok=True)
                (dump / f'{page["name"]}.{variant_name}.txt').write_text("\n".join(l.text for l in ordered),
                                                                           encoding="utf-8")
    Path(args.out).write_text(json.dumps({"peak_memory_mb": round(bench_ppocr.peak_memory_mb()),
                                          "threads": args.threads, "rows": rows}, ensure_ascii=False, indent=1),
                              encoding="utf-8")
    print("peak memory", round(bench_ppocr.peak_memory_mb()), "MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
