# Per-file Testy results

[Run summary](README.md) | [Complete CSV data](results.csv) | [Benchmark methodology](../../../docs/psd-compatibility-benchmark.md)

This page is the GitHub-readable view of run `20260807-153700`. It contains 448 measurements across 64 PSD files and seven editors. No source documents, rendered images, heatmaps, resaved PSDs, local paths, hashes, or layer names are included.

## Editors at a glance

| Editor and build | Opened | Renders scored | Perceptual match | Photoshop round trip | Native-data score | Editable text | Rejected saves |
|---|---:|---:|---:|---:|---:|---:|---:|
| Photoshop 27.8.0 | 64 / 64 | 63 | 100.00% | 100.00% (n=63) | 100.00% | 312 / 312 | 0 |
| **Patchy `879a3a8`** | 64 / 64 | 63 | 98.83% | 99.98% (n=63) | 100.00% | 312 / 312 | 0 |
| Photopea web (Chrome host) | 63 / 64 | 62 | 97.13% | 99.76% (n=62) | 99.62% | 306 / 312 | 0 |
| Affinity 3.2.3.4646 | 61 / 64 | 60 | 88.30% | 88.02% (n=60) | 59.98% | 0 / 305 | 0 |
| GIMP 3.2.4 | 62 / 64 | 61 | 88.11% | 85.45% (n=61) | 54.62% | 0 / 312 | 0 |
| PhotoDemon 2026.01.0251 (testy CLI build) | 64 / 64 | 63 | 81.97% | 80.81% (n=63) | 52.93% | 0 / 312 | 0 |
| Krita 5.3.2.1 (git 0619060) | 56 / 64 | 56 | 81.17% | 88.47% (n=47) | 65.77% | 187 / 205 | 9 |

Perceptual and round-trip percentages are means over rows that produced those measurements. Native-data score is the mean retained-object fraction for saves Photoshop could inspect. The CSV retains four decimal places and every individual field.

## Per-editor details

Expand an editor to see all 64 files. The compact tables show the measurements most useful for review; use the CSV for byte-match, RMSE, SSIM, deltaE, individual layer-kind counts, clipping, and blend-setting fields.

<details open>
<summary><strong>Patchy 879a3a8</strong> | opened 64/64 | perceptual 98.83% (n=63) | rejected saves 0</summary>

| PSD file | Scan | Opened | Perceptual | PS round trip | Native objects | Editable text | Live effects | Masks | PSD save | Issue |
|---|---|---:|---:|---:|---:|---:|---:|---|---|---|
| <code>akiko_cycling_okinawa_with_curves.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>APP_Icon_1024x1024.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 66 / 66 | 4 / 4 | 24 / 24 | U 6 / 8; V 38 / 38 | Accepted | - |
| <code>Arduboy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 2 / 2 | - | U -; V - | Accepted | - |
| <code>AudioSplitterProject.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 3 / 3 | 3 / 3 | U -; V - | Accepted | - |
| <code>bevel_examine.psd</code> | **Flagged** | Yes | 99.36% | 100.00% | 3 / 3 | 1 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>C2Kyoto Nintendo NES Cartridge Label Template (Front).psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 415 / 415 | 107 / 107 | 6 / 6 | U 14 / 17; V 71 / 71 | Accepted | - |
| <code>CDi_A4.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 15 / 15 | 8 / 8 | 1 / 1 | U -; V - | Accepted | - |
| <code>checkbox.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | 1 / 1 | U -; V - | Accepted | - |
| <code>Choose a game sign.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 2 / 2 | - | U -; V - | Accepted | - |
| <code>deko_test.psd</code> | Passed | Yes | 100.00% | 100.00% | 4 / 4 | 1 / 1 | - | U -; V - | Accepted | - |
| <code>DS_ChipBoardToken.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 9 / 9 | 4 / 4 | 6 / 6 | U -; V - | Accepted | - |
| <code>Duke nukem mobile.psd</code> | **Flagged** | Yes | 98.83% | 100.00% | 9 / 9 | 5 / 5 | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_patchy.psd</code> | Passed | Yes | 99.95% | 100.00% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_photoshop.psd</code> | Passed | Yes | 99.95% | 100.00% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_original.psd</code> | Passed | Yes | 99.67% | 100.00% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>EonKun Goodboy Chest.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 9 / 9 | 1 / 1 | - | U 0 / 2; V - | Accepted | - |
| <code>Eons card.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 7 / 7 | 1 / 1 | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>Flat-filter-list.psd</code> | **Flagged** | Yes | 99.20% | 100.00% | 11 / 11 | 1 / 1 | 5 / 5 | U -; V 5 / 5 | Accepted | - |
| <code>generic_bg.psd</code> | **Flagged** | Yes | 94.71% | 100.00% | 95 / 95 | 25 / 25 | 33 / 33 | U 1 / 1; V - | Accepted | - |
| <code>Horror VirtualBoy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 6 / 6 | 2 / 2 | - | U -; V - | Accepted | - |
| <code>interface_mock2.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 11 / 11 | - | - | U -; V - | Accepted | - |
| <code>ipad_main_v04.psd</code> | Passed | Yes | 100.00% | 100.00% | 29 / 29 | 8 / 8 | 10 / 10 | U -; V - | Accepted | - |
| <code>Launcher-icon-template.psd</code> | **Flagged** | Yes | 99.27% | 100.00% | 283 / 283 | - | 125 / 125 | U 19 / 19; V 186 / 186 | Accepted | - |
| <code>LaunchImagesAndIcons-2016.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 210 / 210 | 40 / 40 | - | U 60 / 60; V 52 / 52 | Accepted | - |
| <code>mow_master.psd</code> | **Flagged** | Yes | 97.12% | 100.00% | 36 / 36 | 5 / 5 | 10 / 10 | U -; V - | Accepted | - |
| <code>patchy-plastic-wrap-photoshop-resave.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>pinball_from_photoshop.psd</code> | **Flagged** | Yes | 92.22% | 100.00% | 6 / 6 | - | 3 / 3 | U -; V - | Accepted | - |
| <code>pinball_retronight_poster_a3.psd</code> | **Flagged** | Yes | 83.04% | 99.42% | 49 / 49 | - | 29 / 29 | U 2 / 3; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_patchy.psd</code> | **Flagged** | Yes | 83.46% | 100.00% | 49 / 49 | - | 29 / 29 | U 2 / 2; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_photoshop.psd</code> | **Flagged** | Yes | 83.04% | 99.42% | 49 / 49 | - | 29 / 29 | U 2 / 3; V - | Accepted | - |
| <code>Polymega jump test.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 1 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>polymega_famicom.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | 1 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>ps2026-16bit-flat.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026-16bit.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026-32bit-flat.psd</code> | **Flagged** | Yes | n/a | n/a | 1 / 1 | - | - | U -; V - | Accepted | **no render comparison** |
| <code>ps2026-32bit.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_converted_smart_object.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_place_fit_large.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_probe.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_small.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_smart_filter.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_via_copy.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>restaurant-menu-inside.psd</code> | **Flagged** | Yes | 99.99% | 100.00% | 52 / 52 | 23 / 23 | 8 / 8 | U 3 / 4; V 2 / 2 | Accepted | - |
| <code>snes-box-a3.psd</code> | **Flagged** | Yes | 99.99% | 100.00% | 101 / 101 | 20 / 20 | 8 / 8 | U -; V 50 / 50 | Accepted | - |
| <code>Template.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 31 / 31 | 1 / 1 | 8 / 8 | U -; V - | Accepted | - |
| <code>tips.psd</code> | **Flagged** | Yes | 98.88% | 100.00% | 48 / 48 | 21 / 21 | 33 / 33 | U -; V - | Accepted | - |
| <code>Title Screen_demo.psd</code> | **Flagged** | Yes | 97.85% | 100.00% | 38 / 38 | 15 / 15 | 25 / 25 | U -; V - | Accepted | - |
| <code>Title02.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 29 / 29 | 6 / 6 | 2 / 2 | U 7 / 7; V - | Accepted | - |
| <code>tlm-main-mockup.psd</code> | **Flagged** | Yes | 99.83% | 100.00% | 3 / 3 | 2 / 2 | 1 / 1 | U -; V - | Accepted | - |
| <code>vectors_overlay_stroke.psd</code> | **Flagged** | Yes | 99.97% | 100.00% | 4 / 4 | - | 1 / 1 | U -; V 2 / 2 | Accepted | - |
| <code>weedkiller_skin.psd</code> | **Flagged** | Yes | 99.84% | 100.00% | 12 / 12 | 2 / 2 | 4 / 4 | U -; V - | Accepted | - |
| <code>wordpress_banner3.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 11 / 11 | - | - | U 1 / 2; V - | Accepted | - |

