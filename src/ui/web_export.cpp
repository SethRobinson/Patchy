#include "ui/web_export.hpp"

#include "color/rgb_profiles.hpp"
#include "ui/action_icons.hpp"
#include "ui/activity_spinner.hpp"
#include "ui/app_settings.hpp"
#include "ui/background_workers.hpp"
#include "ui/color_panel.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_save_option_keys.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/measurement_units.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/unit_spin_box.hpp"
#include "ui/zoomable_image_preview.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <utility>

namespace patchy::ui {

namespace {

// Settings keys and their tokens are persisted identifiers (docs/web-export.md): never rename.
constexpr const char* kFormatKey = "exportForWeb/format";
constexpr const char* kJpegQualityKey = "exportForWeb/jpegQuality";
constexpr const char* kJpegProgressiveKey = "exportForWeb/jpegProgressive";
constexpr const char* kJpegOptimizeKey = "exportForWeb/jpegOptimize";
constexpr const char* kJpegBackgroundKey = "exportForWeb/jpegBackground";
constexpr const char* kWebpQualityKey = "exportForWeb/webpQuality";
constexpr const char* kWebpLosslessKey = "exportForWeb/webpLossless";
constexpr const char* kWebpAnimateKey = "exportForWeb/webpAnimate";
constexpr const char* kMetadataKey = "exportForWeb/metadata";
constexpr const char* kThumbnailKey = "exportForWeb/thumbnail";
constexpr const char* kResampleKey = "exportForWeb/resample";
constexpr const char* kTrimKey = "exportForWeb/trim";
constexpr const char* kConvertToSrgbKey = "exportForWeb/convertToSrgb";
constexpr const char* kCompareKey = "exportForWeb/compare";
constexpr const char* kRevealKey = "exportForWeb/revealWhenDone";

constexpr int kPreviewDebounceMs = 200;
constexpr int kMaxOutputPixels = 100000;
constexpr int kMaxPixelScale = 16;
// Finished encodes kept by signature, so switching back to a format or setting shows its
// result at once.
constexpr std::size_t kCachedResults = 12;

[[nodiscard]] QString resample_key(WebExportResample resample) {
  return resample == WebExportResample::PixelArt ? QStringLiteral("pixelArt") : QStringLiteral("smooth");
}

[[nodiscard]] QString compare_key(WebExportCompare compare) {
  switch (compare) {
    case WebExportCompare::SideBySide:
      return QStringLiteral("sideBySide");
    case WebExportCompare::ResultOnly:
      return QStringLiteral("result");
    case WebExportCompare::Split:
      break;
  }
  return QStringLiteral("split");
}

[[nodiscard]] WebExportCompare compare_from_key(const QString& key) {
  if (key == QStringLiteral("sideBySide")) {
    return WebExportCompare::SideBySide;
  }
  if (key == QStringLiteral("result")) {
    return WebExportCompare::ResultOnly;
  }
  return WebExportCompare::Split;
}

[[nodiscard]] QString geometry_signature(const ImageSaveOptions& geometry, bool convert_to_srgb) {
  return QStringLiteral("w%1h%2s%3t%4c%5")
      .arg(geometry.export_width)
      .arg(geometry.export_height)
      .arg(geometry.export_scale)
      .arg(geometry.export_trim_transparent ? 1 : 0)
      .arg(convert_to_srgb ? 1 : 0);
}

[[nodiscard]] QImage display_image(QImage image) {
  if (image.isNull()) {
    return image;
  }
  return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

[[nodiscard]] QString data_size_text(std::size_t bytes) {
  return QLocale().formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeTraditionalFormat);
}

}  // namespace

const std::vector<WebExportFormat>& web_export_formats() {
  static const std::vector<WebExportFormat> formats = {WebExportFormat::Jpeg, WebExportFormat::Webp,
                                                       WebExportFormat::Png};
  return formats;
}

QString web_export_format_key(WebExportFormat format) {
  switch (format) {
    case WebExportFormat::Webp:
      return QStringLiteral("webp");
    case WebExportFormat::Png:
      return QStringLiteral("png");
    case WebExportFormat::Jpeg:
      break;
  }
  return QStringLiteral("jpeg");
}

std::optional<WebExportFormat> web_export_format_from_key(const QString& key) {
  for (const auto format : web_export_formats()) {
    if (web_export_format_key(format) == key) {
      return format;
    }
  }
  return std::nullopt;
}

QString web_export_extension(WebExportFormat format) {
  switch (format) {
    case WebExportFormat::Webp:
      return QStringLiteral("webp");
    case WebExportFormat::Png:
      return QStringLiteral("png");
    case WebExportFormat::Jpeg:
      break;
  }
  return QStringLiteral("jpg");
}

QString web_export_format_name(WebExportFormat format) {
  switch (format) {
    case WebExportFormat::Webp:
      return QStringLiteral("WebP");
    case WebExportFormat::Png:
      return QStringLiteral("PNG");
    case WebExportFormat::Jpeg:
      break;
  }
  return QStringLiteral("JPEG");
}

WebExportSettings load_web_export_settings() {
  WebExportSettings settings;
  auto stored = app_settings();
  settings.format = web_export_format_from_key(stored.value(QLatin1String(kFormatKey)).toString())
                        .value_or(WebExportFormat::Jpeg);
  settings.jpeg_quality = std::clamp(stored.value(QLatin1String(kJpegQualityKey), 80).toInt(), 1, 100);
  settings.jpeg_progressive = stored.value(QLatin1String(kJpegProgressiveKey), true).toBool();
  settings.jpeg_optimize = stored.value(QLatin1String(kJpegOptimizeKey), true).toBool();
  const QColor background(stored.value(QLatin1String(kJpegBackgroundKey)).toString());
  settings.jpeg_background = background.isValid() ? background : QColor(Qt::white);
  settings.webp_quality = std::clamp(stored.value(QLatin1String(kWebpQualityKey), 75).toInt(), 0, 100);
  settings.webp_lossless = stored.value(QLatin1String(kWebpLosslessKey), false).toBool();
  settings.webp_animate = stored.value(QLatin1String(kWebpAnimateKey), false).toBool();
  settings.metadata = metadata_policy_from_key(stored.value(QLatin1String(kMetadataKey)).toString())
                          .value_or(metadata::MetadataPolicy::None);
  settings.thumbnail = stored.value(QLatin1String(kThumbnailKey), false).toBool();
  settings.resample = stored.value(QLatin1String(kResampleKey)).toString() == QStringLiteral("pixelArt")
                          ? WebExportResample::PixelArt
                          : WebExportResample::Smooth;
  settings.trim = stored.value(QLatin1String(kTrimKey), false).toBool();
  settings.convert_to_srgb = stored.value(QLatin1String(kConvertToSrgbKey), true).toBool();
  settings.compare = compare_from_key(stored.value(QLatin1String(kCompareKey)).toString());
  settings.reveal_when_done = stored.value(QLatin1String(kRevealKey), false).toBool();
  return settings;
}

void save_web_export_settings(const WebExportSettings& settings) {
  auto stored = app_settings();
  stored.setValue(QLatin1String(kFormatKey), web_export_format_key(settings.format));
  stored.setValue(QLatin1String(kJpegQualityKey), settings.jpeg_quality);
  stored.setValue(QLatin1String(kJpegProgressiveKey), settings.jpeg_progressive);
  stored.setValue(QLatin1String(kJpegOptimizeKey), settings.jpeg_optimize);
  stored.setValue(QLatin1String(kJpegBackgroundKey), settings.jpeg_background.name(QColor::HexRgb));
  stored.setValue(QLatin1String(kWebpQualityKey), settings.webp_quality);
  stored.setValue(QLatin1String(kWebpLosslessKey), settings.webp_lossless);
  stored.setValue(QLatin1String(kWebpAnimateKey), settings.webp_animate);
  stored.setValue(QLatin1String(kMetadataKey), metadata_policy_key(settings.metadata));
  stored.setValue(QLatin1String(kThumbnailKey), settings.thumbnail);
  stored.setValue(QLatin1String(kResampleKey), resample_key(settings.resample));
  stored.setValue(QLatin1String(kTrimKey), settings.trim);
  stored.setValue(QLatin1String(kConvertToSrgbKey), settings.convert_to_srgb);
  stored.setValue(QLatin1String(kCompareKey), compare_key(settings.compare));
  stored.setValue(QLatin1String(kRevealKey), settings.reveal_when_done);
}

ImageSaveOptions web_export_geometry_options(const WebExportSettings& settings, QSize document_size) {
  ImageSaveOptions options;
  options.export_trim_transparent = settings.trim;
  if (settings.resample == WebExportResample::PixelArt) {
    options.export_scale = std::clamp(settings.pixel_scale, 1, kMaxPixelScale);
  } else if (settings.width > 0 && settings.height > 0 &&
             (settings.width != document_size.width() || settings.height != document_size.height())) {
    options.export_width = settings.width;
    options.export_height = settings.height;
  }
  return options;
}

ImageSaveOptions web_export_image_options(const WebExportSettings& settings, WebExportFormat format,
                                          QSize document_size) {
  auto options = web_export_geometry_options(settings, document_size);
  options.metadata_policy = settings.metadata;
  options.export_reveal_in_file_explorer = settings.reveal_when_done;
  switch (format) {
    case WebExportFormat::Jpeg:
      options.jpeg_quality = std::clamp(settings.jpeg_quality, 1, 100);
      options.jpeg_progressive = settings.jpeg_progressive;
      options.jpeg_optimize = settings.jpeg_optimize;
      options.jpeg_exif_thumbnail = settings.thumbnail;
      // JPEG has no alpha: transparent areas take the background color.
      options.export_fill_transparent = true;
      options.export_background_color = settings.jpeg_background;
      break;
    case WebExportFormat::Webp:
      options.webp_quality = std::clamp(settings.webp_quality, 0, 100);
      options.webp_lossless = settings.webp_lossless;
      options.webp_animate = settings.webp_animate;
      break;
    case WebExportFormat::Png:
      break;
  }
  return options;
}

QString web_export_format_summary(const WebExportSettings& settings, WebExportFormat format) {
  switch (format) {
    case WebExportFormat::Jpeg:
      return settings.jpeg_progressive ? QObject::tr("Quality %1, progressive").arg(settings.jpeg_quality)
                                       : QObject::tr("Quality %1").arg(settings.jpeg_quality);
    case WebExportFormat::Webp: {
      const auto base = settings.webp_lossless ? QObject::tr("Lossless")
                                               : QObject::tr("Quality %1").arg(settings.webp_quality);
      return settings.webp_animate ? QObject::tr("%1, animated").arg(base) : base;
    }
    case WebExportFormat::Png:
      break;
  }
  return QObject::tr("Full color");
}

WebExportJob web_export_job(const WebExportSettings& settings, WebExportFormat format, QSize document_size) {
  WebExportJob job;
  job.format = format;
  job.geometry = web_export_geometry_options(settings, document_size);
  job.options = web_export_image_options(settings, format, document_size);
  job.convert_to_srgb = settings.convert_to_srgb;
  job.animate = format == WebExportFormat::Webp && settings.webp_animate;
  auto signature = web_export_format_key(format) + QLatin1Char('|') +
                   geometry_signature(job.geometry, job.convert_to_srgb) + QLatin1Char('|') +
                   metadata_policy_key(settings.metadata);
  switch (format) {
    case WebExportFormat::Jpeg:
      signature += QStringLiteral("|q%1p%2o%3t%4b%5")
                       .arg(job.options.jpeg_quality)
                       .arg(job.options.jpeg_progressive ? 1 : 0)
                       .arg(job.options.jpeg_optimize ? 1 : 0)
                       .arg(job.options.jpeg_exif_thumbnail ? 1 : 0)
                       .arg(settings.jpeg_background.name(QColor::HexRgb));
      break;
    case WebExportFormat::Webp:
      signature += QStringLiteral("|q%1l%2a%3")
                       .arg(job.options.webp_quality)
                       .arg(job.options.webp_lossless ? 1 : 0)
                       .arg(job.animate ? 1 : 0);
      break;
    case WebExportFormat::Png:
      break;
  }
  job.signature = signature;
  return job;
}

bool web_export_has_non_srgb_profile(const Document& document) {
  return rgb_profile_needs_conversion(document.color_state().embedded_icc_profile);
}

// ---------------------------------------------------------------------------------------
// Preview engine

struct WebExportPreviewEngine::State {
  std::shared_ptr<const Document> source;
  // Worker side: only the one running job touches these (the mutex makes that explicit).
  std::mutex worker_mutex;
  std::shared_ptr<const Document> narrowed;
  QString prepared_key;
  std::shared_ptr<const Document> prepared;
  QImage prepared_image;
  std::vector<std::string> prepared_notices;
  // UI thread.
  std::deque<WebExportJob> queue;
  std::map<QString, WebExportResult> results;
  std::deque<QString> result_order;
  bool in_flight{false};
  bool closed{false};
  std::function<void(const WebExportJob&, const WebExportResult&)> callback;
  std::atomic<std::uint64_t> generation{0};
  std::atomic<int> encodes{0};
};

namespace {

void start_next_web_export_job(const std::shared_ptr<WebExportPreviewEngine::State>& state);

void store_web_export_result(WebExportPreviewEngine::State& state, const QString& signature,
                             const WebExportResult& result) {
  if (state.results.find(signature) == state.results.end()) {
    state.result_order.push_back(signature);
  }
  state.results[signature] = result;
  while (state.result_order.size() > kCachedResults) {
    state.results.erase(state.result_order.front());
    state.result_order.pop_front();
  }
}

void start_next_web_export_job(const std::shared_ptr<WebExportPreviewEngine::State>& state) {
  if (state->closed || state->in_flight) {
    return;
  }
  while (!state->queue.empty() && state->results.find(state->queue.front().signature) != state->results.end()) {
    state->queue.pop_front();
  }
  if (state->queue.empty()) {
    return;
  }
  auto job = std::move(state->queue.front());
  state->queue.pop_front();
  state->in_flight = true;
  const auto generation = state->generation.load(std::memory_order_acquire);
  auto* app = QCoreApplication::instance();
  run_tracked_background_worker([state, job = std::move(job), generation, app]() mutable {
    std::optional<WebExportResult> result;
    // A job whose request was replaced before it started is dropped unrun.
    if (state->generation.load(std::memory_order_acquire) == generation) {
      state->encodes.fetch_add(1, std::memory_order_acq_rel);
      const std::lock_guard lock(state->worker_mutex);
      WebExportResult encoded;
      try {
        if (state->narrowed == nullptr) {
          std::optional<Document> narrowed;
          const auto& eight_bit = document_for_8bit_writer(*state->source, narrowed);
          state->narrowed = narrowed.has_value() ? std::make_shared<const Document>(std::move(*narrowed))
                                                 : std::shared_ptr<const Document>(state->source, &eight_bit);
        }
        const auto key = geometry_signature(job.geometry, job.convert_to_srgb);
        if (state->prepared == nullptr || state->prepared_key != key) {
          state->prepared_notices.clear();
          state->prepared = WebExportPreviewEngine::prepare(*state->narrowed, job.geometry, job.convert_to_srgb,
                                                            &state->prepared_notices);
          state->prepared_image = display_image(flat_export_qimage(*state->prepared, true));
          state->prepared_key = key;
        }
        encoded = WebExportPreviewEngine::encode(*state->narrowed, job, state->prepared);
        encoded.original = state->prepared_image;
        encoded.notices.insert(encoded.notices.begin(), state->prepared_notices.begin(),
                               state->prepared_notices.end());
      } catch (const std::exception& error) {
        encoded.error = QString::fromUtf8(error.what());
        encoded.original = state->prepared_image;
      }
      result = std::move(encoded);
    }
    if (app == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(
        app,
        [state, job = std::move(job), result = std::move(result)]() mutable {
          state->in_flight = false;
          if (state->closed) {
            return;
          }
          if (result.has_value()) {
            if (result->error.isEmpty()) {
              store_web_export_result(*state, job.signature, *result);
            }
            if (state->callback) {
              state->callback(job, *result);
            }
          }
          start_next_web_export_job(state);
        },
        Qt::QueuedConnection);
  });
}

}  // namespace

WebExportPreviewEngine::WebExportPreviewEngine(std::shared_ptr<const Document> source)
    : state_(std::make_shared<State>()) {
  state_->source = std::move(source);
}

WebExportPreviewEngine::~WebExportPreviewEngine() { close(); }

void WebExportPreviewEngine::set_result_callback(
    std::function<void(const WebExportJob&, const WebExportResult&)> callback) {
  state_->callback = std::move(callback);
}

void WebExportPreviewEngine::request(std::vector<WebExportJob> jobs) {
  if (state_->closed) {
    return;
  }
  state_->generation.fetch_add(1, std::memory_order_acq_rel);
  state_->queue.assign(std::make_move_iterator(jobs.begin()), std::make_move_iterator(jobs.end()));
  start_next();
}

void WebExportPreviewEngine::start_next() { start_next_web_export_job(state_); }

std::optional<WebExportResult> WebExportPreviewEngine::cached(const QString& signature) const {
  const auto found = state_->results.find(signature);
  if (found == state_->results.end()) {
    return std::nullopt;
  }
  return found->second;
}

bool WebExportPreviewEngine::busy() const { return state_->in_flight || !state_->queue.empty(); }

std::uint64_t WebExportPreviewEngine::generation() const {
  return state_->generation.load(std::memory_order_acquire);
}

int WebExportPreviewEngine::encodes_started() const { return state_->encodes.load(std::memory_order_acquire); }

void WebExportPreviewEngine::close() {
  state_->closed = true;
  state_->generation.fetch_add(1, std::memory_order_acq_rel);
  state_->queue.clear();
  state_->callback = {};
}

std::shared_ptr<const Document> WebExportPreviewEngine::prepare(const Document& narrowed_source,
                                                                const ImageSaveOptions& geometry,
                                                                bool convert_to_srgb,
                                                                std::vector<std::string>* notices) {
  auto prepared = std::make_shared<Document>(export_transformed_document(narrowed_source, geometry, notices));
  if (convert_to_srgb && rgb_profile_needs_conversion(prepared->color_state().embedded_icc_profile)) {
    // The stand-in is one pixel layer, so every pixel converts (no stored colors stay).
    (void)convert_document_rgb_profile(*prepared, {}, RenderingIntent::RelativeColorimetric, true);
  }
  return prepared;
}

WebExportResult WebExportPreviewEngine::encode(const Document& narrowed_source, const WebExportJob& job,
                                               const std::shared_ptr<const Document>& prepared) {
  WebExportResult result;
  const auto extension = web_export_extension(job.format);
  std::vector<std::uint8_t> bytes;
  if (job.animate) {
    // The animated writers read the layers: the whole document with every option.
    if (job.convert_to_srgb && rgb_profile_needs_conversion(narrowed_source.color_state().embedded_icc_profile)) {
      Document converted = narrowed_source;
      (void)convert_document_rgb_profile(converted, {}, RenderingIntent::RelativeColorimetric, true);
      bytes = encode_flat_image_bytes(converted, extension, job.options, &result.notices);
    } else {
      bytes = encode_flat_image_bytes(narrowed_source, extension, job.options, &result.notices);
    }
  } else {
    const Document& source = prepared != nullptr ? *prepared : narrowed_source;
    auto options = prepared != nullptr ? without_export_transforms(job.options) : job.options;
    // The fill is the one transform applied to the prepared image (JPEG's background).
    options.export_fill_transparent = job.options.export_fill_transparent;
    options.export_background_color = job.options.export_background_color;
    bytes = encode_flat_image_bytes(source, extension, options, &result.notices);
  }
  const auto format = extension.toLatin1();
  result.decoded = display_image(QImage::fromData(
      QByteArrayView(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())),
      format.constData()));
  result.bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
  return result;
}

// ---------------------------------------------------------------------------------------
// Preview panes

namespace {

// A smooth reduction of an image for drawing below 100%, built on a worker once per size,
// so paintEvent only ever draws a cached image (or the full one, scaled by the painter,
// until the reduction lands).
class ScaledImageCache {
public:
  explicit ScaledImageCache(QWidget* owner) : owner_(owner), state_(std::make_shared<State>()) {}

