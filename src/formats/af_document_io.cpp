#include "formats/af_document_io.hpp"
#include "formats/af_import_internal.hpp"

#include "core/environment.hpp"

#include "core/adjustment_layer.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/rect_utils.hpp"
#include "core/smart_object.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "psd/psd_smart_objects.hpp"
#include "filters/smart_filter_renderer.hpp"
#include "formats/document_flatten.hpp"
#include "formats/format_file_io.hpp"
#include "support/translate_noop.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <variant>
#include <string>
#include <utility>

namespace patchy::af {

using namespace detail;

namespace {

// Read-time-only marker for a Gaussian-blur layer effect awaiting its bake
// (resolved and ERASED by bake_pending_blur_effects before read() returns, so
// it never rides a saved document). Value: "<radius> <opacity> <preserve>".
constexpr const char* kAfPendingBlurKey = "patchy.af.pending_blur";

// Affinity blend wire enum (id, enum version) -> Patchy BlendMode. Layer Blnd
// and effect BlnM share ONE versioned enum space: a version-0 base table plus
// modes added later that REUSE ids under an enum-version bump. Pinned twice
// over: the fx-blend-sweep corpus renders (effects) and blend-sweep-v0.afphoto
// (a Photo 2.6 doc with one layer per blend-dropdown entry; the wire values
// pair 1:1 with the labels). The JS-facing BlendMode table (Pigment=1,
// Darken=2, Multiply=4, ...) is the VERSION-6 numbering only - the importer
// originally fed wire ids through it and misread every non-Normal layer
// written with version-0 ids (2.x documents, and 3.x PSD conversions like the
// deko corpus doc, whose Screen layer imported as Darken until this table).
// Unmapped modes return nullopt (caller uses Normal + a notice); Passthrough
// is a group concept, handled apart.
[[nodiscard]] std::optional<BlendMode> map_blend_mode(const af::AfEnum& value) {
  if (value.version == 0) {
    switch (value.id) {
      case 0: return BlendMode::Normal;
      case 1: return BlendMode::Darken;
      case 2: return BlendMode::Multiply;
      case 3: return BlendMode::ColorBurn;
      case 4: return BlendMode::Lighten;
      case 5: return BlendMode::Screen;
      case 6: return BlendMode::ColorDodge;
      case 7: return BlendMode::LinearDodge;  // "Add"
      case 8: return BlendMode::Overlay;
      case 9: return BlendMode::SoftLight;
      case 10: return BlendMode::HardLight;
      case 11: return BlendMode::VividLight;
      case 12: return BlendMode::PinLight;
      case 13: return BlendMode::HardMix;
      case 14: return BlendMode::Difference;
      case 15: return BlendMode::Exclusion;
      case 16: return BlendMode::Subtract;
      case 17: return BlendMode::Hue;
      case 18: return BlendMode::Saturation;
      case 19: return BlendMode::Luminosity;
      case 20: return BlendMode::Color;
      // 21 Average, 22 Negation, 23 Reflect, 24 Glow, 25 Erase: no exact
      // Patchy equivalent (approximate_blend_mode has the best-fit remaps).
      // The original FINDINGS pairing recorded 23 as Glow with Reflect at
      // 24/v2; the 2026-07-29 JS probe (BlendMode.Reflect -> wire 23/v0,
      // BlendMode.Glow -> 24/v0) and the dropdown order of the
      // blend-sweep-v0 layers pin the correct assignment.
      default: return std::nullopt;
    }
  }
  if (value.version == 1 && value.id == 2) {
    return BlendMode::DarkerColor;
  }
  if (value.version == 1 && value.id == 6) {
    return BlendMode::LighterColor;
  }
  if (value.version == 1 && value.id == 15) {
    return BlendMode::LinearLight;
  }
  if (value.version == 3 && value.id == 5) {
    return BlendMode::LinearBurn;
  }
  if (value.version == 4 && value.id == 21) {
    return BlendMode::Divide;
  }
  // 28/v2 Contrast Negate has no Patchy equivalent (and no adopted best fit).
  if (value.version >= 6) {
    // Version 6 renumbers the space to match the JS-facing BlendMode table
    // (Pigment inserted at 1, the darken/lighten groups reordered); seen on
    // layers painted with the unified app's new modes (CyanisePixel's Pigment
    // strokes are 1/v6).
    switch (value.id) {
      case 0: return BlendMode::Normal;
      // 1 Pigment: no Patchy equivalent.
      case 2: return BlendMode::Darken;
      case 3: return BlendMode::DarkerColor;
      case 4: return BlendMode::Multiply;
      case 5: return BlendMode::ColorBurn;
      case 6: return BlendMode::LinearBurn;
      case 7: return BlendMode::Lighten;
      case 8: return BlendMode::LighterColor;
      case 9: return BlendMode::Screen;
      case 10: return BlendMode::ColorDodge;
      case 11: return BlendMode::LinearDodge;  // "Add"
      case 12: return BlendMode::Overlay;
      case 13: return BlendMode::SoftLight;
      case 14: return BlendMode::HardLight;
      case 15: return BlendMode::VividLight;
      case 16: return BlendMode::PinLight;
      case 17: return BlendMode::LinearLight;
      case 18: return BlendMode::HardMix;
      case 19: return BlendMode::Difference;
      case 20: return BlendMode::Exclusion;
      case 21: return BlendMode::Subtract;
      case 22: return BlendMode::Divide;
      case 23: return BlendMode::Hue;
      case 24: return BlendMode::Saturation;
      case 25: return BlendMode::Luminosity;
      case 26: return BlendMode::Color;
      // 27 Average, 28 Negation, 29 Reflect, 30 Glow, 31 ContrastInvert,
      // 32 Erase: no Patchy equivalent.
      default: return std::nullopt;
    }
  }
  return std::nullopt;
}

// Affinity's Erase blend mode: an alpha-removal operator, not a color blend.
// Wire (25, v0) and (32, v6+). There is no BlendMode counterpart and there
// never will be (every import must stay PSD-savable), so fold_erase_layers
// rewrites an Erase carrier into an inverse-alpha MASK on a new isolated
// group holding the sibling layers beneath it.
[[nodiscard]] bool is_erase_blend(const af::AfEnum& value) {
  return (value.version == 0 && value.id == 25) || (value.version >= 6 && value.id == 32);
}

// Best-fit approximations for the Affinity-only blend modes onto modes Patchy
// (and PSD) already store - never new BlendMode values. Chosen by RMSE over a
// full-gamut probe (af-spike blend_probes, 2026-07-29: every (s,d) byte pair
// once in the red channel, scored against Affinity's own render; the harness
// reproduces Normal at 0.0 and Exclusion at 0.3):
//   Average  = (s+d)/2 exactly -> Normal with opacity folded x0.5
//              (algebraically identical; plain Normal scores 54)
//   Negation = 1-|1-s-d| exactly -> Exclusion (60 vs Normal's 109)
//   Reflect  = d^2/(1-s) exactly -> Overlay (37 vs 101)
//   Glow     = s^2/(1-d) exactly -> Linear Light (24 vs 51)
//   Pigment  (no classical formula fits) -> Overlay (45 vs 109)
// ContrastNegate/ContrastInvert stays Normal + notice: its best candidate
// (Exclusion) still scores 84 of Normal's 128 - too wrong to render as if
// right. Erase stays Normal + notice: it is an alpha-removal operator, not a
// color blend (the compositor has no such primitive, and PSD could not store
// it either).
struct BlendApproximation {
  BlendMode mode;
  float opacity_scale;        // folded into the carrier's opacity (Average)
  const char* affinity_name;  // both names ride the notice
  const char* patchy_name;
};

[[nodiscard]] std::optional<BlendApproximation> approximate_blend_mode(const af::AfEnum& value) {
  const bool v6 = value.version >= 6;  // the renumbered JS-facing space
  if ((value.version == 0 && value.id == 21) || (v6 && value.id == 27)) {
    return BlendApproximation{BlendMode::Normal, 0.5F, "Average", "Normal at half opacity"};
  }
  if ((value.version == 0 && value.id == 22) || (v6 && value.id == 28)) {
    return BlendApproximation{BlendMode::Exclusion, 1.0F, "Negation", "Exclusion"};
  }
  if ((value.version == 0 && value.id == 23) || (v6 && value.id == 29)) {
    return BlendApproximation{BlendMode::Overlay, 1.0F, "Reflect", "Overlay"};
  }
  if ((value.version == 0 && value.id == 24) || (v6 && value.id == 30)) {
    return BlendApproximation{BlendMode::LinearLight, 1.0F, "Glow", "Linear Light"};
  }
  if (v6 && value.id == 1) {
    return BlendApproximation{BlendMode::Overlay, 1.0F, "Pigment", "Overlay"};
  }
  return std::nullopt;
}

[[nodiscard]] bool is_identity_transform(const std::array<double, 6>& m) {
  return m[0] == 1.0 && m[1] == 0.0 && m[2] == 0.0 && m[3] == 0.0 && m[4] == 1.0 && m[5] == 0.0;
}

// Adjustment and live-filter node families (JSLib: *AdjustmentRasterNode /
// *FilterRasterNode). Their Bitm is the adjustment's mask plane, not content.
[[nodiscard]] bool is_adjustment_or_filter(std::uint32_t type_tag) {
  // Adjustments follow the *RA suffix convention (CrRA curves, HsRA HSL, ...;
  // ancestor chain *RA/AdjR). Live filters all share the ONE concrete tag FlRN
  // (FilterRasterNode; the filter kind rides in its Filt child class - pinned
  // by tiny-live-filter.af's Gaussian Blur). A mask-format bitmap on any other
  // unknown node lands in the same honest placeholder path anyway.
  const std::uint32_t suffix = type_tag & 0xFFFFU;
  return suffix == (static_cast<std::uint32_t>('R') << 8U | 'A') ||
         type_tag == af::tag4("FlRN");
}

// Whether build_layers may fold Erase-blend carriers into group masks at this
// nesting level. Disabled inside clipping runs: a wrapper group emitted into a
// clipped-sibling list would silently escape its base's matte.
enum class EraseFolding : std::uint8_t { Enabled, Disabled };

void build_layers(LayerBuildContext& ctx, const std::vector<std::shared_ptr<af::AfClass>>& children,
                  std::vector<Layer>& out, EraseFolding folding = EraseFolding::Enabled);
[[nodiscard]] Document read_container(std::span<const std::uint8_t> bytes,
                                      std::vector<std::string>& notices, int embed_depth);

void apply_layer_effects(LayerBuildContext& ctx, const af::AfClass& node, Layer& layer,
                         const std::string& name);

[[nodiscard]] Layer build_group(LayerBuildContext& ctx, const af::AfClass& node,
                                const std::string& name) {
  Layer group(ctx.document.allocate_layer_id(), name.empty() ? "Group" : name, LayerKind::Group);
  group.set_visible(node.bool_field(af::tag4("Visi"), true));
  group.set_opacity(static_cast<float>(std::clamp(node.double_field(af::tag4("Opac"), 1.0), 0.0, 1.0)));
  // Affinity groups blend as pass-through unless an explicit mode is set (the
  // Blnd field is absent for the default), which is exactly Patchy's semantics.
  group.set_blend_mode(BlendMode::PassThrough);
  const auto* blnd = node.field(af::tag4("Blnd"));
  if (blnd != nullptr) {
    if (const auto* e = std::get_if<af::AfEnum>(&blnd->value)) {
      if (const auto mapped = map_blend_mode(*e)) {
        group.set_blend_mode(*mapped);
      } else if (const auto approx = approximate_blend_mode(*e)) {
        group.set_blend_mode(approx->mode);
        group.set_opacity(group.opacity() * approx->opacity_scale);
        ctx.notices.push_back("Layer '" + group.name() + "': Affinity blend mode '" +
                              approx->affinity_name + "' approximated as " + approx->patchy_name);
      } else if (is_erase_blend(*e)) {
        // A Group carrier has no rasterized alpha at build time, so the fold
        // declines it (with a notice); do not keep pass-through meanwhile.
        group.set_blend_mode(BlendMode::Normal);
      } else {
        // An explicit-but-unmapped mode must not keep pass-through: that
        // renders the children as if the group did not exist at all.
        group.set_blend_mode(BlendMode::Normal);
        ctx.notices.push_back("Layer '" + group.name() +
                              "': blend mode not supported by Patchy; shown as Normal");
      }
    }
  }
  const auto* kids = class_list(node, af::tag4("Chld"));
  if (kids != nullptr) {
    // Children live in the group's local space (Affinity is a scene graph:
    // arkanis-discord's scaled containers hold children whose transforms
    // exactly invert the parent scale), so the group transform composes.
    const auto saved = ctx.transform;
    if (const auto xfrm = node.vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
      std::array<double, 6> child{};
      std::copy(xfrm.begin(), xfrm.end(), child.begin());
      ctx.transform = compose_transforms(ctx.transform, child);
    }
    std::vector<Layer> child_layers;
    build_layers(ctx, *kids, child_layers);
    ctx.transform = saved;
    // Affinity scopes a group-member adjustment to the group's own content
    // (the esdreika wild file: a -180 degree HSL shift inside the "Gravel"
    // group darkens only the gravel in Affinity's render); a pass-through
    // Patchy group would leak it over everything below the group. Isolate the
    // group (PS semantics scope adjustments to isolated groups) - only for
    // the pass-through default, an explicit wire blend stays authoritative.
    if (std::as_const(group).blend_mode() == BlendMode::PassThrough) {
      for (const auto& child : child_layers) {
        if (child.kind() == LayerKind::Adjustment && !child.clipped()) {
          group.set_blend_mode(BlendMode::Normal);
          break;
        }
      }
    }
    for (auto& child : child_layers) {
      group.add_child(std::move(child));
    }
  }
  apply_mask_children(ctx, node, group, name.empty() ? "Group" : name);
  apply_layer_effects(ctx, node, group, name.empty() ? "Group" : name);
  return group;
}

// A placed embedded document (EmbN): its Bitm is an EmbR that references the
// nested container stream (edc/<n>); import by recursing and flattening.
[[nodiscard]] std::optional<PixelBuffer> flatten_embedded(LayerBuildContext& ctx,
                                                          const af::AfClass& embr,
                                                          const std::string& name) {
  if (ctx.embed_depth >= LayerBuildContext::kMaxEmbedDepth) {
    ctx.notices.push_back("Layer '" + name + "': embedded documents nest too deeply; not rendered");
    return std::nullopt;
  }
  const af::AfClass* embc = embr.child_class(af::tag4("EmCn"));
  if (embc == nullptr) {
    return std::nullopt;
  }
  const auto* stream_field = embc->field(af::tag4("EmbC"));
  if (stream_field == nullptr) {
    return std::nullopt;
  }
  const auto* embedded_ref = std::get_if<af::AfEmbedded>(&stream_field->value);
  if (embedded_ref == nullptr || embedded_ref->data.empty()) {
    return std::nullopt;
  }
  const auto stream = ctx.container.streams.find(embedded_ref->data);
  if (stream == ctx.container.streams.end()) {
    return std::nullopt;
  }
  try {
    const auto nested_bytes = extract_stream(ctx.bytes, stream->second, embedded_ref->data, nullptr);
    std::span<const std::uint8_t> nested_span(nested_bytes);
    // edc/<n> streams wrap the nested container in an 8-byte "EmDc" tag + u32
    // prefix - in every generation observed (v9-era wild files AND current
    // corpus documents), so this flatten never succeeded before the unwrap
    // (2026-07-28 wild sweep). Tolerate a bare container anyway.
    if (nested_span.size() > 8 && nested_span[0] == 'E' && nested_span[1] == 'm' &&
        nested_span[2] == 'D' && nested_span[3] == 'c') {
      nested_span = nested_span.subspan(8);
    }
    std::vector<std::string> nested_notices;  // inner notices stay summarized
    const Document nested = read_container(nested_span, nested_notices, ctx.embed_depth + 1);
    ctx.notices.push_back("Layer '" + name + "': embedded document flattened on import");
    return flatten_document_rgba8(nested);
  } catch (const std::exception& error) {
    ctx.notices.push_back("Layer '" + name + "': embedded document could not be read (" +
                          error.what() + ")");
    return std::nullopt;
  }
}

// ---- Layer effects (the node's FiEf list of FilE-derived classes) ----
//
// Shared header on every effect: Enab (only enabled ones import), BlnM (an
// enum space of its own, DIFFERENT from the layer Blnd enum), Opac 0..1, SclO
// scale-with-object. Radii/offsets are document pixels; angles are radians.
// Colors are the same RGBA class shape as elsewhere (possibly shared-class
// refs; the tree parser resolves those). All mappings below are pinned by the
// authored one-toggle docs in local-test-fixtures/af-spike/corpus/fx-*.af
// (blend sweep, alignment/bevel-type sweeps, angle-direction renders).

// Effect-blend wire (id, enum_version) -> BlendMode. Effect BlnM uses the SAME
// versioned wire enum as the layer Blnd field (the blend-sweep-v0.afphoto
// pairing matches the fx-blend-sweep effect pinning value for value), so the
// shared table above does the lookup.
[[nodiscard]] std::optional<BlendMode> map_effect_blend_mode(const af::AfEnum& value) {
  return map_blend_mode(value);
}

[[nodiscard]] double degrees_from_radians(double radians) {
  double degrees = radians * 180.0 / 3.14159265358979323846;
  degrees = std::fmod(degrees, 360.0);
  return degrees < 0.0 ? degrees + 360.0 : degrees;
}

// Effect color + alpha (alpha folds into the effect opacity).
struct EffectColor {
  RgbColor color{0, 0, 0};
  float alpha{1.0F};
};

[[nodiscard]] std::optional<EffectColor> effect_color(const af::AfClass& effect, std::uint32_t tag) {
  const auto stored = read_rgba_color(effect.child_class(tag));
  if (!stored.has_value()) {
    return std::nullopt;
  }
  const auto to_channel = [](float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
  };
  return EffectColor{RgbColor{to_channel((*stored)[0]), to_channel((*stored)[1]), to_channel((*stored)[2])},
                     std::clamp((*stored)[3], 0.0F, 1.0F)};
}

[[nodiscard]] std::string effect_tag_text(std::uint32_t tag) {
  std::string text(4, '?');
  for (int i = 0; i < 4; ++i) {
    const auto byte = static_cast<char>((tag >> (24 - i * 8)) & 0xFFU);
    text[static_cast<std::size_t>(i)] = (byte >= 0x20 && byte < 0x7F) ? byte : '?';
  }
  return text;
}

// Map the node's FiEf effects onto layer_style(). Kinds Patchy has no model
// for (Gaussian blur, ...) skip with a notice; the Phong 3D bevel (PhgB)
// notice-approximates as a smooth inner Bevel/Emboss. Angle conventions and
// the enum ids below are pinned by the authored one-toggle fixture docs.
void apply_layer_effects(LayerBuildContext& ctx, const af::AfClass& node, Layer& layer,
                         const std::string& name) {
  const auto* effects = class_list(node, af::tag4("FiEf"));
  if (effects == nullptr || effects->empty()) {
    return;
  }
  auto& style = layer.layer_style();
  for (const auto& entry : *effects) {
    if (entry == nullptr || !entry->bool_field(af::tag4("Enab"), false)) {
      continue;
    }
    const af::AfClass& effect = *entry;
    float opacity =
        static_cast<float>(std::clamp(effect.double_field(af::tag4("Opac"), 1.0), 0.0, 1.0));
    // Every effect kind reads blend() BEFORE opacity, so the Average
    // approximation can fold its half-opacity into the shared local.
    const auto blend = [&](BlendMode fallback) {
      const auto* field = effect.field(af::tag4("BlnM"));
      if (field != nullptr) {
        if (const auto* e = std::get_if<af::AfEnum>(&field->value)) {
          if (const auto mapped = map_effect_blend_mode(*e)) {
            return *mapped;
          }
          if (const auto approx = approximate_blend_mode(*e)) {
            opacity *= approx->opacity_scale;
            ctx.notices.push_back("Layer '" + name + "': effect blend mode '" +
                                  approx->affinity_name + "' approximated as " +
                                  approx->patchy_name);
            return approx->mode;
          }
          ctx.notices.push_back("Layer '" + name + "': effect blend mode approximated");
        }
      }
      return fallback;
    };
    const std::uint32_t kind = effect.type_tag;

    // Shadow Angl is the direction the shadow FALLS, screen-clockwise from +x
    // (0 = right, pi/2 = down; pinned by the fx-shadow-a0/a90 renders).
    // Patchy stores the Photoshop LIGHT angle (CCW from +x, y up): 180 - deg.
    const auto shadow_angle = [&] {
      return static_cast<float>(
          std::fmod(540.0 - degrees_from_radians(effect.double_field(af::tag4("Angl"), 0.0)),
                    360.0));
    };

    if (kind == af::tag4("Shad")) {  // outer shadow
      LayerDropShadow shadow;
      shadow.enabled = true;
      shadow.blend_mode = blend(BlendMode::Multiply);
      shadow.opacity = opacity;
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        shadow.color = color->color;
        shadow.opacity *= color->alpha;
      }
      shadow.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      shadow.distance = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Offs"), 0.0)));
      shadow.angle_degrees = shadow_angle();
      if (!effect.bool_field(af::tag4("Knck"), true)) {
        ctx.notices.push_back("Layer '" + name +
                              "': shadow drawn behind the layer ('fill knocks out shadow' off is not supported)");
      }
      style.drop_shadows.push_back(shadow);
    } else if (kind == af::tag4("InnS")) {  // inner shadow
      LayerInnerShadow shadow;
      shadow.enabled = true;
      shadow.blend_mode = blend(BlendMode::Multiply);
      shadow.opacity = opacity;
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        shadow.color = color->color;
        shadow.opacity *= color->alpha;
      }
      shadow.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      shadow.distance = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Offs"), 0.0)));
      shadow.angle_degrees = shadow_angle();
      style.inner_shadows.push_back(shadow);
    } else if (kind == af::tag4("Strk")) {  // outline
      LayerStroke stroke;
      stroke.enabled = true;
      stroke.blend_mode = blend(BlendMode::Normal);
      stroke.opacity = opacity;
      stroke.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 0.0)));
      // Wire Alig: 0 outside (the UI default), 1 centre, 2 inside (pinned by
      // the fx-outline-* alignment sweep).
      switch (enum_field_id(effect, af::tag4("Alig"), 0)) {
        case 1: stroke.position = LayerStrokePosition::Center; break;
        case 2: stroke.position = LayerStrokePosition::Inside; break;
        default: stroke.position = LayerStrokePosition::Outside; break;
      }
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        stroke.color = color->color;
        stroke.opacity *= color->alpha;
      }
      // Wire Ftyp (StrokeFillType): 0 solid, 1 contour, 2 gradient.
      const auto fill_type = enum_field_id(effect, af::tag4("Ftyp"), 0);
      if (fill_type == 2) {
        if (auto gradient = style_gradient_from_fill(effect.child_class(af::tag4("GrFl")))) {
          stroke.uses_gradient = true;
          stroke.gradient = std::move(*gradient);
        } else {
          ctx.notices.push_back("Layer '" + name + "': stroke effect fill approximated as solid");
        }
      } else if (fill_type != 0) {
        ctx.notices.push_back("Layer '" + name + "': stroke effect fill approximated as solid");
      }
      if (stroke.size <= 0.0F) {
        continue;  // zero-width outline draws nothing in Affinity either
      }
      style.strokes.push_back(stroke);
    } else if (kind == af::tag4("GrdO")) {  // gradient overlay
      LayerGradientFill overlay;
      overlay.enabled = true;
      overlay.blend_mode = blend(BlendMode::Normal);
      overlay.opacity = opacity;
      if (auto gradient = style_gradient_from_fill(effect.child_class(af::tag4("GrFl")))) {
        overlay.gradient = std::move(*gradient);
        style.gradient_fills.push_back(overlay);
      } else {
        ctx.notices.push_back("Layer '" + name + "': gradient overlay effect not decoded; skipped");
      }
    } else if (kind == af::tag4("ColO")) {  // colour overlay
      LayerColorOverlay overlay;
      overlay.enabled = true;
      overlay.blend_mode = blend(BlendMode::Normal);
      overlay.opacity = opacity;
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        overlay.color = color->color;
        overlay.opacity *= color->alpha;
      }
      style.color_overlays.push_back(overlay);
    } else if (kind == af::tag4("OutG")) {  // outer glow
      LayerOuterGlow glow;
      glow.enabled = true;
      glow.blend_mode = blend(BlendMode::Screen);
      glow.opacity = opacity;
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        glow.color = color->color;
        glow.opacity *= color->alpha;
      }
      glow.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      // Affinity glows are plain gaussian falloffs; the raw blur (Range 100)
      // is the closest match, not Photoshop's doubled UI default.
      glow.range = 100.0F;
      style.outer_glows.push_back(glow);
    } else if (kind == af::tag4("InnG")) {  // inner glow
      LayerInnerGlow glow;
      glow.enabled = true;
      glow.blend_mode = blend(BlendMode::Screen);
      glow.opacity = opacity;
      if (const auto color = effect_color(effect, af::tag4("Colr"))) {
        glow.color = color->color;
        glow.opacity *= color->alpha;
      }
      glow.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      glow.source = effect.bool_field(af::tag4("Cntr"), false) ? LayerInnerGlowSource::Center
                                                               : LayerInnerGlowSource::Edge;
      // Affinity glows are plain gaussian falloffs; the raw blur (Range 100)
      // is the closest match, not Photoshop's doubled UI default.
      glow.range = 100.0F;
      style.inner_glows.push_back(glow);
    } else if (kind == af::tag4("BevE")) {  // bevel/emboss
      LayerBevelEmboss bevel;
      bevel.enabled = true;
      bevel.highlight_blend_mode = blend(BlendMode::Screen);
      bevel.highlight_opacity = opacity;
      bevel.shadow_opacity =
          static_cast<float>(std::clamp(effect.double_field(af::tag4("ShOp"), 0.75), 0.0, 1.0));
      const auto* shadow_blend = effect.field(af::tag4("ShBM"));
      if (shadow_blend != nullptr) {
        if (const auto* e = std::get_if<af::AfEnum>(&shadow_blend->value)) {
          if (const auto mapped = map_effect_blend_mode(*e)) {
            bevel.shadow_blend_mode = *mapped;
          }
        }
      }
      if (const auto color = effect_color(effect, af::tag4("HiCl"))) {
        bevel.highlight_color = color->color;
        bevel.highlight_opacity *= color->alpha;
      }
      if (const auto color = effect_color(effect, af::tag4("ShCl"))) {
        bevel.shadow_color = color->color;
        bevel.shadow_opacity *= color->alpha;
      }
      bevel.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      bevel.soften = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Sftn"), 0.0)));
      // Wire Dept is the bump height in px (defaults 5.0 alongside Radi 5.0);
      // Photoshop depth is a percentage of the size, so the ratio maps it.
      bevel.depth = static_cast<float>(std::clamp(
          effect.double_field(af::tag4("Dept"), 5.0) /
              std::max(1.0, effect.double_field(af::tag4("Radi"), 5.0)),
          0.05, 10.0));
      // Azim/Elev are radians in the Photoshop light convention (135deg
      // default lights from the upper-left; pinned by the fx-bevel renders).
      bevel.angle_degrees =
          static_cast<float>(degrees_from_radians(effect.double_field(af::tag4("Azim"), 2.356194490192345)));
      bevel.altitude_degrees =
          static_cast<float>(std::clamp(degrees_from_radians(effect.double_field(af::tag4("Elev"), 0.7853981633974483)), 0.0, 90.0));
      bevel.direction_up = !effect.bool_field(af::tag4("Invt"), false);
      // Wire Beve: 0 inner, 1 outer, 2 emboss, 3 pillow (the JS enum 1:1).
      switch (enum_field_id(effect, af::tag4("Beve"), 0)) {
        case 1: bevel.style = BevelEmbossStyleKind::OuterBevel; break;
        case 2: bevel.style = BevelEmbossStyleKind::Emboss; break;
        case 3: bevel.style = BevelEmbossStyleKind::PillowEmboss; break;
        default: bevel.style = BevelEmbossStyleKind::InnerBevel; break;
      }
      style.bevels.push_back(bevel);
      ctx.notices.push_back("Layer '" + name +
                            "': bevel/emboss effect approximated (engine rendering differs)");
    } else if (kind == af::tag4("PhgB")) {  // 3D (Phong) bevel -> approximate
      LayerBevelEmboss bevel;
      bevel.enabled = true;
      bevel.highlight_blend_mode = BlendMode::Screen;
      bevel.shadow_blend_mode = BlendMode::Multiply;
      bevel.highlight_opacity = opacity;
      bevel.shadow_opacity = opacity * 0.75F;
      bevel.size = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Radi"), 5.0)));
      bevel.soften = static_cast<float>(std::max(0.0, effect.double_field(af::tag4("Sftn"), 0.0)));
      bevel.depth = 1.0F;
      // Lighting: the first PLig light's Alph (azimuth) / Beta (elevation).
      double azimuth = 0.7853981633974483;
      double elevation = 0.7853981633974483;
      if (const af::AfClass* lights = effect.child_class(af::tag4("Lits"))) {
        if (const auto* list = class_list(*lights, af::tag4("PLis"));
            list != nullptr && !list->empty() && list->front() != nullptr) {
          azimuth = list->front()->double_field(af::tag4("Alph"), azimuth);
          elevation = list->front()->double_field(af::tag4("Beta"), elevation);
        }
      }
      bevel.angle_degrees = static_cast<float>(degrees_from_radians(azimuth));
      bevel.altitude_degrees =
          static_cast<float>(std::clamp(degrees_from_radians(elevation), 0.0, 90.0));
      bevel.style = BevelEmbossStyleKind::InnerBevel;
      style.bevels.push_back(bevel);
      ctx.notices.push_back("Layer '" + name +
                            "': 3D effect approximated as Bevel/Emboss");
    } else if (kind == af::tag4("Gaus")) {
      // Gaussian blur layer effect: a content blur, not an lfx2-style
      // destination pass (PSD cannot store it), so it BAKES into the layer's
      // pixels once they are final - the importer stamps a pending marker
      // that bake_pending_blur_effects resolves at the end of the read. Wire:
      // Radi = the UI radius in document px 1:1 (fx-gaussian probe), PrAl =
      // preserve alpha.
      const double radius = std::max(0.0, effect.double_field(af::tag4("Radi"), 0.0));
      if (radius > 0.0) {
        layer.metadata()[kAfPendingBlurKey] =
            std::to_string(radius) + " " + std::to_string(opacity) + " " +
            (effect.bool_field(af::tag4("PrAl"), false) ? "1" : "0");
      }
    } else {
      ctx.notices.push_back("Layer '" + name + "': effect '" + effect_tag_text(kind) +
                            "' is not supported; skipped");
    }
    if (environment_variable_is_set("PATCHY_AF_TRACE")) {
      std::fprintf(stderr, "[af] effect '%s' on '%s'\n", effect_tag_text(kind).c_str(),
                   name.c_str());
    }
  }
}

