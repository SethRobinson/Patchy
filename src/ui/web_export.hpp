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
#include <span>
#include <string>
#include <vector>

class QWidget;

namespace patchy::ui {

// The formats the dialog lists (web_export_formats() gives the list order: JPEG, JPEG XL,
// WebP, AVIF, PNG, GIF; AVIF only where its encoder is built). Append-only: the list order
// is display only.
enum class WebExportFormat : std::uint8_t { Jpeg, Webp, Png, Gif, Avif, Jxl };

[[nodiscard]] const std::vector<WebExportFormat>& web_export_formats();
// Persisted tokens (exportForWeb/format, doc.encodeForWeb): "jpeg", "webp", "png", "gif",
// "avif", "jxl". Never rename.
[[nodiscard]] QString web_export_format_key(WebExportFormat format);
[[nodiscard]] std::optional<WebExportFormat> web_export_format_from_key(const QString& key);
// The file extension written: "jpg", "webp", "png", "gif", "avif", "jxl".
[[nodiscard]] QString web_export_extension(WebExportFormat format);
[[nodiscard]] QString web_export_format_name(WebExportFormat format);

enum class WebExportResample : std::uint8_t { Smooth, PixelArt };
// Persisted as exportForWeb/compare ("split", "sideBySide", "result", "quad"). Append-only.
enum class WebExportCompare : std::uint8_t { Split, SideBySide, ResultOnly, Quad };

// Everything the dialog remembers (exportForWeb/* settings keys, docs/web-export.md),
// plus the output size, which is per document and never persisted. Each format keeps its
// own settings.
struct WebExportSettings {
  WebExportFormat format{WebExportFormat::Jpeg};
  int jpeg_quality{80};
  bool jpeg_progressive{true};
  bool jpeg_optimize{true};
  // The one background color: JPEG's forced fill and every format's "Fill with" (the
  // persisted key stays exportForWeb/jpegBackground).
  QColor jpeg_background{Qt::white};
  // Transparency: Keep transparent, or fill with jpeg_background (formats with alpha).
  bool fill_transparent{false};
  // Animation, shared with the Export > Layers as Animated ... dialogs: the default frame
  // delay (saveOptions/animationFrameDelayMs, GIF in centiseconds through
  // saveOptions/gifFrameDelayCs) and each format's play count (0 forever), seeded from the
  // document's metadata (webp.loop_count, jxl.loop_count, apng.loop_count), never persisted.
  int animation_frame_delay_ms{100};
  int webp_loop_count{0};
  int jxl_loop_count{0};
  int apng_loop_count{0};
  int webp_quality{75};
  bool webp_lossless{false};
  bool webp_animate{false};
  // AVIF: quality 0..100, encoder speed 0..10 (lower is smaller and slower), lossless.
  int avif_quality{80};
  int avif_speed{6};
  bool avif_lossless{false};
  // JPEG XL: quality 1..100, effort 1..9, lossless, animation from visible layers.
  int jxl_quality{90};
  int jxl_effort{7};
  bool jxl_lossless{false};
  bool jxl_animate{false};
  // PNG: full color, or a palette of png_colors with optional error diffusion.
  bool png_palette{false};
  int png_colors{256};
  bool png_dither{false};
  int png_dither_amount{100};
  // PNG: animated PNG (APNG) from visible layers; always full color (the APNG writer is
  // 8-bit RGBA), so it overrides png_palette.
  bool png_animate{false};
  // GIF: colors, error diffusion, and animation from visible layers with a frame delay.
  int gif_colors{256};
  bool gif_dither{false};
  int gif_dither_amount{100};
  bool gif_animate{false};
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

// GIF and PNG (full color and palette) of at least this many output pixels encode only once
// their row has been selected, as AVIF and JPEG XL always do (GitHub issue 571: seconds each
// on a large photo, docs/web-export.md "Preview engine").
inline constexpr std::int64_t kWebExportDeferredPixels = 3'000'000;
// One frame's output pixels at these settings (the size fields or the Pixel art factor,
// before any trim).
[[nodiscard]] std::int64_t web_export_output_pixels(const WebExportSettings& settings, QSize document_size);
// Whether the format's row waits until it has been selected once before encoding in the
// background: AVIF and JPEG XL always, GIF and PNG from kWebExportDeferredPixels.
[[nodiscard]] bool web_export_format_is_slow(WebExportFormat format, const WebExportSettings& settings,
                                             QSize document_size);

// The ImageSaveOptions that write `format` at these settings for a document of
// `document_size` (the export transforms included).
[[nodiscard]] ImageSaveOptions web_export_image_options(const WebExportSettings& settings, WebExportFormat format,
                                                        QSize document_size);
// The part of those options that decides the prepared (resized, trimmed) image.
[[nodiscard]] ImageSaveOptions web_export_geometry_options(const WebExportSettings& settings, QSize document_size);
// A short description of a format's settings for its list row ("Quality 80, progressive").
[[nodiscard]] QString web_export_format_summary(const WebExportSettings& settings, WebExportFormat format);

// The 2 x 2 compare mode's lower tiles: the settings with the format's quality, or a palette
// format's colors (GIF, palette PNG), at `percent` of the set value, rounded and clamped to
// the format's minimum (quality 60 gives 30 and 15). Nullopt when the format at these
// settings has neither (full-color or animated PNG, lossless WebP, AVIF and JPEG XL).
[[nodiscard]] std::optional<WebExportSettings> web_export_reduced_settings(const WebExportSettings& settings,
                                                                           WebExportFormat format, int percent);
// The tile tag for a format's quality or colors: "Quality 30", "128 colors"; empty when the
// format at these settings has neither.
[[nodiscard]] QString web_export_quality_label(const WebExportSettings& settings, WebExportFormat format);

// The quality presets beside the Quality field of JPEG, JPEG XL, WebP and AVIF (Patchy's own
// names and values). `name` is the untranslated source text (context "QObject").
struct WebExportQualityPreset {
  const char* name;
  int quality;
};
[[nodiscard]] std::span<const WebExportQualityPreset> web_export_quality_presets();

// A saved export setup (the presets combo at the top of the settings column): the format, that
// format's options, and the shared ones (transparency and its color, size as a scale,
// resample, trim, metadata, thumbnail, Convert to sRGB). Other formats' options, the compare
// mode, Show when done and the play counts are not part of a preset. User presets are kept in
// exportForWeb/presets (a JSON array, docs/web-export.md); built-in ones cannot be deleted.
struct WebExportPreset {
  QString name;
  bool built_in{false};
  WebExportSettings settings;
  // Width as a percent of the document's (Smooth), or the whole-pixel factor times 100 (Pixel art).
  double scale_percent{100.0};
};
[[nodiscard]] std::vector<WebExportPreset> web_export_builtin_presets();
[[nodiscard]] std::vector<WebExportPreset> load_web_export_user_presets();
void save_web_export_user_presets(const std::vector<WebExportPreset>& presets);
[[nodiscard]] WebExportPreset make_web_export_preset(const QString& name, const WebExportSettings& settings,
                                                     QSize document_size);
// `current` with the preset's part applied (the size follows the preset's scale on this document).
[[nodiscard]] WebExportSettings apply_web_export_preset(const WebExportPreset& preset, WebExportSettings current,
                                                        QSize document_size);
[[nodiscard]] bool web_export_preset_matches(const WebExportPreset& preset, const WebExportSettings& current,
                                             QSize document_size);

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
  // Animated results (GIF, WebP, JPEG XL with animation and 2+ frames): every frame decoded
  // from the encoded bytes with the file's own delays, and the source frames the writer
  // encoded (for_each_animation_export_frame), same order and size. Empty for a still. Only
  // the first frames are kept when all of them would pass kWebExportAnimationCacheBytes;
  // frames_total is then the file's real count.
  std::vector<QImage> frames;
  std::vector<QImage> original_frames;
  std::vector<int> frame_delays_ms;
  int frames_total{0};
};

// The memory the preview may spend on one animated result's cached frames, both sides together.
inline constexpr std::size_t kWebExportAnimationCacheBytes = 384U * 1024U * 1024U;

struct WebExportAnimation {
  std::vector<QImage> frames;  // display images, 8-bit ARGB32 premultiplied
  std::vector<int> delays_ms;  // as written in the file
  int total{0};                // frames in the file (frames.size() can be smaller)
};
// Every frame of an encoded GIF, WebP or JPEG XL, decoded through the readers Patchy opens
// those files with, at most `max_frames` of them. A still file gives one frame.
[[nodiscard]] WebExportAnimation decode_web_export_animation(WebExportFormat format, std::span<const std::uint8_t> bytes,
                                                             std::size_t max_frames);

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

// Encoded bytes read back for display (8-bit ARGB32 premultiplied; an animation's first
// frame): Qt's readers for JPEG, WebP, PNG and GIF, Patchy's for AVIF and JPEG XL.
[[nodiscard]] QImage decode_web_export_bytes(WebExportFormat format, std::span<const std::uint8_t> bytes);

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

// How File > Export > Layers as Animated GIF, WebP and PNG open the dialog: that format
// selected with its "Animation from visible layers" on, and Keep transparent or Fill as the
// frames need. It changes only this opening; what is saved is what the dialog saves on Save.
struct WebExportOpening {
  WebExportFormat format{WebExportFormat::Gif};
  bool animate{true};
  bool keep_transparency{true};
};

// The Export for Web dialog over a snapshot of the document. Nullopt on cancel; settings
// are saved on Save, the window size (exportForWeb/dialogSize) and the compare mode on any close.
[[nodiscard]] std::optional<WebExportDialogResult> request_web_export(
    QWidget* parent, std::shared_ptr<const Document> document,
    const std::optional<WebExportOpening>& opening = std::nullopt);

}  // namespace patchy::ui
