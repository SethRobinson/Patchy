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
live), and the Start button stays disabled until it finishes. Paths in the list that
no longer exist or are not .psd/.psb never block the run: the server drops them,
starts on what is left, and reports them under the box as an amber "skipped N
unusable path(s)" warning (the first few by name, all of them in the Testy console)
next to how many files the run actually got. That matches what the CLI's corpus
reader has always done, and it means a pasted list of a few hundred paths with a
handful of stale ones needs no hand-pruning first. Real errors still stop the start
and stay red: a list with nothing usable left in it, no editors, a bad threshold.
Browser-started runs reuse the panel's server (`--server-url` under the hood) and log to
`testy/runs/last-child-run.log`; their file list is handed to the child through
`testy/runs/last-child-corpus.txt` (`--corpus` under the hood), never argv, because a
pasted corpus of a few hundred paths overflows the Windows 32K command-line limit and
the spawn fails with WinError 206 before the child starts. A server endpoint that
still hits an unexpected error reports it to the panel as a JSON 500 rather than
dropping the connection (which the browser shows only as "TypeError: Failed to
fetch"). A Cancel button (shown while a run is live) kills the
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
Status updates are collision-safe on Windows: the run swaps status.json into place
with a retried os.replace, non-terminal pushes that still fail are logged and skipped
instead of killing the run, and the server serves status.json from memory rather than
streaming the open file. Any reader holding the file open makes the swap fail with a
sharing error (WinError 5); before these guards, big scans died "interrupted" every
few minutes once status.json grew past a megabyte, because the report page polls it
every 1.2s and the streamed send held the handle open long enough to collide.
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

Every report served by the panel also has a "Retest file" button in a file's detail
panel: it re-runs just that file as a fresh run of its own, reusing the viewed run's
editors and options, and the page jumps to the new run's report once it starts. The
typical use is checking whether a Patchy fix landed, so the retest refreshes the
Patchy release build first (a no-op when already built). Caches make everything else
fast: the Photoshop ground truth and the other editors' cells load from
`testy/cache/`, while the Patchy cell re-measures because its cache key includes the
git hash. The button respects the one-run-at-a-time rule (it is disabled while a run
is live) and does not exist in a frozen report opened from disk. Cells cached before
the perceptual metric existed are upgraded in place when a run reuses them: the
render comparison is recomputed from the cached images and the cache entry rewritten,
so old caches never force a strict-only report.

The CLI remains for scripted use:

```powershell
python testy\testy.py [--files a.psd b.psd] [--corpus list.txt] [--editors ...]
```

A default run goes through Photoshop, Patchy, Krita, GIMP, PhotoDemon, and Photopea, refreshes the
Patchy release build first (when configured), serves a live dashboard (auto-opens the
browser), and leaves the frozen report + `results.json` in `testy/runs/<timestamp>/`.
The server root (`http://127.0.0.1:<port>/`) is the same control panel. In every
report, clicking a file name (matrix or detail panel) copies its full path to the
clipboard, and clicking any thumbnail opens the full-size image; a Back link in the
header returns to the control panel (shown only when the page is served, since a
frozen report opened from disk has no panel to go back to). The matrix's column
header row stays pinned to the top of the viewport while the rest of the page
scrolls, so a file hundreds of rows down still shows which editor each cell belongs
to. That is also why the matrix draws its grid with per-cell borders instead of
collapsed ones: a collapsed border belongs to the table, so Chromium leaves it behind
when the row pins and the pinned row arrives with no lines at all. The detail panel labels
each image with the editor's name, and its left edge can be dragged to resize the
panel (the width sticks; double-click the divider to reset). Lost native data is
called out prominently: matrix cells get a red "lost: 5/5 text layers, 5/5 live
effects" line (and a warn dot), and the detail panel's native-preservation banner
separates objects GONE from the resaved file from ones still present but converted
to a different kind (e.g. text rasterized); attribute-only losses (effects, masks,
blend modes stripped from surviving layers) are labeled as such. A resave Photoshop
refuses to open shows a "resave rejected" banner instead of a broken panel.

The matrix is built to be read at a glance on a corpus of thousands. Each file's size
sits next to its document size and layer count ("4000x2500 - 2 layers - 20.3 MB", and
the size shows even when the ground truth failed and the other two are unknown), the
header totals the whole corpus next to the file count, and a scan run's card adds a
size done/total row, since bytes get through at nothing like a steady files-per-hour
rate. Sizes are recorded when a run starts; a run from before that shows none until it
is resumed, which backfills them. A cell's status line also says what kind of "opened"
it was: a render that misses the scan threshold (10% by default, measured by whichever
comparison the run flags on) reads "opened - poor matching" with a yellow dot, and a
resave Photoshop cannot reopen reads "opened - saves corrupted .psd" with a red dot, as
does a render wrong on more than 30% of the pixels. Each editor's summary card rolls
those rejected resaves up as a "bad .psd saves" count, red the moment it is not zero
(also in the CLI summary and history.jsonl as `badSaves`).

Useful flags:

- `--files a.psd b.psd` - explicit file list instead of the corpus.
- `--corpus <file>` - another corpus list (one path per line, relative to the repo root).
- `--editors photoshop,patchy,krita,gimp,photodemon,photopea,affinity` - which columns to run. Affinity is
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
- `--compare strict|perceptual` - which comparison drives scan flagging (default
  perceptual). Both numbers are always computed and shown in the report either way;
  a resumed run keeps the mode it started with, and runs from before this option
  flag strictly.
- `--exit-when-done`, `--no-browser`, `--no-serve`, `--port N` - dashboard behavior.
- `--suffix "~TESTY~"` - the marker string used by the forced text re-render test.

## Scan mode

`--scan` (or the control panel's "scan: keep only flagged" checkbox) turns a run into a
triage pass over a big file list: each file is FLAGGED if anything failed (ground truth,
open, resave, trap, text mutation, a skipped/broken editor, a resave Photoshop rejects,
a trap sentinel hit that Photoshop's own trap render does not share, see "Honest
rendering" below) or if any editor's render differs from Photoshop's on more than the
threshold fraction of pixels (default 10%, `--scan 25` for 25%). Which "fraction of
pixels" that means follows the run's comparison mode: perceptually-wrong pixels
(`renderMetrics.perceptual.badFraction`) by default, raw over-6/255 byte differences
(`renderMetrics.badFraction`) with `--compare strict`. Flagged files keep all artifacts
as usual. Files that pass keep their metrics in
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
  at document size. Two comparisons always run side by side, labeled **byte match**
  and **perceptual** in the report (`--compare strict|perceptual` on the CLI, whose
  values are unchanged). The byte-match one counts pixels off by more than 6/255 per
  channel (plus RMSE); it is honest about raw data but marks a visually identical
  render as ~100% different when a subtle color-management shift moves every pixel a
  hair over the tolerance. The perceptual one counts pixels that actually look wrong:
  SSIM's contrast-structure term (is the same structure present) combined with
  CIEDE2000 deltaE (is the color visibly different), both computed on lightly blurred
  copies so sub-pixel anti-aliasing jitter stays quiet, with the deltaE threshold
  scaled up under strong local contrast (contrast masking). A global 8/255 shift
  scores ~0% perceptually while byte match reports ~100%; a genuinely missing,
  misplaced, or recolored object fires both. Each metric also gets a per-object breakdown using ground-truth layer
  bounds; an object "renders ok" while under 25% of its region's pixels are off
  (text layers legitimately differ on every glyph edge even when correct, and a
  layer's bbox also contains whatever renders behind it, so overlapping errors can
  count against more than one object). Worst offenders are named in the detail
  panel, ranked by the run's comparison mode.
  The byte-match metric runs at document resolution. The perceptual one costs about a
  second and 150 MB of numpy temporaries per megapixel, so it runs on copies
  area-averaged down to `PERCEPTUAL_MAX_PIXELS` (4 MP) first, and is skipped outright
  when the two renders match pixel for pixel. Both shortcuts matter on big documents:
  an 18000x3508 banner took 66s and 12.3 GB for one comparison and now takes 10.9s and
  3.5 GB (most of what is left is decoding two 63 MP PNGs), and the Photoshop column
  compares its own render against the ground truth, which is pixel-identical, so its
  perceptual pass now costs nothing at all. Resolution matters less than it sounds:
  both legs already judge blurred copies, and a 12x4 px defect in an 18000 px document
  still flags its object at 100% after a quarter-scale downsample. What does change is
  the perceptual `badFraction` on documents over 4 MP - it can read a few times higher
  or lower in relative terms (measured 0.28% -> 0.86% on a hairline defect), because
  the blur covers proportionally more of a coarser grid. It drives a 10% triage
  threshold, not a pinned number, and the byte-match figure beside it is unchanged.
  `python testy\analyze.py --selftest` pins all of it (global-shift immunity, defect
  detection under an aggressive cap, the identical-render skip) against synthetic
  renders, with no Photoshop or corpus needed.
- **Honest rendering (trap)** - the editor also opens a byte-patched variant whose
  embedded flat composite is replaced with magenta (`psd_sections.py` rewrites only the
  trailing image-data section; all layer data stays byte-identical). An editor whose
  render shows magenta was displaying Photoshop's baked composite instead of compositing
  layers itself. Flattened files (zero layer records) get no trap: their composite is
  the only image data in the file, so reading it is the correct behavior and even
  Photoshop would trip the sentinel. The detail panel notes when the trap was skipped
  for this reason, and a cached cell that scored a sentinel hit under the old rule is
  cleaned up in place the next time a run reuses it. Photoshop tripping its own trap
  means the file has layers even the ground truth cannot re-render (missing fonts
  etc.), so it fell back to the baked composite; another editor matching that is not
  a cheat. Such cells show a neutral "renders from the baked composite (so does
  Photoshop)" note instead of the cheat flag and do not flag in scan mode; only
  sentinel coverage more than 5 points beyond Photoshop's own still counts as a
  cheat.
- **Native preservation** (labeled "data kept in .psd save" in the report and CLI
  summary; the results.json/history.jsonl keys stay `native`/`nativeScore`) - the
  editor's re-saved PSD is reopened in Photoshop and its
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
  Krita 5.3 and Affinity re-render text on open by design. GIMP's PSD import keeps
  text layers as their baked rasters (no editable text arrives), so it has no
  mutation leg either. The detail panel only shows the "render, text appended" image
  pair for editors that have the leg (Patchy, and Photoshop whose leg lives with the
  ground truth); other editors' panels state the reason it is absent
  (`TEXT_MUTATION_SKIPPED` in testy.py) instead of showing Photoshop's mutated render
  alone, which read as a missing test. Photopea's mutation pass is
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
- PhotoDemon runs as a locally patched build, because the stock app has no automation
  surface at all (its command line only loads files into the GUI; batch processing and
  macros are GUI wizards). The patch lives in a PhotoDemon checkout next to this
  repository (`../PhotoDemon`, BSD-licensed): a small `Testy.bas` module plus a hook in
  `FormMain`'s startup adds `PhotoDemon.exe <in> /testy-export <out>`, which reuses
  PhotoDemon's own batch-processor machinery (MacroBATCH dialog suppression,
  `LoadFileAsNewImage`, `PhotoDemon_BatchSaveImage` with default settings, format by
  extension), writes a one-line phase report to `<out>.testy.txt` (the driver consumes
  and deletes it), and exits. The patch must NEVER be sent upstream: PhotoDemon has a
  strict no-LLM/no-AI contribution policy. Builds are compiled with twinBASIC
  (`C:\Apps\twinBASIC`, Community edition, 32-bit; unattended builds are not licensed,
  so rebuilding after a patch change is a manual click in its IDE), and the exe must
  sit at the checkout root next to the `App\` folder or PhotoDemon refuses to start.
  Editor discovery deliberately ignores stock install locations (a stock build would
  open its GUI and burn the cell timeout); only the sibling checkout or an explicit
  `photodemon` path in config.local.json is used. PhotoDemon keeps text layers
  editable on PSD import, but with no scripting there is no mutation leg.
  The CLI mode also disables PhotoDemon's ExifTool plugin for the session: Testy does
  not measure metadata, and ExifTool's asynchronous metadata pipe wedged a PSD export
  in the wild (July 2026, `shelf_2.psd`: the .psd was fully written and Photoshop
  reopened it fine, then the metadata catch-up wait spun forever, the process hit a
  "has stopped working" dialog, and the cell burned its whole 180s timeout). Two
  driver-side defenses came out of the same incident and apply generally: every CLI
  driver (PhotoDemon, Krita, GIMP) now spawns its editor inside
  `drivers/winproc.suppressed_error_dialogs()`, an inherited error mode that stops
  Windows Error Reporting dialogs from holding a crashed editor open until a human
  clicks them, and the PhotoDemon driver judges a leg by its sidecar plus the artifact
  rather than the exit code, so an export that completed before the process died on
  the way out scores ok with the crash kept as a driver note in the detail panel.
  Only a clean exit with a sidecar verdict counts as "the app refused this file" for
  the circuit breaker; a crash, hang, or launch failure still counts against
  PhotoDemon itself.
- GIMP 3.2 headless Script-Fu batch: `gimp-console-3.exe -i -d -f --batch-interpreter
  plug-in-script-fu-eval -b <script> -b "(gimp-quit 0)"` (PNG render and PSD resave via
  `gimp-file-save`, format by extension). Two hard-won rules live in the driver: GIMP 3
  refuses batch work without an explicit `--batch-interpreter`, and a batch command
  that errors makes the console stop WITHOUT running the trailing `(gimp-quit 0)`, so
  the process hangs forever. The driver wraps the whole script body in Script-Fu's
  `catch` so the quit always runs; success is judged by output existence, and the
  GIMP-Error lines naming the real cause reach stderr either way. A timeout kills the
  process tree via `taskkill /t` because batches execute in a separate script-fu
  plug-in process.
- Photopea (web) runs in a headless Chrome via selenium: `testy/photopea_host.html`
  iframes photopea.com and drives it through the official postMessage API. The host page
  fetches the staged PSD same-origin and posts the bytes as an ArrayBuffer (Photopea's
  https iframe cannot fetch plain-http local URLs itself - a files entry in the hash
  config hangs at "Loading" forever), runs `saveToOE("png"/"psd")`, and POSTs each
  ArrayBuffer back to the server's `/testy-upload` endpoint (uploads are path-confined to
  `runs/`; the server also sends CORS headers). Needs internet; selenium manager fetches
  chromedriver on first use.
- A percent sign in a corpus file's name is a hazard, because two of the three things
  Testy hands a path to decode escape sequences. ExtendScript's `new File(...)`
  URI-decodes its argument, so `eco%20beret.psd` resolved to `eco beret.psd` and
  Photoshop answered "Expected a reference to an existing File/Folder" for a file that
  was sitting right there; the dashboard server unquotes request paths the same way, so
  Photopea's fetch of the staged copy 404'd and the report's thumbnails came back empty.
  Each boundary now encodes the path it is about to hand over (`drivers/photoshop.py`'s
  `_js_path`, `drivers/photopea.py`'s `_file_url`, `report.py`'s `artUrl`), while the
  `/testy-upload` `name` deliberately stays raw: it rides in a query parameter and is
  decoded exactly once already. A run directory inherits the corpus file's stem, so this
  reaches every artifact under it, not just the input file.
- Nuisance modal dialogs are answered from outside the COM call. `DialogModes.NO`
  does not reach every dialog: some files make Photoshop raise a modal alert from
  inside `app.open` ("This file contains file info data which cannot be read and has
  been ignored" - a PSD whose IPTC resource holds something other than IPTC records,
  e.g. `Closed.psd` in the local corpus, which carries a TIFF/Exif block there). The
  scripted call is already blocked when the alert appears, so nothing inside
  ExtendScript can dismiss it, and before July 2026 the file simply burned two
  120s watchdog timeouts plus a Photoshop restart and scored "ground truth failed".
  Every probe now runs under a `testy/win_dialogs.py` DialogGuard, which polls
  Photoshop's windows from a side thread and clicks the acknowledging button. It is
  deliberately narrow: only owned windows or standard `#32770` dialogs with at most
  six push buttons (never an app's own frame, which is full of hidden buttons named
  OK and Cancel), only real push buttons (so an alert's "Don't show again" checkbox
  is never ticked, which would quietly change Photoshop's settings), only buttons
  that mean carry on (OK/Yes/Continue/Update/Close/Done/Proceed - never Cancel, No,
  Save or Discard), and only through messages posted to that dialog's own button, so
  nothing can land in another app. A dialog with no safe answer is left standing and
  named in the failure text, which turns "photoshop hung >120s" into the alert's
  actual wording. Not all of these are open-time dialogs: the Photoshop cell's probe
  also saves a PSD, and Photoshop's save-time warnings are modal too ("contains nested
  layer groups that may change in appearance if opened in applications older than
  Photoshop CS6", seen on a 415-layer template), so that leg can hang on a save rather
  than an open. Dismissed alerts are recorded per file (`groundTruth.dialogs`), per
  cell (`cell.dialogs`, covering that cell's own open/save/trap probes) and per resave
  (`cell.resaveDialogs`, i.e. Photoshop complaining about what that editor wrote), and
  shown in the detail panel; they do not flag a file in scan mode,
  since the metadata they complain about is not something Testy measures.
  `python testy\win_dialogs.py --selftest` exercises the guard against real Win32
  dialogs raised by a helper process, including the refusal and watchdog paths, so
  it needs neither Photoshop nor a broken PSD.
- A file whose ground truth failed does not get probed a second time for the
  Photoshop column: the cell inherits the ground-truth error. The ground truth
  already opened that exact file with that exact driver and already retried on a
  freshly restarted Photoshop, so a second probe would fail identically and cost
  another two minutes and another restart.
- Photoshop self-heals from engine wedges (verified July 2026): during a long session
  Photoshop's scripting engine wedges into a state where EVERY `app.open` returns error
  8000 ("open options are incorrect") regardless of the file - a control file that
  opened fine minutes earlier fails identically once wedged, and opens again after a
  restart. So on any probe failure the driver fully restarts Photoshop (Quit, wait for
  the process to go, taskkill only what is still standing, relaunch) and retries once;
  a wedge costs one ~35s restart rather than failing every remaining file. A hang
  watchdog force-kills Photoshop if a single script blocks past 120s (a stuck modal),
  which unblocks the COM call. Failed cells and cells scored without ground truth are
  never cached, so re-runs retry them.
- Never force-kill Photoshop while it is quitting. It saves its preferences on the way
  out, and a kill inside that write truncates them: the next launch dies at init with
  "Could not initialize Photoshop because an unexpected end-of-file was encountered",
  and every launch after that does the same. In July 2026 a restart landed exactly
  there, left a zero-length `Workspace Prefs.psp` (and no
  `Adobe Photoshop 2026 Prefs.psp` at all) in
  `%APPDATA%\Adobe\Adobe Photoshop 2026\Adobe Photoshop 2026 Settings`, and cost an
  overnight run: 120 files measured against a Photoshop that could no longer start. The
  cure is to delete the empty prefs files, which Photoshop rebuilds on the next launch.
  Do not delete the whole Settings folder; it also holds the brush, style, pattern,
  action and custom-shape libraries. `restart()` now gives a clean quit
  `QUIT_GRACE_SECONDS` (30s) to finish before anything is forced, and only a refused
  quit or a process still up at the deadline is killed.
- A COM error saying the server never started (`CO_E_SERVER_EXEC_FAILURE`,
  `REGDB_E_CLASSNOTREG`) is a broken Photoshop, not a bad PSD, and is worded that way
  in the report, together with whatever alert the dialog guard cleared on the way
  through (which is what names the actual cause). After two files in a row fail that
  way the driver reports itself unavailable and answers instantly instead of buying two
  ~35s launch attempts per file, and the run checkpoints itself exactly like a pause:
  ground truth is the baseline every column is scored against, so there is nothing left
  to measure. Fix Photoshop and resume. The ground-truth verdicts those launch failures
  produced are handed back as pending, so the resume measures them properly rather than
  keeping a verdict that belonged to the machine, and a resave Photoshop never got to
  open is not counted as "resave rejected" against the editor that wrote it.
  `python testy\drivers\photoshop.py --selftest` pins all of it (the classification,
  the wording, the give-up rule, and what a restart may kill) with no Photoshop needed.
- A file that fails scripted open even on a freshly restarted engine (with a passing
  control immediately before) is genuinely bad, not a wedge. The one such corpus file,
  `akiko_cycling_okinawa_with_filters.psd`, was confirmed bad in the Photoshop UI and
  deleted (July 2026); the `smart_objects_warp` core test that used it now [SKIP]s on
  the missing fixture.
- Runs fail fast: the Photopea driver aborts when the host page's step log stalls for
  45s, and the orchestrator trips a per-editor circuit breaker after 3 consecutive
  failed cells (remaining cells report "skipped" instead of burning timeouts). Only
  failures OF THE EDITOR count. A cell where the editor ran and refused the file (Krita
  or Patchy exiting non-zero, Affinity answering INAPPROPRIATE_FILE_TYPE_OR_FORMAT, a
  file Photoshop itself will not open) proves the app is alive and answering, so it
  clears the count exactly like a success does; a hang, a timeout, a crash, or a launch
  failure is the app and still counts. Drivers say which by returning `fileRejected`,
  and the default for anything unclassified is to count, so a new failure mode never
  quietly disables the breaker. Without that split, three unrelated files an importer
  dislikes sitting next to each other in the corpus look exactly like a dead app: in
  July 2026 Krita lost the last 94 files of a run to three icon PSDs from one folder.
  The breaker covers editor columns only; ground truth is not one of them, and a
  Photoshop that stops launching ends the run instead (see above).
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
- Affinity staging I/O rides through `_retry_locked`, which waits out transient
  Windows sharing violations (up to ~2s) on every staged-file unlink, copy, and
  move: Affinity can briefly hold a just-loaded document's handle and antivirus
  scans grab fresh Desktop copies. In July 2026 a cell died with a bare
  "driver error: [WinError 32]" because the post-cell unlink of its own staged
  input raised from the driver's finally block, which replaced the cell's real
  result; cleanup is now non-fatal (a stubbornly locked file is left for the
  next cell's staging retry or cleanup()'s rmtree).
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
  analyze.py         render metrics, sentinel detection, heatmaps (--selftest included)
  manifest.py        original-vs-resave structural diff
  report.py          status.json + live report.html + history
  affinity_js.py     token-free MCP/JS client for the Affinity app (SSE + JSON-RPC,
                     launch/quit lifecycle; also reused by .af format tooling)
  win_dialogs.py     modal-dialog guard for scripted apps (--selftest included)
  drivers/           photoshop (COM, --selftest included), patchy (CLI), krita (CLI),
                     gimp (Script-Fu batch CLI), photodemon (patched-build CLI),
                     photopea (headless Chrome + embed API), affinity (built-in JS
                     automation via affinity_js)
  index.html         run-index landing page (server root)
  photopea_host.html the Photopea embedding/automation page
  corpus/            gitignored: local corpus lists
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
