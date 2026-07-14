// Layer-style codecs for the PSD reader/writer: the lfx2/lrFX effect-descriptor
// parsers (layer_style_from_lefx_descriptor is shared with the .asl codec), the
// per-effect descriptor writers behind photoshop_lfx2_layer_style_payload, and
// the private Patchy plFX block. Split out of psd_document_io.cpp as a pure move.

#include "psd/psd_document_io.hpp"
#include "psd/psd_io_internal.hpp"

#include "color/color_management.hpp"
#include "core/adjustment_layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/pattern_resource.hpp"
#include "core/smart_object.hpp"
#include "core/style_contour.hpp"
#include "core/text_warp.hpp"
#include "formats/acv_curves_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_descriptor.hpp"
#include "psd/psd_filter_effects.hpp"
#include "psd/psd_layer_effects.hpp"
#include "psd/psd_patterns.hpp"
#include "psd/psd_smart_objects.hpp"
#include "render/compositor.hpp"
#include "support/string_utils.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#endif

namespace patchy::psd {

namespace {

std::string descriptor_enum(const DescriptorObject& object, std::string_view key, std::string fallback = {}) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr || value->type != DescriptorValue::Type::Enum) {
    return fallback;
  }
  return value->enum_value;
}

float percent_to_unit(double value) {
  if (!std::isfinite(value)) {
    return 1.0F;
  }
  return std::clamp(static_cast<float>(value / 100.0), 0.0F, 1.0F);
}

RgbColor descriptor_rgb_color(const DescriptorObject& object, std::string_view key,
                              const CmykColorConverter& cmyk, RgbColor fallback = {}) {
  const auto* color_object = descriptor_object(object, key);
  if (color_object == nullptr) {
    return fallback;
  }
  if (color_object->class_id == "CMYC") {
    // CMYK-mode documents store descriptor colors as ink percentages.
    const auto ink = [&](std::string_view component_key) {
      return descriptor_number(*color_object, component_key) / 100.0;
    };
    return cmyk.rgb_from_ink(ink("Cyn "), ink("Mgnt"), ink("Ylw "), ink("Blck"));
  }
  return RgbColor{static_cast<std::uint8_t>(std::clamp(std::lround(descriptor_number(*color_object, "Rd  ")), 0L, 255L)),
                  static_cast<std::uint8_t>(
                      std::clamp(std::lround(descriptor_number(*color_object, "Grn ")), 0L, 255L)),
                  static_cast<std::uint8_t>(
                      std::clamp(std::lround(descriptor_number(*color_object, "Bl  ")), 0L, 255L))};
}

LayerStyleGradientType gradient_type_from_descriptor(std::string_view value) {
  if (value == "Rdl ") {
    return LayerStyleGradientType::Radial;
  }
  if (value == "Angl") {
    return LayerStyleGradientType::Angle;
  }
  if (value == "Rflc") {
    return LayerStyleGradientType::Reflected;
  }
  if (value == "Dmnd") {
    return LayerStyleGradientType::Diamond;
  }
  return LayerStyleGradientType::Linear;
}

GradientInterpolationMethod gradient_interpolation_from_descriptor(std::string_view value) {
  if (value == "perceptual" || value == "Smoo") {
    return GradientInterpolationMethod::Perceptual;
  }
  if (value == "linear") {
    return GradientInterpolationMethod::Linear;
  }
  return GradientInterpolationMethod::Classic;
}

GradientNoiseColorModel gradient_noise_color_model_from_descriptor(std::string_view value) {
  if (value == "HSBl" || value == "HSB " || value == "HSBC") {
    return GradientNoiseColorModel::HSB;
  }
  if (value == "LbCl" || value == "Lab " || value == "LABC") {
    return GradientNoiseColorModel::Lab;
  }
  return GradientNoiseColorModel::RGB;
}

std::array<std::uint16_t, 4> gradient_noise_range(const DescriptorObject& object, std::string_view key,
                                                  std::uint16_t fallback) {
  std::array<std::uint16_t, 4> result{fallback, fallback, fallback, fallback};
  const auto* value = descriptor_value(object, key);
  if (value == nullptr || value->type != DescriptorValue::Type::List) {
    return result;
  }
  for (std::size_t index = 0; index < result.size() && index < value->list_value.size(); ++index) {
    const auto& item = value->list_value[index];
    if (item.type == DescriptorValue::Type::Integer) {
      result[index] = static_cast<std::uint16_t>(std::clamp(item.integer_value, 0, 100));
    }
  }
  return result;
}

LayerStyleGradient parse_gradient(const DescriptorObject& effect, const CmykColorConverter& cmyk) {
  LayerStyleGradient gradient;
  if (const auto* gradient_object = descriptor_object(effect, "Grad"); gradient_object != nullptr) {
    if (const auto* name = descriptor_value(*gradient_object, "Nm  ");
        name != nullptr && name->type == DescriptorValue::Type::String) {
      gradient.name = name->string_value;
    }
    const auto form = descriptor_enum(*gradient_object, "GrdF", "CstS");
    gradient.form = form == "ClNs" ? GradientDefinitionForm::Noise
                                   : GradientDefinitionForm::Solid;
    gradient.smoothness = static_cast<std::uint16_t>(
        std::clamp(static_cast<int>(std::lround(
                       descriptor_number(*gradient_object, "Intr", 4096.0))),
                   0, 4096));
    if (gradient.form == GradientDefinitionForm::Noise) {
      gradient.noise.add_transparency =
          descriptor_bool(*gradient_object, "ShTr", false);
      gradient.noise.restrict_colors =
          descriptor_bool(*gradient_object, "VctC", true);
      gradient.noise.color_model = gradient_noise_color_model_from_descriptor(
          descriptor_enum(*gradient_object, "ClrS", "RGBC"));
      gradient.noise.seed = static_cast<std::uint32_t>(
          std::max(0.0, descriptor_number(*gradient_object, "RndS", 0.0)));
      gradient.noise.roughness = static_cast<std::uint16_t>(
          std::clamp(static_cast<int>(std::lround(
                         descriptor_number(*gradient_object, "Smth", 2048.0))),
                     0, 4096));
      gradient.noise.minimum =
          gradient_noise_range(*gradient_object, "Mnm ", 0);
      gradient.noise.maximum =
          gradient_noise_range(*gradient_object, "Mxm ", 100);
    }
    if (const auto* colors = descriptor_value(*gradient_object, "Clrs");
        colors != nullptr && colors->type == DescriptorValue::Type::List) {
      for (const auto& item : colors->list_value) {
        if (item.type != DescriptorValue::Type::Object || item.object_value == nullptr) {
          continue;
        }
        const auto& stop = *item.object_value;
        auto kind = GradientColorStop::Kind::User;
        const auto type = descriptor_enum(stop, "Type", "UsrS");
        if (type == "FrgC") {
          kind = GradientColorStop::Kind::Foreground;
        } else if (type == "BckC") {
          kind = GradientColorStop::Kind::Background;
        }
        gradient.color_stops.push_back(
            GradientColorStop{std::clamp(static_cast<float>(descriptor_number(stop, "Lctn") / 4096.0), 0.0F, 1.0F),
                              descriptor_rgb_color(stop, "Clr ", cmyk,
                                 kind == GradientColorStop::Kind::Background
                                     ? RgbColor{255, 255, 255}
                                     : RgbColor{0, 0, 0}),
                              std::clamp(static_cast<float>(descriptor_number(stop, "Mdpn", 50.0) / 100.0),
                                         0.0F, 1.0F),
            kind});
      }
    }
    if (const auto* transparency = descriptor_value(*gradient_object, "Trns");
        transparency != nullptr && transparency->type == DescriptorValue::Type::List) {
      for (const auto& item : transparency->list_value) {
        if (item.type != DescriptorValue::Type::Object || item.object_value == nullptr) {
          continue;
        }
        const auto& stop = *item.object_value;
        gradient.alpha_stops.push_back(
            GradientAlphaStop{std::clamp(static_cast<float>(descriptor_number(stop, "Lctn") / 4096.0), 0.0F, 1.0F),
                              percent_to_unit(descriptor_number(stop, "Opct", 100.0)),
                              std::clamp(static_cast<float>(descriptor_number(stop, "Mdpn", 50.0) / 100.0),
                                         0.0F, 1.0F)});
      }
    }
  }
  gradient.angle_degrees = static_cast<float>(descriptor_number(effect, "Angl", 90.0));
  gradient.scale = std::max(0.01F, static_cast<float>(descriptor_number(effect, "Scl ", 100.0) / 100.0));
  gradient.reverse = descriptor_bool(effect, "Rvrs", false);
  gradient.dither = descriptor_bool(effect, "Dthr", false);
  gradient.interpolation =
      gradient_interpolation_from_descriptor(descriptor_enum(
          effect, "gs99",
          descriptor_enum(effect, "gradientsInterpolationMethod", "Gcls")));
  gradient.align_with_layer = descriptor_bool(effect, "Algn", true);
  if (const auto* offset = descriptor_object(effect, "Ofst"); offset != nullptr) {
    gradient.offset_x_percent =
        static_cast<float>(descriptor_number(*offset, "Hrzn", 0.0));
    gradient.offset_y_percent =
        static_cast<float>(descriptor_number(*offset, "Vrtc", 0.0));
  }
  gradient.type = gradient_type_from_descriptor(descriptor_enum(effect, "Type", "Lnr "));
  std::stable_sort(
      gradient.color_stops.begin(), gradient.color_stops.end(),
      [](const GradientColorStop& lhs, const GradientColorStop& rhs) { return lhs.location < rhs.location; });
  std::stable_sort(
      gradient.alpha_stops.begin(), gradient.alpha_stops.end(),
      [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) { return lhs.location < rhs.location; });
  return gradient;
}

std::optional<LayerDropShadow> parse_drop_shadow(const DescriptorObject& effect,
                                                 const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "mul "),
                                                      std::array<char, 4>{'m', 'u', 'l', ' '});
  shadow.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{0, 0, 0});
  shadow.opacity = percent_to_unit(descriptor_number(effect, "Opct", 75.0));
  shadow.angle_degrees = static_cast<float>(descriptor_number(effect, "lagl", 120.0));
  shadow.use_global_light = descriptor_bool(effect, "uglg", false);
  shadow.distance = std::max(0.0F, static_cast<float>(descriptor_number(effect, "Dstn", 5.0)));
  shadow.spread = std::clamp(static_cast<float>(descriptor_number(effect, "Ckmt", 0.0)), 0.0F, 100.0F);
  shadow.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  return shadow;
}

