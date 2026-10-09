# Testy: the PSD compatibility benchmark

Testy (`testy/`) measures PSD compatibility against Adobe Photoshop 2026: whether each editor opens a file, how closely its render matches Photoshop's, whether it composites layers itself, what native data and bit depth survive its PSD save, and for 16-bit files how precise its render is. What each measurement means is in [testy-scoring.md](testy-scoring.md); the public write-up of the latest published run is [psd-compatibility-benchmark.md](psd-compatibility-benchmark.md).

## Setup

Machine-specific settings live in `testy/config.local.json` (gitignored): copy
`testy/config.example.json` and fill in the python path (3.11+ with
`testy/requirements.txt` installed: pywin32, Pillow, numpy, selenium, pywinauto), the
dashboard port, the default corpus (a `corpus_file` list or a `corpus_dir` to scan;
keep personal file lists out of the repo), optional explicit editor paths (standard
install locations are discovered automatically), and the optional `build_command` that
refreshes the Patchy release build before runs (without one, runs measure the existing
patchy.exe as-is).

## Running it

Double-click `testy\start-testy.bat`: it kills stale Testy processes, starts the
dashboard server, and opens the control panel in the browser. The "New run" box is
pre-filled with the default corpus (one absolute path per line) and takes any pasted
.psd list; pick editors and options and hit Start (disabled while a run is live).
Missing or non-.psd/.psb paths never block a start: they are dropped with an amber
"skipped N unusable path(s)" warning (full list in the Testy console). Real errors
still stop the start and stay red: nothing usable left, no editors, a bad threshold.
Browser-started runs reuse the panel's server (`--server-url` under the hood), log to
`testy/runs/last-child-run.log`, and hand the file list to the child through
`testy/runs/last-child-corpus.txt` (`--corpus`), never argv (a big pasted corpus
overflows the Windows 32K command-line limit, WinError 206). Endpoint errors come
back as JSON 500s, not dropped connections. A Cancel button kills the run's
process tree and marks it "canceled".

**Shut down Testy** (top right) and console Ctrl+C finish the current step,
checkpoint an unfinished run, wait for the benchmark child to exit, then close the
server and release its port. The panel shows progress, then goes offline. Restart
Testy and Resume to continue. Shutdown blocks new runs but allows the finishing
cell's uploads; it does not force-close editors. A build or editor call already in
progress must finish first. Use these controls instead of closing the console
window or killing Python. CLI-owned dashboards also shut down this way.

