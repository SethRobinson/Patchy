# Startup: the Home Screen with recent files

Split from [startup-performance.md](startup-performance.md), which owns the phase trace, the benchmark (`patchy_perf_tests startup`) and its seeded profile (`PATCHY_PERF_STARTUP_RECENT`, `_GRID`, `_THUMBNAILS`, `_WINDOW`). This page holds the measurements for GitHub issue 750: the Home Screen's header layout, the Recent Files grid with thumbnails ([ui-conventions-menus.md](ui-conventions-menus.md#recent-file-thumbnails)), the grid as the default view, and pictures for every kind of file.

The rules the numbers guard: nothing thumbnail-related runs before the first paint; only the cells in view are asked for; the first loads wait for the event loop's first idle moment; the worker runs at low priority, one file at a time; the folder sweep runs 4 s after the window is built. Guard test: `ui_start_panel_grid_does_no_thumbnail_work_before_its_first_paint`.

## The grid and the layout

GitHub issue 750 (the header layout, Grid view with thumbnails, the section that grows with the window). A/B on an idle offload host (i7-12700KF, 20 threads, Windows 11), `opt-dev` preset on both sides, update check off, 12 warm launches per build interleaved; reference: the commit before the first #750 merge, which is also before the Assets panel and other later work. Medians in ms, new vs reference (MAD of the new build):

| Scenario | headless ready | real window ready | real window first paint | `recent_thumbnails.first_cells` (window) |
|---|---|---|---|---|
| fresh profile | 213.6 vs 212.5 (1.6) | 316.1 vs 323.1 (16.2) | 223.1 vs 219.7 | |
| 200 recent files, List | 224.1 vs 218.5 (5.8) | 362.3 vs 376.8 (59.1) | 229.2 vs 224.3 | |
| 200, Grid, thumbnails cached | 225.2 vs 219.5 (6.2) | 313.1 vs 317.1 (5.4) | 226.7 vs 222.6 | 46 (12 cells) |
| 200, Grid, cache empty | 220.6 vs 220.3 (3.6) | 321.8 vs 361.0 (20.9) | 227.3 vs 223.6 | 483 (12 cells decoded) |
| 200, Grid, cached, 3126 x 1283 window | 221.1 vs 218.7 (1.5) | 347.6 vs 336.2 (23.4) | 230.2 vs 223.8 | 126 (45 cells) |

The grid then became the default view. Measured the same way against the build above, on profiles with no stored view (the new build opens in the grid, the reference in the list), medians in ms: fresh profile, headless ready 216.4 vs 217.1, real window 312.7 vs 324.1 with first paint 222.9 vs 223.4; 200 recent files, headless 222.1 vs 222.6 with the cache filled and 222.3 vs 222.2 with it empty, real window 329.7 vs 321.4 (MAD 33) and 359.9 vs 395.1 (MAD 65), first paint 228.8 vs 227.0 and 231.1 vs 240.8; the 3126 x 1283 window 339.9 vs 347.2 with first paint 227.3 vs 226.7. A first start after updating, with an empty cache and the seeded files (a third of them 24 megapixel JPEGs), has all 12 pictures 0.6 s after the first paint at the default window size and all 45 on the large window after 2.4 s, on the one background worker.

Every row is inside the margins. The phases the Home Screen owns do not move: `mw.tabs_start_panel` 4.4 vs 4.3 ms, `mw.view_recent` 6.0 vs 6.3 ms with 200 entries (3.5 with none). The 1 to 6 ms before the first paint is in `mw.docks`, `mw.stylesheet` and `main_window.show`, where the reference also lacks the Assets panel. Before the first loads were held for idle, the large window measured 376.9 vs 338.1 ms ready: 45 arrivals kept the event loop from resting. Cold (headless, 5 launches): ready 276 vs 260 on a fresh profile and 279 to 294 vs 265 to 272 with the seeded ones; the difference is in `mw.actions`, `mw.filters_codecs` and `mw.docks` and is the same with an empty Home Screen, so it is not this feature's.

## Pictures for every kind

The worker then learned to picture every kind Patchy opens (a quick pass for embedded previews and images Qt decodes, a slow pass through Patchy's own readers, Qt PDF and the SVG renderer; the slow pass waits behind every quick one). The benchmark's seeded profile gained those kinds, so these rows are not comparable with the tables above: the same 200 files are in both profiles, and the reference (main before the change) leaves two thirds of them without a picture. Same host and method, profiles with no stored view (both builds open in the grid), 12 warm launches per build, medians in ms, new vs reference (MAD of the new build):

| Scenario | headless ready | real window ready | real window first paint | `recent_thumbnails.first_cells` (window) |
|---|---|---|---|---|
| fresh profile | 213.5 vs 214.7 (3.3) | 313.6 vs 309.9 (5.4) | 224.4 vs 223.0 | |
| 200 recent files, thumbnails cached | 221.6 vs 222.7 (4.4) | 312.7 vs 315.2 (2.2) | 226.8 vs 230.5 | 44 vs 38 (12 cells) |
| 200, cache empty | 222.2 vs 224.9 (3.5) | 317.1 vs 347.2 (17.7) | 228.8 vs 231.2 | 574 vs 326 (11 pictures made vs 5) |
| 200, cached, 3126 x 1283 window | 221.1 vs 222.1 (4.8) | 344.7 vs 339.8 (9.1) | 232.9 vs 229.5 | 117 vs 80 (45 cells) |
| 200, cache empty, 3126 x 1283 window | 222.5 vs 221.9 (2.7) | 368.6 vs 399.7 (45.5) | 224.0 vs 261.9 | 2327 vs 937 (42 pictures made vs 15) |

Every row is inside the margins, and `mw.tabs_start_panel` (4.9 ms) and `mw.view_recent` (6.1 to 6.4 ms with 200 entries) do not move. `recent_thumbnails.first_cells` is the time from the first request (the first paint) until the last cell in view has its answer, on the one background worker: with a filled cache the pictures of all 45 cells are up 0.12 s after the first paint; a first start with an empty cache makes 11 pictures in 0.6 s at the default window size and 42 in 2.3 s on the large window (three seeded HEIF files have no picture on a host without the HEVC codec). The window is painted and responsive throughout.

**Peak memory**, one launch on the seeded profile with an empty cache (offscreen, `PeakWorkingSet64`): 308 MB against the reference's 110 MB on the 3126 x 1283 window (125 MB once the pictures are made), 112 MB against 98 MB at 1280 x 860. The peak is the largest file being read, here the seeded OpenRaster file of four 6 megapixel layers.

**Worst cases**, one recent file just under a cap, alone in the list (headless, files made with GIMP 3.2): a 89.9 MB GIMP file of four 11.5 megapixel layers of noise (under the 96 MB byte cap) has its picture 0.30 s after it is asked for and peaks 266 MB over an empty Home Screen; a GIMP file of four 6000 x 4000 layers (exactly the 384 MB decoded cap) takes 0.31 s and peaks 553 MB over. Both give the memory back when the picture is stored. On the very first start of a fresh profile, while the default pattern and brush libraries are seeded on other workers, the same files take 0.6 s. A file past a cap, a damaged file and a password-protected PDF cost one stat and one small marker read on later starts.
