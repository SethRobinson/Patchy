# Photoshop Plug-in Test Fixtures

These `.8bf` files are downloaded test fixtures for the legacy Photoshop plug-in support (docs/plugins.md): the property-list reader parses them on every platform, and on Windows the test suites run them through the out-of-process host (`patchy-8bf-host32.exe` / `patchy-8bf-host64.exe`). They are test data only: never linked into Patchy and not shipped by the app target (developer builds copy them next to the binary, where the plug-in scan picks them up).

- `Greyscale.8bf` (32-bit) and `Greyscale64.8bf` (64-bit)
- `White to Transparent.8bf` (32-bit) and `White to Transparent64.8bf` (64-bit)

Source: <https://misc.daniel-marschall.de/photoshop_filters/>

The source page describes these as Photoshop filters created with Filter Foundry and provides 64-bit Photoshop downloads plus source files.

## Provenance and licensing (recorded 2026-10-20)

| File | Built with | SHA-256 |
|---|---|---|
| `Greyscale.8bf` | Filter Foundry 1.7.0.16 (32-bit) | `ab0533c931ddb49093f09b5f80cc5e02b007c36b311572db19bb780a0a04fd6d` |
| `Greyscale64.8bf` | Filter Foundry 1.7.0.16 (64-bit) | `301a1c7766667c3bff4d83b123341f58c95acc16ce504b9b8f5e47098149e6e0` |
| `White to Transparent.8bf` | Filter Foundry 1.7.0.19 (32-bit) | `f5bf415279d3a645bcd32848bf7b07d05e247877e4ad46e6b3d0602b218f6625` |
| `White to Transparent64.8bf` | Filter Foundry 1.7.0.19 (64-bit) | `263e4b30f2ac1934fbd1c04572bbbd4120b9201a295281d9100b76ae03a8fa56` |

The Filter Foundry version comes from each file's embedded version information ("Built using Filter Foundry ...").

**What a standalone filter is.** Filter Foundry's Make Standalone command writes a copy of the Filter Foundry plug-in itself (a template binary it carries as a resource) and embeds the author's filter expressions as a parameter resource (`make_win.c` in the Filter Foundry repository). Each `.8bf` here therefore contains the complete Filter Foundry program plus Daniel Marschall's expressions for that filter. The expressions are also in the `.afs` parameter files beside the binaries (`Greyscale.afs`, `White to Transparent.afs`, downloaded 2026-10-20 from the source page).

**Licenses.**

- Filter Foundry is free software under the GNU General Public License, version 2 or (at your option) any later version. Copyright (C) 2003-2009 Toby Thain, Copyright (C) 2018-2022 Daniel Marschall, ViaThinkSoft (README and manual at <https://github.com/danielmarschall/filter_foundry>). The license texts are beside this file as `LICENSE-GPL-2.0.txt` and `LICENSE-GPL-3.0.txt`.
- The filter expressions: each file's embedded version information carries the author's own copyright field, "(C) 2022 Daniel Marschall, ViaThinkSoft. Apache 2.0 License." The download page itself states no terms.
- The 32-bit builds include the Open Watcom C run-time (Sybase Open Watcom Public License).

**Redistribution in this repository.** The four binaries are conveyed as object code under GPL version 3 (the "any later version" option; Apache-2.0 expressions are compatible with GPL v3), with the Corresponding Source offered from a third-party server as GPL v3 section 6(d) allows: the Filter Foundry repository above. The binaries record no build commit, so the source is identified by version: 1.7.0.16 is the history from commit `2132f428be3adb0e27a7639d66bc371f1003bd75` (the 1.7.0.16 release, 2022-03-22) up to but not including `af3b7ea81f8f715351e66252dff82ff4f18d2de5` (the 1.7.0.17 bump), and 1.7.0.19 is the history from `3e8ad897e5f964966e7f3c795207544458fd1519` (the 1.7.0.19 bump, 2022-06-30) through `9d3bce58a6c4d1b5384d1c1fcc2d024dc533211f` (2023-11-21, before the 1.7.0.20 bump). Together with the `.afs` files that is the source of the filters. The fixtures are mere aggregation with Patchy's MIT-licensed sources: nothing from them is linked into or shipped with Patchy.
