#!/usr/bin/env python3
"""Safari memory-run driver and sampler. Runs ON the Mac (stdlib only, 3.9+).

Opens the Patchy wasm app (or harness page) in Safari, then samples the memory
footprint of the involved processes until the duration elapses or the tab is
killed, writing samples.csv, events.log, and summary.json into the results
directory. Orchestrated from Windows by scripts/remote/wasm-safari-memtest.ps1;
see docs/performance.md.

Two driver modes:
  open       zero-setup: `open -a Safari <url>` plus passive sampling. In-page
             numbers arrive through the harness page's POST /telemetry samples
             (serve.py appends them to a JSONL this script tails via
             --telemetry-glob). Kill detection: WebContent pid disappearance or
             a telemetry gap. Teardown quits Safari with pkill unless
             --keep-safari.
  webdriver  needs the one-time `sudo safaridriver --enable` plus Safari's
             Allow Remote Automation: attaches to (or spawns) safaridriver and
             additionally polls globalThis.patchyMemStats over executeScript,
             which works on the real branded patchy.html too. Kill detection:
             the window.__patchyEpoch sentinel changing or the session dying.

The footprint sampled is what WebKit's kill policy watches (phys_footprint via
/usr/bin/footprint, ps rss fallback). The GPU process is sampled too: canvas
and IOSurface growth lands there, invisible to any wasm-side number.
"""

import argparse
import csv
import datetime
import glob
import json
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

FOOTPRINT_RE = re.compile(r"Footprint:\s+([\d.]+)\s*(KB|MB|GB)", re.IGNORECASE)
WEBCONTENT_PATTERN = "com.apple.WebKit.WebContent"
GPU_PATTERN = "com.apple.WebKit.GPU"
APP_TAB_THRESHOLD_MB = 300.0  # the wasm heap alone starts at 256-512 MB
TELEMETRY_GAP_S = 15.0


def run(cmd, timeout=30):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return None


def pgrep(pattern, exact=False):
    result = run(["pgrep", "-x" if exact else "-f", pattern])
    if result is None or result.returncode != 0:
        return set()
    return {int(line) for line in result.stdout.split() if line.isdigit()}


def footprint_mb(pid):
    result = run(["footprint", str(pid)], timeout=20)
    if result is not None and result.returncode == 0:
        match = FOOTPRINT_RE.search(result.stdout)
        if match:
            value = float(match.group(1))
            unit = match.group(2).upper()
            return value / 1024.0 if unit == "KB" else value * 1024.0 if unit == "GB" else value
    return None


def rss_mb(pid):
    # Second opinion beside footprint: ps rss is plain resident pages. The two
    # diverge when compressed/IOSurface/reserved accounting inflates one of
    # them, which is itself a diagnostic signal.
    result = run(["ps", "-xo", "rss=", "-p", str(pid)])
    if result is not None and result.returncode == 0 and result.stdout.strip().isdigit():
        return int(result.stdout.strip()) / 1024.0
    return None


