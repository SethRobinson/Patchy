// The Import PDF upgrade (GitHub issues 147, 152, 153, 154, 155) and PDF artboards.
#include "core/artboard.hpp"
#include "core/gray_conversion.hpp"
#include "core/layer_metadata.hpp"
#include "core/layer_render_utils.hpp"
#include "core/vector_shape.hpp"
#include "formats/pdf_file.hpp"
#include "ui/image_document_io.hpp"
#include "ui/pdf_export.hpp"
#include "core/pixel_depth.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/pdf_import.hpp"
#include "ui/app_settings.hpp"
#include "import_print_resolution_test_support.hpp"
#include "test_fonts.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QLabel>
#if defined(PATCHY_HAVE_QT_PDF)
#include <QPdfDocument>
#endif
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QTimer>
#include <QTransform>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace patchy::test::ui;
using namespace patchy::test::ui::import_print_resolution_support;

#if defined(PATCHY_HAVE_QT_PDF)

// Page 1 is 100 x 100 pt with a red square in its lower-left quarter and nothing else
// painted, so most of the page is the transparent nothing PDFium renders; page 2 is
// 60 x 60 pt of solid blue.
QByteArray red_square_pdf_bytes() {
  const std::string first = "1 0 0 rg 10 10 40 40 re f";
  const std::string second = "0 0 1 rg 0 0 60 60 re f";
  return pdf_from_objects({
      "<</Type/Catalog/Pages 2 0 R>>",
      "<</Type/Pages/Kids[3 0 R 5 0 R]/Count 2>>",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 100 100]/Contents 4 0 R>>",
      "<</Length " + std::to_string(first.size()) + ">>\nstream\n" + first + "\nendstream",
      "<</Type/Page/Parent 2 0 R/MediaBox[0 0 60 60]/Contents 6 0 R>>",
      "<</Length " + std::to_string(second.size()) + ">>\nstream\n" + second + "\nendstream",
  });
}

QString write_artifact(const QString& name, const QByteArray& bytes) {
  ensure_artifact_dir();
  const auto path = QStringLiteral("test-artifacts/") + name;
  QFile::remove(path);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  file.write(bytes);
  return path;
}

