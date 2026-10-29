#include "ui/brush_dynamics_popup.hpp"

#include "ui/action_icons.hpp"
#include "ui/app_settings.hpp"
#include "ui/brush_stroke_preview.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/brush_tool_window.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/measurement_units.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace patchy::ui {

namespace {

using Section = BrushDynamicsPanel::Section;

// The popup's size (GitHub issue 506): short enough to open under the options bar on a 768 px
// screen, and every page fits it without scrolling.
constexpr int kPopupWidth = 520;
constexpr int kPopupHeight = 600;
constexpr int kPreviewHeight = 64;

const char* const kSectionSettingsKey = "brushes/dynamicsSection";

// Percent fraction <-> spin value helpers (BrushDynamics stores 0..1 fractions).
[[nodiscard]] int percent_from_fraction(double fraction) {
  return std::clamp(static_cast<int>(std::lround(fraction * 100.0)), 0, 1000);
}

[[nodiscard]] double fraction_from_percent(int percent) {
  return static_cast<double>(percent) / 100.0;
}

[[nodiscard]] int normalized_angle_value(double degrees) {
  auto normalized = std::fmod(degrees, 360.0);
  if (normalized > 180.0) {
    normalized -= 360.0;
  } else if (normalized < -180.0) {
    normalized += 360.0;
  }
  return static_cast<int>(std::lround(normalized));
}

// True for the control sources that actually read the pen / fade (mirrors the core's gating;
// Off and GlobalDefault only decide global-preference precedence).
[[nodiscard]] bool control_has_source(patchy::BrushDynamicControl control) {
  switch (control) {
    case patchy::BrushDynamicControl::Fade:
    case patchy::BrushDynamicControl::PenPressure:
    case patchy::BrushDynamicControl::PenTilt:
    case patchy::BrushDynamicControl::PenRotation:
    case patchy::BrushDynamicControl::StylusWheel:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] patchy::BrushDynamicControl combo_control(const QComboBox& combo) {
  return static_cast<patchy::BrushDynamicControl>(
      combo.currentData().toInt());
}

void select_combo_control(QComboBox& combo, patchy::BrushDynamicControl control) {
  auto index = combo.findData(static_cast<int>(control));
  if (index < 0) {
    index = 0;  // sanitized upstream; first item is the slot's default
  }
  combo.setCurrentIndex(index);
}

// macOS: QMacStyle's Aqua layout spacings/margins are far roomier than the
// Windows metrics this dense panel was designed around. Pin Windows-like metrics
// there; other platforms keep their style defaults (Windows rendering must not move).
void compact_group_grid(QGridLayout* grid) {
#ifdef Q_OS_MACOS
  grid->setHorizontalSpacing(8);
  grid->setVerticalSpacing(6);
#else
  Q_UNUSED(grid);
#endif
}

// The core section a page edits; Tip Shape has none (its angle and roundness live beside
// BrushDynamics, not in it).
[[nodiscard]] patchy::BrushDynamicsSection core_section(Section section) {
  switch (section) {
    case Section::ShapeDynamics: return patchy::BrushDynamicsSection::ShapeDynamics;
    case Section::Scattering: return patchy::BrushDynamicsSection::Scattering;
    case Section::Transfer: return patchy::BrushDynamicsSection::Transfer;
    case Section::Texture: return patchy::BrushDynamicsSection::Texture;
    case Section::DualBrush: return patchy::BrushDynamicsSection::DualBrush;
    case Section::ColorDynamics: return patchy::BrushDynamicsSection::ColorDynamics;
    case Section::BrushEffects:
    case Section::TipShape: break;
  }
  return patchy::BrushDynamicsSection::BrushEffects;
}

[[nodiscard]] QString section_title(Section section) {
  switch (section) {
    case Section::TipShape: return BrushDynamicsPanel::tr("Tip Shape");
    case Section::ShapeDynamics: return BrushDynamicsPanel::tr("Shape Dynamics");
    case Section::Scattering: return BrushDynamicsPanel::tr("Scattering");
    case Section::Transfer: return BrushDynamicsPanel::tr("Transfer");
    case Section::Texture: return BrushDynamicsPanel::tr("Texture");
    case Section::DualBrush: return BrushDynamicsPanel::tr("Dual Brush");
    case Section::ColorDynamics: return BrushDynamicsPanel::tr("Color Dynamics");
    case Section::BrushEffects: return BrushDynamicsPanel::tr("Brush Effects");
  }
  return {};
}

[[nodiscard]] QString object_name_part(Section section) {
  switch (section) {
    case Section::TipShape: return QStringLiteral("TipShape");
    case Section::ShapeDynamics: return QStringLiteral("ShapeDynamics");
    case Section::Scattering: return QStringLiteral("Scattering");
    case Section::Transfer: return QStringLiteral("Transfer");
    case Section::Texture: return QStringLiteral("Texture");
    case Section::DualBrush: return QStringLiteral("DualBrush");
    case Section::ColorDynamics: return QStringLiteral("ColorDynamics");
    case Section::BrushEffects: return QStringLiteral("BrushEffects");
  }
  return {};
}

// The changed-from-default mark on a section tab.
class SectionChangedDot : public QWidget {
public:
  explicit SectionChangedDot(QWidget* parent) : QWidget(parent) {
    setFixedSize(10, 10);
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }

protected:
  void paintEvent(QPaintEvent* /*event*/) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme().brush_dynamics_changed_dot);
    painter.drawEllipse(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
  }
};

// A page's scroll area sizes to its whole page: QScrollArea::sizeHint caps at about 24 font
// heights, which would shrink the popup below its content. It only scrolls when the screen
// itself is shorter than the popup.
class SectionPageScroll : public QScrollArea {
public:
  using QScrollArea::QScrollArea;

  [[nodiscard]] QSize sizeHint() const override {
    const auto frame = 2 * frameWidth();
    const auto content = widget() != nullptr ? widget()->sizeHint() : QSize(0, 0);
    return content + QSize(frame, frame);
  }
  [[nodiscard]] QSize minimumSizeHint() const override { return {80, 80}; }
};

// True when two loaded models paint alike (the sidecar's own field set, plus the tip shape).
[[nodiscard]] bool same_brush_model(const patchy::BrushDynamics& a, double a_angle, double a_roundness,
                                    const patchy::BrushDynamics& b, double b_angle, double b_roundness) {
  return a_angle == b_angle && a_roundness == b_roundness &&
         brush_dynamics_to_json(a) == brush_dynamics_to_json(b);
}

}  // namespace

QString BrushDynamicsPanel::section_id(Section section) {
  switch (section) {
    case Section::TipShape: return QStringLiteral("tipShape");
    case Section::ShapeDynamics: return QStringLiteral("shapeDynamics");
    case Section::Scattering: return QStringLiteral("scattering");
    case Section::Transfer: return QStringLiteral("transfer");
    case Section::Texture: return QStringLiteral("texture");
    case Section::DualBrush: return QStringLiteral("dualBrush");
    case Section::ColorDynamics: return QStringLiteral("colorDynamics");
    case Section::BrushEffects: return QStringLiteral("brushEffects");
  }
  return {};
}