</details>

<details>
<summary><strong>Photopea web (Chrome host)</strong> | opened 63/64 | perceptual 97.13% (n=62) | rejected saves 0</summary>

| PSD file | Scan | Opened | Perceptual | PS round trip | Native objects | Editable text | Live effects | Masks | PSD save | Issue |
|---|---|---:|---:|---:|---:|---:|---:|---|---|---|
| <code>akiko_cycling_okinawa_with_curves.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>APP_Icon_1024x1024.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 66 / 66 | 4 / 4 | 24 / 24 | U 8 / 8; V 38 / 38 | Accepted | - |
| <code>Arduboy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 2 / 2 | - | U -; V - | Accepted | - |
| <code>AudioSplitterProject.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 3 / 3 | 3 / 3 | U -; V - | Accepted | - |
| <code>bevel_examine.psd</code> | **Flagged** | Yes | 94.65% | 99.79% | 3 / 3 | 1 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>C2Kyoto Nintendo NES Cartridge Label Template (Front).psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 415 / 415 | 107 / 107 | 6 / 6 | U 17 / 17; V 71 / 71 | Accepted | - |
| <code>CDi_A4.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 15 / 15 | 8 / 8 | 1 / 1 | U -; V - | Accepted | - |
| <code>checkbox.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | 1 / 1 | U -; V - | Accepted | - |
| <code>Choose a game sign.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 2 / 2 | - | U -; V - | Accepted | - |
| <code>deko_test.psd</code> | Passed | Yes | 100.00% | 100.00% | 4 / 4 | 1 / 1 | - | U -; V - | Accepted | - |
| <code>DS_ChipBoardToken.psd</code> | **Flagged** | Yes | 99.89% | 100.00% | 9 / 9 | 4 / 4 | 6 / 6 | U -; V - | Accepted | - |
| <code>Duke nukem mobile.psd</code> | **Flagged** | Yes | 99.37% | 100.00% | 9 / 9 | 5 / 5 | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_patchy.psd</code> | Passed | Yes | 99.95% | 100.00% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_photoshop.psd</code> | Passed | Yes | 99.95% | 100.00% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_original.psd</code> | Passed | Yes | 99.45% | 100.00% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>EonKun Goodboy Chest.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 9 / 9 | 1 / 1 | - | U 2 / 2; V - | Accepted | - |
| <code>Eons card.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 7 / 7 | 1 / 1 | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>Flat-filter-list.psd</code> | **Flagged** | Yes | 99.33% | 100.00% | 11 / 11 | 1 / 1 | 5 / 5 | U -; V 5 / 5 | Accepted | - |
| <code>generic_bg.psd</code> | **Flagged** | Yes | 84.91% | 100.00% | 95 / 95 | 25 / 25 | 33 / 33 | U 1 / 1; V - | Accepted | - |
| <code>Horror VirtualBoy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 6 / 6 | 2 / 2 | - | U -; V - | Accepted | - |
| <code>interface_mock2.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 11 / 11 | - | - | U -; V - | Accepted | - |
| <code>ipad_main_v04.psd</code> | Passed | Yes | 100.00% | 100.00% | 29 / 29 | 8 / 8 | 10 / 10 | U -; V - | Accepted | - |
| <code>Launcher-icon-template.psd</code> | **Flagged** | Yes | 98.43% | 100.00% | 283 / 283 | - | 125 / 125 | U 19 / 19; V 186 / 186 | Accepted | - |
| <code>LaunchImagesAndIcons-2016.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 210 / 210 | 40 / 40 | - | U 60 / 60; V 52 / 52 | Accepted | - |
| <code>mow_master.psd</code> | **Flagged** | Yes | 96.52% | 100.00% | 36 / 36 | 5 / 5 | 10 / 10 | U -; V - | Accepted | - |
| <code>patchy-plastic-wrap-photoshop-resave.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>pinball_from_photoshop.psd</code> | **Flagged** | Yes | 93.18% | 100.00% | 6 / 6 | - | 3 / 3 | U -; V - | Accepted | - |
| <code>pinball_retronight_poster_a3.psd</code> | **Flagged** | Yes | 78.91% | 100.00% | 49 / 49 | - | 29 / 29 | U 3 / 3; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_patchy.psd</code> | **Flagged** | Yes | 80.75% | 100.00% | 49 / 49 | - | 29 / 29 | U 2 / 2; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_photoshop.psd</code> | **Flagged** | Yes | 78.91% | 100.00% | 49 / 49 | - | 29 / 29 | U 3 / 3; V - | Accepted | - |
| <code>Polymega jump test.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 1 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>polymega_famicom.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | 1 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>ps2026-16bit-flat.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026-16bit.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026-32bit-flat.psd</code> | **Flagged** | Yes | n/a | n/a | 1 / 1 | - | - | U -; V - | Accepted | **no render comparison** |
| <code>ps2026-32bit.psd</code> | **Flagged** | Yes | 33.74% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_converted_smart_object.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_place_fit_large.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_probe.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_small.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_smart_filter.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_via_copy.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>restaurant-menu-inside.psd</code> | **Flagged** | Yes | 99.97% | 99.76% | 52 / 52 | 23 / 23 | 8 / 8 | U 4 / 4; V 2 / 2 | Accepted | - |
| <code>snes-box-a3.psd</code> | **Flagged** | Yes | 99.99% | 100.00% | 101 / 101 | 20 / 20 | 8 / 8 | U -; V 50 / 50 | Accepted | - |
| <code>Template.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 31 / 31 | 1 / 1 | 8 / 8 | U -; V - | Accepted | - |
| <code>tips.psd</code> | **Flagged** | Yes | 99.98% | 97.40% | 48 / 48 | 21 / 21 | 33 / 33 | U -; V - | Accepted | - |
| <code>Title Screen_demo.psd</code> | **Flagged** | Yes | 96.33% | 100.00% | 38 / 38 | 15 / 15 | 25 / 25 | U -; V - | Accepted | - |
| <code>Title02.psd</code> | **Flagged** | Yes | 88.29% | 88.29% | 22 / 29 | 0 / 6 | 0 / 2 | U 7 / 7; V - | Accepted | - |
| <code>tlm-main-mockup.psd</code> | **Flagged** | Yes | 99.74% | 100.00% | 3 / 3 | 2 / 2 | 1 / 1 | U -; V - | Accepted | - |
| <code>vectors_overlay_stroke.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>weedkiller_skin.psd</code> | **Flagged** | Yes | 99.95% | 100.00% | 12 / 12 | 2 / 2 | 4 / 4 | U -; V - | Accepted | - |
| <code>wordpress_banner3.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 11 / 11 | - | - | U 2 / 2; V - | Accepted | - |

