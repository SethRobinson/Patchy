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
import shutil
import subprocess
import sys
import threading
import time
import uuid

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "test-artifacts" / "mcp"
SESSION_TEMP = OUT / "sessions" / ("run-" + str(time.time_ns()))
TEMP_ENV = {key: str(SESSION_TEMP) for key in ("TMPDIR", "TEMP", "TMP")}
TEMP_ENV["PATCHY_SETTINGS_DIR"] = str(OUT / "brush-settings")


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
            # A client installs only the stable entry point. All working
            # instructions come from the connected installation, not this copy.
            installed = OUT / "client-skills" / ("patchy-control-" + str(time.time_ns()))
            installed.mkdir(parents=True)
            shutil.copyfile(kit / "SKILL.md", installed / "SKILL.md")
            bootstrap = (installed / "SKILL.md").read_bytes()
            workflow = (await call("get_help", {"topic": "workflow"})).structuredContent["text"]
            assert workflow == (kit / "references" / "workflow.md").read_bytes().decode("utf-8")
            assert workflow != bootstrap.decode("utf-8")
            assert (await call("get_help")).structuredContent["text"] == workflow
            assert list(installed.iterdir()) == [installed / "SKILL.md"]
            assert (installed / "SKILL.md").read_bytes() == bootstrap
            assert (await call("get_state")).structuredContent["documents"] == []
            assert "slowMode" in info["capabilities"]
            assert "pauseAutomation" in info["capabilities"]
            assert not (await call("get_state")).structuredContent["paused"]
            assert not (await call("get_state")).structuredContent["slowMode"]
            assert not (await call("get_state")).structuredContent["slowModeAvailable"]
            slow_doc = (await call("execute_script", {"code":
                "var d=app.newDocument(32,32);var l=d.addLayer('Slow ink');"
                "patchy.setResult({documentId:d.id,layerId:l.id});"})).structuredContent["result"]
            async def slow_preview():
                preview = await call("get_preview", {"documentId": slow_doc["documentId"]})
                return next(x.data for x in preview.content if x.type == "image")
            blank = await slow_preview()
            before_slow = (await call("get_state")).structuredContent
            await call("execute_script", {"code": "patchy.ui.slowMode=true;"}, error=True)
            assert (await call("get_state")).structuredContent == before_slow
            await call("execute_script", {"code": "patchy.ui.paused=true;"}, error=True)
            assert (await call("get_state")).structuredContent == before_slow
            await call("draw_strokes", {**slow_doc, "strokes": [
                {"size": 12, "color": "#883322", "points": [{"x": 8, "y": 8}]},
                {"size": 12, "color": "#224488", "points": [{"x": 24, "y": 24}]}]})
            painted = await slow_preview()
            await call("undo", {"documentId": slow_doc["documentId"]})
            assert await slow_preview() == blank
            await call("redo", {"documentId": slow_doc["documentId"]})
            assert await slow_preview() == painted
            await call("execute_script", {"code": "patchy.ui.slowMode=false;app.activeDocument.close();"})
            help_result = (await call("get_help", {"topic": "api"})).structuredContent
            assert "drawStrokes" in help_result["text"]
            assert help_result["text"] == (kit / "references" / "patchy.d.ts").read_bytes().decode("utf-8")
            assert (await call("get_help", {"topic": "reference-art"})).structuredContent["text"]
            assert {"vectorShapes", "vectorPaths", "vectorMasks", "vectorPaints"} <= set(info["capabilities"])
            assert {"brushTips", "brushPresets", "brushDynamics", "mixerBrush", "wetEdges", "timedAirbrush"} <= set(info["capabilities"])
            assert (await call("get_help", {"topic": "painting-guide"})).structuredContent["text"]
            abr_path = OUT / "ブラシ-dynamics.abr"
            shutil.copyfile(ROOT / "test-fixtures" / "abr" / "photoshop-dynamics.abr", abr_path)
            imported = (await call("execute_script", {"code":
                f"patchy.setResult(patchy.brushes.importAbr({json.dumps(str(abr_path))}));"})).structuredContent["result"]
            assert imported["ids"] and isinstance(imported["warnings"], list)
            resolved = (await call("execute_script", {"code":
                f"patchy.setResult(patchy.brushes.resolve({{tipId:{json.dumps(imported['ids'][0])}}}));"})).structuredContent["result"]
            assert abs(resolved["settings"]["dynamics"]["sizeJitter"] - .37) < 1e-9
            for topic in ("vector-art", "edit-shape", "paths-masks"):
                assert (await call("get_help", {"topic": topic})).structuredContent["text"] == (kit / "scripts" / (topic + ".js")).read_bytes().decode("utf-8")
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
                                 ("brush-swatches", {"out": str(OUT)}),
                                 ("wet-paint", {"out": str(OUT)}),
                                 ("fur-strokes", {"out": str(OUT)}),
                                 ("timed-brush", {"out": str(OUT)}),
                                 ("brush-library", {}),
                                 ("edit-document", {"input": str(final), "output": str(OUT / "edited.psd")})]:
                await call("execute_script", {"code": (kit / "scripts" / (script + ".js")).read_text(encoding="utf-8"), "args": args})
            vector_code = (kit / "scripts" / "vector-art.js").read_text(encoding="utf-8")
            vector_id = ""
            for stage in range(1, 9):
                result = (await call("execute_script", {"code": vector_code, "args": {
                    "stage": str(stage), "documentId": vector_id, "watch": "true",
                    "output": str(OUT / "vector-example")}})).structuredContent
                vector_id = result["result"]["documentId"]
                preview = await call("get_preview", {"documentId": vector_id, "options": {"maxWidth": 480, "maxHeight": 480}})
                (OUT / ("vector-stage-%d.png" % stage)).write_bytes(base64.b64decode(next(x.data for x in preview.content if x.type == "image")))
            inspected = (await call("execute_script", {"code": "var d=app.getDocument(patchy.args.documentId); patchy.setResult({layerId:d.findLayer('Head').id,shape:d.findLayer('Head').getShape()});",
                                                    "args": {"documentId": vector_id}})).structuredContent["result"]
            assert inspected["shape"]["editable"] and inspected["shape"]["path"]["subpaths"]
            target_args = {"documentId": vector_id, "layerId": inspected["layerId"]}
            await call("execute_script", {"code": "app.getDocument(patchy.args.documentId).getLayer(patchy.args.layerId).updateShape({fill:'#112233'}); throw Error('later failure');",
                                          "args": target_args}, error=True)
            await call("undo", {"documentId": vector_id})
            restored = (await call("execute_script", {"code": "patchy.setResult(app.getDocument(patchy.args.documentId).getLayer(patchy.args.layerId).getShape());",
                                                     "args": target_args})).structuredContent["result"]
            assert restored == inspected["shape"], "Earlier vector edits must remain undoable after a later script failure"
            for example, action in (("edit-shape", "inspect"), ("edit-shape", "revise"), ("paths-masks", "create"), ("paths-masks", "selection")):
                await call("execute_script", {"code": (kit / "scripts" / (example + ".js")).read_text(encoding="utf-8"),
                                              "args": {"documentId": vector_id, "layerId": inspected["layerId"], "action": action}})
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


