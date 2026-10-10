# Photoshop compatibility: COM techniques and PSD write rules

Canonical reference for Photoshop compatibility: the PSD write/corruption rules and the layer-style descriptor shapes. Read this before touching PSD descriptor I/O or layer-style writing; render and adjustment calibration records are indexed at the end.

Conventions: "PS" = Adobe Photoshop 2026 (version 27), the installed ground truth. Every rule below is pinned by PS COM captures unless noted. Fixtures named `photoshop-*` live in `test-fixtures/psd/` with pinning tests; `local-test-fixtures/` is machine-local.

## Required compatibility contract

Every PSD/PSB Patchy writes must open in Adobe Photoshop without warnings or
errors, including saves and exports from the UI, JavaScript, and MCP. A file
that needs repair or produces unknown-data or data-discard prompts fails this
requirement even if its preview looks correct.

Optional Patchy metadata is permitted only in extension points Photoshop
accepts without those problems. Use native PSD structures for editable content
and the established image-resource mechanism for private document metadata.
Do not introduce unknown per-layer tagged keys (the write rules below explain
that failure). Photoshop need not interpret Patchy-only metadata, such as
palette color names, and may discard it when saving.

A successful Patchy save/reopen or a structurally valid file is not evidence
of warning-free Photoshop opening. Changes to PSD writing must keep this
contract and report the verification actually performed. Opening with dialogs
suppressed cannot establish the absence of warnings. If Photoshop was not
checked, say so. Driving Photoshop through COM is always authorized
(AGENTS.md); desktop screenshots are not.

## COM scripting techniques

Lives in [photoshop-com.md](photoshop-com.md): the PowerShell entry point, dialog-mode caveats, the composite and saveAs workarounds, hygiene rules, and `scripts\dev\photoshop-open-check.ps1`, which reports the "unknown data" prompt per file. Read it before any capture or acceptance run.

## Write rules pinned against PS (silent corruption otherwise)

- Unchanged supported 8/16-bit CMYK/gray/Lab layers can retain the original
  document mode, profile and color channels. Content edits conservatively
  select RGB saving; see [native color preservation](psd-native-color.md).

- XMP resource 1060 dates require extended timezone offsets (`-07:00`, not
  `-0700`). Photoshop's XMP parser rejects the latter when importing File Info
  and shows a data-discard warning. The writer repairs basic offsets only in
  recognized, namespace-resolved XMP date properties; other packet bytes and
  valid IPTC/EXIF metadata stay intact. A date-only repair of the issue397
  fixture opens cleanly with Photoshop's error-enabled dialog mode.