std::optional<LayerInnerShadow> parse_inner_shadow(const DescriptorObject& effect,
                                                   const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerInnerShadow shadow;
  shadow.enabled = true;
  shadow.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "mul "),
                                                      std::array<char, 4>{'m', 'u', 'l', ' '});
  shadow.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{0, 0, 0});
  shadow.opacity = percent_to_unit(descriptor_number(effect, "Opct", 75.0));
  shadow.angle_degrees = static_cast<float>(descriptor_number(effect, "lagl", 120.0));
  shadow.use_global_light = descriptor_bool(effect, "uglg", false);
  shadow.distance = std::max(0.0F, static_cast<float>(descriptor_number(effect, "Dstn", 5.0)));
  shadow.choke = std::clamp(static_cast<float>(descriptor_number(effect, "Ckmt", 0.0)), 0.0F, 100.0F);
  shadow.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  return shadow;
}

std::optional<LayerOuterGlow> parse_outer_glow(const DescriptorObject& effect,
                                               const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "scrn"),
                                                      std::array<char, 4>{'s', 'c', 'r', 'n'});
  glow.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{255, 255, 190});
  glow.opacity = percent_to_unit(descriptor_number(effect, "Opct", 75.0));
  glow.spread = std::clamp(static_cast<float>(descriptor_number(effect, "Ckmt", 0.0)), 0.0F, 100.0F);
  glow.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  return glow;
}

LayerInnerGlowSource inner_glow_source_from_descriptor(std::string_view value) {
  return value == "SrcC" ? LayerInnerGlowSource::Center : LayerInnerGlowSource::Edge;
}

std::optional<LayerInnerGlow> parse_inner_glow(const DescriptorObject& effect,
                                               const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "scrn"),
                                                      std::array<char, 4>{'s', 'c', 'r', 'n'});
  glow.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{255, 255, 190});
  glow.opacity = percent_to_unit(descriptor_number(effect, "Opct", 75.0));
  glow.choke = std::clamp(static_cast<float>(descriptor_number(effect, "Ckmt", 0.0)), 0.0F, 100.0F);
  glow.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  glow.source = inner_glow_source_from_descriptor(descriptor_enum(effect, "glwS", "SrcE"));
  return glow;
}

std::optional<LayerColorOverlay> parse_color_overlay(const DescriptorObject& effect,
                                                     const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "norm"),
                                                      std::array<char, 4>{'n', 'o', 'r', 'm'});
  overlay.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{255, 0, 0});
  overlay.opacity = percent_to_unit(descriptor_number(effect, "Opct", 100.0));
  return overlay;
}

std::string descriptor_string(const DescriptorObject& object, std::string_view key, std::string fallback = {}) {
  const auto* value = descriptor_value(object, key);
  if (value == nullptr || value->type != DescriptorValue::Type::String) {
    return fallback;
  }
  return value->string_value;
}

// Reads a ShpC contour object (Nm + Crv list of CrPt{Hrzn, Vrtc[, Cnty]}) into
// the exact-point StyleContour model. Cnty=true means smooth; an absent Cnty is
// smooth too (Photoshop omits the flag on default Linear curves).
StyleContour parse_shape_contour(const DescriptorObject& shape) {
  StyleContour contour;
  contour.name = descriptor_string(shape, "Nm  ", contour.name);
  const auto* curve = descriptor_value(shape, "Crv ");
  if (curve == nullptr || curve->type != DescriptorValue::Type::List) {
    contour.points.clear();
    return contour;
  }
  for (const auto& item : curve->list_value) {
    if (item.type != DescriptorValue::Type::Object || item.object_value == nullptr) {
      continue;
    }
    StyleContourPoint point;
    point.x = static_cast<float>(descriptor_number(*item.object_value, "Hrzn"));
    point.y = static_cast<float>(descriptor_number(*item.object_value, "Vrtc"));
    point.corner = !descriptor_bool(*item.object_value, "Cnty", true);
    contour.points.push_back(point);
  }
  return contour;
}

std::optional<LayerBevelEmboss> parse_bevel_emboss(const DescriptorObject& effect,
                                                   const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode =
      blend_mode_from_descriptor_enum(descriptor_enum(effect, "hglM", "scrn"),
                                      std::array<char, 4>{'s', 'c', 'r', 'n'});
  bevel.highlight_color = descriptor_rgb_color(effect, "hglC", cmyk, RgbColor{255, 255, 255});
  bevel.highlight_opacity = percent_to_unit(descriptor_number(effect, "hglO", 75.0));
  bevel.shadow_blend_mode =
      blend_mode_from_descriptor_enum(descriptor_enum(effect, "sdwM", "mul "),
                                      std::array<char, 4>{'m', 'u', 'l', ' '});
  bevel.shadow_color = descriptor_rgb_color(effect, "sdwC", cmyk, RgbColor{0, 0, 0});
  bevel.shadow_opacity = percent_to_unit(descriptor_number(effect, "sdwO", 75.0));
  bevel.angle_degrees = static_cast<float>(descriptor_number(effect, "lagl", 120.0));
  bevel.use_global_light = descriptor_bool(effect, "uglg", false);
  bevel.altitude_degrees = static_cast<float>(descriptor_number(effect, "Lald", 30.0));
  bevel.depth = std::max(0.01F, static_cast<float>(descriptor_number(effect, "srgR", 100.0) / 100.0));
  bevel.size = std::max(1.0F, static_cast<float>(descriptor_number(effect, "blur", 5.0)));
  bevel.direction_up = descriptor_enum(effect, "bvlD", "In  ") != "Out ";
  // Style/Technique/Soften round-trip losslessly and drive the compositor.
  const auto style_value = descriptor_enum(effect, "bvlS", "InrB");
  if (style_value == "OtrB") {
    bevel.style = BevelEmbossStyleKind::OuterBevel;
  } else if (style_value == "Embs") {
    bevel.style = BevelEmbossStyleKind::Emboss;
  } else if (style_value == "PlEb") {
    bevel.style = BevelEmbossStyleKind::PillowEmboss;
  } else if (style_value == "strokeEmboss") {
    bevel.style = BevelEmbossStyleKind::StrokeEmboss;
  } else {
    bevel.style = BevelEmbossStyleKind::InnerBevel;
  }
  const auto technique_value = descriptor_enum(effect, "bvlT", "SfBL");
  if (technique_value == "PrBL") {
    bevel.technique = BevelTechnique::ChiselHard;
  } else if (technique_value == "Slmt") {
    bevel.technique = BevelTechnique::ChiselSoft;
  } else {
    bevel.technique = BevelTechnique::Smooth;
  }
  bevel.soften = std::max(0.0F, static_cast<float>(descriptor_number(effect, "Sftn", 0.0)));
  if (const auto* gloss = descriptor_object(effect, "TrnS"); gloss != nullptr) {
    bevel.gloss_contour = parse_shape_contour(*gloss);
  }
  bevel.gloss_anti_aliased = descriptor_bool(effect, "antialiasGloss", false);
  bevel.contour.enabled = descriptor_bool(effect, "useShape", false);
  if (const auto* shape = descriptor_object(effect, "MpgS"); shape != nullptr) {
    bevel.contour.contour = parse_shape_contour(*shape);
  }
  bevel.contour.anti_aliased = descriptor_bool(effect, "AntA", false);
  bevel.contour.range =
      std::clamp(static_cast<float>(descriptor_number(effect, "Inpr", 50.0) / 100.0), 0.0F, 1.0F);
  bevel.texture.enabled = descriptor_bool(effect, "useTexture", false);
  bevel.texture.invert = descriptor_bool(effect, "InvT", false);
  bevel.texture.link_with_layer = descriptor_bool(effect, "Algn", true);
  bevel.texture.scale =
      std::max(0.01F, static_cast<float>(descriptor_number(effect, "Scl ", 100.0) / 100.0));
  bevel.texture.depth =
      std::clamp(static_cast<float>(descriptor_number(effect, "textureDepth", 100.0) / 100.0), -10.0F, 10.0F);
  if (const auto* pattern_object = descriptor_object(effect, "Ptrn"); pattern_object != nullptr) {
    bevel.texture.pattern_name = descriptor_string(*pattern_object, "Nm  ");
    bevel.texture.pattern_id = descriptor_string(*pattern_object, "Idnt");
  }
  if (const auto* phase = descriptor_object(effect, "phase"); phase != nullptr) {
    bevel.texture.phase_x = static_cast<float>(descriptor_number(*phase, "Hrzn", 0.0));
    bevel.texture.phase_y = static_cast<float>(descriptor_number(*phase, "Vrtc", 0.0));
  }
  return bevel;
}

std::optional<LayerGradientFill> parse_gradient_fill(const DescriptorObject& effect,
                                                     const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "norm"),
                                                      std::array<char, 4>{'n', 'o', 'r', 'm'});
  fill.opacity = percent_to_unit(descriptor_number(effect, "Opct", 100.0));
  fill.gradient = parse_gradient(effect, cmyk);
  return fill;
}

std::optional<LayerSatin> parse_satin(const DescriptorObject& effect,
                                      const CmykColorConverter& cmyk) {
  LayerSatin satin;
  // Disabled Satin records remain editable Photoshop style entries and may
  // carry contour options that must be warned about before lfx2 regeneration.
  satin.enabled = descriptor_bool(effect, "enab", false);
  satin.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "mul "),
                                                      std::array<char, 4>{'m', 'u', 'l', ' '});
  satin.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{0, 0, 0});
  satin.opacity = percent_to_unit(descriptor_number(effect, "Opct", 50.0));
  satin.angle_degrees = static_cast<float>(descriptor_number(effect, "lagl", 19.0));
  satin.distance = std::max(0.0F, static_cast<float>(descriptor_number(effect, "Dstn", 11.0)));
  satin.size = std::max(0.0F, static_cast<float>(descriptor_number(effect, "blur", 14.0)));
  satin.invert = descriptor_bool(effect, "Invr", true);
  satin.unsupported_contour_options = descriptor_bool(effect, "AntA", false);
  if (const auto* contour = descriptor_object(effect, "MpgS"); contour != nullptr) {
    const auto parsed = parse_shape_contour(*contour);
    // A missing/malformed Crv list (empty points) is unsupported too, matching
    // the historical check; corner flags never affect a two-point identity.
    satin.unsupported_contour_options = satin.unsupported_contour_options || parsed.points.empty() ||
                                        !style_contour_is_linear(parsed);
  }
  return satin;
}