</details>

<details>
<summary><strong>Affinity 3.2.3.4646</strong> | opened 61/64 | perceptual 88.30% (n=60) | rejected saves 0</summary>

| PSD file | Scan | Opened | Perceptual | PS round trip | Native objects | Editable text | Live effects | Masks | PSD save | Issue |
|---|---|---:|---:|---:|---:|---:|---:|---|---|---|
| <code>akiko_cycling_okinawa_with_curves.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 3 | - | - | U -; V - | Accepted | - |
| <code>APP_Icon_1024x1024.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 58 / 66 | 0 / 4 | 22 / 22 | U 3 / 8; V 37 / 37 | Accepted | - |
| <code>Arduboy.psd</code> | **Flagged** | Yes | 99.99% | 100.00% | 2 / 4 | 0 / 2 | - | U -; V - | Accepted | - |
| <code>AudioSplitterProject.psd</code> | **Flagged** | Yes | 79.98% | 80.08% | 1 / 4 | 0 / 3 | 3 / 3 | U -; V - | Accepted | - |
| <code>bevel_examine.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>C2Kyoto Nintendo NES Cartridge Label Template (Front).psd</code> | **Flagged** | Yes | 86.25% | 86.25% | 198 / 415 | 0 / 107 | 6 / 6 | U 11 / 17; V 70 / 71 | Accepted | - |
| <code>CDi_A4.psd</code> | **Flagged** | Yes | 99.81% | 99.79% | 7 / 15 | 0 / 8 | 1 / 1 | U -; V - | Accepted | - |
| <code>checkbox.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | 0 / 1 | U -; V - | Accepted | - |
| <code>Choose a game sign.psd</code> | **Flagged** | Yes | 88.36% | 88.35% | 2 / 4 | 0 / 2 | - | U -; V - | Accepted | - |
| <code>deko_test.psd</code> | Passed | Yes | 97.88% | 98.61% | 3 / 4 | 0 / 1 | - | U -; V - | Accepted | - |
| <code>DS_ChipBoardToken.psd</code> | **Flagged** | Yes | 63.58% | 63.81% | 5 / 9 | 0 / 4 | 6 / 6 | U -; V - | Accepted | - |
| <code>Duke nukem mobile.psd</code> | **Flagged** | Yes | 79.35% | 79.20% | 2 / 9 | 0 / 5 | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_patchy.psd</code> | Passed | Yes | 99.95% | 99.95% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_photoshop.psd</code> | Passed | Yes | 99.95% | 99.95% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_original.psd</code> | Passed | Yes | 99.95% | 99.95% | 5 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>EonKun Goodboy Chest.psd</code> | **Flagged** | Yes | 96.84% | 99.05% | 5 / 9 | 0 / 1 | - | U 0 / 2; V - | Accepted | - |
| <code>Eons card.psd</code> | **Flagged** | Yes | 81.78% | 82.05% | 6 / 7 | 0 / 1 | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>Flat-filter-list.psd</code> | **Flagged** | Yes | 93.98% | 94.11% | 7 / 11 | 0 / 1 | 3 / 5 | U -; V 3 / 5 | Accepted | - |
| <code>generic_bg.psd</code> | **Flagged** | Yes | 56.77% | 56.93% | 66 / 95 | 0 / 25 | 5 / 32 | U 1 / 1; V - | Accepted | - |
| <code>Horror VirtualBoy.psd</code> | **Flagged** | Yes | 98.64% | 98.63% | 4 / 6 | 0 / 2 | - | U -; V - | Accepted | - |
| <code>interface_mock2.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 9 / 11 | - | - | U -; V - | Accepted | - |
| <code>ipad_main_v04.psd</code> | Passed | Yes | 97.93% | 72.79% | 21 / 29 | 0 / 8 | 10 / 10 | U -; V - | Accepted | - |
| <code>Launcher-icon-template.psd</code> | **Flagged** | Yes | 97.88% | 93.56% | 186 / 283 | - | 51 / 123 | U 11 / 15; V 107 / 179 | Accepted | - |
| <code>LaunchImagesAndIcons-2016.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 116 / 210 | 0 / 40 | - | U 60 / 60; V 52 / 52 | Accepted | - |
| <code>mow_master.psd</code> | **Flagged** | Yes | 97.76% | 88.21% | 31 / 36 | 0 / 5 | 6 / 10 | U -; V - | Accepted | - |
| <code>patchy-plastic-wrap-photoshop-resave.psd</code> | **Flagged** | Yes | 86.89% | 86.89% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>pinball_from_photoshop.psd</code> | **Flagged** | Yes | 73.66% | 73.67% | 3 / 6 | - | 1 / 3 | U -; V - | Accepted | - |
| <code>pinball_retronight_poster_a3.psd</code> | **Flagged** | Yes | 39.96% | 30.11% | 20 / 49 | - | 10 / 29 | U 0 / 3; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_patchy.psd</code> | **Flagged** | Yes | 42.71% | 31.17% | 20 / 49 | - | 10 / 29 | U 0 / 2; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_photoshop.psd</code> | **Flagged** | Yes | 39.96% | 30.11% | 20 / 49 | - | 10 / 29 | U 0 / 3; V - | Accepted | - |
| <code>Polymega jump test.psd</code> | **Flagged** | Yes | 70.85% | 70.95% | 2 / 4 | 0 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>polymega_famicom.psd</code> | **Flagged** | Yes | 82.29% | 82.34% | 1 / 2 | 0 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>ps2026-16bit-flat.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026-16bit.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026-32bit-flat.psd</code> | **Flagged** | Yes | n/a | n/a | 1 / 1 | - | - | U -; V - | Accepted | **no render comparison** |
| <code>ps2026-32bit.psd</code> | **Flagged** | Yes | 0.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_converted_smart_object.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_after.psd</code> | Passed | Yes | 93.20% | 93.20% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_before.psd</code> | Passed | Yes | 93.20% | 93.20% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_place_fit_large.psd</code> | **Flagged** | Yes | 70.31% | 70.31% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_probe.psd</code> | **Flagged** | Yes | 48.49% | 48.49% | 0 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_small.psd</code> | **Flagged** | Yes | 84.94% | 84.94% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_smart_filter.psd</code> | **Flagged** | Yes | 88.41% | 88.41% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_via_copy.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>restaurant-menu-inside.psd</code> | **Flagged** | Yes | 98.04% | 92.09% | 26 / 52 | 0 / 23 | 8 / 8 | U 3 / 4; V 2 / 2 | Accepted | - |
| <code>snes-box-a3.psd</code> | **Flagged** | Yes | 99.98% | 99.51% | 73 / 101 | 0 / 20 | 5 / 8 | U -; V 47 / 50 | Accepted | - |
| <code>Template.psd</code> | **Flagged** | Yes | 99.69% | 99.69% | 30 / 31 | 0 / 1 | 8 / 8 | U -; V - | Accepted | - |
| <code>tips.psd</code> | **Flagged** | Yes | 92.91% | 92.47% | 27 / 48 | 0 / 21 | 32 / 33 | U -; V - | Accepted | - |
| <code>Title Screen_demo.psd</code> | **Flagged** | Yes | 83.89% | 49.21% | 22 / 38 | 0 / 15 | 22 / 25 | U -; V - | Accepted | - |
| <code>Title02.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>tlm-main-mockup.psd</code> | **Flagged** | Yes | 98.78% | 98.78% | 1 / 3 | 0 / 2 | 0 / 1 | U -; V - | Accepted | - |
| <code>vectors_overlay_stroke.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>weedkiller_skin.psd</code> | **Flagged** | Yes | 92.99% | 84.32% | 10 / 12 | 0 / 2 | 4 / 4 | U -; V - | Accepted | - |
| <code>wordpress_banner3.psd</code> | **Flagged** | Yes | 99.94% | 99.99% | 10 / 11 | - | - | U 1 / 2; V - | Accepted | - |

