"""Token-free client for Affinity 3.2+'s built-in JavaScript automation.

Affinity by Canva 3.2 ships a JavaScript SDK (the BSD-licensed JSLib inside the
install) and exposes it through a local MCP server at http://[::1]:6767 while
the "AI connector" is enabled in the app's settings. MCP here is nothing more
than JSON-RPC over an SSE stream: this module speaks it directly with the
standard library, no AI anywhere, no tokens spent. One execute_script call runs
arbitrary JS inside the app and returns whatever the script console.log()ed.

Server facts (verified against 3.2.3.4646, July 2026):

- The listener binds IPv6 loopback ONLY ([::1]:6767; 127.0.0.1 is refused).
- GET /sse yields an ``endpoint`` event naming the session's POST URL; JSON-RPC
  responses arrive back on the SSE stream, not in the POST body.
- initialize must request protocolVersion "2025-11-25".
- Each session must call the read_sdk_documentation_topic("preamble") tool once
  before execute_script works (the server enforces it).
- Scripts have no return value channel: console.log() output (including output
  from async callbacks that fire after the script body ends - the host pumps
  them) comes back concatenated in the tool result text.
- Filesystem scope: Document.load/export/saveAs may only touch paths under the
  user's Desktop (PERMISSION_DENIED elsewhere); a NOT_ALLOWED error means the
  user disabled scripting/filesystem access in Affinity's settings.
- Document.close/closeAsync are NOT_IMPLEMENTED on Windows in 3.2.3: opened
  documents stay as tabs until the app quits. Quitting with imported documents
  raises per-document save prompts (they load with needsSaving=true), so the
  graceful quit below keeps dismissing "Don't Save" buttons while it waits.
"""

from __future__ import annotations

import json
import socket
import subprocess
import threading
import time
import urllib.request
from pathlib import Path
from typing import Callable

MCP_HOST = "::1"
MCP_PORT = 6767
MCP_BASE = f"http://[{MCP_HOST}]:{MCP_PORT}"
PROTOCOL_VERSION = "2025-11-25"

# The WindowsApps execution alias; config.py discovers the same path.
DEFAULT_EXE = Path.home() / "AppData/Local/Microsoft/WindowsApps/Affinity.exe"

COLD_START_TIMEOUT = 120  # cold MSIX launch until the MCP port listens


class AffinityJsError(RuntimeError):
    pass


class ConnectorOffError(AffinityJsError):
    """Affinity is running but the MCP port never opened (connector disabled)."""


def port_open(timeout: float = 1.0) -> bool:
    try:
        with socket.create_connection((MCP_HOST, MCP_PORT), timeout=timeout):
            return True
    except OSError:
        return False


def app_running() -> bool:
    import win32gui

    found: list[int] = []

    def callback(hwnd, _):
        try:
            if (win32gui.IsWindowVisible(hwnd)
                    and win32gui.GetWindowText(hwnd) == "Affinity"
                    and "HwndWrapper[Affinity.exe" in win32gui.GetClassName(hwnd)):
                found.append(hwnd)
        except Exception:
            pass
        return True

    win32gui.EnumWindows(callback, None)
    return bool(found)


