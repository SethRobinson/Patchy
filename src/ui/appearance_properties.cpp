#include "ui/appearance_properties.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_compound.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace patchy::ui {
namespace {
// Properties are assembled from type-erased parts so the property machinery
// instantiates a handful of times per Settings type instead of once per
// field: a Locator finds a field's bytes (null when its owner is absent: a
// paint of another kind, an effect slot the target lacks) and a ValueOps
// table supplies the field type's equality, copy and assignment. Field types
// are still checked at registration (`field_of<&Owner::member>` fixes the
// member and `value_ops` is chosen from its type); only the stored property is
// erased. Keep it that way: a generic lambda per field made this unit take
// 40 s to compile alone and 100 s inside a parallel build, and even one
// std::function per closure type costs about fifteen generated functions
// (docs/batch-appearance.md). Locators give a read-only view; writers receive
// a mutable Settings, so dropping that view's const is sound.
template <typename Object>
void destroy(const void* object) { delete static_cast<const Object*>(object); }

// An immutable shared callable `const void*(const Settings&)`: two generated
// functions per callable type.
template <typename Settings>
class Locator {
 public:
  // Implicit on purpose: any callable (a function pointer included) is a locator.
  // The `const void*` casts keep shared_ptr's deleter constructor to one instantiation.
  template <typename Callable>
    requires(!std::is_same_v<Callable, Locator>)
  Locator(Callable callable)
      : state_(static_cast<const void*>(new Callable(std::move(callable))), &destroy<Callable>),
        call_(&invoke<Callable>) {}
  const void* operator()(const Settings& value) const { return call_(state_.get(), value); }

