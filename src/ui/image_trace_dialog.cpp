#include "ui/image_trace_dialog.hpp"

#include "ui/activity_spinner.hpp"
#include "ui/app_settings.hpp"
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
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
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
constexpr std::size_t kLargeTraceAnchors = 20000;
constexpr std::size_t kLargeTraceLayers = 2000;
// The anchor-budget spin tops out exactly where the large-result warning
// begins: a budget above the warning line is as good as no budget.
constexpr int kMaxAnchorsSpinLimit = static_cast<int>(kLargeTraceAnchors);
constexpr const char* kUserPresetsSettingsKey = "imageTrace/userPresets";
// A view preference, not a trace option: outside ImageTraceOptions, presets,
// and the imageTrace/* option spellings. Persisted identifier, never rename.
constexpr const char* kShowAnchorsSettingsKey = "imageTrace/showAnchors";
// Preset combo item data (Qt::UserRole = kind, Qt::UserRole + 1 = index).
constexpr int kPresetKindCustom = 0;
constexpr int kPresetKindBuiltin = 1;
constexpr int kPresetKindUser = 2;

using Mode = ImageTraceOptions::Mode;
using Method = ImageTraceOptions::Method;

ImageTraceOptions make_preset(Mode mode, int colors, int threshold, int paths, int corners, int noise,
                              Method method, bool ignore_white, int smoothing = 0, int max_anchors = 0,
                              int merge_colors = 0) {
  ImageTraceOptions options;
  options.mode = mode;
  options.colors = colors;
  options.threshold = threshold;
  options.paths = paths;
  options.corners = corners;
  options.noise = noise;
  options.smoothing = smoothing;
  options.merge_colors = merge_colors;
  options.max_anchors = max_anchors;
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
    refresh_cursor();
    setToolTip(QObject::tr("Drag to pan. The mouse wheel zooms."));
    pan_center_ = QPointF(document_size_.width() / 2.0, document_size_.height() / 2.0);
  }

  void set_zoom_changed_callback(std::function<void()> callback) { zoom_changed_ = std::move(callback); }

  void set_rendered(QImage image) {
    rendered_ = std::move(image);
    publish_zoom_state();
    update();
  }

  // The traced paths' own anchor points (image-pixel space), marked when
  // "Show anchors" is on. Toggling only repaints; it never re-traces.
  void set_anchors(QVector<QPointF> anchors) {
    anchors_ = std::move(anchors);
    if (show_anchors_) {
      update();
    }
  }
  void set_show_anchors(bool show) {
    if (show_anchors_ == show) {
      return;
    }
    show_anchors_ = show;
    update();
  }

  [[nodiscard]] bool has_rendered() const noexcept { return !rendered_.isNull(); }
  [[nodiscard]] double zoom() const { return fit_mode_ ? fit_zoom() : zoom_; }
  [[nodiscard]] bool fit_mode() const noexcept { return fit_mode_; }

  // The arrow-plus-hourglass cursor while a trace runs; panning still works.
  void set_busy(bool busy) {
    if (busy_ == busy) {
      return;
    }
    busy_ = busy;
    refresh_cursor();
  }
  void refresh_cursor() {
    setCursor(busy_ ? Qt::BusyCursor : panning_ ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
  }

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
    if (show_anchors_ && !anchors_.isEmpty()) {
      // Anchor marks on the traced result (never the source): the canvas
      // path overlay's fixed accent-on-dark pair, which reads over arbitrary
      // artwork (the marching-ants family exemption, docs/ui-conventions.md).
      // Cosmetic pens keep the marks screen-sized; two drawPoints calls stay
      // O(1) painter calls for tens of thousands of anchors.
      painter.save();
      painter.setClipRect(image_rect);
      painter.translate(top_left);
      painter.scale(z, z);
      const auto point_count = static_cast<int>(anchors_.size());
      QPen halo(QColor(30, 34, 40), 5.0);
      halo.setCosmetic(true);
      painter.setPen(halo);
      painter.drawPoints(anchors_.constData(), point_count);
      QPen mark(QColor(116, 192, 255), 3.0);
      mark.setCosmetic(true);
      painter.setPen(mark);
      painter.drawPoints(anchors_.constData(), point_count);
      painter.restore();
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
      refresh_cursor();
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
      refresh_cursor();
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
  QVector<QPointF> anchors_;
  bool show_anchors_{false};
  std::function<void()> zoom_changed_;
  QPointF pan_center_;
  QPointF pan_press_position_;
  QPointF pan_press_center_;
  double zoom_{1.0};
  bool fit_mode_{true};
  bool panning_{false};
  bool busy_{false};
};

// One in-flight trace at a time with a one-deep latest-wins queue (the async
// preview pattern shared with the raw develop and filter dialogs). Stale
// work aborts through the tracer's cancellation poll.
struct TracePreviewState {
  struct Work {
    std::uint64_t generation{0};
    ImageTraceOptions options;
    // Selection traces: palette from the whole layer (palette_pixels) when
    // set. Carried per work item so latest-wins re-traces keep the state the
    // controls had when the work was queued.
    bool palette_from_layer{true};
  };
  struct Completion {
    std::uint64_t generation{0};
    std::shared_ptr<const ImageTraceResult> result;
    QImage rendered;
    QVector<QPointF> anchors;
  };

  std::shared_ptr<const PixelBuffer> pixels;
  // The unmasked layer when tracing inside a selection (else null): the
  // optional palette source handed to trace_image.
  std::shared_ptr<const PixelBuffer> palette_pixels;
  bool closed{false};
  bool in_flight{false};
  std::atomic<std::uint64_t> generation{0};
  std::optional<Work> pending;
  std::function<void(Work)> start;
  std::function<void(Completion)> apply;
};

void enqueue_trace(const std::shared_ptr<TracePreviewState>& state, const ImageTraceOptions& options,
                   bool palette_from_layer) {
  if (state == nullptr || state->closed || !state->start) {
    return;
  }
  const auto generation = state->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  TracePreviewState::Work work{generation, options, palette_from_layer};
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
      // The flat-art presets merge near-duplicate palette entries (15 = a
      // uniform per-channel difference of 15): a narrow-gamut image asked for
      // more colors than it distinctly has degrades to fewer clean layers
      // instead of grain speckle. Distinct flat colors sit far apart, so the
      // merge is a no-op on real flat art. The photo presets keep 0: their
      // close entries are deliberate gradient steps.
      {"3 Colors", make_preset(Mode::Color, 3, 128, 50, 75, 25, Method::Abutting, false, 0, 0, 15)},
      {"6 Colors", make_preset(Mode::Color, 6, 128, 50, 75, 25, Method::Abutting, false, 0, 0, 15)},
      {"16 Colors", make_preset(Mode::Color, 16, 128, 50, 75, 25, Method::Abutting, false, 0, 0, 15)},
      {"Shades of Gray", make_preset(Mode::Grayscale, 16, 128, 50, 75, 25, Method::Abutting, false, 0, 0, 15)},
      {"Low Fidelity Photo", make_preset(Mode::Color, 16, 128, 40, 60, 25, Method::Overlapping, false)},
      {"High Fidelity Photo", make_preset(Mode::Color, 64, 128, 80, 50, 4, Method::Overlapping, false)},
      {"Photo (Maximum)", make_preset(Mode::Color, 256, 128, 80, 50, 4, Method::Overlapping, false, 2)},
  };
  return presets;
}