</details>

<details>
<summary><strong>GIMP 3.2.4</strong> | opened 62/64 | perceptual 88.11% (n=61) | rejected saves 0</summary>

| PSD file | Scan | Opened | Perceptual | PS round trip | Native objects | Editable text | Live effects | Masks | PSD save | Issue |
|---|---|---:|---:|---:|---:|---:|---:|---|---|---|
| <code>akiko_cycling_okinawa_with_curves.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>APP_Icon_1024x1024.psd</code> | **Flagged** | Yes | 96.47% | 23.50% | 24 / 66 | 0 / 4 | 0 / 18 | U 6 / 6; V 0 / 27 | Accepted | - |
| <code>Arduboy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 4 | 0 / 2 | - | U -; V - | Accepted | - |
| <code>AudioSplitterProject.psd</code> | **Flagged** | Yes | 92.11% | 92.11% | 1 / 4 | 0 / 3 | 0 / 2 | U -; V - | Accepted | - |
| <code>bevel_examine.psd</code> | **Flagged** | Yes | 92.86% | 92.86% | 2 / 3 | 0 / 1 | 0 / 1 | U -; V - | Accepted | - |
| <code>C2Kyoto Nintendo NES Cartridge Label Template (Front).psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 41 / 415 | 0 / 107 | 0 / 2 | U 14 / 14; V 0 / 31 | Accepted | - |
| <code>CDi_A4.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 7 / 15 | 0 / 8 | 0 / 1 | U -; V - | Accepted | - |
| <code>checkbox.psd</code> | **Flagged** | Yes | 24.80% | 24.80% | 2 / 2 | - | 0 / 1 | U -; V - | Accepted | - |
| <code>Choose a game sign.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 4 | 0 / 2 | - | U -; V - | Accepted | - |
| <code>deko_test.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 4 | 0 / 1 | - | U -; V - | Accepted | - |
| <code>DS_ChipBoardToken.psd</code> | **Flagged** | Yes | 79.92% | 79.92% | 5 / 9 | 0 / 4 | 0 / 5 | U -; V - | Accepted | - |
| <code>Duke nukem mobile.psd</code> | **Flagged** | Yes | 88.86% | 88.86% | 2 / 9 | 0 / 5 | 0 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_patchy.psd</code> | Passed | Yes | 98.58% | 96.78% | 6 / 6 | - | 0 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_photoshop.psd</code> | Passed | Yes | 98.58% | 96.70% | 6 / 6 | - | 0 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_original.psd</code> | Passed | Yes | 99.06% | 98.16% | 5 / 6 | - | 0 / 1 | U 1 / 1; V - | Accepted | - |
| <code>EonKun Goodboy Chest.psd</code> | **Flagged** | Yes | 88.00% | 88.00% | 3 / 9 | 0 / 1 | - | U 2 / 2; V - | Accepted | - |
| <code>Eons card.psd</code> | **Flagged** | Yes | 97.23% | 97.23% | 6 / 7 | 0 / 1 | - | U 1 / 1; V - | Accepted | - |
| <code>Flat-filter-list.psd</code> | **Flagged** | Yes | 96.71% | 96.71% | 4 / 11 | 0 / 1 | 0 / 5 | U -; V 0 / 5 | Accepted | - |
| <code>generic_bg.psd</code> | **Flagged** | Yes | 40.28% | 40.28% | 63 / 95 | 0 / 25 | 0 / 32 | U 1 / 1; V - | Accepted | - |
| <code>Horror VirtualBoy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 6 | 0 / 2 | - | U -; V - | Accepted | - |
| <code>interface_mock2.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 11 / 11 | - | - | U -; V - | Accepted | - |
| <code>ipad_main_v04.psd</code> | Passed | Yes | 92.52% | 93.20% | 21 / 29 | 0 / 8 | 0 / 10 | U -; V - | Accepted | - |
| <code>Launcher-icon-template.psd</code> | **Flagged** | Yes | 85.45% | 85.74% | 89 / 283 | - | 0 / 120 | U 19 / 19; V 0 / 179 | Accepted | - |
| <code>LaunchImagesAndIcons-2016.psd</code> | **Flagged** | Yes | 25.70% | 25.69% | 62 / 210 | 0 / 40 | - | U 44 / 44; V 0 / 1 | Accepted | - |
| <code>mow_master.psd</code> | **Flagged** | Yes | 78.81% | 78.81% | 30 / 36 | 0 / 5 | 0 / 9 | U -; V - | Accepted | - |
| <code>patchy-plastic-wrap-photoshop-resave.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>pinball_from_photoshop.psd</code> | **Flagged** | Yes | 70.71% | 70.71% | 2 / 6 | - | 0 / 3 | U -; V - | Accepted | - |
| <code>pinball_retronight_poster_a3.psd</code> | **Flagged** | Yes | 41.44% | 16.83% | 14 / 49 | - | 0 / 26 | U 2 / 2; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_patchy.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>pinball_retronight_poster_a3_from_photoshop.psd</code> | **Flagged** | Yes | 41.44% | 16.83% | 14 / 49 | - | 0 / 26 | U 2 / 2; V - | Accepted | - |
| <code>Polymega jump test.psd</code> | **Flagged** | Yes | 71.02% | 71.02% | 2 / 4 | 0 / 1 | - | U -; V - | Accepted | - |
| <code>polymega_famicom.psd</code> | **Flagged** | Yes | 82.53% | 82.53% | 1 / 2 | 0 / 1 | 0 / 1 | U -; V - | Accepted | - |
| <code>ps2026-16bit-flat.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026-16bit.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026-32bit-flat.psd</code> | **Flagged** | Yes | n/a | n/a | 1 / 1 | - | - | U -; V - | Accepted | **no render comparison** |
| <code>ps2026-32bit.psd</code> | **Flagged** | Yes | 33.74% | 33.74% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_converted_smart_object.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_place_fit_large.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_probe.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 0 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_small.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_smart_filter.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_via_copy.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>restaurant-menu-inside.psd</code> | **Flagged** | Yes | 92.88% | 92.88% | 19 / 52 | 0 / 23 | 0 / 5 | U 3 / 3; V 0 / 2 | Accepted | - |
| <code>snes-box-a3.psd</code> | **Flagged** | Yes | 99.15% | 99.15% | 19 / 101 | 0 / 20 | 0 / 8 | U -; V 0 / 36 | Accepted | - |
| <code>Template.psd</code> | **Flagged** | Yes | 97.51% | 97.51% | 23 / 31 | 0 / 1 | 0 / 3 | U -; V - | Accepted | - |
| <code>tips.psd</code> | **Flagged** | Yes | 95.66% | 95.85% | 27 / 48 | 0 / 21 | 0 / 31 | U -; V - | Accepted | - |
| <code>Title Screen_demo.psd</code> | **Flagged** | Yes | 57.89% | 59.00% | 23 / 38 | 0 / 15 | 0 / 23 | U -; V - | Accepted | - |
| <code>Title02.psd</code> | **Flagged** | Yes | 84.41% | 46.25% | 22 / 29 | 0 / 6 | 0 / 2 | U 7 / 7; V - | Accepted | - |
| <code>tlm-main-mockup.psd</code> | **Flagged** | Yes | 92.07% | 92.07% | 1 / 3 | 0 / 2 | 0 / 1 | U -; V - | Accepted | - |
| <code>vectors_overlay_stroke.psd</code> | **Flagged** | Yes | 70.37% | 70.37% | 1 / 4 | - | 0 / 1 | U -; V 0 / 2 | Accepted | - |
| <code>weedkiller_skin.psd</code> | **Flagged** | Yes | 80.44% | 80.47% | 10 / 12 | 0 / 2 | 0 / 4 | U -; V - | Accepted | - |
| <code>wordpress_banner3.psd</code> | **Flagged** | Yes | 87.65% | 87.65% | 8 / 11 | - | - | U 2 / 2; V - | Accepted | - |

