#include <QStatusBar>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QSignalSpy>
#include <QTest>
#include <QWidgetAction>
#include "ui/canvas_widget.hpp"
#include "formats/acv_curves_io.hpp"
#include "formats/bmp_document_io.hpp"
#include "formats/ico_document_io.hpp"
#include "formats/tga_document_io.hpp"
#include <QByteArray>
#include <QIODevice>
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
// File > Open Folder (every image in a folder as its own tab, also a dropped or
// command-line directory), File > Export > Documents to Folder (the checked open
// documents as numbered image files), the shared document-order list's Auto
// Sort / Reverse buttons, and the shape of the File > Export submenu itself
// (docs/import.md, docs/pdf.md, docs/ui-conventions.md).
#include "core/document.hpp"
#include "formats/aseprite_document_io.hpp"
#include "formats/pdf_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/document_order_list.hpp"
#include "ui/export_documents_folder_dialog.hpp"
#include "ui/image_document_io.hpp"
#include "ui/image_sequence_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QKeySequence>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

using patchy::test::ui::accept_save_file_dialog_then;
using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::require_action;
using patchy::test::ui::save_widget_artifact;
using patchy::test::ui::SettingsValueRestorer;
using patchy::test::ui::show_window;

namespace {

using patchy::ui::MainWindowTestAccess;

void write_solid_image(const QString& path, int width, int height, QColor color) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(color);
  CHECK(image.save(path));
}

void write_text_file(const QString& path) {
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  file.write("not an image");
}

// page-1.jpg (12x8), page-2.png (8x8), page-10.png (16x8): natural order is
// 1, 2, 10, which a plain string sort gets wrong (10 before 2). Plus a text file
// and a subfolder holding an image, neither of which may open.
void make_page_folder(const QTemporaryDir& temp) {
  write_solid_image(temp.filePath(QStringLiteral("page-10.png")), 16, 8, QColor(255, 0, 0));
  write_solid_image(temp.filePath(QStringLiteral("page-2.png")), 8, 8, QColor(0, 255, 0));
  write_solid_image(temp.filePath(QStringLiteral("page-1.jpg")), 12, 8, QColor(0, 0, 255));
  write_text_file(temp.filePath(QStringLiteral("notes.txt")));
  CHECK(QDir(temp.path()).mkdir(QStringLiteral("nested")));
  write_solid_image(temp.filePath(QStringLiteral("nested/sub.png")), 4, 4, QColor(0, 0, 0));
}

patchy::Document solid_document(int width, int height, QColor color) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(color);
  return patchy::ui::document_from_qimage(image, "Page");
}

// Restores and seeds every exportDocumentsFolder/* key so the dialog starts from
// its defaults regardless of suite order.
struct ExportDocumentsSettingsGuard {
  SettingsValueRestorer folder{QStringLiteral("exportDocumentsFolder/folder")};
  SettingsValueRestorer prefix{QStringLiteral("exportDocumentsFolder/prefix")};
  SettingsValueRestorer start{QStringLiteral("exportDocumentsFolder/start")};
  SettingsValueRestorer padding{QStringLiteral("exportDocumentsFolder/padding")};
  SettingsValueRestorer format{QStringLiteral("exportDocumentsFolder/format")};
  SettingsValueRestorer existing_files{QStringLiteral("exportDocumentsFolder/existingFiles")};

  ExportDocumentsSettingsGuard() {
    auto settings = patchy::ui::app_settings();
    for (const auto* key : {"exportDocumentsFolder/folder", "exportDocumentsFolder/prefix",
                            "exportDocumentsFolder/start", "exportDocumentsFolder/padding",
                            "exportDocumentsFolder/format", "exportDocumentsFolder/existingFiles"}) {
      settings.remove(QLatin1String(key));
    }
  }
};

void click_message_box_when_shown(const QString& object_name, QMessageBox::StandardButton button,
                                  std::shared_ptr<bool> done, int attempts_left = 300) {
  QTimer::singleShot(10, [object_name, button, done, attempts_left] {
    auto* dialog = find_top_level_dialog(object_name);
    if (dialog == nullptr || !dialog->isVisible()) {
      if (attempts_left > 0 && !*done) {
        click_message_box_when_shown(object_name, button, done, attempts_left - 1);
      }
      return;
    }
    auto* box = qobject_cast<QMessageBox*>(dialog);
    CHECK(box != nullptr);
    if (box != nullptr) {
      *done = true;
      box->button(button)->click();
    }
  });
}