 private:
  template <typename Callable>
  static const void* invoke(const void* state, const Settings& value) {
    return (*static_cast<const Callable*>(state))(value);
  }
  std::shared_ptr<const void> state_;
  const void* (*call_)(const void* state, const Settings& value);
};

struct ValueOps {
  bool (*equal)(const void* left, const void* right);
  std::shared_ptr<const void> (*copy)(const void* value);
  void (*assign)(void* target, const void* value);
};

template <typename Value>
const ValueOps& value_ops() {
  static constexpr ValueOps ops{
      [](const void* left, const void* right) {
        return *static_cast<const Value*>(left) == *static_cast<const Value*>(right);
      },
      [](const void* value) {
        return std::shared_ptr<const void>(
            static_cast<const void*>(new Value(*static_cast<const Value*>(value))), &destroy<Value>);
      },
      [](void* target, const void* value) {
        *static_cast<Value*>(target) = *static_cast<const Value*>(value);
      }};
  return ops;
}

// Equality compares the located values (two absent fields are equal, one
// absent is not); capture copies the source value and the edit writes it to
// a target that has the field, so a parameter write never creates an effect.
template <typename Settings>
AppearanceProperty<Settings> erased_property(std::string key, Locator<Settings> locate, const ValueOps& ops) {
  const ValueOps* const value = &ops;
  return {key,
          [locate, value](const Settings& a, const Settings& b) {
            const void* left = locate(a);
            const void* right = locate(b);
            return left && right ? value->equal(left, right) : left == right;
          },
          [key, locate, value](const Settings& source) -> AppearanceEdit<Settings> {
            const void* found = locate(source);
            if (!found) return {key, [](Settings&) {}};
            return {key, [copy = value->copy(found), locate, value](Settings& target) {
              if (const void* field = locate(target)) value->assign(const_cast<void*>(field), copy.get());
            }};
          }};
}

template <typename Owner, typename Value> Owner owner_type(Value Owner::*);
template <typename Owner, typename Value> Value value_type(Value Owner::*);

// The bytes of one member of an erased owner.
template <auto Member>
const void* field_of(const void* owner) {
  using Owner = decltype(owner_type(Member));
  return &(static_cast<const Owner*>(owner)->*Member);
}

template <typename Settings>
struct MemberLocator {
  Locator<Settings> owner;
  const void* (*member)(const void* owner);
  const void* operator()(const Settings& value) const {
    const void* found = owner(value);
    return found ? member(found) : nullptr;
  }
};

template <typename Settings>
Locator<Settings> member_locator(Locator<Settings> owner, const void* (*member)(const void*)) {
  return MemberLocator<Settings>{std::move(owner), member};
}

template <typename Settings>
void add_erased(std::vector<AppearanceProperty<Settings>>& result, std::string key, const Locator<Settings>& owner,
                const void* (*member)(const void*), const ValueOps& ops) {
  result.push_back(erased_property(std::move(key), member_locator(owner, member), ops));
}

// One member of the located owner, under the full `key`.
template <auto Member, typename Settings>
void add_member(std::vector<AppearanceProperty<Settings>>& result, std::string key, const Locator<Settings>& owner) {
  add_erased(result, std::move(key), owner, &field_of<Member>, value_ops<decltype(value_type(Member))>());
}

template <typename Value>
Value& writable(const Value& located) { return const_cast<Value&>(located); }

template <typename Settings>
void add_gradient(std::vector<AppearanceProperty<Settings>>& result, const std::string& key,
                  const Locator<Settings>& gradient) {
  // The GradientDefinition slice (name, form, stops, noise), then the
  // placement members LayerStyleGradient adds.
  add_erased(result, key + ".definition", gradient, +[](const void* value) -> const void* {
    return static_cast<const GradientDefinition*>(static_cast<const LayerStyleGradient*>(value));
  }, value_ops<GradientDefinition>());
#define GRADIENT_FIELD(member) add_member<&LayerStyleGradient::member>(result, key + "." #member, gradient)
  GRADIENT_FIELD(type);
  GRADIENT_FIELD(angle_degrees);
  GRADIENT_FIELD(scale);
  GRADIENT_FIELD(reverse);
  GRADIENT_FIELD(dither);
  GRADIENT_FIELD(interpolation);
  GRADIENT_FIELD(align_with_layer);
  GRADIENT_FIELD(offset_x_percent);
  GRADIENT_FIELD(offset_y_percent);
#undef GRADIENT_FIELD
}

using ShapePaint = const VectorFill* (*)(const ShapeAppearanceSettings&);

// Type-specific paint parameters reach only paints of that kind.
struct PaintOfKind {
  ShapePaint paint;
  VectorFillKind kind;
  const void* operator()(const ShapeAppearanceSettings& value) const {
    const VectorFill* found = paint(value);
    return found->kind == kind ? found : nullptr;
  }
};

void add_paint(std::vector<AppearanceProperty<ShapeAppearanceSettings>>& result, const std::string& key,
               ShapePaint paint) {
  using Settings = ShapeAppearanceSettings;
  add_member<&VectorFill::kind>(result, key + ".kind", Locator<Settings>(paint));
  result.back().capture = [paint, key](const Settings& source) -> AppearanceEdit<Settings> {
    const VectorFill recipe = *paint(source);
    return {key + ".kind", [paint, recipe](Settings& target) {
      auto& next = writable(*paint(target));
      if (next.kind != recipe.kind) next = recipe;
      else if (recipe.kind == VectorFillKind::Solid) next.color = recipe.color;
      else if (recipe.kind == VectorFillKind::Gradient)
        static_cast<GradientDefinition&>(next.gradient) = recipe.gradient;
      else if (recipe.kind == VectorFillKind::Pattern) {
        next.pattern_id = recipe.pattern_id;
        next.pattern_name = recipe.pattern_name;
      }
    }};
  };
  result.push_back(appearance_property<Settings, RgbColor>(
      key + ".color", [paint](const Settings& value) { return &paint(value)->color; },
      [paint](Settings& target, const RgbColor& value) {
        auto& next = writable(*paint(target));
        next.kind = VectorFillKind::Solid;
        next.color = value;
      }));
  add_gradient(result, key + ".gradient",
               member_locator(Locator<Settings>(PaintOfKind{paint, VectorFillKind::Gradient}),
                              &field_of<&VectorFill::gradient>));
  const Locator<Settings> pattern = PaintOfKind{paint, VectorFillKind::Pattern};
#define PAINT_FIELD(member) add_member<&VectorFill::member>(result, key + "." #member, pattern)
  PAINT_FIELD(pattern_id);
  PAINT_FIELD(pattern_name);
  PAINT_FIELD(pattern_scale);
  PAINT_FIELD(pattern_angle_degrees);
  PAINT_FIELD(pattern_linked);
  PAINT_FIELD(pattern_phase_x);
  PAINT_FIELD(pattern_phase_y);
#undef PAINT_FIELD
}

using StyleProperties = std::vector<AppearanceProperty<LayerStyleSettings>>;

// One LayerStyle effect list (`Effects` is its member pointer), erased behind
// EffectSlot so the `.enabled` capture, which copies a whole effect, has one
// closure type for every effect kind.
template <auto Effects>
struct EffectList {
  using Effect = typename decltype(value_type(Effects))::value_type;

