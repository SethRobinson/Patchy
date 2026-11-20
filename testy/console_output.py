"""Testy progress output for terminals and redirected Windows console streams."""
import sys


def log(message: str) -> None:
    line = f"[testy] {message}"
    try:
        print(line, flush=True)
    except UnicodeEncodeError:
        encoding = getattr(sys.stdout, "encoding", None) or "ascii"
        print(line.encode(encoding, errors="backslashreplace").decode(encoding), flush=True)
