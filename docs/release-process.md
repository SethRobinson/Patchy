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

## Batch files call their siblings by full path

`NoDefaultCurrentDirectoryInExePath` is set in most non-interactive shells, including the
ones coding agents run commands in. It stops cmd from resolving a bare command name out of
the current directory, so `cmd /c "build-release.bat"` fails with `'build-release.bat' is
not recognized` even when the file is right there. A relative path that contains a
separator, like `scripts\remote\release-mac.bat`, still resolves fine, because cmd treats
that as a path rather than a name to search for.

That is why `release-all.bat` and `upload-to-rtsoft.bat` launch their siblings as
`"%~dp0name.bat"`. Keep it that way. The failure mode is nasty: on July 26, 2026 the
Windows console of `release-all.bat` closed instantly without building anything while the
mac and Linux consoles (launched with a path) built normally, and because that console
never reached the delete-previous-artifacts step, the previous version's zip and installer
were still sitting in `build\package` for the upload scripts to pick up.

## scripts\vs-env.bat, not VsDevCmd.bat

Every build entry point (`build-release.bat`, `scripts\run-tests.ps1`,
`scripts\make-readme-screenshots.ps1`, the handoff command in AGENTS.md) enters the
developer environment through `scripts\vs-env.bat`, which forwards its arguments to
VsDevCmd.bat. It is the only place that knows where Visual Studio is installed, and it
prepends the VS Installer directory to `PATH` before the call.

That `PATH` line is what silences `'vswhere.exe' is not recognized`. VsDevCmd.bat reads the
product version by pushd-ing into the Installer directory and running `vswhere.exe` by bare
name, which the rule above blocks. The probe is optional inside VsDevCmd, so it still
returns 0 and the build works, but the line looks exactly like a real failure in a release
log. Do not chase it if you see it from some other caller: check whether that caller went
through vs-env.bat.

## Agent/non-interactive runs: NO_PAUSE

**Agent/non-interactive release runs must set `NO_PAUSE=1` before launching the batch files.** From PowerShell, set `$env:NO_PAUSE='1'` and then run `cmd /c release-all.bat`; the environment is inherited by the three `start`ed consoles and, critically, by `%RT_PROJECTS%\Signing\sign.bat`, which otherwise pauses after EVERY signed Windows file. Do this before the first launch, not after a signing prompt appears.

`release-mac.bat` and `release-linux.bat` have their own unconditional final `pause`, so do not wait for those wrapper `cmd.exe` processes to exit: determine success from the child PowerShell completion and fresh versioned artifacts, then close the completed wrapper consoles.

`upload-to-rtsoft.bat` itself does not read `NO_PAUSE`: it already passes the positional `nopause` argument to the per-platform upload scripts, but the top-level script ends in one final unconditional `pause`, which an automated runner must dismiss (feed Enter) after all uploads complete.

Do not say a release was created unless the release preset build completed successfully.