std::optional<LayerPatternOverlay> parse_pattern_overlay(const DescriptorObject& effect) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerPatternOverlay pattern;
  pattern.enabled = true;
  pattern.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "norm"),
                                                      std::array<char, 4>{'n', 'o', 'r', 'm'});
  pattern.opacity = percent_to_unit(descriptor_number(effect, "Opct", 100.0));
  pattern.scale = std::max(0.01F, static_cast<float>(descriptor_number(effect, "Scl ", 100.0) / 100.0));
  if (const auto* pattern_object = descriptor_object(effect, "Ptrn"); pattern_object != nullptr) {
    pattern.pattern_name = descriptor_string(*pattern_object, "Nm  ");
    pattern.pattern_id = descriptor_string(*pattern_object, "Idnt");
  }
  pattern.angle_degrees = static_cast<float>(descriptor_number(effect, "Angl", 0.0));
  pattern.link_with_layer = descriptor_bool(effect, "Algn", true);
  if (const auto* phase = descriptor_object(effect, "phase"); phase != nullptr) {
    pattern.phase_x = static_cast<float>(descriptor_number(*phase, "Hrzn", 0.0));
    pattern.phase_y = static_cast<float>(descriptor_number(*phase, "Vrtc", 0.0));
  }
  return pattern;
}

LayerStrokePosition stroke_position_from_descriptor(std::string_view value) {
  if (value == "InsF") {
    return LayerStrokePosition::Inside;
  }
  if (value == "CtrF") {
    return LayerStrokePosition::Center;
  }
  return LayerStrokePosition::Outside;
}

std::optional<LayerStroke> parse_stroke(const DescriptorObject& effect,
                                        const CmykColorConverter& cmyk) {
  if (!descriptor_bool(effect, "enab", false)) {
    return std::nullopt;
  }
  LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = blend_mode_from_descriptor_enum(descriptor_enum(effect, "Md  ", "norm"),
                                                      std::array<char, 4>{'n', 'o', 'r', 'm'});
  stroke.opacity = percent_to_unit(descriptor_number(effect, "Opct", 100.0));
  stroke.size = std::max(1.0F, static_cast<float>(descriptor_number(effect, "Sz  ", 3.0)));
  stroke.position = stroke_position_from_descriptor(descriptor_enum(effect, "Styl", "OutF"));
  stroke.color = descriptor_rgb_color(effect, "Clr ", cmyk, RgbColor{0, 0, 0});
  stroke.uses_gradient = descriptor_enum(effect, "PntT", "SClr") == "GrFl";
  if (stroke.uses_gradient) {
    stroke.gradient = parse_gradient(effect, cmyk);
  }
  return stroke;
}

}  // namespace

// Exposed for the .asl style-preset codec (psd/psd_layer_effects.hpp): converts an
// already-parsed effects descriptor (the lfx2 root, identical to an .asl 'Lefx'
// object) into the modeled LayerStyle. Exception semantics match the historical
// lfx2 parse: any descriptor surprise yields an empty style.
LayerStyle layer_style_from_lefx_descriptor(const DescriptorObject& root,
                                            const CmykToRgbTransform* cmyk_icc) {
  const CmykColorConverter cmyk{cmyk_icc};
  LayerStyle style;
  try {
    style.effects_visible = descriptor_bool(root, "masterFXSwitch", true);
    if (const auto* effect = descriptor_object(root, "DrSh"); effect != nullptr) {
      if (const auto shadow = parse_drop_shadow(*effect, cmyk); shadow.has_value()) {
        style.drop_shadows.push_back(*shadow);
      }
    }
    if (const auto* effect = descriptor_object(root, "IrSh"); effect != nullptr) {
      if (const auto shadow = parse_inner_shadow(*effect, cmyk); shadow.has_value()) {
        style.inner_shadows.push_back(*shadow);
      }
    }
    if (const auto* effect = descriptor_object(root, "innerShadow"); effect != nullptr) {
      if (const auto shadow = parse_inner_shadow(*effect, cmyk); shadow.has_value()) {
        style.inner_shadows.push_back(*shadow);
      }
    }
    if (const auto* effect = descriptor_object(root, "OrGl"); effect != nullptr) {
      if (const auto glow = parse_outer_glow(*effect, cmyk); glow.has_value()) {
        style.outer_glows.push_back(*glow);
      }
    }
    if (const auto* effect = descriptor_object(root, "outerGlow"); effect != nullptr) {
      if (const auto glow = parse_outer_glow(*effect, cmyk); glow.has_value()) {
        style.outer_glows.push_back(*glow);
      }
    }
    if (const auto* effect = descriptor_object(root, "IrGl"); effect != nullptr) {
      if (const auto glow = parse_inner_glow(*effect, cmyk); glow.has_value()) {
        style.inner_glows.push_back(*glow);
      }
    }
    if (const auto* effect = descriptor_object(root, "innerGlow"); effect != nullptr) {
      if (const auto glow = parse_inner_glow(*effect, cmyk); glow.has_value()) {
        style.inner_glows.push_back(*glow);
      }
    }
    if (const auto* effect = descriptor_object(root, "ChFX"); effect != nullptr) {
      if (const auto satin = parse_satin(*effect, cmyk); satin.has_value()) {
        style.satins.push_back(*satin);
      }
    }
    if (const auto* effect = descriptor_object(root, "chromeFX"); effect != nullptr) {
      if (const auto satin = parse_satin(*effect, cmyk); satin.has_value()) {
        style.satins.push_back(*satin);
      }
    }
    if (const auto* effect = descriptor_object(root, "ebbl"); effect != nullptr) {
      if (const auto bevel = parse_bevel_emboss(*effect, cmyk); bevel.has_value()) {
        style.bevels.push_back(*bevel);
      }
    }
    if (const auto* effect = descriptor_object(root, "bevelEmboss"); effect != nullptr) {
      if (const auto bevel = parse_bevel_emboss(*effect, cmyk); bevel.has_value()) {
        style.bevels.push_back(*bevel);
      }
    }
    if (const auto* effect = descriptor_object(root, "GrFl"); effect != nullptr) {
      if (const auto fill = parse_gradient_fill(*effect, cmyk); fill.has_value()) {
        style.gradient_fills.push_back(*fill);
      }
    }
    if (const auto* effect = descriptor_object(root, "patternFill"); effect != nullptr) {
      if (const auto pattern = parse_pattern_overlay(*effect); pattern.has_value()) {
        style.pattern_overlays.push_back(*pattern);
      }
    }
    if (const auto* effect = descriptor_object(root, "SoFi"); effect != nullptr) {
      if (const auto overlay = parse_color_overlay(*effect, cmyk); overlay.has_value()) {
        style.color_overlays.push_back(*overlay);
      }
    }
    if (const auto* effect = descriptor_object(root, "solidFill"); effect != nullptr) {
      if (const auto overlay = parse_color_overlay(*effect, cmyk); overlay.has_value()) {
        style.color_overlays.push_back(*overlay);
      }
    }
    if (const auto* effect = descriptor_object(root, "FrFX"); effect != nullptr) {
      if (const auto stroke = parse_stroke(*effect, cmyk); stroke.has_value()) {
        style.strokes.push_back(*stroke);
      }
    }
    if (const auto* value = descriptor_value(root, "dropShadowMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto shadow = parse_drop_shadow(*item.object_value, cmyk); shadow.has_value()) {
            style.drop_shadows.push_back(*shadow);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "innerShadowMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto shadow = parse_inner_shadow(*item.object_value, cmyk); shadow.has_value()) {
            style.inner_shadows.push_back(*shadow);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "outerGlowMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto glow = parse_outer_glow(*item.object_value, cmyk); glow.has_value()) {
            style.outer_glows.push_back(*glow);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "innerGlowMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto glow = parse_inner_glow(*item.object_value, cmyk); glow.has_value()) {
            style.inner_glows.push_back(*glow);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "chromeFXMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto satin = parse_satin(*item.object_value, cmyk); satin.has_value()) {
            style.satins.push_back(*satin);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "bevelEmbossMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto bevel = parse_bevel_emboss(*item.object_value, cmyk); bevel.has_value()) {
            style.bevels.push_back(*bevel);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "gradientFillMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto fill = parse_gradient_fill(*item.object_value, cmyk); fill.has_value()) {
            style.gradient_fills.push_back(*fill);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "patternFillMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto pattern = parse_pattern_overlay(*item.object_value); pattern.has_value()) {
            style.pattern_overlays.push_back(*pattern);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "solidFillMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto overlay = parse_color_overlay(*item.object_value, cmyk); overlay.has_value()) {
            style.color_overlays.push_back(*overlay);
          }
        }
      }
    }
    if (const auto* value = descriptor_value(root, "frameFXMulti");
        value != nullptr && value->type == DescriptorValue::Type::List) {
      for (const auto& item : value->list_value) {
        if (item.type == DescriptorValue::Type::Object && item.object_value != nullptr) {
          if (const auto stroke = parse_stroke(*item.object_value, cmyk); stroke.has_value()) {
            style.strokes.push_back(*stroke);
          }
        }
      }
    }
  } catch (const std::exception&) {
    return {};
  }
  return style;
}

LayerStyle parse_lfx2_layer_style(std::span<const std::uint8_t> payload,
                                  const CmykColorConverter& cmyk) {
  try {
    BigEndianReader reader(payload);
    (void)reader.read_u32();  // object effects version
    const auto descriptor_version = reader.read_u32();
    if (descriptor_version != 16) {
      return {};
    }
    return layer_style_from_lefx_descriptor(read_descriptor(reader), cmyk.icc);
  } catch (const std::exception&) {
    return {};
  }
}

namespace {

RgbColor read_legacy_effect_color(BigEndianReader& reader) {
  (void)reader.read_u16();
  const auto red = reader.read_u16();
  const auto green = reader.read_u16();
  const auto blue = reader.read_u16();
  (void)reader.read_u16();
  return RgbColor{static_cast<std::uint8_t>(red / 257U), static_cast<std::uint8_t>(green / 257U),
                  static_cast<std::uint8_t>(blue / 257U)};
}

}  // namespace

