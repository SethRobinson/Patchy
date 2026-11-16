"""What an engine lost on a Japanese page: the truth characters that were dropped or replaced,
by kind, from a minimum edit distance alignment (the benchmark text repeats, so a
longest-common-block matcher misaligns it).

    python -I error_kinds.py <pages-dir> <page-name> <read-text-file> [...more read files]
"""
import json
import sys
from collections import Counter
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_ppocr  # noqa: E402

SMALL_KANA = set("っゃゅょぁぃぅぇぉッャュョァィゥェォ")


def kind(ch: str) -> str:
    if ch in "、。":
        return "comma, full stop"
    if ch == "ー":
        return "long vowel mark"
    if ch in SMALL_KANA:
        return "small kana"
    if ch in "「」！？・":
        return "brackets, ! and ?"
    if ch.isascii():
        return "Latin and digits"
    return "kana and kanji"


def lost_characters(truth: str, read: str) -> tuple[Counter, int]:
    rows, cols = len(truth) + 1, len(read) + 1
    table = np.zeros((rows, cols), dtype=np.int32)
    table[:, 0] = np.arange(rows)
    table[0, :] = np.arange(cols)
    target = np.frombuffer(read.encode("utf-32-le"), dtype=np.uint32)
    offsets = np.arange(cols, dtype=np.int32)
    for i in range(1, rows):
        cost = (target != ord(truth[i - 1])).astype(np.int32)
        row = np.empty(cols, dtype=np.int32)
        row[0] = i
        np.minimum(table[i - 1, 1:] + 1, table[i - 1, :-1] + cost, out=row[1:])
        table[i] = np.minimum.accumulate(row - offsets) + offsets
    lost, added = Counter(), 0
    i, j = rows - 1, cols - 1
    while i > 0 or j > 0:
        if i > 0 and j > 0 and table[i, j] == table[i - 1, j - 1] + (truth[i - 1] != read[j - 1]):
            if truth[i - 1] != read[j - 1]:
                lost[kind(truth[i - 1])] += 1
                added += 1
            i, j = i - 1, j - 1
        elif i > 0 and table[i, j] == table[i - 1, j] + 1:
            lost[kind(truth[i - 1])] += 1
            i -= 1
        else:
            added += 1
            j -= 1
    return lost, added


pages = {p["name"]: p for p in json.loads((Path(sys.argv[1]) / "pages.json").read_text(encoding="utf-8"))["pages"]}
truth = bench_ppocr.squeeze("".join(pages[sys.argv[2]]["lines"]))
totals = Counter(kind(ch) for ch in truth)
for name in sys.argv[3:]:
    read = bench_ppocr.squeeze(Path(name).read_text(encoding="utf-8"))
    lost, added = lost_characters(truth, read)
    print(Path(name).name)
    for label, total in totals.most_common():
        print(f"   {label:18} lost {lost[label]:4} of {total:4}  ({100.0 * lost[label] / total:5.1f}%)")
    print(f"   characters added or put in place of others: {added}")