class AffinityJs:
    """One SSE session against the app's MCP endpoint.

    Connections are cheap; a fresh instance per app launch is the norm. All
    requests are serialized (the driver is single-threaded by design).
    """

    def __init__(self, log: Callable[[str], None] = lambda m: None) -> None:
        self.log = log
        self.endpoint: str | None = None
        self._responses: dict[int, dict] = {}
        self._cond = threading.Condition()
        self._next_id = 1
        self._dead: str | None = None
        self._preamble_read = False

    # ---------- transport ----------

    def connect(self) -> None:
        ready = threading.Event()

        def reader() -> None:
            try:
                req = urllib.request.Request(
                    MCP_BASE + "/sse", headers={"Accept": "text/event-stream"})
                with urllib.request.urlopen(req, timeout=24 * 3600) as resp:
                    event, data_lines = None, []
                    while True:
                        raw = resp.readline()
                        if not raw:
                            break
                        line = raw.decode("utf-8", "replace").rstrip("\r\n")
                        if line.startswith("event:"):
                            event = line[6:].strip()
                        elif line.startswith("data:"):
                            data_lines.append(line[5:].strip())
                        elif line == "":
                            if event or data_lines:
                                self._on_event(event, "\n".join(data_lines), ready)
                            event, data_lines = None, []
            except Exception as error:
                self._dead = f"SSE stream died: {error}"
            else:
                self._dead = "SSE stream closed"
            with self._cond:
                self._cond.notify_all()
            ready.set()

        threading.Thread(target=reader, daemon=True).start()
        if not ready.wait(timeout=20) or self.endpoint is None:
            raise AffinityJsError(self._dead or "no endpoint event from /sse")
        info = self._request("initialize", {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {"name": "testy", "version": "1.0"},
        })
        self._notify("notifications/initialized")
        server = info.get("serverInfo", {})
        self.log(f"Affinity MCP connected ({server.get('name')} "
                 f"{server.get('version')})")

    def _on_event(self, event: str | None, data: str, ready: threading.Event) -> None:
        if event == "endpoint":
            self.endpoint = data if data.startswith("http") else MCP_BASE + data
            ready.set()
            return
        try:
            msg = json.loads(data)
        except ValueError:
            return
        if isinstance(msg, dict) and "id" in msg and ("result" in msg or "error" in msg):
            with self._cond:
                self._responses[msg["id"]] = msg
                self._cond.notify_all()

    def _post(self, payload: dict) -> None:
        assert self.endpoint is not None
        req = urllib.request.Request(
            self.endpoint, data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=30) as resp:
            resp.read()

    def _request(self, method: str, params: dict | None = None,
                 timeout: float = 60) -> dict:
        mid = self._next_id
        self._next_id += 1
        payload: dict = {"jsonrpc": "2.0", "id": mid, "method": method}
        if params is not None:
            payload["params"] = params
        self._post(payload)
        deadline = time.time() + timeout
        with self._cond:
            while mid not in self._responses:
                if self._dead:
                    raise AffinityJsError(self._dead)
                remaining = deadline - time.time()
                if remaining <= 0:
                    raise AffinityJsError(f"no response to {method} within {timeout}s")
                self._cond.wait(timeout=min(remaining, 5))
            msg = self._responses.pop(mid)
        if "error" in msg:
            raise AffinityJsError(f"{method} error: {json.dumps(msg['error'])}")
        return msg["result"]

    def _notify(self, method: str) -> None:
        self._post({"jsonrpc": "2.0", "method": method})

    @property
    def alive(self) -> bool:
        return self._dead is None and self.endpoint is not None

    def call_tool(self, name: str, arguments: dict, timeout: float = 120) -> str:
        result = self._request("tools/call", {"name": name, "arguments": arguments},
                               timeout=timeout)
        parts = [c.get("text", "") for c in result.get("content", [])
                 if c.get("type") == "text"]
        text = "\n".join(parts)
        if result.get("isError"):
            raise AffinityJsError(f"tool {name} failed: {text[:500]}")
        return text

    # ---------- scripting ----------

    def prime(self) -> None:
        """Read the preamble topic: the server refuses execute_script until the
        session has, and the call doubles as a health ping after connecting."""
        if not self._preamble_read:
            self.call_tool("read_sdk_documentation_topic", {"filename": "preamble"})
            self._preamble_read = True

    def execute(self, script: str, timeout: float = 120) -> str:
        """Run JavaScript inside Affinity; returns the console.log output."""
        self.prime()
        return self.call_tool("execute_script", {"script": script}, timeout=timeout)

    def execute_json(self, script: str, timeout: float = 120) -> dict:
        """Run a script that ends with console.log("@@RESULT " + JSON...)."""
        text = self.execute(script, timeout=timeout)
        for line in reversed(text.splitlines()):
            if line.startswith("@@RESULT "):
                return json.loads(line[len("@@RESULT "):])
        raise AffinityJsError(f"script produced no @@RESULT line: {text[:800]}")


# ---------- app lifecycle ----------