// Read a curves Spln class into Patchy control points. `Vals` carries the xs,
// then the ys, then per-point tangent data; `Cnt ` (trailing space) is the
// point count. Interpolation differs slightly (both are smooth splines through
// the points), which the tolerance-based fixtures accept.
[[nodiscard]] CurveControlPoints curve_points_from_spline(const af::AfClass* spline) {
  if (spline == nullptr) {
    return {};
  }
  const auto count = static_cast<std::size_t>(spline->int_field(af::tag4("Cnt "), 0));
  const auto values = spline->vec_field(af::tag4("Vals"));
  if (count < 2 || values.size() < count * 2) {
    return {};
  }
  CurveControlPoints points;
  points.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double x = std::clamp(values[i], 0.0, 1.0);
    const double y = std::clamp(values[count + i], 0.0, 1.0);
    points.push_back({static_cast<int>(std::lround(x * 255.0)),
                      static_cast<int>(std::lround(y * 255.0))});
  }
  return points;
}

[[nodiscard]] bool curve_points_are_identity(const CurveControlPoints& points) {
  return points.size() == 2 && points.front() == CurveControlPoint{0, 0} &&
         points.back() == CurveControlPoint{255, 255};
}

// Adjustment nodes: map the kinds Patchy models onto real adjustment layers
// (wire semantics pinned by the adjust-* corpus docs; FINDINGS.md records the
// field tables). Unknown kinds and live filters keep the placeholder path.
// The node's own Bitm is its MASK plane; an all-empty plane means no mask.
[[nodiscard]] std::optional<Layer> build_adjustment_layer(LayerBuildContext& ctx,
                                                          const af::AfClass& node,
                                                          const std::string& display) {
  const std::uint32_t tag = node.type_tag;
  const af::AfClass* params = node.child_class(af::tag4("AdjP"));
  const auto to_255 = [](double value) {
    return static_cast<int>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
  };
  const auto to_percent = [](double value) {
    return static_cast<int>(std::lround(std::clamp(value, -1.0, 1.0) * 100.0));
  };

  AdjustmentSettings settings;
  bool approximate = false;
  if (tag == af::tag4("InRA")) {
    settings.kind = AdjustmentKind::Invert;
  } else if (params == nullptr) {
    return std::nullopt;
  } else if (tag == af::tag4("LeRA")) {
    settings.kind = AdjustmentKind::Levels;
    settings.levels.black_input = to_255(params->double_field(af::tag4("Blac"), 0.0));
    settings.levels.white_input = to_255(params->double_field(af::tag4("Whit"), 1.0));
    settings.levels.gamma_percent = std::clamp(
        static_cast<int>(std::lround(params->double_field(af::tag4("Gamm"), 1.0) * 100.0)), 10,
        1000);
    settings.levels.black_output = to_255(params->double_field(af::tag4("OutB"), 0.0));
    settings.levels.white_output = to_255(params->double_field(af::tag4("OutW"), 1.0));
    const auto blacks = params->vec_field(af::tag4("BlkC"));
    const auto whites = params->vec_field(af::tag4("WhtC"));
    const auto gammas = params->vec_field(af::tag4("GamC"));
    const auto out_blacks = params->vec_field(af::tag4("OBlC"));
    const auto out_whites = params->vec_field(af::tag4("OWhC"));
    LevelsRecord* records[3] = {&settings.levels.red, &settings.levels.green,
                               &settings.levels.blue};
    for (std::size_t channel = 0; channel < 3; ++channel) {
      LevelsRecord& record = *records[channel];
      if (blacks.size() > channel) {
        record.black_input = to_255(blacks[channel]);
      }
      if (whites.size() > channel) {
        record.white_input = to_255(whites[channel]);
      }
      if (gammas.size() > channel) {
        record.gamma_percent = std::clamp(
            static_cast<int>(std::lround(gammas[channel] * 100.0)), 10, 1000);
      }
      if (out_blacks.size() > channel) {
        record.black_output = to_255(out_blacks[channel]);
      }
      if (out_whites.size() > channel) {
        record.white_output = to_255(out_whites[channel]);
      }
    }
  } else if (tag == af::tag4("CrRA")) {
    settings.kind = AdjustmentKind::Curves;
    auto master = curve_points_from_spline(params->child_class(af::tag4("Mast")));
    if (master.size() < 2) {
      return std::nullopt;
    }
    settings.curves.rgb = std::move(master);
    CurveControlPoints* channels[3] = {&settings.curves.red, &settings.curves.green,
                                      &settings.curves.blue};
    const std::uint32_t spline_tags[3] = {af::tag4("C1Sp"), af::tag4("C2Sp"), af::tag4("C3Sp")};
    for (std::size_t channel = 0; channel < 3; ++channel) {
      auto points = curve_points_from_spline(params->child_class(spline_tags[channel]));
      if (points.size() >= 2 && !curve_points_are_identity(points)) {
        *channels[channel] = std::move(points);
      }
    }
  } else if (tag == af::tag4("BCRA")) {
    // Affinity's own brightness/contrast math differs from Patchy's
    // PS-legacy-calibrated formula; the value mapping is best-effort.
    settings.kind = AdjustmentKind::BrightnessContrast;
    settings.brightness_contrast.brightness =
        to_percent(params->double_field(af::tag4("Brig"), 0.0));
    settings.brightness_contrast.contrast =
        to_percent(params->double_field(af::tag4("Ctrs"), 1.0) - 1.0);
    approximate = true;
  } else if (tag == af::tag4("PoRA")) {
    settings.kind = AdjustmentKind::Posterize;
    settings.posterize.levels = std::clamp(
        static_cast<int>(params->int_field(af::tag4("Post"), 4)), 2, 255);
  } else if (tag == af::tag4("ThRA")) {
    settings.kind = AdjustmentKind::Threshold;
    settings.threshold.level =
        std::clamp(to_255(params->double_field(af::tag4("Thre"), 0.5)), 1, 255);
  } else if (tag == af::tag4("CBRA")) {
    // Shadows, midtones and highlights map onto Patchy's three tone ranges
    // (approximate). Affinity's -1..1 maps to 40% of Photoshop's -100..100: on
    // the adjust-colourbalance corpus file (midtones 0.35, -0.2, 0.15) scales
    // 34 to 40 give the closest render to Affinity's own (mean 0.84/255, worst
    // 4/255; the earlier 10x, fitted to the pre-#250 flat shift, is 3.0 and 10).
    settings.kind = AdjustmentKind::ColorBalance;
    const auto to_balance = [](double value) {
      return static_cast<int>(std::lround(std::clamp(value, -1.0, 1.0) * 40.0));
    };
    const auto tone = [&](const char* cyan_red, const char* magenta_green, const char* yellow_blue) {
      return ColorBalanceTone{to_balance(params->double_field(tag_of(cyan_red), 0.0)),
                              to_balance(params->double_field(tag_of(magenta_green), 0.0)),
                              to_balance(params->double_field(tag_of(yellow_blue), 0.0))};
    };
    settings.color_balance.shadows = tone("ShCR", "ShMG", "ShYB");
    settings.color_balance.set_tone(ColorBalanceToneRange::Midtones, tone("MiCR", "MiMG", "MiYB"));
    settings.color_balance.highlights = tone("HiCR", "HiMG", "HiYB");
    approximate = true;
  } else if (tag == af::tag4("HsRA")) {
    // HueA is in turns and maps 1:1 onto the visual shift (pinned against the
    // adjust-hsl render; the JS setter's sign is inverted relative to this,
    // which briefly suggested a negation - it is not one).
    settings.kind = AdjustmentKind::HueSaturation;
    settings.hue_saturation.hue_shift = std::clamp(
        static_cast<int>(std::lround(params->double_field(af::tag4("HueA"), 0.0) * 360.0)),
        -180, 180);
    settings.hue_saturation.saturation_delta =
        to_percent(params->double_field(af::tag4("SatA"), 0.0));
    settings.hue_saturation.lightness_delta =
        to_percent(params->double_field(af::tag4("LumA"), 0.0));
  } else {
    return std::nullopt;
  }

  // Unnamed adjustment nodes: Affinity's Layers panel labels these by their
  // adjustment kind, so a generic "Layer" would read as an empty layer here.
  const std::string label = node.string_field(af::tag4("Desc")).empty()
                                ? adjustment_display_name(settings.kind)
                                : display;
  Layer layer(ctx.document.allocate_layer_id(), label, LayerKind::Adjustment);
  configure_adjustment_layer(layer, settings);
  apply_common(ctx, node, layer, label);

  // The node's own bitmap (an M8/M16 plane) is the adjustment's mask; an
  // all-zero plane means "no mask painted".
  if (const af::AfClass* bitmap = node.child_class(af::tag4("Bitm"))) {
    if (auto decoded = decode_bitmap(ctx.bytes, ctx.container, *bitmap, label, &ctx.notices,
                                     nullptr);
        decoded && !decoded->mask.empty()) {
      bool any_nonzero = false;
      for (std::int32_t y = 0; y < decoded->mask.height() && !any_nonzero; ++y) {
        for (std::int32_t x = 0; x < decoded->mask.width(); ++x) {
          if (decoded->mask.pixel(x, y)[0] != 0) {
            any_nonzero = true;
            break;
          }
        }
      }
      if (any_nonzero) {
        const auto origin = layer_origin(node_xfrm(ctx, node));
        LayerMask mask;
        mask.bounds = Rect{origin ? origin->first : 0, origin ? origin->second : 0,
                           decoded->mask.width(), decoded->mask.height()};
        mask.pixels = std::move(decoded->mask);
        mask.default_color = 255;
        layer.set_mask(std::move(mask));
      }
    }
  }
  if (approximate) {
    ctx.notices.push_back("Layer '" + label +
                          "': adjustment converted approximately (the engines' math differs)");
  }
  return layer;
}

