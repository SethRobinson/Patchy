#include "ui/palette_convert_dialog.hpp"

#include "core/palette_presets.hpp"
#include "formats/palette_io.hpp"
#include "ui/dialog_utils.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace patchy::ui {

namespace {

// Fixed source rows before the presets; presets follow, "From file..." is last.
enum SourceRow : int {
  kSourceOptimized = 0,
  kSourceExact = 1,
  kSourceCurrent = 2,
  kSourceFirstPreset = 3,
};

// Pixel budget for the exact full-resolution zoom window. Documents at or under
// it are converted whole (so the Floyd-Steinberg preview pattern never shifts
// while panning); larger ones convert just the visible window, and mid zooms
// whose window would exceed the budget fall back to the scaled overview image.
constexpr std::int64_t kMaxZoomWindowPixels = 2'000'000;
// Extra full-resolution pixels converted around the visible window so small pans
// redraw from cache instead of re-running the conversion.
constexpr int kZoomWindowMargin = 128;
constexpr double kMaxPreviewZoom = 16.0;
constexpr double kMinPreviewZoom = 0.0625;

[[nodiscard]] QImage qimage_from_rgb8(const PixelBuffer& pixels) {
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGB32);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    for (std::int32_t x = 0; x < pixels.width(); ++x) {
      const auto* px = pixels.pixel(x, y);
      image.setPixel(x, y, qRgb(px[0], px[1], px[2]));
    }
  }
  return image;
}

// Zoomable, pannable conversion preview. Fit-to-window by default; the zoom
// buttons, mouse wheel, and double-click zoom, dragging pans. Two conversion
// caches drive the paint: the dialog's debounced refresh supplies the converted
// downscaled overview (fast, always available), and while zoomed past the
// overview's resolution the widget converts an exact full-resolution window on
// demand through the dialog-supplied converter callback.
class ConvertPreviewWidget final : public QWidget {
public:
  using WindowConverter = std::function<QImage(const QImage& source_window)>;

