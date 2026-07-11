#include "ui/layer_style_dialog.hpp"

#include "ui/blend_if_range_editor.hpp"
#include "ui/blend_mode_ui.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/gradient_stops_editor.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <type_traits>
#include <utility>

namespace patchy::ui {

namespace {

// Forwards a left double-click on a passive color patch to its page's Choose Color
// button, so the swatch itself opens the picker (Photoshop behavior). Parented to the
// watched widget; the button is tracked by QPointer in case it dies first.
class DoubleClickClicksButton : public QObject {
 public:
  DoubleClickClicksButton(QAbstractButton* button, QWidget* watched)
      : QObject(watched), button_(button) {
    watched->installEventFilter(this);
  }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonDblClick &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && button_ != nullptr) {
      button_->click();
      return true;
    }
    return QObject::eventFilter(watched, event);
  }

 private:
  QPointer<QAbstractButton> button_;
};

LayerStyleGradient default_layer_style_gradient() {
  LayerStyleGradient gradient;
  gradient.angle_degrees = 90.0F;
  gradient.scale = 1.0F;
  gradient.color_stops.push_back(GradientColorStop{0.0F, RgbColor{255, 255, 255}});
  gradient.color_stops.push_back(GradientColorStop{1.0F, RgbColor{32, 32, 32}});
  gradient.alpha_stops.push_back(GradientAlphaStop{0.0F, 1.0F});
  gradient.alpha_stops.push_back(GradientAlphaStop{1.0F, 1.0F});
  return gradient;
}

LayerDropShadow default_drop_shadow() {
  LayerDropShadow shadow;
  shadow.enabled = true;
  shadow.color = RgbColor{0, 0, 0};
  shadow.opacity = 0.75F;
  shadow.angle_degrees = 120.0F;
  shadow.distance = 5.0F;
  shadow.spread = 0.0F;
  shadow.size = 5.0F;
  return shadow;
}

LayerInnerShadow default_inner_shadow() {
  LayerInnerShadow shadow;
  shadow.enabled = true;
  shadow.color = RgbColor{0, 0, 0};
  shadow.opacity = 0.75F;
  shadow.angle_degrees = 120.0F;
  shadow.distance = 5.0F;
  shadow.choke = 0.0F;
  shadow.size = 5.0F;
  return shadow;
}

LayerOuterGlow default_outer_glow() {
  LayerOuterGlow glow;
  glow.enabled = true;
  glow.blend_mode = BlendMode::Normal;
  glow.color = RgbColor{255, 255, 190};
  glow.opacity = 0.75F;
  glow.spread = 0.0F;
  glow.size = 5.0F;
  return glow;
}

LayerInnerGlow default_inner_glow() {
  LayerInnerGlow glow;
  glow.enabled = true;
  glow.blend_mode = BlendMode::Screen;
  glow.color = RgbColor{255, 255, 190};
  glow.opacity = 0.75F;
  glow.choke = 0.0F;
  glow.size = 5.0F;
  glow.source = LayerInnerGlowSource::Edge;
  return glow;
}

LayerColorOverlay default_color_overlay() {
  LayerColorOverlay overlay;
  overlay.enabled = true;
  overlay.blend_mode = BlendMode::Normal;
  overlay.color = RgbColor{255, 0, 0};
  overlay.opacity = 1.0F;
  return overlay;
}

LayerGradientFill default_gradient_fill() {
  LayerGradientFill fill;
  fill.enabled = true;
  fill.blend_mode = BlendMode::Normal;
  fill.opacity = 1.0F;
  fill.gradient = default_layer_style_gradient();
  return fill;
}

LayerStroke default_stroke() {
  LayerStroke stroke;
  stroke.enabled = true;
  stroke.blend_mode = BlendMode::Normal;
  stroke.color = RgbColor{255, 0, 0};
  stroke.opacity = 1.0F;
  stroke.size = 3.0F;
  stroke.position = LayerStrokePosition::Outside;
  return stroke;
}

LayerBevelEmboss default_bevel_emboss() {
  LayerBevelEmboss bevel;
  bevel.enabled = true;
  bevel.highlight_blend_mode = BlendMode::Screen;
  bevel.highlight_color = RgbColor{255, 255, 255};
  bevel.highlight_opacity = 0.75F;
  bevel.shadow_blend_mode = BlendMode::Multiply;
  bevel.shadow_color = RgbColor{0, 0, 0};
  bevel.shadow_opacity = 0.75F;
  bevel.angle_degrees = 120.0F;
  bevel.altitude_degrees = 30.0F;
  bevel.depth = 1.0F;
  bevel.size = 5.0F;
  bevel.direction_up = true;
  return bevel;
}

LayerSatin default_satin() {
  LayerSatin satin;
  satin.enabled = true;
  return satin;
}

enum class LayerStyleCategoryPage {
  Blending = 0,
  BevelEmboss,
  Stroke,
  InnerShadow,
  InnerGlow,
  Satin,
  ColorOverlay,
  GradientOverlay,
  OuterGlow,
  DropShadow
};

enum class LayerStyleEffectKind {
  None = 0,
  BevelEmboss,
  Stroke,
  InnerShadow,
  InnerGlow,
  Satin,
  ColorOverlay,
  GradientOverlay,
  OuterGlow,
  DropShadow
};

constexpr int kLayerStylePageRole = Qt::UserRole + 1;
constexpr int kLayerStyleEffectKindRole = Qt::UserRole + 2;
constexpr int kLayerStyleEffectIndexRole = Qt::UserRole + 3;
constexpr int kLayerStylePreviewCoalesceDelayMs = 33;

template <typename Settings>
class CoalescedLayerStylePreviewEmitter {
public:
  CoalescedLayerStylePreviewEmitter(QObject& owner, std::function<void(const Settings&)> callback)
      : callback_(std::move(callback)) {
    timer_ = new QTimer(&owner);
    timer_->setSingleShot(true);
    timer_->setInterval(kLayerStylePreviewCoalesceDelayMs);
    QObject::connect(timer_, &QTimer::timeout, &owner, [this] { deliver(); });
  }

  void schedule(Settings settings) {
    pending_ = std::move(settings);
    timer_->start();
  }

  void flush(Settings settings) {
    timer_->stop();
    pending_ = std::move(settings);
    deliver();
  }

private:
  void deliver() {
    if (!pending_.has_value() || !callback_) {
      return;
    }
    auto settings = std::move(*pending_);
    pending_.reset();
    callback_(settings);
  }

  QTimer* timer_{nullptr};
  std::optional<Settings> pending_;
  std::function<void(const Settings&)> callback_;
};

// Shared host state for the callback-driven gradient-stop editor used by both
// Gradient Overlay and gradient Strokes. The editor widget intentionally never
// owns or mutates its stop vectors; these working vectors remain stable during
// a drag, and value() sorts only the copies written back to LayerStyle.
struct LayerStyleGradientControls {
  GradientStopsEditorWidget* editor{nullptr};
  QSpinBox* stop_location{nullptr};
  QLabel* stop_color_label{nullptr};
  QLineEdit* stop_hex{nullptr};
  QPushButton* stop_swatch{nullptr};
  QLabel* stop_opacity_label{nullptr};
  QSpinBox* stop_opacity{nullptr};
  QLabel* stop_midpoint_label{nullptr};
  QSpinBox* stop_midpoint{nullptr};
  QPushButton* add_stop{nullptr};
  QPushButton* remove_stop{nullptr};
  QCheckBox* reverse{nullptr};
  QComboBox* style_combo{nullptr};
  QSpinBox* angle{nullptr};
  QSpinBox* scale{nullptr};
  std::vector<GradientColorStop> color_stops;
  std::vector<GradientAlphaStop> alpha_stops;
  int selected_color_stop{0};
  int selected_alpha_stop{-1};
  bool loading{false};
  std::function<void()> update_previews;
  std::function<void(bool)> changed;
  std::function<void(const LayerStyleGradient&)> load;

  [[nodiscard]] LayerStyleGradient value() const {
    LayerStyleGradient result;
    result.type = static_cast<LayerStyleGradientType>(style_combo->currentData().toInt());
    result.reverse = reverse->isChecked();
    result.angle_degrees = static_cast<float>(angle->value());
    result.scale = static_cast<float>(scale->value()) / 100.0F;
    result.color_stops = color_stops;
    if (result.color_stops.empty()) {
      result.color_stops = default_layer_style_gradient().color_stops;
    }
    std::stable_sort(result.color_stops.begin(), result.color_stops.end(),
                     [](const GradientColorStop& lhs, const GradientColorStop& rhs) {
                       return lhs.location < rhs.location;
                     });
    result.alpha_stops = alpha_stops;
    if (result.alpha_stops.empty()) {
      result.alpha_stops = default_layer_style_gradient().alpha_stops;
    }
    std::stable_sort(result.alpha_stops.begin(), result.alpha_stops.end(),
                     [](const GradientAlphaStop& lhs, const GradientAlphaStop& rhs) {
                       return lhs.location < rhs.location;
                     });
    return result;
  }
};

QListWidgetItem* add_layer_style_category(QListWidget* list, const QString& text, bool checkable, bool checked,
                                           LayerStyleCategoryPage page,
                                           LayerStyleEffectKind kind = LayerStyleEffectKind::None,
                                           int effect_index = -1) {
  auto* item = new QListWidgetItem(text, list);
  item->setData(kLayerStylePageRole, static_cast<int>(page));
  item->setData(kLayerStyleEffectKindRole, static_cast<int>(kind));
  item->setData(kLayerStyleEffectIndexRole, effect_index);
  if (checkable) {
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
  } else {
    item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
  }
  return item;
}

void update_color_preview_label(QWidget* widget, int red, int green, int blue) {
  widget->setStyleSheet(QStringLiteral("%1 { background: rgb(%2, %3, %4); border: 1px solid #9aa4b2; }")
                           .arg(widget->metaObject()->className())
                           .arg(red)
                           .arg(green)
                           .arg(blue));
}

}  // namespace