// Adjustments can ride a layer's AdCh enclosure list instead of its child
// list (Affinity nests an adjustment INSIDE a layer either way to clip it to
// that layer; esdreika stores Brightness/Contrast + HSL on a metal texture as
// adjuncts). Emit each as a clipped adjustment layer directly above the
// owner; apply_mask_children skips these same nodes so their planes never
// become the owner's mask.
void emit_adjunct_adjustments(LayerBuildContext& ctx, const af::AfClass& node,
                              std::vector<Layer>& out) {
  const auto* adjuncts = class_list(node, af::tag4("AdCh"));
  if (adjuncts == nullptr) {
    return;
  }
  for (const auto& adjunct : *adjuncts) {
    if (adjunct == nullptr || !is_adjustment_or_filter(adjunct->type_tag)) {
      continue;
    }
    const std::string name = adjunct->string_field(af::tag4("Desc"));
    if (auto layer = build_adjustment_layer(ctx, *adjunct,
                                            name.empty() ? std::string("Layer") : name)) {
      layer->set_clipped(true);
      out.push_back(std::move(*layer));
    } else {
      ctx.notices.push_back("Layer '" + node.string_field(af::tag4("Desc")) +
                            "': an attached Affinity adjustment or live filter is not supported "
                            "and was skipped");
    }
  }
}