BrushDynamicsPanel::BrushDynamicsPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("brushDynamicsPanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);

  preview_ = new BrushStrokePreview(this);
  preview_->setObjectName(QStringLiteral("dynamicsStrokePreview"));
  preview_->setMinimumSize(160, kPreviewHeight);
  preview_->setFixedHeight(kPreviewHeight);
  preview_->setToolTip(tr("Preview of a pen stroke with these settings"));
  preview_->setVisible(false);
  layout->addWidget(preview_);

  auto* sections_row = new QHBoxLayout();
  sections_row->setSpacing(0);
  layout->addLayout(sections_row, 1);

  auto* tab_column = new QFrame(this);
  tab_column->setObjectName(QStringLiteral("dynamicsSectionTabs"));
  auto* tab_layout = new QVBoxLayout(tab_column);
  tab_layout->setContentsMargins(0, 4, 0, 4);
  tab_layout->setSpacing(0);
  sections_row->addWidget(tab_column);
  auto* tab_group = new QButtonGroup(this);
  tab_group->setExclusive(true);

  stack_ = new QStackedWidget(this);
  stack_->setObjectName(QStringLiteral("dynamicsSectionStack"));
  sections_row->addWidget(stack_, 1);

  // One tab row plus its page. The page holds a header (title + Reset Section) over `body`,
  // which takes the rows; `switchable` sections get their on/off box in the tab.
  const auto add_section = [&](Section section, QCheckBox* enabled_check) -> QGridLayout* {
    auto& tab = tabs_[static_cast<int>(section)];
    const auto name = object_name_part(section);
    auto* row = new QWidget(tab_column);
    row->setObjectName(QStringLiteral("dynamicsTab%1Row").arg(name));
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(6, 0, 4, 0);
    row_layout->setSpacing(2);
    if (enabled_check != nullptr) {
      enabled_check->setParent(row);
      row_layout->addWidget(enabled_check);
    } else {
      // A hidden stand-in keeps the name in line with the switchable tabs' names.
      auto* placeholder = new QCheckBox(row);
      auto policy = placeholder->sizePolicy();
      policy.setRetainSizeWhenHidden(true);
      placeholder->setSizePolicy(policy);
      placeholder->setFocusPolicy(Qt::NoFocus);
      placeholder->setVisible(false);
      row_layout->addWidget(placeholder);
    }
    tab.enabled = enabled_check;
    tab.button = new QPushButton(row);
    tab.button->setObjectName(QStringLiteral("dynamicsTab%1").arg(name));
    tab.button->setCheckable(true);
    tab.button->setFlat(true);
    tab.button->setFocusPolicy(Qt::TabFocus);
    tab.button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    tab.button->setMinimumHeight(fontMetrics().height() + 10);
    tab_group->addButton(tab.button, static_cast<int>(section));
    row_layout->addWidget(tab.button, 1);
    tab.dot = new SectionChangedDot(row);
    tab.dot->setObjectName(QStringLiteral("dynamicsTab%1Dot").arg(name));
    row_layout->addWidget(tab.dot);
    // Keep the column from reflowing when a dot appears.
    auto dot_policy = tab.dot->sizePolicy();
    dot_policy.setRetainSizeWhenHidden(true);
    tab.dot->setSizePolicy(dot_policy);
    tab.dot->setVisible(false);
    tab_layout->addWidget(row);

    auto* scroll = new SectionPageScroll(stack_);
    scroll->setObjectName(QStringLiteral("dynamics%1Page").arg(name));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page = new QWidget(scroll);
    page->setObjectName(QStringLiteral("dynamicsSectionPage"));
    auto* page_layout = new QVBoxLayout(page);
    page_layout->setContentsMargins(10, 6, 8, 6);
    page_layout->setSpacing(8);
    auto* header = new QHBoxLayout();
    tab.title = new QLabel(page);
    tab.title->setObjectName(QStringLiteral("dynamicsSectionTitle"));
    header->addWidget(tab.title);
    header->addStretch(1);
    tab.reset = new QPushButton(tr("Reset Section"), page);
    tab.reset->setObjectName(QStringLiteral("dynamicsResetSectionButton"));
    tab.reset->setToolTip(tr("Reset this section's settings to their defaults"));
    header->addWidget(tab.reset);
    page_layout->addLayout(header);
    tab.body = new QWidget(page);
    tab.body->setObjectName(QStringLiteral("dynamicsSectionBody"));
    auto* grid = new QGridLayout(tab.body);
    grid->setContentsMargins(0, 0, 0, 0);
    compact_group_grid(grid);
    grid->setColumnStretch(1, 1);
    page_layout->addWidget(tab.body);
    page_layout->addStretch(1);
    scroll->setWidget(page);
    stack_->addWidget(scroll);
    connect(tab.reset, &QPushButton::clicked, this, [this, section] { reset_section(section); });
    return grid;
  };

  const auto add_percent_row = [this](QGridLayout* grid, int row, const QString& label,
                                      const QString& object_name, int maximum) -> QSpinBox* {
    auto* text = new QLabel(label, this);
    auto* slider = new QSlider(Qt::Horizontal, this);
    slider->setObjectName(object_name + QStringLiteral("Slider"));
    slider->setRange(0, maximum);
    slider->setMinimumWidth(60);
    auto* spin = new QSpinBox(this);
    spin->setObjectName(object_name);
    spin->setRange(0, maximum);
    spin->setSuffix(percent_suffix());
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    grid->addWidget(text, row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(spin, row, 2);
    rows_.insert(spin, {text, slider});
    return spin;
  };

  // A "Control:" combo plus its fade-steps spin (shown only while the combo says Fade), on the
  // row right under the jitter it drives. The items carry the enum in their data so display
  // order stays decoupled from the enum values; with_global lists "Use Global Pen Setting"
  // first (size/roundness/opacity only).
  const auto add_control_row = [this](QGridLayout* grid, int row, const QString& label,
                                      const QString& combo_name, const QString& fade_name,
                                      bool with_global) -> std::pair<QComboBox*, QSpinBox*> {
    auto* text = new QLabel(label, this);
    text->setIndent(12);
    grid->addWidget(text, row, 0);
    auto* combo = new QComboBox(this);
    combo->setObjectName(combo_name);
    if (with_global) {
      combo->addItem(tr("Use Global Pen Setting"),
                     static_cast<int>(patchy::BrushDynamicControl::GlobalDefault));
    }
    combo->addItem(tr("Off"), static_cast<int>(patchy::BrushDynamicControl::Off));
    combo->addItem(tr("Fade"), static_cast<int>(patchy::BrushDynamicControl::Fade));
    combo->addItem(tr("Pen Pressure"), static_cast<int>(patchy::BrushDynamicControl::PenPressure));
    combo->addItem(tr("Pen Tilt"), static_cast<int>(patchy::BrushDynamicControl::PenTilt));
    combo->addItem(tr("Stylus Wheel"), static_cast<int>(patchy::BrushDynamicControl::StylusWheel));
    combo->addItem(tr("Pen Rotation"), static_cast<int>(patchy::BrushDynamicControl::PenRotation));
    auto* fade_spin = new QSpinBox(this);
    fade_spin->setObjectName(fade_name);
    fade_spin->setRange(1, 9999);
    fade_spin->setValue(25);
    fade_spin->setToolTip(tr("Spacing steps to fade over"));
    fade_spin->setVisible(false);
    auto* row_layout = new QHBoxLayout();
    row_layout->addWidget(combo, 1);
    row_layout->addWidget(fade_spin);
    grid->addLayout(row_layout, row, 1, 1, 2);
    rows_.insert(combo, {text, nullptr});
    return {combo, fade_spin};
  };

  const auto make_section_check = [this](const QString& object_name) {
    auto* check = new QCheckBox(this);
    check->setObjectName(object_name);
    return check;
  };
  shape_dynamics_enabled_check_ =
      make_section_check(QStringLiteral("dynamicsShapeDynamicsEnabledCheck"));
  shape_dynamics_enabled_check_->setChecked(true);
  scattering_enabled_check_ = make_section_check(QStringLiteral("dynamicsScatteringEnabledCheck"));
  scattering_enabled_check_->setChecked(true);
  transfer_enabled_check_ = make_section_check(QStringLiteral("dynamicsTransferEnabledCheck"));
  transfer_enabled_check_->setChecked(true);
  texture_enabled_check_ = make_section_check(QStringLiteral("dynamicsTextureEnabledCheck"));
  dual_brush_enabled_check_ = make_section_check(QStringLiteral("dynamicsDualBrushEnabledCheck"));
  color_dynamics_enabled_check_ = make_section_check(QStringLiteral("dynamicsColorEnabledCheck"));

  // Tip Shape: the static Photoshop "Brush Tip Shape" angle/roundness.
  auto* shape_grid = add_section(Section::TipShape, nullptr);
  shape_grid->addWidget(new QLabel(tr("Angle:"), this), 0, 0);
  base_angle_spin_ = new QSpinBox(this);
  base_angle_spin_->setObjectName(QStringLiteral("dynamicsBaseAngleSpin"));
  base_angle_spin_->setRange(-180, 180);
  base_angle_spin_->setSuffix(degree_suffix());
  shape_grid->addWidget(base_angle_spin_, 0, 1, Qt::AlignLeft);
  shape_grid->addWidget(new QLabel(tr("Roundness:"), this), 1, 0);
  base_roundness_spin_ = new QSpinBox(this);
  base_roundness_spin_->setObjectName(QStringLiteral("dynamicsBaseRoundnessSpin"));
  base_roundness_spin_->setRange(1, 100);
  base_roundness_spin_->setSuffix(percent_suffix());
  base_roundness_spin_->setValue(100);
  shape_grid->addWidget(base_roundness_spin_, 1, 1, Qt::AlignLeft);

  // Shape Dynamics. Each jitter's Control sits right under it.
  auto* dynamics_grid = add_section(Section::ShapeDynamics, shape_dynamics_enabled_check_);
  size_jitter_spin_ =
      add_percent_row(dynamics_grid, 0, tr("Size Jitter:"), QStringLiteral("dynamicsSizeJitterSpin"), 100);
  std::tie(size_control_combo_, size_fade_steps_spin_) =
      add_control_row(dynamics_grid, 1, tr("Size Control:"), QStringLiteral("dynamicsSizeControlCombo"),
                      QStringLiteral("dynamicsSizeFadeStepsSpin"), true);
  minimum_diameter_spin_ = add_percent_row(dynamics_grid, 2, tr("Minimum Diameter:"),
                                           QStringLiteral("dynamicsMinimumDiameterSpin"), 100);
  angle_jitter_spin_ =
      add_percent_row(dynamics_grid, 3, tr("Angle Jitter:"), QStringLiteral("dynamicsAngleJitterSpin"), 100);
  {
    auto* text = new QLabel(tr("Angle Control:"), this);
    text->setIndent(12);
    dynamics_grid->addWidget(text, 4, 0);
    // The angle combo predates the data-mapped ones: its item indices equal the enum values
    // (tests and set_values rely on that), so new sources append in enum order.
    angle_control_combo_ = new QComboBox(this);
    angle_control_combo_->setObjectName(QStringLiteral("dynamicsAngleControlCombo"));
    angle_control_combo_->addItem(tr("Off"));
    angle_control_combo_->addItem(tr("Fade"));
    angle_control_combo_->addItem(tr("Pen Pressure"));
    angle_control_combo_->addItem(tr("Pen Tilt"));
    angle_control_combo_->addItem(tr("Pen Rotation"));
    angle_control_combo_->addItem(tr("Initial Direction"));
    angle_control_combo_->addItem(tr("Direction"));
    angle_control_combo_->addItem(tr("Stylus Wheel"));
    fade_steps_spin_ = new QSpinBox(this);
    fade_steps_spin_->setObjectName(QStringLiteral("dynamicsFadeStepsSpin"));
    fade_steps_spin_->setRange(1, 9999);
    fade_steps_spin_->setValue(25);
    fade_steps_spin_->setToolTip(tr("Spacing steps to fade over"));
    fade_steps_spin_->setVisible(false);
    auto* control_row = new QHBoxLayout();
    control_row->addWidget(angle_control_combo_, 1);
    control_row->addWidget(fade_steps_spin_);
    dynamics_grid->addLayout(control_row, 4, 1, 1, 2);
    rows_.insert(angle_control_combo_, {text, nullptr});
  }
  roundness_jitter_spin_ = add_percent_row(dynamics_grid, 5, tr("Roundness Jitter:"),
                                           QStringLiteral("dynamicsRoundnessJitterSpin"), 100);
  std::tie(roundness_control_combo_, roundness_fade_steps_spin_) = add_control_row(
      dynamics_grid, 6, tr("Roundness Control:"), QStringLiteral("dynamicsRoundnessControlCombo"),
      QStringLiteral("dynamicsRoundnessFadeStepsSpin"), true);
  minimum_roundness_spin_ = add_percent_row(dynamics_grid, 7, tr("Minimum Roundness:"),
                                            QStringLiteral("dynamicsMinimumRoundnessSpin"), 100);
  minimum_roundness_spin_->setValue(25);
  auto* flips_row = new QHBoxLayout();
  flip_x_check_ = new QCheckBox(tr("Flip X Jitter"), this);
  flip_x_check_->setObjectName(QStringLiteral("dynamicsFlipXCheck"));
  flip_y_check_ = new QCheckBox(tr("Flip Y Jitter"), this);
  flip_y_check_->setObjectName(QStringLiteral("dynamicsFlipYCheck"));
  flips_row->addWidget(flip_x_check_);
  flips_row->addWidget(flip_y_check_);
  flips_row->addStretch(1);
  dynamics_grid->addLayout(flips_row, 8, 0, 1, 3);

  // Scattering.
  auto* scatter_grid = add_section(Section::Scattering, scattering_enabled_check_);
  scatter_spin_ = add_percent_row(scatter_grid, 0, tr("Scatter:"), QStringLiteral("dynamicsScatterSpin"), 1000);
  std::tie(scatter_control_combo_, scatter_fade_steps_spin_) =
      add_control_row(scatter_grid, 1, tr("Scatter Control:"), QStringLiteral("dynamicsScatterControlCombo"),
                      QStringLiteral("dynamicsScatterFadeStepsSpin"), false);
  both_axes_check_ = new QCheckBox(tr("Both Axes"), this);
  both_axes_check_->setObjectName(QStringLiteral("dynamicsBothAxesCheck"));
  scatter_grid->addWidget(both_axes_check_, 2, 0, 1, 2);
  auto* count_label = new QLabel(tr("Count:"), this);
  scatter_grid->addWidget(count_label, 3, 0);
  count_spin_ = new QSpinBox(this);
  count_spin_->setObjectName(QStringLiteral("dynamicsCountSpin"));
  count_spin_->setRange(1, 16);
  scatter_grid->addWidget(count_spin_, 3, 1, Qt::AlignLeft);
  count_jitter_spin_ =
      add_percent_row(scatter_grid, 4, tr("Count Jitter:"), QStringLiteral("dynamicsCountJitterSpin"), 100);
  std::tie(count_control_combo_, count_fade_steps_spin_) =
      add_control_row(scatter_grid, 5, tr("Count Control:"), QStringLiteral("dynamicsCountControlCombo"),
                      QStringLiteral("dynamicsCountFadeStepsSpin"), false);

  // Transfer (opacity and flow).
  auto* transfer_grid = add_section(Section::Transfer, transfer_enabled_check_);
  opacity_jitter_spin_ = add_percent_row(transfer_grid, 0, tr("Opacity Jitter:"),
                                         QStringLiteral("dynamicsOpacityJitterSpin"), 100);
  std::tie(opacity_control_combo_, opacity_fade_steps_spin_) = add_control_row(
      transfer_grid, 1, tr("Opacity Control:"), QStringLiteral("dynamicsOpacityControlCombo"),
      QStringLiteral("dynamicsOpacityFadeStepsSpin"), true);
  minimum_opacity_spin_ = add_percent_row(transfer_grid, 2, tr("Minimum Opacity:"),
                                          QStringLiteral("dynamicsMinimumOpacitySpin"), 100);
  flow_jitter_spin_ = add_percent_row(transfer_grid, 3, tr("Flow Jitter:"),
                                      QStringLiteral("dynamicsFlowJitterSpin"), 100);
  std::tie(flow_control_combo_, flow_fade_steps_spin_) = add_control_row(
      transfer_grid, 4, tr("Flow Control:"), QStringLiteral("dynamicsFlowControlCombo"),
      QStringLiteral("dynamicsFlowFadeStepsSpin"), false);
  minimum_flow_spin_ = add_percent_row(transfer_grid, 5, tr("Minimum Flow:"),
                                       QStringLiteral("dynamicsMinimumFlowSpin"), 100);

  // Texture. The generated grain is intentionally static: scale/depth/invert are saved brush
  // settings, never pen-input controls.
  auto* texture_grid = add_section(Section::Texture, texture_enabled_check_);
  texture_grid->addWidget(new QLabel(tr("Grain:"), this), 0, 0);
  texture_style_combo_ = new QComboBox(this);
  texture_style_combo_->setObjectName(QStringLiteral("dynamicsTextureStyleCombo"));
  texture_style_combo_->addItem(tr("Fine Grain"), static_cast<int>(patchy::BrushTextureStyle::FineGrain));
  texture_style_combo_->addItem(tr("Canvas"), static_cast<int>(patchy::BrushTextureStyle::Canvas));
  texture_style_combo_->addItem(tr("Speckle"), static_cast<int>(patchy::BrushTextureStyle::Speckle));
  texture_grid->addWidget(texture_style_combo_, 0, 1, 1, 2);
  texture_scale_spin_ = add_percent_row(texture_grid, 1, tr("Scale:"),
                                        QStringLiteral("dynamicsTextureScaleSpin"), 1000);
  texture_scale_spin_->setMinimum(1);
  texture_depth_spin_ = add_percent_row(texture_grid, 2, tr("Depth:"),
                                        QStringLiteral("dynamicsTextureDepthSpin"), 100);
  texture_invert_check_ = new QCheckBox(tr("Invert Texture"), this);
  texture_invert_check_->setObjectName(QStringLiteral("dynamicsTextureInvertCheck"));
  texture_grid->addWidget(texture_invert_check_, 3, 0, 1, 2);

  // Dual Brush: one fixed secondary computed mask.
  auto* dual_grid = add_section(Section::DualBrush, dual_brush_enabled_check_);
  dual_brush_size_spin_ = add_percent_row(dual_grid, 0, tr("Secondary Size:"),
                                          QStringLiteral("dynamicsDualBrushSizeSpin"), 400);
  dual_brush_size_spin_->setMinimum(5);
  dual_brush_hardness_spin_ = add_percent_row(dual_grid, 1, tr("Secondary Hardness:"),
                                              QStringLiteral("dynamicsDualBrushHardnessSpin"), 100);
  dual_brush_spacing_spin_ = add_percent_row(dual_grid, 2, tr("Secondary Spacing:"),
                                             QStringLiteral("dynamicsDualBrushSpacingSpin"), 1000);
  dual_brush_spacing_spin_->setMinimum(10);

  // Color Dynamics.
  auto* color_grid = add_section(Section::ColorDynamics, color_dynamics_enabled_check_);
  foreground_background_jitter_spin_ = add_percent_row(
      color_grid, 0, tr("Foreground/Background Jitter:"),
      QStringLiteral("dynamicsColorForegroundBackgroundJitterSpin"), 100);
  // The longest label in the popup wraps rather than widening every page.
  rows_.value(foreground_background_jitter_spin_).first->setWordWrap(true);
  std::tie(color_control_combo_, color_fade_steps_spin_) = add_control_row(
      color_grid, 1, tr("Color Control:"), QStringLiteral("dynamicsColorControlCombo"),
      QStringLiteral("dynamicsColorFadeStepsSpin"), false);
  hue_jitter_spin_ = add_percent_row(color_grid, 2, tr("Hue Jitter:"),
                                     QStringLiteral("dynamicsColorHueJitterSpin"), 100);
  saturation_jitter_spin_ = add_percent_row(color_grid, 3, tr("Saturation Jitter:"),
                                            QStringLiteral("dynamicsColorSaturationJitterSpin"), 100);
  brightness_jitter_spin_ = add_percent_row(color_grid, 4, tr("Brightness Jitter:"),
                                            QStringLiteral("dynamicsColorBrightnessJitterSpin"), 100);
  color_grid->addWidget(new QLabel(tr("Purity:"), this), 5, 0);
  purity_spin_ = new QSpinBox(this);
  purity_spin_->setObjectName(QStringLiteral("dynamicsColorPuritySpin"));
  purity_spin_->setRange(-100, 100);
  purity_spin_->setSuffix(percent_suffix());
  color_grid->addWidget(purity_spin_, 5, 1, Qt::AlignLeft);
  color_per_tip_check_ = new QCheckBox(tr("Apply Per Tip"), this);
  color_per_tip_check_->setObjectName(QStringLiteral("dynamicsColorPerTipCheck"));
  color_per_tip_check_->setChecked(true);
  color_grid->addWidget(color_per_tip_check_, 6, 0, 1, 2);

  // Brush Effects: Wet Edges keeps its own box on the page, so the tab has no switch.
  auto* effects_grid = add_section(Section::BrushEffects, nullptr);
  wet_edges_check_ = new QCheckBox(tr("Wet Edges"), this);
  wet_edges_check_->setObjectName(QStringLiteral("dynamicsWetEdgesCheck"));
  wet_edges_check_->setToolTip(
      tr("Builds paint along stroke edges for a watercolor wash. It does not smear canvas "
         "colors; use Smudge for that."));
  effects_grid->addWidget(wet_edges_check_, 0, 0, 1, 3);
  tab_layout->addStretch(1);

  auto* footer = new QHBoxLayout();
  auto* reset_button = new QPushButton(tr("Reset All"), this);
  reset_button->setObjectName(QStringLiteral("dynamicsResetButton"));
  reset_button->setToolTip(tr("Reset the tip shape and all dynamics to defaults"));
  footer->addWidget(reset_button);
  footer->addStretch(1);
  auto* hint = new QLabel(tr("Changes apply as you edit."), this);
  hint->setObjectName(QStringLiteral("dynamicsApplyHint"));
  footer->addWidget(hint);
  layout->addLayout(footer);
  retitle_tabs();

  const auto emit_edited = [this] {
    if (!loading_) {
      after_values_changed();
      emit edited();
    }
  };
  for (auto* spin :
       {base_angle_spin_, base_roundness_spin_, size_jitter_spin_, minimum_diameter_spin_,
        size_fade_steps_spin_, angle_jitter_spin_, fade_steps_spin_, roundness_jitter_spin_,
        minimum_roundness_spin_, roundness_fade_steps_spin_, scatter_spin_, scatter_fade_steps_spin_,
        count_spin_, count_jitter_spin_, count_fade_steps_spin_, opacity_jitter_spin_,
        minimum_opacity_spin_, opacity_fade_steps_spin_, flow_jitter_spin_, minimum_flow_spin_,
        flow_fade_steps_spin_, texture_scale_spin_, texture_depth_spin_, dual_brush_size_spin_,
        dual_brush_hardness_spin_, dual_brush_spacing_spin_,
        foreground_background_jitter_spin_, color_fade_steps_spin_, hue_jitter_spin_,
        saturation_jitter_spin_, brightness_jitter_spin_, purity_spin_}) {
    connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, emit_edited);
  }
  for (auto* combo : {angle_control_combo_, size_control_combo_, roundness_control_combo_,
                      scatter_control_combo_, count_control_combo_, opacity_control_combo_,
                      flow_control_combo_, texture_style_combo_, color_control_combo_}) {
    connect(combo, &QComboBox::currentIndexChanged, this, emit_edited);
  }
  for (auto* check : {flip_x_check_, flip_y_check_, both_axes_check_, shape_dynamics_enabled_check_,
                      scattering_enabled_check_, transfer_enabled_check_, texture_enabled_check_,
                      texture_invert_check_, dual_brush_enabled_check_,
                      color_dynamics_enabled_check_, color_per_tip_check_, wet_edges_check_}) {
    connect(check, &QCheckBox::toggled, this, emit_edited);
  }
  connect(reset_button, &QPushButton::clicked, this, &BrushDynamicsPanel::reset_to_defaults);

  connect(tab_group, &QButtonGroup::idClicked, this, [this](int id) {
    select_section(static_cast<Section>(id));
  });
  // The last tab used comes back on the next opening (stored by id, so a section added later
  // never shifts it).
  auto initial = Section::TipShape;
  const auto stored = app_settings().value(QLatin1String(kSectionSettingsKey)).toString();
  for (int index = 0; index < kSectionCount; ++index) {
    if (section_id(static_cast<Section>(index)) == stored) {
      initial = static_cast<Section>(index);
    }
  }
  stack_->setCurrentIndex(static_cast<int>(initial));
  tabs_[static_cast<int>(initial)].button->setChecked(true);
  after_values_changed();

  // The tab column and page chrome; the - / + spin buttons (see the sub-control gotcha in
  // dialog_utils) go last and apply after all children exist.
  set_themed_style(*this, ThemedQss(QStringLiteral(R"(
    QFrame#dynamicsSectionTabs {
      background: @dialog_tab_bg;
      border: 1px solid @dialog_tab_border;
    }
    QFrame#dynamicsSectionTabs QWidget {
      background: transparent;
    }
    QFrame#dynamicsSectionTabs QPushButton {
      text-align: left;
      padding: 4px 4px;
      border: 0;
      border-radius: 0;
      color: @dialog_tab_text;
      background: transparent;
    }
    QFrame#dynamicsSectionTabs QPushButton:hover:!checked {
      background: @dialog_tab_hover_bg;
    }
    QFrame#dynamicsSectionTabs QPushButton:checked {
      background: @dialog_tab_selected_bg;
      color: @text_on_raised;
    }
    QStackedWidget#dynamicsSectionStack {
      border: 1px solid @dialog_tab_border;
      border-left: 0;
      background: @dialog_tab_bg;
    }
    QStackedWidget#dynamicsSectionStack QScrollArea,
    QStackedWidget#dynamicsSectionStack QWidget#qt_scrollarea_viewport,
    QStackedWidget#dynamicsSectionStack QWidget#dynamicsSectionPage,
    QStackedWidget#dynamicsSectionStack QWidget#dynamicsSectionBody {
      background: transparent;
    }
    QLabel#dynamicsSectionTitle {
      font-weight: bold;
    }
    QLabel#dynamicsApplyHint {
      color: @hint_text;
    }
  )")));
  append_themed_style(*this, dialog_spinbox_button_style());
  // The popup is a Qt::Popup frame, not a dialog, so the exec_dialog hook never sees
  // these rows: pair every "Label", slider, spin row here (GitHub issue 46).
  install_scrub_labels_in(this);
}

