# Testy: the PSD compatibility benchmark

Testy (`testy/`) measures how Patchy and other installed editors handle real PSD files,
with Adobe Photoshop 2026 as ground truth. Repeated runs over time show whether Patchy's
compatibility is improving and which PSDs are trouble.

## Setup

Machine-specific settings live in `testy/config.local.json` (gitignored): copy
`testy/config.example.json` and fill in the python path (3.11+ with
`testy/requirements.txt` installed), dashboard port, the default corpus (a
`corpus_file` list or a `corpus_dir` to scan - keep personal file lists out of the
repo), optional explicit editor paths (standard install locations are discovered
automatically), and the optional `build_command` that refreshes the Patchy release
build before runs (without one, runs measure the existing patchy.exe as-is).

## Running it

Double-click `testy\start-testy.bat`: it kills any stale Testy processes, starts the
dashboard server on the configured port, and opens the control panel in the browser.
The panel's "New run" box is pre-filled with the configured default corpus (one
absolute path per line) and takes any pasted .psd list instead; pick editors and
options and hit Start - the run appears at the top of the runs table (clickable while
live), and the Start button stays disabled until it finishes. Browser-started runs
reuse the panel's server (`--server-url` under the hood) and log to
`testy/runs/last-child-run.log`. A Cancel button (shown while a run is live) kills the
whole run process tree and marks the run "canceled".