// GitHub issue 147: the resolution is an editable combo with the common values, typed
// values still work, and an out-of-range one holds the Import button. Issue 152: Mode and
// Bit Depth are there, Indexed needs flattened pages, and Indexed is 8-bit only.
void ui_pdf_import_dialog_offers_resolution_presets_mode_and_depth() {
  const auto path = write_artifact(QStringLiteral("ui_pdf_import_dialog_presets.pdf"), red_square_pdf_bytes());
  patchy::set_deep_editing_override(true);
  patchy::ui::MainWindow window;
  show_window(window);
  auto done = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [done] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* color_mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportColorModeCombo"));
    auto* bit_depth = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportBitDepthCombo"));
    auto* reverse = dialog->findChild<QCheckBox*>(QStringLiteral("pdfImportReverseOrderCheck"));
    auto* paper = dialog->findChild<QCheckBox*>(QStringLiteral("pdfImportWhiteBackgroundCheck"));
    auto* size_label = dialog->findChild<QLabel*>(QStringLiteral("pdfImportSizeLabel"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (resolution == nullptr || mode == nullptr || color_mode == nullptr || bit_depth == nullptr ||
        reverse == nullptr || paper == nullptr || size_label == nullptr || import_button == nullptr) {
      return false;
    }
    CHECK(resolution->isEditable());
    QStringList presets;
    for (int row = 0; row < resolution->count(); ++row) {
      presets.push_back(resolution->itemText(row));
    }
    CHECK(presets == (QStringList{QStringLiteral("72"), QStringLiteral("100"), QStringLiteral("150"),
                                  QStringLiteral("200"), QStringLiteral("300"), QStringLiteral("400"),
                                  QStringLiteral("600")}));
    // A typed value between the presets sizes the pages: 100 pt at 144 ppi is 200 px.
    resolution->setCurrentText(QStringLiteral("144"));
    QApplication::processEvents();
    CHECK(size_label->text().contains(QStringLiteral("200 x 200")));
    CHECK(import_button->isEnabled());
    // Below the 12 ppi floor (the validator lets a partial value through): no import.
    resolution->setCurrentText(QStringLiteral("5"));
    QApplication::processEvents();
    CHECK(!import_button->isEnabled());
    CHECK(size_label->text().contains(QStringLiteral("12")));
    resolution->setCurrentIndex(resolution->findText(QStringLiteral("300")));
    QApplication::processEvents();
    CHECK(import_button->isEnabled());

    QStringList modes;
    for (int row = 0; row < color_mode->count(); ++row) {
      modes.push_back(color_mode->itemData(row).toString());
    }
    CHECK(modes == (QStringList{QStringLiteral("rgb"), QStringLiteral("grayscale"), QStringLiteral("cmyk"),
                                QStringLiteral("palette")}));
    CHECK(bit_depth->count() == 3);
    const auto palette_enabled = [color_mode] {
      auto* model = qobject_cast<QStandardItemModel*>(color_mode->model());
      return model != nullptr && model->item(color_mode->findData(QStringLiteral("palette")))->isEnabled();
    };
    mode->setCurrentIndex(mode->findData(QStringLiteral("editable")));
    CHECK(!palette_enabled());
    mode->setCurrentIndex(mode->findData(QStringLiteral("flatten")));
    CHECK(palette_enabled());
    bit_depth->setCurrentIndex(bit_depth->findData(16));
    CHECK(bit_depth->isEnabled());
    color_mode->setCurrentIndex(color_mode->findData(QStringLiteral("palette")));
    CHECK(!bit_depth->isEnabled());
    CHECK(bit_depth->currentData().toInt() == 8);
    // Back to editable: an Indexed choice it cannot honor falls back to RGB.
    mode->setCurrentIndex(mode->findData(QStringLiteral("editable")));
    CHECK(color_mode->currentData().toString() == QStringLiteral("rgb"));
    CHECK(bit_depth->isEnabled());
    CHECK(paper->isChecked());  // Photoshop's paper is white, so this is the default
    *done = true;
    dialog->reject();
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*done);
  patchy::set_deep_editing_override(std::nullopt);
}

// GitHub issue 153: the rendered page lands on white paper by default (transparent when
// asked), and Grayscale (issue 152) makes every pixel gray by the shared weights.
void ui_pdf_import_white_background_and_grayscale_render() {
  const auto path = write_artifact(QStringLiteral("ui_pdf_import_white_gray.pdf"), red_square_pdf_bytes());
  for (const bool paper : {true, false}) {
    for (const bool gray : {false, true}) {
      patchy::ui::PdfImportOptions options;
      options.pages = {0};
      options.resolution_ppi = 72;
      options.white_background = paper;
      options.color_mode = gray ? patchy::ui::PdfImportColorMode::Grayscale : patchy::ui::PdfImportColorMode::Rgb;
      QString error;
      auto result = patchy::ui::load_pdf_document(path, options, QString(), &error);
      CHECK(result.has_value());
      if (!result.has_value()) {
        continue;
      }
      CHECK(result->color_mode == options.color_mode);
      const auto& document = result->document;
      CHECK(document.width() == 100 && document.height() == 100);
      const auto& layer = document.layers().front();
      // The square covers x 10..50, y 50..90 in document pixels (PDF y is up).
      const auto* inside = layer.pixels().pixel(30, 70);
      const auto* outside = layer.pixels().pixel(80, 20);
      const int channels = layer.pixels().format().channels;
      if (gray) {
        const int value = patchy::gray_from_rgb(255, 0, 0);
        CHECK(std::abs(inside[0] - value) <= 1 && inside[0] == inside[1] && inside[1] == inside[2]);
      } else {
        CHECK(inside[0] > 240 && inside[1] < 15 && inside[2] < 15);
      }
      if (paper) {
        // Opaque paper: the page is RGB with no alpha to keep.
        CHECK(channels == 3 || outside[3] == 255);
        CHECK(outside[0] == 255 && outside[1] == 255 && outside[2] == 255);
      } else {
        CHECK(channels == 4);
        if (channels == 4) {
          CHECK(outside[3] == 0);
        }
      }
    }
  }
}

// One page per document, driven through the dialog: Reverse Order (issue 152) opens the
// LAST selected page first, Indexed converts every page to palette editing, and a deep
// choice converts the depth.
void ui_pdf_import_reverse_order_palette_and_depth() {
  const auto path = write_artifact(QStringLiteral("ui_pdf_import_reverse_modes.pdf"), red_square_pdf_bytes());
  patchy::set_deep_editing_override(true);
  for (const char* chosen_mode : {"palette", "rgb16"}) {
    patchy::ui::MainWindow window;
    show_window(window);
    const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
    auto clicked = std::make_shared<bool>(false);
    auto step = std::make_shared<std::function<bool()>>();
    const QString wanted = QString::fromLatin1(chosen_mode);
    *step = [clicked, wanted] {
      auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
      if (dialog == nullptr) {
        return false;
      }
      auto* pages = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
      auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
      auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
      auto* pages_target = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportPagesTargetCombo"));
      auto* color_mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportColorModeCombo"));
      auto* bit_depth = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportBitDepthCombo"));
      auto* reverse = dialog->findChild<QCheckBox*>(QStringLiteral("pdfImportReverseOrderCheck"));
      auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
      if (pages == nullptr || resolution == nullptr || mode == nullptr || pages_target == nullptr ||
          color_mode == nullptr || bit_depth == nullptr || reverse == nullptr || import_button == nullptr) {
        return false;
      }
      mode->setCurrentIndex(mode->findData(QStringLiteral("flatten")));
      pages_target->setCurrentIndex(pages_target->findData(QStringLiteral("documents")));
      resolution->setCurrentText(QStringLiteral("72"));
      if (wanted == QStringLiteral("palette")) {
        color_mode->setCurrentIndex(color_mode->findData(QStringLiteral("palette")));
      } else {
        color_mode->setCurrentIndex(color_mode->findData(QStringLiteral("rgb")));
        bit_depth->setCurrentIndex(bit_depth->findData(16));
      }
      reverse->setChecked(true);
      pages->selectAll();
      import_button->click();
      *clicked = true;
      return true;
    };
    drive_modal_dialog(step);
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    CHECK(*clicked);
    CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 2);
    if (patchy::ui::MainWindowTestAccess::session_count(window) != sessions_before + 2) {
      continue;
    }
    auto& first = patchy::ui::MainWindowTestAccess::session_document(window, sessions_before);
    auto& second = patchy::ui::MainWindowTestAccess::session_document(window, sessions_before + 1);
    // Reversed: the 60 pt page 2 comes first.
    CHECK(first.width() == 60 && second.width() == 100);
    CHECK(patchy::ui::MainWindowTestAccess::session_title(window, sessions_before).endsWith(QStringLiteral("Page 2")));
    for (const auto* document : {&first, &second}) {
      if (wanted == QStringLiteral("palette")) {
        CHECK(document->palette_editing().has_value());
        CHECK(document->color_state().bit_depth == patchy::BitDepth::UInt8);
      } else {
        CHECK(!document->palette_editing().has_value());
        CHECK(document->color_state().bit_depth == patchy::BitDepth::UInt16);
      }
    }
    // The choices persist for the next import.
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("imports/pdfReverseOrder")).toBool());
    CHECK(settings.value(QStringLiteral("imports/pdfColorMode")).toString() ==
          (wanted == QStringLiteral("palette") ? QStringLiteral("palette") : QStringLiteral("rgb")));
  }
  auto settings = patchy::ui::app_settings();
  settings.setValue(QStringLiteral("imports/pdfReverseOrder"), false);
  settings.setValue(QStringLiteral("imports/pdfColorMode"), QStringLiteral("rgb"));
  settings.setValue(QStringLiteral("imports/pdfBitDepth"), 8);
  patchy::set_deep_editing_override(std::nullopt);
}

