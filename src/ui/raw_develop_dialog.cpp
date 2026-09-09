#include "ui/raw_develop_dialog.hpp"

#include "ui/raw_develop_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/theme_qss.hpp"

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
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
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
    bool final_render{false};
    raw::DevelopParams params;
    std::optional<raw::WhiteBalance> white_balance;
    QSize output_size;
  };

  QString file_path;
  bool closed{false};
  bool in_flight{false};
  std::optional<Work> active;
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
  const auto generation = state->generation.load(std::memory_order_acquire);
  RawPreviewState::Work work{generation, params, final_render};
  if (state->in_flight) {
    if (state->active && state->active->generation == generation &&
        state->active->params == params && (state->active->final_render || !final_render)) return;
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

std::optional<RawDevelopOutcome> run_raw_develop_dialog(QWidget* parent, const QString& file_path) {
  const QFileInfo file_info(file_path);
  auto saved_settings = load_raw_develop_settings(file_path);
  auto params = saved_settings.params;

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("rawDevelopDialog"));
  dialog.setWindowTitle(QObject::tr("Develop Raw - %1").arg(file_info.fileName()));
  dialog.resize(1120, 760);
  append_themed_style(dialog,
                      QStringLiteral("QScrollArea#rawDevelopControlsScroll,"
                                     "QWidget#rawDevelopControlsPage { background: transparent; }"));

  auto* layout = new QVBoxLayout(&dialog);
  auto* settings_notice = new QLabel(saved_settings.notice, &dialog);
  settings_notice->setObjectName(QStringLiteral("rawSettingsNotice"));
  settings_notice->setWordWrap(true);
  settings_notice->setVisible(!saved_settings.notice.isEmpty());
  layout->addWidget(settings_notice);
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

  auto* profile_group = new QGroupBox(QObject::tr("Rendering"), &dialog);
  auto* profile_form = new QFormLayout(profile_group);
  auto* profile_combo = new QComboBox(profile_group);
  profile_combo->setObjectName(QStringLiteral("rawProfileCombo"));
  profile_combo->addItem(QObject::tr("Natural"), QStringLiteral("natural"));
  profile_combo->addItem(QObject::tr("Neutral"), QStringLiteral("neutral"));
  profile_combo->setToolTip(QObject::tr("Natural adds photographic tone and color. Neutral retains the straight camera-to-sRGB rendering."));
  profile_form->addRow(QObject::tr("Profile:"), profile_combo);
  auto* processing_note = new QLabel(profile_group);
  processing_note->setObjectName(QStringLiteral("rawProcessingNote"));
  processing_note->setWordWrap(true);
  processing_note->setText(QObject::tr("Original processing is preserved. Reset or changing Profile or Color noise uses current processing."));
  profile_form->addRow(processing_note);
  controls_column->addWidget(profile_group);

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
  auto* noise_combo = new QComboBox(detail_group);
  noise_combo->setObjectName(QStringLiteral("rawNoiseReductionCombo"));
  noise_combo->addItem(QObject::tr("Auto"), QStringLiteral("auto"));
  noise_combo->addItem(QObject::tr("Manual"), QStringLiteral("manual"));
  noise_combo->addItem(QObject::tr("Off"), QStringLiteral("off"));
  detail_form->addRow(QObject::tr("Noise reduction:"), noise_combo);
  auto [denoise_slider, denoise_value] =
      add_slider_row(detail_form, QObject::tr("Denoise:"), 0, 1000, "rawDenoiseSlider");
  auto* fbdd_combo = new QComboBox(detail_group);
  fbdd_combo->setObjectName(QStringLiteral("rawFbddCombo"));
  fbdd_combo->addItem(QObject::tr("Off"), QStringLiteral("off"));
  fbdd_combo->addItem(QObject::tr("Light"), QStringLiteral("light"));
  fbdd_combo->addItem(QObject::tr("Full"), QStringLiteral("full"));
  detail_form->addRow(QObject::tr("FBDD noise reduction:"), fbdd_combo);
  auto* color_noise_combo = new QComboBox(detail_group);
  color_noise_combo->setObjectName(QStringLiteral("rawColorNoiseCombo"));
  color_noise_combo->addItem(QObject::tr("Off"), 0);
  color_noise_combo->addItem(QObject::tr("Light"), 1);
  color_noise_combo->addItem(QObject::tr("Standard"), 2);
  color_noise_combo->addItem(QObject::tr("Strong"), 3);
  color_noise_combo->addItem(QObject::tr("Maximum"), 4);
  color_noise_combo->setToolTip(QObject::tr("Reduces colored speckles separately from brightness grain. Stronger settings can soften thin colored details."));
  detail_form->addRow(QObject::tr("Color noise:"), color_noise_combo);
  auto* half_size_check = new QCheckBox(QObject::tr("Open at half size"), detail_group);
  half_size_check->setObjectName(QStringLiteral("rawHalfSizeCheck"));
  detail_form->addRow(half_size_check);
  auto* noise_note = new QLabel(detail_group);
  noise_note->setObjectName(QStringLiteral("rawNoiseReductionNote"));
  noise_note->setWordWrap(true);
  detail_form->addRow(noise_note);
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
  auto* done_button = buttons->addButton(QObject::tr("Done"), QDialogButtonBox::ActionRole);
  done_button->setObjectName(QStringLiteral("rawDoneButton"));
  done_button->setEnabled(false);
  auto* retry_button = buttons->addButton(QObject::tr("Retry Preview"), QDialogButtonBox::ActionRole);
  retry_button->setObjectName(QStringLiteral("rawRetryPreviewButton"));
  retry_button->hide();
  buttons->addButton(QDialogButtonBox::Cancel);
  status_row->addWidget(buttons);
  layout->addLayout(status_row);

  // --- Widget <-> params sync ---
  std::optional<raw::WhiteBalance> as_shot_white_balance;
  std::optional<raw::RawFileInfo> raw_info;
  std::optional<raw::WhiteBalance> effective_white_balance;
  bool auto_white_balance_pending = params.white_balance == raw::WhiteBalanceMode::Auto;
  bool syncing_widgets = false;
  temperature_slider->setToolTip(QObject::tr("Estimated white balance temperature. Adjust to use Custom white balance."));
  tint_slider->setToolTip(QObject::tr("Estimated white balance tint. Adjust to use Custom white balance."));

  const auto refresh_value_labels = [&] {
    temperature_value->setText(QObject::tr("%1 K").arg(std::lround(slider_to_temperature(temperature_slider->value()))));
    tint_value->setText(QString::number(tint_slider->value()));
    if (auto_white_balance_pending) {
      temperature_value->setText(QObject::tr("Calculating..."));
      tint_value->setText(QObject::tr("Calculating..."));
    }
    temperature_slider->setEnabled(!auto_white_balance_pending);
    tint_slider->setEnabled(!auto_white_balance_pending);
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

  const auto refresh_noise_widgets = [&] {
    const auto noise = raw::effective_noise_reduction(params, raw_info.value_or(raw::RawFileInfo{}));
    const QSignalBlocker block_denoise(denoise_slider), block_fbdd(fbdd_combo), block_color_noise(color_noise_combo);
    denoise_slider->setValue(noise.wavelet_threshold);
    select_combo_data(fbdd_combo, fbdd_token(noise.fbdd));
    color_noise_combo->setCurrentIndex(color_noise_combo->findData(noise.color_passes));
    const bool manual = params.noise_reduction == raw::NoiseReductionMode::Manual;
    denoise_slider->setEnabled(manual);
    fbdd_combo->setEnabled(manual);
    color_noise_combo->setEnabled(manual && raw_info && raw_info->is_three_color_bayer);
    processing_note->setVisible(params.processing_version == 1);
    noise_note->setText(params.noise_reduction == raw::NoiseReductionMode::Auto && !noise.auto_available ?
        QObject::tr("Auto requires ISO metadata and a supported Bayer sensor.") : QString());
    noise_note->setVisible(!noise_note->text().isEmpty());
    refresh_value_labels();
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
    const QSignalBlocker block_noise(noise_combo);
    const QSignalBlocker block_profile(profile_combo);
    const QSignalBlocker block_color_noise(color_noise_combo);

    select_combo_data(profile_combo, params.profile == raw::RenderingProfile::Natural ? QStringLiteral("natural") : QStringLiteral("neutral"));
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
    select_combo_data(noise_combo, params.noise_reduction == raw::NoiseReductionMode::Auto ? QStringLiteral("auto") :
        params.noise_reduction == raw::NoiseReductionMode::Manual ? QStringLiteral("manual") : QStringLiteral("off"));
    denoise_slider->setValue(params.wavelet_denoise_threshold);
    select_combo_data(fbdd_combo, fbdd_token(params.fbdd));
    half_size_check->setChecked(params.half_size);
    refresh_noise_widgets();
    syncing_widgets = false;
  };

  const auto read_params_from_widgets = [&] {
    // Preserve supported sidecar precision until its slider actually changes.
    const auto read_slider = [](QSlider* slider, double& value, double scale = 1.0) {
      if (slider->value() != static_cast<int>(std::lround(value * scale)))
        value = slider->value() / scale;
    };
    const auto wb_data = white_balance_combo->currentData().toString();
    if (wb_data == QStringLiteral("asShot")) {
      params.white_balance = raw::WhiteBalanceMode::AsShot;
    } else if (wb_data == QStringLiteral("auto")) {
      params.white_balance = raw::WhiteBalanceMode::Auto;
    } else {
      // Custom and the named presets both develop as explicit temperature/tint; the
      // sliders always hold the effective values (preset selection sets them).
      params.white_balance = raw::WhiteBalanceMode::Custom;
      if (temperature_slider->value() != temperature_to_slider(params.custom_white_balance.temperature_k))
        params.custom_white_balance.temperature_k = slider_to_temperature(temperature_slider->value());
      read_slider(tint_slider, params.custom_white_balance.tint);
    }
    read_slider(exposure_slider, params.exposure_ev, 100.0);
    read_slider(contrast_slider, params.contrast);
    read_slider(tone_highlights_slider, params.highlights);
    read_slider(shadows_slider, params.shadows);
    params.highlight_recovery = highlight_from_token(highlights_combo->currentData().toString());
    params.auto_brighten = auto_brighten_check->isChecked();
    read_slider(brightness_slider, params.brightness, 100.0);
    read_slider(saturation_slider, params.saturation);
    read_slider(vibrance_slider, params.vibrance);
    params.demosaic = demosaic_from_token(demosaic_combo->currentData().toString());
    if (params.noise_reduction == raw::NoiseReductionMode::Manual) {
      params.wavelet_denoise_threshold = denoise_slider->value();
      params.fbdd = fbdd_from_token(fbdd_combo->currentData().toString());
      // Unsupported layouts keep inactive manual values intact, like Auto/Off.
      if (raw_info && raw_info->is_three_color_bayer)
        params.color_denoise_passes = color_noise_combo->currentData().toInt();
    }
    params.half_size = half_size_check->isChecked();
  };

  // --- Async develop machinery ---
  auto state = std::make_shared<RawPreviewState>();
  state->file_path = file_path;
  // The callbacks assigned below capture this frame by reference, but the
  // worker posts its completion to QCoreApplication, which outlives the dialog,
  // so nothing auto-disconnects it. The close after exec_dialog only runs when
  // that call returns normally; an exception unwinding past it would leave the
  // state armed and the completion would later run against destroyed widgets.
  // Placed at the creation site rather than after the assignments so a throw
  // during setup is covered too; close_raw_develop is null-safe, idempotent,
  // and touches nothing but the state, and it breaks the state/start
  // shared_ptr cycle.
  const auto close_preview =
      qScopeGuard([state] { close_raw_develop(state); });

  QString fatal_error;
  std::optional<RawDevelopOutcome> outcome;
  raw::DevelopParams final_params;
  bool accepting = false;
  bool preview_has_render = false;
  bool reset_requested = false;
  std::optional<RawPreviewState::Completion> accurate_cache;

  const auto set_controls_enabled = [&](bool enabled) {
    profile_group->setEnabled(enabled);
    white_balance_group->setEnabled(enabled);
    tone_group->setEnabled(enabled);
    color_group->setEnabled(enabled);
    detail_group->setEnabled(enabled);
    reset_button->setEnabled(enabled);
    open_button->setEnabled(enabled);
    done_button->setEnabled(enabled && raw_info.has_value());
  };

  auto* debounce = new QTimer(&dialog);
  debounce->setSingleShot(true);
  debounce->setInterval(kPreviewDebounceMs);

  auto* refine = new QTimer(&dialog);
  refine->setSingleShot(true);
  refine->setInterval(500);
  const auto sliders_dragging = [&] {
    for (auto* slider : dialog.findChildren<QSlider*>()) if (slider->isSliderDown()) return true;
    return false;
  };
  const auto set_busy_status = [&](const QString& text) { status_label->setText(text); };
  const auto request_preview = [&](bool accurate) {
    if (accepting || state->closed) return;
    read_params_from_widgets();
    retry_button->hide();
    set_busy_status(QObject::tr("Quick preview - refining..."));
    enqueue_raw_develop(state, params, accurate);
  };
  const auto enqueue_preview = [&] { request_preview(false); };

  const auto save_settings = [&](bool allow_open_without_saving) {
    if (!reset_requested && params == saved_settings.params) return true;
    bool replace = false;
    if (saved_settings.exists && !saved_settings.recognized) {
      QMessageBox prompt(QMessageBox::Warning, QObject::tr("RAW settings"),
          QObject::tr("The existing RAW settings file is unreadable or unsupported. Replace it to save these adjustments."),
          QMessageBox::NoButton, &dialog);
      prompt.setObjectName(QStringLiteral("rawSettingsSaveMessageBox"));
      auto* replace_button = prompt.addButton(QObject::tr("Replace Settings"), QMessageBox::AcceptRole);
      replace_button->setObjectName(QStringLiteral("rawReplaceSettingsButton"));
      QPushButton* without = nullptr;
      if (allow_open_without_saving) {
        without = prompt.addButton(QObject::tr("Open Without Saving"), QMessageBox::ActionRole);
        without->setObjectName(QStringLiteral("rawOpenWithoutSavingButton"));
      }
      prompt.addButton(QMessageBox::Cancel);
      exec_dialog(prompt);
      if (without && prompt.clickedButton() == without) return true;
      if (prompt.clickedButton() != replace_button) return false;
      replace = true;
    }
    for (;;) {
      const auto error = save_raw_develop_settings(file_path, params, saved_settings, replace);
      if (error.isEmpty()) return true;
      QMessageBox prompt(QMessageBox::Warning, QObject::tr("RAW settings"), error,
                         QMessageBox::Retry | QMessageBox::Cancel, &dialog);
      prompt.setObjectName(QStringLiteral("rawSettingsSaveMessageBox"));
      QPushButton* without = nullptr;
      if (allow_open_without_saving) {
        without = prompt.addButton(QObject::tr("Open Without Saving"), QMessageBox::ActionRole);
        without->setObjectName(QStringLiteral("rawOpenWithoutSavingButton"));
      }
      exec_dialog(prompt);
      if (without && prompt.clickedButton() == without) return true;
      if (prompt.standardButton(prompt.clickedButton()) != QMessageBox::Retry) return false;
    }
  };
  const auto finish_open = [&] {
    if (!accurate_cache || !accurate_cache->document || accurate_cache->params != final_params) return;
    if (!save_settings(true)) {
      accepting = false;
      set_controls_enabled(true);
      set_busy_status(QObject::tr("Settings were not saved."));
      return;
    }
    outcome = RawDevelopOutcome{std::move(*accurate_cache->document), final_params};
    dialog.accept();
  };

  state->info_ready = [&](raw::RawFileInfo info) {
    raw_info = info;
    as_shot_white_balance = info.as_shot_white_balance;
    done_button->setEnabled(!accepting);
    refresh_noise_widgets();
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
        preview->set_image(rotated_for_orientation(std::move(thumbnail), info.orientation_flip),
            QSize(params.half_size ? (info.output_width + 1) / 2 : info.output_width,
                  params.half_size ? (info.output_height + 1) / 2 : info.output_height));
      }
    }
    // As Shot temperature/tint become displayable once the file is inspected.
    if (params.white_balance == raw::WhiteBalanceMode::AsShot && as_shot_white_balance) {
      const QSignalBlocker block_temperature(temperature_slider), block_tint(tint_slider);
      temperature_slider->setValue(temperature_to_slider(as_shot_white_balance->temperature_k));
      tint_slider->setValue(static_cast<int>(std::lround(as_shot_white_balance->tint)));
      refresh_value_labels();
    }
  };

  state->apply = [&](RawPreviewState::Completion completion) {
    preview_has_render = true;
    preview->set_image(completion.image, completion.output_size);
    if (completion.white_balance) {
      effective_white_balance = completion.white_balance;
      if (params.white_balance != raw::WhiteBalanceMode::Custom) {
        const QSignalBlocker block_temperature(temperature_slider), block_tint(tint_slider);
        temperature_slider->setValue(temperature_to_slider(effective_white_balance->temperature_k));
        tint_slider->setValue(static_cast<int>(std::lround(effective_white_balance->tint)));
        auto_white_balance_pending = false;
        refresh_value_labels();
      }
    }
    dialog.setProperty("rawPreviewAccurate", completion.final_render);
    if (completion.final_render) {
      accurate_cache = std::move(completion);
      set_busy_status(QString());
      if (accepting) finish_open();
    } else {
      set_busy_status(QObject::tr("Quick preview - refining..."));
    }
  };

  state->fail = [&](QString message, bool fatal) {
    if (fatal) {
      fatal_error = message;
      dialog.reject();
      return;
    }
    dialog.setProperty("rawPreviewAccurate", false);
    set_busy_status(QObject::tr("Preview incomplete: %1").arg(message));
    retry_button->show();
    if (accepting) {
      accepting = false;
      set_controls_enabled(true);
    }
  };

  state->start = [state](RawPreviewState::Work work) {
    state->in_flight = true;
    state->active = work;
    auto* app = QCoreApplication::instance();
    run_tracked_background_worker([state, work, app]() mutable {
      RawPreviewState::Completion completion;
      completion.final_render = work.final_render;
      completion.params = work.params;
      QString error;
      bool fatal = false;
      bool cancelled = false;
      try {
        if (state->session == nullptr) {
          state->session = std::make_shared<raw::DevelopSession>(read_file_bytes_for_worker(state->file_path));
          QMetaObject::invokeMethod(app, [state, info = state->session->info()] {
            if (!state->closed && state->info_ready) state->info_ready(info);
          }, Qt::QueuedConnection);
        }
        raw::DevelopOptions options;
        options.quality = work.final_render ? raw::DevelopQuality::Final : raw::DevelopQuality::Draft;
        options.cancelled = [state, generation = work.generation] {
          return state->generation.load(std::memory_order_acquire) != generation;
        };
        auto developed = state->session->develop(work.params, options);
        completion.image = image_from_developed(developed);
        completion.output_size = QSize(developed.output_width, developed.output_height);
        completion.white_balance = developed.effective_white_balance;
        if (work.final_render) {
          auto result = raw::document_from_developed(developed);
          completion.document = std::make_shared<Document>(std::move(result.document));
        }
      } catch (const raw::DevelopCancelled&) {
        cancelled = true;
      } catch (const std::exception& caught) {
        error = QString::fromUtf8(caught.what());
        fatal = state->session == nullptr;
      }
      QMetaObject::invokeMethod(app,
          [state, work, completion = std::move(completion), error, fatal, cancelled]() mutable {
            state->in_flight = false;
            state->active.reset();
            if (state->closed) return;
            const auto is_latest = work.generation == state->generation.load(std::memory_order_acquire);
            if (!error.isEmpty()) {
              if (state->fail && (is_latest || fatal)) state->fail(error, fatal);
            } else if (!cancelled && is_latest && state->apply) {
              state->apply(std::move(completion));
            }
            if (state->pending.has_value() && state->start) {
              auto next = std::move(*state->pending);
              state->pending.reset();
              state->start(std::move(next));
            }
          }, Qt::QueuedConnection);
    });
  };

  // --- Wiring ---
  const auto on_control_changed = [&] {
    if (syncing_widgets) {
      return;
    }
    read_params_from_widgets();
    state->generation.fetch_add(1, std::memory_order_acq_rel);
    state->pending.reset();
    dialog.setProperty("rawPreviewAccurate", false);
    if (params.white_balance == raw::WhiteBalanceMode::Auto) {
      auto_white_balance_pending = true;
      refresh_value_labels();
    }
    set_busy_status(QObject::tr("Quick preview - refining..."));
    debounce->start();
    refine->start();
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
    on_control_changed();
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
    } else if (data == QStringLiteral("custom")) {
      const auto balance = effective_white_balance ? effective_white_balance : as_shot_white_balance;
      if (balance) {
        params.custom_white_balance = *balance;
        const QSignalBlocker block_temperature(temperature_slider), block_tint(tint_slider);
        temperature_slider->setValue(temperature_to_slider(balance->temperature_k));
        tint_slider->setValue(static_cast<int>(std::lround(balance->tint)));
      }
    }
    auto_white_balance_pending = data == QStringLiteral("auto");
    refresh_value_labels();
    on_control_changed();
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
  QObject::connect(profile_combo, &QComboBox::currentIndexChanged, &dialog, [&] {
    if (syncing_widgets) return;
    params.processing_version = raw::kProcessingVersion;
    params.profile = profile_combo->currentData().toString() == QStringLiteral("natural") ?
        raw::RenderingProfile::Natural : raw::RenderingProfile::Neutral;
    refresh_noise_widgets();
    on_control_changed();
  });
  QObject::connect(color_noise_combo, &QComboBox::currentIndexChanged, &dialog, [&] {
    if (syncing_widgets) return;
    params.processing_version = raw::kProcessingVersion;
    params.color_denoise_passes = color_noise_combo->currentData().toInt();
    processing_note->hide();
    on_control_changed();
  });
  QObject::connect(half_size_check, &QCheckBox::toggled, &dialog, on_control_changed);
  QObject::connect(noise_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    if (syncing_widgets) return;
    const auto previous = raw::effective_noise_reduction(params, raw_info.value_or(raw::RawFileInfo{}));
    const auto token = noise_combo->currentData().toString();
    params.noise_reduction = token == QStringLiteral("auto") ? raw::NoiseReductionMode::Auto :
        token == QStringLiteral("manual") ? raw::NoiseReductionMode::Manual : raw::NoiseReductionMode::Off;
    if (params.noise_reduction == raw::NoiseReductionMode::Manual) {
      params.wavelet_denoise_threshold = previous.wavelet_threshold;
      params.fbdd = previous.fbdd;
      params.color_denoise_passes = previous.color_passes;
    }
    refresh_noise_widgets();
    on_control_changed();
  });
  QObject::connect(debounce, &QTimer::timeout, &dialog, enqueue_preview);
  QObject::connect(refine, &QTimer::timeout, &dialog, [&] {
    if (sliders_dragging()) { refine->start(); return; }
    debounce->stop();
    request_preview(true);
  });
  for (auto* slider : dialog.findChildren<QSlider*>()) {
    QObject::connect(slider, &QSlider::sliderReleased, &dialog, [&] {
      debounce->stop();
      refine->stop();
      request_preview(true);
    });
  }
  QObject::connect(retry_button, &QPushButton::clicked, &dialog, [&] { request_preview(true); });

  QObject::connect(reset_button, &QPushButton::clicked, &dialog, [&] {
    params = raw::DevelopParams{};
    reset_requested = true;
    auto_white_balance_pending = false;
    effective_white_balance.reset();
    apply_params_to_widgets();
    on_control_changed();
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
    refine->stop();
    if (accurate_cache && accurate_cache->params == final_params) finish_open();
    else enqueue_raw_develop(state, final_params, true);
  });
  QObject::connect(done_button, &QPushButton::clicked, &dialog, [&] {
    read_params_from_widgets();
    debounce->stop();
    refine->stop();
    if (save_settings(false)) dialog.reject();
    else refine->start();
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(&dialog, &QDialog::finished, &dialog, [state] { close_raw_develop(state); });

  apply_params_to_widgets();
  enqueue_preview();
  refine->start();

  exec_dialog(dialog);
  close_raw_develop(state);

  if (!fatal_error.isEmpty()) {
    throw std::runtime_error(fatal_error.toStdString());
  }
  if (dialog.result() != QDialog::Accepted || !outcome.has_value()) {
    return std::nullopt;
  }
  return outcome;
}

}  // namespace patchy::ui
