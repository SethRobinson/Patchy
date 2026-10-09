"""Extract native frame geometry and composed line spans from COM area captures.

Run capture-area-text.ps1 first, then pass its output directory. This reads Txt2,
not the embedded composite. The companion BMPs are Photoshop's independent renders.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "txt2"))
import txt2tool


def lines(node):
    if isinstance(node, dict):
        if node.get("99") == "/L":
            yield node
        else:
            for value in node.values():
                yield from lines(value)
    elif isinstance(node, list):
        for value in node:
            yield from lines(value)


def capture(path):
    data = txt2tool.read_psd(path)
    _, location = txt2tool.find_txt2(data)
    if location is None:
        raise ValueError(f"No document text engine in {path.name}")
    start, length = location[:2]
    root = txt2tool.to_py(txt2tool.parse_body(data[start:start + length]))
    obj = root["1"]["1"][0]
    frame_index = int(obj["1"]["0"][0]["0"])
    frame = root["0"]["8"]["0"][frame_index]["0"]
    story = obj["0"]["0"]
    assert story.startswith("S:")
    result = {
        "marker": [float(v) for v in frame["2"]["6"]],
        "cubics": [float(v) for v in frame["1"]["0"]],
        "frameTransform": [float(v) for v in frame["2"].get("2", [1, 0, 0, 1, 0, 0])],
        "storyLengthIncludingTerminator": len(story[2:]),
        "lines": [],
    }
    for line in lines(obj["1"].get("2")):
        spans = []
        for span in line.get("6", []):
            if span.get("99") == "/S":
                spans.append({"x": float(span.get("0", {}).get("0", [0])[0]),
                              "start": int(span["16"]), "length": int(span["15"]["0"])})
        result["lines"].append({"baseline": float(line["10"]), "spans": spans})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    records = {path.stem: capture(path) for path in sorted(args.directory.glob("*.psd"))}
    args.output.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
