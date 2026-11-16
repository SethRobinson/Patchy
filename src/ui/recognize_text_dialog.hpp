#pragma once

// Type > Recognize Text (OCR)... (docs/ocr.md, "Type menu commands"): the options of one
// recognition. The engine, language and font are the user's picks; nothing here looks at
// the image.

#include "ui/ocr_recognition.hpp"

#include <QString>

#include <optional>

class QWidget;

namespace patchy::ui {

// What is read. The tokens are persisted (ocr/source): never rename one.
//   layer      the active layer on its own
//   selection  everything visible, inside the selection's bounds
//   image      everything visible
enum class OcrSource { Layer, Selection, Image };
[[nodiscard]] QString ocr_source_token(OcrSource source);
[[nodiscard]] std::optional<OcrSource> ocr_source_from_token(const QString& token);

struct RecognizeTextChoice {
  QString engine;
  QString language;
  OcrSource source{OcrSource::Layer};
  // What becomes of the text: layers (visible, replace, hidden), or nothing added and the
  // text copied to the clipboard (mode None with `copy_text`).
  OcrLayerMode mode{OcrLayerMode::Visible};
  bool copy_text{false};
  OcrGrouping grouping{OcrGrouping::Blocks};
  // Empty: the Type tool's font.
  QString font_family;
  bool match_style{true};
};

struct RecognizeTextDialogContext {
  bool has_layer{true};      // an active layer exists to read on its own
  bool has_selection{false}; // a selection exists
  QString type_tool_family;  // the font the Font row starts on
};

// The stored options (ocr/result, ocr/source, ocr/grouping, ocr/matchStyle, ocr/fontFamily;
// engine and language resolve when the recognition runs) fitted to `context`: a stored
// source that does not apply now falls back to the nearest that does.
[[nodiscard]] RecognizeTextChoice stored_recognize_text_choice(const RecognizeTextDialogContext& context);

// Shows the dialog. Unset when cancelled. When no engine can run the dialog still opens and
// says why; its OK button stays off. Accepting stores every option.
[[nodiscard]] std::optional<RecognizeTextChoice> request_recognize_text(QWidget* parent,
                                                                        const RecognizeTextDialogContext& context);

}  // namespace patchy::ui
