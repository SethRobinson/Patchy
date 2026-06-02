#include "ui/filter_workflows.hpp"

#include "ui/dialog_utils.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRect>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace patchy::ui {

FilterCancelled::FilterCancelled() : std::runtime_error("Filter cancelled") {}

namespace {

struct SliderRowSpec {
  QString label;
  QString object_prefix;
  int minimum{0};
  int maximum{100};
  int value{0};
  QString suffix;
};

QSpinBox* add_slider_row(QDialog& dialog, QFormLayout* form, const SliderRowSpec& spec) {
  auto* container = new QWidget(&dialog);
  auto* row = new QHBoxLayout(container);
  row->setContentsMargins(0, 0, 0, 0);
  auto* slider = new QSlider(Qt::Horizontal, container);
  auto* spin = new QSpinBox(container);
  slider->setRange(spec.minimum, spec.maximum);
  spin->setRange(spec.minimum, spec.maximum);
  slider->setValue(spec.value);
  spin->setValue(spec.value);
  slider->setObjectName(spec.object_prefix + QStringLiteral("Slider"));
  spin->setObjectName(spec.object_prefix + QStringLiteral("Spin"));
  if (!spec.suffix.isEmpty()) {
    spin->setSuffix(spec.suffix);
  }
  configure_dialog_spinbox(spin, 72);
  row->addWidget(slider, 1);
  row->addWidget(spin);
  QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
  QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
  form->addRow(spec.label, container);
  return spin;
}

template <typename Settings>
std::optional<Settings> request_adjustment_settings_dialog(
    QWidget* parent, const QString& object_name, const QString& title, const QString& preview_object_name,
    const std::vector<SliderRowSpec>& row_specs,
    const std::function<Settings(const std::vector<QSpinBox*>&)>& build_settings,
    const std::function<void(bool, const Settings&)>& preview_changed,
    const std::function<void(QDialog&, const std::vector<QSpinBox*>&)>& connect_constraints = {}) {
  QDialog dialog(parent);
  dialog.setObjectName(object_name);
  dialog.setWindowTitle(title);
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  std::vector<QSpinBox*> spins;
  spins.reserve(row_specs.size());
  for (const auto& spec : row_specs) {
    spins.push_back(add_slider_row(dialog, form, spec));
  }

  auto* preview = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview->setObjectName(preview_object_name);
  preview->setChecked(true);
  layout->addWidget(preview);

  if (connect_constraints) {
    connect_constraints(dialog, spins);
  }

  auto emit_preview = [&] {
    if (preview_changed) {
      preview_changed(preview->isChecked(), build_settings(spins));
    }
  };
  for (auto* spin : spins) {
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&emit_preview](int) { emit_preview(); });
  }
  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&emit_preview](bool) { emit_preview(); });

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  QTimer::singleShot(0, &dialog, [&dialog, &emit_preview] {
    if (dialog.isVisible()) {
      emit_preview();
    }
  });
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  return build_settings(spins);
}

}  // namespace

QString filter_action_object_name(const QString& identifier) {
  auto object_name = QStringLiteral("filterAction_") + identifier;
  object_name.replace(QLatin1Char('.'), QLatin1Char('_'));
  return object_name;
}

QString filter_display_name(const FilterDefinition& filter) {
  const auto identifier = QString::fromStdString(filter.identifier);
  if (identifier == QStringLiteral("patchy.filters.invert")) {
    return QObject::tr("Invert");
  }
  if (identifier == QStringLiteral("patchy.filters.brightness_plus")) {
    return QObject::tr("Brightness");
  }
  if (identifier == QStringLiteral("patchy.filters.contrast_plus")) {
    return QObject::tr("Contrast");
  }
  if (identifier == QStringLiteral("patchy.filters.grayscale")) {
    return QObject::tr("Grayscale");
  }
  if (identifier == QStringLiteral("patchy.filters.desaturate")) {
    return QObject::tr("Desaturate");
  }
  if (identifier == QStringLiteral("patchy.filters.auto_contrast")) {
    return QObject::tr("Auto Contrast");
  }
  if (identifier == QStringLiteral("patchy.filters.soft_glow")) {
    return QObject::tr("Soft Glow");
  }
  if (identifier == QStringLiteral("patchy.filters.punchy_color")) {
    return QObject::tr("Punchy Color");
  }
  if (identifier == QStringLiteral("patchy.filters.noir")) {
    return QObject::tr("Noir");
  }
  if (identifier == QStringLiteral("patchy.filters.cinematic_matte")) {
    return QObject::tr("Cinematic Matte");
  }
  if (identifier == QStringLiteral("patchy.filters.vintage_fade")) {
    return QObject::tr("Vintage Fade");
  }
  if (identifier == QStringLiteral("patchy.filters.sepia")) {
    return QObject::tr("Vintage Sepia");
  }
  if (identifier == QStringLiteral("patchy.filters.threshold")) {
    return QObject::tr("Threshold");
  }
  if (identifier == QStringLiteral("patchy.filters.posterize")) {
    return QObject::tr("Posterize");
  }
  if (identifier == QStringLiteral("patchy.filters.box_blur")) {
    return QObject::tr("Box Blur");
  }
  if (identifier == QStringLiteral("patchy.filters.sharpen")) {
    return QObject::tr("Sharpen");
  }
  if (identifier == QStringLiteral("patchy.filters.unsharp_mask")) {
    return QObject::tr("Unsharp Mask");
  }
  if (identifier == QStringLiteral("patchy.filters.gaussian_blur")) {
    return QObject::tr("Gaussian Blur");
  }
  if (identifier == QStringLiteral("patchy.filters.motion_blur")) {
    return QObject::tr("Motion Blur");
  }
  if (identifier == QStringLiteral("patchy.filters.radial_blur")) {
    return QObject::tr("Radial Blur");
  }
  if (identifier == QStringLiteral("patchy.filters.edge_detect")) {
    return QObject::tr("Edge Detect");
  }
  if (identifier == QStringLiteral("patchy.filters.emboss")) {
    return QObject::tr("Emboss");
  }
  if (identifier == QStringLiteral("patchy.filters.glowing_edges")) {
    return QObject::tr("Glowing Edges");
  }
  if (identifier == QStringLiteral("patchy.filters.twirl")) {
    return QObject::tr("Twirl");
  }
  if (identifier == QStringLiteral("patchy.filters.wave")) {
    return QObject::tr("Wave");
  }
  if (identifier == QStringLiteral("patchy.filters.pinch_bloat")) {
    return QObject::tr("Pinch/Bloat");
  }
  if (identifier == QStringLiteral("patchy.filters.clouds")) {
    return QObject::tr("Clouds");
  }
  if (identifier == QStringLiteral("patchy.filters.pixelate")) {
    return QObject::tr("Pixel Mosaic");
  }
  if (identifier == QStringLiteral("patchy.filters.color_halftone")) {
    return QObject::tr("Color Halftone");
  }
  if (identifier == QStringLiteral("patchy.filters.film_grain")) {
    return QObject::tr("Analog Grain");
  }
  if (identifier == QStringLiteral("patchy.filters.vignette")) {
    return QObject::tr("Lens Vignette");
  }
  return QString::fromStdString(filter.display_name);
}

bool is_adjustment_only_filter(const QString& identifier) {
  return identifier == QStringLiteral("patchy.filters.invert") ||
         identifier == QStringLiteral("patchy.filters.brightness_plus") ||
         identifier == QStringLiteral("patchy.filters.contrast_plus") ||
         identifier == QStringLiteral("patchy.filters.grayscale") ||
         identifier == QStringLiteral("patchy.filters.desaturate") ||
         identifier == QStringLiteral("patchy.filters.auto_contrast") ||
         identifier == QStringLiteral("patchy.filters.threshold") ||
         identifier == QStringLiteral("patchy.filters.posterize");
}