// CMYK Color opens every page in CMYK Proof, or says why it could not (no CMYK profile).
void ui_pdf_import_cmyk_mode_opens_cmyk_proof() {
  const auto path = write_artifact(QStringLiteral("ui_pdf_import_cmyk.pdf"), red_square_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* pages = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* pages_target = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportPagesTargetCombo"));
    auto* color_mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportColorModeCombo"));
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (pages == nullptr || mode == nullptr || pages_target == nullptr || color_mode == nullptr ||
        resolution == nullptr || import_button == nullptr) {
      return false;
    }
    mode->setCurrentIndex(mode->findData(QStringLiteral("flatten")));
    pages_target->setCurrentIndex(pages_target->findData(QStringLiteral("documents")));
    color_mode->setCurrentIndex(color_mode->findData(QStringLiteral("cmyk")));
    resolution->setCurrentText(QStringLiteral("72"));
    pages->selectAll();
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*clicked);
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 2);
  if (patchy::ui::MainWindowTestAccess::session_count(window) != sessions_before + 2) {
    return;
  }
  const bool first = patchy::ui::MainWindowTestAccess::session_cmyk_proof(window, sessions_before);
  const bool second = patchy::ui::MainWindowTestAccess::session_cmyk_proof(window, sessions_before + 1);
  // Both or neither: whether a CMYK profile is configured is the machine's business, but
  // the pages must agree, and without one the status bar says why.
  CHECK(first == second);
  if (!first) {
    CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("CMYK")));
  }
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfColorMode"), QStringLiteral("rgb"));
}