void ui_open_folder_opens_images_in_natural_order() {
  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  SettingsValueRestorer last_open_restorer(QStringLiteral("lastOpenDirectory"));
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  make_page_folder(temp);

  patchy::ui::MainWindow window;
  show_window(window);
  const auto before = MainWindowTestAccess::session_count(window);
  const auto* first_document = &MainWindowTestAccess::document(window);

  // The progress dialog is up while the loop pumps events, so a zero-delay timer
  // fires inside it.
  bool saw_progress = false;
  QTimer::singleShot(0, [&saw_progress] {
    auto* dialog = qobject_cast<QProgressDialog*>(find_top_level_dialog(QStringLiteral("openFolderProgressDialog")));
    if (dialog == nullptr) {
      return;
    }
    CHECK(dialog->windowTitle().startsWith(QStringLiteral("Opening ")));
    CHECK(dialog->labelText().contains(QStringLiteral(" of 3")));
    saw_progress = true;
  });
  const int opened = MainWindowTestAccess::open_folder_path(window, temp.path());
  QApplication::processEvents();
  CHECK(opened == 3);
  CHECK(saw_progress);
  CHECK(MainWindowTestAccess::session_count(window) == before + 3);
  // Natural order: page-1 (12 wide), page-2 (8), page-10 (16); the subfolder's
  // image and the text file stayed out.
  CHECK(MainWindowTestAccess::session_document(window, before).width() == 12);
  CHECK(MainWindowTestAccess::session_document(window, before + 1).width() == 8);
  CHECK(MainWindowTestAccess::session_document(window, before + 2).width() == 16);
  CHECK(MainWindowTestAccess::session_title(window, before) == QStringLiteral("page-1.jpg"));
  CHECK(MainWindowTestAccess::session_title(window, before + 1) == QStringLiteral("page-2.png"));
  CHECK(MainWindowTestAccess::session_title(window, before + 2) == QStringLiteral("page-10.png"));
  // The first image is the active document (the rest opened in the background),
  // and every session keeps its file path so Save works.
  CHECK(&MainWindowTestAccess::document(window) == &MainWindowTestAccess::session_document(window, before));
  CHECK(&MainWindowTestAccess::document(window) != first_document);
  CHECK(MainWindowTestAccess::active_session_path(window).endsWith(QStringLiteral("page-1.jpg")));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("3")));
  // The folder, not each image, lands in the recent history.
  const auto recent_folders = patchy::ui::recent_history_settings().value(QStringLiteral("recentFolders")).toStringList();
  CHECK(!recent_folders.isEmpty());
  CHECK(!recent_folders.isEmpty() &&
        QFileInfo(recent_folders.front()).absoluteFilePath() == QFileInfo(temp.path()).absoluteFilePath());
}

void ui_open_folder_empty_reports_status() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  write_text_file(temp.filePath(QStringLiteral("readme.txt")));

  patchy::ui::MainWindow window;
  show_window(window);
  const auto before = MainWindowTestAccess::session_count(window);
  CHECK(MainWindowTestAccess::open_folder_path(window, temp.path()) == 0);
  CHECK(MainWindowTestAccess::session_count(window) == before);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("No supported images")));
  // A path that is not a directory at all is the same no-op.
  CHECK(MainWindowTestAccess::open_folder_path(window, temp.filePath(QStringLiteral("missing"))) == 0);
  CHECK(MainWindowTestAccess::session_count(window) == before);
}

void ui_open_folder_drop_and_cli_expand_directories() {
  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  SettingsValueRestorer last_open_restorer(QStringLiteral("lastOpenDirectory"));
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  make_page_folder(temp);

  patchy::ui::MainWindow window;
  show_window(window);
  const auto before = MainWindowTestAccess::session_count(window);

  // Command line: a directory argument opens its images.
  window.open_command_line_files({temp.path()});
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == before + 3);

  // Drag and drop: a folder URL is accepted by the drag check and opens on the
  // deferred drop, same as a multi-file drop.
  {
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(temp.path())});
    QDragEnterEvent enter(QPoint(50, 50), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    CHECK(MainWindowTestAccess::accept_open_file_drag(window, &enter));
    QDropEvent drop(QPointF(50, 50), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    CHECK(MainWindowTestAccess::open_dropped_files(window, &drop));
    CHECK(drop.isAccepted());
  }
  CHECK(MainWindowTestAccess::session_count(window) == before + 3);  // nothing until the drop returns
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == before + 6);
  CHECK(MainWindowTestAccess::session_title(window, before + 3) == QStringLiteral("page-1.jpg"));
  CHECK(MainWindowTestAccess::session_title(window, before + 5) == QStringLiteral("page-10.png"));
  // A folder with nothing to open is still refused at the drag stage only when it
  // is not a directory; an empty directory reports through the status bar on drop.
  {
    QTemporaryDir empty;
    CHECK(empty.isValid());
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(empty.path())});
    QDropEvent drop(QPointF(50, 50), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    CHECK(MainWindowTestAccess::open_dropped_files(window, &drop));
    QApplication::processEvents();
    CHECK(MainWindowTestAccess::session_count(window) == before + 6);
    CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("No supported images")));
  }
}

void ui_folder_actions_and_commands_registered() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* open_folder = window.findChild<QAction*>(QStringLiteral("fileOpenFolderAction"));
  CHECK(open_folder != nullptr);
  const auto* open_command = window.hotkey_registry().find_command(QStringLiteral("file.open_folder"));
  CHECK(open_command != nullptr);
  CHECK(open_command != nullptr && open_command->action == open_folder);
  auto* export_folder = window.findChild<QAction*>(QStringLiteral("fileExportDocumentsToFolderAction"));
  CHECK(export_folder != nullptr);
  const auto* export_command =
      window.hotkey_registry().find_command(QStringLiteral("file.export_documents_to_folder"));
  CHECK(export_command != nullptr);
  CHECK(export_command != nullptr && export_command->action == export_folder);
}

