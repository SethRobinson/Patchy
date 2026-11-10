# Testy editor drivers

Split from [testy.md](testy.md): how Testy drives each editor (Photoshop over COM, Krita, the patched PhotoDemon build, GIMP, PhotoCraft, Photopea, Affinity) and its defenses against path-encoding hazards, modal dialogs, wedged or crashed editors and failing columns.

## Editor drivers

- Photoshop 2026 via COM (`Photoshop.Application`); techniques per docs/ps-compat.md.
  The driver opens each file once per probe: manifest walk (DOM + ActionManager by
  layer id), duplicate-flatten-save render (damaged and 32-bit files: copy merged, paste
  in place),
  optional save-as-copy resave. A type layer's manifest entry also lists its paragraph
  settings (justification, Hyphenate, direction, line composer), orientation,
  anti-aliasing and kind. The descriptor leaves out what a paragraph inherits from its
  parent sheet (the DOM's `textItem.hyphenation` throws there too) and those stay null
  in the probe's answer: setting a property to its own value would make Photoshop write
  the whole sheet out, but that set also turns Adobe Every-line Composer on
  ([txt2-kept-settings.md](txt2-kept-settings.md#line-composer)). `testy/text_sheets.py`
  reads the inherited Hyphenate and line composer from the file's `Txt2` block (or the
  TySh EngineData) instead. The probe never sets a text style, for a reading or for the
  reference render ([testy-scoring.md](testy-scoring.md#the-reference-render)). Other versions can reopen every resave too:
  [testy-photoshop-versions.md](testy-photoshop-versions.md).
- Krita 5.3.2 runs its own Python: `kritarunner.com -s testy_krita_export -f main <in>
  <out> <report>`, started in `drivers/krita_scripts/` (kritarunner replaces PYTHONPATH,
  so the module is found through the working directory). The script opens the file,
  polls the projection until two reads match, then exports (format by extension) and
  writes its verdict to the report file. The plain CLI (`krita.com <in> --export
  --export-filename <out>`) exports before fill and vector layers are drawn (drawn on
  one run, blank on the next), so it is only the fallback when the script leaves no verdict.
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
  render and PSD resave via `gimp-file-save`, format by extension). Two rules: GIMP 3 refuses batch work without an explicit `--batch-interpreter`, and a
  batch command that errors stops the console WITHOUT running the trailing
  `(gimp-quit 0)`, hanging forever, so the driver wraps the script body in
  Script-Fu's `catch` so the quit always runs. Success is judged by output
  existence; GIMP-Error lines naming the real cause reach stderr either way. A
  timeout kills the process tree via `taskkill /t` (batches execute in a separate
  script-fu plug-in process).
- PhotoCraft (open source, Rust) runs its stock console CLI, one process per leg:
  `photocraft-cli.exe convert <in> <out>` (format by extension). Discovery tries
  `../PhotoCraft/photocraft-cli.exe` (the portable zip unpacked beside this
  repository) before the Program Files install, or a `photocraft` path in
  config.local.json; the version comes from `--version` (the exes carry no
  ProductVersion). Exit 1 is PhotoCraft refusing the file (counts as `fileRejected`),
  anything else nonzero a crash. stderr warnings other than the "layer(s) flattened"
  notice become driver notes. A 16/32-bit document exports a 16-bit PNG, split like
  Patchy's. 0.5.0 shows a type layer's stored pixels and draws nothing for one
  without them, so the cache-free leg runs `photocraft-cli run <in> --cmd
  type.updateAllTextLayers --out <png>` (a plain convert when the document has no
  type layers, which the command refuses); its cells carry `-textafresh1`. Nothing
  in the CLI redraws a smart object, so a blank one is not measured. The text-save
  leg runs the same command with `--out <psd>` (`save_text_afresh`); the count the
  command prints (`{"result":{"updated":N}}`) is kept as `updatedByEditor`.
- Photopea (web) runs in a headless Chrome via selenium: `testy/photopea_host.html`
  iframes photopea.com and drives it through the official postMessage API. The host
  page fetches the staged PSD same-origin and posts the bytes as an ArrayBuffer
  (Photopea's https iframe cannot fetch plain-http local URLs; a files hash-config
  entry hangs forever), runs `saveToOE("png"/"psd")`, and POSTs each
  ArrayBuffer back to the server's `/testy-upload` endpoint (uploads path-confined
  to `runs/`; CORS headers sent). Needs internet; selenium manager fetches
  chromedriver on first use.
- The text-save leg ([testy-scoring.md](testy-scoring.md#the-save-after-a-text-update))
  asks three editors for a .psd saved after their text update, from the original staged
  file. Patchy: `drivers/patchy_text_afresh.js` again (`save_text_afresh`), with
  `smart=0` so smart objects stay as they are and an `out` ending in .psd, which makes
  the script call `doc.saveAs` instead of `exportAs`. `layer.rerenderText()` is the
  per-layer form of Type > Update All Text Layers (`type.update_all_text_layers`): both
  open a hidden text session on the layer, mark it changed and commit it. The per-layer
  call is used because it says which layers were reached and exists in older builds. It
  draws a layer whose font Patchy lacks in a fallback face, which the menu command
  skips; the script prints each layer's name before the call, the driver ties the font
  warning that follows to it (`fontFallback`), and the leg does not measure that layer.
  Photopea: the host page with `nudge=1` and a `resave` target and no `render` (the PNG
  export is skipped when none is asked for). PhotoCraft: above.
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
  inside `app.open` (e.g. a PSD with malformed XMP date properties), and
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
  dialogs from a helper process (no Photoshop needed).
- A file whose ground truth failed is not probed again for the Photoshop column:
  the cell inherits the ground-truth error (already retried on a restarted Photoshop;
  a second probe would fail the same way).
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
  and `testy/affinity_js.py` speaks it directly with the standard library. One execute_script call per document runs Document.load plus
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
  scans grab fresh Desktop copies. Cleanup is non-fatal.
- Affinity lifecycle: Document.close is NOT_IMPLEMENTED on Windows, so opened
  documents pile up as tabs; an instance the driver launched restarts after 10
  documents and is quit at cleanup() via WM_CLOSE while UIA-dismissing whatever
  blocks it (per-document save prompts, the modeless "Opened document information"
  notice, open-failure dialogs). A force-killed
  MSIX instance leaves a zombie single-instance registration, so taskkill stays
  the last resort. A pre-existing user instance is reused but never quit. The trap leg stays skipped: Affinity re-renders layers by
  design, so the baked-composite trap proves nothing. Partial cells (a failed PSD
  leg) are never cached.
