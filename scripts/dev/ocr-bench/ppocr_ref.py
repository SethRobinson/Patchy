"""Reference PP-OCR pipeline on ONNX Runtime, written the way Patchy's C++ engine is planned
(docs/ocr.md): the detector's probability map is thresholded, each connected region becomes
one rotated rectangle (no dilation of the mask, no free quadrilaterals), lines are cropped as
rectangles from the full-resolution image, tall crops are turned a quarter turn, and the
recognizer's output is decoded greedily (CTC).

Only cv2, numpy and onnxruntime are used. Model files are data: nothing in them is executed
outside ONNX Runtime; the YAML next to each model is parsed for its character list only.
"""
from __future__ import annotations

import math
import time
from dataclasses import dataclass, field
from pathlib import Path

import cv2
import numpy as np
import onnxruntime as ort

DET_MEAN = np.array([0.485, 0.456, 0.406], dtype=np.float32)
DET_STD = np.array([0.229, 0.224, 0.225], dtype=np.float32)


def session(path: Path, threads: int = 0) -> ort.InferenceSession:
    options = ort.SessionOptions()
    options.log_severity_level = 3
    if threads > 0:
        options.intra_op_num_threads = threads
    return ort.InferenceSession(str(path), sess_options=options, providers=["CPUExecutionProvider"])


def read_dictionary(yml: Path) -> list[str]:
    """The character_dict list of a recognizer's inference.yml, in order. Parsed by hand so no
    YAML tags or anchors in a downloaded file are ever acted on."""
    chars: list[str] = []
    inside = False
    for raw in yml.read_text(encoding="utf-8").splitlines():
        if raw.startswith("  character_dict:"):
            inside = True
            continue
        if not inside:
            continue
        if not raw.startswith("  - "):
            if raw.startswith("  -") and raw.strip() == "-":
                chars.append("")
                continue
            break
        value = raw[4:]
        if len(value) >= 2 and value[0] == "'" and value[-1] == "'":
            value = value[1:-1].replace("''", "'")
        elif len(value) >= 2 and value[0] == '"' and value[-1] == '"':
            value = bytes(value[1:-1], "utf-8").decode("unicode_escape") if "\\" in value else value[1:-1]
        chars.append(value)
    return chars


@dataclass
class Line:
    quad: np.ndarray  # 4 x 2 float32, top-left, top-right, bottom-right, bottom-left of the text's own frame
    score: float
    text: str = ""
    vertical: bool = False
    confidence: float = 0.0


@dataclass
class Timing:
    detect_ms: float = 0.0
    classify_ms: float = 0.0
    recognize_ms: float = 0.0
    other_ms: float = 0.0
    lines: int = 0
    det_size: tuple[int, int] = (0, 0)
    extra: dict = field(default_factory=dict)


