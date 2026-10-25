#include "ui/raw_develop_dialog.hpp"

#include "ui/raw_develop_settings.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/modifier_names.hpp"
#include "ui/raw_develop_histogram.hpp"
#include "ui/zoomable_image_preview.hpp"
#include "ui/theme_qss.hpp"
#include "ui/localization.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
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
constexpr int kPreviewDebounceMs = 80;
// The preview zoom presets (GitHub issue 395), as factors; Fit comes first.
constexpr std::array<double, 11> kPreviewZoomPresets = {0.125, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0,
                                                        2.0, 3.0, 4.0, 8.0, 16.0};

QString format_zoom_percent(double factor) {
  const auto percent = factor * 100.0;
  const bool whole = std::abs(percent - std::round(percent)) < 0.05;
  return QStringLiteral("%1%").arg(QLocale().toString(percent, 'f', whole ? 0 : 1));
}

// Routes the events of the objects it watches through one handler; a true
// return consumes the event. The dialog clears it before its locals go away.
class CallbackEventFilter final : public QObject {
public:
  CallbackEventFilter(QObject* parent, std::function<bool(QObject*, QEvent*)> handler)
      : QObject(parent), handler_(std::move(handler)) {}
  void clear() { handler_ = {}; }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    return (handler_ && handler_(watched, event)) || QObject::eventFilter(watched, event);
  }

private:
  std::function<bool(QObject*, QEvent*)> handler_;
};

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

// Each lane has one worker and one latest pending request. A small draft can update
// while an obsolete accurate decode is reaching its next cancellation checkpoint.
// Sessions are lane-owned and never accessed concurrently.
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
    std::optional<RawPreviewHistogram> histogram;
  };

  QString file_path;
  std::atomic<bool> closed{false};
  struct Lane {
    bool in_flight{false};
    int progress_percent{0}; // GUI-thread state for this lane's active request.
    std::optional<Work> active;
    std::optional<Work> pending;
    std::shared_ptr<raw::DevelopSession> session;
  } draft, accurate;
  Lane& lane(bool final_render) { return final_render ? accurate : draft; }
  std::atomic<std::uint64_t> generation{0};
  std::shared_ptr<const std::vector<std::uint8_t>> file_bytes;
  std::function<void(Work)> start;
  std::function<void(Completion)> apply;
  // (message, fatal) — fatal means the file itself cannot be decoded.
  std::function<void(QString, bool)> fail;
  std::function<void(raw::RawFileInfo)> info_ready;
  std::function<void(int, bool)> progress;
  // Shift-click auto values (GitHub issue 394) measure a develop with the Tone
  // sliders at their defaults. It runs in its own small lane (one worker, its
  // own session, one latest pending request), never touching the preview.
  struct Baseline {
    bool in_flight{false};
    std::optional<raw::DevelopParams> pending;
    std::shared_ptr<raw::DevelopSession> session;  // worker-owned
  } baseline;
  std::function<void(raw::DevelopParams, RawPreviewHistogram)> baseline_ready;
};

