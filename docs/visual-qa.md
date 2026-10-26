# Native visual QA and app-driving commands

Split from [testing.md](testing.md) (suite organization, filtering, offscreen fonts and traps). Read before capturing Patchy windows, driving the app from the command line, or scripting screenshots. The automation permissions are in `AGENTS.md`.

The permission rule is in AGENTS.md: automation confined to Patchy and its hosted
plug-in windows (KPT included) is authorized; desktop screenshots (even cropped),
global input (`SendInput`, cursor moves), Computer Use, and other applications need
explicit authorization in the current request, and an MCP wrapper does not change
that. Drive Patchy through its scripting, CLI, MCP, or messages targeted to its own
windows; `patchy.ui.captureWindow`, `QWidget::grab`, and the plug-in helper's
`PrintWindow` capture only the intended windows.

Keep unattended runs in task-owned instances with isolated settings, offscreen where
possible. Permission for Patchy input never covers discarding unsaved work, closing
the user's running app/connector, or stopping unrelated processes.

`patchy-mcp --attach` connects to an already-running interactive Patchy; use it
only when authorized to control that workspace. For automated attachment tests,
launch a test-owned app with `QT_QPA_PLATFORM=offscreen`, isolated
`PATCHY_SETTINGS_DIR`, and a unique `PATCHY_MCP_ENDPOINT` shared with the proxy.
Do not pass `--headless`: it deliberately disables attachment. `ui_mcp` covers
state guards, input locking, cancellation, reconnect, and unsaved history.

For persistent background editing use `patchy-mcp`, which owns an isolated
offscreen workspace. When a task needs its own visible Patchy workspace,
`patchy-mcp --visible` runs the same protocol visibly under the rules above.
`patchy-mcp --check` validates native strokes, previews, and
the assembled control kit from its installed location. See [ai-control.md](ai-control.md).
The UI filter `ui_script_automation` covers native stroke parity, pressure,
selection, palette snapping, history, stale IDs, and Unicode preview output.

The standard-client integration test uses a development-only Python 3.10+
environment:

```powershell
python -m venv .deps/mcp-client
.deps/mcp-client/Scripts/python -m pip install 'mcp>=1.26,<2'
cmd /s /c 'scripts\run-throttled.bat .deps\mcp-client\Scripts\python.exe tests\mcp_client_tests.py build\release\patchy-mcp.exe'
```

Run from the repository root; artifacts stay under `test-artifacts/mcp`. The test
uses only owned offscreen processes and also accepts a staged package's connector
(resource discovery without source-relative paths). The shipped connector needs no Python.
Mac host: `.deps/mcp-client-py312/bin/python` (system Python 3.9 cannot install the MCP
dependency); linux host: `.deps/mcp-client/bin/python`; run
`nice -n 10 <python> tests/mcp_client_tests.py <connector>` from the remote repository root.
The suite covers owned and attached workspaces, competing clients, cancellation,
disconnect cleanup, app restart, stale tokens, and shared history; Unix crash
simulations remove their own sockets.
Pass `--attachment-recovery-only` after the connector path to reproduce offline
discovery, late app startup, restart with fresh state tokens, and interruption
without replay. This test owns its offscreen apps and requires no prior artifacts.
The client suite also checks `--visible` option handling and mode/preview metadata on an
explicit offscreen backend. A real visible smoke test may use a task-owned Patchy instance
under the rules above.
On Linux it also holds a fake desktop bus open without answering and verifies that
headless scripting and an immediately closed MCP client still exit promptly.
After installing the current Flatpak user bundle, run `nice -n 10 .deps/mcp-client/bin/python
tests/flatpak_mcp_tests.py` from the host. This launches the app and connector in
separate sandboxes with the default endpoint, verifies an owned document before
editing, and checks preview and unsaved reconnect. Close any existing Patchy Flatpak first; the test refuses
to run alongside it and removes only its own processes and runtime socket.
The host session must set `XDG_RUNTIME_DIR`; the test forwards it through the MCP
SDK's filtered subprocess environment so both launchers use the same runtime mount.
Pass `--recent-history-only` after the connector path to check shared history
with owned headless and MCP processes. The UI filters `ui_unicode_recent_history`
and `ui_vector_preview_action_persistence` cover history merging/live refresh and
the restored preview preference's menu/Preferences synchronization.