  void set_source(QImage image) {
    if (image.cacheKey() == source_.cacheKey()) {
      return;
    }
    source_ = std::move(image);
    scaled_ = {};
    scaled_key_ = 0;
  }
  [[nodiscard]] const QImage& source() const noexcept { return source_; }

  [[nodiscard]] const QImage& best_for(QSize device_size) {
    if (source_.isNull() || device_size.isEmpty() || device_size.width() >= source_.width()) {
      return source_;
    }
    if (scaled_key_ == source_.cacheKey() && scaled_.size() == device_size) {
      return scaled_;
    }
    request(device_size);
    return source_;
  }

private:
  struct State {
    std::uint64_t generation{0};
    bool in_flight{false};
    qint64 requested_key{0};
    QSize requested_size;
    std::optional<std::pair<QImage, QSize>> pending;
  };

  void request(QSize size) {
    if (state_->requested_key == source_.cacheKey() && state_->requested_size == size) {
      return;
    }
    state_->requested_key = source_.cacheKey();
    state_->requested_size = size;
    state_->pending = std::make_pair(source_, size);
    ++state_->generation;
    if (!state_->in_flight) {
      start();
    }
  }

  void start() {
    if (!state_->pending.has_value()) {
      return;
    }
    auto [image, size] = std::move(*state_->pending);
    state_->pending.reset();
    state_->in_flight = true;
    const auto generation = state_->generation;
    auto state = state_;
    auto* app = QCoreApplication::instance();
    const QPointer<QWidget> owner(owner_);
    run_tracked_background_worker([state, image = std::move(image), size, generation, app, owner, this]() {
      auto scaled = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      if (app == nullptr) {
        return;
      }
      QMetaObject::invokeMethod(
          app,
          [state, key = image.cacheKey(), scaled = std::move(scaled), generation, owner, this]() mutable {
            state->in_flight = false;
            if (owner == nullptr) {
              return;
            }
            // The owner is alive, so this cache (its member) is too.
            if (generation == state->generation) {
              scaled_ = std::move(scaled);
              scaled_key_ = key;
              owner->update();
            }
            start();
          },
          Qt::QueuedConnection);
    });
  }

