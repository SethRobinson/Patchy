"""End-to-end tests using the official Python MCP client (development dependency).

Run from the repository root with: python tests/mcp_client_tests.py <patchy-mcp>
All artifacts stay under the project. No desktop UI or existing app is controlled.
"""
import asyncio
import base64
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "test-artifacts" / "mcp"
SESSION_TEMP = OUT / "sessions" / ("run-" + str(time.time_ns()))
TEMP_ENV = {key: str(SESSION_TEMP) for key in ("TMPDIR", "TEMP", "TMP")}


async def sdk_workflow(exe):
    async with stdio_client(StdioServerParameters(command=str(exe), cwd=str(OUT), env=TEMP_ENV)) as (read, write):
        async with ClientSession(read, write) as session:
            initialized = await session.initialize()
            assert initialized.serverInfo.name == "patchy"
            listed = await session.list_tools()
            assert len(listed.tools) == 8

            async def call(name, args=None, error=False):
                result = await session.call_tool(name, args or {})
                assert bool(result.isError) == error, result.model_dump()
                return result

            info = (await call("get_info")).structuredContent
            assert info["mode"] == "offscreen" and not info["windowVisible"]
            kit = Path(info["skillDirectory"])
            assert (kit / "SKILL.md").is_file()
            assert (await call("get_state")).structuredContent["documents"] == []
            help_result = (await call("get_help", {"topic": "api"})).structuredContent
            assert "drawStrokes" in help_result["text"]
            assert (await call("get_help", {"topic": "reference-art"})).structuredContent["text"]
            created = (await call("execute_script", {
                "code": (kit / "scripts" / "pixel-art.js").read_text(encoding="utf-8"),
                "args": {"out": str(OUT)},
            })).structuredContent
            assert created["status"] == "done"
            doc_id = created["result"]["documentId"]
            layer_id = created["result"]["layerId"]
            state_before = (await call("get_state")).structuredContent
            preview = await call("get_preview", {"documentId": doc_id,
                "options": {"nearestNeighbor": True, "maxWidth": 256, "maxHeight": 256}})
            original = base64.b64decode(next(x.data for x in preview.content if x.type == "image"))
            assert original.startswith(b"\x89PNG\r\n\x1a\n")
            (OUT / "mcp-original.png").write_bytes(original)
            assert preview.structuredContent["width"] == 256
            assert (await call("get_state")).structuredContent == state_before
            await call("draw_strokes", {"documentId": doc_id, "layerId": layer_id,
                "strokes": [{"color": "#ffffff", "size": 1, "seed": 0,
                             "points": [{"x": 1, "y": 1}, {"x": 28, "y": 1}]}]})
            changed = await call("get_preview", {"documentId": doc_id,
                "options": {"nearestNeighbor": True, "maxWidth": 256, "maxHeight": 256}})
            changed_png = base64.b64decode(next(x.data for x in changed.content if x.type == "image"))
            assert changed_png != original
            assert (await call("undo", {"documentId": doc_id})).structuredContent["changed"]
            restored = await call("get_preview", {"documentId": doc_id,
                "options": {"nearestNeighbor": True, "maxWidth": 256, "maxHeight": 256}})
            assert base64.b64decode(next(x.data for x in restored.content if x.type == "image")) == original
            await call("redo", {"documentId": doc_id})
            result = (await call("execute_script", {"code": "patchy.setResult(typeof doc);"})).structuredContent
            assert result["result"] == "undefined"
            # A malformed batch must not paint its earlier valid stroke.
            before_bad = (await call("get_state")).structuredContent
            await call("draw_strokes", {"documentId": doc_id, "layerId": layer_id,
                "strokes": [{"points": [{"x": 2, "y": 2}]}, {"points": [], "size": 0}]}, error=True)
            assert (await call("get_state")).structuredContent == before_bad
            await call("get_preview", {"documentId": "999999"}, error=True)
            await call("get_preview", {"options": {"maxWidth": -1}}, error=True)
            await call("execute_script", {"code": "app.runCommand('file.open');"}, error=True)
            await call("execute_script", {"code": "patchy.ui.createCanvas();"}, error=True)
            await call("execute_script", {"code": "app.undoEnabled=false;"}, error=True)
            await call("execute_script", {"code": "var l=app.activeDocument.addLayer('Partial'); throw new Error('expected');"}, error=True)
            partial = (await call("get_state")).structuredContent
            assert partial["documents"][0]["layers"][-1]["name"] == "Partial"
            await call("undo", {"documentId": doc_id})
            final = OUT / "final.psd"
            saved = (await call("execute_script", {"code":
                f"var d=app.getDocument({json.dumps(doc_id)}); patchy.setResult(d.saveAs({json.dumps(str(final))}));"})).structuredContent
            assert saved["result"] and final.stat().st_size > 0
            await call("execute_script", {"code": f"app.open({json.dumps(str(final))});"})
            assert len((await call("get_state")).structuredContent["documents"]) == 2
            reopened = await call("get_preview", {"options": {
                "nearestNeighbor": True, "maxWidth": 256, "maxHeight": 256}})
            assert base64.b64decode(next(x.data for x in reopened.content if x.type == "image")) == changed_png
            assert (await call("get_preview", {"target": "window"})).structuredContent["offscreen"]
            # The view API stages window captures where menu commands are refused.
            zoomed = (await call("execute_script", {"code":
                "patchy.ui.setWindowSize(1000, 700); patchy.ui.fitOnScreen(); var fit = patchy.ui.zoom;"
                " patchy.ui.zoom = 400; patchy.setResult({fit: fit, zoom: patchy.ui.zoom});"})).structuredContent["result"]
            assert zoomed["fit"] > 0 and zoomed["zoom"] == 400
            window = await call("get_preview", {"target": "window"})
            assert window.structuredContent["width"] == 1000 and window.structuredContent["height"] == 700
            await call("execute_script", {"code": "patchy.ui.zoom = NaN;"}, error=True)
            # Exercise every shipped example without relying on a source checkout.
            for script, args in [("painting", {"out": str(OUT)}),
                                 ("edit-document", {"input": str(final), "output": str(OUT / "edited.psd")})]:
                await call("execute_script", {"code": (kit / "scripts" / (script + ".js")).read_text(encoding="utf-8"), "args": args})
    print("[PASS] MCP SDK: discovery, examples, persistent edits, previews, undo/redo, errors, save/reopen")
    assert not list(SESSION_TEMP.glob("patchy-mcp-*")), "Session settings were not cleaned up"


