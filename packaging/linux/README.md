# Linux packaging (Flatpak)

Patchy ships on Linux as a Flatpak (this page) and as an AppImage
(`make-appimage.sh` and `appimage/`, documented in
[docs/appimage.md](../../docs/appimage.md)). The Flatpak comes in two forms built from the same signed commit by
`make-flatpak.sh` (driven from Windows by `scripts/remote/release-linux.ps1`):

- **The repository** at `https://rtsoft.com/flatpak/repo/`, with
  `https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref` and `patchy.flatpakrepo`
  beside it. This is what `flatpak update` and the software centers read (GitHub issue
  28). `build/package/Patchy-<version>-flatpak-repo.tar` holds all three;
  `scripts\release\upload-linux-to-rtsoft.bat` installs it on the server.
- **The single-file bundle** `build/package/Patchy-<version>.flatpak`, published as
  `PatchyLinux.flatpak` on GitHub Releases and the rtsoft.com mirror. It carries the
  repository URL and public key (`--repo-url`, `--gpg-keys`), so a bundle install gets
  the repository as its origin and updates like a repository install.

Install commands, all per user (no root, no polkit prompt; GitHub issue 14):

    flatpak install --user -y https://rtsoft.com/flatpak/com.rtsoft.patchy.flatpakref
    curl -L -o /tmp/PatchyLinux.flatpak <bundle url> && flatpak install --user -y /tmp/PatchyLinux.flatpak
    flatpak update --user -y com.rtsoft.patchy

Keep `--user` on every documented command. Besides the root prompt, flatpak 1.14 fails
a command without it ("While opening repository") on a machine that has no system-wide
installation. Both installs add the Flathub remote themselves when the machine has none (the ref's
`RuntimeRepo`, the bundle's `--runtime-repo`) and pull `org.kde.Platform` from it. The
app's update dialog shows the `flatpak update` line (`show_update_available` in
`src/ui/main_window_files.cpp`, pinned by `ui_update_available_dialog_*` in
`tests/ui/app_shell_tests.cpp`); an AppImage copy instead offers Update Now, which replaces the
`.AppImage` file when its folder is writable, and a download prompt otherwise
([docs/appimage.md](../../docs/appimage.md)).

1.05 (October 2026) is the first release that ships the repository.

## The repository

`build/flatpak-repo` on the build host collects every local build, so the published
repository is made fresh each time in `build/flatpak-publish`: `flatpak
build-commit-from` copies only the app ref of the build just made (no Debug ref) and
signs it, and `flatpak build-update-repo --generate-static-deltas` writes the signed
summary, the appstream branch and a from-scratch delta, so a first install is a few
large downloads instead of thousands of small ones. About 45 MB per release. Clients
need no commit history: an update only has to find a newer commit on the ref.

The upload unpacks the tar beside the live copy, checks that the signed summary is
there, swaps the `repo` directory in with two renames, then requires HTTP 200 from the
public `summary`, `summary.sig`, flatpakref and flatpakrepo URLs.

`site/.htaccess` is uploaded beside the repository on every release. It gives
`.flatpakref` and `.flatpakrepo` their MIME types (`application/vnd.flatpak.ref` and
`.repo`), which is what makes a clicked link open in a software center instead of
showing as text. flatpak itself ignores the type, so the upload only warns when it is
missing.

What each kind of install does once a release has shipped the repository (verified on
the linux build host with flatpak 1.14.6 in empty installations, October 2026):

| Install | Origin remote | `flatpak update` |
|---|---|---|
| flatpakref | `patchy`, GPG verified | updates |
| new bundle on a clean machine | `patchy-origin`, GPG verified | updates |
| new bundle over a 1.04 or older bundle | `patchy-origin` gains the URL but stays `no-gpg-verify` | updates, unverified |
| new bundle over a repository install | unchanged | updates |

