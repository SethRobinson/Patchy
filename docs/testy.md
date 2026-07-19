# Testy: the PSD compatibility benchmark

Testy (`testy/`) measures how Patchy and other installed editors handle real PSD files,
with Adobe Photoshop 2026 as ground truth. Repeated runs over time show whether Patchy's
compatibility is improving and which PSDs are trouble.

## Running it

```powershell
C:\Users\Seth\miniconda3\python.exe testy\testy.py
```

That runs the curated corpus (`testy/corpus/default.txt`, ~14 real PSDs from
`local-test-fixtures/psd/`) through Photoshop, Patchy, Krita, and Aseprite, refreshes the
Patchy release build first, serves a live dashboard (auto-opens the browser), and leaves
the frozen report + `results.json` in `testy/runs/<timestamp>/`.

Useful flags:

- `--files a.psd b.psd` - explicit file list instead of the corpus.
- `--corpus <file>` - another corpus list (one path per line, relative to the repo root).
- `--editors photoshop,patchy,krita,aseprite,affinity` - which columns to run. Affinity is
  opt-in (see below).
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
  per-object breakdown using ground-truth layer bounds (an object "renders ok" when under
  2% of its region's pixels are off). Worst offenders are named in the detail panel.
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
  Krita 5.3 and Affinity re-render text on open by design; Aseprite has no PSD support.

The Photoshop column doubles as a control: it should sit at ~100% render accuracy and
full native preservation, which validates the pipeline itself.

## Machine specifics (July 2026)

- Photoshop 2026 via COM (`Photoshop.Application`); techniques per docs/ps-compat.md.
  The driver opens each file once per probe: manifest walk (DOM + ActionManager by layer
  id), duplicate-flatten-save render (copy-merged fallback for damaged files), optional
  save-as-copy resave, optional text mutation + second render.
- Krita 5.3.2 headless CLI: `krita.com <in> --export --export-filename <out>` (format by
  extension; PSD export works). Its console shim prints nothing through pipes; success is
  exit code + output existence.
- Aseprite 1.3.17 has no PSD import (verified every run via `-b -p`); its column reports
  that honestly and will start measuring automatically if a future version adds PSD.
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
  drivers/           photoshop (COM), patchy (CLI), krita (CLI), aseprite (CLI),
                     affinity (background UIA)
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
