// The layer row context menu's size and submenus (#461) and the layer export actions in its
// Export submenu (#462): Quick Export as PNG and Export As....
#include "core/layer_metadata.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "test_harness.hpp"
#include "unicode_path_names.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

namespace {

using namespace patchy::test::ui;

// The tallest the row menu may be at the current style. A 1366x768 screen with a taskbar
// leaves about 720 px; the margin keeps room for entries a later change adds.
constexpr int kLayerMenuMaxHeight = 680;

patchy::PixelBuffer clear_pixels(int width, int height) {
  return solid_pixels(width, height, patchy::PixelFormat::rgba8(), QColor(0, 0, 0, 0));
}

patchy::Layer solid_layer(patchy::Document& doc, const std::string& name, QRect rect, QColor color) {
  patchy::Layer layer(doc.allocate_layer_id(), name,
                      solid_pixels(rect.width(), rect.height(), patchy::PixelFormat::rgba8(), color));
  layer.set_bounds(patchy::Rect{rect.x(), rect.y(), rect.width(), rect.height()});
  return layer;
}

QStringList action_names(const QMenu& menu) {
  QStringList names;
  for (auto* action : menu.actions()) {
    names.push_back(action->isSeparator() ? QStringLiteral("-") : action->objectName());
  }
  return names;
}

struct RowMenuSeen {
  bool shown{false};
  int rows{0};  // visible top-level entries, separators not counted
  int height{0};
  QStringList texts;
  QStringList submenus;
  QStringList merge;
  QStringList rasterize;
  QStringList export_entries;
  QStringList export_texts;
};

// Right-clicks the named row's name label and reads the row menu; `artifact` saves the
// menu with its Export submenu open over the window.
RowMenuSeen read_row_menu(patchy::ui::MainWindow& window, QListWidget& layers, const QString& row_name,
                          const std::string& artifact = std::string()) {
  RowMenuSeen seen;
  QTimer::singleShot(0, [&] {
    QMenu* row_menu = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu != nullptr && menu->isVisible() && menu->objectName() == QStringLiteral("layerContextMenu")) {
        row_menu = menu;
        break;
      }
    }
    CHECK(row_menu != nullptr);
    if (row_menu == nullptr) {
      return;
    }
    seen.shown = true;
    seen.height = row_menu->sizeHint().height();
    QAction* export_entry = nullptr;
    for (auto* action : row_menu->actions()) {
      if (action->isSeparator() || !action->isVisible()) {
        continue;
      }
      ++seen.rows;
      seen.texts.push_back(action->text().remove(QLatin1Char('&')));
      if (auto* submenu = action->menu(); submenu != nullptr) {
        seen.submenus.push_back(submenu->objectName());
        if (submenu->objectName() == QStringLiteral("layerContextMergeMenu")) {
          seen.merge = action_names(*submenu);
        } else if (submenu->objectName() == QStringLiteral("layerContextRasterizeMenu")) {
          seen.rasterize = action_names(*submenu);
        } else if (submenu->objectName() == QStringLiteral("layerContextExportMenu")) {
          seen.export_entries = action_names(*submenu);
          for (auto* entry : submenu->actions()) {
            seen.export_texts.push_back(entry->text().remove(QLatin1Char('&')));
          }
          export_entry = action;
        }
      }
    }
    if (!artifact.empty()) {
      save_widget_artifact(artifact + "_menu", *row_menu);
    }
    if (!artifact.empty() && export_entry != nullptr) {
      row_menu->setActiveAction(export_entry);
      QTest::keyClick(row_menu, Qt::Key_Right);
      QApplication::processEvents();
      auto* submenu = export_entry->menu();
      if (submenu->isVisible()) {
        const auto origin = window.mapToGlobal(QPoint(0, 0));
        QRect crop;
        for (QWidget* popup : {static_cast<QWidget*>(row_menu), static_cast<QWidget*>(submenu)}) {
          crop = crop.united(QRect(popup->mapToGlobal(QPoint(0, 0)) - origin, popup->size()));
        }
        crop = crop.adjusted(-12, -12, 12, 12);
        QImage shot(crop.size(), QImage::Format_ARGB32_Premultiplied);
        shot.fill(window.palette().color(QPalette::Window));
        {
          QPainter painter(&shot);
          painter.drawPixmap(-crop.topLeft(), window.grab());
          for (QWidget* popup : {static_cast<QWidget*>(row_menu), static_cast<QWidget*>(submenu)}) {
            painter.drawPixmap(popup->mapToGlobal(QPoint(0, 0)) - origin - crop.topLeft(), popup->grab());
          }
        }
        save_image_artifact(artifact + "_export_submenu", shot);
        submenu->close();
      }
    }
    row_menu->close();
  });
  auto* row = layers.itemWidget(require_layer_item(layers, row_name));
  auto* name = row != nullptr ? row->findChild<QWidget*>(QStringLiteral("layerRowName")) : nullptr;
  CHECK(name != nullptr);
  if (name != nullptr) {
    const auto local = name->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, local, name->mapToGlobal(local));
    QApplication::sendEvent(name, &event);
  }
  QApplication::processEvents();
  return seen;
}