FilterDialogSpec filter_dialog_spec_for(const FilterDefinition& filter) {
  const auto identifier = QString::fromStdString(filter.identifier);
  const auto display_name = filter_display_name(filter);
  const auto amount_control = [](int value = 100) {
    return FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 100, value,
                             QStringLiteral("%")};
  };

  if (identifier == QStringLiteral("patchy.filters.invert")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("patchy.filters.brightness_plus")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Brightness"), QStringLiteral("filterBrightness"), -100, 100, 24, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.contrast_plus")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterContrast"), -100, 100, 25,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.grayscale") ||
      identifier == QStringLiteral("patchy.filters.desaturate")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("patchy.filters.auto_contrast")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("patchy.filters.soft_glow")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Glow"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.punchy_color") ||
      identifier == QStringLiteral("patchy.filters.cinematic_matte")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Intensity"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.noir")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.vintage_fade")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Fade"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.sepia")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("patchy.filters.threshold")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Threshold"), QStringLiteral("filterThreshold"), 0, 255, 128, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.posterize")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Levels"), QStringLiteral("filterLevels"), 2, 16, 4, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.box_blur")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 12, 1,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.sharpen")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 300, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.unsharp_mask")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 300, 150,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 12, 2,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Threshold"), QStringLiteral("filterThreshold"), 0, 255, 8, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.gaussian_blur")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 12, 2,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.motion_blur")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Angle"), QStringLiteral("filterAngle"), -180, 180, 0,
                               QStringLiteral(" deg")},
             FilterControlSpec{QObject::tr("Distance"), QStringLiteral("filterDistance"), 1, 64, 12,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.radial_blur")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 100, 35,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Samples"), QStringLiteral("filterSamples"), 4, 32, 16, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.edge_detect")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Strength"), QStringLiteral("filterStrength"), 0, 300, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.emboss")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Angle"), QStringLiteral("filterAngle"), -180, 180, 135,
                               QStringLiteral(" deg")},
             FilterControlSpec{QObject::tr("Height"), QStringLiteral("filterHeight"), 1, 24, 2,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterDepth"), 0, 300, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.glowing_edges")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Edge Width"), QStringLiteral("filterEdgeWidth"), 1, 12, 2,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Brightness"), QStringLiteral("filterBrightness"), 0, 300, 140,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Smoothness"), QStringLiteral("filterSmoothness"), 0, 12, 2,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.twirl")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Angle"), QStringLiteral("filterAngle"), -720, 720, 180,
                               QStringLiteral(" deg")},
             FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.wave")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amplitude"), QStringLiteral("filterAmplitude"), 0, 64, 12,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Wavelength"), QStringLiteral("filterWavelength"), 4, 256, 48,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Phase"), QStringLiteral("filterPhase"), 0, 360, 0,
                               QStringLiteral(" deg")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.pinch_bloat")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), -100, 100, 35,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.clouds")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Scale"), QStringLiteral("filterScale"), 12, 512, 96,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Detail"), QStringLiteral("filterDetail"), 1, 8, 6, {}},
             FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterContrast"), 0, 100, 40,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Seed"), QStringLiteral("filterSeed"), 1, 9999, 1, {}}}};
  }
  if (identifier == QStringLiteral("patchy.filters.pixelate")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Block Size"), QStringLiteral("filterBlockSize"), 2, 32, 4,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.color_halftone")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Cell Size"), QStringLiteral("filterCellSize"), 4, 64, 10,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Intensity"), QStringLiteral("filterIntensity"), 0, 100, 75,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterContrast"), 0, 100, 60,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.film_grain")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 100, 50,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("patchy.filters.vignette")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Strength"), QStringLiteral("filterStrength"), 0, 100, 55,
                               QStringLiteral("%")}}};
  }

  return {identifier, display_name, {amount_control()}};
}

std::optional<std::vector<int>> request_filter_settings(
    QWidget* parent, const FilterDialogSpec& spec, const std::function<void(FilterPreviewSettings)>& preview_changed) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("patchyFilterDialog"));
  dialog.setProperty("patchy.filterIdentifier", spec.identifier);
  dialog.setWindowTitle(spec.display_name);
  auto* layout = new QVBoxLayout(&dialog);

  auto* preview = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview->setObjectName(QStringLiteral("filterPreviewCheck"));
  preview->setChecked(true);
  layout->addWidget(preview);

  auto* form = new QFormLayout();
  layout->addLayout(form);

  std::vector<QSpinBox*> spins;
  spins.reserve(spec.controls.size());
  std::vector<int> defaults;
  defaults.reserve(spec.controls.size());

  for (const auto& control : spec.controls) {
    auto* container = new QWidget(&dialog);
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal, container);
    auto* spin = new QSpinBox(container);
    slider->setRange(control.minimum, control.maximum);
    spin->setRange(control.minimum, control.maximum);
    slider->setValue(control.value);
    spin->setValue(control.value);
    slider->setObjectName(control.object_name + QStringLiteral("Slider"));
    spin->setObjectName(control.object_name + QStringLiteral("Spin"));
    if (!control.suffix.isEmpty()) {
      spin->setSuffix(control.suffix);
    }
    configure_dialog_spinbox(spin, 78);
    row->addWidget(slider, 1);
    row->addWidget(spin);
    QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    form->addRow(control.label, container);
    spins.push_back(spin);
    defaults.push_back(control.value);
  }

  auto build_settings = [&] {
    FilterPreviewSettings settings;
    settings.preview_enabled = preview->isChecked();
    settings.values.reserve(spins.size());
    for (const auto* spin : spins) {
      settings.values.push_back(spin->value());
    }
    return settings;
  };

  auto emit_preview = [&] {
    if (preview_changed) {
      preview_changed(build_settings());
    }
  };

  QObject::connect(preview, &QCheckBox::toggled, &dialog, [&emit_preview](bool) { emit_preview(); });
  for (auto* spin : spins) {
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&emit_preview](int) { emit_preview(); });
  }

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset,
                                       &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (auto* reset = buttons->button(QDialogButtonBox::Reset); reset != nullptr) {
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
      for (std::size_t index = 0; index < spins.size(); ++index) {
        spins[index]->setValue(defaults[index]);
      }
      preview->setChecked(true);
      emit_preview();
    });
  }

  QTimer::singleShot(0, &dialog, [&dialog, &emit_preview] {
    if (dialog.isVisible()) {
      emit_preview();
    }
  });
  if (run_non_modal_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }

  std::vector<int> values;
  values.reserve(spins.size());
  for (const auto* spin : spins) {
    values.push_back(spin->value());
  }
  return values;
}

