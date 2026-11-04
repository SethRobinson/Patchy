# Group compositing and knockout

Split from [blend-modes.md](blend-modes.md): how groups composite (Pass Through fades, isolation, styled groups, channel restrictions) and how Photoshop's Knockout renders.

## Group compositing: Pass Through, isolation, and group opacity

Calibrated against a Photoshop 2026 COM-authored fixture
(`test-fixtures/psd/photoshop-group-opacity.psd/.bmp`, pinned within 1/255 by
`psd_photoshop_group_opacity_fixture_matches_render`). The group branch lives
in `composite_layer` (src/render/layer_compositor.hpp); the UI renderer routes
every blend-if, masked, non-pass-through, or faded group through it
(src/ui/image_document_io.cpp), which also covers Merge Group and Merge Down.

- **Pass Through group opacity is a post-composite fade.** Children composite
  at full strength against the true backdrop (child blend modes and interior
  adjustments keep meeting the layers below), then the whole result
  interpolates back toward the pre-group backdrop by the group opacity: the
  PDF non-isolated-group alpha formula. One fade applies to the composite, so
  overlapping children never double-fade. Implemented as `CompositeSnapshot` +
  `fade_toward_snapshot` + a `store_color` overwrite primitive (source-over
  cannot reduce coverage). The fade covers the full clip because an interior
  adjustment with unlimited bounds can touch backdrop pixels outside the
  children's render bounds. A group raster/vector mask still attenuates each
  child contribution in place; the fade applies once on top.
- **Non-pass-through groups isolate.** Children composite into a transparent
  `IsolatedClipGroupTarget` (bounded by the children's render bounds when no
  overrides are active) and the merged result meets the backdrop with the
  group's blend mode, opacity, and mask: a Multiply child inside a Normal
  group does not see the backdrop. Groups default to `BlendMode::Normal` in
  core, so tests that want Photoshop's default folder behavior must set
  `BlendMode::PassThrough` explicitly (the UI folder command and PSD import
  do).
- **Blend-if groups always isolate** (calibrated separately; see the blend-if
  group tests). A Pass Through group WITH blend-if keeps that path, where
  opacity is an alpha multiply on the merged result.
- **Every compositor target must implement `store_color`** (direct overwrite
  of straight color + coverage) alongside `composite_color`/`sample_color`:
  Rgb8PixelBufferTarget, Rgba8PixelBufferTarget and Rgb8PerPixelTarget
  (render/composite_targets.hpp), QImageCompositeTarget, IsolatedClipGroupTarget,
  and the forwarding GroupMaskedTarget.
- **Styled groups render their layer effects** (the calibrated rules live in
  docs/ps-compat.md "Layer effects on GROUPS"). A non-pass-through
  styled group routes its flattened `IsolatedClipGroupTarget` content through
  `composite_pixel_layer` via a pixel override (the group plays the layer's
  role; folder Fill fades the content only). A
  PASS-THROUGH styled group does NOT isolate: children composite against the
  true backdrop as always, while a silhouette flatten feeds exterior effects
  (painted before the children) and interior effects (painted above them,
  each with its own blend mode). Style-less groups take the unstyled paths; `group_style_renders` (core/layer_render_utils) is the gate, and
  `layer_render_bounds`/`layer_effect_padding` include the group's own style
  padding (zero for empty styles).
- Photoshop Knockout (shallow/deep) renders from the preserved `knko` block;
  see "Knockout transparency" below.
- Advanced Blending "Channels" restrictions ('brst') render on layers,
  adjustment layers, groups (pass-through included, which does NOT force
  isolation), and clip runs; the calibrated rules live in
  [ps-compat.md](ps-compat.md) and the wrapper is `ChannelRestrictedTarget`
  in render/layer_compositor.hpp.

## Knockout transparency

The CPU compositor and canvas renderer interpret native `knko` mode bytes
0/1/2 (None/Shallow/Deep), with the four-byte block preserved verbatim on save.
Unsupported payloads stay raw and do not activate rendering. Blending Options'
Knockout combo edits the mode through `set_layer_knockout` (core/layer.hpp): it
patches the byte in place (PS writes `knko` on every layer) or appends a block for
a non-None mode; an unknown payload disables the combo. Photoshop's [knockout guide](https://helpx.adobe.com/photoshop/using/knockout-reveal-content-layers.html)
describes the general behavior; Photoshop 2026 COM captures pin these details:

- At document level both modes reveal the real Background, or transparency if
  there is none. The bottom RGB layer without a transparency channel is the
  Background; an opaque RGBA layer or the name "Background" is insufficient.
- Pass Through groups give Shallow their entry backdrop and inherit the Deep
  floor. Isolated groups reset both floors to transparency, so even Deep stops
  at that boundary. A folder with knockout composites its child silhouette as
  one source. The eight small psd-tools `knockout-*` cases distinguish these
  boundaries, including a cyan Background and no Background.
- Shape is source alpha times the layer/vector mask and master Opacity. Fill
  scales the paint only: Fill 0 still cuts the whole shape. The content blends
  against the knockout floor. The uncovered contribution of the preceding stack
  is restored in premultiplied space after the content pass, avoiding a second
  application of antialiasing or Opacity. The 24 pixel/folder captures varying
  alpha, Fill and Opacity agree byte-for-byte.
- With Blend Clipped Layers as Group enabled, clipped members knock back to the
  clipping base in both modes, without widening its transparency. The disabled
  option's cross-run knockout and knockout on a clipping base are not modeled;
  these remain approximations. Knockout on an adjustment layer also remains
  unmodeled.
- Floor snapshots exist only in stacks containing knockout. Isolation/silhouette
  rendering has an explicit empty floor; clipped and strip renders retain document
  coordinates. Mask and Opacity attenuation survive Pass Through nesting. The same
  algorithm operates on encoded 8/16-bit and linear 32-bit targets. RGB previews
  retain logical alpha until the final white matte so holes do not turn black.
  Hidden or zero-opacity knockout trees do not change the RGB preview's blend
  backdrop, including temporary visibility overrides.

Coverage: `compositor_knockout_*`, `psd_tools_knockout_matches_photoshop_if_available`,
`knockout_preview_preserves_alpha_depth_and_partial_bounds`, and
`hidden_knockout_does_not_change_rgb_matte_blending`.
Effect/Blend If/channel-restriction combinations beyond these captures are not
calibrated. No non-knockout byte baseline is changed.