A Pause button (panel and live report) checkpoints big runs instead of killing them:
the orchestrator finishes the current file/editor cell (a slow Photoshop probe can take
a couple of minutes; interrupting mid-cell would trip the drivers' watchdogs), records
`state: "paused"` in status.json, and the run process exits. Everything finished so far
is kept; nothing is re-measured on resume. The paused state lives entirely in the run
directory, so it survives server restarts and reboots and frees Photoshop/RAM while
paused. Resume (panel, or the run's report page) spawns
`python testy\testy.py --resume runs\<ts>`, which reconstructs the corpus, editors, and
options from the run's own status.json, skips complete files, re-runs only
pending/interrupted cells (a cell the process died inside is reset to pending), and
finishes normally (results.json, history line, flagged.txt). Resume never rebuilds
Patchy mid-run; a changed git hash is logged into `run.notes`, not blocked. The
end-of-run source-integrity check compares against the sha1 recorded at first staging,
so corpus edits made while the run sat paused are still caught. Crash/kill-interrupted
runs (status stuck "running" with no process) are offered for resume the same way;
canceled runs can be resumed too, but only from their own report page's Resume button.
Failures are terminal: resume never retries a failed or breaker-skipped cell (start a
fresh run for that; caches make it cheap). Pausing a CLI-started run works via the same
mechanism, but since that process owns the dashboard, the dashboard exits with it; the
resume command is printed on the way out. The Pause request itself is just a
`pause.flag` file dropped in the run directory, so scripts can pause a run the same
way.

The panel's runs table has a checkbox per run (plus a select-all box in the header)
and a Delete button for clearing out old runs. Deletion follows the same conservative
rules as scan-mode scrubbing: only the exact artifact file names Testy itself writes
are removed, one by one, and directories go through plain `rmdir`, so anything
unexpected inside a run directory survives and is reported in the panel instead of
deleted. Deleted (and partly deleted) runs are also dropped from `runs/index.jsonl`
and `runs/history.jsonl`; selecting a run whose directory was already removed by hand
simply unlists it. The live run cannot be deleted (its checkbox is disabled and the
server refuses); global caches under `testy/cache/` are never touched by run deletion.

The CLI remains for scripted use:

```powershell
python testy\testy.py [--files a.psd b.psd] [--corpus list.txt] [--editors ...]
```

A default run goes through Photoshop, Patchy, Krita, and Photopea, refreshes the
Patchy release build first (when configured), serves a live dashboard (auto-opens the
browser), and leaves the frozen report + `results.json` in `testy/runs/<timestamp>/`.
The server root (`http://127.0.0.1:<port>/`) is the same control panel. In every
report, clicking a file name (matrix or detail panel) copies its full path to the
clipboard, and clicking any thumbnail opens the full-size image; a Back link in the
header returns to the control panel (shown only when the page is served, since a
frozen report opened from disk has no panel to go back to). The detail panel labels
each image with the editor's name, and its left edge can be dragged to resize the
panel (the width sticks; double-click the divider to reset). Lost native data is
called out prominently: matrix cells get a red "lost: 5/5 text layers, 5/5 live
effects" line (and a warn dot), and the detail panel's native-preservation banner
separates objects GONE from the resaved file from ones still present but converted
to a different kind (e.g. text rasterized); attribute-only losses (effects, masks,
blend modes stripped from surviving layers) are labeled as such. A resave Photoshop
refuses to open shows a "resave rejected" banner instead of a broken panel.

Useful flags:

- `--files a.psd b.psd` - explicit file list instead of the corpus.
- `--corpus <file>` - another corpus list (one path per line, relative to the repo root).
- `--editors photoshop,patchy,krita,photopea,affinity` - which columns to run. Affinity is
  opt-in: its column needs the app's connector enabled once in Affinity's settings (it
  serves the local MCP endpoint the scripting rides on); with it off, Affinity cells fail
  with an actionable message and everything else runs normally. Aseprite was verified to
  have no PSD I/O at all and is not part of the roster.
- `--no-build` - skip the release build refresh (measures the current patchy.exe as-is).
- `--fresh` - ignore cached ground truth / cells (cache lives in `testy/cache/`, keyed by
  file hash + editor version, and by Patchy git hash for the Patchy column).
- `--resume runs\<ts>` - continue a paused/canceled/interrupted run directory, skipping
  completed work (see above; implies `--no-build`, ignores `--files/--corpus/--editors`).
- `--scan [PCT]` - scan mode; see below.
- `--exit-when-done`, `--no-browser`, `--no-serve`, `--port N` - dashboard behavior.
- `--suffix "~TESTY~"` - the marker string used by the forced text re-render test.

## Scan mode

`--scan` (or the control panel's "scan: keep only flagged" checkbox) turns a run into a
triage pass over a big file list: each file is FLAGGED if anything failed (ground truth,
open, resave, trap, text mutation, a skipped/broken editor, a resave Photoshop rejects,
a trap sentinel hit) or if any editor's render differs from Photoshop's on more than the
threshold fraction of pixels (`renderMetrics.badFraction`; default 10%, `--scan 25` for
25%). Flagged files keep all artifacts as usual. Files that pass keep their metrics in
the report and `results.json`, but their images, resaves, and staged copies are deleted
from the run directory so large scans do not fill the disk, and their cells stay out of
`testy/cache/`. Deletion is deliberately conservative: only the exact artifact file
names Testy itself writes are removed, one by one, and directories are removed with
plain `rmdir`, so anything unexpected in a run directory survives and is logged instead.

The run directory also gets `flagged.txt`: one absolute path per flagged file with the
reasons as `#` comments. It is a valid corpus list, so a follow-up deep run is
`python testy\testy.py --corpus testy\runs\<ts>\flagged.txt`.

Photoshop ground-truth results (including renders) are still cached in `testy/cache/`
for every file, flagged or not: they are keyed by file hash + Photoshop version, so a
re-scan after a Patchy fix skips the slow Photoshop leg entirely. Clear `testy/cache/`
if that space ever matters more than re-scan speed.

A paused scan resumes normally: files already given their verdict are not re-scrubbed
or re-flagged, and `flagged.txt` is written once at true completion. The one loss is
benign: cells of a partially-finished file that completed just before the pause are
kept in the report but not cell-cached (their deferred cache entries died with the
process).

Python dependencies are listed in `testy/requirements.txt` (pywin32, Pillow, numpy,
selenium, pywinauto).

## What each cell measures

For every (PSD, editor) pair, the editor opens a staged COPY (corpus files are never
touched; a SHA check at the end of every run proves it), and Testy records:

- **Opens** - did the file load at all.
- **Render accuracy** - the editor's flattened PNG vs Photoshop's, composited over white
  at document size: RMSE, % pixels off by more than 6/255 (AA tolerance), and a
  per-object breakdown using ground-truth layer bounds. An object "renders ok" while
  under 25% of its region's pixels are off: text layers legitimately differ on every
  glyph edge (10-20% of their bbox) even when correct, so the budget is tuned to catch
  missing/misplaced/wrong objects (which blow past 50%), not anti-aliasing jitter. A
  layer's bbox also contains whatever renders behind it, so overlapping errors can
  count against more than one object. Worst offenders are named in the detail panel.
- **Honest rendering (trap)** - the editor also opens a byte-patched variant whose
  embedded flat composite is replaced with magenta (`psd_sections.py` rewrites only the
  trailing image-data section; all layer data stays byte-identical). An editor whose
  render shows magenta was displaying Photoshop's baked composite instead of compositing
  layers itself.
- **Native preservation** - the editor's re-saved PSD is reopened in Photoshop and its
  layer manifest is compared against the original's: text still `TEXT`, each adjustment
  still its exact kind, smart objects still smart, groups/masks/vector masks/live
  effects/clipping/blend modes intact. This is the "23/40 objects survived" number; a
  resave Photoshop refuses to open scores as rejected.
- **Round-trip render** - Photoshop's render of the editor's resave vs the original's
  render (what the file looks like when it comes back).
- **Forced text re-render** - editors that can be scripted append `~TESTY~` to every text
  layer so cached rasters cannot satisfy the render: Photoshop via COM
  (`textItem.contents`), Patchy via `patchy.exe --append-text` (real inline-editor
  sessions per layer). The mutated renders are compared within text-layer regions.
  Krita 5.3 and Affinity re-render text on open by design. Photopea's mutation pass is
  deliberately disabled: its script engine hangs on contents assignment for some
  documents and its DOM never matched text layers reliably.

The Photoshop column doubles as a control: it should sit at ~100% render accuracy and
full native preservation, which validates the pipeline itself.

## Machine specifics (July 2026)

- Photoshop 2026 via COM (`Photoshop.Application`); techniques per docs/ps-compat.md.
  The driver opens each file once per probe: manifest walk (DOM + ActionManager by layer
  id), duplicate-flatten-save render (copy-merged fallback for damaged files), optional
  save-as-copy resave, optional text mutation + second render.
- Krita 5.3.2 headless CLI: `krita.com <in> --export --export-filename <out>` (format by
  extension; PSD export works). Its console shim prints nothing through pipes; success is
  exit code + output existence (Fontconfig warnings are filtered out of reported errors).
- Photopea (web) runs in a headless Chrome via selenium: `testy/photopea_host.html`
  iframes photopea.com and drives it through the official postMessage API. The host page
  fetches the staged PSD same-origin and posts the bytes as an ArrayBuffer (Photopea's
  https iframe cannot fetch plain-http local URLs itself - a files entry in the hash
  config hangs at "Loading" forever), runs `saveToOE("png"/"psd")`, and POSTs each
  ArrayBuffer back to the server's `/testy-upload` endpoint (uploads are path-confined to
  `runs/`; the server also sends CORS headers). Needs internet; selenium manager fetches
  chromedriver on first use.
- Photoshop self-heals from engine wedges (verified July 2026): during a long session
  Photoshop's scripting engine wedges into a state where EVERY `app.open` returns error
  8000 ("open options are incorrect") regardless of the file - a control file that
  opened fine minutes earlier fails identically once wedged, and opens again after a
  restart. So on any probe failure the driver fully restarts Photoshop (Quit + taskkill
  + relaunch) and retries once; a wedge costs one ~35s restart rather than failing every
  remaining file. A hang watchdog force-kills Photoshop if a single script blocks past
  120s (a stuck modal), which unblocks the COM call. Failed cells and cells scored
  without ground truth are never cached, so re-runs retry them.
- A file that fails scripted open even on a freshly restarted engine (with a passing
  control immediately before) is genuinely bad, not a wedge. The one such corpus file,
  `akiko_cycling_okinawa_with_filters.psd`, was confirmed bad in the Photoshop UI and
  deleted (July 2026); the `smart_objects_warp` core test that used it now [SKIP]s on
  the missing fixture.
- Runs fail fast: the Photopea driver aborts when the host page's step log stalls for
  45s, and the orchestrator trips a per-editor circuit breaker after 3 consecutive
  failed cells (remaining cells report "skipped" instead of burning timeouts).
- Affinity (Canva unified app 3.2+) is driven through its built-in JavaScript SDK,
  not UI automation (the background-UIA quick-export driver was retired July 2026):
  the app serves a local MCP endpoint (plain JSON-RPC over SSE on [::1]:6767, IPv6
  loopback ONLY) while its connector is enabled in settings, and
  `testy/affinity_js.py` speaks it directly with the standard library - no AI, no
  tokens. One execute_script call per document runs Document.load plus doc.export
  for both legs (PNG render, then the "PSD (preserve editability)" preset - preset
  names resolve by enumeration with a prefix fallback in case a version renames
  them). Typical cell: under 3s even for a 40 MB PSD, where the UIA driver needed
  minutes (50s relaunch cooldowns, 8s+ popup materialization, shell Save As
  automation). The "preserve editability" resave also outscores the old
  quick-export default on native preservation by a wide margin.
- Affinity JS constraints (verified on 3.2.3.4646): the server demands MCP protocol
  "2025-11-25" and a per-session read of its "preamble" documentation topic before
  execute_script works (affinity_js handles both); scripts may only touch paths
  under the Desktop (PERMISSION_DENIED elsewhere), so inputs stage through
  `Desktop/testy-affinity-work/` and outputs move back to the run dir; script
  output is console.log only, so cell scripts end with an `@@RESULT {json}` line.
  A cold-started app accepts MCP connections before it is ready and then RESETS
  them, so connecting retries until a session survives a prime-pause-ping sequence.
  NOT_ALLOWED errors mean the user restricted scripting/filesystem access in the
  app's settings; a load refusal (INAPPROPRIATE_FILE_TYPE_OR_FORMAT) is Affinity's
  own import rejecting the file and scores honestly as opens=fail
  (vectors_overlay_stroke.psd is such a file - the UI refuses it too).
- Affinity lifecycle: Document.close is NOT_IMPLEMENTED on Windows, so opened
  documents pile up as tabs; an instance the driver launched restarts after 10
  documents and is quit at cleanup() via WM_CLOSE while UIA-dismissing whatever
  blocks it (per-document save prompts, the modeless "Opened document information"
  notice, open-failure dialogs) - the one place pywinauto remains. A force-killed
  MSIX instance leaves a zombie single-instance registration, so taskkill stays the
  last resort. A pre-existing user instance is reused but never quit (its tabs stay
  open; the driver notes it). The trap leg stays skipped: Affinity re-renders
  layers by design, so the baked-composite trap proves nothing. Partial cells (a
  failed PSD leg) are never cached.

## Layout

```
testy/
  testy.py           orchestrator + dashboard server
  config.py          editor discovery + versions
  staging.py         run-dir copies + trap generation
  psd_sections.py    minimal PSD/PSB section walker (trap patching only)
  analyze.py         render metrics, sentinel detection, heatmaps
  manifest.py        original-vs-resave structural diff
  report.py          status.json + live report.html + history
  affinity_js.py     token-free MCP/JS client for the Affinity app (SSE + JSON-RPC,
                     launch/quit lifecycle; also reused by .af format tooling)
  drivers/           photoshop (COM), patchy (CLI), krita (CLI),
                     photopea (headless Chrome + embed API), affinity (built-in JS
                     automation via affinity_js)
  index.html         run-index landing page (server root)
  photopea_host.html the Photopea embedding/automation page
  corpus/default.txt curated corpus
  runs/<ts>/         gitignored: artifacts, results.json, report.html
  cache/             gitignored: ground-truth + cell cache
```

`testy/runs/history.jsonl` accumulates one summary line per run; the report's "Past runs"
table reads it for the over-time view.

## Patchy CLI automation (product side)

Testy drives Patchy through product flags added for it (src/app/main.cpp):
`patchy.exe <in> --export <out>` opens a file, saves it to `<out>` (format by extension)
and exits unattended (single-instance opt-out, prompts suppressed, recents/prefs
untouched); `--append-text <s>` first appends `<s>` to every text layer through real
editor sessions so rasters re-render through the text pipeline. Pinned by the
`ui_cli_append_text_rerenders_and_roundtrips` visual test.