std::uint8_t filter_clamp_byte(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::uint8_t filter_clamp_byte(double value) {
  return static_cast<std::uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

int filter_luminance(const std::uint8_t* px) {
  return (static_cast<int>(px[0]) * 30 + static_cast<int>(px[1]) * 59 + static_cast<int>(px[2]) * 11) / 100;
}

constexpr double kFilterPi = 3.14159265358979323846;

int filter_value(const std::vector<int>& values, std::size_t index, int fallback) {
  return index < values.size() ? values[index] : fallback;
}

void report_filter_progress(const FilterProgress* progress, int completed, int total,
                            const QString& detail = QString()) {
  if (progress == nullptr || !progress->update) {
    return;
  }
  if (!progress->update(std::clamp(completed, 0, std::max(1, total)), std::max(1, total), detail)) {
    throw FilterCancelled();
  }
}

void report_filter_row_progress(const FilterProgress* progress, std::int32_t y, std::int32_t height,
                                int progress_offset = 0, int progress_total = 0) {
  report_filter_progress(progress, progress_offset + y, progress_total > 0 ? progress_total : height,
                         QObject::tr("Filtering pixels"));
}

void finish_filter_row_progress(const FilterProgress* progress, std::int32_t height, int progress_offset = 0,
                                int progress_total = 0) {
  report_filter_progress(progress, progress_offset + height, progress_total > 0 ? progress_total : height,
                         QObject::tr("Filtering pixels"));
}

QRegion layer_selection_region(const QRegion& selection, Rect bounds) {
  if (selection.isEmpty() || bounds.empty()) {
    return {};
  }
  return selection.intersected(QRegion(QRect(bounds.x, bounds.y, bounds.width, bounds.height)));
}

template <typename PixelFn>
void for_each_selected_pixel(PixelBuffer& pixels, Rect bounds, const QRegion& selection, const FilterProgress* progress,
                             PixelFn&& pixel_fn) {
  if (selection.isEmpty()) {
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        pixel_fn(x, y);
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  const auto selected = layer_selection_region(selection, bounds);
  int total_rows = 0;
  for (const auto& rect : selected) {
    const auto local_left = std::max(0, rect.x() - bounds.x);
    const auto local_top = std::max(0, rect.y() - bounds.y);
    const auto local_right = std::min<std::int32_t>(pixels.width(), rect.x() + rect.width() - bounds.x);
    const auto local_bottom = std::min<std::int32_t>(pixels.height(), rect.y() + rect.height() - bounds.y);
    if (local_left < local_right && local_top < local_bottom) {
      total_rows += local_bottom - local_top;
    }
  }
  int completed_rows = 0;
  for (const auto& rect : selected) {
    const auto local_left = std::max(0, rect.x() - bounds.x);
    const auto local_top = std::max(0, rect.y() - bounds.y);
    const auto local_right = std::min<std::int32_t>(pixels.width(), rect.x() + rect.width() - bounds.x);
    const auto local_bottom = std::min<std::int32_t>(pixels.height(), rect.y() + rect.height() - bounds.y);
    for (std::int32_t y = local_top; y < local_bottom; ++y) {
      report_filter_progress(progress, completed_rows, total_rows, QObject::tr("Filtering pixels"));
      for (std::int32_t x = local_left; x < local_right; ++x) {
        pixel_fn(x, y);
      }
      ++completed_rows;
    }
  }
  report_filter_progress(progress, total_rows, total_rows, QObject::tr("Filtering pixels"));
}

void blend_filter_with_original(PixelBuffer& pixels, const PixelBuffer& original, int amount_percent,
                                const FilterProgress* progress = nullptr, int progress_offset = 0,
                                int progress_total = 0) {
  amount_percent = std::clamp(amount_percent, 0, 100);
  if (amount_percent >= 100) {
    finish_filter_row_progress(progress, pixels.height(), progress_offset, progress_total);
    return;
  }
  if (amount_percent <= 0 || pixels.format() != original.format() || pixels.width() != original.width() ||
      pixels.height() != original.height()) {
    pixels = original;
    finish_filter_row_progress(progress, pixels.height(), progress_offset, progress_total);
    return;
  }

  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height(), progress_offset, progress_total);
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        dst[channel] = filter_clamp_byte((static_cast<int>(src[channel]) * (100 - amount_percent) +
                                          static_cast<int>(dst[channel]) * amount_percent) /
                                         100);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height(), progress_offset, progress_total);
}

FilterProgress filter_progress_phase(const FilterProgress* progress, int phase_index, int phase_count) {
  if (progress == nullptr || !progress->update) {
    return {};
  }
  return FilterProgress{[progress, phase_index, phase_count](int completed, int total, const QString& detail) {
    constexpr int kPhaseScale = 1000;
    const auto safe_phase_count = std::max(1, phase_count);
    const auto safe_total = std::max(1, total);
    const auto clamped_completed = std::clamp(completed, 0, safe_total);
    const auto phase_completed = (clamped_completed * kPhaseScale) / safe_total;
    return progress->update(std::clamp(phase_index, 0, safe_phase_count - 1) * kPhaseScale + phase_completed,
                            safe_phase_count * kPhaseScale, detail);
  }};
}

std::uint8_t filter_blend_byte(std::uint8_t base, std::uint8_t overlay, int amount_percent) {
  amount_percent = std::clamp(amount_percent, 0, 100);
  return filter_clamp_byte((static_cast<int>(base) * (100 - amount_percent) +
                            static_cast<int>(overlay) * amount_percent + 50) /
                           100);
}

void adjust_contrast_filter_pixels(PixelBuffer& pixels, double factor, int midpoint, const FilterProgress* progress) {
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        px[channel] = filter_clamp_byte(static_cast<int>((static_cast<double>(px[channel]) -
                                                          static_cast<double>(midpoint)) *
                                                             factor +
                                                         static_cast<double>(midpoint)));
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

void adjust_saturation_filter_pixels(PixelBuffer& pixels, double factor, const FilterProgress* progress) {
  if (pixels.format().channels < 3) {
    return;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      const auto luminance = static_cast<double>(filter_luminance(px));
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        px[channel] = filter_clamp_byte(
            static_cast<int>(luminance + (static_cast<double>(px[channel]) - luminance) * factor));
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

void tint_filter_pixels(PixelBuffer& pixels, int red, int green, int blue, const FilterProgress* progress) {
  if (pixels.format().channels < 3) {
    return;
  }
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      px[0] = filter_clamp_byte(static_cast<int>(px[0]) + red);
      px[1] = filter_clamp_byte(static_cast<int>(px[1]) + green);
      px[2] = filter_clamp_byte(static_cast<int>(px[2]) + blue);
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

void blend_overlay_filter_pixels(PixelBuffer& pixels, const PixelBuffer& overlay, int amount_percent,
                                 const FilterProgress* progress) {
  if (pixels.format() != overlay.format() || pixels.width() != overlay.width() || pixels.height() != overlay.height()) {
    return;
  }
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_row_progress(progress, y, pixels.height());
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* px = pixels.pixel(x, y);
      const auto* over = overlay.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        px[channel] = filter_blend_byte(px[channel], over[channel], amount_percent);
      }
    }
  }
  finish_filter_row_progress(progress, pixels.height());
}

struct FilterPixelAccum {
  std::array<double, 3> premultiplied_color{0.0, 0.0, 0.0};
  double alpha{0.0};
  double weight{0.0};
};

void filter_accumulate_pixel(FilterPixelAccum& accum, const PixelBuffer& original, const std::uint8_t* px,
                             double weight) {
  if (weight <= 0.0) {
    return;
  }
  const auto alpha = original.format().channels >= 4 ? static_cast<double>(px[3]) / 255.0 : 1.0;
  accum.weight += weight;
  accum.alpha += alpha * weight;
  for (std::uint16_t channel = 0; channel < std::min<std::uint16_t>(original.format().channels, 3); ++channel) {
    accum.premultiplied_color[static_cast<std::size_t>(channel)] += static_cast<double>(px[channel]) * alpha * weight;
  }
}

void filter_accumulate_sample(FilterPixelAccum& accum, const PixelBuffer& original, double x, double y,
                              double weight = 1.0) {
  x = std::clamp(x, 0.0, static_cast<double>(std::max<std::int32_t>(0, original.width() - 1)));
  y = std::clamp(y, 0.0, static_cast<double>(std::max<std::int32_t>(0, original.height() - 1)));
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto x1 = std::min<std::int32_t>(original.width() - 1, x0 + 1);
  const auto y1 = std::min<std::int32_t>(original.height() - 1, y0 + 1);
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);
  filter_accumulate_pixel(accum, original, original.pixel(x0, y0), weight * (1.0 - tx) * (1.0 - ty));
  filter_accumulate_pixel(accum, original, original.pixel(x1, y0), weight * tx * (1.0 - ty));
  filter_accumulate_pixel(accum, original, original.pixel(x0, y1), weight * (1.0 - tx) * ty);
  filter_accumulate_pixel(accum, original, original.pixel(x1, y1), weight * tx * ty);
}

void filter_write_accumulated_pixel(PixelBuffer& pixels, std::int32_t x, std::int32_t y,
                                    const FilterPixelAccum& accum) {
  auto* dst = pixels.pixel(x, y);
  const auto channels = pixels.format().channels;
  const auto normalized_alpha = channels >= 4 && accum.weight > 0.0 ? accum.alpha / accum.weight : 1.0;
  for (std::uint16_t channel = 0; channel < std::min<std::uint16_t>(channels, 3); ++channel) {
    const auto value = accum.alpha > 0.000001
                           ? accum.premultiplied_color[static_cast<std::size_t>(channel)] / accum.alpha
                           : 0.0;
    dst[channel] = filter_clamp_byte(value);
  }
  if (channels >= 4) {
    dst[3] = filter_clamp_byte(normalized_alpha * 255.0);
  }
}

void filter_copy_sampled_pixel(PixelBuffer& pixels, const PixelBuffer& original, std::int32_t x, std::int32_t y,
                               double source_x, double source_y) {
  FilterPixelAccum accum;
  filter_accumulate_sample(accum, original, source_x, source_y);
  filter_write_accumulated_pixel(pixels, x, y, accum);
}

void filter_write_blurred_pixel(PixelBuffer& pixels, const PixelBuffer& original, std::int32_t x, std::int32_t y,
                                int radius, bool weighted) {
  radius = std::clamp(radius, 1, 32);
  FilterPixelAccum accum;
  for (int dy = -radius; dy <= radius; ++dy) {
    const auto sy = std::clamp<std::int32_t>(y + dy, 0, original.height() - 1);
    const auto y_weight = weighted ? radius + 1 - std::abs(dy) : 1;
    for (int dx = -radius; dx <= radius; ++dx) {
      const auto sx = std::clamp<std::int32_t>(x + dx, 0, original.width() - 1);
      const auto x_weight = weighted ? radius + 1 - std::abs(dx) : 1;
      filter_accumulate_pixel(accum, original, original.pixel(sx, sy), static_cast<double>(x_weight * y_weight));
    }
  }
  filter_write_accumulated_pixel(pixels, x, y, accum);
}

void apply_builtin_gaussian_blur_filter_pixels(PixelBuffer& pixels, const FilterProgress* progress) {
  if (pixels.format().channels < 3 || pixels.width() == 0 || pixels.height() == 0) {
    return;
  }

  constexpr std::array<int, 5> kWeights = {1, 4, 6, 4, 1};
  const auto original = pixels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Blurring pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      FilterPixelAccum accum;
      for (int ky = -2; ky <= 2; ++ky) {
        const auto sy = std::clamp<std::int32_t>(y + ky, 0, pixels.height() - 1);
        for (int kx = -2; kx <= 2; ++kx) {
          const auto sx = std::clamp<std::int32_t>(x + kx, 0, pixels.width() - 1);
          filter_accumulate_pixel(accum, original, original.pixel(sx, sy),
                                  static_cast<double>(kWeights[static_cast<std::size_t>(kx + 2)] *
                                                      kWeights[static_cast<std::size_t>(ky + 2)]));
        }
      }
      filter_write_accumulated_pixel(pixels, x, y, accum);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Blurring pixels"));
}

std::uint32_t filter_coordinate_hash(std::int32_t x, std::int32_t y, std::uint16_t channel) noexcept {
  auto value = static_cast<std::uint32_t>(x + 1) * 73856093U;
  value ^= static_cast<std::uint32_t>(y + 1) * 19349663U;
  value ^= static_cast<std::uint32_t>(channel + 1) * 83492791U;
  value ^= value >> 13U;
  value *= 1274126177U;
  value ^= value >> 16U;
  return value;
}

std::uint32_t filter_noise_hash(std::int32_t x, std::int32_t y, std::uint32_t seed) noexcept {
  auto value = static_cast<std::uint32_t>(x + 16384) * 374761393U;
  value ^= static_cast<std::uint32_t>(y + 8192) * 668265263U;
  value ^= seed * 2246822519U;
  value ^= value >> 13U;
  value *= 1274126177U;
  value ^= value >> 16U;
  return value;
}

double filter_smooth_step(double value) {
  value = std::clamp(value, 0.0, 1.0);
  return value * value * (3.0 - 2.0 * value);
}

double filter_lattice_noise(double x, double y, std::uint32_t seed) {
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto tx = filter_smooth_step(x - static_cast<double>(x0));
  const auto ty = filter_smooth_step(y - static_cast<double>(y0));
  const auto sample = [seed](std::int32_t sx, std::int32_t sy) {
    return static_cast<double>(filter_noise_hash(sx, sy, seed) & 0xffffU) / 65535.0;
  };
  const auto top = sample(x0, y0) * (1.0 - tx) + sample(x0 + 1, y0) * tx;
  const auto bottom = sample(x0, y0 + 1) * (1.0 - tx) + sample(x0 + 1, y0 + 1) * tx;
  return top * (1.0 - ty) + bottom * ty;
}

double filter_cloud_noise(std::int32_t x, std::int32_t y, int scale, int detail, int contrast, int seed) {
  scale = std::clamp(scale, 12, 512);
  detail = std::clamp(detail, 1, 8);
  contrast = std::clamp(contrast, 0, 100);
  double value = 0.0;
  double amplitude = 1.0;
  double amplitude_sum = 0.0;
  double frequency = 1.0;
  for (int octave = 0; octave < detail; ++octave) {
    value += filter_lattice_noise((static_cast<double>(x) * frequency) / static_cast<double>(scale),
                                  (static_cast<double>(y) * frequency) / static_cast<double>(scale),
                                  static_cast<std::uint32_t>(seed + octave * 101)) *
             amplitude;
    amplitude_sum += amplitude;
    amplitude *= 0.5;
    frequency *= 2.0;
  }
  value /= std::max(0.0001, amplitude_sum);
  const auto contrast_factor = 1.0 + static_cast<double>(contrast) / 65.0;
  return std::clamp((value - 0.5) * contrast_factor + 0.5, 0.0, 1.0);
}

void apply_clouds_to_pixels(PixelBuffer& pixels, QColor foreground, QColor background, int scale, int detail,
                            int contrast, int seed, const FilterProgress* progress) {
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Generating clouds"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto amount = filter_cloud_noise(x, y, scale, detail, contrast, seed);
      auto* px = pixels.pixel(x, y);
      px[0] = filter_clamp_byte(static_cast<double>(background.red()) * (1.0 - amount) +
                                static_cast<double>(foreground.red()) * amount);
      px[1] = filter_clamp_byte(static_cast<double>(background.green()) * (1.0 - amount) +
                                static_cast<double>(foreground.green()) * amount);
      px[2] = filter_clamp_byte(static_cast<double>(background.blue()) * (1.0 - amount) +
                                static_cast<double>(foreground.blue()) * amount);
      if (pixels.format().channels >= 4) {
        px[3] = 255;
      }
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Generating clouds"));
}

void apply_twirl_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int angle_degrees, int radius_percent,
                           const FilterProgress* progress) {
  const auto channels = pixels.format().channels;
  const auto center_x = (static_cast<double>(pixels.width()) - 1.0) * 0.5;
  const auto center_y = (static_cast<double>(pixels.height()) - 1.0) * 0.5;
  const auto radius = std::max(1.0, static_cast<double>(std::min(pixels.width(), pixels.height())) * 0.5 *
                                        static_cast<double>(std::clamp(radius_percent, 1, 100)) / 100.0);
  const auto angle = static_cast<double>(std::clamp(angle_degrees, -720, 720)) * 3.14159265358979323846 / 180.0;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Twisting pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      const auto distance = std::sqrt(dx * dx + dy * dy);
      if (distance > radius) {
        continue;
      }
      const auto falloff = 1.0 - distance / radius;
      const auto source_angle = std::atan2(dy, dx) - angle * falloff * falloff;
      const auto source_x = std::clamp<std::int32_t>(
          static_cast<std::int32_t>(std::lround(center_x + std::cos(source_angle) * distance)), 0,
          pixels.width() - 1);
      const auto source_y = std::clamp<std::int32_t>(
          static_cast<std::int32_t>(std::lround(center_y + std::sin(source_angle) * distance)), 0,
          pixels.height() - 1);
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(source_x, source_y);
      std::copy(src, src + channels, dst);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Twisting pixels"));
}

