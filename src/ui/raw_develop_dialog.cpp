#include "ui/raw_develop_dialog.hpp"

#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/zoomable_image_preview.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Slider granularity: temperature runs on a log scale so kelvin steps feel even.
constexpr double kMinTemperatureK = 2000.0;
constexpr double kMaxTemperatureK = 25000.0;
constexpr int kTemperatureSliderSteps = 1000;
constexpr int kPreviewDebounceMs = 200;

// The persisted imports/rawDevelop* keys are a contract: never rename them.
// kSettingHighlights stores the clipped-highlight RECOVERY mode (its historical meaning);
// the tonal highlights slider uses kSettingToneHighlights.
constexpr auto kSettingWhiteBalance = "imports/rawDevelopWhiteBalance";
constexpr auto kSettingTemperature = "imports/rawDevelopTemperature";
constexpr auto kSettingTint = "imports/rawDevelopTint";
constexpr auto kSettingExposure = "imports/rawDevelopExposure";
constexpr auto kSettingHighlights = "imports/rawDevelopHighlights";
constexpr auto kSettingAutoBrighten = "imports/rawDevelopAutoBrighten";
constexpr auto kSettingBrightness = "imports/rawDevelopBrightness";
constexpr auto kSettingContrast = "imports/rawDevelopContrast";
constexpr auto kSettingToneHighlights = "imports/rawDevelopToneHighlights";
constexpr auto kSettingToneShadows = "imports/rawDevelopToneShadows";
constexpr auto kSettingSaturation = "imports/rawDevelopSaturation";
constexpr auto kSettingVibrance = "imports/rawDevelopVibrance";
constexpr auto kSettingDemosaic = "imports/rawDevelopDemosaic";
constexpr auto kSettingDenoise = "imports/rawDevelopDenoise";
constexpr auto kSettingFbdd = "imports/rawDevelopFbdd";
constexpr auto kSettingHalfSize = "imports/rawDevelopHalfSize";

QString white_balance_token(raw::WhiteBalanceMode mode) {
  switch (mode) {
    case raw::WhiteBalanceMode::AsShot:
      return QStringLiteral("asShot");
    case raw::WhiteBalanceMode::Auto:
      return QStringLiteral("auto");
    case raw::WhiteBalanceMode::Custom:
      return QStringLiteral("custom");
  }
  return QStringLiteral("asShot");
}

raw::WhiteBalanceMode white_balance_from_token(const QString& token) {
  if (token == QStringLiteral("auto")) {
    return raw::WhiteBalanceMode::Auto;
  }
  if (token == QStringLiteral("custom")) {
    return raw::WhiteBalanceMode::Custom;
  }
  return raw::WhiteBalanceMode::AsShot;
}

QString highlight_token(raw::HighlightMode mode) {
  switch (mode) {
    case raw::HighlightMode::Clip:
      return QStringLiteral("clip");
    case raw::HighlightMode::Unclip:
      return QStringLiteral("unclip");
    case raw::HighlightMode::Blend:
      return QStringLiteral("blend");
    case raw::HighlightMode::Rebuild:
      return QStringLiteral("rebuild");
  }
  return QStringLiteral("clip");
}

raw::HighlightMode highlight_from_token(const QString& token) {
  if (token == QStringLiteral("unclip")) {
    return raw::HighlightMode::Unclip;
  }
  if (token == QStringLiteral("blend")) {
    return raw::HighlightMode::Blend;
  }
  if (token == QStringLiteral("rebuild")) {
    return raw::HighlightMode::Rebuild;
  }
  return raw::HighlightMode::Clip;
}

QString demosaic_token(raw::DemosaicAlgorithm algorithm) {
  switch (algorithm) {
    case raw::DemosaicAlgorithm::Linear:
      return QStringLiteral("linear");
    case raw::DemosaicAlgorithm::Vng:
      return QStringLiteral("vng");
    case raw::DemosaicAlgorithm::Ppg:
      return QStringLiteral("ppg");
    case raw::DemosaicAlgorithm::Ahd:
      return QStringLiteral("ahd");
    case raw::DemosaicAlgorithm::Dcb:
      return QStringLiteral("dcb");
    case raw::DemosaicAlgorithm::Dht:
      return QStringLiteral("dht");
    case raw::DemosaicAlgorithm::ModifiedAhd:
      return QStringLiteral("aahd");
  }
  return QStringLiteral("ahd");
}

raw::DemosaicAlgorithm demosaic_from_token(const QString& token) {
  if (token == QStringLiteral("linear")) {
    return raw::DemosaicAlgorithm::Linear;
  }
  if (token == QStringLiteral("vng")) {
    return raw::DemosaicAlgorithm::Vng;
  }
  if (token == QStringLiteral("ppg")) {
    return raw::DemosaicAlgorithm::Ppg;
  }
  if (token == QStringLiteral("dcb")) {
    return raw::DemosaicAlgorithm::Dcb;
  }
  if (token == QStringLiteral("dht")) {
    return raw::DemosaicAlgorithm::Dht;
  }
  if (token == QStringLiteral("aahd")) {
    return raw::DemosaicAlgorithm::ModifiedAhd;
  }
  return raw::DemosaicAlgorithm::Ahd;
}