async def visible_options(exe):
    # Test the flag without opening a desktop window. Report the actual backend.
    params = StdioServerParameters(command=str(exe), args=["--visible"], cwd=str(OUT),
                                   env={**TEMP_ENV, "QT_QPA_PLATFORM": "offscreen"})
    async with stdio_client(params) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            info = await session.call_tool("get_info", {})
            assert not info.isError
            assert info.structuredContent["mode"] == "offscreen"
            assert info.structuredContent["platform"] == "offscreen"
            assert not info.structuredContent["windowVisible"]
            assert not info.structuredContent["liveWindowAttachment"]
            made = await session.call_tool("execute_script", {"code": "app.newDocument(64,64);"})
            assert not made.isError
            preview = await session.call_tool("get_preview", {"target": "window"})
            assert not preview.isError and preview.structuredContent["offscreen"]
    assert not list(SESSION_TEMP.glob("patchy-mcp-*")), "Session settings were not cleaned up"
    invalid = subprocess.run([str(exe), "--visible", "--invalid"], capture_output=True,
                             cwd=OUT, env={**os.environ, **TEMP_ENV}, timeout=30)
    assert invalid.returncode == 2 and b"Usage:" in invalid.stderr
    print("[PASS] MCP visible option: protocol, actual-backend metadata, isolation, invalid arguments")


def protocol_edges(exe):
    log = (OUT / "protocol-stderr.log").open("w", encoding="utf-8")
    proc = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=log, cwd=OUT, env={**os.environ, **TEMP_ENV})
    received = queue.Queue()
    def reader():
        for line in proc.stdout:
            received.put(json.loads(line))
    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    def send(method, params=None, request_id=None):
        message = {"jsonrpc": "2.0", "method": method, "params": params or {}}
        if request_id is not None:
            message["id"] = request_id
        proc.stdin.write(json.dumps(message).encode() + b"\n")
        proc.stdin.flush()
    def take(request_id):
        message = received.get(timeout=30)
        assert message["id"] == request_id, message
        return message
    try:
        send("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                            "clientInfo": {"name": "test", "version": "1"}}, 1)
        assert take(1)["result"]["protocolVersion"] == "2025-06-18"
        send("notifications/initialized")
        send("tools/call", {"name": "get_state"}, 100)
        assert take(100)["result"]["structuredContent"]["documents"] == []
        send("tools/call", {"name": "execute_script", "arguments": {
            "code": "app.newDocument(8,8); app.activeDocument.addLayer('Partial cancel'); while(true){}"}}, 2)
        time.sleep(0.2)
        send("tools/call", {"name": "get_state"}, 3)
        assert take(3)["result"]["structuredContent"]["error"] == "busy"
        send("notifications/cancelled", {"requestId": 2, "reason": "test cancellation"})
        cancelled = take(2)["result"]
        assert cancelled["isError"]
        assert cancelled["structuredContent"]["status"] == "cancelled"
        assert cancelled["structuredContent"]["state"]["documents"][0]["canUndo"]
        send("tools/call", {"name": "execute_script", "arguments": {"code": "patchy.setResult(42);"}}, 4)
        assert take(4)["result"]["structuredContent"]["result"] == 42
        # Immediate cancellation must not get lost while the engine is created.
        for request_id in range(20, 30):
            send("tools/call", {"name": "execute_script", "arguments": {"code": "while(true){}"}}, request_id)
            send("notifications/cancelled", {"requestId": request_id})
            assert take(request_id)["result"]["isError"]
        proc.stdin.write(b"{bad json}\n")
        proc.stdin.flush()
        assert received.get(timeout=5)["error"]["code"] == -32700
        # Disconnect while a callback keeps the run alive.
        send("tools/call", {"name": "execute_script", "arguments": {"code": "setInterval(function(){},50);"}}, 5)
        proc.stdin.close()
        assert proc.wait(timeout=15) == 0
        thread.join(timeout=2)
        assert not list(SESSION_TEMP.glob("patchy-mcp-*")), "Session settings were not cleaned up"
    finally:
        if proc.poll() is None:
            proc.kill()  # only the test-owned background process
            proc.wait()
        log.close()
    print("[PASS] MCP protocol: version negotiation, busy, tight-loop cancellation, recovery, malformed JSON, disconnect")


if __name__ == "__main__":
    executable = Path(sys.argv[1]).resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    SESSION_TEMP.mkdir(parents=True, exist_ok=True)
    asyncio.run(sdk_workflow(executable))
    asyncio.run(visible_options(executable))
    protocol_edges(executable)