LayerStyle parse_lrfx_layer_style(std::span<const std::uint8_t> payload) {
  LayerStyle style;
  try {
    BigEndianReader reader(payload);
    (void)reader.read_u16();
    const auto effect_count = reader.read_u16();
    for (std::uint16_t index = 0; index < effect_count && reader.remaining() >= 12; ++index) {
      const auto signature = read_signature(reader);
      const auto key = read_signature(reader);
      if (signature != std::array<char, 4>{'8', 'B', 'I', 'M'} &&
          signature != std::array<char, 4>{'8', 'B', '6', '4'}) {
        return style;
      }
      const auto effect_payload_size = reader.read_u32();
      if (effect_payload_size > reader.remaining()) {
        return style;
      }
      const auto effect_payload = reader.read_bytes(effect_payload_size);
      if (key != std::array<char, 4>{'d', 's', 'd', 'w'}) {
        continue;
      }
      BigEndianReader effect_reader(effect_payload);
      (void)effect_reader.read_u32();
      LayerDropShadow shadow;
      shadow.enabled = true;
      shadow.size = static_cast<float>(effect_reader.read_u32());
      (void)effect_reader.read_u32();
      shadow.angle_degrees = static_cast<float>(effect_reader.read_u32());
      shadow.distance = static_cast<float>(effect_reader.read_u32());
      shadow.color = read_legacy_effect_color(effect_reader);
      (void)read_signature(effect_reader);
      shadow.blend_mode = blend_mode_from_key(read_signature(effect_reader));
      shadow.enabled = effect_reader.read_u8() != 0;
      (void)effect_reader.read_u8();
      shadow.opacity = percent_to_unit(effect_reader.read_u8());
      if (shadow.enabled) {
        style.drop_shadows.push_back(shadow);
      }
    }
  } catch (const std::exception&) {
    return {};
  }
  return style;
}

// Photoshop renders shadow and bevel effects flagged "use global light" with the document-wide
// light direction (image resources 1037/1049) instead of the effect's own stored angle. Resolve
// those angles while importing so Patchy renders what Photoshop renders, then clear the flag:
// Patchy edits angles per effect, so resolved styles are saved as local angles that mean the
// same thing in both applications.
void resolve_global_light(LayerStyle& style, float angle_degrees, float altitude_degrees) {
  for (auto& shadow : style.drop_shadows) {
    if (shadow.use_global_light) {
      shadow.angle_degrees = angle_degrees;
      shadow.use_global_light = false;
    }
  }
  for (auto& shadow : style.inner_shadows) {
    if (shadow.use_global_light) {
      shadow.angle_degrees = angle_degrees;
      shadow.use_global_light = false;
    }
  }
  for (auto& bevel : style.bevels) {
    if (bevel.use_global_light) {
      bevel.angle_degrees = angle_degrees;
      bevel.altitude_degrees = altitude_degrees;
      bevel.use_global_light = false;
    }
  }
}

void merge_missing_layer_style_effects(LayerStyle& target, LayerStyle source) {
  if (!source.effects_visible) {
    target.effects_visible = false;
  }
  if (target.drop_shadows.empty()) {
    target.drop_shadows = std::move(source.drop_shadows);
  }
  if (target.inner_shadows.empty()) {
    target.inner_shadows = std::move(source.inner_shadows);
  }
  if (target.outer_glows.empty()) {
    target.outer_glows = std::move(source.outer_glows);
  }
  if (target.inner_glows.empty()) {
    target.inner_glows = std::move(source.inner_glows);
  }
  if (target.color_overlays.empty()) {
    target.color_overlays = std::move(source.color_overlays);
  }
  if (target.gradient_fills.empty()) {
    target.gradient_fills = std::move(source.gradient_fills);
  }
  if (target.pattern_overlays.empty()) {
    target.pattern_overlays = std::move(source.pattern_overlays);
  }
  if (target.strokes.empty()) {
    target.strokes = std::move(source.strokes);
  }
  if (target.bevels.empty()) {
    target.bevels = std::move(source.bevels);
  }
  if (target.satins.empty()) {
    target.satins = std::move(source.satins);
  }
}

namespace {

void write_bool(BigEndianWriter& writer, bool value) {
  writer.write_u8(value ? 1U : 0U);
}

bool read_bool(BigEndianReader& reader) {
  return reader.read_u8() != 0U;
}

}  // namespace

void write_f32(BigEndianWriter& writer, float value) {
  writer.write_u32(std::bit_cast<std::uint32_t>(value));
}

namespace {

float read_f32(BigEndianReader& reader) {
  return std::bit_cast<float>(reader.read_u32());
}

void write_rgb_color(BigEndianWriter& writer, RgbColor color) {
  writer.write_u8(color.red);
  writer.write_u8(color.green);
  writer.write_u8(color.blue);
}

RgbColor read_rgb_color(BigEndianReader& reader) {
  return RgbColor{reader.read_u8(), reader.read_u8(), reader.read_u8()};
}

std::uint8_t gradient_type_value(LayerStyleGradientType type) {
  switch (type) {
    case LayerStyleGradientType::Radial:
      return 1U;
    case LayerStyleGradientType::Angle:
      return 2U;
    case LayerStyleGradientType::Reflected:
      return 3U;
    case LayerStyleGradientType::Diamond:
      return 4U;
    case LayerStyleGradientType::Linear:
      return 0U;
  }
  return 0U;
}

LayerStyleGradientType gradient_type_from_value(std::uint8_t value) {
  switch (value) {
    case 1U:
      return LayerStyleGradientType::Radial;
    case 2U:
      return LayerStyleGradientType::Angle;
    case 3U:
      return LayerStyleGradientType::Reflected;
    case 4U:
      return LayerStyleGradientType::Diamond;
    default:
      return LayerStyleGradientType::Linear;
  }
}

std::uint8_t stroke_position_value(LayerStrokePosition position) {
  switch (position) {
    case LayerStrokePosition::Inside:
      return 1U;
    case LayerStrokePosition::Center:
      return 2U;
    case LayerStrokePosition::Outside:
      return 0U;
  }
  return 0U;
}

LayerStrokePosition stroke_position_from_value(std::uint8_t value) {
  switch (value) {
    case 1U:
      return LayerStrokePosition::Inside;
    case 2U:
      return LayerStrokePosition::Center;
    default:
      return LayerStrokePosition::Outside;
  }
}

void write_count(BigEndianWriter& writer, std::size_t count, const char* field) {
  writer.write_u16(checked_u16(count, field));
}

std::uint16_t read_count(BigEndianReader& reader, const char* field) {
  const auto count = reader.read_u16();
  if (count > kMaxPatchyLayerStyleEntries) {
    throw std::runtime_error(std::string("Patchy layer style has too many entries: ") + field);
  }
  return count;
}

void write_layer_style_gradient(BigEndianWriter& writer, const LayerStyleGradient& gradient) {
  writer.write_u8(gradient_type_value(gradient.type));
  write_f32(writer, gradient.angle_degrees);
  write_f32(writer, gradient.scale);
  write_bool(writer, gradient.reverse);
  write_count(writer, gradient.color_stops.size(), "layer style gradient color stops");
  for (const auto& stop : gradient.color_stops) {
    write_f32(writer, stop.location);
    write_rgb_color(writer, stop.color);
  }
  write_count(writer, gradient.alpha_stops.size(), "layer style gradient alpha stops");
  for (const auto& stop : gradient.alpha_stops) {
    write_f32(writer, stop.location);
    write_f32(writer, stop.opacity);
  }
}

LayerStyleGradient read_layer_style_gradient(BigEndianReader& reader) {
  LayerStyleGradient gradient;
  gradient.type = gradient_type_from_value(reader.read_u8());
  gradient.angle_degrees = read_f32(reader);
  gradient.scale = read_f32(reader);
  gradient.reverse = read_bool(reader);

  const auto color_stop_count = read_count(reader, "layer style gradient color stops");
  gradient.color_stops.reserve(color_stop_count);
  for (std::uint16_t i = 0; i < color_stop_count; ++i) {
    gradient.color_stops.push_back(GradientColorStop{read_f32(reader), read_rgb_color(reader)});
  }

  const auto alpha_stop_count = read_count(reader, "layer style gradient alpha stops");
  gradient.alpha_stops.reserve(alpha_stop_count);
  for (std::uint16_t i = 0; i < alpha_stop_count; ++i) {
    gradient.alpha_stops.push_back(GradientAlphaStop{read_f32(reader), read_f32(reader)});
  }
  return gradient;
}

std::vector<std::uint8_t> patchy_layer_style_payload(const LayerStyle& style) {
  BigEndianWriter writer;
  write_signature(writer, kPatchyLayerStylePayloadSignature);
  writer.write_u16(kPatchyLayerStyleVersion);
  write_bool(writer, style.effects_visible);

  write_count(writer, style.drop_shadows.size(), "drop shadows");
  for (const auto& shadow : style.drop_shadows) {
    write_bool(writer, shadow.enabled);
    write_signature(writer, blend_mode_key(shadow.blend_mode));
    write_rgb_color(writer, shadow.color);
    write_f32(writer, shadow.opacity);
    write_f32(writer, shadow.angle_degrees);
    write_f32(writer, shadow.distance);
    write_f32(writer, shadow.spread);
    write_f32(writer, shadow.size);
  }

  write_count(writer, style.outer_glows.size(), "outer glows");
  for (const auto& glow : style.outer_glows) {
    write_bool(writer, glow.enabled);
    write_signature(writer, blend_mode_key(glow.blend_mode));
    write_rgb_color(writer, glow.color);
    write_f32(writer, glow.opacity);
    write_f32(writer, glow.spread);
    write_f32(writer, glow.size);
  }

  write_count(writer, style.gradient_fills.size(), "gradient fills");
  for (const auto& fill : style.gradient_fills) {
    write_bool(writer, fill.enabled);
    write_signature(writer, blend_mode_key(fill.blend_mode));
    write_f32(writer, fill.opacity);
    write_layer_style_gradient(writer, fill.gradient);
  }

  write_count(writer, style.strokes.size(), "strokes");
  for (const auto& stroke : style.strokes) {
    write_bool(writer, stroke.enabled);
    write_signature(writer, blend_mode_key(stroke.blend_mode));
    write_rgb_color(writer, stroke.color);
    write_f32(writer, stroke.opacity);
    write_f32(writer, stroke.size);
    writer.write_u8(stroke_position_value(stroke.position));
    write_bool(writer, stroke.uses_gradient);
    write_layer_style_gradient(writer, stroke.gradient);
  }

  write_count(writer, style.bevels.size(), "bevels");
  for (const auto& bevel : style.bevels) {
    write_bool(writer, bevel.enabled);
    write_signature(writer, blend_mode_key(bevel.highlight_blend_mode));
    write_rgb_color(writer, bevel.highlight_color);
    write_f32(writer, bevel.highlight_opacity);
    write_signature(writer, blend_mode_key(bevel.shadow_blend_mode));
    write_rgb_color(writer, bevel.shadow_color);
    write_f32(writer, bevel.shadow_opacity);
    write_f32(writer, bevel.angle_degrees);
    write_f32(writer, bevel.altitude_degrees);
    write_f32(writer, bevel.depth);
    write_f32(writer, bevel.size);
    write_bool(writer, bevel.direction_up);
  }

  write_count(writer, style.color_overlays.size(), "color overlays");
  for (const auto& overlay : style.color_overlays) {
    write_bool(writer, overlay.enabled);
    write_signature(writer, blend_mode_key(overlay.blend_mode));
    write_rgb_color(writer, overlay.color);
    write_f32(writer, overlay.opacity);
  }

  return writer.bytes();
}

}  // namespace