async def attached_workspace(exe):
    candidates = [exe.with_name("patchy.exe"), exe.with_name("patchy"),
                  exe.parent / "Patchy.app" / "Contents" / "MacOS" / "Patchy",
                  exe.with_name("Patchy")]
    app_exe = next((path for path in candidates if path.is_file()), None)
    assert app_exe, "The matching Patchy application is required for attachment tests"
    endpoint = "PatchyMcpTest-" + uuid.uuid4().hex
    if os.name != "nt":
        # Unix sockets have a short path limit. The isolated TMPDIR below is
        # deliberately deep, so put this socket directly in our artifact area.
        endpoint = str(OUT / ("s-" + uuid.uuid4().hex[:12]))
    settings = SESSION_TEMP / "attached-settings"
    ini = settings / "Patchy" / "Patchy.ini"
    ini.parent.mkdir(parents=True)
    ini.write_text("[updates]\ncheckOnStartup=false\n", encoding="utf-8")
    env = {**os.environ, **TEMP_ENV, "QT_QPA_PLATFORM": "offscreen",
           "PATCHY_SETTINGS_DIR": str(settings), "PATCHY_MCP_ENDPOINT": endpoint,
           "PATCHY_NO_SINGLE_INSTANCE": "1", "PATCHY_NO_SOUND": "1"}
    original = OUT / "final.psd"
    original_bytes = original.read_bytes()
    params = StdioServerParameters(command=str(exe), args=["--attach"], cwd=str(OUT), env=env)
    with (OUT / "attached-app-stderr.log").open("w", encoding="utf-8") as log:
        app = subprocess.Popen([str(app_exe), str(original)], cwd=OUT, env=env,
                               stdout=log, stderr=log)
        try:
            initialized = False
            deadline = time.monotonic() + 45
            while True:
                try:
                    async with stdio_client(params, errlog=log) as (read, write):
                        async with ClientSession(read, write) as client:
                            await client.initialize()
                            initialized = True
                            info = (await client.call_tool("get_info", {})).structuredContent
                            assert info["liveWindowAttachment"] and info["requiresExpectedState"]
                            assert info["workspace"] == "attached" and int(info["processId"]) == app.pid
                            state = (await client.call_tool("get_state", {})).structuredContent
                            assert len(state["documents"]) == 1
                            assert Path(state["documents"][0]["path"]) == original
                            assert not state["documents"][0]["modified"]
                            slow_enabled = await client.call_tool("execute_script", {"code":
                                "patchy.ui.slowMode=true;", "expectedState": state["stateToken"]})
                            assert not slow_enabled.isError
                            slow_state = (await client.call_tool("get_state", {})).structuredContent
                            assert slow_state["slowMode"] and slow_state["documents"] == state["documents"]
                            assert slow_state["slowModeAvailable"]
                            assert slow_state["stateToken"] != state["stateToken"]
                            slow_window = await client.call_tool("get_preview", {"target": "window"})
                            (OUT / "slow-mode-window.png").write_bytes(base64.b64decode(
                                next(x.data for x in slow_window.content if x.type == "image")))
                            slow_disabled = await client.call_tool("execute_script", {"code":
                                "patchy.ui.slowMode=false;", "expectedState": slow_state["stateToken"]})
                            assert not slow_disabled.isError
                            state = (await client.call_tool("get_state", {})).structuredContent
                            # Brush selection invalidates state without changing document pixels/history.
                            activated = await client.call_tool("execute_script", {"code":
                                "patchy.brushes.activate({size:27,dynamics:{wetEdges:true}});",
                                "expectedState": state["stateToken"]})
                            assert not activated.isError
                            brush_state = (await client.call_tool("get_state", {})).structuredContent
                            assert brush_state["stateToken"] != state["stateToken"]
                            assert not brush_state["documents"][0]["modified"]
                            stale_brush = await client.call_tool("execute_script", {"code": "patchy.setResult(1);",
                                "expectedState": state["stateToken"]})
                            assert stale_brush.isError and stale_brush.structuredContent["error"] == "stale_state"
                            read_brush = await client.call_tool("execute_script", {"code":
                                "patchy.setResult(patchy.brushes.getCurrent());", "expectedState": brush_state["stateToken"]})
                            assert read_brush.structuredContent["result"]["dynamics"]["wetEdges"]
                            assert (await client.call_tool("get_state", {})).structuredContent == brush_state
                            # An isolated workspace writes the same persistent library, never the app's window settings.
                            isolated_params = StdioServerParameters(command=str(exe), cwd=str(OUT), env=env)
                            async with stdio_client(isolated_params, errlog=log) as (ir, iw):
                                async with ClientSession(ir, iw) as isolated:
                                    await isolated.initialize()
                                    saved_brush = await isolated.call_tool("execute_script", {"code":
                                        "patchy.setResult(patchy.brushes.savePreset('Cross-process oil',{size:29,dynamics:{wetEdges:true}}));"})
                                    assert not saved_brush.isError, saved_brush.model_dump()
                                    saved_id = saved_brush.structuredContent["result"]["id"]
                            state = (await client.call_tool("get_state", {})).structuredContent
                            assert state["brushLibraryRevision"] != brush_state["brushLibraryRevision"]
                            assert state["stateToken"] != brush_state["stateToken"]
                            persisted = await client.call_tool("execute_script", {"code":
                                f"patchy.setResult(patchy.brushes.getPreset({json.dumps(saved_id)}));",
                                "expectedState": state["stateToken"]})
                            assert persisted.structuredContent["result"]["name"] == "Cross-process oil"
                            before = await client.call_tool("get_preview", {})
                            assert before.structuredContent["stateToken"] == state["stateToken"]
                            code = "app.activeDocument.addLayer('Face correction').fillRect(0,0,4,4,'#ffc080');"
                            missing = await client.call_tool("execute_script", {"code": code})
                            assert missing.isError and missing.structuredContent["error"] == "stale_state"
                            changed = await client.call_tool("execute_script", {
                                "code": code, "name": "Face correction", "expectedState": state["stateToken"]})
                            assert not changed.isError, changed.model_dump()
                            assert changed.structuredContent["state"]["documents"][0]["modified"]
                            stale = await client.call_tool("execute_script", {"code": code, "expectedState": state["stateToken"]})
                            assert stale.isError and stale.structuredContent["error"] == "stale_state"
                            preview = await client.call_tool("get_preview", {})
                            changed_png = next(x.data for x in preview.content if x.type == "image")
                            assert changed_png != next(x.data for x in before.content if x.type == "image")
                            token = preview.structuredContent["stateToken"]
                            # A second client is refused promptly, never queued.
                            extra = subprocess.Popen([str(exe), "--attach"], stdin=subprocess.PIPE,
                                                     stdout=subprocess.PIPE, stderr=log, env=env, cwd=OUT)
                            try:
                                extra.stdin.write(b'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n')
                                extra.stdin.flush()
                                assert await asyncio.to_thread(extra.wait, 10) == 0
                                assert not extra.stdout.read()
                            finally:
                                if extra.poll() is None:
                                    extra.kill()
                                    extra.wait()
                                extra.stdin.close()
                                extra.stdout.close()
                    break
                except Exception:
                    # Retry startup only, never a mutation or failed assertion.
                    if initialized or app.poll() is not None or time.monotonic() >= deadline:
                        raise
                    await asyncio.sleep(0.1)
            assert app.poll() is None, "Disconnect must not close the artist's app"
            async with stdio_client(params, errlog=log) as (read, write):
                async with ClientSession(read, write) as client:
                    await client.initialize()
                    state = (await client.call_tool("get_state", {})).structuredContent
                    assert state["stateToken"] != token
                    assert state["documents"][0]["modified"]
                    preview = await client.call_tool("get_preview", {})
                    assert next(x.data for x in preview.content if x.type == "image") == changed_png
                    undone = await client.call_tool("undo", {"documentId": state["activeDocumentId"],
                                                             "expectedState": state["stateToken"]})
                    assert not undone.isError
                    preview = await client.call_tool("get_preview", {})
                    assert next(x.data for x in preview.content if x.type == "image") == next(x.data for x in before.content if x.type == "image")
            assert original.read_bytes() == original_bytes, "Attachment must not silently save the original"
            # If the app exits, the proxy must exit even with its stdin still open.
            proxy = subprocess.Popen([str(exe), "--attach"], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=log, env=env, cwd=OUT)
            try:
                proxy.stdin.write(b'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n')
                proxy.stdin.flush()
                initialized_reply = await asyncio.wait_for(asyncio.to_thread(proxy.stdout.readline), 10)
                assert json.loads(initialized_reply)["id"] == 1
                app.terminate()  # only the test-owned offscreen application
                await asyncio.to_thread(app.wait, 10)
                assert await asyncio.to_thread(proxy.wait, 10) == 0
            finally:
                if proxy.poll() is None:
                    proxy.kill()
                    proxy.wait()
                proxy.stdin.close()
                proxy.stdout.close()
            absent = subprocess.run([str(exe), "--attach"], input=b"", capture_output=True,
                                    env=env, cwd=OUT, timeout=10)
            assert absent.returncode == 2 and absent.stderr and not absent.stdout
        finally:
            if app.poll() is None:
                app.terminate()
                app.wait(timeout=10)
    print("[PASS] MCP attachment: existing document, guarded edits, previews, single client, unsaved reconnect, undo, app exit, no fallback")


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
            "code": "app.newDocument(8,8).addShape('Partial cancel',{type:'ellipse',x:1,y:1,width:6,height:6}); while(true){}"}}, 2)
        time.sleep(0.2)
        send("tools/call", {"name": "get_state"}, 3)
        assert take(3)["result"]["structuredContent"]["error"] == "busy"
        send("notifications/cancelled", {"requestId": 2, "reason": "test cancellation"})
        cancelled = take(2)["result"]
        assert cancelled["isError"]
        assert cancelled["structuredContent"]["status"] == "cancelled"
        assert cancelled["structuredContent"]["state"]["documents"][0]["canUndo"]
        assert cancelled["structuredContent"]["state"]["documents"][0]["layers"][-1]["isShape"]
        send("tools/call", {"name": "execute_script", "arguments": {"code": "patchy.setResult(42);"}}, 4)
        assert take(4)["result"]["structuredContent"]["result"] == 42
        # Cancel inside simulated native painting, not just inside JavaScript.
        send("tools/call", {"name": "execute_script", "arguments": {"code":
            "app.newDocument(256,256).addLayer('Cancelled airbrush');"}}, 14)
        assert not take(14)["result"]["isError"]
        send("tools/call", {"name": "execute_script", "arguments": {"code":
            "app.activeDocument.activeLayer.drawStrokes([{size:256,flow:10,airbrush:true,points:["
            "{x:128,y:128,timeMs:0},{x:128,y:128,timeMs:3600000}]}]);"}}, 15)
        time.sleep(0.2)
        send("notifications/cancelled", {"requestId": 15, "reason": "native stroke stop"})
        paint_cancelled = take(15)["result"]
        assert paint_cancelled["isError"] and paint_cancelled["structuredContent"]["status"] == "cancelled", paint_cancelled
        assert paint_cancelled["structuredContent"]["state"]["documents"][-1]["canUndo"]
        assert paint_cancelled["structuredContent"]["state"]["currentBrush"] == cancelled["structuredContent"]["state"]["currentBrush"]
        send("tools/call", {"name": "execute_script", "arguments": {"code": "patchy.setResult(43);"}}, 16)
        assert take(16)["result"]["structuredContent"]["result"] == 43
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
    asyncio.run(attached_workspace(executable))