A Pause button (panel and live report) checkpoints big runs instead of killing them:
the orchestrator finishes the current file/editor cell (interrupting mid-cell would
trip the drivers' watchdogs), records `state: "paused"` in status.json, and exits.
Nothing is re-measured on resume; the paused state lives in the run directory, so it
survives server restarts and frees Photoshop/RAM. Resume (panel or report page)
spawns `python testy\testy.py --resume runs\<ts>`, which reconstructs corpus,
editors, and options from status.json, skips complete files, re-runs only
pending/interrupted cells (a cell the process died inside resets to pending), and
finishes normally (results.json, history line, flagged.txt). Resume never rebuilds
Patchy mid-run; a changed git hash is logged into `run.notes`. The end-of-run
source-integrity check compares against the sha1 recorded at first staging, so corpus
edits made while paused are still caught. Crash-interrupted runs (status stuck
"running", no process) offer resume too; canceled runs resume only from their own
report page. Failures are terminal: resume never retries a failed or breaker-skipped
cell (start a fresh run; caches make it cheap). status.json updates are
collision-safe on Windows (retried os.replace swap; non-terminal push failures are
logged and skipped; the server serves it from memory because an open reader makes
the swap fail with a sharing error). A CLI-started run pauses the same way,
but it owns the dashboard, which exits with it (the resume command is printed). The
Pause request is just a `pause.flag` file in the run directory, so scripts can pause
too.

The runs table has per-run checkboxes and a Delete button. Deletion follows the same
conservative artifact-scrub rules as scan mode (below), so anything unexpected in a
run directory survives and is reported in the panel. Deleted runs are dropped from
`runs/index.jsonl` and `runs/history.jsonl`; selecting a run whose directory was
removed by hand simply unlists it. The live run cannot be deleted, and deletion
never touches `testy/cache/`.

A **Rerun** button beside each image in a completed batch refreshes that row and
the batch totals. Choose **Patchy** (default) to check a fix, or **All editors**.
The child rebuilds Patchy when selected and measures the selected editors afresh,
including Photoshop ground truth. The batch keeps showing its previous results
until the rerun finishes. Interrupted runs, failed automation, changed sources,
and failed builds leave the previous results intact. A source modified since the
batch requires a new run. Other rows and unselected editor cells are preserved.

Successful reruns update `status.json`, `results.json`, history totals, and scan
flags. New artifacts and the previous batch snapshot live under
`runs/<batch>/reruns/<child>/`; the row links to the previous results. Refreshed
rows show their build/version and time, and the header identifies the batch as a
partial refresh. These batches contain measurements from multiple builds.
Existing batches get current controls when served by an updated Testy server;
restart the server after updating Testy. Frozen reports have no rerun controls.
Editor-free regressions: run `tests\testy_rerun_tests.py` and
`tests\testy_shutdown_tests.py` through `scripts\run-throttled.bat python`.

**Retest file** in the detail panel creates a separate one-file run, reusing
caches and refreshing Patchy's build. Both controls require an idle Testy server.

The CLI remains for scripted use:

```powershell
python testy\testy.py [--files a.psd b.psd] [--corpus list.txt] [--editors ...]
```

A default run goes through Photoshop, Patchy, Krita, GIMP, PhotoDemon, and Photopea,
refreshes the Patchy release build first (when configured), serves a live dashboard,
and leaves the frozen report + `results.json` in `testy/runs/<timestamp>/`. The
server root is the same control panel. Clicking a file
name in a report copies its full path to the clipboard; clicking a thumbnail opens
the full-size image. The matrix header stays pinned while the page scrolls.
Lost native data is called out: matrix cells get a red "lost: ..." line and a warn
dot, and the detail panel's native-preservation banner separates objects GONE from
the resaved file from ones converted to a different kind (e.g. text rasterized);
attribute-only losses (effects, masks, blend modes stripped) are labeled as such. A
resave Photoshop refuses to open shows a "resave rejected" banner instead of a
broken panel.

Each file's row shows document size, layer count, and file size (shown even when
ground truth failed); the header totals the corpus, and a scan run's card adds a
bytes done/total row. A cell's status line
qualifies "opened": a render missing the scan threshold (10% default, by the run's
comparison mode) reads "opened - poor matching" with a yellow dot; a resave
Photoshop cannot reopen reads "opened - saves corrupted .psd" with a red dot, as
does a render wrong on more than 30% of pixels. Each editor's summary card rolls
rejected resaves up as a "bad .psd saves" count, red when nonzero (also in the CLI
summary and history.jsonl as `badSaves`).

Useful flags:

- `--files a.psd b.psd` - explicit file list instead of the corpus.
- `--corpus <file>` - corpus list (one path per line, relative to the repo root).
- `--editors photoshop,patchy,krita,gimp,photodemon,photopea,affinity` - which columns
  to run. Affinity is opt-in: enable the app's connector once in Affinity's settings
  (it serves the local MCP endpoint); with it off, Affinity
  cells fail with an actionable message and the rest runs. `psdtools` (opt-in) is
  the psd-tools library: its compositor and a load-and-save (`pip install "psd-tools[composite]"`).