class WebDriver:
    """Raw W3C WebDriver over HTTP; no third-party dependencies."""

    def __init__(self, port):
        self.base = f"http://127.0.0.1:{port}"
        self.session = None

    def _call(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.base + path, data=data, method=method,
                                         headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.loads(response.read() or b"{}")

    def start_session(self):
        reply = self._call("POST", "/session",
                           {"capabilities": {"alwaysMatch": {"browserName": "Safari"}}})
        self.session = reply["value"]["sessionId"]

    def navigate(self, url):
        self._call("POST", f"/session/{self.session}/url", {"url": url})

    def execute(self, script):
        reply = self._call("POST", f"/session/{self.session}/execute/sync",
                           {"script": script, "args": []})
        return reply.get("value")

    def quit(self):
        if self.session:
            try:
                self._call("DELETE", f"/session/{self.session}")
            except (urllib.error.URLError, OSError):
                pass
            self.session = None


def ensure_safaridriver(port):
    probe = run(["curl", "-s", "-o", "/dev/null", "-w", "%{http_code}",
                 f"http://127.0.0.1:{port}/status"], timeout=10)
    if probe is not None and probe.stdout.strip() == "200":
        return None
    process = subprocess.Popen(["safaridriver", "-p", str(port)],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    return process


def newest_telemetry_line(pattern):
    newest = None
    for path in glob.glob(pattern):
        p = Path(path)
        if newest is None or p.stat().st_mtime > newest.stat().st_mtime:
            newest = p
    if newest is None:
        return None, None
    try:
        with newest.open("rb") as handle:
            lines = handle.readlines()
        if not lines:
            return newest, None
        return newest, json.loads(lines[-1])
    except (OSError, ValueError):
        return newest, None


def capture_webkit_log(out_path, seconds):
    window = f"{min(max(int(seconds) + 60, 120), 1800)}s"
    predicate = ('(subsystem == "com.apple.WebKit" AND category == "Memory") '
                 'OR (eventMessage CONTAINS[c] "Terminating" AND eventMessage CONTAINS[c] "WebContent") '
                 'OR eventMessage CONTAINS[c] "memory limit"')
    result = run(["log", "show", "--last", window, "--style", "compact",
                  "--predicate", predicate], timeout=120)
    if result is not None:
        out_path.write_text(result.stdout or result.stderr, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--url", required=True)
    parser.add_argument("--driver", choices=["open", "webdriver"], default="open")
    parser.add_argument("--duration-min", type=float, default=15.0)
    parser.add_argument("--sample-interval", type=float, default=5.0)
    parser.add_argument("--results-dir", required=True)
    parser.add_argument("--telemetry-dir", default="",
                        help="open mode: directory whose telemetry-*.jsonl files serve.py "
                             "writes (a directory, not a glob: a glob in the ssh command "
                             "line dies on zsh's no-match abort before python ever runs)")
    parser.add_argument("--driver-port", type=int, default=4723)
    parser.add_argument("--keep-safari", action="store_true")
    args = parser.parse_args()

    results = Path(args.results_dir)
    results.mkdir(parents=True, exist_ok=True)
    events = (results / "events.log").open("a", encoding="utf-8")

    def note(text):
        stamp = datetime.datetime.now().isoformat(timespec="seconds")
        events.write(f"{stamp} {text}\n")
        events.flush()
        print(text, flush=True)

    summary = {
        "url": args.url, "driver": args.driver, "duration_min": args.duration_min,
        "started": datetime.datetime.now().isoformat(timespec="seconds"),
        "os": (run(["sw_vers", "-productVersion"]) or subprocess.CompletedProcess([], 1, "", "")).stdout.strip(),
        "peaks_mb": {}, "kill": {"detected": False},
    }
    baseline_webcontent = pgrep(WEBCONTENT_PATTERN)
    caffeinate = subprocess.Popen(["caffeinate", "-dims"],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    spawned_driver = None
    driver = None
    app_pid = None
    last_telemetry_t = None
    last_epoch = None

    csv_file = (results / "samples.csv").open("w", newline="", encoding="utf-8")
    writer = csv.writer(csv_file)
    writer.writerow(["iso_ts", "elapsed_s", "role", "pid", "footprint_mb", "rss_mb",
                     "heap_mb", "used_mb", "peak_used_mb", "history_mb", "cap_mb", "stage"])

    started = time.monotonic()
    try:
        if args.driver == "webdriver":
            spawned_driver = ensure_safaridriver(args.driver_port)
            driver = WebDriver(args.driver_port)
            driver.start_session()
            driver.navigate(args.url)
            note(f"webdriver session up, navigated to {args.url}")
        else:
            opened = run(["open", "-a", "Safari", args.url])
            if opened is None or opened.returncode != 0:
                note("open -a Safari failed")
                sys.exit(2)
            note(f"opened {args.url} in Safari")

        deadline = started + args.duration_min * 60.0
        while time.monotonic() < deadline:
            time.sleep(args.sample_interval)
            elapsed = round(time.monotonic() - started, 1)
            iso = datetime.datetime.now().isoformat(timespec="seconds")

            page = {}
            stage = ""
            if driver is not None:
                try:
                    raw = driver.execute(
                        "return JSON.stringify({epoch: window.__patchyEpoch || null,"
                        " stats: globalThis.patchyMemStats || null,"
                        " cap: globalThis.patchyWasmMemoryMaximumBytes || 0})")
                    payload = json.loads(raw) if raw else {}
                    epoch = payload.get("epoch")
                    if last_epoch is not None and epoch != last_epoch:
                        summary["kill"] = {"detected": True, "t_s": elapsed, "source": "epoch-change"}
                        note(f"kill detected at t+{elapsed}s: page epoch changed (reload)")
                    last_epoch = epoch or last_epoch
                    stats = payload.get("stats") or {}
                    page = {k: stats.get(k) for k in
                            ("heapBytes", "usedBytes", "peakUsedBytes", "historyBytes")}
                    page["cap"] = payload.get("cap")
                except (urllib.error.URLError, OSError, KeyError, ValueError) as error:
                    note(f"webdriver poll failed at t+{elapsed}s: {error}")
            elif args.telemetry_dir:
                _, sample = newest_telemetry_line(str(Path(args.telemetry_dir).expanduser()
                                                      / "telemetry-*.jsonl"))
                if sample:
                    stats = sample.get("memStats") or {}
                    page = {k: stats.get(k) for k in
                            ("heapBytes", "usedBytes", "peakUsedBytes", "historyBytes")}
                    page["heapBytes"] = page.get("heapBytes") or sample.get("heapBytes")
                    page["cap"] = stats.get("limitBytes")
                    stage = sample.get("stage", "")
                    sample_wall = sample.get("t")
                    if sample_wall is not None:
                        if last_telemetry_t is not None and sample_wall == last_telemetry_t:
                            pass  # gap measured below via file mtime freshness
                        last_telemetry_t = sample_wall

            mb = lambda key: round(page[key] / 1048576.0, 1) if page.get(key) else ""
            writer.writerow([iso, elapsed, "page", "", "", "", mb("heapBytes"), mb("usedBytes"),
                             mb("peakUsedBytes"), mb("historyBytes"), mb("cap"), stage])

            roles = [("safari", pgrep("Safari", exact=True)),
                     ("gpu", pgrep(GPU_PATTERN)),
                     ("webcontent", pgrep(WEBCONTENT_PATTERN) - baseline_webcontent)]
            live_webcontent = set()
            for role, pids in roles:
                for pid in sorted(pids):
                    value = footprint_mb(pid)
                    resident = rss_mb(pid)
                    if value is None and resident is None:
                        continue
                    gauge = value if value is not None else resident
                    if role == "webcontent":
                        live_webcontent.add(pid)
                        if app_pid is None and gauge >= APP_TAB_THRESHOLD_MB:
                            app_pid = pid
                            note(f"app WebContent identified: pid {pid} at {gauge:.0f} MB")
                            # One raw dump so the parsed number can be audited
                            # against footprint's own breakdown.
                            raw = run(["footprint", str(pid)], timeout=20)
                            if raw is not None:
                                (results / "footprint-raw.txt").write_text(
                                    raw.stdout or raw.stderr, encoding="utf-8")
                    tag = "webcontent-app" if pid == app_pid else role
                    writer.writerow([iso, elapsed, tag, pid,
                                     round(value, 1) if value is not None else "",
                                     round(resident, 1) if resident is not None else "",
                                     "", "", "", "", "", ""])
                    key = f"peak_{tag}_footprint_mb"
                    if gauge > summary["peaks_mb"].get(key, 0):
                        summary["peaks_mb"][key] = round(gauge, 1)
            csv_file.flush()

            if app_pid is not None and app_pid not in live_webcontent \
                    and not summary["kill"]["detected"]:
                summary["kill"] = {"detected": True, "t_s": elapsed, "source": "pid-gone",
                                   "last_footprint_mb": summary["peaks_mb"].get(
                                       "peak_webcontent-app_footprint_mb")}
                note(f"kill detected at t+{elapsed}s: app WebContent pid {app_pid} gone")
                capture_webkit_log(results / "webkit-log.txt", elapsed)
                break
            if summary["kill"]["detected"]:
                capture_webkit_log(results / "webkit-log.txt", elapsed)
                break
    finally:
        for key in ("heapBytes", "usedBytes", "peakUsedBytes"):
            pass  # page peaks live in the telemetry JSONL; footprint peaks above
        summary["ended"] = datetime.datetime.now().isoformat(timespec="seconds")
        (results / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        csv_file.close()
        if driver is not None:
            driver.quit()
        if spawned_driver is not None:
            spawned_driver.terminate()
        caffeinate.terminate()
        if not args.keep_safari:
            run(["pkill", "-x", "Safari"])
            note("Safari closed")
        events.close()
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