double filter_sampled_luminance(const PixelBuffer& pixels, double x, double y) {
  x = std::clamp(x, 0.0, static_cast<double>(std::max<std::int32_t>(0, pixels.width() - 1)));
  y = std::clamp(y, 0.0, static_cast<double>(std::max<std::int32_t>(0, pixels.height() - 1)));
  const auto x0 = static_cast<std::int32_t>(std::floor(x));
  const auto y0 = static_cast<std::int32_t>(std::floor(y));
  const auto x1 = std::min<std::int32_t>(pixels.width() - 1, x0 + 1);
  const auto y1 = std::min<std::int32_t>(pixels.height() - 1, y0 + 1);
  const auto tx = x - static_cast<double>(x0);
  const auto ty = y - static_cast<double>(y0);
  const auto l00 = static_cast<double>(filter_luminance(pixels.pixel(x0, y0)));
  const auto l10 = static_cast<double>(filter_luminance(pixels.pixel(x1, y0)));
  const auto l01 = static_cast<double>(filter_luminance(pixels.pixel(x0, y1)));
  const auto l11 = static_cast<double>(filter_luminance(pixels.pixel(x1, y1)));
  const auto top = l00 * (1.0 - tx) + l10 * tx;
  const auto bottom = l01 * (1.0 - tx) + l11 * tx;
  return top * (1.0 - ty) + bottom * ty;
}

void apply_emboss_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int angle_degrees, int height,
                            int amount, const FilterProgress* progress) {
  const auto angle = static_cast<double>(angle_degrees) * kFilterPi / 180.0;
  const auto distance = static_cast<double>(std::clamp(height, 1, 24));
  const auto offset_x = std::cos(angle) * distance;
  const auto offset_y = -std::sin(angle) * distance;
  amount = std::clamp(amount, 0, 300);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Embossing pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto highlight = filter_sampled_luminance(original, static_cast<double>(x) - offset_x,
                                                      static_cast<double>(y) - offset_y);
      const auto shadow = filter_sampled_luminance(original, static_cast<double>(x) + offset_x,
                                                   static_cast<double>(y) + offset_y);
      const auto value = filter_clamp_byte(128.0 + (highlight - shadow) * static_cast<double>(amount) / 100.0);
      auto* px = pixels.pixel(x, y);
      px[0] = value;
      px[1] = value;
      px[2] = value;
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Embossing pixels"));
}

void apply_unsharp_mask_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int amount, int radius,
                                  int threshold, const FilterProgress* progress) {
  amount = std::clamp(amount, 0, 300);
  radius = std::clamp(radius, 1, 12);
  threshold = std::clamp(threshold, 0, 255);
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  const auto total = pixels.height() * 2;
  auto blurred = original;

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, total, QObject::tr("Blurring pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      filter_write_blurred_pixel(blurred, original, x, y, radius, true);
    }
  }

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, pixels.height() + y, total, QObject::tr("Sharpening pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(x, y);
      const auto* soft = blurred.pixel(x, y);
      for (std::uint16_t channel = 0; channel < channels; ++channel) {
        const auto detail = static_cast<int>(src[channel]) - static_cast<int>(soft[channel]);
        dst[channel] = std::abs(detail) < threshold
                           ? src[channel]
                           : filter_clamp_byte(static_cast<int>(src[channel]) + (detail * amount) / 100);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  report_filter_progress(progress, total, total, QObject::tr("Sharpening pixels"));
}

void apply_motion_blur_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int angle_degrees, int distance,
                                 const FilterProgress* progress) {
  distance = std::clamp(distance, 1, 64);
  const auto angle = static_cast<double>(std::clamp(angle_degrees, -180, 180)) * kFilterPi / 180.0;
  const auto step_x = std::cos(angle);
  const auto step_y = -std::sin(angle);

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Blurring pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      FilterPixelAccum accum;
      for (int sample = -distance; sample <= distance; ++sample) {
        filter_accumulate_sample(accum, original, static_cast<double>(x) + step_x * static_cast<double>(sample),
                                 static_cast<double>(y) + step_y * static_cast<double>(sample));
      }
      filter_write_accumulated_pixel(pixels, x, y, accum);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Blurring pixels"));
}

void apply_radial_blur_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int amount, int samples,
                                 const FilterProgress* progress) {
  amount = std::clamp(amount, 0, 100);
  samples = std::clamp(samples, 4, 32);
  const auto center_x = (static_cast<double>(pixels.width()) - 1.0) * 0.5;
  const auto center_y = (static_cast<double>(pixels.height()) - 1.0) * 0.5;
  const auto sweep = static_cast<double>(amount) * 3.6 * kFilterPi / 180.0;

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Blurring pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      FilterPixelAccum accum;
      for (int sample = 0; sample < samples; ++sample) {
        const auto t = samples <= 1 ? 0.0 : static_cast<double>(sample) / static_cast<double>(samples - 1) - 0.5;
        const auto angle = sweep * t;
        const auto source_x = center_x + dx * std::cos(angle) - dy * std::sin(angle);
        const auto source_y = center_y + dx * std::sin(angle) + dy * std::cos(angle);
        filter_accumulate_sample(accum, original, source_x, source_y);
      }
      filter_write_accumulated_pixel(pixels, x, y, accum);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Blurring pixels"));
}

void apply_wave_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int amplitude, int wavelength, int phase,
                          const FilterProgress* progress) {
  amplitude = std::clamp(amplitude, 0, 64);
  wavelength = std::clamp(wavelength, 4, 256);
  const auto phase_radians = static_cast<double>(std::clamp(phase, 0, 360)) * kFilterPi / 180.0;
  const auto frequency = 2.0 * kFilterPi / static_cast<double>(wavelength);

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Distorting pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto source_x = static_cast<double>(x) +
                            std::sin(static_cast<double>(y) * frequency + phase_radians) *
                                static_cast<double>(amplitude);
      const auto source_y = static_cast<double>(y) +
                            std::sin(static_cast<double>(x) * frequency + phase_radians + kFilterPi * 0.5) *
                                static_cast<double>(amplitude) * 0.5;
      filter_copy_sampled_pixel(pixels, original, x, y, source_x, source_y);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Distorting pixels"));
}

