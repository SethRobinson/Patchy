#include "ui/tool_modifier_style.hpp"

#include "ui/app_settings.hpp"
#include "ui/canvas_types.hpp"
#include "ui/canvas_widget_shared.hpp"
#include "ui/modifier_names.hpp"

#include <QLatin1StringView>
#include <atomic>
#include <utility>

namespace patchy::ui {

namespace {

std::atomic<ToolModifierStyle> g_active_style{ToolModifierStyle::Patchy};

// The painting tools whose color pick moves to Ctrl, from each program's manual: GIMP's
// paint tools (Paintbrush, Pencil, Airbrush, Ink, Eraser) and Bucket Fill; Krita's Freehand
// Brush, whose engines cover Patchy's Brush, Mixer Brush, Smudge and Eraser. The rest keep
// Patchy's Alt pick (GIMP's Gradient uses Ctrl for 15-degree steps; neither has the others).
bool pick_uses_ctrl(CanvasTool tool, ToolModifierStyle style) noexcept {
  switch (style) {
    case ToolModifierStyle::Gimp:
      return tool == CanvasTool::Brush || tool == CanvasTool::Eraser || tool == CanvasTool::Fill;
    case ToolModifierStyle::Krita:
      return tool == CanvasTool::Brush || tool == CanvasTool::MixerBrush || tool == CanvasTool::Smudge ||
             tool == CanvasTool::Eraser;
    case ToolModifierStyle::Patchy:
      break;
  }
  return false;
}

bool is_retouch_source_tool(CanvasTool tool) noexcept {
  return tool == CanvasTool::Clone || tool == CanvasTool::Healing;
}

bool is_toning_swap_tool(CanvasTool tool) noexcept {
  return tool == CanvasTool::Dodge || tool == CanvasTool::Burn || tool == CanvasTool::BlurBrush ||
         tool == CanvasTool::SharpenBrush;
}

bool is_selection_tool(CanvasTool tool) noexcept {
  switch (tool) {
    case CanvasTool::Marquee:
    case CanvasTool::EllipticalMarquee:
    case CanvasTool::Lasso:
    case CanvasTool::PolygonLasso:
    case CanvasTool::MagneticLasso:
    case CanvasTool::MagicWand:
    case CanvasTool::QuickSelect:
    case CanvasTool::ObjectSelect:
    case CanvasTool::PatchTool:
    case CanvasTool::ContentAwareMove:
      return true;
    default:
      return false;
  }
}

}  // namespace

QString tool_modifier_style_token(ToolModifierStyle style) {
  switch (style) {
    case ToolModifierStyle::Gimp:
      return QString::fromLatin1(kGimpToolModifiersToken);
    case ToolModifierStyle::Krita:
      return QString::fromLatin1(kKritaToolModifiersToken);
    case ToolModifierStyle::Patchy:
      break;
  }
  return QString::fromLatin1(kPatchyToolModifiersToken);
}

ToolModifierStyle tool_modifier_style_from_token(QStringView token) {
  if (token == QLatin1StringView(kGimpToolModifiersToken)) {
    return ToolModifierStyle::Gimp;
  }
  if (token == QLatin1StringView(kKritaToolModifiersToken)) {
    return ToolModifierStyle::Krita;
  }
  return ToolModifierStyle::Patchy;
}

ToolModifierStyle builtin_set_tool_modifier_style(QStringView set_id) {
  // The built-in set ids ("gimp", "krita") double as their style's token.
  return tool_modifier_style_from_token(set_id);
}

ToolModifierStyle active_tool_modifier_style() noexcept {
  return g_active_style.load(std::memory_order_relaxed);
}

void set_active_tool_modifier_style(ToolModifierStyle style) noexcept {
  g_active_style.store(style, std::memory_order_relaxed);
}

ToolModifierStyle load_active_tool_modifier_style() {
  const auto settings = app_settings();
  const auto stored = settings.value(QStringLiteral("hotkeySets/toolModifiers"));
  if (stored.isValid()) {
    return tool_modifier_style_from_token(stored.toString());
  }
  // Settings from before the key existed: a built-in GIMP-style or Krita-style set chosen then
  // gets its tool modifiers now.
  return builtin_set_tool_modifier_style(settings.value(QStringLiteral("hotkeySets/active")).toString());
}

Qt::KeyboardModifier tool_modifier(ToolModifierRole role, CanvasTool tool, ToolModifierStyle style) noexcept {
  const bool gimp = style == ToolModifierStyle::Gimp;
  const bool other_program = style != ToolModifierStyle::Patchy;
  bool ctrl = false;
  switch (role) {
    case ToolModifierRole::ZoomOut:
      ctrl = other_program;
      break;
    case ToolModifierRole::WheelZoom:
      ctrl = gimp;
      break;
    case ToolModifierRole::ColorPick:
      ctrl = pick_uses_ctrl(tool, style);
      break;
    case ToolModifierRole::RetouchSource:
      ctrl = other_program;
      break;
    case ToolModifierRole::ToningSwap:
      ctrl = gimp;
      break;
    case ToolModifierRole::SelectionSubtract:
      // Krita's selection tools subtract with Alt, as Patchy's do.
      ctrl = gimp;
      break;
    case ToolModifierRole::TemporaryMove:
      // The key a tool's pick, source or swap left free.
      ctrl = !(pick_uses_ctrl(tool, style) || (is_retouch_source_tool(tool) && other_program) ||
               (is_toning_swap_tool(tool) && gimp));
      break;
  }
  return ctrl ? Qt::ControlModifier : Qt::AltModifier;
}

Qt::KeyboardModifier tool_modifier(ToolModifierRole role, CanvasTool tool) noexcept {
  return tool_modifier(role, tool, active_tool_modifier_style());
}

Qt::Key tool_modifier_key(ToolModifierRole role, CanvasTool tool) noexcept {
  return tool_modifier(role, tool) == Qt::ControlModifier ? Qt::Key_Control : Qt::Key_Alt;
}

bool tool_modifier_held(ToolModifierRole role, CanvasTool tool, Qt::KeyboardModifiers modifiers) noexcept {
  return modifiers.testFlag(tool_modifier(role, tool));
}

bool tool_has_ctrl_tool_modifier(CanvasTool tool) noexcept {
  const auto style = active_tool_modifier_style();
  if (style == ToolModifierStyle::Patchy) {
    return false;
  }
  const auto uses_ctrl = [&](ToolModifierRole role) { return tool_modifier(role, tool, style) == Qt::ControlModifier; };
  return (tool == CanvasTool::Zoom && uses_ctrl(ToolModifierRole::ZoomOut)) ||
         (tool_uses_alt_left_for_color_pick(tool) && uses_ctrl(ToolModifierRole::ColorPick)) ||
         (is_retouch_source_tool(tool) && uses_ctrl(ToolModifierRole::RetouchSource)) ||
         (is_toning_swap_tool(tool) && uses_ctrl(ToolModifierRole::ToningSwap)) ||
         (is_selection_tool(tool) && uses_ctrl(ToolModifierRole::SelectionSubtract));
}

QString resolve_tool_modifier_names(QString text, CanvasTool tool) {
  const auto token_for = [tool](ToolModifierRole role) {
    return QLatin1StringView(tool_modifier(role, tool) == Qt::ControlModifier ? kCtrlModifierToken
                                                                               : kAltModifierToken);
  };
  text.replace(QLatin1StringView(kZoomOutKeyToken), token_for(ToolModifierRole::ZoomOut));
  text.replace(QLatin1StringView(kPickKeyToken), token_for(ToolModifierRole::ColorPick));
  text.replace(QLatin1StringView(kSourceKeyToken), token_for(ToolModifierRole::RetouchSource));
  text.replace(QLatin1StringView(kSwapKeyToken), token_for(ToolModifierRole::ToningSwap));
  text.replace(QLatin1StringView(kSubtractKeyToken), token_for(ToolModifierRole::SelectionSubtract));
  return resolve_modifier_names(std::move(text));
}

}  // namespace patchy::ui
