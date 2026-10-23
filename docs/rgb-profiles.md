# RGB color profiles

Patchy edits, composites and shows every RGB document as sRGB: the canvas, the tools, the
color picker and every filter treat RGB values as sRGB values. A file whose pixels are in
another RGB profile (Display P3 from phones, Adobe RGB and ProPhoto from cameras and
editors) therefore looks dull or shifted unless its pixels are converted. Step 1 of RGB
profile support (October 2026) converts such files on open and adds Edit > Convert to
Color Profile. Real working spaces and a profile list in New Document are a later step; CMYK is
[cmyk.md](cmyk.md).

Code: `src/color/rgb_profiles.{hpp,cpp}` (Qt-free, vendored Little CMS), the open hook
`convert_opened_rgb_profile` in `src/ui/main_window_files.cpp`, the dialog in
`src/ui/convert_profile_dialog.cpp`, the command in `src/ui/main_window_palette.cpp`.

## Converting on open

- After the reader and `capture_file_info` ran, a document whose
  `DocumentColorState::embedded_icc_profile` is a usable RGB profile that is not sRGB
  converts to sRGB, the same way for every format whose reader keeps the tag (tested: PSD,
  PSB, layered and flat TIFF, PNG, JPEG, WebP, JPEG XL, AVIF, 8 and 16 bits; GIF, BMP and
  JPEG 2000 take the same path). Readers that already convert to sRGB
  themselves (WIC HEIF and JPEG XR, LibRaw, the HEIF Qt fallback) never leave a tag, so
  nothing happens twice. CMYK sources (they open in CMYK Proof), non-RGB PSD modes and
  documents already in palette mode are skipped.
- Intent: relative colorimetric with black point compensation, Photoshop's conversion
  defaults (Color Settings and Convert to Profile). For the matrix profiles nearly every
  file carries, perceptual and relative colorimetric are the same mapping anyway.
- Afterwards the document has no embedded profile (plain sRGB). File Info keeps the file's
  profile in `source_icc` with the "converted ... reference only" note, and the PNG
  `sRGB`/`gAMA`/`cHRM`/`cICP` color chunks are dropped (they described the source space).
  An RGB PSD save writes resource 1039 only from the embedded profile, so the converted
  file carries no stale tag (`image_resources_for_document`); this also makes File Info's
  "drop the profile" stick for PSDs.
- Import Notes: "Converted from the color profile <name> to sRGB", plus a second note when
  the document holds text, shape, fill or adjustment layers or layer effects (below). A
  profile Little CMS cannot use keeps the pixels and tag, with a note saying so.
- Preference: Application tab, "Convert files with other RGB profiles to sRGB when
  opening", settings key `imports/convertRgbProfilesToSrgb` (permanent), default on. Off
  keeps the old behavior: the file's values with its tag, which saves write back.

## What converts

`convert_document_rgb_profile` converts image content and leaves coverage alone:

- Converted: pixel layers (not shape or fill layers), Smart Object preview rasters, the
  imported indexed palette (pixels and entries go through the same transform, so they still
  match), the retained flat PSD composite.
- Untouched: layer masks, saved channels, alpha.
- Not converted (stored colors keep their file values): text colors, shape and fill layer
  colors and gradients, adjustment layer parameters, layer style colors, artboard colors.
  Their rasters are left as they are too, so a later re-render does not shift them. A Smart
  Object re-rendered from its embedded file shows that file's own values (it is not
  converted to the parent's profile). Converting these is step 2 work.

## sRGB detection

`is_srgb_profile` compares meaning, not bytes, so every sRGB variant (HP's v2 table
curves, Qt's and Little CMS's parametric ones, v4) is left alone: a matrix profile is sRGB
when its three colorants match sRGB's within 0.003 and either its description names sRGB
(and not "linear") or its tone curves follow the sRGB curve within 0.004 at 33 points. A
table-based profile is sRGB only by a description naming sRGB.

## Bit depths

- 8 and 16-bit: Little CMS transforms `TYPE_RGB(A)_8/16` with `cmsFLAGS_COPY_ALPHA`, built
  without the pixel cache (thread-safe), rows converted through a scratch row.
- 32-bit documents hold linear light (Photoshop's model: linear values in the profile's
  primaries), so they convert through the colorant matrices, destination inverse times
  source, without clipping: out-of-gamut colors go negative or above 1. Both profiles must
  be matrix profiles; a table-based one refuses the conversion (open keeps the tag with a
  note, Convert to Color Profile reports it).

## Edit > Convert to Color Profile

`edit.convert_to_profile` (no default key), dialog `convertToProfileDialog`: the current
profile (the embedded description, `kSrgbProfileName` when untagged), a destination list
(sRGB first, then `installed_rgb_profiles()`: RGB display, input, output and color space
profiles from the operating system's and Adobe's folders, the same scan as the CMYK list;
then Choose Profile File...), rendering intent and black point compensation. One undo step;
the converted profile becomes the embedded one, none for sRGB, matching the open path.
Refused in CMYK Proof and palette (Indexed) mode with a status message. The choices persist
under `color/convertProfileDestination`, `color/convertProfileIntent` (the
`rendering_intent_token` values) and `color/convertProfileBlackPoint`. A document in a
non-sRGB profile still displays as sRGB values, which the dialog says. Below the list,
`convertToProfileSourcesNote` (desktop only) says sRGB is built in and the rest are the
profiles installed on this computer, or that none are, and points at the Choose Profile File... entry.
Patchy ships no RGB profile files: sRGB is Little CMS's generated profile, and a profile
the user picks is read in place, never copied into Patchy (only embedded in the converted
document, as every color-managed editor does). The menu text was "Convert to Profile"
before October 24, 2026; the command id and settings keys kept their names.

Scripting: `doc.colorProfile` and `doc.convertToProfile(profile, {intent,
blackPointCompensation})` ([scripting-api-changes.md](scripting-api-changes.md)); `app.open`
follows the preference.

## Tests

Core `rgb_profile_*` (sRGB variants, P3 and Adobe RGB against the published matrices at 8,
16 and 32 bits, document conversion, Unicode profile paths, PSD saves without the stale
tag); UI `ui_open_converts_rgb_profiles_to_srgb` (every writer that embeds a profile, read
back with the preference off and on), `ui_preferences_convert_rgb_profiles_checkbox`,
`ui_convert_to_profile_dialog_converts_and_undoes`, `ui_script_convert_to_profile`. Test
profiles come from `create_rgb_matrix_profile`; Patchy ships no RGB profile data.