- `--no-build` - skip the release build refresh (measures the current patchy.exe).
- `--fresh` - ignore cached ground truth / cells (cache in `testy/cache/`, keyed by
  file hash + editor version; Patchy's key is a hash of patchy.exe itself).
- `--resume runs\<ts>` - continue a paused/canceled/interrupted run directory, skipping
  completed work (implies `--no-build`, ignores `--files/--corpus/--editors`).
- `--scan [PCT]` - scan mode; see below.
- `--compare strict|perceptual` - which comparison drives scan flagging (default
  perceptual). Both numbers are always computed and shown either way; a resumed run
  keeps the mode it started with, and runs recorded without a mode flag strictly.
- `--exit-when-done`, `--no-browser`, `--no-serve`, `--port N` - dashboard behavior.
- `--suffix "~TESTY~"` - a marker that is only part of cache entry names (the argparse help
  text still describes the retired appended-text leg).

## The psd-tools collection and the By folder table

`python testy\fetch_psd_tools_corpus.py` checks out psd-tools' `tests/psd_files`
(309 single-feature PSD/PSB files, GitHub issue 65) at a pinned commit
into `local-test-fixtures/psd-tools` and writes `testy/corpus/psd-tools.txt`; run
it with `--corpus corpus\psd-tools.txt` (relative to `testy/`). A multi-folder corpus gets a "By folder" table above
the matrix (matches, data kept, bad saves per editor); clicking a
row filters it. The "Score without known limitations" checkbox drops
16/32-bit and artboard files (entry `traits`) from the totals. Files sharing a stem (`x.psd` beside `x.psb`) get distinct
artifact directories through the entry's `dir` key (`x~psb`). The core test
`psd_tools_corpus_reads_and_round_trips_if_available` covers the same files
without Photoshop.

## Publishing a run as a static site

`python testy\export_static.py <run name>` writes `testy/public/<run name>/` (gitignored):
`index.html` (a short overview with the editor versions), `report.html`, `status.json`,
`results.json` and every image the report links to. Copy that one folder to any web
host; it needs no server code. Off the Testy server the report hides its run controls,
stops polling once the run is finished, and its Back link goes to the folder's own
`index.html`. The export leaves out every .psd/.psb (the corpus is third-party work and
the report does not need the files) and the cache-free leg's working renders, turns
each file's source into its path below the corpus folder, replaces this machine's
folders inside error messages with `<run>`, `<patchy>` and `<home>`, and refuses to
write if a local path is still left. It only ever replaces a folder an earlier export
made (`testy-export.txt` marks it). Nothing is uploaded by the tool. Published runs live at
`rtsoft.com/testy/<YYYY-MM-DD>/` and are uploaded only on Seth's explicit go-ahead (the upload
target belongs in `agents_local.md`, never here): pack the folder (`tar --force-local -czf ...
--exclude=testy-export.txt .`, about 190 MB for 309 files), copy the one archive to the host,
compare sha256 there, untar, delete the archive; one transfer instead of 17,000 small files. The published report requests
`../history.jsonl` once and gets a harmless 404 (the history section stays empty).

## Scan mode

`--scan` (or the panel's "scan: keep only flagged" checkbox) turns a run into a
triage pass: a file is FLAGGED if anything failed (ground truth, open, resave, trap,
a skipped/broken editor, a resave Photoshop rejects, a trap sentinel
hit Photoshop's own trap render does not share) or if any editor's render differs
from Photoshop's on more than the threshold fraction of pixels (default 10%,
`--scan 25` for 25%). The fraction follows the run's comparison mode:
`renderMetrics.perceptual.badFraction` by default, raw over-6/255 byte differences
(`renderMetrics.badFraction`) with `--compare strict`. Flagged files keep all
artifacts. Passing files keep their metrics in the report and `results.json`, but
their images, resaves, and staged copies are deleted so large scans do not fill the
disk, and their cells stay out of `testy/cache/`. Deletion is deliberately
conservative: only the exact artifact file names Testy itself writes are removed,
one by one, and directories go through plain `rmdir`; anything unexpected survives
and is logged.

The run directory also gets `flagged.txt`: one absolute path per flagged file with
the reasons as `#` comments. It is a valid corpus list, so a follow-up deep run is
`python testy\testy.py --corpus testy\runs\<ts>\flagged.txt`.

Photoshop ground-truth results (including renders) are cached in `testy/cache/` for
every file, flagged or not, keyed by file hash + Photoshop version, so a re-scan
after a Patchy fix skips the slow Photoshop leg.

A paused scan resumes normally: files already given their verdict are not
re-scrubbed or re-flagged, and `flagged.txt` is written once at true completion.

## What each cell measures

Opens, render accuracy, the trap, data kept in the .psd save, the round-trip render,
and the cache-free leg that scores an editor on what it
draws itself: all in [testy-scoring.md](testy-scoring.md), with the reference-render
rules and the "never mark an editor down for the harness's mistake" safeguards.

## Editor drivers

- Photoshop 2026 via COM (`Photoshop.Application`); techniques per docs/ps-compat.md.
  The driver opens each file once per probe: manifest walk (DOM + ActionManager by
  layer id), duplicate-flatten-save render (copy-merged fallback for damaged files),
  optional save-as-copy resave.
- Krita 5.3.2 runs its own Python: `kritarunner.com -s testy_krita_export -f main <in>
  <out> <report>`, started in `drivers/krita_scripts/` (kritarunner replaces PYTHONPATH,
  so the module is found through the working directory). The script opens the file,
  polls the projection until two reads match, then exports (format by extension) and
  writes its verdict to the report file. The plain CLI (`krita.com <in> --export
  --export-filename <out>`) exports before fill and vector layers are drawn: the same
  PSD came out drawn on one run and blank on the next, which scored Krita far below
  what it does. It remains only as the fallback when the script leaves no verdict.
  Krita's font matching still varies between launches (Arial Black on one, a fallback
  face on the next), so a PNG render is made twice, a third time if those differ, and
  the most common picture is kept with a driver note on the cell.
