# Photoshop compatibility: COM techniques and PSD write rules

The PSD write/corruption rules and the layer-style descriptor shapes. Read this before touching PSD descriptor I/O or layer-style writing; render and adjustment calibration records are indexed at the end.

Conventions: "PS" = Adobe Photoshop 2026 (version 27), the installed ground truth. Every rule below is pinned by PS COM captures unless noted. Fixtures named `photoshop-*` live in `test-fixtures/psd/` with pinning tests; `local-test-fixtures/` is machine-local.

## Required compatibility contract

Every PSD/PSB Patchy writes (UI, JavaScript and MCP saves and exports) must open
in Adobe Photoshop without warnings or errors. A file that needs repair or raises
an unknown-data or data-discard prompt fails this even if its preview looks right.

Patchy-only metadata goes only in extension points Photoshop accepts without those
prompts: native PSD structures for editable content, the image-resource mechanism
for private document metadata, never unknown per-layer tagged keys (the write rules
below). Photoshop need not interpret it (palette color names) and may drop it on save.

A clean Patchy reopen or a structurally valid file is no evidence of a warning-free
Photoshop open, and neither is an open with dialogs suppressed. PSD writing changes
must keep this contract and report the verification actually performed; if Photoshop
was not checked, say so. COM is always authorized (AGENTS.md); desktop screenshots are not.

## COM scripting techniques

See [photoshop-com.md](photoshop-com.md) (entry point, dialog-mode caveats, composite and saveAs workarounds, hygiene, `scripts\dev\photoshop-open-check.ps1` for the "unknown data" prompt); read it before any capture or acceptance run.

## Write rules pinned against PS (silent corruption otherwise)

- Unchanged supported 8/16-bit CMYK/gray/Lab layers can retain the original
  document mode, profile and color channels. Content edits conservatively
  select RGB saving; see [native color preservation](psd-native-color.md). Halftone and
  transfer resources are per mode family (1012/1015 gray, 1014/1017/1018 duotone, 1013/1016
  color); saves drop other families', like PS's mode change.

- XMP resource 1060 dates require extended timezone offsets (`-07:00`, not
  `-0700`). Photoshop's XMP parser rejects the latter when importing File Info
  and shows a data-discard warning. The writer repairs basic offsets only in
  recognized, namespace-resolved XMP date properties; other packet bytes and
  valid IPTC/EXIF metadata stay intact (issue397 fixture: a date-only repair
  opens clean in error-enabled dialog mode).