Bundles before 1.05 have no origin URL and can only be replaced by a newer bundle
(their own update dialog printed a curl line for that). `flatpak install --or-update` with
the flatpakref fails with "already installed", so it is not a universal update command.

### Signing key (one-time, on the linux build host)

    gpg --batch --passphrase '' --quick-gen-key "Patchy Flatpak Repository" rsa4096 sign never
    gpg --list-secret-keys --with-colons | awk -F: '/^fpr:/ {print $10; exit}'
    echo 'export PATCHY_FLATPAK_GPG_KEY=<that fingerprint>' >> ~/.patchy-release-env

`release-linux.ps1` sources `~/.patchy-release-env` and sets
`PATCHY_REQUIRE_FLATPAK_REPO=1`, so a release without the key fails instead of shipping
a bundle that installs and then never updates. Run by hand without a key,
`make-flatpak.sh` builds an update-less bundle and says so. `PATCHY_FLATPAK_GPG_HOMEDIR`
points at a keyring outside `~/.gnupg`.

Back the secret key up off the host (`gpg --export-secret-keys --armor <fingerprint>`)
and never commit it. Every install pins the public key, so a lost key means every user
has to remove and re-add the remote.

### Testing the repository without publishing

Generate a throwaway key in a scratch `PATCHY_FLATPAK_GPG_HOMEDIR`, set
`PATCHY_FLATPAK_REPO_URL=http://127.0.0.1:<port>/repo/`, build, and serve
`build/flatpak-publish` with `python3 -m http.server`. Install into an empty
installation: point `FLATPAK_USER_DIR` and `FLATPAK_SYSTEM_DIR` at empty directories
and add `--sideload-repo=$HOME/.local/share/flatpak/repo` so the runtime is copied from
the host instead of downloaded. A second `build-commit-from --force --timestamp=NOW`
plus `build-update-repo` stands in for the next release.

## The manifest

`flatpak/com.rtsoft.patchy.yml`. Qt comes from the `org.kde.Platform` runtime and is
deliberately not vendored. The runtime is 6.11 (GCC 15.2.0 in the SDK), while the
native presets develop against Qt 6.8.3. Keep it on a branch that is not end-of-life:
6.8 and 6.9 went end-of-life in 2026, after which every `flatpak update` warned Patchy
users. Check with `flatpak remote-info
--user flathub org.kde.Platform//<branch>` (an "End-of-life" line) and after a bump
rerun the whole script and both full suites (`remote-build.ps1 -Target linux` builds
and tests inside the same SDK; see [docs/linux-testing.md](../../docs/linux-testing.md)).

`make-flatpak.sh` renders `build/flatpak-release-manifest.yml` from that source using
`scripts/release/prepare-flatpak-manifest.py`. The generated manifest has an absolute
source path and a fresh build-stamp token: packaging rebuilds the app module even
when its sources are unchanged, while dependency modules can retain their caches.
The committed manifest is never rewritten by packaging.

- `--filesystem=home` is the deliberate v1 choice (recents, CLI file arguments, and
  brush/palette folders behave like a desktop editor; file dialogs still go through
  the portal).
- `--share=network` exists only for the update check. A `-DPATCHY_STORE_BUILD=ON` build
  has no update check and no preference for it (`update_checks_available()` in
  `src/ui/update_checker.cpp`); `PATCHY_NO_UPDATE_CHECK=1` does the same at run time.
- HEIC needs nothing in the manifest: the runtime ships kimageformats' HEIF plugin and
  libheif, and its freedesktop 25.08 base declares `codecs-extra` (the HEVC decoder),
  which flatpak installs with the runtime for repository and bundle installs alike
  (verified October 2026: the committed `quadrants.heic` opens in a fresh install).
  Patchy bundles no HEVC code. The open-error hint in `heif_document_io_linux.cpp` names
  `org.freedesktop.Platform.codecs-extra//25.08-extra` and must follow the runtime's
  base version.
