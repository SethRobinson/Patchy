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

// Which options the dialog shows.
//   Document   Type > Recognize Text: what to read in the open document and what to make.
//   PdfImport  Import PDF's "Options...": no Read row (the pages are what is read), the
//              Result rows are the three layer modes with hidden first, stored under
//              imports/pdfOcrResult, and nothing is recognized by accepting it.
enum class RecognizeTextPurpose { Document, PdfImport };

struct RecognizeTextDialogContext {
  bool has_layer{true};      // an active layer exists to read on its own
  bool has_selection{false}; // a selection exists
  QString type_tool_family;  // the font the Font row starts on
  RecognizeTextPurpose purpose{RecognizeTextPurpose::Document};
};

// The stored options (ocr/result, ocr/source, ocr/grouping, ocr/matchStyle, ocr/fontFamily;
// engine and language resolve when the recognition runs) fitted to `context`: a stored
// source that does not apply now falls back to the nearest that does.
[[nodiscard]] RecognizeTextChoice stored_recognize_text_choice(const RecognizeTextDialogContext& context);

// What Import PDF's text recognition makes when the user never opened its options: the
// stored imports/pdfOcrResult (hidden by default: the page looks unchanged) with the layer
// options every recognition shares (ocr/grouping, ocr/matchStyle, ocr/fontFamily). Engine
// and language are left empty: they resolve from the stored picks when the import runs.
[[nodiscard]] RecognizeTextChoice stored_pdf_import_ocr_choice();
// The Result row's text for a layer mode in the PDF import options ("Hidden text layers,
// page unchanged"), for the import dialog's summary line.
[[nodiscard]] QString pdf_import_ocr_result_label(OcrLayerMode mode);

// Shows the dialog. Unset when cancelled. When no engine can run the dialog still opens and
// says why; its OK button stays off. Accepting stores every option.
[[nodiscard]] std::optional<RecognizeTextChoice> request_recognize_text(QWidget* parent,
                                                                        const RecognizeTextDialogContext& context);

}  // namespace patchy::ui