void BrushDynamicsPanel::retitle_tabs() {
  for (int index = 0; index < kSectionCount; ++index) {
    const auto section = static_cast<Section>(index);
    auto& tab = tabs_[index];
    const auto title = section_title(section);
    tab.button->setText(title);
    tab.title->setText(title);
    tab.dot->setToolTip(tr("Changed from the defaults"));
    if (tab.enabled != nullptr) {
      tab.enabled->setToolTip(tr("Turn %1 on or off. Its settings are kept while it is off.").arg(title));
      tab.enabled->setAccessibleName(tr("Use %1").arg(title));
    }
  }
}

void BrushDynamicsPanel::select_section(Section section) {
  const auto index = static_cast<int>(section);
  if (index < 0 || index >= kSectionCount) {
    return;
  }
  stack_->setCurrentIndex(index);
  if (!tabs_[index].button->isChecked()) {
    tabs_[index].button->setChecked(true);
  }
  app_settings().setValue(QLatin1String(kSectionSettingsKey), section_id(section));
}

BrushDynamicsPanel::Section BrushDynamicsPanel::current_section() const {
  return static_cast<Section>(std::clamp(stack_->currentIndex(), 0, kSectionCount - 1));
}

bool BrushDynamicsPanel::section_marked_changed(Section section) const {
  const auto index = static_cast<int>(section);
  return index >= 0 && index < kSectionCount && !tabs_[index].dot->isHidden();
}

