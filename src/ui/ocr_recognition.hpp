#pragma once

// What every text recognition entry point shares on the UI side (docs/ocr.md): the pixels
// a recognition reads, running an engine off the GUI thread, and what becomes of the result.
// The Type menu command, the script calls and the Import PDF option all go through these.

#include "core/document.hpp"
#include "ocr/ocr_engine.hpp"
#include "ui/ocr_text_layers.hpp"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace patchy::ui {

// What recognized text becomes. The tokens are persisted (ocr/result, imports/pdfOcrResult)
// and are script option values: never rename one.
//   none     nothing is added (the caller wanted the text or the boxes only)
//   visible  text layers over the image
//   replace  text layers shown and the original hidden ("show text and hide original")
//   hidden   text layers in a hidden group: the image looks unchanged
enum class OcrLayerMode { None, Visible, Replace, Hidden };
[[nodiscard]] QString ocr_layer_mode_token(OcrLayerMode mode);
[[nodiscard]] std::optional<OcrLayerMode> ocr_layer_mode_from_token(const QString& token);

struct OcrLayerOptions {
  OcrLayerMode mode{OcrLayerMode::Visible};
  OcrGrouping grouping{OcrGrouping::Blocks};
  OcrLayerStyle style;
};

// The pixels an engine reads (straight-alpha RGBA8888) and where their top left sits in
// the document.
struct OcrSourceImage {
  QImage image;
  QPoint origin;
};

// One layer rendered on its own (any kind: pixel, text, shape, Smart Object, a group with
// its contents), or the whole document's composite when `layer` is unset, cropped to
// `rect` (document coordinates) when one is given. The image is 8-bit whatever the
// document's depth. Unset with `error` filled when there is nothing to read: a hidden or
// empty layer, or a rectangle outside the canvas.
[[nodiscard]] std::optional<OcrSourceImage> ocr_source_image(const Document& document, std::optional<LayerId> layer,
                                                             std::optional<QRect> rect, QString* error);

// Runs `engine` for `language` over `image` on a worker thread while this (the GUI) thread
// waits, calling `pump` about every 15 ms: the caller turns its event loop there and may
// request `cancel`. Returns lines in the pixels of `image`. Throws what the engine throws:
// ocr::RecognitionCancelled after a cancel, std::runtime_error with an English diagnostic
// when the engine fails or does not have the language.
[[nodiscard]] ocr::Result recognize_on_worker(const std::shared_ptr<ocr::Engine>& engine, const std::string& language,
                                              const QImage& image, ocr::Cancel& cancel,
                                              const std::function<void()>& pump);

// The same lines with every coordinate moved by `origin`: image pixels to document pixels.
[[nodiscard]] ocr::Result recognition_in_document(ocr::Result result, QPoint origin);

// The engine and language a recognition should use when the caller names none: the stored
// picks (ocr/engine, ocr/language) while they still exist, else the first available engine
// and its default language. `engine` is null when no engine is available; `error` then says
// why in the user's language.
struct OcrChoice {
  std::shared_ptr<ocr::Engine> engine;
  std::string language;
};
[[nodiscard]] OcrChoice resolve_ocr_choice(const QString& engine_id, const QString& language, QString* error);

// One recognition over a document (MainWindow::run_text_recognition): what to read, with
// what, and what to make of it.
struct OcrRecognitionRequest {
  // The layer read on its own; unset reads the whole image as composited.
  std::optional<LayerId> layer;
  // Only this part (document coordinates), when set.
  std::optional<QRect> rect;
  // Empty: the stored picks, see resolve_ocr_choice.
  QString engine;
  QString language;
  OcrLayerOptions layers;
};

struct OcrRecognitionOutcome {
  ocr::Result result;  // document pixels
  QString engine;      // the engine id and language tag that ran
  QString language;
  // The Recognized Text group that was added, when the request asked for layers and text
  // was found.
  std::optional<LayerId> group;
  bool cancelled{false};
};

// The settings keys shared by every entry point.
inline constexpr const char* kOcrEngineSettingsKey = "ocr/engine";
inline constexpr const char* kOcrLanguageSettingsKey = "ocr/language";

}  // namespace patchy::ui
