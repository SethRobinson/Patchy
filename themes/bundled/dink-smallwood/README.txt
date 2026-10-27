Dink Smallwood theme for Patchy
===============================

A theme package built into Patchy (Preferences > Colors and Themes, Included
themes). This folder is its source: the build compiles every file in it except
this README into the application. To see edits without rebuilding, start Patchy
with PATCHY_THEME_SOURCE_DIR set to the themes/bundled folder (docs/theme-files.md).

This product includes software developed by Seth A. Robinson ( www.rtsoft.com )

Dink Smallwood is (C) Robinson Technologies Corporation. The art and the game
sounds below come from Dink Smallwood HD (github.com/SethRobinson/RTDink); see its
LICENSE.md. Section 5a: the original 1998 game data (bin/dink) was released
under a zlib-style license in 2008. Section 5c notes that some sound effects in
the game data come from effect libraries or unknown sources. The only bin/dink
sounds used here are ones Seth A. Robinson made himself (secret, level, grunt1
and high2; confirmed October 27, 2026); every other sound is his own homemade
recording or synthesized for this theme.

What the theme uses
-------------------
theme.json      Base "dark"; every color role retuned to stone and leather
                browns with parchment text and a Dink-green accent.
                recolorIcons: true (built-in icons take the parchment, green
                and gold icon colors; the rope marquee SVG shows the same
                recoloring on a custom icon). iconScaling: smooth (the art is
                painted, not pixel art, so it is downscaled smoothly).
images/grass.jpg  The tiled backdrop behind the document and the empty
                workspace: a seamless grass tile repainted from the Dink grass
                tiles (bin/dink/tiles/ts01.bmp), then softened (a light blur,
                lower contrast and saturation, darker) so it frames the document
                without competing with it (JPEG quality 90, 4:4:4, so the
                256 px tile stays seamless). The scroll bars use the same tile;
                canvas_backdrop and canvas_scrollbar_track match its average
                color.
icons/          PNG icons (20 px plus a 40 px @2x drawing) from the Dink
                inventory and pickup sprites (bin/dink/graphics, inter/menu
                and bonuses), one 32-bit BMP (tool-text.bmp), and three
                hand-drawn SVGs (tool-brush quill, tool-lasso vine,
                tool-marquee rope). Dink art is used only where the picture
                says what the tool does; every other icon (Fill, Sponge, Burn,
                Magic Wand, Quick Select, Sharpen, Custom Shape, Clone, Smudge,
                Red Eye, Eraser, ...) is Patchy's own, recolored.

events          theme.json "events": Dink's bomb and a small burst when a layer
                is deleted, sparkles when a document opens. The other event
                sounds use the sounds/<event>.wav shorthand (list below).
cursors         theme.json "cursors": a sword for the Crop tool.
scripts/main.js The theme script (Patchy's locked-down theme engine; it ships with
                Patchy, so it runs unless turned off in Preferences). It adds the Dink menu right of Help
                (Celebrate, Confetti, Find a Secret, Take a Walk, Say Hello),
                shows the Dink logo as a startup splash, sends Dink walking along
                the empty start screen at startup and now and then while nothing
                is open, sparkles on a new document, plays painting sounds for
                each stroke (stroke-start / stroke-end with startLoop/stopLoop:
                a start as the brush touches down, a loop while it moves, an end
                as it lifts; Brush, Eraser and a soft set for Smudge, Blur,
                Sponge and Dodge/Burn; Clone and Healing open with Dink's
                spell).
panelBackgrounds theme.json "panelBackgrounds": the stone wall tiled behind
                dialogs at 30% opacity, declarative so it shows even when the
                scripts are not allowed.

Other art
  images/dink-walk.png    bin/dink/graphics/dink/walk DS-W6-01..08 (walking right),
                          one frame each, aligned on the dink.ini anchor (38, 72)
  images/sword-cursor.png inter/menu/item-w07 (sword), mirrored, 32 px
  images/splash.png       MediaPack/logo_dink.png (HD media), scaled, then reduced
                          to a 256-color palette with alpha (k-means over
                          premultiplied color and alpha, no dithering, so the
                          soft shadow keeps a smooth ramp)
  images/stone.jpg        MediaPack/stone_background.png (HD media), scaled, made
                          seamless: lighting flattened (luminance fully, color
                          cast halfway), rolled by half, then each edge pair
                          joined along a minimum-error cut clear of the knight
                          and crown (786x576 tile); brightened (autocontrast
                          with 1% clipping, 1-(1-v)^1.6 to lift the darks, hues
                          kept at half saturation), scaled to 65% brightness;
                          JPEG quality 65

Icon sources (bin/dink/graphics, extracted from the dir.ff packs)
  tool-move            inter/menu/item-w22  boots
  tool-pan             inter/menu/item-w01  fist
  tool-crop            inter/menu/item-w07  sword
  tool-perspective-crop inter/menu/item-w06 axe
  tool-text            inter/menu/item-w02  paper (BMP at 1x)
  tool-healing         inter/menu/item-w10  red potion
  tool-spot-healing    bonuses/heart/HEART01 heart
  tool-line            inter/menu/item-w08  bow
  trash                inter/menu/item-w03  bomb (Delete Layer, Channel, Path)
  dir                  bonuses/chest/CHST1-01 chest

Sounds (trimmed, faded, level-matched, 22.05 kHz 16-bit mono WAV). Speed is the
rate the source plays at, relative to the file. "Homemade" is Seth A. Robinson's own
foley recordings (his 2018 and 2021 sets), each level-matched to the sound it
replaced. The generator scripts stay with the source recordings, outside this
repository.
  startup              bin/dink/sound/high2.wav   title screen (start.c)    5a, Seth's
  save-done            Homemade 2018 piece_click2, then 2021 metallic_click Seth's
                       (set down and latched shut)
  export-done          Homemade 2021 marble3 + marble4 (x1.12): coins       Seth's
  long-operation-done  bin/dink/sound/level.wav   level up                  5a, Seth's
  error                bin/dink/sound/grunt1.wav  Dink hurt, x1.14          5a, Seth's
  warning              Homemade 2021 heavy_bump                             Seth's
  script-alert         Homemade 2018 piece_click3: a two-tap knock          Seth's
  tool-changed         Homemade 2018 piece_click1                           Seth's
  document-opened      Homemade 2018 lid_open2                              Seth's
  document-new         Homemade 2021 marble2                                Seth's
  document-closed      Homemade 2018 piece_click_muffled                    Seth's
  sounds/fx/bomb.wav   Homemade 2021 heavy_bump x0.5 and x0.35, 2018        Seth's
                       sandy_drain x0.8 as debris
  sounds/fx/spell.wav  Homemade 2021 marble1/3/4/5/6 at about x2 over       Seth's
                       2018 sandy_shake2 x1.3: a sparkle
  sounds/fx/secret.wav bin/dink/sound/secret.wav  secret found              5a, Seth's

Painting sounds (sounds/strokes): brush-, eraser- and soft- start, loop and end WAVs,
synthesized from filtered noise for this theme (original audio, no samples, free to
redistribute). Each loop repeats without a seam, and
each start leads straight into its loop's first sample.