// GitHub issue 155: text written "1 Tf 18 0 0 18 Tm" (a 1-unit font scaled by the text
// matrix, which most producers write) used to import as a 1 px font under a 75x
// transform, so the 3 px caret and the editor box came out 75 times too big. The em's
// height is folded into the size now: 18 pt at 300 ppi is a 75 px font with no scale left.
void ui_pdf_import_text_size_is_folded_and_caret_stays_small() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const std::string content = "BT /F1 1 Tf 18 0 0 18 20 60 Tm 1 0 0 rg (Hello PDF) Tj ET";
  const auto path = write_artifact(
      QStringLiteral("ui_pdf_import_text_fold.pdf"),
      pdf_from_objects({
          "<</Type/Catalog/Pages 2 0 R>>",
          "<</Type/Pages/Kids[3 0 R]/Count 1>>",
          "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>",
          "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream",
          "<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>",
      }));
  patchy::ui::MainWindow window;
  show_window(window);
  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* paper = dialog->findChild<QCheckBox*>(QStringLiteral("pdfImportWhiteBackgroundCheck"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (mode == nullptr || resolution == nullptr || paper == nullptr || import_button == nullptr) {
      return false;
    }
    mode->setCurrentIndex(mode->findData(QStringLiteral("editable")));
    resolution->setCurrentText(QStringLiteral("300"));
    paper->setChecked(false);
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  CHECK(*clicked);

  auto& opened = patchy::ui::MainWindowTestAccess::document(window);
  const patchy::Layer* text = nullptr;
  for (const auto& layer : std::as_const(opened).layers()) {
    if (patchy::layer_is_text(layer)) {
      text = &layer;
    }
  }
  CHECK(text != nullptr);
  if (text == nullptr) {
    return;
  }
  const int size = std::atoi(text->metadata().at(patchy::kLayerMetadataTextSize).c_str());
  CHECK(std::abs(size - 75) <= 1);
  const auto transform = patchy::parse_layer_affine_transform(text->metadata().at(patchy::kLayerMetadataTextTransform));
  CHECK(transform.has_value());
  if (transform.has_value()) {
    // Only rotation, aspect, flip and position may remain: no scale worth the name.
    CHECK(std::abs(std::hypot((*transform)[2], (*transform)[3]) - 1.0) < 0.05);
    CHECK(std::abs(std::hypot((*transform)[0], (*transform)[1]) - 1.0) < 0.45);
  }
  // The glyphs are 18 pt tall at 300 ppi: about 75 px of em, so the ink sits well inside 120.
  CHECK(text->bounds().height > 30 && text->bounds().height < 120);

  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  patchy::ui::MainWindowTestAccess::edit_text_layer(window, text->id());
  process_events_for(150);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  // Whichever editor draws it (the plain inline one or the transformed overlay), the caret
  // is a thin bar about one line tall at 100% zoom.
  double caret_width = editor->cursorWidth();
  double caret_height = editor->cursorRect().height();
  auto* overlay = canvas->findChild<QWidget*>(QStringLiteral("transformedTextEditOverlay"));
  if (editor->property("patchy.transformedPreviewOverlayActive").toBool() && overlay != nullptr) {
    overlay->repaint();
    const auto polygon = overlay->property("patchy.transformedTextCaretPolygon").toList();
    CHECK(polygon.size() == 4);
    if (polygon.size() == 4) {
      const QPointF a = polygon[0].toPointF();
      const QPointF b = polygon[1].toPointF();
      const QPointF d = polygon[3].toPointF();
      const double first_edge = std::hypot(b.x() - a.x(), b.y() - a.y());
      const double second_edge = std::hypot(d.x() - a.x(), d.y() - a.y());
      caret_width = std::min(first_edge, second_edge);
      caret_height = std::max(first_edge, second_edge);
    }
  }
  CHECK(caret_width <= 6.0);
  CHECK(caret_height >= 40.0 && caret_height <= 200.0);
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  process_events_for(100);
}


// Drives the Import PDF dialog: every page selected, the given mode ("editable" or
// "flatten"), "Pages become" target and paper choice, at 72 ppi.
bool import_through_dialog(patchy::ui::MainWindow& window, const QString& path, const QString& mode_id,
                           const QString& target_id, bool paper) {
  auto clicked = std::make_shared<bool>(false);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [clicked, mode_id, target_id, paper] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* pages = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* pages_target = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportPagesTargetCombo"));
    auto* paper_check = dialog->findChild<QCheckBox*>(QStringLiteral("pdfImportWhiteBackgroundCheck"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (pages == nullptr || resolution == nullptr || mode == nullptr || pages_target == nullptr ||
        paper_check == nullptr || import_button == nullptr) {
      return false;
    }
    mode->setCurrentIndex(mode->findData(mode_id));
    CHECK(pages_target->findData(target_id) >= 0);
    pages_target->setCurrentIndex(pages_target->findData(target_id));
    resolution->setCurrentText(QStringLiteral("72"));
    paper_check->setChecked(paper);
    pages->selectAll();
    import_button->click();
    *clicked = true;
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  return *clicked;
}

void reset_pdf_pages_target() {
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfPagesTarget"), QStringLiteral("documents"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfWhiteBackground"), true);
}

// GitHub issue 154: "Layers in one document" with editable pages used to import page 1
// only. Every page comes in now, each in its own Pass Through "Page N" folder on a canvas
// of the largest page, page 1 on top and the only one visible.
void ui_pdf_import_editable_pages_become_folders() {
  const auto path = write_artifact(QStringLiteral("ui_pdf_import_editable_folders.pdf"), red_square_pdf_bytes());
  for (const bool paper : {false, true}) {
    patchy::ui::MainWindow window;
    show_window(window);
    const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
    CHECK(import_through_dialog(window, path, QStringLiteral("editable"), QStringLiteral("layers"), paper));
    CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
    const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(document.width() == 100 && document.height() == 100);
    CHECK(!patchy::document_has_artboards(document));
    CHECK(document.layers().size() == 2);
    if (document.layers().size() != 2) {
      continue;
    }
    const auto& page1 = document.layers()[1];
    const auto& page2 = document.layers()[0];
    CHECK(page1.name() == "Page 1" && page2.name() == "Page 2");
    CHECK(page1.kind() == patchy::LayerKind::Group && page2.kind() == patchy::LayerKind::Group);
    CHECK(page1.blend_mode() == patchy::BlendMode::PassThrough);
    CHECK(page1.visible() && !page2.visible());
    // Each page keeps its editable shape (and, with paper, its white Background under it).
    for (const auto* page : {&page1, &page2}) {
      CHECK(page->children().size() == (paper ? 2U : 1U));
      CHECK(!page->children().empty() && patchy::layer_is_vector_shape(page->children().back()));
      if (paper && !page->children().empty()) {
        CHECK(page->children().front().name() == "Background");
      }
    }
    // Page 2's 60 pt square sits at the top-left like a raster "Page 2" layer would.
    const auto bounds = patchy::layer_render_bounds(page2.children().back());
    CHECK(bounds.x == 0 && bounds.y == 0 && bounds.width == 60 && bounds.height == 60);
  }
  reset_pdf_pages_target();
}

QByteArray gray_scan_jpeg(int width, int height, int seed);

// Flattened scans imported as page folders go back out page by page: Multi-Page PDF by
// groups (hidden pages included) and Save As with Pages = groups both write every page at
// its own size, in page order, with each scan's original JPEG bytes; Pages = canvas writes
// one page.
void ui_pdf_import_page_folders_export_back_by_groups() {
  const auto first = gray_scan_jpeg(72, 72, 0);
  const auto second = gray_scan_jpeg(48, 96, 40);
  std::vector<std::string> objects = {"<</Type/Catalog/Pages 2 0 R>>",
                                      "<</Type/Pages/Kids[3 0 R 6 0 R]/Count 2>>"};
  const std::array<std::pair<int, int>, 2> sizes{{{72, 72}, {48, 96}}};
  for (std::size_t index = 0; index < 2; ++index) {
    const auto& jpeg = index == 0 ? first : second;
    const auto [width, height] = sizes[index];
    const int base = static_cast<int>(objects.size()) + 1;
    const std::string content =
        "q " + std::to_string(width) + " 0 0 " + std::to_string(height) + " 0 0 cm /Im0 Do Q";
    objects.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 " + std::to_string(width) + " " +
                      std::to_string(height) + "]/Resources<</XObject<</Im0 " + std::to_string(base + 2) +
                      " 0 R>>>>/Contents " + std::to_string(base + 1) + " 0 R>>");
    objects.push_back("<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
    objects.push_back("<</Type/XObject/Subtype/Image/Width " + std::to_string(width) + "/Height " +
                      std::to_string(height) + "/ColorSpace/DeviceGray/BitsPerComponent 8/Filter/DCTDecode/Length " +
                      std::to_string(jpeg.size()) + ">>\nstream\n" + jpeg.toStdString() + "\nendstream");
  }
  const auto path = write_artifact(QStringLiteral("ui_pdf_import_page_folders.pdf"), pdf_from_objects(objects));
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(import_through_dialog(window, path, QStringLiteral("flatten"), QStringLiteral("layers"), true));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(document.layers().size() == 2);
  CHECK(document.width() == 72 && document.height() == 96);
  CHECK(patchy::ui::document_has_imported_page_groups(std::as_const(document)));
  CHECK(std::as_const(document).metadata().pdf_page_source_pages.size() == 2);

  const auto streams_of = [](const QString& written, std::vector<QSizeF>* sizes_out) {
    std::vector<QByteArray> streams;
    QPdfDocument reader;
    if (reader.load(written) == QPdfDocument::Error::None && sizes_out != nullptr) {
      for (int page = 0; page < reader.pageCount(); ++page) {
        sizes_out->push_back(reader.pagePointSize(page));
      }
    }
    reader.close();
    const QByteArray bytes = read_file_bytes(written);
    auto file = patchy::pdf::File::open(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), nullptr);
    if (!file.has_value()) {
      return streams;
    }
    for (const auto& page : file->pages()) {
      const auto* xobjects = file->get(page.resources, "XObject").dictionary();
      if (xobjects == nullptr || xobjects->empty()) {
        streams.emplace_back();
        continue;
      }
      const auto data = file->stream_data(file->resolve(xobjects->begin()->second));
      streams.emplace_back(reinterpret_cast<const char*>(data.data.data()), static_cast<qsizetype>(data.data.size()));
    }
    return streams;
  };

  // Multi-Page PDF by groups: page 2 is hidden and is still a page.
  const auto pages = patchy::ui::documents_for_top_level_groups(std::as_const(document), false);
  CHECK(pages.size() == 2);
  std::vector<const patchy::Document*> pointers;
  for (const auto& page : pages) {
    pointers.push_back(&page);
  }
  const auto groups_path = QStringLiteral("test-artifacts/ui_pdf_import_page_folders_groups.pdf");
  QFile::remove(groups_path);
  patchy::ui::PdfExportOptions options;
  CHECK(patchy::ui::write_multipage_pdf_file(pointers, groups_path, options));
  std::vector<QSizeF> group_sizes;
  const auto group_streams = streams_of(groups_path, &group_sizes);
  CHECK(group_streams.size() == 2 && group_streams[0] == first && group_streams[1] == second);
  CHECK(group_sizes.size() == 2 && std::abs(group_sizes[0].width() - 72.0) < 0.5 &&
        std::abs(group_sizes[1].height() - 96.0) < 0.5);

  // Save As with Pages = groups does the same; canvas writes one page.
  options.pages = patchy::ui::PdfPageSource::Groups;
  const auto save_path = QStringLiteral("test-artifacts/ui_pdf_import_page_folders_save.pdf");
  QFile::remove(save_path);
  patchy::ui::write_pdf_document_file(std::as_const(document), save_path, options);
  std::vector<QSizeF> save_sizes;
  const auto save_streams = streams_of(save_path, &save_sizes);
  CHECK(save_streams.size() == 2 && save_streams[0] == first && save_streams[1] == second);
  options.pages = patchy::ui::PdfPageSource::Canvas;
  const auto canvas_path = QStringLiteral("test-artifacts/ui_pdf_import_page_folders_canvas.pdf");
  QFile::remove(canvas_path);
  patchy::ui::write_pdf_document_file(std::as_const(document), canvas_path, options);
  std::vector<QSizeF> canvas_sizes;
  static_cast<void>(streams_of(canvas_path, &canvas_sizes));
  CHECK(canvas_sizes.size() == 1);
  reset_pdf_pages_target();
}

