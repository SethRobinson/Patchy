"""Serve the Testy dashboard (run index + all past reports) without running a benchmark.

  C:\\Users\\Seth\\miniconda3\\python.exe testy\\serve.py [port]

testy.py starts the same server itself during runs; this exists so the run index at
http://127.0.0.1:8901/ stays browsable between runs. Binding is exclusive, so if a
benchmark's own server already holds the port this simply picks the next one.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from testy import start_server


def main() -> int:
    import time

    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8901
    _server, bound = start_server(port)
    print(f"[testy] dashboard: http://127.0.0.1:{bound}/  (Ctrl+C to stop)", flush=True)
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
