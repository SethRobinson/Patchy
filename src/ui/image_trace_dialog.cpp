#include "ui/image_trace_dialog.hpp"

#include "ui/background_workers.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace patchy::ui {

namespace {

constexpr double kMaxPreviewZoom = 16.0;
constexpr double kMinPreviewZoom = 0.0625;
constexpr int kPreviewDebounceMs = 150;

using Mode = ImageTraceOptions::Mode;
using Method = ImageTraceOptions::Method;

ImageTraceOptions make_preset(Mode mode, int colors, int threshold, int paths, int corners, int noise,
                              Method method, bool ignore_white) {
  ImageTraceOptions options;
  options.mode = mode;
  options.colors = colors;
  options.threshold = threshold;
  options.paths = paths;
  options.corners = corners;
  options.noise = noise;
  options.method = method;
  options.snap_curves_to_lines = false;
  options.ignore_white = ignore_white;
  return options;
}

[[nodiscard]] QImage qimage_from_rgba8(const PixelBuffer& pixels) {
  QImage image(pixels.width(), pixels.height(), QImage::Format_ARGB32);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      row[x] = qRgba(px[0], px[1], px[2], px[3]);
    }
  }
  return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// Zoomable, pannable view of the rendered trace over a transparency
// checkerboard. Fit-to-window by default; the zoom buttons, mouse wheel, and
// double-click zoom, dragging pans.
class TracePreviewWidget final : public QWidget {
public:
  explicit TracePreviewWidget(QWidget* parent, QSize document_size) : QWidget(parent), document_size_(document_size) {
    setObjectName(QStringLiteral("imageTracePreview"));
    setMinimumSize(420, 320);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::OpenHandCursor);
    setToolTip(QObject::tr("Drag to pan. The mouse wheel zooms."));
    pan_center_ = QPointF(document_size_.width() / 2.0, document_size_.height() / 2.0);
  }

  void set_zoom_changed_callback(std::function<void()> callback) { zoom_changed_ = std::move(callback); }

  void set_rendered(QImage image) {
    rendered_ = std::move(image);
    publish_zoom_state();
    update();
  }

  [[nodiscard]] bool has_rendered() const noexcept { return !rendered_.isNull(); }
  [[nodiscard]] double zoom() const { return fit_mode_ ? fit_zoom() : zoom_; }
  [[nodiscard]] bool fit_mode() const noexcept { return fit_mode_; }

  void zoom_to_fit() {
    fit_mode_ = true;
    pan_center_ = QPointF(document_size_.width() / 2.0, document_size_.height() / 2.0);
    publish_zoom_state();
    update();
  }

  void zoom_to(double factor, std::optional<QPointF> anchor = std::nullopt) {
    const auto previous_zoom = zoom();
    const auto bounded = std::clamp(factor, std::min(fit_zoom(), kMinPreviewZoom), kMaxPreviewZoom);
    const QPointF widget_center(width() / 2.0, height() / 2.0);
    if (anchor.has_value() && previous_zoom > 0.0) {
      const QPointF document_point = pan_center_ + (*anchor - widget_center) / previous_zoom;
      pan_center_ = document_point - (*anchor - widget_center) / bounded;
    }
    fit_mode_ = false;
    zoom_ = bounded;
    clamp_pan();
    publish_zoom_state();
    update();
  }

  void zoom_step(int direction, std::optional<QPointF> anchor = std::nullopt) {
    static constexpr std::array<double, 15> kSteps = {kMinPreviewZoom, 0.125, 0.25, 1.0 / 3.0, 0.5,
                                                      2.0 / 3.0,       1.0,   1.5,  2.0,       3.0,
                                                      4.0,             6.0,   8.0,  12.0,      kMaxPreviewZoom};
    const auto current = zoom();
    double target = current;
    if (direction > 0) {
      target = kMaxPreviewZoom;
      for (const auto step : kSteps) {
        if (step > current * 1.001) {
          target = step;
          break;
        }
      }
    } else {
      target = std::min(fit_zoom(), kMinPreviewZoom);
      for (auto it = kSteps.rbegin(); it != kSteps.rend(); ++it) {
        if (*it < current * 0.999) {
          target = *it;
          break;
        }
      }
    }
    zoom_to(target, anchor);
  }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter painter(this);
    painter.fillRect(rect(), theme().canvas_backdrop);
    const auto z = zoom();
    if (z <= 0.0 || document_size_.isEmpty()) {
      return;
    }
    const QPointF widget_center(width() / 2.0, height() / 2.0);
    const QPointF top_left = widget_center - QPointF(pan_center_.x() * z, pan_center_.y() * z);
    const QRectF image_rect(top_left, QSizeF(document_size_.width() * z, document_size_.height() * z));
    // The transparency checkerboard depicts alpha (deliberately not a theme
    // role, docs/ui-conventions.md).
    painter.save();
    painter.setClipRect(image_rect);
    painter.fillRect(image_rect, QColor(200, 200, 200));
    constexpr int kCheck = 8;
    const auto x0 = static_cast<int>(std::floor(image_rect.left()));
    const auto y0 = static_cast<int>(std::floor(image_rect.top()));
    for (int y = y0; y < image_rect.bottom(); y += kCheck) {
      for (int x = x0 + ((((y - y0) / kCheck) % 2 == 0) ? 0 : kCheck); x < image_rect.right(); x += kCheck * 2) {
        painter.fillRect(QRect(x, y, kCheck, kCheck), QColor(240, 240, 240));
      }
    }
    painter.restore();
    if (!rendered_.isNull()) {
      painter.setRenderHint(QPainter::SmoothPixmapTransform, z < 1.0);
      painter.drawImage(image_rect, rendered_);
      painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    }
    painter.setPen(theme().canvas_document_border);
    painter.drawRect(image_rect.adjusted(-1.0, -1.0, 0.0, 0.0));
  }

  void resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    clamp_pan();
    publish_zoom_state();
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      panning_ = true;
      pan_press_position_ = event->position();
      pan_press_center_ = pan_center_;
      setCursor(Qt::ClosedHandCursor);
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (panning_ && (event->buttons() & Qt::LeftButton) != 0) {
      const auto z = zoom();
      if (z > 0.0) {
        pan_center_ = pan_press_center_ - (event->position() - pan_press_position_) / z;
        clamp_pan();
        update();
      }
      event->accept();
      return;
    }
    QWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && panning_) {
      panning_ = false;
      setCursor(Qt::OpenHandCursor);
      event->accept();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      if (fit_mode_) {
        zoom_to(1.0, event->position());
      } else {
        zoom_to_fit();
      }
      event->accept();
      return;
    }
    QWidget::mouseDoubleClickEvent(event);
  }

  void wheelEvent(QWheelEvent* event) override {
    const auto delta = event->angleDelta().y();
    if (delta == 0) {
      event->ignore();
      return;
    }
    zoom_step(delta > 0 ? 1 : -1, event->position());
    event->accept();
  }