QString fbdd_token(raw::FbddNoiseReduction fbdd) {
  switch (fbdd) {
    case raw::FbddNoiseReduction::Off:
      return QStringLiteral("off");
    case raw::FbddNoiseReduction::Light:
      return QStringLiteral("light");
    case raw::FbddNoiseReduction::Full:
      return QStringLiteral("full");
  }
  return QStringLiteral("off");
}

raw::FbddNoiseReduction fbdd_from_token(const QString& token) {
  if (token == QStringLiteral("light")) {
    return raw::FbddNoiseReduction::Light;
  }
  if (token == QStringLiteral("full")) {
    return raw::FbddNoiseReduction::Full;
  }
  return raw::FbddNoiseReduction::Off;
}

int temperature_to_slider(double temperature_k) {
  const auto clamped = std::clamp(temperature_k, kMinTemperatureK, kMaxTemperatureK);
  const auto position = std::log(clamped / kMinTemperatureK) / std::log(kMaxTemperatureK / kMinTemperatureK);
  return static_cast<int>(std::lround(position * kTemperatureSliderSteps));
}

double slider_to_temperature(int slider_value) {
  const auto position = static_cast<double>(std::clamp(slider_value, 0, kTemperatureSliderSteps)) /
                        kTemperatureSliderSteps;
  return kMinTemperatureK * std::pow(kMaxTemperatureK / kMinTemperatureK, position);
}

// Converts a develop result into a QImage; QImage RGB888 rows are 4-byte aligned, so copy
// row by row.
QImage image_from_developed(const raw::DevelopSession::DevelopedImage& developed) {
  QImage image(developed.width, developed.height, QImage::Format_RGB888);
  const auto row_bytes = static_cast<std::size_t>(developed.width) * 3;
  for (int y = 0; y < developed.height; ++y) {
    std::memcpy(image.scanLine(y), developed.rgb.data() + static_cast<std::size_t>(y) * row_bytes, row_bytes);
  }
  return image;
}

QImage rotated_for_orientation(QImage image, int orientation_flip) {
  switch (orientation_flip) {
    case 3:
      return image.transformed(QTransform().rotate(180.0));
    case 5:
      return image.transformed(QTransform().rotate(-90.0));
    case 6:
      return image.transformed(QTransform().rotate(90.0));
    default:
      return image;
  }
}

// One in-flight develop at a time with a one-deep latest-wins queue (the async preview
// pattern from the filter gallery / adjustment dialogs). The DevelopSession is created by
// the first work item and only ever touched by the single in-flight worker.
struct RawPreviewState {
  struct Work {
    std::uint64_t generation{0};
    raw::DevelopParams params;
    bool final_render{false};
  };

  struct Completion {
    QImage image;
    std::shared_ptr<Document> document;
    raw::RawFileInfo info;
    bool final_render{false};
    bool first_completion{false};
  };

  QString file_path;
  bool closed{false};
  bool in_flight{false};
  std::atomic<std::uint64_t> generation{0};
  std::optional<Work> pending;
  std::shared_ptr<raw::DevelopSession> session;
  std::function<void(Work)> start;
  std::function<void(Completion)> apply;
  // (message, fatal) — fatal means the file itself cannot be decoded.
  std::function<void(QString, bool)> fail;
  std::function<void(raw::RawFileInfo)> info_ready;
};

void enqueue_raw_develop(const std::shared_ptr<RawPreviewState>& state, raw::DevelopParams params,
                         bool final_render) {
  if (state == nullptr || state->closed || !state->start) {
    return;
  }
  const auto generation = state->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  RawPreviewState::Work work{generation, params, final_render};
  if (state->in_flight) {
    state->pending = work;
    return;
  }
  state->start(work);
}

void close_raw_develop(const std::shared_ptr<RawPreviewState>& state) {
  if (state == nullptr) {
    return;
  }
  state->closed = true;
  state->generation.fetch_add(1, std::memory_order_acq_rel);
  state->pending.reset();
  state->start = {};
  state->apply = {};
  state->fail = {};
  state->info_ready = {};
}

std::vector<std::uint8_t> read_file_bytes_for_worker(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    throw std::runtime_error(
        QObject::tr("Could not read %1").arg(QFileInfo(path).fileName()).toStdString());
  }
  const auto data = file.readAll();
  return std::vector<std::uint8_t>(data.begin(), data.end());
}

QString format_shutter(double seconds) {
  if (seconds <= 0.0) {
    return {};
  }
  if (seconds >= 1.0) {
    return QObject::tr("%1 s").arg(QString::number(seconds, 'g', 3));
  }
  return QObject::tr("1/%1 s").arg(QString::number(std::lround(1.0 / seconds)));
}

}  // namespace

