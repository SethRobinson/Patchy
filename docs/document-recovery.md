# Automatic document recovery and atomic file writes

Read this before touching the recovery timer, the recovery store, the startup reopen, the
Preferences row, `patchy.recovery`, or any file writer.

## What the user sees

Patchy writes a copy of every modified document to a recovery folder on a timer, the way
Photoshop's "Automatically Save Recovery Information Every N minutes" does. The user's own
file is never touched. When Patchy quits normally the copies are deleted. When it crashes
(or is killed, or the machine loses power), the next interactive launch reopens every copy
as a modified document titled `<name> (Recovered)` (or `Untitled (Recovered)`), with its
path set to the original file when it had one, so Save goes back where the user expects.
No prompt: Photoshop reopens silently too, and a recovered document is just an unsaved
document the user can close.

Preferences > Application: "Automatically save recovery information every" with a
5/10/15/30/60 minute combo. On by default at 10 minutes.

## Trigger rule

- Time-based only. The timer (`documentRecoveryTimer`, a `Qt::VeryCoarseTimer`) fires
  every `recovery/intervalMinutes`; there is no action count (one brush stroke and one
  Liquify session are both "one action").
- A session is written only if it is modified (`session_is_modified`) and its
  `(revision, current_state_id)` pair differs from the pair its current copy holds
  (`recovery_marks_`). `revision` alone is not monotonic: `rotate_history_state` restores
  the old value on undo, and the next edit after an undo reuses the number. The state id
  is minted fresh per history push and only ever goes back through undo/redo, so the pair
  identifies a distinct document state (scripts with `app.undoEnabled = false` bump only
  the revision, still covered).
- A tick is skipped entirely when the app is busy: `QApplication::activeModalWidget()`,
  the preview-dialog edit lock, or `any_canvas_interaction_active()` (pointer gesture,
  Free Transform, warp, path transform, crop session, inline text editor; the same
  predicate the scripting host's `manual_edit_in_progress` uses), or when a previous
  write is still in flight. One write job runs at a time.
- Export, run-script, headless, and hidden-connector instances (`cli_automation_mode_`)
  never tick. `patchy.recovery.writeNow()` ignores that gate so a script can drive it.

## The write

`MainWindow::write_recovery_now` (src/ui/main_window_recovery.cpp) snapshots each
candidate session on the UI thread (a `Document` copy shares pixel storage copy-on-write;
the live side detaches on its next edit, the worker only reads through `const Document&`),
then one `run_tracked_background_worker` job writes them in sequence:
`psd::DocumentIo::write_layered_rgb8(snapshot, WriteOptions{true})` (PSB, so any size fits;
the writer keeps the document's bit depth) and `recovery::write_entry`. Completion posts back through a queued `invokeMethod` on the
application with a `QPointer<MainWindow>`; `finish_recovery_write` stores the marks, and
removes the copy of any session that was closed or saved during the write (its close or
save already removed the previous copy, so the fresh one must not resurrect it). Errors go
to the status bar once, never a modal. `main()` waits for tracked workers before the
application object dies, so a quit mid-write never leaves a half-written file. The
snapshots are released on the worker, and `history_retained_bytes()` deliberately does not
count them (worst case one extra full copy, the same as an undo state).

## On disk

`<root>/<pid>-<start ms>/` per running instance, where `<root>` is
`PATCHY_RECOVERY_DIR` when set, else `<QStandardPaths::AppDataLocation>/AutoRecover`
(`%APPDATA%\RTsoft\Patchy\AutoRecover` on Windows). The folder is created by the first
write, never by construction, so an instance that never wrote leaves nothing behind.

Inside, per session (Qt-free layout in `src/core/document_recovery_store.hpp`):

- `<session_id>.psb`: the copy.
- `<session_id>.recovery`: UTF-8 `key=value` lines, first `format=2`, then `title=`,
  `path=` (the original file as UTF-8, empty when never saved), `savedAt=` (Unix ms).
  Key=value rather than JSON because `patchy_core` has no JSON parser. Format 2 escapes
  a backslash, a newline and a carriage return in values (`\\`, `\n`, `\r`): a POSIX
  path or a title may legally contain a line break, and unescaped it would have produced
  a second `path=` line that redirected the recovered document's Save. A sidecar with no
  `format=` line is the original unescaped layout and decodes verbatim (Windows paths
  there hold raw backslashes). A repeated `title`, `path` or `savedAt` key is malformed:
  the first title and timestamp stay and the path is dropped, so a damaged or crafted
  sidecar can only make a document untitled. The PSB is written first and the sidecar
  second, so a crash between them leaves a recoverable document listed as untitled,
  never a sidecar pointing at nothing.
- `lock`: a `QLockFile` with `setStaleLockTime(0)`. Liveness comes from Qt's own check that
  the pid in the file is a running process; the age heuristic is disabled because a live
  instance older than 30 seconds would otherwise look stale and a second instance would
  recover its documents out from under it. The probe itself (`tryLock` then `unlock`)
  replaces a stale lock file and removes it again, so after an orphan scan a dead
  instance's folder has no `lock`; tests must not expect one to survive.

Both files are written through `write_file_bytes_atomically`.

## Lifecycle

- `set_session_saved` (a successful save) and `close_document_session` (after the
  save-changes decision) call `discard_recovery_for_session`: the copy and its mark go.
- An accepted `closeEvent` stops the timer. `~MainWindow` stops it and calls
  `RecoveryInstanceFolder::discard_on_release`: the folder is deleted by whichever owner
  releases the shared pointer last, the window or a still-running write. A crash never
  reaches the destructor, which is the whole point. One quit path skips the destructor
  on purpose: when a tracked worker is still blocked in the OS 10 s after the event loop
  returned, `main.cpp` (and `mcp_server.cpp`) calls `discard_recovery_folder_for_forced_exit` (timer stopped,
  entries deleted; on Windows the open lock file survives as a lock-only folder that the
  next start sweeps) and ends the process without destructors.
- Startup (`src/app/main.cpp`, interactive path only, not stress/export/run-script/
  screenshot/headless): `recover_orphaned_documents()` before the command-line files open.
  Orphans are folders under the root whose lock is missing or names a dead process
  (`RecoveryInstanceFolder::scan_orphaned`; empty orphan folders are swept). Every entry
  opens through `open_recovered_document` (the regular `load_document_interactive` path
  with prompts off, plus the reopened-text metric fix-up), becomes a modified session, and
  its two files are renamed into this instance's folder under the new session id with a
  mark equal to the session's current state (a second crash is covered; the timer does not
  rewrite an identical copy). The orphan's files are removed only after the PSB rename
  succeeded: when this instance's folder cannot be created or the rename fails, the old
  copy stays where it is (still listed as an orphan), no mark is recorded so the timer
  writes a fresh copy of the open session when it is next due, and the status bar reports
  how many copies stayed behind. A failed sidecar rename is replaced by rewriting the
  sidecar from the entry in hand (`recovery::write_sidecar`). A file that fails to open
  stays where it is and the status bar reports the count. `patchy-mcp` with a visible
  workspace runs the timer like the GUI; hidden connectors do not.
- Deleting an instance folder goes through `RecoveryInstanceFolder::remove_folder`, which
  is not a recursive delete: it refuses a blank or relative path, a name that is not
  `<pid>-<msecs>`, a link or junction, an unreadable folder, and any folder holding
  anything but the store's own files (`recovery::is_store_file_name`: `<id>.psb`,
  `<id>.recovery`, their `.patchy-tmp` temporaries) and the lock; otherwise it removes
  those files one by one and then the empty folder. Ownership is proven by the contents,
  never by the name, so a `PATCHY_RECOVERY_DIR` pointed at a photo library whose `2026-10`
  folder fits the name pattern loses nothing. The orphan scan reads `symlink_status`, so a
  link named like an instance folder is neither scanned nor swept. Keep both rules on any
  new delete here.