// Wrap a pristine placed image as an embedded Patchy smart object: the
// untouched original file becomes the source (Edit/Replace Contents work,
// PSD saves embed it) while the decoded pixels stay the layer raster. Mirrors
// the convert-to-smart-object authoring flow (main_window_smart_objects.cpp).
void attach_placed_smart_object(LayerBuildContext& ctx, Layer& layer,
                                std::shared_ptr<const std::vector<std::uint8_t>> file_bytes,
                                const std::string& source_name, std::int32_t source_w,
                                std::int32_t source_h, const std::array<double, 8>& quad) {
  if (file_bytes == nullptr || file_bytes->empty() || source_w <= 0 || source_h <= 0) {
    return;
  }
  const auto& head = *file_bytes;
  std::string filetype = "    ";
  std::string fallback_extension;
  if (head.size() >= 3 && head[0] == 0xFF && head[1] == 0xD8) {
    filetype = "JPEG";
    fallback_extension = ".jpg";
  } else if (head.size() >= 4 && head[0] == 0x89 && head[1] == 'P') {
    filetype = "png ";
    fallback_extension = ".png";
  }
  const std::string filename =
      source_name.empty() ? "Placed" + fallback_extension : source_name;
  const auto uuid = generate_smart_object_uuid();
  ctx.document.metadata().smart_objects.add_embedded(uuid, filename, filetype,
                                                     std::move(file_bytes));
  SmartObjectPlacement placement;
  placement.uuid = uuid;
  placement.transform = quad;
  placement.width = source_w;
  placement.height = source_h;
  placement.resolution = ctx.document.print_settings().horizontal_ppi;
  const auto placed_instance = generate_smart_object_uuid();
  set_layer_smart_object_metadata(layer, placement, placed_instance, "SoLd", "",
                                  kSmartObjectRasterStatusPatchy);
  // The authored SoLd rides the normal preserve-unless-edited machinery on
  // PSD save, exactly like a Patchy-converted smart object.
  layer.unknown_psd_blocks().push_back(
      UnknownPsdBlock{"SoLd", psd::author_placed_layer_sold_payload(placement, placed_instance)});
}

// Artboard detection: old-generation files mark the node with ABEn=true
// (ns-logo, v7); current files store a non-null phrp -> aprp artboard-
// properties class instead (the authored tiny-artboards fixture pins it;
// arkanis-discord/desktop carry the same shape, including artboards nested
// inside transformed groups).
[[nodiscard]] bool is_artboard_node(const af::AfClass& node) {
  return node.type_tag == af::tag4("ShpN") &&
         (node.bool_field(af::tag4("ABEn"), false) ||
          node.child_class(af::tag4("phrp")) != nullptr);
}

// Union of every artboard box under `node` (transformed through the composed
// ancestor affine), for sizing the canvas when the spread stores no SprB.
void accumulate_artboard_bounds(const af::AfClass& node, const std::array<double, 6>& parent,
                                int depth, bool& any, std::array<double, 4>& bounds) {
  if (depth > 64) {
    return;
  }
  const auto* kids = class_list(node, af::tag4("Chld"));
  if (kids == nullptr) {
    return;
  }
  for (const auto& child : *kids) {
    if (child == nullptr) {
      continue;
    }
    auto composed = parent;
    if (const auto xfrm = child->vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
      std::array<double, 6> local{};
      std::copy(xfrm.begin(), xfrm.end(), local.begin());
      composed = compose_transforms(parent, local);
    }
    if (is_artboard_node(*child)) {
      const auto box = child->vec_field(af::tag4("ShpB"));
      if (box.size() == 4) {
        for (const auto& [sx, sy] : {std::pair<double, double>{box[0], box[1]},
                                     {box[2], box[1]},
                                     {box[0], box[3]},
                                     {box[2], box[3]}}) {
          const double px = composed[0] * sx + composed[1] * sy + composed[2];
          const double py = composed[3] * sx + composed[4] * sy + composed[5];
          bounds[0] = any ? std::min(bounds[0], px) : px;
          bounds[1] = any ? std::min(bounds[1], py) : py;
          bounds[2] = any ? std::max(bounds[2], px) : px;
          bounds[3] = any ? std::max(bounds[3], py) : py;
          any = true;
        }
      }
      continue;  // artboards do not nest inside artboards
    }
    accumulate_artboard_bounds(*child, composed, depth + 1, any, bounds);
  }
}

// Set by build_artboard and consumed by read_container: a top-level artboard becomes a
// native artboard there (docs/artboards.md); one nested in a group keeps this masked-group
// form, because native artboards never nest.
constexpr const char* kAfArtboardMarker = "patchy.af.artboard";

// The masked-group form of a top-level artboard as a native one: the mask's rect is the
// artboard, and a plain opaque solid background shape becomes its background color.
void make_native_artboard(Layer& group) {
  const auto& mask = std::as_const(group).mask();
  if (!mask.has_value() || mask->bounds.empty()) {
    return;
  }
  LayerArtboard artboard;
  artboard.rect = mask->bounds;
  artboard.background = ArtboardBackground::Transparent;
  auto& children = group.children();
  if (!children.empty()) {
    const auto& bottom = std::as_const(children.front());
    const auto* shape = bottom.vector_shape();
    const bool plain = bottom.name().ends_with(" Background") && shape != nullptr &&
                       shape->fill.kind == VectorFillKind::Solid && !shape->stroke.enabled && bottom.visible() &&
                       bottom.opacity() >= 1.0F && bottom.fill_opacity() >= 1.0F &&
                       bottom.blend_mode() == BlendMode::Normal && !bottom.mask().has_value() &&
                       bottom.vector_mask() == nullptr && bottom.layer_style().empty();
    if (plain) {
      const auto color = shape->fill.color;
      artboard.color = color;
      artboard.background = color == RgbColor{255, 255, 255} ? ArtboardBackground::White
                            : color == RgbColor{0, 0, 0}     ? ArtboardBackground::Black
                                                             : ArtboardBackground::Custom;
      if (artboard.background != ArtboardBackground::Custom) {
        artboard.color = RgbColor{255, 255, 255};
      }
      children.erase(children.begin());
    }
  }
  group.clear_mask();
  // Photoshop's artboards are isolated (Normal) groups.
  group.set_blend_mode(BlendMode::Normal);
  group.set_artboard(std::move(artboard));
}

void strip_artboard_markers(std::vector<Layer>& layers) {
  for (auto& layer : layers) {
    layer.metadata().erase(kAfArtboardMarker);
    strip_artboard_markers(layer.children());
  }
}

// An artboard: a container whose children live in its local space. It
// imports as a group at its layout position - the artboard's own rectangle
// (usually a solid background, sometimes fill-less for transparent
// artboards) paints as a bottom layer, the artboard Xfrm composes onto every
// descendant, and a rectangular mask clips the group to the artboard bounds
// exactly as Affinity clips artboard content. Pinned by ns-logo.afdesign (a
// grid of nine 1024 px artboards) and the authored tiny-artboards fixture
// (Affinity's own render clips a child rect at the artboard edge).
[[nodiscard]] Layer build_artboard(LayerBuildContext& ctx, const af::AfClass& node,
                                   const std::string& display) {
  Layer group(ctx.document.allocate_layer_id(), display, LayerKind::Group);
  group.set_visible(node.bool_field(af::tag4("Visi"), true));
  group.set_opacity(
      static_cast<float>(std::clamp(node.double_field(af::tag4("Opac"), 1.0), 0.0, 1.0)));
  group.set_blend_mode(BlendMode::PassThrough);
  group.metadata()[kAfArtboardMarker] = "1";

  std::vector<Layer> children;
  {
    // The artboard's own rectangle; the group carries the node's
    // visibility/opacity, so the background keeps defaults.
    std::string why;
    if (auto background = build_shape_layer(ctx, node, display + " Background", &why)) {
      background->set_visible(true);
      background->set_opacity(1.0F);
      children.push_back(std::move(*background));
    }
  }
  const auto saved = ctx.transform;
  if (const auto xfrm = node.vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
    std::array<double, 6> child{};
    std::copy(xfrm.begin(), xfrm.end(), child.begin());
    ctx.transform = compose_transforms(ctx.transform, child);
  }
  if (const auto* kids = class_list(node, af::tag4("Chld"))) {
    build_layers(ctx, *kids, children);
  }
  ctx.transform = saved;
  for (auto& child : children) {
    group.add_child(std::move(child));
  }

  // Clip to the artboard bounds (the ShpB box through the effective
  // transform; artboards are pure translations in practice, so the bounding
  // rect IS the artboard rect).
  const auto box = node.vec_field(af::tag4("ShpB"));
  if (box.size() == 4 && box[2] > box[0] && box[3] > box[1]) {
    double min_x = box[0];
    double min_y = box[1];
    double max_x = box[2];
    double max_y = box[3];
    if (const auto xfrm = node_xfrm(ctx, node); xfrm.size() == 6) {
      bool first = true;
      for (const auto& [sx, sy] : {std::pair<double, double>{box[0], box[1]},
                                   {box[2], box[1]},
                                   {box[0], box[3]},
                                   {box[2], box[3]}}) {
        const double px = xfrm[0] * sx + xfrm[1] * sy + xfrm[2];
        const double py = xfrm[3] * sx + xfrm[4] * sy + xfrm[5];
        min_x = first ? px : std::min(min_x, px);
        max_x = first ? px : std::max(max_x, px);
        min_y = first ? py : std::min(min_y, py);
        max_y = first ? py : std::max(max_y, py);
        first = false;
      }
    }
    const auto mask_x = static_cast<std::int32_t>(std::lround(min_x));
    const auto mask_y = static_cast<std::int32_t>(std::lround(min_y));
    const auto mask_w = static_cast<std::int32_t>(std::lround(max_x - min_x));
    const auto mask_h = static_cast<std::int32_t>(std::lround(max_y - min_y));
    if (mask_w > 0 && mask_h > 0 && mask_w <= kMaxLayerSide && mask_h <= kMaxLayerSide) {
      PixelBuffer plane(mask_w, mask_h, PixelFormat::gray8());
      for (std::int32_t y = 0; y < mask_h; ++y) {
        auto row = plane.row(y);
        std::memset(row.data(), 0xFF, row.size());
      }
      LayerMask mask;
      mask.bounds = Rect{mask_x, mask_y, mask_w, mask_h};
      mask.pixels = std::move(plane);
      mask.default_color = 0;  // content beyond the artboard is clipped away
      group.set_mask(std::move(mask));
    }
  }
  return group;
}

// The inverse-alpha plane for an Erase carrier: 255 - the coverage the layer
// would have painted. Coverage is the compositor's exact formula for a Pixel
// layer (source alpha x Opacity x Fill opacity x its own raster/vector mask),
// so the hole matches the shape Affinity erased, antialiased rim included.
// Clipped to the canvas; default_color 255 leaves everything beyond the
// carrier untouched. nullopt when the carrier erases nothing.
[[nodiscard]] std::optional<LayerMask> erase_mask_from_layer(const Layer& layer, Rect canvas) {
  const PixelBuffer& pixels = std::as_const(layer).pixels();
  const Rect source = layer.bounds();
  const Rect bounds = intersect_rect(source, canvas);
  if (pixels.empty() || bounds.empty() || bounds.width > kMaxLayerSide ||
      bounds.height > kMaxLayerSide) {
    return std::nullopt;
  }
  const auto channels = static_cast<std::int32_t>(pixels.format().channels);
  if (pixels.format().bit_depth != BitDepth::UInt8 || channels < 4) {
    return std::nullopt;
  }
  const float scale = std::clamp(layer.opacity(), 0.0F, 1.0F) *
                      std::clamp(layer.fill_opacity(), 0.0F, 1.0F);
  PixelBuffer plane(bounds.width, bounds.height, PixelFormat::gray8());
  bool erases_anything = false;
  for (std::int32_t y = 0; y < bounds.height; ++y) {
    const auto row = plane.row(y);
    const auto src = pixels.row(bounds.y + y - source.y);
    for (std::int32_t x = 0; x < bounds.width; ++x) {
      const auto sx = static_cast<std::size_t>(bounds.x + x - source.x);
      const float coverage = static_cast<float>(src[sx * static_cast<std::size_t>(channels) + 3U]) /
                             255.0F * scale *
                             layer_mask_alpha_at(layer, bounds.x + x, bounds.y + y);
      const auto keep = static_cast<std::uint8_t>(
          std::lround(std::clamp(1.0F - coverage, 0.0F, 1.0F) * 255.0F));
      row[static_cast<std::size_t>(x)] = keep;
      erases_anything = erases_anything || keep < 255;
    }
  }
  if (!erases_anything) {
    return std::nullopt;
  }
  LayerMask mask;
  mask.bounds = bounds;
  mask.pixels = std::move(plane);
  mask.default_color = 255;
  return mask;
}