void ui_layer_context_menu_fits_short_screens() {
  patchy::ui::MainWindow window;
  window.resize(1366, 728);
  patchy::Document built(200, 120, patchy::PixelFormat::rgba8());
  built.add_layer(solid_layer(built, "Paper", QRect(0, 0, 200, 120), QColor(Qt::white)));
  built.add_layer(solid_layer(built, "Sun", QRect(40, 20, 50, 50), QColor(240, 180, 40)));
  patchy::Layer text_layer(built.allocate_layer_id(), "Title", clear_pixels(1, 1));
  text_layer.set_bounds(patchy::Rect{20, 80, 1, 1});
  text_layer.metadata()[patchy::kLayerMetadataText] = "Title";
  text_layer.metadata()[patchy::kLayerMetadataTextSize] = "24";
  text_layer.metadata()[patchy::kLayerMetadataTextColor] = "#101010";
  text_layer.metadata()[patchy::kLayerMetadataTextFont] = QApplication::font().family().toStdString();
  built.add_layer(std::move(text_layer));
  patchy::Layer group(built.allocate_layer_id(), "Folder", patchy::LayerKind::Group);
  group.add_child(solid_layer(built, "Dot", QRect(5, 5, 8, 8), QColor(30, 90, 200)));
  built.add_layer(std::move(group));
  window.add_document_session(std::move(built), QStringLiteral("Menu Size"));
  show_window(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  if (layers == nullptr) {
    return;
  }

  const auto pixel = read_row_menu(window, *layers, QStringLiteral("Sun"), "issue461_layer_menu");
  CHECK(pixel.shown);
  CHECK(pixel.rows <= 18);
  CHECK(pixel.height > 0 && pixel.height < kLayerMenuMaxHeight);
  std::printf("[INFO] pixel layer menu: %d rows, %d px\n", pixel.rows, pixel.height);
  CHECK(pixel.texts.value(0) == QStringLiteral("Edit Layer Styles..."));
  CHECK(!pixel.texts.contains(QStringLiteral("Visible")));
  for (const auto* hidden : {"Merge Visible", "Flatten Image", "Rasterize (including layer style)",
                             "Trace Image to Shapes...", "Duplicate Layer to Document..."}) {
    CHECK(!pixel.texts.contains(QLatin1String(hidden)));
  }
  CHECK(pixel.texts.contains(QStringLiteral("Merge Down")));
  // Merge, Rasterize and Smart Objects follow Merge Down; Export sits before Thumbnail Size.
  const auto merge = pixel.submenus.indexOf(QStringLiteral("layerContextMergeMenu"));
  CHECK(merge >= 0 && pixel.submenus.value(merge + 1) == QStringLiteral("layerContextRasterizeMenu") &&
        pixel.submenus.value(merge + 2) == QStringLiteral("layerContextSmartObjectsMenu"));
  const auto exports = pixel.submenus.indexOf(QStringLiteral("layerContextExportMenu"));
  CHECK(exports >= 0 && pixel.submenus.value(exports - 1) == QStringLiteral("layerContextVectorMaskMenu") &&
        pixel.submenus.value(exports + 1) == QStringLiteral("layerThumbnailSizeMenu"));
  CHECK(pixel.merge == (QStringList{QStringLiteral("layerContextMergeVisibleLayersAction"), QString(),
                                    QStringLiteral("imageFlattenAction")}));
  CHECK(pixel.rasterize == (QStringList{QStringLiteral("layerRasterizeAction"),
                                        QStringLiteral("layerRasterizeLayerStyleAction"), QStringLiteral("-"),
                                        QStringLiteral("layerTraceImageAction")}));
  CHECK(pixel.export_entries.mid(0, 3) == (QStringList{QStringLiteral("layerQuickExportPngAction"),
                                                       QStringLiteral("layerExportAsAction"), QStringLiteral("-")}));
  CHECK(pixel.export_texts.value(3) == QStringLiteral("Duplicate Layer to Document..."));

  const auto text = read_row_menu(window, *layers, QStringLiteral("Title"));
  CHECK(text.shown);
  CHECK(text.rows <= 20);
  CHECK(text.height > 0 && text.height < kLayerMenuMaxHeight);
  std::printf("[INFO] text layer menu: %d rows, %d px\n", text.rows, text.height);
  CHECK(text.texts.contains(QStringLiteral("Warp Text...")));

  const auto folder = read_row_menu(window, *layers, QStringLiteral("Folder"));
  CHECK(folder.shown);
  CHECK(folder.rows <= 19);
  CHECK(folder.height > 0 && folder.height < kLayerMenuMaxHeight);
  CHECK(folder.texts.contains(QStringLiteral("Ungroup Layers")));
  CHECK(folder.export_texts.value(3) == QStringLiteral("Duplicate Group to Document..."));
}

// The pixel thumbnail's own menu carries the same Export submenu before Thumbnail Size.
void ui_layer_thumbnail_menu_offers_layer_export() {
  patchy::ui::MainWindow window;
  patchy::Document built(80, 60, patchy::PixelFormat::rgba8());
  built.add_layer(solid_layer(built, "Paper", QRect(0, 0, 80, 60), QColor(Qt::white)));
  built.add_layer(solid_layer(built, "Spot", QRect(10, 10, 20, 20), QColor(200, 40, 40)));
  window.add_document_session(std::move(built), QStringLiteral("Thumbnail Export"));
  show_window(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  if (layers == nullptr) {
    return;
  }
  QString menu_name;
  QStringList entries;
  QStringList export_entries;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || !menu->isVisible()) {
        continue;
      }
      menu_name = menu->objectName();
      for (auto* action : menu->actions()) {
        entries.push_back(action->isSeparator() ? QStringLiteral("-")
                                                : (action->menu() != nullptr ? action->menu()->objectName()
                                                                             : action->objectName()));
        if (action->menu() != nullptr && action->menu()->objectName() == QStringLiteral("layerThumbnailExportMenu")) {
          export_entries = action_names(*action->menu());
        }
      }
      save_widget_artifact("issue462_thumbnail_menu", *menu);
      menu->close();
      return;
    }
  });
  auto* row = layers->itemWidget(require_layer_item(*layers, QStringLiteral("Spot")));
  auto* thumbnail = row != nullptr ? row->findChild<QWidget*>(QStringLiteral("layerContentThumbnail")) : nullptr;
  CHECK(thumbnail != nullptr);
  if (thumbnail != nullptr) {
    const auto local = thumbnail->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, local, thumbnail->mapToGlobal(local));
    QApplication::sendEvent(thumbnail, &event);
  }
  QApplication::processEvents();
  CHECK(menu_name == QStringLiteral("layerThumbnailMenu"));
  CHECK(entries.mid(4) == (QStringList{QStringLiteral("-"), QStringLiteral("layerThumbnailExportMenu"),
                                       QStringLiteral("-"), QStringLiteral("layerThumbnailSizeMenu")}));
  CHECK(export_entries ==
        (QStringList{QStringLiteral("layerQuickExportPngAction"), QStringLiteral("layerExportAsAction")}));
}