bool image_trace_result_is_large(std::size_t layers, std::size_t anchors) noexcept {
  return layers >= kLargeTraceLayers || anchors >= kLargeTraceAnchors;
}

QByteArray serialize_image_trace_user_presets(const std::vector<ImageTraceUserPreset>& presets) {
  QJsonArray array;
  for (const auto& preset : presets) {
    QJsonObject object;
    object.insert(QStringLiteral("name"), preset.name);
    object.insert(QStringLiteral("mode"), static_cast<int>(preset.options.mode));
    object.insert(QStringLiteral("colors"), preset.options.colors);
    object.insert(QStringLiteral("threshold"), preset.options.threshold);
    object.insert(QStringLiteral("paths"), preset.options.paths);
    object.insert(QStringLiteral("corners"), preset.options.corners);
    object.insert(QStringLiteral("noise"), preset.options.noise);
    object.insert(QStringLiteral("smoothing"), preset.options.smoothing);
    object.insert(QStringLiteral("mergeColors"), preset.options.merge_colors);
    object.insert(QStringLiteral("maxAnchors"), preset.options.max_anchors);
    object.insert(QStringLiteral("method"), static_cast<int>(preset.options.method));
    object.insert(QStringLiteral("snapCurvesToLines"), preset.options.snap_curves_to_lines);
    object.insert(QStringLiteral("ignoreWhite"), preset.options.ignore_white);
    array.push_back(object);
  }
  return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

std::vector<ImageTraceUserPreset> deserialize_image_trace_user_presets(const QByteArray& json) {
  std::vector<ImageTraceUserPreset> presets;
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !document.isArray()) {
    return presets;
  }
  for (const auto& value : document.array()) {
    if (!value.isObject()) {
      continue;
    }
    const auto object = value.toObject();
    const auto name = object.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
      continue;
    }
    const bool duplicate = std::any_of(presets.begin(), presets.end(), [&name](const ImageTraceUserPreset& preset) {
      return preset.name.compare(name, Qt::CaseInsensitive) == 0;
    });
    if (duplicate) {
      continue;
    }
    ImageTraceOptions options;
    const auto read_int = [&object](const char* key, int fallback, int low, int high) {
      const auto entry = object.value(QLatin1String(key));
      return entry.isDouble() ? std::clamp(entry.toInt(), low, high) : fallback;
    };
    options.mode = static_cast<Mode>(read_int("mode", static_cast<int>(options.mode), 0, 2));
    options.colors =
        read_int("colors", options.colors, ImageTraceOptions::kMinColors, ImageTraceOptions::kMaxColors);
    options.threshold = read_int("threshold", options.threshold, 1, 255);
    options.paths = read_int("paths", options.paths, 0, 100);
    options.corners = read_int("corners", options.corners, 0, 100);
    options.noise = read_int("noise", options.noise, 1, 100);
    options.smoothing = read_int("smoothing", options.smoothing, 0, ImageTraceOptions::kMaxSmoothing);
    options.merge_colors = read_int("mergeColors", options.merge_colors, 0, 100);
    options.max_anchors = read_int("maxAnchors", options.max_anchors, 0, kMaxAnchorsSpinLimit);
    options.method = static_cast<Method>(read_int("method", static_cast<int>(options.method), 0, 1));
    options.snap_curves_to_lines =
        object.value(QStringLiteral("snapCurvesToLines")).toBool(options.snap_curves_to_lines);
    options.ignore_white = object.value(QStringLiteral("ignoreWhite")).toBool(options.ignore_white);
    presets.push_back(ImageTraceUserPreset{name, options});
  }
  return presets;
}