// Rewrites each recorded Erase carrier (bottom-up) into an ISOLATED Normal
// group wrapping the sibling layers beneath it, masked by the carrier's
// inverse alpha - the only PSD-expressible construction: a pass-through group
// mask attenuates every child contribution separately (coverage 1-(1-m)^N
// over N stacked children), while the isolated path applies the mask exactly
// once to the merged stack. The carrier itself is dropped. Carriers the
// construction cannot express keep their layer (rendered Normal) + a notice.
void fold_erase_layers(LayerBuildContext& ctx, std::vector<Layer>& out,
                       const std::vector<std::size_t>& slots, EraseFolding folding) {
  constexpr int kMaxEraseFolds = 64;  // bounds wrapper nesting depth
  const Rect canvas{0, 0, ctx.document.width(), ctx.document.height()};
  std::size_t removed = 0;
  int folded = 0;
  for (const auto slot : slots) {
    const std::size_t index = slot - removed;
    if (index >= out.size()) {
      continue;  // defensive: a carrier whose branch emitted nothing
    }
    const Layer& carrier = out[index];
    const std::string name = carrier.name();
    const auto decline = [&](const std::string& why) {
      ctx.notices.push_back("Layer '" + name + "': Affinity blend mode 'Erase' " + why +
                            "; shown as Normal");
    };
    if (!carrier.visible() || carrier.opacity() <= 0.0F) {
      continue;  // erases nothing and paints nothing; leave silently
    }
    if (folding == EraseFolding::Disabled) {
      decline("is not supported inside a clipping group");
      continue;
    }
    if (carrier.kind() != LayerKind::Pixel) {
      decline("is not supported on a group or adjustment layer");
      continue;
    }
    if (carrier.clipped()) {
      decline("is not supported on a clipped layer");
      continue;
    }
    if (index + 1 < out.size() && out[index + 1].clipped()) {
      decline("is not supported on the base of a clipping group");
      continue;
    }
    if (index == 0) {
      decline("has no layers beneath it");
      continue;
    }
    if (folded >= kMaxEraseFolds) {
      decline("exceeds the supported number of Erase layers");
      break;
    }
    auto mask = erase_mask_from_layer(carrier, canvas);
    if (!mask.has_value()) {
      decline("is empty");
      continue;
    }
    const bool had_effects = !carrier.layer_style().empty();
    Layer wrapper(ctx.document.allocate_layer_id(), name + " (Erase)", LayerKind::Group);
    wrapper.set_blend_mode(BlendMode::Normal);  // isolated: the mask applies once
    for (std::size_t k = 0; k < index; ++k) {
      wrapper.add_child(std::move(out[k]));
    }
    wrapper.set_mask(std::move(*mask));
    out.erase(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(index) + 1);
    out.insert(out.begin(), std::move(wrapper));
    removed += index;
    ++folded;
    ctx.notices.push_back("Layer '" + name +
                          "': Affinity blend mode 'Erase' imported as a mask on a new group "
                          "holding the layers beneath it");
    if (had_effects) {
      ctx.notices.push_back("Layer '" + name +
                            "': its layer effects were dropped when the Erase was applied");
    }
  }
}

// Emit `node` (and, for content layers, its clipped Chld children after it) into
// `out`. Affinity nests clipped layers INSIDE their base layer's child list;
// Patchy models the same thing as clipped siblings above the base.
void build_layers(LayerBuildContext& ctx, const std::vector<std::shared_ptr<af::AfClass>>& children,
                  std::vector<Layer>& out, EraseFolding folding) {
  std::vector<std::size_t> erase_slots;
  for (const auto& child : children) {
    if (child == nullptr) {
      continue;
    }
    if (++ctx.layer_count > LayerBuildContext::kMaxLayers) {
      throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Affinity document has an implausible number of layers"));
    }
    const af::AfClass& node = *child;
    std::string name = node.string_field(af::tag4("Desc"));
    if (name.empty()) {
      // Placed images with no user-given name: Affinity's Layers panel shows
      // the image resource's file name (IRFN), so mirror that instead of the
      // generic "Layer" (the esdreika wild file's metal_grid.png nodes).
      name = node.string_field(af::tag4("IRFN"));
    }
    const std::string display = name.empty() ? std::string("Layer") : name;
    const std::uint32_t tag = node.type_tag;
    const af::AfClass* bitmap_class = node.child_class(af::tag4("Bitm"));

    // Erase carriers: remember the index this node's own layer will take.
    // Every dispatch branch below pushes it first (clipped children follow),
    // so out.size() here IS that index.
    if (const auto* blnd = node.field(af::tag4("Blnd")); blnd != nullptr) {
      if (const auto* e = std::get_if<af::AfEnum>(&blnd->value);
          e != nullptr && is_erase_blend(*e)) {
        erase_slots.push_back(out.size());
      }
    }

    // `compose` composes this node's own transform onto the child build:
    // children of ANY content node live in its local space (Affinity is a
    // scene graph; arkanis-discord's scaled containers carry children whose
    // transforms exactly invert the parent scale, and ns-logo's artboard
    // grid places children only through the artboard transform).
    const auto emit_clipped_children = [&](const af::AfClass* compose,
                                           bool base_is_placeholder = false) {
      const auto* kids = class_list(node, af::tag4("Chld"));
      if (kids == nullptr || kids->empty()) {
        return;
      }
      const auto saved = ctx.transform;
      if (compose != nullptr) {
        if (const auto xfrm = compose->vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
          std::array<double, 6> child{};
          std::copy(xfrm.begin(), xfrm.end(), child.begin());
          ctx.transform = compose_transforms(ctx.transform, child);
        }
      }
      // The clipped-sibling rule holds across generations (tiny-live-filter,
      // the corpus docs, and the 1.x wild trading cards), with one 1.x
      // exception: children of a base that painted NOTHING emit unclipped
      // there. vh-check's paint-less phone container (doc v4) holds its five
      // painted panels as plain scene-graph children; clipping them to the
      // empty placeholder hid the whole mockup. Modern files keep clipping
      // even to placeholders (tiny-live-filter's exposure adjustment pin).
      const bool clip_children = ctx.document_version >= 10 || !base_is_placeholder;
      std::vector<Layer> clipped;
      build_layers(ctx, *kids, clipped,
                   clip_children ? EraseFolding::Disabled : EraseFolding::Enabled);
      ctx.transform = saved;
      for (auto& layer : clipped) {
        if (clip_children) {
          layer.set_clipped(true);
        }
        out.push_back(std::move(layer));
      }
    };

    const auto emit_placeholder = [&](const std::string& why) {
      ctx.notices.push_back("Layer '" + display + "' " + why +
                            "; imported as an empty placeholder (not rendered)");
      Layer layer(ctx.document.allocate_layer_id(), display, LayerKind::Pixel);
      apply_common(ctx, node, layer, display);
      out.push_back(std::move(layer));
    };

    // Emit a successfully-built vector base (PCrv/ShpN) together with its crop
    // children. Flat children keep the clipped-siblings model. Children that
    // include a GROUP cannot (groups can never be clipped - Photoshop's rule -
    // so the run rendered unclipped, and a HIDDEN container leaked every
    // stacked card of the wild trading-card template). Those containers become
    // an ISOLATED wrapper group carrying the node's own visibility/opacity/
    // blend/effects/masks, the shape's paint as bottom child, the children
    // above it, and the shape outline baked into the group's mask (the crop) -
    // the same PSD-native construction artboards and the Erase fold use.
    const auto emit_vector_base_with_children = [&](Layer&& base_layer) {
      const auto* kids = class_list(node, af::tag4("Chld"));
      if (kids == nullptr || kids->empty()) {
        out.push_back(std::move(base_layer));
        return;
      }
      const auto saved = ctx.transform;
      if (const auto xfrm = node.vec_field(af::tag4("Xfrm")); xfrm.size() == 6) {
        std::array<double, 6> child{};
        std::copy(xfrm.begin(), xfrm.end(), child.begin());
        ctx.transform = compose_transforms(ctx.transform, child);
      }
      std::vector<Layer> child_layers;
      build_layers(ctx, *kids, child_layers, EraseFolding::Disabled);
      ctx.transform = saved;
      const auto has_group_child =
          std::any_of(child_layers.begin(), child_layers.end(),
                      [](const Layer& child) { return child.kind() == LayerKind::Group; });
      if (!has_group_child) {
        out.push_back(std::move(base_layer));
        for (auto& child : child_layers) {
          child.set_clipped(true);
          out.push_back(std::move(child));
        }
        return;
      }
      Layer wrapper(ctx.document.allocate_layer_id(), display, LayerKind::Group);
      // Node-level properties live on the wrapper; absent Blnd keeps the
      // constructor's Normal, which is the isolated compositing a crop needs.
      apply_common(ctx, node, wrapper, display);
      apply_mask_children(ctx, node, wrapper, display);
      if (std::as_const(wrapper).vector_mask() == nullptr) {
        if (auto crop = crop_outline_path(ctx, node)) {
          LayerVectorMask mask;
          mask.path = std::move(*crop);
          wrapper.set_vector_mask(std::move(mask));
          update_vector_mask_raster(wrapper, Rect{0, 0, ctx.document.width(), ctx.document.height()});
          // Bake the outline into a raster group mask when the raster mask
          // slot is free: folder records round-trip raster masks natively,
          // while group records never write vmsk blocks (the artboard/Erase
          // construction). An adjunct-occupied slot keeps the vector mask
          // instead (still renders; a PSD save loses only the crop then).
          const auto* baked = std::as_const(wrapper).vector_mask();
          if (!std::as_const(wrapper).mask().has_value() && baked != nullptr && !baked->cache.empty()) {
            LayerMask raster_mask;
            raster_mask.bounds = baked->cache_bounds;
            raster_mask.pixels = baked->cache;
            raster_mask.default_color = 0;  // content beyond the outline is cropped away
            wrapper.clear_vector_mask();
            wrapper.set_mask(std::move(raster_mask));
          }
        }
      }
      // The base keeps only its own paint; the unit properties (visibility,
      // opacity, blend, effects, masks) moved to the wrapper.
      base_layer.set_visible(true);
      base_layer.set_opacity(1.0F);
      base_layer.set_blend_mode(BlendMode::Normal);
      base_layer.layer_style() = LayerStyle{};
      base_layer.clear_mask();
      base_layer.clear_vector_mask();
      wrapper.add_child(std::move(base_layer));
      for (auto& child : child_layers) {
        wrapper.add_child(std::move(child));
      }
      out.push_back(std::move(wrapper));
    };

    // Artboards: containers whose children live in artboard-local space; the
    // artboard's own rectangle renders as a background inside the group and
    // the group clips to the artboard bounds (Affinity clips artboard
    // content). read_document sizes the canvas to the spread bounds so every
    // artboard imports at its layout position.
    if (is_artboard_node(node)) {
      out.push_back(build_artboard(ctx, node, display));
      continue;
    }

    // Adjustment nodes: the kinds Patchy models become real adjustment layers
    // (with their Bitm mask plane); the rest stay placeholders.
    if (is_adjustment_or_filter(tag)) {
      if (auto adjustment = build_adjustment_layer(ctx, node, display)) {
        out.push_back(std::move(*adjustment));
        emit_clipped_children(&node);
        continue;
      }
      emit_placeholder("is an Affinity adjustment or live filter (not applied yet)");
      emit_clipped_children(&node, /*base_is_placeholder=*/true);
      continue;
    }

    // Compound shapes: Affinity bakes the boolean result into the node's own
    // lowercase 'crvs' poly-curve, so the compound imports as ONE exact shape
    // layer with the node's own fill/stroke (per-child ComO ops stay
    // unmined). Without a decodable baked path the children still carry the
    // operand shapes; they import as a group (subtract operands render
    // opaque - approximate, the pre-2026-07 behavior).
    if (tag == af::tag4("Comp")) {
      if (auto compound = build_vector_layer(ctx, node, display)) {
        out.push_back(std::move(*compound));
        continue;
      }
      out.push_back(build_group(ctx, node, name));
      continue;
    }

    // Group: nest children (a group has a child list and no bitmap).
    if (tag == af::tag4("Grup") ||
        (node.field(af::tag4("Chld")) != nullptr && bitmap_class == nullptr &&
         tag != af::tag4("TxtA") && tag != af::tag4("TxtF") && tag != af::tag4("PCrv") &&
         tag != af::tag4("ShpN"))) {
      out.push_back(build_group(ctx, node, name));
      continue;
    }

    if (bitmap_class != nullptr) {
      std::optional<PixelBuffer> pixels;
      bool approximate_color = false;
      bool bitmap_is_mask_plane = false;
      std::string fail_reason;
      std::shared_ptr<const std::vector<std::uint8_t>> original_bytes;
      std::string original_name;
      if (bitmap_class->type_tag == af::tag4("EmbR")) {
        pixels = flatten_embedded(ctx, *bitmap_class, display);
      } else {
        auto decoded = decode_bitmap(ctx.bytes, ctx.container, *bitmap_class, display,
                                     &ctx.notices, &fail_reason);
        if (decoded && !decoded->rgba.empty()) {
          approximate_color = decoded->approximate_color;
          pixels = std::move(decoded->rgba);
          original_bytes = std::move(decoded->original_bytes);
          original_name = std::move(decoded->original_name);
        } else if (decoded && !decoded->mask.empty()) {
          // A node whose own bitmap is a MASK plane is an adjustment or live
          // filter Patchy does not map (live filters follow this shape; their
          // bitmap is the filter's mask, not content).
          bitmap_is_mask_plane = true;
        }
      }
      // The placed quad (source rect corners through the node transform), for
      // the smart-object wrapper when the layer is a pristine placed image.
      std::array<double, 8> quad{};
      std::int32_t source_w = 0;
      std::int32_t source_h = 0;
      auto xfrm = node_xfrm(ctx, node);
      // A MODERN embedded document (EmbR) is CENTER-anchored: the node
      // transform's translation maps the CENTER of the flattened canvas, not
      // its origin. Pinned against Affinity's render of the restaurant-menu
      // corpus doc (version 30), where all three placed swashes land exactly
      // on the canvas corners only under this rule (raw-origin placement put
      // them ~half a canvas low/right). The 1.x generation (versions 3..9)
      // anchors at the ORIGIN instead: center-anchoring ns-splash (v7)
      // measurably worsens its layout against its own thumbnail. Plain
      // Rstr/ImgN rasters keep origin anchoring in every generation
      // (corpus-pinned).
      if (bitmap_class->type_tag == af::tag4("EmbR") && pixels &&
          ctx.document_version >= 20) {
        if (xfrm.size() != 6) {
          xfrm = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
        }
        const double half_w = static_cast<double>(pixels->width()) / 2.0;
        const double half_h = static_cast<double>(pixels->height()) / 2.0;
        xfrm[2] -= xfrm[0] * half_w + xfrm[1] * half_h;
        xfrm[5] -= xfrm[3] * half_w + xfrm[4] * half_h;
      }
      if (pixels) {
        source_w = pixels->width();
        source_h = pixels->height();
        const double xs[4] = {0.0, static_cast<double>(source_w), static_cast<double>(source_w),
                              0.0};
        const double ys[4] = {0.0, 0.0, static_cast<double>(source_h),
                              static_cast<double>(source_h)};
        for (int corner = 0; corner < 4; ++corner) {
          if (xfrm.size() == 6) {
            quad[static_cast<std::size_t>(corner) * 2U] =
                xfrm[0] * xs[corner] + xfrm[1] * ys[corner] + xfrm[2];
            quad[static_cast<std::size_t>(corner) * 2U + 1U] =
                xfrm[3] * xs[corner] + xfrm[4] * ys[corner] + xfrm[5];
          } else {
            quad[static_cast<std::size_t>(corner) * 2U] = xs[corner];
            quad[static_cast<std::size_t>(corner) * 2U + 1U] = ys[corner];
          }
        }
      }
      const auto origin = layer_origin(xfrm);
      std::optional<PlacedRaster> placed;
      if (pixels && origin) {
        placed = PlacedRaster{std::move(*pixels), origin->first, origin->second};
      } else if (pixels) {
        // Scale/rotate transform: rasterize through the affine into an
        // axis-aligned layer (bilinear, pinned against Affinity's render).
        placed = resample_affine(*pixels, xfrm, false);
      }
      if (placed) {
        if (approximate_color) {
          ctx.notices.push_back("Layer '" + display +
                                "': CMYK converted without a color profile (approximate)");
        }
        const std::int32_t w = placed->pixels.width();
        const std::int32_t h = placed->pixels.height();
        Layer layer(ctx.document.allocate_layer_id(), display, std::move(placed->pixels));
        layer.set_bounds(Rect{placed->x, placed->y, w, h});
        apply_common(ctx, node, layer, display);
        const bool wrapped_smart_object = original_bytes != nullptr;
        if (original_bytes != nullptr) {
          attach_placed_smart_object(ctx, layer, std::move(original_bytes), original_name,
                                     source_w, source_h, quad);
        }
        apply_mask_children(ctx, node, layer, display);
        if (environment_variable_is_set("PATCHY_AF_TRACE")) {
          std::size_t opaque = 0;
          const auto& px = layer.pixels();
          for (std::int32_t yy = 0; yy < px.height(); yy += 16) {
            const auto row_span = std::as_const(px).row(yy);
            for (std::int32_t xx = 0; xx < px.width(); xx += 16) {
              opaque += row_span[static_cast<std::size_t>(xx) * 4U + 3U] > 0 ? 1U : 0U;
            }
          }
          std::fprintf(stderr, "[af] raster layer '%s': %dx%d at %d,%d sampled-opaque=%zu so=%d\n",
                       display.c_str(), w, h, placed->x, placed->y, opaque,
                       wrapped_smart_object ? 1 : 0);
        }
        out.push_back(std::move(layer));
        emit_adjunct_adjustments(ctx, node, out);
        emit_clipped_children(&node);
        continue;
      }
      emit_placeholder(
          bitmap_is_mask_plane
              ? "is an Affinity adjustment or live filter (not applied yet)"
              : (!pixels ? (fail_reason.empty() ? "has an unsupported pixel format" : fail_reason)
                         : "has a degenerate transform"));
      emit_clipped_children(&node, /*base_is_placeholder=*/true);
      continue;
    }

    // Text: extract the story into a pending text layer (rendered post-open).
    const bool is_text = tag == af::tag4("TxtA") || tag == af::tag4("TxtF");
    if (is_text) {
      if (auto text_layer = build_text_layer(ctx, node, display)) {
        apply_mask_children(ctx, node, *text_layer, display);
        out.push_back(std::move(*text_layer));
        emit_clipped_children(&node);
        continue;
      }
    }

    // Vector curves: real shape layers with baked pixels.
    const bool is_vector = tag == af::tag4("PCrv");
    if (is_vector) {
      if (auto vector_layer = build_vector_layer(ctx, node, display)) {
        emit_vector_base_with_children(std::move(*vector_layer));
        continue;
      }
    }

    // Parametric shapes: the kinds Patchy models (rectangle with corner
    // radii, ellipse, regular polygon) become shape layers; the rest keep a
    // placeholder naming the reason.
    const bool is_shape = tag == af::tag4("ShpN");
    if (is_shape) {
      std::string why;
      if (auto shape_layer = build_shape_layer(ctx, node, display, &why)) {
        emit_vector_base_with_children(std::move(*shape_layer));
        continue;
      }
      emit_placeholder(why.empty() ? "is an Affinity shape kind Patchy does not model" : why);
      emit_clipped_children(&node, /*base_is_placeholder=*/true);
      continue;
    }

    // Undecodable vector leaves and text whose story shape is missing: named
    // placeholders.
    emit_placeholder(is_text ? "is text content" : (is_vector ? "is vector content"
                                                              : "is unsupported content"));
  }
  if (!erase_slots.empty()) {
    fold_erase_layers(ctx, out, erase_slots, folding);
  }
}

