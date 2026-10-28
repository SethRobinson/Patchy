// The layer row context menu's size and submenus (#461) and the layer export actions in its
// Export submenu (#462): Quick Export as PNG and Export As....
#include "core/layer_metadata.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
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
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStatusBar>
#include <QStringList>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <cstdint>
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
  // Merge, Rasterize and Smart Objects follow Merge Down; Export ends the menu (Thumbnail Size
  // moved to the thumbnail's own menu, #481).
  const auto merge = pixel.submenus.indexOf(QStringLiteral("layerContextMergeMenu"));
  CHECK(merge >= 0 && pixel.submenus.value(merge + 1) == QStringLiteral("layerContextRasterizeMenu") &&
        pixel.submenus.value(merge + 2) == QStringLiteral("layerContextSmartObjectsMenu"));
  const auto exports = pixel.submenus.indexOf(QStringLiteral("layerContextExportMenu"));
  CHECK(exports >= 0 && pixel.submenus.value(exports - 1) == QStringLiteral("layerContextVectorMaskMenu") &&
        exports == pixel.submenus.size() - 1);
  CHECK(!pixel.submenus.contains(QStringLiteral("layerThumbnailSizeMenu")));
  CHECK(pixel.merge == (QStringList{QStringLiteral("layerContextMergeVisibleLayersAction"), QString(),
                                    QStringLiteral("layerMergeSelectedToNewAction"),
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

struct ThumbnailMenuSeen {
  QString name;
  QStringList entries;  // separators as "-", submenus by their object name
  QStringList export_entries;
  QStringList display_entries;  // the Thumbnail Display submenu's actions, checked ones marked "*"
};

// Right-clicks `part` of the named row and reads the short thumbnail menu. With `artifact`
// it saves the menu with the `open_submenu` submenu (Export by default) open over the window.
ThumbnailMenuSeen read_thumbnail_menu(patchy::ui::MainWindow& window, QListWidget& layers, const QString& row_name,
                                      const QString& part, const std::string& artifact = std::string(),
                                      const QString& open_submenu = QStringLiteral("layerThumbnailExportMenu"),
                                      const std::string& open_suffix = "_export_open") {
  ThumbnailMenuSeen seen;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || !menu->isVisible()) {
        continue;
      }
      seen.name = menu->objectName();
      QAction* export_entry = nullptr;
      for (auto* action : menu->actions()) {
        seen.entries.push_back(action->isSeparator() ? QStringLiteral("-")
                                                     : (action->menu() != nullptr ? action->menu()->objectName()
                                                                                  : action->objectName()));
        if (action->menu() != nullptr && action->menu()->objectName() == QStringLiteral("layerThumbnailExportMenu")) {
          seen.export_entries = action_names(*action->menu());
        }
        if (action->menu() != nullptr && action->menu()->objectName() == QStringLiteral("layerThumbnailDisplayMenu")) {
          for (auto* entry : action->menu()->actions()) {
            seen.display_entries << entry->objectName() + (entry->isChecked() ? QStringLiteral("*") : QString());
          }
        }
        if (action->menu() != nullptr && action->menu()->objectName() == open_submenu) {
          export_entry = action;
        }
      }
      if (!artifact.empty() && export_entry != nullptr) {
        save_widget_artifact(artifact, *menu);
        menu->setActiveAction(export_entry);
        QTest::keyClick(menu, Qt::Key_Right);
        QApplication::processEvents();
        auto* submenu = export_entry->menu();
        if (submenu->isVisible()) {
          const auto origin = window.mapToGlobal(QPoint(0, 0));
          QRect crop;
          for (QWidget* popup : {static_cast<QWidget*>(menu), static_cast<QWidget*>(submenu)}) {
            crop = crop.united(QRect(popup->mapToGlobal(QPoint(0, 0)) - origin, popup->size()));
          }
          crop = crop.adjusted(-12, -12, 12, 12);
          QImage shot(crop.size(), QImage::Format_ARGB32_Premultiplied);
          shot.fill(window.palette().color(QPalette::Window));
          {
            QPainter painter(&shot);
            painter.drawPixmap(-crop.topLeft(), window.grab());
            for (QWidget* popup : {static_cast<QWidget*>(menu), static_cast<QWidget*>(submenu)}) {
              painter.drawPixmap(popup->mapToGlobal(QPoint(0, 0)) - origin - crop.topLeft(), popup->grab());
            }
          }
          save_image_artifact(artifact + open_suffix, shot);
          submenu->close();
        }
      }
      menu->close();
      return;
    }
  });
  auto* row = layers.itemWidget(require_layer_item(layers, row_name));
  auto* thumbnail = row != nullptr ? row->findChild<QWidget*>(part) : nullptr;
  CHECK(thumbnail != nullptr);
  if (thumbnail != nullptr) {
    const auto local = thumbnail->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, local, thumbnail->mapToGlobal(local));
    QApplication::sendEvent(thumbnail, &event);
  }
  QApplication::processEvents();
  return seen;
}

