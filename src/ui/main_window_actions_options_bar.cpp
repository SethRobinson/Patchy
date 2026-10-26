// MainWindow::build_options_bar(): the Options-bar phase of create_actions()
// (the top toolbar, its FlowLayout/OptionsFlowContainer hosts from
// options_flow_layout.hpp, and every
// per-tool option control), split out of main_window_actions.cpp along with
// the anonymous-namespace widget helpers only this phase uses.
// Pure function move; behavior must stay identical, and the construction
// order is load-bearing (see create_actions() for the phase order).

#include "core/layer_alignment.hpp"
#include "ui/canvas_types.hpp"
#include "ui/main_window.hpp"
#include "ui/gradient_stops_editor.hpp"
#include <QApplication>
#include <QStatusBar>
#include "ui/canvas_widget.hpp"
#include "ui/clone_source_panel.hpp"
#include "ui/appearance_edits.hpp"
#include "ui/artboard_preset_picker.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/main_window_actions_internal.hpp"
#include "ui/options_flow_layout.hpp"

#include "ai/segmentation_engine.hpp"
#include "ui/object_selection_model_download.hpp"

#include "core/vector_shape.hpp"
#include "core/pixel_tools.hpp"
#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/brush_dynamics_popup.hpp"
#include "ui/brush_presets.hpp"
#include "ui/brush_automation.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tip_manager_dialog.hpp"
#include "ui/brush_tip_picker.hpp"
#include "ui/modifier_names.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/curved_slider.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_float_window.hpp"
#include "ui/font_picker.hpp"
#include "ui/color_panel.hpp"
#include "ui/layer_style_dialog.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/localization.hpp"
#include "ui/measurement_units.hpp"
#include "ui/pattern_library.hpp"
#include "ui/pattern_manager_dialog.hpp"
#include "ui/scanner_import.hpp"
#include "ui/theme_palette.hpp"
#include "ui/tile_preview_window.hpp"
#include "ui/unit_spin_box.hpp"
#include "ui/warp_text_dialog.hpp"
#include "ui/qt_geometry.hpp"
#include "ui/update_checker.hpp"
#include "ui/zoom_status_bar.hpp"

#include <QAction>
#include <QActionGroup>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QFont>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLatin1String>
#include <QMenu>
#include <QObject>
#include <QOverload>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPointer>
#include <QPushButton>
#include <QRect>
#include <QSignalBlocker>
#include <QSize>
#include <QSizeF>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QStyle>
#include <QStringLiteral>
#include <QTextEdit>
#include <QToolBar>
#include <QTimer>
#include <QToolButton>
#include <QVariant>
#include <QWidget>
#include <QWidgetAction>
#include <Qt>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cstddef>
#include <numeric>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <initializer_list>
#include <optional>
#include <qlatin1stringview.h>
#include <utility>


// Icon resources live in the static patchy_ui library; force registration before first use.
int qInitResources_icons();

namespace patchy::ui {

namespace {

class CheckGlyphBox final : public QCheckBox {
public:
  explicit CheckGlyphBox(const QString& text, QWidget* parent = nullptr) : QCheckBox(text, parent) {
    setMinimumHeight(24);
  }

  // Text plus the glyph; the 58 px floor keeps a one-word box from looking cramped. The old
  // 92 px floor (chip-era) left "End" or "Stroke" wider than their text and pushed long
  // rows onto a second line at 1366 px (GitHub issue 91).
  QSize sizeHint() const override {
    const auto text_width = fontMetrics().horizontalAdvance(text());
    return QSize(std::max(58, text_width + 34), 24);
  }

protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Every color here is a role: this glyph is painted, not styled, so it would
    // otherwise stay dark-on-dark in the Light scheme. No box ever gets a field
    // frame behind it: Auto-Select and Anti-alias once did and read as buttons
    // beside their plain neighbours (GitHub issue 167).
    const auto& colors = theme();
    const QRect box(7, (height() - 14) / 2, 14, 14);
    painter.setBrush(isChecked() ? colors.accent : colors.checkbox_compact_bg);
    painter.setPen(
        QPen(isChecked() ? colors.checkbox_accent_border : colors.checkbox_compact_border, 1));
    painter.drawRect(box);
    if (isChecked()) {
      painter.setPen(QPen(colors.text_on_accent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.drawLine(QPointF(box.left() + 3.0, box.center().y() + 0.5), QPointF(box.left() + 6.0, box.bottom() - 3.0));
      painter.drawLine(QPointF(box.left() + 6.0, box.bottom() - 3.0), QPointF(box.right() - 2.0, box.top() + 3.0));
    }

    painter.setPen(isEnabled() ? colors.text_bright : colors.text_disabled);
    painter.drawText(QRect(box.right() + 7, 0, width() - box.right() - 10, height()), Qt::AlignVCenter | Qt::AlignLeft,
                     text());

    // Keyboard focus only: the bar's focus outline around the glyph and its text (GitHub
    // issue 440). A click focuses without it, so the box never reads as a framed button.
    if (hasFocus() && property(kOptionsBarKeyboardFocusProperty).toBool()) {
      painter.setRenderHint(QPainter::Antialiasing, false);
      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(colors.accent_border_bright, 1));
      painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }
  }
};

// Photoshop's Useful Mixer Brush Combinations as Wet/Load/Mix triples,
// VERIFIED against Photoshop 2026 on 2026-08-14 by selecting each preset in
// the real UI and reading currentToolOptions back over COM
// (local-test-fixtures/mixer-calibration/preset-readings.txt). Photoshop sets
// Mix to 0 on the dry rows even though the control is greyed there. Names are
// generic descriptive English words, tr()'d for display.
struct MixerCombination {
  const char* name;
  int wet;
  int load;
  int mix;
};

const std::array<MixerCombination, 12>& mixer_useful_combinations() {
  static constexpr std::array<MixerCombination, 12> kCombinations = {{
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Dry"), 0, 50, 0},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Dry, Light Load"), 0, 5, 0},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Dry, Heavy Load"), 0, 100, 0},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Moist"), 10, 5, 50},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Moist, Light Mix"), 10, 5, 0},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Moist, Heavy Mix"), 10, 5, 100},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Wet"), 50, 50, 50},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Wet, Light Mix"), 50, 50, 0},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Wet, Heavy Mix"), 50, 50, 100},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Very Wet"), 100, 50, 50},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Very Wet, Light Mix"), 100, 50, 0},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Very Wet, Heavy Mix"), 100, 50, 100},
  }};
  return kCombinations;
}

}  // namespace

namespace {

// The transform types on the Move bar and the transform session bar (GitHub issue
// 299), in Edit > Transform's order. Scale and Rotate stay in the menu: the plain box
// does both, and the session row must keep one line in a 1180 px window.
struct TransformTypeButton {
  const char* tooltip;
  const char* icon;
  const char* move_object_name;
  const char* session_object_name;
  TransformDistortMode mode;
  bool warp;
};

constexpr std::array<TransformTypeButton, 4> kTransformTypeButtons{{
    {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Skew"), "transform-skew", "moveTransformSkewButton",
     "transformSkewModeButton", TransformDistortMode::Skew, false},
    {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Distort"), "transform-distort", "moveTransformDistortButton",
     "transformDistortModeButton", TransformDistortMode::Distort, false},
    {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Perspective"), "transform-perspective",
     "moveTransformPerspectiveButton", "transformPerspectiveModeButton", TransformDistortMode::Perspective, false},
    {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Warp Transform"), "warp", "moveWarpButton",
     "transformWarpModeButton", TransformDistortMode::None, true},
}};

// The session button's TransformDistortMode, read by sync_transform_type_buttons.
constexpr const char* kTransformTypeModeProperty = "patchy.transformTypeMode";

// The sample squares of the Pick tool's Size and the Magic Wand's Sample (GitHub issues
// 151 and 435): one shared setting, two combos with the same entries.
constexpr std::array<int, 7> kEyedropperSampleSizes{1, 3, 5, 11, 31, 51, 101};

QString eyedropper_sample_size_text(int size) {
  return size == 1 ? QCoreApplication::translate(kMainWindowTranslationContext, "Point Sample")
                   : QCoreApplication::translate(kMainWindowTranslationContext, "%1 by %1 Average").arg(size);
}

void fill_sample_size_combo(QComboBox* combo, int current_size) {
  for (const int size : kEyedropperSampleSizes) {
    combo->addItem(eyedropper_sample_size_text(size), size);
  }
  combo->setCurrentIndex(std::max(0, combo->findData(current_size)));
}

void retranslate_sample_size_combo(QComboBox* combo) {
  const QSignalBlocker blocker(combo);
  for (int index = 0; index < combo->count(); ++index) {
    combo->setItemText(index, eyedropper_sample_size_text(combo->itemData(index).toInt()));
  }
}

}  // namespace

namespace {

// An options-bar button that opens a menu of further options (Move's align
// menu, Crop options, Smoothing options): the gear icon, InstantPopup, and the
// optionsBarMenuButton QSS rule (main_window_theme.cpp), which pins the height
// to 20 px so the bar row holds (ui_brush_tip_picker_keeps_options_bar_height)
// and hides the menu arrow.
void make_options_menu_button(QToolButton* button) {
  button->setIcon(simple_icon(QStringLiteral("gear")));
  button->setIconSize(QSize(16, 16));
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setProperty("optionsBarMenuButton", true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setPopupMode(QToolButton::InstantPopup);
}

// The closed combo is deliberately compact (the Options bar must keep the
// mixer row on one line), so the popup list needs its own width or every
// entry truncates to "Dry...oad".
void widen_combo_popup_to_items(QComboBox* combo) {
  const QFontMetrics metrics(combo->font());
  int width = 0;
  for (int i = 0; i < combo->count(); ++i) {
    width = std::max(width, metrics.horizontalAdvance(combo->itemText(i)));
  }
  combo->view()->setMinimumWidth(width + 36);
}

// Runs `callback` once, on the first click, key press or wheel turn on `widget`, before the
// widget handles it. Owned by the widget.
class FirstUseHook final : public QObject {
 public:
  FirstUseHook(QWidget* widget, std::function<void()> callback) : QObject(widget), callback_(std::move(callback)) {
    widget->installEventFilter(this);
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    const auto type = event->type();
    if (callback_ && (type == QEvent::MouseButtonPress || type == QEvent::KeyPress || type == QEvent::Wheel)) {
      watched->removeEventFilter(this);
      const auto callback = std::move(callback_);
      callback_ = nullptr;
      callback();
    }
    return false;
  }

 private:
  std::function<void()> callback_;
};

// The control a focus or key event in the Options bar belongs to: a spin box or combo box
// for its line edit.
QWidget* options_bar_focus_control(QWidget* widget) {
  if (auto* parent = widget->parentWidget();
      qobject_cast<QLineEdit*>(widget) != nullptr &&
      (qobject_cast<QAbstractSpinBox*>(parent) != nullptr || qobject_cast<QComboBox*>(parent) != nullptr)) {
    return parent;
  }
  return widget;
}

// A control the keyboard types into, where Enter commits.
bool is_options_bar_text_field(const QWidget* widget) {
  if (qobject_cast<const QAbstractSpinBox*>(widget) != nullptr || qobject_cast<const QLineEdit*>(widget) != nullptr) {
    return true;
  }
  const auto* combo = qobject_cast<const QComboBox*>(widget);
  return combo != nullptr && combo->isEditable();
}

constexpr char kOptionsBarFocusTextProperty[] = "patchy.optionsFocusText";

void set_options_bar_keyboard_focus(QWidget* control, bool keyboard) {
  if (control->property(kOptionsBarKeyboardFocusProperty).toBool() == keyboard) {
    return;
  }
  control->setProperty(kOptionsBarKeyboardFocusProperty, keyboard);
  // Dynamic properties are matched at polish time only.
  control->style()->unpolish(control);
  control->style()->polish(control);
  control->update();
}

}  // namespace

bool MainWindow::handle_options_bar_focus_event(QObject* watched, QEvent* event) {
  const auto type = event->type();
  if ((type != QEvent::FocusIn && type != QEvent::FocusOut && type != QEvent::KeyPress &&
       type != QEvent::ShortcutOverride) ||
      options_flow_container_ == nullptr || !watched->isWidgetType()) {
    return false;
  }
  auto* widget = static_cast<QWidget*>(watched);
  if (!options_flow_container_->isAncestorOf(widget)) {
    return false;
  }
  auto* control = options_bar_focus_control(widget);
  if (type == QEvent::FocusIn || type == QEvent::FocusOut) {
    // Tab and Backtab show the focus outline; a click or a programmatic focus clears it. A popup
    // or a window switch round trip keeps whatever the control had.
    const auto reason = static_cast<QFocusEvent*>(event)->reason();
    if (type == QEvent::FocusIn) {
      // The text Esc puts back in an editable combo (the font box).
      if (auto* combo = qobject_cast<QComboBox*>(control); combo != nullptr && combo->isEditable() &&
                                                           reason != Qt::PopupFocusReason &&
                                                           reason != Qt::ActiveWindowFocusReason) {
        combo->setProperty(kOptionsBarFocusTextProperty, combo->currentText());
      }
      if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason) {
        set_options_bar_keyboard_focus(control, true);
      } else if (reason != Qt::PopupFocusReason && reason != Qt::ActiveWindowFocusReason) {
        set_options_bar_keyboard_focus(control, false);
      }
    } else if (reason != Qt::PopupFocusReason && reason != Qt::ActiveWindowFocusReason) {
      set_options_bar_keyboard_focus(control, false);
    }
    return false;
  }
  // Keys: only the delivery to the focused control itself (a spin box hands the key to its line
  // edit by a direct call no filter sees, and a sent event may target an unfocused field).
  if (widget != QApplication::focusWidget()) {
    return false;
  }
  auto* key_event = static_cast<QKeyEvent*>(event);
  const bool escape = key_event->key() == Qt::Key_Escape && key_event->modifiers() == Qt::NoModifier;
  const bool enter = (key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) &&
                     is_options_bar_text_field(control);
  if (type == QEvent::ShortcutOverride) {
    // Esc belongs to the field (revert, then leave): no window shortcut runs on the same press.
    if (escape) {
      key_event->accept();
      return true;
    }
    return false;
  }
  if (!escape && !enter) {
    return false;
  }
  if (escape) {
    // A spin box puts back its value from before the edit itself (ToolbarSpinboxEscapeReverter,
    // GitHub issue 186). An editable combo (the font box) puts back its text from focus-in.
    if (auto* combo = qobject_cast<QComboBox*>(control); combo != nullptr && combo->isEditable()) {
      if (const auto before = combo->property(kOptionsBarFocusTextProperty);
          before.isValid() && combo->currentText() != before.toString()) {
        combo->setEditText(before.toString());
      }
    }
  }
  // The control handles the key first (a field commits on Enter), then the keyboard goes back to
  // the canvas, after this press, so the canvas never sees it (an Esc there would cancel a
  // transform or drop the selection). A control that lost the keyboard meanwhile keeps its place.
  QTimer::singleShot(0, this, [this, control = QPointer<QWidget>(control)] {
    if (control != nullptr && control->hasFocus() && QApplication::activePopupWidget() == nullptr) {
      return_keyboard_from_options_bar();
    }
  });
  return false;
}

void MainWindow::return_keyboard_from_options_bar() {
  if (canvas_ == nullptr) {
    if (auto* focus = QApplication::focusWidget();
        focus != nullptr && options_flow_container_ != nullptr && options_flow_container_->isAncestorOf(focus)) {
      focus->clearFocus();
    }
    return;
  }
  if (const auto* editor = canvas_->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
      editor != nullptr && !editor->property(kTextEditorFinishedProperty).toBool() && editor->isVisible()) {
    return_keyboard_to_text_editor();
    return;
  }
  // The active document may sit in a float window.
  if (auto* window = canvas_->window(); window != nullptr && !window->isActiveWindow()) {
    window->activateWindow();
  }
  canvas_->setFocus(Qt::OtherFocusReason);
}

