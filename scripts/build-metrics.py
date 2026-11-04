#!/usr/bin/env python3
"""Build timing metrics for the Patchy build and the Raven external project.

Reads what the build already records, so it costs nothing while building:
  - .ninja_log of the Patchy build tree and of Raven's build tree: start and end of every
    command in the last ninja run, giving wall time, summed command time, average
    parallelism, the slowest commands, time per directory and a concurrency timeline
    (low-concurrency stretches are where the build waits on one long command);
  - the patchy_raven ExternalProject steps in the Patchy ninja log: how long download,
    patch, configure (Raven's guest DLL provisioning plus its CMake configure), build and
    install (staging) took;
  - ccache statistics when ccache is installed.
Each run is appended as one JSON line to <build>/build-metrics.jsonl, so --history can
compare runs after a change (more jobs, a different compiler, unity builds...).

  build-metrics.py [build-dir] [--top N] [--history] [--no-record]
build-dir defaults to build/linux-dev.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def last_ninja_run(build_dir: Path) -> list[tuple[int, int, str]]:
    """(start_ms, end_ms, output) of every command in the most recent ninja run."""
    log = build_dir / ".ninja_log"
    if not log.is_file():
        return []
    runs: list[list[tuple[int, int, str]]] = [[]]
    previous_end = -1
    for line in log.read_text(errors="replace").splitlines():
        if line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        start, end, output = int(parts[0]), int(parts[1]), parts[3]
        # Ninja appends each command when it finishes, so end times rise within a run, and
        # it restarts its clock at 0 for every run: an end earlier than the previous entry's
        # begins a new run. (After ninja recompacts its log the order is lost; the next run
        # starts a clean section again.)
        if end < previous_end and runs[-1]:
            runs.append([])
        previous_end = end
        runs[-1].append((start, end, output))
    # Commands that produce several outputs appear once per output: keep one.
    seen: set[tuple[int, int]] = set()
    unique = []
    for start, end, output in runs[-1]:
        if (start, end) in seen:
            continue
        seen.add((start, end))
        unique.append((start, end, output))
    return unique


def group_of(output: str) -> str:
    """A directory-level bucket for an output path (object files are named after sources)."""
    path = output.replace("\\", "/")
    m = re.search(r"\.dir/(.+)\.(o|obj)$", path)
    if m:
        source = m.group(1)
        parts = source.split("/")
        return "/".join(parts[:2]) if len(parts) > 2 else parts[0]
    if "-stamp/" in path:
        return "(external projects)"
    if path.endswith((".a", ".so")) or "/" not in path:
        return "(link/other)"
    return path.split("/")[0]


def ninja_metrics(name: str, build_dir: Path, top: int) -> dict | None:
    entries = last_ninja_run(build_dir)
    if not entries:
        return None
    first = min(s for s, _, _ in entries)
    last = max(e for _, e, _ in entries)
    wall = max(last - first, 1)
    busy = sum(e - s for s, e, _ in entries)
    slowest = sorted(entries, key=lambda x: x[1] - x[0], reverse=True)[:top]
    groups: dict[str, int] = defaultdict(int)
    for s, e, out in entries:
        groups[group_of(out)] += e - s
    # Concurrency in 20 equal slices of the wall time.
    slices = 20
    width = wall / slices
    timeline = []
    for i in range(slices):
        lo, hi = first + i * width, first + (i + 1) * width
        overlap = sum(max(0, min(e, hi) - max(s, lo)) for s, e, _ in entries)
        timeline.append(round(overlap / width, 1))
    return {
        "name": name,
        "build_dir": str(build_dir),
        "commands": len(entries),
        "wall_s": round(wall / 1000, 1),
        "command_s": round(busy / 1000, 1),
        "parallelism": round(busy / wall, 2),
        "slowest": [{"s": round((e - s) / 1000, 1), "output": out} for s, e, out in slowest],
        "groups": sorted(({"group": g, "s": round(v / 1000, 1)} for g, v in groups.items()),
                         key=lambda x: x["s"], reverse=True)[:top],
        "timeline": timeline,
    }


def cache_value(build_dir: Path, key: str) -> str | None:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith(key + ":"):
            return line.split("=", 1)[1]
    return None


STEP_RE = re.compile(r"patchy_raven-stamp/patchy_raven-([a-z]+)$")


def raven_steps(patchy_entries: list[tuple[int, int, str]]) -> dict | None:
    """Exact duration of each patchy_raven step that ran in the last Patchy ninja run (the
    outer ninja records every ExternalProject step as a command producing its stamp)."""
    steps = {}
    for start, end, output in sorted(patchy_entries):
        m = STEP_RE.search(output.replace("\\", "/"))
        if m:
            steps[m.group(1)] = round((end - start) / 1000, 1)
    return {"steps_s": steps} if steps else None


def ccache_stats() -> dict | None:
    if not shutil.which("ccache"):
        return None
    try:
        out = subprocess.run(["ccache", "--print-stats"], capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    stats = dict(line.split("\t", 1) for line in out.splitlines() if "\t" in line)
    hits = int(stats.get("direct_cache_hit", 0)) + int(stats.get("preprocessed_cache_hit", 0))
    misses = int(stats.get("cache_miss", 0))
    total = hits + misses
    return {"hits": hits, "misses": misses, "hit_rate": round(hits / total, 3) if total else None}


def spark(values: list[float], ceiling: float) -> str:
    bars = " ▁▂▃▄▅▆▇█"
    return "".join(bars[min(8, int(round(8 * v / ceiling)))] if ceiling else " " for v in values)


def print_ninja(m: dict, cpus: int) -> None:
    print(f"== {m['name']}  ({m['build_dir']})")
    print(f"   {m['commands']} commands, wall {m['wall_s']} s, command time {m['command_s']} s, "
          f"parallelism {m['parallelism']} of {cpus} CPUs ({100 * m['parallelism'] / cpus:.0f}%)")
    print(f"   concurrency over time |{spark(m['timeline'], cpus)}|  (full bar = {cpus})")
    print("   slowest commands:")
    for item in m["slowest"]:
        print(f"     {item['s']:8.1f} s  {item['output']}")
    print("   time per directory:")
    for item in m["groups"]:
        print(f"     {item['s']:8.1f} s  {item['group']}")
    print()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build_dir", nargs="?", default=str(ROOT / "build" / "linux-dev"))
    parser.add_argument("--top", type=int, default=10)
    parser.add_argument("--history", action="store_true", help="compare the recorded runs")
    parser.add_argument("--no-record", action="store_true", help="do not append this run to the history")
    args = parser.parse_args()

    build_dir = Path(args.build_dir).resolve()
    history_file = build_dir / "build-metrics.jsonl"
    if args.history:
        if not history_file.is_file():
            print(f"no history yet in {history_file}")
            return 0
        print(f"{'when':19}  {'tree':6}  {'wall s':>7}  {'cmd s':>8}  {'par':>5}  raven steps (s)")
        for line in history_file.read_text().splitlines():
            run = json.loads(line)
            raven = run.get("raven_steps") or {}
            steps = " ".join(f"{k}={v}" for k, v in raven.get("steps_s", {}).items())
            for m in run.get("ninja", []):
                print(f"{run['when']:19}  {m['name'][:6]:6}  {m['wall_s']:7}  {m['command_s']:8}  "
                      f"{m['parallelism']:5}  {steps if m['name'] == 'Raven' else ''}")
        return 0

    cpus = os.cpu_count() or 1
    run: dict = {"when": time.strftime("%Y-%m-%d %H:%M:%S"), "cpus": cpus, "ninja": []}
    patchy = ninja_metrics("Patchy", build_dir, args.top)
    if patchy:
        run["ninja"].append(patchy)
        print_ninja(patchy, cpus)
    else:
        print(f"no .ninja_log in {build_dir}")

    work = cache_value(build_dir, "PATCHY_RAVEN_WORK_DIR")
    if work:
        work_dir = Path(work)
        raven = ninja_metrics("Raven", work_dir / "build", args.top)
        if raven:
            run["ninja"].append(raven)
            print_ninja(raven, cpus)
    steps = raven_steps(last_ninja_run(build_dir))
    if steps:
        run["raven_steps"] = steps
        print("== Raven external project steps that ran (outer ninja log)")
        for step, seconds in steps["steps_s"].items():
            note = {"configure": "  (guest DLL provisioning + Raven's CMake configure)",
                    "build": "  (Raven's own ninja, broken down above)",
                    "install": "  (staging into raven/)"}.get(step, "")
            print(f"     {seconds:8.1f} s  {step}{note}")
        print()

    cc = ccache_stats()
    if cc:
        run["ccache"] = cc
        rate = f"{100 * cc['hit_rate']:.0f}%" if cc["hit_rate"] is not None else "n/a"
        print(f"== ccache: {cc['hits']} hits, {cc['misses']} misses, hit rate {rate}")

    if not args.no_record and run["ninja"]:
        with history_file.open("a") as f:
            f.write(json.dumps(run) + "\n")
        print(f"\nrecorded in {history_file} (compare runs with --history)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
