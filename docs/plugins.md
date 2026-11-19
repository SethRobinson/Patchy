# Plugins

Patchy's plug-in support: the classic Photoshop `.8bf` filter host and the unused native C ABI.

Related documents:

- [plugin-host.md](plugin-host.md): running `.8bf` filters and the helper process (`src/plugins/host/`).

## Legacy Photoshop .8bf filters

Windows runs both 32-bit and 64-bit filter plug-ins. On macOS, Linux and the web, the **Legacy Photoshop Plug-ins** submenu shows a disabled Windows-only note. Those builds omit the scan, the folder commands and the plug-in Preferences page. Other entries in Plugins, including [Companions](companions.md), have their own platform rules.

### Where plug-ins come from

- Scan roots, recursive, `*.8bf` (`*.8bi` and `*.8li` are reported as unsupported, never offered; the `.8bi` reason points at [dds.md](dds.md)): `plugins` next to `patchy.exe`, `<AppDataLocation>/plugins` (`%APPDATA%\RTsoft\Patchy\plugins`, beside the user scripts and fonts stores), every folder in the persisted setting `plugins/userFolders` (`stored_legacy_plugin_folders()` in `app_settings.hpp`, a compatibility contract), and, only when it exists, `<appdir>/test-fixtures/photoshop-plugins` (the UI tests stage the fetched fixtures there; packages never ship them).
- **The plug-ins folder** is `plugins` next to `patchy.exe` (`legacy_plugins_folder_path()`). Plugins > Open Plug-ins Folder (`plugins.open_folder`, `pluginsOpenFolderAction`) and the About row (`splashOpenPluginsFolderButton`) open it in Explorer. A script reading `patchy.plugins.folder` gets its path. Both routes call `ensure_legacy_plugins_folder()` to create the folder and its README if needed. `ui_legacy_plugins_readme_matches_packaged_file` compares that README with `packaging/plugins/README.txt`.
- **Rescan.** Plugins > Rescan Plug-in Folders (`plugins.rescan`, `pluginsRescanAction`) starts a worker scan and reports "Plug-in scan finished: N available[, M not usable]". Preferences > Plug-ins saves extra search folders and rescans on OK. Keep the `register_legacy_plugin_path` report format `"%1: %2 (%3, %4)"` for the file name, translated reason, kind and architecture.
- **Startup never waits for the scan**: `start_legacy_plugin_scan()` probes on a worker (`run_tracked_background_worker`), posts the result back and applies it on the UI thread (`finish_legacy_plugin_scan`, generation-checked so a synchronous `rescan_legacy_plugin_folders()` from a script or Preferences supersedes it; a rebuild under an open popup menu is retried a second later). The constructor raises the in-flight flag before its zero-delay timer, so `legacy_plugin_scan_in_flight()` is true from construction until the result lands and tests wait with `wait_for_legacy_plugin_scan`. While in flight and empty, the submenu shows "Scanning plug-in folders..." (`legacyPluginsScanningNote`). The probe itself is cheap: `read_pipl_from_pe_file` reads the headers and only the resource section (`pipl_partial_read_matches_full_read`).
- A scan is a pure file read: `LegacyPhotoshopAdapter::probe` reads the PE header for the machine type and `pipl::read_pipl_from_pe_file` (`src/plugins/pipl.{hpp,cpp}`) walks the PE resource directory for the `PIPL` resources and parses the property list (kind, name, category, `wx86`/`8664` entry point names, FilterCaseInfo, modes). Nothing is loaded, so a malicious file in a scanned folder runs nothing until the user picks its menu item. A plug-in without a PiPL is named after its file stem, has no category, and the host tries the conventional export names (`PluginMain`, `ENTRYPOINT`, ordinal 1).
- Windows PiPL layout, confirmed on the fixtures: `int16 1`, `int32 0`, `int32 count`, then properties `{vendor, key, id, length, data padded to 4}` with the four-character codes stored least-significant byte first (`'8BIM'` appears as `MIB8`).
- Entries live in `MainWindow::legacy_plugins_` (`LegacyPluginEntry`: identifier, absolute path, probe). Identifiers are `legacy.photoshop.<file stem>` (the persisted form since the first release); a second file with the same stem gets `.2`, `.3`. The same plug-in present in two scanned folders (same file name, size, PiPL name and bitness: a pack copied into both the app and the user folder) is listed once, the first root wins, and the rescan report says so (`ui_legacy_plugin_folder_scan_builds_category_menu`). `plugin_host_` still holds a `PluginDescriptor` per supported entry.
- **Menu entries.** `rebuild_legacy_plugins_menu` groups supported filters by PiPL category (`legacyPluginCategoryMenu`) under `legacyPluginsMenu`. Each document action (`legacyPluginAction`) stores the identifier and displays the PiPL name through `escape_qaction_ampersands`. Rebuilds unregister the old actions first. An empty scan leaves the disabled `legacyPluginsEmptyNote`. Submenu actions use `NoRole`.
- **Paired builds.** When 32-bit and 64-bit files share a PiPL name, category and folder, the menu shows the 64-bit filter. The 32-bit filter remains in `patchy.plugins.list()` and runs by id. A 32-bit-only filter or a copy in a separate folder has its own menu entry.