raw::DevelopParams saved_raw_develop_params() {
  auto settings = app_settings();
  raw::DevelopParams params;
  params.white_balance =
      white_balance_from_token(settings.value(kSettingWhiteBalance, white_balance_token(params.white_balance))
                                   .toString());
  params.custom_white_balance.temperature_k =
      std::clamp(settings.value(kSettingTemperature, params.custom_white_balance.temperature_k).toDouble(),
                 kMinTemperatureK, kMaxTemperatureK);
  params.custom_white_balance.tint =
      std::clamp(settings.value(kSettingTint, params.custom_white_balance.tint).toDouble(), -150.0, 150.0);
  params.exposure_ev = std::clamp(settings.value(kSettingExposure, params.exposure_ev).toDouble(), -2.0, 3.0);
  params.highlight_recovery = highlight_from_token(
      settings.value(kSettingHighlights, highlight_token(params.highlight_recovery)).toString());
  params.auto_brighten = settings.value(kSettingAutoBrighten, params.auto_brighten).toBool();
  params.brightness = std::clamp(settings.value(kSettingBrightness, params.brightness).toDouble(), 0.25, 4.0);
  params.contrast = std::clamp(settings.value(kSettingContrast, params.contrast).toDouble(), -100.0, 100.0);
  params.highlights =
      std::clamp(settings.value(kSettingToneHighlights, params.highlights).toDouble(), -100.0, 100.0);
  params.shadows = std::clamp(settings.value(kSettingToneShadows, params.shadows).toDouble(), -100.0, 100.0);
  params.saturation =
      std::clamp(settings.value(kSettingSaturation, params.saturation).toDouble(), -100.0, 100.0);
  params.vibrance = std::clamp(settings.value(kSettingVibrance, params.vibrance).toDouble(), -100.0, 100.0);
  params.demosaic =
      demosaic_from_token(settings.value(kSettingDemosaic, demosaic_token(params.demosaic)).toString());
  params.wavelet_denoise_threshold =
      std::clamp(settings.value(kSettingDenoise, params.wavelet_denoise_threshold).toInt(), 0, 1000);
  params.fbdd = fbdd_from_token(settings.value(kSettingFbdd, fbdd_token(params.fbdd)).toString());
  params.half_size = settings.value(kSettingHalfSize, params.half_size).toBool();
  return params;
}

void save_raw_develop_params(const raw::DevelopParams& params) {
  auto settings = app_settings();
  settings.setValue(kSettingWhiteBalance, white_balance_token(params.white_balance));
  settings.setValue(kSettingTemperature, params.custom_white_balance.temperature_k);
  settings.setValue(kSettingTint, params.custom_white_balance.tint);
  settings.setValue(kSettingExposure, params.exposure_ev);
  settings.setValue(kSettingHighlights, highlight_token(params.highlight_recovery));
  settings.setValue(kSettingAutoBrighten, params.auto_brighten);
  settings.setValue(kSettingBrightness, params.brightness);
  settings.setValue(kSettingContrast, params.contrast);
  settings.setValue(kSettingToneHighlights, params.highlights);
  settings.setValue(kSettingToneShadows, params.shadows);
  settings.setValue(kSettingSaturation, params.saturation);
  settings.setValue(kSettingVibrance, params.vibrance);
  settings.setValue(kSettingDemosaic, demosaic_token(params.demosaic));
  settings.setValue(kSettingDenoise, params.wavelet_denoise_threshold);
  settings.setValue(kSettingFbdd, fbdd_token(params.fbdd));
  settings.setValue(kSettingHalfSize, params.half_size);
}