// The pixel, layer mask and vector mask thumbnails' own menus carry the same Export submenu
// (#462): before Thumbnail Size and the thumbnail bounds pair (#481) on a pixel thumbnail, last
// on a mask thumbnail.
void ui_layer_thumbnail_menu_offers_layer_export() {
  patchy::ui::MainWindow window;
  patchy::Document built(80, 60, patchy::PixelFormat::rgba8());
  built.add_layer(solid_layer(built, "Paper", QRect(0, 0, 80, 60), QColor(Qt::white)));
  built.add_layer(solid_layer(built, "Spot", QRect(10, 10, 20, 20), QColor(200, 40, 40)));
  auto masked = solid_layer(built, "Masked", QRect(0, 0, 80, 60), QColor(40, 160, 60));
  patchy::PixelBuffer mask(80, 60, patchy::PixelFormat::gray8());
  for (int y = 0; y < mask.height(); ++y) {
    auto row = mask.row(y);
    std::fill(row.begin(), row.end(), y < 30 ? std::uint8_t{255} : std::uint8_t{0});
  }
  masked.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 80, 60}, std::move(mask), 0, false});
  built.add_layer(std::move(masked));
  auto vector_masked = solid_layer(built, "Vector Masked", QRect(40, 30, 30, 20), QColor(40, 60, 200));
  vector_masked.set_vector_mask(patchy::LayerVectorMask{});
  built.add_layer(std::move(vector_masked));
  window.add_document_session(std::move(built), QStringLiteral("Thumbnail Export"));
  show_window(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  if (layers == nullptr) {
    return;
  }
  const QStringList both_actions{QStringLiteral("layerQuickExportPngAction"), QStringLiteral("layerExportAsAction")};
  // A mask thumbnail's entries export the mask itself: the menu's own actions.
  const QStringList mask_actions{QStringLiteral("layerMaskQuickExportPngAction"),
                                 QStringLiteral("layerMaskExportAsAction")};

  auto seen = read_thumbnail_menu(window, *layers, QStringLiteral("Spot"), QStringLiteral("layerContentThumbnail"),
                                  "issue462_thumbnail_menu");
  CHECK(seen.name == QStringLiteral("layerThumbnailMenu"));
  CHECK(seen.entries.mid(4) == (QStringList{QStringLiteral("-"), QStringLiteral("layerThumbnailExportMenu"),
                                            QStringLiteral("-"), QStringLiteral("layerThumbnailSizeMenu"),
                                            QStringLiteral("layerThumbnailDisplayMenu")}));
  CHECK(seen.export_entries == both_actions);
  // The thumbnail bounds pair sits in its own submenu under Thumbnail Size (#481), the current
  // setting checked.
  seen = read_thumbnail_menu(window, *layers, QStringLiteral("Spot"), QStringLiteral("layerContentThumbnail"),
                             "issue481_thumbnail_menu", QStringLiteral("layerThumbnailDisplayMenu"), "_display_open");
  const auto zoomed = patchy::ui::app_settings().value(QStringLiteral("view/zoomLayerThumbnailsToContent"), true).toBool();
  CHECK(seen.display_entries ==
        (QStringList{QStringLiteral("layerThumbnailBoundsLayerAction") + (zoomed ? QStringLiteral("*") : QString()),
                     QStringLiteral("layerThumbnailBoundsDocumentAction") + (zoomed ? QString() : QStringLiteral("*"))}));

  seen = read_thumbnail_menu(window, *layers, QStringLiteral("Masked"), QStringLiteral("layerMaskThumbnail"),
                             "issue462_mask_thumbnail_menu");
  CHECK(seen.name == QStringLiteral("layerMaskThumbnailMenu"));
  CHECK(seen.entries.size() >= 2 && seen.entries.mid(seen.entries.size() - 2) ==
                                        (QStringList{QStringLiteral("-"), QStringLiteral("layerThumbnailExportMenu")}));
  CHECK(seen.export_entries == mask_actions);

  seen = read_thumbnail_menu(window, *layers, QStringLiteral("Vector Masked"),
                             QStringLiteral("layerVectorMaskThumbnail"));
  CHECK(seen.name == QStringLiteral("layerVectorMaskThumbnailMenu"));
  CHECK(seen.entries.size() >= 2 && seen.entries.mid(seen.entries.size() - 2) ==
                                        (QStringList{QStringLiteral("-"), QStringLiteral("layerThumbnailExportMenu")}));
  CHECK(seen.export_entries == mask_actions);
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

// A layer whose 20x20 vector mask square spans (10, 10) to (30, 30).
patchy::LayerVectorMask square_vector_mask() {
  patchy::LiveShapeParams params;
  params.kind = patchy::LiveShapeKind::Rectangle;
  params.left = 10;
  params.top = 10;
  params.right = 30;
  params.bottom = 30;
  params.index = 0;
  patchy::populate_live_shape_box_corners(params);
  patchy::LayerVectorMask mask;
  mask.path.subpaths = patchy::generate_live_shape_subpaths(params);
  return mask;
}

// Right-clicks `part` of the named row and picks `action_name` in its Export submenu.
void pick_thumbnail_export_entry(QListWidget& layers, const QString& row_name, const QString& part,
                                 const QString& action_name, bool& picked) {
  QTimer::singleShot(0, [&layers, action_name, &picked] {
    static_cast<void>(layers);
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || !menu->isVisible()) {
        continue;
      }
      auto* export_menu = menu->findChild<QMenu*>(QStringLiteral("layerThumbnailExportMenu"));
      QAction* entry = nullptr;
      for (auto* action : export_menu != nullptr ? export_menu->actions() : QList<QAction*>{}) {
        if (action->objectName() == action_name) {
          entry = action;
        }
      }
      CHECK(entry != nullptr);
      if (entry == nullptr) {
        menu->close();
        return;
      }
      picked = true;
      menu->setActiveAction(export_menu->menuAction());
      QTest::keyClick(menu, Qt::Key_Right);
      QApplication::processEvents();
      export_menu->setActiveAction(entry);
      QTest::keyClick(export_menu, Qt::Key_Return);
      return;
    }
  });
  auto* row = layers.itemWidget(require_layer_item(layers, row_name));
  auto* thumbnail = row != nullptr ? row->findChild<QWidget*>(part) : nullptr;
  CHECK(thumbnail != nullptr);
  if (thumbnail != nullptr) {
    const auto local = thumbnail->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, local, thumbnail->mapToGlobal(local));
    QApplication::sendEvent(thumbnail, &event);
  }
  QApplication::processEvents();
}