void BrushDynamicsPanel::set_preview_tip(std::shared_ptr<const patchy::BrushTip> tip,
                                         double spacing) {
  preview_->setVisible(tip != nullptr);
  preview_->set_tip(std::move(tip));
  preview_->set_spacing(spacing);
  preview_->set_dynamics(dynamics(), base_angle_degrees(), base_roundness());
}

void BrushDynamicsPanel::after_values_changed() {
  refresh_control_dependent_widgets();
  refresh_section_markers();
  if (preview_->has_tip()) {
    preview_->set_dynamics(dynamics(), base_angle_degrees(), base_roundness());
  }
}

void BrushDynamicsPanel::set_values(const patchy::BrushDynamics& dynamics, double base_angle_degrees,
                                    double base_roundness) {
  loading_ = true;
  base_angle_spin_->setValue(normalized_angle_value(base_angle_degrees));
  base_roundness_spin_->setValue(
      std::clamp(static_cast<int>(std::lround(base_roundness)), 1, 100));
  shape_dynamics_enabled_check_->setChecked(dynamics.shape_dynamics_enabled);
  scattering_enabled_check_->setChecked(dynamics.scattering_enabled);
  transfer_enabled_check_->setChecked(dynamics.transfer_enabled);
  size_jitter_spin_->setValue(percent_from_fraction(dynamics.size_jitter));
  minimum_diameter_spin_->setValue(percent_from_fraction(dynamics.minimum_diameter));
  select_combo_control(*size_control_combo_, dynamics.size_control);
  size_fade_steps_spin_->setValue(std::clamp(dynamics.size_fade_steps, 1, 9999));
  angle_jitter_spin_->setValue(percent_from_fraction(dynamics.angle_jitter));
  angle_control_combo_->setCurrentIndex(static_cast<int>(dynamics.angle_control));
  fade_steps_spin_->setValue(std::clamp(dynamics.angle_fade_steps, 1, 9999));
  roundness_jitter_spin_->setValue(percent_from_fraction(dynamics.roundness_jitter));
  minimum_roundness_spin_->setValue(percent_from_fraction(dynamics.minimum_roundness));
  select_combo_control(*roundness_control_combo_, dynamics.roundness_control);
  roundness_fade_steps_spin_->setValue(std::clamp(dynamics.roundness_fade_steps, 1, 9999));
  flip_x_check_->setChecked(dynamics.flip_x_jitter);
  flip_y_check_->setChecked(dynamics.flip_y_jitter);
  scatter_spin_->setValue(percent_from_fraction(dynamics.scatter));
  select_combo_control(*scatter_control_combo_, dynamics.scatter_control);
  scatter_fade_steps_spin_->setValue(std::clamp(dynamics.scatter_fade_steps, 1, 9999));
  both_axes_check_->setChecked(dynamics.scatter_both_axes);
  count_spin_->setValue(std::clamp(dynamics.count, 1, 16));
  count_jitter_spin_->setValue(percent_from_fraction(dynamics.count_jitter));
  select_combo_control(*count_control_combo_, dynamics.count_control);
  count_fade_steps_spin_->setValue(std::clamp(dynamics.count_fade_steps, 1, 9999));
  opacity_jitter_spin_->setValue(percent_from_fraction(dynamics.opacity_jitter));
  minimum_opacity_spin_->setValue(percent_from_fraction(dynamics.minimum_opacity));
  select_combo_control(*opacity_control_combo_, dynamics.opacity_control);
  opacity_fade_steps_spin_->setValue(std::clamp(dynamics.opacity_fade_steps, 1, 9999));
  flow_jitter_spin_->setValue(percent_from_fraction(dynamics.flow_jitter));
  minimum_flow_spin_->setValue(percent_from_fraction(dynamics.minimum_flow));
  select_combo_control(*flow_control_combo_, dynamics.flow_control);
  flow_fade_steps_spin_->setValue(std::clamp(dynamics.flow_fade_steps, 1, 9999));
  texture_enabled_check_->setChecked(dynamics.texture_enabled);
  select_combo_control(*color_control_combo_, dynamics.color_control);
  texture_style_combo_->setCurrentIndex(std::max(
      0, texture_style_combo_->findData(static_cast<int>(dynamics.texture_style))));
  texture_scale_spin_->setValue(percent_from_fraction(dynamics.texture_scale));
  texture_depth_spin_->setValue(percent_from_fraction(dynamics.texture_depth));
  texture_invert_check_->setChecked(dynamics.texture_invert);
  texture_seed_ = dynamics.texture_seed;
  dual_brush_enabled_check_->setChecked(dynamics.dual_brush_enabled);
  dual_brush_size_spin_->setValue(percent_from_fraction(dynamics.dual_brush_size));
  dual_brush_hardness_spin_->setValue(percent_from_fraction(dynamics.dual_brush_hardness));
  dual_brush_spacing_spin_->setValue(percent_from_fraction(dynamics.dual_brush_spacing));
  color_dynamics_enabled_check_->setChecked(dynamics.color_dynamics_enabled);
  foreground_background_jitter_spin_->setValue(
      percent_from_fraction(dynamics.foreground_background_jitter));
  color_fade_steps_spin_->setValue(std::clamp(dynamics.color_fade_steps, 1, 9999));
  hue_jitter_spin_->setValue(percent_from_fraction(dynamics.hue_jitter));
  saturation_jitter_spin_->setValue(percent_from_fraction(dynamics.saturation_jitter));
  brightness_jitter_spin_->setValue(percent_from_fraction(dynamics.brightness_jitter));
  purity_spin_->setValue(static_cast<int>(std::lround(dynamics.purity * 100.0)));
  color_per_tip_check_->setChecked(dynamics.color_per_tip);
  wet_edges_check_->setChecked(dynamics.wet_edges);
  after_values_changed();
  loading_ = false;
}

