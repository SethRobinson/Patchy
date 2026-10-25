#include "ui/camera_raw_filter_dialog.hpp"

#include "core/resample.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/filter_preview_proxy.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/modifier_names.hpp"
#include "ui/raw_develop_histogram.hpp"
#include "ui/theme_qss.hpp"
#include "ui/zoomable_image_preview.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// The quick preview's long edge, Develop Raw's draft bound (docs/camera-raw.md).
constexpr int kDraftLongEdge = 1280;
constexpr int kPreviewThrottleMs = 80;
constexpr int kRefineIdleMs = 500;
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

// The histogram counts visible pixels only (transparent ones carry no color), on a
// stride bounded like Develop Raw's.
std::optional<RawPreviewHistogram> visible_histogram(const QImage& image) {
  if (image.isNull()) return std::nullopt;
  const auto area = static_cast<double>(image.width()) * image.height();
  const auto step = std::max(1, static_cast<int>(std::ceil(std::sqrt(area / kRawHistogramMaxSamples))));
  std::vector<std::uint8_t> rgb;
  rgb.reserve(static_cast<std::size_t>(area / (step * step) + image.width()) * 3U);
  for (int y = 0; y < image.height(); y += step) {
    const auto* row = image.constScanLine(y);
    for (int x = 0; x < image.width(); x += step) {
      const auto* px = row + static_cast<std::size_t>(x) * 4U;
      if (px[3] == 0) continue;
      rgb.insert(rgb.end(), px, px + 3);
    }
  }
  if (rgb.empty()) return std::nullopt;
  return raw_preview_histogram(rgb, static_cast<int>(rgb.size() / 3U), 1);
}

int depth_bits(BitDepth depth) {
  switch (depth) {
    case BitDepth::UInt8:
      return 8;
    case BitDepth::UInt16:
      return 16;
    case BitDepth::Float32:
      return 32;
  }
  return 8;
}

// Two lanes like Develop Raw's: a quick draft of a bounded copy and the accurate render
// of the whole layer, each with one worker and one latest pending request.
struct FilterPreviewState {
  struct Work {
    std::uint64_t generation{0};
    PhotoDevelopParams params;
    bool final_render{false};
  };
  struct Completion {
    QImage image;
    std::shared_ptr<const PixelBuffer> pixels;  // accurate renders only
    bool final_render{false};
    PhotoDevelopParams params;
    std::optional<RawPreviewHistogram> histogram;
  };
  struct Lane {
    bool in_flight{false};
    std::optional<Work> active;
    std::optional<Work> pending;
  } draft, accurate;
  Lane& lane(bool final_render) { return final_render ? accurate : draft; }

  CameraRawFilterRequest request;
  std::atomic<bool> closed{false};
  std::atomic<std::uint64_t> generation{0};
  // Built by the first draft and then only read by the draft lane.
  std::shared_ptr<const PixelBuffer> draft_source;
  FilterSelection draft_selection;
  std::function<void(Work)> start;
  std::function<void(Completion)> apply;
  std::function<void(std::array<double, 3>)> mean_ready;
};

void enqueue_filter_render(const std::shared_ptr<FilterPreviewState>& state, const PhotoDevelopParams& params,
                           bool final_render) {
  if (state == nullptr || state->closed || !state->start) return;
  const auto generation = state->generation.load(std::memory_order_acquire);
  FilterPreviewState::Work work{generation, params, final_render};
  auto& lane = state->lane(final_render);
  if (lane.in_flight) {
    if (lane.active && lane.active->generation == generation && lane.active->params == params) return;
    lane.pending = work;
    return;
  }
  state->start(work);
}

void close_filter_render(const std::shared_ptr<FilterPreviewState>& state) {
  if (state == nullptr) return;
  state->closed = true;
  state->generation.fetch_add(1, std::memory_order_acq_rel);
  state->draft.pending.reset();
  state->accurate.pending.reset();
  state->start = {};
  state->apply = {};
  state->mean_ready = {};
}