- **Photoshop reads type from the document-level `Txt2` block and trusts it over the TySh.**
  The block holds one text object per type layer, addressed by the TySh TextIndex; a stale
  object silently wins (a Bahnschrift Light retype read back as Bold).
  Patchy rebuilds the block on every save with type layers: an authored object per regenerated type layer,
  untouched objects kept, caches emptied to `/2 [ ]` (no `/2` crashes PS 2025), so Patchy type opens as live text.
  Never drop a preserved block: every layer would demote to Photoshop's old-text path. A block
  that fails to parse stays verbatim and regenerated layers take `kRegeneratedTextIndexBase` + n,
  an index no object has, so Photoshop reads them from their TySh (`oldText = true`, the "Some
  text layers might need to be updated" prompt). **Variable-font named instances are
  static-substituted on the old-text path** ("Bahnschrift-SemiBold", the name Photoshop itself
  writes, reads back as Regular; Photoshop's own objects carry the instance axes); Patchy renders
  them correctly in both engines (the Bahnschrift weights share advance widths, so compare ink,
  not bounds). Format, key map and the writer: [txt2.md](txt2.md); Photoshop 5.x `tySh`
  documents: [psd-legacy-text.md](psd-legacy-text.md).
- **16/32-bit files** ([high-bit-depth.md](high-bit-depth.md)): channels at the document's
  depth, an empty standard layer-info section, records in the `Lr16`/`Lr32` global
  block, descriptor colors linear in 32-bit files. PS refuses to open a 32-bit PSD ("open
  options are incorrect") with a layer or group in Color Burn, Linear Burn, Screen, Color Dodge,
  Overlay, Soft/Hard/Vivid/Linear/Pin Light, Hard Mix or Exclusion, so the writer writes Normal
  for those (`writable_layer_blend_mode`; `psd_deep_32_bit_blend_modes_follow_photoshop`).
  Layer-effect blend modes are unaffected.
- **Record and channel limits**: PS rejects more than 8000 layer records (a group counts as
  two; 8001 opens composite-only) and more than 56 channels including merged
  transparency; the writer refuses both with an error.
- **A gradient descriptor's `Trns` list is never empty.** Photoshop's gradients carry at least
  two transparency stops; an empty list (a scripted fill without alphaStops) raises the
  "discard unknown data" prompt and empties the layer. The vector fill writer supplies
  two opaque end stops when the model has none (the layer-style writers already did); a file
  that carries an empty list heals on save: the reader marks the layer's blocks dirty
  and the fill/stroke payload builders skip the byte-exact shortcut. Pinned by
  `psd_vector_gradient_without_transparency_stops_heals_on_save` and its sibling test.
- **Engine-data paragraph metrics carry a decimal point.** PS reads a bare integer token as 16.16 fixed point (`/FirstLineIndent 24` read back as 24/65536 px); `24.0` reads as 24 px. `engine_paragraph_metric` writes `24.0`, `-20.0`, `0.0`; pinned by `psd_writer_emits_v2_paragraph_layout`.
- **Do not author unknown per-layer tagged keys.** Even correctly padded private tags trigger Photoshop's unknown-data warning. Merged vector associations use image resource 4211 in the plug-in resource range (4000-4999) with native `lyid` references; legacy `pvcl`/`pvfi` tags stay read-only ([layer-merging.md](layer-merging.md)).
- **Layer color labels ride Photoshop's `lclr`** (u16 index 0..7 plus six zero bytes, after `lspf`, groups too). None writes no block; an imported block goes back verbatim while its index matches, else is regenerated; an unknown index is kept and shown as none (`psd_layer_color_labels_round_trip_lclr`).
- **Artboards are Photoshop's own `artb` folder block and global `artd` block**, regenerated in Photoshop's key order and kept verbatim while unchanged; never a private key ([artboards.md](artboards.md)).
- **File Info text rides plug-in image resource 4213 (`'PtcM'`).** PNG text chunks and comments with no Photoshop resource travel there; XMP, EXIF, IPTC, 1061 and the other metadata resources are lifted into `file_info()` on read and written back from it ([file-info.md](file-info.md)).
- **Patchy-only style options ride a plug-in image resource, never descriptor keys.** The continuous (long) drop shadow's `continuous`/`fade` fields travel in resource 4212 (layout in psd_io_internal.hpp) keyed by the layer's `lyid`, which `prepare_compound_vector_psd` assigns on save. The lfx2 keeps the pinned 12-item DrSh with `Dstn` = sweep length, so PS shows a plain shadow at Distance and may drop the resource on resave.
- **Compound path groups use Photoshop's continuation records**: the lead record carries the op, continuations op 0xFFFF; a contour with its own op unites (PS fills a donut solid). Detail in [vector-tools.md](vector-tools.md).
- **Multiple open strokes need separate native shapes.** Photoshop closes open contours when a stroked shape holds several subpaths, even in its own files with correct open-path selectors. Solid centered strokes use a reversible native-group export: [open-path-strokes.md](open-path-strokes.md).
- **No orphaned lnk2 or lnkE elements.** PS refuses an embedded or linked element no layer uses; the writer omits them ([smart-objects.md](smart-objects.md)).
- **Per-layer tagged blocks declare an even length with the pad byte inside it.** PS walks by declared length rounded up to even and never writes odd lengths; one odd block makes PS discard every later block in the record. `write_additional_layer_block` enforces it. Generated TySh instead keeps its body even internally (its 16-byte end-anchored tail must not be followed by a pad). Global-section blocks keep 4-byte alignment OUTSIDE the declared length. The reader stays exact-advance so old odd-block Patchy files load.
- **Every pixel record except PS's Background carries a transparency channel (-1).** PS reads a record without one as the Background: opaque over the WHOLE canvas, hiding every layer beneath. Only the bottom record covering exactly the canvas may omit it (`encode_layer`; `psd_opaque_rgb_layers_write_transparency_unless_background`).
- **Layer record flags bit 5 (0x20) = the Layers panel shows the rows under the layer**, one bit for effects and Smart Filters (#361). PS 2026 sets it on new ones and keeps a cleared bit through open and resave; nothing else changes with it. Read into `patchy.layer_fx_expanded`; written when true, or with no state on a Smart Filter layer ([layer-panel.md](layer-panel.md#effect-rows)).
- **Layer record flags bit 3 must be set on every layer** or PS applies legacy semantics. The layer mask shapes effect sources regardless of link state (the chain toggle only affects move). Effect output still lands on mask-hidden areas unless 'lmgm' is set (4 bytes, first byte bool; `LayerStyle::layer_mask_hides_effects`). 'vmgm' is the vector-mask twin (`vector_mask_hides_effects`, written only when on); each mask switches on its own ([layer-effects-render.md](layer-effects-render.md)).
- **Layer-style descriptors, pattern blocks, `.pat` and `.asl`** (lfx2/lmfx effect fields, GrFl, FrFX, Satin, ebbl, contours, patternFill, Patt/Pat2/Pat3, the lrFX mirror): [psd-layer-styles.md](psd-layer-styles.md).
- **A clipping run is masked by the base layer's TRANSPARENCY, never by its layer styles.** Clip shape = base source alpha x layer/vector mask x opacity; the base's shadow/glow/stroke still render and merge with the group but must not let members paint where the base is absent. `freeze_clip` uses the coverage `record_clip_coverage` accumulates during the base's pixel pass. Pixel layers and folders can be bases; a folder's source is its merged children, including their effects, before the folder's own mask, opacity and effects.
- **Layer blend mode vs its own effects** (`photoshop-interior-exterior-blending.psd/bmp`); invisible while the layer is Normal and opaque:
  - **Interior effects ride `infx` "Blend Interior Effects as Group"** (4-byte block, first byte bool; PS default OFF, no block). OFF: the layer's mode carries its pixels alone and interior effects blend over the result with THEIR own modes. ON: effects fold into the layer's color first. "Interior" = the three overlays, Satin, Inner Glow; Inner Shadow and Bevel composite onto the blended result either way. `LayerStyle::blend_interior_elements`, written only when true.
  - **A layer and its EXTERIOR effects contribute additively against the backdrop they both met**: the shadow is not attenuated again by the layer landing on top, and a non-Normal layer blends with the backdrop, not its own shadow. The knockout is by the transparency SHAPE, independent of Fill and master Opacity: outer glow always concedes the shape; drop shadow concedes when `layerConceals` is on, otherwise only the painted coverage. `exterior_effect_knockout` + a pre-effect `CompositeSnapshot` implement it. Divergence: a stroke knockout band and a Blend-If gate paint less than `shape x Fill x Opacity` and leave a little extra effect.
- **Layer effects on GROUPS** (`photoshop-group-fx-{passthrough,interior,blend-fill,mask-stroke}.psd/bmp`): the LAYER rules on the group's flattened content, except:
  - **Effects do NOT force isolation**: a pass-through group with a drop shadow keeps children blending against the outside backdrop; the shadow derives from the group's union silhouette and paints behind the group. A Normal-mode group isolates as usual.
  - **The effect source is the flattened children WITH their opacities, after the group mask.** `lmgm` was not discriminated; assume the layer rule.
  - **Exterior effects are additive against the pre-effect backdrop**, knocked out by the silhouette's alpha.
  - **Interior effects blend with their OWN modes over the group's result unless `infx` folds them in**, pass-through groups included.
  - **Folder Fill fades the content, never the effects** (PS flattens of psd-tools' `knockout-none-*`).
  - **Stroke follows the layer rule**: outside bands the silhouette; an inside band replaces group content at the stroke's own mode.
  Renderer (`group_style_renders`, core/layer_render_utils.hpp, decides; layer_compositor.hpp renders): a styled group routes through the effect pipeline with the flattened children as the source buffer and the group in the layer's role; style-less groups keep the historical composite paths with Fill as one more opacity factor (`group_fill_factor_for_render`).
- **Fill Opacity**: `8BIM` + `iOpa` + u32 length 4 + one value byte + three zero pad bytes (37% = 94; 100% omits the block). Ordinary modes scale base-content coverage; the eight special modes (Color/Linear Burn, Color/Linear Dodge, Difference, Vivid/Linear Light, Hard Mix) use calibrated 8-bit Fill kernels ([blend-modes.md](blend-modes.md)). Effects use master Opacity and stay visible at Fill 0%. Clipped members fade with their base's Fill; adjustment strength multiplies by Fill; folder Fill fades group content.
- **Advanced Blending "Channels" ('brst')** (`photoshop-channel-restrictions.psd/bmp`): an excluded channel keeps the backdrop's PREMULTIPLIED value, `out[c] = dst[c] * dst_alpha / out_alpha`, applied AFTER the blend/special-fill kernel; other channels and alpha composite normally. ALL three excluded removes the layer entirely, effects and alpha included. Covers content, interior, and exterior effects; Fill and Blend If compose orthogonally; adjustments leave excluded channels unadjusted. Isolated groups restrict at the merge; PASS-THROUGH groups do NOT isolate (unlike Blend If). A restricted clip BASE gates the whole merged clip ensemble against the original backdrop; a restricted MEMBER keeps the BASE's channel. `ChannelRestrictedTarget` (layer_compositor.hpp); the style cache and pass-through fast walk are gated. The AM list `channelRestrictions` names the channels that stay ENABLED.
- **PS refuses the whole document ("program error")** when (a) a `PtFl` fill or `vstk` pattern stroke references a pattern id resolving in neither the file's pattern blocks nor PS's loaded presets (preset fallback masks this in tests: fixture GUIDs sit in this machine's presets), or (b) a `vogk` keyDescriptorList covers only some vmsk subpath groups. Patchy embeds a 1x1 transparent placeholder for unresolvable referenced ids (never a dangling one) and writes vogk/vowv only when every subpath group has an emitting origination entry. Read-side heal: [docs/vector-tools.md](vector-tools.md).
- **Smart Filter documents, two extra open-time requirements**: with a parseable FEid cache, PS eagerly parses the placed layer's embedded lnk2 file and rejects the WHOLE document if (a) the embedded PSD/PSB's merged-composite RLE rows include any odd byte count (the embed parser reads rows in two-byte units; layer channels, FEid planes, and the outer composite tolerate odd rows, and PS itself writes them there), or (b) the smart-object layer has no 'lyid' block. Without FEid both are ignored. Patchy writes every merged composite with even PackBits rows (one literal split per odd row, decode-identical; `make_packbits_row_even`), normalizes stored embeds on save (surgical lnk2 rebuild preserves foreign wrappers), and synthesizes unique 'lyid' blocks at write time.
- **Native Blend If** uses the layer-record blending-ranges field, not a tagged block. PS RGB files write 40 bytes: source then destination ranges for Composite Gray, R, G, B, plus an identity transparency pair; each 4-byte range is black-low, black-high, white-low, white-high. Patchy preserves the payload until a modeled edit, then patches the first 32 bytes and leaves the final identity pair; fresh identity stays zero-length. Unknown shapes, nonidentity transparency tails, converted non-RGB ranges, and group-boundary records stay raw-only. Adobe's [Blend If docs](https://helpx.adobe.com/uk/photoshop/using/layer-opacity-blending.html).

## Calibration records

- Brightness/Contrast legacy calibration, "Modern Brightness/Contrast", Curves 2.0, Levels and Posterize, Exposure, CMYK-document adjustment layers, Auto adjustments, "Hue/Saturation master" and colorize/bands: [adjustments-calibration.md](adjustments-calibration.md).
- "Native Smart Filters" (filterFX descriptors, FEid/FXid cache, per-filter render semantics): [smart-filters-native.md](smart-filters-native.md).
- Per-effect render calibration (Blend If, Satin, Stroke bands and overprint knockout, Shape Burst, shadow/glow pipeline, interior effects, Pattern Overlay, Bevel and Emboss): [layer-effects-render.md](layer-effects-render.md).
- Photoshop text model (type layers): [text-render-calibration.md](text-render-calibration.md).

## CMYK import color conversion (lcms2 vs Adobe ACE)

Calibrated against PS's `convertProfile("sRGB IEC61966-2.1", RELATIVECOLORIMETRIC, BPC=true, dither=false)` on a 20-patch SWOP v2 probe. Patchy's import (vendored lcms2, `TYPE_CMYK_8_REV -> TYPE_RGB_8`, `INTENT_RELATIVE_COLORIMETRIC`, `cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE`) matched 14/20 patches byte-exactly; max per-channel delta 2, mean 0.133. The residue is lcms2-vs-ACE CLUT interpolation, unfixable without Adobe's CMM. `dither=false` matters: PS's default 8-bit dither adds noise. Pins: SWOP K100 -> (35,31,32); C0 M100 Y100 K0 -> (237,28,36) ("Photoshop red"); paper -> white; all-100 -> (0,0,0).

## Clipping masks

`photoshop-clipping-mask.psd` (semi-transparent base, clipped Multiply and Levels) matches PS byte-exactly. Members composite in isolation against the base's color at full strength (destination alpha 1); base alpha masks the ensemble, which merges with the base's blend mode and opacity. Hiding the base hides the run.

**The base's layer effects draw OVER its clipped members** (`photoshop-clip-base-effects.psd/bmp`, #41): the members are part of the base's fill, so its Color/Gradient/Pattern Overlay, Satin, Inner Glow, Inner Shadow, Bevel and Stroke all land on top of them, and its blend mode, opacity, Fill, mask and channel restriction apply to the merged result. `clbl` "Blend Clipped Layers as Group" (4-byte bool block, PS default ON and absence means on; `LayerStyle::blend_clipped_elements`, written only when off) matters only through `infx`: clbl OFF with infx ON folds the overlays and Satin into the base first, so the members cover them, while Inner Shadow and Stroke still paint over the members. A Multiply member over a 50%-opacity Normal base renders identically with clbl on or off: members keep blending against the base color at full strength and fading with the base either way. Renderer: `composite_sibling_layers` builds the base's raw content plus its members as one `ClipRunContent` source and runs it through the ordinary effect pipeline, which keeps every effect mask on the real source (members cannot widen the clip). Divergences: a Blend If base keeps the isolated-buffer path (its per-pixel gate is calibrated on the base's own colors) and there its effects still render under the members; adjustment-layer bases render clipped siblings unrestricted; whether a non-Normal base's blend mode stops carrying the members with clbl off is unprobed.

Folder bases use their union silhouette for clipping coverage. Plain Pass Through
bases first blend their children against the outside backdrop, then remove the
backdrop contribution to recover straight base colors for the clipped members.
An empty clipped member leaves Linear Dodge unchanged; a half-opacity member
keeps the same backdrop colors (`passthrough_clipping_mask_blendmode.psd` probes).
Blend If, reduced Fill, knockout and styled bases retain their isolated pipelines.
Override-aware bounds preserve moves and effect geometry. Local `backglass-invert/Backglass_homebrew.psd` pins the background,
dragons and opaque text against PS's saved composite: `Invert 1` clips to the
dragon folder only; fractional text edges have a separate blending mismatch.

## Slices and annotations

No slice tool. Resources 1050 (Slices) and 1054 (slice URLs) round-trip byte for byte while canvas geometry is unchanged; `forget_psd_slices` (core/document.hpp) drops both on any crop, resize, rotation, flip or canvas size change (GitHub issue 242). No Note tool either: Photoshop's notes (the document-global `Anno` block) survive every edit and save byte for byte through the generic unknown-global path and are never shown (GitHub issue 258, `psd_annotations_survive_save_if_available`).

## Liquify compatibility boundary

Patchy's manual Liquify writes only the resulting layer pixels (no private Liquify block); it never authors or edits PS's native Liquify Smart Filter descriptor. Smart Objects must be rasterized before Liquify; imported unsupported Liquify stacks stay byte-preserved and preview-locked. Details and legal constraints: [liquify.md](liquify.md). Before adding native support: capture PS PSD pairs differing in one manual setting, determine the filterFX and FEid/FXid shapes, add fail-closed descriptor tests. Never approximate an uncalibrated descriptor or keep only a supported prefix.