  QWidget* owner_;
  std::shared_ptr<State> state_;
  QImage source_;
  QImage scaled_;
  qint64 scaled_key_{0};
};

// One preview viewport (ZoomableImagePreview's zoom and pan): the original, the encoded
// result, or both split by a draggable divider (original left, result right). The divider
// sits at a fraction of the widget's width, so panning moves the image under it.
class WebExportPane final : public ZoomableImagePreview {
public:
  enum class Content { Split, Original, Result };

  explicit WebExportPane(QWidget* parent) : ZoomableImagePreview(parent), original_(this), result_(this) {
    setMinimumSize(300, 260);
    setFocusPolicy(Qt::NoFocus);
  }

  void set_images(QImage original, QImage result) {
    const auto size = !original.isNull() ? original.size() : result.size();
    original_.set_source(std::move(original));
    result_.set_source(std::move(result));
    if (!size.isEmpty()) {
      set_logical_size(size);
    }
    update();
  }
  void set_content(Content content) {
    content_ = content;
    update();
  }
  [[nodiscard]] Content content() const noexcept { return content_; }
  void set_labels(QString original_label, QString result_label) {
    original_label_ = std::move(original_label);
    result_label_ = std::move(result_label);
    update();
  }
  [[nodiscard]] double split() const noexcept { return split_; }
  void set_split(double split) {
    split_ = std::clamp(split, 0.0, 1.0);
    setProperty("webExportSplit", split_);
    update();
  }
  [[nodiscard]] bool has_result() const { return !result_.source().isNull(); }

protected:
  void paint_backdrop(QPainter& painter) override { painter.fillRect(rect(), theme().canvas_backdrop); }