`patchy.exe --screenshot <out.png>` captures the running instance without raising or focusing it; `--screenshot-widget <qtObjectName>` and/or `--screenshot-rect x,y,w,h` narrow it; positional files open a document. The invoker exits at once: poll for the output. With no instance running, Patchy opens, waits about 1.5 s, captures and exits 0 (3 on failure). Add `--language <code>` (with `--headless`, so no running instance is reused) to capture a UI language without changing the saved preference ([localization.md](localization.md)). Never run `patchy.exe --help` or `--version` during verification: the Windows GUI build has no console, so Qt shows them in a message box over whatever Seth is doing.

`patchy.exe --stress-test[=quick|small|standard|huge] [--stress-report-dir <dir>] [--stress-depth 16|32]` builds the deterministic performance scene (16 or 32 bits optional) and exits. Reports default to `%LOCALAPPDATA%\RTsoft\Patchy\stress-reports\` (`stress-latest.json`). Quick (1024 px) is for iteration, standard (4096 px) for full-scale measurements; real timings need a real screen ([performance.md](performance.md)).

`patchy.exe --headless ...` runs any of those modes on Qt's offscreen platform. It never forwards to a running instance, so the exit code and `--script-output` file are the run's own; prompts are suppressed and sound muted. Leave it off when a capture must show the real platform and installed fonts (offscreen sees only bundled fonts; a Windows headless run loads installed fonts from the registry on the first text request).

Useful diagnostic variables:

- `PATCHY_NO_SINGLE_INSTANCE=1` allows multiple instances.
- `PATCHY_FAKE_SCANNER_FILE=<path>` bypasses native scanner acquisition in tests.
- `PATCHY_REV_TRACE=1` logs revision bumps.
- `PATCHY_ZOOM_TRACE=1` logs paint and zoom phases over 2 ms.
- `PATCHY_STYLE_MASK_CACHE_OFF=1` disables the style-mask cache.
- `PATCHY_RENDER_SINGLE_THREADED=1` forces byte-stable sequential rendering.
- `PATCHY_RENDER_THREADS=<n>` caps every parallel fan-out at n workers in place of the hardware thread count (perf harness emulation of a low-core machine; the transform proxy gate scales with it).
- `PATCHY_PROCESSING_OVERLAY_MIN_PIXELS` overrides the processing-overlay threshold.
- `PATCHY_NO_SOUND=1` suppresses script audio; offscreen suites rely on it.
- `PATCHY_UPDATE_MANIFEST_URL=<url>` replaces the update check's manifest URL (a loopback server in the update-check tests). An unresolvable host such as `https://x.local/m.json` holds a resolver call open for seconds and reproduced the issue 48 macOS quit freeze (quit in that window and time the exit). `PATCHY_NO_UPDATE_CHECK=1` removes the check, like a `PATCHY_STORE_BUILD` build.
- `PATCHY_SETTINGS_DIR=<dir>` redirects the app's ini settings store (automation isolation).
- `PATCHY_RECOVERY_DIR=<dir>` redirects the automatic document recovery store (the UI suite uses `test-artifacts/recovery`); `PATCHY_RECOVERY_INTERVAL_MS=<ms>` overrides the timer interval ([document-recovery.md](document-recovery.md)). `PATCHY_USER_FONTS_DIR=<dir>` moves the font store ([fonts.md](fonts.md)); `PATCHY_USER_SCRIPTS_DIR=<dir>` the user scripts folder (the UI suite uses `test-artifacts/user-scripts/<pid>`).
- `PATCHY_HEADLESS=1` marks a `--headless` app run (main.cpp sets it). Never set it for the suites: it re-enables the registry font rescue and makes layouts machine-dependent.
- `QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES=1` makes `patchy.exe --help` and command-line parse errors print to the redirected stdout/stderr instead of a modal Win32 message box (patchy.exe is a GUI-subsystem binary), which is what an automated `--help` check needs.
- `PATCHY_UI_PROFILE=1` prints stderr timing lines for instrumented UI stages (layer-panel rebuild phases, layer-style dialog open/close, undo snapshots).
- `PATCHY_PERF_SAMPLER=1` (patchy_perf_tests only) samples the main thread's stacks every 10 ms and prints the hottest ones at exit.

Composite checksums from stress reports or large renders are comparable only on the same machine: text antialiasing varies by system and the parallel strip renderer varies with thread count.

Committed PSD corpus fixtures must decode; unreadable committed files fail the
corpus test. Optional local files may still skip. The real-photo HEIC sweep skips
only a recognized unavailable-codec/backend condition; decoding or assertion
failures with an available decoder fail the test.