- PhotoDemon runs as a locally patched build; the stock app has no automation
  surface (its command line only loads files into the GUI). The patch lives in a
  PhotoDemon checkout next to this repository (`../PhotoDemon`, BSD-licensed): a
  `Testy.bas` module plus a `FormMain` startup hook add
  `PhotoDemon.exe <in> /testy-export <out>`, which reuses PhotoDemon's own
  batch-processor machinery (MacroBATCH dialog suppression, `LoadFileAsNewImage`,
  `PhotoDemon_BatchSaveImage` with defaults, format by extension), writes a one-line
  phase report to `<out>.testy.txt` (the driver consumes and deletes it), and exits.
  The patch must NEVER be sent upstream: PhotoDemon has a strict no-LLM/no-AI
  contribution policy. Builds compile with twinBASIC (Community
  edition, 32-bit; unattended builds are not licensed, so rebuilding after a patch
  change is a manual click in its IDE), and the exe must sit at the checkout root
  next to the `App\` folder or PhotoDemon refuses to start. Editor discovery
  deliberately ignores stock install locations (a stock build would open its GUI and
  burn the cell timeout); only the sibling checkout or an explicit `photodemon` path
  in config.local.json is used. PhotoDemon imports every PSD layer as plain
  pixels (pdPSD.cls creates `PDL_Image` only and never reads `TySh`), so there is no
  text to mutate. CLI mode disables PhotoDemon's
  ExifTool plugin (Testy does not measure metadata). Related defenses: every CLI driver
  (PhotoDemon, Krita, GIMP) spawns its editor inside
  `drivers/winproc.suppressed_error_dialogs()` so Windows Error Reporting dialogs
  cannot hold a crashed editor open, and the PhotoDemon driver judges a leg by its
  sidecar plus the artifact rather than the exit code, so an export that completed
  before the process died scores ok (the crash stays as a driver note). Only a
  clean exit with a sidecar verdict counts as "the app refused this file" for the
  circuit breaker; a crash, hang, or launch failure counts against PhotoDemon
  itself.
- GIMP 3.2 headless Script-Fu batch: `gimp-console-3.exe -i -d -f
  --batch-interpreter plug-in-script-fu-eval -b <script> -b "(gimp-quit 0)"` (PNG
  render and PSD resave via `gimp-file-save`, format by extension). Two hard-won
  rules: GIMP 3 refuses batch work without an explicit `--batch-interpreter`, and a
  batch command that errors stops the console WITHOUT running the trailing
  `(gimp-quit 0)`, hanging forever, so the driver wraps the script body in
  Script-Fu's `catch` so the quit always runs. Success is judged by output
  existence; GIMP-Error lines naming the real cause reach stderr either way. A
  timeout kills the process tree via `taskkill /t` (batches execute in a separate
  script-fu plug-in process).
- Photopea (web) runs in a headless Chrome via selenium: `testy/photopea_host.html`
  iframes photopea.com and drives it through the official postMessage API. The host
  page fetches the staged PSD same-origin and posts the bytes as an ArrayBuffer
  (Photopea's https iframe cannot fetch plain-http local URLs; a files hash-config
  entry hangs forever), runs `saveToOE("png"/"psd")`, and POSTs each
  ArrayBuffer back to the server's `/testy-upload` endpoint (uploads path-confined
  to `runs/`; CORS headers sent). Needs internet; selenium manager fetches
  chromedriver on first use.
- A percent sign in a corpus file name is a hazard: ExtendScript's `new File(...)`
  URI-decodes its argument and the dashboard server unquotes request paths, so
  `%20` in a name became a space (Photoshop saw a missing file; Photopea's staged
  fetch 404'd). Each boundary encodes the path it hands over
  (`drivers/photoshop.py`'s `_js_path`, `drivers/photopea.py`'s `_file_url`,
  `report.py`'s `artUrl`); the `/testy-upload` `name` deliberately stays raw (it
  rides a query parameter, already decoded exactly once). A run directory inherits
  the corpus file's stem, so this reaches every artifact.
- Nuisance modal dialogs are answered from outside the COM call. `DialogModes.NO`
  does not reach every dialog: some files make Photoshop raise a modal alert from
  inside `app.open` (e.g. a PSD whose IPTC resource holds non-IPTC records), and
  the scripted call is already blocked when the alert appears. Save-time warnings
  (nested-layer-groups compatibility) are modal too, so the save leg can hang the
  same way. Every probe runs under a `testy/win_dialogs.py` DialogGuard, which
  polls Photoshop's windows from a side thread and clicks the acknowledging
  button. It is deliberately narrow: only owned windows or standard `#32770`
  dialogs with at most six push buttons (never an app's own frame), only real push
  buttons (a "Don't show again" checkbox is never ticked), only carry-on buttons
  (OK/Yes/Continue/Update/Close/Done/Proceed, never Cancel/No/Save/Discard), and
  only via messages posted to that dialog's own button, so nothing lands in
  another app. A dialog with no safe answer is left standing and named in the
  failure text, which turns "photoshop hung >120s" into the alert's actual
  wording. Dismissed alerts are recorded per file (`groundTruth.dialogs`), per
  cell (`cell.dialogs`), and per resave (`cell.resaveDialogs`), shown in the
  detail panel, and never flag a file in scan mode.
  `python testy\win_dialogs.py --selftest` exercises the guard against real Win32
  dialogs from a helper process; needs neither Photoshop nor a broken PSD.
- A file whose ground truth failed is not probed again for the Photoshop column:
  the cell inherits the ground-truth error. The same driver already retried it on
  a freshly restarted Photoshop; a second probe would fail identically and cost
  minutes and another restart.
- Photoshop self-heals from engine wedges: a long session can wedge the scripting
  engine so EVERY `app.open` returns error 8000 ("open options are incorrect")
  regardless of file, until a restart. On any probe failure the driver fully
  restarts Photoshop (Quit, wait, taskkill what remains, relaunch) and retries
  once. A hang watchdog force-kills Photoshop when
  a script blocks past 120s. Failed cells and cells scored without
  ground truth are never cached, so re-runs retry them.
- Never force-kill Photoshop while it is quitting: it saves preferences on the way
  out, and a kill inside that write truncates them, after which every launch dies
  at init with "unexpected end-of-file". The cure is deleting the zero-length
  prefs files (e.g. `Workspace Prefs.psp`) in
  `%APPDATA%\Adobe\Adobe Photoshop 2026\Adobe Photoshop 2026 Settings`; Photoshop
  rebuilds them. Do not delete the whole Settings folder (it also holds
  brush/style/pattern/action/shape libraries). `restart()` gives a clean quit
  `QUIT_GRACE_SECONDS` (30s) before forcing anything; only a refused quit or a
  process still up at the deadline is killed.
- A COM error saying the server never started (`CO_E_SERVER_EXEC_FAILURE`,
  `REGDB_E_CLASSNOTREG`) is a broken Photoshop, not a bad PSD, and is worded that
  way in the report, together with whatever alert the dialog guard cleared.
  After two consecutive such failures the driver reports
  itself unavailable and the run checkpoints exactly like a pause (ground truth is
  the baseline; nothing left to measure). Fix Photoshop and resume; those files'
  verdicts are handed back as pending so the resume measures them properly, and a
  resave Photoshop never got to open is not counted as "resave rejected".
  `python testy\drivers\photoshop.py --selftest` pins the classification, wording,
  give-up rule, and what a restart may kill; no Photoshop needed.
- A file that fails scripted open even on a freshly restarted engine (with a
  passing control immediately before) is genuinely bad, not a wedge.
- Runs fail fast: the Photopea driver aborts when the host page's step log stalls
  for 45s, and the orchestrator trips a per-editor circuit breaker after 3
  consecutive failed cells (remaining cells report "skipped"). Only failures OF
  THE EDITOR count: a cell where the editor ran and refused the file (Krita or
  Patchy exiting non-zero, Affinity answering INAPPROPRIATE_FILE_TYPE_OR_FORMAT, a
  file Photoshop itself will not open) proves the app alive and clears the count
  like a success; a hang, timeout, crash, or launch failure counts. Drivers say
  which by returning `fileRejected`, and anything unclassified counts, so a new
  failure mode never quietly disables the breaker. The breaker covers editor
  columns only; a Photoshop that stops launching ends the run instead (above).
- Affinity (Canva unified app 3.2+) is driven through its built-in JavaScript SDK,
  not UI automation: the app serves a local MCP endpoint (plain JSON-RPC over SSE
  on [::1]:6767, IPv6 loopback ONLY) while its connector is enabled in settings,
  and `testy/affinity_js.py` speaks it directly with the standard library (no AI,
  no tokens). One execute_script call per document runs Document.load plus
  doc.export for both legs (PNG render, then the "PSD (preserve editability)"
  preset; preset names resolve by enumeration with a prefix fallback).
- Affinity JS constraints (verified on 3.2.3.4646): the server demands MCP
  protocol "2025-11-25" and a per-session read of its "preamble" documentation
  topic before execute_script works (affinity_js handles both); scripts may only
  touch paths under the Desktop (PERMISSION_DENIED elsewhere), so inputs stage
  through `Desktop/testy-affinity-work/` and outputs move back to the run dir;
  script output is console.log only, so cell scripts end with an `@@RESULT {json}`
  line. A cold-started app accepts MCP connections before it is ready and then
  RESETS them, so connecting retries until a session survives a prime-pause-ping
  sequence. NOT_ALLOWED means the user restricted scripting/filesystem access in
  the app's settings; a load refusal (INAPPROPRIATE_FILE_TYPE_OR_FORMAT) is
  Affinity's own import rejecting the file and scores honestly as opens=fail.
- Affinity staging I/O rides through `_retry_locked`, which waits out transient
  Windows sharing violations (up to ~2s) on every staged-file unlink, copy, and
  move: Affinity can briefly hold a just-loaded document's handle and antivirus
  scans grab fresh Desktop copies. Cleanup is non-fatal; a stubbornly locked file is left
  for the next cell's staging retry or cleanup()'s rmtree.
- Affinity lifecycle: Document.close is NOT_IMPLEMENTED on Windows, so opened
  documents pile up as tabs; an instance the driver launched restarts after 10
  documents and is quit at cleanup() via WM_CLOSE while UIA-dismissing whatever
  blocks it (per-document save prompts, the modeless "Opened document information"
  notice, open-failure dialogs); the one place pywinauto remains. A force-killed
  MSIX instance leaves a zombie single-instance registration, so taskkill stays
  the last resort. A pre-existing user instance is reused but never quit (the
  driver notes it). The trap leg stays skipped: Affinity re-renders layers by
  design, so the baked-composite trap proves nothing. Partial cells (a failed PSD
  leg) are never cached.

## Layout

```
testy/
  start-testy.bat    double-click launcher (dashboard + control panel)
  testy.py           orchestrator + dashboard server
  serve.py           the dashboard alone, between runs (port from config, default 8901)
  config.py          editor discovery + versions
  config.example.json  template for the gitignored config.local.json
  fetch_psd_tools_corpus.py  pinned psd-tools corpus checkout + corpus list
  staging.py         run-dir copies: trap, cache-stripped and plain variants
  psd_sections.py    minimal PSD/PSB section walker (trap patching, cache stripping)
  analyze.py         render metrics, sentinel detection, heatmaps (--selftest included)
  fonts.py           installed font files by PostScript name (handed to Photopea)
  manifest.py        original-vs-resave structural diff
  report.py          status.json + live report.html + history
  rerun.py           one-image updates and previous-result snapshots
  export_static.py   one finished run as a folder a plain web host can serve
  affinity_js.py     MCP/JS client for the Affinity app (also reused by .af tooling)
  win_dialogs.py     modal-dialog guard for scripted apps (--selftest included)
  drivers/           one per editor: photoshop (COM, --selftest included), patchy
                     (+ patchy_text_afresh.js), krita (+ krita_scripts/), gimp,
                     photodemon, photopea, affinity, psdtools; winproc.py (error-dialog
                     suppression)
  index.html         run-index landing page (server root)
  photopea_host.html the Photopea embedding/automation page
  corpus/            gitignored: local corpus lists
  runs/<ts>/         gitignored: artifacts, results.json, report.html
  cache/             gitignored: ground-truth + cell cache
  public/            gitignored: export_static.py output
  published-runs/    committed image-free export of the August 2026 run
```

`testy/runs/history.jsonl` accumulates one summary line per run; the report's "Past
runs" table reads it for the over-time view.

## Patchy CLI automation (product side)

`patchy.exe <in> --export <out>` saves and exits unattended; set
`PATCHY_SETTINGS_DIR` to isolate history/settings. With deep editing on (the default), a
16/32-bit document exports a 16-bit PNG; the Patchy driver splits it into `<stem>16.png` for
the precision metric and an 8-bit narrowing for every other metric (`analyze.split_deep_png`). `--append-text <s>` edits every
text layer before export, pinned by `ui_cli_append_text_rerenders_and_roundtrips`.
Flags live in `src/app/main.cpp`; see [scripting.md](scripting.md).