class Detector:
    def __init__(self, model_dir: Path, threads: int = 0, thresh: float = 0.2, box_thresh: float = 0.45,
                 unclip_ratio: float = 1.4):
        self.session = session(model_dir / "inference.onnx", threads)
        self.thresh, self.box_thresh, self.unclip_ratio = thresh, box_thresh, unclip_ratio

    def detect(self, bgr: np.ndarray, long_side: int, timing: Timing) -> list[Line]:
        height, width = bgr.shape[:2]
        scale = min(1.0, long_side / max(height, width)) if long_side > 0 else 1.0
        # Sides are multiples of 32, at least 32.
        target_w = max(32, int(round(width * scale / 32.0)) * 32)
        target_h = max(32, int(round(height * scale / 32.0)) * 32)
        resized = cv2.resize(bgr, (target_w, target_h), interpolation=cv2.INTER_AREA if scale < 1.0 else cv2.INTER_LINEAR)
        tensor = ((resized.astype(np.float32) / 255.0 - DET_MEAN) / DET_STD).transpose(2, 0, 1)[None]
        started = time.perf_counter()
        probability = self.session.run(None, {"x": tensor})[0][0, 0]
        timing.detect_ms += (time.perf_counter() - started) * 1000.0
        timing.det_size = (target_w, target_h)
        started = time.perf_counter()
        lines = self._boxes(probability, width / target_w, height / target_h)
        timing.other_ms += (time.perf_counter() - started) * 1000.0
        return lines

    def _boxes(self, probability: np.ndarray, scale_x: float, scale_y: float) -> list[Line]:
        mask = (probability > self.thresh).astype(np.uint8)
        count, labels, stats, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
        lines: list[Line] = []
        for label in range(1, count):
            x, y, w, h, area = stats[label]
            if area < 9 or min(w, h) < 3:
                continue
            ys, xs = np.nonzero(labels[y:y + h, x:x + w] == label)
            points = np.stack([xs + x, ys + y], axis=1).astype(np.float32)
            (cx, cy), (rw, rh), angle = cv2.minAreaRect(points)
            if min(rw, rh) < 3:
                continue
            # The box's score is the mean probability over the region's own pixels.
            score = float(probability[ys + y, xs + x].mean())
            if score < self.box_thresh:
                continue
            # Grow every side by the unclip distance (area * ratio / perimeter), as a rectangle.
            distance = (rw * rh) * self.unclip_ratio / (2.0 * (rw + rh))
            rect = ((cx, cy), (rw + 2.0 * distance, rh + 2.0 * distance), angle)
            corners = cv2.boxPoints(rect)
            corners[:, 0] *= scale_x
            corners[:, 1] *= scale_y
            lines.append(Line(order_corners(corners), score))
        return lines


def order_corners(corners: np.ndarray) -> np.ndarray:
    """Top-left, top-right, bottom-right, bottom-left, for a rectangle that is near upright."""
    by_x = corners[np.argsort(corners[:, 0])]
    left = by_x[:2][np.argsort(by_x[:2, 1])]
    right = by_x[2:][np.argsort(by_x[2:, 1])]
    return np.array([left[0], right[0], right[1], left[1]], dtype=np.float32)


def crop_line(bgr: np.ndarray, quad: np.ndarray) -> np.ndarray:
    width = int(round(max(np.linalg.norm(quad[0] - quad[1]), np.linalg.norm(quad[3] - quad[2]))))
    height = int(round(max(np.linalg.norm(quad[0] - quad[3]), np.linalg.norm(quad[1] - quad[2]))))
    width, height = max(1, width), max(1, height)
    target = np.array([[0, 0], [width, 0], [width, height], [0, height]], dtype=np.float32)
    # Three corners fix the map: a rectangle crop is a rotation plus a scale, never a perspective warp.
    matrix = cv2.getAffineTransform(quad[:3], target[:3])
    return cv2.warpAffine(bgr, matrix, (width, height), flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE)


class Classifier:
    """Text line orientation: 0 or 180 degrees."""

    def __init__(self, model_dir: Path, threads: int = 0):
        self.session = session(model_dir / "inference.onnx", threads)

    def upside_down(self, crops: list[np.ndarray], timing: Timing) -> list[bool]:
        result: list[bool] = []
        for start in range(0, len(crops), 16):
            batch = []
            for crop in crops[start:start + 16]:
                resized = cv2.resize(crop, (160, 80), interpolation=cv2.INTER_LINEAR).astype(np.float32) / 255.0
                batch.append(((resized - DET_MEAN) / DET_STD).transpose(2, 0, 1))
            started = time.perf_counter()
            scores = self.session.run(None, {"x": np.stack(batch)})[0]
            timing.classify_ms += (time.perf_counter() - started) * 1000.0
            result.extend(bool(row[1] > row[0] and row[1] > 0.9) for row in scores)
        return result