void apply_pinch_bloat_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int amount, int radius_percent,
                                 const FilterProgress* progress) {
  amount = std::clamp(amount, -100, 100);
  radius_percent = std::clamp(radius_percent, 1, 100);
  const auto center_x = (static_cast<double>(pixels.width()) - 1.0) * 0.5;
  const auto center_y = (static_cast<double>(pixels.height()) - 1.0) * 0.5;
  const auto radius = std::max(1.0, static_cast<double>(std::min(pixels.width(), pixels.height())) * 0.5 *
                                        static_cast<double>(radius_percent) / 100.0);
  const auto strength = static_cast<double>(amount) / 100.0;

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Distorting pixels"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto dx = static_cast<double>(x) - center_x;
      const auto dy = static_cast<double>(y) - center_y;
      const auto distance = std::sqrt(dx * dx + dy * dy);
      if (distance <= 0.0001 || distance > radius) {
        continue;
      }
      const auto normalized = distance / radius;
      const auto falloff = (1.0 - normalized) * (1.0 - normalized);
      const auto source_distance = std::clamp(distance * (1.0 - strength * falloff * 0.75), 0.0, radius);
      const auto scale = source_distance / distance;
      filter_copy_sampled_pixel(pixels, original, x, y, center_x + dx * scale, center_y + dy * scale);
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Distorting pixels"));
}

void apply_glowing_edges_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int edge_width, int brightness,
                                   int smoothness, const FilterProgress* progress) {
  edge_width = std::clamp(edge_width, 1, 12);
  brightness = std::clamp(brightness, 0, 300);
  smoothness = std::clamp(smoothness, 0, 12);
  auto source = original;
  const auto color_channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  const auto total = pixels.height() * (smoothness > 0 ? 2 : 1);
  int progress_offset = 0;

  if (smoothness > 0) {
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, total, QObject::tr("Blurring pixels"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        filter_write_blurred_pixel(source, original, x, y, smoothness, true);
      }
    }
    progress_offset = pixels.height();
  }

  const auto luminance_at = [&source](std::int32_t x, std::int32_t y) {
    x = std::clamp<std::int32_t>(x, 0, source.width() - 1);
    y = std::clamp<std::int32_t>(y, 0, source.height() - 1);
    return filter_luminance(source.pixel(x, y));
  };
  constexpr std::array<int, 9> kSobelX = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
  constexpr std::array<int, 9> kSobelY = {-1, -2, -1, 0, 0, 0, 1, 2, 1};

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, progress_offset + y, total, QObject::tr("Detecting edges"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      int gx = 0;
      int gy = 0;
      int index = 0;
      for (int ky = -1; ky <= 1; ++ky) {
        for (int kx = -1; kx <= 1; ++kx) {
          const auto luminance = luminance_at(x + kx, y + ky);
          gx += luminance * kSobelX[static_cast<std::size_t>(index)];
          gy += luminance * kSobelY[static_cast<std::size_t>(index)];
          ++index;
        }
      }
      const auto magnitude = std::sqrt(static_cast<double>(gx * gx + gy * gy));
      const auto glow = filter_clamp_byte(magnitude * static_cast<double>(brightness) / 100.0 *
                                          (0.75 + static_cast<double>(edge_width) * 0.25));
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(x, y);
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        const auto colorize = 0.35 + 0.65 * static_cast<double>(src[channel]) / 255.0;
        dst[channel] = filter_clamp_byte(static_cast<double>(glow) * colorize);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  report_filter_progress(progress, total, total, QObject::tr("Detecting edges"));
}

double halftone_cell_offset(double value, double cell_size) {
  auto offset = std::fmod(value, cell_size);
  if (offset < 0.0) {
    offset += cell_size;
  }
  return offset - cell_size * 0.5;
}

void apply_color_halftone_to_pixels(PixelBuffer& pixels, const PixelBuffer& original, int cell_size, int intensity,
                                    int contrast, const FilterProgress* progress) {
  cell_size = std::clamp(cell_size, 4, 64);
  intensity = std::clamp(intensity, 0, 100);
  contrast = std::clamp(contrast, 0, 100);
  constexpr std::array<double, 3> kAngles = {15.0, 75.0, 0.0};
  const auto contrast_factor = 1.0 + static_cast<double>(contrast) / 50.0;
  const auto cell = static_cast<double>(cell_size);
  const auto color_channels = std::min<std::uint16_t>(pixels.format().channels, 3);

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    report_filter_progress(progress, y, pixels.height(), QObject::tr("Rendering halftone"));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(x, y);
      for (std::uint16_t channel = 0; channel < color_channels; ++channel) {
        const auto angle = kAngles[static_cast<std::size_t>(channel)] * kFilterPi / 180.0;
        const auto rotated_x = static_cast<double>(x) * std::cos(angle) + static_cast<double>(y) * std::sin(angle);
        const auto rotated_y = -static_cast<double>(x) * std::sin(angle) + static_cast<double>(y) * std::cos(angle);
        const auto local_x = halftone_cell_offset(rotated_x, cell);
        const auto local_y = halftone_cell_offset(rotated_y, cell);
        auto coverage = 1.0 - static_cast<double>(src[channel]) / 255.0;
        coverage = std::clamp((coverage - 0.5) * contrast_factor + 0.5, 0.0, 1.0);
        const auto radius = std::sqrt(coverage) * cell * 0.48;
        const auto distance = std::sqrt(local_x * local_x + local_y * local_y);
        const auto dot = std::clamp(radius - distance + 1.0, 0.0, 1.0);
        const auto screen = filter_clamp_byte(255.0 * (1.0 - dot));
        dst[channel] = filter_blend_byte(src[channel], screen, intensity);
      }
      if (pixels.format().channels >= 4) {
        dst[3] = src[3];
      }
    }
  }
  report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Rendering halftone"));
}