- The source is `type: dir`, skipping build output and root `test-artifacts` (Unix
  sockets left by interrupted MCP tests are not a packaging input).

`com.rtsoft.patchy.desktop`, `com.rtsoft.patchy.metainfo.xml` and `icons/hicolor/*` are
installed by CMake's `UNIX AND NOT APPLE` rules (binary in `bin/`, resources under
`share/patchy/`, licenses under `share/licenses/com.rtsoft.patchy`). The PNG icons and
scalable SVG share the folded source artwork; regenerate with
`scripts/dev/generate-branding.py` (see [branding](../branding/README.md)). Each version
bump adds a metainfo `<release>` with a short `<description>` (docs/release-process.md).
The metainfo screenshots are raw GitHub URLs at a release tag, so they keep resolving. They
are the README captures in `docs/images/screenshots/` (`scripts/make-readme-screenshots.ps1`);
when they are regenerated, point the URLs at the next release's tag so the listing (Flathub,
the AppImage catalog) shows the shipping UI once that tag exists.
`make-flatpak.sh` fails when the exported desktop file or the metainfo does not pass the
host's `desktop-file-validate` or `appstreamcli validate --no-net` (each skipped with a note
when not installed). The AppImage ships a copy of the desktop file without the Desktop
Entry 1.5 `SingleMainWindow` key ([docs/appimage.md](../../docs/appimage.md)).

## Sandbox notes

Known Wayland caveats (accepted for v1): a second launch raises the running window
but compositors may only flash the taskbar entry instead of stealing focus (no
xdg-activation token), and clipboard content set by Patchy vanishes when the app
exits (no clipboard manager in the sandbox). `--socket=fallback-x11` lets users force
`QT_QPA_PLATFORM=xcb` if a Wayland quirk bites.

Headless runs need no `--env`: `flatpak run com.rtsoft.patchy --headless --run-script
/path/to/script.js --script-output /path/to/out.txt` selects Qt's offscreen platform
inside the sandbox (the runtime ships the plugin), and `--filesystem=home` covers the
script, the output file, and the documents it opens. Offscreen startup disables the
process's desktop D-Bus connection so Qt's portal appearance query cannot delay a
headless command or MCP client disconnect; Qt 6.11 prints one "portal.Settings.ReadAll
failed" line for it, which is expected. The default MCP socket lives in
`$XDG_RUNTIME_DIR/app/$FLATPAK_ID`, shared by separate app and connector sandboxes. It
retains per-user socket permissions; the private `/tmp` in each invocation cannot
support this attachment. `make-flatpak.sh` runs the headless command inside the built
sandbox (`flatpak-builder --run`, nothing installed) before anything is exported and
fails unless the script output ends in `[done]`.

Running `tests/mcp_client_tests.py` inside the sandbox (the release check: `flatpak run
--user --devel --command=bash com.rtsoft.patchy -c 'cd ~/patchy/src &&
.deps/mcp-client-flatpak/bin/python tests/mcp_client_tests.py /app/bin/patchy-mcp'`)
needs a virtualenv made by the SDK's own Python. The host's `.deps/mcp-client` targets
the host Python and fails there with `No module named 'mcp'` once the versions differ
(6.11 ships Python 3.13). Create it once per runtime bump from the same sandbox shell:
`python3 -m venv .deps/mcp-client-flatpak && .deps/mcp-client-flatpak/bin/python -m pip
install 'mcp>=1.26,<2'`. `tests/flatpak_mcp_tests.py` runs on the host and keeps using
`.deps/mcp-client`.

The Patchy Flatpak installed on the linux build host is a manual test install and the
release flow never refreshes it. To test the shipped bundle there, reinstall it first:
`flatpak install --user -y --reinstall --bundle build/package/Patchy-<version>.flatpak`,
then `flatpak run --user com.rtsoft.patchy --headless --run-script ...`.