void enqueue_raw_develop(const std::shared_ptr<RawPreviewState>& state, raw::DevelopParams params,
                         bool final_render) {
  if (state == nullptr || state->closed || !state->start) {
    return;
  }
  const auto generation = state->generation.load(std::memory_order_acquire);
  RawPreviewState::Work work{generation, params, final_render};
  auto& lane = state->lane(final_render);
  if (lane.in_flight) {
    if (lane.active && lane.active->generation == generation && lane.active->params == params) return;
    lane.pending = work;
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
  state->draft.pending.reset();
  state->accurate.pending.reset();
  state->start = {};
  state->apply = {};
  state->fail = {};
  state->info_ready = {};
  state->progress = {};
  state->baseline.pending.reset();
  state->baseline_ready = {};
}

// The develop the Shift-click auto values measure: every setting as it is, the
// four Tone sliders at their defaults.
raw::DevelopParams tone_baseline_params(raw::DevelopParams params) {
  const raw::DevelopParams defaults;
  params.exposure_ev = defaults.exposure_ev;
  params.contrast = defaults.contrast;
  params.highlights = defaults.highlights;
  params.shadows = defaults.shadows;
  return params;
}

// Settings that change the baseline's histogram. Output size, demosaic and
// noise reduction do not reach a draft develop, so they never invalidate it.
raw::DevelopParams tone_baseline_key(raw::DevelopParams params) {
  params = tone_baseline_params(params);
  const raw::DevelopParams defaults;
  params.half_size = defaults.half_size;
  params.demosaic = defaults.demosaic;
  params.noise_reduction = defaults.noise_reduction;
  params.wavelet_denoise_threshold = defaults.wavelet_denoise_threshold;
  params.fbdd = defaults.fbdd;
  params.color_denoise_passes = defaults.color_denoise_passes;
  return params;
}

bool tone_sliders_at_defaults(const raw::DevelopParams& params) {
  return params == tone_baseline_params(params);
}

// Develops `params` as a draft on the baseline lane and reports its histogram.
// Call only after the first draft has published the file snapshot.
void request_baseline_develop(const std::shared_ptr<RawPreviewState>& state, raw::DevelopParams params) {
  if (state == nullptr || state->closed || !state->file_bytes) return;
  if (state->baseline.in_flight) {
    state->baseline.pending = params;
    return;
  }
  state->baseline.in_flight = true;
  auto* app = QCoreApplication::instance();
  run_tracked_background_worker([state, params, app] {
    std::optional<RawPreviewHistogram> histogram;
    try {
      if (state->baseline.session == nullptr)
        state->baseline.session = std::make_shared<raw::DevelopSession>(*state->file_bytes);
      raw::DevelopOptions options;
      options.quality = raw::DevelopQuality::Draft;
      options.cancelled = [state] { return state->closed.load(); };
      const auto developed = state->baseline.session->develop(params, options);
      histogram = raw_preview_histogram(developed.rgb, developed.width, developed.height);
    } catch (const std::exception&) {
      // Cancelled or undecodable: Shift-click then has nothing to apply.
    }
    QMetaObject::invokeMethod(app, [state, params, histogram] {
      state->baseline.in_flight = false;
      if (state->closed) return;
      if (histogram && state->baseline_ready) state->baseline_ready(params, *histogram);
      if (state->baseline.pending) {
        const auto next = *state->baseline.pending;
        state->baseline.pending.reset();
        request_baseline_develop(state, next);
      }
    }, Qt::QueuedConnection);
  });
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

  // Each slider with its name and value labels: Shift-click and Alt-click
  // (GitHub issue 394) work on all three.
  struct SliderRow {
    QSlider* slider;
    QWidget* name;
    QLabel* value;
  };
  std::vector<SliderRow> slider_rows;
  const auto add_slider_row = [&slider_rows](QFormLayout* form, const QString& label, int minimum, int maximum,
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
    slider_rows.push_back({slider, form->labelForField(row), value_label});
    return std::make_pair(slider, value_label);
  };

  // --- Histogram (GitHub issue 393) ---
  auto* histogram = new RawHistogramWidget(controls_page);
  histogram->setObjectName(QStringLiteral("rawHistogram"));
  controls_column->addWidget(histogram);
  auto* histogram_label = new QLabel(controls_page);
  histogram_label->setObjectName(QStringLiteral("rawHistogramRegionLabel"));
  histogram_label->setAlignment(Qt::AlignCenter);
  histogram_label->setMinimumHeight(histogram_label->fontMetrics().height());
  controls_column->addWidget(histogram_label);

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
    white_balance_combo->addItem(translate_data_text(presets[index].label),
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
  // --- Preview zoom (GitHub issue 395) ---
  auto* zoom_combo = new QComboBox(&dialog);
  zoom_combo->setObjectName(QStringLiteral("rawZoomCombo"));
  zoom_combo->setEditable(true);
  zoom_combo->setInsertPolicy(QComboBox::NoInsert);
  zoom_combo->setMinimumContentsLength(9);
  zoom_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  zoom_combo->addItem(QObject::tr("Fit"), 0.0);
  for (const auto factor : kPreviewZoomPresets) zoom_combo->addItem(format_zoom_percent(factor), factor);
  zoom_combo->setToolTip(QObject::tr("Preview zoom. Pick a size or type a percentage."));
  auto* zoom_100_button = new QPushButton(QStringLiteral("100%"), &dialog);
  zoom_100_button->setObjectName(QStringLiteral("rawZoom100Button"));
  zoom_100_button->setToolTip(QObject::tr("Zoom to 100% (1 image pixel = 1 screen pixel)"));
  auto* zoom_fit_button = new QPushButton(QObject::tr("Fit"), &dialog);
  zoom_fit_button->setObjectName(QStringLiteral("rawZoomFitButton"));
  zoom_fit_button->setToolTip(QObject::tr("Fit the image in the preview"));
  auto* scrubby_check = new QCheckBox(QObject::tr("Scrubby Zoom"), &dialog);
  scrubby_check->setObjectName(QStringLiteral("rawScrubbyZoomCheck"));
  scrubby_check->setToolTip(QObject::tr("Drag left or right on the preview to zoom."));
  status_row->addWidget(zoom_combo);
  status_row->addWidget(zoom_100_button);
  status_row->addWidget(zoom_fit_button);
  status_row->addWidget(scrubby_check);
  status_row->addSpacing(8);
  auto* status_label = new QLabel(&dialog);
  status_label->setObjectName(QStringLiteral("rawDevelopStatus"));
  status_label->setToolTip(QObject::tr("Progress is estimated from processing stages. Some stages report only when complete."));
  status_row->addWidget(status_label, 1);
  auto* buttons = new QDialogButtonBox(&dialog);
  auto* reset_button = buttons->addButton(QObject::tr("Reset"), QDialogButtonBox::ResetRole);
  reset_button->setObjectName(QStringLiteral("rawResetButton"));
  auto* open_button = buttons->addButton(QObject::tr("Open"), QDialogButtonBox::AcceptRole);
  open_button->setObjectName(QStringLiteral("rawOpenButton"));
  open_button->setDefault(true);
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
    processing_note->setVisible(params.processing_version < raw::kProcessingVersion);
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
  // The Shift-click baseline: the histogram of a develop with the Tone sliders
  // at their defaults, its settings key, and the auto values solved from it on
  // first use. The first measurement for a key is kept, so repeated clicks see
  // the same histogram and give the same value.
  std::optional<raw::DevelopParams> auto_baseline_key;
  std::optional<RawPreviewHistogram> auto_baseline;
  bool auto_values_solved = false;
  std::optional<RawToneAutoValues> auto_values;
  QSlider* pending_auto_slider = nullptr;
  const auto store_auto_baseline = [&](const raw::DevelopParams& rendered, const RawPreviewHistogram& measured) {
    const auto key = tone_baseline_key(rendered);
    if (auto_baseline_key && *auto_baseline_key == key) return;
    auto_baseline_key = key;
    auto_baseline = measured;
    auto_values_solved = false;
  };
  bool reset_requested = false;
  std::optional<raw::DevelopParams> displayed_draft;
  bool refinement_due = false;
  std::optional<RawPreviewState::Completion> accurate_cache;

  const auto set_controls_enabled = [&](bool enabled) {
    histogram->setEnabled(enabled);
    profile_group->setEnabled(enabled);
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

  auto* refine = new QTimer(&dialog);
  refine->setSingleShot(true);
  refine->setInterval(500);
  const auto sliders_dragging = [&] {
    for (auto* slider : dialog.findChildren<QSlider*>()) if (slider->isSliderDown()) return true;
    return false;
  };
  const auto set_busy_status = [&](const QString& text) { status_label->setText(text); };
  const auto show_open_progress = [&] {
    // Before the first draft completes, Open is waiting for the source snapshot.
    // Afterwards it joins accurate processing, preserving any progress already made.
    const bool accurate = displayed_draft.has_value();
    const auto& lane = state->lane(accurate);
    const int percent = lane.active && lane.active->generation == state->generation.load() &&
        lane.active->params == final_params ? lane.progress_percent : 0;
    dialog.setProperty("rawProgressPercent", percent);
    dialog.setProperty("rawProgressAccurate", accurate);
    if (!accurate) {
      set_busy_status(QObject::tr("Preparing RAW... %1%").arg(percent));
    } else {
      set_busy_status((final_params.half_size ? QObject::tr("Developing half size... %1%")
                                            : QObject::tr("Developing full resolution... %1%")).arg(percent));
    }
  };
  const auto request_preview = [&](bool accurate) {
    if (accepting || state->closed) return;
    read_params_from_widgets();
    retry_button->hide();
    if (accurate && accurate_cache && accurate_cache->params == params) {
      refinement_due = false;
      if (state->apply) state->apply(*accurate_cache);
      return;
    }
    if (accurate && (!displayed_draft || *displayed_draft != params)) {
      refinement_due = true;
      enqueue_raw_develop(state, params, false);
      return;
    }
    if (accurate) refinement_due = false;
    set_busy_status(accurate ? QObject::tr("Refining... %1%").arg(0) : QObject::tr("Updating quick preview... %1%").arg(0));
    enqueue_raw_develop(state, params, accurate);
  };
  const auto enqueue_preview = [&] { request_preview(false); };

  const auto save_settings = [&] {
    if (!reset_requested && params == saved_settings.params) return true;
    bool replace = false;
    if (saved_settings.exists && !saved_settings.recognized) {
      QMessageBox prompt(QMessageBox::Warning, QObject::tr("RAW settings"),
          QObject::tr("The existing RAW settings file is unreadable or unsupported. Replace it to save these adjustments."),
          QMessageBox::NoButton, &dialog);
      prompt.setObjectName(QStringLiteral("rawSettingsSaveMessageBox"));
      auto* replace_button = prompt.addButton(QObject::tr("Replace Settings"), QMessageBox::AcceptRole);
      replace_button->setObjectName(QStringLiteral("rawReplaceSettingsButton"));
      auto* without = prompt.addButton(QObject::tr("Open Without Saving"), QMessageBox::ActionRole);
      without->setObjectName(QStringLiteral("rawOpenWithoutSavingButton"));
      prompt.addButton(QMessageBox::Cancel);
      exec_dialog(prompt);
      if (prompt.clickedButton() == without) return true;
      if (prompt.clickedButton() != replace_button) return false;
      replace = true;
    }
    for (;;) {
      const auto error = save_raw_develop_settings(file_path, params, saved_settings, replace);
      if (error.isEmpty()) return true;
      QMessageBox prompt(QMessageBox::Warning, QObject::tr("RAW settings"), error,
                         QMessageBox::Retry | QMessageBox::Cancel, &dialog);
      prompt.setObjectName(QStringLiteral("rawSettingsSaveMessageBox"));
      auto* without = prompt.addButton(QObject::tr("Open Without Saving"), QMessageBox::ActionRole);
      without->setObjectName(QStringLiteral("rawOpenWithoutSavingButton"));
      exec_dialog(prompt);
      if (prompt.clickedButton() == without) return true;
      if (prompt.standardButton(prompt.clickedButton()) != QMessageBox::Retry) return false;
    }
  };
  const auto finish_open = [&] {
    if (!accurate_cache || !accurate_cache->document || accurate_cache->params != final_params) return;
    if (!save_settings()) {
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
    dialog.setProperty("rawInfoReady", true);
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
    if (!completion.final_render && accurate_cache && accurate_cache->params == completion.params)
      completion = *accurate_cache;
    preview_has_render = true;
    preview->set_image(completion.image, completion.output_size);
    histogram->set_histogram(completion.histogram);
    // A preview with the Tone sliders at their defaults is itself the baseline.
    if (completion.histogram && tone_sliders_at_defaults(completion.params))
      store_auto_baseline(completion.params, *completion.histogram);
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
    dialog.setProperty("rawProgressPercent", 100);
    if (completion.final_render) {
      accurate_cache = std::move(completion);
      set_busy_status(QString());
      if (accepting) finish_open();
    } else {
      displayed_draft = completion.params;
      if (accepting) {
        show_open_progress();
        enqueue_raw_develop(state, final_params, true);
      } else {
        set_busy_status(QObject::tr("Quick preview - waiting to refine"));
        if (refinement_due && !sliders_dragging()) request_preview(true);
      }
    }
  };

  state->progress = [&](int percent, bool accurate) {
    state->lane(accurate).progress_percent = percent;
    if (accepting) {
      // Draft completions/progress must not replace Open's accurate progress.
      show_open_progress();
      return;
    }
    dialog.setProperty("rawProgressPercent", percent);
    dialog.setProperty("rawProgressAccurate", accurate);
    if (accurate) set_busy_status(QObject::tr("Refining... %1%").arg(percent));
    else if (!accepting) set_busy_status(QObject::tr("Updating quick preview... %1%").arg(percent));
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
    auto& lane = state->lane(work.final_render);
    lane.in_flight = true;
    lane.active = work;
    lane.progress_percent = 0;
    if (state->progress) state->progress(0, work.final_render);
    auto* app = QCoreApplication::instance();
    run_tracked_background_worker([state, work, app]() mutable {
      RawPreviewState::Completion completion;
      completion.final_render = work.final_render;
      completion.params = work.params;
      QString error;
      bool fatal = false;
      bool cancelled = false;
      try {
        auto& worker_lane = state->lane(work.final_render);
        if (worker_lane.session == nullptr) {
          // Accurate work is admitted only after the first draft completed, so this
          // immutable byte snapshot is published before the second lane reads it.
          if (!state->file_bytes)
            state->file_bytes = std::make_shared<const std::vector<std::uint8_t>>(read_file_bytes_for_worker(state->file_path));
          worker_lane.session = std::make_shared<raw::DevelopSession>(*state->file_bytes);
          if (!work.final_render) QMetaObject::invokeMethod(app, [state, info = worker_lane.session->info()] {
            if (!state->closed && state->info_ready) state->info_ready(info);
          }, Qt::QueuedConnection);
        }
        raw::DevelopOptions options;
        options.quality = work.final_render ? raw::DevelopQuality::Final : raw::DevelopQuality::Draft;
        options.cancelled = [state, work] {
          // Short drafts finish into the cache even when superseded. This avoids
          // repeated sensor unpacking during a drag. Closure cancels both lanes.
          return state->closed.load() || (work.final_render &&
              state->generation.load(std::memory_order_acquire) != work.generation);
        };
        options.progress = [state, work, app](int percent) {
          QMetaObject::invokeMethod(app, [state, work, percent] {
            if (!state->closed && state->progress && work.generation == state->generation.load())
              state->progress(std::min(percent, 99), work.final_render);
          }, Qt::QueuedConnection);
        };
        auto developed = worker_lane.session->develop(work.params, options);
        completion.image = image_from_developed(developed);
        completion.output_size = QSize(developed.output_width, developed.output_height);
        completion.white_balance = developed.effective_white_balance;
        completion.histogram = raw_preview_histogram(developed.rgb, developed.width, developed.height);
        if (work.final_render) {
          auto result = raw::document_from_developed(developed);
          completion.document = std::make_shared<Document>(std::move(result.document));
        }
      } catch (const raw::DevelopCancelled&) {
        cancelled = true;
      } catch (const std::exception& caught) {
        error = QString::fromUtf8(caught.what());
        fatal = !work.final_render && state->lane(false).session == nullptr;
      }
      QMetaObject::invokeMethod(app,
          [state, work, completion = std::move(completion), error, fatal, cancelled]() mutable {
            auto& completed_lane = state->lane(work.final_render);
            completed_lane.in_flight = false;
            completed_lane.active.reset();
            if (state->closed) return;
            const auto is_latest = work.generation == state->generation.load(std::memory_order_acquire);
            if (!error.isEmpty()) {
              if (state->fail && (is_latest || fatal)) state->fail(error, fatal);
            } else if (!cancelled && is_latest && state->apply) {
              state->apply(std::move(completion));
            }
            if (completed_lane.pending.has_value() && state->start) {
              auto next = std::move(*completed_lane.pending);
              completed_lane.pending.reset();
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
    state->draft.pending.reset();
    state->accurate.pending.reset();
    refinement_due = false;
    dialog.setProperty("rawPreviewAccurate", false);
    if (params.white_balance == raw::WhiteBalanceMode::Auto) {
      auto_white_balance_pending = true;
      refresh_value_labels();
    }
    set_busy_status(QObject::tr("Updating quick preview... %1%").arg(0));
    // Throttle during a continuous drag, rather than postponing every frame.
    if (!debounce->isActive()) debounce->start();
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
    request_preview(true);
  });
  for (auto* slider : dialog.findChildren<QSlider*>()) {
    QObject::connect(slider, &QSlider::sliderReleased, &dialog, [&] {
      debounce->stop();
      request_preview(false);
      refine->start();
    });
  }
  QObject::connect(retry_button, &QPushButton::clicked, &dialog, [&] { request_preview(true); });

  // --- Histogram regions (GitHub issue 393) ---
  // The dialog has no Blacks or Whites slider: those ends of the histogram
  // drive Shadows and Highlights, the nearest controls.
  const auto slider_for_region = [&](RawHistogramRegion region) -> QSlider* {
    switch (region) {
      case RawHistogramRegion::Shadows:
        return shadows_slider;
      case RawHistogramRegion::Exposure:
        return exposure_slider;
      case RawHistogramRegion::Highlights:
        return tone_highlights_slider;
      case RawHistogramRegion::None:
        break;
    }
    return nullptr;
  };
  const auto value_label_for_region = [&](RawHistogramRegion region) -> QLabel* {
    switch (region) {
      case RawHistogramRegion::Shadows:
        return shadows_value;
      case RawHistogramRegion::Exposure:
        return exposure_value;
      case RawHistogramRegion::Highlights:
        return tone_highlights_value;
      case RawHistogramRegion::None:
        break;
    }
    return nullptr;
  };
  const auto region_name = [](RawHistogramRegion region) {
    switch (region) {
      case RawHistogramRegion::Shadows:
        return QObject::tr("Shadows");
      case RawHistogramRegion::Exposure:
        return QObject::tr("Exposure");
      case RawHistogramRegion::Highlights:
        return QObject::tr("Highlights");
      case RawHistogramRegion::None:
        break;
    }
    return QString();
  };
  QSlider* histogram_drag_slider = nullptr;
  int histogram_drag_start = 0;
  auto histogram_drag_region = RawHistogramRegion::None;
  const auto refresh_histogram_label = [&] {
    const auto region =
        histogram_drag_region != RawHistogramRegion::None ? histogram_drag_region : histogram->hovered_region();
    auto* value = value_label_for_region(region);
    histogram_label->setText(value == nullptr ? QString()
                                              : QObject::tr("%1: %2").arg(region_name(region), value->text()));
  };
  histogram->set_hover_callback([&](RawHistogramRegion) { refresh_histogram_label(); });
  histogram->set_drag_callback([&](RawHistogramRegion region, double travel, RawHistogramDragPhase phase) {
    if (phase == RawHistogramDragPhase::Begin) {
      auto* slider = slider_for_region(region);
      if (slider == nullptr || !slider->isEnabled() || accepting) return;
      histogram_drag_slider = slider;
      histogram_drag_start = slider->value();
      histogram_drag_region = region;
      // Held down like a slider drag: refinement waits for the release, which
      // requests the quick preview and the refinement once.
      slider->setSliderDown(true);
      refresh_histogram_label();
      return;
    }
    if (histogram_drag_slider == nullptr) return;
    // A drag across the whole histogram covers half the slider's range.
    const auto range = histogram_drag_slider->maximum() - histogram_drag_slider->minimum();
    // Open locking the controls ends a drag; the settings it captured stay.
    if (!accepting) histogram_drag_slider->setValue(histogram_drag_start + static_cast<int>(std::lround(travel * range / 2.0)));
    if (phase == RawHistogramDragPhase::End) {
      auto* slider = std::exchange(histogram_drag_slider, nullptr);
      histogram_drag_region = RawHistogramRegion::None;
      slider->setSliderDown(false);
    }
    refresh_histogram_label();
  });

  // --- Shift-click auto, Alt-click reset (GitHub issue 394) ---
  const raw::DevelopParams default_params;
  const auto apply_auto_value = [&](QSlider* slider) {
    if (!auto_values_solved) {
      auto_values_solved = true;
      auto_values.reset();
      if (auto_baseline)
        auto_values = raw_auto_tone(*auto_baseline, RawToneModel(params.profile, params.processing_version));
    }
    if (!auto_values) return;
    const auto& values = *auto_values;
    if (slider == exposure_slider) slider->setValue(static_cast<int>(std::lround(values.exposure_ev * 100.0)));
    else if (slider == contrast_slider) slider->setValue(static_cast<int>(std::lround(values.contrast)));
    else if (slider == tone_highlights_slider) slider->setValue(static_cast<int>(std::lround(values.highlights)));
    else if (slider == shadows_slider) slider->setValue(static_cast<int>(std::lround(values.shadows)));
  };
  state->baseline_ready = [&](raw::DevelopParams rendered, RawPreviewHistogram measured) {
    store_auto_baseline(rendered, measured);
    if (pending_auto_slider == nullptr || accepting) return;
    read_params_from_widgets();
    if (*auto_baseline_key != tone_baseline_key(params)) {
      // Settings moved on while it was measured: measure again.
      request_baseline_develop(state, tone_baseline_params(params));
      return;
    }
    auto* slider = std::exchange(pending_auto_slider, nullptr);
    dialog.setProperty("rawAutoMeasuring", false);
    apply_auto_value(slider);
  };
  const auto apply_modifier_click = [&](QSlider* slider, bool reset) {
    if (accepting) return;
    if (slider == temperature_slider || slider == tint_slider) {
      select_combo_data(white_balance_combo, reset ? QStringLiteral("asShot") : QStringLiteral("auto"));
      return;
    }
    if (slider == denoise_slider) {
      // Auto is both the default and the automatic noise reduction.
      if (reset) select_combo_data(noise_combo, QStringLiteral("auto"));
      return;
    }
    if (reset) {
      if (slider == brightness_slider) slider->setValue(static_cast<int>(std::lround(default_params.brightness * 100.0)));
      else if (slider == exposure_slider) slider->setValue(static_cast<int>(std::lround(default_params.exposure_ev * 100.0)));
      else if (slider == contrast_slider) slider->setValue(static_cast<int>(std::lround(default_params.contrast)));
      else if (slider == tone_highlights_slider) slider->setValue(static_cast<int>(std::lround(default_params.highlights)));
      else if (slider == shadows_slider) slider->setValue(static_cast<int>(std::lround(default_params.shadows)));
      else if (slider == saturation_slider) slider->setValue(static_cast<int>(std::lround(default_params.saturation)));
      else if (slider == vibrance_slider) slider->setValue(static_cast<int>(std::lround(default_params.vibrance)));
      return;
    }
    // Brightness, Saturation and Vibrance have no automatic value.
    if (slider != exposure_slider && slider != contrast_slider && slider != tone_highlights_slider &&
        slider != shadows_slider)
      return;
    // Nothing is developed yet (the file snapshot is not published either).
    if (!displayed_draft) return;
    read_params_from_widgets();
    if (!auto_baseline_key || *auto_baseline_key != tone_baseline_key(params)) {
      // Measure the baseline for the current settings; the click applies when it arrives.
      pending_auto_slider = slider;
      dialog.setProperty("rawAutoMeasuring", true);
      request_baseline_develop(state, tone_baseline_params(params));
      return;
    }
    apply_auto_value(slider);
  };
  auto* modifier_click_filter = new CallbackEventFilter(&dialog, [&](QObject* watched, QEvent* event) {
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick) return false;
    const auto* mouse = static_cast<QMouseEvent*>(event);
    // Modifiers folded into this event, never the global keyboard state.
    const auto reset = mouse->modifiers().testFlag(Qt::AltModifier);
    const auto automatic = !reset && mouse->modifiers().testFlag(Qt::ShiftModifier);
    if (mouse->button() != Qt::LeftButton || (!reset && !automatic)) return false;
    const auto row = std::find_if(slider_rows.begin(), slider_rows.end(), [watched](const SliderRow& candidate) {
      return candidate.slider == watched || candidate.name == watched || candidate.value == watched;
    });
    if (row == slider_rows.end()) return false;
    // A double-click's press already acted; consume the second one too.
    if (event->type() == QEvent::MouseButtonPress) apply_modifier_click(row->slider, reset);
    return true;
  });
  const auto auto_and_reset_hint =
      resolve_modifier_names(QObject::tr("Shift-click sets the automatic value. %ALT%-click resets it."));
  const auto reset_hint = resolve_modifier_names(QObject::tr("%ALT%-click resets it."));
  const auto white_balance_hint =
      resolve_modifier_names(QObject::tr("Shift-click uses Auto white balance. %ALT%-click returns to As Shot."));
  const auto denoise_hint = resolve_modifier_names(QObject::tr("%ALT%-click returns noise reduction to Auto."));
  for (const auto& row : slider_rows) {
    const auto& hint = row.slider == temperature_slider || row.slider == tint_slider ? white_balance_hint
                       : row.slider == denoise_slider                                  ? denoise_hint
                       : row.slider == exposure_slider || row.slider == contrast_slider ||
                               row.slider == tone_highlights_slider || row.slider == shadows_slider
                           ? auto_and_reset_hint
                           : reset_hint;
    for (QWidget* widget : {static_cast<QWidget*>(row.slider), row.name, static_cast<QWidget*>(row.value)}) {
      if (widget == nullptr) continue;
      widget->installEventFilter(modifier_click_filter);
      widget->setToolTip(widget->toolTip().isEmpty() ? hint : widget->toolTip() + QLatin1Char('\n') + hint);
    }
  }

  // --- Preview zoom (GitHub issue 395) ---
  const auto refresh_zoom_combo = [preview, zoom_combo] {
    const QSignalBlocker block(zoom_combo);
    const auto factor = preview->zoom();
    if (preview->fit_mode()) {
      zoom_combo->setCurrentIndex(0);
      zoom_combo->setEditText(QObject::tr("Fit (%1%)").arg(std::lround(factor * 100.0)));
      return;
    }
    for (int index = 1; index < zoom_combo->count(); ++index) {
      if (std::abs(zoom_combo->itemData(index).toDouble() - factor) < 0.0005) {
        zoom_combo->setCurrentIndex(index);
        zoom_combo->setEditText(zoom_combo->itemText(index));
        return;
      }
    }
    zoom_combo->setEditText(format_zoom_percent(factor));
  };
  preview->set_zoom_changed_callback(refresh_zoom_combo);
  refresh_zoom_combo();
  const auto apply_typed_zoom = [preview, zoom_combo, refresh_zoom_combo] {
    auto text = zoom_combo->currentText().trimmed();
    if (text.startsWith(zoom_combo->itemText(0), Qt::CaseInsensitive)) {
      preview->zoom_to_fit();
    } else {
      text.remove(QLatin1Char('%'));
      text = text.trimmed();
      bool ok = false;
      auto percent = QLocale().toDouble(text, &ok);
      if (!ok) percent = text.toDouble(&ok);
      if (ok && percent > 0.0) preview->zoom_to(percent / 100.0);
    }
    refresh_zoom_combo();
  };
  QObject::connect(zoom_combo, &QComboBox::activated, &dialog, [preview, zoom_combo](int index) {
    const auto factor = zoom_combo->itemData(index).toDouble();
    if (factor > 0.0) preview->zoom_to(factor);
    else preview->zoom_to_fit();
  });
  // Enter applies a typed zoom without pressing the dialog's default Open button.
  auto* zoom_entry_filter = new CallbackEventFilter(&dialog, [apply_typed_zoom](QObject*, QEvent* event) {
    if (event->type() != QEvent::KeyPress) return false;
    const auto key = static_cast<QKeyEvent*>(event)->key();
    if (key != Qt::Key_Return && key != Qt::Key_Enter) return false;
    apply_typed_zoom();
    return true;
  });
  zoom_combo->lineEdit()->installEventFilter(zoom_entry_filter);
  QObject::connect(zoom_combo->lineEdit(), &QLineEdit::editingFinished, &dialog, apply_typed_zoom);
  QObject::connect(zoom_100_button, &QPushButton::clicked, &dialog, [preview] { preview->zoom_to(1.0); });
  QObject::connect(zoom_fit_button, &QPushButton::clicked, &dialog, [preview] { preview->zoom_to_fit(); });
  const auto scrubby_key = QStringLiteral("imports/rawDevelopScrubbyZoom");
  scrubby_check->setChecked(app_settings().value(scrubby_key, false).toBool());
  preview->set_scrubby_zoom(scrubby_check->isChecked());
  QObject::connect(scrubby_check, &QCheckBox::toggled, &dialog, [preview, scrubby_key](bool checked) {
    preview->set_scrubby_zoom(checked);
    auto settings = app_settings();
    settings.setValue(scrubby_key, checked);
  });
  // The callbacks above capture this frame's locals by reference; disarm them
  // before those locals go, ahead of the dialog's own destruction.
  const auto disarm_callbacks = qScopeGuard([state, histogram, preview, modifier_click_filter, zoom_entry_filter] {
    state->baseline_ready = {};
    histogram->set_hover_callback({});
    histogram->set_drag_callback({});
    preview->set_zoom_changed_callback({});
    modifier_click_filter->clear();
    zoom_entry_filter->clear();
  });

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
    debounce->stop();
    refine->stop();
    if (accurate_cache && accurate_cache->params == final_params) {
      finish_open();
      return;
    }
    show_open_progress();
    if (displayed_draft) enqueue_raw_develop(state, final_params, true);
    else {
      // Initial file loading must publish its immutable byte snapshot before
      // another lane starts. Complete the first draft, then accept accurately.
      refinement_due = true;
      enqueue_raw_develop(state, final_params, false);
    }
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
