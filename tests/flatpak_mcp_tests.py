"""Verify MCP attachment across separate Flatpak invocations, without desktop UI.

Run on Linux after installing the current user bundle: python tests/flatpak_mcp_tests.py
Requires the same Python MCP dependency as mcp_client_tests.py.
"""
import asyncio
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[1]
APP_ID = "com.rtsoft.patchy"


async def test():
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    if not runtime:
        raise RuntimeError("Run this test in the desktop user's session with XDG_RUNTIME_DIR set.")
    running = subprocess.run(["flatpak", "ps", "--columns=application"],
                             capture_output=True, text=True, check=True)
    if APP_ID in running.stdout.splitlines():
        raise RuntimeError("Close the existing Patchy Flatpak before this test.")
    out = ROOT / "test-artifacts" / ("flatpak-separate-" + str(time.time_ns()))
    out.mkdir(parents=True)
    settings = out / "settings"
    ini = settings / "Patchy" / "Patchy.ini"
    ini.parent.mkdir(parents=True)
    ini.write_text("[updates]\ncheckOnStartup=false\n", encoding="utf-8")
    sandbox = ["flatpak", "run", "--user", "--command=env", APP_ID]
    base = [*sandbox, "-u", "PATCHY_MCP_ENDPOINT", "QT_QPA_PLATFORM=offscreen",
            "PATCHY_NO_SOUND=1", "PATCHY_NO_SINGLE_INSTANCE=1",
            "PATCHY_SETTINGS_DIR=" + str(settings)]
    fixture = out / "owned-document.psd"
    script = out / "create-fixture.js"
    script.write_text("var d=app.newDocument(16,16); d.addLayer('Seed').fill('#ffffff');"
                      f"if(!d.saveAs({json.dumps(str(fixture))})) throw new Error('save failed');",
                      encoding="utf-8")
    # The default endpoint is deliberately not overridden. Record any preexisting
    # socket so cleanup can never delete one this test did not create.
    name = "PatchyMcp-" + hashlib.sha256((str(Path.home()) + "\n/app/bin").encode()).hexdigest()[:32]
    endpoint = Path(runtime) / "app" / APP_ID / name
    if endpoint.exists():
        raise RuntimeError(f"A preexisting attachment endpoint needs inspection: {endpoint}")
    owned_inode = None
    with (out / "stderr.log").open("w", encoding="utf-8") as log:
        subprocess.run([*base, "/app/bin/patchy", "--headless", "--run-script", str(script)],
                       cwd=ROOT, stdout=log, stderr=log, check=True, timeout=30)
        assert fixture.is_file()
        app = subprocess.Popen([*base, "/app/bin/patchy", str(fixture)], cwd=ROOT,
                               stdout=log, stderr=log, start_new_session=True)
        # The Python MCP SDK filters inherited environment variables. Flatpak's
        # host launcher must keep the desktop runtime directory, otherwise its
        # per-app bind mount can come from the GLib cache-directory fallback.
        params = StdioServerParameters(command=base[0],
                                       args=[*base[1:], "/app/bin/patchy-mcp", "--attach"], cwd=str(ROOT),
                                       env={"XDG_RUNTIME_DIR": runtime})
        try:
            async with stdio_client(params, errlog=log) as (read, write):
                async with ClientSession(read, write) as client:
                    await client.initialize()
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        if owned_inode is None and endpoint.exists():
                            owned_inode = endpoint.stat().st_ino
                        info = await client.call_tool("get_info", {})
                        if not info.isError:
                            break
                        assert app.poll() is None
                        await asyncio.sleep(.1)
                    else:
                        raise AssertionError("Separate Flatpak sandboxes cannot attach using the default endpoint")
                    state = (await client.call_tool("get_state", {})).structuredContent
                    assert len(state["documents"]) == 1 and state["documents"][0]["path"] == str(fixture)
                    owned_inode = endpoint.stat().st_ino
                    edit = await client.call_tool("execute_script", {
                        "code": "app.activeDocument.addLayer('Sandbox test').fill('#ff0000');",
                        "expectedState": state["stateToken"]})
                    assert not edit.isError, edit
                    preview = await client.call_tool("get_preview", {})
                    assert not preview.isError and any(item.type == "image" for item in preview.content)
            async with stdio_client(params, errlog=log) as (read, write):
                async with ClientSession(read, write) as client:
                    await client.initialize()
                    state = (await client.call_tool("get_state", {})).structuredContent
                    assert len(state["documents"]) == 1 and state["documents"][0]["modified"]
            print("[PASS] Separate Flatpak sandboxes: default attachment, guarded edit, preview, unsaved reconnect")
        finally:
            if app.poll() is None:
                os.killpg(app.pid, signal.SIGTERM)
                try:
                    app.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    os.killpg(app.pid, signal.SIGKILL)
                    app.wait(timeout=10)
            if owned_inode is not None and endpoint.exists() and endpoint.stat().st_ino == owned_inode:
                endpoint.unlink()


if __name__ == "__main__":
    asyncio.run(test())