</details>

<details>
<summary><strong>PhotoDemon 2026.01.0251 (testy CLI build)</strong> | opened 64/64 | perceptual 81.97% (n=63) | rejected saves 0</summary>

| PSD file | Scan | Opened | Perceptual | PS round trip | Native objects | Editable text | Live effects | Masks | PSD save | Issue |
|---|---|---:|---:|---:|---:|---:|---:|---|---|---|
| <code>akiko_cycling_okinawa_with_curves.psd</code> | **Flagged** | Yes | 20.74% | 20.74% | 2 / 3 | - | - | U -; V - | Accepted | - |
| <code>APP_Icon_1024x1024.psd</code> | **Flagged** | Yes | 98.69% | 98.69% | 24 / 66 | 0 / 4 | 0 / 23 | U 0 / 8; V 0 / 38 | Accepted | - |
| <code>Arduboy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 4 | 0 / 2 | - | U -; V - | Accepted | - |
| <code>AudioSplitterProject.psd</code> | **Flagged** | Yes | 81.05% | 81.05% | 1 / 4 | 0 / 3 | 0 / 3 | U -; V - | Accepted | - |
| <code>bevel_examine.psd</code> | **Flagged** | Yes | 92.86% | 92.86% | 2 / 3 | 0 / 1 | 0 / 1 | U -; V - | Accepted | - |
| <code>C2Kyoto Nintendo NES Cartridge Label Template (Front).psd</code> | **Flagged** | Yes | 99.94% | 100.00% | 143 / 415 | 0 / 107 | 0 / 6 | U 0 / 13; V 0 / 71 | Accepted | - |
| <code>CDi_A4.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 7 / 15 | 0 / 8 | 0 / 1 | U -; V - | Accepted | - |
| <code>checkbox.psd</code> | **Flagged** | Yes | 15.53% | 15.53% | 2 / 2 | - | 0 / 1 | U -; V - | Accepted | - |
| <code>Choose a game sign.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 4 | 0 / 2 | - | U -; V - | Accepted | - |
| <code>deko_test.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 4 | 0 / 1 | - | U -; V - | Accepted | - |
| <code>DS_ChipBoardToken.psd</code> | **Flagged** | Yes | 72.01% | 72.01% | 5 / 9 | 0 / 4 | 0 / 6 | U -; V - | Accepted | - |
| <code>Duke nukem mobile.psd</code> | **Flagged** | Yes | 79.26% | 79.26% | 2 / 9 | 0 / 5 | 0 / 1 | U 0 / 1; V - | Accepted | - |
| <code>eon_spider_from_patchy.psd</code> | Passed | Yes | 98.30% | 98.31% | 6 / 6 | - | 0 / 1 | U 0 / 1; V - | Accepted | - |
| <code>eon_spider_from_photoshop.psd</code> | Passed | Yes | 98.30% | 98.31% | 6 / 6 | - | 0 / 1 | U 0 / 1; V - | Accepted | - |
| <code>eon_spider_original.psd</code> | Passed | Yes | 98.99% | 98.99% | 5 / 6 | - | 0 / 1 | U 0 / 1; V - | Accepted | - |
| <code>EonKun Goodboy Chest.psd</code> | **Flagged** | Yes | 88.00% | 88.00% | 3 / 9 | 0 / 1 | - | U -; V - | Accepted | - |
| <code>Eons card.psd</code> | **Flagged** | Yes | 81.36% | 81.36% | 6 / 7 | 0 / 1 | 0 / 1 | U 0 / 1; V - | Accepted | - |
| <code>Flat-filter-list.psd</code> | **Flagged** | Yes | 96.71% | 96.71% | 5 / 11 | 0 / 1 | - | U -; V - | Accepted | - |
| <code>generic_bg.psd</code> | **Flagged** | Yes | 33.19% | 33.15% | 65 / 95 | 0 / 25 | 0 / 33 | U -; V - | Accepted | - |
| <code>Horror VirtualBoy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 6 | 0 / 2 | - | U -; V - | Accepted | - |
| <code>interface_mock2.psd</code> | **Flagged** | Yes | 0.26% | 0.26% | 10 / 11 | - | - | U -; V - | Accepted | - |
| <code>ipad_main_v04.psd</code> | Passed | Yes | 93.92% | 94.02% | 21 / 29 | 0 / 8 | 0 / 10 | U -; V - | Accepted | - |
| <code>Launcher-icon-template.psd</code> | **Flagged** | Yes | 84.98% | 0.17% | 1 / 283 | - | - | U -; V - | Accepted | - |
| <code>LaunchImagesAndIcons-2016.psd</code> | **Flagged** | Yes | 21.24% | 25.76% | 64 / 210 | 0 / 40 | - | U 44 / 60; V 0 / 52 | Accepted | - |
| <code>mow_master.psd</code> | **Flagged** | Yes | 79.42% | 79.42% | 31 / 36 | 0 / 5 | 0 / 10 | U -; V - | Accepted | - |
| <code>patchy-plastic-wrap-photoshop-resave.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>pinball_from_photoshop.psd</code> | **Flagged** | Yes | 72.61% | 72.61% | 3 / 6 | - | 0 / 3 | U -; V - | Accepted | - |
| <code>pinball_retronight_poster_a3.psd</code> | **Flagged** | Yes | 19.85% | 19.85% | 19 / 49 | - | 0 / 29 | U 0 / 2; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_patchy.psd</code> | **Flagged** | Yes | 21.04% | 21.04% | 19 / 49 | - | 0 / 29 | U 0 / 2; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_photoshop.psd</code> | **Flagged** | Yes | 19.85% | 19.85% | 19 / 49 | - | 0 / 29 | U 0 / 2; V - | Accepted | - |
| <code>Polymega jump test.psd</code> | **Flagged** | Yes | 71.02% | 71.02% | 3 / 4 | 0 / 1 | 0 / 1 | U -; V - | Accepted | - |
| <code>polymega_famicom.psd</code> | **Flagged** | Yes | 82.53% | 82.53% | 1 / 2 | 0 / 1 | 0 / 1 | U -; V - | Accepted | - |
| <code>ps2026-16bit-flat.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 0 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026-16bit.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026-32bit-flat.psd</code> | **Flagged** | Yes | n/a | n/a | 0 / 1 | - | - | U -; V - | Accepted | **no render comparison** |
| <code>ps2026-32bit.psd</code> | **Flagged** | Yes | 26.71% | 26.71% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_converted_smart_object.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_place_fit_large.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_probe.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 0 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_small.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_smart_filter.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_via_copy.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>restaurant-menu-inside.psd</code> | **Flagged** | Yes | 90.80% | 90.79% | 23 / 52 | 0 / 23 | 0 / 8 | U 0 / 3; V 0 / 2 | Accepted | - |
| <code>snes-box-a3.psd</code> | **Flagged** | Yes | 92.73% | 99.16% | 26 / 101 | 0 / 20 | 0 / 8 | U -; V 0 / 50 | Accepted | - |
| <code>Template.psd</code> | **Flagged** | Yes | 86.51% | 86.51% | 30 / 31 | 0 / 1 | 0 / 8 | U -; V - | Accepted | - |
| <code>tips.psd</code> | **Flagged** | Yes | 73.16% | 73.17% | 27 / 48 | 0 / 21 | 0 / 33 | U -; V - | Accepted | - |
| <code>Title Screen_demo.psd</code> | **Flagged** | Yes | 54.26% | 54.48% | 22 / 38 | 0 / 15 | 0 / 25 | U -; V - | Accepted | - |
| <code>Title02.psd</code> | **Flagged** | Yes | 88.05% | 88.05% | 22 / 29 | 0 / 6 | 0 / 2 | U 0 / 7; V - | Accepted | - |
| <code>tlm-main-mockup.psd</code> | **Flagged** | Yes | 91.99% | 91.99% | 1 / 3 | 0 / 2 | 0 / 1 | U -; V - | Accepted | - |
| <code>vectors_overlay_stroke.psd</code> | **Flagged** | Yes | 70.37% | 70.37% | 1 / 4 | - | - | U -; V - | Accepted | - |
| <code>weedkiller_skin.psd</code> | **Flagged** | Yes | 80.48% | 80.46% | 10 / 12 | 0 / 2 | 0 / 4 | U -; V - | Accepted | - |
| <code>wordpress_banner3.psd</code> | **Flagged** | Yes | 87.65% | 87.65% | 8 / 11 | - | - | U -; V - | Accepted | - |

