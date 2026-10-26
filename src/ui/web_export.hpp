#pragma once

// File > Export for Web (docs/web-export.md): the settings model, the background preview
// engine and the dialog. The dialog encodes every web format with the real writers
// (encode_flat_image_bytes) and shows the decoded result beside the original.

#include "core/document.hpp"
#include "ui/image_save_options.hpp"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QWidget;

namespace patchy::ui {

// The formats the dialog lists (web_export_formats() gives the list order). Append-only:
// AVIF and JPEG XL join later (the keys are persisted).
enum class WebExportFormat : std::uint8_t { Jpeg, Webp, Png, Gif };

[[nodiscard]] const std::vector<WebExportFormat>& web_export_formats();
// Persisted tokens (exportForWeb/format): "jpeg", "webp", "png", "gif". Never rename.
[[nodiscard]] QString web_export_format_key(WebExportFormat format);
[[nodiscard]] std::optional<WebExportFormat> web_export_format_from_key(const QString& key);
// The file extension written: "jpg", "webp", "png", "gif".
[[nodiscard]] QString web_export_extension(WebExportFormat format);
[[nodiscard]] QString web_export_format_name(WebExportFormat format);

enum class WebExportResample : std::uint8_t { Smooth, PixelArt };
enum class WebExportCompare : std::uint8_t { Split, SideBySide, ResultOnly };

// Everything the dialog remembers (exportForWeb/* settings keys, docs/web-export.md),
// plus the output size, which is per document and never persisted. Each format keeps its
// own settings.
struct WebExportSettings {
  WebExportFormat format{WebExportFormat::Jpeg};
  int jpeg_quality{80};
  bool jpeg_progressive{true};
  bool jpeg_optimize{true};
  QColor jpeg_background{Qt::white};
  int webp_quality{75};
  bool webp_lossless{false};
  bool webp_animate{false};
  // PNG: full color, or a palette of png_colors with optional error diffusion.
  bool png_palette{false};
  int png_colors{256};
  bool png_dither{false};
  int png_dither_amount{100};
  // GIF: colors, error diffusion, and animation from visible layers with a frame delay.
  int gif_colors{256};
  bool gif_dither{false};
  int gif_dither_amount{100};
  bool gif_animate{false};
  int gif_frame_delay_ms{100};
  metadata::MetadataPolicy metadata{metadata::MetadataPolicy::None};
  bool thumbnail{false};
  WebExportResample resample{WebExportResample::Smooth};
  bool trim{false};
  bool convert_to_srgb{true};
  WebExportCompare compare{WebExportCompare::Split};
  bool reveal_when_done{false};
  // Per document: the output size before any trim (0 = the document's own size), and the
  // whole-pixel factor in Pixel art mode.
  int width{0};
  int height{0};
  int pixel_scale{1};

  friend bool operator==(const WebExportSettings&, const WebExportSettings&) = default;
};

[[nodiscard]] WebExportSettings load_web_export_settings();
void save_web_export_settings(const WebExportSettings& settings);

// The ImageSaveOptions that write `format` at these settings for a document of
// `document_size` (the export transforms included).
[[nodiscard]] ImageSaveOptions web_export_image_options(const WebExportSettings& settings, WebExportFormat format,
                                                        QSize document_size);
// The part of those options that decides the prepared (resized, trimmed) image.
[[nodiscard]] ImageSaveOptions web_export_geometry_options(const WebExportSettings& settings, QSize document_size);
// A short description of a format's settings for its list row ("Quality 80, progressive").
[[nodiscard]] QString web_export_format_summary(const WebExportSettings& settings, WebExportFormat format);

// One encode the engine runs: a format at a settings signature. `animate` encodes from the
// whole document (the animated writers need the layers); otherwise from the prepared one.
struct WebExportJob {
  WebExportFormat format{WebExportFormat::Jpeg};
  ImageSaveOptions geometry;
  ImageSaveOptions options;
  bool convert_to_srgb{true};
  bool animate{false};
  // Identifies the output: equal signatures give equal bytes.
  QString signature;
};

[[nodiscard]] WebExportJob web_export_job(const WebExportSettings& settings, WebExportFormat format,
                                          QSize document_size);

struct WebExportResult {
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;
  QImage decoded;   // the encoded file read back, 8-bit ARGB32 premultiplied
  QImage original;  // the prepared image it was encoded from, same size
  QString error;    // non-empty when encoding failed
  std::vector<std::string> notices;
  // GIF and palette PNG: the colors the file uses (from the decoded result, at most 256).
  std::vector<QRgb> palette;
};

// The background preview engine: one worker at a time works through a queue of jobs, the
// newest request first. request() replaces the queue and bumps the generation; a queued job
// of an older generation is skipped, and a finished one is kept in the cache by signature
// (equal settings give equal bytes) but reported as current only when it matches. The source
// is narrowed to 8 bits once; the prepared (resized, trimmed, sRGB) document is cached by its
// geometry. Nothing here runs on the UI thread except bookkeeping.
class WebExportPreviewEngine {
public:
  explicit WebExportPreviewEngine(std::shared_ptr<const Document> source);
  ~WebExportPreviewEngine();
  WebExportPreviewEngine(const WebExportPreviewEngine&) = delete;
  WebExportPreviewEngine& operator=(const WebExportPreviewEngine&) = delete;

  // Called on the UI thread with each finished job.
  void set_result_callback(std::function<void(const WebExportJob&, const WebExportResult&)> callback);
  void request(std::vector<WebExportJob> jobs);
  [[nodiscard]] std::optional<WebExportResult> cached(const QString& signature) const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] std::uint64_t generation() const;
  // Encodes started so far (tests: stale jobs never start).
  [[nodiscard]] int encodes_started() const;
  // Stops reporting; a running encode finishes on its worker and is dropped.
  void close();

  // The synchronous encode the worker runs (also Save's fallback when no current result).
  [[nodiscard]] static WebExportResult encode(const Document& narrowed_source, const WebExportJob& job,
                                              const std::shared_ptr<const Document>& prepared);
  [[nodiscard]] static std::shared_ptr<const Document> prepare(const Document& narrowed_source,
                                                               const ImageSaveOptions& geometry,
                                                               bool convert_to_srgb,
                                                               std::vector<std::string>* notices);

  // Implementation detail, shared with the worker completions in web_export.cpp.
  struct State;

private:
  void start_next();
  std::shared_ptr<State> state_;
};

// Whether the document carries an RGB profile other than sRGB (the dialog then offers
// Convert to sRGB).
[[nodiscard]] bool web_export_has_non_srgb_profile(const Document& document);

struct WebExportDialogResult {
  WebExportSettings settings;
  WebExportJob job;
  // The bytes encoded at exactly these settings, when the preview had them; else empty
  // and the caller encodes.
  std::shared_ptr<const std::vector<std::uint8_t>> bytes;
  // The writer's notices for those bytes (a trim that kept the full canvas).
  std::vector<std::string> notices;
};

// The Export for Web dialog over a snapshot of the document. Nullopt on cancel; settings
// are saved on Save.
[[nodiscard]] std::optional<WebExportDialogResult> request_web_export(QWidget* parent,
                                                                      std::shared_ptr<const Document> document);

}  // namespace patchy::ui
