# Release process

How to cut and publish a Patchy release. Read this in full before bumping a version or running any release batch file. The per-change build/test handoff (every code change must refresh `build\release\patchy.exe`) is separate and lives in AGENTS.md.

## Version bump checklist

When bumping the release version, update the version fields:

- `CMakeLists.txt` (`project(... VERSION x.y)`)
- `vcpkg.json` (`version-semver`)
- `latest_version.json` — the per-platform `version` entries: windows always; macos/linux only when those artifacts actually ship. This is the update-check manifest served to the app from raw.githubusercontent.com on main, and only takes effect once pushed.
- The `<release>` tag in `packaging/linux/com.rtsoft.patchy.metainfo.xml`
- A new top entry under `README.md`'s "What's New" section for that version,
  dated with the release date and summarizing the user-visible changes.
- Keep only the two newest release entries in `README.md`. After adding the new
  entry, move the entry that has become third-newest to the top of
  `RELEASE-HISTORY.md`, preserving its date, wording, order, and author credits.
  Keep the `[Older releases](RELEASE-HISTORY.md)` link immediately after the two
  README entries. `RELEASE-HISTORY.md` stays newest-first and must not duplicate
  either release still shown in the README.

## What's New author credits

Always credit the correct author on each "What's New" bullet, including entries
moved to `RELEASE-HISTORY.md`. Seth is the default and is left uncredited; any
feature or fix contributed by someone else must name them with a GitHub handle
link like `([@handle](https://github.com/handle))`. Check `git log`'s author for
the commits behind each bullet (e.g. `git log --format='%an %s'`) rather than
assuming, and when one bullet mixes work from more than one person, credit the
specific clause that person wrote (see the existing 0.10/0.12 entries in
`RELEASE-HISTORY.md` for the mid-bullet style).

## Build and upload order

Build order matters: finalize the README first (the Windows zip/installer embed a copy), then `release-all.bat` (three consoles: Windows + remote mac/linux; every builder deletes its previous artifacts up front so a failed build can never leave stale files for the newest-file upload scripts), then `upload-to-rtsoft.bat`.

## Agent/non-interactive runs: NO_PAUSE

**Agent/non-interactive release runs must set `NO_PAUSE=1` before launching the batch files.** From PowerShell, set `$env:NO_PAUSE='1'` and then run `cmd /c release-all.bat`; the environment is inherited by the three `start`ed consoles and, critically, by `%RT_PROJECTS%\Signing\sign.bat`, which otherwise pauses after EVERY signed Windows file. Do this before the first launch, not after a signing prompt appears.

`release-mac.bat` and `release-linux.bat` have their own unconditional final `pause`, so do not wait for those wrapper `cmd.exe` processes to exit: determine success from the child PowerShell completion and fresh versioned artifacts, then close the completed wrapper consoles.

Also keep `NO_PAUSE=1` set for `upload-to-rtsoft.bat`; its per-platform upload scripts receive `nopause`, but the top-level script still has one final `pause`, which an automated runner must dismiss after all uploads complete.

Do not say a release was created unless the release preset build completed successfully.