class Recognizer:
    def __init__(self, model_dir: Path, threads: int = 0, max_width: int = 3200, batch: int = 8):
        self.session = session(model_dir / "inference.onnx", threads)
        self.characters = read_dictionary(model_dir / "inference.yml")
        self.max_width, self.batch = max_width, batch

    def recognize(self, crops: list[np.ndarray], timing: Timing) -> list[tuple[str, float]]:
        results: list[tuple[str, float]] = [("", 0.0)] * len(crops)
        # Similar widths share a batch, so little of a batch is padding.
        order = sorted(range(len(crops)), key=lambda i: crops[i].shape[1] / max(1, crops[i].shape[0]))
        for start in range(0, len(order), self.batch):
            indices = order[start:start + self.batch]
            widths = []
            for i in indices:
                h, w = crops[i].shape[:2]
                widths.append(min(self.max_width, max(16, int(math.ceil(48.0 * w / max(1, h))))))
            batch_width = max(max(widths), 160)
            tensor = np.zeros((len(indices), 3, 48, batch_width), dtype=np.float32)
            for row, (i, width) in enumerate(zip(indices, widths)):
                resized = cv2.resize(crops[i], (width, 48), interpolation=cv2.INTER_LINEAR).astype(np.float32)
                tensor[row, :, :, :width] = ((resized / 255.0 - 0.5) / 0.5).transpose(2, 0, 1)
            started = time.perf_counter()
            logits = self.session.run(None, {"x": tensor})[0]
            timing.recognize_ms += (time.perf_counter() - started) * 1000.0
            best = logits.argmax(axis=2)
            peak = logits.max(axis=2)
            for row, i in enumerate(indices):
                results[i] = self._decode(best[row], peak[row])
        return results

    def _decode(self, indices: np.ndarray, peaks: np.ndarray) -> tuple[str, float]:
        text, scores, previous = [], [], 0
        for index, peak in zip(indices.tolist(), peaks.tolist()):
            if index != 0 and index != previous:
                # 0 is the CTC blank; 1..N are the dictionary; N + 1 is the space.
                text.append(self.characters[index - 1] if index - 1 < len(self.characters) else " ")
                scores.append(peak)
            previous = index
        return "".join(text), (float(np.mean(scores)) if scores else 0.0)


def read_page(bgr: np.ndarray, detector: Detector, recognizer: Recognizer, classifier: Classifier | None,
              det_long_side: int, turn: int = 1) -> tuple[list[Line], Timing]:
    timing = Timing()
    lines = detector.detect(bgr, det_long_side, timing)
    started = time.perf_counter()
    crops = []
    for line in lines:
        crop = crop_line(bgr, line.quad)
        if crop.shape[0] >= 1.5 * crop.shape[1]:
            # A column: turned a quarter turn so it reads left to right (the top goes to the left).
            line.vertical = True
            crop = np.ascontiguousarray(np.rot90(crop, turn))  # 1: top goes left; 3: top goes right
        crops.append(crop)
    timing.other_ms += (time.perf_counter() - started) * 1000.0
    if classifier is not None and crops:
        for index, flipped in enumerate(classifier.upside_down(crops, timing)):
            if flipped:
                crops[index] = np.ascontiguousarray(np.rot90(crops[index], 2))
    if crops:
        for line, (text, confidence) in zip(lines, recognizer.recognize(crops, timing)):
            line.text, line.confidence = text, confidence
    timing.lines = len(lines)
    return lines, timing


def reading_order(lines: list[Line]) -> list[Line]:
    """Horizontal lines top to bottom; vertical columns right to left. Horizontal first."""
    def centre(line: Line) -> tuple[float, float]:
        return float(line.quad[:, 0].mean()), float(line.quad[:, 1].mean())

    horizontal = sorted((line for line in lines if not line.vertical), key=lambda l: (centre(l)[1], centre(l)[0]))
    vertical = sorted((line for line in lines if line.vertical), key=lambda l: (-centre(l)[0], centre(l)[1]))
    return horizontal + vertical