void ui_export_documents_to_folder_writes_numbered_files() {
  SettingsValueRestorer last_save_restorer(QStringLiteral("lastSaveDirectory"));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  patchy::ui::MainWindow window;
  show_window(window);
  const auto base = MainWindowTestAccess::session_count(window);
  window.add_document_session(solid_document(10, 6, QColor(255, 0, 0)), QStringLiteral("Red"));
  window.add_document_session(solid_document(12, 6, QColor(0, 255, 0)), QStringLiteral("Green"));
  window.add_document_session(solid_document(14, 6, QColor(0, 0, 255)), QStringLiteral("Blue"));
  const auto* active_before = &MainWindowTestAccess::document(window);
  const std::vector<std::int64_t> ids = {MainWindowTestAccess::session_id(window, base),
                                         MainWindowTestAccess::session_id(window, base + 1),
                                         MainWindowTestAccess::session_id(window, base + 2)};
  patchy::ui::ImageSequenceNaming naming;
  naming.prefix = QStringLiteral("doc_");
  naming.start = 1;
  naming.padding = 3;

  const auto written = MainWindowTestAccess::export_document_sessions_to_folder(
      window, ids, temp.path(), QStringLiteral("png"), naming,
      patchy::ui::ExportDocumentsExistingFiles::AddNumbering);
  CHECK(written.has_value());
  CHECK(written.has_value() && written->size() == 3);
  const QStringList expected_names = {QStringLiteral("doc_001.png"), QStringLiteral("doc_002.png"),
                                      QStringLiteral("doc_003.png")};
  const std::vector<QColor> expected_colors = {QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255)};
  for (int index = 0; index < 3; ++index) {
    CHECK(QFileInfo(written->at(index)).fileName() == expected_names[index]);
    const QImage image(written->at(index));
    CHECK(image.width() == 10 + index * 2);
    CHECK(image.height() == 6);
    CHECK(image.pixelColor(image.width() / 2, 3) == expected_colors[static_cast<std::size_t>(index)]);
  }
  // Nothing about the sessions changed: same active document, none marked saved.
  CHECK(&MainWindowTestAccess::document(window) == active_before);
  CHECK(MainWindowTestAccess::active_session_path(window).isEmpty());

  // Add mode continues after the files already there.
  const auto added = MainWindowTestAccess::export_document_sessions_to_folder(
      window, ids, temp.path(), QStringLiteral("png"), naming,
      patchy::ui::ExportDocumentsExistingFiles::AddNumbering);
  CHECK(added.has_value());
  CHECK(added.has_value() && QFileInfo(added->front()).fileName() == QStringLiteral("doc_004.png"));
  CHECK(added.has_value() && QFileInfo(added->back()).fileName() == QStringLiteral("doc_006.png"));

  // Overwrite mode asks once, naming the first colliding file; Yes rewrites from 001.
  {
    QFile placeholder(temp.filePath(QStringLiteral("doc_001.png")));
    CHECK(placeholder.open(QIODevice::WriteOnly));
    placeholder.write("placeholder");
  }
  auto declined = std::make_shared<bool>(false);
  QString prompt_text;
  QTimer::singleShot(0, [declined, &prompt_text] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("exportDocumentsOverwriteMessageBox")));
    CHECK(box != nullptr);
    if (box != nullptr) {
      prompt_text = box->text();
      *declined = true;
      box->button(QMessageBox::No)->click();
    }
  });
  const auto refused = MainWindowTestAccess::export_document_sessions_to_folder(
      window, ids, temp.path(), QStringLiteral("png"), naming, patchy::ui::ExportDocumentsExistingFiles::Overwrite);
  CHECK(*declined);
  CHECK(prompt_text.contains(QStringLiteral("doc_001.png")));
  CHECK(!refused.has_value());
  {
    QFile placeholder(temp.filePath(QStringLiteral("doc_001.png")));
    CHECK(placeholder.open(QIODevice::ReadOnly));
    CHECK(placeholder.readAll() == QByteArray("placeholder"));
  }
  auto accepted = std::make_shared<bool>(false);
  click_message_box_when_shown(QStringLiteral("exportDocumentsOverwriteMessageBox"), QMessageBox::Yes, accepted);
  const auto overwritten = MainWindowTestAccess::export_document_sessions_to_folder(
      window, ids, temp.path(), QStringLiteral("png"), naming, patchy::ui::ExportDocumentsExistingFiles::Overwrite);
  CHECK(*accepted);
  CHECK(overwritten.has_value());
  CHECK(overwritten.has_value() && QFileInfo(overwritten->front()).fileName() == QStringLiteral("doc_001.png"));
  const QImage rewritten(temp.filePath(QStringLiteral("doc_001.png")));
  CHECK(rewritten.width() == 10);
  CHECK(QDir(temp.path()).entryList(QDir::Files).size() == 6);
}

// PSD and Aseprite rows write through the layered writers: every page keeps its
// layer tree instead of the flattened composite the image formats get.
void ui_export_documents_to_folder_writes_layered_psd_and_aseprite() {
  SettingsValueRestorer last_save_restorer(QStringLiteral("lastSaveDirectory"));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  patchy::ui::MainWindow window;
  show_window(window);
  const auto base = MainWindowTestAccess::session_count(window);
  auto two_layers = solid_document(10, 6, QColor(255, 0, 0));
  two_layers.add_pixel_layer("Top", patchy::PixelBuffer(10, 6, patchy::PixelFormat::rgba8()));
  window.add_document_session(std::move(two_layers), QStringLiteral("Layered"));
  window.add_document_session(solid_document(12, 6, QColor(0, 255, 0)), QStringLiteral("Flat"));
  const std::vector<std::int64_t> ids = {MainWindowTestAccess::session_id(window, base),
                                         MainWindowTestAccess::session_id(window, base + 1)};
  patchy::ui::ImageSequenceNaming naming;
  naming.prefix = QStringLiteral("doc_");
  naming.start = 1;
  naming.padding = 3;

  const auto psd_written = MainWindowTestAccess::export_document_sessions_to_folder(
      window, ids, temp.path(), QStringLiteral("psd"), naming, patchy::ui::ExportDocumentsExistingFiles::AddNumbering);
  CHECK(psd_written.has_value());
  CHECK(psd_written.has_value() && psd_written->size() == 2);
  if (psd_written.has_value() && psd_written->size() == 2) {
    CHECK(QFileInfo(psd_written->at(0)).fileName() == QStringLiteral("doc_001.psd"));
    const auto layered = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(psd_written->at(0)));
    CHECK(layered.width() == 10);
    CHECK(layered.layers().size() == 2U);
    CHECK(layered.layers().size() == 2U && layered.layers().back().name() == "Top");
    const auto flat = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(psd_written->at(1)));
    CHECK(flat.width() == 12);
    CHECK(flat.layers().size() == 1U);
  }

  const auto ase_written = MainWindowTestAccess::export_document_sessions_to_folder(
      window, ids, temp.path(), QStringLiteral("aseprite"), naming,
      patchy::ui::ExportDocumentsExistingFiles::AddNumbering);
  CHECK(ase_written.has_value());
  CHECK(ase_written.has_value() && ase_written->size() == 2);
  if (ase_written.has_value() && ase_written->size() == 2) {
    // Numbering is per extension: the PSDs do not push the Aseprite files past 001.
    CHECK(QFileInfo(ase_written->at(0)).fileName() == QStringLiteral("doc_001.aseprite"));
    const auto layered =
        patchy::aseprite::DocumentIo::read_file(patchy::ui::to_filesystem_path(ase_written->at(0)));
    CHECK(layered.width() == 10);
    CHECK(layered.layers().size() == 2U);
    CHECK(layered.layers().size() == 2U && layered.layers().back().name() == "Top");
  }
  // The sessions are untouched: nothing got a path.
  CHECK(MainWindowTestAccess::active_session_path(window).isEmpty());
}