  void paint_content(QPainter& painter, const QRectF& image_rect) override {
    const auto visible = image_rect.intersected(QRectF(rect()));
    if (visible.isEmpty()) {
      return;
    }
    // The transparency checkerboard depicts alpha (deliberately not a theme role,
    // docs/ui-conventions.md).
    painter.save();
    painter.setClipRect(visible);
    painter.fillRect(visible, QColor(200, 200, 200));
    constexpr int kCheck = 8;
    const auto x0 = static_cast<int>(std::floor(image_rect.left()));
    const auto y0 = static_cast<int>(std::floor(image_rect.top()));
    const auto first_row = std::max(0, static_cast<int>(std::floor((visible.top() - y0) / kCheck)));
    const auto first_column = std::max(0, static_cast<int>(std::floor((visible.left() - x0) / kCheck)));
    for (int row = first_row; y0 + row * kCheck < visible.bottom(); ++row) {
      for (int column = first_column + ((first_column + row) % 2 == 0 ? 0 : 1); x0 + column * kCheck < visible.right();
           column += 2) {
        painter.fillRect(QRect(x0 + column * kCheck, y0 + row * kCheck, kCheck, kCheck), QColor(240, 240, 240));
      }
    }
    painter.restore();

    const auto device = (image_rect.size() * devicePixelRatioF()).toSize();
    const auto draw = [&](ScaledImageCache& cache, const QRectF& clip) {
      if (cache.source().isNull() || clip.isEmpty()) {
        return;
      }
      const auto& image = cache.best_for(device);
      painter.save();
      painter.setClipRect(clip);
      painter.setRenderHint(QPainter::SmoothPixmapTransform, zoom() < 1.0);
      painter.drawImage(image_rect, image);
      painter.restore();
    };
    const auto divider_x = width() * split_;
    switch (content_) {
      case Content::Original:
        draw(original_, visible);
        break;
      case Content::Result:
        draw(result_.source().isNull() ? original_ : result_, visible);
        break;
      case Content::Split: {
        const QRectF left(visible.left(), visible.top(), std::max(0.0, divider_x - visible.left()), visible.height());
        const QRectF right(std::max(divider_x, visible.left()), visible.top(),
                           std::max(0.0, visible.right() - std::max(divider_x, visible.left())), visible.height());
        draw(original_, left.intersected(visible));
        draw(result_.source().isNull() ? original_ : result_, right.intersected(visible));
        break;
      }
    }
    painter.setPen(theme().canvas_document_border);
    painter.drawRect(image_rect.adjusted(-1.0, -1.0, 0.0, 0.0));
  }

  void paintEvent(QPaintEvent* event) override {
    ZoomableImagePreview::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (content_ == Content::Split && !logical_size().isEmpty()) {
      // The divider reads over any artwork: a light line with a dark edge, the text
      // resize handles' pair.
      const auto x = std::round(width() * split_) + 0.5;
      painter.setPen(QPen(theme().resize_handle_border, 3.0));
      painter.drawLine(QPointF(x, 0.0), QPointF(x, height()));
      painter.setPen(QPen(theme().resize_handle_bg, 1.0));
      painter.drawLine(QPointF(x, 0.0), QPointF(x, height()));
      const QPointF center(x, height() / 2.0);
      painter.setPen(QPen(theme().resize_handle_border, 1.0));
      painter.setBrush(theme().resize_handle_bg);
      painter.drawEllipse(center, 11.0, 11.0);
      painter.setPen(QPen(theme().resize_handle_border, 1.5));
      painter.drawLine(center + QPointF(-3.0, -4.0), center + QPointF(-6.0, 0.0));
      painter.drawLine(center + QPointF(-6.0, 0.0), center + QPointF(-3.0, 4.0));
      painter.drawLine(center + QPointF(3.0, -4.0), center + QPointF(6.0, 0.0));
      painter.drawLine(center + QPointF(6.0, 0.0), center + QPointF(3.0, 4.0));
    }
    const auto draw_tag = [&](const QString& text, bool right_side) {
      if (text.isEmpty()) {
        return;
      }
      const auto metrics = painter.fontMetrics();
      const QSize size(metrics.horizontalAdvance(text) + 16, metrics.height() + 6);
      const QPoint top_left(right_side ? width() - size.width() - 8 : 8, 8);
      const QRect box(top_left, size);
      auto fill = theme().status_field_bg;
      fill.setAlpha(225);
      painter.setPen(theme().status_field_border);
      painter.setBrush(fill);
      painter.drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 2.0, 2.0);
      painter.setPen(theme().status_text);
      painter.drawText(box, Qt::AlignCenter, text);
    };
    switch (content_) {
      case Content::Split:
        draw_tag(original_label_, false);
        draw_tag(result_label_, true);
        break;
      case Content::Original:
        draw_tag(original_label_, false);
        break;
      case Content::Result:
        draw_tag(result_label_, false);
        break;
    }
  }

  [[nodiscard]] QCursor interaction_cursor(bool is_panning) const override {
    return is_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
  }

  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && near_divider(event->position())) {
      dragging_divider_ = true;
      event->accept();
      return;
    }
    ZoomableImagePreview::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (dragging_divider_) {
      set_split(width() > 0 ? event->position().x() / width() : 0.5);
      event->accept();
      return;
    }
    ZoomableImagePreview::mouseMoveEvent(event);
    if (!panning() && near_divider(event->position())) {
      setCursor(Qt::SplitHCursor);
    }
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (dragging_divider_ && event->button() == Qt::LeftButton) {
      dragging_divider_ = false;
      setCursor(interaction_cursor(false));
      event->accept();
      return;
    }
    ZoomableImagePreview::mouseReleaseEvent(event);
  }

private:
  [[nodiscard]] bool near_divider(QPointF position) const {
    return content_ == Content::Split && !logical_size().isEmpty() &&
           std::abs(position.x() - width() * split_) <= 8.0;
  }

  ScaledImageCache original_;
  ScaledImageCache result_;
  Content content_{Content::Split};
  QString original_label_;
  QString result_label_;
  double split_{0.5};
  bool dragging_divider_{false};
};

ThemedQss background_swatch_style(QColor color) {
  // The swatch shows the user's chosen color (user data); its chrome stays on theme roles.
  return ThemedQss(QStringLiteral("QPushButton#webExportBackgroundSwatch { background: rgb(%1, %2, %3); "
                                  "border: 1px solid @dlg_neutral_border; border-radius: 3px; padding: 0; "
                                  "min-width: 44px; max-width: 44px; min-height: 22px; max-height: 22px; } "
                                  "QPushButton#webExportBackgroundSwatch:hover { border-color: "
                                  "@dlg_neutral_border_bright; }")
                       .arg(color.red())
                       .arg(color.green())
                       .arg(color.blue()));
}

[[nodiscard]] QGroupBox* make_group(QWidget* parent, const QString& title, const char* object_name) {
  auto* group = new QGroupBox(title, parent);
  group->setObjectName(QLatin1String(object_name));
  return group;
}

[[nodiscard]] QFormLayout* make_form(QGroupBox* group) {
  auto* form = new QFormLayout(group);
  form->setContentsMargins(10, 10, 10, 8);
  form->setHorizontalSpacing(8);
  form->setVerticalSpacing(6);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  return form;
}

}  // namespace

// ---------------------------------------------------------------------------------------
// The dialog