// "Artboards in one document": one artboard per page, page 1 the topmost and leftmost,
// with the page's paper as the artboard background; exporting it again writes the same
// two page sizes (issue 19's custom sizes), and the gap between artboards stays empty.
void ui_pdf_import_pages_become_artboards_and_export_back() {
  const auto path = write_artifact(QStringLiteral("ui_pdf_import_artboards.pdf"), red_square_pdf_bytes());
  for (const char* mode : {"flatten", "editable"}) {
    patchy::ui::MainWindow window;
    show_window(window);
    const auto sessions_before = patchy::ui::MainWindowTestAccess::session_count(window);
    CHECK(import_through_dialog(window, path, QString::fromLatin1(mode), QStringLiteral("artboards"), true));
    CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == sessions_before + 1);
    auto& document = patchy::ui::MainWindowTestAccess::document(window);
    const auto artboards = patchy::document_artboards(std::as_const(document));
    CHECK(artboards.size() == 2);
    if (artboards.size() != 2) {
      continue;
    }
    CHECK(artboards[0]->name() == "Page 1" && artboards[1]->name() == "Page 2");
    CHECK((artboards[0]->artboard()->rect == patchy::Rect{0, 0, 100, 100}));
    CHECK((artboards[1]->artboard()->rect == patchy::Rect{200, 0, 60, 60}));
    CHECK(artboards[0]->artboard()->background == patchy::ArtboardBackground::White);
    CHECK(document.width() == 300 && document.height() == 100);
    const QImage composite = patchy::ui::flat_export_qimage(std::as_const(document), true);
    CHECK(composite.pixelColor(30, 70).red() > 240 && composite.pixelColor(30, 70).green() < 15);
    CHECK(composite.pixelColor(80, 20) == QColor(255, 255, 255));  // page 1's paper
    CHECK(composite.pixelColor(230, 30).blue() > 240 && composite.pixelColor(230, 30).red() < 15);
    CHECK(composite.pixelColor(150, 50).alpha() == 0);  // the gap is nobody's paper

    const auto out_path = QStringLiteral("test-artifacts/ui_pdf_import_artboards_out_%1.pdf").arg(QLatin1String(mode));
    QFile::remove(out_path);
    patchy::ui::PdfExportOptions options;
    patchy::ui::write_pdf_document_file(std::as_const(document), out_path, options);
    {
      QPdfDocument reader;  // closed before the next write replaces nothing it holds
      CHECK(reader.load(out_path) == QPdfDocument::Error::None);
      CHECK(reader.pageCount() == 2);
      if (reader.pageCount() == 2) {
        CHECK(std::abs(reader.pagePointSize(0).width() - 100.0) < 0.5);
        CHECK(std::abs(reader.pagePointSize(1).width() - 60.0) < 0.5);
        const QImage page1 = reader.render(0, QSize(100, 100));
        CHECK(color_close(page1.pixelColor(80, 20), QColor(255, 255, 255), 6));
        CHECK(page1.pixelColor(30, 70).red() > 200 && page1.pixelColor(30, 70).green() < 60);
      }
    }
    // Whole canvas when asked: one page of the canvas size.
    options.pages = patchy::ui::PdfPageSource::Canvas;
    const auto whole_path = QStringLiteral("test-artifacts/ui_pdf_import_artboards_whole_%1.pdf").arg(QLatin1String(mode));
    QFile::remove(whole_path);
    patchy::ui::write_pdf_document_file(std::as_const(document), whole_path, options);
    QPdfDocument whole;
    CHECK(whole.load(whole_path) == QPdfDocument::Error::None);
    CHECK(whole.pageCount() == 1);
    CHECK(whole.pageCount() != 1 || std::abs(whole.pagePointSize(0).width() - 300.0) < 0.5);
  }
  reset_pdf_pages_target();
}