def connect(log: Callable[[str], None] = lambda m: None,
            exe: Path | None = None, launch: bool = True) -> AffinityJs:
    """Connect to a running Affinity, launching one first if needed."""
    launched = False
    if not port_open():
        if not launch:
            raise ConnectorOffError("Affinity MCP port not open")
        if app_running():
            raise ConnectorOffError(
                "Affinity is running but its MCP port is closed: enable the "
                "connector (Settings > AI connector) and keep scripting/"
                "filesystem access allowed")
        subprocess.Popen([str(exe or DEFAULT_EXE)])
        launched = True
        log("launched Affinity, waiting for the MCP port")
        deadline = time.time() + COLD_START_TIMEOUT
        while time.time() < deadline:
            if port_open():
                break
            time.sleep(2.0)
        else:
            hint = ("app never started" if not app_running() else
                    "app started but the MCP connector never listened "
                    "(enable it in Settings > AI connector)")
            raise ConnectorOffError(f"MCP port not open after launch: {hint}")
    if launched:
        # The port accepts connections well before the app finishes starting,
        # then the MCP subsystem restarts and resets every stream. Wait for the
        # main window plus a settle before even trying.
        deadline = time.time() + 30
        while time.time() < deadline and not app_running():
            time.sleep(1.0)
        time.sleep(8.0)
    # Connect + prime, then prove the session survives a pause: a stream that
    # lives through prime can still be reset moments later during startup.
    deadline = time.time() + 90
    last_error: Exception | None = None
    while time.time() < deadline:
        client = AffinityJs(log)
        try:
            client.connect()
            client.prime()
            time.sleep(2.0)
            client._request("tools/list", timeout=30)
            return client
        except AffinityJsError as error:
            last_error = error
            time.sleep(3.0)
    raise AffinityJsError(f"MCP session would not stabilize: {last_error}")


def quit_app(log: Callable[[str], None] = lambda m: None,
             timeout: float = 60) -> None:
    """Graceful quit: WM_CLOSE, then keep dismissing per-document save prompts
    (imported documents carry needsSaving even untouched). taskkill only as the
    last resort - a force-killed MSIX instance leaves a zombie single-instance
    registration behind."""
    import win32con
    import win32gui

    def main_hwnd() -> int | None:
        found: list[int] = []

        def callback(hwnd, _):
            try:
                if (win32gui.IsWindowVisible(hwnd)
                        and win32gui.GetWindowText(hwnd) == "Affinity"
                        and "HwndWrapper[Affinity.exe" in win32gui.GetClassName(hwnd)):
                    found.append(hwnd)
            except Exception:
                pass
            return True

        win32gui.EnumWindows(callback, None)
        return found[0] if found else None

    hwnd = main_hwnd()
    if hwnd is None:
        return
    win32gui.PostMessage(hwnd, win32con.WM_CLOSE, 0, 0)
    deadline = time.time() + timeout
    while time.time() < deadline and main_hwnd() is not None:
        time.sleep(1.0)
        if _dismiss_blocking_windows():
            # Something (a save prompt, an "Opened document information"
            # notice) was in the way; it may have swallowed the close, so
            # re-post it and refresh the patience.
            current = main_hwnd()
            if current is not None:
                win32gui.PostMessage(current, win32con.WM_CLOSE, 0, 0)
            deadline = max(deadline, time.time() + 20)
    if main_hwnd() is not None:
        log("graceful quit timed out; taskkill fallback")
        try:
            subprocess.run(["taskkill", "/IM", "Affinity.exe", "/F"],
                           capture_output=True, timeout=30)
        except Exception:
            pass
        deadline = time.time() + 10
        while time.time() < deadline and main_hwnd() is not None:
            time.sleep(1.0)


def _dismiss_blocking_windows() -> bool:
    """Press the dismiss button on any Affinity window that blocks a quit: a
    per-document save prompt ("Don't Save"/"No") or the modeless "Opened
    document information" notice ("Close") that some PSDs raise on open. WPF,
    driven by UIA patterns; returns True if anything was dismissed."""
    try:
        from pywinauto import Desktop

        for window in Desktop(backend="uia").windows():
            try:
                if "HwndWrapper[Affinity" not in (window.class_name() or ""):
                    continue
                title = (window.window_text() or "").strip()
                if title == "Affinity":
                    continue  # the main window: nothing to dismiss there
                # Dismissal priority: decline saving first, then plain
                # closes/acknowledgements (open-failure dialogs and the
                # "Opened document information" notice both count - we are
                # quitting, the only goal is to unblock the close).
                priority = ["Don't Save", "Don&apos;t Save", "No", "Close", "OK"]
                buttons = {}
                for button in window.descendants(control_type="Button"):
                    label = (button.window_text() or "").strip()
                    if label in priority and label not in buttons:
                        buttons[label] = button
                for label in priority:
                    if label not in buttons:
                        continue
                    button = buttons[label]
                    try:
                        button.invoke()
                    except Exception:
                        from pywinauto.uia_defines import get_elem_interface

                        get_elem_interface(button.element_info.element,
                                           "LegacyIAccessible").DoDefaultAction()
                    return True
            except Exception:
                continue
    except Exception:
        pass
    return False


def js_string(value: str | Path) -> str:
    """A safely quoted JS string literal (forward slashes keep WPF happy)."""
    return json.dumps(str(value).replace("\\", "/"))
