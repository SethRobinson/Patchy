"""Verify MCP attachment across separate AppImage launches, without desktop UI.

Run on Linux with FUSE (docs/appimage.md, "Testing"):
    python tests/appimage_mcp_tests.py build/package/Patchy-<version>-x86_64.AppImage
Each launch mounts the AppImage at its own /tmp/.mount_* directory, which is what the
attach endpoint must not depend on. An extracted run (APPIMAGE_EXTRACT_AND_RUN) reuses one
directory and would hide that bug, so it is refused. Requires the same Python MCP
dependency as mcp_client_tests.py.
"""
import asyncio
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[1]


async def test(appimage: Path):
    if os.environ.get("APPIMAGE_EXTRACT_AND_RUN"):
        raise RuntimeError("Unset APPIMAGE_EXTRACT_AND_RUN: this test needs separate FUSE mounts.")
    out = ROOT / "test-artifacts" / ("appimage-separate-" + str(time.time_ns()))
    out.mkdir(parents=True)
    settings = out / "settings"
    ini = settings / "RTsoft" / "Patchy" / "Patchy.ini"
    ini.parent.mkdir(parents=True)
    ini.write_text("[updates]\ncheckOnStartup=false\n", encoding="utf-8")
    env = {key: value for key, value in os.environ.items() if key != "PATCHY_MCP_ENDPOINT"}
    env.update({"QT_QPA_PLATFORM": "offscreen", "PATCHY_NO_SOUND": "1",
                "PATCHY_NO_SINGLE_INSTANCE": "1", "PATCHY_SETTINGS_DIR": str(settings)})
    fixture = out / "owned-document.psd"
    script = out / "create-fixture.js"
    script.write_text("var d=app.newDocument(16,16); d.addLayer('Seed').fill('#ffffff');"
                      f"if(!d.saveAs({json.dumps(str(fixture))})) throw new Error('save failed');",
                      encoding="utf-8")
    # The default endpoint (src/ui/mcp_attachment.cpp): an AppImage's identity is the home
    # folder plus "appimage", never its mount. Record any preexisting socket so cleanup can
    # never delete one this test did not create.
    name = "PatchyMcp-" + hashlib.sha256((str(Path.home()) + "\nappimage").encode()).hexdigest()[:32]
    endpoint = Path(tempfile.gettempdir()) / name
    if endpoint.exists():
        raise RuntimeError(f"A preexisting attachment endpoint needs inspection (is a Patchy AppImage open?): {endpoint}")
    owned_inode = None
    with (out / "stderr.log").open("w", encoding="utf-8") as log:
        subprocess.run([str(appimage), "--headless", "--run-script", str(script)],
                       cwd=ROOT, env=env, stdout=log, stderr=log, check=True, timeout=60)
        assert fixture.is_file()
        app = subprocess.Popen([str(appimage), str(fixture)], cwd=ROOT, env=env,
                               stdout=log, stderr=log, start_new_session=True)
        # The Python MCP SDK filters inherited environment variables; pass the ones the
        # connector's endpoint and settings depend on.
        connector_env = {key: env[key] for key in ("TMPDIR", "PATCHY_SETTINGS_DIR", "XDG_RUNTIME_DIR") if key in env}
        params = StdioServerParameters(command=str(appimage), args=["patchy-mcp", "--attach"],
                                       cwd=str(ROOT), env=connector_env)
        try:
            async with stdio_client(params, errlog=log) as (read, write):
                async with ClientSession(read, write) as client:
                    await client.initialize()
                    deadline = time.monotonic() + 60
                    while time.monotonic() < deadline:
                        if owned_inode is None and endpoint.exists():
                            owned_inode = endpoint.stat().st_ino
                        info = await client.call_tool("get_info", {})
                        if not info.isError:
                            break
                        assert app.poll() is None, "the Patchy AppImage exited"
                        await asyncio.sleep(.2)
                    else:
                        raise AssertionError("Separate AppImage launches cannot attach using the default endpoint")
                    state = (await client.call_tool("get_state", {})).structuredContent
                    assert len(state["documents"]) == 1 and state["documents"][0]["path"] == str(fixture)
                    owned_inode = endpoint.stat().st_ino
                    edit = await client.call_tool("execute_script", {
                        "code": "app.activeDocument.addLayer('AppImage test').fill('#ff0000');",
                        "expectedState": state["stateToken"]})
                    assert not edit.isError, edit
                    preview = await client.call_tool("get_preview", {})
                    assert not preview.isError and any(item.type == "image" for item in preview.content)
            async with stdio_client(params, errlog=log) as (read, write):
                async with ClientSession(read, write) as client:
                    await client.initialize()
                    state = (await client.call_tool("get_state", {})).structuredContent
                    assert len(state["documents"]) == 1 and state["documents"][0]["modified"]
            print("[PASS] Separate AppImage launches: default attachment, guarded edit, preview, unsaved reconnect")
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
    if len(sys.argv) != 2:
        sys.exit("usage: python tests/appimage_mcp_tests.py <file.AppImage>")
    asyncio.run(test(Path(sys.argv[1]).resolve()))
