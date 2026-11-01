#!/usr/bin/env python3
"""Query every plug-in of a GIMP 3 install through patchy-gimp-host and summarize
(docs/gimp-plugins.md, development tools).

    python scripts/dev/gimp/query_sweep.py <patchy-gimp-host> <gimp install folder> [--json out.json]

Runs the helper's --query-plugin mode once per plug-in (native programs directly, .py
through GIMP's bundled Python, .scm through its Script-Fu interpreter), with PATH set
to GIMP's bin folder and a scratch GIMP3_DIRECTORY, and prints one line per plug-in:
how many procedures it registered and how Patchy classifies them. A plug-in that fails
to register prints its error. Nothing is written into the GIMP install.
"""

import json
import os
import struct
import subprocess
import sys
import tempfile


def frames(data):
    at = 0
    while at + 8 <= len(data):
        kind, n = struct.unpack_from("<II", data, at)
        yield kind, data[at + 8:at + 8 + n]
        at += 8 + n


def query(helper, program, script, env):
    args = [helper, "--query-plugin", program] + ([script] if script else [])
    try:
        out = subprocess.run(args, capture_output=True, timeout=120, env=env).stdout
    except subprocess.TimeoutExpired:
        return None, [], "timed out"
    logs = []
    result = None
    for kind, payload in frames(out):
        n = struct.unpack_from("<I", payload, 0)[0]
        text = payload[4:4 + n].decode("utf-8", "replace")
        if kind == 101:
            logs.append(text)
        elif kind == 107:
            result = json.loads(text)
    return result, logs, None


def main():
    helper, install = sys.argv[1], sys.argv[2]
    out_json = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
    bin_dir = os.path.join(install, "bin")
    plugins = os.path.join(install, "lib", "gimp", "3.0", "plug-ins")
    env = dict(os.environ)
    env["PATH"] = bin_dir + os.pathsep + env.get("PATH", "")
    env["GIMP3_DIRECTORY"] = tempfile.mkdtemp(prefix="patchy-gimp-sweep-")
    env.pop("PYTHONHOME", None)
    env.pop("PYTHONPATH", None)
    python = os.path.join(bin_dir, "pythonw.exe" if os.name == "nt" else "python3")
    scriptfu = os.path.join(bin_dir, "gimp-script-fu-interpreter-3.0" + (".exe" if os.name == "nt" else ""))
    summary = {}
    unsupported_calls = {}
    for name in sorted(os.listdir(plugins)):
        folder = os.path.join(plugins, name)
        exe = os.path.join(folder, name + (".exe" if os.name == "nt" else ""))
        py = os.path.join(folder, name + ".py")
        scm = os.path.join(folder, name + ".scm")
        if os.path.isfile(exe):
            result, logs, error = query(helper, exe, None, env)
        elif os.path.isfile(py):
            result, logs, error = query(helper, python, py, env)
        elif os.path.isfile(scm):
            result, logs, error = query(helper, scriptfu, scm, env)
        else:
            print(f"{name:32s} (no plug-in file)")
            continue
        for line in logs:
            if line.startswith("unsupported procedure: "):
                unsupported_calls.setdefault(line[23:], set()).add(name)
        if error or result is None:
            print(f"{name:32s} FAILED {error or 'no result'}")
            summary[name] = {"error": error or "no result"}
            continue
        plugin = result["plugins"][0]
        procs = plugin["procedures"]
        uses = {}
        for p in procs:
            key = p["use"] + ("" if not p["reason"] else ":" + p["reason"])
            uses[key] = uses.get(key, 0) + 1
        status = "ok" if plugin["ok"] else "FAILED " + plugin["error"][:200]
        print(f"{name:32s} {status:8s} {len(procs):3d} procedures {uses}")
        summary[name] = plugin
    print()
    for call, names in sorted(unsupported_calls.items()):
        print(f"unsupported at query time: {call} ({', '.join(sorted(names))})")
    if out_json:
        with open(out_json, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=1)


if __name__ == "__main__":
    main()