std::optional<LayerStyle> parse_patchy_layer_style(std::span<const std::uint8_t> payload) {
  try {
    BigEndianReader reader(payload);
    if (read_signature(reader) != kPatchyLayerStylePayloadSignature) {
      return std::nullopt;
    }
    if (reader.read_u16() != kPatchyLayerStyleVersion) {
      return std::nullopt;
    }

    LayerStyle style;
    style.effects_visible = read_bool(reader);

    const auto shadow_count = read_count(reader, "drop shadows");
    style.drop_shadows.reserve(shadow_count);
    for (std::uint16_t i = 0; i < shadow_count; ++i) {
      LayerDropShadow shadow;
      shadow.enabled = read_bool(reader);
      shadow.blend_mode = blend_mode_from_key(read_signature(reader));
      shadow.color = read_rgb_color(reader);
      shadow.opacity = read_f32(reader);
      shadow.angle_degrees = read_f32(reader);
      shadow.distance = read_f32(reader);
      shadow.spread = read_f32(reader);
      shadow.size = read_f32(reader);
      style.drop_shadows.push_back(shadow);
    }

    const auto glow_count = read_count(reader, "outer glows");
    style.outer_glows.reserve(glow_count);
    for (std::uint16_t i = 0; i < glow_count; ++i) {
      LayerOuterGlow glow;
      glow.enabled = read_bool(reader);
      glow.blend_mode = blend_mode_from_key(read_signature(reader));
      glow.color = read_rgb_color(reader);
      glow.opacity = read_f32(reader);
      glow.spread = read_f32(reader);
      glow.size = read_f32(reader);
      style.outer_glows.push_back(glow);
    }

    const auto gradient_fill_count = read_count(reader, "gradient fills");
    style.gradient_fills.reserve(gradient_fill_count);
    for (std::uint16_t i = 0; i < gradient_fill_count; ++i) {
      LayerGradientFill fill;
      fill.enabled = read_bool(reader);
      fill.blend_mode = blend_mode_from_key(read_signature(reader));
      fill.opacity = read_f32(reader);
      fill.gradient = read_layer_style_gradient(reader);
      style.gradient_fills.push_back(std::move(fill));
    }

    const auto stroke_count = read_count(reader, "strokes");
    style.strokes.reserve(stroke_count);
    for (std::uint16_t i = 0; i < stroke_count; ++i) {
      LayerStroke stroke;
      stroke.enabled = read_bool(reader);
      stroke.blend_mode = blend_mode_from_key(read_signature(reader));
      stroke.color = read_rgb_color(reader);
      stroke.opacity = read_f32(reader);
      stroke.size = read_f32(reader);
      stroke.position = stroke_position_from_value(reader.read_u8());
      stroke.uses_gradient = read_bool(reader);
      stroke.gradient = read_layer_style_gradient(reader);
      style.strokes.push_back(std::move(stroke));
    }

    const auto bevel_count = read_count(reader, "bevels");
    style.bevels.reserve(bevel_count);
    for (std::uint16_t i = 0; i < bevel_count; ++i) {
      LayerBevelEmboss bevel;
      bevel.enabled = read_bool(reader);
      bevel.highlight_blend_mode = blend_mode_from_key(read_signature(reader));
      bevel.highlight_color = read_rgb_color(reader);
      bevel.highlight_opacity = read_f32(reader);
      bevel.shadow_blend_mode = blend_mode_from_key(read_signature(reader));
      bevel.shadow_color = read_rgb_color(reader);
      bevel.shadow_opacity = read_f32(reader);
      bevel.angle_degrees = read_f32(reader);
      bevel.altitude_degrees = read_f32(reader);
      bevel.depth = read_f32(reader);
      bevel.size = read_f32(reader);
      bevel.direction_up = read_bool(reader);
      style.bevels.push_back(bevel);
    }

    if (reader.remaining() > 0) {
      const auto overlay_count = read_count(reader, "color overlays");
      style.color_overlays.reserve(overlay_count);
      for (std::uint16_t i = 0; i < overlay_count; ++i) {
        LayerColorOverlay overlay;
        overlay.enabled = read_bool(reader);
        overlay.blend_mode = blend_mode_from_key(read_signature(reader));
        overlay.color = read_rgb_color(reader);
        overlay.opacity = read_f32(reader);
        style.color_overlays.push_back(overlay);
      }
    }

    return style;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// write_f64 moved to psd_descriptor.{hpp,cpp} alongside read_f64.

namespace {

// Photoshop 2026's lfx2 parser resolves 'BlnM' enum values only through their
// full stringID names ("multiply", "screen", ...); 4-char codes like 'Mltp'
// are silently read as Normal (verified July 2026 by byte-patching a probe
// PSD). Photoshop itself serializes these enums as length-prefixed strings,
// so write exactly that form.
std::string_view blend_mode_descriptor_value(BlendMode mode) {
  switch (mode) {
    case BlendMode::Multiply:
      return "multiply";
    case BlendMode::Screen:
      return "screen";
    case BlendMode::Overlay:
      return "overlay";
    case BlendMode::Darken:
      return "darken";
    case BlendMode::Lighten:
      return "lighten";
    case BlendMode::ColorDodge:
      return "colorDodge";
    case BlendMode::ColorBurn:
      return "colorBurn";
    case BlendMode::HardLight:
      return "hardLight";
    case BlendMode::SoftLight:
      return "softLight";
    case BlendMode::Difference:
      return "difference";
    case BlendMode::LinearBurn:
      return "linearBurn";
    case BlendMode::PinLight:
      return "pinLight";
    case BlendMode::Saturation:
      return "saturation";
    case BlendMode::Luminosity:
      return "luminosity";
    case BlendMode::Exclusion:
      return "exclusion";
    case BlendMode::Hue:
      return "hue";
    case BlendMode::Color:
      return "color";
    case BlendMode::LinearDodge:
      return "linearDodge";
    // Photoshop's stringIDs for these two really are "blendSubtraction"/"blendDivide".
    case BlendMode::Subtract:
      return "blendSubtraction";
    case BlendMode::Divide:
      return "blendDivide";
    case BlendMode::PassThrough:
    case BlendMode::Normal:
      return "normal";
  }
  return "normal";
}

void write_blend_mode_descriptor_item(BigEndianWriter& writer, std::string_view key, BlendMode mode) {
  write_descriptor_enum_item(writer, key, "BlnM", blend_mode_descriptor_value(mode));
}

void write_rgb_color_descriptor(BigEndianWriter& writer, RgbColor color) {
  write_descriptor_object_header(writer, "", "RGBC", 3);
  write_descriptor_unit_float_item(writer, "Rd  ", {'#', 'P', 'r', 'c'}, color.red);
  write_descriptor_unit_float_item(writer, "Grn ", {'#', 'P', 'r', 'c'}, color.green);
  write_descriptor_unit_float_item(writer, "Bl  ", {'#', 'P', 'r', 'c'}, color.blue);
}

void write_rgb_color_descriptor_item(BigEndianWriter& writer, std::string_view key, RgbColor color) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_rgb_color_descriptor(writer, color);
}

void write_gradient_color_stop(BigEndianWriter& writer, const GradientColorStop& stop) {
  const auto dynamic = stop.kind != GradientColorStop::Kind::User;
  write_descriptor_object_header(writer, "", "Clrt", dynamic ? 3U : 4U);
  write_descriptor_enum_item(writer, "Type", "Clry", stop.kind == GradientColorStop::Kind::Foreground
                                                        ? "FrgC"
                                                        : stop.kind == GradientColorStop::Kind::Background ? "BckC"
                                                                                                           : "UsrS");
  write_descriptor_long_item(writer, "Lctn", static_cast<std::int32_t>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 4096.0F)));
  write_descriptor_long_item(
      writer, "Mdpn",
      static_cast<std::int32_t>(std::lround(std::clamp(stop.midpoint, 0.0F, 1.0F) * 100.0F)));
  if (!dynamic) {
    write_rgb_color_descriptor_item(writer, "Clr ", stop.color);
  }
}

void write_gradient_alpha_stop(BigEndianWriter& writer, const GradientAlphaStop& stop) {
  write_descriptor_object_header(writer, "", "TrnS", 3);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, std::clamp(stop.opacity, 0.0F, 1.0F) * 100.0);
  write_descriptor_long_item(writer, "Lctn", static_cast<std::int32_t>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 4096.0F)));
  write_descriptor_long_item(
      writer, "Mdpn",
      static_cast<std::int32_t>(std::lround(std::clamp(stop.midpoint, 0.0F, 1.0F) * 100.0F)));
}

std::string_view gradient_type_descriptor_value(LayerStyleGradientType type) {
  switch (type) {
    case LayerStyleGradientType::Radial:
      return "Rdl ";
    case LayerStyleGradientType::Angle:
      return "Angl";
    case LayerStyleGradientType::Reflected:
      return "Rflc";
    case LayerStyleGradientType::Diamond:
      return "Dmnd";
    case LayerStyleGradientType::Linear:
      return "Lnr ";
  }
  return "Lnr ";
}