std::optional<WebExportDialogResult> request_web_export(QWidget* parent, std::shared_ptr<const Document> document) {
  if (document == nullptr || document->width() <= 0 || document->height() <= 0) {
    return std::nullopt;
  }
  const QSize document_size(document->width(), document->height());
  const auto& layers = document->layers();
  const bool offer_animation =
      layers.size() >= 2 && std::any_of(layers.begin(), layers.end(), [](const Layer& layer) { return layer.visible(); });
  const bool non_srgb = web_export_has_non_srgb_profile(*document);

  auto settings = load_web_export_settings();
  settings.width = document_size.width();
  settings.height = document_size.height();
  settings.pixel_scale = 1;
  if (!offer_animation) {
    settings.webp_animate = false;
  }

  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("webExportDialog"));
  auto* content = install_dark_dialog_chrome(dialog, new QVBoxLayout(&dialog), QObject::tr("Export for Web"));
  auto* body = new QHBoxLayout();
  body->setSpacing(12);
  content->addLayout(body, 1);

  // --- left column: formats and settings ---
  auto* controls_widget = new QWidget(&dialog);
  controls_widget->setObjectName(QStringLiteral("webExportControls"));
  controls_widget->setFixedWidth(392);
  auto* controls = new QVBoxLayout(controls_widget);
  controls->setContentsMargins(0, 0, 0, 0);
  controls->setSpacing(8);
  body->addWidget(controls_widget);

  controls->addWidget(new QLabel(QObject::tr("Format"), &dialog));
  auto* format_list = new QTreeWidget(&dialog);
  format_list->setObjectName(QStringLiteral("webExportFormatList"));
  format_list->setColumnCount(3);
  format_list->setHeaderHidden(true);
  format_list->setRootIsDecorated(false);
  format_list->setItemsExpandable(false);
  format_list->setUniformRowHeights(true);
  format_list->setSelectionMode(QAbstractItemView::SingleSelection);
  format_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  format_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  format_list->header()->setStretchLastSection(false);
  format_list->header()->setSectionResizeMode(0, QHeaderView::Fixed);
  format_list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  format_list->header()->setSectionResizeMode(2, QHeaderView::Fixed);
  format_list->header()->resizeSection(0, 70);
  format_list->header()->resizeSection(2, 104);
  for (const auto format : web_export_formats()) {
    auto* item = new QTreeWidgetItem(format_list);
    item->setText(0, web_export_format_name(format));
    item->setData(0, Qt::UserRole, web_export_format_key(format));
    item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
    item->setSizeHint(0, QSize(0, 26));
    auto bold = item->font(0);
    bold.setBold(true);
    item->setFont(0, bold);
  }
  format_list->setFixedHeight(format_list->sizeHintForRow(0) * static_cast<int>(web_export_formats().size()) +
                              2 * format_list->frameWidth() + 2);
  controls->addWidget(format_list);
  auto* format_hint = new QLabel(
      QObject::tr("Sizes are the real encoded files at each format's own settings and update as you change them."),
      &dialog);
  format_hint->setObjectName(QStringLiteral("webExportFormatHint"));
  format_hint->setWordWrap(true);
  set_themed_style(*format_hint, QStringLiteral("color: @hint_text;"));
  controls->addWidget(format_hint);

  // JPEG settings.
  auto* jpeg_group = make_group(&dialog, QObject::tr("JPEG settings"), "webExportJpegGroup");
  auto* jpeg_form = make_form(jpeg_group);
  auto* jpeg_quality = add_dialog_slider_spin_row(jpeg_form, jpeg_group, QObject::tr("Quality:"),
                                                  QStringLiteral("webExportJpegQualitySlider"),
                                                  QStringLiteral("webExportJpegQualitySpin"), 1, 100,
                                                  settings.jpeg_quality);
  auto* jpeg_progressive =
      new QCheckBox(QObject::tr("Progressive (shows a rough image first while loading)"), jpeg_group);
  jpeg_progressive->setObjectName(QStringLiteral("webExportJpegProgressiveCheck"));
  jpeg_form->addRow(jpeg_progressive);
  auto* jpeg_optimize = new QCheckBox(QObject::tr("Optimize coding (smaller file, same image)"), jpeg_group);
  jpeg_optimize->setObjectName(QStringLiteral("webExportJpegOptimizeCheck"));
  jpeg_form->addRow(jpeg_optimize);
  auto* background_row = new QWidget(jpeg_group);
  auto* background_layout = new QHBoxLayout(background_row);
  background_layout->setContentsMargins(0, 0, 0, 0);
  background_layout->setSpacing(8);
  auto* background_swatch = new QPushButton(background_row);
  background_swatch->setObjectName(QStringLiteral("webExportBackgroundSwatch"));
  background_swatch->setAccessibleName(QObject::tr("Background color"));
  background_swatch->setToolTip(QObject::tr("Choose background color"));
  background_swatch->setCursor(Qt::PointingHandCursor);
  background_layout->addWidget(background_swatch);
  auto* background_hint = new QLabel(QObject::tr("Fills transparent areas (JPEG has no transparency)"), background_row);
  background_hint->setWordWrap(true);
  set_themed_style(*background_hint, QStringLiteral("color: @hint_text;"));
  background_layout->addWidget(background_hint, 1);
  jpeg_form->addRow(QObject::tr("Background:"), background_row);
  controls->addWidget(jpeg_group);

  // WebP settings.
  auto* webp_group = make_group(&dialog, QObject::tr("WebP settings"), "webExportWebpGroup");
  auto* webp_form = make_form(webp_group);
  auto* webp_quality = add_dialog_slider_spin_row(webp_form, webp_group, QObject::tr("Quality:"),
                                                  QStringLiteral("webExportWebpQualitySlider"),
                                                  QStringLiteral("webExportWebpQualitySpin"), 0, 100,
                                                  settings.webp_quality);
  auto* webp_lossless = new QCheckBox(QObject::tr("Lossless (larger file)"), webp_group);
  webp_lossless->setObjectName(QStringLiteral("webExportWebpLosslessCheck"));
  webp_form->addRow(webp_lossless);
  auto* webp_animate = new QCheckBox(QObject::tr("Animation from visible layers"), webp_group);
  webp_animate->setObjectName(QStringLiteral("webExportWebpAnimateCheck"));
  webp_animate->setToolTip(QObject::tr("Each visible top-level layer becomes one frame, with the top layer first."));
  webp_animate->setVisible(offer_animation);
  webp_form->addRow(webp_animate);
  controls->addWidget(webp_group);

  // PNG settings.
  auto* png_group = make_group(&dialog, QObject::tr("PNG settings"), "webExportPngGroup");
  auto* png_form = make_form(png_group);
  auto* png_note = new QLabel(QObject::tr("Full color, lossless. Transparency is kept."), png_group);
  png_note->setObjectName(QStringLiteral("webExportPngNote"));
  png_note->setWordWrap(true);
  png_form->addRow(png_note);
  controls->addWidget(png_group);

  // Image size.
  auto* size_group = make_group(&dialog, QObject::tr("Image size"), "webExportSizeGroup");
  auto* size_grid = new QGridLayout(size_group);
  size_grid->setContentsMargins(10, 10, 10, 8);
  size_grid->setHorizontalSpacing(8);
  size_grid->setVerticalSpacing(6);
  const auto make_pixels_spin = [&](const char* object_name, int basis) {
    auto* spin = new UnitIntSpinBox(SpinUnit::Pixels, size_group);
    spin->set_context_provider([basis] { return UnitConversionContext{72.0, static_cast<double>(basis)}; });
    spin->setObjectName(QLatin1String(object_name));
    spin->setRange(1, kMaxOutputPixels);
    configure_dialog_spinbox(spin, 96);
    return spin;
  };
  // Each spin goes in right after its label (insertion order pairs the scrub handle).
  size_grid->addWidget(new QLabel(QObject::tr("Width:"), size_group), 0, 0);
  auto* width_spin = make_pixels_spin("webExportWidthSpin", document_size.width());
  size_grid->addWidget(width_spin, 0, 1);
  auto* link = new QToolButton(size_group);
  link->setObjectName(QStringLiteral("webExportLinkButton"));
  link->setIcon(simple_icon(QStringLiteral("link")));
  link->setIconSize(QSize(18, 18));
  link->setCheckable(true);
  link->setChecked(true);
  link->setAutoRaise(true);
  link->setToolTip(QObject::tr("Constrain proportions"));
  size_grid->addWidget(link, 0, 2, 2, 1, Qt::AlignCenter);
  size_grid->addWidget(new QLabel(QObject::tr("Height:"), size_group), 1, 0);
  auto* height_spin = make_pixels_spin("webExportHeightSpin", document_size.height());
  size_grid->addWidget(height_spin, 1, 1);
  size_grid->addWidget(new QLabel(QObject::tr("Scale:"), size_group), 2, 0);
  auto* scale_spin = new QDoubleSpinBox(size_group);
  scale_spin->setObjectName(QStringLiteral("webExportScaleSpin"));
  scale_spin->setSuffix(percent_suffix());
  configure_dialog_spinbox(scale_spin, 96);
  size_grid->addWidget(scale_spin, 2, 1);
  auto* original_size = new QLabel(
      QObject::tr("of %1 x %2").arg(document_size.width()).arg(document_size.height()), size_group);
  original_size->setObjectName(QStringLiteral("webExportOriginalSizeLabel"));
  set_themed_style(*original_size, QStringLiteral("color: @hint_text;"));
  size_grid->addWidget(original_size, 2, 2, 1, 2);
  size_grid->addWidget(new QLabel(QObject::tr("Resample:"), size_group), 3, 0);
  auto* resample_combo = new QComboBox(size_group);
  resample_combo->setObjectName(QStringLiteral("webExportResampleCombo"));
  resample_combo->addItem(QObject::tr("Smooth"), resample_key(WebExportResample::Smooth));
  resample_combo->addItem(QObject::tr("Pixel art (whole pixels)"), resample_key(WebExportResample::PixelArt));
  resample_combo->setToolTip(QObject::tr("Pixel art enlarges by whole pixels with no smoothing, so sprites stay crisp."));
  size_grid->addWidget(resample_combo, 3, 1, 1, 3);
  auto* trim_check = new QCheckBox(QObject::tr("Trim transparent edges"), size_group);
  trim_check->setObjectName(QStringLiteral("webExportTrimCheck"));
  size_grid->addWidget(trim_check, 4, 0, 1, 4);
  size_grid->setColumnStretch(3, 1);
  controls->addWidget(size_group);

  // Metadata and color.
  auto* metadata_group = make_group(&dialog, QObject::tr("Metadata and color"), "webExportMetadataGroup");
  auto* metadata_form = make_form(metadata_group);
  auto* metadata_combo = new QComboBox(metadata_group);
  metadata_combo->setObjectName(QStringLiteral("webExportMetadataCombo"));
  metadata_combo->addItem(QObject::tr("None"), metadata_policy_key(metadata::MetadataPolicy::None));
  metadata_combo->addItem(QObject::tr("Copyright and Contact Info"),
                          metadata_policy_key(metadata::MetadataPolicy::CopyrightAndContact));
  metadata_combo->addItem(QObject::tr("All"), metadata_policy_key(metadata::MetadataPolicy::All));
  metadata_combo->setToolTip(QObject::tr("File Info metadata to include. None writes the smallest file."));
  metadata_form->addRow(QObject::tr("Metadata:"), metadata_combo);
  auto* thumbnail_check = new QCheckBox(QObject::tr("Include thumbnail (about 5 to 10 KB)"), metadata_group);
  thumbnail_check->setObjectName(QStringLiteral("webExportThumbnailCheck"));
  thumbnail_check->setToolTip(
      QObject::tr("Adds a small preview image to the EXIF data. Written only when the file keeps EXIF metadata."));
  metadata_form->addRow(thumbnail_check);
  QCheckBox* convert_check = nullptr;
  if (non_srgb) {
    const auto profile_name =
        QString::fromStdString(document_rgb_profile_name(document->color_state().embedded_icc_profile));
    convert_check = new QCheckBox(QObject::tr("Convert to sRGB (from %1)").arg(profile_name), metadata_group);
    convert_check->setObjectName(QStringLiteral("webExportConvertToSrgbCheck"));
    convert_check->setToolTip(
        QObject::tr("Browsers show untagged images as sRGB. Unchecked, the file keeps its color profile."));
    metadata_form->addRow(QObject::tr("Color:"), convert_check);
  } else {
    auto* color_label = new QLabel(QObject::tr("sRGB, no profile needed"), metadata_group);
    color_label->setObjectName(QStringLiteral("webExportColorLabel"));
    metadata_form->addRow(QObject::tr("Color:"), color_label);
  }
  controls->addWidget(metadata_group);
  controls->addStretch(1);

  // --- right column: preview ---
  auto* preview_column = new QVBoxLayout();
  preview_column->setSpacing(6);
  auto* panes_row = new QHBoxLayout();
  panes_row->setSpacing(10);
  auto* original_pane = new WebExportPane(&dialog);
  original_pane->setObjectName(QStringLiteral("webExportOriginalPreview"));
  original_pane->set_content(WebExportPane::Content::Original);
  auto* main_pane = new WebExportPane(&dialog);
  main_pane->setObjectName(QStringLiteral("webExportPreview"));
  main_pane->setMinimumSize(300, 420);
  original_pane->setMinimumSize(300, 420);
  panes_row->addWidget(original_pane, 1);
  panes_row->addWidget(main_pane, 1);
  preview_column->addLayout(panes_row, 1);

  auto* view_row = new QHBoxLayout();
  view_row->setSpacing(4);
  view_row->addWidget(new QLabel(QObject::tr("Compare:"), &dialog));
  auto* compare_buttons = new QButtonGroup(&dialog);
  compare_buttons->setExclusive(true);
  const auto make_compare_button = [&](const QString& text, const char* object_name, WebExportCompare mode) {
    auto* button = new QPushButton(text, &dialog);
    button->setObjectName(QLatin1String(object_name));
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    compare_buttons->addButton(button, static_cast<int>(mode));
    view_row->addWidget(button);
    return button;
  };
  (void)make_compare_button(QObject::tr("Split"), "webExportCompareSplit", WebExportCompare::Split);
  (void)make_compare_button(QObject::tr("Side by side"), "webExportCompareSideBySide", WebExportCompare::SideBySide);
  (void)make_compare_button(QObject::tr("Result only"), "webExportCompareResult", WebExportCompare::ResultOnly);
  view_row->addStretch(1);
  const auto make_zoom_button = [&dialog](const char* object_name, const QString& text, const QString& tooltip) {
    auto* button = new QToolButton(&dialog);
    button->setObjectName(QLatin1String(object_name));
    button->setText(text);
    button->setToolTip(tooltip);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
  };
  auto* zoom_fit = make_zoom_button("webExportZoomFit", QObject::tr("Fit"), QObject::tr("Fit the image in the preview"));
  auto* zoom_100 = make_zoom_button("webExportZoom100", QStringLiteral("100%"),
                                    QObject::tr("Zoom to 100% (1 image pixel = 1 screen pixel)"));
  auto* zoom_out = make_zoom_button("webExportZoomOut", QStringLiteral("-"), QObject::tr("Zoom out"));
  auto* zoom_in = make_zoom_button("webExportZoomIn", QStringLiteral("+"), QObject::tr("Zoom in"));
  auto* zoom_label = new QLabel(&dialog);
  zoom_label->setObjectName(QStringLiteral("webExportZoomLabel"));
  zoom_label->setMinimumWidth(72);
  zoom_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  view_row->addWidget(zoom_fit);
  view_row->addWidget(zoom_100);
  view_row->addWidget(zoom_out);
  view_row->addWidget(zoom_in);
  view_row->addWidget(zoom_label);
  preview_column->addLayout(view_row);

  auto* info_row = new QHBoxLayout();
  info_row->setSpacing(6);
  auto* busy = new ActivitySpinner(&dialog);
  busy->setObjectName(QStringLiteral("webExportBusySpinner"));
  info_row->addWidget(busy);
  auto* info_label = new QLabel(&dialog);
  info_label->setObjectName(QStringLiteral("webExportInfoLabel"));
  info_label->setWordWrap(true);
  info_row->addWidget(info_label, 1);
  preview_column->addLayout(info_row);
  body->addLayout(preview_column, 1);

  // --- footer ---
  auto* footer = new QHBoxLayout();
  footer->setSpacing(8);
  auto* reveal_check = new QCheckBox(export_reveal_check_label(), &dialog);
  reveal_check->setObjectName(QStringLiteral("webExportRevealCheck"));
  footer->addWidget(reveal_check);
  footer->addStretch(1);
  auto* reset_button = new QPushButton(QObject::tr("Reset"), &dialog);
  reset_button->setObjectName(QStringLiteral("webExportResetButton"));
  reset_button->setToolTip(QObject::tr("Return every setting to its default"));
  reset_button->setAutoDefault(false);
  auto* save_button = new QPushButton(QObject::tr("Save..."), &dialog);
  save_button->setObjectName(QStringLiteral("webExportSaveButton"));
  save_button->setDefault(true);
  auto* cancel_button = new QPushButton(QObject::tr("Cancel"), &dialog);
  cancel_button->setObjectName(QStringLiteral("webExportCancelButton"));
  cancel_button->setAutoDefault(false);
  footer->addWidget(reset_button);
  footer->addWidget(save_button);
  footer->addWidget(cancel_button);
  // Frameless chrome has no native resize border; the corner grip is the resize handle.
  footer->addWidget(new VisibleSizeGrip(&dialog), 0, Qt::AlignBottom);
  content->addLayout(footer);

  // --- state ---
  WebExportPreviewEngine engine(document);
  bool syncing = false;
  bool accepting = false;
  // Encodes that failed, by signature (their list row says so instead of "encoding...").
  std::map<QString, QString> errors;
  const auto selected_format = [&] {
    const auto* item = format_list->currentItem();
    return item != nullptr ? web_export_format_from_key(item->data(0, Qt::UserRole).toString())
                                 .value_or(WebExportFormat::Jpeg)
                           : WebExportFormat::Jpeg;
  };
  const auto read_settings = [&] {
    auto read = settings;
    read.format = selected_format();
    read.jpeg_quality = jpeg_quality->value();
    read.jpeg_progressive = jpeg_progressive->isChecked();
    read.jpeg_optimize = jpeg_optimize->isChecked();
    read.jpeg_background = background_swatch->property("webExportColor").value<QColor>();
    read.webp_quality = webp_quality->value();
    read.webp_lossless = webp_lossless->isChecked();
    read.webp_animate = offer_animation && webp_animate->isChecked();
    read.metadata = metadata_policy_from_key(metadata_combo->currentData().toString())
                        .value_or(metadata::MetadataPolicy::None);
    read.thumbnail = thumbnail_check->isChecked();
    read.resample = resample_combo->currentData().toString() == resample_key(WebExportResample::PixelArt)
                        ? WebExportResample::PixelArt
                        : WebExportResample::Smooth;
    read.trim = trim_check->isChecked();
    read.convert_to_srgb = convert_check == nullptr || convert_check->isChecked();
    read.compare = static_cast<WebExportCompare>(std::max(0, compare_buttons->checkedId()));
    read.reveal_when_done = reveal_check->isChecked();
    read.width = width_spin->value();
    read.height = height_spin->value();
    read.pixel_scale = std::clamp(static_cast<int>(std::lround(scale_spin->value() / 100.0)), 1, kMaxPixelScale);
    return read;
  };
  const auto set_background = [&](QColor color) {
    background_swatch->setProperty("webExportColor", color);
    set_themed_style(*background_swatch, background_swatch_style(color));
  };
  const auto refresh_size_mode = [&] {
    const bool pixel_art = resample_combo->currentData().toString() == resample_key(WebExportResample::PixelArt);
    const QSignalBlocker block_scale(scale_spin);
    if (pixel_art) {
      scale_spin->setDecimals(0);
      scale_spin->setRange(100.0, 100.0 * kMaxPixelScale);
      scale_spin->setSingleStep(100.0);
    } else {
      scale_spin->setDecimals(2);
      scale_spin->setRange(1.0, 1000.0);
      scale_spin->setSingleStep(10.0);
    }
    width_spin->setEnabled(!pixel_art);
    height_spin->setEnabled(!pixel_art);
    link->setEnabled(!pixel_art);
    scale_spin->setEnabled(pixel_art || link->isChecked());
  };
  const auto set_size_fields = [&](int width, int height, double percent) {
    const QSignalBlocker block_width(width_spin);
    const QSignalBlocker block_height(height_spin);
    const QSignalBlocker block_scale(scale_spin);
    width_spin->setValue(std::clamp(width, 1, kMaxOutputPixels));
    height_spin->setValue(std::clamp(height, 1, kMaxOutputPixels));
    scale_spin->setValue(percent);
  };
  const auto pixels_for = [](int source, double percent) {
    return static_cast<int>(std::clamp<long long>(std::llround(source * percent / 100.0), 1, kMaxOutputPixels));
  };
  const auto refresh_format_rows = [&] {
    const auto current = read_settings();
    for (int row = 0; row < format_list->topLevelItemCount(); ++row) {
      auto* item = format_list->topLevelItem(row);
      const auto format =
          web_export_format_from_key(item->data(0, Qt::UserRole).toString()).value_or(WebExportFormat::Jpeg);
      item->setText(1, web_export_format_summary(current, format));
      const auto job = web_export_job(current, format, document_size);
      const auto cached = engine.cached(job.signature);
      item->setText(2, cached.has_value() && cached->bytes != nullptr ? data_size_text(cached->bytes->size())
                       : errors.contains(job.signature)               ? QObject::tr("failed")
                                                                      : QObject::tr("encoding..."));
    }
  };
  const auto refresh_visibility = [&] {
    const auto format = selected_format();
    jpeg_group->setVisible(format == WebExportFormat::Jpeg);
    webp_group->setVisible(format == WebExportFormat::Webp);
    png_group->setVisible(format == WebExportFormat::Png);
    thumbnail_check->setVisible(format == WebExportFormat::Jpeg);
    thumbnail_check->setEnabled(metadata_combo->currentData().toString() !=
                                metadata_policy_key(metadata::MetadataPolicy::None));
    webp_quality->parentWidget()->setEnabled(!webp_lossless->isChecked());
  };
  const auto refresh_compare = [&] {
    const auto mode = static_cast<WebExportCompare>(std::max(0, compare_buttons->checkedId()));
    original_pane->setVisible(mode == WebExportCompare::SideBySide);
    main_pane->set_content(mode == WebExportCompare::Split ? WebExportPane::Content::Split
                                                           : WebExportPane::Content::Result);
    if (original_pane->isVisible()) {
      original_pane->set_view_state(main_pane->view_state());
    }
  };
  const auto show_result = [&] {
    const auto current = read_settings();
    const auto format = current.format;
    const auto job = web_export_job(current, format, document_size);
    const auto cached = engine.cached(job.signature);
    const auto format_label = web_export_format_name(format);
    if (cached.has_value()) {
      main_pane->set_images(cached->original, cached->decoded);
      original_pane->set_images(cached->original, {});
      const auto size_text = data_size_text(cached->bytes->size());
      main_pane->set_labels(QObject::tr("Original"), QObject::tr("%1: %2").arg(format_label, size_text));
      original_pane->set_labels(QObject::tr("Original"), {});
      const auto width = cached->decoded.width();
      const auto height = cached->decoded.height();
      const auto channels = cached->decoded.hasAlphaChannel() ? 4 : 3;
      const auto raw = static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height)) *
                       static_cast<std::size_t>(channels);
      const auto percent = raw > 0 ? static_cast<int>(std::lround(100.0 * static_cast<double>(cached->bytes->size()) /
                                                                  static_cast<double>(raw)))
                                   : 0;
      auto text = QObject::tr("Output %1 x %2, %3 %4 (%5% of %6 uncompressed)")
                      .arg(width)
                      .arg(height)
                      .arg(format_label, size_text)
                      .arg(percent)
                      .arg(data_size_text(raw));
      for (const auto& notice : cached->notices) {
        text += QLatin1Char(' ') + QString::fromStdString(notice);
      }
      info_label->setText(text);
    } else {
      // The last result stays on screen until the new one lands.
      main_pane->set_labels(QObject::tr("Original"), QObject::tr("%1: encoding...").arg(format_label));
      const auto failed = errors.find(job.signature);
      info_label->setText(failed != errors.end() ? QObject::tr("Encoding failed: %1").arg(failed->second)
                                                 : QObject::tr("Encoding %1...").arg(format_label));
    }
    main_pane->setProperty("webExportShowsCurrent", cached.has_value());
    busy->set_active(engine.busy() || !cached.has_value());
  };

  auto* debounce = new QTimer(&dialog);
  debounce->setSingleShot(true);
  debounce->setInterval(kPreviewDebounceMs);
  const auto request_encodes = [&] {
    const auto current = read_settings();
    std::vector<WebExportJob> jobs;
    // The selected format first, then the others one at a time.
    jobs.push_back(web_export_job(current, current.format, document_size));
    for (const auto format : web_export_formats()) {
      if (format != current.format) {
        jobs.push_back(web_export_job(current, format, document_size));
      }
    }
    engine.request(std::move(jobs));
    refresh_format_rows();
    show_result();
  };
  QObject::connect(debounce, &QTimer::timeout, &dialog, [&] { request_encodes(); });

  engine.set_result_callback([&](const WebExportJob& job, const WebExportResult& result) {
    if (!result.error.isEmpty()) {
      errors[job.signature] = result.error;
    }
    refresh_format_rows();
    const auto current_job = web_export_job(read_settings(), selected_format(), document_size);
    if (job.signature == current_job.signature) {
      if (!result.error.isEmpty()) {
        info_label->setText(QObject::tr("Encoding failed: %1").arg(result.error));
        busy->set_active(false);
        if (accepting) {
          accepting = false;
          save_button->setEnabled(true);
        }
        return;
      }
      show_result();
      if (accepting) {
        dialog.accept();
        return;
      }
    }
    busy->set_active(engine.busy() || !engine.cached(current_job.signature).has_value());
  });

  const auto on_changed = [&] {
    if (syncing) {
      return;
    }
    refresh_visibility();
    refresh_format_rows();
    show_result();
    debounce->start();
  };
  const auto write_settings = [&](const WebExportSettings& values) {
    syncing = true;
    for (int row = 0; row < format_list->topLevelItemCount(); ++row) {
      if (format_list->topLevelItem(row)->data(0, Qt::UserRole).toString() == web_export_format_key(values.format)) {
        format_list->setCurrentItem(format_list->topLevelItem(row));
      }
    }
    jpeg_quality->setValue(values.jpeg_quality);
    jpeg_progressive->setChecked(values.jpeg_progressive);
    jpeg_optimize->setChecked(values.jpeg_optimize);
    set_background(values.jpeg_background);
    webp_quality->setValue(values.webp_quality);
    webp_lossless->setChecked(values.webp_lossless);
    webp_animate->setChecked(values.webp_animate);
    metadata_combo->setCurrentIndex(std::max(0, metadata_combo->findData(metadata_policy_key(values.metadata))));
    thumbnail_check->setChecked(values.thumbnail);
    resample_combo->setCurrentIndex(std::max(0, resample_combo->findData(resample_key(values.resample))));
    trim_check->setChecked(values.trim);
    if (convert_check != nullptr) {
      convert_check->setChecked(values.convert_to_srgb);
    }
    if (auto* button = compare_buttons->button(static_cast<int>(values.compare))) {
      button->setChecked(true);
    }
    reveal_check->setChecked(values.reveal_when_done);
    refresh_size_mode();
    if (values.resample == WebExportResample::PixelArt) {
      const auto scale = std::clamp(values.pixel_scale, 1, kMaxPixelScale);
      set_size_fields(document_size.width() * scale, document_size.height() * scale, 100.0 * scale);
    } else {
      set_size_fields(values.width, values.height, 100.0 * values.width / std::max(1, document_size.width()));
    }
    refresh_compare();
    refresh_visibility();
    syncing = false;
  };

  // Size fields: linked, width, height and scale follow each other.
  QObject::connect(width_spin, &QSpinBox::valueChanged, &dialog, [&](int width) {
    if (syncing) {
      return;
    }
    const double percent = width * 100.0 / std::max(1, document_size.width());
    if (link->isChecked()) {
      set_size_fields(width, pixels_for(document_size.height(), percent), percent);
    }
    on_changed();
  });
  QObject::connect(height_spin, &QSpinBox::valueChanged, &dialog, [&](int height) {
    if (syncing) {
      return;
    }
    const double percent = height * 100.0 / std::max(1, document_size.height());
    if (link->isChecked()) {
      set_size_fields(pixels_for(document_size.width(), percent), height, percent);
    }
    on_changed();
  });
  QObject::connect(scale_spin, &QDoubleSpinBox::valueChanged, &dialog, [&](double percent) {
    if (syncing) {
      return;
    }
    set_size_fields(pixels_for(document_size.width(), percent), pixels_for(document_size.height(), percent), percent);
    on_changed();
  });
  QObject::connect(link, &QToolButton::toggled, &dialog, [&](bool linked) {
    refresh_size_mode();
    if (linked) {
      // Re-link at the width's scale.
      const double percent = width_spin->value() * 100.0 / std::max(1, document_size.width());
      set_size_fields(width_spin->value(), pixels_for(document_size.height(), percent), percent);
      on_changed();
    }
  });
  QObject::connect(resample_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) {
    if (syncing) {
      return;
    }
    refresh_size_mode();
    const bool pixel_art = resample_combo->currentData().toString() == resample_key(WebExportResample::PixelArt);
    if (pixel_art) {
      const auto scale = std::clamp(static_cast<int>(std::lround(width_spin->value() /
                                                                  static_cast<double>(std::max(1, document_size.width())))),
                                    1, kMaxPixelScale);
      set_size_fields(document_size.width() * scale, document_size.height() * scale, 100.0 * scale);
    }
    on_changed();
  });

  QObject::connect(format_list, &QTreeWidget::currentItemChanged, &dialog,
                   [&](QTreeWidgetItem*, QTreeWidgetItem*) { on_changed(); });
  QObject::connect(jpeg_quality, &QSpinBox::valueChanged, &dialog, [&](int) { on_changed(); });
  QObject::connect(webp_quality, &QSpinBox::valueChanged, &dialog, [&](int) { on_changed(); });
  QObject::connect(metadata_combo, &QComboBox::currentIndexChanged, &dialog, [&](int) { on_changed(); });
  for (auto* check : {jpeg_progressive, jpeg_optimize, webp_lossless, webp_animate, thumbnail_check, trim_check}) {
    QObject::connect(check, &QCheckBox::toggled, &dialog, [&](bool) { on_changed(); });
  }
  if (convert_check != nullptr) {
    QObject::connect(convert_check, &QCheckBox::toggled, &dialog, [&](bool) { on_changed(); });
  }
  QObject::connect(background_swatch, &QPushButton::clicked, &dialog, [&] {
    const auto original = background_swatch->property("webExportColor").value<QColor>();
    const auto chosen = request_patchy_color(&dialog, original, QObject::tr("Export Background Color"),
                                             [&](QColor color) { set_background(color); });
    set_background(chosen.value_or(original));
    on_changed();
  });
  QObject::connect(compare_buttons, &QButtonGroup::idClicked, &dialog, [&](int) { refresh_compare(); });

  // Zoom: the main pane drives; side by side keeps the two panes in step.
  const auto refresh_zoom_label = [&] {
    const auto percent = main_pane->property("previewZoomPercent").toInt();
    zoom_label->setText(main_pane->fit_mode() ? QObject::tr("Fit (%1%)").arg(percent)
                                              : QStringLiteral("%1%").arg(percent));
  };
  bool syncing_view = false;
  const auto sync_view = [&](WebExportPane* from, WebExportPane* to) {
    // Only while both panes show (side by side): a hidden pane's updates never move the other.
    if (syncing_view || !to->isVisible() || !from->isVisible()) {
      return;
    }
    syncing_view = true;
    to->set_view_state(from->view_state());
    syncing_view = false;
  };
  main_pane->set_zoom_changed_callback([&] {
    refresh_zoom_label();
    sync_view(main_pane, original_pane);
  });
  original_pane->set_zoom_changed_callback([&] { sync_view(original_pane, main_pane); });
  QObject::connect(zoom_fit, &QToolButton::clicked, &dialog, [&] { main_pane->zoom_to_fit(); });
  QObject::connect(zoom_100, &QToolButton::clicked, &dialog, [&] { main_pane->zoom_to(1.0); });
  QObject::connect(zoom_out, &QToolButton::clicked, &dialog, [&] { main_pane->zoom_step(-1); });
  QObject::connect(zoom_in, &QToolButton::clicked, &dialog, [&] { main_pane->zoom_step(1); });

  QObject::connect(reset_button, &QPushButton::clicked, &dialog, [&] {
    auto defaults = WebExportSettings{};
    defaults.compare = static_cast<WebExportCompare>(std::max(0, compare_buttons->checkedId()));
    defaults.reveal_when_done = reveal_check->isChecked();
    defaults.width = document_size.width();
    defaults.height = document_size.height();
    write_settings(defaults);
    on_changed();
  });
  QObject::connect(cancel_button, &QPushButton::clicked, &dialog, &QDialog::reject);
  QObject::connect(save_button, &QPushButton::clicked, &dialog, [&] {
    const auto current_job = web_export_job(read_settings(), selected_format(), document_size);
    if (!debounce->isActive() && engine.cached(current_job.signature).has_value()) {
      dialog.accept();
      return;
    }
    // The current settings are still encoding: finish first, then close.
    accepting = true;
    save_button->setEnabled(false);
    if (debounce->isActive()) {
      debounce->stop();
      request_encodes();
      // Settings that were changed and changed back are already encoded.
      if (engine.cached(current_job.signature).has_value()) {
        dialog.accept();
      }
    }
  });

  write_settings(settings);
  append_themed_style(dialog, dialog_spinbox_button_style());
  const auto engine_cleanup = qScopeGuard([&engine] { engine.close(); });
  dialog.resize(1280, 820);
  request_encodes();

  const auto code = exec_dialog(dialog);
  engine.close();
  if (code != QDialog::Accepted) {
    return std::nullopt;
  }
  WebExportDialogResult result;
  result.settings = read_settings();
  result.job = web_export_job(result.settings, result.settings.format, document_size);
  if (const auto cached = engine.cached(result.job.signature); cached.has_value()) {
    result.bytes = cached->bytes;
    result.notices = cached->notices;
  }
  save_web_export_settings(result.settings);
  return result;
}

}  // namespace patchy::ui
