# Repository Instructions

Keep this file current: when a change makes anything in here stale (build steps, test conventions, gotchas), update it in the same change. Multiple different coding agents read this file — it is the shared knowledge channel, so write notes for any AI, not a specific one.

When bumping the release version, update all three places: `CMakeLists.txt` (`project(... VERSION x.y)`), `vcpkg.json` (`version-semver`), and `latest_version.json` (the windows `version` — this is the update-check manifest, served to the app from raw.githubusercontent.com on main, so it only takes effect once pushed).

When adding or changing user-facing English text, make sure it is wired through Patchy's localization system and update the Japanese translation in `translations/patchy_ja.ts` in the same change.

If tests need files from outside the project directory, copy those files into `local-test-fixtures` first and have the tests read them from there. Do not add hardcoded external drive paths such as `C:\temp` or `D:\projects` to test code.

## Testing notes

- `patchy_ui_visual_tests.exe` must run with `QT_QPA_PLATFORM=offscreen`. The offscreen platform does **not** enumerate installed Windows fonts; register what a test needs with `QFontDatabase::addApplicationFont("C:/Windows/Fonts/<file>.ttf")`. Never call `removeApplicationFont` to clean up — invalidating an in-use font cache can hard-crash the suite.
- A registered font may not appear under its familiar GDI name: the offscreen FreeType database uses the OpenType *typographic* family, so ariblk.ttf registers as family "Arial" + style "Black", not "Arial Black". Patchy's text code resolves such names via `available_text_family_style_match` (main_window.cpp); tests should not gate on `QFontDatabase::families().contains("Arial Black")`-style checks.
- The test harness `CHECK()` macro throws. A failing CHECK while a `MainWindow` with an open inline text editor is still alive aborts the process during unwind (exit code 3, no `[FAIL]` line printed). Prefer asserting after the editor session is committed/closed, or structure tests so CHECKs that may fail do not unwind past a live editor.
- Tests save PNGs via `save_widget_artifact(...)` into `test-artifacts/` next to the binary — inspect them to visually confirm rendering behavior.
- Run a subset of visual tests by passing a name substring as the first argument (or `PATCHY_UI_TEST_FILTER`): `.\patchy_ui_visual_tests.exe ui_audio_splitter`. There is no `--test` flag.

After making code changes in this repository, create or refresh the local release build before final handoff.

Required release handoff steps:

1. Build the release preset:

   ```powershell
   cmd /s /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset release'
   ```

2. Run the release test binaries from `build\release`:

   ```powershell
   .\patchy_core_tests.exe
   $env:QT_QPA_PLATFORM='offscreen'; .\patchy_ui_visual_tests.exe
   ```

3. In the final response, explicitly report whether the release executable exists at:

   ```text
   build\release\patchy.exe
   ```

Do not say a release was created unless the release preset build completed successfully.