patchy::BrushDynamics BrushDynamicsPanel::dynamics() const {
  patchy::BrushDynamics dynamics;
  dynamics.shape_dynamics_enabled = shape_dynamics_enabled_check_->isChecked();
  dynamics.scattering_enabled = scattering_enabled_check_->isChecked();
  dynamics.transfer_enabled = transfer_enabled_check_->isChecked();
  dynamics.size_jitter = fraction_from_percent(size_jitter_spin_->value());
  dynamics.minimum_diameter = fraction_from_percent(minimum_diameter_spin_->value());
  dynamics.size_control = combo_control(*size_control_combo_);
  dynamics.size_fade_steps = size_fade_steps_spin_->value();
  dynamics.angle_jitter = fraction_from_percent(angle_jitter_spin_->value());
  dynamics.angle_control = static_cast<patchy::BrushDynamicControl>(
      std::clamp(angle_control_combo_->currentIndex(), 0,
                 static_cast<int>(patchy::BrushDynamicControl::StylusWheel)));
  dynamics.angle_fade_steps = fade_steps_spin_->value();
  dynamics.roundness_jitter = fraction_from_percent(roundness_jitter_spin_->value());
  dynamics.minimum_roundness = fraction_from_percent(minimum_roundness_spin_->value());
  dynamics.roundness_control = combo_control(*roundness_control_combo_);
  dynamics.roundness_fade_steps = roundness_fade_steps_spin_->value();
  dynamics.flip_x_jitter = flip_x_check_->isChecked();
  dynamics.flip_y_jitter = flip_y_check_->isChecked();
  dynamics.scatter = fraction_from_percent(scatter_spin_->value());
  dynamics.scatter_both_axes = both_axes_check_->isChecked();
  dynamics.scatter_control = combo_control(*scatter_control_combo_);
  dynamics.scatter_fade_steps = scatter_fade_steps_spin_->value();
  dynamics.count = count_spin_->value();
  dynamics.count_jitter = fraction_from_percent(count_jitter_spin_->value());
  dynamics.count_control = combo_control(*count_control_combo_);
  dynamics.count_fade_steps = count_fade_steps_spin_->value();
  dynamics.opacity_jitter = fraction_from_percent(opacity_jitter_spin_->value());
  dynamics.minimum_opacity = fraction_from_percent(minimum_opacity_spin_->value());
  dynamics.opacity_control = combo_control(*opacity_control_combo_);
  dynamics.opacity_fade_steps = opacity_fade_steps_spin_->value();
  dynamics.flow_jitter = fraction_from_percent(flow_jitter_spin_->value());
  dynamics.minimum_flow = fraction_from_percent(minimum_flow_spin_->value());
  dynamics.flow_control = combo_control(*flow_control_combo_);
  dynamics.flow_fade_steps = flow_fade_steps_spin_->value();
  dynamics.texture_enabled = texture_enabled_check_->isChecked();
  dynamics.texture_style = static_cast<patchy::BrushTextureStyle>(
      texture_style_combo_->currentData().toInt());
  dynamics.texture_scale = fraction_from_percent(texture_scale_spin_->value());
  dynamics.texture_depth = fraction_from_percent(texture_depth_spin_->value());
  dynamics.texture_invert = texture_invert_check_->isChecked();
  dynamics.texture_seed = texture_seed_;
  dynamics.dual_brush_enabled = dual_brush_enabled_check_->isChecked();
  dynamics.dual_brush_size = fraction_from_percent(dual_brush_size_spin_->value());
  dynamics.dual_brush_hardness = fraction_from_percent(dual_brush_hardness_spin_->value());
  dynamics.dual_brush_spacing = fraction_from_percent(dual_brush_spacing_spin_->value());
  dynamics.color_dynamics_enabled = color_dynamics_enabled_check_->isChecked();
  dynamics.foreground_background_jitter =
      fraction_from_percent(foreground_background_jitter_spin_->value());
  dynamics.color_control = combo_control(*color_control_combo_);
  dynamics.color_fade_steps = color_fade_steps_spin_->value();
  dynamics.hue_jitter = fraction_from_percent(hue_jitter_spin_->value());
  dynamics.saturation_jitter = fraction_from_percent(saturation_jitter_spin_->value());
  dynamics.brightness_jitter = fraction_from_percent(brightness_jitter_spin_->value());
  dynamics.purity = fraction_from_percent(purity_spin_->value());
  dynamics.color_per_tip = color_per_tip_check_->isChecked();
  dynamics.wet_edges = wet_edges_check_->isChecked();
  return dynamics;
}

