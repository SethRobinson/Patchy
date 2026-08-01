# Testy: the PSD compatibility benchmark

Testy (`testy/`) measures how Patchy and other installed editors handle real PSD files,
with Adobe Photoshop 2026 as ground truth. Repeated runs over time show whether Patchy's
compatibility is improving and which PSDs are trouble.

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

A "Retest file" button in a served report's detail panel re-runs just that file as a
fresh run with the same editors and options, refreshing the Patchy build
first; caches keep the rest fast (Photoshop and the other editors load from
`testy/cache/`, Patchy re-measures because its cache key includes the git hash). It
is disabled while a run is live and absent from a frozen report opened from disk.
Cells cached before the perceptual metric existed are upgraded in place on reuse
(recomputed from the cached images).

The CLI remains for scripted use:

```powershell
python testy\testy.py [--files a.psd b.psd] [--corpus list.txt] [--editors ...]
```

A default run goes through Photoshop, Patchy, Krita, GIMP, PhotoDemon, and Photopea,
refreshes the Patchy release build first (when configured), serves a live dashboard,
and leaves the frozen report + `results.json` in `testy/runs/<timestamp>/`. The
server root is the same control panel. Clicking a file
name in a report copies its full path to the clipboard; clicking a thumbnail opens
the full-size image. The matrix header stays pinned while the page scrolls (the grid
uses per-cell borders because Chromium drops collapsed borders from a pinned row).
Lost native data is called out: matrix cells get a red "lost: ..." line and a warn
dot, and the detail panel's native-preservation banner separates objects GONE from
the resaved file from ones converted to a different kind (e.g. text rasterized);
attribute-only losses (effects, masks, blend modes stripped) are labeled as such. A
resave Photoshop refuses to open shows a "resave rejected" banner instead of a
broken panel.

Each file's row shows document size, layer count, and file size (shown even when
ground truth failed); the header totals the corpus, and a scan run's card adds a
bytes done/total row (older runs backfill sizes on resume). A cell's status line
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
  (it serves the local MCP endpoint the scripting rides on); with it off, Affinity
  cells fail with an actionable message and the rest runs. Aseprite has no PSD I/O
  and is not in the roster.
- `--no-build` - skip the release build refresh (measures the current patchy.exe).
- `--fresh` - ignore cached ground truth / cells (cache in `testy/cache/`, keyed by
  file hash + editor version, plus Patchy git hash for the Patchy column).
- `--resume runs\<ts>` - continue a paused/canceled/interrupted run directory, skipping
  completed work (implies `--no-build`, ignores `--files/--corpus/--editors`).
- `--scan [PCT]` - scan mode; see below.
- `--compare strict|perceptual` - which comparison drives scan flagging (default
  perceptual). Both numbers are always computed and shown either way; a resumed run
  keeps the mode it started with, and runs from before this option flag strictly.
- `--exit-when-done`, `--no-browser`, `--no-serve`, `--port N` - dashboard behavior.
- `--suffix "~TESTY~"` - the marker string used by the forced text re-render test.

## Scan mode

