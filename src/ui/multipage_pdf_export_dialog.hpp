#pragma once

#include "ui/document_order_list.hpp"
#include "ui/image_save_options.hpp"
#include "ui/pdf_export.hpp"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

class QWidget;

namespace patchy::ui {

// PDF Options, the one dialog every interactive PDF save shows (docs/pdf.md): Save As and
// Export Flat Image to .pdf, and File > Export > Multi-Page PDF, each after the file name
// is chosen. It picks which pages go into the file and how. The writing itself is
// write_pdf_document_file / write_multipage_pdf_file (pdf_export.hpp); MainWindow resolves
// the choice into documents because only it owns the sessions.

// One open document as the dialog lists it (the shared document-order row).
using MultiPagePdfDocumentEntry = DocumentOrderEntry;

enum class MultiPagePdfSource {
  // Every checked open document, one page each, in the list's order (offered only from
  // File > Export > Multi-Page PDF).
  OpenDocuments,
  // Every top-level layer group of the active document (hidden ones too unless the
  // choice says otherwise), one page each, top of the layer stack first ("print a folder
  // as a page"; a PDF imported as page folders goes back page by page).
  TopLevelGroups,
  // Every visible artboard of the active document, one page each, sized to it
  // (documents_for_artboards, docs/artboards.md).
  Artboards,
  // The active document as one page of its whole canvas.
  Canvas,
};

struct MultiPagePdfExportChoice {
  MultiPagePdfSource source{MultiPagePdfSource::OpenDocuments};
  // OpenDocuments: the checked sessions in page order.
  std::vector<std::int64_t> session_ids;
  // TopLevelGroups: ungrouped root layers (a shared background, say) draw on every page.
  bool include_ungrouped_layers{true};
  // TopLevelGroups: hidden groups are pages too (a PDF imported as page folders shows
  // only page 1). Persists as exportOptions/multiPagePdfHiddenGroups.
  bool include_hidden_groups{true};
  PdfExportOptions options;
};

struct PdfOptionsDialogContext {
  // The open sessions for the OpenDocuments source; empty hides that source (Save As
  // writes the active document only).
  std::vector<MultiPagePdfDocumentEntry> documents;
  // The session whose groups and artboards the other sources page.
  std::int64_t active_session_id{0};
  int top_level_group_count{0};
  int hidden_group_count{0};
  int artboard_count{0};
  // Some page still carries the image data it was imported from a PDF with: the only
  // time "keep original image data" shows.
  bool original_image_data_available{false};
  // Some page holds recognized text: the only time "include recognized text" shows.
  bool recognized_text_available{false};
  // The pages hold more than one flat image, so "Keep layers as editable objects" (and
  // the missing-fonts choice under it) applies.
  bool layered{true};
  // The editable check's starting state; nullopt takes the remembered one
  // (exportOptions/multiPagePdfEditableLayers).
  std::optional<bool> editable_default;
  // The source to start on; nullopt takes the remembered one
  // (exportOptions/multiPagePdfSource), artboards first for an artboard document.
  std::optional<MultiPagePdfSource> default_source;
  QString title;        // the window title
  QString file_name;    // the chosen file, named above the choices
  QString accept_text;  // "Save" or "Export"
};

// nullopt when cancelled.
[[nodiscard]] std::optional<MultiPagePdfExportChoice> run_pdf_options_dialog(QWidget* parent,
                                                                           const PdfOptionsDialogContext& context);

// The PDF fields of `options` (quality, editable layers, pass-through, missing fonts,
// pages and the group choices) from the dialog's answer, for the single-document writers.
void apply_pdf_choice_to_save_options(const MultiPagePdfExportChoice& choice, ImageSaveOptions& options);

}  // namespace patchy::ui