</details>

<details>
<summary><strong>Krita 5.3.2.1 (git 0619060)</strong> | opened 56/64 | perceptual 81.17% (n=56) | rejected saves 9</summary>

| PSD file | Scan | Opened | Perceptual | PS round trip | Native objects | Editable text | Live effects | Masks | PSD save | Issue |
|---|---|---:|---:|---:|---:|---:|---:|---|---|---|
| <code>akiko_cycling_okinawa_with_curves.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>APP_Icon_1024x1024.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>Arduboy.psd</code> | **Flagged** | Yes | 87.03% | 81.77% | 3 / 4 | 1 / 2 | - | U -; V - | Accepted | - |
| <code>AudioSplitterProject.psd</code> | **Flagged** | Yes | 80.32% | 89.58% | 3 / 4 | 2 / 3 | 2 / 2 | U -; V - | Accepted | - |
| <code>bevel_examine.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>C2Kyoto Nintendo NES Cartridge Label Template (Front).psd</code> | **Flagged** | Yes | 3.84% | 3.84% | 322 / 415 | 107 / 107 | 6 / 6 | U 14 / 17; V 69 / 71 | Accepted | - |
| <code>CDi_A4.psd</code> | **Flagged** | Yes | 72.28% | n/a | n/a | n/a | n/a | U n/a; V n/a | **Rejected** | **resave rejected by Photoshop** |
| <code>checkbox.psd</code> | **Flagged** | Yes | 51.37% | 100.00% | 2 / 2 | - | 1 / 1 | U -; V - | Accepted | - |
| <code>Choose a game sign.psd</code> | **Flagged** | Yes | 89.92% | 89.92% | 2 / 4 | 0 / 2 | - | U -; V - | Accepted | - |
| <code>deko_test.psd</code> | Passed | Yes | 94.38% | 98.24% | 3 / 4 | 0 / 1 | - | U -; V - | Accepted | - |
| <code>DS_ChipBoardToken.psd</code> | **Flagged** | Yes | 63.78% | 64.05% | 8 / 9 | 3 / 4 | 5 / 5 | U -; V - | Accepted | - |
| <code>Duke nukem mobile.psd</code> | **Flagged** | Yes | 56.63% | n/a | n/a | n/a | n/a | U n/a; V n/a | **Rejected** | **resave rejected by Photoshop** |
| <code>eon_spider_from_patchy.psd</code> | Passed | Yes | 97.16% | 99.95% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_photoshop.psd</code> | Passed | Yes | 97.16% | 99.95% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_original.psd</code> | Passed | Yes | 99.43% | 99.95% | 5 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>EonKun Goodboy Chest.psd</code> | **Flagged** | Yes | 85.79% | 85.79% | 3 / 9 | 0 / 1 | - | U 2 / 2; V - | Accepted | - |
| <code>Eons card.psd</code> | **Flagged** | Yes | 87.84% | 90.30% | 6 / 7 | 0 / 1 | - | U 1 / 1; V - | Accepted | - |
| <code>Flat-filter-list.psd</code> | **Flagged** | Yes | 87.45% | 88.50% | 11 / 11 | 1 / 1 | 5 / 5 | U -; V 5 / 5 | Accepted | - |
| <code>generic_bg.psd</code> | **Flagged** | Yes | 43.38% | n/a | n/a | n/a | n/a | U n/a; V n/a | **Rejected** | **resave rejected by Photoshop** |
| <code>Horror VirtualBoy.psd</code> | **Flagged** | Yes | 86.07% | 86.07% | 6 / 6 | 2 / 2 | - | U -; V - | Accepted | - |
| <code>interface_mock2.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 11 / 11 | - | - | U -; V - | Accepted | - |
| <code>ipad_main_v04.psd</code> | Passed | Yes | 99.81% | 99.99% | 29 / 29 | 8 / 8 | 10 / 10 | U -; V - | Accepted | - |
| <code>Launcher-icon-template.psd</code> | **Flagged** | Yes | 86.61% | n/a | n/a | n/a | n/a | U n/a; V n/a | **Rejected** | **resave rejected by Photoshop** |
| <code>LaunchImagesAndIcons-2016.psd</code> | **Flagged** | Yes | 21.35% | 21.37% | 156 / 210 | 39 / 40 | - | U 8 / 60; V 52 / 52 | Accepted | - |
| <code>mow_master.psd</code> | **Flagged** | Yes | 78.65% | n/a | n/a | n/a | n/a | U n/a; V n/a | **Rejected** | **resave rejected by Photoshop** |
| <code>patchy-plastic-wrap-photoshop-resave.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>pinball_from_photoshop.psd</code> | **Flagged** | Yes | 0.32% | 72.62% | 3 / 6 | - | 2 / 3 | U -; V - | Accepted | - |
| <code>pinball_retronight_poster_a3.psd</code> | **Flagged** | Yes | 59.13% | 61.98% | 19 / 49 | - | 12 / 29 | U 3 / 3; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_patchy.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>pinball_retronight_poster_a3_from_photoshop.psd</code> | **Flagged** | Yes | 59.13% | 61.98% | 19 / 49 | - | 12 / 29 | U 3 / 3; V - | Accepted | - |
| <code>Polymega jump test.psd</code> | **Flagged** | Yes | 63.97% | 64.09% | 3 / 4 | 0 / 1 | - | U -; V - | Accepted | - |
| <code>polymega_famicom.psd</code> | **Flagged** | Yes | 82.22% | 85.42% | 2 / 2 | 1 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>ps2026-16bit-flat.psd</code> | **Flagged** | Yes | 33.84% | 100.00% | 1 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026-16bit.psd</code> | **Flagged** | Yes | 33.84% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026-32bit-flat.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>ps2026-32bit.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>ps2026_converted_smart_object.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_place_fit_large.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_probe.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 0 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_small.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_smart_filter.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_via_copy.psd</code> | Passed | Yes | 100.00% | 100.00% | 1 / 3 | - | - | U -; V - | Accepted | - |
| <code>restaurant-menu-inside.psd</code> | **Flagged** | Yes | 32.28% | 44.09% | 46 / 52 | 21 / 23 | 8 / 8 | U 4 / 4; V 2 / 2 | Accepted | - |
| <code>snes-box-a3.psd</code> | **Flagged** | Yes | 91.19% | n/a | n/a | n/a | n/a | U n/a; V n/a | **Rejected** | **resave rejected by Photoshop** |
| <code>Template.psd</code> | **Flagged** | Yes | 99.60% | n/a | n/a | n/a | n/a | U n/a; V n/a | **Rejected** | **resave rejected by Photoshop** |
| <code>tips.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>Title Screen_demo.psd</code> | **Flagged** | Yes | 71.98% | n/a | n/a | n/a | n/a | U n/a; V n/a | **Rejected** | **resave rejected by Photoshop** |
| <code>Title02.psd</code> | **Flagged** | Yes | 88.05% | 88.05% | 22 / 29 | 0 / 6 | 0 / 2 | U 7 / 7; V - | Accepted | - |
| <code>tlm-main-mockup.psd</code> | **Flagged** | Yes | 86.05% | n/a | n/a | n/a | n/a | U n/a; V n/a | **Rejected** | **resave rejected by Photoshop** |
| <code>vectors_overlay_stroke.psd</code> | **Flagged** | **No** | n/a | n/a | n/a | n/a | n/a | U n/a; V n/a | n/a | **editor failed to open source** |
| <code>weedkiller_skin.psd</code> | **Flagged** | Yes | 86.19% | 92.80% | 12 / 12 | 2 / 2 | 4 / 4 | U -; V - | Accepted | - |
| <code>wordpress_banner3.psd</code> | **Flagged** | Yes | 87.65% | 87.65% | 8 / 11 | - | - | U 2 / 2; V - | Accepted | - |