  static std::size_t size(const LayerStyleSettings& value) { return (value.style.*Effects).size(); }
  // The instance at `index`; null when the target has fewer instances.
  static const void* at(const LayerStyleSettings& value, std::size_t index) {
    const auto& list = value.style.*Effects;
    return index < list.size() ? &list[index] : nullptr;
  }
  // The instance at `index`, or a default instance when there is none.
  static std::shared_ptr<const void> copy(const LayerStyleSettings& value, std::size_t index) {
    const auto* effect = static_cast<const Effect*>(at(value, index));
    return {static_cast<const void*>(new Effect(effect ? *effect : Effect{})), &destroy<Effect>};
  }
  static bool enabled(const void* effect) { return static_cast<const Effect*>(effect)->enabled; }
  static void set_enabled(LayerStyleSettings& target, std::size_t index, bool enabled) {
    (target.style.*Effects)[index].enabled = enabled;
  }
  // Grows the list to hold `index` and stores the instance there.
  static void store(LayerStyleSettings& target, std::size_t index, const void* effect) {
    auto& list = target.style.*Effects;
    if (list.size() <= index) list.resize(index + 1);
    list[index] = *static_cast<const Effect*>(effect);
  }
};

struct EffectSlot {
  std::size_t (*size)(const LayerStyleSettings& value);
  const void* (*at)(const LayerStyleSettings& value, std::size_t index);
  std::shared_ptr<const void> (*copy)(const LayerStyleSettings& value, std::size_t index);
  bool (*enabled)(const void* effect);
  void (*set_enabled)(LayerStyleSettings& target, std::size_t index, bool enabled);
  void (*store)(LayerStyleSettings& target, std::size_t index, const void* effect);
  std::size_t index;

  const void* operator()(const LayerStyleSettings& value) const { return at(value, index); }
};

template <auto Effects>
EffectSlot effect_slot(std::size_t index) {
  using List = EffectList<Effects>;
  return {&List::size, &List::at, &List::copy, &List::enabled, &List::set_enabled, &List::store, index};
}

// Registration context for one effect instance's properties under `key`
// (`<prefix>.<index>`). The per-type lists below name each editable member in
// its persisted key order; these keys are compatibility contracts.
template <typename Effect>
struct EffectFields {
  StyleProperties& result;
  std::string key;
  EffectSlot slot;
  Locator<LayerStyleSettings> effect;