// True when at least one layer in the tree carries decoded pixels or a pending
// text render (real content once the post-open pass runs). An import where
// every node degraded to an empty placeholder renders as a blank canvas, which
// is worse than the tier-0 embedded preview; read_container checks this and
// prefers the preview for such documents.
[[nodiscard]] bool any_layer_has_pixels(const std::vector<Layer>& layers) {
  for (const auto& layer : layers) {
    if (layer.kind() == LayerKind::Pixel && !layer.pixels().empty()) {
      return true;
    }
    if (layer.metadata().contains(kLayerMetadataAfPendingText)) {
      return true;
    }
    if (any_layer_has_pixels(layer.children())) {
      return true;
    }
  }
  return false;
}

// Build a full tier-1 document from the parsed tree. Throws on a structurally
// unusable tree; the caller then falls back to the tier-0 preview.
[[nodiscard]] Document build_tier1(std::span<const std::uint8_t> bytes, const Container& container,
                                   const af::AfDocument& tree, std::vector<std::string>& notices,
                                   int embed_depth, bool* placeholders_only = nullptr) {
  if (tree.root == nullptr) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Affinity document tree is empty"));
  }
  // root (Pers) -> DocR field -> document node (DfSz + Chld=[spread]).
  const af::AfClass* doc_node = tree.root->child_class(af::tag4("DocR"));
  if (doc_node == nullptr) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Affinity document has no document node"));
  }
  // The document node carries DfSz [w,h]; the spread carries the layer children.
  const auto size = doc_node->vec_field(af::tag4("DfSz"));
  if (size.size() != 2) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Affinity document has no canvas size"));
  }
  std::int32_t width = static_cast<std::int32_t>(std::lround(size[0]));
  std::int32_t height = static_cast<std::int32_t>(std::lround(size[1]));
  if (width <= 0 || height <= 0 || width > kMaxLayerSide || height > kMaxLayerSide) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Affinity document has an invalid canvas size"));
  }
  // The document's Chld is the spread(s); each spread's Chld is the layers.
  const auto* doc_children = doc_node->field(af::tag4("Chld"));
  const auto* spreads =
      doc_children != nullptr
          ? std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&doc_children->value)
          : nullptr;
  if (spreads == nullptr || spreads->empty() || spreads->front() == nullptr) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Affinity document has no spread"));
  }
  if (spreads->size() > 1) {
    notices.push_back("This document has " + std::to_string(spreads->size()) +
                      " pages/artboards; only the first was imported");
  }
  const af::AfClass& spread = *spreads->front();
  const auto* spread_children = spread.field(af::tag4("Chld"));
  const auto* layers =
      spread_children != nullptr
          ? std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&spread_children->value)
          : nullptr;

  // The first spread's bounds (SprB) are the canvas whenever they are
  // present and sane. DfSz is only a fallback: 2.x-era files routinely
  // store the New Document preset size there rather than the current canvas
  // (a wild favicon: DfSz 1920x1080, SprB 800x800, and Affinity's own
  // thumbnail is square; across the 2026-07 wild sweeps every DfSz/SprB
  // mismatch resolves to SprB by thumbnail aspect). Artboard spreads lay
  // every artboard out side by side, so when SprB is missing the canvas is
  // the union of the artboard boxes; the whole build then shifts by the
  // spread origin. Affinity's own export of the tiny-artboards fixture pins
  // the union-canvas shape. Photo-persona spreads (2.x .afphoto and the
  // scripted 3.x fixtures) store NO SprB at all; their page rect lives in
  // SpMd -> PagR[0] (PgIn) -> rctp, which tracks canvas resizes while DfSz
  // keeps the creation size (a wild resized document: DfSz 512x512, rctp
  // 0,0,1024,1024, and Affinity shows 1024; every committed fixture's rctp
  // equals its true canvas, including the artboard union).
  double origin_x = 0.0;
  double origin_y = 0.0;
  {
    auto bounds = spread.vec_field(af::tag4("SprB"));
    if (bounds.size() != 4 || !(bounds[2] > bounds[0]) || !(bounds[3] > bounds[1])) {
      bool any_artboard = false;
      std::array<double, 4> computed{};
      accumulate_artboard_bounds(spread, {1.0, 0.0, 0.0, 0.0, 1.0, 0.0}, 0, any_artboard,
                                 computed);
      bounds.clear();
      if (any_artboard) {
        bounds.assign(computed.begin(), computed.end());
      }
    }
    if (bounds.size() != 4) {
      if (const af::AfClass* spread_metadata = spread.child_class(af::tag4("SpMd"))) {
        if (const auto* pages = class_list(*spread_metadata, af::tag4("PagR"));
            pages != nullptr && !pages->empty() && pages->front() != nullptr) {
          auto page_rect = pages->front()->vec_field(af::tag4("rctp"));
          if (page_rect.size() == 4 && page_rect[2] > page_rect[0] &&
              page_rect[3] > page_rect[1]) {
            bounds = std::move(page_rect);
          }
        }
      }
    }
    if (bounds.size() == 4) {
      const auto spread_w = static_cast<std::int32_t>(std::lround(bounds[2] - bounds[0]));
      const auto spread_h = static_cast<std::int32_t>(std::lround(bounds[3] - bounds[1]));
      if (spread_w > 0 && spread_h > 0 && spread_w <= kMaxLayerSide &&
          spread_h <= kMaxLayerSide) {
        width = spread_w;
        height = spread_h;
        origin_x = bounds[0];
        origin_y = bounds[1];
      }
    }
  }

  Document document(width, height, PixelFormat::rgba8());
  // Document resolution first (the root's units block stores pixels-per-inch):
  // placed smart objects record it in their placement.
  if (const af::AfClass* units = tree.root->child_class(af::tag4("UVCn"))) {
    const double ppi = units->double_field(af::tag4("UPPI"), 0.0);
    if (ppi >= 1.0 && ppi <= 10000.0) {
      document.print_settings().horizontal_ppi = ppi;
      document.print_settings().vertical_ppi = ppi;
    }
  }
  std::vector<Layer> built;
  if (layers != nullptr) {
    LayerBuildContext ctx{bytes, container, document, notices, embed_depth};
    ctx.document_version = tree.document_version;
    ctx.transform[2] = -origin_x;
    ctx.transform[5] = -origin_y;
    build_layers(ctx, *layers, built);
  }
  if (placeholders_only != nullptr) {
    *placeholders_only = !any_layer_has_pixels(built);
  }

  // Spread background: unless the spread is transparent (SprT), Affinity
  // paints its background color (BgrC, default white) behind every layer and
  // composites it into its own exports; mirror that with a bottom fill layer.
  // It is added BEFORE `built` deliberately: Affinity's spread background is
  // not a layer, so an Erase wrapper folded inside `built` must not cover it.
  if (!spread.bool_field(af::tag4("SprT"), false)) {
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F};
    if (const auto stored = read_rgba_color(spread.child_class(af::tag4("BgrC")))) {
      color = *stored;
    }
    if (color[3] > 0.0F) {
      PixelBuffer fill(width, height, PixelFormat::rgba8());
      std::array<std::uint8_t, 4> rgba{};
      for (int i = 0; i < 4; ++i) {
        rgba[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(std::lround(color[static_cast<std::size_t>(i)] * 255.0F));
      }
      for (std::int32_t y = 0; y < height; ++y) {
        auto row = fill.row(y);
        for (std::int32_t x = 0; x < width; ++x) {
          std::memcpy(row.data() + static_cast<std::size_t>(x) * 4U, rgba.data(), 4U);
        }
      }
      document.add_pixel_layer("Background", std::move(fill));
    }
  }
  for (auto& layer : built) {
    if (layer.kind() == LayerKind::Group && std::as_const(layer).metadata().contains(kAfArtboardMarker)) {
      make_native_artboard(layer);
    }
    document.add_layer(std::move(layer));
  }
  strip_artboard_markers(document.layers());
  if (document.layers().empty()) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Affinity document produced no layers"));
  }
  return document;
}

}  // namespace

bool sniff(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= 4 && bytes[0] == 0x00 && bytes[1] == 0xFF && bytes[2] == 0x4B &&
         bytes[3] == 0x41;
}

bool DocumentIo::can_read(std::span<const std::uint8_t> bytes) noexcept {
  return sniff(bytes);
}

