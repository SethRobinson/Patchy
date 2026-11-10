# Testy: the PSD compatibility benchmark

Testy (`testy/`) measures PSD compatibility against Adobe Photoshop 2026: whether each editor opens a file, how closely its render matches Photoshop's, whether it composites layers itself, what native data and bit depth survive its PSD save, whether text survives a save made after the editor re-lays it out, and for 16-bit files how precise its render is. What each measurement means is in [testy-scoring.md](testy-scoring.md); the public write-up of the latest published run is [psd-compatibility-benchmark.md](psd-compatibility-benchmark.md).

Related documents:

- [testy-scoring.md](testy-scoring.md): what each cell measures (opens, render accuracy, the trap, data kept in the .psd save, the round-trip render, text kept in a save made after the editor's text update, bit depth and 16-bit precision, and the cache-free leg that scores an editor on what it draws itself), with the reference-render rules and the "never mark an editor down for the harness's mistake" safeguards.
- [testy-editor-drivers.md](testy-editor-drivers.md): how each editor is driven (Photoshop over COM, Krita, the patched PhotoDemon build, GIMP, PhotoCraft, Photopea, Affinity) and the defenses against modal dialogs, wedged or crashed editors and failing columns (the circuit breaker).
- [testy-photoshop-versions.md](testy-photoshop-versions.md): reopening every resave in more Photoshop versions, remote Photoshop hosts, and running Testy on a Photoshop host over ssh.

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

When Seth says "run the Testy suite", he means the psd-tools corpus
(`--corpus corpus\psd-tools.txt`, below), not the small default list in `config.local.json`.

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
Editor-free regressions (via `scripts\run-throttled.bat python`): `tests\testy_rerun_tests.py`,
`testy_shutdown_tests.py`, `testy_driver_failure_tests.py`, `testy_text_save_tests.py`,
`testy_text_sheets_tests.py`.

**Retest file** in the detail panel creates a separate one-file run, reusing
caches and refreshing Patchy's build. Both controls require an idle Testy server.

The CLI remains for scripted use:

```powershell
python testy\testy.py [--files a.psd b.psd] [--corpus list.txt] [--editors ...]
```

A default run goes through Photoshop, Patchy, Krita, GIMP, PhotoDemon, Photopea, and PhotoCraft,
refreshes the Patchy release build first (when configured), serves a live dashboard,
and leaves the frozen report + `results.json` in `testy/runs/<timestamp>/`. The
server root is the same control panel. Clicking a file
name in a report copies its full path to the clipboard; clicking a thumbnail opens
the full-size image.
Lost native data is called out: matrix cells get a red "lost: ..." line and a warn
dot, and the detail panel's native-preservation banner separates objects GONE from
the resaved file from ones converted to a different kind (e.g. text rasterized);
attribute-only losses (effects, masks, blend modes stripped) are labeled as such. A
resave Photoshop refuses to open shows a "resave rejected" banner instead of a
broken panel. For Patchy, PhotoCraft and Photopea a cell with type layers also reads
"after text update N/M text layers", the summary card has a "text kept after a text
update" row, and the detail panel shows Photoshop's render of that save and names the
layers that are no longer text or changed, with what changed.

Each file's row shows document size, layer count, and file size (shown even when
ground truth failed); the header totals the corpus, and a scan run's card adds a
bytes done/total row. A cell's status line
qualifies "opened": a render missing the scan threshold (10% default, by the run's
comparison mode) reads "opened - poor matching" with a yellow dot; a resave
Photoshop cannot reopen reads "opened - saves corrupted .psd" with a red dot, as
does a render wrong on more than 30% of pixels. Each editor's summary card rolls
rejected resaves up as a "bad .psd saves" count, red when nonzero (also in the CLI
summary and history.jsonl as `badSaves`), counting a save any checked Photoshop rejects.

Useful flags:

- `--files a.psd b.psd` - explicit file list instead of the corpus.
- `--corpus <file>` - corpus list (one path per line, relative to the repo root).
- `--editors photoshop,patchy,krita,gimp,photodemon,photopea,photocraft,affinity` - which columns
  to run. Affinity is opt-in: enable the app's connector once in Affinity's settings
  (it serves the local MCP endpoint); with it off, Affinity
  cells fail with an actionable message and the rest runs. `psdtools` (opt-in) is
  the psd-tools library: its compositor and a load-and-save (`pip install "psd-tools[composite]"`).
- `--no-build` - skip the release build refresh (measures the current patchy.exe).
- `--fresh` - ignore cached ground truth / cells (cache in `testy/cache/`, keyed by
  file hash + editor version; Patchy's key is a hash of patchy.exe itself).
- `--resume runs\<ts>` - continue a paused/canceled/interrupted run directory, skipping
  completed work (implies `--no-build`, ignores `--files/--corpus/--editors`).
- `--resume runs\<ts> --add-editors photocraft` - add columns to a finished non-scan run;
  other columns, `finishedAt` and the Patchy build stay. New cells are scored against the
  ground truth in `testy/cache` (never re-probed; a missing entry or changed source skips the
  cell with the reason), the history line is replaced, and the header says "added <date>".
- `--scan [PCT]` - scan mode; see below.
- `--photoshop 2025,2026`, `--photoshop-reference LABEL` - more Photoshop versions reopen
  every resave ([testy-photoshop-versions.md](testy-photoshop-versions.md)).
- `--compare strict|perceptual` - which comparison drives scan flagging (default
  perceptual). Both numbers are always computed and shown either way; a resumed run
  keeps the mode it started with, and runs recorded without a mode flag strictly.
- `--exit-when-done`, `--no-browser`, `--no-serve`, `--port N` - dashboard behavior.
- `--suffix "~TESTY~"` - a marker that is only part of cache entry names.

## The psd-tools collection and the By folder table

`python testy\fetch_psd_tools_corpus.py` checks out psd-tools' `tests/psd_files` (issue 65), pinned, into `local-test-fixtures/psd-tools` and writes `testy/corpus/psd-tools.txt`; run it with `--corpus corpus\psd-tools.txt`. A multi-folder corpus gets a "By folder" table above the matrix (matches, data kept, bad saves per editor); a row click filters it. "Score without known limitations" drops 16/32-bit files (entry `traits`) from the totals. Same-stem files get distinct artifact directories via the entry's `dir` key (`x~psb`). `psd_tools_corpus_reads_and_round_trips_if_available` covers the same files without Photoshop.

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
made (`testy-export.txt` marks it). Published runs live at
`rtsoft.com/testy/<YYYY-MM-DD>/` and are uploaded only on Seth's explicit go-ahead (the upload
target belongs in `agents_local.md`, never here): pack the folder (`tar --force-local -czf ...
--exclude=testy-export.txt .`), copy the one archive into a
`<date>.new` folder on the host, compare sha256, untar, delete the archive, then swap it in with
two renames, so a republished run never serves half a copy. The published report requests
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
  text_sheets.py     paragraph settings a type layer leaves to its Txt2 or TySh sheets
  report.py          status.json + live report.html + history
  rerun.py           one-image updates and previous-result snapshots
  export_static.py   one finished run as a folder a plain web host can serve
  affinity_js.py     MCP/JS client for the Affinity app (also reused by .af tooling)
  win_dialogs.py     modal-dialog guard for scripted apps (--selftest included)
  photoshop_agent.py remote Photoshop reopen agent; remote/ runs it or Testy by ssh
  drivers/           one per editor: photoshop (COM, --selftest included; photoshop_remote), patchy
                     (+ patchy_text_afresh.js: text re-render, then a render or a .psd save), krita (+ krita_scripts/), gimp,
                     photodemon, photopea, photocraft, affinity, psdtools; winproc.py (error-dialog
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