// Accepts the named file dialog with `path` whenever it comes up (polls, so it also catches a
// dialog opened from a menu's own event loop).
struct FileDialogAcceptor {
  QTimer poll;
  bool seen{false};
  QString suggested;  // the file name the dialog showed
  int polls{0};

  FileDialogAcceptor(const QString& object_name, const QString& path) {
    QObject::connect(&poll, &QTimer::timeout, [this, object_name, path] {
      auto* dialog = qobject_cast<QFileDialog*>(find_top_level_dialog(object_name));
      if (dialog == nullptr || !dialog->isVisible()) {
        // Some other dialog (or none) for 5 s: close it so a wrong flow fails instead of hanging.
        if (++polls > 500) {
          poll.stop();
          if (auto* modal = QApplication::activeModalWidget(); modal != nullptr) {
            std::printf("[INFO] unexpected modal dialog: %s\n", qPrintable(modal->objectName()));
            modal->close();
          }
        }
        return;
      }
      poll.stop();
      seen = true;
      const auto selected = dialog->selectedFiles();
      suggested = selected.isEmpty() ? QString() : QFileInfo(selected.front()).fileName();
      if (dialog->fileMode() == QFileDialog::Directory) {
        dialog->setDirectory(path);
      }
      dialog->selectFile(path);
      // selectFile leaves the name box alone while it has focus (it does on macOS), and accept
      // reads that box: put the path there directly.
      if (auto* name_edit = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit")); name_edit != nullptr) {
        name_edit->setText(path);
      }
      static_cast<QDialog*>(dialog)->accept();  // QFileDialog::accept is protected
    });
    poll.start(10);
  }
};