QByteArray gray_scan_jpeg(int width, int height, int seed) {
  QImage image(width, height, QImage::Format_Grayscale8);
  for (int y = 0; y < height; ++y) {
    auto* row = image.scanLine(y);
    for (int x = 0; x < width; ++x) {
      row[x] = static_cast<uchar>(((x * 3 + seed) ^ (y * 5)) & 0xFF);
    }
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  QImageWriter writer(&buffer, "JPEG");
  writer.setQuality(80);
  CHECK(writer.write(image));
  return bytes;
}

// Two scanned pages (one gray JPEG over each whole page) merged into artboards: each
// untouched artboard page re-exports its original JPEG bytes (per-artboard pass-through),
// and an edit on one artboard re-encodes that page only.
void ui_pdf_import_artboard_pages_pass_through() {
  const auto first = gray_scan_jpeg(72, 72, 0);
  const auto second = gray_scan_jpeg(72, 72, 40);
  std::vector<std::string> objects = {"<</Type/Catalog/Pages 2 0 R>>",
                                      "<</Type/Pages/Kids[3 0 R 6 0 R]/Count 2>>"};
  for (const auto* jpeg : {&first, &second}) {
    const int base = static_cast<int>(objects.size()) + 1;
    const std::string content = "q 72 0 0 72 0 0 cm /Im0 Do Q";
    objects.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 72 72]/Resources<</XObject<</Im0 " +
                      std::to_string(base + 2) + " 0 R>>>>/Contents " + std::to_string(base + 1) + " 0 R>>");
    objects.push_back("<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
    objects.push_back("<</Type/XObject/Subtype/Image/Width 72/Height 72/ColorSpace/DeviceGray/BitsPerComponent 8"
                      "/Filter/DCTDecode/Length " +
                      std::to_string(jpeg->size()) + ">>\nstream\n" + jpeg->toStdString() + "\nendstream");
  }
  const auto path = write_artifact(QStringLiteral("ui_pdf_import_artboard_scans.pdf"), pdf_from_objects(objects));
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(import_through_dialog(window, path, QStringLiteral("flatten"), QStringLiteral("artboards"), true));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto artboards = patchy::document_artboards(std::as_const(document));
  CHECK(artboards.size() == 2);
  CHECK(std::as_const(document).metadata().pdf_page_source_pages.size() == 2);
  if (artboards.size() != 2) {
    reset_pdf_pages_target();
    return;
  }
  const auto streams_of = [](const QString& written) {
    const QByteArray bytes = read_file_bytes(written);
    std::vector<QByteArray> streams;
    auto file = patchy::pdf::File::open(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), nullptr);
    if (!file.has_value()) {
      return streams;
    }
    for (const auto& page : file->pages()) {
      const auto* xobjects = file->get(page.resources, "XObject").dictionary();
      if (xobjects == nullptr || xobjects->empty()) {
        streams.emplace_back();
        continue;
      }
      const auto data = file->stream_data(file->resolve(xobjects->begin()->second));
      streams.emplace_back(reinterpret_cast<const char*>(data.data.data()), static_cast<qsizetype>(data.data.size()));
    }
    return streams;
  };
  patchy::ui::PdfExportOptions options;
  CHECK(options.keep_original_image_data && options.pages == patchy::ui::PdfPageSource::Artboards);
  const auto out_path = QStringLiteral("test-artifacts/ui_pdf_import_artboard_scans_out.pdf");
  QFile::remove(out_path);
  patchy::ui::write_pdf_document_file(std::as_const(document), out_path, options);
  auto streams = streams_of(out_path);
  CHECK(streams.size() == 2);
  CHECK(streams.size() == 2 && streams[0] == first && streams[1] == second);

  // Paint over page 2's artboard: that page re-encodes, page 1 still passes through.
  auto* page2 = document.find_layer(artboards[1]->id());
  CHECK(page2 != nullptr && !page2->children().empty());
  if (page2 != nullptr && !page2->children().empty()) {
    auto& scan = page2->children().front();
    auto row = scan.pixels().row(5);
    row[0] = static_cast<std::uint8_t>(row[0] ^ 0xFFU);
    row[1] = row[0];
    row[2] = row[0];
  }
  QFile::remove(out_path);
  patchy::ui::write_pdf_document_file(std::as_const(document), out_path, options);
  streams = streams_of(out_path);
  CHECK(streams.size() == 2);
  CHECK(streams.size() == 2 && streams[0] == first && streams[1] != second);
  reset_pdf_pages_target();
}