namespace {

// Tier-0 fallback: import just the embedded preview as one layer.
[[nodiscard]] Document read_preview_only(std::span<const std::uint8_t> bytes,
                                         const Container& container, std::uint32_t document_version,
                                         std::vector<std::string>& notices) {
  PixelBuffer preview = extract_preview(bytes, container);
  Document document(preview.width(), preview.height(), PixelFormat::rgba8());
  document.add_pixel_layer("Affinity preview", std::move(preview));
  std::string summary = "Imported the embedded Affinity preview only (" +
                        std::to_string(document.width()) + "x" + std::to_string(document.height()) +
                        "; the document's layers could not be decoded)";
  if (document_version != 0) {
    summary += "; document format version " + std::to_string(document_version);
  }
  notices.push_back(std::move(summary));
  return document;
}

// Old-generation files SAVE their snapshot caches: the root's Snap subtree
// holds a nested document whose Rstr DyBm names a full-resolution PNG render
// of the whole document in a c/<n> stream (modern files save these slots
// empty). When the tree parses but nothing decodes to pixels, that baked
// render beats the small embedded thumbnail. The snapshot is a CACHE and can
// lag the document's latest edits (fladder-banner2's logo sits elsewhere in
// its snapshot than its node transform says), so it is a fallback only.
[[nodiscard]] std::optional<Document> read_snapshot_render(std::span<const std::uint8_t> bytes,
                                                           const Container& container,
                                                           const af::AfDocument& tree,
                                                           std::uint32_t document_version,
                                                           std::vector<std::string>& notices) {
  if (tree.root == nullptr) {
    return std::nullopt;
  }
  const auto* snap_field = tree.root->field(af::tag4("Snap"));
  if (environment_variable_is_set("PATCHY_AF_TRACE")) {
    std::fprintf(stderr, "[af] snapshot: field=%d\n", snap_field != nullptr);
  }
  if (snap_field == nullptr) {
    return std::nullopt;
  }
  std::vector<const af::AfClass*> pending;
  if (const auto* single = std::get_if<std::shared_ptr<af::AfClass>>(&snap_field->value)) {
    pending.push_back(single->get());
  } else if (const auto* list =
                 std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&snap_field->value)) {
    for (const auto& item : *list) {
      pending.push_back(item.get());
    }
  }
  // The largest decodable render wins (the full-document slot; other DyBms
  // under the snapshot are per-layer caches).
  std::optional<PixelBuffer> best;
  int guard = 0;
  while (!pending.empty()) {
    const af::AfClass* node = pending.back();
    pending.pop_back();
    if (node == nullptr) {
      continue;
    }
    if (++guard > 4096) {
      break;
    }
    if (node->type_tag == af::tag4("DyBm")) {
      auto original = decode_embedded_original(bytes, container, *node);
      if (environment_variable_is_set("PATCHY_AF_TRACE")) {
        std::fprintf(stderr, "[af] snapshot: dybm decoded=%d size=%dx%d\n", original.has_value(),
                     original ? original->pixels.width() : 0,
                     original ? original->pixels.height() : 0);
      }
      if (original) {
        if (!original->pixels.empty() &&
            (!best || static_cast<std::int64_t>(original->pixels.width()) *
                              original->pixels.height() >
                          static_cast<std::int64_t>(best->width()) * best->height())) {
          best = std::move(original->pixels);
        }
      }
      continue;
    }
    for (const auto& field : node->fields) {
      if (const auto* child = std::get_if<std::shared_ptr<af::AfClass>>(&field.value)) {
        pending.push_back(child->get());
      } else if (const auto* list =
                     std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&field.value)) {
        for (const auto& item : *list) {
          pending.push_back(item.get());
        }
      }
    }
  }
  if (!best) {
    return std::nullopt;
  }
  Document document(best->width(), best->height(), PixelFormat::rgba8());
  document.add_pixel_layer("Affinity preview", std::move(*best));
  std::string summary = "Imported the document's saved snapshot render only (" +
                        std::to_string(document.width()) + "x" + std::to_string(document.height()) +
                        "; the document's layers could not be decoded)";
  if (document_version != 0) {
    summary += "; document format version " + std::to_string(document_version);
  }
  notices.push_back(std::move(summary));
  return document;
}

// Affinity's Layers panel opens every document with its groups closed, and the
// .af tree records no disclosure state to restore (a Grup node carries only
// Visi/Edtb/MEtb/TrFV/deco, and the container holds no UI-state stream), so
// Affinity is applying a fixed closed default rather than reloading anything.
// Patchy's default for a group with no recorded state is expanded, so match the
// file's appearance in Affinity by stamping every imported group closed. This
// runs as a sweep over the finished tree so it also covers the groups the
// importer synthesizes itself: artboards, Erase folds, and crop-to-shape
// wrappers. The flag is native PSD (the folder's section-divider type), so it
// survives a save.
void collapse_imported_groups(std::vector<Layer>& layers) {
  for (auto& layer : layers) {
    if (layer.kind() != LayerKind::Group) {
      continue;
    }
    set_layer_group_expanded(layer, false);
    collapse_imported_groups(layer.children());
  }
}

// Shared by the public read() and the embedded-document recursion (embedded
// containers are complete .af containers stored in edc/<n> streams).
// Resolve every pending Gaussian-blur layer effect (stamped by
// apply_layer_effects) now that layer pixels are final: blur through the
// calibrated smart-filter kernel (Affinity's wire radius maps 1:1 onto
// document px), honoring the effect's opacity (blend back over the sharp
// original) and preserve-alpha flag (blur stays inside the original
// coverage). Pending text layers render post-open, after this pass can see
// them, so they keep a notice instead.
void bake_pending_blur_effects(std::vector<Layer>& layers, std::vector<std::string>& notices) {
  for (auto& layer : layers) {
    if (layer.kind() == LayerKind::Group) {
      bake_pending_blur_effects(layer.children(), notices);
    }
    auto& metadata = layer.metadata();
    const auto entry = metadata.find(kAfPendingBlurKey);
    if (entry == metadata.end()) {
      continue;
    }
    double radius = 0.0;
    double opacity = 1.0;
    int preserve_alpha = 0;
    {
      char* end = nullptr;
      radius = std::strtod(entry->second.c_str(), &end);
      if (end != nullptr) {
        opacity = std::strtod(end, &end);
      }
      if (end != nullptr) {
        preserve_alpha = static_cast<int>(std::strtol(end, nullptr, 10));
      }
    }
    metadata.erase(entry);
    if (metadata.contains(kLayerMetadataAfPendingText)) {
      notices.push_back("Layer '" + layer.name() +
                        "': Gaussian blur layer effect is not applied to text");
      continue;
    }
    const auto& source = std::as_const(layer).pixels();
    if (source.empty() || radius <= 0.0) {
      continue;
    }
    if (!std::isfinite(radius)) {
      notices.push_back("Layer '" + layer.name() + "': invalid Gaussian blur radius; effect skipped");
      continue;
    }
    radius = std::clamp(radius, 0.2, 2000.0);
    opacity = std::isfinite(opacity) ? std::clamp(opacity, 0.0, 1.0) : 1.0;
    try {
      const Rect source_bounds = layer.bounds();
      // The smart-filter blur keeps the supplied bounds (edge-repeat outside),
      // but Affinity's effect diffuses the layer's alpha boundary too - pad
      // with transparency so the edges soften and the bounds grow with the
      // spill.
      const std::int32_t pad =
          static_cast<std::int32_t>(std::ceil(radius)) * 3 + 1;
      PixelBuffer padded(source.width() + 2 * pad, source.height() + 2 * pad,
                         PixelFormat::rgba8());
      for (std::int32_t y = 0; y < source.height(); ++y) {
        const auto src_row = std::as_const(source).row(y);
        auto dst_row = padded.row(y + pad);
        std::copy(src_row.begin(), src_row.end(),
                  dst_row.begin() + static_cast<std::size_t>(pad) * 4U);
      }
      const Rect padded_bounds{source_bounds.x - pad, source_bounds.y - pad, padded.width(),
                               padded.height()};
      // Affinity's wire radius measures 2 sigma on its own renders while the
      // Photoshop-calibrated kernel's radius measures ~1 sigma (edge-profile
      // fit of the fx-gaussian probe), so halve it.
      auto blurred = render_photoshop_gaussian_blur(padded, padded_bounds, radius * 0.5);
      if (blurred.pixels.empty()) {
        continue;
      }
      if (opacity < 1.0) {
        // The effect blends over the sharp original at its opacity.
        auto& px = blurred.pixels;
        for (std::int32_t y = 0; y < px.height(); ++y) {
          auto row = px.row(y);
          const std::int32_t sy = blurred.bounds.y + y - source_bounds.y;
          for (std::int32_t x = 0; x < px.width(); ++x) {
            const std::int32_t sx = blurred.bounds.x + x - source_bounds.x;
            std::uint8_t original[4] = {0, 0, 0, 0};
            if (sx >= 0 && sy >= 0 && sx < source.width() && sy < source.height()) {
              const std::uint8_t* sp = source.pixel(sx, sy);
              std::copy(sp, sp + 4, original);
            }
            std::uint8_t* bp = row.data() + static_cast<std::size_t>(x) * 4U;
            // Mix in premultiplied space so transparent-area colors stay inert.
            const double ba = bp[3] / 255.0;
            const double oa = original[3] / 255.0;
            const double out_a = ba * opacity + oa * (1.0 - opacity);
            for (int c = 0; c < 3; ++c) {
              const double mixed =
                  bp[c] * ba * opacity + original[c] * oa * (1.0 - opacity);
              bp[c] = static_cast<std::uint8_t>(std::lround(
                  out_a > 0.0 ? std::clamp(mixed / out_a, 0.0, 255.0) : 0.0));
            }
            bp[3] = static_cast<std::uint8_t>(std::lround(std::clamp(out_a * 255.0, 0.0, 255.0)));
          }
        }
      }
      if (preserve_alpha != 0) {
        // Preserve Alpha keeps the original coverage: crop the blur back to the
        // source bounds and reinstate the original alpha plane.
        PixelBuffer kept(source.width(), source.height(), PixelFormat::rgba8());
        for (std::int32_t y = 0; y < kept.height(); ++y) {
          auto row = kept.row(y);
          const std::int32_t by = source_bounds.y + y - blurred.bounds.y;
          for (std::int32_t x = 0; x < kept.width(); ++x) {
            std::uint8_t* kp = row.data() + static_cast<std::size_t>(x) * 4U;
            const std::int32_t bx = source_bounds.x + x - blurred.bounds.x;
            if (bx >= 0 && by >= 0 && bx < blurred.pixels.width() && by < blurred.pixels.height()) {
              const std::uint8_t* bp = blurred.pixels.pixel(bx, by);
              std::copy(bp, bp + 3, kp);
            }
            kp[3] = source.pixel(x, y)[3];
          }
        }
        layer.set_pixels(std::move(kept));
        notices.push_back("Layer '" + layer.name() +
                          "': Gaussian blur layer effect baked into the layer pixels");
        continue;
      }
      layer.set_pixels(std::move(blurred.pixels));
      layer.set_bounds(blurred.bounds);
      notices.push_back("Layer '" + layer.name() +
                        "': Gaussian blur layer effect baked into the layer pixels");
    } catch (const std::exception&) {
      notices.push_back("Layer '" + layer.name() + "': Gaussian blur could not be applied; original pixels kept");
    }
  }
}

[[nodiscard]] Document read_container(std::span<const std::uint8_t> bytes,
                                      std::vector<std::string>& notices, int embed_depth) {
  if (!sniff(bytes)) {
    throw std::runtime_error(PATCHY_TRANSLATE_NOOP("QObject", "Not an Affinity document"));
  }
  const Container container = parse_container(bytes);
  if (container.version > kNewestVerifiedContainerVersion) {
    notices.push_back("This file was saved by a newer Affinity (container version " +
                      std::to_string(container.version) + "; Patchy has been verified up to " +
                      std::to_string(kNewestVerifiedContainerVersion) +
                      "); the layer import may be incomplete");
  }

  std::uint32_t document_version = 0;
  const auto doc_stream = container.streams.find("doc.dat");
  if (doc_stream != container.streams.end()) {
    try {
      const auto tree_bytes = extract_stream(bytes, doc_stream->second, "doc.dat", &notices);
      const af::AfDocument tree = af::parse_tree(std::span<const std::uint8_t>(tree_bytes));
      document_version = tree.document_version;
      bool placeholders_only = false;
      Document document = build_tier1(bytes, container, tree, notices, embed_depth,
                                      &placeholders_only);
      if (placeholders_only) {
        // Nothing decoded to pixels (all placeholders); a flat render shows
        // the user their document where the structural import would show a
        // blank canvas. Old files save a full-resolution snapshot render,
        // which beats the small embedded thumbnail; keep the structural
        // result only when neither exists.
        if (auto snapshot =
                read_snapshot_render(bytes, container, tree, document_version, notices)) {
          return std::move(*snapshot);
        }
        try {
          return read_preview_only(bytes, container, document_version, notices);
        } catch (const std::exception&) {
        }
      }
      bake_pending_blur_effects(document.layers(), notices);
      collapse_imported_groups(document.layers());
      return document;
    } catch (const std::exception& error) {
      // A structurally unusable tree (or an unforeseen shape) falls back to the
      // preview rather than failing the open. Record why for diagnosis.
      notices.emplace_back(std::string("Full layer import failed (") + error.what() +
                           "); fell back to the embedded preview");
      if (environment_variable_is_set("PATCHY_AF_TRACE")) {
        std::fprintf(stderr, "[af] tier-1 import threw: %s\n", error.what());
      }
    }
  }
  return read_preview_only(bytes, container, document_version, notices);
}

}  // namespace

Document DocumentIo::read(std::span<const std::uint8_t> bytes, std::vector<std::string>* notices) {
  std::vector<std::string> pending;
  Document document = read_container(bytes, pending, 0);
  if (notices != nullptr) {
    for (auto& notice : pending) {
      notices->push_back(std::move(notice));
    }
  }
  return document;
}

Document DocumentIo::read_file(const std::filesystem::path& path,
                               std::vector<std::string>* notices) {
  const auto bytes = formats::read_file_bytes(path, "Affinity");
  return read(std::span<const std::uint8_t>(bytes), notices);
}