### Running and the helper

The host loop, the per-plug-in quirks and the out-of-process helper in `src/plugins/host/` are in [plugin-host.md](plugin-host.md).

### Safety

Scanning executes nothing. Running a plug-in executes third-party code with the user's permissions inside the helper process (the Preferences page says so); the helper is crash-isolated, not a security sandbox. Third-party plug-ins live in gitignored `local-test-fixtures/photoshop-plugins/` and are hash-checked and Defender-scanned before a first run on an offload machine, never on the dev box (agents_local.md).

### Scripting

Clicks, typing, and captures confined to Patchy's hosted plug-in windows (KPT's framed
window included, through window-targeted messages and `PrintWindow`) need no further
permission; desktop screenshots, global input, and control of other applications do
(AGENTS.md). Use task-owned instances with isolated settings. See
[visual-qa.md](visual-qa.md#permissions-and-task-owned-instances).

| Scripting interface | Behavior |
|---|---|
| `patchy.plugins.list()` | Returns `{id, name, category, path, supported, reason, architecture}` for each file in the last scan. |
| `patchy.plugins.rescan()` | Refreshes the list. |
| `patchy.plugins.folders` | Reads or sets the stored extra folders. Setting the list triggers a scan. |
| `patchy.plugins.folder` | Returns the install's plug-in folder, creating it and its README on demand. Returns an empty string off Windows. |
| `layer.applyPlugin(id, {dialog?, captureDialog?})` | Runs a filter on an 8- or 16-bit pixel layer. `dialog` is a boolean and `captureDialog` is a PNG path. Unknown options throw. See [host bit-depth handling](plugin-host.md). |

**Dialog capture.** `DialogAcceptor` selects the helper's largest visible top-level window,
so a small stub window cannot hide KPT's full-screen canvas. After the settle delay it
captures that window with `PrintWindow(PW_RENDERFULLCONTENT)`. `WindowDpiContextScope`
measures and renders in the helper's DPI context to avoid black margins on scaled
displays. An attended run with `dialog: true` leaves the dialog to the user. An
unattended run can capture and answer it. On a noninteractive Windows station the
helper can answer hidden `#32770` dialogs but cannot capture them (`dialogs_can_be_hidden`).

**Unattended responses.** For a window that stays open, the runner tries its OK button
or IDOK command, Enter, then WM_CLOSE and SC_CLOSE. After eight responses it stops the
helper and reports cancellation. A plugin's modal error on the ordinary menu path
waits for the user.

| Developer setting | Purpose |
|---|---|
| `PATCHY_8BF_DIALOG_SETTLE_MS` | Time to leave a new helper window alone before capture or response. Default 700 ms. |
| `PATCHY_8BF_ACCEPT_CLICK=dx,dy` | First click at that offset from the window's bottom-right corner. KPT's OK mark accepts `30,30`. |
| `PATCHY_8BF_TRACE=1` | Log helper window classes, titles and sizes, and every `advanceState` request. |

### Compatibility checks

- Filter Foundry and Mehdi's Absolute Color run in both bitnesses. The Filter Foundry Greyscale fixture can show a "plugin debug: DialogBoxParam failed" modal with a real owner window (64-bit, also 32-bit after a parameter block exists). Tests use `{dialog:false}` for the 64-bit fixtures and a fresh 32-bit copy for menu tests.
- Kai's Power Tools 5 Blurrrr and Orb-It apply their results with the full-screen MetaOS interface and preview. KPT 3 is untested.
- [G'MIC-Qt for 8bf hosts](https://github.com/0xC0000054/gmic-8bf/releases) 4.0.4 x64 ran Sepia on a layer and inside a selection. Unzip it into the plug-ins folder. It needs the Parameters selector and a real display. Unattended or headless use ends as cancelled. 16-bit processing, transparency, resizing filters and the 32-bit build are untested.
- The `plugin_dialog` README scene uses Mehdi's Absolute Color through `captureDialog` and skips without the local `photoshop-plugins/mehdi` fixtures (`scripts/dev/readme-shots/plugin-dialog.js`).

### Tests

The README's KPT shot comes from `scripts/make-readme-screenshots.ps1 -SkipBuild -Scene
plugin_kpt5` (Orb-It in an isolated fixture copy, direct captures of Patchy and the
framed plug-in; inputs and geometry in [readme-showcase.md](readme-showcase.md)).

- Core (`tests/core/pipl_tests.cpp`, filter `pipl`/`legacy`): PiPL blob round trip, the four fetched Filter Foundry fixtures' property lists (every platform), the probe's names and bitness, and on Windows the helper self-tests in both bitnesses plus the crash exit code. `plugin_host_and_legacy_probe_work` and the Unicode probe tests remain.
- UI (`tests/ui/legacy_plugin_tests.cpp`, filter `legacy_plugin`): folder scan and category submenu through `patchy.plugins`, non-filter files listed but not offered, a selection-limited run in both bitnesses with undo (Windows), the Plugins menu commands and `patchy.plugins.folder` creating the README (`ui_legacy_plugin_menu_opens_folder_and_rescans`), the About row (`ui_about_dialog_has_plugins_folder_row`), the packaged README pin, the companion box offscreen (`ui_legacy_plugin_run_dialog_states`, every platform), the Repeat commands (`ui_legacy_plugin_repeat_last_commands`), the job object (`ui_legacy_plugin_process_tree_ends_child_processes`), `ui_legacy_plugin_mehdi_filters_run_if_available` and `ui_legacy_plugin_mehdi_dialog_capture_if_available` (also pins the box's waiting state) over the local third-party set, and `ui_legacy_plugin_ui_absent_off_windows` on the other platforms. KPT has no automated suite case; its commercial fixtures stay local. Explicitly invoked scripted captures of its framed window are permitted under the rules above, including targeted acceptance with `PATCHY_8BF_ACCEPT_CLICK`. Tests that expect plug-in actions call `wait_for_legacy_plugin_scan` first. `ui_bundled_legacy_plugin_action_applies_filter` and `ui_transparency_checkerboard_and_copy_paste_preserve_alpha` go through the real host.
- Fixtures: Filter Foundry's Greyscale and White to Transparent (both bitnesses) are GPL binaries fetched by `scripts/fetch-test-plugins` into `local-test-fixtures/photoshop-plugins/filter-foundry/` (hashes, licenses: `test-fixtures/photoshop-plugins/README.md`); tests skip without them.

## Native plugin ABI

- `src/plugins/plugin_api.h` declares a C ABI (`PATCHY_PLUGIN_ABI_VERSION`, reverse-DNS identifiers) and `plugin_host.*` keeps an in-memory descriptor list. Nothing loads native plug-ins yet; the legacy entries register descriptors there for bookkeeping only.