std::string_view gradient_interpolation_descriptor_value(GradientInterpolationMethod method) {
  switch (method) {
    case GradientInterpolationMethod::Perceptual:
      return "perceptual";
    case GradientInterpolationMethod::Linear:
      return "linear";
    case GradientInterpolationMethod::Classic:
      return "Gcls";
  }
  return "Gcls";
}

void write_layer_style_gradient_descriptor(BigEndianWriter& writer, const LayerStyleGradient& gradient) {
  auto color_stops = gradient.color_stops;
  auto alpha_stops = gradient.alpha_stops;
  if (color_stops.empty()) {
    color_stops.push_back(GradientColorStop{0.0F, RgbColor{0, 0, 0}});
    color_stops.push_back(GradientColorStop{1.0F, RgbColor{255, 255, 255}});
  }
  if (alpha_stops.empty()) {
    alpha_stops.push_back(GradientAlphaStop{0.0F, 1.0F});
    alpha_stops.push_back(GradientAlphaStop{1.0F, 1.0F});
  }
  std::stable_sort(
      color_stops.begin(), color_stops.end(),
      [](const GradientColorStop& lhs, const GradientColorStop& rhs) { return lhs.location < rhs.location; });
  std::stable_sort(
      alpha_stops.begin(), alpha_stops.end(),
      [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) { return lhs.location < rhs.location; });

  if (gradient.form == GradientDefinitionForm::Noise) {
    write_descriptor_object_header(writer, "", "Grdn", 9);
    write_descriptor_text_item(
        writer, "Nm  ", gradient.name.empty() ? "Custom" : gradient.name);
    write_descriptor_enum_item(writer, "GrdF", "GrdF", "ClNs");
    write_descriptor_bool_item(writer, "ShTr", gradient.noise.add_transparency);
    write_descriptor_bool_item(writer, "VctC", gradient.noise.restrict_colors);
    const auto color_space =
        gradient.noise.color_model == GradientNoiseColorModel::HSB   ? "HSBC"
        : gradient.noise.color_model == GradientNoiseColorModel::Lab ? "LABC"
                                                                     : "RGBC";
    write_descriptor_enum_item(writer, "ClrS", "ClrS", color_space);
    write_descriptor_long_item(writer, "RndS",
                               static_cast<std::int32_t>(gradient.noise.seed));
    write_descriptor_long_item(writer, "Smth", gradient.noise.roughness);
    const auto write_range = [&writer](std::string_view key, const std::array<std::uint16_t, 4>& values) {
      write_descriptor_item_header(writer, key, {'V', 'l', 'L', 's'});
      writer.write_u32(4);
      for (const auto value : values) {
        write_signature(writer, {'l', 'o', 'n', 'g'});
        writer.write_u32(std::clamp<std::uint16_t>(value, 0, 100));
      }
    };
    write_range("Mnm ", gradient.noise.minimum);
    write_range("Mxm ", gradient.noise.maximum);
    return;
  }

  write_descriptor_object_header(writer, "", "Grdn", 5);
  write_descriptor_text_item(writer, "Nm  ",
                             gradient.name.empty() ? "Custom" : gradient.name);
  write_descriptor_enum_item(writer, "GrdF", "GrdF", "CstS");
  write_descriptor_double_item(writer, "Intr", gradient.smoothness);

  write_descriptor_item_header(writer, "Clrs", {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(color_stops.size(), "gradient color stops"));
  for (const auto& stop : color_stops) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_gradient_color_stop(writer, stop);
  }

  write_descriptor_item_header(writer, "Trns", {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(alpha_stops.size(), "gradient alpha stops"));
  for (const auto& stop : alpha_stops) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_gradient_alpha_stop(writer, stop);
  }
}

void write_layer_style_gradient_descriptor_item(BigEndianWriter& writer, std::string_view key,
                                                const LayerStyleGradient& gradient) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_layer_style_gradient_descriptor(writer, gradient);
}

// Photoshop's FrFX gradient shape differs from the otherwise similar GrFl
// descriptor. In particular, RGB components are plain doubles, each color stop
// puts Clr before Type/Lctn/Mdpn, and the Grad object's unicode header name is
// "Gradient". Native ChFX colors use the same double RGB object.
void write_native_rgb_color_descriptor(BigEndianWriter& writer, RgbColor color) {
  write_descriptor_object_header(writer, "", "RGBC", 3);
  write_descriptor_double_item(writer, "Rd  ", color.red);
  write_descriptor_double_item(writer, "Grn ", color.green);
  write_descriptor_double_item(writer, "Bl  ", color.blue);
}

void write_native_rgb_color_descriptor_item(BigEndianWriter& writer, std::string_view key, RgbColor color) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_native_rgb_color_descriptor(writer, color);
}

void write_stroke_gradient_color_stop(BigEndianWriter& writer, const GradientColorStop& stop) {
  write_descriptor_object_header(writer, "", "Clrt", 4);
  write_native_rgb_color_descriptor_item(writer, "Clr ", stop.color);
  write_descriptor_enum_item(writer, "Type", "Clry", "UsrS");
  write_descriptor_long_item(
      writer, "Lctn",
      static_cast<std::int32_t>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 4096.0F)));
  write_descriptor_long_item(
      writer, "Mdpn",
      static_cast<std::int32_t>(std::lround(std::clamp(stop.midpoint, 0.0F, 1.0F) * 100.0F)));
}

void write_stroke_gradient_descriptor(BigEndianWriter& writer, const LayerStyleGradient& gradient) {
  if (gradient.form == GradientDefinitionForm::Noise) {
    write_layer_style_gradient_descriptor(writer, gradient);
    return;
  }
  auto color_stops = gradient.color_stops;
  auto alpha_stops = gradient.alpha_stops;
  if (color_stops.empty()) {
    color_stops.push_back(GradientColorStop{0.0F, RgbColor{0, 0, 0}});
    color_stops.push_back(GradientColorStop{1.0F, RgbColor{255, 255, 255}});
  }
  if (alpha_stops.empty()) {
    alpha_stops.push_back(GradientAlphaStop{0.0F, 1.0F});
    alpha_stops.push_back(GradientAlphaStop{1.0F, 1.0F});
  }
  std::stable_sort(
      color_stops.begin(), color_stops.end(),
      [](const GradientColorStop& lhs, const GradientColorStop& rhs) { return lhs.location < rhs.location; });
  std::stable_sort(
      alpha_stops.begin(), alpha_stops.end(),
      [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) { return lhs.location < rhs.location; });

  write_descriptor_object_header(writer, "Gradient", "Grdn", 5);
  write_descriptor_text_item(writer, "Nm  ",
                             gradient.name.empty() ? "Custom" : gradient.name);
  write_descriptor_enum_item(writer, "GrdF", "GrdF", "CstS");
  write_descriptor_double_item(writer, "Intr", gradient.smoothness);

  write_descriptor_item_header(writer, "Clrs", {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(color_stops.size(), "gradient color stops"));
  for (const auto& stop : color_stops) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_stroke_gradient_color_stop(writer, stop);
  }

  write_descriptor_item_header(writer, "Trns", {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(alpha_stops.size(), "gradient alpha stops"));
  for (const auto& stop : alpha_stops) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_gradient_alpha_stop(writer, stop);
  }
}

void write_stroke_gradient_descriptor_item(BigEndianWriter& writer, std::string_view key,
                                           const LayerStyleGradient& gradient) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_stroke_gradient_descriptor(writer, gradient);
}

std::string_view stroke_position_descriptor_value(LayerStrokePosition position) {
  switch (position) {
    case LayerStrokePosition::Inside:
      return "InsF";
    case LayerStrokePosition::Center:
      return "CtrF";
    case LayerStrokePosition::Outside:
      return "OutF";
  }
  return "OutF";
}

std::string_view inner_glow_source_descriptor_value(LayerInnerGlowSource source) {
  return source == LayerInnerGlowSource::Center ? "SrcC" : "SrcE";
}

void write_drop_shadow_descriptor(BigEndianWriter& writer, const LayerDropShadow& shadow) {
  write_descriptor_object_header(writer, "", "DrSh", 12);
  write_descriptor_bool_item(writer, "enab", shadow.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", shadow.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", shadow.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, shadow.opacity * 100.0);
  // Claiming "use global light" would make Photoshop ignore lagl and swing the
  // shadow to the document's global angle; Patchy's angles are per-effect, so
  // always declare them local.
  write_descriptor_bool_item(writer, "uglg", shadow.use_global_light);
  write_descriptor_unit_float_item(writer, "lagl", {'#', 'A', 'n', 'g'}, shadow.angle_degrees);
  write_descriptor_unit_float_item(writer, "Dstn", {'#', 'P', 'x', 'l'}, shadow.distance);
  write_descriptor_unit_float_item(writer, "Ckmt", {'#', 'P', 'x', 'l'}, shadow.spread);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, shadow.size);
  write_descriptor_unit_float_item(writer, "Nose", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_bool_item(writer, "AntA", false);
  write_descriptor_bool_item(writer, "layerConceals", true);
}

void write_inner_shadow_descriptor(BigEndianWriter& writer, const LayerInnerShadow& shadow) {
  write_descriptor_object_header(writer, "", "IrSh", 11);
  write_descriptor_bool_item(writer, "enab", shadow.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", shadow.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", shadow.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, shadow.opacity * 100.0);
  write_descriptor_bool_item(writer, "uglg", shadow.use_global_light);
  write_descriptor_unit_float_item(writer, "lagl", {'#', 'A', 'n', 'g'}, shadow.angle_degrees);
  write_descriptor_unit_float_item(writer, "Dstn", {'#', 'P', 'x', 'l'}, shadow.distance);
  write_descriptor_unit_float_item(writer, "Ckmt", {'#', 'P', 'x', 'l'}, shadow.choke);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, shadow.size);
  write_descriptor_unit_float_item(writer, "Nose", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_bool_item(writer, "AntA", false);
}