  explicit ConvertPreviewWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("paletteConvertPreview"));
    setMinimumSize(420, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::OpenHandCursor);
    setToolTip(QObject::tr("Drag to pan. The mouse wheel zooms."));
  }

  void set_source(QImage full_resolution, QImage fit_scaled) {
    full_ = std::move(full_resolution);
    fit_source_ = std::move(fit_scaled);
    pan_center_ = QPointF(full_.width() / 2.0, full_.height() / 2.0);
  }

  void set_window_converter(WindowConverter converter) { convert_window_ = std::move(converter); }
  void set_zoom_changed_callback(std::function<void()> callback) { zoom_changed_ = std::move(callback); }

  // The dialog's debounced refresh pushes the converted overview here (null when
  // the current settings resolve no palette) and invalidates the zoom window.
  void set_fit_converted(QImage image) {
    fit_converted_ = std::move(image);
    window_converted_ = QImage();
    window_rect_ = QRect();
    publish_zoom_state();
    update();
  }

  [[nodiscard]] double zoom() const { return fit_mode_ ? fit_zoom() : zoom_; }
  [[nodiscard]] bool fit_mode() const noexcept { return fit_mode_; }

  void zoom_to_fit() {
    fit_mode_ = true;
    pan_center_ = QPointF(full_.width() / 2.0, full_.height() / 2.0);
    publish_zoom_state();
    update();
  }

  void zoom_to(double factor, std::optional<QPointF> anchor = std::nullopt) {
    const auto previous_zoom = zoom();
    const auto bounded = std::clamp(factor, std::min(fit_zoom(), kMinPreviewZoom), kMaxPreviewZoom);
    const QPointF widget_center(width() / 2.0, height() / 2.0);
    if (anchor.has_value() && previous_zoom > 0.0) {
      // Keep the document point under the anchor stationary through the zoom.
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
    static constexpr std::array<double, 15> kSteps = {kMinPreviewZoom,
                                                      0.125,
                                                      0.25,
                                                      1.0 / 3.0,
                                                      0.5,
                                                      2.0 / 3.0,
                                                      1.0,
                                                      1.5,
                                                      2.0,
                                                      3.0,
                                                      4.0,
                                                      6.0,
                                                      8.0,
                                                      12.0,
                                                      kMaxPreviewZoom};
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
    painter.fillRect(rect(), QColor(34, 36, 40));
    if (full_.isNull() || fit_converted_.isNull()) {
      return;
    }
    const auto z = zoom();
    if (z <= 0.0) {
      return;
    }
    const QPointF widget_center(width() / 2.0, height() / 2.0);
    const QPointF top_left = widget_center - QPointF(pan_center_.x() * z, pan_center_.y() * z);
    const QRectF image_rect(top_left, QSizeF(full_.width() * z, full_.height() * z));
    const auto fit_scale = full_.width() > 0 ? static_cast<double>(fit_source_.width()) / full_.width() : 1.0;

    bool drew_exact = false;
    if (convert_window_ && z > fit_scale * 1.0001) {
      const auto visible = visible_document_rect(top_left, z);
      if (!visible.isEmpty()) {
        if (window_converted_.isNull() || !window_rect_.contains(visible)) {
          const auto window = conversion_window_for(visible);
          if (!window.isEmpty()) {
            window_converted_ = convert_window_(full_.copy(window));
            window_rect_ = window;
          }
        }
        if (!window_converted_.isNull() && window_rect_.contains(visible)) {
          const QRectF target(top_left + QPointF(window_rect_.x() * z, window_rect_.y() * z),
                              QSizeF(window_rect_.width() * z, window_rect_.height() * z));
          painter.setRenderHint(QPainter::SmoothPixmapTransform, z < 1.0);
          painter.drawImage(target, window_converted_);
          drew_exact = true;
        }
      }
    }
    if (!drew_exact) {
      // Overview fallback: the whole-image conversion of the downscaled copy.
      // Crisp only when that copy is already full resolution and we are at or
      // past 100%; every other case is a resample and smooths.
      painter.setRenderHint(QPainter::SmoothPixmapTransform, !(fit_scale >= 1.0 && z >= 1.0));
      painter.drawImage(image_rect, fit_converted_);
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setPen(QColor(20, 22, 26));
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
      // Double-click toggles between fit and 100%.
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
    if (full_.isNull() || full_.width() <= 0 || full_.height() <= 0 || width() <= 0 || height() <= 0) {
      return 1.0;
    }
    const auto scale = std::min(static_cast<double>(width()) / full_.width(),
                                static_cast<double>(height()) / full_.height());
    return std::clamp(scale, 0.01, kMaxPreviewZoom);
  }

  [[nodiscard]] QRect visible_document_rect(QPointF top_left, double z) const {
    const auto x0 = static_cast<int>(std::floor(-top_left.x() / z));
    const auto y0 = static_cast<int>(std::floor(-top_left.y() / z));
    const auto x1 = static_cast<int>(std::ceil((width() - top_left.x()) / z)) + 1;
    const auto y1 = static_cast<int>(std::ceil((height() - top_left.y()) / z)) + 1;
    return QRect(x0, y0, x1 - x0, y1 - y0).intersected(full_.rect());
  }

  [[nodiscard]] QRect conversion_window_for(QRect visible) const {
    const auto total = static_cast<std::int64_t>(full_.width()) * full_.height();
    if (total <= kMaxZoomWindowPixels) {
      // Convert the whole image: the cache then survives every pan/zoom and the
      // Floyd-Steinberg pattern matches what the real conversion will produce.
      return full_.rect();
    }
    auto window = visible.adjusted(-kZoomWindowMargin, -kZoomWindowMargin, kZoomWindowMargin, kZoomWindowMargin);
    // Align the origin DOWN to a multiple of 8 so the ordered-dither matrices
    // (indexed by buffer-local coordinates in apply_palette_to_pixels) keep
    // whole-image phase; Floyd-Steinberg windows are approximate by nature.
    window.setLeft(std::max(0, (window.left() / 8) * 8));
    window.setTop(std::max(0, (window.top() / 8) * 8));
    window = window.intersected(full_.rect());
    if (static_cast<std::int64_t>(window.width()) * window.height() > kMaxZoomWindowPixels) {
      return {};
    }
    return window;
  }

  void clamp_pan() {
    if (full_.isNull()) {
      return;
    }
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
    pan_center_.setX(clamp_axis(pan_center_.x(), width() / z, full_.width()));
    pan_center_.setY(clamp_axis(pan_center_.y(), height() / z, full_.height()));
  }

  void publish_zoom_state() {
    // Exposed as dynamic properties so tests (and the zoom label) can read the
    // state without the class being visible outside this translation unit.
    setProperty("previewZoomPercent", static_cast<int>(std::lround(zoom() * 100.0)));
    setProperty("previewFitMode", fit_mode_);
    if (zoom_changed_) {
      zoom_changed_();
    }
  }

  QImage full_;
  QImage fit_source_;
  QImage fit_converted_;
  QImage window_converted_;
  QRect window_rect_;
  WindowConverter convert_window_;
  std::function<void()> zoom_changed_;
  QPointF pan_center_;
  QPointF pan_press_position_;
  QPointF pan_press_center_;
  double zoom_{1.0};
  bool fit_mode_{true};
  bool panning_{false};
};

}  // namespace