void BrushDynamicsPanel::set_row_enabled(QWidget* field, bool enabled) {
  field->setEnabled(enabled);
  if (const auto found = rows_.constFind(field); found != rows_.constEnd()) {
    if (found->first != nullptr) {
      found->first->setEnabled(enabled);
    }
    if (found->second != nullptr) {
      found->second->setEnabled(enabled);
    }
  }
}

void BrushDynamicsPanel::refresh_control_dependent_widgets() {
  fade_steps_spin_->setVisible(angle_control_combo_->currentIndex() ==
                               static_cast<int>(patchy::BrushDynamicControl::Fade));
  const std::pair<QComboBox*, QSpinBox*> control_rows[] = {
      {size_control_combo_, size_fade_steps_spin_},
      {roundness_control_combo_, roundness_fade_steps_spin_},
      {scatter_control_combo_, scatter_fade_steps_spin_},
      {count_control_combo_, count_fade_steps_spin_},
      {opacity_control_combo_, opacity_fade_steps_spin_},
      {flow_control_combo_, flow_fade_steps_spin_},
      {color_control_combo_, color_fade_steps_spin_},
  };
  for (const auto& [combo, fade_spin] : control_rows) {
    fade_spin->setVisible(combo_control(*combo) == patchy::BrushDynamicControl::Fade);
  }
  // The size and roundness floors bound both the jitter and the control; with neither they
  // change nothing.
  set_row_enabled(minimum_diameter_spin_, size_jitter_spin_->value() > 0 ||
                                              control_has_source(combo_control(*size_control_combo_)));
  set_row_enabled(minimum_roundness_spin_,
                  roundness_jitter_spin_->value() > 0 ||
                      control_has_source(combo_control(*roundness_control_combo_)));
  // The Minimum Opacity and Flow floors only participate while their control has a source.
  set_row_enabled(minimum_opacity_spin_, control_has_source(combo_control(*opacity_control_combo_)));
  set_row_enabled(minimum_flow_spin_, control_has_source(combo_control(*flow_control_combo_)));
  // Count Jitter and Count Control vary a count above one; at one dab they do nothing.
  const auto multiple_dabs = count_spin_->value() > 1;
  set_row_enabled(count_jitter_spin_, multiple_dabs);
  set_row_enabled(count_control_combo_, multiple_dabs);
  count_fade_steps_spin_->setEnabled(multiple_dabs);
  // A section that is off keeps its values; its rows read as inactive until it is back on.
  for (const auto& tab : tabs_) {
    if (tab.enabled != nullptr) {
      tab.body->setEnabled(tab.enabled->isChecked());
    }
  }
}

