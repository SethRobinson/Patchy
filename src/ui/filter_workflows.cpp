#include "ui/filter_workflows.hpp"

#include "ui/dialog_utils.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPoint>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
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

namespace photoslop::ui {

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

  emit_preview();
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

bool is_adjustment_only_filter(const QString& identifier) {
  return identifier == QStringLiteral("photoslop.filters.invert") ||
         identifier == QStringLiteral("photoslop.filters.brightness_plus") ||
         identifier == QStringLiteral("photoslop.filters.contrast_plus") ||
         identifier == QStringLiteral("photoslop.filters.grayscale") ||
         identifier == QStringLiteral("photoslop.filters.desaturate") ||
         identifier == QStringLiteral("photoslop.filters.auto_contrast");
}

FilterDialogSpec filter_dialog_spec_for(const FilterDefinition& filter) {
  const auto identifier = QString::fromStdString(filter.identifier);
  const auto display_name = QString::fromStdString(filter.display_name);
  const auto amount_control = [](int value = 100) {
    return FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 100, value,
                             QStringLiteral("%")};
  };

  if (identifier == QStringLiteral("photoslop.filters.invert")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("photoslop.filters.brightness_plus")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Brightness"), QStringLiteral("filterBrightness"), -100, 100, 24, {}}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.contrast_plus")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterContrast"), -100, 100, 25,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.grayscale") ||
      identifier == QStringLiteral("photoslop.filters.desaturate")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("photoslop.filters.auto_contrast")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("photoslop.filters.soft_glow")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Glow"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.punchy_color") ||
      identifier == QStringLiteral("photoslop.filters.cinematic_matte")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Intensity"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.noir")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.vintage_fade")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Fade"), QStringLiteral("filterAmount"), 0, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.sepia")) {
    return {identifier, display_name, {amount_control()}};
  }
  if (identifier == QStringLiteral("photoslop.filters.threshold")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Threshold"), QStringLiteral("filterThreshold"), 0, 255, 128, {}}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.posterize")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Levels"), QStringLiteral("filterLevels"), 2, 16, 4, {}}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.box_blur")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 12, 1,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.sharpen")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 300, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.gaussian_blur")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 12, 2,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.edge_detect")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Strength"), QStringLiteral("filterStrength"), 0, 300, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.emboss")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Angle"), QStringLiteral("filterAngle"), -180, 180, 135,
                               QStringLiteral(" deg")},
             FilterControlSpec{QObject::tr("Height"), QStringLiteral("filterHeight"), 1, 24, 2,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterDepth"), 0, 300, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.twirl")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Angle"), QStringLiteral("filterAngle"), -720, 720, 180,
                               QStringLiteral(" deg")},
             FilterControlSpec{QObject::tr("Radius"), QStringLiteral("filterRadius"), 1, 100, 100,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.clouds")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Scale"), QStringLiteral("filterScale"), 12, 512, 96,
                               QStringLiteral(" px")},
             FilterControlSpec{QObject::tr("Detail"), QStringLiteral("filterDetail"), 1, 8, 6, {}},
             FilterControlSpec{QObject::tr("Contrast"), QStringLiteral("filterContrast"), 0, 100, 40,
                               QStringLiteral("%")},
             FilterControlSpec{QObject::tr("Seed"), QStringLiteral("filterSeed"), 1, 9999, 1, {}}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.pixelate")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Block Size"), QStringLiteral("filterBlockSize"), 2, 32, 4,
                               QStringLiteral(" px")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.film_grain")) {
    return {identifier,
            display_name,
            {FilterControlSpec{QObject::tr("Amount"), QStringLiteral("filterAmount"), 0, 100, 50,
                               QStringLiteral("%")}}};
  }
  if (identifier == QStringLiteral("photoslop.filters.vignette")) {
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
  dialog.setObjectName(QStringLiteral("photoslopFilterDialog"));
  dialog.setProperty("photoslop.filterIdentifier", spec.identifier);
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

  emit_preview();
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

void blend_filter_with_original(PixelBuffer& pixels, const PixelBuffer& original, int amount_percent) {
  amount_percent = std::clamp(amount_percent, 0, 100);
  if (amount_percent >= 100) {
    return;
  }
  if (amount_percent <= 0 || pixels.format() != original.format() || pixels.width() != original.width() ||
      pixels.height() != original.height()) {
    pixels = original;
    return;
  }

  const auto channels = std::min<std::uint16_t>(pixels.format().channels, 3);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
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
}

std::uint8_t filter_blurred_channel(const PixelBuffer& original, std::int32_t x, std::int32_t y,
                                    std::uint16_t channel, int radius, bool weighted) {
  radius = std::clamp(radius, 1, 32);
  int sum = 0;
  int weight_sum = 0;
  for (int dy = -radius; dy <= radius; ++dy) {
    const auto sy = std::clamp<std::int32_t>(y + dy, 0, original.height() - 1);
    const auto y_weight = weighted ? radius + 1 - std::abs(dy) : 1;
    for (int dx = -radius; dx <= radius; ++dx) {
      const auto sx = std::clamp<std::int32_t>(x + dx, 0, original.width() - 1);
      const auto x_weight = weighted ? radius + 1 - std::abs(dx) : 1;
      const auto weight = x_weight * y_weight;
      sum += static_cast<int>(original.pixel(sx, sy)[channel]) * weight;
      weight_sum += weight;
    }
  }
  return filter_clamp_byte((sum + weight_sum / 2) / std::max(1, weight_sum));
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
  const auto angle = static_cast<double>(angle_degrees) * 3.14159265358979323846 / 180.0;
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

  if (identifier == QStringLiteral("photoslop.filters.invert")) {
    const auto amount = filter_value(values, 0, 100);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = static_cast<std::uint8_t>(255 - px[channel]);
        }
      }
    }
    blend_filter_with_original(pixels, original, amount);
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.brightness_plus")) {
    const auto brightness = std::clamp(filter_value(values, 0, 24), -100, 100);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = filter_clamp_byte(static_cast<int>(px[channel]) + brightness);
        }
      }
    }
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.contrast_plus")) {
    const auto contrast = std::clamp(filter_value(values, 0, 25), -100, 100);
    const auto factor = 1.0 + static_cast<double>(contrast) / 100.0;
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          px[channel] = filter_clamp_byte((static_cast<double>(px[channel]) - 128.0) * factor + 128.0);
        }
      }
    }
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.grayscale") ||
      identifier == QStringLiteral("photoslop.filters.desaturate")) {
    const auto amount = filter_value(values, 0, 100);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto luminance = filter_clamp_byte(filter_luminance(px));
        px[0] = luminance;
        px[1] = luminance;
        px[2] = luminance;
      }
    }
    blend_filter_with_original(pixels, original, amount);
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.auto_contrast")) {
    const auto amount = filter_value(values, 0, 100);
    std::array<int, 3> min_channel = {255, 255, 255};
    std::array<int, 3> max_channel = {0, 0, 0};
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
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
    blend_filter_with_original(pixels, original, amount);
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.sepia")) {
    const auto amount = filter_value(values, 0, 100);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
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
    blend_filter_with_original(pixels, original, amount);
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.threshold")) {
    const auto threshold = std::clamp(filter_value(values, 0, 128), 0, 255);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        const auto value = filter_luminance(px) >= threshold ? 255 : 0;
        px[0] = static_cast<std::uint8_t>(value);
        px[1] = static_cast<std::uint8_t>(value);
        px[2] = static_cast<std::uint8_t>(value);
      }
    }
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.posterize")) {
    const auto levels = std::clamp(filter_value(values, 0, 4), 2, 16);
    const auto denominator = std::max(1, levels - 1);
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* px = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          const auto bucket = static_cast<int>(std::round(static_cast<double>(px[channel]) * denominator / 255.0));
          px[channel] = filter_clamp_byte(std::round(static_cast<double>(bucket) * 255.0 / denominator));
        }
      }
    }
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.box_blur") ||
      identifier == QStringLiteral("photoslop.filters.gaussian_blur")) {
    const auto radius = std::clamp(filter_value(values, 0,
                                               identifier == QStringLiteral("photoslop.filters.gaussian_blur") ? 2 : 1),
                                   1, 12);
    const auto weighted = identifier == QStringLiteral("photoslop.filters.gaussian_blur");
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      report_filter_progress(progress, y, pixels.height(), QObject::tr("Blurring pixels"));
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        auto* dst = pixels.pixel(x, y);
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          dst[channel] = filter_blurred_channel(original, x, y, channel, radius, weighted);
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Blurring pixels"));
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.sharpen")) {
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

  if (identifier == QStringLiteral("photoslop.filters.edge_detect")) {
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

  if (identifier == QStringLiteral("photoslop.filters.emboss")) {
    const auto angle = std::clamp(filter_value(values, 0, 135), -180, 180);
    const auto height = std::clamp(filter_value(values, 1, 2), 1, 24);
    const auto amount = std::clamp(filter_value(values, 2, 100), 0, 300);
    apply_emboss_to_pixels(pixels, original, angle, height, amount, progress);
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.twirl")) {
    const auto angle = std::clamp(filter_value(values, 0, 180), -720, 720);
    const auto radius = std::clamp(filter_value(values, 1, 100), 1, 100);
    apply_twirl_to_pixels(pixels, original, angle, radius, progress);
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.clouds")) {
    const auto scale = std::clamp(filter_value(values, 0, 96), 12, 512);
    const auto detail = std::clamp(filter_value(values, 1, 6), 1, 8);
    const auto contrast = std::clamp(filter_value(values, 2, 40), 0, 100);
    const auto seed = std::clamp(filter_value(values, 3, 1), 1, 9999);
    apply_clouds_to_pixels(pixels, foreground, background, scale, detail, contrast, seed, progress);
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.pixelate")) {
    const auto block_size = std::clamp(filter_value(values, 0, 4), 2, 32);
    for (std::int32_t block_y = 0; block_y < pixels.height(); block_y += block_size) {
      report_filter_progress(progress, block_y, pixels.height(), QObject::tr("Pixelating blocks"));
      for (std::int32_t block_x = 0; block_x < pixels.width(); block_x += block_size) {
        const auto block_width = std::min(block_size, pixels.width() - block_x);
        const auto block_height = std::min(block_size, pixels.height() - block_y);
        const auto count = std::max<std::int32_t>(1, block_width * block_height);
        std::array<int, 3> sum = {0, 0, 0};
        for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
          for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
            const auto* src = original.pixel(x, y);
            for (std::uint16_t channel = 0; channel < channels; ++channel) {
              sum[static_cast<std::size_t>(channel)] += src[channel];
            }
          }
        }
        std::array<std::uint8_t, 3> average = {0, 0, 0};
        for (std::uint16_t channel = 0; channel < channels; ++channel) {
          average[static_cast<std::size_t>(channel)] =
              filter_clamp_byte(sum[static_cast<std::size_t>(channel)] / count);
        }
        for (std::int32_t y = block_y; y < block_y + block_height; ++y) {
          for (std::int32_t x = block_x; x < block_x + block_width; ++x) {
            auto* dst = pixels.pixel(x, y);
            for (std::uint16_t channel = 0; channel < channels; ++channel) {
              dst[channel] = average[static_cast<std::size_t>(channel)];
            }
          }
        }
      }
    }
    report_filter_progress(progress, pixels.height(), pixels.height(), QObject::tr("Pixelating blocks"));
    return;
  }

  if (identifier == QStringLiteral("photoslop.filters.film_grain")) {
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

  if (identifier == QStringLiteral("photoslop.filters.vignette")) {
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

  registry.apply(identifier.toStdString(), pixels);
  blend_filter_with_original(pixels, original, filter_value(values, 0, 100));
}

void restore_pixels_outside_selection(PixelBuffer& pixels, const PixelBuffer& original, const QRegion& selection,
                                      Rect bounds) {
  if (selection.isEmpty() || pixels.format() != original.format() || pixels.width() != original.width() ||
      pixels.height() != original.height()) {
    return;
  }

  const auto channels = pixels.format().channels;
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
        continue;
      }
      auto* dst = pixels.pixel(x, y);
      const auto* src = original.pixel(x, y);
      std::copy(src, src + channels, dst);
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

void apply_levels_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, LevelsSettings settings) {
  settings.black_input = std::clamp(settings.black_input, 0, 254);
  settings.white_input = std::clamp(settings.white_input, settings.black_input + 1, 255);
  settings.gamma_percent = std::clamp(settings.gamma_percent, 10, 999);
  const auto input_range = static_cast<double>(settings.white_input - settings.black_input);
  const auto gamma = static_cast<double>(settings.gamma_percent) / 100.0;
  const auto inverse_gamma = gamma <= 0.0 ? 1.0 : 1.0 / gamma;

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (!selection.isEmpty() && !selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
        continue;
      }
      auto* px = pixels.pixel(x, y);
      for (std::uint16_t channel = 0; channel < 3; ++channel) {
        const auto normalized =
            std::clamp((static_cast<double>(px[channel]) - static_cast<double>(settings.black_input)) / input_range,
                       0.0, 1.0);
        px[channel] = filter_clamp_byte(std::pow(normalized, inverse_gamma) * 255.0);
      }
    }
  }
}