private:
  [[nodiscard]] double fit_zoom() const {
    if (document_size_.isEmpty() || width() <= 0 || height() <= 0) {
      return 1.0;
    }
    const auto scale = std::min(static_cast<double>(width()) / document_size_.width(),
                                static_cast<double>(height()) / document_size_.height());
    return std::clamp(scale, 0.01, kMaxPreviewZoom);
  }

  void clamp_pan() {
    const auto z = zoom();
    if (z <= 0.0) {
      return;
    }
    const auto clamp_axis = [](double center, double visible, double total) {
      if (visible >= total) {
        return total / 2.0;
      }
      return std::clamp(center, visible / 2.0, total - visible / 2.0);
    };
    pan_center_.setX(clamp_axis(pan_center_.x(), width() / z, document_size_.width()));
    pan_center_.setY(clamp_axis(pan_center_.y(), height() / z, document_size_.height()));
  }

  void publish_zoom_state() {
    setProperty("previewZoomPercent", static_cast<int>(std::lround(zoom() * 100.0)));
    setProperty("previewFitMode", fit_mode_);
    if (zoom_changed_) {
      zoom_changed_();
    }
  }

  QSize document_size_;
  QImage rendered_;
  std::function<void()> zoom_changed_;
  QPointF pan_center_;
  QPointF pan_press_position_;
  QPointF pan_press_center_;
  double zoom_{1.0};
  bool fit_mode_{true};
  bool panning_{false};
};

// One in-flight trace at a time with a one-deep latest-wins queue (the async
// preview pattern shared with the raw develop and filter dialogs). Stale
// work aborts through the tracer's cancellation poll.
struct TracePreviewState {
  struct Work {
    std::uint64_t generation{0};
    ImageTraceOptions options;
  };
  struct Completion {
    std::uint64_t generation{0};
    std::shared_ptr<const ImageTraceResult> result;
    QImage rendered;
  };

