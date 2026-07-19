# Testy: the PSD compatibility benchmark

Testy (`testy/`) measures how Patchy and other installed editors handle real PSD files,
with Adobe Photoshop 2026 as ground truth. Repeated runs over time show whether Patchy's
compatibility is improving and which PSDs are trouble.

## Running it

Double-click `testy\start-testy.bat`: it kills any stale Testy processes, starts the
dashboard server on port 8901, and opens the control panel in the browser. The panel's
"New run" box is pre-filled with the default corpus (one absolute path per line) and
takes any pasted .psd list instead; pick editors and options and hit Start - the run
appears at the top of the runs table (clickable while live), and the Start button stays
disabled until it finishes. Browser-started runs reuse the panel's server
(`--server-url` under the hood), rebuild Patchy first unless unchecked, and log to
`testy/runs/last-child-run.log`. A Cancel button (shown while a run is live) kills the
whole run process tree and marks the run "canceled".

The CLI remains for scripted use:

```powershell
C:\Users\Seth\miniconda3\python.exe testy\testy.py
```

That runs the curated corpus (`testy/corpus/default.txt`, real PSDs from
`local-test-fixtures/psd/`) through Photoshop, Patchy, Krita, and Photopea, refreshes the
Patchy release build first, serves a live dashboard (auto-opens the browser), and leaves
the frozen report + `results.json` in `testy/runs/<timestamp>/`. The server root
(`http://127.0.0.1:<port>/`) is the same control panel. In every report, clicking a
file name (matrix or detail panel) copies its full path to the clipboard.

Useful flags:

- `--files a.psd b.psd` - explicit file list instead of the corpus.
- `--corpus <file>` - another corpus list (one path per line, relative to the repo root).
- `--editors photoshop,patchy,krita,photopea,affinity` - which columns to run. Affinity is
  opt-in (see below). Aseprite was verified to have no PSD I/O at all and is not part of
  the roster.
- `--no-build` - skip the release build refresh (measures the current patchy.exe as-is).
- `--fresh` - ignore cached ground truth / cells (cache lives in `testy/cache/`, keyed by
  file hash + editor version, and by Patchy git hash for the Patchy column).
- `--exit-when-done`, `--no-browser`, `--no-serve`, `--port N` - dashboard behavior.
- `--suffix "~TESTY~"` - the marker string used by the forced text re-render test.

The miniconda base env has all dependencies (pywin32, Pillow, numpy, pywinauto).
Requirements if a different Python is ever used: those four packages.

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
  `akiko_cycling_okinawa_with_filters.psd`, was confirmed bad in the Photoshop UI by
  Seth and deleted (July 2026); the `smart_objects_warp` core test that used it now
  [SKIP]s on the missing fixture.
- Runs fail fast: the Photopea driver aborts when the host page's step log stalls for
  45s, and the orchestrator trips a per-editor circuit breaker after 3 consecutive
  failed cells (remaining cells report "skipped" instead of burning timeouts).
- The Affinity driver runs ONE document per app session under a hard wall-clock budget
  (180s + any relaunch cooldown; overruns abort the cell naming the stage they died
  in). Hard-won specifics: quick relaunches drop the launch file argument entirely
  (50s cooldown between sessions fixes it; forwards into a live instance drop files
  too); the export panel opens once per document with paced toggles (rapid re-toggles
  cancel each other); WPF tab headers CONSUME UNDERSCORES as mnemonic markers
  ("deko_test.psd" displays as "dekotest.psd"), so tab matching compares
  underscore-stripped names; and the trap leg is skipped (needs a second document,
  and Affinity re-renders by design so the baked-composite trap proves nothing).
  Partial cells (a failed PSD leg) are never cached.
- Affinity (Canva unified app 3.2) has no CLI or scripting API. The driver
  (`testy/drivers/affinity.py`) automates it WITHOUT stealing focus via background UIA
  patterns: the quick-export panel opens via the dropdown's Toggle pattern (WPF
  light-dismiss popups survive in background; synthetic clicks and posted messages do
  NOT work - WPF re-reads the physical cursor), format chips select via SelectionItem
  with the quick button's label as feedback, and the export lands through the standard
  shell Save As dialog, which appears without focus and is driven strictly by
  automation id (filename box 1001) and exact button titles. Cold-start toolbar
  population is flaky, which is why the column is opt-in. The driver reuses a running
  instance and never kills one it did not launch.

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
  drivers/           photoshop (COM), patchy (CLI), krita (CLI),
                     photopea (headless Chrome + embed API), affinity (background UIA)
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