void BrushDynamicsPanel::refresh_section_markers() {
  const auto values = dynamics();
  for (int index = 0; index < kSectionCount; ++index) {
    const auto section = static_cast<Section>(index);
    const auto changed =
        section == Section::TipShape
            ? (base_angle_spin_->value() != 0 || base_roundness_spin_->value() != 100)
            : !patchy::brush_dynamics_section_is_default(values, core_section(section));
    tabs_[index].dot->setVisible(changed);
    tabs_[index].reset->setEnabled(changed);
  }
}

double BrushDynamicsPanel::base_angle_degrees() const {
  return base_angle_spin_->value();
}

double BrushDynamicsPanel::base_roundness() const {
  return base_roundness_spin_->value();
}

void BrushDynamicsPanel::reset_to_defaults() {
  set_values({}, 0.0, 100.0);
  emit edited();
}

void BrushDynamicsPanel::reset_section(Section section) {
  if (section == Section::TipShape) {
    set_values(dynamics(), 0.0, 100.0);
  } else {
    auto values = dynamics();
    patchy::reset_brush_dynamics_section(values, core_section(section));
    set_values(values, base_angle_degrees(), base_roundness());
  }
  emit edited();
}

BrushDynamicsButton::BrushDynamicsButton(QWidget* parent) : QToolButton(parent) {
  setObjectName(QStringLiteral("brushDynamicsButton"));
  // An icon button (GitHub issue 364 follow-up): three sliders, not the gear, which already
  // marks the Smoothing options. "Dynamics" stays its text for screen readers.
  setToolButtonStyle(Qt::ToolButtonIconOnly);
  setIcon(simple_icon(QStringLiteral("brush-dynamics")));
  setIconSize(QSize(16, 16));
  // Checked while the popup is open, so the button reads as pressed (sync_checked).
  setCheckable(true);
  popup_clock_.start();
  connect(this, &QToolButton::clicked, this, &BrushDynamicsButton::show_popup);
  retranslate();
  set_active_entry(nullptr);
}

BrushDynamicsButton::~BrushDynamicsButton() {
  // An open popup is a child: delete it while this button is still whole, so its destroyed()
  // handler never runs against a half-destroyed button (a window closed with the popup up).
  if (popup_ != nullptr) {
    popup_->disconnect(this);
    delete popup_.data();
  }
  // The floating window belongs to the main window; it must not call back into this button.
  if (window_ != nullptr) {
    window_->disconnect(this);
    if (window_panel_ != nullptr) {
      window_panel_->disconnect(this);
    }
  }
}

void BrushDynamicsButton::load_panel(BrushDynamicsPanel& panel) const {
  panel.set_values(dynamics_, base_angle_degrees_, base_roundness_);
  if (tip_id_.isEmpty()) {
    panel.set_preview_tip(nullptr, tip_spacing_);
  } else if (round_session_) {
    panel.set_preview_tip(procedural_brush_preview_tip(tip_id_ == builtin_square_brush_tip_id()),
                          tip_spacing_);
  } else if (preview_tip_provider_) {
    panel.set_preview_tip(preview_tip_provider_(tip_id_), tip_spacing_);
  }
}

void BrushDynamicsButton::connect_panel(BrushDynamicsPanel& panel) {
  connect(&panel, &BrushDynamicsPanel::edited, this, [this, panel_ptr = QPointer<BrushDynamicsPanel>(&panel)] {
    if (panel_ptr == nullptr) {
      return;
    }
    dynamics_ = panel_ptr->dynamics();
    base_angle_degrees_ = panel_ptr->base_angle_degrees();
    base_roundness_ = panel_ptr->base_roundness();
    schedule_emit();
  });
}

void BrushDynamicsButton::reload_open_panels() {
  if (popup_panel_ != nullptr) {
    load_panel(*popup_panel_);
  }
  if (window_panel_ != nullptr) {
    load_panel(*window_panel_);
  }
  refresh_window_availability();
}

void BrushDynamicsButton::refresh_window_availability() {
  if (window_ == nullptr) {
    return;
  }
  window_->set_unavailable_reason(!tool_supported_ ? unsupported_reason_
                                  : tip_id_.isEmpty() ? tr("No brush tip is active.")
                                                      : QString());
}

void BrushDynamicsButton::set_window_key_target(std::function<QWidget*()> key_target) {
  window_key_target_ = std::move(key_target);
  if (window_ != nullptr) {
    window_->set_key_target(window_key_target_);
  }
}

void BrushDynamicsButton::set_tool_supported(bool supported, const QString& reason) {
  tool_supported_ = supported;
  unsupported_reason_ = supported ? QString() : reason;
  refresh_window_availability();
}

void BrushDynamicsButton::hide_window_for_shutdown() {
  if (window_ != nullptr) {
    window_->hide_for_shutdown();
  }
}

void BrushDynamicsButton::open_window(std::optional<QPoint> global_top_left, bool activate) {
  if (popup_ != nullptr) {
    popup_->close();
  }
  if (window_ == nullptr) {
    auto* window = new BrushToolWindow(QStringLiteral("brushSettingsWindow"), tr("Brush Dynamics and Effects"),
                                       QStringLiteral("brushes/settingsWindow"), this->window());
    auto* panel = new BrushDynamicsPanel(window);
    window->set_body(panel);
    window->set_key_target(window_key_target_);
    window_ = window;
    window_panel_ = panel;
    connect_panel(*panel);
    connect(window, &QDialog::finished, this, [this] { sync_checked(); });
  }
  load_panel(*window_panel_);
  refresh_window_availability();
  window_->open_window(global_top_left, activate);
  sync_checked();
}

void BrushDynamicsButton::open_popup() {
  if (popup_ != nullptr) {
    return;
  }
  popup_dismissed_ms_ = -1;
  show_popup();
}

void BrushDynamicsButton::retranslate() {
  setText(tr("Dynamics"));
  setAccessibleName(tr("Dynamics"));
  setToolTip(!round_session_ ? tr("Brush dynamics and effects for the active brush tip")
             : tip_id_ == builtin_square_brush_tip_id()
                 ? tr("Brush dynamics and effects for the Square brush "
                      "(this session only; resets on the next launch)")
                 : tr("Brush dynamics and effects for the Round brush "
                      "(this session only; resets on the next launch)"));
}

void BrushDynamicsButton::set_active_entry(const BrushTipEntry* entry) {
  if (entry == nullptr) {
    if (popup_ != nullptr) {
      popup_->close();
    }
    tip_id_.clear();
    round_session_ = false;
    dynamics_ = {};
    base_angle_degrees_ = 0.0;
    base_roundness_ = 100.0;
    setEnabled(false);
    retranslate();
    refresh_active_indicator();
    reload_open_panels();
    return;
  }
  if (entry->id == tip_id_ && !round_session_ &&
      (edit_pending() ||
       same_brush_model(entry->dynamics, entry->base_angle_degrees, entry->base_roundness, dynamics_,
                        base_angle_degrees_, base_roundness_))) {
    // Our own edit echoing back through the library's changed(), or any reload (the brush
    // preset library's one-second disk refresh re-resolves the active tip) before a pending
    // edit is emitted: the open panel or the pending edit owns the values. Only a real change
    // from elsewhere (a script, a preset) reloads an open popup or window.
    return;
  }
  tip_id_ = entry->id;
  tip_spacing_ = entry->spacing;
  round_session_ = false;
  dynamics_ = entry->dynamics;
  base_angle_degrees_ = entry->base_angle_degrees;
  base_roundness_ = entry->base_roundness;
  setEnabled(true);
  retranslate();
  refresh_active_indicator();
  reload_open_panels();
}