void MainWindow::retranslate_mixer_combination_combo() {
  if (mixer_combination_combo_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(mixer_combination_combo_);
  mixer_combination_combo_->setItemText(0, tr("Custom"));
  const auto& combinations = mixer_useful_combinations();
  for (std::size_t i = 0; i < combinations.size(); ++i) {
    mixer_combination_combo_->setItemText(static_cast<int>(i) + 1, tr(combinations[i].name));
  }
  widen_combo_popup_to_items(mixer_combination_combo_);
}

void MainWindow::sync_mixer_combination_combo() {
  if (mixer_combination_combo_ == nullptr) {
    return;
  }
  const auto* wet_spin = findChild<QSpinBox*>(QStringLiteral("mixerWetSpin"));
  const auto* load_spin = findChild<QSpinBox*>(QStringLiteral("mixerLoadSpin"));
  const auto* mix_spin = findChild<QSpinBox*>(QStringLiteral("mixerMixSpin"));
  if (wet_spin == nullptr || load_spin == nullptr || mix_spin == nullptr) {
    return;
  }
  int selection = 0;  // Custom
  const auto& combinations = mixer_useful_combinations();
  for (std::size_t i = 0; i < combinations.size(); ++i) {
    const auto& combination = combinations[i];
    if (combination.wet != wet_spin->value() || combination.load != load_spin->value()) {
      continue;
    }
    if (combination.mix >= 0 && combination.mix != mix_spin->value()) {
      continue;
    }
    selection = static_cast<int>(i) + 1;
    break;
  }
  const QSignalBlocker blocker(mixer_combination_combo_);
  mixer_combination_combo_->setCurrentIndex(selection);
}

void MainWindow::build_options_bar(ActionBuildContext& ctx) {
  // The startup-defaults donor canvas resolved by create_actions() (see the
  // comment there); a local alias keeps the moved body identical.
  auto* canvas_defaults = ctx.canvas_defaults;
  auto* toolbar = new QToolBar(tr("Options"), this);
  toolbar->setObjectName(QStringLiteral("Options"));
  toolbar->setMovable(false);
  toolbar->setFloatable(false);
  toolbar->setAllowedAreas(Qt::TopToolBarArea);
  toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar->setIconSize(QSize(18, 18));
  addToolBar(Qt::TopToolBarArea, toolbar);

  // Host the tool options in a wrapping flow layout so they fold onto a second
  // row when the window is too narrow, instead of being clipped off the edge.
  auto* options_content = new OptionsFlowContainer(toolbar);
  options_content->setObjectName(QStringLiteral("OptionsContent"));
  options_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  // 4 px between controls (was 5): every long row must fit one line at 1366 px (GitHub issue 91).
  auto* options_flow = new FlowLayout(options_content, 4, 4);
  // Side padding so the first control does not butt against the window edge.
  // 4 px above and below: one 26 px row fills the 34 px strip exactly, so it sits evenly,
  // and a wrapped bar has 4 px above, between and below its rows (GitHub issue 409).
  options_flow->setContentsMargins(6, 4, 0, 4);
  // Controls sit on their row's middle; rows keep their place from the top.
  options_flow->set_center_rows(true);
  options_content->setLayout(options_flow);
  toolbar->addWidget(options_content);
  options_flow_container_ = options_content;
  // A resize across the slider-collapse width re-runs the visibility pass.
  options_content->set_collapse_changed_callback([this] { refresh_options_bar(); });

  option_actions_.clear();
  transform_option_actions_.clear();
  warp_option_actions_.clear();
  transform_session_actions_.clear();
  const auto make_option_separator = [options_content, options_flow]() -> QWidget* {
    auto* line = new QFrame(options_content);
    line->setObjectName(QStringLiteral("optionSeparator"));
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setFixedHeight(24);
    options_flow->addWidget(line);
    return line;
  };
  const auto add_option_separator = [this, make_option_separator](std::initializer_list<CanvasTool> tools) {
    register_option_action(make_option_separator(), tools);
  };
  const auto add_option_action = [this, options_content, options_flow](const QIcon& icon, const char* source,
                                                                       std::initializer_list<CanvasTool> tools) {
    auto* action = new QAction(icon, tr(source), this);
    bind_action_text(action, source);
    auto* button = new QToolButton(options_content);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(18, 18));
    button->setAutoRaise(true);
    // Tagged for the compact Options-bar button style (see the app stylesheet).
    // The default QToolButton min-height + padding makes it the tallest item in
    // the row, which grows the whole Options toolbar when a selection tool is
    // active. The icon keeps its full size; only the padding around it shrinks.
    button->setProperty("optionsBarButton", true);
    options_flow->addWidget(button);
    register_option_action(button, tools);
    return action;
  };
  const auto add_option_widget = [this, options_flow](QWidget* widget, std::initializer_list<CanvasTool> tools) {
    options_flow->addWidget(widget);
    register_option_action(widget, tools);
    return widget;
  };
  const auto add_transform_option_widget = [this, options_flow](QWidget* widget) {
    options_flow->addWidget(widget);
    transform_option_actions_.push_back(widget);
    return widget;
  };
  const auto add_option_label = [options_content, add_option_widget](const char* source,
                                                                     std::initializer_list<CanvasTool> tools) {
    auto* label = new QLabel(options_content);
    label->setProperty(kTranslationStripsTrailingColonProperty, true);  // issue 127
    bind_widget_text(label, source);
    label->setProperty("optionLabel", true);
    label->setAlignment(Qt::AlignVCenter);
    return add_option_widget(label, tools);
  };

  move_auto_select_check_ = new CheckGlyphBox(tr("Auto-Select"), toolbar);
  move_auto_select_check_->setObjectName(QStringLiteral("moveAutoSelectCheck"));
  bind_tooltip(move_auto_select_check_, QT_TR_NOOP("Select layers by clicking artwork or dragging a rectangle from empty space"));
  move_auto_select_check_->setChecked(canvas_defaults->auto_select_layer());
  add_option_widget(move_auto_select_check_, {CanvasTool::Move});
  connect(move_auto_select_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_auto_select_layer(checked);
    }
    // Remembered across runs (GitHub issue 73). Written here, by the user's
    // click, rather than by save_tool_settings: tests set the canvas flag
    // directly and must not leak it into later windows.
    app_settings().setValue(QStringLiteral("tools/moveAutoSelect"), checked);
  });
  // Photoshop's Auto-Select Layer/Group choice (GitHub issue 168). It also
  // decides what a Ctrl/Cmd+click picks with Auto-Select off, so it stays enabled.
  move_auto_select_target_combo_ = new QComboBox(toolbar);
  move_auto_select_target_combo_->setObjectName(QStringLiteral("moveAutoSelectTargetCombo"));
  move_auto_select_target_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  move_auto_select_target_combo_->setMinimumWidth(72);
  move_auto_select_target_combo_->addItem(tr("Layer"), move_auto_select_target_token(MoveAutoSelectTarget::Layer));
  move_auto_select_target_combo_->addItem(tr("Group"), move_auto_select_target_token(MoveAutoSelectTarget::Group));
  move_auto_select_target_combo_->setCurrentIndex(
      canvas_defaults->auto_select_target() == MoveAutoSelectTarget::Group ? 1 : 0);
  bind_tooltip(move_auto_select_target_combo_,
               QT_TR_NOOP("What a click or a selection rectangle selects: the layer itself, or the outermost "
                          "group that holds it"));
  register_retranslation([this] {
    if (move_auto_select_target_combo_ == nullptr || move_auto_select_target_combo_->count() < 2) {
      return;
    }
    QSignalBlocker blocker(move_auto_select_target_combo_);
    move_auto_select_target_combo_->setItemText(0, tr("Layer"));
    move_auto_select_target_combo_->setItemText(1, tr("Group"));
  });
  add_option_widget(move_auto_select_target_combo_, {CanvasTool::Move});
  // Auto-Select and its target are one group, set apart from the transform types (GitHub
  // issue 350).
  add_option_separator({CanvasTool::Move});
  connect(move_auto_select_target_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || move_auto_select_target_combo_ == nullptr) {
      return;
    }
    const auto token = move_auto_select_target_combo_->itemData(index).toString();
    if (canvas_ != nullptr) {
      canvas_->set_auto_select_target(move_auto_select_target_from_token(token));
    }
    // Written by the user's choice, like tools/moveAutoSelect above.
    app_settings().setValue(QLatin1String(kMoveAutoSelectTargetSettingsKey), token);
  });
  // Transform types straight from the Move bar (GitHub issues 225 and 299): the
  // same entries as Edit > Free Transform and Edit > Transform, so any type is one
  // click away before a transform starts. During a session the bar swaps to the
  // transform row, which leads with the same group as checkable buttons.
  for (const auto& type : kTransformTypeButtons) {
    auto* button = new QToolButton(toolbar);
    button->setObjectName(QLatin1String(type.move_object_name));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIcon(simple_icon(QLatin1String(type.icon)));
    button->setIconSize(QSize(18, 18));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setProperty("optionsBarButton", true);
    bind_tooltip(button, type.tooltip);
    add_option_widget(button, {CanvasTool::Move});
    connect(button, &QToolButton::clicked, this, [this, type] {
      if (type.warp) {
        warp_transform_active_layer();
      } else if (type.mode == TransformDistortMode::None) {
        transform_active_layer_dialog();
      } else {
        transform_active_layer_mode(type.mode);
      }
    });
  }
  // Align buttons MIRROR the Layer > Arrange > Align QActions (docs/alignment.md)
  // rather than wrapping them as default actions: refresh_options_bar hides
  // and re-enables a button's default action with the tool row, which would
  // override the menu commands' own enabled state whenever Move is not active.
  add_option_separator({CanvasTool::Move});
  const auto add_option_action_mirror = [this, options_content, options_flow](QAction* action,
                                                                             const QString& object_name,
                                                                             std::initializer_list<CanvasTool> tools) {
    auto* button = new QToolButton(options_content);
    button->setObjectName(object_name);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(18, 18));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setProperty("optionsBarButton", true);
    // refresh_options_bar leaves the enabled state to the mirrored action.
    button->setProperty("optionsBarMirrorsAction", true);
    const auto sync = [button, action] {
      button->setIcon(action->icon());
      button->setToolTip(action->toolTip());
      button->setEnabled(action->isEnabled());
    };
    sync();
    connect(action, &QAction::changed, button, sync);
    connect(button, &QToolButton::clicked, action, [action] { action->trigger(); });
    options_flow->addWidget(button);
    register_option_action(button, tools);
    return button;
  };
  {
    const char* const align_button_names[] = {"moveAlignLeftButton", "moveAlignHCenterButton",
                                              "moveAlignRightButton", "moveAlignTopButton",
                                              "moveAlignVCenterButton", "moveAlignBottomButton"};
    for (std::size_t i = 0; i < layer_align_actions_.size(); ++i) {
      if (layer_align_actions_[i] != nullptr) {
        add_option_action_mirror(layer_align_actions_[i], QLatin1String(align_button_names[i]),
                                 {CanvasTool::Move});
      }
    }
  }
  // The six feature Distribute commands follow as buttons, like Photoshop's bar
  // (GitHub issue 172), vertical modes first in Photoshop's order (GitHub issue 405);
  // the two spacing modes stay in the gear menu below.
  add_option_separator({CanvasTool::Move});
  {
    struct DistributeButton {
      DistributeMode mode;
      const char* object_name;
    };
    const DistributeButton distribute_buttons[] = {
        {DistributeMode::Top, "moveDistributeTopButton"},
        {DistributeMode::VerticalCenter, "moveDistributeVCenterButton"},
        {DistributeMode::Bottom, "moveDistributeBottomButton"},
        {DistributeMode::Left, "moveDistributeLeftButton"},
        {DistributeMode::HorizontalCenter, "moveDistributeHCenterButton"},
        {DistributeMode::Right, "moveDistributeRightButton"},
    };
    for (const auto& button : distribute_buttons) {
      if (auto* action = layer_distribute_actions_[static_cast<std::size_t>(button.mode)]; action != nullptr) {
        add_option_action_mirror(action, QLatin1String(button.object_name), {CanvasTool::Move});
      }
    }
  }
  move_align_more_button_ = new QToolButton(toolbar);
  move_align_more_button_->setObjectName(QStringLiteral("moveAlignMoreButton"));
  make_options_menu_button(move_align_more_button_);
  bind_tooltip(move_align_more_button_, QT_TR_NOOP("Distribute spacing and choose what to align to"));
  {
    auto* more_menu = new QMenu(move_align_more_button_);
    more_menu->setObjectName(QStringLiteral("moveAlignMoreMenu"));
    for (const auto mode : {DistributeMode::HorizontalSpacing, DistributeMode::VerticalSpacing}) {
      if (auto* action = layer_distribute_actions_[static_cast<std::size_t>(mode)]; action != nullptr) {
        more_menu->addAction(action);
      }
    }
    more_menu->addSeparator();
    if (layer_align_to_selection_action_ != nullptr) {
      more_menu->addAction(layer_align_to_selection_action_);
    }
    if (layer_align_to_canvas_action_ != nullptr) {
      more_menu->addAction(layer_align_to_canvas_action_);
    }
    move_align_more_button_->setMenu(more_menu);
  }
  add_option_widget(move_align_more_button_, {CanvasTool::Move});

  // Transform session types (GitHub issue 299): tool modes lead the row (options
  // bar grouping rule 1), shown with both the free transform and the warp controls.
  // Each is a toggle: a click on the checked type (Warp included) goes back to plain
  // Free Transform. sync_transform_type_buttons checks the session's type.
  transform_type_buttons_.clear();
  for (const auto& type : kTransformTypeButtons) {
    auto* button = new QPushButton(toolbar);
    button->setObjectName(QLatin1String(type.session_object_name));
    button->setCheckable(true);
    button->setIcon(simple_icon(QLatin1String(type.icon)));
    bind_tooltip(button, type.tooltip);
    button->setFixedWidth(30);
    // Session buttons render their icons at 20px (the QPushButton default of 16px
    // reads tiny on the bar); optionsSessionButton relaxes the QSS side padding so
    // the larger icon is not clipped.
    button->setIconSize(QSize(20, 20));
    button->setProperty("optionsSessionButton", true);
    options_flow->addWidget(button);
    transform_session_actions_.push_back(button);
    if (type.warp) {
      transform_warp_mode_button_ = button;
      continue;
    }
    button->setProperty(kTransformTypeModeProperty, static_cast<int>(type.mode));
    transform_type_buttons_.push_back(button);
    connect(button, &QPushButton::clicked, this, [this, mode = type.mode] {
      // Like the Warp toggle, a click on the checked type goes back to Free Transform.
      const bool checked_type = canvas_ != nullptr && canvas_->free_transform_active() &&
                                canvas_->free_transform_distort_mode() == mode;
      transform_active_layer_mode(checked_type ? TransformDistortMode::None : mode);
      // Re-sync the checks (a refused switch leaves the type unchanged).
      sync_transform_type_buttons();
    });
  }
  transform_session_actions_.push_back(make_option_separator());
  {
    auto* pivot_label = new QLabel(QCoreApplication::translate(kMainWindowTranslationContext, "Pivot:"), toolbar);
    pivot_label->setObjectName(QStringLiteral("freeTransformPivotLabel"));
    pivot_label->setProperty("optionLabel", true);
    pivot_label->setProperty(kTranslationStripsTrailingColonProperty, true);
    pivot_label->setAlignment(Qt::AlignVCenter);
    bind_widget_text(pivot_label, QT_TR_NOOP("Pivot:"));
    add_transform_option_widget(pivot_label);
  }
  transform_reference_combo_ = new QComboBox(toolbar);
  transform_reference_combo_->setObjectName(QStringLiteral("freeTransformReferenceCombo"));
  bind_tooltip(transform_reference_combo_, QT_TR_NOOP("Reference point"));
  transform_reference_combo_->setMinimumWidth(96);
  add_transform_option_widget(transform_reference_combo_);
  register_retranslation([this] {
    if (transform_reference_combo_ == nullptr) {
      return;
    }
    const auto current = transform_reference_combo_->currentData();
    QSignalBlocker blocker(transform_reference_combo_);
    transform_reference_combo_->clear();
    transform_reference_combo_->addItem(tr("Top Left"), static_cast<int>(CanvasAnchor::TopLeft));
    transform_reference_combo_->addItem(tr("Top"), static_cast<int>(CanvasAnchor::Top));
    transform_reference_combo_->addItem(tr("Top Right"), static_cast<int>(CanvasAnchor::TopRight));
    transform_reference_combo_->addItem(tr("Left"), static_cast<int>(CanvasAnchor::Left));
    transform_reference_combo_->addItem(tr("Center"), static_cast<int>(CanvasAnchor::Center));
    transform_reference_combo_->addItem(tr("Right"), static_cast<int>(CanvasAnchor::Right));
    transform_reference_combo_->addItem(tr("Bottom Left"), static_cast<int>(CanvasAnchor::BottomLeft));
    transform_reference_combo_->addItem(tr("Bottom"), static_cast<int>(CanvasAnchor::Bottom));
    transform_reference_combo_->addItem(tr("Bottom Right"), static_cast<int>(CanvasAnchor::BottomRight));
    const auto index = transform_reference_combo_->findData(current.isValid() ? current : QVariant(static_cast<int>(CanvasAnchor::Center)));
    transform_reference_combo_->setCurrentIndex(index >= 0 ? index : transform_reference_combo_->findData(static_cast<int>(CanvasAnchor::Center)));
  });
  connect(transform_reference_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (updating_transform_controls_ || canvas_ == nullptr || transform_reference_combo_ == nullptr || index < 0) {
      return;
    }
    canvas_->set_transform_reference_point(
        static_cast<CanvasAnchor>(transform_reference_combo_->itemData(index).toInt()));
    sync_transform_controls_from_canvas();
  });

  const auto make_transform_label = [toolbar, add_transform_option_widget](const char* source) {
    auto* label = new QLabel(QCoreApplication::translate(kMainWindowTranslationContext, source), toolbar);
    label->setProperty("optionLabel", true);
    label->setProperty(kTranslationStripsTrailingColonProperty, true);
    label->setAlignment(Qt::AlignVCenter);
    bind_widget_text(label, source);
    add_transform_option_widget(label);
    return label;
  };
  // Unit-entry fields (Photoshop behavior): the display stays in the native unit,
  // but "2 in", "50%" or "200 px" typed into any of them converts on entry. The
  // context providers supply the document PPI and what 100% means per field: X/Y
  // percent is relative to the document extent, W/H pixels are relative to the
  // session's original extent.
  const auto make_transform_spin = [toolbar, add_transform_option_widget](const QString& object_name,
                                                                          double minimum, double maximum,
                                                                          int decimals, SpinUnit native) {
    auto* spin = new UnitSpinBox(native, toolbar);
    spin->setObjectName(object_name);
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setKeyboardTracking(false);
    spin->setMinimumWidth(82);
    configure_dialog_spinbox(spin, 82);
    add_transform_option_widget(spin);
    return spin;
  };
  const auto document_ppi = [this]() -> double {
    return has_active_document() ? text_size_ppi(document()) : 300.0;
  };
  const auto document_axis_context = [this, document_ppi](bool horizontal) {
    return [this, document_ppi, horizontal]() -> UnitConversionContext {
      UnitConversionContext context;
      context.ppi = document_ppi();
      if (has_active_document()) {
        context.percent_reference_pixels =
            static_cast<double>(horizontal ? document().width() : document().height());
      }
      return context;
    };
  };
  const auto transform_extent_context = [this, document_ppi](bool horizontal) {
    return [this, document_ppi, horizontal]() -> UnitConversionContext {
      UnitConversionContext context;
      context.ppi = document_ppi();
      const auto state = canvas_ != nullptr ? canvas_->transform_controls_state()
                                            : std::optional<CanvasWidget::TransformControlsState>{};
      if (state.has_value()) {
        context.percent_reference_pixels = horizontal ? state->original_size.width() : state->original_size.height();
      }
      return context;
    };
  };

  // Separators group the row: pivot | position | size | angle | interpolation and the
  // warp toggle | apply and cancel (GitHub issue 181).
  const auto add_transform_separator = [this, make_option_separator] {
    transform_option_actions_.push_back(make_option_separator());
  };
  add_transform_separator();
  make_transform_label(QT_TR_NOOP("X:"));
  transform_x_spin_ = make_transform_spin(QStringLiteral("freeTransformXSpin"), -30000.0, 30000.0, 2,
                                          SpinUnit::Pixels);
  transform_x_spin_->set_context_provider(document_axis_context(true));
  register_ruler_unit_field(transform_x_spin_);  // starts in the ruler unit (Photoshop)
  bind_tooltip(transform_x_spin_, QT_TR_NOOP("Reference X position"));
  make_transform_label(QT_TR_NOOP("Y:"));
  transform_y_spin_ = make_transform_spin(QStringLiteral("freeTransformYSpin"), -30000.0, 30000.0, 2,
                                          SpinUnit::Pixels);
  transform_y_spin_->set_context_provider(document_axis_context(false));
  register_ruler_unit_field(transform_y_spin_);
  bind_tooltip(transform_y_spin_, QT_TR_NOOP("Reference Y position"));
  add_transform_separator();
  make_transform_label(QT_TR_NOOP("W:"));
  transform_scale_x_spin_ = make_transform_spin(QStringLiteral("freeTransformScaleXSpin"), -10000.0, 10000.0, 2,
                                                 SpinUnit::Percent);
  transform_scale_x_spin_->set_context_provider(transform_extent_context(true));
  transform_scale_x_spin_->set_display_unit_switchable(true);
  bind_tooltip(transform_scale_x_spin_, QT_TR_NOOP("Horizontal scale"));
  transform_link_scale_button_ = new QPushButton(toolbar);
  transform_link_scale_button_->setObjectName(QStringLiteral("freeTransformLinkScaleButton"));
  transform_link_scale_button_->setCheckable(true);
  transform_link_scale_button_->setChecked(true);
  transform_link_scale_button_->setIcon(simple_icon(QStringLiteral("link"), QColor(220, 226, 235)));
  bind_tooltip(transform_link_scale_button_, QT_TR_NOOP("Link horizontal and vertical scale"));
  transform_link_scale_button_->setFixedWidth(28);
  add_transform_option_widget(transform_link_scale_button_);
  make_transform_label(QT_TR_NOOP("H:"));
  transform_scale_y_spin_ = make_transform_spin(QStringLiteral("freeTransformScaleYSpin"), -10000.0, 10000.0, 2,
                                                 SpinUnit::Percent);
  transform_scale_y_spin_->set_context_provider(transform_extent_context(false));
  transform_scale_y_spin_->set_display_unit_switchable(true);
  // The linked W/H pair reads as one control, so a unit switch on either side
  // carries the other along (X and Y stay independent, like Photoshop).
  connect(transform_scale_x_spin_, &UnitSpinBox::display_unit_changed, transform_scale_y_spin_,
          &UnitSpinBox::set_display_unit);
  connect(transform_scale_y_spin_, &UnitSpinBox::display_unit_changed, transform_scale_x_spin_,
          &UnitSpinBox::set_display_unit);
  bind_tooltip(transform_scale_y_spin_, QT_TR_NOOP("Vertical scale"));
  add_transform_separator();
  make_transform_label(QT_TR_NOOP("Angle:"));
  transform_rotation_spin_ = make_transform_spin(QStringLiteral("freeTransformRotationSpin"), -3600.0, 3600.0, 2,
                                                 SpinUnit::Degrees);
  bind_tooltip(transform_rotation_spin_, QT_TR_NOOP("Rotation angle"));
  register_retranslation([this] {
    for (auto* spin : {transform_x_spin_, transform_y_spin_, transform_scale_x_spin_, transform_scale_y_spin_,
                       transform_rotation_spin_}) {
      if (spin != nullptr) {
        spin->refresh_suffix();
      }
    }
  });
  add_transform_separator();
  transform_interpolation_combo_ = new QComboBox(toolbar);
  transform_interpolation_combo_->setObjectName(QStringLiteral("freeTransformInterpolationCombo"));
  bind_tooltip(transform_interpolation_combo_, QT_TR_NOOP("Interpolation"));
  transform_interpolation_combo_->setMinimumWidth(132);
  add_transform_option_widget(transform_interpolation_combo_);
  register_retranslation([this] {
    if (transform_interpolation_combo_ == nullptr) {
      return;
    }
    const auto current = transform_interpolation_combo_->currentData();
    QSignalBlocker blocker(transform_interpolation_combo_);
    transform_interpolation_combo_->clear();
    transform_interpolation_combo_->addItem(tr("Nearest Neighbor"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::NearestNeighbor));
    transform_interpolation_combo_->addItem(tr("Bilinear"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::Bilinear));
    transform_interpolation_combo_->addItem(tr("Bicubic"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::Bicubic));
    // Photoshop's Free Transform set, in its order; the kernels are Image Size's.
    transform_interpolation_combo_->addItem(tr("Bicubic Smoother"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::BicubicSmoother));
    transform_interpolation_combo_->addItem(tr("Bicubic Sharper"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::BicubicSharper));
    transform_interpolation_combo_->addItem(tr("Bicubic Automatic"),
                                            static_cast<int>(CanvasWidget::TransformInterpolation::Automatic));
    const auto fallback = static_cast<int>(CanvasWidget::TransformInterpolation::Bicubic);
    const auto index = transform_interpolation_combo_->findData(current.isValid() ? current : QVariant(fallback));
    transform_interpolation_combo_->setCurrentIndex(index >= 0 ? index : transform_interpolation_combo_->findData(fallback));
  });
  const auto apply_transform_from_spin = [this] { apply_transform_controls_from_ui(); };
  connect(transform_x_spin_, &QDoubleSpinBox::valueChanged, this, apply_transform_from_spin);
  connect(transform_y_spin_, &QDoubleSpinBox::valueChanged, this, apply_transform_from_spin);
  connect(transform_scale_x_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (!updating_transform_controls_ && transform_link_scale_button_ != nullptr && transform_link_scale_button_->isChecked() &&
        transform_scale_y_spin_ != nullptr) {
      QSignalBlocker blocker(transform_scale_y_spin_);
      transform_scale_y_spin_->setValue(value);
    }
    apply_transform_controls_from_ui();
  });
  connect(transform_scale_y_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    if (!updating_transform_controls_ && transform_link_scale_button_ != nullptr && transform_link_scale_button_->isChecked() &&
        transform_scale_x_spin_ != nullptr) {
      QSignalBlocker blocker(transform_scale_x_spin_);
      transform_scale_x_spin_->setValue(value);
    }
    apply_transform_controls_from_ui();
  });
  connect(transform_link_scale_button_, &QPushButton::toggled, this, [this](bool checked) {
    if (checked && transform_scale_x_spin_ != nullptr && transform_scale_y_spin_ != nullptr) {
      QSignalBlocker blocker(transform_scale_y_spin_);
      transform_scale_y_spin_->setValue(transform_scale_x_spin_->value());
      apply_transform_controls_from_ui();
    }
  });
  connect(transform_rotation_spin_, &QDoubleSpinBox::valueChanged, this, apply_transform_from_spin);
  connect(transform_interpolation_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (updating_transform_controls_ || canvas_ == nullptr || transform_interpolation_combo_ == nullptr || index < 0) {
      return;
    }
    canvas_->set_transform_interpolation(
        static_cast<CanvasWidget::TransformInterpolation>(transform_interpolation_combo_->itemData(index).toInt()));
  });
  // Warp Transform options: visible only while the warp cage is active.
  const auto add_warp_option_widget = [this, options_flow](QWidget* widget) {
    options_flow->addWidget(widget);
    warp_option_actions_.push_back(widget);
    return widget;
  };
  {
    auto* label = new QLabel(QObject::tr("Warp:"), toolbar);
    label->setProperty("optionLabel", true);
    label->setProperty(kTranslationStripsTrailingColonProperty, true);
    label->setAlignment(Qt::AlignVCenter);
    bind_widget_text(label, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Warp:"));
    add_warp_option_widget(label);
  }
  warp_style_combo_ = new QComboBox(toolbar);
  warp_style_combo_->setObjectName(QStringLiteral("warpStyleCombo"));
  bind_tooltip(warp_style_combo_, QT_TR_NOOP("Warp style"));
  warp_style_combo_->setMinimumWidth(110);
  add_warp_option_widget(warp_style_combo_);
  register_retranslation([this] {
    if (warp_style_combo_ == nullptr) {
      return;
    }
    const auto current = warp_style_combo_->currentData();
    QSignalBlocker blocker(warp_style_combo_);
    warp_style_combo_->clear();
    warp_style_combo_->addItem(tr("Custom"), QStringLiteral("warpCustom"));
    warp_style_combo_->addItem(tr("Arc"), QStringLiteral("warpArc"));
    warp_style_combo_->addItem(tr("Arc Lower"), QStringLiteral("warpArcLower"));
    warp_style_combo_->addItem(tr("Arc Upper"), QStringLiteral("warpArcUpper"));
    warp_style_combo_->addItem(tr("Arch"), QStringLiteral("warpArch"));
    warp_style_combo_->addItem(tr("Bulge"), QStringLiteral("warpBulge"));
    warp_style_combo_->addItem(tr("Shell Lower"), QStringLiteral("warpShellLower"));
    warp_style_combo_->addItem(tr("Shell Upper"), QStringLiteral("warpShellUpper"));
    warp_style_combo_->addItem(tr("Flag"), QStringLiteral("warpFlag"));
    warp_style_combo_->addItem(tr("Wave"), QStringLiteral("warpWave"));
    warp_style_combo_->addItem(tr("Fish"), QStringLiteral("warpFish"));
    warp_style_combo_->addItem(tr("Rise"), QStringLiteral("warpRise"));
    warp_style_combo_->addItem(tr("Fisheye"), QStringLiteral("warpFisheye"));
    warp_style_combo_->addItem(tr("Inflate"), QStringLiteral("warpInflate"));
    warp_style_combo_->addItem(tr("Squeeze"), QStringLiteral("warpSqueeze"));
    warp_style_combo_->addItem(tr("Twist"), QStringLiteral("warpTwist"));
    const auto index = warp_style_combo_->findData(current.isValid() ? current : QVariant(QStringLiteral("warpCustom")));
    warp_style_combo_->setCurrentIndex(std::max(0, index));
  });
  {
    auto* label = new QLabel(QObject::tr("Bend:"), toolbar);
    label->setProperty("optionLabel", true);
    label->setProperty(kTranslationStripsTrailingColonProperty, true);
    label->setAlignment(Qt::AlignVCenter);
    bind_widget_text(label, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Bend:"));
    add_warp_option_widget(label);
  }
  warp_bend_spin_ = new QDoubleSpinBox(toolbar);
  warp_bend_spin_->setObjectName(QStringLiteral("warpBendSpin"));
  warp_bend_spin_->setRange(-100.0, 100.0);
  warp_bend_spin_->setDecimals(0);
  warp_bend_spin_->setKeyboardTracking(false);
  warp_bend_spin_->setSuffix(percent_suffix());
  warp_bend_spin_->setValue(50.0);
  bind_tooltip(warp_bend_spin_, QT_TR_NOOP("Warp bend"));
  configure_dialog_spinbox(warp_bend_spin_, 74);
  add_warp_option_widget(warp_bend_spin_);
  const auto apply_warp_style_from_ui = [this] {
    if (updating_transform_controls_ || canvas_ == nullptr || warp_style_combo_ == nullptr ||
        warp_bend_spin_ == nullptr) {
      return;
    }
    canvas_->apply_warp_style_preset(warp_style_combo_->currentData().toString(), warp_bend_spin_->value());
  };
  connect(warp_style_combo_, &QComboBox::currentIndexChanged, this,
          [apply_warp_style_from_ui](int index) {
            if (index >= 0) {
              apply_warp_style_from_ui();
            }
          });
  connect(warp_bend_spin_, &QDoubleSpinBox::valueChanged, this,
          [apply_warp_style_from_ui](double) { apply_warp_style_from_ui(); });

  // Shared session trio, laid out after both control sets so it closes the row in
  // either mode (Photoshop's options-bar order: mode toggle, then cancel/commit).
  // Apply/cancel dispatch on whichever session is active.
  const auto add_session_option_widget = [this, options_flow](QWidget* widget) {
    options_flow->addWidget(widget);
    transform_session_actions_.push_back(widget);
    return widget;
  };
  transform_session_actions_.push_back(make_option_separator());
  // Apply/Cancel are the same pair on the Transform, Crop and Type bars: the
  // authored apply and cancel SVGs take their green and red from icon_success and
  // icon_danger at paint time. Cancel's round stop sign is its own glyph, not the
  // square "clear" one, which is also Layer > Clear (GitHub issue 182).
  transform_apply_button_ = new QPushButton(toolbar);
  transform_apply_button_->setObjectName(QStringLiteral("freeTransformApplyButton"));
  transform_apply_button_->setIcon(simple_icon(QStringLiteral("ok")));
  bind_tooltip(transform_apply_button_, QT_TR_NOOP("Apply transform"));
  transform_apply_button_->setFixedWidth(30);
  transform_apply_button_->setIconSize(QSize(20, 20));
  transform_apply_button_->setProperty("optionsSessionButton", true);
  add_session_option_widget(transform_apply_button_);
  transform_cancel_button_ = new QPushButton(toolbar);
  transform_cancel_button_->setObjectName(QStringLiteral("freeTransformCancelButton"));
  transform_cancel_button_->setIcon(simple_icon(QStringLiteral("cancel")));
  bind_tooltip(transform_cancel_button_, QT_TR_NOOP("Cancel transform"));
  transform_cancel_button_->setFixedWidth(30);
  transform_cancel_button_->setIconSize(QSize(20, 20));
  transform_cancel_button_->setProperty("optionsSessionButton", true);
  add_session_option_widget(transform_cancel_button_);
  connect(transform_warp_mode_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ == nullptr) {
      return;
    }
    if (canvas_->warp_transform_active()) {
      canvas_->switch_warp_to_free_transform();
    } else if (canvas_->free_transform_active()) {
      canvas_->begin_warp_transform();  // refusal reasons land in the status bar
    }
    // Re-sync the checked state (a refused switch leaves the mode unchanged).
    refresh_options_bar();
    sync_transform_type_buttons();
  });
  connect(transform_apply_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ == nullptr) {
      return;
    }
    if (canvas_->warp_transform_active()) {
      canvas_->finish_warp_transform();
    } else {
      canvas_->finish_free_transform();
    }
  });
  connect(transform_cancel_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ == nullptr) {
      return;
    }
    if (canvas_->warp_transform_active()) {
      canvas_->cancel_warp_transform();
    } else {
      canvas_->cancel_free_transform();
    }
  });

  auto* selection_new = add_option_action(
      simple_icon(QStringLiteral("N")), QT_TR_NOOP("New Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::PolygonLasso,
       CanvasTool::MagneticLasso, CanvasTool::MagicWand, CanvasTool::QuickSelect, CanvasTool::PatchTool,
       CanvasTool::ObjectSelect});
  selection_new->setObjectName(QStringLiteral("selectionNewModeAction"));
  auto* selection_add = add_option_action(
      simple_icon(QStringLiteral("+")), QT_TR_NOOP("Add to Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::PolygonLasso,
       CanvasTool::MagneticLasso, CanvasTool::MagicWand, CanvasTool::QuickSelect, CanvasTool::PatchTool,
       CanvasTool::ObjectSelect});
  selection_add->setObjectName(QStringLiteral("selectionAddModeAction"));
  auto* selection_subtract = add_option_action(
      simple_icon(QStringLiteral("-")), QT_TR_NOOP("Subtract from Selection"),
      {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::PolygonLasso,
       CanvasTool::MagneticLasso, CanvasTool::MagicWand, CanvasTool::QuickSelect, CanvasTool::PatchTool,
       CanvasTool::ObjectSelect});
  selection_subtract->setObjectName(QStringLiteral("selectionSubtractModeAction"));
  auto* selection_intersect = add_option_action(simple_icon(QStringLiteral("Ix")), QT_TR_NOOP("Intersect Selection"),
                                                {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                                                 CanvasTool::PolygonLasso, CanvasTool::MagneticLasso,
                                                 CanvasTool::MagicWand, CanvasTool::PatchTool});
  selection_intersect->setObjectName(QStringLiteral("selectionIntersectModeAction"));
  selection_new_mode_action_ = selection_new;
  selection_add_mode_action_ = selection_add;
  selection_subtract_mode_action_ = selection_subtract;
  selection_intersect_mode_action_ = selection_intersect;
  auto* selection_mode_group = new QActionGroup(this);
  selection_mode_group->setExclusive(true);
  const auto configure_selection_mode_action = [selection_mode_group](QAction* action) {
    action->setCheckable(true);
    selection_mode_group->addAction(action);
  };
  configure_selection_mode_action(selection_new);
  configure_selection_mode_action(selection_add);
  configure_selection_mode_action(selection_subtract);
  configure_selection_mode_action(selection_intersect);
  selection_new->setChecked(true);
  const auto set_selection_mode = [this](CanvasWidget::SelectionMode mode) {
    // Each selection tool keeps its own combine mode; store it for the active
    // tool (so new documents inherit it) and apply it to the live canvas.
    if (const auto index = CanvasWidget::selection_tool_index(current_tool_); index >= 0) {
      selection_modes_[static_cast<std::size_t>(index)] = mode;
    }
    if (canvas_ != nullptr) {
      canvas_->set_selection_mode(mode);
    }
    refresh_options_bar();
  };
  connect(selection_new, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Replace); });
  connect(selection_add, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Add); });
  connect(selection_subtract, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Subtract); });
  connect(selection_intersect, &QAction::triggered, this,
          [set_selection_mode] { set_selection_mode(CanvasWidget::SelectionMode::Intersect); });
  add_option_separator({CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::PolygonLasso,
                        CanvasTool::MagneticLasso, CanvasTool::MagicWand, CanvasTool::QuickSelect,
                        CanvasTool::ObjectSelect, CanvasTool::PatchTool});

  // The Magic Wand's Sample (GitHub issue 435): the seed color averages this square
  // around the click. It is the Pick tool's Size setting, shared as Photoshop shares it,
  // so this combo and eyedropperSampleSizeCombo mirror one value.
  add_option_label(QT_TR_NOOP("Sample:"), {CanvasTool::MagicWand});
  wand_sample_size_combo_ = new QComboBox(toolbar);
  wand_sample_size_combo_->setObjectName(QStringLiteral("wandSampleSizeCombo"));
  fill_sample_size_combo(wand_sample_size_combo_, current_eyedropper_sample_size_);
  bind_tooltip(wand_sample_size_combo_,
               QT_TR_NOOP("Compare against the average color of a square around the click instead of one pixel"));
  add_option_widget(wand_sample_size_combo_, {CanvasTool::MagicWand});
  connect(wand_sample_size_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || wand_sample_size_combo_ == nullptr) {
      return;
    }
    apply_shared_sample_size(wand_sample_size_combo_->itemData(index).toInt());
  });
  add_option_separator({CanvasTool::MagicWand});

  // Quick Select leads with its brush Size, set apart from Feather (GitHub issue 291).
  add_option_label(QT_TR_NOOP("Size:"), {CanvasTool::QuickSelect});
  auto* quick_select_size = new QSpinBox(toolbar);
  quick_select_size->setObjectName(QStringLiteral("quickSelectSizeSpin"));
  quick_select_size->setRange(1, 512);
  quick_select_size->setValue(canvas_defaults->quick_select_size());
  configure_toolbar_spinbox(quick_select_size, 46);
  add_option_widget(quick_select_size, {CanvasTool::QuickSelect});
  auto* quick_select_size_slider = new QSlider(Qt::Horizontal, toolbar);
  quick_select_size_slider->setObjectName(QStringLiteral("quickSelectSizeSlider"));
  quick_select_size_slider->setProperty(kOptionsBarCollapsibleProperty, true);
  bind_curved_slider(*quick_select_size_slider, *quick_select_size);
  quick_select_size_slider->setFixedWidth(150);
  bind_tooltip(quick_select_size_slider, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Quick Select brush size: press [ or ]"));
  add_option_widget(quick_select_size_slider, {CanvasTool::QuickSelect});
  connect(quick_select_size, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_quick_select_size(value);
      canvas_->refresh_tool_cursor();
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  add_option_separator({CanvasTool::QuickSelect});

  // A plain label and field like every other option. Feather used to share one
  // nested group widget, whose height clipped the field's bottom edge on some
  // desktops (GitHub issue 160).
  add_option_label(QT_TR_NOOP("Feather:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                                            CanvasTool::PolygonLasso, CanvasTool::MagneticLasso, CanvasTool::MagicWand,
                                            CanvasTool::QuickSelect, CanvasTool::ObjectSelect})
      ->setObjectName(QStringLiteral("selectionFeatherLabel"));
  auto* feather = new UnitIntSpinBox(SpinUnit::Pixels, options_content);
  feather->setObjectName(QStringLiteral("selectionFeatherSpin"));
  feather->set_context_provider(document_unit_context_provider(true));  // "2 mm" converts at the document PPI
  feather->setRange(0, kMaxSelectionFeatherRadius);
  feather->setValue(current_selection_feather_radius_);
  feather->setProperty(kToolbarSpinboxSliderCurvedProperty, true);
  configure_toolbar_spinbox(feather, 64);
  add_option_widget(feather, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso,
                              CanvasTool::PolygonLasso, CanvasTool::MagneticLasso, CanvasTool::MagicWand,
                              CanvasTool::QuickSelect, CanvasTool::ObjectSelect});
  // Quick Select has no Anti-alias, so its checkboxes stand apart from Feather (GitHub
  // issue 291); everywhere else Anti-alias stays beside Feather.
  add_option_separator({CanvasTool::QuickSelect});
  auto* anti_alias = new CheckGlyphBox(tr("Anti-alias"), toolbar);
  bind_widget_text(anti_alias, QT_TR_NOOP("Anti-alias"));
  anti_alias->setObjectName(QStringLiteral("selectionAntiAliasCheck"));
  anti_alias->setChecked(current_selection_antialias_);
  add_option_widget(anti_alias,
                    {CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::PolygonLasso,
                     CanvasTool::MagneticLasso, CanvasTool::MagicWand, CanvasTool::ObjectSelect});
  // Feather and Anti-alias are one group. The Lassos and the Magic Wand get their separator
  // after the marquee fields below, which they do not show. Object Selection runs Feather,
  // Anti-alias and Sample All Layers together up to Model (GitHub issue 292).
  add_option_separator({CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  const auto apply_selection_edge_settings = [this, feather, anti_alias] {
    current_selection_feather_radius_ = feather->value();
    current_selection_antialias_ = anti_alias->isChecked();
    // Each selection tool keeps its own Feather and Anti-alias.
    if (const auto index = CanvasWidget::selection_tool_index(current_tool_); index >= 0) {
      selection_feather_by_tool_[static_cast<std::size_t>(index)] = current_selection_feather_radius_;
      selection_antialias_by_tool_[static_cast<std::size_t>(index)] = current_selection_antialias_;
    }
    if (canvas_ != nullptr) {
      canvas_->set_selection_feather_radius(current_selection_feather_radius_);
      canvas_->set_selection_antialias(current_selection_antialias_);
    }
    refresh_document_info();
  };
  connect(feather, &QSpinBox::valueChanged, this, [this, apply_selection_edge_settings](int) {
    apply_selection_edge_settings();
    schedule_save_tool_settings();
  });
  connect(anti_alias, &QCheckBox::toggled, this, [this, apply_selection_edge_settings](bool) {
    apply_selection_edge_settings();
    save_tool_settings();
  });
  add_option_label(QT_TR_NOOP("Radius:"), {CanvasTool::Marquee});
  auto* marquee_corner_radius = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  marquee_corner_radius->setObjectName(QStringLiteral("selectionCornerRadiusSpin"));
  marquee_corner_radius->set_context_provider(document_unit_context_provider(true));
  marquee_corner_radius->setRange(0, 512);
  marquee_corner_radius->setValue(current_marquee_corner_radius_);
  bind_tooltip(marquee_corner_radius, QT_TR_NOOP("Rounded-corner radius for the rectangular marquee (0 = sharp corners)"));
  configure_toolbar_spinbox(marquee_corner_radius, 64);
  add_option_widget(marquee_corner_radius, {CanvasTool::Marquee});
  connect(marquee_corner_radius, &QSpinBox::valueChanged, this, [this](int value) {
    current_marquee_corner_radius_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_marquee_corner_radius(value);
    }
  });
  add_option_label(QT_TR_NOOP("Style:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  auto* style_combo = new QComboBox(toolbar);
  style_combo->setObjectName(QStringLiteral("selectionStyleCombo"));
  style_combo->addItems({tr("Normal"), tr("Fixed Ratio"), tr("Fixed Size")});
  style_combo->setCurrentText(tr("Normal"));
  style_combo->setFixedWidth(92);
  QPointer<QComboBox> selection_style_combo(style_combo);
  register_retranslation([selection_style_combo] {
    if (selection_style_combo == nullptr || selection_style_combo->count() < 3) {
      return;
    }
    QSignalBlocker blocker(selection_style_combo);
    selection_style_combo->setItemText(0, QObject::tr("Normal"));
    selection_style_combo->setItemText(1, QObject::tr("Fixed Ratio"));
    selection_style_combo->setItemText(2, QObject::tr("Fixed Size"));
  });
  add_option_widget(style_combo, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  add_option_separator({CanvasTool::Marquee, CanvasTool::EllipticalMarquee});
  // Fixed Ratio has its own unitless pair (1 : 1 by default, as Photoshop); Normal and
  // Fixed Size show the pixel fields (GitHub issue 201). marquee_option_widget_visible
  // swaps the two sets.
  marquee_ratio_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("Width:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee}));
  auto* ratio_width = new TrimmedDoubleSpinBox(toolbar);
  ratio_width->setObjectName(QStringLiteral("selectionRatioWidthSpin"));
  ratio_width->setRange(0.01, 1000.0);
  ratio_width->setDecimals(2);
  ratio_width->setValue(current_marquee_ratio_w_);
  bind_tooltip(ratio_width, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Width part of the selection's aspect ratio"));
  configure_toolbar_spinbox(ratio_width, 56);
  marquee_ratio_option_widgets_.push_back(
      add_option_widget(ratio_width, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee}));
  // Width and Height trade places with one click (GitHub issue 286), here and on the Artboard
  // bar: two opposed arrows in the icon ink, an icon button sized like the bar's others.
  // Each marquee pair has its own button; Normal, whose fields are greyed, shows neither.
  const auto make_swap_dimensions_button = [toolbar](const char* object_name) {
    auto* button = new QToolButton(toolbar);
    button->setObjectName(QLatin1String(object_name));
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIcon(simple_icon(QStringLiteral("swap-dimensions")));
    button->setIconSize(QSize(18, 18));
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setProperty("optionsBarButton", true);
    bind_tooltip(button, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Swap width and height (portrait or landscape)"));
    // Even space on both sides (GitHub issue 286): the Height label after the button drops
    // its text padding (label_after_swap_button) and the button adds 2 px on its left, so the
    // glyph sits as far from the Width field's edge as from the label's text (11 px each).
    button->setStyleSheet(QStringLiteral("QToolButton { margin-left: 2px; }"));
    return button;
  };
  const auto label_after_swap_button = [](QWidget* label) {
    label->setStyleSheet(QStringLiteral("QLabel { padding-left: 0; }"));
    return label;
  };
  auto* ratio_swap = make_swap_dimensions_button("selectionRatioSwapButton");
  marquee_ratio_option_widgets_.push_back(
      add_option_widget(ratio_swap, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee}));
  marquee_ratio_option_widgets_.push_back(label_after_swap_button(
      add_option_label(QT_TR_NOOP("Height:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee})));
  auto* ratio_height = new TrimmedDoubleSpinBox(toolbar);
  ratio_height->setObjectName(QStringLiteral("selectionRatioHeightSpin"));
  ratio_height->setRange(0.01, 1000.0);
  ratio_height->setDecimals(2);
  ratio_height->setValue(current_marquee_ratio_h_);
  bind_tooltip(ratio_height, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Height part of the selection's aspect ratio"));
  configure_toolbar_spinbox(ratio_height, 56);
  marquee_ratio_option_widgets_.push_back(
      add_option_widget(ratio_height, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee}));
  marquee_size_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("Width:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee}));
  auto* fixed_width = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  fixed_width->set_context_provider(document_axis_context(true));
  fixed_width->setObjectName(QStringLiteral("selectionFixedWidthSpin"));
  fixed_width->setRange(1, 30000);
  fixed_width->setValue(has_active_document() ? document().width() : 1024);
  configure_toolbar_spinbox(fixed_width, 78);
  marquee_size_option_widgets_.push_back(
      add_option_widget(fixed_width, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee}));
  auto* size_swap = make_swap_dimensions_button("selectionFixedSizeSwapButton");
  marquee_fixed_size_option_widgets_.push_back(
      add_option_widget(size_swap, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee}));
  marquee_size_option_widgets_.push_back(label_after_swap_button(
      add_option_label(QT_TR_NOOP("Height:"), {CanvasTool::Marquee, CanvasTool::EllipticalMarquee})));
  auto* fixed_height = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  fixed_height->set_context_provider(document_axis_context(false));
  fixed_height->setObjectName(QStringLiteral("selectionFixedHeightSpin"));
  fixed_height->setRange(1, 30000);
  fixed_height->setValue(has_active_document() ? document().height() : 768);
  configure_toolbar_spinbox(fixed_height, 78);
  marquee_size_option_widgets_.push_back(
      add_option_widget(fixed_height, {CanvasTool::Marquee, CanvasTool::EllipticalMarquee}));
  const auto apply_marquee_settings = [this, style_combo, fixed_width, fixed_height, ratio_width, ratio_height] {
    // Normal ignores the size fields, so they grey out (Fixed Ratio reads
    // them as the ratio, Fixed Size as the size).
    fixed_width->setEnabled(style_combo->currentIndex() != 0);
    fixed_height->setEnabled(style_combo->currentIndex() != 0);
    switch (style_combo->currentIndex()) {
      case 1:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::FixedRatio;
        break;
      case 2:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::FixedSize;
        break;
      default:
        current_marquee_style_ = CanvasWidget::MarqueeStyle::Normal;
        break;
    }
    current_marquee_width_ = fixed_width->value();
    current_marquee_height_ = fixed_height->value();
    current_marquee_ratio_w_ = ratio_width->value();
    current_marquee_ratio_h_ = ratio_height->value();
    if (canvas_ != nullptr) {
      canvas_->set_marquee_style(current_marquee_style_);
      const auto fixed = marquee_fixed_size_for_canvas();
      canvas_->set_marquee_fixed_size(fixed.width(), fixed.height());
    }
  };
  connect(style_combo, &QComboBox::currentIndexChanged, this, [this, apply_marquee_settings](int) {
    apply_marquee_settings();
    refresh_options_bar();
  });
  connect(ratio_width, &QDoubleSpinBox::valueChanged, this, [apply_marquee_settings](double) {
    apply_marquee_settings();
  });
  connect(ratio_height, &QDoubleSpinBox::valueChanged, this, [apply_marquee_settings](double) {
    apply_marquee_settings();
  });
  connect(fixed_width, &QSpinBox::valueChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  connect(fixed_height, &QSpinBox::valueChanged, this, [apply_marquee_settings](int) {
    apply_marquee_settings();
  });
  connect(ratio_swap, &QToolButton::clicked, this, [ratio_width, ratio_height] {
    const double width = ratio_width->value();
    ratio_width->setValue(ratio_height->value());
    ratio_height->setValue(width);
  });
  connect(size_swap, &QToolButton::clicked, this, [fixed_width, fixed_height] {
    const int width = fixed_width->value();
    fixed_width->setValue(fixed_height->value());
    fixed_height->setValue(width);
  });
  apply_marquee_settings();
  add_option_separator({CanvasTool::Marquee, CanvasTool::EllipticalMarquee, CanvasTool::Lasso, CanvasTool::PolygonLasso,
                        CanvasTool::MagneticLasso, CanvasTool::MagicWand});

  // Crop: Style picks what the fields mean, like the marquee's Style combo.
  // Ratio shows the preset combo and the unitless W : H pair; Size shows unit
  // Width / Height fields that mirror the box and resize it about its center.
  add_option_label(QT_TR_NOOP("Style:"), {CanvasTool::Crop});
  crop_style_combo_ = new QComboBox(toolbar);
  crop_style_combo_->setObjectName(QStringLiteral("cropStyleCombo"));
  crop_style_combo_->addItems({tr("Ratio"), tr("Size")});
  crop_style_combo_->setFixedWidth(72);
  QPointer<QComboBox> crop_style_combo(crop_style_combo_);
  register_retranslation([crop_style_combo] {
    if (crop_style_combo == nullptr || crop_style_combo->count() < 2) {
      return;
    }
    QSignalBlocker blocker(crop_style_combo);
    crop_style_combo->setItemText(0, QObject::tr("Ratio"));
    crop_style_combo->setItemText(1, QObject::tr("Size"));
  });
  bind_tooltip(crop_style_combo_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Ratio constrains the crop box; Size shows its exact width and height and sets them"));
  add_option_widget(crop_style_combo_, {CanvasTool::Crop});
  add_option_separator({CanvasTool::Crop});
  crop_ratio_option_widgets_.push_back(add_option_label(QT_TR_NOOP("Ratio:"), {CanvasTool::Crop}));
  crop_ratio_preset_combo_ = new QComboBox(toolbar);
  crop_ratio_preset_combo_->setObjectName(QStringLiteral("cropRatioPresetCombo"));
  crop_ratio_preset_combo_->setMinimumWidth(118);
  // Index 0 "None" and the last index "Custom" are fixed anchors: the preset
  // handler and sync_crop_ratio_preset_combo key on them. "Original Ratio"
  // (index 1) reads the active document at selection time.
  crop_ratio_preset_combo_->addItem(tr("None"));
  crop_ratio_preset_combo_->addItem(tr("Original Ratio"));
  crop_ratio_preset_combo_->addItem(tr("1 : 1 (Square)"), QSizeF(1.0, 1.0));
  crop_ratio_preset_combo_->addItem(QStringLiteral("4 : 5 (8 : 10)"), QSizeF(4.0, 5.0));
  crop_ratio_preset_combo_->addItem(QStringLiteral("5 : 7"), QSizeF(5.0, 7.0));
  crop_ratio_preset_combo_->addItem(QStringLiteral("2 : 3 (4 : 6)"), QSizeF(2.0, 3.0));
  crop_ratio_preset_combo_->addItem(QStringLiteral("16 : 9"), QSizeF(16.0, 9.0));
  crop_ratio_preset_combo_->addItem(tr("Custom"));
  QPointer<QComboBox> crop_preset_combo(crop_ratio_preset_combo_);
  register_retranslation([crop_preset_combo] {
    if (crop_preset_combo == nullptr || crop_preset_combo->count() < 8) {
      return;
    }
    QSignalBlocker blocker(crop_preset_combo);
    crop_preset_combo->setItemText(0, QObject::tr("None"));
    crop_preset_combo->setItemText(1, QObject::tr("Original Ratio"));
    crop_preset_combo->setItemText(2, QObject::tr("1 : 1 (Square)"));
    crop_preset_combo->setItemText(crop_preset_combo->count() - 1, QObject::tr("Custom"));
  });
  bind_tooltip(crop_ratio_preset_combo_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Aspect ratio preset for the crop box"));
  crop_ratio_option_widgets_.push_back(add_option_widget(crop_ratio_preset_combo_, {CanvasTool::Crop}));
  crop_ratio_w_spin_ = new TrimmedDoubleSpinBox(toolbar);
  crop_ratio_w_spin_->setObjectName(QStringLiteral("cropRatioWidthSpin"));
  crop_ratio_w_spin_->setRange(0.0, 10000.0);
  crop_ratio_w_spin_->setDecimals(2);
  crop_ratio_w_spin_->setValue(0.0);
  bind_tooltip(crop_ratio_w_spin_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Aspect ratio width (0 = unconstrained)"));
  configure_toolbar_spinbox(crop_ratio_w_spin_, 64);
  crop_ratio_option_widgets_.push_back(add_option_widget(crop_ratio_w_spin_, {CanvasTool::Crop}));
  auto* crop_ratio_separator = new QLabel(QStringLiteral(":"), options_content);
  crop_ratio_separator->setProperty("optionLabel", true);
  crop_ratio_separator->setAlignment(Qt::AlignVCenter);
  crop_ratio_option_widgets_.push_back(add_option_widget(crop_ratio_separator, {CanvasTool::Crop}));
  crop_ratio_h_spin_ = new TrimmedDoubleSpinBox(toolbar);
  crop_ratio_h_spin_->setObjectName(QStringLiteral("cropRatioHeightSpin"));
  crop_ratio_h_spin_->setRange(0.0, 10000.0);
  crop_ratio_h_spin_->setDecimals(2);
  crop_ratio_h_spin_->setValue(0.0);
  bind_tooltip(crop_ratio_h_spin_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Aspect ratio height (0 = unconstrained)"));
  configure_toolbar_spinbox(crop_ratio_h_spin_, 64);
  crop_ratio_option_widgets_.push_back(add_option_widget(crop_ratio_h_spin_, {CanvasTool::Crop}));
  crop_ratio_clear_button_ = new QPushButton(tr("Clear"), toolbar);
  crop_ratio_clear_button_->setObjectName(QStringLiteral("cropRatioClearButton"));
  bind_widget_text(crop_ratio_clear_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Clear"));
  bind_tooltip(crop_ratio_clear_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Clear the aspect ratio constraint"));
  crop_ratio_option_widgets_.push_back(add_option_widget(crop_ratio_clear_button_, {CanvasTool::Crop}));
  // Size mode: the same unit fields as the marquee's Fixed Size pair (pixel
  // native, a typed unit token converts at the document PPI, the same quick
  // values in the popup). Values commit on Enter or focus-out, never per
  // keystroke, so a half-typed width never reshapes the box.
  crop_size_option_widgets_.push_back(add_option_label(QT_TR_NOOP("Width:"), {CanvasTool::Crop}));
  crop_width_spin_ = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  crop_width_spin_->set_context_provider(document_axis_context(true));
  crop_width_spin_->setObjectName(QStringLiteral("cropWidthSpin"));
  crop_width_spin_->setRange(1, 30000);
  crop_width_spin_->setKeyboardTracking(false);
  bind_tooltip(crop_width_spin_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Width of the crop box"));
  configure_toolbar_spinbox(crop_width_spin_, 78);
  crop_size_option_widgets_.push_back(add_option_widget(crop_width_spin_, {CanvasTool::Crop}));
  crop_link_size_button_ = new QPushButton(toolbar);
  crop_link_size_button_->setObjectName(QStringLiteral("cropLinkSizeButton"));
  crop_link_size_button_->setCheckable(true);
  crop_link_size_button_->setChecked(false);
  crop_link_size_button_->setIcon(simple_icon(QStringLiteral("link"), QColor(220, 226, 235)));
  bind_tooltip(crop_link_size_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Keep the crop box's width and height in proportion"));
  crop_link_size_button_->setFixedWidth(28);
  crop_size_option_widgets_.push_back(add_option_widget(crop_link_size_button_, {CanvasTool::Crop}));
  crop_size_option_widgets_.push_back(add_option_label(QT_TR_NOOP("Height:"), {CanvasTool::Crop}));
  crop_height_spin_ = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  crop_height_spin_->set_context_provider(document_axis_context(false));
  crop_height_spin_->setObjectName(QStringLiteral("cropHeightSpin"));
  crop_height_spin_->setRange(1, 30000);
  crop_height_spin_->setKeyboardTracking(false);
  bind_tooltip(crop_height_spin_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Height of the crop box"));
  configure_toolbar_spinbox(crop_height_spin_, 78);
  crop_size_option_widgets_.push_back(add_option_widget(crop_height_spin_, {CanvasTool::Crop}));
  // Composition overlay (GitHub issue 325): the guides inside the box. The Crop
  // options menu below turns the Triangle and Golden Spiral and chooses whether
  // the guides show all the time or only while the box is dragged. No label (the
  // entries name themselves, "No Overlay" first) and a fixed width keep the Crop
  // bar on one row at the 1180 px test width. Items follow
  // CanvasWidget::CropOverlay; the data is the persisted tools/cropOverlay token.
  static constexpr std::array<std::pair<const char*, const char*>, 7> kCropOverlayItems = {{
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "No Overlay"), "none"},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Rule of Thirds"), "thirds"},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Grid"), "grid"},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Diagonal"), "diagonal"},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Triangle"), "triangle"},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Golden Ratio"), "goldenRatio"},
      {QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Golden Spiral"), "goldenSpiral"},
  }};
  add_option_separator({CanvasTool::Crop});
  crop_overlay_combo_ = new QComboBox(toolbar);
  crop_overlay_combo_->setObjectName(QStringLiteral("cropOverlayCombo"));
  for (const auto& [source, token] : kCropOverlayItems) {
    crop_overlay_combo_->addItem(tr(source), QString::fromLatin1(token));
  }
  crop_overlay_combo_->setCurrentIndex(current_crop_overlay_);
  crop_overlay_combo_->setFixedWidth(112);
  QPointer<QComboBox> crop_overlay_combo(crop_overlay_combo_);
  register_retranslation([crop_overlay_combo] {
    if (crop_overlay_combo == nullptr || crop_overlay_combo->count() != static_cast<int>(kCropOverlayItems.size())) {
      return;
    }
    QSignalBlocker blocker(crop_overlay_combo);
    for (int i = 0; i < crop_overlay_combo->count(); ++i) {
      crop_overlay_combo->setItemText(
          i, QCoreApplication::translate("patchy::ui::MainWindow", kCropOverlayItems[static_cast<std::size_t>(i)].first));
    }
  });
  bind_tooltip(crop_overlay_combo_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Composition guides drawn inside the crop box"));
  add_option_widget(crop_overlay_combo_, {CanvasTool::Crop});
  connect(crop_overlay_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0) {
      return;
    }
    current_crop_overlay_ = index;
    apply_crop_guide_options();
    schedule_save_tool_settings();
  });
  // Crop options (GitHub issues 325 and 326): the overlay's turn and when it
  // shows, then how the area outside the box draws. Its
  // own group after the checkboxes (options-bar rule 1: advanced options), on a
  // gear menu button (make_options_menu_button).
  crop_options_button_ = new QToolButton(toolbar);
  crop_options_button_->setObjectName(QStringLiteral("cropOptionsButton"));
  make_options_menu_button(crop_options_button_);
  bind_tooltip(crop_options_button_, QT_TR_NOOP("Crop options: overlay, cropped area and crop shield"));
  {
    auto* crop_menu = new QMenu(crop_options_button_);
    crop_menu->setObjectName(QStringLiteral("cropOptionsMenu"));
    // Steps the orientation 0-3: the Triangle mirrors, the Golden Spiral visits
    // each corner. Enabled for those two only.
    crop_overlay_turn_action_ = crop_menu->addAction(tr("Turn Overlay"));
    crop_overlay_turn_action_->setObjectName(QStringLiteral("cropOverlayTurnAction"));
    bind_action_text(crop_overlay_turn_action_, QT_TR_NOOP("Turn Overlay"));
    connect(crop_overlay_turn_action_, &QAction::triggered, this, [this] {
      current_crop_overlay_orientation_ = (current_crop_overlay_orientation_ + 1) % 4;
      apply_crop_guide_options();
      schedule_save_tool_settings();
    });
    crop_menu->addSeparator();
    auto* show_group = new QActionGroup(crop_menu);
    show_group->setExclusive(true);
    const auto add_show_choice = [this, crop_menu, show_group](const char* source, const char* object_name,
                                                                  bool while_dragging) {
      auto* action = crop_menu->addAction(tr(source));
      action->setObjectName(QLatin1String(object_name));
      bind_action_text(action, source);
      action->setCheckable(true);
      action->setChecked(current_crop_overlay_while_dragging_ == while_dragging);
      show_group->addAction(action);
      connect(action, &QAction::triggered, this, [this, while_dragging] {
        current_crop_overlay_while_dragging_ = while_dragging;
        apply_crop_guide_options();
        schedule_save_tool_settings();
      });
      return action;
    };
    crop_overlay_show_always_action_ =
        add_show_choice(QT_TR_NOOP("Always Show Overlay"), "cropOverlayShowAlwaysAction", false);
    crop_overlay_show_while_dragging_action_ =
        add_show_choice(QT_TR_NOOP("Show Overlay Only While Dragging"), "cropOverlayShowWhileDraggingAction", true);
    crop_menu->addSeparator();
    crop_show_cropped_area_action_ = crop_menu->addAction(tr("Show Cropped Area"));
    crop_show_cropped_area_action_->setObjectName(QStringLiteral("cropShowCroppedAreaAction"));
    bind_action_text(crop_show_cropped_area_action_, QT_TR_NOOP("Show Cropped Area"));
    crop_show_cropped_area_action_->setCheckable(true);
    connect(crop_show_cropped_area_action_, &QAction::toggled, this, [this](bool on) {
      auto options = current_crop_overlay_options_;
      options.show_cropped_area = on;
      apply_crop_overlay_options(options);
    });
    crop_menu->addSeparator();
    crop_shield_action_ = crop_menu->addAction(tr("Enable Crop Shield"));
    crop_shield_action_->setObjectName(QStringLiteral("cropShieldAction"));
    bind_action_text(crop_shield_action_, QT_TR_NOOP("Enable Crop Shield"));
    crop_shield_action_->setCheckable(true);
    connect(crop_shield_action_, &QAction::toggled, this, [this](bool on) {
      auto options = current_crop_overlay_options_;
      options.shield_enabled = on;
      apply_crop_overlay_options(options);
    });
    // The shield's color and opacity share one row under the toggle.
    auto* shield_row = new QWidget(crop_menu);
    shield_row->setObjectName(QStringLiteral("cropShieldRow"));
    auto* shield_layout = new QHBoxLayout(shield_row);
    shield_layout->setContentsMargins(24, 2, 10, 4);
    shield_layout->setSpacing(6);
    auto* color_label = new QLabel(tr("Color:"), shield_row);
    bind_widget_text(color_label, QT_TR_NOOP("Color:"));
    shield_layout->addWidget(color_label);
    crop_shield_color_button_ = new QPushButton(shield_row);
    crop_shield_color_button_->setObjectName(QStringLiteral("cropShieldColorButton"));
    crop_shield_color_button_->setFixedSize(36, 20);
    bind_tooltip(crop_shield_color_button_, QT_TR_NOOP("Color of the crop shield"));
    shield_layout->addWidget(crop_shield_color_button_);
    shield_layout->addSpacing(8);
    auto* opacity_label = new QLabel(tr("Opacity:"), shield_row);
    bind_widget_text(opacity_label, QT_TR_NOOP("Opacity:"));
    shield_layout->addWidget(opacity_label);
    crop_shield_opacity_spin_ = new QSpinBox(shield_row);
    crop_shield_opacity_spin_->setObjectName(QStringLiteral("cropShieldOpacitySpin"));
    crop_shield_opacity_spin_->setRange(0, 100);
    crop_shield_opacity_spin_->setSuffix(percent_suffix());
    bind_tooltip(crop_shield_opacity_spin_, QT_TR_NOOP("Opacity of the crop shield"));
    configure_dialog_spinbox(crop_shield_opacity_spin_, 64);
    shield_layout->addWidget(crop_shield_opacity_spin_);
    auto* shield_action = new QWidgetAction(crop_menu);
    shield_action->setDefaultWidget(shield_row);
    crop_menu->addAction(shield_action);
    connect(crop_shield_opacity_spin_, &QSpinBox::valueChanged, this, [this](int percent) {
      auto options = current_crop_overlay_options_;
      options.shield_opacity_percent = percent;
      apply_crop_overlay_options(options);
    });
    // The picker is a dialog: close the popup first so it does not hold the input grab.
    connect(crop_shield_color_button_, &QPushButton::clicked, this, [this, crop_menu] {
      crop_menu->close();
      QTimer::singleShot(0, this, [this] { choose_crop_shield_color(); });
    });
    crop_options_button_->setMenu(crop_menu);
  }
  sync_crop_overlay_controls();
  apply_crop_guide_options();
  // Canvas Size's two layer options, as Photoshop's "Delete Cropped Pixels"
  // (on by default there too) plus Patchy's own off-canvas layer delete.
  crop_delete_pixels_check_ = new CheckGlyphBox(tr("Delete Cropped Pixels"), toolbar);
  crop_delete_pixels_check_->setObjectName(QStringLiteral("cropDeletePixelsCheck"));
  crop_delete_pixels_check_->setChecked(current_crop_delete_pixels_);
  bind_tooltip(crop_delete_pixels_check_,
               QT_TR_NOOP("Crop every layer to the new canvas; off, layers keep their pixels beyond it"));
  add_option_separator({CanvasTool::Crop});
  add_option_widget(crop_delete_pixels_check_, {CanvasTool::Crop});
  connect(crop_delete_pixels_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_crop_delete_pixels_ = checked;
    save_tool_settings();
  });
  crop_delete_layers_check_ = new CheckGlyphBox(tr("Delete Off-Canvas Layers"), toolbar);
  crop_delete_layers_check_->setObjectName(QStringLiteral("cropDeleteLayersCheck"));
  crop_delete_layers_check_->setChecked(current_crop_delete_layers_);
  bind_tooltip(crop_delete_layers_check_,
               QT_TR_NOOP("Delete layers that end up fully outside the crop box"));
  add_option_widget(crop_delete_layers_check_, {CanvasTool::Crop});
  connect(crop_delete_layers_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_crop_delete_layers_ = checked;
    save_tool_settings();
  });
  add_option_separator({CanvasTool::Crop});
  add_option_widget(crop_options_button_, {CanvasTool::Crop});
  crop_apply_button_ = new QPushButton(toolbar);
  crop_apply_button_->setObjectName(QStringLiteral("cropApplyButton"));
  crop_apply_button_->setIcon(simple_icon(QStringLiteral("ok")));
  bind_tooltip(crop_apply_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Apply crop (Enter)"));
  crop_apply_button_->setFixedWidth(30);
  crop_apply_button_->setIconSize(QSize(20, 20));
  crop_apply_button_->setProperty("optionsSessionButton", true);
  add_option_separator({CanvasTool::Crop});
  add_option_widget(crop_apply_button_, {CanvasTool::Crop});
  crop_cancel_button_ = new QPushButton(toolbar);
  crop_cancel_button_->setObjectName(QStringLiteral("cropCancelButton"));
  crop_cancel_button_->setIcon(simple_icon(QStringLiteral("cancel")));
  bind_tooltip(crop_cancel_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Reset the crop box to the canvas (Esc)"));
  crop_cancel_button_->setFixedWidth(30);
  crop_cancel_button_->setIconSize(QSize(20, 20));
  crop_cancel_button_->setProperty("optionsSessionButton", true);
  add_option_widget(crop_cancel_button_, {CanvasTool::Crop});

  // Perspective Crop (GitHub issue 328, docs/crop-tool.md): the result's size and
  // resolution. A blank W or H takes the quad's mean edge lengths, a blank Resolution
  // keeps the document's; Front Image fills all three from the active document and
  // Clear empties them, as in Photoshop. The fields are pixel native with typed unit
  // tokens converting at the document PPI, like the Crop tool's Size fields.
  add_option_label(QT_TR_NOOP("W:"), {CanvasTool::PerspectiveCrop});
  perspective_crop_width_spin_ = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  perspective_crop_width_spin_->set_context_provider(document_axis_context(true));
  perspective_crop_width_spin_->setObjectName(QStringLiteral("perspectiveCropWidthSpin"));
  perspective_crop_width_spin_->setRange(0, 300000);
  perspective_crop_width_spin_->setSpecialValueText(QStringLiteral(" "));  // 0 = from the corners
  perspective_crop_width_spin_->setKeyboardTracking(false);
  bind_tooltip(perspective_crop_width_spin_,
               QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Width of the result; blank uses the spacing of the corners"));
  configure_toolbar_spinbox(perspective_crop_width_spin_, 78);
  add_option_widget(perspective_crop_width_spin_, {CanvasTool::PerspectiveCrop});
  auto* perspective_swap = make_swap_dimensions_button("perspectiveCropSwapButton");
  add_option_widget(perspective_swap, {CanvasTool::PerspectiveCrop});
  label_after_swap_button(add_option_label(QT_TR_NOOP("H:"), {CanvasTool::PerspectiveCrop}));
  perspective_crop_height_spin_ = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  perspective_crop_height_spin_->set_context_provider(document_axis_context(false));
  perspective_crop_height_spin_->setObjectName(QStringLiteral("perspectiveCropHeightSpin"));
  perspective_crop_height_spin_->setRange(0, 300000);
  perspective_crop_height_spin_->setSpecialValueText(QStringLiteral(" "));
  perspective_crop_height_spin_->setKeyboardTracking(false);
  bind_tooltip(perspective_crop_height_spin_,
               QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Height of the result; blank uses the spacing of the corners"));
  configure_toolbar_spinbox(perspective_crop_height_spin_, 78);
  add_option_widget(perspective_crop_height_spin_, {CanvasTool::PerspectiveCrop});
  add_option_label(QT_TR_NOOP("Resolution:"), {CanvasTool::PerspectiveCrop});
  auto* perspective_resolution = new TrimmedDoubleSpinBox(toolbar);
  perspective_crop_resolution_spin_ = perspective_resolution;
  perspective_resolution->setObjectName(QStringLiteral("perspectiveCropResolutionSpin"));
  perspective_resolution->setRange(0.0, 9999.0);
  perspective_resolution->setDecimals(2);
  perspective_resolution->setSpecialValueText(QStringLiteral(" "));  // 0 = keep the document's
  perspective_resolution->setKeyboardTracking(false);
  bind_tooltip(perspective_resolution, QT_TRANSLATE_NOOP("patchy::ui::MainWindow",
                                                         "Resolution of the result in pixels per inch; blank keeps the "
                                                         "document's"));
  configure_toolbar_spinbox(perspective_resolution, 64);
  add_option_widget(perspective_resolution, {CanvasTool::PerspectiveCrop});
  add_option_separator({CanvasTool::PerspectiveCrop});
  auto* perspective_front_image = new QPushButton(tr("Front Image"), toolbar);
  perspective_front_image->setObjectName(QStringLiteral("perspectiveCropFrontImageButton"));
  bind_widget_text(perspective_front_image, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Front Image"));
  bind_tooltip(perspective_front_image, QT_TRANSLATE_NOOP("patchy::ui::MainWindow",
                                                          "Fill in the width, height and resolution of this document"));
  add_option_widget(perspective_front_image, {CanvasTool::PerspectiveCrop});
  auto* perspective_clear = new QPushButton(tr("Clear"), toolbar);
  perspective_clear->setObjectName(QStringLiteral("perspectiveCropClearButton"));
  bind_widget_text(perspective_clear, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Clear"));
  bind_tooltip(perspective_clear, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Clear the width, height and resolution"));
  add_option_widget(perspective_clear, {CanvasTool::PerspectiveCrop});
  add_option_separator({CanvasTool::PerspectiveCrop});
  // Moves the up marker to the next side: the result turns a quarter clockwise
  // without refitting the corners (a page photographed sideways).
  perspective_crop_turn_button_ = new QToolButton(toolbar);
  perspective_crop_turn_button_->setObjectName(QStringLiteral("perspectiveCropTurnButton"));
  perspective_crop_turn_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  perspective_crop_turn_button_->setIcon(simple_icon(QStringLiteral("rotate")));
  perspective_crop_turn_button_->setIconSize(QSize(18, 18));
  perspective_crop_turn_button_->setAutoRaise(true);
  perspective_crop_turn_button_->setFocusPolicy(Qt::NoFocus);
  perspective_crop_turn_button_->setProperty("optionsBarButton", true);
  bind_tooltip(perspective_crop_turn_button_,
               QT_TRANSLATE_NOOP("patchy::ui::MainWindow",
                                 "Turn the result a quarter turn: the top marker moves to the next side"));
  add_option_widget(perspective_crop_turn_button_, {CanvasTool::PerspectiveCrop});
  perspective_crop_grid_check_ = new CheckGlyphBox(tr("Show Grid"), toolbar);
  perspective_crop_grid_check_->setObjectName(QStringLiteral("perspectiveCropShowGridCheck"));
  perspective_crop_grid_check_->setChecked(current_perspective_crop_show_grid_);
  bind_tooltip(perspective_crop_grid_check_,
               QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Draw a grid inside the corners to line them up with the area"));
  add_option_widget(perspective_crop_grid_check_, {CanvasTool::PerspectiveCrop});
  perspective_crop_apply_button_ = new QPushButton(toolbar);
  perspective_crop_apply_button_->setObjectName(QStringLiteral("perspectiveCropApplyButton"));
  perspective_crop_apply_button_->setIcon(simple_icon(QStringLiteral("ok")));
  bind_tooltip(perspective_crop_apply_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Apply perspective crop (Enter)"));
  perspective_crop_apply_button_->setFixedWidth(30);
  perspective_crop_apply_button_->setIconSize(QSize(20, 20));
  perspective_crop_apply_button_->setProperty("optionsSessionButton", true);
  add_option_separator({CanvasTool::PerspectiveCrop});
  add_option_widget(perspective_crop_apply_button_, {CanvasTool::PerspectiveCrop});
  perspective_crop_cancel_button_ = new QPushButton(toolbar);
  perspective_crop_cancel_button_->setObjectName(QStringLiteral("perspectiveCropCancelButton"));
  perspective_crop_cancel_button_->setIcon(simple_icon(QStringLiteral("cancel")));
  bind_tooltip(perspective_crop_cancel_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Clear the corners (Esc)"));
  perspective_crop_cancel_button_->setFixedWidth(30);
  perspective_crop_cancel_button_->setIconSize(QSize(20, 20));
  perspective_crop_cancel_button_->setProperty("optionsSessionButton", true);
  add_option_widget(perspective_crop_cancel_button_, {CanvasTool::PerspectiveCrop});
  connect(perspective_swap, &QToolButton::clicked, this, [this] {
    const int width = perspective_crop_width_spin_->value();
    perspective_crop_width_spin_->setValue(perspective_crop_height_spin_->value());
    perspective_crop_height_spin_->setValue(width);
  });
  connect(perspective_front_image, &QPushButton::clicked, this, [this] {
    if (!has_active_document()) {
      return;
    }
    const auto& doc = std::as_const(document());
    perspective_crop_width_spin_->setValue(doc.width());
    perspective_crop_height_spin_->setValue(doc.height());
    const auto ppi = doc.print_settings().horizontal_ppi;
    perspective_crop_resolution_spin_->setValue(std::isfinite(ppi) && ppi > 0.0 ? ppi : 0.0);
  });
  connect(perspective_clear, &QPushButton::clicked, this, [this] {
    perspective_crop_width_spin_->setValue(0);
    perspective_crop_height_spin_->setValue(0);
    perspective_crop_resolution_spin_->setValue(0.0);
  });
  connect(perspective_crop_grid_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_perspective_crop_show_grid_ = checked;
    for (const auto& target_session : sessions_) {
      if (target_session != nullptr && target_session->canvas != nullptr) {
        target_session->canvas->set_perspective_crop_show_grid(checked);
      }
    }
    save_tool_settings();
  });
  connect(perspective_crop_turn_button_, &QToolButton::clicked, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->turn_perspective_crop_top();
    }
  });
  connect(perspective_crop_apply_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->commit_perspective_crop_session();
    }
  });
  connect(perspective_crop_cancel_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->cancel_perspective_crop_session();
    }
  });

  // Artboard tool (docs/artboards.md): the size and background show the selected
  // artboard and edit it; with none selected they are what a new artboard gets.
  add_option_label(QT_TR_NOOP("Size:"), {CanvasTool::Artboard});
  // The presets grouped by template group, with a search field (GitHub issue 285 follow-up):
  // the flat list of every New Document preset was too long to scan.
  artboard_preset_picker_ = new ArtboardPresetPicker(toolbar);
  QPointer<ArtboardPresetPicker> artboard_presets(artboard_preset_picker_);
  register_retranslation([artboard_presets] {
    if (artboard_presets != nullptr) {
      artboard_presets->rebuild();
    }
  });
  bind_tooltip(artboard_preset_picker_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Size preset for the selected artboard, or for new artboards"));
  add_option_widget(artboard_preset_picker_, {CanvasTool::Artboard});
  const auto make_artboard_size_spin = [this, toolbar](const char* name, const char* tooltip) {
    auto* spin = new QSpinBox(toolbar);
    spin->setObjectName(QLatin1String(name));
    spin->setRange(1, 30000);
    spin->setSuffix(pixel_suffix());
    QPointer<QSpinBox> guarded(spin);
    register_retranslation([guarded] {
      if (guarded != nullptr) {
        guarded->setSuffix(pixel_suffix());
      }
    });
    spin->setKeyboardTracking(false);
    bind_tooltip(spin, tooltip);
    configure_toolbar_spinbox(spin, 84);
    return spin;
  };
  // Separators set the size fields, the background and Add Artboard apart (GitHub issue 285).
  add_option_separator({CanvasTool::Artboard});
  add_option_label(QT_TR_NOOP("Width:"), {CanvasTool::Artboard});
  artboard_width_spin_ = make_artboard_size_spin(
      "artboardWidthSpin", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Width of the selected artboard, or of new artboards"));
  add_option_widget(artboard_width_spin_, {CanvasTool::Artboard});
  auto* artboard_swap = make_swap_dimensions_button("artboardSwapSizeButton");
  add_option_widget(artboard_swap, {CanvasTool::Artboard});
  label_after_swap_button(add_option_label(QT_TR_NOOP("Height:"), {CanvasTool::Artboard}));
  artboard_height_spin_ = make_artboard_size_spin(
      "artboardHeightSpin", QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Height of the selected artboard, or of new artboards"));
  add_option_widget(artboard_height_spin_, {CanvasTool::Artboard});
  add_option_separator({CanvasTool::Artboard});
  add_option_label(QT_TR_NOOP("Background:"), {CanvasTool::Artboard});
  artboard_background_combo_ = new QComboBox(toolbar);
  artboard_background_combo_->setObjectName(QStringLiteral("artboardBackgroundCombo"));
  populate_artboard_background_combo(artboard_background_combo_);
  QPointer<QComboBox> artboard_backgrounds(artboard_background_combo_);
  register_retranslation([artboard_backgrounds] {
    if (artboard_backgrounds != nullptr) {
      const QSignalBlocker blocker(artboard_backgrounds);
      const auto index = artboard_backgrounds->currentIndex();
      populate_artboard_background_combo(artboard_backgrounds);
      artboard_backgrounds->setCurrentIndex(index);
    }
  });
  bind_tooltip(artboard_background_combo_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Background of the selected artboard, or of new artboards"));
  add_option_widget(artboard_background_combo_, {CanvasTool::Artboard});
  add_option_separator({CanvasTool::Artboard});
  auto* artboard_add = new QPushButton(tr("Add Artboard"), toolbar);
  artboard_add->setObjectName(QStringLiteral("artboardAddButton"));
  bind_widget_text(artboard_add, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Add Artboard"));
  bind_tooltip(artboard_add, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Add an artboard of this size beside the others"));
  add_option_widget(artboard_add, {CanvasTool::Artboard});
  // Set apart from the Add Artboard button and the Move bar's Distribute buttons (GitHub issue 211).
  add_option_separator({CanvasTool::Artboard, CanvasTool::Move});
  // The Move bar's checkboxes form one group with Auto-Nest Layers (GitHub issue 350,
  // docs/options-bar.md rule 3): Transform Controls, Snap, Auto-Nest Layers.
  move_show_transform_controls_check_ = new CheckGlyphBox(tr("Transform Controls"), toolbar);
  move_show_transform_controls_check_->setObjectName(QStringLiteral("moveShowTransformControlsCheck"));
  bind_tooltip(move_show_transform_controls_check_, QT_TR_NOOP("Show transform controls when selecting a layer with Move"));
  move_show_transform_controls_check_->setChecked(canvas_defaults->show_transform_controls());
  add_option_widget(move_show_transform_controls_check_, {CanvasTool::Move});
  connect(move_show_transform_controls_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_show_transform_controls(checked);
    }
  });
  // Snap mirrors View > Snap: one persisted setting (view/snapEnabled), two
  // surfaces. The action owns the state; the checkbox follows its toggled signal.
  move_snap_check_ = new CheckGlyphBox(tr("Snap"), toolbar);
  move_snap_check_->setObjectName(QStringLiteral("moveSnapCheck"));
  bind_tooltip(move_snap_check_,
               QT_TR_NOOP("Snap moved layers to other layers, guides, the grid, and the canvas (View > Snap). "
                          "Choose the targets under View > Snap To."));
  move_snap_check_->setChecked(view_snap_action_ != nullptr ? view_snap_action_->isChecked() : view_snap_enabled_);
  add_option_widget(move_snap_check_, {CanvasTool::Move});
  connect(move_snap_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (view_snap_action_ != nullptr) {
      view_snap_action_->setChecked(checked);
    }
  });
  if (view_snap_action_ != nullptr) {
    connect(view_snap_action_, &QAction::toggled, this, [this](bool checked) {
      if (move_snap_check_ != nullptr) {
        QSignalBlocker blocker(move_snap_check_);
        move_snap_check_->setChecked(checked);
      }
    });
  }
  artboard_auto_nest_ = app_settings().value(QStringLiteral("tools/artboardAutoNest"), true).toBool();
  auto* artboard_auto_nest = new CheckGlyphBox(tr("Auto-Nest Layers"), toolbar);
  artboard_auto_nest->setObjectName(QStringLiteral("artboardAutoNestCheck"));
  bind_widget_text(artboard_auto_nest, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Auto-Nest Layers"));
  artboard_auto_nest->setChecked(artboard_auto_nest_);
  bind_tooltip(artboard_auto_nest,
               QT_TR_NOOP("A layer moved onto an artboard joins it, and one moved off every artboard leaves it"));
  add_option_widget(artboard_auto_nest, {CanvasTool::Artboard, CanvasTool::Move});
  connect(artboard_auto_nest, &QCheckBox::toggled, this, [this](bool checked) {
    artboard_auto_nest_ = checked;
    app_settings().setValue(QStringLiteral("tools/artboardAutoNest"), checked);
    for (const auto& target_session : sessions_) {
      if (target_session != nullptr && target_session->canvas != nullptr) {
        target_session->canvas->set_artboard_auto_nest(checked);
      }
    }
  });
  connect(artboard_preset_picker_, &ArtboardPresetPicker::preset_chosen, this,
          [this](QSize size) { apply_artboard_size_option(size); });
  // A scrub previews on the canvas and commits at its editingFinished; typed values and
  // arrow steps apply at once, one undo step per run (artboard_field_edit_run_).
  for (auto* spin : {artboard_width_spin_, artboard_height_spin_}) {
    connect(spin, &QSpinBox::valueChanged, this, [this, spin](int) {
      if (!spin_box_scrub_active(spin) || !preview_artboard_field_rect(false)) {
        apply_artboard_size_option(QSize(artboard_width_spin_->value(), artboard_height_spin_->value()), true);
      }
    });
    connect(spin, &QAbstractSpinBox::editingFinished, this, [this] {
      commit_artboard_field_preview();
      artboard_field_edit_run_ = ArtboardNudgeRun{};
    });
  }
  connect(artboard_swap, &QToolButton::clicked, this, [this] {
    apply_artboard_size_option(QSize(artboard_height_spin_->value(), artboard_width_spin_->value()));
  });
  connect(artboard_background_combo_, &QComboBox::activated, this, [this](int index) {
    apply_artboard_background_option(artboard_background_combo_->itemData(index).toString(), false);
  });
  connect(artboard_add, &QPushButton::clicked, this, [this] { add_artboard_beside(); });
  const auto apply_crop_ratio = [this] {
    if (crop_ratio_w_spin_ == nullptr || crop_ratio_h_spin_ == nullptr) {
      return;
    }
    current_crop_ratio_w_ = crop_ratio_w_spin_->value();
    current_crop_ratio_h_ = crop_ratio_h_spin_->value();
    if (canvas_ != nullptr) {
      canvas_->set_crop_ratio(effective_crop_ratio_width(), effective_crop_ratio_height());
    }
    sync_crop_ratio_preset_combo();
    schedule_save_tool_settings();
  };
  connect(crop_style_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    apply_crop_style(index);
  });
  connect(crop_width_spin_, &QSpinBox::valueChanged, this,
          [this](int value) { handle_crop_size_value_changed(true, value); });
  // The link also binds handle drags on the canvas (GitHub issue 332).
  connect(crop_link_size_button_, &QPushButton::toggled, this, [this](bool) {
    if (canvas_ != nullptr) {
      canvas_->set_crop_keep_proportions(crop_size_link_active());
    }
  });
  connect(crop_height_spin_, &QSpinBox::valueChanged, this,
          [this](int value) { handle_crop_size_value_changed(false, value); });
  connect(crop_ratio_w_spin_, &QDoubleSpinBox::valueChanged, this,
          [apply_crop_ratio](double) { apply_crop_ratio(); });
  connect(crop_ratio_h_spin_, &QDoubleSpinBox::valueChanged, this,
          [apply_crop_ratio](double) { apply_crop_ratio(); });
  connect(crop_ratio_preset_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (crop_ratio_preset_combo_ == nullptr || crop_ratio_w_spin_ == nullptr ||
        crop_ratio_h_spin_ == nullptr || index < 0) {
      return;
    }
    if (index == crop_ratio_preset_combo_->count() - 1) {
      return;  // Custom: whatever the fields hold stays.
    }
    auto ratio = QSizeF(0.0, 0.0);
    if (index == 1) {
      if (!has_active_document() || document().width() <= 0 || document().height() <= 0) {
        return;
      }
      const auto divisor = std::gcd(document().width(), document().height());
      ratio = QSizeF(static_cast<double>(document().width() / divisor),
                     static_cast<double>(document().height() / divisor));
    } else if (index > 1) {
      ratio = crop_ratio_preset_combo_->itemData(index).toSizeF();
    }
    crop_ratio_w_spin_->setValue(ratio.width());
    crop_ratio_h_spin_->setValue(ratio.height());
  });
  connect(crop_ratio_clear_button_, &QPushButton::clicked, this, [this] {
    if (crop_ratio_w_spin_ != nullptr) {
      crop_ratio_w_spin_->setValue(0.0);
    }
    if (crop_ratio_h_spin_ != nullptr) {
      crop_ratio_h_spin_->setValue(0.0);
    }
  });
  connect(crop_apply_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->commit_crop_session();
    }
  });
  connect(crop_cancel_button_, &QPushButton::clicked, this, [this] {
    if (canvas_ != nullptr) {
      canvas_->reset_crop_session_to_canvas();
    }
  });

  add_option_label(QT_TR_NOOP("Preset:"),
                   {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                    CanvasTool::Eraser});
  brush_preset_combo_ = new QComboBox(toolbar);
  brush_preset_combo_->setObjectName(QStringLiteral("brushPresetCombo"));
  // 112 (was 132): reclaims room for the labeled Smoothing controls on the
  // one-line Brush row (ui_brush_tip_picker_keeps_options_bar_height).
  brush_preset_combo_->setMinimumWidth(112);
  brush_preset_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  brush_preset_combo_->setMinimumContentsLength(8);
  for (const auto& preset : builtin_brush_presets()) {
    brush_preset_combo_->addItem(brush_preset_display_name(preset), preset.id);
  }
  {
    const auto preset_index = brush_preset_combo_->findData(default_startup_brush_preset_id());
    if (preset_index >= 0) {
      brush_preset_combo_->setCurrentIndex(preset_index);
    }
  }
  brush_preset_combo_->setProperty("lastBrushPresetId", brush_preset_combo_->currentData());
  add_option_widget(
      brush_preset_combo_,
      {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
       CanvasTool::Eraser});
  // Separators follow the grouping rules in docs/options-bar.md; a group a tool or mode hides
  // drops its separator by itself (stray_option_separators).
  add_option_separator({CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::Clone, CanvasTool::Healing,
                        CanvasTool::Smudge, CanvasTool::Eraser});

  // Mode sits FIRST for the shape tools so it never moves when the Pixels-mode
  // brush controls below appear (Seth, September 2026).
  // Shape | Path | Pixels for the vector-capable draw tools (Shape is the
  // Photoshop-parity default; Pixels is the legacy raster behavior). The
  // vector appearance/combine widgets below register for the same tools and
  // refresh_vector_tool_options_visibility() refines them per mode.
  add_option_label(QT_TR_NOOP("Mode:"), {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                     CanvasTool::Polygon, CanvasTool::CustomShape});
  vector_mode_combo_ = new QComboBox(toolbar);
  vector_mode_combo_->setObjectName(QStringLiteral("vectorModeCombo"));
  vector_mode_combo_->addItems({tr("Shape"), tr("Path"), tr("Pixels")});
  vector_mode_combo_->setCurrentIndex(0);
  vector_mode_combo_->setFixedWidth(76);
  bind_tooltip(vector_mode_combo_, QT_TR_NOOP("What the shape tools create: a shape layer, work-path subpaths, or raster pixels"));
  QPointer<QComboBox> vector_mode_combo_pointer(vector_mode_combo_);
  register_retranslation([vector_mode_combo_pointer] {
    if (vector_mode_combo_pointer == nullptr || vector_mode_combo_pointer->count() < 3) {
      return;
    }
    QSignalBlocker blocker(vector_mode_combo_pointer);
    // MainWindow::tr (not QObject::tr): "Pixels"/"Subtract" exist in the
    // QObject context with unrelated meanings (color mode, blend mode).
    vector_mode_combo_pointer->setItemText(0, MainWindow::tr("Shape"));
    vector_mode_combo_pointer->setItemText(1, MainWindow::tr("Path"));
    vector_mode_combo_pointer->setItemText(2, MainWindow::tr("Pixels"));
  });
  add_option_widget(vector_mode_combo_, {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                     CanvasTool::Polygon, CanvasTool::CustomShape});
  connect(vector_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    current_vector_tool_mode_ = index == 1   ? VectorToolMode::Path
                                : index == 2 ? VectorToolMode::Pixels
                                             : VectorToolMode::Shape;
    if (canvas_ != nullptr) {
      canvas_->set_vector_tool_mode(current_vector_tool_mode_);
      schedule_save_tool_settings();
    }
    refresh_options_bar();
  });

  add_option_separator({CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                        CanvasTool::Polygon, CanvasTool::CustomShape});

  // The raster brush controls double as the shape tools' Pixels-mode options;
  // refresh_vector_tool_options_visibility hides them in the vector modes.
  vector_pixel_only_option_widgets_.push_back(add_option_label(
      QT_TR_NOOP("Size:"),
      {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::SpotHealing, CanvasTool::Smudge,
       CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
       CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
       CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* brush_size = new QSpinBox(toolbar);
  brush_size->setObjectName(QStringLiteral("brushSizeSpin"));
  brush_size->setRange(1, kMaxBrushSize);
  brush_size->setValue(canvas_defaults->brush_size());
  brush_size->setProperty(kToolbarSpinboxSliderCurvedProperty, true);
  configure_toolbar_spinbox(brush_size, 58);
  add_option_widget(brush_size,
                    {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::SpotHealing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  auto* brush_size_slider = new QSlider(Qt::Horizontal, toolbar);
  brush_size_slider->setObjectName(QStringLiteral("brushSizeSlider"));
  brush_size_slider->setProperty(kOptionsBarCollapsibleProperty, true);
  bind_curved_slider(*brush_size_slider, *brush_size);
  // 84 (was 124, 150 before that): the Brush row must keep one Options-bar line,
  // slider included, at 1366 px with the fixed-width Tip picker and the separator
  // after Opacity (GitHub issue 364; ui_brush_tip_picker_keeps_a_fixed_width).
  brush_size_slider->setFixedWidth(84);
  register_retranslation([brush_size_slider] {
    brush_size_slider->setToolTip(resolve_modifier_names(MainWindow::tr("Brush size: press [ or ], or %ALT%+Right-drag on the canvas")));
  });
  add_option_widget(brush_size_slider,
                    {CanvasTool::Brush, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::SpotHealing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  add_option_separator({CanvasTool::Brush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing,
                        CanvasTool::Smudge, CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle,
                        CanvasTool::Ellipse});
  // Brush Mode (core/paint_mode.hpp), Brush only: the Eraser's Mode would mean its tip
  // shape (Round/Square already cover that) and the retouch tools paint by their own math.
  add_option_label(QT_TR_NOOP("Mode:"), {CanvasTool::Brush});
  brush_mode_combo_ = new QComboBox(toolbar);
  brush_mode_combo_->setObjectName(QStringLiteral("brushModeCombo"));
  add_paint_mode_items(brush_mode_combo_);
  brush_mode_combo_->setCurrentIndex(
      std::max(0, brush_mode_combo_->findData(paint_mode_item_data(current_brush_paint_mode_))));
  // Compact closed width (the list still shows full names), like the mixer combinations.
  brush_mode_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  brush_mode_combo_->setMinimumContentsLength(7);
  widen_combo_popup_to_items(brush_mode_combo_);
  bind_tooltip(brush_mode_combo_, QT_TR_NOOP("Brush mode: how the paint combines with the pixels under it"));
  add_option_widget(brush_mode_combo_, {CanvasTool::Brush});
  connect(brush_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || brush_mode_combo_ == nullptr) {
      return;
    }
    current_brush_paint_mode_ = paint_mode_from_item_data(brush_mode_combo_->itemData(index).toInt());
    if (canvas_ != nullptr) {
      canvas_->set_brush_paint_mode(current_brush_paint_mode_);
      save_tool_settings();
      refresh_document_info();
    }
  });
  QPointer<QComboBox> brush_mode_combo(brush_mode_combo_);
  register_retranslation([brush_mode_combo] {
    if (brush_mode_combo != nullptr) {
      const QSignalBlocker blocker(brush_mode_combo);
      retranslate_paint_mode_items(brush_mode_combo);
    }
  });
  // The Healing Brush's Mode (GitHub issue 381). Item data is the HealingMode value.
  add_option_label(QT_TR_NOOP("Mode:"), {CanvasTool::Healing});
  healing_mode_combo_ = new QComboBox(toolbar);
  healing_mode_combo_->setObjectName(QStringLiteral("healingModeCombo"));
  const auto healing_mode_text = [](HealingMode mode) {
    return mode == HealingMode::Replace ? QCoreApplication::translate(kMainWindowTranslationContext, "Replace")
                                        : blend_mode_name(healing_mode_blend(mode));
  };
  static_cast<void>(QT_TR_NOOP("Replace"));
  for (const auto mode : {HealingMode::Normal, HealingMode::Replace, HealingMode::Multiply, HealingMode::Screen,
                          HealingMode::Darken, HealingMode::Lighten, HealingMode::Color, HealingMode::Luminosity}) {
    healing_mode_combo_->addItem(healing_mode_text(mode), static_cast<int>(mode));
  }
  healing_mode_combo_->setCurrentIndex(
      std::max(0, healing_mode_combo_->findData(static_cast<int>(canvas_defaults->healing_mode()))));
  bind_tooltip(healing_mode_combo_,
               QT_TR_NOOP("Healing mode: Replace keeps the source texture as sampled; the blend modes combine the "
                          "healed color with the pixels under it"));
  add_option_widget(healing_mode_combo_, {CanvasTool::Healing});
  connect(healing_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || healing_mode_combo_ == nullptr || canvas_ == nullptr) {
      return;
    }
    canvas_->set_healing_mode(static_cast<HealingMode>(healing_mode_combo_->itemData(index).toInt()));
    save_tool_settings();
  });
  {
    QPointer<QComboBox> healing_combo(healing_mode_combo_);
    register_retranslation([healing_combo, healing_mode_text] {
      if (healing_combo == nullptr) {
        return;
      }
      const QSignalBlocker blocker(healing_combo);
      for (int index = 0; index < healing_combo->count(); ++index) {
        healing_combo->setItemText(index,
                                   healing_mode_text(static_cast<HealingMode>(healing_combo->itemData(index).toInt())));
      }
    });
  }
  vector_pixel_only_option_widgets_.push_back(add_option_label(
      QT_TR_NOOP("Opacity:"),
      {CanvasTool::Brush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
       CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* brush_opacity = new QSpinBox(toolbar);
  brush_opacity->setObjectName(QStringLiteral("brushOpacitySpin"));
  // 0..100 like Soft and Smooth (issue 253): a 0% stroke paints nothing. Photoshop stops
  // both Opacity and Flow at 1%; Patchy keeps one range for every options-bar percentage.
  brush_opacity->setRange(0, 100);
  brush_opacity->setValue(canvas_defaults->brush_opacity());
  brush_opacity->setSuffix(percent_suffix());
  configure_toolbar_spinbox(brush_opacity, 52);
  add_option_widget(brush_opacity,
                    {CanvasTool::Brush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::Smudge,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  // Opacity and Soft have no inline slider (Flow and Smooth never did): the field's
  // popup slider, a drag on its label or inside the field, and the number keys set them.
  bind_tooltip(brush_opacity, QT_TR_NOOP("Brush opacity: press number keys (5 = 50%, 0 = 100%)"));
  // Mode and Opacity | the tip's own settings (GitHub issue 364). Pixels mode only on the
  // shape tools, like the controls on either side.
  {
    auto* separator = make_option_separator();
    register_option_action(separator, {CanvasTool::Brush, CanvasTool::PatternStamp, CanvasTool::Clone,
                                       CanvasTool::Healing, CanvasTool::Smudge, CanvasTool::Eraser,
                                       CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
    vector_pixel_only_option_widgets_.push_back(separator);
  }
  vector_pixel_only_option_widgets_.push_back(add_option_label(
      QT_TR_NOOP("Soft:"),
      {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::SpotHealing, CanvasTool::Smudge,
       CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
       CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
       CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* brush_softness = new QSpinBox(toolbar);
  brush_softness->setObjectName(QStringLiteral("brushSoftnessSpin"));
  brush_softness->setRange(0, 100);
  brush_softness->setValue(canvas_defaults->brush_softness());
  brush_softness->setSuffix(percent_suffix());
  configure_toolbar_spinbox(brush_softness, 52);
  add_option_widget(brush_softness,
                    {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Clone, CanvasTool::Healing, CanvasTool::SpotHealing, CanvasTool::Smudge,
                     CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush,
                     CanvasTool::Eraser, CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse});
  register_retranslation([brush_softness] {
    brush_softness->setToolTip(resolve_modifier_names(MainWindow::tr("Brush edge softness: %ALT%+Right-drag up or down on the canvas")));
  });
  for (auto* raster_only :
       std::initializer_list<QWidget*>{brush_size, brush_size_slider, brush_opacity, brush_softness}) {
    vector_pixel_only_option_widgets_.push_back(raster_only);
  }
  connect(brush_size, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_size(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_opacity, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_opacity(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  auto* brush_flow_label = add_option_label(QT_TR_NOOP("Flow:"), {CanvasTool::Brush, CanvasTool::PatternStamp});
  bind_widget_text(brush_flow_label, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Flow:"));
  auto* brush_flow = new QSpinBox(toolbar);
  brush_flow->setObjectName(QStringLiteral("brushFlowSpin"));
  brush_flow->setRange(0, 100);  // 0..100 like Opacity (issue 253)
  brush_flow->setValue(canvas_defaults->brush_flow());
  brush_flow->setSuffix(percent_suffix());
  bind_tooltip(brush_flow, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Brush flow: Shift+number keys (number keys with Airbrush)"));
  configure_toolbar_spinbox(brush_flow, 60);
  add_option_widget(brush_flow, {CanvasTool::Brush, CanvasTool::PatternStamp});
  auto* brush_airbrush = new CheckGlyphBox(tr("Airbrush"), toolbar);
  brush_airbrush->setObjectName(QStringLiteral("brushAirbrushCheck"));
  bind_widget_text(brush_airbrush, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Airbrush"));
  brush_airbrush->setChecked(canvas_defaults->brush_build_up());
  bind_tooltip(brush_airbrush, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Build paint while the pointer is held still"));
  add_option_widget(brush_airbrush, {CanvasTool::Brush});
  connect(brush_flow, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_flow(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_airbrush, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_build_up(checked);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  // Stroke Smoothing (Brush, Mixer Brush, Eraser): the percent spin plus a
  // gear button whose menu holds the four Photoshop smoothing toggles.
  const std::initializer_list<CanvasTool> smoothing_tools = {
      CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::Eraser};
  // Created here, APPENDED to the bar after the mixer cluster below so every
  // row reads Photoshop-style (..., Flow, Sample All Layers, Smooth).
  auto* brush_smoothing = new QSpinBox(toolbar);
  brush_smoothing->setObjectName(QStringLiteral("brushSmoothingSpin"));
  brush_smoothing->setRange(0, 100);
  brush_smoothing->setValue(current_brush_smoothing_);
  brush_smoothing->setSuffix(percent_suffix());
  bind_tooltip(brush_smoothing, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Stroke smoothing - 0% paints the raw pointer path"));
  configure_toolbar_spinbox(brush_smoothing, 48);
  connect(brush_smoothing, &QSpinBox::valueChanged, this, [this](int value) {
    current_brush_smoothing_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_brush_smoothing(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  brush_smoothing_options_button_ = new QToolButton(toolbar);
  brush_smoothing_options_button_->setObjectName(QStringLiteral("brushSmoothingOptionsButton"));
  make_options_menu_button(brush_smoothing_options_button_);
  bind_tooltip(brush_smoothing_options_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Smoothing options"));
  auto* smoothing_menu = new QMenu(brush_smoothing_options_button_);
  const auto add_smoothing_option = [this, smoothing_menu](const char* source, bool checked,
                                                           auto setter) {
    auto* action = smoothing_menu->addAction(tr(source));
    bind_action_text(action, source);
    action->setCheckable(true);
    action->setChecked(checked);
    connect(action, &QAction::toggled, this, [this, setter](bool on) {
      setter(*this, on);
      schedule_save_tool_settings();
      refresh_document_info();
    });
    return action;
  };
  brush_smoothing_pulled_string_action_ = add_smoothing_option(
      QT_TR_NOOP("Pulled String Mode"), current_brush_smoothing_pulled_string_,
      [](MainWindow& window, bool on) {
        window.current_brush_smoothing_pulled_string_ = on;
        if (window.canvas_ != nullptr) {
          window.canvas_->set_brush_smoothing_pulled_string(on);
        }
      });
  brush_smoothing_catch_up_action_ = add_smoothing_option(
      QT_TR_NOOP("Stroke Catch-up"), current_brush_smoothing_catch_up_,
      [](MainWindow& window, bool on) {
        window.current_brush_smoothing_catch_up_ = on;
        if (window.canvas_ != nullptr) {
          window.canvas_->set_brush_smoothing_catch_up(on);
        }
      });
  brush_smoothing_catch_up_end_action_ = add_smoothing_option(
      QT_TR_NOOP("Catch-up on Stroke End"), current_brush_smoothing_catch_up_end_,
      [](MainWindow& window, bool on) {
        window.current_brush_smoothing_catch_up_end_ = on;
        if (window.canvas_ != nullptr) {
          window.canvas_->set_brush_smoothing_catch_up_end(on);
        }
      });
  brush_smoothing_zoom_adjust_action_ = add_smoothing_option(
      QT_TR_NOOP("Adjust for Zoom"), current_brush_smoothing_zoom_adjust_,
      [](MainWindow& window, bool on) {
        window.current_brush_smoothing_zoom_adjust_ = on;
        if (window.canvas_ != nullptr) {
          window.canvas_->set_brush_smoothing_zoom_adjust(on);
        }
      });
  brush_smoothing_options_button_->setMenu(smoothing_menu);

  // Photoshop's "Useful Mixer Brush Combinations" dropdown: each entry is one
  // Wet/Load/Mix triple from the verified kCombinations table above. Selection
  // is derived from the current spin values and never persisted separately.
  mixer_combination_combo_ = new QComboBox(toolbar);
  mixer_combination_combo_->setObjectName(QStringLiteral("mixerCombinationCombo"));
  mixer_combination_combo_->addItem(tr("Custom"));
  for (const auto& combination : mixer_useful_combinations()) {
    mixer_combination_combo_->addItem(tr(combination.name));
  }
  // Compact closed width (the popup still shows full names): the Options bar
  // must hold the whole mixer row in one line or the bar height changes on
  // tool switch (ui_brush_tip_picker_keeps_options_bar_height).
  mixer_combination_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  mixer_combination_combo_->setMinimumContentsLength(7);
  widen_combo_popup_to_items(mixer_combination_combo_);
  bind_tooltip(mixer_combination_combo_, QT_TR_NOOP("Useful mixer brush combinations"));
  add_option_separator({CanvasTool::MixerBrush});
  add_option_widget(mixer_combination_combo_, {CanvasTool::MixerBrush});

  const auto add_mixer_percentage = [this, toolbar, add_option_label, add_option_widget](
                                        const char* label_source, const char* object_name,
                                        int minimum, int value, auto setter) {
    auto* label = add_option_label(label_source, {CanvasTool::MixerBrush});
    bind_widget_text(label, label_source);
    auto* spin = new QSpinBox(toolbar);
    spin->setObjectName(QString::fromLatin1(object_name));
    spin->setRange(minimum, 100);
    spin->setValue(value);
    spin->setSuffix(percent_suffix());
    configure_toolbar_spinbox(spin, 60);
    add_option_widget(spin, {CanvasTool::MixerBrush});
    connect(spin, &QSpinBox::valueChanged, this, [this, setter](int new_value) {
      setter(*this, new_value);
      schedule_save_tool_settings();
      refresh_document_info();
    });
    return spin;
  };
  auto* mixer_wet_spin =
      add_mixer_percentage(QT_TR_NOOP("Wet:"), "mixerWetSpin", 0, current_mixer_wet_,
                           [](MainWindow& window, int value) {
                             window.current_mixer_wet_ = value;
                             if (window.canvas_ != nullptr) {
                               window.canvas_->set_mixer_wet(value);
                             }
                           });
  auto* mixer_load_spin =
      add_mixer_percentage(QT_TR_NOOP("Load:"), "mixerLoadSpin", 1, current_mixer_load_,
                           [](MainWindow& window, int value) {
                             window.current_mixer_load_ = value;
                             if (window.canvas_ != nullptr) {
                               window.canvas_->set_mixer_load(value);
                             }
                           });
  auto* mixer_mix_spin =
      add_mixer_percentage(QT_TR_NOOP("Mix:"), "mixerMixSpin", 0, current_mixer_mix_,
                           [](MainWindow& window, int value) {
                             window.current_mixer_mix_ = value;
                             if (window.canvas_ != nullptr) {
                               window.canvas_->set_mixer_mix(value);
                             }
                           });
  // Wet 0 is a dry brush with no pickup, so Mix has nothing to blend;
  // Photoshop greys the control out the same way.
  mixer_mix_spin->setEnabled(current_mixer_wet_ > 0);
  connect(mixer_wet_spin, &QSpinBox::valueChanged, mixer_mix_spin,
          [mixer_mix_spin](int wet) { mixer_mix_spin->setEnabled(wet > 0); });

  const auto apply_mixer_combination = [mixer_wet_spin, mixer_load_spin, mixer_mix_spin](
                                           int combo_index) {
    if (combo_index <= 0) {
      return;  // Custom
    }
    const auto& combinations = mixer_useful_combinations();
    const auto table_index = static_cast<std::size_t>(combo_index - 1);
    if (table_index >= combinations.size()) {
      return;
    }
    const auto& combination = combinations[table_index];
    mixer_wet_spin->setValue(combination.wet);
    mixer_load_spin->setValue(combination.load);
    if (combination.mix >= 0) {
      mixer_mix_spin->setValue(combination.mix);
    }
  };
  // currentIndexChanged (not activated) so programmatic selection also applies;
  // sync_mixer_combination_combo() sets the index under a QSignalBlocker, so
  // derive-and-set never loops back through this handler.
  connect(mixer_combination_combo_, &QComboBox::currentIndexChanged, this, apply_mixer_combination);
  const auto resync_combination = [this] { sync_mixer_combination_combo(); };
  connect(mixer_wet_spin, &QSpinBox::valueChanged, this, resync_combination);
  connect(mixer_load_spin, &QSpinBox::valueChanged, this, resync_combination);
  connect(mixer_mix_spin, &QSpinBox::valueChanged, this, resync_combination);
  sync_mixer_combination_combo();

  add_mixer_percentage(QT_TR_NOOP("Flow:"), "mixerFlowSpin", 1, current_mixer_flow_,
                       [](MainWindow& window, int value) {
                         window.current_mixer_flow_ = value;
                         if (window.canvas_ != nullptr) {
                           window.canvas_->set_mixer_flow(value);
                         }
                       });

  mixer_sample_all_layers_check_ = new CheckGlyphBox(tr("Sample All Layers"), toolbar);
  mixer_sample_all_layers_check_->setObjectName(QStringLiteral("mixerSampleAllLayersCheck"));
  mixer_sample_all_layers_check_->setChecked(canvas_defaults->mixer_sample_all_layers());
  bind_tooltip(mixer_sample_all_layers_check_, QT_TR_NOOP("Sample the merged document instead of the active layer"));
  // No separator before the checkbox: the Mixer row has no room for it at 1366 px next to
  // the fixed-width Tip picker (docs/options-bar.md rule 9; GitHub issue 364).
  add_option_widget(mixer_sample_all_layers_check_, {CanvasTool::MixerBrush});
  connect(mixer_sample_all_layers_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_mixer_sample_all_layers(checked);
      save_tool_settings();
    }
  });

  // Smoothing lands after the mixer cluster so every row that shows it reads
  // Photoshop-style: ..., Flow, (Sample All Layers,) Smooth, gear.
  add_option_separator(smoothing_tools);
  auto* brush_smoothing_label = add_option_label(QT_TR_NOOP("Smooth:"), smoothing_tools);
  bind_widget_text(brush_smoothing_label, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Smooth:"));
  add_option_widget(brush_smoothing, smoothing_tools);
  add_option_widget(brush_smoothing_options_button_, smoothing_tools);

  connect(brush_softness, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_brush_softness(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(brush_preset_combo_, &QComboBox::currentIndexChanged, this,
          [this, brush_size, brush_opacity, brush_flow, brush_softness,
           brush_airbrush](int index) {
    if (brush_preset_combo_ == nullptr || canvas_ == nullptr || index < 0) {
      return;
    }
    const auto preset_id = brush_preset_combo_->itemData(index).toString();
    if (preset_id.startsWith("__")) {
      const QSignalBlocker block(brush_preset_combo_);
      brush_preset_combo_->setCurrentIndex(brush_preset_combo_->findData(brush_preset_combo_->property("lastBrushPresetId")));
    } else {
      brush_preset_combo_->setProperty("lastBrushPresetId", preset_id);
    }
    const auto* preset = find_brush_preset(preset_id);
    if (preset == nullptr) {
      if (preset_id == "__saveBrush") save_current_automation_brush();
      else if (preset_id == "__manageBrushes") manage_automation_brush_presets();
      else if (!preset_id.isEmpty()) {
        try {
          auto& library = brush_automation_library(); library.refresh();
          auto s = library.resolve(QJsonObject{{"presetId", preset_id}});
          if (!library.preset(preset_id)["includeColors"].toBool()) {
            s.color = canvas_->primary_color(); s.background = canvas_->secondary_color();
          }
          activate_automation_brush(s);
        } catch (const std::exception& e) { show_status_error(tr("Brush preset operation failed: %1").arg(translate_data_text(e.what()))); }
      }
      return;
    }
    if (active_automation_brush_) set_active_brush_tip(builtin_round_brush_tip_id(), false, false);
    // A built-in preset names its procedural tip (Square, or Round for the rest), so switching
    // presets never leaves a stale bitmap tip behind.
    const auto preset_tip = preset->tip_id.isEmpty() ? builtin_round_brush_tip_id() : preset->tip_id;
    if (active_brush_tip_id_ != preset_tip) {
      set_active_brush_tip(preset_tip, false, false);
    }
    if (preset_id == QStringLiteral("airbrush")) {
      // The quick Airbrush preset is a predictable soft Round brush. Existing sampled tips
      // already cover Smoke/Spray/Spatter/Stipple, so do not invent a duplicate airbrush tip or
      // carry a surprising Round dynamics session into this basic preset.
      round_brush_dynamics_ = {};
      round_brush_base_angle_degrees_ = 0.0;
      round_brush_base_roundness_ = 100.0;
      set_active_brush_tip(builtin_round_brush_tip_id(), false, false);
    }
    apply_brush_preset(*canvas_, *preset);
    brush_size->setValue(preset->size);
    brush_opacity->setValue(preset->opacity);
    brush_flow->setValue(preset->flow);
    brush_softness->setValue(preset->softness);
    brush_airbrush->setChecked(preset->build_up);
    save_tool_settings();
    refresh_document_info();
    statusBar()->showMessage(tr("Brush preset: %1").arg(brush_preset_display_name(*preset)));
  });

  add_option_separator({CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp, CanvasTool::Eraser});
  add_option_label(QT_TR_NOOP("Tip:"),
                   {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp,
                   CanvasTool::Eraser});
  (void)brush_automation_library();
  refresh_automation_brush_presets();
  register_retranslation([this] { refresh_automation_brush_presets(); });
  brush_tip_picker_ = new BrushTipPicker(brush_tip_library(), toolbar);
  // The options bar is built after load_tool_settings() reset the active tip to Round.
  brush_tip_picker_->set_current_tip_id(active_brush_tip_id_);
  add_option_widget(brush_tip_picker_,
                    {CanvasTool::Brush, CanvasTool::MixerBrush, CanvasTool::PatternStamp,
                     CanvasTool::Eraser});
  connect(brush_tip_picker_, &BrushTipPicker::tip_selected, this,
          [this](const QString& id) { set_active_brush_tip(id, true); });
  connect(brush_tip_picker_, &BrushTipPicker::import_requested, this,
          [this] { import_brush_tips_from_abr(); });
  connect(brush_tip_picker_, &BrushTipPicker::define_requested, this,
          [this] { define_brush_tip_from_selection(); });
  connect(brush_tip_picker_, &BrushTipPicker::manage_requested, this, [this] { open_brush_tip_manager(); });
  connect(&brush_tip_library(), &BrushTipLibrary::changed, this, [this] {
    // A removed tip must not stay active; re-resolving also refreshes renamed/respaced tips.
    // Re-applying after a library edit must not reset Flow/Airbrush to imported tool settings.
    if (!active_automation_brush_) set_active_brush_tip(active_brush_tip_id_, false, false);
  });
  QPointer<BrushTipPicker> tip_picker(brush_tip_picker_);
  register_retranslation([tip_picker] {
    if (tip_picker != nullptr) {
      tip_picker->refresh();
    }
  });

  brush_dynamics_button_ = new BrushDynamicsButton(toolbar);
  add_option_widget(brush_dynamics_button_, {CanvasTool::Brush});
  connect(brush_dynamics_button_, &BrushDynamicsButton::dynamics_edited, this,
          [this](const QString& tip_id, const patchy::BrushDynamics& dynamics, double base_angle,
                 double base_roundness) {
            if (is_builtin_brush_tip_id(tip_id)) {
              // Session-only: the Round and Square brushes' dynamics live in the window, not
              // the library, and deliberately reset on the next launch.
              round_brush_dynamics_ = dynamics;
              round_brush_base_angle_degrees_ = base_angle;
              round_brush_base_roundness_ = base_roundness;
              if (canvas_ != nullptr &&
                  (active_preset_tip_ || active_brush_tip_id_.isEmpty() ||
                   is_builtin_brush_tip_id(active_brush_tip_id_))) {
                canvas_->set_brush_dynamics(dynamics);
                canvas_->set_brush_base_shape(base_angle, static_cast<int>(std::lround(base_roundness)));
              }
              return;
            }
            if (canvas_ != nullptr && tip_id == active_brush_tip_id_) {
              canvas_->set_brush_dynamics(dynamics);
              canvas_->set_brush_base_shape(base_angle, static_cast<int>(std::lround(base_roundness)));
            }
            // Persisting to the sidecar emits changed(), which re-applies the (identical) values.
            brush_tip_library().set_tip_dynamics(tip_id, dynamics, base_angle, base_roundness);
          });
  // The options bar is built after load_tool_settings() already selected the startup tip, so
  // seed the button's model now (Round session values, or the entry if a tip is active).
  if (active_brush_tip_id_.isEmpty() || is_builtin_brush_tip_id(active_brush_tip_id_)) {
    brush_dynamics_button_->set_round_session(
        active_brush_tip_id_.isEmpty() ? builtin_round_brush_tip_id() : active_brush_tip_id_, round_brush_dynamics_,
                                              round_brush_base_angle_degrees_,
                                              round_brush_base_roundness_);
  } else if (const auto* entry = brush_tip_library().find_entry(active_brush_tip_id_);
             entry != nullptr) {
    brush_dynamics_button_->set_active_entry(entry);
  }
  QPointer<BrushDynamicsButton> dynamics_button(brush_dynamics_button_);
  register_retranslation([dynamics_button] {
    if (dynamics_button != nullptr) {
      dynamics_button->retranslate();
    }
  });

  add_option_separator({CanvasTool::PatternStamp});
  auto* pattern_label = add_option_label(QT_TR_NOOP("Pattern:"), {CanvasTool::PatternStamp});
  bind_widget_text(pattern_label, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Pattern:"));
  pattern_stamp_pattern_combo_ = new QComboBox(toolbar);
  pattern_stamp_pattern_combo_->setObjectName(QStringLiteral("patternStampPatternCombo"));
  pattern_stamp_pattern_combo_->setIconSize(QSize(18, 18));
  pattern_stamp_pattern_combo_->setMinimumWidth(150);
  use_scrolling_list_popup(*pattern_stamp_pattern_combo_);
  register_retranslation([this] { refresh_pattern_stamp_pattern_combo(); });
  // The list fills when the startup pattern preload lands; using it before then loads it now.
  new FirstUseHook(pattern_stamp_pattern_combo_, [this] { (void)pattern_library(); });
  add_option_widget(pattern_stamp_pattern_combo_, {CanvasTool::PatternStamp});
  connect(pattern_stamp_pattern_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (pattern_stamp_pattern_combo_ == nullptr || index < 0) {
      return;
    }
    current_pattern_stamp_pattern_id_ = pattern_stamp_pattern_combo_->itemData(index).toString();
    apply_pattern_stamp_settings_to_canvas(canvas_);
    schedule_save_tool_settings();
  });

  auto* manage_patterns = new QPushButton(toolbar);
  manage_patterns->setObjectName(QStringLiteral("patternStampManageButton"));
  manage_patterns->setText(tr("Manage..."));
  bind_widget_text(manage_patterns, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Manage..."));
  bind_tooltip(manage_patterns, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Import or manage patterns"));
  add_option_widget(manage_patterns, {CanvasTool::PatternStamp});
  connect(manage_patterns, &QPushButton::clicked, this, [this] {
    const auto selected_storage_id =
        request_pattern_manager(this, pattern_library(), current_pattern_stamp_pattern_id_);
    if (selected_storage_id.isEmpty()) {
      return;
    }
    if (const auto* entry = pattern_library().find_entry(selected_storage_id); entry != nullptr) {
      current_pattern_stamp_pattern_id_ = entry->id;
      refresh_pattern_stamp_pattern_combo();
      apply_pattern_stamp_settings_to_canvas(canvas_);
      save_tool_settings();
    }
  });

  pattern_stamp_aligned_check_ = new CheckGlyphBox(tr("Aligned"), toolbar);

  bind_widget_text(pattern_stamp_aligned_check_, QT_TR_NOOP("Aligned"));
  pattern_stamp_aligned_check_->setObjectName(QStringLiteral("patternStampAlignedCheck"));
  pattern_stamp_aligned_check_->setChecked(current_pattern_stamp_aligned_);
  bind_tooltip(pattern_stamp_aligned_check_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Keep pattern alignment continuous across strokes"));
  add_option_separator({CanvasTool::PatternStamp});
  add_option_widget(pattern_stamp_aligned_check_, {CanvasTool::PatternStamp});
  connect(pattern_stamp_aligned_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_pattern_stamp_aligned_ = checked;
    apply_pattern_stamp_settings_to_canvas(canvas_);
    save_tool_settings();
  });

  add_option_label(QT_TR_NOOP("Method:"), {CanvasTool::Gradient});
  gradient_method_combo_ = new QComboBox(toolbar);
  gradient_method_combo_->setObjectName(QStringLiteral("gradientMethodCombo"));
  gradient_method_combo_->addItem(tr("Linear"), static_cast<int>(GradientMethod::Linear));
  gradient_method_combo_->addItem(tr("Radial"), static_cast<int>(GradientMethod::Radial));
  gradient_method_combo_->setFixedWidth(86);
  add_option_widget(gradient_method_combo_, {CanvasTool::Gradient});
  add_option_separator({CanvasTool::Gradient});
  QPointer<QComboBox> gradient_method_combo(gradient_method_combo_);
  register_retranslation([gradient_method_combo] {
    if (gradient_method_combo == nullptr || gradient_method_combo->count() < 2) {
      return;
    }
    const QSignalBlocker blocker(gradient_method_combo);
    gradient_method_combo->setItemText(0, QCoreApplication::translate(kMainWindowTranslationContext, "Linear"));
    gradient_method_combo->setItemText(1, QCoreApplication::translate(kMainWindowTranslationContext, "Radial"));
  });

  add_option_label(QT_TR_NOOP("Opacity:"), {CanvasTool::Gradient});
  gradient_opacity_spin_ = new QSpinBox(toolbar);
  gradient_opacity_spin_->setObjectName(QStringLiteral("gradientOpacitySpin"));
  gradient_opacity_spin_->setRange(0, 100);
  gradient_opacity_spin_->setValue(canvas_defaults->gradient_opacity());
  gradient_opacity_spin_->setSuffix(percent_suffix());
  configure_toolbar_spinbox(gradient_opacity_spin_, 52);
  add_option_widget(gradient_opacity_spin_, {CanvasTool::Gradient});
  gradient_opacity_slider_ = new QSlider(Qt::Horizontal, toolbar);
  gradient_opacity_slider_->setObjectName(QStringLiteral("gradientOpacitySlider"));
  gradient_opacity_slider_->setProperty(kOptionsBarCollapsibleProperty, true);
  gradient_opacity_slider_->setRange(0, 100);
  gradient_opacity_slider_->setValue(canvas_defaults->gradient_opacity());
  gradient_opacity_slider_->setFixedWidth(110);
  bind_tooltip(gradient_opacity_slider_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Gradient opacity"));
  add_option_widget(gradient_opacity_slider_, {CanvasTool::Gradient});
  gradient_reverse_check_ = new CheckGlyphBox(tr("Reverse"), toolbar);
  gradient_reverse_check_->setObjectName(QStringLiteral("gradientReverseCheck"));
  gradient_reverse_check_->setChecked(canvas_defaults->gradient_reverse());
  add_option_separator({CanvasTool::Gradient});
  add_option_widget(gradient_reverse_check_, {CanvasTool::Gradient});
  gradient_preview_button_ = new QPushButton(toolbar);
  gradient_preview_button_->setObjectName(QStringLiteral("gradientPreviewButton"));
  bind_tooltip(gradient_preview_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Gradient preview"));
  add_option_separator({CanvasTool::Gradient});
  add_option_widget(gradient_preview_button_, {CanvasTool::Gradient});
  gradient_presets_button_ = new QPushButton(toolbar);
  gradient_presets_button_->setObjectName(QStringLiteral("gradientPresetsButton"));
  gradient_presets_button_->setText(tr("Presets"));
  bind_widget_text(gradient_presets_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Presets"));
  bind_tooltip(gradient_presets_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Choose a gradient preset"));
  add_option_widget(gradient_presets_button_, {CanvasTool::Gradient});
  gradient_edit_stops_button_ = new QPushButton(tr("Edit Stops..."), toolbar);
  gradient_edit_stops_button_->setObjectName(QStringLiteral("gradientEditStopsButton"));
  add_option_widget(gradient_edit_stops_button_, {CanvasTool::Gradient});
  refresh_gradient_controls_from_canvas();
  connect(gradient_method_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (canvas_ == nullptr || gradient_method_combo_ == nullptr || index < 0) {
      return;
    }
    canvas_->set_gradient_method(static_cast<GradientMethod>(gradient_method_combo_->itemData(index).toInt()));
    save_tool_settings();
    refresh_document_info();
  });
  connect(gradient_opacity_spin_, &QSpinBox::valueChanged, gradient_opacity_slider_, &QSlider::setValue);
  connect(gradient_opacity_slider_, &QSlider::valueChanged, gradient_opacity_spin_, &QSpinBox::setValue);
  connect(gradient_opacity_spin_, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_gradient_opacity(value);
      refresh_gradient_controls_from_canvas();
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  connect(gradient_reverse_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_gradient_reverse(checked);
      refresh_gradient_controls_from_canvas();
      save_tool_settings();
      refresh_document_info();
    }
  });
  connect(gradient_preview_button_, &QPushButton::clicked, this, [this] { edit_gradient_stops(); });
  connect(gradient_presets_button_, &QPushButton::clicked, this, [this] { choose_gradient_preset(); });
  connect(gradient_edit_stops_button_, &QPushButton::clicked, this, [this] { edit_gradient_stops(); });

  add_option_separator({CanvasTool::Healing});
  add_option_label(QT_TR_NOOP("Diffusion:"), {CanvasTool::Healing});
  auto* healing_diffusion = new QSpinBox(toolbar);
  healing_diffusion->setObjectName(QStringLiteral("healingDiffusionSpin"));
  healing_diffusion->setRange(1, 7);
  healing_diffusion->setValue(current_healing_diffusion_);
  bind_tooltip(healing_diffusion, QT_TR_NOOP("Lower values preserve fine texture; higher values adapt more quickly"));
  configure_toolbar_spinbox(healing_diffusion, 42);
  add_option_widget(healing_diffusion, {CanvasTool::Healing});
  connect(healing_diffusion, &QSpinBox::valueChanged, this, [this](int value) {
    current_healing_diffusion_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_healing_diffusion(value);
      save_tool_settings();
    }
  });

  add_option_separator({CanvasTool::Clone, CanvasTool::Healing});
  clone_aligned_check_ = new CheckGlyphBox(tr("Aligned"), toolbar);
  clone_aligned_check_->setObjectName(QStringLiteral("cloneAlignedCheck"));
  clone_aligned_check_->setChecked(canvas_defaults->clone_aligned());
  bind_tooltip(clone_aligned_check_, QT_TR_NOOP("Keep sample source offset aligned across strokes"));
  add_option_widget(clone_aligned_check_, {CanvasTool::Clone, CanvasTool::Healing});
  connect(clone_aligned_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_clone_aligned(checked);
      save_tool_settings();
      refresh_clone_source_panel();
    }
  });
  // Show Overlay (GitHub issue 426): the popup's overlay checkbox, also on the bar after
  // Aligned; its opacity stays in the popup.
  clone_overlay_check_ = new CheckGlyphBox(tr("Show Overlay"), toolbar);
  clone_overlay_check_->setObjectName(QStringLiteral("cloneOverlayCheck"));
  clone_overlay_check_->setChecked(current_clone_overlay_shown_);
  bind_tooltip(clone_overlay_check_, QT_TR_NOOP("Preview the source under the brush before you paint"));
  add_option_widget(clone_overlay_check_, {CanvasTool::Clone, CanvasTool::Healing});
  connect(clone_overlay_check_, &QCheckBox::toggled, this,
          [this](bool checked) { apply_clone_overlay_settings(checked, current_clone_overlay_opacity_); });
  // Clone source settings (GitHub issue 424): the slots, offset, scale and rotation and
  // the overlay, in a popup on a gear button (rule 8) after the checkbox group.
  clone_source_options_button_ = new QToolButton(toolbar);
  clone_source_options_button_->setObjectName(QStringLiteral("cloneSourceOptionsButton"));
  make_options_menu_button(clone_source_options_button_);
  bind_tooltip(clone_source_options_button_,
               QT_TR_NOOP("Clone source settings: sources, offset, scale, rotation and overlay"));
  {
    clone_source_menu_ = new QMenu(clone_source_options_button_);
    clone_source_menu_->setObjectName(QStringLiteral("cloneSourceMenu"));
    clone_source_panel_ = new CloneSourcePanel(clone_source_menu_);
    auto* panel_action = new QWidgetAction(clone_source_menu_);
    panel_action->setDefaultWidget(clone_source_panel_);
    clone_source_menu_->addAction(panel_action);
    connect(clone_source_menu_, &QMenu::aboutToShow, this, [this] { refresh_clone_source_panel(); });
    connect(clone_source_panel_, &CloneSourcePanel::slot_picked, this, [this](int slot) {
      if (canvas_ != nullptr) {
        canvas_->set_clone_source_slot(slot);
      }
      refresh_clone_source_panel();
    });
    connect(clone_source_panel_, &CloneSourcePanel::offset_edited, this, [this](QPoint offset) {
      if (canvas_ != nullptr) {
        canvas_->set_clone_source_offset(offset);
      }
      refresh_clone_source_panel();
    });
    connect(clone_source_panel_, &CloneSourcePanel::transform_edited, this,
            [this](patchy::CloneSourceTransform transform) {
              if (canvas_ != nullptr) {
                canvas_->set_clone_source_transform(transform);
              }
              refresh_clone_source_panel();
            });
    connect(clone_source_panel_, &CloneSourcePanel::overlay_edited, this,
            [this](bool shown, int opacity) { apply_clone_overlay_settings(shown, opacity); });
    clone_source_options_button_->setMenu(clone_source_menu_);
  }
  add_option_widget(clone_source_options_button_, {CanvasTool::Clone, CanvasTool::Healing});

  add_option_label(QT_TR_NOOP("Patch:"), {CanvasTool::PatchTool});
  patch_mode_combo_ = new QComboBox(toolbar);
  patch_mode_combo_->setObjectName(QStringLiteral("patchModeCombo"));
  patch_mode_combo_->addItem(tr("Source"), static_cast<int>(CanvasWidget::PatchToolMode::Source));
  patch_mode_combo_->addItem(tr("Destination"), static_cast<int>(CanvasWidget::PatchToolMode::Destination));
  patch_mode_combo_->setCurrentIndex(std::max(
      0, patch_mode_combo_->findData(static_cast<int>(canvas_defaults->patch_tool_mode()))));
  patch_mode_combo_->setFixedWidth(104);
  bind_tooltip(patch_mode_combo_, QT_TR_NOOP("Source heals the dragged-from region; Destination copies it onto the drop point"));
  {
    QPointer<QComboBox> patch_mode_combo(patch_mode_combo_);
    register_retranslation([patch_mode_combo] {
      if (patch_mode_combo == nullptr || patch_mode_combo->count() < 2) {
        return;
      }
      QSignalBlocker blocker(patch_mode_combo);
      patch_mode_combo->setItemText(0, QObject::tr("Source"));
      patch_mode_combo->setItemText(1, QObject::tr("Destination"));
    });
  }
  add_option_widget(patch_mode_combo_, {CanvasTool::PatchTool});
  connect(patch_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || patch_mode_combo_ == nullptr) {
      return;
    }
    if (canvas_ != nullptr) {
      canvas_->set_patch_tool_mode(
          static_cast<CanvasWidget::PatchToolMode>(patch_mode_combo_->itemData(index).toInt()));
      save_tool_settings();
    }
  });

  patch_transparent_check_ = new CheckGlyphBox(tr("Transparent"), toolbar);
  patch_transparent_check_->setObjectName(QStringLiteral("patchTransparentCheck"));
  patch_transparent_check_->setChecked(canvas_defaults->patch_tool_transparent());
  bind_tooltip(patch_transparent_check_, QT_TR_NOOP("Keep the region and add only the sampled texture instead of replacing it; clearest when the "
         "source has distinct marks over a plain background"));
  add_option_separator({CanvasTool::PatchTool});
  add_option_widget(patch_transparent_check_, {CanvasTool::PatchTool});
  connect(patch_transparent_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_patch_tool_transparent(checked);
      save_tool_settings();
    }
  });

  // Sample (GitHub issue 382): which layers the retouch tools read. Item data is the
  // settings token; the texts are the Pick tool's Sample texts.
  const std::initializer_list<CanvasTool> retouch_tools{CanvasTool::Clone, CanvasTool::Healing,
                                                        CanvasTool::SpotHealing, CanvasTool::PatchTool};
  add_option_separator({CanvasTool::Clone, CanvasTool::Healing, CanvasTool::SpotHealing, CanvasTool::PatchTool});
  add_option_label(QT_TR_NOOP("Sample:"), retouch_tools);
  retouch_sample_layers_combo_ = new QComboBox(toolbar);
  retouch_sample_layers_combo_->setObjectName(QStringLiteral("retouchSampleLayersCombo"));
  const auto retouch_sample_text = [](SampleLayers mode) {
    static constexpr std::array<const char*, 3> kRetouchSampleTexts{
        QT_TR_NOOP("Current Layer"), QT_TR_NOOP("Current & Below"), QT_TR_NOOP("All Layers")};
    return QCoreApplication::translate(kMainWindowTranslationContext,
                                       kRetouchSampleTexts[static_cast<std::size_t>(mode)]);
  };
  for (const auto mode : {SampleLayers::CurrentLayer, SampleLayers::CurrentAndBelow, SampleLayers::AllLayers}) {
    retouch_sample_layers_combo_->addItem(retouch_sample_text(mode), QString::fromLatin1(sample_layers_token(mode)));
  }
  retouch_sample_layers_combo_->setCurrentIndex(std::max(
      0, retouch_sample_layers_combo_->findData(
             QString::fromLatin1(sample_layers_token(canvas_defaults->retouch_sample_layers())))));
  bind_tooltip(retouch_sample_layers_combo_,
               QT_TR_NOOP("Which layers the tool reads: the active layer, it and the layers below (heal on an "
                          "empty layer above the image), or the merged image"));
  add_option_widget(retouch_sample_layers_combo_, retouch_tools);
  connect(retouch_sample_layers_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || retouch_sample_layers_combo_ == nullptr || canvas_ == nullptr) {
      return;
    }
    canvas_->set_retouch_sample_layers(sample_layers_from_settings(
        retouch_sample_layers_combo_->itemData(index).toString().toStdString(), std::nullopt));
    sync_retouch_sample_controls_from_canvas();
    save_tool_settings();
  });
  // Ignore Adjustment Layers (GitHub issue 383): only the composite choices read it.
  retouch_ignore_adjustments_check_ = new CheckGlyphBox(tr("Ignore Adjustment Layers"), toolbar);
  retouch_ignore_adjustments_check_->setObjectName(QStringLiteral("retouchIgnoreAdjustmentsCheck"));
  retouch_ignore_adjustments_check_->setChecked(canvas_defaults->retouch_ignore_adjustments());
  retouch_ignore_adjustments_check_->setEnabled(canvas_defaults->retouch_sample_layers() !=
                                                SampleLayers::CurrentLayer);
  bind_tooltip(retouch_ignore_adjustments_check_,
               QT_TR_NOOP("Leave adjustment layers out of the sampled image, so an adjustment above the "
                          "retouched layer is not applied twice"));
  add_option_widget(retouch_ignore_adjustments_check_, retouch_tools);
  connect(retouch_ignore_adjustments_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_retouch_ignore_adjustments(checked);
      save_tool_settings();
    }
  });
  {
    QPointer<QComboBox> retouch_combo(retouch_sample_layers_combo_);
    register_retranslation([retouch_combo, retouch_sample_text] {
      if (retouch_combo == nullptr) {
        return;
      }
      const QSignalBlocker blocker(retouch_combo);
      for (int index = 0; index < retouch_combo->count(); ++index) {
        const auto token = retouch_combo->itemData(index).toString().toStdString();
        retouch_combo->setItemText(index, retouch_sample_text(sample_layers_from_settings(token, std::nullopt)));
      }
    });
  }

  add_option_separator({CanvasTool::PatchTool});
  // Patch tool: Remove Object heals the drawn selection automatically, the
  // same command as Edit > Remove Object and the Enter key.
  patch_remove_object_button_ = new QPushButton(tr("Remove Object"), toolbar);
  patch_remove_object_button_->setObjectName(QStringLiteral("patchRemoveObjectButton"));
  bind_widget_text(patch_remove_object_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Remove Object"));
  bind_tooltip(patch_remove_object_button_,
               QT_TRANSLATE_NOOP("patchy::ui::MainWindow",
                                 "Fill the selection from its surroundings with the content-aware search (Enter)"));
  add_option_widget(patch_remove_object_button_, {CanvasTool::PatchTool});
  connect(patch_remove_object_button_, &QPushButton::clicked, this, [this] {
    if (remove_object_action_ != nullptr) {
      remove_object_action_->trigger();
    }
  });

  // Red Eye (canvas_widget_red_eye.cpp): Pupil Size and Darken Amount, a spin box and a
  // slider each like the Fill tool's Opacity.
  const auto add_red_eye_percent = [&](const char* label, const char* spin_name, const char* slider_name,
                                       const char* tooltip, int value, QSpinBox*& spin_out, QSlider*& slider_out,
                                       void (MainWindow::*apply)(int)) {
    add_option_label(label, {CanvasTool::RedEye});
    auto* spin = new QSpinBox(toolbar);
    spin->setObjectName(QString::fromLatin1(spin_name));
    spin->setRange(1, 100);
    spin->setValue(value);
    spin->setSuffix(percent_suffix());
    configure_toolbar_spinbox(spin, 52);
    bind_tooltip(spin, tooltip);
    add_option_widget(spin, {CanvasTool::RedEye});
    auto* slider = new QSlider(Qt::Horizontal, toolbar);
    slider->setObjectName(QString::fromLatin1(slider_name));
    slider->setProperty(kOptionsBarCollapsibleProperty, true);
    slider->setRange(1, 100);
    slider->setValue(value);
    slider->setFixedWidth(110);
    bind_tooltip(slider, tooltip);
    add_option_widget(slider, {CanvasTool::RedEye});
    connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
    connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    connect(spin, &QSpinBox::valueChanged, this, apply);
    spin_out = spin;
    slider_out = slider;
  };
  add_red_eye_percent(QT_TR_NOOP("Pupil Size:"), "redEyePupilSizeSpin", "redEyePupilSizeSlider",
                      QT_TR_NOOP("Higher values take in more of the reddish rim around the pupil"),
                      current_red_eye_pupil_size_, red_eye_pupil_spin_, red_eye_pupil_slider_,
                      &MainWindow::set_red_eye_pupil_size);
  add_option_separator({CanvasTool::RedEye});
  add_red_eye_percent(QT_TR_NOOP("Darken Amount:"), "redEyeDarkenAmountSpin", "redEyeDarkenAmountSlider",
                      QT_TR_NOOP("How much darker the corrected pupil gets"), current_red_eye_darken_amount_,
                      red_eye_darken_spin_, red_eye_darken_slider_, &MainWindow::set_red_eye_darken_amount);

  add_option_separator({CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge, CanvasTool::BlurBrush,
                        CanvasTool::SharpenBrush});
  add_option_label(QT_TR_NOOP("Strength:"), {CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                                      CanvasTool::BlurBrush, CanvasTool::SharpenBrush});
  local_adjustment_strength_spin_ = new QSpinBox(toolbar);
  local_adjustment_strength_spin_->setObjectName(QStringLiteral("localAdjustmentStrengthSpin"));
  local_adjustment_strength_spin_->setRange(1, 100);
  local_adjustment_strength_spin_->setValue(current_local_adjustment_strength_);
  local_adjustment_strength_spin_->setSuffix(percent_suffix());
  bind_tooltip(local_adjustment_strength_spin_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Maximum adjustment applied during one stroke"));
  configure_toolbar_spinbox(local_adjustment_strength_spin_, 52);
  add_option_widget(local_adjustment_strength_spin_,
                    {CanvasTool::Dodge, CanvasTool::Burn, CanvasTool::Sponge,
                     CanvasTool::BlurBrush, CanvasTool::SharpenBrush});
  connect(local_adjustment_strength_spin_, &QSpinBox::valueChanged, this, [this](int value) {
    current_local_adjustment_strength_ = value;
    if (canvas_ != nullptr) {
      canvas_->set_local_adjustment_strength(value);
    }
    schedule_save_tool_settings();
    refresh_document_info();
  });

  add_option_label(QT_TR_NOOP("Range:"), {CanvasTool::Dodge, CanvasTool::Burn});
  local_tone_range_combo_ = new QComboBox(toolbar);
  local_tone_range_combo_->setObjectName(QStringLiteral("localToneRangeCombo"));
  local_tone_range_combo_->addItem(tr("Shadows"), static_cast<int>(CanvasWidget::LocalToneRange::Shadows));
  local_tone_range_combo_->addItem(tr("Midtones"), static_cast<int>(CanvasWidget::LocalToneRange::Midtones));
  local_tone_range_combo_->addItem(tr("Highlights"), static_cast<int>(CanvasWidget::LocalToneRange::Highlights));
  local_tone_range_combo_->setCurrentIndex(
      std::max(0, local_tone_range_combo_->findData(static_cast<int>(current_local_tone_range_))));
  local_tone_range_combo_->setFixedWidth(92);
  add_option_widget(local_tone_range_combo_, {CanvasTool::Dodge, CanvasTool::Burn});
  connect(local_tone_range_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || local_tone_range_combo_ == nullptr) {
      return;
    }
    current_local_tone_range_ =
        static_cast<CanvasWidget::LocalToneRange>(local_tone_range_combo_->itemData(index).toInt());
    if (canvas_ != nullptr) {
      canvas_->set_local_tone_range(current_local_tone_range_);
    }
    save_tool_settings();
    refresh_document_info();
  });
  QPointer<QComboBox> local_tone_range_combo(local_tone_range_combo_);
  register_retranslation([local_tone_range_combo] {
    if (local_tone_range_combo == nullptr || local_tone_range_combo->count() < 3) {
      return;
    }
    const QSignalBlocker blocker(local_tone_range_combo);
    local_tone_range_combo->setItemText(
        0, QCoreApplication::translate(kMainWindowTranslationContext, "Shadows"));
    local_tone_range_combo->setItemText(
        1, QCoreApplication::translate(kMainWindowTranslationContext, "Midtones"));
    local_tone_range_combo->setItemText(
        2, QCoreApplication::translate(kMainWindowTranslationContext, "Highlights"));
  });

  local_protect_tones_check_ = new CheckGlyphBox(tr("Protect Tones"), toolbar);
  local_protect_tones_check_->setObjectName(QStringLiteral("localProtectTonesCheck"));
  local_protect_tones_check_->setChecked(current_local_protect_tones_);
  bind_tooltip(local_protect_tones_check_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Preserve local color differences while lightening or darkening"));
  bind_widget_text(local_protect_tones_check_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Protect Tones"));
  add_option_separator({CanvasTool::Dodge, CanvasTool::Burn});
  add_option_widget(local_protect_tones_check_, {CanvasTool::Dodge, CanvasTool::Burn});
  connect(local_protect_tones_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_local_protect_tones_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_local_protect_tones(checked);
    }
    save_tool_settings();
  });

  add_option_label(QT_TR_NOOP("Mode:"), {CanvasTool::Sponge});
  sponge_mode_combo_ = new QComboBox(toolbar);
  sponge_mode_combo_->setObjectName(QStringLiteral("spongeModeCombo"));
  sponge_mode_combo_->addItem(tr("Saturate"), static_cast<int>(CanvasWidget::SpongeMode::Saturate));
  sponge_mode_combo_->addItem(tr("Desaturate"), static_cast<int>(CanvasWidget::SpongeMode::Desaturate));
  sponge_mode_combo_->setCurrentIndex(
      std::max(0, sponge_mode_combo_->findData(static_cast<int>(current_sponge_mode_))));
  sponge_mode_combo_->setFixedWidth(94);
  add_option_widget(sponge_mode_combo_, {CanvasTool::Sponge});
  connect(sponge_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || sponge_mode_combo_ == nullptr) {
      return;
    }
    current_sponge_mode_ =
        static_cast<CanvasWidget::SpongeMode>(sponge_mode_combo_->itemData(index).toInt());
    if (canvas_ != nullptr) {
      canvas_->set_sponge_mode(current_sponge_mode_);
    }
    save_tool_settings();
    refresh_document_info();
  });
  QPointer<QComboBox> sponge_mode_combo(sponge_mode_combo_);
  register_retranslation([sponge_mode_combo] {
    if (sponge_mode_combo == nullptr || sponge_mode_combo->count() < 2) {
      return;
    }
    const QSignalBlocker blocker(sponge_mode_combo);
    sponge_mode_combo->setItemText(
        0, QCoreApplication::translate(kMainWindowTranslationContext, "Saturate"));
    sponge_mode_combo->setItemText(
        1, QCoreApplication::translate(kMainWindowTranslationContext, "Desaturate"));
  });

  sponge_vibrance_check_ = new CheckGlyphBox(tr("Vibrance"), toolbar);
  sponge_vibrance_check_->setObjectName(QStringLiteral("spongeVibranceCheck"));
  sponge_vibrance_check_->setChecked(current_sponge_vibrance_);
  bind_tooltip(sponge_vibrance_check_, QT_TR_NOOP("Reduce the adjustment on colors that are already strongly saturated"));
  bind_tooltip(sponge_vibrance_check_,
               QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Reduce the adjustment on colors that are already strongly saturated"));
  bind_widget_text(sponge_vibrance_check_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Vibrance"));
  add_option_separator({CanvasTool::Sponge});
  add_option_widget(sponge_vibrance_check_, {CanvasTool::Sponge});
  connect(sponge_vibrance_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_sponge_vibrance_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_sponge_vibrance(checked);
    }
    save_tool_settings();
  });

  quick_select_sample_all_layers_check_ = new CheckGlyphBox(tr("Sample All Layers"), toolbar);
  quick_select_sample_all_layers_check_->setObjectName(QStringLiteral("quickSelectSampleAllLayersCheck"));
  quick_select_sample_all_layers_check_->setChecked(canvas_defaults->quick_select_sample_all_layers());
  bind_tooltip(quick_select_sample_all_layers_check_, QT_TR_NOOP("Sample the merged document instead of the active layer"));
  add_option_widget(quick_select_sample_all_layers_check_, {CanvasTool::QuickSelect});
  connect(quick_select_sample_all_layers_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_quick_select_sample_all_layers(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  quick_select_enhance_edge_check_ = new CheckGlyphBox(tr("Enhance Edge"), toolbar);
  quick_select_enhance_edge_check_->setObjectName(QStringLiteral("quickSelectEnhanceEdgeCheck"));
  quick_select_enhance_edge_check_->setChecked(canvas_defaults->quick_select_enhance_edge());
  bind_tooltip(quick_select_enhance_edge_check_, QT_TR_NOOP("Smooth the selection boundary after each stroke"));
  add_option_widget(quick_select_enhance_edge_check_, {CanvasTool::QuickSelect});
  connect(quick_select_enhance_edge_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_quick_select_enhance_edge(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  // Object Selection (docs/ai-selection.md): what the encoder samples, plus the Select
  // Subject command, which Quick Select's bar also offers (Photoshop parity).
  object_select_sample_all_layers_check_ = new CheckGlyphBox(tr("Sample All Layers"), toolbar);
  object_select_sample_all_layers_check_->setObjectName(QStringLiteral("objectSelectSampleAllLayersCheck"));
  object_select_sample_all_layers_check_->setChecked(canvas_defaults->object_select_sample_all_layers());
  bind_tooltip(object_select_sample_all_layers_check_,
               QT_TR_NOOP("Sample the merged document instead of the active layer"));
  add_option_widget(object_select_sample_all_layers_check_, {CanvasTool::ObjectSelect});
  connect(object_select_sample_all_layers_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_object_select_sample_all_layers(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });
#if !defined(Q_OS_WASM)
  // The model Object Selection runs (docs/ai-selection.md): the bundled Standard model, or
  // the High Quality model once downloaded. Choosing High Quality offers the download.
  configure_automatic_selection_models();
  if (patchy::ai::segmentation_available()) {
    add_option_separator({CanvasTool::ObjectSelect});  // GitHub issue 292
    add_option_label(QT_TR_NOOP("Model:"), {CanvasTool::ObjectSelect});
    object_select_model_combo_ = new QComboBox(toolbar);
    object_select_model_combo_->setObjectName(QStringLiteral("objectSelectModelCombo"));
    object_select_model_combo_->addItems({tr("Standard"), tr("High Quality")});
    QPointer<QComboBox> model_combo(object_select_model_combo_);
    register_retranslation([model_combo] {
      if (model_combo == nullptr || model_combo->count() < 2) {
        return;
      }
      QSignalBlocker blocker(model_combo);
      model_combo->setItemText(0, MainWindow::tr("Standard"));
      model_combo->setItemText(1, MainWindow::tr("High Quality"));
    });
    bind_tooltip(object_select_model_combo_,
                 QT_TRANSLATE_NOOP("patchy::ui::MainWindow",
                                   "Standard is built in. High Quality finds objects more accurately and is "
                                   "downloaded once."));
    add_option_widget(object_select_model_combo_, {CanvasTool::ObjectSelect});
    refresh_object_select_model_combo();
    connect(object_select_model_combo_, &QComboBox::activated, this, [this](int index) {
      if (index == 0) {
        set_object_selection_high_quality_setting(false);
      } else if (patchy::ai::high_quality_model_available()) {
        set_object_selection_high_quality_setting(true);
      } else {
        offer_high_quality_object_selection_model();
      }
      refresh_object_select_model_combo();
    });
  }
#endif
  select_subject_button_ = new QPushButton(tr("Select Subject"), toolbar);
  select_subject_button_->setObjectName(QStringLiteral("selectSubjectButton"));
  bind_widget_text(select_subject_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Select Subject"));
  bind_tooltip(select_subject_button_,
               QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Select the main subject of the image automatically"));
  // Both buttons run the subject model, which builds can leave out (subject_selection_available).
  configure_automatic_selection_models();
  if (patchy::ai::subject_selection_available()) {
    add_option_widget(select_subject_button_, {CanvasTool::QuickSelect, CanvasTool::ObjectSelect});
  } else {
    select_subject_button_->hide();
  }
  remove_background_button_ = new QPushButton(tr("Remove Background"), toolbar);
  remove_background_button_->setObjectName(QStringLiteral("removeBackgroundButton"));
  bind_widget_text(remove_background_button_, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Remove Background"));
  bind_tooltip(remove_background_button_,
               QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Hide the background with a layer mask on the active layer"));
  if (patchy::ai::subject_selection_available()) {
    add_option_widget(remove_background_button_, {CanvasTool::ObjectSelect});
  } else {
    remove_background_button_->hide();
  }
  connect(remove_background_button_, &QPushButton::clicked, this, [this] { remove_background(); });
  connect(select_subject_button_, &QPushButton::clicked, this, [this] {
    // The tool's own combine mode applies, so Add grows an existing selection.
    auto mode = canvas_ != nullptr ? canvas_->selection_mode() : CanvasWidget::SelectionMode::Replace;
    if (mode == CanvasWidget::SelectionMode::Intersect) {
      mode = CanvasWidget::SelectionMode::Add;
    }
    select_subject(mode);
  });

  add_option_label(QT_TR_NOOP("Width:"), {CanvasTool::MagneticLasso});
  auto* magnetic_width = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  magnetic_width->setObjectName(QStringLiteral("magneticLassoWidthSpin"));
  magnetic_width->set_context_provider(document_unit_context_provider(true));
  magnetic_width->setRange(1, 256);
  magnetic_width->setValue(canvas_defaults->magnetic_lasso_width());
  bind_tooltip(magnetic_width, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Edge search width in document pixels: press [ or ]"));
  configure_toolbar_spinbox(magnetic_width, 64);
  add_option_widget(magnetic_width, {CanvasTool::MagneticLasso});
  connect(magnetic_width, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_magnetic_lasso_width(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  add_option_label(QT_TR_NOOP("Contrast:"), {CanvasTool::MagneticLasso});
  auto* magnetic_contrast = new QSpinBox(toolbar);
  magnetic_contrast->setObjectName(QStringLiteral("magneticLassoContrastSpin"));
  magnetic_contrast->setRange(1, 100);
  magnetic_contrast->setSuffix(percent_suffix());
  magnetic_contrast->setValue(canvas_defaults->magnetic_lasso_edge_contrast());
  bind_tooltip(magnetic_contrast, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "Minimum edge contrast the trace snaps to"));
  configure_toolbar_spinbox(magnetic_contrast, 56);
  add_option_widget(magnetic_contrast, {CanvasTool::MagneticLasso});
  connect(magnetic_contrast, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_magnetic_lasso_edge_contrast(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });
  add_option_label(QT_TR_NOOP("Frequency:"), {CanvasTool::MagneticLasso});
  auto* magnetic_frequency = new QSpinBox(toolbar);
  magnetic_frequency->setObjectName(QStringLiteral("magneticLassoFrequencySpin"));
  magnetic_frequency->setRange(0, 100);
  magnetic_frequency->setValue(canvas_defaults->magnetic_lasso_frequency());
  bind_tooltip(magnetic_frequency, QT_TRANSLATE_NOOP("patchy::ui::MainWindow", "How often anchor points are placed while tracing"));
  configure_toolbar_spinbox(magnetic_frequency, 46);
  add_option_widget(magnetic_frequency, {CanvasTool::MagneticLasso});
  connect(magnetic_frequency, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_magnetic_lasso_frequency(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  auto* brush_smaller_action = new QAction(tr("Brush Smaller"), this);
  auto* brush_larger_action = new QAction(tr("Brush Larger"), this);
  auto* brush_much_smaller_action = new QAction(tr("Brush Much Smaller"), this);
  auto* brush_much_larger_action = new QAction(tr("Brush Much Larger"), this);
  brush_smaller_action->setObjectName(QStringLiteral("brushSmallerAction"));
  brush_larger_action->setObjectName(QStringLiteral("brushLargerAction"));
  brush_much_smaller_action->setObjectName(QStringLiteral("brushMuchSmallerAction"));
  brush_much_larger_action->setObjectName(QStringLiteral("brushMuchLargerAction"));
  register_hotkey(brush_smaller_action, "brush.smaller", QKeySequence(Qt::Key_BracketLeft), QStringLiteral("brush"));
  register_hotkey(brush_larger_action, "brush.larger", QKeySequence(Qt::Key_BracketRight), QStringLiteral("brush"));
  register_hotkey(brush_much_smaller_action, "brush.much_smaller", QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft), QStringLiteral("brush"));
  register_hotkey(brush_much_larger_action, "brush.much_larger", QKeySequence(Qt::SHIFT | Qt::Key_BracketRight), QStringLiteral("brush"));
  addAction(brush_smaller_action);
  addAction(brush_larger_action);
  addAction(brush_much_smaller_action);
  addAction(brush_much_larger_action);
  // The bracket keys resize whichever brush the active tool uses (Quick Select
  // has its own; for the Magnetic Lasso they adjust the edge search width).
  const auto adjust_brush_size = [this, brush_size, quick_select_size, magnetic_width](int direction, bool coarse) {
    const bool quick_select = current_tool_ == CanvasTool::QuickSelect;
    const bool magnetic = current_tool_ == CanvasTool::MagneticLasso;
    auto* spin = quick_select ? quick_select_size : magnetic ? magnetic_width : brush_size;
    const int cap = quick_select ? 512 : magnetic ? 256 : kMaxBrushSize;
    const int value = spin->value();
    const int step = proportional_brush_step(value, direction, coarse);
    spin->setValue(std::clamp(value + direction * step, 1, cap));
  };
  connect(brush_smaller_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(-1, false); });
  connect(brush_larger_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(1, false); });
  connect(brush_much_smaller_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(-1, true); });
  connect(brush_much_larger_action, &QAction::triggered, brush_size,
          [adjust_brush_size] { adjust_brush_size(1, true); });
  for (auto* action : {brush_smaller_action, brush_larger_action, brush_much_smaller_action,
                       brush_much_larger_action}) {
    register_document_action(action);
  }

  // Spelled out and set apart from the checkboxes (GitHub issue 290).
  add_option_label(QT_TR_NOOP("Tolerance:"), {CanvasTool::MagicWand});
  auto* wand_tolerance = new QSpinBox(toolbar);
  wand_tolerance->setObjectName(QStringLiteral("wandToleranceSpin"));
  wand_tolerance->setRange(0, 255);
  wand_tolerance->setValue(canvas_defaults->wand_tolerance());
  configure_toolbar_spinbox(wand_tolerance, 46);
  add_option_widget(wand_tolerance, {CanvasTool::MagicWand});
  add_option_separator({CanvasTool::MagicWand});
  connect(wand_tolerance, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_tolerance(value);
      schedule_save_tool_settings();
      refresh_document_info();
    }
  });

  wand_contiguous_check_ = new CheckGlyphBox(tr("Contiguous"), toolbar);
  wand_contiguous_check_->setObjectName(QStringLiteral("wandContiguousCheck"));
  wand_contiguous_check_->setChecked(canvas_defaults->wand_contiguous());
  bind_tooltip(wand_contiguous_check_, QT_TR_NOOP("Limit Magic Wand selection to connected pixels"));
  add_option_widget(wand_contiguous_check_, {CanvasTool::MagicWand});
  connect(wand_contiguous_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_contiguous(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  wand_sample_all_layers_check_ = new CheckGlyphBox(tr("Sample All Layers"), toolbar);
  wand_sample_all_layers_check_->setObjectName(QStringLiteral("wandSampleAllLayersCheck"));
  wand_sample_all_layers_check_->setChecked(canvas_defaults->wand_sample_all_layers());
  bind_tooltip(wand_sample_all_layers_check_, QT_TR_NOOP("Sample the merged document instead of the active layer"));
  add_option_widget(wand_sample_all_layers_check_, {CanvasTool::MagicWand});
  connect(wand_sample_all_layers_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_wand_sample_all_layers(checked);
      save_tool_settings();
      refresh_document_info();
    }
  });

  // Sets (GitHub issue 151): which color a click sets, a two-way choice like
  // GIMP's Pick Target. Alt inverts it for one click, and the cursor badge says
  // which color the click will set. The pair leads the bar as the tool's mode
  // group, unlabeled (GitHub issue 322; docs/options-bar.md rule 1).
  auto* eyedropper_sets_group = new QButtonGroup(toolbar);
  eyedropper_sets_group->setExclusive(true);
  const auto add_sets_button = [&](QPushButton*& button, const char* text, const char* tooltip,
                                   const QString& object_name, bool background) {
    button = new QPushButton(tr(text), toolbar);
    button->setObjectName(object_name);
    button->setCheckable(true);
    button->setChecked(current_eyedropper_picks_background_ == background);
    button->setFocusPolicy(Qt::NoFocus);
    bind_widget_text(button, text);
    // %ALT% names Option on macOS (resolve_modifier_names), so this tooltip is
    // set by a callback rather than bound.
    QPointer<QPushButton> guarded(button);
    const auto apply_tooltip = [guarded, tooltip] {
      if (guarded != nullptr) {
        guarded->setToolTip(resolve_modifier_names(tr(tooltip)));
      }
    };
    apply_tooltip();
    register_retranslation(apply_tooltip);
    eyedropper_sets_group->addButton(button);
    add_option_widget(button, {CanvasTool::Eyedropper});
    connect(button, &QPushButton::toggled, this, [this, background](bool checked) {
      if (!checked) {
        return;
      }
      current_eyedropper_picks_background_ = background;
      if (canvas_ != nullptr) {
        canvas_->set_eyedropper_picks_background(background);
      }
      save_tool_settings();
    });
  };
  add_sets_button(eyedropper_sets_foreground_button_, QT_TR_NOOP("Foreground"),
                  QT_TR_NOOP("A click sets the foreground color; %ALT%+click sets the background"),
                  QStringLiteral("eyedropperSetsForegroundButton"), false);
  add_sets_button(eyedropper_sets_background_button_, QT_TR_NOOP("Background"),
                  QT_TR_NOOP("A click sets the background color; %ALT%+click sets the foreground"),
                  QStringLiteral("eyedropperSetsBackgroundButton"), true);
  add_option_separator({CanvasTool::Eyedropper});

  // Photoshop's Eyedropper options (GitHub issue 151): Size averages a square
  // around the click, Layer picks from the merged document or the active layer
  // alone (labels shortened in GitHub issue 409). The paint tools' Alt
  // eyedropper uses the same settings.
  add_option_label(QT_TR_NOOP("Size:"), {CanvasTool::Eyedropper});
  eyedropper_sample_size_combo_ = new QComboBox(toolbar);
  eyedropper_sample_size_combo_->setObjectName(QStringLiteral("eyedropperSampleSizeCombo"));
  fill_sample_size_combo(eyedropper_sample_size_combo_, current_eyedropper_sample_size_);
  // Marked for lupdate; eyedropper_sample_size_text builds its texts from these sources.
  static_cast<void>(QT_TR_NOOP("Point Sample"));
  static_cast<void>(QT_TR_NOOP("%1 by %1 Average"));
  bind_tooltip(eyedropper_sample_size_combo_,
               QT_TR_NOOP("Average the colors in a square around the click instead of one pixel"));
  add_option_widget(eyedropper_sample_size_combo_, {CanvasTool::Eyedropper});
  connect(eyedropper_sample_size_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || eyedropper_sample_size_combo_ == nullptr) {
      return;
    }
    apply_shared_sample_size(eyedropper_sample_size_combo_->itemData(index).toInt());
  });

  add_option_label(QT_TR_NOOP("Layer:"), {CanvasTool::Eyedropper});
  eyedropper_sample_layers_combo_ = new QComboBox(toolbar);
  eyedropper_sample_layers_combo_->setObjectName(QStringLiteral("eyedropperSampleLayersCombo"));
  // Item data is the settings token (GitHub issue 321); the language refresh below
  // relabels each item from its token.
  const auto sample_layers_text = [](SampleLayers mode) {
    static constexpr std::array<const char*, 5> kSampleLayersTexts{
        QT_TR_NOOP("Current Layer"), QT_TR_NOOP("Current & Below"), QT_TR_NOOP("All Layers"),
        QT_TR_NOOP("All Layers No Adjustments"), QT_TR_NOOP("Current & Below No Adjustments")};
    return QCoreApplication::translate(kMainWindowTranslationContext,
                                       kSampleLayersTexts[static_cast<std::size_t>(mode)]);
  };
  for (const auto mode : {SampleLayers::CurrentLayer, SampleLayers::CurrentAndBelow, SampleLayers::AllLayers,
                          SampleLayers::AllLayersNoAdjustments, SampleLayers::CurrentAndBelowNoAdjustments}) {
    eyedropper_sample_layers_combo_->addItem(sample_layers_text(mode),
                                             QString::fromLatin1(sample_layers_token(mode)));
  }
  eyedropper_sample_layers_combo_->setCurrentIndex(std::max(
      0, eyedropper_sample_layers_combo_->findData(
             QString::fromLatin1(sample_layers_token(current_eyedropper_sample_layers_)))));
  bind_tooltip(eyedropper_sample_layers_combo_,
               QT_TR_NOOP("Which layers a pick reads: the active layer, it and the layers below, or the merged "
                          "image, optionally without adjustment layers"));
  add_option_widget(eyedropper_sample_layers_combo_, {CanvasTool::Eyedropper});
  connect(eyedropper_sample_layers_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index < 0 || eyedropper_sample_layers_combo_ == nullptr) {
      return;
    }
    current_eyedropper_sample_layers_ =
        sample_layers_from_settings(eyedropper_sample_layers_combo_->itemData(index).toString().toStdString(),
                                    std::nullopt);
    if (canvas_ != nullptr) {
      canvas_->set_eyedropper_sample_layers(current_eyedropper_sample_layers_);
    }
    save_tool_settings();
  });
  // Show Sample Ring (GitHub issue 320): the sample loupe while a pick is held,
  // for the Pick tool and the paint tools' Alt eyedropper alike.
  add_option_separator({CanvasTool::Eyedropper});
  eyedropper_show_sample_ring_check_ = new CheckGlyphBox(tr("Show Sample Ring"), toolbar);
  eyedropper_show_sample_ring_check_->setObjectName(QStringLiteral("eyedropperShowSampleRingCheck"));
  eyedropper_show_sample_ring_check_->setChecked(current_eyedropper_show_sample_ring_);
  bind_tooltip(eyedropper_show_sample_ring_check_,
               QT_TR_NOOP("While picking, show the pixels enlarged and the new color above the one it replaces"));
  add_option_widget(eyedropper_show_sample_ring_check_, {CanvasTool::Eyedropper});
  connect(eyedropper_show_sample_ring_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_eyedropper_show_sample_ring_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_eyedropper_show_sample_ring(checked);
    }
    save_tool_settings();
  });
  QPointer<QComboBox> eyedropper_size_combo(eyedropper_sample_size_combo_);
  QPointer<QComboBox> eyedropper_layers_combo(eyedropper_sample_layers_combo_);
  QPointer<QComboBox> wand_size_combo(wand_sample_size_combo_);
  register_retranslation([eyedropper_size_combo, wand_size_combo, eyedropper_layers_combo, sample_layers_text] {
    for (QComboBox* combo : {eyedropper_size_combo.data(), wand_size_combo.data()}) {
      if (combo != nullptr) {
        retranslate_sample_size_combo(combo);
      }
    }
    if (eyedropper_layers_combo != nullptr) {
      const QSignalBlocker blocker(eyedropper_layers_combo);
      for (int index = 0; index < eyedropper_layers_combo->count(); ++index) {
        const auto token = eyedropper_layers_combo->itemData(index).toString().toStdString();
        eyedropper_layers_combo->setItemText(index, sample_layers_text(sample_layers_from_settings(token, std::nullopt)));
      }
    }
  });


  // The appearance controls also register for the path-select tools: there
  // they show only while an editable shape layer is active and live-edit it
  // (refresh_vector_tool_options_visibility refines; Photoshop's behavior).
  const std::initializer_list<CanvasTool> vector_appearance_tools{
      CanvasTool::Line,    CanvasTool::Rectangle,  CanvasTool::Ellipse,
      CanvasTool::Pen,     CanvasTool::Polygon,    CanvasTool::CustomShape,
      CanvasTool::PathSelect, CanvasTool::DirectSelect};
  // Not on the Move bar (GitHub issue 350): there the shape's size is in the Properties panel.
  const std::initializer_list<CanvasTool> vector_shape_size_tools{
      CanvasTool::Line,     CanvasTool::Rectangle,  CanvasTool::Ellipse,
      CanvasTool::Pen,      CanvasTool::Polygon,    CanvasTool::CustomShape,
      CanvasTool::PathSelect, CanvasTool::DirectSelect};
  vector_shape_mode_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("Fill:"), vector_appearance_tools));
  // The Fill and Stroke swatches are framed buttons with a wide chip, so they read as
  // something to click (GitHub issue 311); the frame is the QSS optionsBarSwatch rule.
  // The Text color button below is the same button (GitHub issue 343).
  const auto configure_swatch_button = [](QToolButton* button) {
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setProperty("optionsBarSwatch", true);
    button->setIconSize(QSize(28, 16));
    button->setFixedSize(38, 26);
    button->setFocusPolicy(Qt::NoFocus);
  };
  vector_fill_swatch_button_ = new QToolButton(toolbar);
  vector_fill_swatch_button_->setObjectName(QStringLiteral("vectorFillSwatchButton"));
  bind_tooltip(vector_fill_swatch_button_, QT_TR_NOOP("Shape fill: none, solid color, gradient, or pattern"));
  configure_swatch_button(vector_fill_swatch_button_);
  add_option_widget(vector_fill_swatch_button_, vector_appearance_tools);
  vector_shape_mode_option_widgets_.push_back(vector_fill_swatch_button_);
  connect(vector_fill_swatch_button_, &QToolButton::clicked, this,
          [this] { show_vector_paint_menu(false); });
  // Fill | Stroke [width] [swatch] (GitHub issue 311). There is no Stroke checkbox: the
  // width is the switch, 0 px for no stroke (set_vector_stroke_width_from_field).
  add_option_separator({CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                        CanvasTool::Polygon, CanvasTool::CustomShape});
  // A thickness has no percent basis: the document extent means nothing to it.
  const auto thickness_context = [this]() -> UnitConversionContext {
    auto context = document_unit_context(true);
    context.percent_reference_pixels = 0.0;
    return context;
  };
  auto* vector_stroke_width_label = add_option_label(QT_TR_NOOP("Stroke:"), vector_appearance_tools);
  vector_stroke_width_label->setObjectName(QStringLiteral("vectorStrokeWidthLabel"));
  vector_shape_mode_option_widgets_.push_back(vector_stroke_width_label);
  auto* vector_stroke_width = new UnitSpinBox(SpinUnit::Pixels, toolbar);
  vector_stroke_width->setObjectName(QStringLiteral("vectorStrokeWidthSpin"));
  vector_stroke_width->setRange(0.0, 1000.0);
  vector_stroke_width->setDecimals(1);
  vector_stroke_width->setValue(vector_stroke_field_value());
  vector_stroke_width->set_context_provider(thickness_context);
  bind_tooltip(vector_stroke_width, QT_TR_NOOP("Stroke width (0 for no stroke)"));
  configure_toolbar_spinbox(vector_stroke_width, 64);
  add_option_widget(vector_stroke_width, vector_appearance_tools);
  vector_shape_mode_option_widgets_.push_back(vector_stroke_width);
  register_ruler_unit_field(vector_stroke_width);  // print users think in mm strokes
  connect(vector_stroke_width, &QDoubleSpinBox::valueChanged, this,
          [this](double value) { set_vector_stroke_width_from_field(value); });

  install_appearance_edit_intent(vector_stroke_width, [this, vector_stroke_width] {
    set_vector_stroke_width_from_field(vector_stroke_width->value());
  });
  connect(vector_stroke_width, &QDoubleSpinBox::editingFinished, this,
          [this] { finish_pending_shape_appearance_edit(); });

  vector_stroke_swatch_button_ = new QToolButton(toolbar);
  vector_stroke_swatch_button_->setObjectName(QStringLiteral("vectorStrokeSwatchButton"));
  bind_tooltip(vector_stroke_swatch_button_, QT_TR_NOOP("Shape stroke: solid color, gradient, or pattern"));
  configure_swatch_button(vector_stroke_swatch_button_);
  add_option_widget(vector_stroke_swatch_button_, vector_appearance_tools);
  vector_shape_mode_option_widgets_.push_back(vector_stroke_swatch_button_);
  connect(vector_stroke_swatch_button_, &QToolButton::clicked, this,
          [this] { show_vector_paint_menu(true); });

  // W / H of the ACTIVE shape layer (Photoshop's options-bar readouts): they
  // mirror the selected shape's bounds and resize it live (top-left anchored,
  // axis-aligned scale, so live shapes stay live); disabled without one.
  const auto make_shape_size_spin = [this, toolbar, &vector_shape_size_tools,
                                     add_option_widget](const char* name, const char* tooltip) {
    auto* spin = new UnitSpinBox(SpinUnit::Pixels, toolbar);
    spin->setObjectName(QLatin1String(name));
    spin->setRange(0.0, 60000.0);
    spin->setDecimals(1);
    spin->setSpecialValueText(QStringLiteral(" "));  // 0 = no shape to show
    spin->setKeyboardTracking(false);
    spin->setEnabled(false);
    bind_tooltip(spin, tooltip);
    configure_toolbar_spinbox(spin, 84);
    add_option_widget(spin, vector_shape_size_tools);
    vector_shape_size_option_widgets_.push_back(spin);
    return spin;
  };
  add_option_separator({CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                        CanvasTool::Polygon, CanvasTool::CustomShape});
  vector_shape_size_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("W:"), vector_shape_size_tools));
  vector_shape_width_spin_ =
      make_shape_size_spin("vectorShapeWidthSpin", QT_TR_NOOP("Width of the active shape"));
  vector_shape_link_size_button_ = new QPushButton(toolbar);
  vector_shape_link_size_button_->setObjectName(QStringLiteral("vectorShapeLinkSizeButton"));
  vector_shape_link_size_button_->setCheckable(true);
  vector_shape_link_size_button_->setChecked(false);
  vector_shape_link_size_button_->setIcon(simple_icon(QStringLiteral("link"), QColor(220, 226, 235)));
  bind_tooltip(vector_shape_link_size_button_, QT_TR_NOOP("Keep the shape's width and height in proportion"));
  vector_shape_link_size_button_->setFixedWidth(28);
  vector_shape_link_size_button_->setEnabled(false);
  add_option_widget(vector_shape_link_size_button_, vector_shape_size_tools);
  vector_shape_size_option_widgets_.push_back(vector_shape_link_size_button_);
  connect(vector_shape_link_size_button_, &QPushButton::toggled, this, [this](bool checked) {
    if (properties_shape_link_size_button_ != nullptr) {
      const QSignalBlocker blocker(properties_shape_link_size_button_);
      properties_shape_link_size_button_->setChecked(checked);
    }
    if (canvas_ != nullptr) {
      canvas_->set_custom_shape_keep_proportions(checked);
    }
  });
  // Even space on both sides of the toggle (GitHub issue 311, the icon-button rule in
  // docs/options-bar.md): the H label drops its text padding, so its text sits as far from the
  // button as the W field does.
  vector_shape_size_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("H:"), vector_shape_size_tools));
  vector_shape_size_option_widgets_.back()->setObjectName(QStringLiteral("vectorShapeHeightLabel"));
  vector_shape_size_option_widgets_.back()->setStyleSheet(QStringLiteral("QLabel { padding-left: 0; }"));
  vector_shape_height_spin_ =
      make_shape_size_spin("vectorShapeHeightSpin", QT_TR_NOOP("Height of the active shape"));
  // The readouts follow the ruler unit; value() stays document pixels.
  vector_shape_width_spin_->set_context_provider(document_unit_context_provider(true));
  vector_shape_height_spin_->set_context_provider(document_unit_context_provider(false));
  register_ruler_unit_field(vector_shape_width_spin_);
  register_ruler_unit_field(vector_shape_height_spin_);
  connect(vector_shape_width_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    handle_vector_shape_size_value_changed(true, value);
  });
  connect(vector_shape_height_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    handle_vector_shape_size_value_changed(false, value);
  });

  add_option_separator({CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                        CanvasTool::Polygon, CanvasTool::CustomShape});
  vector_vector_mode_option_widgets_.push_back(add_option_label(
      QT_TR_NOOP("Combine:"), {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                       CanvasTool::Polygon, CanvasTool::CustomShape,
                       CanvasTool::PathSelect, CanvasTool::DirectSelect}));
  auto* vector_combine_combo = new QComboBox(toolbar);
  vector_combine_combo->setObjectName(QStringLiteral("vectorCombineCombo"));
  vector_combine_combo->addItems(
      {tr("New Layer"), tr("Add"), tr("Subtract"), tr("Intersect"), tr("Exclude")});
  vector_combine_combo->setCurrentIndex(0);
  vector_combine_combo->setFixedWidth(88);
  widen_combo_popup_to_items(vector_combine_combo);
  bind_tooltip(vector_combine_combo, QT_TR_NOOP("How the next shape combines with the active shape layer or work path"));
  QPointer<QComboBox> vector_combine_combo_pointer(vector_combine_combo);
  register_retranslation([vector_combine_combo_pointer] {
    if (vector_combine_combo_pointer == nullptr || vector_combine_combo_pointer->count() < 5) {
      return;
    }
    QSignalBlocker blocker(vector_combine_combo_pointer);
    vector_combine_combo_pointer->setItemText(0, MainWindow::tr("New Layer"));
    vector_combine_combo_pointer->setItemText(1, MainWindow::tr("Add"));
    vector_combine_combo_pointer->setItemText(2, MainWindow::tr("Subtract"));
    vector_combine_combo_pointer->setItemText(3, MainWindow::tr("Intersect"));
    vector_combine_combo_pointer->setItemText(4, MainWindow::tr("Exclude"));
  });
  add_option_widget(vector_combine_combo,
                    {CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                     CanvasTool::Polygon, CanvasTool::CustomShape,
                     CanvasTool::PathSelect, CanvasTool::DirectSelect});
  vector_vector_mode_option_widgets_.push_back(vector_combine_combo);
  connect(vector_combine_combo, &QComboBox::currentIndexChanged, this, [this](int index) {
    current_vector_combine_index_ = index;
    // With a path-select selection, the combo edits the selected shapes'
    // combine operation in place (indices 1-4; "New Layer" is creation-only).
    if (canvas_ != nullptr && index >= 1 &&
        (canvas_->tool() == CanvasTool::PathSelect ||
         canvas_->tool() == CanvasTool::DirectSelect) &&
        canvas_->path_edit_has_selection()) {
      canvas_->set_selected_subpaths_combine_op(index == 1   ? patchy::PathCombineOp::Add
                                                : index == 2 ? patchy::PathCombineOp::Subtract
                                                : index == 3 ? patchy::PathCombineOp::Intersect
                                                             : patchy::PathCombineOp::Xor);
    }
  });
  update_vector_swatch_icons();

  // Each tool's own settings follow Combine as one group (GitHub issue 311): Line's Weight
  // and arrowheads, Pen's Auto Add/Delete, Polygon's Sides and Star inset, Custom Shape's
  // Shape, and Rectangle's Radius. The Line bar used to put Weight before Combine.
  add_option_separator({CanvasTool::Line, CanvasTool::Pen, CanvasTool::Polygon, CanvasTool::CustomShape});
  vector_vector_mode_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("Weight:"), {CanvasTool::Line}));
  // Fractional so a 0.5 mm hairline survives a ruler unit of mm; the shape
  // model's line_weight is a double already.
  auto* vector_line_weight = new UnitSpinBox(SpinUnit::Pixels, toolbar);
  vector_line_weight->setObjectName(QStringLiteral("vectorLineWeightSpin"));
  vector_line_weight->setRange(1.0, 1000.0);
  vector_line_weight->setDecimals(1);
  vector_line_weight->setValue(current_vector_line_weight_);
  vector_line_weight->set_context_provider(thickness_context);
  bind_tooltip(vector_line_weight, QT_TR_NOOP("Line thickness"));
  vector_line_weight->setProperty(kToolbarSpinboxSliderCurvedProperty, true);
  configure_toolbar_spinbox(vector_line_weight, 58);
  add_option_widget(vector_line_weight, {CanvasTool::Line});
  vector_vector_mode_option_widgets_.push_back(vector_line_weight);
  register_ruler_unit_field(vector_line_weight);
  connect(vector_line_weight, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    current_vector_line_weight_ = value;
    schedule_save_tool_settings();
  });

  // Photoshop's Pen "Auto Add/Delete": on, a click on the target path edits
  // its anchors (add on a segment, delete on a point); off, every click draws.
  auto* pen_auto_add_delete = new CheckGlyphBox(tr("Auto Add/Delete"), toolbar);
  bind_widget_text(pen_auto_add_delete, QT_TR_NOOP("Auto Add/Delete"));
  pen_auto_add_delete->setObjectName(QStringLiteral("penAutoAddDeleteCheck"));
  pen_auto_add_delete->setChecked(current_pen_auto_add_delete_);
  bind_tooltip(pen_auto_add_delete, QT_TR_NOOP("Clicking a segment of the path adds a point and clicking a point deletes it"));
  add_option_widget(pen_auto_add_delete, {CanvasTool::Pen});
  connect(pen_auto_add_delete, &QCheckBox::toggled, this, [this](bool checked) {
    current_pen_auto_add_delete_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_pen_auto_add_delete(checked);
    }
    schedule_save_tool_settings();
  });

  vector_vector_mode_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("Sides:"), {CanvasTool::Polygon}));
  auto* polygon_sides = new QSpinBox(toolbar);
  polygon_sides->setObjectName(QStringLiteral("polygonSidesSpin"));
  polygon_sides->setRange(3, 100);
  polygon_sides->setValue(5);
  configure_toolbar_spinbox(polygon_sides, 52);
  add_option_widget(polygon_sides, {CanvasTool::Polygon});
  vector_vector_mode_option_widgets_.push_back(polygon_sides);
  connect(polygon_sides, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_polygon_sides(value);
      schedule_save_tool_settings();
    }
  });

  vector_vector_mode_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("Star inset:"), {CanvasTool::Polygon}));
  auto* polygon_star_inset = new QSpinBox(toolbar);
  polygon_star_inset->setObjectName(QStringLiteral("polygonStarInsetSpin"));
  polygon_star_inset->setRange(0, 99);
  polygon_star_inset->setValue(0);
  polygon_star_inset->setSuffix(percent_suffix());
  bind_tooltip(polygon_star_inset, QT_TR_NOOP("0 makes a plain polygon; higher values pull in star points"));
  configure_toolbar_spinbox(polygon_star_inset, 56);
  add_option_widget(polygon_star_inset, {CanvasTool::Polygon});
  vector_vector_mode_option_widgets_.push_back(polygon_star_inset);
  connect(polygon_star_inset, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_polygon_star_inset(value);
      schedule_save_tool_settings();
    }
  });

  vector_vector_mode_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("Shape:"), {CanvasTool::CustomShape}));
  custom_shape_combo_ = new QComboBox(toolbar);
  custom_shape_combo_->setObjectName(QStringLiteral("customShapeCombo"));
  custom_shape_combo_->setIconSize(QSize(24, 24));
  custom_shape_combo_->setFixedWidth(150);
  register_retranslation([this] { refresh_custom_shape_combo(); });
  add_option_widget(custom_shape_combo_, {CanvasTool::CustomShape});
  vector_vector_mode_option_widgets_.push_back(custom_shape_combo_);
  connect(custom_shape_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
    apply_custom_shape_selection();
    schedule_save_tool_settings();
  });

  // Arrowheads are two glyph toggles like the Type bar's align buttons. The old
  // "Arrow start" / "Arrow end" checkboxes made the Line row the widest one that
  // cannot shrink, and it wrapped Appearance... onto a second line (GitHub issue 174).
  const auto make_line_arrow_button = [this, toolbar, add_option_widget](const char* object_name, bool at_end,
                                                                         const char* tooltip, bool& setting) {
    auto* button = new QPushButton(toolbar);
    button->setObjectName(QLatin1String(object_name));
    button->setIcon(line_arrowhead_icon(at_end));
    button->setCheckable(true);
    button->setChecked(setting);
    bind_tooltip(button, tooltip);
    button->setFixedSize(30, 26);
    button->setFocusPolicy(Qt::NoFocus);
    add_option_widget(button, {CanvasTool::Line});
    vector_vector_mode_option_widgets_.push_back(button);
    connect(button, &QPushButton::toggled, this, [this, &setting](bool checked) {
      setting = checked;
      schedule_save_tool_settings();
    });
  };
  make_line_arrow_button("lineArrowStartButton", false, QT_TR_NOOP("Add an arrowhead at the line start"),
                         current_line_arrow_start_);
  make_line_arrow_button("lineArrowEndButton", true, QT_TR_NOOP("Add an arrowhead at the line end"),
                         current_line_arrow_end_);

  auto* fill_shapes = new CheckGlyphBox(tr("Fill"), toolbar);
  fill_shapes->setObjectName(QStringLiteral("shapeFillCheck"));
  add_option_widget(fill_shapes, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_pixel_only_option_widgets_.push_back(fill_shapes);
  connect(fill_shapes, &QCheckBox::toggled, this, [this](bool checked) {
    current_fill_shapes_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_fill_shapes(checked);
    }
  });

  add_option_separator({CanvasTool::Rectangle});
  add_option_label(QT_TR_NOOP("Radius:"), {CanvasTool::Rectangle});
  auto* shape_corner_radius = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  shape_corner_radius->setObjectName(QStringLiteral("shapeCornerRadiusSpin"));
  shape_corner_radius->set_context_provider(document_unit_context_provider(true));
  shape_corner_radius->setRange(0, 512);
  shape_corner_radius->setValue(canvas_defaults->shape_corner_radius());
  bind_tooltip(shape_corner_radius, QT_TR_NOOP("Rounded-corner radius for the rectangle tool (0 = sharp corners)"));
  configure_toolbar_spinbox(shape_corner_radius, 64);
  add_option_widget(shape_corner_radius, {CanvasTool::Rectangle});
  connect(shape_corner_radius, &QSpinBox::valueChanged, this, [this](int value) {
    current_shape_corner_radius_ = value;
    apply_selected_shape_corner_radius(value);
    if (canvas_ != nullptr) {
      canvas_->set_shape_corner_radius(value);
      schedule_save_tool_settings();
    }
  });

  install_appearance_edit_intent(shape_corner_radius, [this, shape_corner_radius] {
    current_shape_corner_radius_ = shape_corner_radius->value();
    apply_selected_shape_corner_radius(current_shape_corner_radius_);
    if (canvas_ != nullptr) canvas_->set_shape_corner_radius(current_shape_corner_radius_);
    schedule_save_tool_settings();
  });
  connect(shape_corner_radius, &QSpinBox::editingFinished, this,
          [this] { finish_pending_shape_appearance_edit(); });

  // Style / Width / Height for the shape draw tools, mirroring the marquee's
  // Normal / Fixed Ratio / Fixed Size options (session-only, like the marquee's).
  // Pixels-mode only: the vector modes show the active shape's W / H instead,
  // and a tap opens the Create dialog (docs/vector-tools.md).
  vector_pixel_only_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("Style:"), {CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* shape_style_combo = new QComboBox(toolbar);
  shape_style_combo->setObjectName(QStringLiteral("shapeStyleCombo"));
  shape_style_combo->addItems({tr("Normal"), tr("Fixed Ratio"), tr("Fixed Size")});
  shape_style_combo->setCurrentText(tr("Normal"));
  shape_style_combo->setFixedWidth(92);
  QPointer<QComboBox> shape_style_combo_pointer(shape_style_combo);
  register_retranslation([shape_style_combo_pointer] {
    if (shape_style_combo_pointer == nullptr || shape_style_combo_pointer->count() < 3) {
      return;
    }
    QSignalBlocker blocker(shape_style_combo_pointer);
    shape_style_combo_pointer->setItemText(0, QObject::tr("Normal"));
    shape_style_combo_pointer->setItemText(1, QObject::tr("Fixed Ratio"));
    shape_style_combo_pointer->setItemText(2, QObject::tr("Fixed Size"));
  });
  add_option_widget(shape_style_combo, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_pixel_only_option_widgets_.push_back(shape_style_combo);
  vector_pixel_only_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("Width:"), {CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* shape_fixed_width = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  shape_fixed_width->set_context_provider(document_axis_context(true));
  shape_fixed_width->setObjectName(QStringLiteral("shapeFixedWidthSpin"));
  shape_fixed_width->setRange(1, 30000);
  shape_fixed_width->setValue(has_active_document() ? document().width() : 1024);
  configure_toolbar_spinbox(shape_fixed_width, 78);
  add_option_widget(shape_fixed_width, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_pixel_only_option_widgets_.push_back(shape_fixed_width);
  vector_pixel_only_option_widgets_.push_back(
      add_option_label(QT_TR_NOOP("Height:"), {CanvasTool::Rectangle, CanvasTool::Ellipse}));
  auto* shape_fixed_height = new UnitIntSpinBox(SpinUnit::Pixels, toolbar);
  shape_fixed_height->set_context_provider(document_axis_context(false));
  shape_fixed_height->setObjectName(QStringLiteral("shapeFixedHeightSpin"));
  shape_fixed_height->setRange(1, 30000);
  shape_fixed_height->setValue(has_active_document() ? document().height() : 768);
  configure_toolbar_spinbox(shape_fixed_height, 78);
  add_option_widget(shape_fixed_height, {CanvasTool::Rectangle, CanvasTool::Ellipse});
  vector_pixel_only_option_widgets_.push_back(shape_fixed_height);
  const auto apply_shape_style_settings = [this, shape_style_combo, shape_fixed_width, shape_fixed_height] {
    // Normal ignores the size fields, so they grey out (Fixed Ratio reads
    // them as the ratio, Fixed Size as the size).
    shape_fixed_width->setEnabled(shape_style_combo->currentIndex() != 0);
    shape_fixed_height->setEnabled(shape_style_combo->currentIndex() != 0);
    switch (shape_style_combo->currentIndex()) {
      case 1:
        current_shape_style_ = CanvasWidget::MarqueeStyle::FixedRatio;
        break;
      case 2:
        current_shape_style_ = CanvasWidget::MarqueeStyle::FixedSize;
        break;
      default:
        current_shape_style_ = CanvasWidget::MarqueeStyle::Normal;
        break;
    }
    current_shape_width_ = shape_fixed_width->value();
    current_shape_height_ = shape_fixed_height->value();
    if (canvas_ != nullptr) {
      canvas_->set_shape_style(current_shape_style_);
      canvas_->set_shape_fixed_size(current_shape_width_, current_shape_height_);
    }
  };
  connect(shape_style_combo, &QComboBox::currentIndexChanged, this, [apply_shape_style_settings](int) {
    apply_shape_style_settings();
  });
  connect(shape_fixed_width, &QSpinBox::valueChanged, this, [apply_shape_style_settings](int) {
    apply_shape_style_settings();
  });
  connect(shape_fixed_height, &QSpinBox::valueChanged, this, [apply_shape_style_settings](int) {
    apply_shape_style_settings();
  });
  apply_shape_style_settings();

  // Extra settings follow every shape-specific control at the end of the row.
  vector_appearance_button_ = new QPushButton(tr("Appearance..."), toolbar);
  vector_appearance_button_->setObjectName(QStringLiteral("vectorAppearanceButton"));
  bind_widget_text(vector_appearance_button_, QT_TR_NOOP("Appearance..."));
  bind_tooltip(vector_appearance_button_, QT_TR_NOOP("Edit the active shape layer's fill, stroke, opacity, and edge"));
  vector_appearance_button_->setProperty("optionsBarButton", true);
  vector_appearance_button_->setMinimumHeight(24);
  vector_appearance_button_->setMaximumHeight(26);
  add_option_separator({CanvasTool::Line, CanvasTool::Rectangle, CanvasTool::Ellipse, CanvasTool::Pen,
                        CanvasTool::Polygon, CanvasTool::CustomShape});
  add_option_widget(vector_appearance_button_, vector_appearance_tools);
  vector_shape_mode_option_widgets_.push_back(vector_appearance_button_);
  connect(vector_appearance_button_, &QPushButton::clicked, this, [this] { edit_active_shape_appearance(); });

  // Fill tool / Fill hotkey settings (independent of the brush; default 100% opacity, 0 softness).
  add_option_label(QT_TR_NOOP("Opacity:"), {CanvasTool::Fill});
  auto* fill_opacity = new QSpinBox(toolbar);
  fill_opacity->setObjectName(QStringLiteral("fillOpacitySpin"));
  fill_opacity->setRange(0, 100);  // 0..100 like the Brush bar's Opacity (issue 253); 0% fills nothing
  fill_opacity->setValue(canvas_defaults->fill_opacity());
  fill_opacity->setSuffix(percent_suffix());
  configure_toolbar_spinbox(fill_opacity, 52);
  add_option_widget(fill_opacity, {CanvasTool::Fill});
  auto* fill_opacity_slider = new QSlider(Qt::Horizontal, toolbar);
  fill_opacity_slider->setObjectName(QStringLiteral("fillOpacitySlider"));
  fill_opacity_slider->setProperty(kOptionsBarCollapsibleProperty, true);
  fill_opacity_slider->setRange(0, 100);
  fill_opacity_slider->setValue(canvas_defaults->fill_opacity());
  fill_opacity_slider->setFixedWidth(120);
  bind_tooltip(fill_opacity_slider, QT_TR_NOOP("Fill opacity for the Fill tool and Fill shortcut"));
  add_option_widget(fill_opacity_slider, {CanvasTool::Fill});
  add_option_label(QT_TR_NOOP("Soft:"), {CanvasTool::Fill});
  auto* fill_softness = new QSpinBox(toolbar);
  fill_softness->setObjectName(QStringLiteral("fillSoftnessSpin"));
  fill_softness->setRange(0, 100);
  fill_softness->setValue(canvas_defaults->fill_softness());
  fill_softness->setSuffix(percent_suffix());
  configure_toolbar_spinbox(fill_softness, 52);
  add_option_widget(fill_softness, {CanvasTool::Fill});
  auto* fill_softness_slider = new QSlider(Qt::Horizontal, toolbar);
  fill_softness_slider->setObjectName(QStringLiteral("fillSoftnessSlider"));
  fill_softness_slider->setProperty(kOptionsBarCollapsibleProperty, true);
  fill_softness_slider->setRange(0, 100);
  fill_softness_slider->setValue(canvas_defaults->fill_softness());
  fill_softness_slider->setFixedWidth(110);
  bind_tooltip(fill_softness_slider, QT_TR_NOOP("Soft edge feather for the Fill tool and Fill shortcut"));
  add_option_widget(fill_softness_slider, {CanvasTool::Fill});
  connect(fill_opacity, &QSpinBox::valueChanged, fill_opacity_slider, &QSlider::setValue);
  connect(fill_opacity_slider, &QSlider::valueChanged, fill_opacity, &QSpinBox::setValue);
  connect(fill_opacity, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_opacity(value);
      schedule_save_tool_settings();
    }
  });
  connect(fill_softness, &QSpinBox::valueChanged, fill_softness_slider, &QSlider::setValue);
  connect(fill_softness_slider, &QSlider::valueChanged, fill_softness, &QSpinBox::setValue);
  connect(fill_softness, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_softness(value);
      schedule_save_tool_settings();
    }
  });
  // Tolerance and Contiguous belong to the Fill tool's flood only (the Fill command fills the
  // whole selection). Same metric and range as the Magic Wand's Tol.
  add_option_separator({CanvasTool::Fill});
  add_option_label(QT_TR_NOOP("Tolerance:"), {CanvasTool::Fill});
  auto* fill_tolerance = new QSpinBox(toolbar);
  fill_tolerance->setObjectName(QStringLiteral("fillToleranceSpin"));
  fill_tolerance->setRange(0, 255);
  fill_tolerance->setValue(canvas_defaults->fill_tolerance());
  configure_toolbar_spinbox(fill_tolerance, 46);
  bind_tooltip(fill_tolerance,
               QT_TR_NOOP("How far a pixel's color may differ from the clicked color and still be filled"));
  add_option_widget(fill_tolerance, {CanvasTool::Fill});
  connect(fill_tolerance, &QSpinBox::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_tolerance(value);
      schedule_save_tool_settings();
    }
  });
  fill_contiguous_check_ = new CheckGlyphBox(tr("Contiguous"), toolbar);
  fill_contiguous_check_->setObjectName(QStringLiteral("fillContiguousCheck"));
  fill_contiguous_check_->setChecked(canvas_defaults->fill_contiguous());
  bind_tooltip(fill_contiguous_check_, QT_TR_NOOP("Limit the fill to pixels connected to the click"));
  add_option_separator({CanvasTool::Fill});
  add_option_widget(fill_contiguous_check_, {CanvasTool::Fill});
  connect(fill_contiguous_check_, &QCheckBox::toggled, this, [this](bool checked) {
    if (canvas_ != nullptr) {
      canvas_->set_fill_contiguous(checked);
      save_tool_settings();
    }
  });

  // Zoom tool options (docs/view-navigation.md), Photoshop's row: the Zoom In /
  // Zoom Out click direction (tools/zoomToolZoomsOut; Alt inverts it), Scrubby
  // Zoom, then the 100% / Fit Screen / Fill Screen view presets. Every widget
  // carries optionsBarAllowedWhileLocked: the Zoom tool works while a preview
  // dialog locks editing, so its row stays enabled then (refresh_options_bar).
  const auto allow_while_locked = [](QWidget* widget) {
    widget->setProperty("optionsBarAllowedWhileLocked", true);
  };
  zoom_in_mode_action_ =
      add_option_action(simple_icon(QStringLiteral("zoomIn")), QT_TR_NOOP("Zoom In"), {CanvasTool::Zoom});
  zoom_in_mode_action_->setObjectName(QStringLiteral("zoomInModeAction"));
  allow_while_locked(option_actions_.back().first);
  zoom_out_mode_action_ =
      add_option_action(simple_icon(QStringLiteral("zoomOut")), QT_TR_NOOP("Zoom Out"), {CanvasTool::Zoom});
  zoom_out_mode_action_->setObjectName(QStringLiteral("zoomOutModeAction"));
  allow_while_locked(option_actions_.back().first);
  auto* zoom_mode_group = new QActionGroup(this);
  zoom_mode_group->setExclusive(true);
  for (auto* action : {zoom_in_mode_action_, zoom_out_mode_action_}) {
    action->setCheckable(true);
    zoom_mode_group->addAction(action);
  }
  zoom_in_mode_action_->setChecked(!canvas_defaults->zoom_tool_zooms_out());
  zoom_out_mode_action_->setChecked(canvas_defaults->zoom_tool_zooms_out());
  connect(zoom_mode_group, &QActionGroup::triggered, this, [this](QAction* action) {
    const bool zooms_out = action == zoom_out_mode_action_;
    current_zoom_tool_zooms_out_ = zooms_out;
    if (canvas_ != nullptr) {
      canvas_->set_zoom_tool_zooms_out(zooms_out);
      save_tool_settings();
    }
  });
  add_option_separator({CanvasTool::Zoom});
  // Scrubby Zoom (GitHub issue 51, Photoshop's gesture): a persisted view
  // preference (tools/zoomScrubby, default off) mirrored into every session
  // canvas.
  zoom_scrubby_check_ = new CheckGlyphBox(tr("Scrubby Zoom"), toolbar);
  allow_while_locked(zoom_scrubby_check_);
  zoom_scrubby_check_->setObjectName(QStringLiteral("zoomScrubbyCheck"));
  zoom_scrubby_check_->setChecked(canvas_defaults->zoom_scrubby());
  bind_tooltip(zoom_scrubby_check_,
               QT_TR_NOOP("Drag right to zoom in and left to zoom out around the point you pressed. "
                          "Off: drag a rectangle to zoom to it"));
  add_option_widget(zoom_scrubby_check_, {CanvasTool::Zoom});
  connect(zoom_scrubby_check_, &QCheckBox::toggled, this, [this](bool checked) {
    current_zoom_scrubby_ = checked;
    if (canvas_ != nullptr) {
      canvas_->set_zoom_scrubby(checked);
      save_tool_settings();
    }
  });
  add_option_separator({CanvasTool::Zoom});
  // The view presets call the canvas directly (the View menu actions do the
  // same), so the row does not depend on the menu build order. The Hand tool's
  // row shows the same three buttons (GitHub issue 303); Photoshop's Scroll All
  // Windows is left out because Patchy has no multi-window scroll sync.
  const auto add_zoom_view_button = [this, toolbar, add_option_widget, allow_while_locked](
                                        const char* source, const QString& object_name, const char* tooltip,
                                        std::function<void(CanvasWidget&)> apply) {
    auto* button = new QPushButton(tr(source), toolbar);
    button->setObjectName(object_name);
    bind_tooltip(button, tooltip);
    allow_while_locked(button);
    add_option_widget(button, {CanvasTool::Zoom, CanvasTool::Pan});
    connect(button, &QPushButton::clicked, this, [this, apply] {
      if (canvas_ != nullptr) {
        apply(*canvas_);
      }
    });
    return button;
  };
  zoom_actual_pixels_button_ =
      add_zoom_view_button(QT_TR_NOOP("100%"), QStringLiteral("zoomActualPixelsButton"),
                           QT_TR_NOOP("Show the image at actual pixels (View > Actual Pixels)"),
                           [](CanvasWidget& canvas) { canvas.set_view_zoom_centered(1.0); });
  zoom_fit_screen_button_ =
      add_zoom_view_button(QT_TR_NOOP("Fit Screen"), QStringLiteral("zoomFitScreenButton"),
                           QT_TR_NOOP("Fit the whole image in the window (View > Fit on Screen)"),
                           [](CanvasWidget& canvas) { canvas.fit_to_view(); });
  zoom_fill_screen_button_ =
      add_zoom_view_button(QT_TR_NOOP("Fill Screen"), QStringLiteral("zoomFillScreenButton"),
                           QT_TR_NOOP("Zoom until the image fills the window (View > Fill Screen)"),
                           [](CanvasWidget& canvas) { canvas.fill_to_view(); });

  add_option_label(QT_TR_NOOP("Font:"), {CanvasTool::Text});
  text_font_combo_ = new FontPickerCombo(toolbar);
  text_font_combo_->setObjectName(QStringLiteral("textFontCombo"));
  text_font_combo_->setCurrentFont(font());
  // GitHub issue 91: narrower closed controls keep the Text row on one line at
  // 1366 px; the family popup sizes itself, and the style list opens wider.
  text_font_combo_->setFixedWidth(160);
  add_option_widget(text_font_combo_, {CanvasTool::Text});
  // Photoshop's style picker: the family's OWN face list, and the only face control in the bar
  // (no B/I buttons, like Photoshop). A family with no italic simply does not offer one, and
  // Ctrl+B / Ctrl+I toggle the real face during a session, falling back to faux when the family
  // lacks that axis.
  text_style_combo_ = new QComboBox(toolbar);
  text_style_combo_->setObjectName(QStringLiteral("textStyleCombo"));
  bind_tooltip(text_style_combo_, QT_TR_NOOP("Font style"));
  text_style_combo_->setFixedWidth(104);
  text_style_combo_->view()->setMinimumWidth(170);
  refresh_text_style_combo(text_font_combo_->currentFont().family(), QString());
  add_option_widget(text_style_combo_, {CanvasTool::Text});
  // Font and style | Size and Smoothing (GitHub issue 343).
  add_option_separator({CanvasTool::Text});
  add_option_label(QT_TR_NOOP("Size:"), {CanvasTool::Text});
  text_size_spin_ = new UnitSpinBox(SpinUnit::Points, toolbar);
  text_size_spin_->setObjectName(QStringLiteral("textSizeSpin"));
  text_size_spin_->set_context_provider([this] {
    return UnitConversionContext{has_active_document() ? text_size_ppi(document()) : 300.0, 0.0};
  });
  // Photoshop's type size shows at most two decimals and drops trailing zeros ("12 pt",
  // "14.45 pt"). Text sizes are whole pixels underneath (text_points_to_pixels), so two
  // decimals round-trip every pixel size below 7200 ppi; a third was false precision.
  text_size_spin_->setDecimals(2);
  text_size_spin_->set_trim_trailing_zeros(true);
  text_size_spin_->setRange(0.01, 10000.0);
  // Applies on Enter, focus loss or a step, like the Character panel fields: with no session
  // every value change commits a re-render of each selected text layer as an undo step, so
  // typing "120" must not land three of them.
  text_size_spin_->setKeyboardTracking(false);
  // Typing accepts up to 10000 pt, but the popup slider stays usable at 0..200.
  text_size_spin_->setProperty(kToolbarSpinboxSliderMaxProperty, 200.0);
  text_size_spin_->setSingleStep(0.25);
  // 48 px at the default document's 72 ppi = 48 pt (startup builds the bar with
  // no document open).
  text_size_spin_->setValue(has_active_document() ? text_pixels_to_points(48, document()) : 48.0);
  text_size_auto_points_ = text_size_spin_->value();
  // Sized for a realistic widest value; the trimmed max ("10000 pt") is narrower.
  text_size_spin_->setProperty(kToolbarSpinboxWidthSampleProperty, 888.88);
  configure_toolbar_spinbox(text_size_spin_, 60);
  add_option_widget(text_size_spin_, {CanvasTool::Text});
  add_option_label(QT_TR_NOOP("Smoothing:"), {CanvasTool::Text});
  text_smoothing_combo_ = new QComboBox(toolbar);
  text_smoothing_combo_->setObjectName(QStringLiteral("textSmoothingCombo"));
  bind_tooltip(text_smoothing_combo_, QT_TR_NOOP("Text smoothing"));
  text_smoothing_combo_->addItem(tr("None"), kTextAntiAliasNone);
  text_smoothing_combo_->addItem(tr("Sharp"), kTextAntiAliasSharp);
  text_smoothing_combo_->addItem(tr("Crisp"), kTextAntiAliasCrisp);
  text_smoothing_combo_->addItem(tr("Strong"), kTextAntiAliasStrong);
  text_smoothing_combo_->addItem(tr("Smooth"), kTextAntiAliasSmooth);
  text_smoothing_combo_->addItem(tr("Windows LCD"), kTextAntiAliasWindowsLcd);
  text_smoothing_combo_->addItem(tr("Windows"), kTextAntiAliasWindows);
  register_retranslation([this] {
    const QSignalBlocker blocker(text_smoothing_combo_);
    const char* sources[] = {QT_TR_NOOP("None"), QT_TR_NOOP("Sharp"), QT_TR_NOOP("Crisp"),
                             QT_TR_NOOP("Strong"), QT_TR_NOOP("Smooth"), QT_TR_NOOP("Windows LCD"), QT_TR_NOOP("Windows")};
    for (int index = 0; index < 7; ++index) text_smoothing_combo_->setItemText(index, tr(sources[index]));
  });
  text_smoothing_combo_->setFixedWidth(104);
  set_text_smoothing_combo_value(
      text_smoothing_combo_,
      app_settings().value(QStringLiteral("tools/textSmoothing"), kDefaultTextAntiAlias).toInt());
  add_option_widget(text_smoothing_combo_, {CanvasTool::Text});
  // No "Color:" label (Photoshop has none either): the swatch and its tooltip name it, and
  // the Type row must fit one line at 1366 px (GitHub issue 91). It is the shape bar's
  // Fill/Stroke swatch button, a framed chip of the color (GitHub issue 343).
  text_color_button_ = new QToolButton(toolbar);
  text_color_button_->setObjectName(QStringLiteral("textColorButton"));
  bind_tooltip(text_color_button_, QT_TR_NOOP("Text color"));
  // configure_swatch_button sets Qt::NoFocus like the session buttons below: keyboard focus
  // stays in the editor, so typing after a color pick replaces the selected "Type"
  // placeholder (GitHub issue 175). The alignment buttons follow the same rule.
  configure_swatch_button(text_color_button_);
  add_option_separator({CanvasTool::Text});
  add_option_widget(text_color_button_, {CanvasTool::Text});
  // Ragged-lines glyphs instead of L / C / R letters, which needed an "Align:" label to
  // read; sync_text_orientation_controls_from_editor swaps in the column glyphs for
  // vertical type.
  auto* text_alignment_group = new QButtonGroup(toolbar);
  text_alignment_group->setExclusive(true);
  text_align_left_button_ = new QPushButton(toolbar);
  text_align_left_button_->setIcon(text_alignment_icon(Qt::AlignLeft, false));
  text_align_left_button_->setObjectName(QStringLiteral("textAlignLeftButton"));
  text_align_left_button_->setCheckable(true);
  text_align_left_button_->setChecked(true);
  bind_tooltip(text_align_left_button_, QT_TR_NOOP("Align Left"));
  text_align_left_button_->setFixedSize(30, 26);
  text_align_left_button_->setFocusPolicy(Qt::NoFocus);
  text_alignment_group->addButton(text_align_left_button_);
  add_option_separator({CanvasTool::Text});
  add_option_widget(text_align_left_button_, {CanvasTool::Text});
  text_align_center_button_ = new QPushButton(toolbar);
  text_align_center_button_->setIcon(text_alignment_icon(Qt::AlignHCenter, false));
  text_align_center_button_->setObjectName(QStringLiteral("textAlignCenterButton"));
  text_align_center_button_->setCheckable(true);
  bind_tooltip(text_align_center_button_, QT_TR_NOOP("Align Center"));
  text_align_center_button_->setFixedSize(30, 26);
  text_align_center_button_->setFocusPolicy(Qt::NoFocus);
  text_alignment_group->addButton(text_align_center_button_);
  add_option_widget(text_align_center_button_, {CanvasTool::Text});
  text_align_right_button_ = new QPushButton(toolbar);
  text_align_right_button_->setIcon(text_alignment_icon(Qt::AlignRight, false));
  text_align_right_button_->setObjectName(QStringLiteral("textAlignRightButton"));
  text_align_right_button_->setCheckable(true);
  bind_tooltip(text_align_right_button_, QT_TR_NOOP("Align Right"));
  text_align_right_button_->setFixedSize(30, 26);
  text_align_right_button_->setFocusPolicy(Qt::NoFocus);
  text_alignment_group->addButton(text_align_right_button_);
  add_option_widget(text_align_right_button_, {CanvasTool::Text});
  // Photoshop's "Toggle text orientation": one Type tool with a vertical switch instead of a
  // second tool. Qt::NoFocus like the session buttons, so toggling never auto-commits.
  text_orientation_button_ = new QPushButton(tr("Vertical"), toolbar);
  bind_widget_text(text_orientation_button_, QT_TR_NOOP("Vertical"));
  text_orientation_button_->setObjectName(QStringLiteral("textOrientationButton"));
  text_orientation_button_->setCheckable(true);
  text_orientation_button_->setChecked(false);
  bind_tooltip(text_orientation_button_, QT_TR_NOOP("Vertical text: columns read top to bottom, right to left"));
  text_orientation_button_->setFocusPolicy(Qt::NoFocus);
  add_option_separator({CanvasTool::Text});
  add_option_widget(text_orientation_button_, {CanvasTool::Text});
  text_warp_button_ = new QPushButton(tr("Warp..."), toolbar);
  bind_widget_text(text_warp_button_, QT_TR_NOOP("Warp..."));
  text_warp_button_->setObjectName(QStringLiteral("textWarpButton"));
  bind_tooltip(text_warp_button_, QT_TR_NOOP("Warp Text (Photoshop-style styles: arc, flag, fish, ...)"));
  // Qt::NoFocus so the press does not auto-commit the session before request_warp_text_dialog
  // can note its selection and reopen it afterwards (GitHub issue 175).
  text_warp_button_->setFocusPolicy(Qt::NoFocus);
  add_option_widget(text_warp_button_, {CanvasTool::Text});
  // Character panel: works on the LIVE editor session, so Qt::NoFocus is load-bearing here
  // exactly like the session apply/cancel buttons (a focus-taking button would fire the
  // editor's focus-loss auto-commit on mouse press).
  text_character_button_ = new QPushButton(tr("Character..."), toolbar);
  bind_widget_text(text_character_button_, QT_TR_NOOP("Character..."));
  text_character_button_->setObjectName(QStringLiteral("textCharacterButton"));
  bind_tooltip(text_character_button_, QT_TR_NOOP("Character panel (leading, tracking, glyph scales)"));
  text_character_button_->setFocusPolicy(Qt::NoFocus);
  add_option_separator({CanvasTool::Text});
  add_option_widget(text_character_button_, {CanvasTool::Text});
  connect(text_character_button_, &QPushButton::clicked, this, [this] { open_text_character_dialog(); });
  // Paragraph panel: same live-session rules as the Character panel (Qt::NoFocus).
  text_paragraph_button_ = new QPushButton(tr("Paragraph..."), toolbar);
  bind_widget_text(text_paragraph_button_, QT_TR_NOOP("Paragraph..."));
  text_paragraph_button_->setObjectName(QStringLiteral("textParagraphButton"));
  bind_tooltip(text_paragraph_button_, QT_TR_NOOP("Paragraph panel (alignment, indents, spacing)"));
  text_paragraph_button_->setFocusPolicy(Qt::NoFocus);
  add_option_widget(text_paragraph_button_, {CanvasTool::Text});
  connect(text_paragraph_button_, &QPushButton::clicked, this, [this] { open_text_paragraph_dialog(); });
  connect(text_font_combo_, &QFontComboBox::currentFontChanged, this, [this](const QFont& chosen) {
    // Repopulate before applying: apply_text_family_to_active_editor renders with the style the
    // combo is showing, and the outgoing family's style list may not contain it.
    refresh_text_style_combo(chosen.family(), current_text_style_name());
    apply_text_family_to_active_editor();
  });
  connect(text_style_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { apply_text_style_to_active_editor(); });
  connect(text_size_spin_, &QDoubleSpinBox::valueChanged, this,
          [this](double) {
    apply_text_size_to_active_editor();
    refresh_document_info();
  });
  connect(text_smoothing_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            apply_text_smoothing_to_active_editor();
            save_tool_settings();
            refresh_document_info();
          });
  connect(text_color_button_, &QToolButton::clicked, this, [this] { choose_text_color(); });
  // The controls that need the keyboard while in use hand it back to a live session once the
  // pick lands: a popup choice, or Enter in the size field. editingFinished on focus loss finds
  // the spin no longer focused, except when its slider popup opens: Qt sends that FocusOut while
  // the field still holds focus, and an active popup tells the two apart.
  for (QComboBox* combo : {static_cast<QComboBox*>(text_font_combo_), text_style_combo_, text_smoothing_combo_}) {
    connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this](int) { return_keyboard_to_text_editor(); });
  }
  connect(text_size_spin_, &QAbstractSpinBox::editingFinished, this, [this] {
    if (text_size_spin_->hasFocus() && QApplication::activePopupWidget() == nullptr) {
      return_keyboard_to_text_editor();
    }
  });
  connect(text_align_left_button_, &QPushButton::clicked, this,
          [this] { apply_text_alignment_to_active_editor(Qt::AlignLeft); });
  connect(text_align_center_button_, &QPushButton::clicked, this,
          [this] { apply_text_alignment_to_active_editor(Qt::AlignHCenter); });
  connect(text_align_right_button_, &QPushButton::clicked, this,
          [this] { apply_text_alignment_to_active_editor(Qt::AlignRight); });
  connect(text_warp_button_, &QPushButton::clicked, this, [this] { request_warp_text_dialog(); });
  connect(text_orientation_button_, &QPushButton::toggled, this,
          [this](bool checked) { apply_text_orientation(checked); });
  // Session apply/cancel, shown only while an inline text editor is open (the
  // text controls above stay visible too -- they apply live to the editor, so
  // unlike a transform session the bar keeps them).  Qt::NoFocus is load-bearing:
  // a focus-taking button would fire the editor's focus-loss auto-commit on
  // mouse press, committing the text before a Cancel click could cancel it.
  text_apply_button_ = new QPushButton(toolbar);
  text_apply_button_->setObjectName(QStringLiteral("textApplyButton"));
  text_apply_button_->setIcon(simple_icon(QStringLiteral("ok")));
  bind_tooltip(text_apply_button_, QT_TR_NOOP("Apply text edit"));
  text_apply_button_->setFixedWidth(30);
  text_apply_button_->setIconSize(QSize(20, 20));
  text_apply_button_->setProperty("optionsSessionButton", true);
  text_apply_button_->setFocusPolicy(Qt::NoFocus);
  options_flow->addWidget(text_apply_button_);
  text_cancel_button_ = new QPushButton(toolbar);
  text_cancel_button_->setObjectName(QStringLiteral("textCancelButton"));
  text_cancel_button_->setIcon(simple_icon(QStringLiteral("cancel")));
  bind_tooltip(text_cancel_button_, QT_TR_NOOP("Cancel text edit"));
  text_cancel_button_->setFixedWidth(30);
  text_cancel_button_->setIconSize(QSize(20, 20));
  text_cancel_button_->setProperty("optionsSessionButton", true);
  text_cancel_button_->setFocusPolicy(Qt::NoFocus);
  options_flow->addWidget(text_cancel_button_);
  connect(text_apply_button_, &QPushButton::clicked, this, [this] { commit_active_text_editor(); });
  connect(text_cancel_button_, &QPushButton::clicked, this, [this] { cancel_active_text_editor(); });

  // Every "Label:" before a numeric field is that field's scrub handle
  // (GitHub issue 46; install_scrub_labels_in pairs them by layout order, nested
  // groups such as Feather included, so a new label+field pair opts in by itself).
  install_scrub_labels_in(options_content);

  // Export the cross-phase locals bind_action_translations() still needs.
  ctx.options_toolbar = toolbar;
  ctx.brush_smaller_action = brush_smaller_action;
  ctx.brush_larger_action = brush_larger_action;
  ctx.brush_much_smaller_action = brush_much_smaller_action;
  ctx.brush_much_larger_action = brush_much_larger_action;
  ctx.fill_shapes = fill_shapes;
}

void MainWindow::sync_transform_type_buttons() {
  const bool free_transform = canvas_ != nullptr && canvas_->free_transform_active();
  const auto mode = canvas_ != nullptr ? canvas_->free_transform_distort_mode() : TransformDistortMode::None;
  for (auto* button : transform_type_buttons_) {
    if (button != nullptr) {
      button->setChecked(free_transform && button->property(kTransformTypeModeProperty).toInt() == static_cast<int>(mode));
    }
  }
}

}  // namespace patchy::ui