// PDF pages ask flatten-or-editable once per batch, like Save As: the remembered
// policy answers silently, "ask" raises pdfLayersMessageBox, and Cancel writes nothing.
void ui_export_documents_to_folder_pdf_layer_choice() {
  SettingsValueRestorer last_save_restorer(QStringLiteral("lastSaveDirectory"));
  SettingsValueRestorer policy_restorer(QStringLiteral("saveOptions/pdfLayerPolicy"));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  patchy::ui::MainWindow window;
  show_window(window);
  const auto base = MainWindowTestAccess::session_count(window);
  // Red page with an opaque blue band across the top rows: two images in editable mode.
  auto two_layers = solid_document(20, 12, QColor(255, 0, 0));
  {
    patchy::PixelBuffer band(20, 12, patchy::PixelFormat::rgba8());
    for (int y = 0; y < 4; ++y) {
      auto row = band.row(y);
      for (int x = 0; x < 20; ++x) {
        row[static_cast<std::size_t>(x) * 4U + 0U] = 0;
        row[static_cast<std::size_t>(x) * 4U + 1U] = 0;
        row[static_cast<std::size_t>(x) * 4U + 2U] = 255;
        row[static_cast<std::size_t>(x) * 4U + 3U] = 255;
      }
    }
    two_layers.add_pixel_layer("Band", std::move(band));
  }
  window.add_document_session(std::move(two_layers), QStringLiteral("Layered"));
  const std::vector<std::int64_t> ids = {MainWindowTestAccess::session_id(window, base)};
  patchy::ui::ImageSequenceNaming naming;
  naming.prefix = QStringLiteral("doc_");
  naming.start = 1;
  naming.padding = 3;
  const auto layer_count = [](const QString& path) {
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    const std::span<const std::uint8_t> span(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                             static_cast<std::size_t>(bytes.size()));
    patchy::pdf::VectorReadOptions read_options;
    read_options.pixels_per_point = 1.0;
    return patchy::pdf::read_page_as_vectors(span, read_options).document.layers().size();
  };
  const auto export_pdf = [&] {
    return MainWindowTestAccess::export_document_sessions_to_folder(
        window, ids, temp.path(), QStringLiteral("pdf"), naming, patchy::ui::ExportDocumentsExistingFiles::AddNumbering);
  };
  auto settings = patchy::ui::app_settings();

  // A remembered policy answers without a prompt.
  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("editable"));
  const auto editable = export_pdf();
  CHECK(editable.has_value());
  if (editable.has_value()) {
    CHECK(QFileInfo(editable->front()).fileName() == QStringLiteral("doc_001.pdf"));
    CHECK(layer_count(editable->front()) >= 2U);
  }
  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("flatten"));
  const auto flattened = export_pdf();
  CHECK(flattened.has_value());
  if (flattened.has_value()) {
    CHECK(QFileInfo(flattened->front()).fileName() == QStringLiteral("doc_002.pdf"));
    CHECK(layer_count(flattened->front()) == 1U);
  }

  // "ask" raises the question once for the batch; Cancel writes nothing.
  settings.setValue(QStringLiteral("saveOptions/pdfLayerPolicy"), QStringLiteral("ask"));
  const auto drive = [](const char* button_text, std::shared_ptr<bool> seen) {
    QTimer::singleShot(0, [button_text, seen] {
      auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("pdfLayersMessageBox")));
      CHECK(box != nullptr);
      if (box == nullptr) {
        return;
      }
      *seen = true;
      if (button_text == nullptr) {
        box->reject();
        return;
      }
      for (auto* button : box->buttons()) {
        if (button->text().contains(QString::fromUtf8(button_text))) {
          button->click();
          return;
        }
      }
      CHECK(false);  // button not found
    });
  };
  auto cancelled = std::make_shared<bool>(false);
  drive(nullptr, cancelled);
  const auto none = export_pdf();
  CHECK(*cancelled);
  CHECK(!none.has_value());
  CHECK(QDir(temp.path()).entryList(QDir::Files).size() == 2);
  auto answered = std::make_shared<bool>(false);
  drive("Keep Layers Editable", answered);
  const auto asked = export_pdf();
  CHECK(*answered);
  CHECK(asked.has_value());
  if (asked.has_value()) {
    CHECK(QFileInfo(asked->front()).fileName() == QStringLiteral("doc_003.pdf"));
    CHECK(layer_count(asked->front()) >= 2U);
  }
  // The question was not remembered: the policy is still "ask".
  CHECK(settings.value(QStringLiteral("saveOptions/pdfLayerPolicy")).toString() == QStringLiteral("ask"));
}