void apply_filter_with_settings(const QString& identifier, const FilterRegistry& registry, PixelBuffer& pixels,
                                const std::vector<int>& values, QColor foreground, QColor background,
                                const FilterProgress* progress) {
  if (pixels.format().bit_depth != BitDepth::UInt8) {
    throw std::invalid_argument("Filter previews support UInt8 buffers only");
  }
  if (pixels.format().channels < 3 || pixels.empty()) {
    return;
  }

  const auto original = pixels;
  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);

  if (identifier == QStringLiteral("patchy.filters.invert")) {
    const auto amount = filter_value(values, 0, 100);
    const auto total = pixels.height() * 2;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height(), 0, total);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = static_cast<std::uint8_t>(255 - px[channel]);
        }
      }
    }
    blend_filter_with_original(pixels, original, amount, progress, pixels.height(), total);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.brightness_plus")) {
    const auto brightness = std::clamp(filter_value(values, 0, 24), -100, 100);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = filter_clamp_byte(static_cast<int>(px[channel]) + brightness);
        }
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.contrast_plus")) {
    const auto contrast = std::clamp(filter_value(values, 0, 25), -100, 100);
    const auto factor = 1.0 + static_cast<double>(contrast) / 100.0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = filter_clamp_byte((static_cast<double>(px[channel]) - 128.0) * factor + 128.0);
        }
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.grayscale") ||
      identifier == QStringLiteral("patchy.filters.desaturate")) {
    const auto amount = filter_value(values, 0, 100);
    const auto total = pixels.height() * 2;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height(), 0, total);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto luminance = filter_clamp_byte(filter_luminance(px));
        px[0] = luminance;
        px[1] = luminance;
        px[2] = luminance;
      }
    }
    blend_filter_with_original(pixels, original, amount, progress, pixels.height(), total);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.auto_contrast")) {
    const auto amount = filter_value(values, 0, 100);
    std::array<int, 3> min_channel = {255, 255, 255};
    std::array<int, 3> max_channel = {0, 0, 0};
    const auto total = pixels.height() * 3;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, total, QObject::tr("Filtering pixels"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          min_channel[static_cast<std::size_t>(channel)] =
              std::min(min_channel[static_cast<std::size_t>(channel)], static_cast<int>(px[channel]));
          max_channel[static_cast<std::size_t>(channel)] =
              std::max(max_channel[static_cast<std::size_t>(channel)], static_cast<int>(px[channel]));
        }
      }
    }
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, pixels.height() + y, total, QObject::tr("Filtering pixels"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto index = static_cast<std::size_t>(channel);
          const auto range = max_channel[index] - min_channel[index];
          if (range > 0) {
            px[channel] = filter_clamp_byte(((static_cast<int>(px[channel]) - min_channel[index]) * 255) / range);
          }
        }
      }
    }
    blend_filter_with_original(pixels, original, amount, progress, pixels.height() * 2, total);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.soft_glow")) {
    constexpr int kPhaseCount = 6;
    const auto amount = filter_value(values, 0, 100);
    auto glow = pixels;
    auto blur_progress = filter_progress_phase(progress, 0, kPhaseCount);
    apply_builtin_gaussian_blur_filter_pixels(glow, &blur_progress);
    auto glow_tint_progress = filter_progress_phase(progress, 1, kPhaseCount);
    tint_filter_pixels(glow, 26, 14, -4, &glow_tint_progress);
    auto glow_contrast_progress = filter_progress_phase(progress, 2, kPhaseCount);
    adjust_contrast_filter_pixels(glow, 0.9, 128, &glow_contrast_progress);
    auto blend_progress = filter_progress_phase(progress, 3, kPhaseCount);
    blend_overlay_filter_pixels(pixels, glow, 38, &blend_progress);
    auto tint_progress = filter_progress_phase(progress, 4, kPhaseCount);
    tint_filter_pixels(pixels, 8, 4, 0, &tint_progress);
    auto amount_progress = filter_progress_phase(progress, 5, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.punchy_color")) {
    constexpr int kPhaseCount = 4;
    const auto amount = filter_value(values, 0, 100);
    auto contrast_progress = filter_progress_phase(progress, 0, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 1.28, 128, &contrast_progress);
    auto saturation_progress = filter_progress_phase(progress, 1, kPhaseCount);
    adjust_saturation_filter_pixels(pixels, 1.26, &saturation_progress);
    auto sharpen_progress = filter_progress_phase(progress, 2, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.sharpen"), registry, pixels, {100}, foreground,
                               background, &sharpen_progress);
    auto amount_progress = filter_progress_phase(progress, 3, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.noir")) {
    constexpr int kPhaseCount = 5;
    const auto amount = filter_value(values, 0, 100);
    auto grain_progress = filter_progress_phase(progress, 0, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.film_grain"), registry, pixels, {50}, foreground,
                               background, &grain_progress);
    auto grayscale_progress = filter_progress_phase(progress, 1, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.grayscale"), registry, pixels, {100}, foreground,
                               background, &grayscale_progress);
    auto contrast_progress = filter_progress_phase(progress, 2, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 1.55, 128, &contrast_progress);
    auto vignette_progress = filter_progress_phase(progress, 3, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.vignette"), registry, pixels, {55}, foreground,
                               background, &vignette_progress);
    auto amount_progress = filter_progress_phase(progress, 4, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.cinematic_matte")) {
    constexpr int kPhaseCount = 5;
    const auto amount = filter_value(values, 0, 100);
    auto saturation_progress = filter_progress_phase(progress, 0, kPhaseCount);
    adjust_saturation_filter_pixels(pixels, 0.82, &saturation_progress);
    auto contrast_progress = filter_progress_phase(progress, 1, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 0.92, 128, &contrast_progress);
    auto matte_progress = filter_progress_phase(progress, 2, kPhaseCount);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(&matte_progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto luminance = filter_luminance(px);
        const auto shadow = std::clamp(160 - luminance, 0, 160);
        const auto highlight = std::clamp(luminance - 96, 0, 159);
        px[0] = filter_clamp_byte(static_cast<int>(px[0]) + 18 - shadow / 14 + highlight / 18);
        px[1] = filter_clamp_byte(static_cast<int>(px[1]) + 12 + shadow / 18 + highlight / 30);
        px[2] = filter_clamp_byte(static_cast<int>(px[2]) + 18 + shadow / 11 - highlight / 24);
      }
    }
    finish_filter_row_progress(&matte_progress, pixels.height());
    auto vignette_progress = filter_progress_phase(progress, 3, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.vignette"), registry, pixels, {55}, foreground,
                               background, &vignette_progress);
    auto amount_progress = filter_progress_phase(progress, 4, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.vintage_fade")) {
    constexpr int kPhaseCount = 7;
    const auto amount = filter_value(values, 0, 100);
    const auto effect_original = pixels;
    auto tinted = pixels;
    auto sepia_progress = filter_progress_phase(progress, 0, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.sepia"), registry, tinted, {100}, foreground, background,
                               &sepia_progress);
    auto tint_blend_progress = filter_progress_phase(progress, 1, kPhaseCount);
    blend_overlay_filter_pixels(pixels, tinted, 45, &tint_blend_progress);
    auto saturation_progress = filter_progress_phase(progress, 2, kPhaseCount);
    adjust_saturation_filter_pixels(pixels, 0.72, &saturation_progress);
    auto contrast_progress = filter_progress_phase(progress, 3, kPhaseCount);
    adjust_contrast_filter_pixels(pixels, 0.86, 112, &contrast_progress);
    auto fade_progress = filter_progress_phase(progress, 4, kPhaseCount);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(&fade_progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto* src = effect_original.pixel(x, y);
        for (std::uint16_t channel = 0; channel < 3; ++channel) {
          px[channel] = filter_clamp_byte(static_cast<int>(px[channel]) + 16);
          px[channel] = filter_blend_byte(px[channel], src[channel], 12);
        }
      }
    }
    finish_filter_row_progress(&fade_progress, pixels.height());
    auto grain_progress = filter_progress_phase(progress, 5, kPhaseCount);
    apply_filter_with_settings(QStringLiteral("patchy.filters.film_grain"), registry, pixels, {50}, foreground,
                               background, &grain_progress);
    auto amount_progress = filter_progress_phase(progress, 6, kPhaseCount);
    blend_filter_with_original(pixels, original, amount, &amount_progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.sepia")) {
    const auto amount = filter_value(values, 0, 100);
    const auto total = pixels.height() * 2;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height(), 0, total);
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto r = static_cast<int>(px[0]);
        const auto g = static_cast<int>(px[1]);
        const auto b = static_cast<int>(px[2]);
        px[0] = filter_clamp_byte((r * 393 + g * 769 + b * 189) / 1000);
        px[1] = filter_clamp_byte((r * 349 + g * 686 + b * 168) / 1000);
        px[2] = filter_clamp_byte((r * 272 + g * 534 + b * 131) / 1000);
      }
    }
    blend_filter_with_original(pixels, original, amount, progress, pixels.height(), total);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.threshold")) {
    const auto threshold = std::clamp(filter_value(values, 0, 128), 0, 255);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto value = filter_luminance(px) >= threshold ? 255 : 0;
        px[0] = static_cast<std::uint8_t>(value);
        px[1] = static_cast<std::uint8_t>(value);
        px[2] = static_cast<std::uint8_t>(value);
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.posterize")) {
    const auto levels = std::clamp(filter_value(values, 0, 4), 2, 16);
    const auto denominator = std::max(1, levels - 1);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_row_progress(progress, y, pixels.height());
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto bucket = static_cast<int>(std::round(static_cast<double>(px[channel]) * denominator / 255.0));
          px[channel] = filter_clamp_byte(std::round(static_cast<double>(bucket) * 255.0 / denominator));
        }
      }
    }
    finish_filter_row_progress(progress, pixels.height());
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.box_blur") ||
      identifier == QStringLiteral("patchy.filters.gaussian_blur")) {
    const auto radius = std::clamp(filter_value(values, 0,
                                               identifier == QStringLiteral("patchy.filters.gaussian_blur") ? 2 : 1),
                                   1, 12);
    const auto weighted = identifier == QStringLiteral("patchy.filters.gaussian_blur");
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(), QObject::tr("Blurring pixels"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        filter_write_blurred_pixel(pixels, original, x, y, radius, weighted);
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Blurring pixels"));
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.sharpen")) {
    const auto amount = std::clamp(filter_value(values, 0, 100), 0, 300);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(), QObject::tr("Sharpening pixels"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* dst = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto center = static_cast<int>(original.pixel(x, y)[channel]) * 5;
          const auto left = x > 0 ? static_cast<int>(original.pixel(x - 1, y)[channel])
                                  : static_cast<int>(original.pixel(x, y)[channel]);
          const auto right = x + 1 < pixels.width() ? static_cast<int>(original.pixel(x + 1, y)[channel])
                                                    : static_cast<int>(original.pixel(x, y)[channel]);
          const auto up = y > 0 ? static_cast<int>(original.pixel(x, y - 1)[channel])
                                : static_cast<int>(original.pixel(x, y)[channel]);
          const auto down = y + 1 < pixels.height() ? static_cast<int>(original.pixel(x, y + 1)[channel])
                                                    : static_cast<int>(original.pixel(x, y)[channel]);
          const auto sharpened = center - left - right - up - down;
          dst[channel] = filter_clamp_byte(static_cast<int>(original.pixel(x, y)[channel]) +
                                           ((sharpened - static_cast<int>(original.pixel(x, y)[channel])) * amount) /
                                               100);
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Sharpening pixels"));
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.unsharp_mask")) {
    const auto amount = std::clamp(filter_value(values, 0, 150), 0, 300);
    const auto radius = std::clamp(filter_value(values, 1, 2), 1, 12);
    const auto threshold = std::clamp(filter_value(values, 2, 8), 0, 255);
    apply_unsharp_mask_to_pixels(pixels, original, amount, radius, threshold, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.motion_blur")) {
    const auto angle = std::clamp(filter_value(values, 0, 0), -180, 180);
    const auto distance = std::clamp(filter_value(values, 1, 12), 1, 64);
    apply_motion_blur_to_pixels(pixels, original, angle, distance, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.radial_blur")) {
    const auto amount = std::clamp(filter_value(values, 0, 35), 0, 100);
    const auto samples = std::clamp(filter_value(values, 1, 16), 4, 32);
    apply_radial_blur_to_pixels(pixels, original, amount, samples, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.edge_detect")) {
    constexpr std::array<int, 9> sobel_x = {-1, 0, 1, -2, 0, 2, -1, 0, 1};
    constexpr std::array<int, 9> sobel_y = {-1, -2, -1, 0, 0, 0, 1, 2, 1};
    const auto strength = std::clamp(filter_value(values, 0, 100), 0, 300);
    const auto luminance_at = [&original](std::int32_t x, std::int32_t y) {
      x = std::clamp<std::int32_t>(x, 0, original.width() - 1);
      y = std::clamp<std::int32_t>(y, 0, original.height() - 1);
      return filter_luminance(original.pixel(x, y));
    };
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(), QObject::tr("Detecting edges"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        int gx = 0;
        int gy = 0;
        int index = 0;
        for (int ky = -1; ky <= 1; ++ky) {
          for (int kx = -1; kx <= 1; ++kx) {
            const auto luminance = luminance_at(x + kx, y + ky);
            gx += luminance * sobel_x[static_cast<std::size_t>(index)];
            gy += luminance * sobel_y[static_cast<std::size_t>(index)];
            ++index;
          }
        }
        const auto magnitude = filter_clamp_byte(std::sqrt(gx * gx + gy * gy) * static_cast<double>(strength) / 100.0);
        auto* px = pixels.pixel(x, y);
        px[0] = magnitude;
        px[1] = magnitude;
        px[2] = magnitude;
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Detecting edges"));
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.glowing_edges")) {
    const auto edge_width = std::clamp(filter_value(values, 0, 2), 1, 12);
    const auto brightness = std::clamp(filter_value(values, 1, 140), 0, 300);
    const auto smoothness = std::clamp(filter_value(values, 2, 2), 0, 12);
    apply_glowing_edges_to_pixels(pixels, original, edge_width, brightness, smoothness, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.emboss")) {
    const auto angle = std::clamp(filter_value(values, 0, 135), -180, 180);
    const auto height = std::clamp(filter_value(values, 1, 2), 1, 24);
    const auto amount = std::clamp(filter_value(values, 2, 100), 0, 300);
    apply_emboss_to_pixels(pixels, original, angle, height, amount, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.twirl")) {
    const auto angle = std::clamp(filter_value(values, 0, 180), -720, 720);
    const auto radius = std::clamp(filter_value(values, 1, 100), 1, 100);
    apply_twirl_to_pixels(pixels, original, angle, radius, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.wave")) {
    const auto amplitude = std::clamp(filter_value(values, 0, 12), 0, 64);
    const auto wavelength = std::clamp(filter_value(values, 1, 48), 4, 256);
    const auto phase = std::clamp(filter_value(values, 2, 0), 0, 360);
    apply_wave_to_pixels(pixels, original, amplitude, wavelength, phase, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.pinch_bloat")) {
    const auto amount = std::clamp(filter_value(values, 0, 35), -100, 100);
    const auto radius = std::clamp(filter_value(values, 1, 100), 1, 100);
    apply_pinch_bloat_to_pixels(pixels, original, amount, radius, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.clouds")) {
    const auto scale = std::clamp(filter_value(values, 0, 96), 12, 512);
    const auto detail = std::clamp(filter_value(values, 1, 6), 1, 8);
    const auto contrast = std::clamp(filter_value(values, 2, 40), 0, 100);
    const auto seed = std::clamp(filter_value(values, 3, 1), 1, 9999);
    apply_clouds_to_pixels(pixels, foreground, background, scale, detail, contrast, seed, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.pixelate")) {
    const auto block_size = std::clamp(filter_value(values, 0, 4), 2, 32);
    for (std::int32_t block_y = 0; block_y < pixels.height(); block_y += block_size) {
      report_filter_progress(progress, block_y, pixels.height(), QObject::tr("Pixelating blocks"));
      for (std::int32_t block_x = 0; block_x < pixels.width(); block_x += block_size) {
        const auto block_width = std::min(block_size, pixels.width() - block_x);
        const auto block_height = std::min(block_size, pixels.height() - block_y);
        FilterPixelAccum accum;
        for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
          for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
            filter_accumulate_pixel(accum, original, original.pixel(x, y), 1.0);
          }
        }
        for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
          for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
            filter_write_accumulated_pixel(pixels, x, y, accum);
          }
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Pixelating blocks"));
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.color_halftone")) {
    const auto cell_size = std::clamp(filter_value(values, 0, 10), 4, 64);
    const auto intensity = std::clamp(filter_value(values, 1, 75), 0, 100);
    const auto contrast = std::clamp(filter_value(values, 2, 60), 0, 100);
    apply_color_halftone_to_pixels(pixels, original, cell_size, intensity, contrast, progress);
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.film_grain")) {
    const auto amount = std::clamp(filter_value(values, 0, 50), 0, 100);
    const auto amplitude = static_cast<int>(std::round(static_cast<double>(amount) * 0.3));
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(), QObject::tr("Adding grain"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto span = amplitude * 2 + 1;
          const auto grain = span <= 1 ? 0
                                       : static_cast<int>(filter_coordinate_hash(x, y, channel) %
                                                          static_cast<std::uint32_t>(span)) -
                                             amplitude;
          px[channel] = filter_clamp_byte(static_cast<int>(px[channel]) + grain);
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Adding grain"));
    return;
  }

  if (identifier == QStringLiteral("patchy.filters.vignette")) {
    const auto strength = std::clamp(filter_value(values, 0, 55), 0, 100);
    const auto center_x = (static_cast<double>(pixels.width()) - 1.0) * 0.5;
    const auto center_y = (static_cast<double>(pixels.height()) - 1.0) * 0.5;
    const auto max_distance = std::sqrt(center_x * center_x + center_y * center_y);
    if (max_distance <= 0.0) {
      return;
    }
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(), QObject::tr("Applying vignette"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto dx = static_cast<double>(x) - center_x;
        const auto dy = static_cast<double>(y) - center_y;
        const auto distance = std::sqrt(dx * dx + dy * dy) / max_distance;
        const auto darken = 1.0 - (static_cast<double>(strength) / 100.0) * std::clamp(distance * distance, 0.0, 1.0);
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = filter_clamp_byte(static_cast<double>(px[channel]) * darken);
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Applying vignette"));
    return;
  }

  report_filter_progress(progress, 0, 1, QObject::tr("Filtering pixels"));
  registry.apply(identifier.toStdString(), pixels);
  report_filter_progress(progress, 1, 1, QObject::tr("Filtering pixels"));
  blend_filter_with_original(pixels, original, filter_value(values, 0, 100), progress);
}

void restore_pixels_outside_selection(PixelBuffer& pixels, const PixelBuffer& original, const QRegion& selection,
                                      Rect bounds) {
  if (selection.isEmpty() || pixels.format() != original.format() || pixels.width() != original.width() ||
      pixels.height() != original.height()) {
    return;
  }

  const auto selected = layer_selection_region(selection, bounds);
  if (selected.isEmpty()) {
    pixels = original;
    return;
  }

  auto filtered = std::move(pixels);
  pixels = original;
  const auto pixel_bytes = bytes_per_pixel(pixels.format());
  for (const auto& rect : selected) {
    const auto local_left = std::max(0, rect.x() - bounds.x);
    const auto local_top = std::max(0, rect.y() - bounds.y);
    const auto local_right = std::min<std::int32_t>(pixels.width(), rect.x() + rect.width() - bounds.x);
    const auto local_bottom = std::min<std::int32_t>(pixels.height(), rect.y() + rect.height() - bounds.y);
    const auto row_bytes = static_cast<std::size_t>(std::max(0, local_right - local_left)) * pixel_bytes;
    if (row_bytes == 0U) {
      continue;
    }
    for (std::int32_t y = local_top; y < local_bottom; ++y) {
      const auto* src = filtered.pixel(local_left, y);
      auto* dst = pixels.pixel(local_left, y);
      std::copy(src, src + row_bytes, dst);
    }
  }
}

PixelBuffer build_filter_preview_pixels(const PixelBuffer& original, const QRegion& selection, Rect bounds,
                                        const QString& identifier, const FilterRegistry& registry,
                                        const FilterPreviewSettings& settings, QColor foreground, QColor background,
                                        const FilterProgress* progress) {
  auto pixels = original;
  if (!settings.preview_enabled) {
    return pixels;
  }
  if (!selection.isEmpty() && layer_selection_region(selection, bounds).isEmpty()) {
    return pixels;
  }
  apply_filter_with_settings(identifier, registry, pixels, settings.values, foreground, background, progress);
  restore_pixels_outside_selection(pixels, original, selection, bounds);
  return pixels;
}

bool pixel_buffers_equal(const PixelBuffer& lhs, const PixelBuffer& rhs) {
  return lhs.format() == rhs.format() && lhs.width() == rhs.width() && lhs.height() == rhs.height() &&
         lhs.data().size() == rhs.data().size() && std::equal(lhs.data().begin(), lhs.data().end(), rhs.data().begin());
}

bool editable_rgb8_layer(const Layer* layer) {
  return layer != nullptr && layer->kind() == LayerKind::Pixel && layer->pixels().format().bit_depth == BitDepth::UInt8 &&
         layer->pixels().format().channels >= 3;
}

void apply_levels_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, LevelsSettings settings,
                            const FilterProgress* progress) {
  settings.black_input = std::clamp(settings.black_input, 0, 254);
  settings.white_input = std::clamp(settings.white_input, settings.black_input + 1, 255);
  settings.gamma_percent = std::clamp(settings.gamma_percent, 10, 999);
  const auto input_range = static_cast<double>(settings.white_input - settings.black_input);
  const auto gamma = static_cast<double>(settings.gamma_percent) / 100.0;
  const auto inverse_gamma = gamma <= 0.0 ? 1.0 : 1.0 / gamma;

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    for (std::uint16_t channel = 0; channel < 3; ++channel) {
      const auto normalized =
          std::clamp((static_cast<double>(px[channel]) - static_cast<double>(settings.black_input)) / input_range, 0.0,
                     1.0);
      px[channel] = filter_clamp_byte(std::pow(normalized, inverse_gamma) * 255.0);
    }
  });
}

void apply_curves_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, CurvesSettings settings,
                            const FilterProgress* progress) {
  settings.shadow_output = std::clamp(settings.shadow_output, 0, 255);
  settings.midtone_output = std::clamp(settings.midtone_output, 0, 255);
  settings.highlight_output = std::clamp(settings.highlight_output, 0, 255);
  const auto map_value = [settings](std::uint8_t value) {
    const auto input = static_cast<double>(value);
    double output = 0.0;
    if (input <= 128.0) {
      const auto t = input / 128.0;
      output = static_cast<double>(settings.shadow_output) +
               (static_cast<double>(settings.midtone_output) - static_cast<double>(settings.shadow_output)) * t;
    } else {
      const auto t = (input - 128.0) / 127.0;
      output = static_cast<double>(settings.midtone_output) +
               (static_cast<double>(settings.highlight_output) - static_cast<double>(settings.midtone_output)) * t;
    }
    return filter_clamp_byte(output);
  };

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    px[0] = map_value(px[0]);
    px[1] = map_value(px[1]);
    px[2] = map_value(px[2]);
  });
}

void apply_hue_saturation_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                    HueSaturationSettings settings, const FilterProgress* progress) {
  settings.hue_shift = std::clamp(settings.hue_shift, -180, 180);
  settings.saturation_delta = std::clamp(settings.saturation_delta, -100, 100);
  settings.lightness_delta = std::clamp(settings.lightness_delta, -100, 100);
  const auto channels = pixels.format().channels;
  const auto saturation_offset =
      static_cast<int>(std::round(static_cast<double>(settings.saturation_delta) * 255.0 / 100.0));
  const auto lightness_offset =
      static_cast<int>(std::round(static_cast<double>(settings.lightness_delta) * 255.0 / 100.0));

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    QColor color(px[0], px[1], px[2]);
    const auto original_alpha = channels >= 4 ? px[3] : 255;
    auto hue = color.hslHue();
    if (hue < 0) {
      hue = 0;
    }
    hue = (hue + settings.hue_shift) % 360;
    if (hue < 0) {
      hue += 360;
    }
    const auto saturation = std::clamp(color.hslSaturation() + saturation_offset, 0, 255);
    const auto lightness = std::clamp(color.lightness() + lightness_offset, 0, 255);
    const auto adjusted = QColor::fromHsl(hue, saturation, lightness);
    px[0] = static_cast<std::uint8_t>(adjusted.red());
    px[1] = static_cast<std::uint8_t>(adjusted.green());
    px[2] = static_cast<std::uint8_t>(adjusted.blue());
    if (channels >= 4) {
      px[3] = original_alpha;
    }
  });
}

void apply_color_balance_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                   ColorBalanceSettings settings, const FilterProgress* progress) {
  settings.cyan_red = std::clamp(settings.cyan_red, -100, 100);
  settings.magenta_green = std::clamp(settings.magenta_green, -100, 100);
  settings.yellow_blue = std::clamp(settings.yellow_blue, -100, 100);
  const auto red_delta = static_cast<int>(std::round(static_cast<double>(settings.cyan_red) * 255.0 / 100.0));
  const auto green_delta =
      static_cast<int>(std::round(static_cast<double>(settings.magenta_green) * 255.0 / 100.0));
  const auto blue_delta = static_cast<int>(std::round(static_cast<double>(settings.yellow_blue) * 255.0 / 100.0));

  for_each_selected_pixel(pixels, bounds, selection, progress, [&](std::int32_t x, std::int32_t y) {
    auto* px = pixels.pixel(x, y);
    px[0] = filter_clamp_byte(static_cast<int>(px[0]) + red_delta);
    px[1] = filter_clamp_byte(static_cast<int>(px[1]) + green_delta);
    px[2] = filter_clamp_byte(static_cast<int>(px[2]) + blue_delta);
  });
}


std::optional<LevelsSettings> request_levels_settings(
    QWidget* parent, std::function<void(bool, const LevelsSettings&)> preview_changed, LevelsSettings initial) {
  initial.black_input = std::clamp(initial.black_input, 0, 254);
  initial.white_input = std::clamp(initial.white_input, initial.black_input + 1, 255);
  initial.gamma_percent = std::clamp(initial.gamma_percent, 10, 999);
  return request_adjustment_settings_dialog<LevelsSettings>(
      parent, QStringLiteral("patchyLevelsDialog"), QObject::tr("Levels"), QStringLiteral("levelsPreviewCheck"),
      {{QObject::tr("Black Input"), QStringLiteral("levelsBlackInput"), 0, 254, initial.black_input, {}},
       {QObject::tr("White Input"), QStringLiteral("levelsWhiteInput"), 1, 255, initial.white_input, {}},
       {QObject::tr("Gamma"), QStringLiteral("levelsGamma"), 10, 999, initial.gamma_percent, QStringLiteral("%")}},
      [](const std::vector<QSpinBox*>& spins) {
        return LevelsSettings{spins[0]->value(), spins[1]->value(), spins[2]->value()};
      },
      std::move(preview_changed), [](QDialog& dialog, const std::vector<QSpinBox*>& spins) {
        auto* black = spins[0];
        auto* white = spins[1];
        QObject::connect(black, &QSpinBox::valueChanged, &dialog, [white](int value) {
          if (white->value() <= value) {
            white->setValue(std::min(255, value + 1));
          }
        });
        QObject::connect(white, &QSpinBox::valueChanged, &dialog, [black](int value) {
          if (black->value() >= value) {
            black->setValue(std::max(0, value - 1));
          }
        });
      });
}

std::optional<CurvesSettings> request_curves_settings(
    QWidget* parent, std::function<void(bool, const CurvesSettings&)> preview_changed, CurvesSettings initial) {
  initial.shadow_output = std::clamp(initial.shadow_output, 0, 255);
  initial.midtone_output = std::clamp(initial.midtone_output, 0, 255);
  initial.highlight_output = std::clamp(initial.highlight_output, 0, 255);
  return request_adjustment_settings_dialog<CurvesSettings>(
      parent, QStringLiteral("patchyCurvesDialog"), QObject::tr("Curves"), QStringLiteral("curvesPreviewCheck"),
      {{QObject::tr("Shadows Output"), QStringLiteral("curvesShadowOutput"), 0, 255, initial.shadow_output, {}},
       {QObject::tr("Midtones Output"), QStringLiteral("curvesMidtoneOutput"), 0, 255, initial.midtone_output, {}},
       {QObject::tr("Highlights Output"), QStringLiteral("curvesHighlightOutput"), 0, 255, initial.highlight_output, {}}},
      [](const std::vector<QSpinBox*>& spins) {
        return CurvesSettings{spins[0]->value(), spins[1]->value(), spins[2]->value()};
      },
      std::move(preview_changed));
}

std::optional<HueSaturationSettings> request_hue_saturation_settings(
    QWidget* parent, std::function<void(bool, const HueSaturationSettings&)> preview_changed,
    HueSaturationSettings initial) {
  initial.hue_shift = std::clamp(initial.hue_shift, -180, 180);
  initial.saturation_delta = std::clamp(initial.saturation_delta, -100, 100);
  initial.lightness_delta = std::clamp(initial.lightness_delta, -100, 100);
  return request_adjustment_settings_dialog<HueSaturationSettings>(
      parent, QStringLiteral("patchyHueSaturationDialog"), QObject::tr("Hue/Saturation"),
      QStringLiteral("hueSaturationPreviewCheck"),
      {{QObject::tr("Hue"), QStringLiteral("hueSaturationHue"), -180, 180, initial.hue_shift, {}},
       {QObject::tr("Saturation"), QStringLiteral("hueSaturationSaturation"), -100, 100, initial.saturation_delta, {}},
       {QObject::tr("Lightness"), QStringLiteral("hueSaturationLightness"), -100, 100, initial.lightness_delta, {}}},
      [](const std::vector<QSpinBox*>& spins) {
        return HueSaturationSettings{spins[0]->value(), spins[1]->value(), spins[2]->value()};
      },
      std::move(preview_changed));
}

std::optional<ColorBalanceSettings> request_color_balance_settings(
    QWidget* parent, std::function<void(bool, const ColorBalanceSettings&)> preview_changed,
    ColorBalanceSettings initial) {
  initial.cyan_red = std::clamp(initial.cyan_red, -100, 100);
  initial.magenta_green = std::clamp(initial.magenta_green, -100, 100);
  initial.yellow_blue = std::clamp(initial.yellow_blue, -100, 100);
  return request_adjustment_settings_dialog<ColorBalanceSettings>(
      parent, QStringLiteral("patchyColorBalanceDialog"), QObject::tr("Color Balance"),
      QStringLiteral("colorBalancePreviewCheck"),
      {{QObject::tr("Cyan / Red"), QStringLiteral("colorBalanceCyanRed"), -100, 100, initial.cyan_red, {}},
       {QObject::tr("Magenta / Green"), QStringLiteral("colorBalanceMagentaGreen"), -100, 100,
        initial.magenta_green, {}},
       {QObject::tr("Yellow / Blue"), QStringLiteral("colorBalanceYellowBlue"), -100, 100, initial.yellow_blue, {}}},
      [](const std::vector<QSpinBox*>& spins) {
        return ColorBalanceSettings{spins[0]->value(), spins[1]->value(), spins[2]->value()};
      },
      std::move(preview_changed));
}

}  // namespace patchy::ui
