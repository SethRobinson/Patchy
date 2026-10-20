# Photoshop Plug-in Test Fixtures

The legacy plug-in tests (docs/plugins.md) run four standalone Photoshop filters built with Filter Foundry: Greyscale and White to Transparent, each as a 32-bit and a 64-bit `.8bf`. The property-list reader parses them on every platform, and on Windows the test suites run them through the out-of-process host (`patchy-8bf-host32.exe` / `patchy-8bf-host64.exe`). They are GPL binaries, so the repository does not carry them (they were committed here until October 20, 2026 and remain in the history before that date). Fetch them into the untracked local fixture tree:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch-test-plugins.ps1
```

```bash
scripts/fetch-test-plugins.sh
```

Both download the files below from the author's page into `local-test-fixtures/photoshop-plugins/filter-foundry/` and verify the SHA-256 hashes; tests that need them skip with an "optional fixture missing" line while the folder is absent. Nothing from them is linked into or shipped with Patchy; the UI tests stage copies in `test-fixtures/photoshop-plugins` next to the test binary, the developer scan root, and packages never include that folder.

Source: <https://misc.daniel-marschall.de/photoshop_filters/> (Daniel Marschall). The page describes the filters as created with Filter Foundry, states no license terms, and also publishes each filter's `.afs` expression file, which the scripts fetch alongside.

| File | Built with | SHA-256 |
|---|---|---|
| `Greyscale.8bf` | Filter Foundry 1.7.0.16 (32-bit) | `ab0533c931ddb49093f09b5f80cc5e02b007c36b311572db19bb780a0a04fd6d` |
| `Greyscale64.8bf` | Filter Foundry 1.7.0.16 (64-bit) | `301a1c7766667c3bff4d83b123341f58c95acc16ce504b9b8f5e47098149e6e0` |
| `White to Transparent.8bf` | Filter Foundry 1.7.0.19 (32-bit) | `f5bf415279d3a645bcd32848bf7b07d05e247877e4ad46e6b3d0602b218f6625` |
| `White to Transparent64.8bf` | Filter Foundry 1.7.0.19 (64-bit) | `263e4b30f2ac1934fbd1c04572bbbd4120b9201a295281d9100b76ae03a8fa56` |
| `Greyscale.afs` | expression source | `f08d9829f9bcaef1214390fc6ad20f87b6d0953b6cd3252b5090350474d97b35` |
| `White to Transparent.afs` | expression source | `56b6d8c9eb4064d319e656cbd9aef6d11cbfa8e052090ed4a729540dff6c87dc` |

The Filter Foundry version comes from each file's embedded version information ("Built using Filter Foundry ...").

**What a standalone filter is.** Filter Foundry's Make Standalone command writes a copy of the Filter Foundry plug-in itself (a template binary it carries as a resource) and embeds the author's filter expressions as a parameter resource (`make_win.c` in the Filter Foundry repository). Each `.8bf` therefore contains the complete Filter Foundry program plus Daniel Marschall's expressions for that filter.

**Licenses.**

- Filter Foundry is free software under the GNU General Public License, version 2 or (at your option) any later version. Copyright (C) 2003-2009 Toby Thain, Copyright (C) 2018-2022 Daniel Marschall, ViaThinkSoft (README, manual and `LICENSE_GPLv2.txt` at <https://github.com/danielmarschall/filter_foundry>).
- The filter expressions: each file's embedded version information carries the author's own copyright field, "(C) 2022 Daniel Marschall, ViaThinkSoft. Apache 2.0 License."
- The 32-bit builds include the Open Watcom C run-time (Sybase Open Watcom Public License).

**Corresponding Source.** The GPL's source obligation covers the whole Filter Foundry engine and build materials of the embedded versions, not only the `.afs` formulas. The upstream `releases/` folder names each release's Subversion revision and the git mirror carries `git-svn-id` lines, so the source is pinned: 1.7.0.16 is SVN revision 470, commit `2132f428be3adb0e27a7639d66bc371f1003bd75` (2022-03-22), and 1.7.0.19 is SVN revision 531, commit `d60906b7c3507c183873567a5680b8bf586c8b0d` (2022-06-30). The archives there (`FilterFoundry 1.7.0.16 (SVN Rev 470).zip`, `FilterFoundry 1.7.0.19 (SVN Rev 531).zip`) hold the FilterFoundry.8bf plug-ins these standalones were made from, not source. The repository no longer conveys the binaries (each developer downloads them from the author), so it carries no GPL obligation for them; anyone who redistributes copies needs that source reachable.
