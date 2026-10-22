# Testy: reopening resaves in several Photoshop versions

Companion to [testy.md](testy.md). A PSD one Photoshop opens and another refuses is a bad save:
Photoshop 2025 crashed on Patchy text blocks that 2026 read fine (issue 341). Testy can reopen
every resave in more than one Photoshop so such regressions show up in normal runs.

## Reference and checked versions

- The **reference** renders the ground truth, runs the Photoshop column and does the scored
  reopen of every resave (`resaveRejected`, `native`, the round-trip render). Unless a run
  names one, it is exactly what Testy always used: the configured ProgID
  (`editors.photoshop_progid`, default `Photoshop.Application`, Photoshop 2026 on the dev box).
- Each **checked version** only opens files: the corpus original, then every editor's resave
  (no render). Its verdicts sit beside the reference's; they never change the reference's
  numbers.
- Installs are found in the registry: the versioned ProgIDs `Photoshop.Application.<n>` (190 is
  2025, 200 is 2026), their CLSID and its `LocalServer32` exe (`drivers/photoshop.py`,
  `discover_installs`). The label is the install folder's year, else the ProgID number.
  `config.local.json`'s `photoshop.installs` adds remote installs or overrides a local one.
- Choosing: `--photoshop 2025,2026` (the reference is always one of them; an empty value means
  the reference alone) and `--photoshop-reference 2025`, else `photoshop.check` and
  `photoshop.reference` in `config.local.json`. The panel shows a "reopen resaves in" box per
  install when there are two or more; the reference box is ticked and fixed. A resume keeps
  the run's recorded versions; a row Rerun or Retest uses its batch's.