// A document for the export tests: a small red layer with a Unicode name, the same name
// again, a name with characters files cannot hold, a group of two dots (exported as its
// composite) and an empty layer.
struct ExportFixture {
  patchy::LayerId red{0};
  patchy::LayerId red_again{0};
  patchy::LayerId slashed{0};
  patchy::LayerId group{0};
  patchy::LayerId empty{0};
};

ExportFixture add_export_document(patchy::ui::MainWindow& window, const std::string& red_name) {
  ExportFixture ids;
  patchy::Document built(64, 48, patchy::PixelFormat::rgba8());
  auto red = solid_layer(built, red_name, QRect(5, 7, 10, 6), QColor(220, 20, 20));
  ids.red = red.id();
  built.add_layer(std::move(red));
  auto again = solid_layer(built, red_name, QRect(40, 30, 3, 5), QColor(20, 20, 220));
  ids.red_again = again.id();
  built.add_layer(std::move(again));
  auto slashed = solid_layer(built, "a/b:c?", QRect(0, 0, 2, 2), QColor(20, 200, 20));
  ids.slashed = slashed.id();
  built.add_layer(std::move(slashed));
  patchy::Layer group(built.allocate_layer_id(), "Dots", patchy::LayerKind::Group);
  ids.group = group.id();
  group.add_child(solid_layer(built, "Dot A", QRect(20, 20, 4, 4), QColor(10, 10, 10)));
  group.add_child(solid_layer(built, "Dot B", QRect(30, 24, 4, 4), QColor(250, 250, 250)));
  built.add_layer(std::move(group));
  patchy::Layer empty(built.allocate_layer_id(), "Empty", clear_pixels(64, 48));
  ids.empty = empty.id();
  built.add_layer(std::move(empty));
  window.add_document_session(std::move(built), QStringLiteral("Layer Export"));
  QApplication::processEvents();
  return ids;
}