  template <auto Member>
  void add(const char* name) const {
    static_assert(std::is_same_v<decltype(owner_type(Member)), Effect>, "member of another effect type");
    add_member<Member>(result, key + "." + name, effect);
  }
};

// Explicit enablement creates a missing instance from the displayed template
// and otherwise only flips the flag. One closure serves every effect kind.
std::function<AppearanceEdit<LayerStyleSettings>(const LayerStyleSettings&)> enabled_capture(
    const EffectSlot& slot, const std::string& key) {
  return [slot, key](const LayerStyleSettings& source) -> AppearanceEdit<LayerStyleSettings> {
    return {key, [slot, recipe = slot.copy(source, slot.index)](LayerStyleSettings& target) {
      const bool enabled = slot.enabled(recipe.get());
      if (slot.index >= slot.size(target)) {
        if (enabled) slot.store(target, slot.index, recipe.get());
      } else {
        slot.set_enabled(target, slot.index, enabled);
      }
    }};
  };
}

template <typename Effect>
void add_effect_enabled(const EffectFields<Effect>& fields) {
  const auto key = fields.key + ".enabled";
  add_member<&Effect::enabled>(fields.result, key, fields.effect);
  fields.result.back().capture = enabled_capture(fields.slot, key);
}

// Enabling a Bevel sub-option on a target without the bevel creates it from
// the template with the bevel itself and the other sub-option switched off.
template <auto Option>
void add_bevel_option_enabled(const EffectFields<LayerBevelEmboss>& fields, const std::string& option_key) {
  using Sub = decltype(value_type(Option));
  const auto key = option_key + ".enabled";
  add_member<&Sub::enabled>(fields.result, key, member_locator(fields.effect, &field_of<Option>));
  fields.result.back().capture =
      [slot = fields.slot, key](const LayerStyleSettings& source) -> AppearanceEdit<LayerStyleSettings> {
    const auto* effect = static_cast<const LayerBevelEmboss*>(slot(source));
    const auto recipe = effect ? *effect : LayerBevelEmboss{};
    return {key, [index = slot.index, recipe](LayerStyleSettings& target) {
      auto& bevels = target.style.bevels;
      if (index >= bevels.size()) {
        if (!(recipe.*Option).enabled) return;
        bevels.resize(index + 1);
        bevels[index] = recipe;
        bevels[index].enabled = false;
        bevels[index].contour.enabled = false;
        bevels[index].texture.enabled = false;
        (bevels[index].*Option).enabled = true;
      } else {
        (bevels[index].*Option).enabled = (recipe.*Option).enabled;
      }
    }};
  };
}

#define EFFECT_FIELD(member) fields.add<&Effect::member>(#member)

void add_effect_fields(const EffectFields<LayerDropShadow>& fields) {
  using Effect = LayerDropShadow;
  EFFECT_FIELD(blend_mode);
  EFFECT_FIELD(color);
  EFFECT_FIELD(opacity);
  EFFECT_FIELD(angle_degrees);
  EFFECT_FIELD(distance);
  EFFECT_FIELD(spread);
  EFFECT_FIELD(size);
  EFFECT_FIELD(layer_conceals);
  EFFECT_FIELD(use_global_light);
  EFFECT_FIELD(continuous);
  EFFECT_FIELD(fade);
  EFFECT_FIELD(contour);
  EFFECT_FIELD(anti_aliased);
}

void add_effect_fields(const EffectFields<LayerInnerShadow>& fields) {
  using Effect = LayerInnerShadow;
  EFFECT_FIELD(blend_mode);
  EFFECT_FIELD(color);
  EFFECT_FIELD(opacity);
  EFFECT_FIELD(angle_degrees);
  EFFECT_FIELD(distance);
  EFFECT_FIELD(choke);
  EFFECT_FIELD(size);
  EFFECT_FIELD(use_global_light);
}

void add_effect_fields(const EffectFields<LayerOuterGlow>& fields) {
  using Effect = LayerOuterGlow;
  EFFECT_FIELD(blend_mode);
  EFFECT_FIELD(color);
  EFFECT_FIELD(opacity);
  EFFECT_FIELD(spread);
  EFFECT_FIELD(size);
  EFFECT_FIELD(technique);
  EFFECT_FIELD(range);
}

void add_effect_fields(const EffectFields<LayerInnerGlow>& fields) {
  using Effect = LayerInnerGlow;
  EFFECT_FIELD(blend_mode);
  EFFECT_FIELD(color);
  EFFECT_FIELD(opacity);
  EFFECT_FIELD(choke);
  EFFECT_FIELD(size);
  EFFECT_FIELD(technique);
  EFFECT_FIELD(range);
  EFFECT_FIELD(source);
}

void add_effect_fields(const EffectFields<LayerColorOverlay>& fields) {
  using Effect = LayerColorOverlay;
  EFFECT_FIELD(blend_mode);
  EFFECT_FIELD(color);
  EFFECT_FIELD(opacity);
}

void add_effect_fields(const EffectFields<LayerGradientFill>& fields) {
  using Effect = LayerGradientFill;
  EFFECT_FIELD(blend_mode);
  EFFECT_FIELD(opacity);
  add_gradient(fields.result, fields.key + ".gradient",
               member_locator(fields.effect, &field_of<&Effect::gradient>));
}

void add_effect_fields(const EffectFields<LayerPatternOverlay>& fields) {
  using Effect = LayerPatternOverlay;
  EFFECT_FIELD(blend_mode);
  EFFECT_FIELD(opacity);
  EFFECT_FIELD(angle_degrees);
  EFFECT_FIELD(scale);
  EFFECT_FIELD(pattern_name);
  EFFECT_FIELD(pattern_id);
  EFFECT_FIELD(link_with_layer);
  EFFECT_FIELD(phase_x);
  EFFECT_FIELD(phase_y);
}

void add_effect_fields(const EffectFields<LayerStroke>& fields) {
  using Effect = LayerStroke;
  EFFECT_FIELD(blend_mode);
  EFFECT_FIELD(color);
  EFFECT_FIELD(opacity);
  EFFECT_FIELD(size);
  EFFECT_FIELD(position);
  EFFECT_FIELD(uses_gradient);
  EFFECT_FIELD(overprint);
  // A solid stroke's stored gradient recipe stays out of parameter patches.
  add_gradient(fields.result, fields.key + ".gradient",
               member_locator(fields.effect, +[](const void* effect) -> const void* {
                 const auto& stroke = *static_cast<const LayerStroke*>(effect);
                 return stroke.uses_gradient ? &stroke.gradient : nullptr;
               }));
}

void add_effect_fields(const EffectFields<LayerBevelEmboss>& fields) {
  using Effect = LayerBevelEmboss;
  EFFECT_FIELD(angle_degrees);
  EFFECT_FIELD(size);
  EFFECT_FIELD(use_global_light);
  EFFECT_FIELD(technique);
  EFFECT_FIELD(highlight_blend_mode);
  EFFECT_FIELD(highlight_color);
  EFFECT_FIELD(highlight_opacity);
  EFFECT_FIELD(shadow_blend_mode);
  EFFECT_FIELD(shadow_color);
  EFFECT_FIELD(shadow_opacity);
  EFFECT_FIELD(altitude_degrees);
  EFFECT_FIELD(depth);
  EFFECT_FIELD(direction_up);
  EFFECT_FIELD(style);
  EFFECT_FIELD(soften);
  EFFECT_FIELD(gloss_contour);
  EFFECT_FIELD(gloss_anti_aliased);
  const auto contour_key = fields.key + ".contour";
  const auto contour = member_locator(fields.effect, &field_of<&Effect::contour>);
  add_bevel_option_enabled<&Effect::contour>(fields, contour_key);
#define CONTOUR_FIELD(member) add_member<&LayerBevelContour::member>(fields.result, contour_key + "." #member, contour)
  CONTOUR_FIELD(contour);
  CONTOUR_FIELD(anti_aliased);
  CONTOUR_FIELD(range);
#undef CONTOUR_FIELD
  const auto texture_key = fields.key + ".texture";
  const auto texture = member_locator(fields.effect, &field_of<&Effect::texture>);
  add_bevel_option_enabled<&Effect::texture>(fields, texture_key);
#define TEXTURE_FIELD(member) add_member<&LayerBevelTexture::member>(fields.result, texture_key + "." #member, texture)
  TEXTURE_FIELD(pattern_name);
  TEXTURE_FIELD(pattern_id);
  TEXTURE_FIELD(scale);
  TEXTURE_FIELD(depth);
  TEXTURE_FIELD(invert);
  TEXTURE_FIELD(link_with_layer);
  TEXTURE_FIELD(phase_x);
  TEXTURE_FIELD(phase_y);
#undef TEXTURE_FIELD
}

void add_effect_fields(const EffectFields<LayerSatin>& fields) {
  using Effect = LayerSatin;
  EFFECT_FIELD(blend_mode);
  EFFECT_FIELD(color);
  EFFECT_FIELD(opacity);
  EFFECT_FIELD(angle_degrees);
  EFFECT_FIELD(distance);
  EFFECT_FIELD(size);
  EFFECT_FIELD(invert);
  EFFECT_FIELD(unsupported_contour_options);
}

#undef EFFECT_FIELD


// One property set per effect slot the reference carries (at least one, so an
// absent effect can still be enabled from its template).
template <auto Effects>
void add_effects(StyleProperties& result, const LayerStyleSettings& reference, const std::string& prefix) {
  using Effect = typename EffectList<Effects>::Effect;
  const auto count = std::max<std::size_t>(1, EffectList<Effects>::size(reference));
  for (std::size_t index = 0; index < count; ++index) {
    const EffectSlot slot = effect_slot<Effects>(index);
    const EffectFields<Effect> fields{result, prefix + "." + std::to_string(index), slot, slot};
    add_effect_enabled(fields);
    add_effect_fields(fields);
  }
}
}  // namespace