std::optional<RawDevelopOutcome> run_raw_develop_dialog(QWidget* parent, const QString& file_path) {
  const QFileInfo file_info(file_path);
  auto params = saved_raw_develop_params();

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("rawDevelopDialog"));
  dialog.setWindowTitle(QObject::tr("Develop Raw - %1").arg(file_info.fileName()));
  dialog.resize(1120, 760);
  dialog.setStyleSheet(dialog.styleSheet() +
                       QStringLiteral("QScrollArea#rawDevelopControlsScroll,"
                                      "QWidget#rawDevelopControlsPage { background: transparent; }"));

  auto* layout = new QVBoxLayout(&dialog);
  auto* content_row = new QHBoxLayout();
  layout->addLayout(content_row, 1);

  auto* preview = new ZoomableImagePreview(&dialog);
  preview->setObjectName(QStringLiteral("rawDevelopPreview"));
  preview->setMinimumSize(420, 320);
  content_row->addWidget(preview, 1);

  // The controls live inside a scroll area so the taller Tone/Color panel still fits
  // short screens (the Preferences-tab pattern).
  auto* controls_scroll = new QScrollArea(&dialog);
  controls_scroll->setObjectName(QStringLiteral("rawDevelopControlsScroll"));
  controls_scroll->setWidgetResizable(true);
  controls_scroll->setFrameShape(QFrame::NoFrame);
  controls_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* controls_page = new QWidget(controls_scroll);
  controls_page->setObjectName(QStringLiteral("rawDevelopControlsPage"));
  auto* controls_column = new QVBoxLayout(controls_page);
  controls_column->setContentsMargins(0, 0, 6, 0);
  controls_scroll->setWidget(controls_page);
  controls_scroll->setMinimumWidth(390);
  content_row->addWidget(controls_scroll);

  const auto add_slider_row = [](QFormLayout* form, const QString& label, int minimum, int maximum,
                                 const char* object_name) {
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setObjectName(QLatin1String(object_name));
    slider->setRange(minimum, maximum);
    slider->setMinimumWidth(170);
    auto* value_label = new QLabel();
    value_label->setObjectName(QLatin1String(object_name) + QStringLiteral("Value"));
    value_label->setMinimumWidth(58);
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(slider, 1);
    row->addWidget(value_label);
    form->addRow(label, row);
    return std::make_pair(slider, value_label);
  };

  // --- White balance ---
  auto* white_balance_group = new QGroupBox(QObject::tr("White Balance"), &dialog);
  auto* white_balance_form = new QFormLayout(white_balance_group);
  auto* white_balance_combo = new QComboBox(white_balance_group);
  white_balance_combo->setObjectName(QStringLiteral("rawWhiteBalanceCombo"));
  struct WhiteBalancePreset {
    const char* label;
    double temperature_k;
    double tint;
  };
  // Fixed CCT presets in the classic converter tradition (they resolve through the camera
  // matrix, so the rendered result is camera-specific).
  const std::array<WhiteBalancePreset, 6> presets = {{
      {QT_TRANSLATE_NOOP("QObject", "Daylight"), 5500.0, 10.0},
      {QT_TRANSLATE_NOOP("QObject", "Cloudy"), 6500.0, 10.0},
      {QT_TRANSLATE_NOOP("QObject", "Shade"), 7500.0, 10.0},
      {QT_TRANSLATE_NOOP("QObject", "Tungsten"), 2850.0, 0.0},
      {QT_TRANSLATE_NOOP("QObject", "Fluorescent"), 3800.0, 21.0},
      {QT_TRANSLATE_NOOP("QObject", "Flash"), 5500.0, 0.0},
  }};
  white_balance_combo->addItem(QObject::tr("As Shot"), QStringLiteral("asShot"));
  white_balance_combo->addItem(QObject::tr("Auto"), QStringLiteral("auto"));
  for (std::size_t index = 0; index < presets.size(); ++index) {
    white_balance_combo->addItem(QCoreApplication::translate("QObject", presets[index].label),
                                 QStringLiteral("preset:%1").arg(index));
  }
  white_balance_combo->addItem(QObject::tr("Custom"), QStringLiteral("custom"));
  white_balance_form->addRow(QObject::tr("Preset:"), white_balance_combo);
  auto [temperature_slider, temperature_value] =
      add_slider_row(white_balance_form, QObject::tr("Temperature:"), 0, kTemperatureSliderSteps,
                     "rawTemperatureSlider");
  auto [tint_slider, tint_value] =
      add_slider_row(white_balance_form, QObject::tr("Tint:"), -150, 150, "rawTintSlider");
  controls_column->addWidget(white_balance_group);

  // --- Tone ---
  auto* tone_group = new QGroupBox(QObject::tr("Tone"), &dialog);
  auto* tone_form = new QFormLayout(tone_group);
  // LibRaw's linear exposure correction spans -2..+3 EV.
  auto [exposure_slider, exposure_value] =
      add_slider_row(tone_form, QObject::tr("Exposure:"), -200, 300, "rawExposureSlider");
  auto [contrast_slider, contrast_value] =
      add_slider_row(tone_form, QObject::tr("Contrast:"), -100, 100, "rawContrastSlider");
  auto [tone_highlights_slider, tone_highlights_value] =
      add_slider_row(tone_form, QObject::tr("Highlights:"), -100, 100, "rawToneHighlightsSlider");
  auto [shadows_slider, shadows_value] =
      add_slider_row(tone_form, QObject::tr("Shadows:"), -100, 100, "rawShadowsSlider");
  // Reconstruction of clipped sensor data — a different job than the Highlights slider
  // above, which is tonal compression of intact data.
  auto* highlights_combo = new QComboBox(tone_group);
  highlights_combo->setObjectName(QStringLiteral("rawHighlightsCombo"));
  highlights_combo->addItem(QObject::tr("Clip to white"), QStringLiteral("clip"));
  highlights_combo->addItem(QObject::tr("Unclipped"), QStringLiteral("unclip"));
  highlights_combo->addItem(QObject::tr("Blend"), QStringLiteral("blend"));
  highlights_combo->addItem(QObject::tr("Rebuild detail"), QStringLiteral("rebuild"));
  tone_form->addRow(QObject::tr("Highlight recovery:"), highlights_combo);
  auto* auto_brighten_check = new QCheckBox(QObject::tr("Auto brighten"), tone_group);
  auto_brighten_check->setObjectName(QStringLiteral("rawAutoBrightenCheck"));
  tone_form->addRow(auto_brighten_check);
  auto [brightness_slider, brightness_value] =
      add_slider_row(tone_form, QObject::tr("Brightness:"), 25, 400, "rawBrightnessSlider");
  controls_column->addWidget(tone_group);

  // --- Color ---
  auto* color_group = new QGroupBox(QObject::tr("Color"), &dialog);
  auto* color_form = new QFormLayout(color_group);
  auto [saturation_slider, saturation_value] =
      add_slider_row(color_form, QObject::tr("Saturation:"), -100, 100, "rawSaturationSlider");
  auto [vibrance_slider, vibrance_value] =
      add_slider_row(color_form, QObject::tr("Vibrance:"), -100, 100, "rawVibranceSlider");
  controls_column->addWidget(color_group);

  // --- Detail ---
  auto* detail_group = new QGroupBox(QObject::tr("Detail"), &dialog);
  auto* detail_form = new QFormLayout(detail_group);
  auto* demosaic_combo = new QComboBox(detail_group);
  demosaic_combo->setObjectName(QStringLiteral("rawDemosaicCombo"));
  demosaic_combo->addItem(QObject::tr("AHD (default)"), QStringLiteral("ahd"));
  demosaic_combo->addItem(QObject::tr("DHT (good for high ISO)"), QStringLiteral("dht"));
  demosaic_combo->addItem(QObject::tr("Modified AHD"), QStringLiteral("aahd"));
  demosaic_combo->addItem(QObject::tr("DCB"), QStringLiteral("dcb"));
  demosaic_combo->addItem(QObject::tr("PPG"), QStringLiteral("ppg"));
  demosaic_combo->addItem(QObject::tr("VNG"), QStringLiteral("vng"));
  demosaic_combo->addItem(QObject::tr("Bilinear (fastest)"), QStringLiteral("linear"));
  detail_form->addRow(QObject::tr("Demosaic:"), demosaic_combo);
  auto [denoise_slider, denoise_value] =
      add_slider_row(detail_form, QObject::tr("Denoise:"), 0, 1000, "rawDenoiseSlider");
  auto* fbdd_combo = new QComboBox(detail_group);
  fbdd_combo->setObjectName(QStringLiteral("rawFbddCombo"));
  fbdd_combo->addItem(QObject::tr("Off"), QStringLiteral("off"));
  fbdd_combo->addItem(QObject::tr("Light"), QStringLiteral("light"));
  fbdd_combo->addItem(QObject::tr("Full"), QStringLiteral("full"));
  detail_form->addRow(QObject::tr("FBDD noise reduction:"), fbdd_combo);
  auto* half_size_check = new QCheckBox(QObject::tr("Open at half size (faster)"), detail_group);
  half_size_check->setObjectName(QStringLiteral("rawHalfSizeCheck"));
  detail_form->addRow(half_size_check);
  controls_column->addWidget(detail_group);

  // --- File info ---
  auto* info_label = new QLabel(&dialog);
  info_label->setObjectName(QStringLiteral("rawInfoLabel"));
  info_label->setWordWrap(true);
  info_label->setText(QObject::tr("Reading raw file..."));
  controls_column->addWidget(info_label);
  controls_column->addStretch(1);

  auto* status_row = new QHBoxLayout();
  auto* status_label = new QLabel(&dialog);
  status_label->setObjectName(QStringLiteral("rawDevelopStatus"));
  status_row->addWidget(status_label, 1);
  auto* buttons = new QDialogButtonBox(&dialog);
  auto* reset_button = buttons->addButton(QObject::tr("Reset"), QDialogButtonBox::ResetRole);
  reset_button->setObjectName(QStringLiteral("rawResetButton"));
  auto* open_button = buttons->addButton(QObject::tr("Open"), QDialogButtonBox::AcceptRole);
  open_button->setObjectName(QStringLiteral("rawOpenButton"));
  open_button->setDefault(true);
  buttons->addButton(QDialogButtonBox::Cancel);
  status_row->addWidget(buttons);
  layout->addLayout(status_row);

  // --- Widget <-> params sync ---
  std::optional<raw::WhiteBalance> as_shot_white_balance;
  bool syncing_widgets = false;

  const auto refresh_value_labels = [&] {
    temperature_value->setText(QObject::tr("%1 K").arg(std::lround(slider_to_temperature(temperature_slider->value()))));
    tint_value->setText(QString::number(tint_slider->value()));
    const auto ev = exposure_slider->value() / 100.0;
    exposure_value->setText(QObject::tr("%1 EV").arg(QString::number(ev, 'f', 2)));
    contrast_value->setText(QString::number(contrast_slider->value()));
    tone_highlights_value->setText(QString::number(tone_highlights_slider->value()));
    shadows_value->setText(QString::number(shadows_slider->value()));
    brightness_value->setText(QString::number(brightness_slider->value() / 100.0, 'f', 2));
    saturation_value->setText(QString::number(saturation_slider->value()));
    vibrance_value->setText(QString::number(vibrance_slider->value()));
    denoise_value->setText(denoise_slider->value() == 0 ? QObject::tr("Off")
                                                        : QString::number(denoise_slider->value()));
  };

  const auto select_combo_data = [](QComboBox* combo, const QString& data) {
    const auto index = combo->findData(data);
    combo->setCurrentIndex(index >= 0 ? index : 0);
  };

  const auto apply_params_to_widgets = [&] {
    syncing_widgets = true;
    const QSignalBlocker block_wb(white_balance_combo);
    const QSignalBlocker block_temperature(temperature_slider);
    const QSignalBlocker block_tint(tint_slider);
    const QSignalBlocker block_exposure(exposure_slider);
    const QSignalBlocker block_contrast(contrast_slider);
    const QSignalBlocker block_tone_highlights(tone_highlights_slider);
    const QSignalBlocker block_shadows(shadows_slider);
    const QSignalBlocker block_highlights(highlights_combo);
    const QSignalBlocker block_auto_brighten(auto_brighten_check);
    const QSignalBlocker block_brightness(brightness_slider);
    const QSignalBlocker block_saturation(saturation_slider);
    const QSignalBlocker block_vibrance(vibrance_slider);
    const QSignalBlocker block_demosaic(demosaic_combo);
    const QSignalBlocker block_denoise(denoise_slider);
    const QSignalBlocker block_fbdd(fbdd_combo);
    const QSignalBlocker block_half(half_size_check);

    select_combo_data(white_balance_combo, white_balance_token(params.white_balance));
    auto displayed_white_balance = params.custom_white_balance;
    if (params.white_balance == raw::WhiteBalanceMode::AsShot && as_shot_white_balance.has_value()) {
      displayed_white_balance = *as_shot_white_balance;
    }
    temperature_slider->setValue(temperature_to_slider(displayed_white_balance.temperature_k));
    tint_slider->setValue(static_cast<int>(std::lround(std::clamp(displayed_white_balance.tint, -150.0, 150.0))));
    exposure_slider->setValue(static_cast<int>(std::lround(params.exposure_ev * 100.0)));
    contrast_slider->setValue(static_cast<int>(std::lround(params.contrast)));
    tone_highlights_slider->setValue(static_cast<int>(std::lround(params.highlights)));
    shadows_slider->setValue(static_cast<int>(std::lround(params.shadows)));
    select_combo_data(highlights_combo, highlight_token(params.highlight_recovery));
    auto_brighten_check->setChecked(params.auto_brighten);
    brightness_slider->setValue(static_cast<int>(std::lround(params.brightness * 100.0)));
    saturation_slider->setValue(static_cast<int>(std::lround(params.saturation)));
    vibrance_slider->setValue(static_cast<int>(std::lround(params.vibrance)));
    select_combo_data(demosaic_combo, demosaic_token(params.demosaic));
    denoise_slider->setValue(params.wavelet_denoise_threshold);
    select_combo_data(fbdd_combo, fbdd_token(params.fbdd));
    half_size_check->setChecked(params.half_size);
    refresh_value_labels();
    syncing_widgets = false;
  };

  const auto read_params_from_widgets = [&] {
    const auto wb_data = white_balance_combo->currentData().toString();
    if (wb_data == QStringLiteral("asShot")) {
      params.white_balance = raw::WhiteBalanceMode::AsShot;
    } else if (wb_data == QStringLiteral("auto")) {
      params.white_balance = raw::WhiteBalanceMode::Auto;
    } else {
      // Custom and the named presets both develop as explicit temperature/tint; the
      // sliders always hold the effective values (preset selection sets them).
      params.white_balance = raw::WhiteBalanceMode::Custom;
      params.custom_white_balance.temperature_k = slider_to_temperature(temperature_slider->value());
      params.custom_white_balance.tint = tint_slider->value();
    }
    params.exposure_ev = exposure_slider->value() / 100.0;
    params.contrast = contrast_slider->value();
    params.highlights = tone_highlights_slider->value();
    params.shadows = shadows_slider->value();
    params.highlight_recovery = highlight_from_token(highlights_combo->currentData().toString());
    params.auto_brighten = auto_brighten_check->isChecked();
    params.brightness = brightness_slider->value() / 100.0;
    params.saturation = saturation_slider->value();
    params.vibrance = vibrance_slider->value();
    params.demosaic = demosaic_from_token(demosaic_combo->currentData().toString());
    params.wavelet_denoise_threshold = denoise_slider->value();
    params.fbdd = fbdd_from_token(fbdd_combo->currentData().toString());
    params.half_size = half_size_check->isChecked();
  };

  // --- Async develop machinery ---
  auto state = std::make_shared<RawPreviewState>();
  state->file_path = file_path;

  QString fatal_error;
  std::optional<RawDevelopOutcome> outcome;
  raw::DevelopParams final_params;
  bool accepting = false;
  bool preview_has_render = false;

  const auto set_controls_enabled = [&](bool enabled) {
    white_balance_group->setEnabled(enabled);
    tone_group->setEnabled(enabled);
    color_group->setEnabled(enabled);
    detail_group->setEnabled(enabled);
    reset_button->setEnabled(enabled);
    open_button->setEnabled(enabled);
  };

  auto* debounce = new QTimer(&dialog);
  debounce->setSingleShot(true);
  debounce->setInterval(kPreviewDebounceMs);

  const auto set_busy_status = [&](const QString& text) { status_label->setText(text); };

  const auto enqueue_preview = [&, state] {
    read_params_from_widgets();
    auto preview_params = params;
    preview_params.half_size = true;  // previews always develop at half size for speed
    set_busy_status(QObject::tr("Developing preview..."));
    enqueue_raw_develop(state, preview_params, false);
  };

  state->info_ready = [&](raw::RawFileInfo info) {
    as_shot_white_balance = info.as_shot_white_balance;
    QStringList lines;
    QString camera = QString::fromStdString(info.camera_make);
    const auto model = QString::fromStdString(info.camera_model);
    if (!model.isEmpty()) {
      camera = camera.isEmpty() ? model : camera + QLatin1Char(' ') + model;
    }
    if (!camera.isEmpty()) {
      lines.push_back(camera);
    }
    if (!info.lens.empty()) {
      lines.push_back(QString::fromStdString(info.lens));
    }
    QStringList exposure_parts;
    if (info.iso > 0.0) {
      exposure_parts.push_back(QObject::tr("ISO %1").arg(std::lround(info.iso)));
    }
    if (const auto shutter = format_shutter(info.shutter_seconds); !shutter.isEmpty()) {
      exposure_parts.push_back(shutter);
    }
    if (info.aperture_f_number > 0.0) {
      exposure_parts.push_back(QObject::tr("f/%1").arg(QString::number(info.aperture_f_number, 'f', 1)));
    }
    if (info.focal_length_mm > 0.0) {
      exposure_parts.push_back(QObject::tr("%1 mm").arg(std::lround(info.focal_length_mm)));
    }
    if (!exposure_parts.isEmpty()) {
      lines.push_back(exposure_parts.join(QStringLiteral("   ")));
    }
    const auto megapixels = static_cast<double>(info.output_width) * info.output_height / 1e6;
    lines.push_back(QObject::tr("%1 x %2 (%3 MP)")
                        .arg(info.output_width)
                        .arg(info.output_height)
                        .arg(QString::number(megapixels, 'f', 1)));
    if (info.timestamp > 0) {
      lines.push_back(QLocale().toString(QDateTime::fromSecsSinceEpoch(info.timestamp), QLocale::ShortFormat));
    }
    info_label->setText(lines.join(QLatin1Char('\n')));

    // Show the embedded camera preview instantly while the first develop runs.
    if (!preview_has_render && !info.thumbnail.empty()) {
      auto thumbnail = QImage::fromData(info.thumbnail.data(), static_cast<int>(info.thumbnail.size()));
      if (!thumbnail.isNull()) {
        preview->set_image(rotated_for_orientation(std::move(thumbnail), info.orientation_flip));
      }
    }
    // As Shot temperature/tint become displayable once the file is inspected.
    if (params.white_balance == raw::WhiteBalanceMode::AsShot) {
      apply_params_to_widgets();
    }
  };

  state->apply = [&](RawPreviewState::Completion completion) {
    if (completion.final_render) {
      if (completion.document != nullptr) {
        outcome = RawDevelopOutcome{std::move(*completion.document), final_params};
        dialog.accept();
      }
      return;
    }
    preview_has_render = true;
    preview->set_image(std::move(completion.image));
    set_busy_status(QString());
  };

  state->fail = [&](QString message, bool fatal) {
    if (fatal) {
      fatal_error = message;
      dialog.reject();
      return;
    }
    set_busy_status(message);
    if (accepting) {
      accepting = false;
      set_controls_enabled(true);
    }
  };

  state->start = [state](RawPreviewState::Work work) {
    state->in_flight = true;
    auto* app = QCoreApplication::instance();
    run_tracked_background_worker([state, work, app]() mutable {
      RawPreviewState::Completion completion;
      completion.final_render = work.final_render;
      QString error;
      bool fatal = false;
      try {
        if (state->session == nullptr) {
          state->session = std::make_shared<raw::DevelopSession>(read_file_bytes_for_worker(state->file_path));
          completion.first_completion = true;
          completion.info = state->session->info();
        }
        if (work.final_render) {
          auto result = state->session->develop_document(work.params);
          completion.document = std::make_shared<Document>(std::move(result.document));
        } else {
          completion.image = image_from_developed(state->session->develop(work.params));
        }
      } catch (const std::exception& caught) {
        error = QString::fromUtf8(caught.what());
        fatal = state->session == nullptr;
      }
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [state, work, completion = std::move(completion), error, fatal]() mutable {
            state->in_flight = false;
            if (state->closed) {
              return;
            }
            if (completion.first_completion && error.isEmpty() && state->info_ready) {
              state->info_ready(completion.info);
            }
            const auto is_latest =
                work.generation == state->generation.load(std::memory_order_acquire);
            if (!error.isEmpty()) {
              if (state->fail && (is_latest || fatal)) {
                state->fail(error, fatal);
              }
            } else if (is_latest && state->apply) {
              state->apply(std::move(completion));
            }
            if (state->pending.has_value() && state->start) {
              auto next = std::move(*state->pending);
              state->pending.reset();
              state->start(std::move(next));
            }
          },
          Qt::QueuedConnection);
    });
  };

  // --- Wiring ---
  const auto on_control_changed = [&] {
    if (syncing_widgets) {
      return;
    }
    debounce->start();
  };
  const auto on_white_balance_slider = [&] {
    if (syncing_widgets) {
      return;
    }
    // Dragging temperature/tint while in As Shot/Auto/preset turns the setting custom,
    // keeping the currently displayed values as the starting point.
    if (white_balance_combo->currentData().toString() != QStringLiteral("custom")) {
      const QSignalBlocker block(white_balance_combo);
      select_combo_data(white_balance_combo, QStringLiteral("custom"));
    }
    refresh_value_labels();
    debounce->start();
  };
  QObject::connect(temperature_slider, &QSlider::valueChanged, &dialog, on_white_balance_slider);
  QObject::connect(tint_slider, &QSlider::valueChanged, &dialog, on_white_balance_slider);
  QObject::connect(white_balance_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    if (syncing_widgets) {
      return;
    }
    const auto data = white_balance_combo->currentData().toString();
    if (data.startsWith(QStringLiteral("preset:"))) {
      // A named preset stays selected in the combo; it just drives the sliders to its
      // fixed temperature/tint (dragging a slider afterwards flips the combo to Custom).
      const auto preset_index = data.mid(7).toInt();
      if (preset_index >= 0 && preset_index < static_cast<int>(presets.size())) {
        const auto& preset = presets[static_cast<std::size_t>(preset_index)];
        const QSignalBlocker block_temperature(temperature_slider);
        const QSignalBlocker block_tint(tint_slider);
        temperature_slider->setValue(temperature_to_slider(preset.temperature_k));
        tint_slider->setValue(static_cast<int>(std::lround(preset.tint)));
        refresh_value_labels();
      }
    } else if (data == QStringLiteral("asShot") && as_shot_white_balance.has_value()) {
      const QSignalBlocker block_temperature(temperature_slider);
      const QSignalBlocker block_tint(tint_slider);
      temperature_slider->setValue(temperature_to_slider(as_shot_white_balance->temperature_k));
      tint_slider->setValue(
          static_cast<int>(std::lround(std::clamp(as_shot_white_balance->tint, -150.0, 150.0))));
      refresh_value_labels();
    } else {
      refresh_value_labels();
    }
    debounce->start();
  });
  for (auto* value_slider : {exposure_slider, contrast_slider, tone_highlights_slider, shadows_slider,
                             brightness_slider, saturation_slider, vibrance_slider, denoise_slider}) {
    QObject::connect(value_slider, &QSlider::valueChanged, &dialog, [&] {
      refresh_value_labels();
      on_control_changed();
    });
  }
  QObject::connect(highlights_combo, &QComboBox::currentIndexChanged, &dialog, on_control_changed);
  QObject::connect(auto_brighten_check, &QCheckBox::toggled, &dialog, on_control_changed);
  QObject::connect(demosaic_combo, &QComboBox::currentIndexChanged, &dialog, on_control_changed);
  QObject::connect(fbdd_combo, &QComboBox::currentIndexChanged, &dialog, on_control_changed);
  // Half size affects only the final decode; no preview refresh needed.
  QObject::connect(debounce, &QTimer::timeout, &dialog, enqueue_preview);

  QObject::connect(reset_button, &QPushButton::clicked, &dialog, [&] {
    params = raw::DevelopParams{};
    apply_params_to_widgets();
    debounce->start();
  });
  QObject::connect(open_button, &QPushButton::clicked, &dialog, [&, state] {
    if (accepting) {
      return;
    }
    accepting = true;
    read_params_from_widgets();
    final_params = params;
    set_controls_enabled(false);
    set_busy_status(final_params.half_size ? QObject::tr("Developing half size...")
                                           : QObject::tr("Developing full resolution..."));
    debounce->stop();
    enqueue_raw_develop(state, final_params, true);
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  apply_params_to_widgets();
  enqueue_preview();

  exec_dialog(dialog);
  close_raw_develop(state);

  if (!fatal_error.isEmpty()) {
    throw std::runtime_error(fatal_error.toStdString());
  }
  if (dialog.result() != QDialog::Accepted || !outcome.has_value()) {
    return std::nullopt;
  }
  save_raw_develop_params(outcome->params);
  return outcome;
}

}  // namespace patchy::ui