- Concurrent instances are real (`PATCHY_NO_SINGLE_INSTANCE`, the test binaries, the
  connector), which is why liveness is per folder and never "files exist".
- wasm: compiled out (`Q_OS_WASM`). MEMFS is recreated per page load, so there is nothing
  durable to recover from. No Preferences row, no timer; `patchy.recovery.enabled` is false
  and every list is empty.

## Settings and environment

- `recovery/enabled` (bool, default true) and `recovery/intervalMinutes` (int, default 10,
  normalized to `kRecoveryIntervalMinutes`), through the accessors in
  `src/ui/app_settings.hpp`. Both keys are compatibility contracts.
- `PATCHY_RECOVERY_DIR` (the root; the UI suite sets `test-artifacts/recovery` so test
  windows never touch the user's store) and `PATCHY_RECOVERY_INTERVAL_MS` (timer override
  for tests).

## Scripting

`patchy.recovery`: `enabled`, `intervalMinutes` (throws outside the step list), `directory`,
`writeNow()`, `listFiles()`, `listOrphaned()`, `recoverAll()`, `discardOrphaned()`.
Documented in `scripts/bundled/patchy.d.ts`, `scripts/bundled/scripting-guide.md`, and
`docs/scripting-api-changes.md`. It exists so the tests below drive the real code path
instead of test-only hooks.

## Atomic file writes