- **Photoshop reads type from the document-level `Txt2` block and trusts it over the TySh.**
  The block holds one text object per type layer, addressed by the TySh TextIndex; a stale
  object silently wins (a layer retyped in Bahnschrift Light read back as Bahnschrift Bold).
  Patchy rebuilds the block on every save with type layers: an authored object per regenerated type layer,
  untouched objects kept, no layout caches, so Patchy type opens as native text with no prompt.
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
  depth, an empty standard layer-info section with the records in the `Lr16`/`Lr32` global
  block, descriptor colors linear in 32-bit files. PS refuses to open a 32-bit PSD ("open
  options are incorrect") with a layer or group in Color Burn, Linear Burn, Screen, Color Dodge,
  Overlay, Soft/Hard/Vivid/Linear/Pin Light, Hard Mix or Exclusion, so the writer writes Normal
  for those (`writable_layer_blend_mode`; `psd_deep_32_bit_blend_modes_follow_photoshop`).
  Layer-effect blend modes are unaffected.
- **Record and channel limits**: PS rejects more than 8000 layer records (a group counts as
  two; 8001 opens composite-only) and more than 56 channels including merged
  transparency. The writer refuses both with an error rather than writing such a file.
- **A gradient descriptor's `Trns` list is never empty.** Photoshop's gradients carry at least
  two transparency stops; an empty list (a scripted gradient fill without alphaStops) raises the
  "discard unknown data" prompt and the layer comes back empty. The vector fill writer supplies
  two opaque end stops when the model has none (the layer-style writers already did), and a file
  that already carries an empty list heals on save: the reader marks the layer's blocks dirty
  and the fill/stroke payload builders skip the byte-exact shortcut. Pinned by
  `psd_vector_gradient_fill_without_alpha_stops_writes_opaque_stops` and
  `psd_vector_gradient_without_transparency_stops_heals_on_save`
  (`patchy-gradient-empty-transparency.psd`).
- **Engine-data paragraph metrics carry a decimal point.** Photoshop reads a bare integer token as 16.16 fixed point: `/FirstLineIndent 24` read back as 0.000366 px (24/65536), losing the indent; `24.0` reads as 24 px (September 27, 2026). `engine_paragraph_metric` writes `24.0`, `-20.0`, `0.0`; pinned by `psd_writer_emits_v2_paragraph_layout`.
- **Do not author unknown per-layer tagged keys.** Even correctly padded private tags trigger Photoshop's unknown-data warning. Merged vector associations use image resource 4211 in the documented plug-in resource range (4000-4999), with native `lyid` references; legacy `pvcl`/`pvfi` tags remain read-only. See [layer-merging.md](layer-merging.md).
- **Patchy-only style options ride a plug-in image resource, never descriptor keys.** The continuous (long) drop shadow's `continuous`/`fade` fields travel in resource 4212 (layout in psd_io_internal.hpp) keyed by the layer's `lyid`, which `prepare_compound_vector_psd` assigns on save. The lfx2 keeps the pinned 12-item DrSh with `Dstn` = sweep length, so PS shows a plain shadow at Distance and may drop the resource on resave.
- **Compound path groups use Photoshop's continuation records.** Contours sharing one vmsk group index are one shape: the lead length record carries the combine op with +6 field 1 (even-odd; PS's own compound shapes write 2 = nonzero), every continuation record op 0xFFFF and +6 field 0. Giving each contour its own op unites them: PS fills a donut solid (2026-09-26 probes; `patchy-compound-group.psd/bmp`). Detail in [vector-tools.md](vector-tools.md).
- **Multiple open strokes need separate native shapes.** Photoshop closes open contours when a stroked shape contains multiple subpaths, even in Photoshop-authored files with correct open-path selectors. Solid centered strokes use a reversible native-group export; see [open-path-strokes.md](open-path-strokes.md).
- **No orphaned lnk2 or lnkE elements.** PS refuses an embedded or linked element no layer uses; the writer omits them ([smart-objects.md](smart-objects.md)).
- **Per-layer tagged blocks declare an even length with the pad byte inside it.** PS walks by declared length rounded up to even and never writes odd lengths; one odd block makes PS discard every later block in the record. `write_additional_layer_block` enforces it. Generated TySh instead keeps its body even internally (its 16-byte end-anchored tail must not be followed by a pad). Global-section blocks keep 4-byte alignment OUTSIDE the declared length. The reader stays exact-advance so old odd-block Patchy files load.
- **Every pixel record except PS's Background carries a transparency channel (-1).** PS reads a record without one as the Background: opaque over the WHOLE canvas whatever its bounds, hiding every layer beneath it. Only the bottom record covering exactly the canvas may omit it (`encode_layer`; `psd_opaque_rgb_layers_write_transparency_unless_background`).
- **Layer record flags bit 3 must be set on every layer** or PS applies legacy semantics. The layer mask shapes effect sources regardless of link state (the chain toggle only affects move). Effect output still lands on mask-hidden areas unless 'lmgm' is set (4 bytes, first byte bool; `LayerStyle::layer_mask_hides_effects`).
- **lfx2 effect blend modes must be full stringIDs** in 'BlnM': PS reads a 4-char code inside a length-prefixed stringID as Normal; the length-0 charID encoding works. CS-era files store true charIDs; `blend_mode_from_descriptor_enum` maps both, the writer emits stringIDs. Dissolve maps as 'Dslv', 'diss', and "dissolve". See [blend-modes.md](blend-modes.md).
- **GrFl (Gradient Overlay) is shape-sensitive**: PS resets its blend mode to Normal unless the descriptor uses PS's 14-item layout `enab, present, showInDialog, Md, Opct, Grad, Angl, Type, Rvrs, Dthr, gs99, Algn, Scl, Ofst` (other effects tolerate leaner layouts). `Mdpn` belongs to the destination stop (piecewise remap around 50%; first stop's value unused); `Intr` = Classic cubic after the remap; `gs99` selects Classic/Perceptual OKLab/Linear; `Algn=true` aligns to the visible transparency bounds, not the padded channel rect. Calibrated geometry (`photoshop-gradient-overlay-geometry.psd/bmp`; constants in the LayerProjection code): centers snap to `floor(bounds + extent/2)` per axis; point-mapped types share the half-ramp `floor(projected_span * scale / 2)`; Linear holds 0.5 at the center; Reflected/Radial/Diamond run 0 (center) to 1 (half_ramp); Radial/Diamond are isotropic in the angle-rotated frame, so `Angl` matters on non-square bounds; Angle sweeps one ramp per revolution clockwise, ignores Scale, 0.25 color on the center pixel. GdFl fill layers share the anchors but not the quantized span (docs/vector-tools.md). Offset-nonzero snapping unprobed.
- **FrFX (Stroke)**: solid = 10 items `enab, present, showInDialog, Styl, PntT, Md, Opct, Sz, Clr, overprint`; gradient = 19, inserting `Grad, gradientsInterpolationMethod, Angl, Type, Rvrs, Dthr, Scl, Algn, Ofst` before `overprint` (`Clr` stays as a black placeholder). `Grad` header name `Gradient`, plain-double RGB, stops ordered `Clr, Type, Lctn, Mdpn`, enum `gradientInterpolationMethodType/Gcls`, percent offset. Sixth type Shape Burst = stringID `shapeburst`, Stroke-only, NOT normalized to Linear by PS (render in [layer-effects-render.md](layer-effects-render.md)). `overprint` (default false) drives the stroke knockout ([layer-effects-render.md](layer-effects-render.md)). PS re-quantizes stop opacities to 8 bits on resave.
- **Satin (`ChFX`)**: 12 fields `enab, present, showInDialog, Md, Clr, AntA, Invr, Opct, lagl, Dstn, blur, MpgS`; no `Nose`. The Linear contour carries name `$$$/Contours/Defaults/Linear=Linear` and a `Crv ` list of `(0,0)`, `(255,255)` `CrPt`s. Patchy writes `AntA=false` after an edit, keeps untouched curves/`AntA` byte-for-byte, and keeps disabled records modeled (`photoshop-satin-default.psd`).
- **ebbl (Bevel & Emboss)**: 22 base items ordered `enab, present, showInDialog, hglM, hglC, hglO, sdwM, sdwC, sdwO, bvlT, bvlS, uglg, lagl, Lald, srgR, blur, bvlD, TrnS, antialiasGloss, Sftn, useShape, useTexture`; Contour inserts `MpgS, AntA, Inpr` after `useShape`, Texture appends `InvT, Algn, Scl , textureDepth, Ptrn, phase`; conditional keys are OMITTED while a sub-option is off. `bvlD`'s enum type is `BESs` (PS tolerated `BESl`). Spellings: `BESl` = InrB/OtrB/Embs/PlEb + stringID `strokeEmboss`; `bvlT` = SfBL/PrBL/Slmt.
- **ShpC contour objects**: `Nm  ` + `Crv ` list of `CrPt`s. Linear default = name "Linear", two-point identity ramp, NO `Cnty` keys; custom curves write `Cnty` on EVERY point (true = smooth). `StyleContour` keeps exact points (lossless round trip; Satin normalizes to Linear on edit); `build_style_contour_lut` renders them.
- **patternFill (Pattern Overlay)**: 10 items `enab, present, showInDialog, Md, Opct, Ptrn{Nm,Idnt}, Angl, Scl , Algn, phase{Hrzn,Vrtc doubles}`. `Algn` anchors at the layer's `fxrp` block (16 bytes, two BE doubles), which PS updates on move; render rules in [layer-effects-render.md](layer-effects-render.md).
- **Pattern data blocks** (`Patt`/`Pat2`/`Pat3` global blocks; codec src/psd/psd_patterns.*): per pattern `{u32 length, u32 version=1, u32 image mode, u16 height, u16 width, UnicodeString name (count includes trailing NUL), PascalString id (no padding), [768-byte table if indexed], VMA list}`, 4-byte padded. VMA list: `{u32 version=3, u32 length, rect, u32 max-channels (PS declares 24)}` then max+2 slots (`u32 written`, then `u32 length, u32 depth, rect, u16 depth, u8 compression, data`). Color channels first; transparency in the LAST slot (max+1). Compression 0 = raw planar rows (PS writes it for small tiles, Patchy always); 1 = PackBits with per-row u16 counts, read only. Modes: Gray 1, Indexed 2, RGB 3, CMYK 4, Multichannel 7 (CS-era bevel textures; one plane, PS treats as grayscale and preserves on resave). Imported blocks stay raw in `unknown_psd_resources` AND decode into `DocumentMetadata::patterns`; saves append one authored `Patt` block holding only referenced patterns no raw block covers (PS reads both together).
- **Rejected multi-ink patterns:** PS 2026 rejects mode-7 patterns with multiple color planes (`multichannel-pattern-fill.psd`) with a program error. At PSD/PSB save, `repair_multichannel_patterns` replaces only decodable records of that shape with their displayed RGBA8 tile encoded as RGB, retaining the UUID, name and alpha. Patchy displays the first ink as gray; it does not model spot-ink mixing. Valid single-plane mode-7 textures and other records remain byte-exact. The source raw blocks remain unchanged for undo.
- **`.pat` import** (src/psd/pat_reader.*): big-endian `8BPT` v1; the same VMA v3 planes without the outer per-pattern length/padding. Indexed PAT records carry a 772-byte ACT table (768 RGB + colors-used + transparent-index) vs PSD's 768. Accepts Gray/Indexed/RGB/CMYK, 8/16-bit, raw and PackBits; bounded bad items skip with warnings, structural damage stops the scan keeping the decoded prefix; trailing `8BIMphry` ignored. `test-fixtures/pat/hue.pat` is from Jaroslav Bereza's MIT-licensed `jardicc/pat-parser`; source URL, SHA-256, and license in `test-fixtures/pat/NOTICE.txt`.
- **Bevel Texture with an unresolvable pattern is DISABLED by PS** (Texture unchecked, renders off); Patchy mirrors that, untouched styles keep the raw lfx2.
- **Roundtrip**: PS opens Patchy-authored style files without warnings and returns every value via Action Manager (`photoshop-pattern-bevel-roundtrip.psd`). Built-in pattern presets carry fixed GUID-shaped ids (pattern_presets.cpp) PS accepts and re-embeds.
- **Legacy 'lrFX' is a PS 5.x mirror, IGNORED whenever lfx2/lmfx exists** (an lfx2 effect with enab=false must stay disabled; merging lrFX resurrects it); only layers with NO descriptor effects block read it. 'dsdw' stores blur/intensity/angle/distance as 16.16 fixed point plus a 0-255 opacity byte. An effect record is `'8BIM' + key + u32 size` with the payload BEGINNING at the u32 version (no second size field; PS 5.x writes 41-byte version-0 records). The 10-byte color = u16 space + four u16 components: 0 = RGB, 2 = CMYK INVERTED (0xFFFF = 0% ink), 8 = Grayscale 0-10000. PS color-manages CMYK black to RGB (35,31,32); Patchy's profile-less mix gives black, as for lfx2 CMYK. Legacy Intensity is NOT Spread/Choke but a transfer contour (coverage x `1 + Intensity/100`, clamped); Patchy leaves it unread (real files store 0).
- **Multi-instance effects ride 'lmfx'** (payload identical to lfx2 plus `dropShadowMulti`/`frameFXMulti`/... lists) beside a compatibility lfx2. Patchy parses lmfx via the lfx2 parser, treats it as authoritative in either block order, and drops the raw lmfx on edit (a stale one would win in PS). The writer emits multi lists inside lfx2 only, which PS reads (`photoshop-lmfx-multi-stroke.psd`).
- **Style presets and .asl** (codec src/psd/asl_io.*; feature doc docs/style-presets.md). Container: u16 version 2, '8BSL', u16 patterns version 3, u32 patterns-section length (EMPTY = length 0, no count field), 'Patt'-block pattern records, u32 style count, then per style a 4-aligned length-prefixed record of two version-16 descriptors: class 'null' {'Nm  ' TEXT (shipped files use the ZString "$$$/key=Display Name" form), 'Idnt' TEXT GUID} and class 'Styl' {documentMode (empty Objc), 'Lefx' (same shape as the lfx2 root, class id "Lefx"), optional 'blendOptions'}. Trailing '8BIM'+'phry' hierarchy ignored. blendOptions: 'Opct' UntF#Prc, 'Md  ' enum BlnM stringID, stringID-keyed 'fillOpacity' UntF#Prc, 'Blnd' channel objects; Fill Opacity omitted at 100%. Blend If layout pinned by `test-fixtures/asl/photoshop-style-blend-options.asl`.
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
- **PS refuses the whole document ("program error")** when (a) a `PtFl` fill or `vstk` pattern stroke references a pattern id resolving in neither the file's pattern blocks nor PS's loaded presets (preset fallback masks this in tests: fixture GUIDs live in this machine's presets), or (b) a `vogk` keyDescriptorList covers only some vmsk subpath groups. Patchy embeds a 1x1 transparent placeholder for unresolvable referenced ids (never a dangling reference) and writes vogk/vowv only when every subpath group has an emitting origination entry. Read-side heal: [docs/vector-tools.md](vector-tools.md).
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

**The base's layer effects draw OVER its clipped members** (`photoshop-clip-base-effects.psd/bmp`, COM-probed September 2026, GitHub issue 41): the members are part of the base's fill, so its Color/Gradient/Pattern Overlay, Satin, Inner Glow, Inner Shadow, Bevel and Stroke all land on top of them, and its blend mode, opacity, Fill, mask and channel restriction apply to the merged result. `clbl` "Blend Clipped Layers as Group" (4-byte bool block, PS default ON and absence means on; `LayerStyle::blend_clipped_elements`, written only when off) matters only through `infx`: clbl OFF with infx ON folds the overlays and Satin into the base first, so the members cover them, while Inner Shadow and Stroke still paint over the members. A Multiply member over a 50%-opacity Normal base renders identically with clbl on or off (the D cells): members keep blending against the base color at full strength and fading with the base either way. Renderer: `composite_sibling_layers` builds the base's raw content plus its members as one `ClipRunContent` source and runs it through the ordinary effect pipeline, which keeps every effect mask on the real source (members cannot widen the clip). Divergences: a Blend If base keeps the isolated-buffer path (its per-pixel gate is calibrated on the base's own colors) and there its effects still render under the members; adjustment-layer bases render clipped siblings unrestricted; whether a non-Normal base's blend mode stops carrying the members with clbl off is unprobed.