// The real scanned document (85 pages of JPEG 2000; the first 5 here) round-trips both
// ways: imported as page folders and exported by groups, and imported as artboards and
// exported by artboards, every page comes back with its original image bytes, in order.
// Copy Merged on a grid of scanned pages as artboards (Seth's report: 20 s frozen before
// the empty-area warning). It composites only the selected rect: blank space between the
// pages refuses at once, and a rect on a page copies that page's pixels.
void ui_pdf_local_scans_copy_merged_renders_only_the_selection_if_available() {
  const auto fixture_path = patchy::test::local_format_fixture_path("pdf", "C2_Kyoto_House_Plans_Compressed.pdf");
  patchy::test::skip_unless_exists(fixture_path);
  const auto fixture = QString::fromStdString(fixture_path.string());
  patchy::ui::MainWindow window;
  show_window(window);
  auto step = std::make_shared<std::function<bool()>>();
  *step = [] {
    auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
    if (dialog == nullptr) {
      return false;
    }
    auto* pages = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* pages_target = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportPagesTargetCombo"));
    auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    if (pages == nullptr || resolution == nullptr || mode == nullptr || pages_target == nullptr ||
        import_button == nullptr) {
      return false;
    }
    mode->setCurrentIndex(mode->findData(QStringLiteral("editable")));
    pages_target->setCurrentIndex(pages_target->findData(QStringLiteral("artboards")));
    resolution->setCurrentText(QStringLiteral("150"));
    pages->clearSelection();
    for (int row = 0; row < 15 && row < pages->count(); ++row) {
      pages->item(row)->setSelected(true);
    }
    import_button->click();
    return true;
  };
  drive_modal_dialog(step);
  patchy::ui::MainWindowTestAccess::open_document_path(window, fixture);
  reset_pdf_pages_target();
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto boards = patchy::document_artboards(document);
  CHECK(boards.size() == 15U);
  if (boards.size() < 2U) {
    return;
  }
  auto* canvas = require_canvas(window);
  const auto select = [&](QRect rect) {
    patchy::PixelBuffer coverage(document.width(), document.height(), patchy::PixelFormat::gray8());
    coverage.clear(0);
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
      std::memset(coverage.pixel(rect.left(), y), 255, static_cast<std::size_t>(rect.width()));
    }
    canvas->apply_grayscale_to_selection(coverage);
    QApplication::processEvents();
  };
  // The gutter between the first two pages: nothing composites there.
  const auto first = boards[0]->artboard()->rect;
  int boxes = 0;
  QTimer answer;
  answer.setInterval(20);
  QObject::connect(&answer, &QTimer::timeout, [&boxes] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("copyEmptySelectionMessageBox")));
    if (box != nullptr && box->isVisible()) {
      ++boxes;
      box->button(QMessageBox::Ok)->click();
    }
  });
  answer.start();
  select(QRect(first.x + first.width + 20, first.y + 100, 40, 200));
  const auto started = std::chrono::steady_clock::now();
  require_action(window, "editCopyMergedAction")->trigger();
  QApplication::processEvents();
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
  CHECK(boxes == 1);
  CHECK(elapsed_ms < 2000);
  // A rect on page 1 copies its pixels.
  select(QRect(first.x + 200, first.y + 200, 300, 200));
  QApplication::clipboard()->clear();
  require_action(window, "editCopyMergedAction")->trigger();
  QApplication::processEvents();
  CHECK(boxes == 1);
  CHECK(QApplication::clipboard()->image().size() == QSize(300, 200));
  QApplication::clipboard()->clear();
}