std::optional<PaletteConvertSettings> request_palette_convert_settings(
    QWidget* parent, const PixelBuffer& flattened_rgb8, const std::optional<Palette>& current_palette) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("paletteConvertDialog"));
  dialog.setWindowTitle(QObject::tr("Convert to Indexed (Palette)"));
  auto* layout = new QVBoxLayout(&dialog);

  auto* form = new QFormLayout();
  form->setHorizontalSpacing(10);
  form->setVerticalSpacing(6);
  auto* source_combo = new QComboBox(&dialog);
  source_combo->setObjectName(QStringLiteral("paletteConvertSourceCombo"));
  source_combo->addItem(QObject::tr("Optimized (median cut)"));
  source_combo->addItem(QObject::tr("Exact image colors"));
  source_combo->addItem(QObject::tr("Current palette"));
  const auto presets = builtin_palette_presets();
  for (const auto& preset : presets) {
    source_combo->addItem(QObject::tr(preset.english_name));
  }
  source_combo->addItem(QObject::tr("From file..."));
  const auto file_row = source_combo->count() - 1;
  if (!current_palette.has_value()) {
    // Keep row indices stable; just disable the row when nothing is attached.
    if (auto* model = qobject_cast<QStandardItemModel*>(source_combo->model()); model != nullptr) {
      model->item(kSourceCurrent)->setEnabled(false);
    }
  }
  form->addRow(QObject::tr("Palette:"), source_combo);

  auto* colors_spin = new QSpinBox(&dialog);
  colors_spin->setObjectName(QStringLiteral("paletteConvertColorsSpin"));
  colors_spin->setRange(2, 256);
  colors_spin->setValue(16);
  form->addRow(QObject::tr("Colors:"), colors_spin);

  auto* dither_combo = new QComboBox(&dialog);
  dither_combo->setObjectName(QStringLiteral("paletteConvertDitherCombo"));
  dither_combo->addItem(QObject::tr("None"), static_cast<int>(PaletteDither::None));
  dither_combo->addItem(QObject::tr("Floyd-Steinberg"), static_cast<int>(PaletteDither::FloydSteinberg));
  dither_combo->addItem(QObject::tr("Ordered 4x4"), static_cast<int>(PaletteDither::OrderedBayer4x4));
  dither_combo->addItem(QObject::tr("Ordered 8x8"), static_cast<int>(PaletteDither::OrderedBayer8x8));
  form->addRow(QObject::tr("Dither:"), dither_combo);

  auto* alpha_spin = new QSpinBox(&dialog);
  alpha_spin->setObjectName(QStringLiteral("paletteConvertAlphaSpin"));
  alpha_spin->setRange(1, 255);
  alpha_spin->setValue(128);
  alpha_spin->setToolTip(QObject::tr("Pixels with alpha below this become fully transparent; the rest become opaque"));
  form->addRow(QObject::tr("Alpha threshold:"), alpha_spin);
  layout->addLayout(form);

  auto* preview = new ConvertPreviewWidget(&dialog);
  layout->addWidget(preview, 1);

  auto* info_row = new QHBoxLayout();
  info_row->setSpacing(4);
  auto* preview_info = new QLabel(&dialog);
  preview_info->setObjectName(QStringLiteral("paletteConvertPreviewInfo"));
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
  auto* zoom_fit = make_zoom_button("paletteConvertZoomFit", QObject::tr("Fit"),
                                    QObject::tr("Fit the image in the preview"));
  auto* zoom_100 = make_zoom_button("paletteConvertZoom100", QStringLiteral("100%"),
                                    QObject::tr("Zoom to 100% (1 image pixel = 1 screen pixel)"));
  auto* zoom_out = make_zoom_button("paletteConvertZoomOut", QStringLiteral("-"), QObject::tr("Zoom out"));
  auto* zoom_in = make_zoom_button("paletteConvertZoomIn", QStringLiteral("+"), QObject::tr("Zoom in"));
  auto* zoom_label = new QLabel(&dialog);
  zoom_label->setObjectName(QStringLiteral("paletteConvertZoomLabel"));
  zoom_label->setMinimumWidth(72);
  zoom_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  info_row->addWidget(zoom_fit);
  info_row->addWidget(zoom_100);
  info_row->addWidget(zoom_out);
  info_row->addWidget(zoom_in);
  info_row->addWidget(zoom_label);
  layout->addLayout(info_row);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);

  std::optional<Palette> file_palette;
  QString file_name;

  // Resolves the palette for the current control state; empty palette = invalid.
  const auto resolve_palette = [&]() -> Palette {
    const auto row = source_combo->currentIndex();
    if (row == kSourceOptimized) {
      return quantize_to_palette(flattened_rgb8, static_cast<std::size_t>(colors_spin->value()), 0);
    }
    if (row == kSourceExact) {
      auto exact = exact_palette_from_pixels(flattened_rgb8, 256, 0);
      return exact.has_value() ? std::move(*exact) : Palette{};
    }
    if (row == kSourceCurrent) {
      return current_palette.value_or(Palette{});
    }
    if (row == file_row) {
      return file_palette.value_or(Palette{});
    }
    const auto preset_index = row - kSourceFirstPreset;
    if (preset_index >= 0 && preset_index < static_cast<int>(presets.size())) {
      const auto& preset = presets[static_cast<std::size_t>(preset_index)];
      return Palette{{preset.colors.begin(), preset.colors.end()}};
    }
    return {};
  };

  const auto full_source = qimage_from_rgb8(flattened_rgb8);
  const auto fit_source = [&full_source] {
    // Bounded overview copy so 4k documents re-quantize instantly on every
    // settings change; the zoom window path shows exact pixels on demand.
    if (full_source.width() > 640 || full_source.height() > 640) {
      return full_source.scaled(640, 640, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return full_source;
  }();
  preview->set_source(full_source, fit_source);

  // Conversion state shared by the debounced overview refresh and the preview's
  // on-demand zoom windows; cached here so pan/zoom never mixes a half-updated
  // combo state with a stale LUT.
  PaletteLut preview_lut;
  bool preview_lut_ready = false;
  auto preview_dither = PaletteDither::None;
  std::uint8_t preview_alpha = 128;
  const auto convert_source_image = [&](const QImage& source) {
    PixelBuffer pixels(source.width(), source.height(), PixelFormat::rgb8());
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      for (std::int32_t x = 0; x < pixels.width(); ++x) {
        const auto rgb = source.pixel(x, y);
        auto* px = pixels.pixel(x, y);
        px[0] = static_cast<std::uint8_t>(qRed(rgb));
        px[1] = static_cast<std::uint8_t>(qGreen(rgb));
        px[2] = static_cast<std::uint8_t>(qBlue(rgb));
      }
    }
    (void)apply_palette_to_pixels(pixels, preview_lut, preview_dither, preview_alpha);
    return qimage_from_rgb8(pixels);
  };
  preview->set_window_converter(
      [&](const QImage& window) { return preview_lut_ready ? convert_source_image(window) : QImage(); });
  preview->set_zoom_changed_callback([preview, zoom_label] {
    const auto percent = preview->property("previewZoomPercent").toInt();
    zoom_label->setText(preview->fit_mode() ? QObject::tr("Fit (%1%)").arg(percent)
                                            : QStringLiteral("%1%").arg(percent));
  });
  QObject::connect(zoom_fit, &QToolButton::clicked, &dialog, [preview] { preview->zoom_to_fit(); });
  QObject::connect(zoom_100, &QToolButton::clicked, &dialog, [preview] { preview->zoom_to(1.0); });
  QObject::connect(zoom_out, &QToolButton::clicked, &dialog, [preview] { preview->zoom_step(-1); });
  QObject::connect(zoom_in, &QToolButton::clicked, &dialog, [preview] { preview->zoom_step(1); });

  auto* preview_timer = new QTimer(&dialog);
  preview_timer->setSingleShot(true);
  preview_timer->setInterval(180);
  const auto refresh_preview = [&, preview, preview_info] {
    const auto palette = resolve_palette();
    if (palette.colors.empty()) {
      preview_lut_ready = false;
      preview->set_fit_converted(QImage());
      preview_info->setText(source_combo->currentIndex() == kSourceExact
                                ? QObject::tr("The image has more than 256 colors; choose Optimized instead.")
                                : QObject::tr("Choose a palette."));
      return;
    }
    preview_lut.build(palette.colors);
    preview_lut_ready = true;
    preview_dither = static_cast<PaletteDither>(dither_combo->currentData().toInt());
    preview_alpha = static_cast<std::uint8_t>(alpha_spin->value());
    preview->set_fit_converted(convert_source_image(fit_source));
    preview_info->setText(QObject::tr("%n color(s)", nullptr, static_cast<int>(palette.colors.size())));
  };
  QObject::connect(preview_timer, &QTimer::timeout, &dialog, refresh_preview);
  const auto schedule_preview = [preview_timer] { preview_timer->start(); };

  QObject::connect(source_combo, &QComboBox::activated, &dialog, [&, file_row](int row) {
    colors_spin->setEnabled(row == kSourceOptimized);
    if (row == file_row) {
      const auto path = QFileDialog::getOpenFileName(
          &dialog, QObject::tr("Load Palette"), QString(),
          QObject::tr("Palette Files (*.pal *.gpl *.hex *.act *.aco *.ase *.bmp);;All Files (*)"));
      if (!path.isEmpty()) {
        try {
          auto data = palette_io::read_palette_bytes([&path] {
            std::vector<std::uint8_t> bytes;
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
              const auto blob = file.readAll();
              bytes.assign(blob.begin(), blob.end());
            }
            return bytes;
          }());
          file_palette = Palette{std::move(data.colors)};
          file_name = QFileInfo(path).fileName();
        } catch (const std::exception& error) {
          QMessageBox::warning(&dialog, QObject::tr("Load Palette"),
                               QObject::tr("Could not load the palette file.\n%1")
                                   .arg(QString::fromUtf8(error.what())));
          file_palette.reset();
        }
      }
      source_combo->setItemText(file_row, file_palette.has_value()
                                              ? QObject::tr("From file: %1").arg(file_name)
                                              : QObject::tr("From file..."));
    }
    schedule_preview();
  });
  QObject::connect(colors_spin, &QSpinBox::valueChanged, &dialog, [&](int) { schedule_preview(); });
  QObject::connect(dither_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) { schedule_preview(); });
  QObject::connect(alpha_spin, &QSpinBox::valueChanged, &dialog, [&](int) { schedule_preview(); });

  colors_spin->setEnabled(true);
  // Keep readable - / + buttons on the spin boxes (sub-control gotcha: apply the
  // style after all children exist, with unprefixed selectors; see dialog_utils).
  dialog.setStyleSheet(dialog.styleSheet() + dialog_spinbox_button_style());
  refresh_preview();

  if (exec_dialog(dialog) != QDialog::Accepted) {
    return std::nullopt;
  }
  auto palette = resolve_palette();
  if (palette.colors.empty()) {
    return std::nullopt;
  }
  PaletteConvertSettings settings;
  settings.palette = std::move(palette);
  settings.dither = static_cast<PaletteDither>(dither_combo->currentData().toInt());
  settings.alpha_threshold = alpha_spin->value();
  return settings;
}

}  // namespace patchy::ui
