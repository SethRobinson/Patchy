"""Scores the Windows engine's output (the RESULT lines bench_windows.js printed).

    python -I score_windows.py <pages-dir> <script-output.txt> <out-json>
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_ppocr  # noqa: E402


def main() -> int:
    pages_dir = Path(sys.argv[1])
    described = {page["name"]: page for page in
                 json.loads((pages_dir / "pages.json").read_text(encoding="utf-8"))["pages"]}
    rows = []
    for raw in Path(sys.argv[2]).read_text(encoding="utf-8", errors="replace").splitlines():
        at = raw.find("RESULT {")
        if at < 0:
            continue
        result = json.loads(raw[at + 7:])
        page = described[result["page"]]
        truth = bench_ppocr.squeeze("".join(page["lines"]))
        text = bench_ppocr.squeeze(result["text"])
        distance = bench_ppocr.edit_distance(truth, text)
        row = {"page": result["page"], "language": page["language"], "variant": result["variant"],
               "truth_chars": len(truth), "read_chars": len(text), "errors": distance,
               "cer": round(distance / max(1, len(truth)), 5), "truth_lines": len(page["lines"]),
               "lines": result["lines"], "vertical_lines": result["vertical"], "wall_ms": result["ms"]}
        rows.append(row)
        print(f'{row["page"]:16} {row["variant"]:9} CER {row["cer"] * 100:6.2f}%  lines {row["lines"]:3}/'
              f'{row["truth_lines"]:3} (vertical {row["vertical_lines"]})  {row["wall_ms"]:6} ms')
        dump = Path(sys.argv[3]).parent / "dump-windows"  # beside the result, where rescore.py looks
        dump.mkdir(parents=True, exist_ok=True)
        (dump / f'{row["page"]}.{row["variant"]}.txt').write_text(result["text"], encoding="utf-8")
    Path(sys.argv[3]).write_text(json.dumps({"rows": rows}, ensure_ascii=False, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