void BrushDynamicsButton::set_round_session(const QString& round_tip_id,
                                            const patchy::BrushDynamics& dynamics,
                                            double base_angle_degrees, double base_roundness) {
  if (tip_id_ == round_tip_id && round_session_ &&
      (edit_pending() || same_brush_model(dynamics, base_angle_degrees, base_roundness, dynamics_,
                                          base_angle_degrees_, base_roundness_))) {
    // Our own edit echoing back through MainWindow's re-apply; the open panel or the pending
    // edit owns the values.
    return;
  }
  tip_id_ = round_tip_id;
  tip_spacing_ = 0.25;  // the Round and Square brushes stamp at Photoshop's default spacing
  round_session_ = true;
  dynamics_ = dynamics;
  base_angle_degrees_ = base_angle_degrees;
  base_roundness_ = base_roundness;
  setEnabled(true);
  retranslate();
  refresh_active_indicator();
  reload_open_panels();
}

void BrushDynamicsButton::set_preview_tip_provider(
    std::function<std::shared_ptr<const patchy::BrushTip>(const QString& tip_id)> provider) {
  preview_tip_provider_ = std::move(provider);
}

void BrushDynamicsButton::refresh_active_indicator() {
  // Keyed on non-default (not active()): a brush whose only customization is a control of Off
  // (ignore the pen) never runs the per-dab path but is still a deliberate setup worth showing.
  const auto active = !tip_id_.isEmpty() &&
                      (!brush_dynamics_is_default(dynamics_) || base_angle_degrees_ != 0.0 ||
                       base_roundness_ != 100.0);
  if (property("dynamicsActive").toBool() == active) {
    return;
  }
  setProperty("dynamicsActive", active);
  style()->unpolish(this);
  style()->polish(this);
}

void BrushDynamicsButton::schedule_emit() {
  refresh_active_indicator();
  static constexpr int kDebounceMs = 200;
  pending_tip_id_ = tip_id_;
  pending_dynamics_ = dynamics_;
  pending_base_angle_degrees_ = base_angle_degrees_;
  pending_base_roundness_ = base_roundness_;
  if (emit_timer_ == nullptr) {
    emit_timer_ = new QTimer(this);
    emit_timer_->setSingleShot(true);
    emit_timer_->setInterval(kDebounceMs);
    connect(emit_timer_, &QTimer::timeout, this, [this] {
      if (!pending_tip_id_.isEmpty()) {
        emit dynamics_edited(pending_tip_id_, pending_dynamics_, pending_base_angle_degrees_,
                             pending_base_roundness_);
      }
    });
  }
  emit_timer_->start();
}

bool BrushDynamicsButton::edit_pending() const {
  return emit_timer_ != nullptr && emit_timer_->isActive();
}

void BrushDynamicsButton::sync_checked() {
  setChecked(popup_ != nullptr || (window_ != nullptr && window_->isVisible()));
}

void BrushDynamicsButton::show_popup() {
  // A click toggles the checkable button by itself; every way out of here sets it back to
  // whether the popup is open.
  struct SyncChecked {
    BrushDynamicsButton* button;
    ~SyncChecked() {
      if (button != nullptr) {
        button->sync_checked();
      }
    }
  } sync{this};
  if (tip_id_.isEmpty()) {
    return;
  }
  // With the floating window open, the button brings it forward: one panel edits the brush.
  if (window_ != nullptr && window_->isVisible()) {
    window_->raise();
    window_->activateWindow();
    return;
  }
  // Clicking the button while its popup is open must DISMISS it, not reopen it. The press that
  // closes the Qt::Popup is replayed onto the button, so by the time clicked() fires the popup
  // is either still closing (pointer alive) or was just destroyed (timestamp) — both mean this
  // click was the dismissal.
  if (popup_ != nullptr) {
    popup_->close();  // deleted later: popup_ stays set until then
    sync.button = nullptr;
    setChecked(false);
    return;
  }
  if (popup_dismissed_ms_ >= 0 && popup_clock_.elapsed() - popup_dismissed_ms_ < 300) {
    popup_dismissed_ms_ = -1;
    return;
  }
  auto* popup = new QFrame(this, Qt::Popup);
  popup->setAttribute(Qt::WA_DeleteOnClose);
  popup->setObjectName(QStringLiteral("brushDynamicsPopup"));
  popup->setFrameShape(QFrame::StyledPanel);
  popup_ = popup;
  connect(popup, &QObject::destroyed, this, [this] {
    // Arm the swallow window only when the popup died under a click on the button itself;
    // closing it by choosing elsewhere must not eat a quick legitimate reopen.
    if (rect().contains(mapFromGlobal(QCursor::pos()))) {
      popup_dismissed_ms_ = popup_clock_.elapsed();
    }
    setChecked(window_ != nullptr && window_->isVisible());  // Keep Open: the window took over
  });

  auto* layout = new QVBoxLayout(popup);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  // The header: Keep Open turns the popup into the floating Brush Dynamics and Effects window in
  // place. A push button, framed like Reset Section, so it reads as a control (GitHub issue 506).
  auto* header = new QHBoxLayout();
  header->setContentsMargins(8, 6, 8, 0);
  auto* title = new QLabel(tr("Brush Dynamics and Effects"), popup);
  title->setObjectName(QStringLiteral("brushDynamicsPopupTitle"));
  header->addWidget(title);
  header->addStretch(1);
  auto* keep_open = new QPushButton(tr("Keep Open"), popup);
  keep_open->setObjectName(QStringLiteral("brushDynamicsKeepOpenButton"));
  keep_open->setAutoDefault(false);
  keep_open->setToolTip(
      tr("Open the brush dynamics and effects in a window that stays open while you paint"));
  header->addWidget(keep_open);
  layout->addLayout(header);
  auto* panel = new BrushDynamicsPanel(popup);
  popup_panel_ = panel;
  load_panel(*panel);
  layout->addWidget(panel);
  connect_panel(*panel);
  connect(keep_open, &QPushButton::clicked, popup, [this, popup, panel] {
    const auto top_left = popup->mapToGlobal(QPoint(0, 0));
    const auto popup_size = popup->size();
    const auto section = panel->current_section();
    const bool first_open = window_ == nullptr;
    popup->close();
    open_window(top_left);
    if (window_panel_ != nullptr) {
      window_panel_->select_section(section);
    }
    if (first_open && window_ != nullptr &&
        !app_settings().value(QStringLiteral("brushes/settingsWindowSize")).toSize().isValid()) {
      window_->resize(popup_size.expandedTo(window_->minimumSizeHint()));
    }
  });

  // A fixed 520 x 600 (GitHub issue 506), wider only if a translation needs it, so switching
  // tabs never resizes the popup and it opens under the options bar. Never taller than the
  // screen: on a very short one the pages scroll instead.
  const auto frame = 2 * popup->frameWidth();
  const auto panel_hint = panel->sizeHint();
  auto popup_width = std::max(kPopupWidth, panel_hint.width() + frame);
  auto popup_height = kPopupHeight;
  const auto* screen = this->screen();
  if (screen != nullptr) {
    const auto available = screen->availableGeometry();
    popup_height = std::min(popup_height, std::max(200, available.height() - 16));
    popup_width = std::min(popup_width, available.width());
  }
  popup->setFixedSize(popup_width, popup_height);
  // A hidden options bar: under the window's top-left corner.
  auto position = isVisible() ? mapToGlobal(QPoint(0, height())) : window()->mapToGlobal(QPoint(12, 12));
  if (screen != nullptr) {
    const auto available = screen->availableGeometry();
    if (position.y() + popup->height() > available.bottom() + 1) {
      position.setY(std::max(available.top(), mapToGlobal(QPoint(0, 0)).y() - popup->height()));
    }
    position.setY(std::clamp(position.y(), available.top(),
                             std::max(available.top(), available.bottom() + 1 - popup->height())));
    position.setX(std::clamp(position.x(), available.left(),
                             std::max(available.left(), available.right() + 1 - popup->width())));
  }
  popup->move(position);
  popup->show();
}

}  // namespace patchy::ui