void apply_curves_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection, CurvesSettings settings) {
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

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (!selection.isEmpty() && !selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
        continue;
      }
      auto* px = pixels.pixel(x, y);
      px[0] = map_value(px[0]);
      px[1] = map_value(px[1]);
      px[2] = map_value(px[2]);
    }
  }
}

void apply_hue_saturation_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                    HueSaturationSettings settings) {
  settings.hue_shift = std::clamp(settings.hue_shift, -180, 180);
  settings.saturation_delta = std::clamp(settings.saturation_delta, -100, 100);
  settings.lightness_delta = std::clamp(settings.lightness_delta, -100, 100);
  const auto channels = pixels.format().channels;
  const auto saturation_offset =
      static_cast<int>(std::round(static_cast<double>(settings.saturation_delta) * 255.0 / 100.0));
  const auto lightness_offset =
      static_cast<int>(std::round(static_cast<double>(settings.lightness_delta) * 255.0 / 100.0));

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (!selection.isEmpty() && !selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
        continue;
      }
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
    }
  }
}

void apply_color_balance_to_pixels(PixelBuffer& pixels, Rect bounds, const QRegion& selection,
                                   ColorBalanceSettings settings) {
  settings.cyan_red = std::clamp(settings.cyan_red, -100, 100);
  settings.magenta_green = std::clamp(settings.magenta_green, -100, 100);
  settings.yellow_blue = std::clamp(settings.yellow_blue, -100, 100);
  const auto red_delta = static_cast<int>(std::round(static_cast<double>(settings.cyan_red) * 255.0 / 100.0));
  const auto green_delta =
      static_cast<int>(std::round(static_cast<double>(settings.magenta_green) * 255.0 / 100.0));
  const auto blue_delta = static_cast<int>(std::round(static_cast<double>(settings.yellow_blue) * 255.0 / 100.0));

  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      if (!selection.isEmpty() && !selection.contains(QPoint(bounds.x + x, bounds.y + y))) {
        continue;
      }
      auto* px = pixels.pixel(x, y);
      px[0] = filter_clamp_byte(static_cast<int>(px[0]) + red_delta);
      px[1] = filter_clamp_byte(static_cast<int>(px[1]) + green_delta);
      px[2] = filter_clamp_byte(static_cast<int>(px[2]) + blue_delta);
    }
  }
}


std::optional<LevelsSettings> request_levels_settings(
    QWidget* parent, std::function<void(bool, const LevelsSettings&)> preview_changed, LevelsSettings initial) {
  initial.black_input = std::clamp(initial.black_input, 0, 254);
  initial.white_input = std::clamp(initial.white_input, initial.black_input + 1, 255);
  initial.gamma_percent = std::clamp(initial.gamma_percent, 10, 999);
  return request_adjustment_settings_dialog<LevelsSettings>(
      parent, QStringLiteral("photoslopLevelsDialog"), QObject::tr("Levels"), QStringLiteral("levelsPreviewCheck"),
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
      parent, QStringLiteral("photoslopCurvesDialog"), QObject::tr("Curves"), QStringLiteral("curvesPreviewCheck"),
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
      parent, QStringLiteral("photoslopHueSaturationDialog"), QObject::tr("Hue/Saturation"),
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
      parent, QStringLiteral("photoslopColorBalanceDialog"), QObject::tr("Color Balance"),
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

}  // namespace photoslop::ui
