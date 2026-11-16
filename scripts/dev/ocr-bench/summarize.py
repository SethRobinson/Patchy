"""Prints a compact table from a benchmark result JSON.

    python -I summarize.py <result.json> [det_side]
"""
import json
import sys
from collections import defaultdict

data = json.load(open(sys.argv[1], encoding="utf-8"))
only_det = int(sys.argv[2]) if len(sys.argv) > 2 else None
variants = ["clean300", "scan300", "clean150", "scan150"]
table = defaultdict(dict)
for row in data["rows"]:
    if only_det is not None and row.get("det_side") not in (None, only_det):
        continue
    key = (row["page"], row.get("recognizer", "")[:24], row.get("det_side", 0))
    table[key][row["variant"]] = row
print(f'{"page":16} {"recognizer":24} {"det":>5} ' + " ".join(f'{v:>22}' for v in variants))
for key in sorted(table):
    cells = []
    for v in variants:
        row = table[key].get(v)
        cells.append(f'{row["cer"] * 100:6.2f}% {row["lines"]:3}/{row["truth_lines"]:<3}{row["wall_ms"]:6}ms' if row else " " * 22)
    print(f"{key[0]:16} {key[1]:24} {key[2]:5} " + " ".join(cells))
if "peak_memory_mb" in data:
    print("peak memory MB:", data["peak_memory_mb"])