ShapeAppearanceSettings shape_appearance_settings(const Layer& layer) {
  ShapeAppearanceSettings result;
  const auto* content = layer.vector_shape();
  if (!content) return result;
  result.fill = content->fill;
  result.stroke = content->stroke;
  result.layer_opacity = layer.opacity();
  result.fill_opacity = layer.fill_opacity();
  result.feather = content->feather;
  result.density = content->density;
  if (content->origination.size() == 1 && content->origination.front().raw_descriptor.empty() &&
      content->parts.empty()) {
    const auto& params = content->origination.front();
    if ((params.kind == LiveShapeKind::Rectangle || params.kind == LiveShapeKind::RoundedRectangle ||
         params.kind == LiveShapeKind::Ellipse || params.kind == LiveShapeKind::Line) &&
        std::all_of(content->path.subpaths.begin(), content->path.subpaths.end(),
                    [&](const auto& path) { return path.shape_group == params.index; }))
      result.geometry = params;
  }
  return result;
}

LayerStyleSettings layer_style_settings(const Layer& layer) {
  return {static_cast<int>(std::lround(layer.opacity() * 100.0F)), layer.blend_mode(),
          layer.layer_style(), layer.blend_if(), false,
          static_cast<int>(std::lround(layer.fill_opacity() * 100.0F)), layer.restricted_channels(),
          layer_knockout_setting(layer).value_or(0U)};
}