void write_outer_glow_descriptor(BigEndianWriter& writer, const LayerOuterGlow& glow) {
  write_descriptor_object_header(writer, "", "OrGl", 10);
  write_descriptor_bool_item(writer, "enab", glow.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", glow.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", glow.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, glow.opacity * 100.0);
  write_descriptor_unit_float_item(writer, "Ckmt", {'#', 'P', 'x', 'l'}, glow.spread);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, glow.size);
  write_descriptor_unit_float_item(writer, "Nose", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_unit_float_item(writer, "ShdN", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_bool_item(writer, "AntA", false);
  write_descriptor_unit_float_item(writer, "Inpr", {'#', 'P', 'r', 'c'}, 50.0);
}

void write_inner_glow_descriptor(BigEndianWriter& writer, const LayerInnerGlow& glow) {
  write_descriptor_object_header(writer, "", "IrGl", 11);
  write_descriptor_bool_item(writer, "enab", glow.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", glow.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", glow.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, glow.opacity * 100.0);
  write_descriptor_unit_float_item(writer, "Ckmt", {'#', 'P', 'x', 'l'}, glow.choke);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, glow.size);
  write_descriptor_unit_float_item(writer, "Nose", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_enum_item(writer, "glwS", "IGSr", inner_glow_source_descriptor_value(glow.source));
  write_descriptor_unit_float_item(writer, "ShdN", {'#', 'P', 'r', 'c'}, 0.0);
  write_descriptor_bool_item(writer, "AntA", false);
  write_descriptor_unit_float_item(writer, "Inpr", {'#', 'P', 'r', 'c'}, 50.0);
}

void write_color_overlay_descriptor(BigEndianWriter& writer, const LayerColorOverlay& overlay) {
  write_descriptor_object_header(writer, "", "SoFi", 4);
  write_descriptor_bool_item(writer, "enab", overlay.enabled);
  write_blend_mode_descriptor_item(writer, "Md  ", overlay.blend_mode);
  write_rgb_color_descriptor_item(writer, "Clr ", overlay.color);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, overlay.opacity * 100.0);
}

void write_gradient_fill_descriptor(BigEndianWriter& writer, const LayerGradientFill& fill) {
  // Field set and order mirror what Photoshop 2026 writes for a gradient
  // overlay. PS silently resets the blend mode of GrFl descriptors that lack
  // this shape (byte-diffed July 2026), so keep the layout exact.
  write_descriptor_object_header(writer, "", "GrFl", 14);
  write_descriptor_bool_item(writer, "enab", fill.enabled);
  write_descriptor_bool_item(writer, "present", true);
  write_descriptor_bool_item(writer, "showInDialog", true);
  write_blend_mode_descriptor_item(writer, "Md  ", fill.blend_mode);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, fill.opacity * 100.0);
  write_layer_style_gradient_descriptor_item(writer, "Grad", fill.gradient);
  write_descriptor_unit_float_item(writer, "Angl", {'#', 'A', 'n', 'g'}, fill.gradient.angle_degrees);
  write_descriptor_enum_item(writer, "Type", "GrdT", gradient_type_descriptor_value(fill.gradient.type));
  write_descriptor_bool_item(writer, "Rvrs", fill.gradient.reverse);
  write_descriptor_bool_item(writer, "Dthr", fill.gradient.dither);
  write_descriptor_enum_item(writer, "gs99", "gradientInterpolationMethodType",
      gradient_interpolation_descriptor_value(fill.gradient.interpolation));
  write_descriptor_bool_item(writer, "Algn", fill.gradient.align_with_layer);
  write_descriptor_unit_float_item(writer, "Scl ", {'#', 'P', 'r', 'c'}, fill.gradient.scale * 100.0);
  write_descriptor_item_header(writer, "Ofst", {'O', 'b', 'j', 'c'});
  write_descriptor_object_header(writer, "", "Pnt ", 2);
  write_descriptor_unit_float_item(writer, "Hrzn", {'#', 'P', 'r', 'c'},
                                   fill.gradient.offset_x_percent);
  write_descriptor_unit_float_item(writer, "Vrtc", {'#', 'P', 'r', 'c'},
                                   fill.gradient.offset_y_percent);
}

void write_stroke_descriptor(BigEndianWriter& writer, const LayerStroke& stroke) {
  write_descriptor_object_header(writer, "", "FrFX", stroke.uses_gradient ? 19U : 10U);
  write_descriptor_bool_item(writer, "enab", stroke.enabled);
  write_descriptor_bool_item(writer, "present", true);
  write_descriptor_bool_item(writer, "showInDialog", true);
  write_descriptor_enum_item(writer, "Styl", "FStl", stroke_position_descriptor_value(stroke.position));
  write_descriptor_enum_item(writer, "PntT", "FrFl", stroke.uses_gradient ? "GrFl" : "SClr");
  write_blend_mode_descriptor_item(writer, "Md  ", stroke.blend_mode);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, stroke.opacity * 100.0);
  write_descriptor_unit_float_item(writer, "Sz  ", {'#', 'P', 'x', 'l'}, stroke.size);
  if (stroke.uses_gradient) {
    // Photoshop writes a black Clr placeholder even though PntT selects the
    // following gradient. Omitting it makes the descriptor non-native.
    write_native_rgb_color_descriptor_item(writer, "Clr ", RgbColor{0, 0, 0});
    write_stroke_gradient_descriptor_item(writer, "Grad", stroke.gradient);
    write_descriptor_enum_item(writer, "gradientsInterpolationMethod", "gradientInterpolationMethodType",
        gradient_interpolation_descriptor_value(stroke.gradient.interpolation));
    write_descriptor_unit_float_item(writer, "Angl", {'#', 'A', 'n', 'g'}, stroke.gradient.angle_degrees);
    write_descriptor_enum_item(writer, "Type", "GrdT", gradient_type_descriptor_value(stroke.gradient.type));
    write_descriptor_bool_item(writer, "Rvrs", stroke.gradient.reverse);
    write_descriptor_bool_item(writer, "Dthr", stroke.gradient.dither);
    write_descriptor_unit_float_item(writer, "Scl ", {'#', 'P', 'r', 'c'}, stroke.gradient.scale * 100.0);
    write_descriptor_bool_item(writer, "Algn",
                               stroke.gradient.align_with_layer);
    write_descriptor_item_header(writer, "Ofst", {'O', 'b', 'j', 'c'});
    write_descriptor_object_header(writer, "", "Pnt ", 2);
    write_descriptor_unit_float_item(writer, "Hrzn", {'#', 'P', 'r', 'c'},
                                     stroke.gradient.offset_x_percent);
    write_descriptor_unit_float_item(writer, "Vrtc", {'#', 'P', 'r', 'c'},
                                     stroke.gradient.offset_y_percent);
  } else {
    write_native_rgb_color_descriptor_item(writer, "Clr ", stroke.color);
  }
  write_descriptor_bool_item(writer, "overprint", false);
}

// Writes a ShpC contour object. A Linear contour mirrors PS 27.8's default form
// (name "Linear", the two-point identity Crv, no Cnty flags); custom curves
// carry Cnty on every point (true = smooth), PS's canonical custom-curve form.
void write_shape_contour_descriptor_item(BigEndianWriter& writer, std::string_view key,
                                         const StyleContour& contour) {
  write_descriptor_item_header(writer, key, {'O', 'b', 'j', 'c'});
  write_descriptor_object_header(writer, "", "ShpC", 2);
  if (style_contour_is_linear(contour)) {
    write_descriptor_text_item(writer, "Nm  ", "Linear");
    write_descriptor_item_header(writer, "Crv ", {'V', 'l', 'L', 's'});
    writer.write_u32(2);
    for (const auto point : {0.0, 255.0}) {
      write_signature(writer, {'O', 'b', 'j', 'c'});
      write_descriptor_object_header(writer, "", "CrPt", 2);
      write_descriptor_double_item(writer, "Hrzn", point);
      write_descriptor_double_item(writer, "Vrtc", point);
    }
    return;
  }
  write_descriptor_text_item(writer, "Nm  ", contour.name.empty() ? "Custom" : contour.name);
  write_descriptor_item_header(writer, "Crv ", {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(contour.points.size(), "contour points"));
  for (const auto& point : contour.points) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_descriptor_object_header(writer, "", "CrPt", 3);
    write_descriptor_double_item(writer, "Hrzn", point.x);
    write_descriptor_double_item(writer, "Vrtc", point.y);
    write_descriptor_bool_item(writer, "Cnty", !point.corner);
  }
}

std::string_view bevel_style_descriptor_value(BevelEmbossStyleKind style) {
  switch (style) {
    case BevelEmbossStyleKind::OuterBevel:
      return "OtrB";
    case BevelEmbossStyleKind::Emboss:
      return "Embs";
    case BevelEmbossStyleKind::PillowEmboss:
      return "PlEb";
    case BevelEmbossStyleKind::StrokeEmboss:
      return "strokeEmboss";
    case BevelEmbossStyleKind::InnerBevel:
    default:
      return "InrB";
  }
}

std::string_view bevel_technique_descriptor_value(BevelTechnique technique) {
  switch (technique) {
    case BevelTechnique::ChiselHard:
      return "PrBL";
    case BevelTechnique::ChiselSoft:
      return "Slmt";
    case BevelTechnique::Smooth:
    default:
      return "SfBL";
  }
}

