#!/usr/bin/env python3
"""Static file server for the wasm app build (stdlib only, python 3.9+).

Mirror of serve.mjs for machines without node (studiomac): sends the COOP/COEP
headers cross-origin isolation needs, the wasm MIME types, and the .br/.gz
precompressed-variant negotiation. Additionally accepts POST /memtest-sample
from scripts/wasm/stress-harness.html and appends one JSON line per sample to
the results directory, so a memory run needs no channel out of the browser
beyond same-origin fetch.

Usage: python3 serve.py <root-dir> [port] [--results-dir <dir>]
Binds 127.0.0.1 only. Exits non-zero if the port is already taken.
"""

import http.server
import json
import re
import sys
import threading
from pathlib import Path

TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript",
    ".mjs": "text/javascript",
    ".wasm": "application/wasm",
    ".data": "application/octet-stream",
    ".json": "application/json",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".ico": "image/x-icon",
}

RUN_ID_RE = re.compile(r"^[A-Za-z0-9_.-]{1,64}$")


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    root = Path(".")
    results_dir = None
    telemetry_lock = threading.Lock()

    def do_GET(self):  # noqa: N802 (BaseHTTPRequestHandler contract)
        try:
            path = self.path.split("?", 1)[0].split("#", 1)[0]
            if path.endswith("/"):
                path += "patchy.html"
            file = (self.root / path.lstrip("/")).resolve()
            if self.root != file and self.root not in file.parents:
                raise FileNotFoundError(path)
            body = None
            content_encoding = None
            accept = self.headers.get("Accept-Encoding", "")
            for token, suffix in (("br", ".br"), ("gzip", ".gz")):
                variant = file.with_name(file.name + suffix)
                if body is None and token in accept and variant.is_file():
                    body = variant.read_bytes()
                    content_encoding = token
            if body is None:
                body = file.read_bytes()
            self.send_response(200)
            self.send_header(
                "Content-Type", TYPES.get(file.suffix.lower(), "application/octet-stream")
            )
            if content_encoding:
                self.send_header("Content-Encoding", content_encoding)
                self.send_header("Vary", "Accept-Encoding")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            # Cross-origin isolation, matching the deployed .htaccess: the
            # multithreaded app needs SharedArrayBuffer, which browsers only
            # enable when these two headers are present.
            self.send_header("Cross-Origin-Opener-Policy", "same-origin")
            self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
            self.end_headers()
            self.wfile.write(body)
        except (OSError, ValueError):
            self.send_error(404, "not found")

    def do_POST(self):  # noqa: N802
        # /memtest-sample, not /telemetry: "telemetry" URLs sit on standard
        # content-blocker lists (EasyPrivacy and friends) and the POSTs die as
        # ERR_BLOCKED_BY_CLIENT, silently gutting open-driver kill detection.
        if self.path.split("?", 1)[0] != "/memtest-sample" or self.results_dir is None:
            self.send_error(404, "not found")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(min(length, 1 << 20))
            sample = json.loads(raw)
            run_id = str(sample.get("runId", "default"))
            if not RUN_ID_RE.match(run_id):
                run_id = "default"
            line = json.dumps(sample, separators=(",", ":"))
            with self.telemetry_lock:
                self.results_dir.mkdir(parents=True, exist_ok=True)
                out = self.results_dir / f"telemetry-{run_id}.jsonl"
                with out.open("a", encoding="utf-8") as handle:
                    handle.write(line + "\n")
            self.send_response(204)
            self.send_header("Content-Length", "0")
            self.end_headers()
        except (OSError, ValueError):
            self.send_error(400, "bad telemetry")

    def log_message(self, fmt, *args):
        # Page GETs are the open-driver kill signal (a memory-killed tab
        # auto-reloads), so log html requests only; asset noise is dropped.
        if ".html" in self.path or self.path == "/":
            sys.stderr.write("%s - %s\n" % (self.log_date_time_string(), fmt % args))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    root = Path(args[0] if args else ".").resolve()
    port = int(args[1]) if len(args) > 1 else 8973
    results = None
    if "--results-dir" in sys.argv:
        results = Path(sys.argv[sys.argv.index("--results-dir") + 1]).resolve()
    if not root.is_dir():
        sys.exit(f"serve.py: root directory not found: {root}")
    Handler.root = root
    Handler.results_dir = results
    try:
        server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    except OSError as error:
        sys.exit(f"serve.py: cannot bind 127.0.0.1:{port} ({error})")
    print(f"serving {root} at http://localhost:{port}/patchy.html", flush=True)
    if results:
        print(f"telemetry to {results}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