// Export from a mask thumbnail writes the mask itself (#462 follow-up): a canvas-size 8-bit
// grayscale PNG, untrimmed, white where it reveals, the default color outside the raster
// mask's rect, a vector mask's path coverage (inside white, outside black); several selected
// layers write one mask each and skip a layer without that mask.
void ui_layer_mask_thumbnail_export_writes_the_mask() {
  using patchy::test::kUnicodePathStems;
  const auto stem = QString::fromStdString(patchy::test::utf8_string(kUnicodePathStems[1]));
  const auto folder = patchy::ui::to_qstring(
      std::filesystem::absolute(patchy::test::unicode_artifact_dir(u8"layer-mask-export")));
  const QDir directory(folder);
  patchy::ui::MainWindow window;
  patchy::Document built(64, 48, patchy::PixelFormat::rgba8());
  // A small red patch whose raster mask covers (8, 4) to (40, 24): black on its left half,
  // 200 on its right half, white (the default color) outside it.
  auto masked = solid_layer(built, patchy::test::utf8_string(kUnicodePathStems[1]), QRect(20, 10, 6, 6),
                            QColor(220, 20, 20));
  patchy::PixelBuffer mask(32, 20, patchy::PixelFormat::gray8());
  for (int y = 0; y < mask.height(); ++y) {
    for (int x = 0; x < mask.width(); ++x) {
      *mask.pixel(x, y) = x < 16 ? std::uint8_t{0} : std::uint8_t{200};
    }
  }
  masked.set_mask(patchy::LayerMask{patchy::Rect{8, 4, 32, 20}, std::move(mask), 255, false});
  const auto masked_id = masked.id();
  built.add_layer(std::move(masked));
  auto plain = solid_layer(built, "Plain", QRect(0, 0, 8, 8), QColor(20, 200, 20));
  const auto plain_id = plain.id();
  built.add_layer(std::move(plain));
  auto shaped = solid_layer(built, "Shaped", QRect(0, 0, 64, 48), QColor(20, 20, 220));
  shaped.set_vector_mask(square_vector_mask());
  patchy::update_vector_mask_raster(shaped, patchy::Rect{0, 0, 64, 48});
  const auto shaped_id = shaped.id();
  built.add_layer(std::move(shaped));
  auto framed = solid_layer(built, "Framed", QRect(0, 0, 64, 48), QColor(200, 200, 20));
  framed.set_vector_mask(square_vector_mask());
  patchy::update_vector_mask_raster(framed, patchy::Rect{0, 0, 64, 48});
  const auto framed_id = framed.id();
  built.add_layer(std::move(framed));
  window.add_document_session(std::move(built), QStringLiteral("Mask Export"));
  show_window(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  if (layers == nullptr) {
    return;
  }

  // One layer: the save dialog suggests "<layer> mask"; the PNG is the whole mask.
  patchy::ui::MainWindowTestAccess::select_layers(window, {masked_id}, masked_id);
  QApplication::processEvents();
  const auto single = directory.filePath(stem + QStringLiteral(" single.png"));
  {
    FileDialogAcceptor accept(QStringLiteral("layerQuickExportFileDialog"), single);
    bool picked = false;
    pick_thumbnail_export_entry(*layers, stem, QStringLiteral("layerMaskThumbnail"),
                                QStringLiteral("layerMaskQuickExportPngAction"), picked);
    process_events_until([&] { return accept.seen && QFileInfo::exists(single); });
    CHECK(picked);
    CHECK(accept.seen);
    CHECK(accept.suggested == stem + QStringLiteral(" mask.png"));
  }
  const QImage raster(single, "PNG");
  CHECK(raster.size() == QSize(64, 48));
  CHECK(raster.format() == QImage::Format_Grayscale8);
  CHECK(raster.isGrayscale());
  CHECK(qGray(raster.pixel(2, 2)) == 255);    // outside the rect: the default color
  CHECK(qGray(raster.pixel(10, 10)) == 0);    // the black half
  CHECK(qGray(raster.pixel(30, 10)) == 200);  // the 200 half
  CHECK(qGray(raster.pixel(60, 40)) == 255);
  save_image_artifact("issue462_mask_export", raster);
  if (auto* panel = layers->parentWidget(); panel != nullptr) {
    save_widget_artifact("issue462_mask_export_panel", *panel);
  }

  // Several layers from the vector mask menu: one file per vector mask, the two layers without
  // one skipped.
  patchy::ui::MainWindowTestAccess::select_layers(window, {masked_id, plain_id, shaped_id, framed_id}, shaped_id);
  QApplication::processEvents();
  {
    FileDialogAcceptor accept(QStringLiteral("layerQuickExportFolderDialog"), folder);
    bool picked = false;
    pick_thumbnail_export_entry(*layers, QStringLiteral("Shaped"), QStringLiteral("layerVectorMaskThumbnail"),
                                QStringLiteral("layerMaskQuickExportPngAction"), picked);
    process_events_until([&] {
      return accept.seen && QFileInfo::exists(directory.filePath(QStringLiteral("Shaped vector mask.png")));
    });
    CHECK(picked);
    CHECK(accept.seen);
  }
  const QImage vector(directory.filePath(QStringLiteral("Shaped vector mask.png")), "PNG");
  CHECK(vector.size() == QSize(64, 48));
  CHECK(vector.format() == QImage::Format_Grayscale8);
  CHECK(qGray(vector.pixel(20, 20)) == 255);  // inside the square
  CHECK(qGray(vector.pixel(2, 2)) == 0);      // outside it
  CHECK(qGray(vector.pixel(50, 40)) == 0);
  CHECK(QImage(directory.filePath(QStringLiteral("Framed vector mask.png")), "PNG").size() == QSize(64, 48));
  // The two layers without a vector mask are skipped and the status bar says so.
  CHECK(!QFileInfo::exists(directory.filePath(stem + QStringLiteral(" vector mask.png"))));
  CHECK(!QFileInfo::exists(directory.filePath(QStringLiteral("Plain vector mask.png"))));
  const auto message = window.statusBar()->currentMessage();
  CHECK(message.contains(QStringLiteral("Exported 2 masks")) && message.contains(QStringLiteral("2 layers")));

  // The pixel thumbnail menu keeps exporting the layer as composited and trimmed: of the 6x6
  // patch at (20, 10), only the two columns over the mask's 200 half show.
  patchy::ui::MainWindowTestAccess::select_layers(window, {masked_id}, masked_id);
  QApplication::processEvents();
  const auto layer_file = directory.filePath(QStringLiteral("layer.png"));
  {
    FileDialogAcceptor accept(QStringLiteral("layerQuickExportFileDialog"), layer_file);
    bool picked = false;
    pick_thumbnail_export_entry(*layers, stem, QStringLiteral("layerContentThumbnail"),
                                QStringLiteral("layerQuickExportPngAction"), picked);
    process_events_until([&] { return accept.seen && QFileInfo::exists(layer_file); });
    CHECK(picked);
  }
  const auto composited = read_png(layer_file);
  CHECK(composited.size() == QSize(2, 6));
  CHECK(qAlpha(composited.pixel(0, 0)) == 200);
}

// With thumbnails zoomed to content (the default), the layer and vector mask previews frame
// the mask itself, not the layer's pixels (#462 follow-up): a canvas-size mask keeps the
// document's shape beside a small layer's square tile, and a vector mask shows the canvas.
void ui_layer_mask_thumbnails_frame_the_mask() {
  SettingsValueRestorer restore_zoom(QStringLiteral("view/zoomLayerThumbnailsToContent"));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("view/zoomLayerThumbnailsToContent"), true);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  patchy::Document built(160, 40, patchy::PixelFormat::rgba8());
  auto blob = solid_layer(built, "Blob", QRect(10, 10, 20, 20), QColor(220, 20, 20));
  patchy::PixelBuffer mask(160, 40, patchy::PixelFormat::gray8());
  mask.clear(255);
  for (int y = 0; y < 40; ++y) {
    for (int x = 100; x < 160; ++x) {
      *mask.pixel(x, y) = 0;
    }
  }
  blob.set_mask(patchy::LayerMask{patchy::Rect{0, 0, 160, 40}, std::move(mask), 255, false});
  built.add_layer(std::move(blob));
  auto strip = solid_layer(built, "Strip", QRect(0, 0, 160, 10), QColor(20, 20, 220));
  strip.set_vector_mask(square_vector_mask());
  patchy::update_vector_mask_raster(strip, patchy::Rect{0, 0, 160, 40});
  built.add_layer(std::move(strip));
  window.add_document_session(std::move(built), QStringLiteral("Mask Thumbnails"));
  show_window(window);
  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  if (layers == nullptr) {
    return;
  }
  const auto pixmap = [layers](const QString& row_name, const QString& part) {
    auto* row = layers->itemWidget(require_layer_item(*layers, row_name));
    auto* label = row != nullptr ? row->findChild<QLabel*>(part) : nullptr;
    CHECK(label != nullptr);
    return label != nullptr ? label->pixmap(Qt::ReturnByValue) : QPixmap();
  };
  if (auto* panel = layers->parentWidget(); panel != nullptr) {
    save_widget_artifact("issue462_mask_thumbnails", *panel);
  }
  // The 20x20 blob zooms to a square tile; its canvas-size mask keeps the canvas's 4:1 shape
  // and shows the black right part that lies outside the blob.
  CHECK(pixmap(QStringLiteral("Blob"), QStringLiteral("layerContentThumbnail")).size() == QSize(28, 28));
  const auto mask_image = pixmap(QStringLiteral("Blob"), QStringLiteral("layerMaskThumbnail")).toImage();
  CHECK(mask_image.size() == QSize(28, 7));
  CHECK(qGray(mask_image.pixel(4, 3)) == 255);
  CHECK(qGray(mask_image.pixel(24, 3)) == 0);
  // The strip's pixels zoom to a 16:1 tile; its vector mask shows the whole canvas: the white
  // square at the left, black elsewhere.
  const auto vector_image = pixmap(QStringLiteral("Strip"), QStringLiteral("layerVectorMaskThumbnail")).toImage();
  CHECK(vector_image.size() == QSize(28, 7));
  CHECK(qGray(vector_image.pixel(3, 3)) > 200);
  CHECK(qGray(vector_image.pixel(20, 3)) == 0);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_export_tests() {
  return {
      {"ui_layer_context_menu_fits_short_screens", ui_layer_context_menu_fits_short_screens},
      {"ui_layer_thumbnail_menu_offers_layer_export", ui_layer_thumbnail_menu_offers_layer_export},
      {"ui_layer_quick_export_png_writes_trimmed_named_files", ui_layer_quick_export_png_writes_trimmed_named_files},
      {"ui_layer_export_as_uses_chosen_format_and_options", ui_layer_export_as_uses_chosen_format_and_options},
      {"ui_layer_mask_thumbnail_export_writes_the_mask", ui_layer_mask_thumbnail_export_writes_the_mask},
      {"ui_layer_mask_thumbnails_frame_the_mask", ui_layer_mask_thumbnails_frame_the_mask},
  };
}
