# Red Eye tool

GitHub issue 370. The fourth tool of the healing flyout ([healing.md](healing.md#palette-placement)). The user picks the eye; the tool never looks for eyes. The legal boundary is the "Red Eye" bullet in [legal-constraints.md](legal-constraints.md), record in [patent-research.md](patent-research.md) (checked 2026-10-24): fixed color and geometry rules on the region the user picked. Face or eye detection, whole-image scans, learned models, circle fitting that finds the eye without the user's point, and a fix-all command need a new check.

## Gesture

A click works in a square of `red_eye_click_box_size` around the press point (a third of the document's longer side, 64 to 1000 px) and must start within a sixth of that side of a strongly red pixel; a drag of at least 3 px both ways works inside the dragged box (drawn like the Object Selection box, `draw_red_eye_overlay`); Esc mid-drag cancels. The correction runs once on release (`CanvasWidget::apply_red_eye_at`, canvas_widget_red_eye.cpp) as one "Red Eye" undo step on the active pixel layer; masks, channels, Quick Mask and non-pixel layers are refused with a status message, as is a click with no red near it.

## Method

`core/red_eye.{hpp,cpp}`. Every weight is integer math on display-encoded 8-bit values (0..256 fixed point), so the mask is the same on every toolchain and at every depth. Calibrated on the real photos in `test-fixtures/red-eye` (licenses in its README) plus three CC BY-SA photos kept out of the repository (October 24, 2026). The first version (one hard ratio threshold, a 1 px feather, darken by multiplication) left a red ring at the anti-aliased rim, a pink halo round the glint and flat black blobs on real eyes.

- **Redness** (`red_eye_redness`): red must be the largest channel; hue within 8 degrees of red toward orange (fading out by 18, where brown irises, skin and lashes sit) or 30 toward magenta (fading out by 45); saturation ramping in from 0.35 to 0.55; and red at least 24 to 64 levels above green and blue. The three ramps multiply. Pupil Size maps 1..100 onto an internal 50..90 (default 70) that lowers the saturation and excess ramps, so 100 also takes pink and purple red-eye.
- **Tint**: any reddish or pinkish cast (hue to 15/40 degrees, saturation from 0.06). **Cast**: any saturation at all, any hue.
- **Area.** Seed: the pixel of redness at least 160 nearest the press point. Growth: 8-connected from the seed over redness at least 80 (60 at Pupil Size 100), inside the box and, for a click, the disk inscribed in it. If the result is irregular (under 45% of its bounding box), elongated (aspect over 2.2) or, for a click, over 70% of the disk, it leaked into lashes or skin and is regrown at 140, then 192. Holes the outside cannot reach (the glint) are filled, and three rings outside get 192, 112 and 40 of the 255 inside.
- **Fix**, per pixel, from its own color: desaturate toward the mean of green and blue by its tint (inside the area also by its cast, so a glint's pink or purple halo turns white or gray), times the area weight; then scale by 1 - (0.75 x Darken Amount) x redness^2, so Darken 50 keeps about 60% of the green/blue level and 100 a quarter (texture survives, never flat black), and partly red rim pixels mostly lose their cast rather than going dark. A white glint keeps its brightness. Alpha never changes and fully transparent pixels are skipped; the selection's coverage scales the area weight; palette documents binarize it and snap.
- **Depth.** 16-bit works on the encoded values through `load_pixel`/`store_pixel`; a 32-bit pixel within 0..1 goes through the sRGB curve, is fixed the same way and goes back (a brighter one is fixed in linear light, the darkening raised to 2.2), so both narrow to within a few levels of the 8-bit result and untouched pixels keep their exact values.

Known limit: magenta red-eye on dark brown eyes with a pale pink reflection needs Pupil Size near 100; at the default a pink patch can stay on the reflection, and the reddish iris round such a pupil turns gray with it.

## Options and script

Options bar: Pupil Size and Darken Amount, 1..100 %, default 50, a spin box and slider each (`redEyePupilSizeSpin`, `redEyeDarkenAmountSpin`), kept on MainWindow and applied to every canvas, saved as `tools/redEyePupilSize` and `tools/redEyeDarkenAmount`. Script: `layer.redEye({x, y, width?, height?, pupilSize?, darkenAmount?})` ([scripting-api-changes.md](scripting-api-changes.md)).

Coverage: `red_eye_*` (core, synthetic eye at 8/16/32 bits), `ui_red_eye_real_photos_leave_no_red_fringe` (every committed real eye: no red-eye-strength pixel left in the area plus a 2 px ring, nothing outside it changed, no leak past the pupil, the glint not dimmed), `ui_red_eye_tool_in_healing_flyout_corrects_click_and_undoes`, `ui_script_red_eye_corrects_pupil`.