- A remote install cannot be the reference (the ground truth's renders stay on this PC).

## What is recorded

- `status.run.photoshop`: `reference`, `referenceChosen`, `checks` and `versions` (display name,
  exe file version). Written only when a run checks another version or names its reference,
  so a single-version run's status.json and history line are what they always were.
- `entry.psVersions[label]`: that version's verdict on the corpus file itself.
- `cell.resaveChecks[label]`: its verdict on the editor's resave, with `nativeKept`,
  `nativeTotal`, `nativeScore` when it opened. That score compares against the same version's
  manifest of the original, so a feature the older version lacks is not counted as a loss.
- Verdict states (`reopen_verdict` in testy.py):
  - `ok`;
  - `rejected`: Photoshop ran and refused the file, crashed on it (`crashed`) or hung on it
    (`hung`), the same rule the reference's `resaveRejected` follows;
  - `error`: the version never ran (launch failure, unreachable agent). Not a verdict on the
    file, never cached, and a rerun refuses to apply it;
  - `not checked`: the version could not open the original either, so it is no judge of a
    resave of it. The row says "PS 2025 cannot open this file".
- A resave any version rejects is a bad save: `badSaves` counts it once, `badSavesBy`
  (aggregate and `history.jsonl`, multi-version runs only) counts per version, the status line
  reads "saves corrupted .psd (rejected by PS 2025)", the summary card adds a "rejected by PS
  <label>" row per version, the detail panel lists every version's verdict, and scan mode flags
  "resave rejected by Photoshop 2025 (it crashed)". `history.jsonl` also gets a `photoshop`
  object (`reference`, `checks`).

## Caching and processes

- Verdicts are cached as `testy/cache/reopen-<sha1>-<label>-<exe file version>-<suffix>.json`,
  keyed by the opened file's bytes. The exe's file version is read without launching
  Photoshop, so a fully cached run never starts a checked version.
- A driver built for an install restarts, force-kills and dialog-guards only processes whose
  image path is that install's exe (`photoshop_pids`); two versions run side by side and a
  restart of one never touches the other. Without a resolved exe the old image-name behavior
  applies. The reference driver gets its exe whenever the registry resolves it.
- A checked version Testy launched is quit when the run ends (clean quit, 30 s grace, then a
  kill of that exe only); one that was already running is left alone.
- A crash under a running call (RPC server unavailable, disconnected) is classified as
  `crashed` and worded "Photoshop crashed while opening the file".

## Remote Photoshop host

For a version that only exists on another Windows machine. On that host COM only starts
Photoshop from the logged-on console session, never from ssh, so:

- `testy/photoshop_agent.py` runs there in the console user's session, started over ssh by
  `testy/remote/start-agent.ps1` as a temporary interactive scheduled task
  (`testy-photoshop-agent`, removed again by `quit()`). It uses the same `drivers/photoshop.py`
  driver, dialog guard and restart rules as this PC; `drivers/photoshop_remote.py` copies those
  files into the run's folder on the host before starting it.
- The protocol is a folder on a share (`<share>/testy-<run>/`): `inbox/` holds the PSD copies,
  `jobs/<id>.json` and `results/<id>.json` the requests and probe results (written as a
  temporary file and renamed), `heartbeat.json` a counter a side thread bumps every 5 s plus
  the host's installs and exe versions, `stop.flag` the quit request. A heartbeat that stops
  for 60 s means the agent died: the client starts it once more, then reports the version
  unavailable (`error` verdicts). The agent exits after 30 idle minutes on its own.
- Crash dumps Photoshop writes during a crashed or hung job are moved from the host's
  `%LOCALAPPDATA%\CrashDumps` into `<run folder>/dumps/`. At the end the client deletes exactly
  the files it created; `agent.log` and `dumps/` stay.
- Errors coming back name the file, not the host's copy, and the host folder becomes
  `<remote>`, so a published report carries no host paths. Host names, shares and users live
  only in `config.local.json` (`photoshop.hosts`): `ssh`, `share`, `dir` (the same folder as
  the host sees it), `user` (the console user) and `python` (3.11+ with pywin32; `pythonw.exe`
  beside it is used so no console window opens on the host's desktop). An install entry names
  its `host` and the host's own label (`remote_label`) or ProgID (`progid`).
- With an empty `ssh` the agent runs on this PC as a child process: that is how the protocol is
  exercised against a real Photoshop without a second machine.

## Running Testy on a Photoshop host over ssh

To measure against a Photoshop that only another machine has (Photoshop 2025 alone, say), run
Testy there. ssh sessions cannot start Photoshop over COM, so `testy\remote\run-console.ps1`
runs `testy\testy.py` in the console user's session through a temporary interactive
scheduled task (`testy-console-run`), waits, prints the log's tail and exits with Testy's exit
code (`-NoWait` returns at once; the same command later waits and cleans up):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File <repo>\testy\remote\run-console.ps1 -Python <python.exe> -User <console user> -Arguments "--corpus corpus\psd-tools.txt --editors photoshop,patchy --photoshop-reference 2025 --no-build --no-serve --no-browser --exit-when-done"
```

`--photoshop-reference 2025` with no `--photoshop` and no `photoshop.check` makes 2025 the only
Photoshop of the run: ground truth, Photoshop column and every resave reopen. The host needs
Python 3.11+ with pywin32, Pillow and numpy, the corpus (`python testy\fetch_psd_tools_corpus.py`
for psd-tools), a built `build\release\patchy.exe` (or `editors.patchy` in its
`config.local.json`), and its console user logged on. The log is `testy\runs\console-run.log`
(`-Log` changes it, written in UTF-8 through `PYTHONUTF8=1`); a literal `%` in `-Arguments`
must be written `%%`. A localized Photoshop's alerts may not fit a console's code page:
`log()` falls back to escapes and the dialog guard swallows logging errors, because an
exception escaping the guard's `stop()` lands inside the probe's `with` block and once read
as Photoshop rejecting the file (a Japanese Photoshop 2025, October 2026).

## Tests

`tests\testy_photoshop_versions_tests.py` (through `scripts\run-throttled.bat python`) covers
verdicts, aggregation, caching, the not-checked rule, single-version records and the agent
protocol with an in-process agent; it launches no Photoshop.
`python testy\drivers\photoshop.py --selftest` pins install labels, LocalServer32 parsing and
crash classification.