// Accepts the named file dialog with `path` once it is up (exec_dialog nests a loop).
void accept_file_dialog_later(const QString& object_name, const QString& path, bool& seen,
                              const std::function<void(QFileDialog&)>& inspect = {}) {
  QTimer::singleShot(0, [object_name, path, &seen, inspect] {
    auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(object_name));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    seen = true;
    if (inspect) {
      inspect(*dialog);
    }
    if (dialog->fileMode() == QFileDialog::Directory) {
      dialog->setDirectory(path);
    }
    dialog->selectFile(path);
    static_cast<QDialog*>(dialog)->accept();  // QFileDialog::accept is protected
  });
}

QImage read_png(const QString& path) {
  QImage image(path, "PNG");
  return image.convertToFormat(QImage::Format_ARGB32);
}

void ui_layer_quick_export_png_writes_trimmed_named_files() {
  using patchy::test::kUnicodePathStems;
  const auto stem = QString::fromStdString(patchy::test::utf8_string(kUnicodePathStems[0]));
  const auto folder = patchy::ui::to_qstring(
      std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"layer-quick-export")));
  patchy::ui::MainWindow window;
  const auto ids = add_export_document(window, patchy::test::utf8_string(kUnicodePathStems[0]) + " red");
  show_window(window);

  // Several layers: one folder pick, a file per layer named after it.
  patchy::ui::MainWindowTestAccess::select_layers(window, {ids.red, ids.red_again, ids.slashed, ids.group, ids.empty},
                                                  ids.red);
  QApplication::processEvents();
  bool saw_folder_dialog = false;
  accept_file_dialog_later(QStringLiteral("layerQuickExportFolderDialog"), folder, saw_folder_dialog);
  require_action(window, "layerQuickExportPngAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_folder_dialog);

  const QDir directory(folder);
  // Files follow the panel order, top first: the upper of the two same-named layers keeps the
  // plain name.
  const auto upper = read_png(directory.filePath(stem + QStringLiteral(" red.png")));
  CHECK(upper.size() == QSize(3, 5));
  CHECK(upper.pixelColor(1, 1) == QColor(20, 20, 220));
  const auto lower = read_png(directory.filePath(stem + QStringLiteral(" red 2.png")));
  CHECK(lower.size() == QSize(10, 6));
  CHECK(lower.pixelColor(0, 0) == QColor(220, 20, 20));
  CHECK(lower.pixelColor(9, 5) == QColor(220, 20, 20));
  CHECK(read_png(directory.filePath(QStringLiteral("a_b_c_.png"))).size() == QSize(2, 2));
  // The group as its composite, trimmed to both dots.
  const auto group = read_png(directory.filePath(QStringLiteral("Dots.png")));
  CHECK(group.size() == QSize(14, 8));
  CHECK(group.pixelColor(0, 0) == QColor(10, 10, 10));
  CHECK(group.pixelColor(13, 7) == QColor(250, 250, 250));
  CHECK(qAlpha(group.pixel(13, 0)) == 0);
  // The empty layer writes nothing and the status bar says it was skipped.
  CHECK(!QFileInfo::exists(directory.filePath(QStringLiteral("Empty.png"))));
  CHECK(directory.entryList(QDir::Files).size() == 4);
  const auto message = window.statusBar()->currentMessage();
  CHECK(message.contains(QStringLiteral("Exported 4 layers")) && message.contains(QStringLiteral("skipped")));
  CHECK(QDir(patchy::ui::app_settings().value(QStringLiteral("layerExport/lastFolder")).toString()) == directory);

  // One layer: a save dialog in the remembered folder with the layer's name filled in.
  patchy::ui::MainWindowTestAccess::select_layers(window, {ids.group}, ids.group);
  QApplication::processEvents();
  bool saw_save_dialog = false;
  QString suggested;
  QString suggested_folder;
  const auto single = directory.filePath(stem + QStringLiteral(" single.png"));
  accept_file_dialog_later(QStringLiteral("layerQuickExportFileDialog"), single, saw_save_dialog,
                           [&](QFileDialog& dialog) {
                             const auto selected = dialog.selectedFiles();
                             suggested = selected.isEmpty() ? QString() : QFileInfo(selected.front()).fileName();
                             suggested_folder = dialog.directory().absolutePath();
                           });
  require_action(window, "layerQuickExportPngAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_save_dialog);
  CHECK(suggested == QStringLiteral("Dots.png"));
  CHECK(QDir(suggested_folder) == directory);
  CHECK(read_png(single).size() == QSize(14, 8));

  // An empty layer alone asks nothing and writes nothing.
  patchy::ui::MainWindowTestAccess::select_layers(window, {ids.empty}, ids.empty);
  QApplication::processEvents();
  QTimer::singleShot(0, [] {
    if (auto* dialog = find_top_level_dialog(QStringLiteral("layerQuickExportFileDialog")); dialog != nullptr) {
      CHECK(false);
      dialog->reject();
    }
  });
  require_action(window, "layerQuickExportPngAction")->trigger();
  QApplication::processEvents();
  CHECK(find_top_level_dialog(QStringLiteral("layerQuickExportFileDialog")) == nullptr);
  CHECK(directory.entryList(QDir::Files).size() == 5);
}

// Export As...: the save dialog picks the format and the Export Options dialog its options.
// It keeps the canvas size unless Trim is ticked there.
void ui_layer_export_as_uses_chosen_format_and_options() {
  const auto folder = patchy::ui::to_qstring(
      std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"layer-export-as")));
  patchy::ui::MainWindow window;
  const auto ids = add_export_document(window, "Red");
  show_window(window);
  const QDir directory(folder);
  const auto run = [&](const std::vector<patchy::LayerId>& selection, const QString& typed,
                       const QString& options_dialog, bool trim) {
    patchy::ui::MainWindowTestAccess::select_layers(window, selection, selection.front());
    QApplication::processEvents();
    bool saw_save_dialog = false;
    bool saw_options = false;
    accept_file_dialog_later(QStringLiteral("layerExportAsFileDialog"), directory.filePath(typed), saw_save_dialog);
    // The options dialog comes up after the save dialog's loop ends.
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&] {
      auto* options = find_top_level_dialog(options_dialog);
      if (options == nullptr || !options->isVisible()) {
        return;
      }
      poll.stop();
      saw_options = true;
      auto* trim_check = options->findChild<QCheckBox*>(QStringLiteral("exportTrimCheck"));
      CHECK(trim_check != nullptr);
      if (trim_check != nullptr) {
        trim_check->setChecked(trim);
      }
      auto* buttons = options->findChild<QDialogButtonBox*>();
      CHECK(buttons != nullptr);
      if (buttons != nullptr) {
        buttons->button(QDialogButtonBox::Ok)->click();
      } else {
        options->reject();
      }
    });
    poll.start(10);
    require_action(window, "layerExportAsAction")->trigger();
    QApplication::processEvents();
    poll.stop();
    CHECK(saw_save_dialog);
    CHECK(saw_options);
  };

  // Two layers as BMP at the canvas size, each named after its layer.
  run({ids.red, ids.group}, QStringLiteral("anything.bmp"), QStringLiteral("bmpSaveOptionsDialog"), false);
  const QImage red(directory.filePath(QStringLiteral("Red.bmp")));
  CHECK(red.size() == QSize(64, 48));
  CHECK(red.pixelColor(6, 8) == QColor(220, 20, 20));
  CHECK(QImage(directory.filePath(QStringLiteral("Dots.bmp"))).size() == QSize(64, 48));
  CHECK(!QFileInfo::exists(directory.filePath(QStringLiteral("anything.bmp"))));

  // One layer takes the typed name; Trim in the options trims it.
  run({ids.red}, QStringLiteral("typed.png"), QStringLiteral("exportScaleOptionsDialog"), true);
  CHECK(read_png(directory.filePath(QStringLiteral("typed.png"))).size() == QSize(10, 6));
  auto settings = patchy::ui::app_settings();
  settings.setValue(QStringLiteral("saveOptions/exportTrim"), false);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_export_tests() {
  return {
      {"ui_layer_context_menu_fits_short_screens", ui_layer_context_menu_fits_short_screens},
      {"ui_layer_thumbnail_menu_offers_layer_export", ui_layer_thumbnail_menu_offers_layer_export},
      {"ui_layer_quick_export_png_writes_trimmed_named_files", ui_layer_quick_export_png_writes_trimmed_named_files},
      {"ui_layer_export_as_uses_chosen_format_and_options", ui_layer_export_as_uses_chosen_format_and_options},
  };
}