std::optional<LayerStyleSettings> request_layer_style_settings(
    QWidget* parent, const Layer& layer, std::function<void(const LayerStyleSettings&)> preview_changed) {
  const LayerStyleSettings original_settings{
      static_cast<int>(std::round(layer.opacity() * 100.0F)), layer.blend_mode(), layer.layer_style(),
      layer.blend_if(), false};
  auto style = layer.layer_style();
  auto blend_if = layer.blend_if();
  const auto blend_if_payload_status = layer.blend_if_payload_status();
  bool replace_unsupported_blend_if = false;
  auto shadow = style.drop_shadows.empty() ? default_drop_shadow() : style.drop_shadows.front();
  auto inner_shadow = style.inner_shadows.empty() ? default_inner_shadow() : style.inner_shadows.front();
  auto outer_glow = style.outer_glows.empty() ? default_outer_glow() : style.outer_glows.front();
  auto inner_glow = style.inner_glows.empty() ? default_inner_glow() : style.inner_glows.front();
  auto color_overlay = style.color_overlays.empty() ? default_color_overlay() : style.color_overlays.front();
  auto gradient = style.gradient_fills.empty() ? default_gradient_fill() : style.gradient_fills.front();
  auto stroke = style.strokes.empty() ? default_stroke() : style.strokes.front();
  auto bevel = style.bevels.empty() ? default_bevel_emboss() : style.bevels.front();
  auto satin = style.satins.empty() ? default_satin() : style.satins.front();
  if (gradient.gradient.color_stops.empty()) {
    gradient.gradient = default_layer_style_gradient();
  }
  if (stroke.uses_gradient && stroke.gradient.color_stops.empty()) {
    stroke.gradient = default_layer_style_gradient();
  }

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyLayerStyleDialog"));
  dialog.resize(840, 680);
  auto* root = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), QObject::tr("Layer Style"),
                                          DialogChromeCloseMode::Accept);

  auto* name_row = new QHBoxLayout();
  name_row->addWidget(new QLabel(QObject::tr("Name:"), &dialog));
  auto* name = new QLineEdit(QString::fromStdString(layer.name()), &dialog);
  name->setObjectName(QStringLiteral("layerStyleLayerNameEdit"));
  name->setReadOnly(true);
  name_row->addWidget(name, 1);
  root->addLayout(name_row);

  const bool has_enabled_pattern = std::any_of(
      style.pattern_overlays.begin(), style.pattern_overlays.end(),
      [](const LayerPatternOverlay& effect) { return effect.enabled; });
  if (has_enabled_pattern) {
    auto* warning = new QLabel(&dialog);
    warning->setObjectName(QStringLiteral("layerStyleUnsupportedEffectsWarning"));
    warning->setWordWrap(true);
    warning->setProperty("warningBanner", true);
    warning->setText(
        QObject::tr(
            "%1 is preserved for PSD round-trip, but Patchy does not render or edit it. Saving other layer style "
            "changes may normalize Photoshop-only effect options.")
            .arg(QObject::tr("Pattern Overlay")));
    warning->setStyleSheet(QStringLiteral(
        "QLabel#layerStyleUnsupportedEffectsWarning { background: #4a3a1f; border: 1px solid #9a7430; "
        "border-radius: 3px; color: #ffe0a3; padding: 7px 9px; }"));
    root->addWidget(warning);
  }
  const bool has_unsupported_satin_contour =
      std::any_of(style.satins.begin(), style.satins.end(), [](const LayerSatin& effect) {
        return effect.unsupported_contour_options;
      });
  if (has_unsupported_satin_contour) {
    auto* warning = new QLabel(
        QObject::tr("Photoshop Satin custom contours and contour anti-aliasing are preserved until you edit layer "
                    "styles. Patchy previews and saves edited Satin with the non-anti-aliased Linear contour."),
        &dialog);
    warning->setObjectName(QStringLiteral("layerStyleSatinContourWarning"));
    warning->setWordWrap(true);
    warning->setProperty("warningBanner", true);
    warning->setStyleSheet(QStringLiteral(
        "QLabel#layerStyleSatinContourWarning { background: #4a3a1f; border: 1px solid #9a7430; "
        "border-radius: 3px; color: #ffe0a3; padding: 7px 9px; }"));
    root->addWidget(warning);
  }
  if (layer.kind() == LayerKind::Group) {
    auto* warning = new QLabel(
        QObject::tr("Layer effects on groups are preserved for PSD round-trip but are not rendered yet. Satin "
                    "controls remain editable, but Preview cannot show the group result."),
        &dialog);
    warning->setObjectName(QStringLiteral("layerStyleGroupEffectsWarning"));
    warning->setWordWrap(true);
    warning->setProperty("warningBanner", true);
    warning->setStyleSheet(QStringLiteral(
        "QLabel#layerStyleGroupEffectsWarning { background: #4a3a1f; border: 1px solid #9a7430; "
        "border-radius: 3px; color: #ffe0a3; padding: 7px 9px; }"));
    root->addWidget(warning);
  }

  QLabel* blend_if_unsupported_warning = nullptr;
  QPushButton* replace_blend_if_button = nullptr;
  if (blend_if_payload_status == BlendIfPayloadStatus::Unsupported) {
    auto* warning_row = new QWidget(&dialog);
    warning_row->setObjectName(QStringLiteral("layerStyleBlendIfUnsupportedWarningRow"));
    auto* warning_layout = new QHBoxLayout(warning_row);
    warning_layout->setContentsMargins(8, 6, 8, 6);
    warning_layout->setSpacing(8);
    blend_if_unsupported_warning = new QLabel(
        QObject::tr("This layer contains Photoshop Blend If data for an unsupported color mode or payload shape. "
                    "Patchy preserves it unchanged and does not preview it unless you replace it."),
        warning_row);
    blend_if_unsupported_warning->setObjectName(QStringLiteral("layerStyleBlendIfUnsupportedWarning"));
    blend_if_unsupported_warning->setWordWrap(true);
    blend_if_unsupported_warning->setProperty("warningBanner", true);
    warning_layout->addWidget(blend_if_unsupported_warning, 1);
    replace_blend_if_button = new QPushButton(QObject::tr("Replace with Editable Defaults"), warning_row);
    replace_blend_if_button->setObjectName(QStringLiteral("layerStyleBlendIfReplaceButton"));
    warning_layout->addWidget(replace_blend_if_button, 0, Qt::AlignVCenter);
    warning_row->setStyleSheet(QStringLiteral(
        "QWidget#layerStyleBlendIfUnsupportedWarningRow { background: #4a3a1f; border: 1px solid #9a7430; "
        "border-radius: 3px; }"));
    root->addWidget(warning_row);
  }

  const auto boundary_blend_if = decode_layer_blend_if(layer.raw_psd_group_boundary_blending_ranges());
  if (!layer.raw_psd_group_boundary_blending_ranges().empty() &&
      (boundary_blend_if.status == BlendIfPayloadStatus::Unsupported ||
       !blend_if_is_identity(boundary_blend_if.settings))) {
    auto* warning = new QLabel(
        QObject::tr("This folder's closing PSD record contains separate Blend If data. Patchy preserves that "
                    "boundary data unchanged; the controls below edit only the visible folder record."),
        &dialog);
    warning->setObjectName(QStringLiteral("layerStyleBlendIfBoundaryWarning"));
    warning->setWordWrap(true);
    warning->setProperty("warningBanner", true);
    warning->setStyleSheet(QStringLiteral(
        "QLabel#layerStyleBlendIfBoundaryWarning { background: #4a3a1f; border: 1px solid #9a7430; "
        "border-radius: 3px; color: #ffe0a3; padding: 7px 9px; }"));
    root->addWidget(warning);
  }

  auto* body = new QHBoxLayout();
  root->addLayout(body, 1);

  auto* categories = new QListWidget(&dialog);
  categories->setObjectName(QStringLiteral("layerStyleCategoryList"));
  categories->setMinimumWidth(210);
  categories->setMaximumWidth(230);
  auto* category_pane = new QWidget(&dialog);
  auto* category_layout = new QVBoxLayout(category_pane);
  category_layout->setContentsMargins(0, 0, 0, 0);
  category_layout->setSpacing(6);
  category_layout->addWidget(categories, 1);
  auto* category_actions = new QHBoxLayout();
  category_actions->setContentsMargins(0, 0, 0, 0);
  category_actions->setSpacing(4);
  category_actions->addStretch(1);
  auto* remove_selected_instance = new QPushButton(category_pane);
  remove_selected_instance->setObjectName(QStringLiteral("layerStyleRemoveSelectedInstanceButton"));
  remove_selected_instance->setIcon(dialog.style()->standardIcon(QStyle::SP_TrashIcon));
  remove_selected_instance->setFixedSize(26, 24);
  remove_selected_instance->setToolTip(QObject::tr("Remove Selected Instance"));
  category_actions->addWidget(remove_selected_instance);
  category_layout->addLayout(category_actions);
  body->addWidget(category_pane);

  auto* controls = new QStackedWidget(&dialog);
  controls->setObjectName(QStringLiteral("layerStyleOptionsStack"));
  body->addWidget(controls, 1);
  auto make_page = [controls](const QString& object_name) {
    auto* page = new QWidget(controls);
    page->setObjectName(object_name);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    controls->addWidget(page);
    return layout;
  };
  auto slider_object_name = [](QString spin_object_name) {
    if (spin_object_name.endsWith(QStringLiteral("Spin"))) {
      spin_object_name.chop(4);
    }
    return spin_object_name + QStringLiteral("Slider");
  };
  auto add_slider_spin_row = [&slider_object_name](QFormLayout* form, QWidget* parent, const QString& label,
                                                   const QString& spin_object_name, int minimum, int maximum,
                                                   int value, const QString& suffix = {}, int spin_width = 72) {
    auto* row = new QWidget(parent);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(8);
    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setObjectName(slider_object_name(spin_object_name));
    slider->setRange(minimum, maximum);
    slider->setValue(value);
    auto* spin = new QSpinBox(row);
    spin->setObjectName(spin_object_name);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    if (!suffix.isEmpty()) {
      spin->setSuffix(suffix);
    }
    configure_dialog_spinbox(spin, spin_width);
    row_layout->addWidget(slider, 1);
    row_layout->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    form->addRow(label, row);
    return spin;
  };
  auto add_color_slider_row = [&slider_object_name](QVBoxLayout* layout, QWidget* parent, const QString& label,
                                                    const QString& spin_object_name, std::uint8_t value) {
    auto* row = new QWidget(parent);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(8);
    auto* channel_label = new QLabel(label, row);
    channel_label->setFixedWidth(18);
    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setObjectName(slider_object_name(spin_object_name));
    slider->setRange(0, 255);
    slider->setValue(value);
    auto* spin = new QSpinBox(row);
    spin->setObjectName(spin_object_name);
    spin->setRange(0, 255);
    spin->setValue(value);
    configure_dialog_spinbox(spin, 54);
    row_layout->addWidget(channel_label);
    row_layout->addWidget(slider, 1);
    row_layout->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    layout->addWidget(row);
    return spin;
  };
  auto make_gradient_controls = [&](QFormLayout* form, QWidget* parent_widget, const QString& object_prefix,
                                    const LayerStyleGradient& initial_gradient,
                                    const QString& color_picker_title) {
    auto controls_state = std::make_unique<LayerStyleGradientControls>();
    auto* state = controls_state.get();

    state->editor = new GradientStopsEditorWidget(parent_widget);
    state->editor->setObjectName(object_prefix + QStringLiteral("StopsEditor"));
    state->editor->set_opacity_track_enabled(true);
    form->addRow(QObject::tr("Gradient"), state->editor);

    auto* selected_row = new QWidget(parent_widget);
    selected_row->setObjectName(object_prefix + QStringLiteral("SelectedStopRow"));
    auto* selected_layout = new QHBoxLayout(selected_row);
    selected_layout->setContentsMargins(0, 0, 0, 0);
    selected_layout->setSpacing(8);
    auto* location_label = new QLabel(QObject::tr("Location"), selected_row);
    state->stop_location = new QSpinBox(selected_row);
    state->stop_location->setObjectName(object_prefix + QStringLiteral("StopLocationSpin"));
    state->stop_location->setRange(0, 100);
    state->stop_location->setSuffix(QStringLiteral("%"));
    configure_dialog_spinbox(state->stop_location, 64);
    state->stop_color_label = new QLabel(QObject::tr("Color"), selected_row);
    state->stop_hex = new QLineEdit(selected_row);
    state->stop_hex->setObjectName(object_prefix + QStringLiteral("StopHexEdit"));
    state->stop_hex->setFixedWidth(72);
    state->stop_hex->setMaxLength(7);
    state->stop_swatch = new QPushButton(selected_row);
    state->stop_swatch->setObjectName(object_prefix + QStringLiteral("StopSwatchButton"));
    state->stop_swatch->setFixedSize(34, 24);
    state->stop_swatch->setToolTip(QObject::tr("Choose Color..."));
    state->stop_opacity_label = new QLabel(QObject::tr("Opacity"), selected_row);
    state->stop_opacity = new QSpinBox(selected_row);
    state->stop_opacity->setObjectName(object_prefix + QStringLiteral("StopOpacitySpin"));
    state->stop_opacity->setRange(0, 100);
    state->stop_opacity->setSuffix(QStringLiteral("%"));
    configure_dialog_spinbox(state->stop_opacity, 64);
    selected_layout->addWidget(location_label);
    selected_layout->addWidget(state->stop_location);
    selected_layout->addWidget(state->stop_color_label);
    selected_layout->addWidget(state->stop_hex);
    selected_layout->addWidget(state->stop_swatch);
    selected_layout->addWidget(state->stop_opacity_label);
    selected_layout->addWidget(state->stop_opacity);
    selected_layout->addStretch(1);
    form->addRow(QString(), selected_row);

    auto* midpoint_row = new QWidget(parent_widget);
    auto* midpoint_layout = new QHBoxLayout(midpoint_row);
    midpoint_layout->setContentsMargins(0, 0, 0, 0);
    midpoint_layout->setSpacing(8);
    state->stop_midpoint_label = new QLabel(QObject::tr("Midpoint"), midpoint_row);
    state->stop_midpoint = new QSpinBox(midpoint_row);
    state->stop_midpoint->setObjectName(object_prefix + QStringLiteral("StopMidpointSpin"));
    state->stop_midpoint->setRange(5, 95);
    state->stop_midpoint->setSuffix(QStringLiteral("%"));
    configure_dialog_spinbox(state->stop_midpoint, 64);
    midpoint_layout->addWidget(state->stop_midpoint_label);
    midpoint_layout->addWidget(state->stop_midpoint);
    midpoint_layout->addStretch(1);
    form->addRow(QString(), midpoint_row);

    auto* stop_buttons = new QWidget(parent_widget);
    auto* stop_button_layout = new QHBoxLayout(stop_buttons);
    stop_button_layout->setContentsMargins(0, 0, 0, 0);
    stop_button_layout->setSpacing(6);
    state->add_stop = new QPushButton(QObject::tr("Add Stop"), stop_buttons);
    state->add_stop->setObjectName(object_prefix + QStringLiteral("AddStopButton"));
    state->remove_stop = new QPushButton(QObject::tr("Remove Stop"), stop_buttons);
    state->remove_stop->setObjectName(object_prefix + QStringLiteral("RemoveStopButton"));
    stop_button_layout->addWidget(state->add_stop);
    stop_button_layout->addWidget(state->remove_stop);
    stop_button_layout->addStretch(1);
    form->addRow(QString(), stop_buttons);

    state->reverse = new QCheckBox(QObject::tr("Reverse"), parent_widget);
    state->reverse->setObjectName(object_prefix + QStringLiteral("ReverseCheck"));
    form->addRow(QString(), state->reverse);
    state->style_combo = new QComboBox(parent_widget);
    state->style_combo->setObjectName(object_prefix + QStringLiteral("StyleCombo"));
    state->style_combo->addItem(QObject::tr("Linear"), static_cast<int>(LayerStyleGradientType::Linear));
    state->style_combo->addItem(QObject::tr("Radial"), static_cast<int>(LayerStyleGradientType::Radial));
    state->style_combo->addItem(QObject::tr("Angle", "gradient style"),
                                static_cast<int>(LayerStyleGradientType::Angle));
    state->style_combo->addItem(QObject::tr("Reflected"), static_cast<int>(LayerStyleGradientType::Reflected));
    state->style_combo->addItem(QObject::tr("Diamond"), static_cast<int>(LayerStyleGradientType::Diamond));
    form->addRow(QObject::tr("Style"), state->style_combo);
    state->angle = add_slider_spin_row(form, parent_widget, QObject::tr("Angle"),
                                       object_prefix + QStringLiteral("AngleSpin"), -180, 180,
                                       static_cast<int>(std::round(initial_gradient.angle_degrees)));
    state->scale = add_slider_spin_row(form, parent_widget, QObject::tr("Scale"),
                                       object_prefix + QStringLiteral("ScaleSpin"), 1, 1000,
                                       static_cast<int>(std::round(initial_gradient.scale * 100.0F)),
                                       QStringLiteral("%"));

    state->update_previews = [state] {
      if (state->color_stops.empty()) {
        state->color_stops = default_layer_style_gradient().color_stops;
      }
      if (state->alpha_stops.empty()) {
        state->alpha_stops = default_layer_style_gradient().alpha_stops;
      }
      if (state->selected_alpha_stop >= 0) {
        state->selected_alpha_stop =
            std::clamp(state->selected_alpha_stop, 0, static_cast<int>(state->alpha_stops.size()) - 1);
        state->selected_color_stop = -1;
      } else {
        state->selected_color_stop =
            std::clamp(state->selected_color_stop, 0, static_cast<int>(state->color_stops.size()) - 1);
      }

      std::vector<GradientStop> editor_stops;
      editor_stops.reserve(state->color_stops.size());
      for (const auto& stop : state->color_stops) {
        editor_stops.push_back(
            GradientStop{stop.location, EditColor{stop.color.red, stop.color.green, stop.color.blue, 255}});
      }
      state->editor->set_stops(std::move(editor_stops));
      std::vector<float> color_midpoints;
      color_midpoints.reserve(state->color_stops.size());
      for (const auto& stop : state->color_stops) {
        color_midpoints.push_back(stop.midpoint);
      }
      state->editor->set_color_midpoints(std::move(color_midpoints));
      state->editor->set_opacity_stops(state->alpha_stops);
      state->editor->set_current_row(state->selected_color_stop);
      state->editor->set_current_opacity_row(state->selected_alpha_stop);

      const bool color_selected = state->selected_color_stop >= 0;
      state->stop_color_label->setVisible(color_selected);
      state->stop_hex->setVisible(color_selected);
      state->stop_swatch->setVisible(color_selected);
      state->stop_opacity_label->setVisible(!color_selected);
      state->stop_opacity->setVisible(!color_selected);

      const QSignalBlocker location_blocker(state->stop_location);
      const QSignalBlocker hex_blocker(state->stop_hex);
      const QSignalBlocker opacity_blocker(state->stop_opacity);
      const QSignalBlocker midpoint_blocker(state->stop_midpoint);
      auto has_previous_stop = [](const auto& stops, int row) {
        std::vector<int> rows(stops.size());
        std::iota(rows.begin(), rows.end(), 0);
        std::stable_sort(rows.begin(), rows.end(), [&](int lhs, int rhs) {
          return stops[static_cast<std::size_t>(lhs)].location <
                 stops[static_cast<std::size_t>(rhs)].location;
        });
        const auto found = std::find(rows.begin(), rows.end(), row);
        if (found == rows.end() || found == rows.begin()) {
          return false;
        }
        const auto previous = *(found - 1);
        return stops[static_cast<std::size_t>(previous)].location <
               stops[static_cast<std::size_t>(row)].location;
      };
      bool midpoint_available = false;
      if (color_selected) {
        const auto& stop = state->color_stops[static_cast<std::size_t>(state->selected_color_stop)];
        state->stop_location->setValue(
            static_cast<int>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 100.0F)));
        state->stop_hex->setText(
            QColor(stop.color.red, stop.color.green, stop.color.blue).name(QColor::HexRgb).toUpper());
        update_color_preview_label(state->stop_swatch, stop.color.red, stop.color.green, stop.color.blue);
        state->remove_stop->setEnabled(state->color_stops.size() > 2U);
        midpoint_available = has_previous_stop(state->color_stops, state->selected_color_stop);
        state->stop_midpoint->setValue(
            static_cast<int>(std::lround(std::clamp(stop.midpoint, 0.05F, 0.95F) * 100.0F)));
      } else {
        const auto& stop = state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)];
        state->stop_location->setValue(
            static_cast<int>(std::lround(std::clamp(stop.location, 0.0F, 1.0F) * 100.0F)));
        state->stop_opacity->setValue(
            static_cast<int>(std::lround(std::clamp(stop.opacity, 0.0F, 1.0F) * 100.0F)));
        state->remove_stop->setEnabled(state->alpha_stops.size() > 2U);
        midpoint_available = has_previous_stop(state->alpha_stops, state->selected_alpha_stop);
        state->stop_midpoint->setValue(
            static_cast<int>(std::lround(std::clamp(stop.midpoint, 0.05F, 0.95F) * 100.0F)));
      }
      state->stop_midpoint_label->setEnabled(midpoint_available);
      state->stop_midpoint->setEnabled(midpoint_available);
    };
    state->load = [state](const LayerStyleGradient& value) {
      state->loading = true;
      state->color_stops = value.color_stops;
      state->alpha_stops = value.alpha_stops;
      state->selected_color_stop = 0;
      state->selected_alpha_stop = -1;
      state->reverse->setChecked(value.reverse);
      state->style_combo->setCurrentIndex(
          std::max(0, state->style_combo->findData(static_cast<int>(value.type))));
      state->angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
      state->scale->setValue(static_cast<int>(std::round(value.scale * 100.0F)));
      state->update_previews();
      state->loading = false;
    };

    auto notify_changed = [state](bool immediate) {
      if (!state->loading && state->changed) {
        state->changed(immediate);
      }
    };
    state->editor->stop_selected = [state](int row) {
      state->selected_color_stop = row;
      state->selected_alpha_stop = -1;
      state->update_previews();
    };
    state->editor->opacity_stop_selected = [state](int row) {
      state->selected_alpha_stop = row;
      state->selected_color_stop = -1;
      state->update_previews();
    };
    state->editor->stop_location_changed = [state, notify_changed](int row, int location) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->color_stops[static_cast<std::size_t>(row)].location =
          static_cast<float>(std::clamp(location, 0, 100)) / 100.0F;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->opacity_stop_location_changed = [state, notify_changed](int row, int location) {
      if (row < 0 || row >= static_cast<int>(state->alpha_stops.size())) {
        return;
      }
      state->alpha_stops[static_cast<std::size_t>(row)].location =
          static_cast<float>(std::clamp(location, 0, 100)) / 100.0F;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->color_midpoint_changed = [state, notify_changed](int row, int midpoint) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->color_stops[static_cast<std::size_t>(row)].midpoint =
          static_cast<float>(std::clamp(midpoint, 5, 95)) / 100.0F;
      state->selected_color_stop = row;
      state->selected_alpha_stop = -1;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->opacity_midpoint_changed = [state, notify_changed](int row, int midpoint) {
      if (row < 0 || row >= static_cast<int>(state->alpha_stops.size())) {
        return;
      }
      state->alpha_stops[static_cast<std::size_t>(row)].midpoint =
          static_cast<float>(std::clamp(midpoint, 5, 95)) / 100.0F;
      state->selected_alpha_stop = row;
      state->selected_color_stop = -1;
      state->update_previews();
      notify_changed(false);
    };
    state->editor->stop_color_picked = [state, notify_changed](int row, QColor color) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size()) || !color.isValid()) {
        return;
      }
      state->color_stops[static_cast<std::size_t>(row)].color =
          RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue())};
      state->update_previews();
      notify_changed(true);
    };
    state->editor->stop_add_requested = [state, notify_changed](GradientStop stop) {
      state->color_stops.push_back(GradientColorStop{std::clamp(stop.location, 0.0F, 1.0F),
                                                     RgbColor{stop.color.r, stop.color.g, stop.color.b}});
      state->selected_color_stop = static_cast<int>(state->color_stops.size()) - 1;
      state->selected_alpha_stop = -1;
      state->update_previews();
      notify_changed(true);
      return state->selected_color_stop;
    };
    state->editor->opacity_stop_add_requested = [state, notify_changed](GradientAlphaStop stop) {
      state->alpha_stops.push_back(
          GradientAlphaStop{std::clamp(stop.location, 0.0F, 1.0F), std::clamp(stop.opacity, 0.0F, 1.0F)});
      state->selected_alpha_stop = static_cast<int>(state->alpha_stops.size()) - 1;
      state->selected_color_stop = -1;
      state->update_previews();
      notify_changed(true);
      return state->selected_alpha_stop;
    };
    state->editor->stop_delete_requested = [state, notify_changed](int row) {
      if (state->color_stops.size() <= 2U || row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->color_stops.erase(state->color_stops.begin() + row);
      state->selected_color_stop = std::min(row, static_cast<int>(state->color_stops.size()) - 1);
      state->selected_alpha_stop = -1;
      state->update_previews();
      notify_changed(true);
    };
    state->editor->opacity_stop_delete_requested = [state, notify_changed](int row) {
      if (state->alpha_stops.size() <= 2U || row < 0 || row >= static_cast<int>(state->alpha_stops.size())) {
        return;
      }
      state->alpha_stops.erase(state->alpha_stops.begin() + row);
      state->selected_alpha_stop = std::min(row, static_cast<int>(state->alpha_stops.size()) - 1);
      state->selected_color_stop = -1;
      state->update_previews();
      notify_changed(true);
    };
    QObject::connect(state->stop_location, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [state, notify_changed](int value) {
                       if (state->loading) {
                         return;
                       }
                       const auto location = static_cast<float>(std::clamp(value, 0, 100)) / 100.0F;
                       if (state->selected_color_stop >= 0 &&
                           state->selected_color_stop < static_cast<int>(state->color_stops.size())) {
                         state->color_stops[static_cast<std::size_t>(state->selected_color_stop)].location = location;
                       } else if (state->selected_alpha_stop >= 0 &&
                                  state->selected_alpha_stop < static_cast<int>(state->alpha_stops.size())) {
                         state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)].location = location;
                       } else {
                         return;
                       }
                       state->update_previews();
                       notify_changed(false);
                     });
    QObject::connect(state->stop_midpoint, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [state, notify_changed](int value) {
                       if (state->loading || !state->stop_midpoint->isEnabled()) {
                         return;
                       }
                       const auto midpoint = static_cast<float>(std::clamp(value, 5, 95)) / 100.0F;
                       if (state->selected_color_stop >= 0 &&
                           state->selected_color_stop < static_cast<int>(state->color_stops.size())) {
                         state->color_stops[static_cast<std::size_t>(state->selected_color_stop)].midpoint = midpoint;
                       } else if (state->selected_alpha_stop >= 0 &&
                                  state->selected_alpha_stop < static_cast<int>(state->alpha_stops.size())) {
                         state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)].midpoint = midpoint;
                       } else {
                         return;
                       }
                       state->update_previews();
                       notify_changed(false);
                     });
    QObject::connect(state->stop_opacity, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [state, notify_changed](int value) {
                       if (state->loading || state->selected_alpha_stop < 0 ||
                           state->selected_alpha_stop >= static_cast<int>(state->alpha_stops.size())) {
                         return;
                       }
                       state->alpha_stops[static_cast<std::size_t>(state->selected_alpha_stop)].opacity =
                           static_cast<float>(std::clamp(value, 0, 100)) / 100.0F;
                       state->update_previews();
                       notify_changed(false);
                     });
    QObject::connect(state->stop_hex, &QLineEdit::editingFinished, &dialog, [state, notify_changed] {
      if (state->loading || state->selected_color_stop < 0 ||
          state->selected_color_stop >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      auto text = state->stop_hex->text().trimmed();
      if (!text.startsWith(QLatin1Char('#')) && text.size() == 6) {
        text.prepend(QLatin1Char('#'));
      }
      const QColor color(text);
      if (!color.isValid()) {
        state->update_previews();
        return;
      }
      state->color_stops[static_cast<std::size_t>(state->selected_color_stop)].color =
          RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                   static_cast<std::uint8_t>(color.blue())};
      state->update_previews();
      notify_changed(true);
    });
    auto choose_stop_color = [&, state, notify_changed, color_picker_title] {
      if (state->selected_color_stop < 0 ||
          state->selected_color_stop >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      const auto row = state->selected_color_stop;
      const auto original = state->color_stops[static_cast<std::size_t>(row)].color;
      auto apply_color = [state, row](QColor color) {
        if (!color.isValid() || row >= static_cast<int>(state->color_stops.size())) {
          return;
        }
        state->color_stops[static_cast<std::size_t>(row)].color =
            RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                     static_cast<std::uint8_t>(color.blue())};
        state->update_previews();
      };
      const auto chosen = request_patchy_color(&dialog, QColor(original.red, original.green, original.blue),
                                               color_picker_title, [&](QColor color) {
                                                 apply_color(color);
                                                 notify_changed(false);
                                               });
      if (!chosen.has_value()) {
        apply_color(QColor(original.red, original.green, original.blue));
        notify_changed(true);
        return;
      }
      apply_color(*chosen);
      notify_changed(true);
    };
    QObject::connect(state->stop_swatch, &QPushButton::clicked, &dialog, choose_stop_color);
    state->editor->choose_stop_color_requested = [state, choose_stop_color](int row) {
      if (row < 0 || row >= static_cast<int>(state->color_stops.size())) {
        return;
      }
      state->selected_color_stop = row;
      state->selected_alpha_stop = -1;
      state->update_previews();
      choose_stop_color();
    };
    QObject::connect(state->add_stop, &QPushButton::clicked, &dialog, [state, notify_changed] {
      if (state->selected_alpha_stop >= 0) {
        auto stop = GradientAlphaStop{0.5F, 1.0F};
        if (!state->alpha_stops.empty()) {
          const auto source_index = static_cast<std::size_t>(
              std::clamp(state->selected_alpha_stop, 0, static_cast<int>(state->alpha_stops.size()) - 1));
          stop = state->alpha_stops[source_index];
          stop.location = std::clamp(stop.location + 0.1F, 0.0F, 1.0F);
        }
        state->alpha_stops.push_back(stop);
        state->selected_alpha_stop = static_cast<int>(state->alpha_stops.size()) - 1;
        state->selected_color_stop = -1;
      } else {
        auto stop = GradientColorStop{0.5F, RgbColor{255, 255, 255}};
        if (!state->color_stops.empty()) {
          const auto source_index = static_cast<std::size_t>(
              std::clamp(state->selected_color_stop, 0, static_cast<int>(state->color_stops.size()) - 1));
          stop = state->color_stops[source_index];
          stop.location = std::clamp(stop.location + 0.1F, 0.0F, 1.0F);
        }
        state->color_stops.push_back(stop);
        state->selected_color_stop = static_cast<int>(state->color_stops.size()) - 1;
        state->selected_alpha_stop = -1;
      }
      state->update_previews();
      notify_changed(true);
    });
    QObject::connect(state->remove_stop, &QPushButton::clicked, &dialog, [state, notify_changed] {
      if (state->selected_alpha_stop >= 0) {
        if (state->alpha_stops.size() <= 2U) {
          return;
        }
        const auto row =
            std::clamp(state->selected_alpha_stop, 0, static_cast<int>(state->alpha_stops.size()) - 1);
        state->alpha_stops.erase(state->alpha_stops.begin() + row);
        state->selected_alpha_stop = std::min(row, static_cast<int>(state->alpha_stops.size()) - 1);
      } else {
        if (state->color_stops.size() <= 2U) {
          return;
        }
        const auto row =
            std::clamp(state->selected_color_stop, 0, static_cast<int>(state->color_stops.size()) - 1);
        state->color_stops.erase(state->color_stops.begin() + row);
        state->selected_color_stop = std::min(row, static_cast<int>(state->color_stops.size()) - 1);
      }
      state->update_previews();
      notify_changed(true);
    });
    QObject::connect(state->reverse, &QCheckBox::toggled, &dialog,
                     [notify_changed](bool) { notify_changed(true); });
    QObject::connect(state->style_combo, &QComboBox::currentIndexChanged, &dialog,
                     [notify_changed](int) { notify_changed(true); });
    QObject::connect(state->angle, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [notify_changed](int) { notify_changed(false); });
    QObject::connect(state->scale, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [notify_changed](int) { notify_changed(false); });

    state->load(initial_gradient);
    return controls_state;
  };

  auto* blending_layout = make_page(QStringLiteral("layerStyleBlendingPage"));
  auto* bevel_layout = make_page(QStringLiteral("layerStyleBevelEmbossPage"));
  auto* stroke_layout = make_page(QStringLiteral("layerStyleStrokePage"));
  auto* inner_shadow_layout = make_page(QStringLiteral("layerStyleInnerShadowPage"));
  auto* inner_glow_layout = make_page(QStringLiteral("layerStyleInnerGlowPage"));
  auto* satin_layout = make_page(QStringLiteral("layerStyleSatinPage"));
  auto* color_overlay_layout = make_page(QStringLiteral("layerStyleColorOverlayPage"));
  auto* gradient_layout = make_page(QStringLiteral("layerStyleGradientOverlayPage"));
  auto* outer_glow_layout = make_page(QStringLiteral("layerStyleOuterGlowPage"));
  auto* shadow_layout = make_page(QStringLiteral("layerStyleDropShadowPage"));

  auto* blending_group = new QGroupBox(QObject::tr("Blending Options"), controls);
  auto* blending_form = new QFormLayout(blending_group);
  auto* blend = new QComboBox(blending_group);
  blend->setObjectName(QStringLiteral("layerStyleBlendModeCombo"));
  add_blend_mode_items(blend);
  blend->setCurrentIndex(std::max(0, blend->findData(static_cast<int>(layer.blend_mode()))));
  blending_form->addRow(QObject::tr("Blend Mode"), blend);
  auto* opacity = add_slider_spin_row(blending_form, blending_group, QObject::tr("Opacity"),
                                      QStringLiteral("layerStyleOpacitySpin"), 0, 100,
                                      static_cast<int>(std::round(layer.opacity() * 100.0F)), QStringLiteral("%"));
  auto* show_effects = new QCheckBox(QObject::tr("Show Effects"), blending_group);
  show_effects->setObjectName(QStringLiteral("layerStyleShowEffectsCheck"));
  show_effects->setChecked(style.effects_visible);
  blending_form->addRow(QString(), show_effects);
  auto* mask_hides_effects = new QCheckBox(QObject::tr("Layer Mask Hides Effects"), blending_group);
  mask_hides_effects->setObjectName(QStringLiteral("layerStyleMaskHidesEffectsCheck"));
  mask_hides_effects->setChecked(style.layer_mask_hides_effects);
  mask_hides_effects->setToolTip(
      QObject::tr("Clip drop shadows, glows, and strokes with the layer mask instead of only shaping them"));
  blending_form->addRow(QString(), mask_hides_effects);
  blending_layout->addWidget(blending_group);

  struct BlendIfRowWidgets {
    BlendIfRangeEditorWidget* editor{nullptr};
    QSpinBox* black_low{nullptr};
    QSpinBox* black_high{nullptr};
    QSpinBox* white_low{nullptr};
    QSpinBox* white_high{nullptr};
  };

  auto* blend_if_group = new QGroupBox(QObject::tr("Blend If"), controls);
  blend_if_group->setObjectName(QStringLiteral("layerStyleBlendIfGroup"));
  auto* blend_if_layout = new QVBoxLayout(blend_if_group);
  blend_if_layout->setSpacing(4);
  auto* blend_if_channel_row = new QHBoxLayout();
  auto* blend_if_channel_label = new QLabel(QObject::tr("Blend If:"), blend_if_group);
  auto* blend_if_channel = new QComboBox(blend_if_group);
  blend_if_channel->setObjectName(QStringLiteral("layerStyleBlendIfChannelCombo"));
  blend_if_channel->addItem(QObject::tr("Gray"), static_cast<int>(BlendIfChannel::Gray));
  blend_if_channel->addItem(QObject::tr("Red"), static_cast<int>(BlendIfChannel::Red));
  blend_if_channel->addItem(QObject::tr("Green"), static_cast<int>(BlendIfChannel::Green));
  blend_if_channel->addItem(QObject::tr("Blue"), static_cast<int>(BlendIfChannel::Blue));
  blend_if_channel_label->setBuddy(blend_if_channel);
  blend_if_channel_row->addWidget(blend_if_channel_label);
  blend_if_channel_row->addWidget(blend_if_channel, 1);
  auto* reset_blend_if_channel = new QPushButton(QObject::tr("Reset Channel"), blend_if_group);
  reset_blend_if_channel->setObjectName(QStringLiteral("layerStyleBlendIfResetChannelButton"));
  blend_if_channel_row->addWidget(reset_blend_if_channel);
  blend_if_layout->addLayout(blend_if_channel_row);

  auto make_blend_if_spin = [&](QWidget* parent_widget, const QString& object_name,
                                const QString& accessible_name) {
    auto* spin = new QSpinBox(parent_widget);
    spin->setObjectName(object_name);
    spin->setRange(0, 255);
    spin->setAccessibleName(accessible_name);
    spin->setFixedWidth(70);
    return spin;
  };

  auto make_blend_if_row = [&](const QString& title, const QString& object_prefix,
                               const QString& editor_object_name) {
    BlendIfRowWidgets row;
    auto* title_label = new QLabel(title, blend_if_group);
    title_label->setStyleSheet(QStringLiteral("font-weight: 600;"));
    blend_if_layout->addWidget(title_label);
    row.editor = new BlendIfRangeEditorWidget(blend_if_group);
    row.editor->setObjectName(editor_object_name);
    row.editor->set_accessibility_text(
        QObject::tr("%1 Blend If range").arg(title),
        QObject::tr("Use Page Up or Page Down to select a handle, arrow keys to move it, and Alt/Option-drag to "
                    "split a joined handle."));
    blend_if_layout->addWidget(row.editor);

    auto* values = new QHBoxLayout();
    values->setSpacing(5);
    auto* black_label = new QLabel(QObject::tr("Black"), blend_if_group);
    values->addWidget(black_label);
    row.black_low = make_blend_if_spin(
        blend_if_group, object_prefix + QStringLiteral("BlackLowSpin"),
        QObject::tr("%1 black transition start").arg(title));
    row.black_high = make_blend_if_spin(
        blend_if_group, object_prefix + QStringLiteral("BlackHighSpin"),
        QObject::tr("%1 black transition end").arg(title));
    black_label->setBuddy(row.black_low);
    values->addWidget(row.black_low);
    values->addWidget(new QLabel(QObject::tr("to"), blend_if_group));
    values->addWidget(row.black_high);
    values->addStretch(1);
    auto* white_label = new QLabel(QObject::tr("White"), blend_if_group);
    values->addWidget(white_label);
    row.white_low = make_blend_if_spin(
        blend_if_group, object_prefix + QStringLiteral("WhiteLowSpin"),
        QObject::tr("%1 white transition start").arg(title));
    row.white_high = make_blend_if_spin(
        blend_if_group, object_prefix + QStringLiteral("WhiteHighSpin"),
        QObject::tr("%1 white transition end").arg(title));
    white_label->setBuddy(row.white_low);
    values->addWidget(row.white_low);
    values->addWidget(new QLabel(QObject::tr("to"), blend_if_group));
    values->addWidget(row.white_high);
    blend_if_layout->addLayout(values);
    return row;
  };

  auto blend_if_this = make_blend_if_row(QObject::tr("This Layer"), QStringLiteral("layerStyleBlendIfThis"),
                                         QStringLiteral("layerStyleBlendIfThisEditor"));
  auto blend_if_underlying =
      make_blend_if_row(QObject::tr("Underlying Layer"), QStringLiteral("layerStyleBlendIfUnderlying"),
                        QStringLiteral("layerStyleBlendIfUnderlyingEditor"));
  blend_if_group->setEnabled(blend_if_payload_status != BlendIfPayloadStatus::Unsupported);
  blending_layout->addWidget(blend_if_group);

  bool loading_blend_if_controls = false;
  const auto current_blend_if_channel_index = [&] {
    return static_cast<std::size_t>(std::clamp(blend_if_channel->currentData().toInt(), 0, 3));
  };
  const auto set_blend_if_row = [&](BlendIfRowWidgets& row, BlendIfThresholds thresholds) {
    const QSignalBlocker black_low_blocker(row.black_low);
    const QSignalBlocker black_high_blocker(row.black_high);
    const QSignalBlocker white_low_blocker(row.white_low);
    const QSignalBlocker white_high_blocker(row.white_high);
    for (auto* spin : {row.black_low, row.black_high, row.white_low, row.white_high}) {
      spin->setRange(0, 255);
    }
    row.black_low->setValue(thresholds.black_low);
    row.black_high->setValue(thresholds.black_high);
    row.white_low->setValue(thresholds.white_low);
    row.white_high->setValue(thresholds.white_high);
    row.black_low->setRange(0, thresholds.black_high);
    row.black_high->setRange(thresholds.black_low, thresholds.white_low);
    row.white_low->setRange(thresholds.black_high, thresholds.white_high);
    row.white_high->setRange(thresholds.white_low, 255);
    row.editor->set_thresholds(thresholds);
  };
  const auto load_blend_if_controls = [&] {
    loading_blend_if_controls = true;
    const auto channel_index = current_blend_if_channel_index();
    const auto channel = static_cast<BlendIfChannel>(channel_index);
    blend_if_this.editor->set_ramp_channel(channel);
    blend_if_underlying.editor->set_ramp_channel(channel);
    set_blend_if_row(blend_if_this, blend_if.channels[channel_index].this_layer);
    set_blend_if_row(blend_if_underlying, blend_if.channels[channel_index].underlying_layer);
    loading_blend_if_controls = false;
  };
  load_blend_if_controls();

  // Preview is transient dialog state, independent of Photoshop's persisted
  // master effects switch (Show Effects above). It stays visible on every page.
  auto* preview_check = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview_check->setObjectName(QStringLiteral("layerStylePreviewCheck"));
  preview_check->setChecked(true);
  blending_layout->addStretch(1);

  auto* bevel_group = new QGroupBox(QObject::tr("Bevel & Emboss"), controls);
  auto* bevel_form = new QFormLayout(bevel_group);
  auto* bevel_size = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Size"),
                                         QStringLiteral("layerStyleBevelSizeSpin"), 1, 250,
                                         static_cast<int>(std::round(bevel.size)));
  auto* bevel_depth = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Depth"),
                                          QStringLiteral("layerStyleBevelDepthSpin"), 1, 1000,
                                          static_cast<int>(std::round(bevel.depth * 100.0F)), QStringLiteral("%"));
  auto* bevel_angle = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Angle"),
                                          QStringLiteral("layerStyleBevelAngleSpin"), -180, 180,
                                          static_cast<int>(std::round(bevel.angle_degrees)));
  auto* bevel_altitude = add_slider_spin_row(bevel_form, bevel_group, QObject::tr("Altitude"),
                                             QStringLiteral("layerStyleBevelAltitudeSpin"), 0, 90,
                                             static_cast<int>(std::round(bevel.altitude_degrees)));
  auto* bevel_direction = new QComboBox(bevel_group);
  bevel_direction->setObjectName(QStringLiteral("layerStyleBevelDirectionCombo"));
  bevel_direction->addItem(QObject::tr("Up"), true);
  bevel_direction->addItem(QObject::tr("Down"), false);
  bevel_direction->setCurrentIndex(bevel.direction_up ? 0 : 1);
  bevel_form->addRow(QObject::tr("Direction"), bevel_direction);

  auto bevel_highlight_color = bevel.highlight_color;
  auto* bevel_highlight_group = new QGroupBox(QObject::tr("Highlight"), bevel_group);
  auto* bevel_highlight_form = new QFormLayout(bevel_highlight_group);
  auto* bevel_highlight_blend = new QComboBox(bevel_highlight_group);
  bevel_highlight_blend->setObjectName(QStringLiteral("layerStyleBevelHighlightBlendModeCombo"));
  add_blend_mode_items(bevel_highlight_blend);
  bevel_highlight_blend->setCurrentIndex(
      std::max(0, bevel_highlight_blend->findData(static_cast<int>(bevel.highlight_blend_mode))));
  bevel_highlight_form->addRow(QObject::tr("Blend Mode"), bevel_highlight_blend);
  auto* bevel_highlight_opacity =
      add_slider_spin_row(bevel_highlight_form, bevel_highlight_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleBevelHighlightOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(bevel.highlight_opacity * 100.0F)), QStringLiteral("%"));
  auto* bevel_highlight_color_button = new QPushButton(bevel_highlight_group);
  bevel_highlight_color_button->setObjectName(QStringLiteral("layerStyleBevelHighlightColorButton"));
  bevel_highlight_color_button->setFixedSize(34, 24);
  bevel_highlight_color_button->setToolTip(QObject::tr("Choose Color..."));
  bevel_highlight_form->addRow(QObject::tr("Color"), bevel_highlight_color_button);
  bevel_form->addRow(bevel_highlight_group);

  auto bevel_shadow_color = bevel.shadow_color;
  auto* bevel_shadow_group = new QGroupBox(QObject::tr("Shadow"), bevel_group);
  auto* bevel_shadow_form = new QFormLayout(bevel_shadow_group);
  auto* bevel_shadow_blend = new QComboBox(bevel_shadow_group);
  bevel_shadow_blend->setObjectName(QStringLiteral("layerStyleBevelShadowBlendModeCombo"));
  add_blend_mode_items(bevel_shadow_blend);
  bevel_shadow_blend->setCurrentIndex(
      std::max(0, bevel_shadow_blend->findData(static_cast<int>(bevel.shadow_blend_mode))));
  bevel_shadow_form->addRow(QObject::tr("Blend Mode"), bevel_shadow_blend);
  auto* bevel_shadow_opacity =
      add_slider_spin_row(bevel_shadow_form, bevel_shadow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleBevelShadowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(bevel.shadow_opacity * 100.0F)), QStringLiteral("%"));
  auto* bevel_shadow_color_button = new QPushButton(bevel_shadow_group);
  bevel_shadow_color_button->setObjectName(QStringLiteral("layerStyleBevelShadowColorButton"));
  bevel_shadow_color_button->setFixedSize(34, 24);
  bevel_shadow_color_button->setToolTip(QObject::tr("Choose Color..."));
  bevel_shadow_form->addRow(QObject::tr("Color"), bevel_shadow_color_button);
  bevel_form->addRow(bevel_shadow_group);
  auto update_bevel_color_previews = [&] {
    update_color_preview_label(bevel_highlight_color_button, bevel_highlight_color.red,
                               bevel_highlight_color.green, bevel_highlight_color.blue);
    update_color_preview_label(bevel_shadow_color_button, bevel_shadow_color.red,
                               bevel_shadow_color.green, bevel_shadow_color.blue);
  };
  update_bevel_color_previews();
  bevel_layout->addWidget(bevel_group);
  bevel_layout->addStretch(1);

  auto* stroke_group = new QGroupBox(QObject::tr("Stroke"), controls);
  auto* stroke_form = new QFormLayout(stroke_group);
  auto* stroke_size = add_slider_spin_row(stroke_form, stroke_group, QObject::tr("Size"),
                                          QStringLiteral("layerStyleStrokeSizeSpin"), 1, 250,
                                          static_cast<int>(std::round(stroke.size)));
  auto* stroke_position = new QComboBox(stroke_group);
  stroke_position->setObjectName(QStringLiteral("layerStyleStrokePositionCombo"));
  stroke_position->addItem(QObject::tr("Outside"), static_cast<int>(LayerStrokePosition::Outside));
  stroke_position->addItem(QObject::tr("Inside"), static_cast<int>(LayerStrokePosition::Inside));
  stroke_position->addItem(QObject::tr("Center"), static_cast<int>(LayerStrokePosition::Center));
  stroke_position->setCurrentIndex(std::max(0, stroke_position->findData(static_cast<int>(stroke.position))));
  stroke_form->addRow(QObject::tr("Position"), stroke_position);
  auto* stroke_blend = new QComboBox(stroke_group);
  stroke_blend->setObjectName(QStringLiteral("layerStyleStrokeBlendModeCombo"));
  add_blend_mode_items(stroke_blend);
  stroke_blend->setCurrentIndex(std::max(0, stroke_blend->findData(static_cast<int>(stroke.blend_mode))));
  stroke_form->addRow(QObject::tr("Blend Mode"), stroke_blend);
  auto* stroke_opacity = add_slider_spin_row(stroke_form, stroke_group, QObject::tr("Opacity"),
                                             QStringLiteral("layerStyleStrokeOpacitySpin"), 0, 100,
                                             static_cast<int>(std::round(stroke.opacity * 100.0F)),
                                             QStringLiteral("%"));
  auto* stroke_fill = new QComboBox(stroke_group);
  stroke_fill->setObjectName(QStringLiteral("layerStyleStrokeFillCombo"));
  stroke_fill->addItem(QObject::tr("Color"), false);
  stroke_fill->addItem(QObject::tr("Gradient"), true);
  stroke_fill->setCurrentIndex(stroke.uses_gradient ? 1 : 0);
  stroke_form->addRow(QObject::tr("Fill"), stroke_fill);
  auto* stroke_color_row = new QWidget(stroke_group);
  auto* stroke_color_layout = new QVBoxLayout(stroke_color_row);
  stroke_color_layout->setContentsMargins(0, 0, 0, 0);
  stroke_color_layout->setSpacing(4);
  auto* stroke_red =
      add_color_slider_row(stroke_color_layout, stroke_color_row, QObject::tr("R"),
                           QStringLiteral("layerStyleStrokeRedSpin"), stroke.color.red);
  auto* stroke_green =
      add_color_slider_row(stroke_color_layout, stroke_color_row, QObject::tr("G"),
                           QStringLiteral("layerStyleStrokeGreenSpin"), stroke.color.green);
  auto* stroke_blue =
      add_color_slider_row(stroke_color_layout, stroke_color_row, QObject::tr("B"),
                           QStringLiteral("layerStyleStrokeBlueSpin"), stroke.color.blue);
  auto* stroke_preview_row = new QWidget(stroke_color_row);
  auto* stroke_preview_layout = new QHBoxLayout(stroke_preview_row);
  stroke_preview_layout->setContentsMargins(26, 0, 0, 0);
  stroke_preview_layout->setSpacing(8);
  auto* stroke_color_preview = new QPushButton(stroke_group);
  stroke_color_preview->setObjectName(QStringLiteral("layerStyleStrokeColorPreview"));
  stroke_color_preview->setFixedSize(28, 22);
  stroke_color_preview->setToolTip(QObject::tr("Choose Color..."));
  stroke_preview_layout->addWidget(stroke_color_preview);
  stroke_preview_layout->addStretch(1);
  stroke_color_layout->addWidget(stroke_preview_row);
  auto update_stroke_color_preview = [stroke_color_preview, stroke_red, stroke_green, stroke_blue] {
    update_color_preview_label(stroke_color_preview, stroke_red->value(), stroke_green->value(), stroke_blue->value());
  };
  update_stroke_color_preview();
  stroke_form->addRow(QObject::tr("Color RGB"), stroke_color_row);
  auto* stroke_gradient_group = new QGroupBox(QObject::tr("Gradient"), stroke_group);
  stroke_gradient_group->setObjectName(QStringLiteral("layerStyleStrokeGradientGroup"));
  auto* stroke_gradient_form = new QFormLayout(stroke_gradient_group);
  auto stroke_gradient_controls =
      make_gradient_controls(stroke_gradient_form, stroke_gradient_group,
                             QStringLiteral("layerStyleStrokeGradient"), stroke.gradient,
                             QObject::tr("Choose Gradient Stop Color"));
  stroke_form->addRow(stroke_gradient_group);
  auto update_stroke_fill_visibility = [&] {
    const bool uses_gradient = stroke_fill->currentData().toBool();
    stroke_form->setRowVisible(stroke_color_row, !uses_gradient);
    stroke_form->setRowVisible(stroke_gradient_group, uses_gradient);
  };
  update_stroke_fill_visibility();
  stroke_layout->addWidget(stroke_group);
  stroke_layout->addStretch(1);

  auto* inner_shadow_group = new QGroupBox(QObject::tr("Inner Shadow"), controls);
  auto* inner_shadow_form = new QFormLayout(inner_shadow_group);
  auto* inner_shadow_blend = new QComboBox(inner_shadow_group);
  inner_shadow_blend->setObjectName(QStringLiteral("layerStyleInnerShadowBlendModeCombo"));
  add_blend_mode_items(inner_shadow_blend);
  inner_shadow_blend->setCurrentIndex(
      std::max(0, inner_shadow_blend->findData(static_cast<int>(inner_shadow.blend_mode))));
  inner_shadow_form->addRow(QObject::tr("Blend Mode"), inner_shadow_blend);
  auto* inner_shadow_opacity =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleInnerShadowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(inner_shadow.opacity * 100.0F)), QStringLiteral("%"));
  auto* inner_shadow_angle =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Angle"),
                          QStringLiteral("layerStyleInnerShadowAngleSpin"), -180, 180,
                          static_cast<int>(std::round(inner_shadow.angle_degrees)));
  auto* inner_shadow_distance =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Distance"),
                          QStringLiteral("layerStyleInnerShadowDistanceSpin"), 0, 1000,
                          static_cast<int>(std::round(inner_shadow.distance)));
  auto* inner_shadow_size = add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Size"),
                                                QStringLiteral("layerStyleInnerShadowSizeSpin"), 0, 1000,
                                                static_cast<int>(std::round(inner_shadow.size)));
  auto* inner_shadow_choke =
      add_slider_spin_row(inner_shadow_form, inner_shadow_group, QObject::tr("Choke"),
                          QStringLiteral("layerStyleInnerShadowChokeSpin"), 0, 100,
                          static_cast<int>(std::round(inner_shadow.choke)), QStringLiteral("%"));
  auto* inner_shadow_color_row = new QWidget(inner_shadow_group);
  auto* inner_shadow_color_layout = new QVBoxLayout(inner_shadow_color_row);
  inner_shadow_color_layout->setContentsMargins(0, 0, 0, 0);
  inner_shadow_color_layout->setSpacing(4);
  auto* inner_shadow_red =
      add_color_slider_row(inner_shadow_color_layout, inner_shadow_color_row, QObject::tr("R"),
                           QStringLiteral("layerStyleInnerShadowRedSpin"), inner_shadow.color.red);
  auto* inner_shadow_green =
      add_color_slider_row(inner_shadow_color_layout, inner_shadow_color_row, QObject::tr("G"),
                           QStringLiteral("layerStyleInnerShadowGreenSpin"), inner_shadow.color.green);
  auto* inner_shadow_blue =
      add_color_slider_row(inner_shadow_color_layout, inner_shadow_color_row, QObject::tr("B"),
                           QStringLiteral("layerStyleInnerShadowBlueSpin"), inner_shadow.color.blue);
  auto* inner_shadow_preview_row = new QWidget(inner_shadow_color_row);
  auto* inner_shadow_preview_layout = new QHBoxLayout(inner_shadow_preview_row);
  inner_shadow_preview_layout->setContentsMargins(26, 0, 0, 0);
  inner_shadow_preview_layout->setSpacing(8);
  auto* inner_shadow_color_preview = new QPushButton(inner_shadow_group);
  inner_shadow_color_preview->setObjectName(QStringLiteral("layerStyleInnerShadowColorPreview"));
  inner_shadow_color_preview->setFixedSize(28, 22);
  inner_shadow_color_preview->setToolTip(QObject::tr("Choose Color..."));
  inner_shadow_preview_layout->addWidget(inner_shadow_color_preview);
  inner_shadow_preview_layout->addStretch(1);
  inner_shadow_color_layout->addWidget(inner_shadow_preview_row);
  auto update_inner_shadow_color_preview = [inner_shadow_color_preview, inner_shadow_red, inner_shadow_green,
                                            inner_shadow_blue] {
    update_color_preview_label(inner_shadow_color_preview, inner_shadow_red->value(), inner_shadow_green->value(),
                               inner_shadow_blue->value());
  };
  update_inner_shadow_color_preview();
  inner_shadow_form->addRow(QObject::tr("Color RGB"), inner_shadow_color_row);
  auto* inner_shadow_instance_row = new QWidget(inner_shadow_group);
  auto* inner_shadow_instance_layout = new QHBoxLayout(inner_shadow_instance_row);
  inner_shadow_instance_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_inner_shadow = new QPushButton(QStringLiteral("+"), inner_shadow_instance_row);
  add_inner_shadow->setObjectName(QStringLiteral("layerStyleAddInnerShadowButton"));
  add_inner_shadow->setToolTip(QObject::tr("Add Inner Shadow"));
  configure_compact_symbol_button(add_inner_shadow);
  auto* remove_inner_shadow = new QPushButton(QStringLiteral("-"), inner_shadow_instance_row);
  remove_inner_shadow->setObjectName(QStringLiteral("layerStyleRemoveInnerShadowButton"));
  remove_inner_shadow->setToolTip(QObject::tr("Remove Inner Shadow"));
  configure_compact_symbol_button(remove_inner_shadow);
  inner_shadow_instance_layout->addWidget(add_inner_shadow);
  inner_shadow_instance_layout->addWidget(remove_inner_shadow);
  inner_shadow_instance_layout->addStretch(1);
  inner_shadow_form->addRow(QObject::tr("Instances"), inner_shadow_instance_row);
  inner_shadow_layout->addWidget(inner_shadow_group);
  inner_shadow_layout->addStretch(1);

  auto* inner_glow_group = new QGroupBox(QObject::tr("Inner Glow"), controls);
  auto* inner_glow_form = new QFormLayout(inner_glow_group);
  auto* inner_glow_blend = new QComboBox(inner_glow_group);
  inner_glow_blend->setObjectName(QStringLiteral("layerStyleInnerGlowBlendModeCombo"));
  add_blend_mode_items(inner_glow_blend);
  inner_glow_blend->setCurrentIndex(std::max(0, inner_glow_blend->findData(static_cast<int>(inner_glow.blend_mode))));
  inner_glow_form->addRow(QObject::tr("Blend Mode"), inner_glow_blend);
  auto* inner_glow_opacity =
      add_slider_spin_row(inner_glow_form, inner_glow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleInnerGlowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(inner_glow.opacity * 100.0F)), QStringLiteral("%"));
  auto* inner_glow_size = add_slider_spin_row(inner_glow_form, inner_glow_group, QObject::tr("Size"),
                                              QStringLiteral("layerStyleInnerGlowSizeSpin"), 0, 1000,
                                              static_cast<int>(std::round(inner_glow.size)));
  auto* inner_glow_choke =
      add_slider_spin_row(inner_glow_form, inner_glow_group, QObject::tr("Choke"),
                          QStringLiteral("layerStyleInnerGlowChokeSpin"), 0, 100,
                          static_cast<int>(std::round(inner_glow.choke)), QStringLiteral("%"));
  auto* inner_glow_source = new QComboBox(inner_glow_group);
  inner_glow_source->setObjectName(QStringLiteral("layerStyleInnerGlowSourceCombo"));
  inner_glow_source->addItem(QObject::tr("Edge"), static_cast<int>(LayerInnerGlowSource::Edge));
  inner_glow_source->addItem(QObject::tr("Center"), static_cast<int>(LayerInnerGlowSource::Center));
  inner_glow_source->setCurrentIndex(std::max(0, inner_glow_source->findData(static_cast<int>(inner_glow.source))));
  inner_glow_form->addRow(QObject::tr("Source"), inner_glow_source);
  auto* inner_glow_color_row = new QWidget(inner_glow_group);
  auto* inner_glow_color_layout = new QVBoxLayout(inner_glow_color_row);
  inner_glow_color_layout->setContentsMargins(0, 0, 0, 0);
  inner_glow_color_layout->setSpacing(4);
  auto* inner_glow_red =
      add_color_slider_row(inner_glow_color_layout, inner_glow_color_row, QObject::tr("R"),
                           QStringLiteral("layerStyleInnerGlowRedSpin"), inner_glow.color.red);
  auto* inner_glow_green =
      add_color_slider_row(inner_glow_color_layout, inner_glow_color_row, QObject::tr("G"),
                           QStringLiteral("layerStyleInnerGlowGreenSpin"), inner_glow.color.green);
  auto* inner_glow_blue =
      add_color_slider_row(inner_glow_color_layout, inner_glow_color_row, QObject::tr("B"),
                           QStringLiteral("layerStyleInnerGlowBlueSpin"), inner_glow.color.blue);
  auto* inner_glow_preview_row = new QWidget(inner_glow_color_row);
  auto* inner_glow_preview_layout = new QHBoxLayout(inner_glow_preview_row);
  inner_glow_preview_layout->setContentsMargins(26, 0, 0, 0);
  inner_glow_preview_layout->setSpacing(8);
  auto* inner_glow_color_preview = new QPushButton(inner_glow_group);
  inner_glow_color_preview->setObjectName(QStringLiteral("layerStyleInnerGlowColorPreview"));
  inner_glow_color_preview->setFixedSize(28, 22);
  inner_glow_color_preview->setToolTip(QObject::tr("Choose Color..."));
  inner_glow_preview_layout->addWidget(inner_glow_color_preview);
  inner_glow_preview_layout->addStretch(1);
  inner_glow_color_layout->addWidget(inner_glow_preview_row);
  auto update_inner_glow_color_preview = [inner_glow_color_preview, inner_glow_red, inner_glow_green,
                                          inner_glow_blue] {
    update_color_preview_label(inner_glow_color_preview, inner_glow_red->value(), inner_glow_green->value(),
                               inner_glow_blue->value());
  };
  update_inner_glow_color_preview();
  inner_glow_form->addRow(QObject::tr("Color RGB"), inner_glow_color_row);
  auto* inner_glow_instance_row = new QWidget(inner_glow_group);
  auto* inner_glow_instance_layout = new QHBoxLayout(inner_glow_instance_row);
  inner_glow_instance_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_inner_glow = new QPushButton(QStringLiteral("+"), inner_glow_instance_row);
  add_inner_glow->setObjectName(QStringLiteral("layerStyleAddInnerGlowButton"));
  add_inner_glow->setToolTip(QObject::tr("Add Inner Glow"));
  configure_compact_symbol_button(add_inner_glow);
  auto* remove_inner_glow = new QPushButton(QStringLiteral("-"), inner_glow_instance_row);
  remove_inner_glow->setObjectName(QStringLiteral("layerStyleRemoveInnerGlowButton"));
  remove_inner_glow->setToolTip(QObject::tr("Remove Inner Glow"));
  configure_compact_symbol_button(remove_inner_glow);
  inner_glow_instance_layout->addWidget(add_inner_glow);
  inner_glow_instance_layout->addWidget(remove_inner_glow);
  inner_glow_instance_layout->addStretch(1);
  inner_glow_form->addRow(QObject::tr("Instances"), inner_glow_instance_row);
  inner_glow_layout->addWidget(inner_glow_group);
  inner_glow_layout->addStretch(1);

  auto* satin_group = new QGroupBox(QObject::tr("Satin"), controls);
  auto* satin_form = new QFormLayout(satin_group);
  auto* satin_blend = new QComboBox(satin_group);
  satin_blend->setObjectName(QStringLiteral("layerStyleSatinBlendModeCombo"));
  add_blend_mode_items(satin_blend);
  satin_blend->setCurrentIndex(std::max(0, satin_blend->findData(static_cast<int>(satin.blend_mode))));
  satin_form->addRow(QObject::tr("Blend Mode"), satin_blend);
  auto* satin_opacity =
      add_slider_spin_row(satin_form, satin_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleSatinOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(satin.opacity * 100.0F)), QStringLiteral("%"));
  auto* satin_angle =
      add_slider_spin_row(satin_form, satin_group, QObject::tr("Angle"),
                          QStringLiteral("layerStyleSatinAngleSpin"), -180, 180,
                          static_cast<int>(std::round(satin.angle_degrees)));
  auto* satin_distance =
      add_slider_spin_row(satin_form, satin_group, QObject::tr("Distance"),
                          QStringLiteral("layerStyleSatinDistanceSpin"), 0, 1000,
                          static_cast<int>(std::round(satin.distance)));
  auto* satin_size = add_slider_spin_row(satin_form, satin_group, QObject::tr("Size"),
                                         QStringLiteral("layerStyleSatinSizeSpin"), 0, 1000,
                                         static_cast<int>(std::round(satin.size)));
  auto* satin_invert = new QCheckBox(QObject::tr("Invert"), satin_group);
  satin_invert->setObjectName(QStringLiteral("layerStyleSatinInvertCheck"));
  satin_invert->setChecked(satin.invert);
  satin_form->addRow(QString(), satin_invert);
  auto* satin_color_row = new QWidget(satin_group);
  auto* satin_color_layout = new QVBoxLayout(satin_color_row);
  satin_color_layout->setContentsMargins(0, 0, 0, 0);
  satin_color_layout->setSpacing(4);
  auto* satin_red = add_color_slider_row(satin_color_layout, satin_color_row, QObject::tr("R"),
                                         QStringLiteral("layerStyleSatinRedSpin"), satin.color.red);
  auto* satin_green = add_color_slider_row(satin_color_layout, satin_color_row, QObject::tr("G"),
                                           QStringLiteral("layerStyleSatinGreenSpin"), satin.color.green);
  auto* satin_blue = add_color_slider_row(satin_color_layout, satin_color_row, QObject::tr("B"),
                                          QStringLiteral("layerStyleSatinBlueSpin"), satin.color.blue);
  auto* satin_preview_row = new QWidget(satin_color_row);
  auto* satin_preview_layout = new QHBoxLayout(satin_preview_row);
  satin_preview_layout->setContentsMargins(26, 0, 0, 0);
  satin_preview_layout->setSpacing(8);
  auto* satin_color_preview = new QPushButton(satin_preview_row);
  satin_color_preview->setObjectName(QStringLiteral("layerStyleSatinColorPreview"));
  satin_color_preview->setFixedSize(28, 22);
  satin_color_preview->setToolTip(QObject::tr("Choose Color..."));
  satin_preview_layout->addWidget(satin_color_preview);
  satin_preview_layout->addStretch(1);
  satin_color_layout->addWidget(satin_preview_row);
  auto update_satin_color_preview = [satin_color_preview, satin_red, satin_green, satin_blue] {
    update_color_preview_label(satin_color_preview, satin_red->value(), satin_green->value(), satin_blue->value());
  };
  update_satin_color_preview();
  satin_form->addRow(QObject::tr("Color RGB"), satin_color_row);
  satin_layout->addWidget(satin_group);
  satin_layout->addStretch(1);

  auto* color_overlay_group = new QGroupBox(QObject::tr("Color Overlay"), controls);
  auto* color_overlay_form = new QFormLayout(color_overlay_group);
  auto* color_overlay_blend = new QComboBox(color_overlay_group);
  color_overlay_blend->setObjectName(QStringLiteral("layerStyleColorOverlayBlendModeCombo"));
  add_blend_mode_items(color_overlay_blend);
  color_overlay_blend->setCurrentIndex(
      std::max(0, color_overlay_blend->findData(static_cast<int>(color_overlay.blend_mode))));
  color_overlay_form->addRow(QObject::tr("Blend Mode"), color_overlay_blend);
  auto* color_overlay_opacity =
      add_slider_spin_row(color_overlay_form, color_overlay_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleColorOverlayOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(color_overlay.opacity * 100.0F)), QStringLiteral("%"));
  auto* color_overlay_color_row = new QWidget(color_overlay_group);
  auto* color_overlay_color_layout = new QVBoxLayout(color_overlay_color_row);
  color_overlay_color_layout->setContentsMargins(0, 0, 0, 0);
  color_overlay_color_layout->setSpacing(4);
  auto* color_overlay_red =
      add_color_slider_row(color_overlay_color_layout, color_overlay_color_row, QObject::tr("R"),
                           QStringLiteral("layerStyleColorOverlayRedSpin"), color_overlay.color.red);
  auto* color_overlay_green =
      add_color_slider_row(color_overlay_color_layout, color_overlay_color_row, QObject::tr("G"),
                           QStringLiteral("layerStyleColorOverlayGreenSpin"), color_overlay.color.green);
  auto* color_overlay_blue =
      add_color_slider_row(color_overlay_color_layout, color_overlay_color_row, QObject::tr("B"),
                           QStringLiteral("layerStyleColorOverlayBlueSpin"), color_overlay.color.blue);
  auto* color_overlay_preview_row = new QWidget(color_overlay_color_row);
  auto* color_overlay_preview_layout = new QHBoxLayout(color_overlay_preview_row);
  color_overlay_preview_layout->setContentsMargins(26, 0, 0, 0);
  color_overlay_preview_layout->setSpacing(8);
  auto* color_overlay_color_preview = new QLabel(color_overlay_group);
  color_overlay_color_preview->setObjectName(QStringLiteral("layerStyleColorOverlayColorPreview"));
  color_overlay_color_preview->setFixedSize(34, 24);
  color_overlay_color_preview->setToolTip(QObject::tr("Choose Color..."));
  auto* color_overlay_pick_color = new QPushButton(QObject::tr("Choose Color..."), color_overlay_group);
  color_overlay_pick_color->setObjectName(QStringLiteral("layerStyleColorOverlayPickColorButton"));
  // Double-clicking the patch opens the same picker as the button.
  new DoubleClickClicksButton(color_overlay_pick_color, color_overlay_color_preview);
  color_overlay_preview_layout->addWidget(color_overlay_color_preview);
  color_overlay_preview_layout->addWidget(color_overlay_pick_color);
  color_overlay_preview_layout->addStretch(1);
  color_overlay_color_layout->addWidget(color_overlay_preview_row);
  auto update_color_overlay_color_preview = [color_overlay_color_preview, color_overlay_red, color_overlay_green,
                                             color_overlay_blue] {
    update_color_preview_label(color_overlay_color_preview, color_overlay_red->value(), color_overlay_green->value(),
                               color_overlay_blue->value());
  };
  update_color_overlay_color_preview();
  color_overlay_form->addRow(QObject::tr("Color RGB"), color_overlay_color_row);
  color_overlay_layout->addWidget(color_overlay_group);
  color_overlay_layout->addStretch(1);

  auto* gradient_group = new QGroupBox(QObject::tr("Gradient Overlay"), controls);
  auto* gradient_form = new QFormLayout(gradient_group);
  auto* gradient_blend = new QComboBox(gradient_group);
  gradient_blend->setObjectName(QStringLiteral("layerStyleGradientBlendModeCombo"));
  add_blend_mode_items(gradient_blend);
  gradient_blend->setCurrentIndex(std::max(0, gradient_blend->findData(static_cast<int>(gradient.blend_mode))));
  gradient_form->addRow(QObject::tr("Blend Mode"), gradient_blend);
  auto* gradient_opacity =
      add_slider_spin_row(gradient_form, gradient_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleGradientOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(gradient.opacity * 100.0F)), QStringLiteral("%"));
  auto gradient_controls =
      make_gradient_controls(gradient_form, gradient_group, QStringLiteral("layerStyleGradient"),
                             gradient.gradient, QObject::tr("Choose Gradient Stop Color"));
  gradient_layout->addWidget(gradient_group);
  gradient_layout->addStretch(1);

  auto* outer_glow_group = new QGroupBox(QObject::tr("Outer Glow"), controls);
  auto* outer_glow_form = new QFormLayout(outer_glow_group);
  auto* outer_glow_blend = new QComboBox(outer_glow_group);
  outer_glow_blend->setObjectName(QStringLiteral("layerStyleOuterGlowBlendModeCombo"));
  add_blend_mode_items(outer_glow_blend);
  outer_glow_blend->setCurrentIndex(std::max(0, outer_glow_blend->findData(static_cast<int>(outer_glow.blend_mode))));
  outer_glow_form->addRow(QObject::tr("Blend Mode"), outer_glow_blend);
  auto* outer_glow_opacity =
      add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Opacity"),
                          QStringLiteral("layerStyleOuterGlowOpacitySpin"), 0, 100,
                          static_cast<int>(std::round(outer_glow.opacity * 100.0F)), QStringLiteral("%"));
  auto* outer_glow_size = add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Size"),
                                              QStringLiteral("layerStyleOuterGlowSizeSpin"), 0, 1000,
                                              static_cast<int>(std::round(outer_glow.size)));
  auto* outer_glow_spread = add_slider_spin_row(outer_glow_form, outer_glow_group, QObject::tr("Spread"),
                                                QStringLiteral("layerStyleOuterGlowSpreadSpin"), 0, 100,
                                                static_cast<int>(std::round(outer_glow.spread)),
                                                QStringLiteral("%"));
  auto* outer_glow_color_row = new QWidget(outer_glow_group);
  auto* outer_glow_color_layout = new QVBoxLayout(outer_glow_color_row);
  outer_glow_color_layout->setContentsMargins(0, 0, 0, 0);
  outer_glow_color_layout->setSpacing(4);
  auto* outer_glow_red =
      add_color_slider_row(outer_glow_color_layout, outer_glow_color_row, QObject::tr("R"),
                           QStringLiteral("layerStyleOuterGlowRedSpin"), outer_glow.color.red);
  auto* outer_glow_green =
      add_color_slider_row(outer_glow_color_layout, outer_glow_color_row, QObject::tr("G"),
                           QStringLiteral("layerStyleOuterGlowGreenSpin"), outer_glow.color.green);
  auto* outer_glow_blue =
      add_color_slider_row(outer_glow_color_layout, outer_glow_color_row, QObject::tr("B"),
                           QStringLiteral("layerStyleOuterGlowBlueSpin"), outer_glow.color.blue);
  auto* outer_glow_preview_row = new QWidget(outer_glow_color_row);
  auto* outer_glow_preview_layout = new QHBoxLayout(outer_glow_preview_row);
  outer_glow_preview_layout->setContentsMargins(26, 0, 0, 0);
  outer_glow_preview_layout->setSpacing(8);
  auto* outer_glow_color_preview = new QPushButton(outer_glow_group);
  outer_glow_color_preview->setObjectName(QStringLiteral("layerStyleOuterGlowColorPreview"));
  outer_glow_color_preview->setFixedSize(28, 22);
  outer_glow_color_preview->setToolTip(QObject::tr("Choose Color..."));
  outer_glow_preview_layout->addWidget(outer_glow_color_preview);
  outer_glow_preview_layout->addStretch(1);
  outer_glow_color_layout->addWidget(outer_glow_preview_row);
  auto update_outer_glow_color_preview = [outer_glow_color_preview, outer_glow_red, outer_glow_green,
                                          outer_glow_blue] {
    update_color_preview_label(outer_glow_color_preview, outer_glow_red->value(), outer_glow_green->value(),
                               outer_glow_blue->value());
  };
  update_outer_glow_color_preview();
  outer_glow_form->addRow(QObject::tr("Color RGB"), outer_glow_color_row);
  outer_glow_layout->addWidget(outer_glow_group);
  outer_glow_layout->addStretch(1);

  auto* shadow_group = new QGroupBox(QObject::tr("Drop Shadow"), controls);
  auto* shadow_form = new QFormLayout(shadow_group);
  auto* shadow_blend = new QComboBox(shadow_group);
  shadow_blend->setObjectName(QStringLiteral("layerStyleDropShadowBlendModeCombo"));
  add_blend_mode_items(shadow_blend);
  shadow_blend->setCurrentIndex(std::max(0, shadow_blend->findData(static_cast<int>(shadow.blend_mode))));
  shadow_form->addRow(QObject::tr("Blend Mode"), shadow_blend);
  auto* shadow_opacity = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Opacity"),
                                             QStringLiteral("layerStyleDropShadowOpacitySpin"), 0, 100,
                                             static_cast<int>(std::round(shadow.opacity * 100.0F)),
                                             QStringLiteral("%"));
  auto* shadow_angle = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Angle"),
                                           QStringLiteral("layerStyleDropShadowAngleSpin"), -180, 180,
                                           static_cast<int>(std::round(shadow.angle_degrees)));
  auto* shadow_distance = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Distance"),
                                              QStringLiteral("layerStyleDropShadowDistanceSpin"), 0, 1000,
                                              static_cast<int>(std::round(shadow.distance)));
  auto* shadow_size = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Size"),
                                          QStringLiteral("layerStyleDropShadowSizeSpin"), 0, 1000,
                                          static_cast<int>(std::round(shadow.size)));
  auto* shadow_spread = add_slider_spin_row(shadow_form, shadow_group, QObject::tr("Spread"),
                                            QStringLiteral("layerStyleDropShadowSpreadSpin"), 0, 100,
                                            static_cast<int>(std::round(shadow.spread)), QStringLiteral("%"));
  auto* shadow_color_row = new QWidget(shadow_group);
  auto* shadow_color_layout = new QVBoxLayout(shadow_color_row);
  shadow_color_layout->setContentsMargins(0, 0, 0, 0);
  shadow_color_layout->setSpacing(4);
  auto* shadow_red =
      add_color_slider_row(shadow_color_layout, shadow_color_row, QObject::tr("R"),
                           QStringLiteral("layerStyleDropShadowRedSpin"), shadow.color.red);
  auto* shadow_green =
      add_color_slider_row(shadow_color_layout, shadow_color_row, QObject::tr("G"),
                           QStringLiteral("layerStyleDropShadowGreenSpin"), shadow.color.green);
  auto* shadow_blue =
      add_color_slider_row(shadow_color_layout, shadow_color_row, QObject::tr("B"),
                           QStringLiteral("layerStyleDropShadowBlueSpin"), shadow.color.blue);
  auto* shadow_preview_row = new QWidget(shadow_color_row);
  auto* shadow_preview_layout = new QHBoxLayout(shadow_preview_row);
  shadow_preview_layout->setContentsMargins(26, 0, 0, 0);
  shadow_preview_layout->setSpacing(8);
  auto* shadow_color_preview = new QPushButton(shadow_group);
  shadow_color_preview->setObjectName(QStringLiteral("layerStyleDropShadowColorPreview"));
  shadow_color_preview->setFixedSize(28, 22);
  shadow_color_preview->setToolTip(QObject::tr("Choose Color..."));
  shadow_preview_layout->addWidget(shadow_color_preview);
  shadow_preview_layout->addStretch(1);
  shadow_color_layout->addWidget(shadow_preview_row);
  auto update_shadow_color_preview = [shadow_color_preview, shadow_red, shadow_green, shadow_blue] {
    update_color_preview_label(shadow_color_preview, shadow_red->value(), shadow_green->value(), shadow_blue->value());
  };
  update_shadow_color_preview();
  shadow_form->addRow(QObject::tr("Color RGB"), shadow_color_row);
  shadow_layout->addWidget(shadow_group);
  shadow_layout->addStretch(1);

  auto item_page = [](const QListWidgetItem* item) {
    return item == nullptr ? LayerStyleCategoryPage::Blending
                           : static_cast<LayerStyleCategoryPage>(item->data(kLayerStylePageRole).toInt());
  };
  auto item_kind = [](const QListWidgetItem* item) {
    return item == nullptr ? LayerStyleEffectKind::None
                           : static_cast<LayerStyleEffectKind>(item->data(kLayerStyleEffectKindRole).toInt());
  };
  auto item_effect_index = [](const QListWidgetItem* item) {
    return item == nullptr ? -1 : item->data(kLayerStyleEffectIndexRole).toInt();
  };
  auto item_checked = [](const QListWidgetItem* item) {
    return item != nullptr && item->checkState() == Qt::Checked;
  };
  auto set_combo_data = [](QComboBox* combo, int data) {
    combo->setCurrentIndex(std::max(0, combo->findData(data)));
  };
  auto indexed_object_name = [](const QString& base_name, int index) {
    return index <= 0 ? base_name : base_name + QString::number(index + 1);
  };
  auto is_stackable_kind = [](LayerStyleEffectKind kind) {
    return kind == LayerStyleEffectKind::Stroke || kind == LayerStyleEffectKind::InnerShadow ||
           kind == LayerStyleEffectKind::InnerGlow || kind == LayerStyleEffectKind::Satin ||
           kind == LayerStyleEffectKind::ColorOverlay ||
           kind == LayerStyleEffectKind::GradientOverlay || kind == LayerStyleEffectKind::OuterGlow ||
           kind == LayerStyleEffectKind::DropShadow;
  };
  auto vector_count_for_kind = [](const LayerStyle& source, LayerStyleEffectKind kind) -> std::size_t {
    switch (kind) {
      case LayerStyleEffectKind::Stroke:
        return source.strokes.size();
      case LayerStyleEffectKind::InnerShadow:
        return source.inner_shadows.size();
      case LayerStyleEffectKind::InnerGlow:
        return source.inner_glows.size();
      case LayerStyleEffectKind::Satin:
        return source.satins.size();
      case LayerStyleEffectKind::ColorOverlay:
        return source.color_overlays.size();
      case LayerStyleEffectKind::GradientOverlay:
        return source.gradient_fills.size();
      case LayerStyleEffectKind::OuterGlow:
        return source.outer_glows.size();
      case LayerStyleEffectKind::DropShadow:
        return source.drop_shadows.size();
      case LayerStyleEffectKind::BevelEmboss:
        return source.bevels.size();
      case LayerStyleEffectKind::None:
        return 0U;
    }
    return 0U;
  };
  auto ensure_bevel = [](LayerStyle& target, int index) -> LayerBevelEmboss& {
    while (index >= 0 && target.bevels.size() <= static_cast<std::size_t>(index)) {
      target.bevels.push_back(default_bevel_emboss());
    }
    return target.bevels[static_cast<std::size_t>(std::max(0, index))];
  };
  auto ensure_stroke = [](LayerStyle& target, int index) -> LayerStroke& {
    while (index >= 0 && target.strokes.size() <= static_cast<std::size_t>(index)) {
      target.strokes.push_back(default_stroke());
    }
    return target.strokes[static_cast<std::size_t>(std::max(0, index))];
  };
  auto ensure_inner_shadow = [](LayerStyle& target, int index) -> LayerInnerShadow& {
    while (index >= 0 && target.inner_shadows.size() <= static_cast<std::size_t>(index)) {
      target.inner_shadows.push_back(default_inner_shadow());
    }
    return target.inner_shadows[static_cast<std::size_t>(std::max(0, index))];
  };
  auto ensure_inner_glow = [](LayerStyle& target, int index) -> LayerInnerGlow& {
    while (index >= 0 && target.inner_glows.size() <= static_cast<std::size_t>(index)) {
      target.inner_glows.push_back(default_inner_glow());
    }
    return target.inner_glows[static_cast<std::size_t>(std::max(0, index))];
  };
  auto ensure_satin = [](LayerStyle& target, int index) -> LayerSatin& {
    while (index >= 0 && target.satins.size() <= static_cast<std::size_t>(index)) {
      target.satins.push_back(default_satin());
    }
    return target.satins[static_cast<std::size_t>(std::max(0, index))];
  };
  auto ensure_color_overlay = [](LayerStyle& target, int index) -> LayerColorOverlay& {
    while (index >= 0 && target.color_overlays.size() <= static_cast<std::size_t>(index)) {
      target.color_overlays.push_back(default_color_overlay());
    }
    return target.color_overlays[static_cast<std::size_t>(std::max(0, index))];
  };
  auto ensure_gradient_fill = [](LayerStyle& target, int index) -> LayerGradientFill& {
    while (index >= 0 && target.gradient_fills.size() <= static_cast<std::size_t>(index)) {
      target.gradient_fills.push_back(default_gradient_fill());
    }
    return target.gradient_fills[static_cast<std::size_t>(std::max(0, index))];
  };
  auto ensure_outer_glow = [](LayerStyle& target, int index) -> LayerOuterGlow& {
    while (index >= 0 && target.outer_glows.size() <= static_cast<std::size_t>(index)) {
      target.outer_glows.push_back(default_outer_glow());
    }
    return target.outer_glows[static_cast<std::size_t>(std::max(0, index))];
  };
  auto ensure_drop_shadow = [](LayerStyle& target, int index) -> LayerDropShadow& {
    while (index >= 0 && target.drop_shadows.size() <= static_cast<std::size_t>(index)) {
      target.drop_shadows.push_back(default_drop_shadow());
    }
    return target.drop_shadows[static_cast<std::size_t>(std::max(0, index))];
  };
  auto apply_enabled_states = [&](LayerStyle& result) {
    for (int row = 0; row < categories->count(); ++row) {
      auto* category = categories->item(row);
      const auto enabled = item_checked(category);
      const auto index = item_effect_index(category);
      switch (item_kind(category)) {
        case LayerStyleEffectKind::BevelEmboss:
          if (enabled || !result.bevels.empty()) {
            ensure_bevel(result, index).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::Stroke:
          if (enabled || result.strokes.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_stroke(result, index).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::InnerShadow:
          if (enabled || result.inner_shadows.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_inner_shadow(result, index).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::InnerGlow:
          if (enabled || result.inner_glows.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_inner_glow(result, index).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::Satin:
          if (enabled || result.satins.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_satin(result, index).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::ColorOverlay:
          if (enabled || result.color_overlays.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_color_overlay(result, index).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::GradientOverlay:
          if (enabled || result.gradient_fills.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_gradient_fill(result, index).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::OuterGlow:
          if (enabled || result.outer_glows.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_outer_glow(result, index).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::DropShadow:
          if (enabled || result.drop_shadows.size() > static_cast<std::size_t>(std::max(0, index))) {
            ensure_drop_shadow(result, index).enabled = enabled;
          }
          break;
        case LayerStyleEffectKind::None:
          break;
      }
    }
  };
  auto save_controls_to_style = [&](LayerStyle& result, const QListWidgetItem* category) {
    const auto index = item_effect_index(category);
    if (index < 0) {
      return;
    }
    const auto enabled = item_checked(category);
    switch (item_kind(category)) {
      case LayerStyleEffectKind::BevelEmboss: {
        if (!enabled && result.bevels.empty()) {
          return;
        }
        auto& target = ensure_bevel(result, index);
        target.enabled = enabled;
        target.size = static_cast<float>(bevel_size->value());
        target.depth = static_cast<float>(bevel_depth->value()) / 100.0F;
        target.angle_degrees = static_cast<float>(bevel_angle->value());
        target.altitude_degrees = static_cast<float>(bevel_altitude->value());
        target.direction_up = bevel_direction->currentData().toBool();
        target.highlight_blend_mode =
            static_cast<BlendMode>(bevel_highlight_blend->currentData().toInt());
        target.highlight_color = bevel_highlight_color;
        target.highlight_opacity = static_cast<float>(bevel_highlight_opacity->value()) / 100.0F;
        target.shadow_blend_mode = static_cast<BlendMode>(bevel_shadow_blend->currentData().toInt());
        target.shadow_color = bevel_shadow_color;
        target.shadow_opacity = static_cast<float>(bevel_shadow_opacity->value()) / 100.0F;
        break;
      }
      case LayerStyleEffectKind::Stroke: {
        if (!enabled && result.strokes.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_stroke(result, index);
        target.enabled = enabled;
        target.size = static_cast<float>(stroke_size->value());
        target.blend_mode = static_cast<BlendMode>(stroke_blend->currentData().toInt());
        target.opacity = static_cast<float>(stroke_opacity->value()) / 100.0F;
        target.color = RgbColor{static_cast<std::uint8_t>(stroke_red->value()),
                                static_cast<std::uint8_t>(stroke_green->value()),
                                static_cast<std::uint8_t>(stroke_blue->value())};
        target.position = static_cast<LayerStrokePosition>(stroke_position->currentData().toInt());
        target.uses_gradient = stroke_fill->currentData().toBool();
        target.gradient = stroke_gradient_controls->value();
        break;
      }
      case LayerStyleEffectKind::InnerShadow: {
        if (!enabled && result.inner_shadows.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_inner_shadow(result, index);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(inner_shadow_blend->currentData().toInt());
        target.opacity = static_cast<float>(inner_shadow_opacity->value()) / 100.0F;
        target.angle_degrees = static_cast<float>(inner_shadow_angle->value());
        target.distance = static_cast<float>(inner_shadow_distance->value());
        target.size = static_cast<float>(inner_shadow_size->value());
        target.choke = static_cast<float>(inner_shadow_choke->value());
        target.color = RgbColor{static_cast<std::uint8_t>(inner_shadow_red->value()),
                                static_cast<std::uint8_t>(inner_shadow_green->value()),
                                static_cast<std::uint8_t>(inner_shadow_blue->value())};
        break;
      }
      case LayerStyleEffectKind::InnerGlow: {
        if (!enabled && result.inner_glows.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_inner_glow(result, index);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(inner_glow_blend->currentData().toInt());
        target.opacity = static_cast<float>(inner_glow_opacity->value()) / 100.0F;
        target.size = static_cast<float>(inner_glow_size->value());
        target.choke = static_cast<float>(inner_glow_choke->value());
        target.source = static_cast<LayerInnerGlowSource>(inner_glow_source->currentData().toInt());
        target.color = RgbColor{static_cast<std::uint8_t>(inner_glow_red->value()),
                                static_cast<std::uint8_t>(inner_glow_green->value()),
                                static_cast<std::uint8_t>(inner_glow_blue->value())};
        break;
      }
      case LayerStyleEffectKind::Satin: {
        if (!enabled && result.satins.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_satin(result, index);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(satin_blend->currentData().toInt());
        target.opacity = static_cast<float>(satin_opacity->value()) / 100.0F;
        target.angle_degrees = static_cast<float>(satin_angle->value());
        target.distance = static_cast<float>(satin_distance->value());
        target.size = static_cast<float>(satin_size->value());
        target.invert = satin_invert->isChecked();
        target.color = RgbColor{static_cast<std::uint8_t>(satin_red->value()),
                                static_cast<std::uint8_t>(satin_green->value()),
                                static_cast<std::uint8_t>(satin_blue->value())};
        break;
      }
      case LayerStyleEffectKind::ColorOverlay: {
        if (!enabled && result.color_overlays.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_color_overlay(result, index);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(color_overlay_blend->currentData().toInt());
        target.opacity = static_cast<float>(color_overlay_opacity->value()) / 100.0F;
        target.color = RgbColor{static_cast<std::uint8_t>(color_overlay_red->value()),
                                static_cast<std::uint8_t>(color_overlay_green->value()),
                                static_cast<std::uint8_t>(color_overlay_blue->value())};
        break;
      }
      case LayerStyleEffectKind::GradientOverlay: {
        if (!enabled && result.gradient_fills.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_gradient_fill(result, index);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(gradient_blend->currentData().toInt());
        target.opacity = static_cast<float>(gradient_opacity->value()) / 100.0F;
        target.gradient = gradient_controls->value();
        break;
      }
      case LayerStyleEffectKind::OuterGlow: {
        if (!enabled && result.outer_glows.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_outer_glow(result, index);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(outer_glow_blend->currentData().toInt());
        target.opacity = static_cast<float>(outer_glow_opacity->value()) / 100.0F;
        target.size = static_cast<float>(outer_glow_size->value());
        target.spread = static_cast<float>(outer_glow_spread->value());
        target.color = RgbColor{static_cast<std::uint8_t>(outer_glow_red->value()),
                                static_cast<std::uint8_t>(outer_glow_green->value()),
                                static_cast<std::uint8_t>(outer_glow_blue->value())};
        break;
      }
      case LayerStyleEffectKind::DropShadow: {
        if (!enabled && result.drop_shadows.size() <= static_cast<std::size_t>(index)) {
          return;
        }
        auto& target = ensure_drop_shadow(result, index);
        target.enabled = enabled;
        target.blend_mode = static_cast<BlendMode>(shadow_blend->currentData().toInt());
        target.opacity = static_cast<float>(shadow_opacity->value()) / 100.0F;
        target.angle_degrees = static_cast<float>(shadow_angle->value());
        target.distance = static_cast<float>(shadow_distance->value());
        target.size = static_cast<float>(shadow_size->value());
        target.spread = static_cast<float>(shadow_spread->value());
        target.color = RgbColor{static_cast<std::uint8_t>(shadow_red->value()),
                                static_cast<std::uint8_t>(shadow_green->value()),
                                static_cast<std::uint8_t>(shadow_blue->value())};
        break;
      }
      case LayerStyleEffectKind::None:
        break;
    }
  };

  bool loading_controls = false;
  bool rebuilding_categories = false;
  std::function<LayerStyleSettings(const QListWidgetItem*)> build_current_settings_for_item;
  std::function<void(bool)> emit_preview;
  std::function<void(const QListWidgetItem*)> load_controls_from_style;
  std::function<void(LayerStyleEffectKind, int)> rebuild_category_list;

  build_current_settings_for_item = [&](const QListWidgetItem* category) {
    LayerStyle result = style;
    result.effects_visible = show_effects->isChecked();
    result.layer_mask_hides_effects = mask_hides_effects->isChecked();
    apply_enabled_states(result);
    save_controls_to_style(result, category);
    // Accepting any style edit regenerates the complete native lfx2 descriptor.
    // Every modeled Satin is therefore written with the Linear contour, even
    // when a different effect page is active.
    for (auto& satin : result.satins) {
      satin.unsupported_contour_options = false;
    }
    return LayerStyleSettings{opacity->value(), static_cast<BlendMode>(blend->currentData().toInt()),
                              std::move(result), blend_if, replace_unsupported_blend_if};
  };
  auto build_current_settings = [&]() {
    return build_current_settings_for_item(categories->currentItem());
  };

  load_controls_from_style = [&](const QListWidgetItem* category) {
    loading_controls = true;
    controls->setCurrentIndex(static_cast<int>(item_page(category)));
    const auto kind = item_kind(category);
    const auto index = std::max(0, item_effect_index(category));
    remove_selected_instance->setEnabled(is_stackable_kind(kind) && vector_count_for_kind(style, kind) > 0U);

    switch (kind) {
      case LayerStyleEffectKind::BevelEmboss: {
        const auto value = style.bevels.size() > static_cast<std::size_t>(index) ? style.bevels[static_cast<std::size_t>(index)]
                                                                                 : default_bevel_emboss();
        bevel_size->setValue(static_cast<int>(std::round(value.size)));
        bevel_depth->setValue(static_cast<int>(std::round(value.depth * 100.0F)));
        bevel_angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
        bevel_altitude->setValue(static_cast<int>(std::round(value.altitude_degrees)));
        bevel_direction->setCurrentIndex(value.direction_up ? 0 : 1);
        set_combo_data(bevel_highlight_blend, static_cast<int>(value.highlight_blend_mode));
        bevel_highlight_color = value.highlight_color;
        bevel_highlight_opacity->setValue(static_cast<int>(std::round(value.highlight_opacity * 100.0F)));
        set_combo_data(bevel_shadow_blend, static_cast<int>(value.shadow_blend_mode));
        bevel_shadow_color = value.shadow_color;
        bevel_shadow_opacity->setValue(static_cast<int>(std::round(value.shadow_opacity * 100.0F)));
        update_bevel_color_previews();
        break;
      }
      case LayerStyleEffectKind::Stroke: {
        const auto value =
            style.strokes.size() > static_cast<std::size_t>(index) ? style.strokes[static_cast<std::size_t>(index)]
                                                                   : default_stroke();
        stroke_size->setValue(static_cast<int>(std::round(value.size)));
        set_combo_data(stroke_blend, static_cast<int>(value.blend_mode));
        stroke_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        stroke_red->setValue(value.color.red);
        stroke_green->setValue(value.color.green);
        stroke_blue->setValue(value.color.blue);
        set_combo_data(stroke_position, static_cast<int>(value.position));
        stroke_fill->setCurrentIndex(value.uses_gradient ? 1 : 0);
        stroke_gradient_controls->load(value.gradient);
        update_stroke_fill_visibility();
        break;
      }
      case LayerStyleEffectKind::InnerShadow: {
        const auto value = style.inner_shadows.size() > static_cast<std::size_t>(index)
                               ? style.inner_shadows[static_cast<std::size_t>(index)]
                               : default_inner_shadow();
        set_combo_data(inner_shadow_blend, static_cast<int>(value.blend_mode));
        inner_shadow_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        inner_shadow_angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
        inner_shadow_distance->setValue(static_cast<int>(std::round(value.distance)));
        inner_shadow_size->setValue(static_cast<int>(std::round(value.size)));
        inner_shadow_choke->setValue(static_cast<int>(std::round(value.choke)));
        inner_shadow_red->setValue(value.color.red);
        inner_shadow_green->setValue(value.color.green);
        inner_shadow_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::InnerGlow: {
        const auto value = style.inner_glows.size() > static_cast<std::size_t>(index)
                               ? style.inner_glows[static_cast<std::size_t>(index)]
                               : default_inner_glow();
        set_combo_data(inner_glow_blend, static_cast<int>(value.blend_mode));
        inner_glow_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        inner_glow_size->setValue(static_cast<int>(std::round(value.size)));
        inner_glow_choke->setValue(static_cast<int>(std::round(value.choke)));
        set_combo_data(inner_glow_source, static_cast<int>(value.source));
        inner_glow_red->setValue(value.color.red);
        inner_glow_green->setValue(value.color.green);
        inner_glow_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::Satin: {
        const auto value = style.satins.size() > static_cast<std::size_t>(index)
                               ? style.satins[static_cast<std::size_t>(index)]
                               : default_satin();
        set_combo_data(satin_blend, static_cast<int>(value.blend_mode));
        satin_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        satin_angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
        satin_distance->setValue(static_cast<int>(std::round(value.distance)));
        satin_size->setValue(static_cast<int>(std::round(value.size)));
        satin_invert->setChecked(value.invert);
        satin_red->setValue(value.color.red);
        satin_green->setValue(value.color.green);
        satin_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::ColorOverlay: {
        const auto value = style.color_overlays.size() > static_cast<std::size_t>(index)
                               ? style.color_overlays[static_cast<std::size_t>(index)]
                               : default_color_overlay();
        set_combo_data(color_overlay_blend, static_cast<int>(value.blend_mode));
        color_overlay_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        color_overlay_red->setValue(value.color.red);
        color_overlay_green->setValue(value.color.green);
        color_overlay_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::GradientOverlay: {
        auto value = style.gradient_fills.size() > static_cast<std::size_t>(index)
                         ? style.gradient_fills[static_cast<std::size_t>(index)]
                         : default_gradient_fill();
        if (value.gradient.color_stops.empty()) {
          value.gradient = default_layer_style_gradient();
        }
        set_combo_data(gradient_blend, static_cast<int>(value.blend_mode));
        gradient_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        gradient_controls->load(value.gradient);
        break;
      }
      case LayerStyleEffectKind::OuterGlow: {
        const auto value =
            style.outer_glows.size() > static_cast<std::size_t>(index) ? style.outer_glows[static_cast<std::size_t>(index)]
                                                                       : default_outer_glow();
        set_combo_data(outer_glow_blend, static_cast<int>(value.blend_mode));
        outer_glow_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        outer_glow_size->setValue(static_cast<int>(std::round(value.size)));
        outer_glow_spread->setValue(static_cast<int>(std::round(value.spread)));
        outer_glow_red->setValue(value.color.red);
        outer_glow_green->setValue(value.color.green);
        outer_glow_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::DropShadow: {
        const auto value = style.drop_shadows.size() > static_cast<std::size_t>(index)
                               ? style.drop_shadows[static_cast<std::size_t>(index)]
                               : default_drop_shadow();
        set_combo_data(shadow_blend, static_cast<int>(value.blend_mode));
        shadow_opacity->setValue(static_cast<int>(std::round(value.opacity * 100.0F)));
        shadow_angle->setValue(static_cast<int>(std::round(value.angle_degrees)));
        shadow_distance->setValue(static_cast<int>(std::round(value.distance)));
        shadow_size->setValue(static_cast<int>(std::round(value.size)));
        shadow_spread->setValue(static_cast<int>(std::round(value.spread)));
        shadow_red->setValue(value.color.red);
        shadow_green->setValue(value.color.green);
        shadow_blue->setValue(value.color.blue);
        break;
      }
      case LayerStyleEffectKind::None:
        break;
    }

    update_stroke_color_preview();
    update_color_overlay_color_preview();
    gradient_controls->update_previews();
    stroke_gradient_controls->update_previews();
    update_outer_glow_color_preview();
    update_inner_glow_color_preview();
    update_satin_color_preview();
    update_shadow_color_preview();
    update_inner_shadow_color_preview();
    loading_controls = false;
  };

  auto add_effect_instance = [&](LayerStyleEffectKind kind, int source_index) {
    auto duplicate = [source_index](auto& vector, auto make_default) {
      using Vector = std::decay_t<decltype(vector)>;
      using Value = typename Vector::value_type;
      Value value = make_default();
      if (!vector.empty()) {
        value = vector[static_cast<std::size_t>(std::clamp(source_index, 0, static_cast<int>(vector.size()) - 1))];
      }
      value.enabled = true;
      const auto insert_index = vector.empty() ? 0 : std::clamp(source_index + 1, 0, static_cast<int>(vector.size()));
      vector.insert(vector.begin() + insert_index, std::move(value));
      return insert_index;
    };
    switch (kind) {
      case LayerStyleEffectKind::Stroke:
        return duplicate(style.strokes, [] { return default_stroke(); });
      case LayerStyleEffectKind::InnerShadow:
        return duplicate(style.inner_shadows, [] { return default_inner_shadow(); });
      case LayerStyleEffectKind::InnerGlow:
        return duplicate(style.inner_glows, [] { return default_inner_glow(); });
      case LayerStyleEffectKind::Satin:
        return duplicate(style.satins, [] { return default_satin(); });
      case LayerStyleEffectKind::ColorOverlay:
        return duplicate(style.color_overlays, [] { return default_color_overlay(); });
      case LayerStyleEffectKind::GradientOverlay:
        return duplicate(style.gradient_fills, [] { return default_gradient_fill(); });
      case LayerStyleEffectKind::OuterGlow:
        return duplicate(style.outer_glows, [] { return default_outer_glow(); });
      case LayerStyleEffectKind::DropShadow:
        return duplicate(style.drop_shadows, [] { return default_drop_shadow(); });
      case LayerStyleEffectKind::BevelEmboss:
      case LayerStyleEffectKind::None:
        break;
    }
    return 0;
  };

  auto remove_effect_instance = [&](LayerStyleEffectKind kind, int index) {
    auto remove = [index](auto& vector) {
      if (index >= 0 && vector.size() > static_cast<std::size_t>(index)) {
        vector.erase(vector.begin() + index);
      }
      return vector.empty() ? 0 : std::min(index, static_cast<int>(vector.size()) - 1);
    };
    switch (kind) {
      case LayerStyleEffectKind::Stroke:
        return remove(style.strokes);
      case LayerStyleEffectKind::InnerShadow:
        return remove(style.inner_shadows);
      case LayerStyleEffectKind::InnerGlow:
        return remove(style.inner_glows);
      case LayerStyleEffectKind::Satin:
        return remove(style.satins);
      case LayerStyleEffectKind::ColorOverlay:
        return remove(style.color_overlays);
      case LayerStyleEffectKind::GradientOverlay:
        return remove(style.gradient_fills);
      case LayerStyleEffectKind::OuterGlow:
        return remove(style.outer_glows);
      case LayerStyleEffectKind::DropShadow:
        return remove(style.drop_shadows);
      case LayerStyleEffectKind::BevelEmboss:
      case LayerStyleEffectKind::None:
        break;
    }
    return 0;
  };

  auto category_check_object_name = [&](LayerStyleEffectKind kind, int index) {
    switch (kind) {
      case LayerStyleEffectKind::BevelEmboss:
        return indexed_object_name(QStringLiteral("layerStyleBevelEmbossCategoryCheck"), index);
      case LayerStyleEffectKind::Stroke:
        return indexed_object_name(QStringLiteral("layerStyleStrokeCategoryCheck"), index);
      case LayerStyleEffectKind::InnerShadow:
        return indexed_object_name(QStringLiteral("layerStyleInnerShadowCategoryCheck"), index);
      case LayerStyleEffectKind::InnerGlow:
        return indexed_object_name(QStringLiteral("layerStyleInnerGlowCategoryCheck"), index);
      case LayerStyleEffectKind::Satin:
        return indexed_object_name(QStringLiteral("layerStyleSatinCategoryCheck"), index);
      case LayerStyleEffectKind::ColorOverlay:
        return indexed_object_name(QStringLiteral("layerStyleColorOverlayCategoryCheck"), index);
      case LayerStyleEffectKind::GradientOverlay:
        return indexed_object_name(QStringLiteral("layerStyleGradientOverlayCategoryCheck"), index);
      case LayerStyleEffectKind::OuterGlow:
        return indexed_object_name(QStringLiteral("layerStyleOuterGlowCategoryCheck"), index);
      case LayerStyleEffectKind::DropShadow:
        return indexed_object_name(QStringLiteral("layerStyleDropShadowCategoryCheck"), index);
      case LayerStyleEffectKind::None:
        break;
    }
    return QString();
  };
  auto add_button_object_name = [&](LayerStyleEffectKind kind, int index) {
    switch (kind) {
      case LayerStyleEffectKind::Stroke:
        return indexed_object_name(QStringLiteral("layerStyleAddStrokeInstanceButton"), index);
      case LayerStyleEffectKind::InnerShadow:
        return indexed_object_name(QStringLiteral("layerStyleAddInnerShadowInstanceButton"), index);
      case LayerStyleEffectKind::InnerGlow:
        return indexed_object_name(QStringLiteral("layerStyleAddInnerGlowInstanceButton"), index);
      case LayerStyleEffectKind::Satin:
        return indexed_object_name(QStringLiteral("layerStyleAddSatinInstanceButton"), index);
      case LayerStyleEffectKind::ColorOverlay:
        return indexed_object_name(QStringLiteral("layerStyleAddColorOverlayInstanceButton"), index);
      case LayerStyleEffectKind::GradientOverlay:
        return indexed_object_name(QStringLiteral("layerStyleAddGradientOverlayInstanceButton"), index);
      case LayerStyleEffectKind::OuterGlow:
        return indexed_object_name(QStringLiteral("layerStyleAddOuterGlowInstanceButton"), index);
      case LayerStyleEffectKind::DropShadow:
        return indexed_object_name(QStringLiteral("layerStyleAddDropShadowInstanceButton"), index);
      case LayerStyleEffectKind::BevelEmboss:
      case LayerStyleEffectKind::None:
        break;
    }
    return QString();
  };
  auto add_button_tooltip = [](LayerStyleEffectKind kind) {
    switch (kind) {
      case LayerStyleEffectKind::Stroke:
        return QObject::tr("Add Stroke");
      case LayerStyleEffectKind::InnerShadow:
        return QObject::tr("Add Inner Shadow");
      case LayerStyleEffectKind::InnerGlow:
        return QObject::tr("Add Inner Glow");
      case LayerStyleEffectKind::Satin:
        return QObject::tr("Add Satin");
      case LayerStyleEffectKind::ColorOverlay:
        return QObject::tr("Add Color Overlay");
      case LayerStyleEffectKind::GradientOverlay:
        return QObject::tr("Add Gradient Overlay");
      case LayerStyleEffectKind::OuterGlow:
        return QObject::tr("Add Outer Glow");
      case LayerStyleEffectKind::DropShadow:
        return QObject::tr("Add Drop Shadow");
      case LayerStyleEffectKind::BevelEmboss:
      case LayerStyleEffectKind::None:
        break;
    }
    return QString();
  };

  // The row widgets are opaque and cover their QListWidget items completely, so the built-in
  // ::item:selected background would only peek out around the widget edges. Selection is
  // therefore painted on the row widgets themselves, like the layers panel does.
  auto restyle_category_rows = [categories] {
    for (int row = 0; row < categories->count(); ++row) {
      auto* item = categories->item(row);
      auto* row_widget = categories->itemWidget(item);
      if (row_widget == nullptr) {
        continue;
      }
      // The QLabel/QCheckBox backgrounds must be explicitly transparent: once the row's
      // stylesheet applies to them, they would otherwise fill with the inherited palette.
      row_widget->setStyleSheet(
          item->isSelected()
              ? QStringLiteral("QWidget#layerStyleCategoryRow { background: #2d4c6d; border: 1px solid #4f91ca; }"
                               "QWidget#layerStyleCategoryRow QLabel {"
                               " background: transparent; color: #ffffff; font-weight: 600; }"
                               "QWidget#layerStyleCategoryRow QCheckBox { background: transparent; }")
              : QStringLiteral("QWidget#layerStyleCategoryRow { background: #2b2b2b;"
                               " border: 0; border-bottom: 1px solid #3b3b3b; }"
                               "QWidget#layerStyleCategoryRow QLabel { background: transparent; color: #e6e6e6; }"
                               "QWidget#layerStyleCategoryRow QCheckBox { background: transparent; }"));
    }
  };

  rebuild_category_list = [&](LayerStyleEffectKind select_kind, int select_index) {
    rebuilding_categories = true;
    const QSignalBlocker blocker(categories);
    for (int row = 0; row < categories->count(); ++row) {
      auto* item = categories->item(row);
      auto* row_widget = categories->itemWidget(item);
      if (row_widget == nullptr) {
        continue;
      }
      categories->removeItemWidget(item);
      delete row_widget;
    }
    categories->clear();
    int selected_row = 0;

    auto install_category_widget = [&](QListWidgetItem* item, const QString& check_object_name) {
      auto* row = new QWidget(categories);
      row->setObjectName(QStringLiteral("layerStyleCategoryRow"));
      row->setAttribute(Qt::WA_StyledBackground, true);
      row->setMinimumHeight(30);
      auto* layout = new QHBoxLayout(row);
      layout->setContentsMargins(10, 0, 10, 0);
      layout->setSpacing(4);
      QCheckBox* check = nullptr;
      if (!check_object_name.isEmpty()) {
        // The checkbox holds only the indicator; the name lives in a separate label so
        // clicking the name selects the row without toggling the effect on/off.
        check = new QCheckBox(row);
        check->setObjectName(check_object_name);
        check->setChecked(item->checkState() == Qt::Checked);
        check->setMinimumHeight(24);
        layout->addWidget(check, 0, Qt::AlignVCenter);
      }
      auto* label = new QLabel(item->text(), row);
      label->setMinimumHeight(24);
      label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      layout->addWidget(label, 1, Qt::AlignVCenter);
      const auto kind = item_kind(item);
      if (is_stackable_kind(kind)) {
        auto* add_instance = new QPushButton(QStringLiteral("+"), row);
        add_instance->setObjectName(add_button_object_name(kind, item_effect_index(item)));
        add_instance->setToolTip(add_button_tooltip(kind));
        configure_compact_symbol_button(add_instance);
        layout->addWidget(add_instance, 0, Qt::AlignVCenter);
        QObject::connect(add_instance, &QPushButton::clicked, &dialog, [&, item, kind] {
          const auto pending_style = build_current_settings_for_item(categories->currentItem()).style;
          const auto source_index = item_effect_index(item);
          QTimer::singleShot(0, &dialog, [&, pending_style, kind, source_index] {
            style = pending_style;
            const auto new_index = add_effect_instance(kind, source_index);
            rebuild_category_list(kind, new_index);
            load_controls_from_style(categories->currentItem());
            emit_preview(true);
          });
        });
      }
      item->setSizeHint(QSize(0, 30));
      categories->setItemWidget(item, row);
      if (check != nullptr) {
        QObject::connect(check, &QCheckBox::toggled, &dialog, [categories, item](bool checked) {
          item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
          categories->setCurrentItem(item);
        });
      }
    };

    auto add_row = [&](const QString& text, LayerStyleCategoryPage page, LayerStyleEffectKind kind, int index,
                       bool checked, bool checkable) {
      auto* item = add_layer_style_category(categories, text, checkable, checked, page, kind, index);
      install_category_widget(item, checkable ? category_check_object_name(kind, index) : QString());
      if (kind == select_kind && index == select_index) {
        selected_row = categories->row(item);
      }
      return item;
    };

    add_row(QObject::tr("Blending Options"), LayerStyleCategoryPage::Blending, LayerStyleEffectKind::None, -1, true, false);
    add_row(QObject::tr("Bevel & Emboss"), LayerStyleCategoryPage::BevelEmboss,
            LayerStyleEffectKind::BevelEmboss, 0, !style.bevels.empty() && style.bevels.front().enabled, true);

    auto add_vector_rows = [&](const QString& text, LayerStyleCategoryPage page, LayerStyleEffectKind kind,
                               const auto& vector) {
      if (vector.empty()) {
        add_row(text, page, kind, 0, false, true);
        return;
      }
      for (std::size_t index = 0; index < vector.size(); ++index) {
        add_row(text, page, kind, static_cast<int>(index), vector[index].enabled, true);
      }
    };
    add_vector_rows(QObject::tr("Stroke"), LayerStyleCategoryPage::Stroke, LayerStyleEffectKind::Stroke, style.strokes);
    add_vector_rows(QObject::tr("Inner Shadow"), LayerStyleCategoryPage::InnerShadow,
                    LayerStyleEffectKind::InnerShadow, style.inner_shadows);
    add_vector_rows(QObject::tr("Inner Glow"), LayerStyleCategoryPage::InnerGlow, LayerStyleEffectKind::InnerGlow,
                    style.inner_glows);
    add_vector_rows(QObject::tr("Satin"), LayerStyleCategoryPage::Satin, LayerStyleEffectKind::Satin, style.satins);
    add_vector_rows(QObject::tr("Color Overlay"), LayerStyleCategoryPage::ColorOverlay,
                    LayerStyleEffectKind::ColorOverlay, style.color_overlays);
    add_vector_rows(QObject::tr("Gradient Overlay"), LayerStyleCategoryPage::GradientOverlay,
                    LayerStyleEffectKind::GradientOverlay, style.gradient_fills);
    add_vector_rows(QObject::tr("Outer Glow"), LayerStyleCategoryPage::OuterGlow, LayerStyleEffectKind::OuterGlow,
                    style.outer_glows);
    add_vector_rows(QObject::tr("Drop Shadow"), LayerStyleCategoryPage::DropShadow, LayerStyleEffectKind::DropShadow,
                    style.drop_shadows);

    categories->setCurrentRow(std::clamp(selected_row, 0, std::max(0, categories->count() - 1)));
    // Signals are blocked during the rebuild, so sync the row selection styling directly.
    restyle_category_rows();
    rebuilding_categories = false;
  };

  CoalescedLayerStylePreviewEmitter<LayerStyleSettings> preview_emitter(
      dialog, [&](const LayerStyleSettings& settings) {
        if (preview_changed) {
          preview_changed(settings);
        }
      });

  emit_preview = [&](bool immediate) {
    if (loading_controls || rebuilding_categories) {
      return;
    }
    update_stroke_color_preview();
    update_bevel_color_previews();
    update_color_overlay_color_preview();
    gradient_controls->update_previews();
    stroke_gradient_controls->update_previews();
    update_outer_glow_color_preview();
    update_inner_glow_color_preview();
    update_satin_color_preview();
    update_shadow_color_preview();
    update_inner_shadow_color_preview();
    auto settings = build_current_settings();
    style = settings.style;
    auto preview_settings = preview_check->isChecked() ? settings : original_settings;
    if (immediate) {
      preview_emitter.flush(std::move(preview_settings));
    } else {
      preview_emitter.schedule(std::move(preview_settings));
    }
  };

  const auto connect_blend_if_row = [&](BlendIfRowWidgets& row, bool this_layer) {
    auto* row_ptr = &row;
    row.editor->changed = [&, row_ptr, this_layer](BlendIfThresholds thresholds, bool immediate) {
      if (loading_blend_if_controls) {
        return;
      }
      auto& ranges = blend_if.channels[current_blend_if_channel_index()];
      (this_layer ? ranges.this_layer : ranges.underlying_layer) = thresholds;
      loading_blend_if_controls = true;
      set_blend_if_row(*row_ptr, thresholds);
      loading_blend_if_controls = false;
      emit_preview(immediate);
    };
    const auto update_from_spins = [&, row_ptr, this_layer] {
      if (loading_blend_if_controls) {
        return;
      }
      const BlendIfThresholds thresholds{
          static_cast<std::uint8_t>(row_ptr->black_low->value()),
          static_cast<std::uint8_t>(row_ptr->black_high->value()),
          static_cast<std::uint8_t>(row_ptr->white_low->value()),
          static_cast<std::uint8_t>(row_ptr->white_high->value()),
      };
      auto& ranges = blend_if.channels[current_blend_if_channel_index()];
      (this_layer ? ranges.this_layer : ranges.underlying_layer) = thresholds;
      loading_blend_if_controls = true;
      set_blend_if_row(*row_ptr, thresholds);
      loading_blend_if_controls = false;
      emit_preview(false);
    };
    for (auto* spin : {row.black_low, row.black_high, row.white_low, row.white_high}) {
      QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                       [update_from_spins](int) { update_from_spins(); });
    }
  };
  connect_blend_if_row(blend_if_this, true);
  connect_blend_if_row(blend_if_underlying, false);
  QObject::connect(blend_if_channel, &QComboBox::currentIndexChanged, &dialog,
                   [&](int) { load_blend_if_controls(); });
  QObject::connect(reset_blend_if_channel, &QPushButton::clicked, &dialog, [&] {
    blend_if.channels[current_blend_if_channel_index()] = {};
    load_blend_if_controls();
    emit_preview(true);
  });
  if (replace_blend_if_button != nullptr) {
    QObject::connect(replace_blend_if_button, &QPushButton::clicked, &dialog, [&] {
      replace_unsupported_blend_if = true;
      blend_if = {};
      blend_if_group->setEnabled(true);
      replace_blend_if_button->setEnabled(false);
      if (blend_if_unsupported_warning != nullptr) {
        blend_if_unsupported_warning->setText(
            QObject::tr("The preserved Photoshop Blend If payload will be replaced with editable RGB defaults "
                        "when you choose OK."));
      }
      load_blend_if_controls();
      emit_preview(true);
    });
  }
  gradient_controls->changed = [&emit_preview](bool immediate) { emit_preview(immediate); };
  stroke_gradient_controls->changed = [&emit_preview](bool immediate) { emit_preview(immediate); };
  QObject::connect(categories, &QListWidget::itemSelectionChanged, &dialog, restyle_category_rows);
  QObject::connect(categories, &QListWidget::currentItemChanged, &dialog,
                   [&](QListWidgetItem* current, QListWidgetItem* previous) {
                     if (loading_controls || rebuilding_categories) {
                       return;
                     }
                     if (previous != nullptr) {
                       style = build_current_settings_for_item(previous).style;
                     }
                     load_controls_from_style(current);
                     emit_preview(true);
                   });
  QObject::connect(categories, &QListWidget::itemChanged, &dialog, [&](QListWidgetItem* changed) {
    if (auto* widget = categories->itemWidget(changed); widget != nullptr) {
      if (auto* check = widget->findChild<QCheckBox*>(); check != nullptr) {
        const QSignalBlocker blocker(check);
        check->setChecked(changed->checkState() == Qt::Checked);
      }
    }
    emit_preview(true);
  });
  rebuild_category_list(LayerStyleEffectKind::None, -1);
  load_controls_from_style(categories->currentItem());
  QObject::connect(blend, &QComboBox::currentIndexChanged, &dialog, [&emit_preview](int) { emit_preview(true); });
  QObject::connect(opacity, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                   [&emit_preview](int) { emit_preview(false); });
  QObject::connect(preview_check, &QCheckBox::toggled, &dialog, [&emit_preview](bool) { emit_preview(true); });
  QObject::connect(show_effects, &QCheckBox::toggled, &dialog, [&emit_preview](bool) { emit_preview(true); });
  QObject::connect(mask_hides_effects, &QCheckBox::toggled, &dialog, [&emit_preview](bool) { emit_preview(true); });
  for (auto* spin : {bevel_size, bevel_depth, bevel_angle, bevel_altitude, bevel_highlight_opacity,
                     bevel_shadow_opacity, stroke_size, stroke_opacity, stroke_red, stroke_green, stroke_blue,
                     color_overlay_opacity, color_overlay_red, color_overlay_green, color_overlay_blue,
                     gradient_opacity, outer_glow_opacity, outer_glow_size, outer_glow_spread, outer_glow_red,
                     outer_glow_green, outer_glow_blue, inner_glow_opacity, inner_glow_size, inner_glow_choke,
                     inner_glow_red, inner_glow_green, inner_glow_blue, satin_opacity, satin_angle, satin_distance,
                     satin_size, satin_red, satin_green, satin_blue, shadow_opacity, shadow_angle, shadow_distance,
                     shadow_size, shadow_spread, shadow_red,
                     shadow_green, shadow_blue, inner_shadow_opacity, inner_shadow_angle, inner_shadow_distance,
                     inner_shadow_size, inner_shadow_choke, inner_shadow_red, inner_shadow_green, inner_shadow_blue}) {
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog,
                     [&emit_preview](int) { emit_preview(false); });
  }
  QObject::connect(bevel_direction, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(bevel_highlight_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(bevel_shadow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(color_overlay_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(outer_glow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(inner_glow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(inner_glow_source, &QComboBox::currentIndexChanged, &dialog,
                    [&emit_preview](int) { emit_preview(true); });
  QObject::connect(satin_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(satin_invert, &QCheckBox::toggled, &dialog,
                   [&emit_preview](bool) { emit_preview(true); });
  QObject::connect(shadow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(inner_shadow_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(stroke_position, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(stroke_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(stroke_fill, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    update_stroke_fill_visibility();
    emit_preview(true);
  });
  QObject::connect(gradient_blend, &QComboBox::currentIndexChanged, &dialog,
                   [&emit_preview](int) { emit_preview(true); });
  QObject::connect(stroke_color_preview, &QPushButton::clicked, &dialog, [&] {
    const auto chosen =
        request_patchy_color(&dialog, QColor(stroke_red->value(), stroke_green->value(), stroke_blue->value()),
                             QObject::tr("Choose Stroke Color"));
    if (!chosen.has_value()) {
      return;
    }
    stroke_red->setValue(chosen->red());
    stroke_green->setValue(chosen->green());
    stroke_blue->setValue(chosen->blue());
    emit_preview(true);
  });
  auto choose_bevel_color = [&](RgbColor& target, const QString& title) {
    const auto original = target;
    auto apply_color = [&](QColor color) {
      if (!color.isValid()) {
        return;
      }
      target = RgbColor{static_cast<std::uint8_t>(color.red()), static_cast<std::uint8_t>(color.green()),
                        static_cast<std::uint8_t>(color.blue())};
      update_bevel_color_previews();
    };
    const auto chosen = request_patchy_color(&dialog, QColor(original.red, original.green, original.blue), title,
                                             [&](QColor color) {
                                               apply_color(color);
                                               emit_preview(false);
                                             });
    if (!chosen.has_value()) {
      apply_color(QColor(original.red, original.green, original.blue));
      emit_preview(true);
      return;
    }
    apply_color(*chosen);
    emit_preview(true);
  };
  QObject::connect(bevel_highlight_color_button, &QPushButton::clicked, &dialog, [&] {
    choose_bevel_color(bevel_highlight_color, QObject::tr("Choose Highlight Color"));
  });
  QObject::connect(bevel_shadow_color_button, &QPushButton::clicked, &dialog, [&] {
    choose_bevel_color(bevel_shadow_color, QObject::tr("Choose Shadow Color"));
  });
  QObject::connect(color_overlay_pick_color, &QPushButton::clicked, &dialog, [&] {
    const auto chosen =
        request_patchy_color(&dialog, QColor(color_overlay_red->value(), color_overlay_green->value(), color_overlay_blue->value()),
                                QObject::tr("Choose Color Overlay Color"));
    if (!chosen.has_value()) {
      return;
    }
    color_overlay_red->setValue(chosen->red());
    color_overlay_green->setValue(chosen->green());
    color_overlay_blue->setValue(chosen->blue());
    emit_preview(true);
  });
  QObject::connect(outer_glow_color_preview, &QPushButton::clicked, &dialog, [&] {
    const auto chosen = request_patchy_color(
        &dialog, QColor(outer_glow_red->value(), outer_glow_green->value(), outer_glow_blue->value()),
        QObject::tr("Choose Outer Glow Color"));
    if (!chosen.has_value()) {
      return;
    }
    outer_glow_red->setValue(chosen->red());
    outer_glow_green->setValue(chosen->green());
    outer_glow_blue->setValue(chosen->blue());
    emit_preview(true);
  });
  QObject::connect(inner_glow_color_preview, &QPushButton::clicked, &dialog, [&] {
    const auto chosen = request_patchy_color(
        &dialog, QColor(inner_glow_red->value(), inner_glow_green->value(), inner_glow_blue->value()),
        QObject::tr("Choose Inner Glow Color"));
    if (!chosen.has_value()) {
      return;
    }
    inner_glow_red->setValue(chosen->red());
    inner_glow_green->setValue(chosen->green());
    inner_glow_blue->setValue(chosen->blue());
    emit_preview(true);
  });
  QObject::connect(satin_color_preview, &QPushButton::clicked, &dialog, [&] {
    const auto chosen =
        request_patchy_color(&dialog, QColor(satin_red->value(), satin_green->value(), satin_blue->value()),
                             QObject::tr("Choose Satin Color"));
    if (!chosen.has_value()) {
      return;
    }
    satin_red->setValue(chosen->red());
    satin_green->setValue(chosen->green());
    satin_blue->setValue(chosen->blue());
    emit_preview(true);
  });
  QObject::connect(shadow_color_preview, &QPushButton::clicked, &dialog, [&] {
    const auto chosen =
        request_patchy_color(&dialog, QColor(shadow_red->value(), shadow_green->value(), shadow_blue->value()),
                             QObject::tr("Choose Drop Shadow Color"));
    if (!chosen.has_value()) {
      return;
    }
    shadow_red->setValue(chosen->red());
    shadow_green->setValue(chosen->green());
    shadow_blue->setValue(chosen->blue());
    emit_preview(true);
  });
  QObject::connect(inner_shadow_color_preview, &QPushButton::clicked, &dialog, [&] {
    const auto chosen = request_patchy_color(
        &dialog, QColor(inner_shadow_red->value(), inner_shadow_green->value(), inner_shadow_blue->value()),
        QObject::tr("Choose Inner Shadow Color"));
    if (!chosen.has_value()) {
      return;
    }
    inner_shadow_red->setValue(chosen->red());
    inner_shadow_green->setValue(chosen->green());
    inner_shadow_blue->setValue(chosen->blue());
    emit_preview(true);
  });
  auto add_selected_instance = [&](LayerStyleEffectKind fallback_kind) {
    const auto* current = categories->currentItem();
    const auto kind = is_stackable_kind(item_kind(current)) ? item_kind(current) : fallback_kind;
    const auto source_index = item_effect_index(current) >= 0 ? item_effect_index(current) : 0;
    style = build_current_settings().style;
    const auto new_index = add_effect_instance(kind, source_index);
    rebuild_category_list(kind, new_index);
    load_controls_from_style(categories->currentItem());
    emit_preview(true);
  };
  auto remove_selected_stackable_instance = [&] {
    const auto* current = categories->currentItem();
    const auto kind = item_kind(current);
    if (!is_stackable_kind(kind)) {
      return;
    }
    style = build_current_settings().style;
    const auto next_index = remove_effect_instance(kind, item_effect_index(current));
    rebuild_category_list(kind, next_index);
    load_controls_from_style(categories->currentItem());
    emit_preview(true);
  };
  QObject::connect(add_inner_shadow, &QPushButton::clicked, &dialog, [&] {
    add_selected_instance(LayerStyleEffectKind::InnerShadow);
  });
  QObject::connect(remove_inner_shadow, &QPushButton::clicked, &dialog, remove_selected_stackable_instance);
  QObject::connect(add_inner_glow, &QPushButton::clicked, &dialog, [&] {
    add_selected_instance(LayerStyleEffectKind::InnerGlow);
  });
  QObject::connect(remove_inner_glow, &QPushButton::clicked, &dialog, remove_selected_stackable_instance);
  QObject::connect(remove_selected_instance, &QPushButton::clicked, &dialog, remove_selected_stackable_instance);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto* footer = new QHBoxLayout();
  footer->addWidget(preview_check);
  footer->addStretch(1);
  footer->addWidget(buttons);
  root->addLayout(footer);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  return build_current_settings();
}


}  // namespace patchy::ui