  std::shared_ptr<const PixelBuffer> pixels;
  bool closed{false};
  bool in_flight{false};
  std::atomic<std::uint64_t> generation{0};
  std::optional<Work> pending;
  std::function<void(Work)> start;
  std::function<void(Completion)> apply;
};

void enqueue_trace(const std::shared_ptr<TracePreviewState>& state, const ImageTraceOptions& options) {
  if (state == nullptr || state->closed || !state->start) {
    return;
  }
  const auto generation = state->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  TracePreviewState::Work work{generation, options};
  if (state->in_flight) {
    state->pending = work;
    return;
  }
  state->start(work);
}

void close_trace_preview(const std::shared_ptr<TracePreviewState>& state) {
  if (state == nullptr) {
    return;
  }
  state->closed = true;
  state->generation.fetch_add(1, std::memory_order_acq_rel);
  state->pending.reset();
  state->start = {};
  state->apply = {};
}

}  // namespace

const std::vector<ImageTracePreset>& image_trace_presets() {
  static const std::vector<ImageTracePreset> presets = {
      {"Black and White Logo", make_preset(Mode::BlackAndWhite, 16, 128, 50, 75, 25, Method::Abutting, true)},
      {"Sketched Art", make_preset(Mode::BlackAndWhite, 16, 200, 60, 85, 10, Method::Abutting, true)},
      {"Silhouettes", make_preset(Mode::BlackAndWhite, 16, 80, 40, 60, 40, Method::Abutting, true)},
      {"3 Colors", make_preset(Mode::Color, 3, 128, 50, 75, 25, Method::Abutting, false)},
      {"6 Colors", make_preset(Mode::Color, 6, 128, 50, 75, 25, Method::Abutting, false)},
      {"16 Colors", make_preset(Mode::Color, 16, 128, 50, 75, 25, Method::Abutting, false)},
      {"Shades of Gray", make_preset(Mode::Grayscale, 16, 128, 50, 75, 25, Method::Abutting, false)},
      {"Low Fidelity Photo", make_preset(Mode::Color, 16, 128, 40, 60, 25, Method::Overlapping, false)},
      {"High Fidelity Photo", make_preset(Mode::Color, 64, 128, 80, 50, 4, Method::Overlapping, false)},
  };
  return presets;
}