</details>

<details>
<summary><strong>Photoshop 27.8.0</strong> | opened 64/64 | perceptual 100.00% (n=63) | rejected saves 0</summary>

| PSD file | Scan | Opened | Perceptual | PS round trip | Native objects | Editable text | Live effects | Masks | PSD save | Issue |
|---|---|---:|---:|---:|---:|---:|---:|---|---|---|
| <code>akiko_cycling_okinawa_with_curves.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>APP_Icon_1024x1024.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 66 / 66 | 4 / 4 | 24 / 24 | U 8 / 8; V 38 / 38 | Accepted | - |
| <code>Arduboy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 2 / 2 | - | U -; V - | Accepted | - |
| <code>AudioSplitterProject.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 3 / 3 | 3 / 3 | U -; V - | Accepted | - |
| <code>bevel_examine.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 3 / 3 | 1 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>C2Kyoto Nintendo NES Cartridge Label Template (Front).psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 415 / 415 | 107 / 107 | 6 / 6 | U 17 / 17; V 71 / 71 | Accepted | - |
| <code>CDi_A4.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 15 / 15 | 8 / 8 | 1 / 1 | U -; V - | Accepted | - |
| <code>checkbox.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | 1 / 1 | U -; V - | Accepted | - |
| <code>Choose a game sign.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 2 / 2 | - | U -; V - | Accepted | - |
| <code>deko_test.psd</code> | Passed | Yes | 100.00% | 100.00% | 4 / 4 | 1 / 1 | - | U -; V - | Accepted | - |
| <code>DS_ChipBoardToken.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 9 / 9 | 4 / 4 | 6 / 6 | U -; V - | Accepted | - |
| <code>Duke nukem mobile.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 9 / 9 | 5 / 5 | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_patchy.psd</code> | Passed | Yes | 100.00% | 100.00% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_from_photoshop.psd</code> | Passed | Yes | 100.00% | 100.00% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>eon_spider_original.psd</code> | Passed | Yes | 100.00% | 100.00% | 6 / 6 | - | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>EonKun Goodboy Chest.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 9 / 9 | 1 / 1 | - | U 2 / 2; V - | Accepted | - |
| <code>Eons card.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 7 / 7 | 1 / 1 | 1 / 1 | U 1 / 1; V - | Accepted | - |
| <code>Flat-filter-list.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 11 / 11 | 1 / 1 | 5 / 5 | U -; V 5 / 5 | Accepted | - |
| <code>generic_bg.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 95 / 95 | 25 / 25 | 33 / 33 | U 1 / 1; V - | Accepted | - |
| <code>Horror VirtualBoy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 6 / 6 | 2 / 2 | - | U -; V - | Accepted | - |
| <code>interface_mock2.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 11 / 11 | - | - | U -; V - | Accepted | - |
| <code>ipad_main_v04.psd</code> | Passed | Yes | 100.00% | 100.00% | 29 / 29 | 8 / 8 | 10 / 10 | U -; V - | Accepted | - |
| <code>Launcher-icon-template.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 283 / 283 | - | 125 / 125 | U 19 / 19; V 186 / 186 | Accepted | - |
| <code>LaunchImagesAndIcons-2016.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 210 / 210 | 40 / 40 | - | U 60 / 60; V 52 / 52 | Accepted | - |
| <code>mow_master.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 36 / 36 | 5 / 5 | 10 / 10 | U -; V - | Accepted | - |
| <code>patchy-plastic-wrap-photoshop-resave.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>pinball_from_photoshop.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 6 / 6 | - | 3 / 3 | U -; V - | Accepted | - |
| <code>pinball_retronight_poster_a3.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 49 / 49 | - | 29 / 29 | U 3 / 3; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_patchy.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 49 / 49 | - | 29 / 29 | U 2 / 2; V - | Accepted | - |
| <code>pinball_retronight_poster_a3_from_photoshop.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 49 / 49 | - | 29 / 29 | U 3 / 3; V - | Accepted | - |
| <code>Polymega jump test.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | 1 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>polymega_famicom.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | 1 / 1 | 1 / 1 | U -; V - | Accepted | - |
| <code>ps2026-16bit-flat.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026-16bit.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026-32bit-flat.psd</code> | **Flagged** | Yes | n/a | n/a | 1 / 1 | - | - | U -; V - | Accepted | **no render comparison** |
| <code>ps2026-32bit.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_converted_smart_object.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case1_small_to_big_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case2_big_to_small_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case3_scaled_then_replace_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case4_dpi_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e5_case5_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_after.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_e6_warp_before.psd</code> | Passed | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_place_fit_large.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_probe.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 1 / 1 | - | - | U -; V - | Accepted | - |
| <code>ps2026_plastic_wrap_small.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 2 / 2 | - | - | U -; V - | Accepted | - |
| <code>ps2026_smart_filter.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>ps2026_via_copy.psd</code> | Passed | Yes | 100.00% | 100.00% | 3 / 3 | - | - | U -; V - | Accepted | - |
| <code>restaurant-menu-inside.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 52 / 52 | 23 / 23 | 8 / 8 | U 4 / 4; V 2 / 2 | Accepted | - |
| <code>snes-box-a3.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 101 / 101 | 20 / 20 | 8 / 8 | U -; V 50 / 50 | Accepted | - |
| <code>Template.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 31 / 31 | 1 / 1 | 8 / 8 | U -; V - | Accepted | - |
| <code>tips.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 48 / 48 | 21 / 21 | 33 / 33 | U -; V - | Accepted | - |
| <code>Title Screen_demo.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 38 / 38 | 15 / 15 | 25 / 25 | U -; V - | Accepted | - |
| <code>Title02.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 29 / 29 | 6 / 6 | 2 / 2 | U 7 / 7; V - | Accepted | - |
| <code>tlm-main-mockup.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 3 / 3 | 2 / 2 | 1 / 1 | U -; V - | Accepted | - |
| <code>vectors_overlay_stroke.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 4 / 4 | - | 1 / 1 | U -; V 2 / 2 | Accepted | - |
| <code>weedkiller_skin.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 12 / 12 | 2 / 2 | 4 / 4 | U -; V - | Accepted | - |
| <code>wordpress_banner3.psd</code> | **Flagged** | Yes | 100.00% | 100.00% | 11 / 11 | - | - | U 2 / 2; V - | Accepted | - |

</details>

## Field notes

- **Scan** is a file-level verdict. A row can be clean while the file is flagged because another editor failed or crossed the 10% perceptual-difference threshold.
- **PS round trip** compares Photoshop rendering the editor's resaved PSD with Photoshop's original render.
- **Native objects** counts retained text, adjustment, Smart Object, group, fill, and raster layer kinds.
- **Masks** uses `U` for user masks and `V` for vector masks.
- `n/a` means the run did not produce that measurement. A dash means the source contained none of that object type.