void write_bevel_emboss_descriptor(BigEndianWriter& writer, const LayerBevelEmboss& bevel) {
  // Photoshop 2026's native ebbl shape (COM captures; docs/ps-compat.md): 22
  // base items in this exact order, the Contour sub inserting MpgS/AntA/Inpr
  // right after useShape and the Texture sub appending its six keys after
  // useTexture. Conditional keys are omitted entirely while a sub-option is
  // off, mirroring PS.
  std::uint32_t item_count = 22;
  if (bevel.contour.enabled) {
    item_count += 3;
  }
  if (bevel.texture.enabled) {
    item_count += 6;
  }
  write_descriptor_object_header(writer, "", "ebbl", item_count);
  write_descriptor_bool_item(writer, "enab", bevel.enabled);
  write_descriptor_bool_item(writer, "present", true);
  write_descriptor_bool_item(writer, "showInDialog", true);
  write_blend_mode_descriptor_item(writer, "hglM", bevel.highlight_blend_mode);
  write_rgb_color_descriptor_item(writer, "hglC", bevel.highlight_color);
  write_descriptor_unit_float_item(writer, "hglO", {'#', 'P', 'r', 'c'}, bevel.highlight_opacity * 100.0);
  write_blend_mode_descriptor_item(writer, "sdwM", bevel.shadow_blend_mode);
  write_rgb_color_descriptor_item(writer, "sdwC", bevel.shadow_color);
  write_descriptor_unit_float_item(writer, "sdwO", {'#', 'P', 'r', 'c'}, bevel.shadow_opacity * 100.0);
  write_descriptor_enum_item(writer, "bvlT", "bvlT", bevel_technique_descriptor_value(bevel.technique));
  write_descriptor_enum_item(writer, "bvlS", "BESl", bevel_style_descriptor_value(bevel.style));
  write_descriptor_bool_item(writer, "uglg", bevel.use_global_light);
  write_descriptor_unit_float_item(writer, "lagl", {'#', 'A', 'n', 'g'}, bevel.angle_degrees);
  write_descriptor_unit_float_item(writer, "Lald", {'#', 'A', 'n', 'g'}, bevel.altitude_degrees);
  write_descriptor_unit_float_item(writer, "srgR", {'#', 'P', 'r', 'c'}, bevel.depth * 100.0);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, bevel.size);
  write_descriptor_enum_item(writer, "bvlD", "BESs", bevel.direction_up ? "In  " : "Out ");
  write_shape_contour_descriptor_item(writer, "TrnS", bevel.gloss_contour);
  write_descriptor_bool_item(writer, "antialiasGloss", bevel.gloss_anti_aliased);
  write_descriptor_unit_float_item(writer, "Sftn", {'#', 'P', 'x', 'l'}, bevel.soften);
  write_descriptor_bool_item(writer, "useShape", bevel.contour.enabled);
  if (bevel.contour.enabled) {
    write_shape_contour_descriptor_item(writer, "MpgS", bevel.contour.contour);
    write_descriptor_bool_item(writer, "AntA", bevel.contour.anti_aliased);
    write_descriptor_unit_float_item(writer, "Inpr", {'#', 'P', 'r', 'c'}, bevel.contour.range * 100.0);
  }
  write_descriptor_bool_item(writer, "useTexture", bevel.texture.enabled);
  if (bevel.texture.enabled) {
    write_descriptor_bool_item(writer, "InvT", bevel.texture.invert);
    write_descriptor_bool_item(writer, "Algn", bevel.texture.link_with_layer);
    write_descriptor_unit_float_item(writer, "Scl ", {'#', 'P', 'r', 'c'}, bevel.texture.scale * 100.0);
    write_descriptor_unit_float_item(writer, "textureDepth", {'#', 'P', 'r', 'c'},
                                     bevel.texture.depth * 100.0);
    write_descriptor_item_header(writer, "Ptrn", {'O', 'b', 'j', 'c'});
    write_descriptor_object_header(writer, "", "Ptrn", 2);
    write_descriptor_text_item(writer, "Nm  ", bevel.texture.pattern_name);
    write_descriptor_text_item(writer, "Idnt", bevel.texture.pattern_id);
    write_descriptor_item_header(writer, "phase", {'O', 'b', 'j', 'c'});
    write_descriptor_object_header(writer, "", "Pnt ", 2);
    write_descriptor_double_item(writer, "Hrzn", bevel.texture.phase_x);
    write_descriptor_double_item(writer, "Vrtc", bevel.texture.phase_y);
  }
}

void write_satin_descriptor(BigEndianWriter& writer, const LayerSatin& satin) {
  // Photoshop 2026's native Satin shape. MpgS is the Linear contour: custom
  // curves and AntA remain byte-preserved with untouched imported lfx2 data,
  // but an edited modeled Satin deliberately regenerates with AntA off.
  write_descriptor_object_header(writer, "", "ChFX", 12);
  write_descriptor_bool_item(writer, "enab", satin.enabled);
  write_descriptor_bool_item(writer, "present", true);
  write_descriptor_bool_item(writer, "showInDialog", true);
  write_blend_mode_descriptor_item(writer, "Md  ", satin.blend_mode);
  write_native_rgb_color_descriptor_item(writer, "Clr ", satin.color);
  write_descriptor_bool_item(writer, "AntA", false);
  write_descriptor_bool_item(writer, "Invr", satin.invert);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, satin.opacity * 100.0);
  write_descriptor_unit_float_item(writer, "lagl", {'#', 'A', 'n', 'g'}, satin.angle_degrees);
  write_descriptor_unit_float_item(writer, "Dstn", {'#', 'P', 'x', 'l'}, satin.distance);
  write_descriptor_unit_float_item(writer, "blur", {'#', 'P', 'x', 'l'}, satin.size);
  write_descriptor_item_header(writer, "MpgS", {'O', 'b', 'j', 'c'});
  write_descriptor_object_header(writer, "", "ShpC", 2);
  write_descriptor_text_item(writer, "Nm  ", "$$$/Contours/Defaults/Linear=Linear");
  write_descriptor_item_header(writer, "Crv ", {'V', 'l', 'L', 's'});
  writer.write_u32(2);
  for (const auto point : {0.0, 255.0}) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_descriptor_object_header(writer, "", "CrPt", 2);
    write_descriptor_double_item(writer, "Hrzn", point);
    write_descriptor_double_item(writer, "Vrtc", point);
  }
}

void write_pattern_descriptor(BigEndianWriter& writer, const LayerPatternOverlay& pattern) {
  // Photoshop 2026's native 10-item patternFill layout (COM capture; the GrFl
  // shape-sensitivity precedent says mirror PS exactly).
  write_descriptor_object_header(writer, "", "patternFill", 10);
  write_descriptor_bool_item(writer, "enab", pattern.enabled);
  write_descriptor_bool_item(writer, "present", true);
  write_descriptor_bool_item(writer, "showInDialog", true);
  write_blend_mode_descriptor_item(writer, "Md  ", pattern.blend_mode);
  write_descriptor_unit_float_item(writer, "Opct", {'#', 'P', 'r', 'c'}, pattern.opacity * 100.0);
  write_descriptor_item_header(writer, "Ptrn", {'O', 'b', 'j', 'c'});
  write_descriptor_object_header(writer, "", "Ptrn", 2);
  write_descriptor_text_item(writer, "Nm  ", pattern.pattern_name);
  write_descriptor_text_item(writer, "Idnt", pattern.pattern_id);
  write_descriptor_unit_float_item(writer, "Angl", {'#', 'A', 'n', 'g'}, pattern.angle_degrees);
  write_descriptor_unit_float_item(writer, "Scl ", {'#', 'P', 'r', 'c'}, pattern.scale * 100.0);
  write_descriptor_bool_item(writer, "Algn", pattern.link_with_layer);
  write_descriptor_item_header(writer, "phase", {'O', 'b', 'j', 'c'});
  write_descriptor_object_header(writer, "", "Pnt ", 2);
  write_descriptor_double_item(writer, "Hrzn", pattern.phase_x);
  write_descriptor_double_item(writer, "Vrtc", pattern.phase_y);
}

template <typename Effect, typename Writer>
void write_layer_effect_item(BigEndianWriter& writer, std::string_view single_key, std::string_view multi_key,
                             const std::vector<Effect>& effects, Writer write_effect) {
  if (effects.empty()) {
    return;
  }
  if (effects.size() == 1U) {
    write_descriptor_item_header(writer, single_key, {'O', 'b', 'j', 'c'});
    write_effect(writer, effects.front());
    return;
  }
  write_descriptor_item_header(writer, multi_key, {'V', 'l', 'L', 's'});
  writer.write_u32(checked_u32(effects.size(), "layer effect list"));
  for (const auto& effect : effects) {
    write_signature(writer, {'O', 'b', 'j', 'c'});
    write_effect(writer, effect);
  }
}

}  // namespace

// Exposed for the .asl style-preset codec (psd/psd_layer_effects.hpp): the same
// 'BlnM' conversions the lfx2 effect descriptors use.
BlendMode blend_mode_from_lfx2_enum(std::string_view value) {
  return blend_mode_from_descriptor_enum(value, std::array<char, 4>{'n', 'o', 'r', 'm'});
}

std::string_view blend_mode_lfx2_string(BlendMode mode) {
  return blend_mode_descriptor_value(mode);
}

// Exposed for the .asl style-preset codec (psd/psd_layer_effects.hpp). The
// payload after its 8-byte header is exactly the serialized effects descriptor
// an .asl 'Lefx' object embeds.
std::vector<std::uint8_t> photoshop_lfx2_layer_style_payload(const LayerStyle& style) {
  BigEndianWriter payload;
  payload.write_u32(0);   // object effects version
  payload.write_u32(16);  // descriptor version

  std::uint32_t item_count = 2;
  const auto add_item_if_present = [&item_count](const auto& effects) {
    if (!effects.empty()) {
      ++item_count;
    }
  };
  add_item_if_present(style.drop_shadows);
  add_item_if_present(style.inner_shadows);
  add_item_if_present(style.outer_glows);
  add_item_if_present(style.inner_glows);
  add_item_if_present(style.satins);
  add_item_if_present(style.bevels);
  add_item_if_present(style.color_overlays);
  add_item_if_present(style.gradient_fills);
  add_item_if_present(style.pattern_overlays);
  add_item_if_present(style.strokes);

  write_descriptor_object_header(payload, "", "null", item_count);
  write_descriptor_unit_float_item(payload, "Scl ", {'#', 'P', 'r', 'c'}, 100.0);
  write_descriptor_bool_item(payload, "masterFXSwitch", style.effects_visible);
  write_layer_effect_item(payload, "DrSh", "dropShadowMulti", style.drop_shadows, write_drop_shadow_descriptor);
  write_layer_effect_item(payload, "IrSh", "innerShadowMulti", style.inner_shadows, write_inner_shadow_descriptor);
  write_layer_effect_item(payload, "OrGl", "outerGlowMulti", style.outer_glows, write_outer_glow_descriptor);
  write_layer_effect_item(payload, "IrGl", "innerGlowMulti", style.inner_glows, write_inner_glow_descriptor);
  write_layer_effect_item(payload, "ChFX", "chromeFXMulti", style.satins, write_satin_descriptor);
  write_layer_effect_item(payload, "ebbl", "bevelEmbossMulti", style.bevels, write_bevel_emboss_descriptor);
  write_layer_effect_item(payload, "SoFi", "solidFillMulti", style.color_overlays, write_color_overlay_descriptor);
  write_layer_effect_item(payload, "GrFl", "gradientFillMulti", style.gradient_fills, write_gradient_fill_descriptor);
  write_layer_effect_item(payload, "patternFill", "patternFillMulti", style.pattern_overlays, write_pattern_descriptor);
  write_layer_effect_item(payload, "FrFX", "frameFXMulti", style.strokes, write_stroke_descriptor);
  return payload.bytes();
}

}  // namespace patchy::psd