std::vector<ImageTraceUserPreset> load_image_trace_user_presets() {
  auto settings = app_settings();
  return deserialize_image_trace_user_presets(settings.value(QLatin1String(kUserPresetsSettingsKey)).toByteArray());
}

void save_image_trace_user_presets(const std::vector<ImageTraceUserPreset>& presets) {
  auto settings = app_settings();
  if (presets.empty()) {
    settings.remove(QLatin1String(kUserPresetsSettingsKey));
    return;
  }
  settings.setValue(QLatin1String(kUserPresetsSettingsKey), serialize_image_trace_user_presets(presets));
}

std::optional<ImageTraceDialogResult> request_image_trace(QWidget* parent, std::shared_ptr<const PixelBuffer> pixels,
                                                          const ImageTraceOptions& initial, bool inside_selection,
                                                          std::shared_ptr<const PixelBuffer> whole_layer_pixels,
                                                          bool initial_palette_from_layer) {
  if (pixels == nullptr || pixels->empty()) {
    return std::nullopt;
  }
  if (!inside_selection) {
    whole_layer_pixels = nullptr;
  }
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("imageTraceDialog"));
  auto* content =
      install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), QObject::tr("Trace Image to Shapes"));
  auto* layout = new QHBoxLayout();
  content->addLayout(layout, 1);

  auto* controls = new QVBoxLayout();
  auto* form = new QFormLayout();
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(6);

  auto* preset_combo = new QComboBox(&dialog);
  preset_combo->setObjectName(QStringLiteral("imageTracePresetCombo"));
  auto* save_preset = new QToolButton(&dialog);
  save_preset->setObjectName(QStringLiteral("imageTraceSavePresetButton"));
  save_preset->setText(QObject::tr("Save..."));
  save_preset->setToolTip(QObject::tr("Save the current settings as a preset"));
  save_preset->setAutoRaise(true);
  auto* delete_preset = new QToolButton(&dialog);
  delete_preset->setObjectName(QStringLiteral("imageTraceDeletePresetButton"));
  delete_preset->setText(QObject::tr("Delete"));
  delete_preset->setToolTip(QObject::tr("Delete the selected user preset"));
  delete_preset->setAutoRaise(true);
  delete_preset->setEnabled(false);
  auto* preset_row = new QWidget(&dialog);
  auto* preset_layout = new QHBoxLayout(preset_row);
  preset_layout->setContentsMargins(0, 0, 0, 0);
  preset_layout->setSpacing(4);
  preset_layout->addWidget(preset_combo, 1);
  preset_layout->addWidget(save_preset);
  preset_layout->addWidget(delete_preset);
  form->addRow(QObject::tr("Preset:"), preset_row);
  const auto& presets = image_trace_presets();
  auto user_presets = load_image_trace_user_presets();
  // Rows: Custom, the built-ins, a separator, then the user presets.
  const auto rebuild_preset_combo = [&] {
    const QSignalBlocker block(preset_combo);
    preset_combo->clear();
    preset_combo->addItem(QObject::tr("Custom"), kPresetKindCustom);
    for (std::size_t i = 0; i < presets.size(); ++i) {
      preset_combo->addItem(QObject::tr(presets[i].english_name), kPresetKindBuiltin);
      preset_combo->setItemData(preset_combo->count() - 1, static_cast<int>(i), Qt::UserRole + 1);
    }
    if (!user_presets.empty()) {
      preset_combo->insertSeparator(preset_combo->count());
      for (std::size_t i = 0; i < user_presets.size(); ++i) {
        preset_combo->addItem(user_presets[i].name, kPresetKindUser);
        preset_combo->setItemData(preset_combo->count() - 1, static_cast<int>(i), Qt::UserRole + 1);
      }
    }
  };
  const auto preset_row_for = [&](int kind, int index) {
    for (int row = 0; row < preset_combo->count(); ++row) {
      if (preset_combo->itemData(row, Qt::UserRole).toInt() == kind &&
          (kind == kPresetKindCustom || preset_combo->itemData(row, Qt::UserRole + 1).toInt() == index)) {
        return row;
      }
    }
    return 0;
  };
  rebuild_preset_combo();

  auto* mode_combo = new QComboBox(&dialog);
  mode_combo->setObjectName(QStringLiteral("imageTraceModeCombo"));
  mode_combo->addItem(QObject::tr("Color"), static_cast<int>(Mode::Color));
  mode_combo->addItem(QObject::tr("Grayscale"), static_cast<int>(Mode::Grayscale));
  mode_combo->addItem(QObject::tr("Black and White"), static_cast<int>(Mode::BlackAndWhite));
  form->addRow(QObject::tr("Mode:", "image trace"), mode_combo);

  // Slider + spin rows (the nudge-and-look controls); the spin boxes keep
  // their object names and the row widgets carry the tooltips.
  auto* colors_spin = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Colors:"), QStringLiteral("imageTraceColorsSlider"),
      QStringLiteral("imageTraceColorsSpin"), ImageTraceOptions::kMinColors, ImageTraceOptions::kMaxColors,
      initial.colors);
  auto* colors_label = qobject_cast<QLabel*>(form->labelForField(colors_spin->parentWidget()));

  auto* threshold_spin = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Threshold:"), QStringLiteral("imageTraceThresholdSlider"),
      QStringLiteral("imageTraceThresholdSpin"), 1, 255, initial.threshold);
  threshold_spin->parentWidget()->setToolTip(QObject::tr("Pixels darker than this luminance become black"));

  auto* paths_spin = add_dialog_slider_spin_row(form, &dialog, QObject::tr("Paths:"),
                                                QStringLiteral("imageTracePathsSlider"),
                                                QStringLiteral("imageTracePathsSpin"), 0, 100, initial.paths,
                                                QStringLiteral("%"));
  paths_spin->parentWidget()->setToolTip(
      QObject::tr("Higher values follow the pixels more tightly and use more anchors"));

  auto* corners_spin = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Corners:"), QStringLiteral("imageTraceCornersSlider"),
      QStringLiteral("imageTraceCornersSpin"), 0, 100, initial.corners, QStringLiteral("%"));
  corners_spin->parentWidget()->setToolTip(QObject::tr("Higher values keep more bends as sharp corners"));

  auto* noise_spin = add_dialog_slider_spin_row(form, &dialog, QObject::tr("Noise:"),
                                                QStringLiteral("imageTraceNoiseSlider"),
                                                QStringLiteral("imageTraceNoiseSpin"), 1, 100, initial.noise,
                                                QStringLiteral(" px"));
  noise_spin->parentWidget()->setToolTip(
      QObject::tr("Regions smaller than this many pixels merge into their neighbors"));

  auto* smoothing_spin = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Smoothing:"), QStringLiteral("imageTraceSmoothingSlider"),
      QStringLiteral("imageTraceSmoothingSpin"), 0, ImageTraceOptions::kMaxSmoothing, initial.smoothing,
      QStringLiteral(" px"));
  smoothing_spin->parentWidget()->setToolTip(
      QObject::tr("Blurs away grain and compression noise before colors are chosen"));

  auto* merge_colors_spin = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Merge colors:"), QStringLiteral("imageTraceMergeColorsSlider"),
      QStringLiteral("imageTraceMergeColorsSpin"), 0, 100, initial.merge_colors);
  merge_colors_spin->setSpecialValueText(QObject::tr("Off"));
  merge_colors_spin->parentWidget()->setToolTip(
      QObject::tr("Merges traced colors that are nearly identical, so flat areas stay clean; higher values merge "
                  "colors that are further apart"));

  auto* max_anchors_spin = add_dialog_slider_spin_row(
      form, &dialog, QObject::tr("Max anchors:"), QStringLiteral("imageTraceMaxAnchorsSlider"),
      QStringLiteral("imageTraceMaxAnchorsSpin"), 0, kMaxAnchorsSpinLimit, initial.max_anchors);
  max_anchors_spin->setSpecialValueText(QObject::tr("Off"));
  max_anchors_spin->parentWidget()->setToolTip(
      QObject::tr("Limits the total anchor count by loosening the curve fit until the result fits; "
                  "Off keeps every anchor"));

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
  auto* show_anchors_check = new QCheckBox(QObject::tr("Show anchors"), &dialog);
  show_anchors_check->setObjectName(QStringLiteral("imageTraceShowAnchorsCheck"));
  show_anchors_check->setToolTip(QObject::tr("Mark the anchor points of the traced paths on the preview"));
  controls->addWidget(show_anchors_check);
  auto* selection_note = new QLabel(QObject::tr("Tracing inside the selection"), &dialog);
  selection_note->setObjectName(QStringLiteral("imageTraceSelectionNoteLabel"));
  selection_note->setVisible(inside_selection);
  controls->addWidget(selection_note);
  auto* palette_check = new QCheckBox(QObject::tr("Pick colors from the whole layer"), &dialog);
  palette_check->setObjectName(QStringLiteral("imageTracePaletteFromLayerCheck"));
  palette_check->setToolTip(
      QObject::tr("Choose the traced colors from every pixel of the layer, so the selection traces with the same "
                  "colors as the whole layer. Turn off to choose colors only from the selected pixels."));
  palette_check->setChecked(initial_palette_from_layer);
  palette_check->setVisible(inside_selection && whole_layer_pixels != nullptr);
  controls->addWidget(palette_check);
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
  auto* busy = new ActivitySpinner(&dialog);
  busy->setObjectName(QStringLiteral("imageTraceBusySpinner"));
  info_row->addWidget(busy);
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
  // Frameless chrome has no native resize border; the corner grip is the resize handle.
  info_row->addWidget(new VisibleSizeGrip(&dialog), 0, Qt::AlignBottom);
  preview_column->addLayout(info_row);
  auto* size_warning = new QLabel(
      QObject::tr("Large result: editing will be slower and exported SVG files will be large. Lower Paths, "
                  "raise Noise, or set Max anchors to simplify."),
      &dialog);
  size_warning->setObjectName(QStringLiteral("imageTraceSizeWarningLabel"));
  size_warning->setWordWrap(true);
  size_warning->hide();
  preview_column->addWidget(size_warning);
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
    options.smoothing = smoothing_spin->value();
    options.merge_colors = merge_colors_spin->value();
    options.max_anchors = max_anchors_spin->value();
    options.method = static_cast<Method>(method_combo->currentData().toInt());
    options.snap_curves_to_lines = snap_check->isChecked();
    options.ignore_white = ignore_white_check->isChecked();
    return options;
  };
  const auto refresh_mode_rows = [&] {
    const auto mode = static_cast<Mode>(mode_combo->currentData().toInt());
    const bool black_and_white = mode == Mode::BlackAndWhite;
    colors_label->setText(mode == Mode::Grayscale ? QObject::tr("Grays:") : QObject::tr("Colors:"));
    colors_spin->parentWidget()->setEnabled(!black_and_white);
    merge_colors_spin->parentWidget()->setEnabled(!black_and_white);
    threshold_spin->parentWidget()->setEnabled(black_and_white);
  };
  const auto write_options = [&](const ImageTraceOptions& options) {
    syncing_widgets = true;
    mode_combo->setCurrentIndex(std::max(0, mode_combo->findData(static_cast<int>(options.mode))));
    colors_spin->setValue(std::clamp(options.colors, ImageTraceOptions::kMinColors, ImageTraceOptions::kMaxColors));
    threshold_spin->setValue(std::clamp(options.threshold, 1, 255));
    paths_spin->setValue(std::clamp(options.paths, 0, 100));
    corners_spin->setValue(std::clamp(options.corners, 0, 100));
    noise_spin->setValue(std::clamp(options.noise, 1, 100));
    smoothing_spin->setValue(std::clamp(options.smoothing, 0, ImageTraceOptions::kMaxSmoothing));
    merge_colors_spin->setValue(std::clamp(options.merge_colors, 0, 100));
    max_anchors_spin->setValue(std::clamp(options.max_anchors, 0, kMaxAnchorsSpinLimit));
    method_combo->setCurrentIndex(std::max(0, method_combo->findData(static_cast<int>(options.method))));
    snap_check->setChecked(options.snap_curves_to_lines);
    ignore_white_check->setChecked(options.ignore_white);
    refresh_mode_rows();
    syncing_widgets = false;
  };
  // Built-ins win ties with user presets; Delete applies to user rows only.
  const auto select_matching_preset = [&] {
    const auto current = read_options();
    int row = 0;
    for (std::size_t i = 0; i < presets.size() && row == 0; ++i) {
      if (presets[i].options == current) {
        row = preset_row_for(kPresetKindBuiltin, static_cast<int>(i));
      }
    }
    for (std::size_t i = 0; i < user_presets.size() && row == 0; ++i) {
      if (user_presets[i].options == current) {
        row = preset_row_for(kPresetKindUser, static_cast<int>(i));
      }
    }
    const QSignalBlocker block(preset_combo);
    preset_combo->setCurrentIndex(row);
    delete_preset->setEnabled(preset_combo->itemData(row, Qt::UserRole).toInt() == kPresetKindUser);
  };

  // Show anchors is a view preference: restored from settings, persisted on
  // toggle, and only ever repaints (the trace itself is untouched).
  {
    auto settings = app_settings();
    show_anchors_check->setChecked(settings.value(QLatin1String(kShowAnchorsSettingsKey), false).toBool());
  }
  preview->set_show_anchors(show_anchors_check->isChecked());
  QObject::connect(show_anchors_check, &QCheckBox::toggled, &dialog, [preview](bool checked) {
    preview->set_show_anchors(checked);
    auto settings = app_settings();
    settings.setValue(QLatin1String(kShowAnchorsSettingsKey), checked);
  });

  // --- async preview ---
  auto state = std::make_shared<TracePreviewState>();
  state->pixels = pixels;
  state->palette_pixels = whole_layer_pixels;
  std::shared_ptr<const ImageTraceResult> latest_result;
  std::uint64_t latest_generation = 0;
  bool accepting = false;
  auto* ok_button = buttons->button(QDialogButtonBox::Ok);
  const auto set_tracing_busy = [&](bool on) {
    busy->set_active(on);
    preview->set_busy(on);
    if (on) {
      preview_info->setText(QObject::tr("Tracing..."));
    }
  };
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
    preview->set_anchors(std::move(completion.anchors));
    set_tracing_busy(false);
    preview_info->setText(describe(*latest_result));
    size_warning->setVisible(image_trace_result_is_large(latest_result->layers.size(), latest_result->anchor_count));
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
      const PixelBuffer* palette_source =
          (work.palette_from_layer && state->palette_pixels != nullptr) ? state->palette_pixels.get() : nullptr;
      auto traced =
          std::make_shared<ImageTraceResult>(trace_image(*state->pixels, work.options, stale, 0, palette_source));
      if (!stale()) {
        completion.rendered =
            qimage_from_rgba8(render_image_trace(*traced, state->pixels->width(), state->pixels->height()));
        completion.anchors.reserve(static_cast<qsizetype>(traced->anchor_count));
        for (const auto& layer : traced->layers) {
          for (const auto& subpath : layer.path.subpaths) {
            for (const auto& anchor : subpath.anchors) {
              completion.anchors.push_back(QPointF(anchor.anchor_x, anchor.anchor_y));
            }
          }
        }
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
    set_tracing_busy(true);
    enqueue_trace(state, read_options(), palette_check->isChecked());
  });
  // A palette-scope change re-traces (it changes the result) but never
  // touches preset matching: the flag is not an ImageTraceOptions member.
  QObject::connect(palette_check, &QCheckBox::toggled, &dialog, [&](bool) { debounce->start(); });
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
  for (auto* spin : {colors_spin, threshold_spin, paths_spin, corners_spin, noise_spin, smoothing_spin,
                     merge_colors_spin, max_anchors_spin}) {
    QObject::connect(spin, &QSpinBox::valueChanged, &dialog, [&](int) { on_control_changed(); });
  }
  for (auto* check : {snap_check, ignore_white_check}) {
    QObject::connect(check, &QCheckBox::toggled, &dialog, [&](bool) { on_control_changed(); });
  }
  QObject::connect(preset_combo, &QComboBox::activated, &dialog, [&](int row) {
    const auto kind = preset_combo->itemData(row, Qt::UserRole).toInt();
    const auto index = static_cast<std::size_t>(preset_combo->itemData(row, Qt::UserRole + 1).toInt());
    if (kind == kPresetKindBuiltin && index < presets.size()) {
      write_options(presets[index].options);
    } else if (kind == kPresetKindUser && index < user_presets.size()) {
      write_options(user_presets[index].options);
    } else {
      return;
    }
    delete_preset->setEnabled(kind == kPresetKindUser);
    debounce->start();
  });
  const auto is_builtin_name = [&](const QString& name) {
    for (const auto& preset : presets) {
      if (name.compare(QObject::tr(preset.english_name), Qt::CaseInsensitive) == 0 ||
          name.compare(QLatin1String(preset.english_name), Qt::CaseInsensitive) == 0) {
        return true;
      }
    }
    return name.compare(QObject::tr("Custom"), Qt::CaseInsensitive) == 0;
  };
  QObject::connect(save_preset, &QToolButton::clicked, &dialog, [&] {
    QString initial_name;
    if (const auto row = preset_combo->currentIndex();
        preset_combo->itemData(row, Qt::UserRole).toInt() == kPresetKindUser) {
      initial_name = preset_combo->itemText(row);
    }
    QInputDialog input(&dialog);
    input.setObjectName(QStringLiteral("imageTraceSavePresetNameDialog"));
    input.setWindowTitle(QObject::tr("Save Trace Preset"));
    input.setLabelText(QObject::tr("Preset name:"));
    input.setInputMode(QInputDialog::TextInput);
    input.setTextValue(initial_name);
    input.setOkButtonText(QObject::tr("Save"));
    input.resize(420, input.sizeHint().height());
    if (exec_dialog(input) != QDialog::Accepted) {
      return;
    }
    const auto name = input.textValue().trimmed();
    if (name.isEmpty()) {
      (void)show_warning_message(&dialog, QObject::tr("Save Trace Preset"), QObject::tr("Enter a name for the preset."),
                           QMessageBox::Ok, QMessageBox::Ok, QStringLiteral("imageTracePresetNameMessageBox"));
      return;
    }
    if (is_builtin_name(name)) {
      (void)show_warning_message(&dialog, QObject::tr("Save Trace Preset"),
                           QObject::tr("\"%1\" is a built-in preset. Choose another name.").arg(name),
                           QMessageBox::Ok, QMessageBox::Ok, QStringLiteral("imageTracePresetNameMessageBox"));
      return;
    }
    const auto options = read_options();
    auto existing = std::find_if(user_presets.begin(), user_presets.end(), [&name](const ImageTraceUserPreset& p) {
      return p.name.compare(name, Qt::CaseInsensitive) == 0;
    });
    if (existing != user_presets.end()) {
      const auto answer = show_warning_message(
          &dialog, QObject::tr("Save Trace Preset"), QObject::tr("Replace the preset \"%1\"?").arg(name),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QStringLiteral("imageTraceReplacePresetMessageBox"));
      if (answer != QMessageBox::Yes) {
        return;
      }
      existing->name = name;
      existing->options = options;
    } else {
      user_presets.push_back(ImageTraceUserPreset{name, options});
    }
    save_image_trace_user_presets(user_presets);
    rebuild_preset_combo();
    select_matching_preset();
  });
  QObject::connect(delete_preset, &QToolButton::clicked, &dialog, [&] {
    const auto row = preset_combo->currentIndex();
    if (preset_combo->itemData(row, Qt::UserRole).toInt() != kPresetKindUser) {
      return;
    }
    const auto index = static_cast<std::size_t>(preset_combo->itemData(row, Qt::UserRole + 1).toInt());
    if (index >= user_presets.size()) {
      return;
    }
    const auto answer = show_warning_message(
        &dialog, QObject::tr("Delete Trace Preset"),
        QObject::tr("Delete the preset \"%1\"?").arg(user_presets[index].name), QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No, QStringLiteral("imageTraceDeletePresetMessageBox"));
    if (answer != QMessageBox::Yes) {
      return;
    }
    user_presets.erase(user_presets.begin() + static_cast<std::ptrdiff_t>(index));
    save_image_trace_user_presets(user_presets);
    rebuild_preset_combo();
    select_matching_preset();
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
    set_tracing_busy(true);
    if (debounce->isActive()) {
      debounce->stop();
      enqueue_trace(state, read_options(), palette_check->isChecked());
    }
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  write_options(initial);
  select_matching_preset();
  append_themed_style(dialog, dialog_spinbox_button_style());
  set_tracing_busy(true);
  enqueue_trace(state, read_options(), palette_check->isChecked());

  const auto code = exec_dialog(dialog);
  close_trace_preview(state);
  if (code != QDialog::Accepted || latest_result == nullptr) {
    return std::nullopt;
  }
  ImageTraceDialogResult result;
  result.options = read_options();
  result.result = latest_result;
  result.palette_from_layer = palette_check->isChecked();
  return result;
}

}  // namespace patchy::ui