bool appearance_has_editable_radii(const ShapeAppearanceSettings& settings) {
  return settings.geometry && (settings.geometry->kind == LiveShapeKind::Rectangle ||
                               settings.geometry->kind == LiveShapeKind::RoundedRectangle);
}

std::vector<AppearanceProperty<ShapeAppearanceSettings>> shape_appearance_properties() {
  using Settings = ShapeAppearanceSettings;
  std::vector<AppearanceProperty<Settings>> result;
  add_paint(result, "fill", [](const Settings& value) { return &value.fill; });
  add_paint(result, "stroke.content", [](const Settings& value) { return &value.stroke.content; });
  const Locator<Settings> self = +[](const Settings& value) -> const void* { return &value; };
  const Locator<Settings> stroke = +[](const Settings& value) -> const void* { return &value.stroke; };
#define STROKE_FIELD(member) add_member<&VectorStroke::member>(result, "stroke." #member, stroke)
#define SHAPE_FIELD(member) add_member<&Settings::member>(result, #member, self)
  STROKE_FIELD(enabled);
  STROKE_FIELD(fill_enabled);
  STROKE_FIELD(width);
  STROKE_FIELD(dash_offset);
  STROKE_FIELD(miter_limit);
  STROKE_FIELD(cap);
  STROKE_FIELD(join);
  STROKE_FIELD(alignment);
  STROKE_FIELD(scale_lock);
  STROKE_FIELD(stroke_adjust);
  STROKE_FIELD(dashes);
  STROKE_FIELD(blend_mode);
  STROKE_FIELD(opacity);
  STROKE_FIELD(resolution);
  SHAPE_FIELD(layer_opacity);
  SHAPE_FIELD(fill_opacity);
  SHAPE_FIELD(feather);
  SHAPE_FIELD(density);
#undef SHAPE_FIELD
#undef STROKE_FIELD
  for (std::size_t corner = 0; corner < 4; ++corner) {
    result.push_back(appearance_property<Settings, double>(
        "radius." + std::to_string(corner),
        [corner](const Settings& value) -> const double* {
          return appearance_has_editable_radii(value) ? &value.geometry->corner_radii[corner] : nullptr;
        },
        [corner](Settings& target, double radius) {
          if (!appearance_has_editable_radii(target)) return;
          target.geometry->corner_radii[corner] = radius;
          if (radius > 0.0) target.geometry->kind = LiveShapeKind::RoundedRectangle;
        }));
  }
  auto geometry = appearance_property<Settings, std::optional<LiveShapeParams>>(
      "geometry", [](const Settings& value) { return &value.geometry; },
      [](Settings& target, const std::optional<LiveShapeParams>& value) { target.geometry = value; });
  geometry.equal = [](const Settings& a, const Settings& b) {
    auto left = a.geometry, right = b.geometry;
    for (auto* params : {&left, &right}) {
      if (*params) {
        (*params)->corner_radii = {};
        if ((*params)->kind == LiveShapeKind::RoundedRectangle) (*params)->kind = LiveShapeKind::Rectangle;
      }
    }
    return left == right;
  };
  result.push_back(std::move(geometry));
  return result;
}