`--scan` (or the panel's "scan: keep only flagged" checkbox) turns a run into a
triage pass: a file is FLAGGED if anything failed (ground truth, open, resave, trap,
text mutation, a skipped/broken editor, a resave Photoshop rejects, a trap sentinel
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
after a Patchy fix skips the slow Photoshop leg. Clear `testy/cache/` if the space
matters more than re-scan speed.

A paused scan resumes normally: files already given their verdict are not
re-scrubbed or re-flagged, and `flagged.txt` is written once at true completion.
One benign loss: cells finished just before the pause in a partially-done file stay
in the report but are not cell-cached.

## What each cell measures

For every (PSD, editor) pair, the editor opens a staged COPY (corpus files are never
touched; a SHA check at the end of every run proves it), and Testy records:

- **Opens** - did the file load at all.
- **Render accuracy** - the editor's flattened PNG vs Photoshop's, composited over
  white at document size. Two comparisons always run, labeled **byte match** and
  **perceptual** in the report. Byte match counts pixels off by more than 6/255 per
  channel (plus RMSE); honest about raw data, but a subtle color-management shift
  can mark a visually identical render ~100% different. Perceptual counts pixels
  that actually look wrong: SSIM's contrast-structure term combined with CIEDE2000
  deltaE, both computed on lightly blurred copies so anti-aliasing jitter stays
  quiet, with the deltaE threshold scaled up under strong local contrast. A global
  8/255 shift scores ~0% perceptually while byte match reports ~100%; a genuinely
  missing, misplaced, or recolored object fires both. Each metric also gets a
  per-object breakdown using ground-truth layer bounds; an object "renders ok"
  while under 25% of its region's pixels are off (text legitimately differs on
  glyph edges; a bbox also contains what renders behind it, so one error can hit
  several objects). Worst offenders are named in the detail panel, ranked by the
  run's comparison mode. Byte match runs at document resolution; perceptual costs
  about a second and 150 MB of numpy temporaries per megapixel, so it runs on
  copies area-averaged down to `PERCEPTUAL_MAX_PIXELS` (4 MP) and is skipped when
  the renders match pixel for pixel. Above 4 MP the downsample can shift the
  perceptual `badFraction` in relative terms; it drives a 10% triage threshold, not
  a pinned number, and the byte-match figure is unchanged.
  `python testy\analyze.py --selftest` pins all of it against synthetic renders; no
  Photoshop or corpus needed.
- **Honest rendering (trap)** - the editor also opens a byte-patched variant whose
  embedded flat composite is replaced with magenta (`psd_sections.py` rewrites only
  the trailing image-data section; all layer data stays byte-identical). Magenta in
  the render means the editor displayed Photoshop's baked composite instead of
  compositing layers itself. Flattened files (zero layer records) get no trap: the
  composite is the only image data, so reading it is correct and even Photoshop
  would trip the sentinel (noted in the detail panel; old cached cells are fixed
  on reuse). Photoshop tripping its own trap means even the ground
  truth could not re-render the layers (missing fonts etc.) and fell back to the
  baked composite; another editor matching that is not a cheat (a neutral note says
  so) and does not flag in scan mode. Only sentinel coverage more than 5 points
  beyond Photoshop's own counts as a cheat.
- **Native preservation** (labeled "data kept in .psd save" in the report and CLI
  summary; the results.json/history.jsonl keys stay `native`/`nativeScore`) - the
  editor's re-saved PSD is reopened in Photoshop and its layer manifest compared
  against the original's: text still `TEXT`, each adjustment still its exact kind,
  smart objects still smart, groups/masks/vector masks/live effects/clipping/blend
  modes intact. This is the "23/40 objects survived" number; a resave Photoshop
  refuses to open scores as rejected.
- **Round-trip render** - Photoshop's render of the editor's resave vs the
  original's render.
- **Forced text re-render** - scriptable editors append `~TESTY~` to every text
  layer so cached rasters cannot satisfy the render: Photoshop via COM
  (`textItem.contents`), Patchy via `patchy.exe --append-text` (real inline-editor
  sessions per layer). Mutated renders are compared within text-layer regions.
  Krita 5.3 and Affinity re-render text on open by design, and GIMP's PSD import
  keeps text layers as baked rasters, so none of them has a mutation leg. The detail panel shows the "render, text appended" pair only
  for editors with the leg (Patchy; Photoshop's lives with the ground truth);
  others state why it is absent (`TEXT_MUTATION_SKIPPED` in testy.py). Photopea's
  mutation pass is deliberately disabled: its script engine hangs on contents
  assignment for some documents and its DOM never matched text layers reliably.

The Photoshop column doubles as a control: ~100% render accuracy and full native
preservation validate the pipeline itself.

## Machine specifics (July 2026)

- Photoshop 2026 via COM (`Photoshop.Application`); techniques per docs/ps-compat.md.
  The driver opens each file once per probe: manifest walk (DOM + ActionManager by
  layer id), duplicate-flatten-save render (copy-merged fallback for damaged files),
  optional save-as-copy resave, optional text mutation + second render.
- Krita 5.3.2 headless CLI: `krita.com <in> --export --export-filename <out>` (format
  by extension; PSD export works). Its console shim prints nothing through pipes;
  success is exit code + output existence (Fontconfig warnings are filtered out of
  reported errors).
- PhotoDemon runs as a locally patched build; the stock app has no automation
  surface (its command line only loads files into the GUI). The patch lives in a
  PhotoDemon checkout next to this repository (`../PhotoDemon`, BSD-licensed): a
  `Testy.bas` module plus a `FormMain` startup hook add
  `PhotoDemon.exe <in> /testy-export <out>`, which reuses PhotoDemon's own
  batch-processor machinery (MacroBATCH dialog suppression, `LoadFileAsNewImage`,
  `PhotoDemon_BatchSaveImage` with defaults, format by extension), writes a one-line
  phase report to `<out>.testy.txt` (the driver consumes and deletes it), and exits.
  The patch must NEVER be sent upstream: PhotoDemon has a strict no-LLM/no-AI
  contribution policy. Builds compile with twinBASIC (`C:\Apps\twinBASIC`, Community
  edition, 32-bit; unattended builds are not licensed, so rebuilding after a patch
  change is a manual click in its IDE), and the exe must sit at the checkout root
  next to the `App\` folder or PhotoDemon refuses to start. Editor discovery
  deliberately ignores stock install locations (a stock build would open its GUI and
  burn the cell timeout); only the sibling checkout or an explicit `photodemon` path
  in config.local.json is used. PhotoDemon keeps text layers editable on PSD import,
  but with no scripting there is no mutation leg. CLI mode disables PhotoDemon's
  ExifTool plugin (Testy does not measure metadata, and its async pipe once wedged
  an export after the .psd was fully written). Related defenses: every CLI driver
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
  fetch 404'd). Each boundary now encodes the path it hands over
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
  once; a wedge costs one ~35s restart. A hang watchdog force-kills Photoshop when
  a script blocks past 120s (a stuck modal). Failed cells and cells scored without
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
  passing control immediately before) is genuinely bad, not a wedge. The one such
  corpus file, `akiko_cycling_okinawa_with_filters.psd`, was confirmed bad in the
  Photoshop UI and deleted; the `smart_objects_warp` core test that used it now
  [SKIP]s on the missing fixture.
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
  preset; preset names resolve by enumeration with a prefix fallback). Typical
  cell: under 3s even for a 40 MB PSD.
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
  Affinity's own import rejecting the file and scores honestly as opens=fail
  (vectors_overlay_stroke.psd is such a file; the UI refuses it too).
- Affinity staging I/O rides through `_retry_locked`, which waits out transient
  Windows sharing violations (up to ~2s) on every staged-file unlink, copy, and
  move: Affinity can briefly hold a just-loaded document's handle and antivirus
  scans grab fresh Desktop copies. Cleanup is non-fatal (a finally-block unlink
  failure once replaced a cell's real result); a stubbornly locked file is left
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
  testy.py           orchestrator + dashboard server
  config.py          editor discovery + versions
  staging.py         run-dir copies + trap generation
  psd_sections.py    minimal PSD/PSB section walker (trap patching only)
  analyze.py         render metrics, sentinel detection, heatmaps (--selftest included)
  manifest.py        original-vs-resave structural diff
  report.py          status.json + live report.html + history
  affinity_js.py     MCP/JS client for the Affinity app (also reused by .af tooling)
  win_dialogs.py     modal-dialog guard for scripted apps (--selftest included)
  drivers/           one per editor: photoshop (COM, --selftest included), patchy,
                     krita, gimp, photodemon, photopea, affinity
  index.html         run-index landing page (server root)
  photopea_host.html the Photopea embedding/automation page
  corpus/            gitignored: local corpus lists
  runs/<ts>/         gitignored: artifacts, results.json, report.html
  cache/             gitignored: ground-truth + cell cache
```

`testy/runs/history.jsonl` accumulates one summary line per run; the report's "Past
runs" table reads it for the over-time view.

## Patchy CLI automation (product side)

Testy drives Patchy through product flags added for it (src/app/main.cpp):
`patchy.exe <in> --export <out>` opens a file, saves it to `<out>` (format by
extension) and exits unattended (single-instance opt-out, prompts suppressed,
recents/prefs untouched); `--append-text <s>` first appends `<s>` to every text
layer through real editor sessions so rasters re-render through the text pipeline.
Pinned by the `ui_cli_append_text_rerenders_and_roundtrips` visual test.