void ui_export_documents_dialog_round_trip() {
  ExportDocumentsSettingsGuard settings_guard;
  SettingsValueRestorer last_save_restorer(QStringLiteral("lastSaveDirectory"));
  QTemporaryDir temp;
  CHECK(temp.isValid());
  patchy::ui::MainWindow window;
  show_window(window);
  const auto base = MainWindowTestAccess::session_count(window);
  window.add_document_session(solid_document(10, 6, QColor(255, 0, 0)), QStringLiteral("Red"));
  window.add_document_session(solid_document(12, 6, QColor(0, 255, 0)), QStringLiteral("Green"));
  window.add_document_session(solid_document(14, 6, QColor(0, 0, 255)), QStringLiteral("Blue"));
  CHECK(MainWindowTestAccess::session_count(window) == base + 3);

  bool saw_dialog = false;
  QTimer::singleShot(0, [&saw_dialog, &temp, base] {
    auto* dialog = find_top_level_dialog(QStringLiteral("exportDocumentsFolderDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* list = dialog->findChild<QListWidget*>(QStringLiteral("exportDocumentsDocumentsList"));
    auto* folder_edit = dialog->findChild<QLineEdit*>(QStringLiteral("exportDocumentsFolderEdit"));
    auto* prefix_edit = dialog->findChild<QLineEdit*>(QStringLiteral("exportDocumentsPrefixEdit"));
    auto* start_spin = dialog->findChild<QSpinBox*>(QStringLiteral("exportDocumentsStartSpin"));
    auto* padding_spin = dialog->findChild<QSpinBox*>(QStringLiteral("exportDocumentsPaddingSpin"));
    auto* format_combo = dialog->findChild<QComboBox*>(QStringLiteral("exportDocumentsFormatCombo"));
    auto* preview = dialog->findChild<QLabel*>(QStringLiteral("exportDocumentsPreviewLabel"));
    auto* summary = dialog->findChild<QLabel*>(QStringLiteral("exportDocumentsSummaryLabel"));
    auto* reverse = dialog->findChild<QPushButton*>(QStringLiteral("exportDocumentsReverseButton"));
    auto* export_button = dialog->findChild<QPushButton*>(QStringLiteral("exportDocumentsExportButton"));
    CHECK(list != nullptr && folder_edit != nullptr && prefix_edit != nullptr && start_spin != nullptr &&
          padding_spin != nullptr && format_combo != nullptr && preview != nullptr && summary != nullptr &&
          reverse != nullptr && export_button != nullptr);
    if (list == nullptr || folder_edit == nullptr || prefix_edit == nullptr || start_spin == nullptr ||
        padding_spin == nullptr || format_combo == nullptr || preview == nullptr || summary == nullptr ||
        reverse == nullptr || export_button == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(static_cast<std::size_t>(list->count()) == base + 3);
    // The current row is the active document (the last one added).
    CHECK(list->currentRow() == list->count() - 1);
    // Defaults: page_001.png onward, Export gated on a folder.
    CHECK(prefix_edit->text() == QStringLiteral("page_"));
    CHECK(start_spin->value() == 1);
    CHECK(padding_spin->value() == 3);
    CHECK(format_combo->currentData().toString() == QStringLiteral("png"));
    // The layer-keeping formats are offered alongside the flat ones.
    CHECK(format_combo->findData(QStringLiteral("psd")) >= 0);
    CHECK(format_combo->findData(QStringLiteral("aseprite")) >= 0);
    CHECK(format_combo->findData(QStringLiteral("svg")) < 0);
    CHECK(format_combo->findData(QStringLiteral("ico")) < 0);
    CHECK(preview->text().contains(QStringLiteral("page_001.png")));
    folder_edit->setText(QDir::toNativeSeparators(temp.path()));
    CHECK(export_button->isEnabled());
    // Every document starts selected; drop the startup document, reverse the rest,
    // and rename the set.
    CHECK(list->selectedItems().size() == list->count());
    list->item(0)->setSelected(false);
    reverse->click();
    CHECK(list->item(0)->text() == QStringLiteral("Blue"));
    CHECK(!list->item(list->count() - 1)->isSelected());
    prefix_edit->setText(QStringLiteral("pg"));
    start_spin->setValue(7);
    padding_spin->setValue(2);
    CHECK(preview->text().contains(QStringLiteral("pg07.png")));
    CHECK(preview->text().contains(QStringLiteral("pg09.png")));
    CHECK(summary->text().contains(QStringLiteral("3")));
    save_widget_artifact("ui_export_documents_folder_dialog", *dialog);
    saw_dialog = true;
    export_button->click();
  });
  require_action(window, "fileExportDocumentsToFolderAction")->trigger();
  CHECK(saw_dialog);
  // Reversed order: Blue, Green, Red as pg07, pg08, pg09.
  const auto names = QDir(temp.path()).entryList(QDir::Files, QDir::Name);
  CHECK(names == QStringList({QStringLiteral("pg07.png"), QStringLiteral("pg08.png"), QStringLiteral("pg09.png")}));
  CHECK(QImage(temp.filePath(QStringLiteral("pg07.png"))).width() == 14);
  CHECK(QImage(temp.filePath(QStringLiteral("pg09.png"))).width() == 10);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Exported 3 images")));
  // The choices persisted for next time.
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("exportDocumentsFolder/prefix")).toString() == QStringLiteral("pg"));
  CHECK(settings.value(QStringLiteral("exportDocumentsFolder/start")).toInt() == 7);
  CHECK(settings.value(QStringLiteral("exportDocumentsFolder/padding")).toInt() == 2);
  CHECK(settings.value(QStringLiteral("exportDocumentsFolder/format")).toString() == QStringLiteral("png"));
  CHECK(QFileInfo(settings.value(QStringLiteral("exportDocumentsFolder/folder")).toString()).absoluteFilePath() ==
        QFileInfo(temp.path()).absoluteFilePath());
}

// The shared list's Auto Sort (numbering-aware, stable, case-insensitive) and
// Reverse, driven through the Export Multi-Page PDF dialog that hosts it.
void ui_document_order_auto_sort_and_reverse() {
  // Widget-free: the collation rule itself.
  const auto sorted = patchy::ui::natural_sorted_titles(
      {QStringLiteral("scan.pdf - Page 10"), QStringLiteral("b2.png"), QStringLiteral("scan.pdf - Page 2"),
       QStringLiteral("B10.png"), QStringLiteral("scan.pdf - Page 1"), QStringLiteral("b2.PNG")});
  CHECK(sorted == QStringList({QStringLiteral("b2.png"), QStringLiteral("b2.PNG"), QStringLiteral("B10.png"),
                               QStringLiteral("scan.pdf - Page 1"), QStringLiteral("scan.pdf - Page 2"),
                               QStringLiteral("scan.pdf - Page 10")}));

  patchy::ui::app_settings().setValue(QStringLiteral("exportOptions/multiPagePdfSource"), QStringLiteral("documents"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto base = MainWindowTestAccess::session_count(window);
  window.add_document_session(solid_document(8, 8, QColor(255, 0, 0)), QStringLiteral("scan.pdf - Page 10"));
  window.add_document_session(solid_document(8, 8, QColor(0, 255, 0)), QStringLiteral("scan.pdf - Page 2"));
  window.add_document_session(solid_document(8, 8, QColor(0, 0, 255)), QStringLiteral("scan.pdf - Page 1"));
  CHECK(MainWindowTestAccess::session_count(window) == base + 3);

  bool seen = false;
  accept_save_file_dialog_then(QStringLiteral("exportMultiPagePdfFileDialog"), QDir::current().absoluteFilePath(QStringLiteral("test-artifacts/multipage_unused.pdf")), [&seen, base] {
    auto* dialog = find_top_level_dialog(QStringLiteral("multiPagePdfExportDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* list = dialog->findChild<QListWidget*>(QStringLiteral("multiPagePdfDocumentsList"));
    auto* auto_sort = dialog->findChild<QPushButton*>(QStringLiteral("multiPagePdfAutoSortButton"));
    auto* reverse = dialog->findChild<QPushButton*>(QStringLiteral("multiPagePdfReverseButton"));
    auto* move_up = dialog->findChild<QPushButton*>(QStringLiteral("multiPagePdfMoveUpButton"));
    auto* select_all = dialog->findChild<QPushButton*>(QStringLiteral("multiPagePdfSelectAllButton"));
    auto* summary = dialog->findChild<QLabel*>(QStringLiteral("multiPagePdfSummaryLabel"));
    CHECK(list != nullptr && auto_sort != nullptr && reverse != nullptr && move_up != nullptr &&
          select_all != nullptr && summary != nullptr);
    if (list == nullptr || auto_sort == nullptr || reverse == nullptr || move_up == nullptr ||
        select_all == nullptr || summary == nullptr) {
      dialog->reject();
      return;
    }
    const auto row_of = [list](const QString& title) {
      for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->text() == title) {
          return row;
        }
      }
      return -1;
    };
    const QString page1 = QStringLiteral("scan.pdf - Page 1");
    const QString page2 = QStringLiteral("scan.pdf - Page 2");
    const QString page10 = QStringLiteral("scan.pdf - Page 10");
    // Creation order: the startup document, then Page 10, Page 2, Page 1.
    CHECK(row_of(page10) == static_cast<int>(base));
    CHECK(row_of(page1) == static_cast<int>(base) + 2);
    // The deselected state and the current row ride along with their rows.
    CHECK(list->selectedItems().size() == list->count());
    list->item(row_of(page2))->setSelected(false);
    list->setCurrentItem(list->item(row_of(page10)), QItemSelectionModel::NoUpdate);
    CHECK(auto_sort->isEnabled());
    auto_sort->click();
    CHECK(static_cast<std::size_t>(list->count()) == base + 3);
    // Numbering-aware: 1, 2, 10 (a plain string sort would put 10 before 2). The
    // startup document sorts by its own title wherever that lands.
    CHECK(row_of(page1) < row_of(page2));
    CHECK(row_of(page2) < row_of(page10));
    CHECK(!list->item(row_of(page2))->isSelected());
    CHECK(list->item(row_of(page10))->isSelected());
    CHECK(list->currentItem() != nullptr && list->currentItem()->text() == page10);
    CHECK(summary->text().contains(QString::number(list->count() - 1)));
    reverse->click();
    CHECK(row_of(page10) < row_of(page2));
    CHECK(row_of(page2) < row_of(page1));
    CHECK(!list->item(row_of(page2))->isSelected());
    CHECK(list->currentItem() != nullptr && list->currentItem()->text() == page10);
    // Move Up is off while the first row is selected and comes back below it. A
    // plain click is ClearAndSelect (setCurrentRow on a multi-select list only adds).
    const auto click_row = [list](int row) {
      list->setCurrentItem(list->item(row), QItemSelectionModel::ClearAndSelect);
    };
    click_row(0);
    CHECK(list->selectedItems().size() == 1);
    CHECK(!move_up->isEnabled());
    click_row(1);
    CHECK(move_up->isEnabled());
    // A multi-row selection moves as a block and stays selected; a run against the
    // top edge stays put.
    click_row(2);
    list->item(3)->setSelected(true);
    const QString third = list->item(2)->text();
    const QString fourth = list->item(3)->text();
    move_up->click();
    CHECK(list->item(1)->text() == third);
    CHECK(list->item(2)->text() == fourth);
    CHECK(list->item(1)->isSelected() && list->item(2)->isSelected() && !list->item(0)->isSelected());
    CHECK(list->currentItem() != nullptr && list->currentItem()->text() == third);
    move_up->click();
    CHECK(list->item(0)->text() == third);
    CHECK(!move_up->isEnabled());
    // Select All brings every row back and grays itself out.
    CHECK(select_all->isEnabled());
    select_all->click();
    CHECK(list->selectedItems().size() == list->count());
    CHECK(!select_all->isEnabled());
    CHECK(summary->text().contains(QString::number(list->count())));
    seen = true;
    dialog->reject();
  });
  require_action(window, "fileExportMultiPagePdfAction")->trigger();
  CHECK(seen);
}

// Every export command lives under File > Export, with the verb carried by the
// submenu title instead of repeated on each row (docs/ui-conventions.md).
void ui_file_export_menu_actions_registered() {
  patchy::ui::MainWindow window;
  show_window(window);

  auto* export_menu = window.findChild<QMenu*>(QStringLiteral("fileExportMenu"));
  CHECK(export_menu != nullptr);
  if (export_menu == nullptr) {
    return;
  }
  const std::vector<std::pair<const char*, const char*>> exports = {
      {"fileExportFlatAction", "file.export_flat"},
      {"fileExportMultiPagePdfAction", "file.export_multipage_pdf"},
      {"fileExportDocumentsToFolderAction", "file.export_documents_to_folder"},
      {"fileExportArtboardsAction", "file.export_artboards"},
      {"fileExportSpriteSheetAction", "file.export_sprite_sheet"},
      {"fileExportImageSequenceAction", "file.export_image_sequence"},
      {"fileExportAnimatedGifAction", "file.export_animated_gif"},
  };
  // The File menu itself holds the submenu, never the commands.
  auto* file_menu = qobject_cast<QMenu*>(export_menu->parentWidget());
  CHECK(file_menu != nullptr);
  for (const auto& [object_name, command_id] : exports) {
    auto* action = require_action(window, object_name);
    CHECK(export_menu->actions().contains(action));
    if (file_menu != nullptr) {
      CHECK(!file_menu->actions().contains(action));
    }
    // Command ids are persisted identifiers; regrouping the menu never moves one.
    const auto* command = window.hotkey_registry().find_command(QString::fromLatin1(command_id));
    CHECK(command != nullptr);
    CHECK(command != nullptr && command->action == action);
  }
  CHECK(require_action(window, "fileExportFlatAction")->text() == QStringLiteral("&Flat Image..."));
  CHECK(require_action(window, "fileExportDocumentsToFolderAction")->text() ==
        QStringLiteral("&Documents to Folder..."));
  CHECK(require_action(window, "fileExportAnimatedGifAction")->text() ==
        QStringLiteral("Layers as Animated &GIF..."));
  // Export for Web sits in the File menu itself, right under Save As (docs/web-export.md),
  // and has Ctrl+Alt+Shift+S; Flat Image gave the key up.
  auto* web = require_action(window, "fileExportForWebAction");
  CHECK(!export_menu->actions().contains(web));
  if (file_menu != nullptr) {
    const auto actions = file_menu->actions();
    const auto web_index = actions.indexOf(web);
    CHECK(web_index > 0 && actions.at(web_index - 1) == require_action(window, "fileSaveAsAction"));
  }
  const auto* web_command = window.hotkey_registry().find_command(QStringLiteral("file.export_for_web"));
  CHECK(web_command != nullptr && web_command->action == web);
  CHECK(web->text() == QStringLiteral("Export for &Web..."));
  CHECK(web->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_S));
  CHECK(require_action(window, "fileExportFlatAction")->shortcut().isEmpty());
}

// Distance from an action's row edge to the first drawn pixel of its text.
int menu_text_start(QMenu& menu, QAction* action) {
  menu.ensurePolished();
  menu.adjustSize();
  const QImage image = menu.grab().toImage();
  const qreal dpr = image.devicePixelRatio();
  const QRect row = menu.actionGeometry(action);
  const int left = static_cast<int>(std::lround((row.left() + 1) * dpr));
  const int right = static_cast<int>(std::lround(row.right() * dpr));
  const int top = static_cast<int>(std::lround(row.top() * dpr));
  const int bottom = static_cast<int>(std::lround(row.bottom() * dpr));
  const QColor background = image.pixelColor(left, (top + bottom) / 2);
  for (int x = left; x < right && x < image.width(); ++x) {
    for (int y = top; y <= bottom && y < image.height(); ++y) {
      const QColor color = image.pixelColor(x, y);
      if (std::abs(color.red() - background.red()) + std::abs(color.green() - background.green()) +
              std::abs(color.blue() - background.blue()) >
          60) {
        return static_cast<int>(std::lround(x / dpr)) - row.left();
      }
    }
  }
  return -1;
}

// Every menu-bar menu starts its text in the same column. Qt reserves the
// check-and-icon column only in a menu holding an icon or a check mark, so the
// Plugins menu off Windows (a note and a submenu) drew its text further left
// than its neighbours until reserve_menu_icon_column (GitHub issue 131).
void ui_menubar_menus_share_the_text_column() {
  patchy::ui::MainWindow window;
  show_window(window);

  const auto first_text_action = [](QMenu& menu) -> QAction* {
    for (auto* action : menu.actions()) {
      if (action->isVisible() && !action->isSeparator() && qobject_cast<QWidgetAction*>(action) == nullptr) {
        return action;
      }
    }
    return nullptr;
  };
  int expected = -1;
  for (auto* top : window.menuBar()->actions()) {
    auto* menu = top->menu();
    if (menu == nullptr || !top->isVisible()) {
      continue;
    }
    auto* action = first_text_action(*menu);
    if (action == nullptr) {
      continue;
    }
    const int start = menu_text_start(*menu, action);
    std::printf("  %s: text starts %d px in\n", menu->objectName().toUtf8().constData(), start);
    CHECK(start > 0);
    if (expected < 0) {
      expected = start;
    }
    // First letters differ in side bearing by a pixel or two; the missing
    // column is over ten.
    CHECK(std::abs(start - expected) <= 3);
  }
  CHECK(expected > 0);

  QMenu bare(&window);
  bare.addAction(QStringLiteral("Legacy entry"));
  const int bare_start = menu_text_start(bare, bare.actions().front());
  QMenu reserved(&window);
  reserved.addAction(QStringLiteral("Legacy entry"));
  reserved.addAction(QStringLiteral("Second entry"));
  patchy::ui::reserve_menu_icon_column(&reserved);
  patchy::ui::hide_menu_action_icons(&reserved);
  const int reserved_start = menu_text_start(reserved, reserved.actions().front());
  CHECK(bare_start > 0 && bare_start + 8 < expected);
  CHECK(std::abs(reserved_start - expected) <= 3);
}

// A click on a menu separator leaves the menu open, like a click on a disabled
// item; Qt's QMenu closed as if the click landed outside it (GitHub issue 317).
// MainWindow installs the guard app-wide, so a bare QMenu is covered too.
void ui_menu_separator_click_keeps_menu_open() {
  struct TestMenu {
    QMenu menu;
    QAction* first = nullptr;
    QAction* separator = nullptr;
    QAction* second = nullptr;
    QMenu* submenu = nullptr;
    QAction* sub_item = nullptr;
    TestMenu() {
      first = menu.addAction(QStringLiteral("First"));
      separator = menu.addSeparator();
      second = menu.addAction(QStringLiteral("Second"));
      menu.addSeparator();
      submenu = menu.addMenu(QStringLiteral("More"));
      sub_item = submenu->addAction(QStringLiteral("Inside"));
    }
    void open() {
      menu.popup(QPoint(40, 40));
      QApplication::processEvents();
    }
    QPoint at(QAction* action) const { return menu.actionGeometry(action).center(); }
  };

  // Without the guard (only when no MainWindow ran earlier in this process),
  // the separator click closes the menu: the bug reproduces here.
  if (!qApp->property("patchyMenuSeparatorClickGuard").toBool()) {
    TestMenu before;
    before.open();
    CHECK(before.menu.isVisible());
    QTest::mouseClick(&before.menu, Qt::LeftButton, Qt::NoModifier, before.at(before.separator));
    QApplication::processEvents();
    std::printf("  without the guard the menu is %s after a separator click\n",
                before.menu.isVisible() ? "open" : "closed");
    CHECK(!before.menu.isVisible());
    before.menu.hide();
  }

  patchy::ui::MainWindow window;
  show_window(window);

  TestMenu test;
  QSignalSpy triggered(&test.menu, &QMenu::triggered);
  test.open();
  CHECK(test.menu.isVisible());
  const QRect separator_rect = test.menu.actionGeometry(test.separator);
  CHECK(separator_rect.isValid() && separator_rect.height() > 0);
  QTest::mouseClick(&test.menu, Qt::LeftButton, Qt::NoModifier, separator_rect.center());
  QApplication::processEvents();
  CHECK(test.menu.isVisible());
  CHECK(triggered.count() == 0);
  QTest::mouseDClick(&test.menu, Qt::LeftButton, Qt::NoModifier, separator_rect.center());
  QApplication::processEvents();
  CHECK(test.menu.isVisible());
  CHECK(triggered.count() == 0);

  // A separator in the parent menu while a submenu is open: the click reaches
  // the submenu first and Qt forwards it to the parent.
  test.menu.setActiveAction(test.submenu->menuAction());
  test.submenu->popup(test.menu.mapToGlobal(test.menu.actionGeometry(test.submenu->menuAction()).topRight()));
  QApplication::processEvents();
  if (test.submenu->isVisible()) {
    QTest::mouseClick(test.submenu, Qt::LeftButton, Qt::NoModifier,
                      test.submenu->mapFromGlobal(test.menu.mapToGlobal(separator_rect.center())));
    QApplication::processEvents();
    CHECK(test.menu.isVisible());
    CHECK(triggered.count() == 0);
    test.submenu->hide();
    QApplication::processEvents();
  } else {
    std::printf("  the submenu did not open offscreen; parent-separator case skipped\n");
  }

  // A normal item still runs and closes the menu.
  CHECK(test.menu.isVisible());
  QTest::mouseClick(&test.menu, Qt::LeftButton, Qt::NoModifier, test.at(test.second));
  QApplication::processEvents();
  CHECK(triggered.count() == 1);
  CHECK(triggered.count() == 1 && triggered.front().front().value<QAction*>() == test.second);
  CHECK(!test.menu.isVisible());
}

}  // namespace

std::vector<patchy::test::TestCase> folder_open_export_tests() {
  return {
      {"ui_open_folder_opens_images_in_natural_order", ui_open_folder_opens_images_in_natural_order},
      {"ui_open_folder_empty_reports_status", ui_open_folder_empty_reports_status},
      {"ui_open_folder_drop_and_cli_expand_directories", ui_open_folder_drop_and_cli_expand_directories},
      {"ui_folder_actions_and_commands_registered", ui_folder_actions_and_commands_registered},
      {"ui_file_export_menu_actions_registered", ui_file_export_menu_actions_registered},
      {"ui_menubar_menus_share_the_text_column", ui_menubar_menus_share_the_text_column},
      {"ui_menu_separator_click_keeps_menu_open", ui_menu_separator_click_keeps_menu_open},
      {"ui_export_documents_to_folder_writes_numbered_files", ui_export_documents_to_folder_writes_numbered_files},
      {"ui_export_documents_to_folder_writes_layered_psd_and_aseprite",
       ui_export_documents_to_folder_writes_layered_psd_and_aseprite},
      {"ui_export_documents_to_folder_pdf_layer_choice", ui_export_documents_to_folder_pdf_layer_choice},
      {"ui_export_documents_dialog_round_trip", ui_export_documents_dialog_round_trip},
      {"ui_document_order_auto_sort_and_reverse", ui_document_order_auto_sort_and_reverse},
  };
}