std::vector<AppearanceProperty<LayerStyleSettings>> layer_style_properties(const LayerStyleSettings& reference) {
  using Settings = LayerStyleSettings;
  std::vector<AppearanceProperty<Settings>> result;
  const Locator<Settings> self = +[](const Settings& value) -> const void* { return &value; };
  const Locator<Settings> style = +[](const Settings& value) -> const void* { return &value.style; };
#define STYLE_FIELD(member) add_member<&Settings::member>(result, #member, self)
#define LAYER_STYLE_FIELD(member) add_member<&LayerStyle::member>(result, "style." #member, style)
  STYLE_FIELD(opacity);
  result.back().capture = [](const Settings& source) -> AppearanceEdit<Settings> {
    return {"opacity", [value = source.opacity](Settings& target) {
      target.opacity = value; target.opacity_edited = true;
    }};
  };
  STYLE_FIELD(fill_opacity);
  result.back().capture = [](const Settings& source) -> AppearanceEdit<Settings> {
    return {"fill_opacity", [value = source.fill_opacity](Settings& target) {
      target.fill_opacity = value; target.fill_opacity_edited = true;
    }};
  };
  STYLE_FIELD(blend_mode);
  STYLE_FIELD(replace_unsupported_blend_if);
  LAYER_STYLE_FIELD(effects_visible);
  LAYER_STYLE_FIELD(layer_mask_hides_effects);
  LAYER_STYLE_FIELD(vector_mask_hides_effects);
  STYLE_FIELD(knockout);
  LAYER_STYLE_FIELD(blend_interior_elements);
  LAYER_STYLE_FIELD(blend_clipped_elements);
#undef LAYER_STYLE_FIELD
#undef STYLE_FIELD
  for (unsigned channel = 0; channel < 3; ++channel) {
    const auto key = "restricted_channels." + std::to_string(channel);
    const auto mask = static_cast<std::uint8_t>(1U << channel);
    result.push_back({key, [mask](const Settings& a, const Settings& b) {
      return (a.restricted_channels & mask) == (b.restricted_channels & mask);
    }, [mask, key](const Settings& source) -> AppearanceEdit<Settings> {
      const auto value = source.restricted_channels & mask;
      return {key, [mask, value](Settings& target) {
        target.restricted_channels = static_cast<std::uint8_t>((target.restricted_channels & ~mask) | value);
      }};
    }});
  }
  for (std::size_t channel = 0; channel < reference.blend_if.channels.size(); ++channel) {
    const auto add_side = [&](const char* name, BlendIfThresholds BlendIfChannelRanges::*side) {
      const auto key = "blend_if." + std::to_string(channel) + "." + name;
      const Locator<Settings> thresholds = [channel, side](const Settings& value) -> const void* {
        return &(value.blend_if.channels[channel].*side);
      };
#define BLEND_FIELD(member) add_member<&BlendIfThresholds::member>(result, key + "." #member, thresholds)
      BLEND_FIELD(black_low);
      BLEND_FIELD(black_high);
      BLEND_FIELD(white_low);
      BLEND_FIELD(white_high);
#undef BLEND_FIELD
    };
    add_side("this_layer", &BlendIfChannelRanges::this_layer);
    add_side("underlying_layer", &BlendIfChannelRanges::underlying_layer);
  }
  add_effects<&LayerStyle::drop_shadows>(result, reference, "drop_shadows");
  add_effects<&LayerStyle::inner_shadows>(result, reference, "inner_shadows");
  add_effects<&LayerStyle::outer_glows>(result, reference, "outer_glows");
  add_effects<&LayerStyle::inner_glows>(result, reference, "inner_glows");
  add_effects<&LayerStyle::color_overlays>(result, reference, "color_overlays");
  add_effects<&LayerStyle::gradient_fills>(result, reference, "gradient_fills");
  add_effects<&LayerStyle::pattern_overlays>(result, reference, "pattern_overlays");
  add_effects<&LayerStyle::strokes>(result, reference, "strokes");
  add_effects<&LayerStyle::bevels>(result, reference, "bevels");
  add_effects<&LayerStyle::satins>(result, reference, "satins");
  return result;
}