void ui_pdf_local_scans_round_trip_as_folders_and_artboards_if_available() {
  const auto fixture_path = patchy::test::local_format_fixture_path("pdf", "C2_Kyoto_House_Plans_Compressed.pdf");
  patchy::test::skip_unless_exists(fixture_path);
  const auto fixture = QString::fromStdString(fixture_path.string());
  const auto source_streams = [](const QString& written, int limit) {
    std::vector<QByteArray> streams;
    const QByteArray bytes = read_file_bytes(written);
    auto file = patchy::pdf::File::open(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), nullptr);
    if (!file.has_value()) {
      return streams;
    }
    for (const auto& page : file->pages()) {
      if (static_cast<int>(streams.size()) == limit) {
        break;
      }
      const auto* xobjects = file->get(page.resources, "XObject").dictionary();
      if (xobjects == nullptr || xobjects->empty()) {
        streams.emplace_back();
        continue;
      }
      const auto data = file->stream_data(file->resolve(xobjects->begin()->second));
      streams.emplace_back(reinterpret_cast<const char*>(data.data.data()), static_cast<qsizetype>(data.data.size()));
    }
    return streams;
  };
  const auto originals = source_streams(fixture, 5);
  CHECK(originals.size() == 5);
  for (const char* target : {"layers", "artboards"}) {
    patchy::ui::MainWindow window;
    show_window(window);
    auto clicked = std::make_shared<bool>(false);
    auto step = std::make_shared<std::function<bool()>>();
    const QString target_id = QString::fromLatin1(target);
    *step = [clicked, target_id] {
      auto* dialog = find_top_level_dialog(QStringLiteral("pdfImportDialog"));
      if (dialog == nullptr) {
        return false;
      }
      auto* pages = dialog->findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
      auto* resolution = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
      auto* mode = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
      auto* pages_target = dialog->findChild<QComboBox*>(QStringLiteral("pdfImportPagesTargetCombo"));
      auto* import_button = dialog->findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
      if (pages == nullptr || resolution == nullptr || mode == nullptr || pages_target == nullptr ||
          import_button == nullptr) {
        return false;
      }
      mode->setCurrentIndex(mode->findData(QStringLiteral("flatten")));
      pages_target->setCurrentIndex(pages_target->findData(target_id));
      resolution->setCurrentText(QStringLiteral("72"));
      pages->clearSelection();
      for (int row = 0; row < 5 && row < pages->count(); ++row) {
        pages->item(row)->setSelected(true);
      }
      import_button->click();
      *clicked = true;
      return true;
    };
    drive_modal_dialog(step);
    patchy::ui::MainWindowTestAccess::open_document_path(window, fixture);
    CHECK(*clicked);
    const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(document.layers().size() == 5);
    CHECK(document.metadata().pdf_page_source_pages.size() == 5);
    patchy::ui::PdfExportOptions options;
    options.pages = target_id == QStringLiteral("layers") ? patchy::ui::PdfPageSource::Groups
                                                          : patchy::ui::PdfPageSource::Artboards;
    const auto out = QStringLiteral("test-artifacts/ui_pdf_local_scans_%1.pdf").arg(target_id);
    QFile::remove(out);
    patchy::ui::write_pdf_document_file(document, out, options);
    const auto written = source_streams(out, 99);
    CHECK(written.size() == 5);
    CHECK(written == originals);
  }
  reset_pdf_pages_target();
}

#endif  // PATCHY_HAVE_QT_PDF

}  // namespace

std::vector<patchy::test::TestCase> import_print_resolution_tests_part4() {
  return {
#if defined(PATCHY_HAVE_QT_PDF)
      {"ui_pdf_import_dialog_offers_resolution_presets_mode_and_depth",
       ui_pdf_import_dialog_offers_resolution_presets_mode_and_depth},
      {"ui_pdf_import_white_background_and_grayscale_render", ui_pdf_import_white_background_and_grayscale_render},
      {"ui_pdf_import_reverse_order_palette_and_depth", ui_pdf_import_reverse_order_palette_and_depth},
      {"ui_pdf_import_cmyk_mode_opens_cmyk_proof", ui_pdf_import_cmyk_mode_opens_cmyk_proof},
      {"ui_pdf_import_text_size_is_folded_and_caret_stays_small",
       ui_pdf_import_text_size_is_folded_and_caret_stays_small},
      {"ui_pdf_import_editable_pages_become_folders", ui_pdf_import_editable_pages_become_folders},
      {"ui_pdf_import_pages_become_artboards_and_export_back", ui_pdf_import_pages_become_artboards_and_export_back},
      {"ui_pdf_import_artboard_pages_pass_through", ui_pdf_import_artboard_pages_pass_through},
      {"ui_pdf_import_page_folders_export_back_by_groups", ui_pdf_import_page_folders_export_back_by_groups},
      {"ui_pdf_local_scans_round_trip_as_folders_and_artboards_if_available",
       ui_pdf_local_scans_round_trip_as_folders_and_artboards_if_available},
      {"ui_pdf_local_scans_copy_merged_renders_only_the_selection_if_available",
       ui_pdf_local_scans_copy_merged_renders_only_the_selection_if_available},
#endif
  };
}