namespace detail {

// parent-then-child composition in the wire layout [a, b, tx, c, d, ty]
// (dest = [a b; c d] * src + [tx, ty]).
[[nodiscard]] std::array<double, 6> compose_transforms(const std::array<double, 6>& parent,
                                                       const std::array<double, 6>& child) {
  return {parent[0] * child[0] + parent[1] * child[3],
          parent[0] * child[1] + parent[1] * child[4],
          parent[0] * child[2] + parent[1] * child[5] + parent[2],
          parent[3] * child[0] + parent[4] * child[3],
          parent[3] * child[1] + parent[4] * child[4],
          parent[3] * child[2] + parent[4] * child[5] + parent[5]};
}

// A node's effective transform: its wire Xfrm composed under the ancestor
// transform. Empty when both are identity (callers treat empty as identity,
// keeping the historical fast paths byte-identical).
[[nodiscard]] std::vector<double> node_xfrm(const LayerBuildContext& ctx, const af::AfClass& node) {
  auto xfrm = node.vec_field(af::tag4("Xfrm"));
  if (is_identity_transform(ctx.transform)) {
    return xfrm;
  }
  std::array<double, 6> child{1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
  if (xfrm.size() == 6) {
    std::copy(xfrm.begin(), xfrm.end(), child.begin());
  }
  const auto composed = compose_transforms(ctx.transform, child);
  return std::vector<double>(composed.begin(), composed.end());
}

[[nodiscard]] const std::vector<std::shared_ptr<af::AfClass>>* class_list(const af::AfClass& node,
                                                                          std::uint32_t tag) {
  const auto* field = node.field(tag);
  if (field == nullptr) {
    return nullptr;
  }
  return std::get_if<std::vector<std::shared_ptr<af::AfClass>>>(&field->value);
}

// Attach the node's mask (an M8/M16 raster in the AdCh "enclosure" list, or a
// vector layer subtree acting as the owner's clip mask - the latter becomes a
// native LayerVectorMask) to the built layer. Extra or undecodable masks
// degrade to a notice.
void apply_mask_children(LayerBuildContext& ctx, const af::AfClass& node, Layer& layer,
                         const std::string& name) {
  const auto* adjuncts = class_list(node, af::tag4("AdCh"));
  if (adjuncts == nullptr) {
    return;
  }
  bool applied = false;
  bool vector_applied = false;
  for (const auto& adjunct : *adjuncts) {
    if (adjunct == nullptr) {
      continue;
    }
    if (is_adjustment_or_filter(adjunct->type_tag)) {
      // An adjustment/live-filter riding the AdCh list is clipped TO the
      // owner, and its plane is the ADJUSTMENT's mask, never the owner's
      // (esdreika attaches Brightness/Contrast + HSL to a metal texture this
      // way; reading their all-opaque planes as owner masks dropped the
      // adjustments and raised a bogus duplicate-mask notice).
      // emit_adjunct_adjustments imports them after the owner layer.
      continue;
    }
    const af::AfClass* dybm = adjunct->child_class(af::tag4("Bitm"));
    if (dybm == nullptr) {
      // No raster plane: the adjunct may be a vector layer subtree (a Grup of
      // shapes, a PCrv, a ShpN, or a baked Comp) acting as the owner's vector
      // mask (the steam-logo wild construct). Same owner-space rule as raster
      // masks: the adjunct is a CHILD of its owner, so the owner's transform
      // composes before the subtree's own.
      auto base = ctx.transform;
      if (const auto owner = node.vec_field(af::tag4("Xfrm")); owner.size() == 6) {
        std::array<double, 6> own{};
        std::copy(owner.begin(), owner.end(), own.begin());
        base = compose_transforms(ctx.transform, own);
      }
      VectorPath path;
      std::int32_t group = 0;
      int contributors = 0;
      int unsupported = 0;
      append_vector_mask_paths(*adjunct, base, 0, path, group, contributors, unsupported);
      if (contributors == 0 && unsupported == 0) {
        continue;  // not vector geometry (an adjunct kind Patchy does not know)
      }
      if (unsupported > 0 || path.subpaths.empty()) {
        // A partially-decoded mask would clip wrongly; drop the whole mask
        // honestly instead.
        ctx.notices.push_back("Layer '" + name +
                              "': a vector mask could not be imported and was dropped");
        continue;
      }
      if (vector_applied) {
        ctx.notices.push_back("Layer '" + name +
                              "': has more than one vector mask; only the first was imported");
        continue;
      }
      LayerVectorMask mask;
      mask.path = std::move(path);
      layer.set_vector_mask(std::move(mask));
      update_vector_mask_raster(layer, Rect{0, 0, ctx.document.width(), ctx.document.height()});
      vector_applied = true;
      continue;
    }
    auto decoded = decode_bitmap(ctx.bytes, ctx.container, *dybm, name, &ctx.notices, nullptr);
    if (!decoded || decoded->mask.empty()) {
      ctx.notices.push_back("Layer '" + name + "': a mask could not be decoded and was dropped");
      continue;
    }
    if (applied) {
      ctx.notices.push_back("Layer '" + name +
                            "': has more than one mask; only the first was imported");
      break;
    }
    // The adjunct is a CHILD of its owner node, so its plane lives in the
    // owner's LOCAL space (the scene-graph rule): the owner's transform
    // composes before the adjunct's own. 2.x-generation masks store no
    // adjunct Xfrm at all (the plane anchors at the owner's origin - the
    // wild phone mockup's screen mask), while 3.x masks carry an
    // inverse-of-owner Xfrm that lands the plane in spread space
    // (restaurant-menu's image masks pin the pairing).
    const auto saved_transform = ctx.transform;
    if (const auto owner = node.vec_field(af::tag4("Xfrm")); owner.size() == 6) {
      std::array<double, 6> own{};
      std::copy(owner.begin(), owner.end(), own.begin());
      ctx.transform = compose_transforms(ctx.transform, own);
    }
    const auto mask_xfrm = node_xfrm(ctx, *adjunct);
    ctx.transform = saved_transform;
    const auto origin = layer_origin(mask_xfrm);
    std::int32_t mask_x = 0;
    std::int32_t mask_y = 0;
    if (origin) {
      mask_x = origin->first;
      mask_y = origin->second;
    } else {
      // Scale/rotate transform: rasterize the mask plane through the affine.
      auto placed = resample_affine(decoded->mask, mask_xfrm, true);
      if (placed) {
        decoded->mask = std::move(placed->pixels);
        mask_x = placed->x;
        mask_y = placed->y;
      } else {
        ctx.notices.push_back("Layer '" + name +
                              "': mask has a degenerate transform; its position is approximate");
        if (mask_xfrm.size() == 6) {
          mask_x = static_cast<std::int32_t>(std::lround(mask_xfrm[2]));
          mask_y = static_cast<std::int32_t>(std::lround(mask_xfrm[5]));
        }
      }
    }
    LayerMask mask;
    mask.bounds = Rect{mask_x, mask_y, decoded->mask.width(), decoded->mask.height()};
    mask.pixels = std::move(decoded->mask);
    mask.default_color = 255;  // beyond the stored plane the layer stays visible
    layer.set_mask(std::move(mask));
    applied = true;
  }
}

void apply_common(LayerBuildContext& ctx, const af::AfClass& node, Layer& layer,
                  const std::string& name) {
  layer.set_visible(node.bool_field(af::tag4("Visi"), true));
  layer.set_opacity(static_cast<float>(std::clamp(node.double_field(af::tag4("Opac"), 1.0), 0.0, 1.0)));
  layer.set_fill_opacity(
      static_cast<float>(std::clamp(node.double_field(af::tag4("FOpc"), 1.0), 0.0, 1.0)));
  const auto* blnd = node.field(af::tag4("Blnd"));
  if (blnd != nullptr) {
    if (const auto* e = std::get_if<af::AfEnum>(&blnd->value)) {
      const auto mapped = map_blend_mode(*e);
      if (mapped) {
        layer.set_blend_mode(*mapped);
      } else if (const auto approx = approximate_blend_mode(*e)) {
        layer.set_blend_mode(approx->mode);
        layer.set_opacity(layer.opacity() * approx->opacity_scale);
        ctx.notices.push_back("Layer '" + name + "': Affinity blend mode '" +
                              approx->affinity_name + "' approximated as " + approx->patchy_name);
      } else if (is_erase_blend(*e)) {
        // Carrier keeps Normal here; fold_erase_layers rewrites the sibling
        // list once the rasterized alpha exists and owns every Erase notice.
      } else {
        ctx.notices.push_back("Layer '" + name +
                              "': blend mode not supported by Patchy; shown as Normal");
      }
    }
  }
  apply_layer_effects(ctx, node, layer, name);
}

[[nodiscard]] std::int64_t enum_field_id(const af::AfClass& cls, std::uint32_t tag,
                                         std::int64_t fallback) {
  const auto* f = cls.field(tag);
  if (f != nullptr) {
    if (const auto* e = std::get_if<af::AfEnum>(&f->value)) {
      return e->id;
    }
  }
  return fallback;
}

// GrFl -> FDsc -> FDeF (FilG/Fill) -> Grad: Posn = [position, midpoint] float2
// pairs and Cols = the RGBA stop colors. FilG Type (GradientFillType): 0
// linear, 1 elliptical, 2 radial, 3 conical. The BASE gradient runs along +x
// (left -> right; pinned by the default-overlay render), and the descriptor's
// optional FDeX [a,b,tx,c,d,ty] transform rotates/scales it: the direction
// vector is (a, c) in screen (y-down) space, so the PS light-convention angle
// is atan2(-c, a); hypot(a, c) is the span scale.
[[nodiscard]] std::optional<LayerStyleGradient> style_gradient_from_fill(const af::AfClass* descriptor) {
  if (descriptor == nullptr) {
    return std::nullopt;
  }
  const af::AfClass* fill = descriptor->child_class(af::tag4("FDeF"));
  if (fill == nullptr) {
    return std::nullopt;
  }
  const af::AfClass* gradient = fill->child_class(af::tag4("Grad"));
  if (gradient == nullptr) {
    return std::nullopt;
  }
  const auto positions = gradient->vec_field(af::tag4("Posn"));
  const auto* colors = class_list(*gradient, af::tag4("Cols"));
  if (colors == nullptr || positions.size() != colors->size() * 2 || colors->size() < 2) {
    return std::nullopt;
  }
  LayerStyleGradient result;
  switch (enum_field_id(*fill, af::tag4("Type"), 0)) {
    case 1:
    case 2: result.type = LayerStyleGradientType::Radial; break;
    case 3: result.type = LayerStyleGradientType::Angle; break;
    default: result.type = LayerStyleGradientType::Linear; break;
  }
  result.angle_degrees = 0.0F;  // the wire base direction is left -> right
  if (const auto xfrm = descriptor->vec_field(af::tag4("FDeX")); xfrm.size() == 6) {
    const double a = xfrm[0];
    const double c = xfrm[3];
    const double span = std::hypot(a, c);
    if (span > 0.01) {
      result.angle_degrees = static_cast<float>(degrees_from_radians(std::atan2(-c, a)));
      result.scale = static_cast<float>(std::clamp(span, 0.1, 1.5));
    }
  }
  result.color_stops.reserve(colors->size());
  result.alpha_stops.reserve(colors->size());
  for (std::size_t i = 0; i < colors->size(); ++i) {
    const auto stored = read_rgba_color((*colors)[i].get());
    if (!stored.has_value()) {
      return std::nullopt;
    }
    const auto to_channel = [](float value) {
      return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    const auto location = static_cast<float>(std::clamp(positions[i * 2], 0.0, 1.0));
    // The wire midpoint rides with the DEPARTING stop; Patchy stores it on the
    // destination stop, so stop i+1 takes stop i's midpoint.
    const auto midpoint =
        i == 0 ? 0.5F : static_cast<float>(std::clamp(positions[(i - 1) * 2 + 1], 0.05, 0.95));
    result.color_stops.push_back(GradientColorStop{
        location,
        RgbColor{to_channel((*stored)[0]), to_channel((*stored)[1]), to_channel((*stored)[2])},
        midpoint});
    result.alpha_stops.push_back(GradientAlphaStop{location, std::clamp((*stored)[3], 0.0F, 1.0F), midpoint});
  }
  return result;
}

// Read an Affinity color class: its `_col` field is a sized struct of
// little-endian float32 components in 0..1. RGBA classes carry four
// sRGB-encoded channels directly (like the UI); HSLA classes (older-
// generation gradient stops, 2026-07-28 wild sweep) store hue in turns plus
// saturation/lightness and convert through the standard HSL model; CMYK
// classes (five floats c,m,y,k,a - embedded icon docs inside CMYK parents)
// convert with the same naive profile-less (1-ink)(1-k) mix the CMYK raster
// decoder uses. Reading a CMYK payload's first four floats as RGBA painted
// the restaurant-menu icons cyan-green once their embeds started importing.
// Other color classes (Gray, LABA) stay unmapped for now.
[[nodiscard]] std::optional<std::array<float, 4>> read_rgba_color(const af::AfClass* color_class) {
  if (color_class == nullptr) {
    return std::nullopt;
  }
  const auto* field = color_class->field(af::tag4("_col"));
  if (field == nullptr) {
    return std::nullopt;
  }
  const auto* data = std::get_if<std::vector<std::uint8_t>>(&field->value);
  if (data == nullptr || data->size() < 16) {
    return std::nullopt;
  }
  const auto read_component = [&data](int i) {
    const std::uint32_t bits =
        static_cast<std::uint32_t>((*data)[static_cast<std::size_t>(i) * 4U]) |
        (static_cast<std::uint32_t>((*data)[static_cast<std::size_t>(i) * 4U + 1]) << 8U) |
        (static_cast<std::uint32_t>((*data)[static_cast<std::size_t>(i) * 4U + 2]) << 16U) |
        (static_cast<std::uint32_t>((*data)[static_cast<std::size_t>(i) * 4U + 3]) << 24U);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    if (!(value >= 0.0F)) {
      value = 0.0F;  // also catches NaN
    }
    return std::min(value, 1.0F);
  };
  std::array<float, 4> color{};
  for (int i = 0; i < 4; ++i) {
    color[i] = read_component(i);
  }
  if (color_class->type_tag == af::tag4("CMYK") && data->size() >= 20) {
    const float k = color[3];
    const std::array<float, 4> converted{(1.0F - color[0]) * (1.0F - k),
                                         (1.0F - color[1]) * (1.0F - k),
                                         (1.0F - color[2]) * (1.0F - k), read_component(4)};
    return converted;
  }
  if (color_class->type_tag == af::tag4("HSLA")) {
    const float hue = color[0] - std::floor(color[0]);  // turns, wrapped
    const float saturation = color[1];
    const float lightness = color[2];
    const float chroma = (1.0F - std::abs(2.0F * lightness - 1.0F)) * saturation;
    const float segment = hue * 6.0F;
    const float x = chroma * (1.0F - std::abs(std::fmod(segment, 2.0F) - 1.0F));
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    switch (static_cast<int>(segment)) {
      case 0: r = chroma; g = x; break;
      case 1: r = x; g = chroma; break;
      case 2: g = chroma; b = x; break;
      case 3: g = x; b = chroma; break;
      case 4: r = x; b = chroma; break;
      default: r = chroma; b = x; break;
    }
    const float m = lightness - chroma / 2.0F;
    color[0] = std::clamp(r + m, 0.0F, 1.0F);
    color[1] = std::clamp(g + m, 0.0F, 1.0F);
    color[2] = std::clamp(b + m, 0.0F, 1.0F);
  }
  return color;
}

}  // namespace detail

}  // namespace patchy::af