bool shape_appearance_equal(const ShapeAppearanceSettings& a, const ShapeAppearanceSettings& b) {
  return a.fill == b.fill && a.stroke == b.stroke && a.geometry == b.geometry &&
         a.layer_opacity == b.layer_opacity && a.fill_opacity == b.fill_opacity &&
         a.feather == b.feather && a.density == b.density;
}

bool layer_style_settings_equal(const LayerStyleSettings& a, const LayerStyleSettings& b) {
  // Type-specific parameter patches skip solid strokes, but full replacement
  // still carries their stored gradient recipes, including disabled effects.
  if (a.style.strokes.size() != b.style.strokes.size()) return false;
  for (std::size_t i = 0; i < a.style.strokes.size(); ++i)
    if (a.style.strokes[i].gradient != b.style.strokes[i].gradient) return false;
  const auto left = layer_style_properties(a);
  const auto right = layer_style_properties(b);
  return std::all_of(left.begin(), left.end(), [&](const auto& field) { return field.equal(a, b); }) &&
         std::all_of(right.begin(), right.end(), [&](const auto& field) { return field.equal(a, b); });
}

bool shape_vector_appearance_equal(const VectorShapeContent& a, const VectorShapeContent& b) {
  if (a.fill != b.fill || a.stroke != b.stroke || a.path != b.path ||
      a.origination != b.origination || a.feather != b.feather || a.density != b.density ||
      a.parts.size() != b.parts.size()) return false;
  for (std::size_t i = 0; i < a.parts.size(); ++i)
    if (a.parts[i].fill != b.parts[i].fill || a.parts[i].stroke != b.parts[i].stroke) return false;
  return true;
}

ShapeAppearanceSettings apply_shape_appearance_edits(const ShapeAppearanceSettings& original,
                                                     const ShapeAppearanceSettings& edited) {
  if (!edited.preview_enabled) return original;
  auto result = edited.edits ? edited.edits->applied(original) : edited;
  result.edits = edited.edits;
  // Preview belongs to the transaction, not to an Apply All recipe captured
  // while preview was off. Compound parts replay this same edit log below.
  result.preview_enabled = edited.preview_enabled;
  return result;
}

LayerStyleSettings apply_layer_style_edits(const LayerStyleSettings& original,
                                           const LayerStyleSettings& edited) {
  if (!edited.preview_enabled) return original;
  return edited.edits ? edited.edits->applied(original) : edited;
}

VectorShapeContent assemble_shape_appearance(const VectorShapeContent& original,
                                             const ShapeAppearanceSettings& settings) {
  auto content = original;
  content.fill = settings.fill;
  content.stroke = settings.stroke;
  content.feather = settings.feather;
  content.density = settings.density;
  if (!settings.edits) update_vector_part_appearance(content, original.fill, original.stroke);
  else for (auto& part : content.parts) {
    ShapeAppearanceSettings baseline;
    baseline.fill = part.fill;
    baseline.stroke = part.stroke;
    const auto next = apply_shape_appearance_edits(baseline, settings);
    part.fill = next.fill;
    part.stroke = next.stroke;
  }
  if (settings.geometry && content.origination.size() == 1 &&
      *settings.geometry != content.origination.front()) {
    auto params = *settings.geometry;
    params.index = content.origination.front().index;
    const auto& prior = content.origination.front();
    if (params.left != prior.left || params.top != prior.top ||
        params.right != prior.right || params.bottom != prior.bottom)
      populate_live_shape_box_corners(params);
    auto paths = generate_live_shape_subpaths(params);
    if (params.kind == LiveShapeKind::Line) {
      VectorPath path;
      path.subpaths = paths;
      if (const auto bounds = path.bounds()) {
        params.left = bounds->left; params.top = bounds->top;
        params.right = bounds->right; params.bottom = bounds->bottom;
      }
    }
    auto op = PathCombineOp::Add;
    for (const auto& path : content.path.subpaths)
      if (path.shape_group == params.index) { op = path.op; break; }
    std::erase_if(content.path.subpaths, [&](const auto& path) { return path.shape_group == params.index; });
    for (auto& path : paths) {
      path.shape_group = params.index; path.op = op;
      content.path.subpaths.push_back(std::move(path));
    }
    content.origination.front() = params;
  }
  return content;
}
}  // namespace patchy::ui