`write_file_bytes_atomically(path, bytes, open_message, write_message)` in
`src/support/atomic_file_write.hpp` creates `<name>.<pid>-<counter>-<random>.patchy-tmp`
beside the target exclusively (`CREATE_NEW` with `FILE_FLAG_OPEN_REPARSE_POINT` on
Windows, `O_CREAT|O_EXCL|O_NOFOLLOW` elsewhere; a name that exists in any form is skipped
and another drawn), writes the bytes through that same handle, flushes them to the device
(`FlushFileBuffers`; `fsync`, `F_FULLFSYNC` first on macOS), and renames it over the
target (`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`; POSIX
`rename` followed by a best-effort `fsync` of the directory). The temporary file is
removed on every failure path, and only a temporary the call itself created is ever
removed. Guarantees: a process crash, a full disk or a writer error never leaves the
target truncated (the old file survives until the new bytes are complete); against an OS
crash or power loss the new bytes are on the device before the rename, and the one window
left is a directory entry the file system has not persisted yet, in which case the old
file is what comes back. A flush failure is a write failure. Callers:
`psd::write_file_bytes` (every PSD/PSB write), `formats::write_file_bytes` (BMP, TGA,
PCX, ICO, ILBM, Aseprite, GIF, JPEG XR, SVG, RTTEX, DDS), the animated WebP writer, the
palette (`palette_io::write_palette_file`) and Curves preset (`acv::write_file`) exports,
and the recovery store. `write_flat_image_file` (PNG, JPEG, WebP, TIFF and the other
`QImageWriter` formats) goes through `QSaveFile` with an explicit format instead, since
`QImageWriter` on a device cannot infer it from a suffix. Small Qt-side text files (the
Script Manager's Save and Save As, `patchy.io.writeTextFile`) go through
`save_text_file_atomically` in `src/ui/qt_text_file.hpp`, a `QSaveFile` wrapper that
reports the commit result; the editor clears its modified flag only after that succeeds.

Writers that stream to disk or hand a path to a library use `AtomicFileReplacement` (same
header, same temporary name, reserved empty by the constructor's exclusive create): write
`temporary_path()`, close it, `commit()` reopens it without following links, refuses
anything that is not a regular file with one hard link, flushes it and renames it over
the target; without a commit the destructor removes the temporary. Because these callers
reopen the reserved name by path, a link swapped in under it between the reservation and
their open would receive their bytes; the commit check keeps such a swap from ever
replacing the target, and the in-memory writer has no such window. Callers: the PDF
image-page writer (`pdf::ImageWriter`, pages stream into the temporary and `finish()`
commits), the Qt-engine PDF writers through `pdf_detail::QtPdfOutput` (`QPdfWriter` on a
`QFile` device opened on the temporary, so a failed write is caught from `QFile::error()`;
the text-merge pass rewrites the temporary before the commit), and Print > Save PDF
(`write_print_pdf`, `QPrinter` output file name set to the temporary; QPrinter reports no
write errors, so only a crash or a failed paint is covered there). A cancelled multi-page
export therefore keeps the previous file instead of deleting it.

Deliberate semantics: a target another process holds open with a share-deny lock (Windows)
fails at the rename and reports "Could not write" rather than truncating the file; a
symlink target is replaced by a regular file; the new file takes the directory's default
permissions rather than the old file's. The byte canaries pin encoder bytes, not the write path, and are unaffected.

## Tests

Core (`patchy_core_tests`, filters `atomic_write`, `recovery` and `palette_and_curves`;
group `atomic_write_recovery_tests`): replace-existing with no temporary left, missing
directory throws and keeps nothing, failed rename keeps the target and removes the
temporary, planted hard links at the old predictable temporary names stay untouched and
two reservations never share a name, a hard link or symlink swapped in under the reserved
name is refused at commit, the streaming replacement's commit/discard/destructor (and
`pdf_image_writer_keeps_existing`), palette and Curves exports replacing and failing
atomically, sidecar round trip with Unicode and CRLF input, format 2 escaping, legacy
decoding and duplicate-key rejection, `is_store_file_name`, directory scan with strays,
Unicode instance root. No power-loss test exists; the flush ordering is by inspection.

UI (`patchy_ui_visual_tests`, filters `ui_recovery`, `ui_flat_save`, `ui_preferences_recovery`;
group `document_recovery_tests`): `writeNow` round trip through the PSB reader and the
sidecar, the `(revision, state_id)` rule across undo/redo/edit, discard on save and on close,
the modal-dialog busy skip and the enabled toggle on a 50 ms timer, orphan detection through
a dead-pid lock and `recoverAll`, a live instance's folder never reported, foreign
date-named folders (photos, a dead lock beside a photo, a directory link) surviving the
sweep while a lock-and-temporary leftover folder goes, a blocked instance folder leaving
the orphan copy in place and still listed until the timer covers the session, Unicode
roots and titles, the atomic flat and PSD save (overwrite leaves no temporary, a missing
folder fails and keeps the old files), and the Preferences row plus the scripting setters.
Scripting (filters `ui_script_editor_save_failure` and `ui_script_io_write_text_file`):
the Script Manager save that cannot land keeps the editor modified and the folder clean,
and `writeTextFile` throws on a blocked target. PDF:
`ui_pdf_export_failure_keeps_existing_file` and `ui_pdf_export_writes_unicode_paths`
(filter `ui_pdf_export`).
