# README showcase: animated and newer scenes

Child of [readme-showcase.md](readme-showcase.md): how the animated captures are made, and the
composition rules of the scenes added in November 2026. Read the parent first.

## Animated captures

Two scenes are animations, written as looping lossy WebP (GitHub shows animated WebP in
`<img>`): `save_readme_animation` and its streaming form `ReadmeAnimationWriter`
(`tests/ui/readme_screenshot_test_support.hpp`) round each frame's corners like a still and
encode with Patchy's own `patchy::webp::AnimationEncoder`, which stores only what changed
between frames. The driver publishes `.webp` artifacts beside the PNGs.

- `shot_readme_breakout` starts the bundled Breakout from the Script Manager and keeps the
  manager open on the left (Breakout chosen, breakout.js in the editor, the run status), so the
  animation shows how scripts run. The Color group is closed and the panel column narrowed; the
  playfield is zoomed and panned into the canvas space right of the manager, and the controller
  window (score, lives, keys) sits over the empty lower layer list. It captures the 1600 x 1000
  window for twelve seconds; each frame's delay is the real time between captures, so it plays
  at the game's speed (breakout.js moves by elapsed time). The manager is grabbed every half
  second (only its timer changes). `ReadmeAnimationWriter` encodes on a worker thread, so the
  capture runs near 38 fps: at a slow capture rate breakout.js takes long steps, and the ball
  can pass through the paddle's 12 px hit band.
- The Breakout autopilot (`BreakoutAutopilot`, shared with `shot_readme_script_manager`) holds
  Space, keeps the paddle still while the ball climbs, and on each descent glides once to where
  the ball will cross the paddle line (folded off the walls), aiming 10 px off center,
  alternating sides, so the rally works across the wall. A target counts only once two game
  frames agree (a frame that straddles a bounce gives a wrong heading), and it steers between
  every batch of events with start and stop thresholds wider than one frame's move. Steering
  only per captured frame on the ball's last position made the paddle twitch back and forth.
- `shot_readme_web_export_quad` opens Export for Web on the time-lapse with animated WebP at
  quality 80 in the 4-up view (80, 40, 20 beside the original), waits for every tile's
  frames, zooms to 100% so the lower qualities' blocking reads, then steps the eight frames,
  checking each tile shows the same index before capturing. Frames show 450 ms, the last
  1.4 s. The animation's own lossy encoding (quality 90) is mild next to the tiles' quality
  20, so the differences stay visible.

## Scene notes

- Quick Mask: a Magic Wand selection (tolerance 34, contiguous, 5 px sample, Shift-clicks
  from cloud to horizon) of the San Francisco sky, so the overlay follows a real edge. A
  pale tower right of the skyline is left partly clear on purpose; it is what the brush is
  for. Never go back to a drawn shape: the old feathered capsule read as fake.
- Seamless tiling: the document is one Dink tile, which is 50 x 50 pixels (never a bigger
  picture composed of tiles), at 400% (below the pixel grid's zoom). A red squiggle is painted
  across its right and bottom edges: strokes do not wrap on their own, so the scene splits the
  curve where it leaves the tile and continues it from the opposite edge, each run overshooting
  2 px. The copies then show the line joined across every seam, which is the point of the shot.
- Dink theme: applied without trusting its scripts (nothing plays on its own); the scene plays
  main.js's start-screen walk sprite and advances it 3 s. Recent files are emptied first (and
  restored) so no local path shows; the themed Preferences paints its own title bar, so it is
  grabbed without `readme_captioned_dialog`.
- KPT: a full run once captured KPT's window before it had drawn (all black); the driver now
  refuses a mostly black plug-in capture. Look at plugin_kpt5.png after every regeneration.
- Material Styles: the Layer Style dialog opens on its Style Presets grid (the default
  view, `layerStyle/stylesGridView`, restored after); the scene clicks Riveted Steel in the
  grid (`layerStyleStylesGrid`, found as a QListWidget: PresetGridWidget has no Q_OBJECT)
  and keeps the lettering in the left half, clear of the dialog.
- Divide Scanned Photos: Image > Divide Scanned Photos on the composed scan, waiting for
  `regionCount` 8. The scene sets `dividePhotos/folder` to a neutral path first: the default
  is the account's Documents folder, which would put a user name in the picture.
- Scan a Book to PDF: the sample book is a light sand cover (a dark cover on the dark lid is
  not found whole, and the cover's outline sets the book area every page scan uses) and four
  spreads on an 11.7 x 8.5 inch bed at 150 dpi (`PATCHY_FAKE_BOOK_SCANNER_BED`, `_DPIS`).
  Captures the scanning step after the fourth spread and the review step.

- Feature scenes (`readme_screenshot_tests_features.cpp`): Rotate View keeps the Rotate View
  tool active so the options bar shows the angle. GEGL picks Stereographic Projection (Little
  Planet), falling back to Mosaic or Cartoon; a little planet wraps the image through 360
  degrees, so the source is the photo beside its mirror image, or a seam shows. Liquify holds
  Twirl and Bloat (they work while held still) through `send_mouse` press, wait, release.
  Remove Object hides the selection edges (View > Show > Selection Edges, persisted, restored
  after) and draws a labeled Before inset grabbed from the canvas first; its fill needs no model.
  GEGL, Liquify and Remove Object draw their dialog's caption with `readme_captioned_dialog`.
- OpenAI Image Tools makes a real request that costs money, so it runs only when named
  (`-Scene openai_images`): the driver reads the key from `OPENAI_API_KEY` or the first `sk-`
  line of the untracked `agents_secret.md` beside the main checkout, sets it for that run only
  and never prints it, and a full run clears it so the scene skips. A test key store keeps the
  system's Credential Manager out of it, and the Config tab (the masked key field) is never
  shown. The output varies run to run; regenerate only to change the picture.
- PDF viewer: `write_sample_book_pdf` writes the sample book through `pdf::ImageWriter`; the
  viewer opens on its pages 5 and 6 (the picture plate). Print as Book opens from the viewer's
  button as a saddle-stitched booklet on sheet 1 (the covers). Its PNG is named
  `pdf_viewer_book_print` so the driver's `shot_readme_pdf_viewer*` pattern copies it.