Folder bases use their union silhouette for clipping coverage. Plain Pass Through
bases first blend their children against the outside backdrop, then remove the
backdrop contribution to recover straight base colors for the clipped members.
An empty clipped member leaves Linear Dodge unchanged; a half-opacity member
keeps the same backdrop colors (PS COM probes on `passthrough_clipping_mask_blendmode.psd`).
Blend If, reduced Fill, knockout and styled bases retain their isolated pipelines.
Override-aware bounds preserve moves and effect geometry. Local `backglass-invert/Backglass_homebrew.psd` pins the background,
dragons and opaque text against PS's saved composite: `Invert 1` clips to the
dragon folder only; fractional text edges have a separate blending mismatch.

## Liquify compatibility boundary

Patchy's manual Liquify writes only the resulting ordinary layer pixels (no private Liquify block), so PS sees the rasterized appearance; it never authors or edits PS's native Liquify Smart Filter descriptor. Smart Objects must be rasterized before Liquify; imported unsupported Liquify stacks stay byte-preserved and preview-locked. Implementation and legal constraints: `docs/liquify.md`. Before adding native support: capture PS PSD pairs differing in one manual setting, determine the filterFX and FEid/FXid shapes, add fail-closed descriptor tests. Never approximate an uncalibrated descriptor or keep only a supported prefix.