std::optional<ImageTraceDialogResult> request_image_trace(QWidget* parent, std::shared_ptr<const PixelBuffer> pixels,
                                                          const ImageTraceOptions& initial) {
  if (pixels == nullptr || pixels->empty()) {
    return std::nullopt;
  }
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("imageTraceDialog"));
  dialog.setWindowTitle(QObject::tr("Trace Image to Shapes"));
  auto* layout = new QHBoxLayout(&dialog);

  auto* controls = new QVBoxLayout();
  auto* form = new QFormLayout();
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(6);

  auto* preset_combo = new QComboBox(&dialog);
  preset_combo->setObjectName(QStringLiteral("imageTracePresetCombo"));
  preset_combo->addItem(QObject::tr("Custom"));
  const auto& presets = image_trace_presets();
  for (const auto& preset : presets) {
    preset_combo->addItem(QObject::tr(preset.english_name));
  }
  form->addRow(QObject::tr("Preset:"), preset_combo);

  auto* mode_combo = new QComboBox(&dialog);
  mode_combo->setObjectName(QStringLiteral("imageTraceModeCombo"));
  mode_combo->addItem(QObject::tr("Color"), static_cast<int>(Mode::Color));
  mode_combo->addItem(QObject::tr("Grayscale"), static_cast<int>(Mode::Grayscale));
  mode_combo->addItem(QObject::tr("Black and White"), static_cast<int>(Mode::BlackAndWhite));
  form->addRow(QObject::tr("Mode:", "image trace"), mode_combo);

  auto* colors_spin = new QSpinBox(&dialog);
  colors_spin->setObjectName(QStringLiteral("imageTraceColorsSpin"));
  colors_spin->setRange(ImageTraceOptions::kMinColors, ImageTraceOptions::kMaxColors);
  auto* colors_label = new QLabel(QObject::tr("Colors:"), &dialog);
  form->addRow(colors_label, colors_spin);

  auto* threshold_spin = new QSpinBox(&dialog);
  threshold_spin->setObjectName(QStringLiteral("imageTraceThresholdSpin"));
  threshold_spin->setRange(1, 255);
  threshold_spin->setToolTip(QObject::tr("Pixels darker than this luminance become black"));
  auto* threshold_label = new QLabel(QObject::tr("Threshold:"), &dialog);
  form->addRow(threshold_label, threshold_spin);

  auto* paths_spin = new QSpinBox(&dialog);
  paths_spin->setObjectName(QStringLiteral("imageTracePathsSpin"));
  paths_spin->setRange(0, 100);
  paths_spin->setSuffix(QStringLiteral("%"));
  paths_spin->setToolTip(QObject::tr("Higher values follow the pixels more tightly and use more anchors"));
  form->addRow(QObject::tr("Paths:"), paths_spin);

  auto* corners_spin = new QSpinBox(&dialog);
  corners_spin->setObjectName(QStringLiteral("imageTraceCornersSpin"));
  corners_spin->setRange(0, 100);
  corners_spin->setSuffix(QStringLiteral("%"));
  corners_spin->setToolTip(QObject::tr("Higher values keep more bends as sharp corners"));
  form->addRow(QObject::tr("Corners:"), corners_spin);

  auto* noise_spin = new QSpinBox(&dialog);
  noise_spin->setObjectName(QStringLiteral("imageTraceNoiseSpin"));
  noise_spin->setRange(1, 100);
  noise_spin->setSuffix(QStringLiteral(" px"));
  noise_spin->setToolTip(QObject::tr("Regions smaller than this many pixels merge into their neighbors"));
  form->addRow(QObject::tr("Noise:"), noise_spin);

  auto* method_combo = new QComboBox(&dialog);
  method_combo->setObjectName(QStringLiteral("imageTraceMethodCombo"));
  method_combo->addItem(QObject::tr("Abutting (cutout shapes)"), static_cast<int>(Method::Abutting));
  method_combo->addItem(QObject::tr("Overlapping (stacked shapes)"), static_cast<int>(Method::Overlapping));
  method_combo->setToolTip(
      QObject::tr("Abutting shapes share exact edges. Overlapping shapes are painted without holes and stacked, "
                  "which hides hairline gaps."));
  form->addRow(QObject::tr("Method:"), method_combo);
  controls->addLayout(form);

  auto* snap_check = new QCheckBox(QObject::tr("Snap curves to lines"), &dialog);
  snap_check->setObjectName(QStringLiteral("imageTraceSnapCheck"));
  snap_check->setToolTip(QObject::tr("Replace nearly straight curves with straight segments"));
  controls->addWidget(snap_check);
  auto* ignore_white_check = new QCheckBox(QObject::tr("Ignore white"), &dialog);
  ignore_white_check->setObjectName(QStringLiteral("imageTraceIgnoreWhiteCheck"));
  ignore_white_check->setToolTip(QObject::tr("Leave white areas transparent instead of tracing them"));
  controls->addWidget(ignore_white_check);
  controls->addStretch(1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->setObjectName(QStringLiteral("imageTraceButtons"));
  controls->addWidget(buttons);
  layout->addLayout(controls);

  auto* preview_column = new QVBoxLayout();
  auto* preview = new TracePreviewWidget(&dialog, QSize(pixels->width(), pixels->height()));
  preview_column->addWidget(preview, 1);
  auto* info_row = new QHBoxLayout();
  info_row->setSpacing(4);
  auto* preview_info = new QLabel(&dialog);
  preview_info->setObjectName(QStringLiteral("imageTracePreviewInfo"));
  info_row->addWidget(preview_info, 1);
  const auto make_zoom_button = [&dialog](const char* object_name, const QString& text, const QString& tooltip) {
    auto* button = new QToolButton(&dialog);
    button->setObjectName(QLatin1String(object_name));
    button->setText(text);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
  };
  auto* zoom_fit =
      make_zoom_button("imageTraceZoomFit", QObject::tr("Fit"), QObject::tr("Fit the image in the preview"));
  auto* zoom_100 = make_zoom_button("imageTraceZoom100", QStringLiteral("100%"),
                                    QObject::tr("Zoom to 100% (1 image pixel = 1 screen pixel)"));
  auto* zoom_out = make_zoom_button("imageTraceZoomOut", QStringLiteral("-"), QObject::tr("Zoom out"));
  auto* zoom_in = make_zoom_button("imageTraceZoomIn", QStringLiteral("+"), QObject::tr("Zoom in"));
  auto* zoom_label = new QLabel(&dialog);
  zoom_label->setObjectName(QStringLiteral("imageTraceZoomLabel"));
  zoom_label->setMinimumWidth(72);
  zoom_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  info_row->addWidget(zoom_fit);
  info_row->addWidget(zoom_100);
  info_row->addWidget(zoom_out);
  info_row->addWidget(zoom_in);
  info_row->addWidget(zoom_label);
  preview_column->addLayout(info_row);
  layout->addLayout(preview_column, 1);

  preview->set_zoom_changed_callback([preview, zoom_label] {
    const auto percent = preview->property("previewZoomPercent").toInt();
    zoom_label->setText(preview->fit_mode() ? QObject::tr("Fit (%1%)").arg(percent)
                                            : QStringLiteral("%1%").arg(percent));
  });
  QObject::connect(zoom_fit, &QToolButton::clicked, &dialog, [preview] { preview->zoom_to_fit(); });
  QObject::connect(zoom_100, &QToolButton::clicked, &dialog, [preview] { preview->zoom_to(1.0); });
  QObject::connect(zoom_out, &QToolButton::clicked, &dialog, [preview] { preview->zoom_step(-1); });
  QObject::connect(zoom_in, &QToolButton::clicked, &dialog, [preview] { preview->zoom_step(1); });

  // --- control state <-> options ---
  bool syncing_widgets = false;
  const auto read_options = [&]() {
    ImageTraceOptions options;
    options.mode = static_cast<Mode>(mode_combo->currentData().toInt());
    options.colors = colors_spin->value();
    options.threshold = threshold_spin->value();
    options.paths = paths_spin->value();
    options.corners = corners_spin->value();
    options.noise = noise_spin->value();
    options.method = static_cast<Method>(method_combo->currentData().toInt());
    options.snap_curves_to_lines = snap_check->isChecked();
    options.ignore_white = ignore_white_check->isChecked();
    return options;
  };
  const auto refresh_mode_rows = [&] {
    const auto mode = static_cast<Mode>(mode_combo->currentData().toInt());
    const bool black_and_white = mode == Mode::BlackAndWhite;
    colors_label->setText(mode == Mode::Grayscale ? QObject::tr("Grays:") : QObject::tr("Colors:"));
    colors_spin->setEnabled(!black_and_white);
    threshold_spin->setEnabled(black_and_white);
  };
  const auto write_options = [&](const ImageTraceOptions& options) {
    syncing_widgets = true;
    mode_combo->setCurrentIndex(std::max(0, mode_combo->findData(static_cast<int>(options.mode))));
    colors_spin->setValue(std::clamp(options.colors, ImageTraceOptions::kMinColors, ImageTraceOptions::kMaxColors));
    threshold_spin->setValue(std::clamp(options.threshold, 1, 255));
    paths_spin->setValue(std::clamp(options.paths, 0, 100));
    corners_spin->setValue(std::clamp(options.corners, 0, 100));
    noise_spin->setValue(std::clamp(options.noise, 1, 100));
    method_combo->setCurrentIndex(std::max(0, method_combo->findData(static_cast<int>(options.method))));
    snap_check->setChecked(options.snap_curves_to_lines);
    ignore_white_check->setChecked(options.ignore_white);
    refresh_mode_rows();
    syncing_widgets = false;
  };
  const auto options_equal = [](const ImageTraceOptions& a, const ImageTraceOptions& b) {
    return a.mode == b.mode && a.colors == b.colors && a.threshold == b.threshold && a.paths == b.paths &&
           a.corners == b.corners && a.noise == b.noise && a.method == b.method &&
           a.snap_curves_to_lines == b.snap_curves_to_lines && a.ignore_white == b.ignore_white;
  };
  const auto select_matching_preset = [&] {
    const auto current = read_options();
    int row = 0;
    for (std::size_t i = 0; i < presets.size(); ++i) {
      if (options_equal(presets[i].options, current)) {
        row = static_cast<int>(i) + 1;
        break;
      }
    }
    const QSignalBlocker block(preset_combo);
    preset_combo->setCurrentIndex(row);
  };

  // --- async preview ---
  auto state = std::make_shared<TracePreviewState>();
  state->pixels = pixels;
  std::shared_ptr<const ImageTraceResult> latest_result;
  std::uint64_t latest_generation = 0;
  bool accepting = false;
  auto* ok_button = buttons->button(QDialogButtonBox::Ok);
  const auto describe = [&](const ImageTraceResult& result) {
    const auto shapes = static_cast<int>(result.layers.size());
    const auto anchors = static_cast<int>(result.anchor_count);
    if (shapes == 0) {
      return QObject::tr("Nothing to trace with these settings.");
    }
    return QObject::tr("%n shape layer(s)", nullptr, shapes) + QStringLiteral(", ") +
           QObject::tr("%n anchor(s)", nullptr, anchors);
  };
  state->apply = [&](TracePreviewState::Completion completion) {
    latest_result = completion.result;
    latest_generation = completion.generation;
    preview->set_rendered(std::move(completion.rendered));
    preview_info->setText(describe(*latest_result));
    if (accepting) {
      dialog.accept();
    }
  };
  state->start = [state](TracePreviewState::Work work) {
    state->in_flight = true;
    auto* app = QCoreApplication::instance();
    run_tracked_background_worker([state, work, app]() mutable {
      TracePreviewState::Completion completion;
      completion.generation = work.generation;
      const auto stale = [state, generation = work.generation] {
        return state->generation.load(std::memory_order_acquire) != generation;
      };
      auto traced = std::make_shared<ImageTraceResult>(trace_image(*state->pixels, work.options, stale));
      if (!stale()) {
        completion.rendered =
            qimage_from_rgba8(render_image_trace(*traced, state->pixels->width(), state->pixels->height()));
      }
      completion.result = std::move(traced);
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [state, completion = std::move(completion)]() mutable {
            state->in_flight = false;
            if (state->closed) {
              return;
            }
            const auto is_latest = completion.generation == state->generation.load(std::memory_order_acquire);
            if (is_latest && state->apply) {
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

  auto* debounce = new QTimer(&dialog);
  debounce->setSingleShot(true);
  debounce->setInterval(kPreviewDebounceMs);
  QObject::connect(debounce, &QTimer::timeout, &dialog, [&] {
    preview_info->setText(QObject::tr("Tracing..."));
    enqueue_trace(state, read_options());
  });
  const auto on_control_changed = [&] {
    if (syncing_widgets) {
      return;
    }
    refresh_mode_rows();
    select_matching_preset();
    debounce->start();
  };
  QObject::connect(mode_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) { on_control_changed(); });
  QObject::connect(method_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) { on_control_changed(); });
  for (auto* spin : {colors_spin, threshold_spin, paths_spin, corners_spin, noise_spin}) {
    QObject::connect(spin, &QSpinBox::valueChanged, &dialog, [&](int) { on_control_changed(); });
  }
  for (auto* check : {snap_check, ignore_white_check}) {
    QObject::connect(check, &QCheckBox::toggled, &dialog, [&](bool) { on_control_changed(); });
  }
  QObject::connect(preset_combo, &QComboBox::activated, &dialog, [&](int row) {
    if (row <= 0 || row > static_cast<int>(presets.size())) {
      return;
    }
    write_options(presets[static_cast<std::size_t>(row - 1)].options);
    debounce->start();
  });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    const auto current_generation = state->generation.load(std::memory_order_acquire);
    if (!debounce->isActive() && latest_result != nullptr && latest_generation == current_generation) {
      dialog.accept();
      return;
    }
    // A trace of the current settings is still pending: finish it first.
    accepting = true;
    ok_button->setEnabled(false);
    preview_info->setText(QObject::tr("Tracing..."));
    if (debounce->isActive()) {
      debounce->stop();
      enqueue_trace(state, read_options());
    }
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  write_options(initial);
  select_matching_preset();
  append_themed_style(dialog, dialog_spinbox_button_style());
  preview_info->setText(QObject::tr("Tracing..."));
  enqueue_trace(state, read_options());

  const auto code = exec_dialog(dialog);
  close_trace_preview(state);
  if (code != QDialog::Accepted || latest_result == nullptr) {
    return std::nullopt;
  }
  ImageTraceDialogResult result;
  result.options = read_options();
  result.result = latest_result;
  return result;
}

}  // namespace patchy::ui