void prepare_draft(FilterPreviewState& state) {
  const auto& source = *state.request.source;
  const auto largest = std::max(source.width(), source.height());
  const auto scale = largest > kDraftLongEdge ? static_cast<double>(kDraftLongEdge) / largest : 1.0;
  auto width = std::max(1, static_cast<int>(std::lround(source.width() * scale)));
  auto height = std::max(1, static_cast<int>(std::lround(source.height() * scale)));
  if (!state.request.selection.isEmpty()) {
    // The gallery's proxy scaling of the selection (and its soft coverage) to draft pixels.
    auto proxy = make_filter_preview_proxy(source, state.request.bounds, state.request.selection, kDraftLongEdge);
    if (!proxy.original.empty()) {
      width = proxy.original.width();
      height = proxy.original.height();
    }
    state.draft_selection = std::move(proxy.selection);
  }
  state.draft_source = width == source.width() && height == source.height()
                           ? state.request.source
                           : std::make_shared<const PixelBuffer>(
                                 resample_pixels(source, width, height, ResampleMethod::Bilinear));
}

}  // namespace

std::optional<CameraRawFilterOutcome> run_camera_raw_filter_dialog(QWidget* parent,
                                                                   const CameraRawFilterRequest& request) {
  if (request.source == nullptr || request.source->empty()) return std::nullopt;
  auto params = normalize_photo_develop_params(request.initial);

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("cameraRawFilterDialog"));
  dialog.setWindowTitle(QObject::tr("Camera Raw Filter"));
  dialog.resize(1120, 760);
  append_themed_style(dialog, QStringLiteral("QScrollArea#cameraRawFilterControlsScroll,"
                                             "QWidget#cameraRawFilterControlsPage { background: transparent; }"));

  auto* layout = new QVBoxLayout(&dialog);
  auto* content_row = new QHBoxLayout();
  layout->addLayout(content_row, 1);

  auto* preview = new ZoomableImagePreview(&dialog);
  preview->setObjectName(QStringLiteral("cameraRawFilterPreview"));
  preview->setMinimumSize(420, 320);
  content_row->addWidget(preview, 1);

  auto* controls_scroll = new QScrollArea(&dialog);
  controls_scroll->setObjectName(QStringLiteral("cameraRawFilterControlsScroll"));
  controls_scroll->setWidgetResizable(true);
  controls_scroll->setFrameShape(QFrame::NoFrame);
  controls_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* controls_page = new QWidget(controls_scroll);
  controls_page->setObjectName(QStringLiteral("cameraRawFilterControlsPage"));
  auto* controls_column = new QVBoxLayout(controls_page);
  controls_column->setContentsMargins(0, 0, 6, 0);
  controls_scroll->setWidget(controls_page);
  controls_scroll->setMinimumWidth(390);
  content_row->addWidget(controls_scroll);

  // Each slider with its name and value labels: Shift-click and Alt-click work on all three.
  struct SliderRow {
    QSlider* slider;
    QWidget* name;
    QLabel* value;
  };
  std::vector<SliderRow> slider_rows;
  // One value column width for every group, so sliders end at one x too.
  constexpr int kValueSpacing = 6;
  const int value_width =
      std::max(58, dialog.fontMetrics().horizontalAdvance(QObject::tr("%1 EV").arg(QStringLiteral("-0.00"))) + 6);
  const auto add_slider_row = [&slider_rows, value_width](QFormLayout* form, const QString& label, int minimum, int maximum,
                                             const char* object_name) {
    auto* slider = new QSlider(Qt::Horizontal);
    slider->setObjectName(QLatin1String(object_name));
    slider->setRange(minimum, maximum);
    slider->setMinimumWidth(170);
    auto* value_label = new QLabel();
    value_label->setObjectName(QLatin1String(object_name) + QStringLiteral("Value"));
    value_label->setFixedWidth(value_width);
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kValueSpacing);
    row->addWidget(slider, 1);
    row->addWidget(value_label);
    form->addRow(label, row);
    slider_rows.push_back({slider, form->labelForField(row), value_label});
    return std::make_pair(slider, value_label);
  };

  auto* histogram = new RawHistogramWidget(controls_page);
  histogram->setObjectName(QStringLiteral("cameraRawFilterHistogram"));
  controls_column->addWidget(histogram);
  auto* histogram_label = new QLabel(controls_page);
  histogram_label->setObjectName(QStringLiteral("cameraRawFilterHistogramRegionLabel"));
  histogram_label->setAlignment(Qt::AlignCenter);
  histogram_label->setMinimumHeight(histogram_label->fontMetrics().height());
  controls_column->addWidget(histogram_label);

  // --- White balance: relative to the layer's own colors ---
  auto* white_balance_group = new QGroupBox(QObject::tr("White Balance"), &dialog);
  auto* white_balance_form = new QFormLayout(white_balance_group);
  auto [temperature_slider, temperature_value] =
      add_slider_row(white_balance_form, QObject::tr("Temperature:"), -100, 100, "cameraRawFilterTemperatureSlider");
  auto [tint_slider, tint_value] =
      add_slider_row(white_balance_form, QObject::tr("Tint:"), -100, 100, "cameraRawFilterTintSlider");
  auto* auto_white_balance_button = new QPushButton(QObject::tr("Auto"), white_balance_group);
  auto_white_balance_button->setObjectName(QStringLiteral("cameraRawFilterAutoWhiteBalanceButton"));
  auto_white_balance_button->setToolTip(QObject::tr("Neutralize the layer's average color"));
  // A text-sized button under the sliders, its right edge on theirs (Seth, October 25, 2026).
  auto_white_balance_button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  auto* auto_row = new QHBoxLayout();
  auto_row->setContentsMargins(0, 0, 0, 0);
  auto_row->setSpacing(0);
  auto_row->addStretch(1);
  auto_row->addWidget(auto_white_balance_button);
  auto_row->addSpacing(kValueSpacing + value_width);
  white_balance_form->addRow(QString(), auto_row);
  temperature_slider->setToolTip(QObject::tr("Warms or cools the layer. 0 keeps its colors."));
  tint_slider->setToolTip(QObject::tr("Shifts the layer toward magenta or green. 0 keeps its colors."));
  controls_column->addWidget(white_balance_group);

  // --- Tone ---
  auto* tone_group = new QGroupBox(QObject::tr("Tone"), &dialog);
  auto* tone_form = new QFormLayout(tone_group);
  const auto exposure_steps = static_cast<int>(kPhotoDevelopMaxExposureEv * 100.0);
  auto [exposure_slider, exposure_value] =
      add_slider_row(tone_form, QObject::tr("Exposure:"), -exposure_steps, exposure_steps, "cameraRawFilterExposureSlider");
  auto [contrast_slider, contrast_value] =
      add_slider_row(tone_form, QObject::tr("Contrast:"), -100, 100, "cameraRawFilterContrastSlider");
  auto [highlights_slider, highlights_value] =
      add_slider_row(tone_form, QObject::tr("Highlights:"), -100, 100, "cameraRawFilterHighlightsSlider");
  auto [shadows_slider, shadows_value] =
      add_slider_row(tone_form, QObject::tr("Shadows:"), -100, 100, "cameraRawFilterShadowsSlider");
  controls_column->addWidget(tone_group);

  // --- Color ---
  auto* color_group = new QGroupBox(QObject::tr("Color"), &dialog);
  auto* color_form = new QFormLayout(color_group);
  auto [saturation_slider, saturation_value] =
      add_slider_row(color_form, QObject::tr("Saturation:"), -100, 100, "cameraRawFilterSaturationSlider");
  auto [vibrance_slider, vibrance_value] =
      add_slider_row(color_form, QObject::tr("Vibrance:"), -100, 100, "cameraRawFilterVibranceSlider");
  controls_column->addWidget(color_group);
  align_form_label_columns({white_balance_form, tone_form, color_form});

  auto* info_label = new QLabel(&dialog);
  info_label->setObjectName(QStringLiteral("cameraRawFilterInfoLabel"));
  info_label->setWordWrap(true);
  {
    QStringList lines;
    lines.push_back(QObject::tr("Layer: %1 x %2, %3 bits per channel")
                        .arg(request.source->width())
                        .arg(request.source->height())
                        .arg(depth_bits(request.source->format().bit_depth)));
    if (!request.selection.isEmpty()) lines.push_back(QObject::tr("Applies inside the selection."));
    lines.push_back(QObject::tr("Profile, demosaic and noise reduction need raw sensor data: they are in Develop Raw "
                                "when a camera raw file opens."));
    info_label->setText(lines.join(QLatin1Char('\n')));
  }
  controls_column->addWidget(info_label);
  controls_column->addStretch(1);

  auto* status_row = new QHBoxLayout();
  auto* zoom_combo = new QComboBox(&dialog);
  zoom_combo->setObjectName(QStringLiteral("cameraRawFilterZoomCombo"));
  zoom_combo->setEditable(true);
  zoom_combo->setInsertPolicy(QComboBox::NoInsert);
  zoom_combo->setMinimumContentsLength(9);
  zoom_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  zoom_combo->addItem(QObject::tr("Fit"), 0.0);
  for (const auto factor : kPreviewZoomPresets) zoom_combo->addItem(format_zoom_percent(factor), factor);
  zoom_combo->setToolTip(QObject::tr("Preview zoom. Pick a size or type a percentage."));
  auto* zoom_100_button = new QPushButton(QStringLiteral("100%"), &dialog);
  zoom_100_button->setObjectName(QStringLiteral("cameraRawFilterZoom100Button"));
  zoom_100_button->setToolTip(QObject::tr("Zoom to 100% (1 image pixel = 1 screen pixel)"));
  auto* zoom_fit_button = new QPushButton(QObject::tr("Fit"), &dialog);
  zoom_fit_button->setObjectName(QStringLiteral("cameraRawFilterZoomFitButton"));
  zoom_fit_button->setToolTip(QObject::tr("Fit the image in the preview"));
  auto* scrubby_check = new QCheckBox(QObject::tr("Scrubby Zoom"), &dialog);
  scrubby_check->setObjectName(QStringLiteral("cameraRawFilterScrubbyZoomCheck"));
  scrubby_check->setToolTip(QObject::tr("Drag left or right on the preview to zoom."));
  auto* preview_check = new QCheckBox(QObject::tr("Preview"), &dialog);
  preview_check->setObjectName(QStringLiteral("cameraRawFilterPreviewCheck"));
  preview_check->setChecked(true);
  preview_check->setToolTip(QObject::tr("Turn off to compare with the layer before the filter"));
  status_row->addWidget(zoom_combo);
  status_row->addWidget(zoom_100_button);
  status_row->addWidget(zoom_fit_button);
  status_row->addWidget(scrubby_check);
  status_row->addWidget(preview_check);
  status_row->addSpacing(8);
  auto* status_label = new QLabel(&dialog);
  status_label->setObjectName(QStringLiteral("cameraRawFilterStatus"));
  status_row->addWidget(status_label, 1);
  auto* buttons = new QDialogButtonBox(&dialog);
  auto* reset_button = buttons->addButton(QObject::tr("Reset"), QDialogButtonBox::ResetRole);
  reset_button->setObjectName(QStringLiteral("cameraRawFilterResetButton"));
  auto* ok_button = buttons->addButton(QDialogButtonBox::Ok);
  ok_button->setObjectName(QStringLiteral("cameraRawFilterOkButton"));
  ok_button->setDefault(true);
  buttons->addButton(QDialogButtonBox::Cancel);
  status_row->addWidget(buttons);
  layout->addLayout(status_row);

  // --- Widget <-> params sync ---
  bool syncing_widgets = false;
  const auto refresh_value_labels = [&] {
    temperature_value->setText(QString::number(temperature_slider->value()));
    tint_value->setText(QString::number(tint_slider->value()));
    exposure_value->setText(QObject::tr("%1 EV").arg(QString::number(exposure_slider->value() / 100.0, 'f', 2)));
    contrast_value->setText(QString::number(contrast_slider->value()));
    highlights_value->setText(QString::number(highlights_slider->value()));
    shadows_value->setText(QString::number(shadows_slider->value()));
    saturation_value->setText(QString::number(saturation_slider->value()));
    vibrance_value->setText(QString::number(vibrance_slider->value()));
  };
  const auto apply_params_to_widgets = [&] {
    syncing_widgets = true;
    const auto set = [](QSlider* slider, double value) {
      const QSignalBlocker block(slider);
      slider->setValue(static_cast<int>(std::lround(value)));
    };
    set(temperature_slider, params.temperature);
    set(tint_slider, params.tint);
    set(exposure_slider, params.exposure_ev * 100.0);
    set(contrast_slider, params.contrast);
    set(highlights_slider, params.highlights);
    set(shadows_slider, params.shadows);
    set(saturation_slider, params.saturation);
    set(vibrance_slider, params.vibrance);
    refresh_value_labels();
    syncing_widgets = false;
  };
  const auto read_params_from_widgets = [&] {
    // Script-set or initial fractions stay until their slider actually moves.
    const auto read_slider = [](QSlider* slider, double& value, double scale = 1.0) {
      if (slider->value() != static_cast<int>(std::lround(value * scale))) value = slider->value() / scale;
    };
    read_slider(temperature_slider, params.temperature);
    read_slider(tint_slider, params.tint);
    read_slider(exposure_slider, params.exposure_ev, 100.0);
    read_slider(contrast_slider, params.contrast);
    read_slider(highlights_slider, params.highlights);
    read_slider(shadows_slider, params.shadows);
    read_slider(saturation_slider, params.saturation);
    read_slider(vibrance_slider, params.vibrance);
  };
  // With Preview off the preview shows the layer as it is.
  const auto shown_params = [&] { return preview_check->isChecked() ? params : PhotoDevelopParams{}; };

  // --- Async rendering ---
  auto state = std::make_shared<FilterPreviewState>();
  state->request = request;
  // The callbacks below capture this frame by reference while the worker posts to the
  // application object, which outlives the dialog: disarm them on every exit path.
  const auto close_preview = qScopeGuard([state] { close_filter_render(state); });

  std::optional<CameraRawFilterOutcome> outcome;
  PhotoDevelopParams final_params;
  bool accepting = false;
  std::optional<PhotoDevelopParams> displayed_draft;
  std::optional<PhotoDevelopParams> histogram_params;
  bool refinement_due = false;
  std::optional<FilterPreviewState::Completion> accurate_cache;
  std::optional<std::array<double, 3>> mean_linear_rgb;

  const auto set_controls_enabled = [&](bool enabled) {
    histogram->setEnabled(enabled);
    white_balance_group->setEnabled(enabled);
    tone_group->setEnabled(enabled);
    color_group->setEnabled(enabled);
    reset_button->setEnabled(enabled);
    ok_button->setEnabled(enabled);
    preview_check->setEnabled(enabled);
  };

  auto* throttle = new QTimer(&dialog);
  throttle->setSingleShot(true);
  throttle->setInterval(kPreviewThrottleMs);
  auto* refine = new QTimer(&dialog);
  refine->setSingleShot(true);
  refine->setInterval(kRefineIdleMs);
  const auto sliders_dragging = [&] {
    for (auto* slider : dialog.findChildren<QSlider*>())
      if (slider->isSliderDown()) return true;
    return false;
  };

  const auto finish_accept = [&] {
    if (!accurate_cache || !accurate_cache->pixels || accurate_cache->params != final_params) return;
    outcome = CameraRawFilterOutcome{final_params, accurate_cache->pixels};
    dialog.accept();
  };
  const auto request_preview = [&](bool accurate) {
    if (accepting || state->closed) return;
    read_params_from_widgets();
    const auto shown = shown_params();
    if (accurate && accurate_cache && accurate_cache->params == shown) {
      refinement_due = false;
      if (state->apply) state->apply(*accurate_cache);
      return;
    }
    if (accurate && (!displayed_draft || *displayed_draft != shown)) {
      refinement_due = true;
      enqueue_filter_render(state, shown, false);
      return;
    }
    if (accurate) refinement_due = false;
    status_label->setText(accurate ? QObject::tr("Refining...") : QObject::tr("Updating quick preview..."));
    enqueue_filter_render(state, shown, accurate);
  };

  state->mean_ready = [&](std::array<double, 3> mean) {
    mean_linear_rgb = mean;
    auto_white_balance_button->setEnabled(!accepting);
  };
  state->apply = [&](FilterPreviewState::Completion completion) {
    if (completion.final_render) accurate_cache = completion;
    if (accepting) {
      finish_accept();
      return;
    }
    if (!completion.final_render && accurate_cache && accurate_cache->params == completion.params)
      completion = *accurate_cache;
    preview->set_image(completion.image, QSize(request.source->width(), request.source->height()));
    histogram->set_histogram(completion.histogram);
    histogram_params = completion.params;
    dialog.setProperty("cameraRawFilterPreviewAccurate", completion.final_render);
    if (completion.final_render) {
      status_label->setText(QString());
    } else {
      displayed_draft = completion.params;
      status_label->setText(QObject::tr("Quick preview - waiting to refine"));
      if (refinement_due && !sliders_dragging()) request_preview(true);
    }
  };
  state->start = [state](FilterPreviewState::Work work) {
    auto& lane = state->lane(work.final_render);
    lane.in_flight = true;
    lane.active = work;
    auto* app = QCoreApplication::instance();
    run_tracked_background_worker([state, work, app]() mutable {
      FilterPreviewState::Completion completion;
      completion.final_render = work.final_render;
      completion.params = work.params;
      bool superseded = false;
      if (work.final_render) {
        // A superseded accurate render is skipped rather than finished.
        superseded = state->closed.load() ||
                     state->generation.load(std::memory_order_acquire) != work.generation;
        if (!superseded) {
          auto pixels = *state->request.source;
          apply_camera_raw_filter_to_pixels(pixels, state->request.bounds, state->request.selection, work.params);
          completion.image = display_qimage_from_pixel_buffer(pixels);
          completion.pixels = std::make_shared<const PixelBuffer>(std::move(pixels));
        }
      } else {
        if (state->draft_source == nullptr) {
          prepare_draft(*state);
          QMetaObject::invokeMethod(app, [state, mean = photo_mean_linear_rgb(*state->draft_source)] {
            if (!state->closed && state->mean_ready) state->mean_ready(mean);
          }, Qt::QueuedConnection);
        }
        auto pixels = *state->draft_source;
        apply_camera_raw_filter_to_pixels(pixels, Rect::from_size(pixels.width(), pixels.height()),
                                          state->draft_selection, work.params);
        completion.image = display_qimage_from_pixel_buffer(pixels);
      }
      if (!superseded) completion.histogram = visible_histogram(completion.image);
      QMetaObject::invokeMethod(app, [state, work, completion = std::move(completion), superseded]() mutable {
        auto& completed_lane = state->lane(work.final_render);
        completed_lane.in_flight = false;
        completed_lane.active.reset();
        if (state->closed) return;
        if (!superseded && work.generation == state->generation.load(std::memory_order_acquire) && state->apply)
          state->apply(std::move(completion));
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
    if (syncing_widgets) return;
    read_params_from_widgets();
    state->generation.fetch_add(1, std::memory_order_acq_rel);
    state->draft.pending.reset();
    state->accurate.pending.reset();
    refinement_due = false;
    dialog.setProperty("cameraRawFilterPreviewAccurate", false);
    status_label->setText(QObject::tr("Updating quick preview..."));
    // Throttle during a continuous drag, rather than postponing every frame.
    if (!throttle->isActive()) throttle->start();
    refine->start();
  };
  for (auto* value_slider : {temperature_slider, tint_slider, exposure_slider, contrast_slider, highlights_slider,
                             shadows_slider, saturation_slider, vibrance_slider}) {
    QObject::connect(value_slider, &QSlider::valueChanged, &dialog, [&] {
      refresh_value_labels();
      on_control_changed();
    });
    QObject::connect(value_slider, &QSlider::sliderReleased, &dialog, [&] {
      throttle->stop();
      request_preview(false);
      refine->start();
    });
  }
  QObject::connect(preview_check, &QCheckBox::toggled, &dialog, on_control_changed);
  QObject::connect(throttle, &QTimer::timeout, &dialog, [&] { request_preview(false); });
  QObject::connect(refine, &QTimer::timeout, &dialog, [&] {
    if (sliders_dragging()) {
      refine->start();
      return;
    }
    request_preview(true);
  });
  const auto apply_auto_white_balance = [&] {
    if (!mean_linear_rgb || accepting) return;
    const auto [temperature, tint] = photo_auto_white_balance(*mean_linear_rgb);
    params.temperature = temperature;
    params.tint = tint;
    apply_params_to_widgets();
    on_control_changed();
  };
  auto_white_balance_button->setEnabled(false);
  QObject::connect(auto_white_balance_button, &QPushButton::clicked, &dialog, apply_auto_white_balance);

  // --- Histogram regions: Develop Raw's (GitHub issue 393) ---
  const auto slider_for_region = [&](RawHistogramRegion region) -> QSlider* {
    switch (region) {
      case RawHistogramRegion::Shadows:
        return shadows_slider;
      case RawHistogramRegion::Exposure:
        return exposure_slider;
      case RawHistogramRegion::Highlights:
        return highlights_slider;
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
        return highlights_value;
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
    histogram_label->setText(value == nullptr ? QString() : QObject::tr("%1: %2").arg(region_name(region), value->text()));
  };
  histogram->set_hover_callback([&](RawHistogramRegion) { refresh_histogram_label(); });
  histogram->set_drag_callback([&](RawHistogramRegion region, double travel, RawHistogramDragPhase phase) {
    if (phase == RawHistogramDragPhase::Begin) {
      auto* slider = slider_for_region(region);
      if (slider == nullptr || !slider->isEnabled() || accepting) return;
      histogram_drag_slider = slider;
      histogram_drag_start = slider->value();
      histogram_drag_region = region;
      slider->setSliderDown(true);
      refresh_histogram_label();
      return;
    }
    if (histogram_drag_slider == nullptr) return;
    // A drag across the whole histogram covers half the slider's range.
    const auto range = histogram_drag_slider->maximum() - histogram_drag_slider->minimum();
    if (!accepting) histogram_drag_slider->setValue(histogram_drag_start + static_cast<int>(std::lround(travel * range / 2.0)));
    if (phase == RawHistogramDragPhase::End) {
      auto* slider = std::exchange(histogram_drag_slider, nullptr);
      histogram_drag_region = RawHistogramRegion::None;
      slider->setSliderDown(false);
    }
    refresh_histogram_label();
  });

  // --- Shift-click auto, Alt-click reset (Develop Raw's GitHub issue 394) ---
  const auto apply_modifier_click = [&](QSlider* slider, bool reset) {
    if (accepting) return;
    if (reset) {
      slider->setValue(0);
      return;
    }
    if (slider == temperature_slider || slider == tint_slider) {
      apply_auto_white_balance();
      return;
    }
    // Saturation and Vibrance have no automatic value.
    const auto& measured = histogram->histogram();
    if (!measured || !histogram_params) return;
    std::optional<int> value;
    if (slider == exposure_slider) {
      if (const auto ev = raw_auto_exposure_ev(*measured, histogram_params->exposure_ev))
        value = static_cast<int>(std::lround(*ev * 100.0));
    } else if (slider == contrast_slider) {
      if (const auto amount = raw_auto_contrast(*measured, histogram_params->contrast))
        value = static_cast<int>(std::lround(*amount));
    } else if (slider == highlights_slider) {
      if (const auto amount = raw_auto_highlights(*measured, histogram_params->highlights))
        value = static_cast<int>(std::lround(*amount));
    } else if (slider == shadows_slider) {
      if (const auto amount = raw_auto_shadows(*measured, histogram_params->shadows))
        value = static_cast<int>(std::lround(*amount));
    }
    if (value) slider->setValue(*value);
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
    if (event->type() == QEvent::MouseButtonPress) apply_modifier_click(row->slider, reset);
    return true;
  });
  const auto auto_and_reset_hint =
      resolve_modifier_names(QObject::tr("Shift-click sets the automatic value. %ALT%-click resets it."));
  const auto reset_hint = resolve_modifier_names(QObject::tr("%ALT%-click resets it."));
  const auto white_balance_hint =
      resolve_modifier_names(QObject::tr("Shift-click neutralizes the average color. %ALT%-click resets it."));
  for (const auto& row : slider_rows) {
    const auto& hint = row.slider == temperature_slider || row.slider == tint_slider ? white_balance_hint
                       : row.slider == saturation_slider || row.slider == vibrance_slider ? reset_hint
                                                                                         : auto_and_reset_hint;
    for (QWidget* widget : {static_cast<QWidget*>(row.slider), row.name, static_cast<QWidget*>(row.value)}) {
      if (widget == nullptr) continue;
      widget->installEventFilter(modifier_click_filter);
      widget->setToolTip(widget->toolTip().isEmpty() ? hint : widget->toolTip() + QLatin1Char('\n') + hint);
    }
  }

  // --- Preview zoom (Develop Raw's GitHub issue 395) ---
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
  // Enter applies a typed zoom without pressing the dialog's default OK button.
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
  // One Scrubby Zoom preference for both raw develop previews.
  const auto scrubby_key = QStringLiteral("imports/rawDevelopScrubbyZoom");
  scrubby_check->setChecked(app_settings().value(scrubby_key, false).toBool());
  preview->set_scrubby_zoom(scrubby_check->isChecked());
  QObject::connect(scrubby_check, &QCheckBox::toggled, &dialog, [preview, scrubby_key](bool checked) {
    preview->set_scrubby_zoom(checked);
    auto settings = app_settings();
    settings.setValue(scrubby_key, checked);
  });
  const auto disarm_callbacks = qScopeGuard([histogram, preview, modifier_click_filter, zoom_entry_filter] {
    histogram->set_hover_callback({});
    histogram->set_drag_callback({});
    preview->set_zoom_changed_callback({});
    modifier_click_filter->clear();
    zoom_entry_filter->clear();
  });

  QObject::connect(reset_button, &QPushButton::clicked, &dialog, [&] {
    params = PhotoDevelopParams{};
    apply_params_to_widgets();
    on_control_changed();
  });
  QObject::connect(ok_button, &QPushButton::clicked, &dialog, [&, state] {
    if (accepting) return;
    read_params_from_widgets();
    final_params = params;
    if (PhotoDeveloper(final_params).is_identity()) {
      outcome = CameraRawFilterOutcome{final_params, nullptr};
      dialog.accept();
      return;
    }
    accepting = true;
    set_controls_enabled(false);
    auto_white_balance_button->setEnabled(false);
    throttle->stop();
    refine->stop();
    if (accurate_cache && accurate_cache->params == final_params) {
      finish_accept();
      return;
    }
    status_label->setText(QObject::tr("Applying..."));
    // Accept renders the real settings even when the preview is switched off.
    state->generation.fetch_add(1, std::memory_order_acq_rel);
    state->draft.pending.reset();
    enqueue_filter_render(state, final_params, true);
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QObject::connect(&dialog, &QDialog::finished, &dialog, [state] { close_filter_render(state); });

  apply_params_to_widgets();
  request_preview(false);
  refinement_due = true;
  refine->start();

  exec_dialog(dialog);
  close_filter_render(state);
  if (dialog.result() != QDialog::Accepted || !outcome.has_value()) return std::nullopt;
  return outcome;
}

}  // namespace patchy::ui
