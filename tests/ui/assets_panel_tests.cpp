// The Assets panel (GitHub issue 751, docs/assets-panel.md): layers saved for reuse in any
// document. The library's storage, the panel, adding layers, placing assets by drag,
// double-click and menu, and the panel's place in the saved layout.
#include "core/document.hpp"
#include "core/layer.hpp"
#include "core/layer_metadata.hpp"
#include "core/smart_object.hpp"
#include "core/vector_live_shapes.hpp"
#include "core/vector_raster.hpp"
#include "core/vector_shape.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/asset_library.hpp"
#include "ui/asset_thumbnails.hpp"
#include "ui/assets_panel.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/image_document_io.hpp"
#include "ui/layer_list_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/panel_column.hpp"
#include "ui/qt_paths.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrl>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;
using namespace patchy::test::ui::scripting_support;
using patchy::test::kUnicodeCombinedStem;
using patchy::test::unicode_artifact_dir;
using patchy::test::unicode_path_piece;
using patchy::test::utf8_string;
using patchy::ui::AssetLibrary;
using patchy::ui::AssetThumbnailState;
using patchy::ui::AssetsPanel;
using patchy::ui::MainWindowTestAccess;

const QString kAssetsPanelId = QStringLiteral("assetsDock");

// Points the next MainWindow's asset library at an empty folder of this test's own, and
// puts the suite's folder back after it.
struct ScopedAssetsDir {
  QByteArray previous;
  QString path;
  SettingsValueRestorer category{QStringLiteral("assets/currentCategory")};

  explicit ScopedAssetsDir(const char* leaf) : previous(qgetenv("PATCHY_ASSETS_DIR")) {
    path = QDir::current().filePath(QStringLiteral("test-artifacts/assets-tests/%1-%2")
                                        .arg(QLatin1String(leaf))
                                        .arg(QCoreApplication::applicationPid()));
    if (QFileInfo::exists(path)) {
      CHECK(remove_test_scratch_dir(path));
    }
    qputenv("PATCHY_ASSETS_DIR", path.toUtf8());
    patchy::ui::app_settings().remove(QStringLiteral("assets/currentCategory"));
  }
  ~ScopedAssetsDir() {
    qputenv("PATCHY_ASSETS_DIR", previous);
    if (QFileInfo::exists(path)) {
      remove_test_scratch_dir(path);
    }
  }
  ScopedAssetsDir(const ScopedAssetsDir&) = delete;
  ScopedAssetsDir& operator=(const ScopedAssetsDir&) = delete;
};

// Bytes the library takes for an asset file: it checks the signature only.
std::vector<std::uint8_t> fake_psd_bytes(std::uint8_t fill) {
  std::vector<std::uint8_t> bytes(64, fill);
  bytes[0] = '8';
  bytes[1] = 'B';
  bytes[2] = 'P';
  bytes[3] = 'S';
  return bytes;
}

QImage solid_thumbnail(QColor color) {
  QImage image(32, 24, QImage::Format_RGBA8888);
  image.fill(color);
  return image;
}

QStringList asset_names(const patchy::ui::AssetCategory& category) {
  QStringList names;
  for (const auto& asset : category.assets) {
    names.push_back(asset.name);
  }
  return names;
}

QStringList folder_files(const QString& directory) {
  auto names = QDir(directory).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
  return names;
}

patchy::Document plain_document(int width, int height, QColor color) {
  patchy::Document document(width, height, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", solid_pixels(width, height, patchy::PixelFormat::rgba8(), color));
  return document;
}

patchy::Layer shape_layer(patchy::Document& document, const char* name, patchy::Rect box, patchy::RgbColor color) {
  patchy::LiveShapeParams params;
  params.kind = patchy::LiveShapeKind::Rectangle;
  params.left = box.x;
  params.top = box.y;
  params.right = box.x + box.width;
  params.bottom = box.y + box.height;
  patchy::VectorShapeContent shape;
  shape.path.subpaths = patchy::generate_live_shape_subpaths(params);
  shape.fill.color = color;
  patchy::Layer layer(document.allocate_layer_id(), name, patchy::LayerKind::Pixel);
  layer.metadata()[patchy::kLayerMetadataVectorShape] = "1";
  layer.set_vector_shape(std::move(shape));
  patchy::update_vector_shape_raster(layer, patchy::Rect::from_size(document.width(), document.height()),
                                     &std::as_const(document).metadata().patterns);
  return layer;
}

// Bottom to top: Background, "Mark" (20x12 at 10,8), folder "Set" holding "Inner" (8x8 at
// 40,30), and the shape "Badge" (a 20x10 rectangle at 8,30). "Mark" is active.
patchy::Document asset_source_document() {
  auto document = plain_document(64, 48, QColor(255, 255, 255));
  patchy::Layer mark(document.allocate_layer_id(), "Mark",
                     solid_pixels(20, 12, patchy::PixelFormat::rgba8(), QColor(200, 30, 30)));
  mark.set_bounds(patchy::Rect{10, 8, 20, 12});
  document.add_layer(std::move(mark));
  patchy::Layer group(document.allocate_layer_id(), "Set", patchy::LayerKind::Group);
  patchy::Layer inner(document.allocate_layer_id(), "Inner",
                      solid_pixels(8, 8, patchy::PixelFormat::rgba8(), QColor(30, 200, 30)));
  inner.set_bounds(patchy::Rect{40, 30, 8, 8});
  group.add_child(std::move(inner));
  document.add_layer(std::move(group));
  document.add_layer(shape_layer(document, "Badge", patchy::Rect{8, 30, 20, 10}, patchy::RgbColor{20, 80, 140}));
  document.set_active_layer(document.layers()[1].id());
  return document;
}

patchy::LayerId layer_id_named(const patchy::Document& document, const char* name) {
  for (const auto& layer : document.layers()) {
    if (layer.name() == name) {
      return layer.id();
    }
  }
  CHECK(false);
  return 0;
}

const patchy::Layer* layer_named(const patchy::Document& document, const char* name) {
  for (const auto& layer : document.layers()) {
    if (layer.name() == name) {
      return &layer;
    }
  }
  return nullptr;
}

struct AssetWindow {
  AssetLibrary* library{nullptr};
  AssetsPanel* panel{nullptr};
  QListWidget* grid{nullptr};
};

// Pumps events until the Assets panel has nothing left to read or write in the background:
// the library's load and file check, assets being written, and the thumbnails of the items
// the grid has painted (a paint may ask for more, so several quiet passes in a row).
void settle_assets(patchy::ui::MainWindow& window) {
  const auto* panel = window.findChild<AssetsPanel*>(QStringLiteral("assetsPanel"));
  CHECK(panel != nullptr);
  if (panel == nullptr) {
    return;
  }
  int quiet = 0;
  for (int pass = 0; pass < 120000 && quiet < 4; ++pass) {
    QApplication::processEvents(QEventLoop::AllEvents);
    if (panel->busy()) {
      quiet = 0;
      QThread::msleep(1);  // the worker's turn: processEvents returns at once with nothing queued
    } else {
      ++quiet;
    }
  }
  CHECK(!panel->busy());
}

AssetWindow asset_widgets(patchy::ui::MainWindow& window) {
  settle_assets(window);
  AssetWindow widgets;
  widgets.library = window.findChild<AssetLibrary*>();
  widgets.panel = window.findChild<AssetsPanel*>(QStringLiteral("assetsPanel"));
  widgets.grid = window.findChild<QListWidget*>(QStringLiteral("assetsGrid"));
  CHECK(widgets.library != nullptr && widgets.panel != nullptr && widgets.grid != nullptr);
  return widgets;
}

// Selects the named layers of the active document and runs Add to Assets.
void add_layers_as_assets(patchy::ui::MainWindow& window, const patchy::Document& document,
                          const std::vector<const char*>& names) {
  std::vector<patchy::LayerId> ids;
  for (const auto* name : names) {
    ids.push_back(layer_id_named(document, name));
  }
  MainWindowTestAccess::select_layers(window, ids, ids.front());
  QApplication::processEvents();
  require_action(window, "layerAddToAssetsAction")->trigger();
  // The assets are encoded and written on the library's worker and listed when whole.
  settle_assets(window);
}

QString asset_id_named(const AssetLibrary& library, const QString& name) {
  for (const auto& category : library.categories()) {
    for (const auto& asset : category.assets) {
      if (asset.name == name) {
        return asset.id;
      }
    }
  }
  CHECK(false);
  return {};
}

struct AssetDrop {
  bool entered{false};
  bool move_accepted{false};
  bool drop_accepted{false};
};

// An asset drag out of the panel: enter, move and drop on `target`. The placement is
// deferred past the drop, so the caller pumps events.
AssetDrop send_asset_drop(QWidget& target, QPoint position, const QStringList& asset_ids, bool own_process = true) {
  QMimeData mime;
  mime.setData(QLatin1String(patchy::ui::kAssetDragMimeType), asset_ids.join(QLatin1Char('\n')).toUtf8());
  mime.setData(QLatin1String(patchy::ui::kAssetDragSourceMimeType),
               own_process ? QByteArray::number(QCoreApplication::applicationPid()) : QByteArray("1"));
  const Qt::DropActions actions = Qt::CopyAction;
  AssetDrop result;
  QDragEnterEvent enter(position, actions, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&target, &enter);
  result.entered = enter.isAccepted();
  QDragMoveEvent move(position, actions, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&target, &move);
  result.move_accepted = move.isAccepted();
  QDropEvent drop(QPointF(position), actions, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&target, &drop);
  result.drop_accepted = drop.isAccepted();
  return result;
}

bool image_has_opaque_pixel(const QImage& image) {
  const auto converted = image.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < converted.height(); ++y) {
    for (int x = 0; x < converted.width(); ++x) {
      if (qAlpha(converted.pixel(x, y)) > 200) {
        return true;
      }
    }
  }
  return false;
}

// Storage: add, reload, rename, move between categories, sort, delete, and what the folder
// holds afterwards.
void ui_asset_library_adds_reloads_renames_moves_and_deletes() {
  ScopedAssetsDir dir("library");
  QString first;
  QString second;
  QString third;
  QString extra_category;
  {
    AssetLibrary library(dir.path);
    library.load();
    // Loading creates nothing on disk and always has one category.
    CHECK(!QFileInfo::exists(dir.path));
    CHECK(library.categories().size() == 1U);
    CHECK(library.categories().front().id == QLatin1String(patchy::ui::kDefaultAssetCategoryId));
    int changes = 0;
    QObject::connect(&library, &AssetLibrary::changed, [&changes] { ++changes; });

    QString error;
    const auto added_first = library.add_asset(QLatin1String(patchy::ui::kDefaultAssetCategoryId),
                                               QStringLiteral("  Zebra\nstripes "), fake_psd_bytes(1),
                                               solid_thumbnail(QColor(200, 0, 0)), &error);
    const auto added_second = library.add_asset(QString(), QStringLiteral("apple"), fake_psd_bytes(2),
                                                solid_thumbnail(QColor(0, 200, 0)), &error);
    const auto added_third = library.add_asset(QString(), QStringLiteral("Mango 10"), fake_psd_bytes(3), QImage(), &error);
    CHECK(added_first.has_value() && added_second.has_value() && added_third.has_value());
    CHECK(error.isEmpty());
    CHECK(changes == 3);
    first = added_first.value_or(QString());
    second = added_second.value_or(QString());
    third = added_third.value_or(QString());
    // Names are cleaned to one line; file names are generated ids, never the name.
    CHECK(asset_names(library.categories().front()) ==
          (QStringList{QStringLiteral("Zebra stripes"), QStringLiteral("apple"), QStringLiteral("Mango 10")}));
    CHECK(AssetLibrary::is_asset_id(first) && first != second);
    CHECK(QFileInfo(library.asset_path(first)).fileName() == first + QStringLiteral(".psd"));
    // Every write went through a QSaveFile: the folder holds the finished files only.
    QStringList expected{QStringLiteral("index.json"), first + QStringLiteral(".png"), first + QStringLiteral(".psd"),
                         second + QStringLiteral(".png"), second + QStringLiteral(".psd"),
                         third + QStringLiteral(".psd")};
    expected.sort();
    CHECK(folder_files(dir.path) == expected);
    CHECK(library.thumbnail(first).size() == QSize(32, 24));
    CHECK(library.thumbnail(third).isNull());
    CHECK(library.read_asset_bytes(second).size() == 64);

    const auto category = library.add_category(QStringLiteral("Logos"), &error);
    CHECK(category.has_value());
    extra_category = category.value_or(QString());
    CHECK(!library.add_category(QStringLiteral("   ")).has_value());
    CHECK(library.move_asset(second, extra_category, &error));
    CHECK(library.category_of(second) == extra_category);
    CHECK(library.rename_asset(first, QStringLiteral("Tiger"), &error));
    CHECK(library.rename_category(QLatin1String(patchy::ui::kDefaultAssetCategoryId), QStringLiteral("Main"), &error));
    CHECK(library.sort_category_by_name(QLatin1String(patchy::ui::kDefaultAssetCategoryId), &error));
    CHECK(asset_names(library.categories().front()) == (QStringList{QStringLiteral("Mango 10"), QStringLiteral("Tiger")}));
  }
  {
    // A new library over the same folder reads everything back.
    AssetLibrary library(dir.path);
    library.load();
    CHECK(library.categories().size() == 2U);
    CHECK(library.categories()[0].name == QStringLiteral("Main"));
    CHECK(asset_names(library.categories()[0]) == (QStringList{QStringLiteral("Mango 10"), QStringLiteral("Tiger")}));
    CHECK(library.categories()[1].id == extra_category && library.categories()[1].name == QStringLiteral("Logos"));
    CHECK(asset_names(library.categories()[1]) == QStringList{QStringLiteral("apple")});
    CHECK(library.asset_count() == 3);
    CHECK(library.find_asset(first) != nullptr && library.find_asset(first)->name == QStringLiteral("Tiger"));

    // Deleting an asset removes its files; deleting a category removes the assets in it.
    CHECK(library.remove_asset(first));
    CHECK(!QFileInfo::exists(library.asset_path(first)) && !QFileInfo::exists(library.thumbnail_path(first)));
    CHECK(library.remove_category(extra_category));
    CHECK(!QFileInfo::exists(library.asset_path(second)));
    CHECK(library.asset_count() == 1);
    // The last category stays.
    CHECK(!library.remove_category(library.categories().front().id));
    CHECK(library.categories().size() == 1U);
    QStringList expected{QStringLiteral("index.json"), third + QStringLiteral(".psd")};
    expected.sort();
    CHECK(folder_files(dir.path) == expected);
  }
  // The index is plain JSON with the format's own tokens.
  QFile index(QDir(dir.path).filePath(QStringLiteral("index.json")));
  CHECK(index.open(QIODevice::ReadOnly));
  const auto root = QJsonDocument::fromJson(index.readAll()).object();
  CHECK(root.value(QStringLiteral("format")).toString() == QStringLiteral("patchy-assets"));
  CHECK(root.value(QStringLiteral("version")).toInt() == 1);
}

// A missing, damaged or foreign file never breaks the library: it is left out and the rest
// loads. A damaged index is rebuilt from the asset files.
void ui_asset_library_skips_damaged_and_foreign_files() {
  ScopedAssetsDir dir("damaged");
  QString good;
  QString missing;
  QString not_psd;
  {
    AssetLibrary library(dir.path);
    good = library.add_asset(QString(), QStringLiteral("Good"), fake_psd_bytes(1), QImage()).value_or(QString());
    missing = library.add_asset(QString(), QStringLiteral("Missing"), fake_psd_bytes(2), QImage()).value_or(QString());
    not_psd = library.add_asset(QString(), QStringLiteral("Damaged"), fake_psd_bytes(3), QImage()).value_or(QString());
    CHECK(!good.isEmpty() && !missing.isEmpty() && !not_psd.isEmpty());
  }
  const QDir folder(dir.path);
  CHECK(QFile::remove(folder.filePath(missing + QStringLiteral(".psd"))));
  {
    QFile damaged(folder.filePath(not_psd + QStringLiteral(".psd")));
    CHECK(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    damaged.write("this is not a PSD");
  }
  {
    QFile note(folder.filePath(QStringLiteral("notes.txt")));
    CHECK(note.open(QIODevice::WriteOnly));
    note.write("someone's notes");
    QFile stray(folder.filePath(QStringLiteral("holiday photo.psd")));
    CHECK(stray.open(QIODevice::WriteOnly));
    const auto bytes = fake_psd_bytes(9);
    stray.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  }
  {
    AssetLibrary library(dir.path);
    library.load();
    CHECK(library.asset_count() == 1);
    CHECK(library.find_asset(good) != nullptr);
    CHECK(library.find_asset(missing) == nullptr && library.find_asset(not_psd) == nullptr);
  }
  // Nothing was deleted or rewritten by loading.
  CHECK(QFileInfo::exists(folder.filePath(QStringLiteral("notes.txt"))));
  CHECK(QFileInfo::exists(folder.filePath(QStringLiteral("holiday photo.psd"))));
  CHECK(QFileInfo::exists(folder.filePath(not_psd + QStringLiteral(".psd"))));

  // An index that is not JSON: the asset files present come back in the first category,
  // named by the window's provider, and a file with someone else's name stays out.
  {
    QFile index(folder.filePath(QStringLiteral("index.json")));
    CHECK(index.open(QIODevice::WriteOnly | QIODevice::Truncate));
    index.write("{ not json at all");
  }
  {
    AssetLibrary library(dir.path);
    library.set_recovered_name_provider([](const QString& path) { return QFileInfo(path).completeBaseName().left(4); });
    library.load();
    CHECK(library.categories().size() == 1U);
    CHECK(library.asset_count() == 1);
    CHECK(library.find_asset(good) != nullptr);
    if (const auto* asset = library.find_asset(good); asset != nullptr) {
      CHECK(asset->name == good.left(4));
    }
    // The next change writes a good index again.
    CHECK(library.rename_asset(good, QStringLiteral("Recovered")));
  }
  AssetLibrary reloaded(dir.path);
  reloaded.load();
  CHECK(reloaded.find_asset(good) != nullptr && reloaded.find_asset(good)->name == QStringLiteral("Recovered"));
}

// The library folder and the asset names may hold any Unicode text.
void ui_unicode_asset_library_round_trips() {
  const auto folder = unicode_artifact_dir(u8"assets 資産 \U0001F5BC");
  const auto directory = patchy::ui::to_qstring(std::filesystem::absolute(folder / unicode_path_piece(kUnicodeCombinedStem)));
  if (QFileInfo::exists(directory)) {
    CHECK(remove_test_scratch_dir(directory));
  }
  const auto name = QString::fromUtf8(utf8_string(kUnicodeCombinedStem).c_str());
  QString id;
  {
    AssetLibrary library(directory);
    QString error;
    const auto added = library.add_asset(QString(), name, fake_psd_bytes(7), solid_thumbnail(QColor(10, 20, 30)), &error);
    CHECK(added.has_value());
    CHECK(error.isEmpty());
    id = added.value_or(QString());
    const auto category = library.add_category(name, &error);
    CHECK(category.has_value());
    CHECK(library.move_asset(id, category.value_or(QString()), &error));
  }
  AssetLibrary library(directory);
  library.load();
  CHECK(library.asset_count() == 1);
  const auto* asset = library.find_asset(id);
  CHECK(asset != nullptr && asset->name == name);
  CHECK(library.categories().size() == 2U && library.categories()[1].name == name);
  const auto bytes = library.read_asset_bytes(id);
  CHECK(bytes.size() == 64 && bytes.startsWith("8BPS") && static_cast<std::uint8_t>(bytes.back()) == 7);
  CHECK(!library.thumbnail(id).isNull());
  // The file on disk is named by its id under the Unicode folder.
  CHECK(std::filesystem::exists(patchy::ui::to_filesystem_path(library.asset_path(id))));
  CHECK(remove_test_scratch_dir(directory));
}

// The panel is closed on a first launch and after Reset Panels; Window > Assets opens it.
void ui_assets_panel_is_closed_by_default_and_opens_from_window_menu() {
  ScopedAssetsDir dir("default-closed");
  patchy::ui::MainWindow window;
  show_window(window);
  auto* column = MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr);
  if (column == nullptr) {
    return;
  }
  CHECK(column->has_panel(kAssetsPanelId));
  CHECK(!column->panel_visible(kAssetsPanelId));
  // The last section, on its own, so no existing panel moved to make room for it.
  CHECK(column->section_order().back() == kAssetsPanelId);
  CHECK(!column->in_tab_group(kAssetsPanelId));
  // Nothing is read or created on disk until the panel is used.
  CHECK(!QFileInfo::exists(dir.path));

  auto* entry = require_action(window, "windowPanelAssetsAction");
  CHECK(entry->isCheckable() && !entry->isChecked());
  CHECK(entry->text().remove(QLatin1Char('&')) == QStringLiteral("Assets"));
  CHECK(entry->menuRole() == QAction::NoRole);
  entry->trigger();
  QApplication::processEvents();
  CHECK(column->panel_visible(kAssetsPanelId));
  CHECK(column->panel_expanded(kAssetsPanelId));
  CHECK(entry->isChecked());
  // The usual title row names (the theme and the MCP allowlist go by them).
  CHECK(window.findChild<QWidget*>(QStringLiteral("assetsDockTitle")) != nullptr);
  CHECK(window.findChild<QLabel*>(QStringLiteral("assetsDockTitleLabel")) != nullptr);
  CHECK(window.findChild<QToolButton*>(QStringLiteral("assetsDockCollapseButton")) != nullptr);
  const auto widgets = asset_widgets(window);
  CHECK(widgets.panel->isVisible());
  // Empty: the hint shows in place of the grid.
  auto* hint = window.findChild<QLabel*>(QStringLiteral("assetsEmptyHint"));
  CHECK(hint != nullptr && hint->isVisible() && !widgets.grid->isVisible());
  CHECK(hint->text().contains(QStringLiteral("Add Selected Layers")));
  CHECK(window.findChild<QLineEdit*>(QStringLiteral("assetsSearchEdit")) != nullptr);
  CHECK(window.findChild<QComboBox*>(QStringLiteral("assetsCategoryCombo")) != nullptr);
  save_widget_artifact("issue_751_assets_panel_empty", window);

  CHECK(run_script(window, QStringLiteral(R"JS(
    patchy.ui.closePanel('assets');
    console.log('closed=' + patchy.ui.panelState('assets').open);
    patchy.ui.expandPanel('assets');
    var state = patchy.ui.panelState('assets');
    console.log('opened=' + state.open + ',' + state.expanded);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("closed=false")));
  CHECK(backlog_contains(window, QStringLiteral("opened=true,true")));

  require_action(window, "windowResetPanelsAction")->trigger();
  QApplication::processEvents();
  CHECK(!column->panel_visible(kAssetsPanelId));
  CHECK(column->panel_visible(QStringLiteral("layersDock")));
  CHECK(column->section_order().back() == kAssetsPanelId);
}

// A layout saved before the panel existed (version 4) loads with every choice it records
// and the Assets panel closed; the panel's own state is saved and restored after that.
void ui_panel_layout_from_before_assets_panel_keeps_it_closed() {
  ScopedAssetsDir dir("layout");
  const QStringList keys = {QStringLiteral("window/panelLayoutVersion"), QStringLiteral("window/hiddenPanels"),
                            QStringLiteral("window/collapsedPanels"), QStringLiteral("window/frontPanel"),
                            QStringLiteral("window/frontPanels"),     QStringLiteral("window/panelColumnWidth"),
                            QStringLiteral("window/panelSplitterState"), QStringLiteral("window/panelOrder"),
                            QStringLiteral("window/panelGroups")};
  std::vector<std::unique_ptr<SettingsValueRestorer>> restorers;
  for (const auto& key : keys) {
    restorers.push_back(std::make_unique<SettingsValueRestorer>(key));
  }
  const QStringList old_order{QStringLiteral("layersDock"), QStringLiteral("colorDock"), QStringLiteral("infoDock"),
                              QStringLiteral("propertiesDock"), QStringLiteral("historyDock")};
  {
    patchy::ui::MainWindow window;
    auto* column = MainWindowTestAccess::panel_column(window);
    CHECK(column != nullptr);
    if (column == nullptr) {
      return;
    }
    // The heights of the five sections a version 4 column had, as its splitter saved them.
    QByteArray old_heights;
    {
      QSplitter splitter(Qt::Vertical);
      splitter.setChildrenCollapsible(false);
      splitter.setHandleWidth(column->splitter()->handleWidth());
      for (int index = 0; index < 5; ++index) {
        splitter.addWidget(new QWidget(&splitter));
      }
      splitter.resize(300, 700);
      splitter.setSizes({380, 170, 24, 102, 24});
      old_heights = splitter.saveState();
    }
    {
      auto settings = patchy::ui::app_settings();
      for (const auto& key : keys) {
        settings.remove(key);
      }
      settings.setValue(QStringLiteral("window/panelLayoutVersion"), 4);
      settings.setValue(QStringLiteral("window/hiddenPanels"), QStringList{QStringLiteral("infoDock")});
      settings.setValue(QStringLiteral("window/collapsedPanels"),
                        QStringList{QStringLiteral("infoDock"), QStringLiteral("historyDock")});
      settings.setValue(QStringLiteral("window/frontPanels"),
                        QStringList{QStringLiteral("pathsDock"), QStringLiteral("paletteDock")});
      settings.setValue(QStringLiteral("window/panelOrder"), old_order);
      settings.setValue(QStringLiteral("window/panelGroups"),
                        QStringList{QStringLiteral("layersDock,channelsDock,pathsDock"),
                                    QStringLiteral("colorDock,paletteDock")});
      settings.setValue(QStringLiteral("window/panelSplitterState"), old_heights);
      settings.sync();
    }
    MainWindowTestAccess::read_panel_layout(window);
    show_window(window);
    // The record's own choices stand, and the new panel joins closed at the end.
    QStringList expected_order = old_order;
    expected_order << kAssetsPanelId;
    CHECK(column->section_order() == expected_order);
    CHECK(!column->panel_visible(kAssetsPanelId));
    CHECK(!column->panel_visible(QStringLiteral("infoDock")));
    CHECK(column->panel_visible(QStringLiteral("historyDock")) && !column->panel_expanded(QStringLiteral("historyDock")));
    CHECK(column->panel_expanded(QStringLiteral("propertiesDock")));
    CHECK(column->panel_in_front(QStringLiteral("pathsDock")));
    CHECK(column->panel_in_front(QStringLiteral("paletteDock")));

    // Opened and saved, the record is the current version and names the panel.
    MainWindowTestAccess::show_panel(window, kAssetsPanelId);
    QApplication::processEvents();
    CHECK(column->panel_visible(kAssetsPanelId));
    MainWindowTestAccess::write_panel_layout(window);
    const auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("window/panelLayoutVersion")).toInt() == 5);
    CHECK(settings.value(QStringLiteral("window/panelOrder")).toStringList().contains(kAssetsPanelId));
    CHECK(!settings.value(QStringLiteral("window/hiddenPanels")).toStringList().contains(kAssetsPanelId));
  }
  {
    // The next window reads the panel back open; closed and saved, it comes back closed.
    patchy::ui::MainWindow window;
    auto* column = MainWindowTestAccess::panel_column(window);
    CHECK(column != nullptr);
    if (column == nullptr) {
      return;
    }
    MainWindowTestAccess::read_panel_layout(window);
    show_window(window);
    CHECK(column->panel_visible(kAssetsPanelId));
    CHECK(column->panel_expanded(kAssetsPanelId));
    CHECK(!column->panel_visible(QStringLiteral("infoDock")));
    column->set_panel_visible(kAssetsPanelId, false);
    MainWindowTestAccess::write_panel_layout(window);
    CHECK(patchy::ui::app_settings().value(QStringLiteral("window/hiddenPanels")).toStringList().contains(kAssetsPanelId));
  }
  patchy::ui::MainWindow window;
  auto* column = MainWindowTestAccess::panel_column(window);
  CHECK(column != nullptr);
  if (column == nullptr) {
    return;
  }
  MainWindowTestAccess::read_panel_layout(window);
  show_window(window);
  CHECK(!column->panel_visible(kAssetsPanelId));
}

// Add to Assets: one asset per selected root, named after it, with a thumbnail, as a PSD
// trimmed to the layers. The document itself is untouched.
void ui_assets_add_selected_layers_creates_assets_with_thumbnails() {
  ScopedAssetsDir dir("add");
  patchy::ui::MainWindow window;
  show_window_empty(window);
  // No document: the command and the button are off.
  CHECK(!require_action(window, "layerAddToAssetsAction")->isEnabled());
  window.add_document_session(asset_source_document(), QStringLiteral("Source"));
  QApplication::processEvents();
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* document = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(canvas != nullptr && document != nullptr);
  if (document == nullptr) {
    return;
  }
  auto* column = MainWindowTestAccess::panel_column(window);
  const auto widgets = asset_widgets(window);
  CHECK(!column->panel_visible(kAssetsPanelId));
  CHECK(require_action(window, "layerAddToAssetsAction")->isEnabled());
  CHECK(require_action(window, "layerAddToAssetsAction")->shortcut().isEmpty());

  add_layers_as_assets(window, *document, {"Set", "Mark"});
  // The command opens the panel so its result shows.
  CHECK(column->panel_visible(kAssetsPanelId) && column->panel_expanded(kAssetsPanelId));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Added 2 asset(s)"));
  // Adding reads the document only.
  CHECK(std::as_const(*document).layers().size() == 4U);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 0);
  CHECK(std::as_const(*document).layers()[1].bounds().x == 10);

  CHECK(widgets.library->asset_count() == 2);
  const auto& category = widgets.library->categories().front();
  // Top to bottom, as the Layers panel lists them; a group is one asset.
  CHECK(asset_names(category) == (QStringList{QStringLiteral("Set"), QStringLiteral("Mark")}));
  CHECK(widgets.grid->isVisible() && widgets.grid->count() == 2);
  CHECK(widgets.grid->item(0)->text() == QStringLiteral("Set") && widgets.grid->item(1)->text() == QStringLiteral("Mark"));
  // An asset just added brings its picture along: both are shown without a read.
  CHECK(widgets.panel->thumbnails()->image(widgets.panel->shown_asset_ids().front()) != nullptr);
  CHECK(widgets.panel->thumbnails()->image(widgets.panel->shown_asset_ids().back()) != nullptr);

  const auto mark_id = asset_id_named(*widgets.library, QStringLiteral("Mark"));
  const auto set_id = asset_id_named(*widgets.library, QStringLiteral("Set"));
  // The thumbnail is the layers on a transparent ground.
  const auto mark_thumbnail = widgets.library->thumbnail(mark_id);
  CHECK(!mark_thumbnail.isNull() && image_has_opaque_pixel(mark_thumbnail));
  CHECK(mark_thumbnail.size() == QSize(20, 12));
  CHECK(QColor(mark_thumbnail.pixel(10, 6)).red() > 180);
  CHECK(image_has_opaque_pixel(widgets.library->thumbnail(set_id)));

  // The asset file is a PSD of the layer alone, on a canvas trimmed to it, with the
  // source's depth and resolution.
  {
    const auto bytes = widgets.library->read_asset_bytes(mark_id);
    const auto asset = patchy::psd::DocumentIo::read(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                      static_cast<std::size_t>(bytes.size())));
    CHECK(asset.width() == 20 && asset.height() == 12);
    CHECK(asset.layers().size() == 1U);
    CHECK(asset.layers().front().name() == "Mark");
    CHECK(asset.layers().front().bounds().x == 0 && asset.layers().front().bounds().y == 0);
    CHECK(asset.color_state().bit_depth == std::as_const(*document).color_state().bit_depth);
    CHECK(asset.print_settings().horizontal_ppi == std::as_const(*document).print_settings().horizontal_ppi);
  }
  {
    const auto bytes = widgets.library->read_asset_bytes(set_id);
    const auto asset = patchy::psd::DocumentIo::read(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                      static_cast<std::size_t>(bytes.size())));
    CHECK(asset.width() == 8 && asset.height() == 8);
    CHECK(asset.layers().size() == 1U && asset.layers().front().kind() == patchy::LayerKind::Group);
    CHECK(asset.layers().front().children().size() == 1U);
  }

  // A thumbnail whose file is gone is rendered from the asset again, once, and kept.
  CHECK(QFile::remove(widgets.library->thumbnail_path(mark_id)));
  {
    patchy::ui::MainWindow second;
    show_window_empty(second);
    MainWindowTestAccess::show_panel(second, kAssetsPanelId);
    QApplication::processEvents();
    const auto second_widgets = asset_widgets(second);
    CHECK(second_widgets.grid->count() == 2);  // the assets outlive the window that made them
    CHECK(QFileInfo::exists(second_widgets.library->thumbnail_path(mark_id)));
    CHECK(image_has_opaque_pixel(second_widgets.library->thumbnail(mark_id)));
  }

  // The panel's own button runs the same command on the current selection.
  auto* add_button = window.findChild<QToolButton*>(QStringLiteral("assetsAddButton"));
  CHECK(add_button != nullptr && add_button->isEnabled());
  MainWindowTestAccess::select_layers(window, {layer_id_named(*document, "Badge")}, layer_id_named(*document, "Badge"));
  QApplication::processEvents();
  if (add_button != nullptr) {
    add_button->click();
  }
  settle_assets(window);
  CHECK(widgets.library->asset_count() == 3);
  CHECK(widgets.grid->count() == 3 && widgets.grid->item(2)->text() == QStringLiteral("Badge"));
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 0);
}

// Placing by double-click and by the Place entry: text stays text, a shape stays a shape,
// both land centered on the canvas as one undo step each, selected, with no tie to the asset.
void ui_assets_place_keeps_text_and_shape_layers_editable() {
  ScopedAssetsDir dir("place");
  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(asset_source_document(), QStringLiteral("Source"));
  QApplication::processEvents();
  auto* source_canvas = MainWindowTestAccess::canvas(window);
  auto* source = MainWindowTestAccess::document_for_canvas(window, source_canvas);
  CHECK(source != nullptr);
  if (source == nullptr) {
    return;
  }
  CHECK(run_script(window, QStringLiteral(
                               "var d = app.activeDocument; var t = d.addTextLayer('Sale', {size: 14, x: 6, y: 20, "
                               "color: '#c04000'}); t.name = 'Title';")));
  QApplication::processEvents();
  const auto* title = layer_named(std::as_const(*source), "Title");
  CHECK(title != nullptr && patchy::layer_is_text(*title));
  const auto widgets = asset_widgets(window);
  add_layers_as_assets(window, *source, {"Title", "Badge", "Mark"});
  CHECK(widgets.library->asset_count() == 3);
  const auto source_undo = MainWindowTestAccess::undo_depth_for_canvas(window, source_canvas);

  window.add_document_session(plain_document(128, 96, QColor(40, 40, 40)), QStringLiteral("Target"));
  QApplication::processEvents();
  auto* target_canvas = MainWindowTestAccess::canvas(window);
  auto* target = MainWindowTestAccess::document_for_canvas(window, target_canvas);
  CHECK(target != nullptr && target != source);
  if (target == nullptr) {
    return;
  }

  // Double-click the text asset.
  QListWidgetItem* title_item = nullptr;
  QListWidgetItem* badge_item = nullptr;
  QListWidgetItem* mark_item = nullptr;
  for (int row = 0; row < widgets.grid->count(); ++row) {
    auto* item = widgets.grid->item(row);
    if (item->text() == QStringLiteral("Title")) {
      title_item = item;
    } else if (item->text() == QStringLiteral("Badge")) {
      badge_item = item;
    } else if (item->text() == QStringLiteral("Mark")) {
      mark_item = item;
    }
  }
  CHECK(title_item != nullptr && badge_item != nullptr && mark_item != nullptr);
  if (title_item == nullptr || badge_item == nullptr || mark_item == nullptr) {
    return;
  }
  emit widgets.grid->itemDoubleClicked(title_item);
  QApplication::processEvents();
  CHECK(std::as_const(*target).layers().size() == 2U);
  {
    const auto& placed = std::as_const(*target).layers().back();
    CHECK(placed.name() == "Title");
    CHECK(patchy::layer_is_text(placed));  // still text, not pixels
    CHECK(std::as_const(*target).active_layer_id() == placed.id());
    CHECK(MainWindowTestAccess::selected_layer_ids(window) == std::vector<patchy::LayerId>{placed.id()});
    // Centered on the 128 x 96 canvas, within the text's own rounding.
    const auto bounds = placed.bounds();
    CHECK(std::abs(bounds.x + bounds.width / 2 - 64) <= 12);
    CHECK(std::abs(bounds.y + bounds.height / 2 - 48) <= 12);
  }
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, target_canvas) == 1);
  CHECK(MainWindowTestAccess::current_history_label(window) == QStringLiteral("Place asset"));
  CHECK(window.statusBar()->currentMessage() == QStringLiteral("Placed the asset \"Title\""));

  // The shape and the pixel asset together through the panel's Place request: one step.
  emit widgets.panel->place_requested({asset_id_named(*widgets.library, QStringLiteral("Badge")),
                                       asset_id_named(*widgets.library, QStringLiteral("Mark"))});
  QApplication::processEvents();
  CHECK(std::as_const(*target).layers().size() == 4U);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, target_canvas) == 2);
  {
    const auto* badge = layer_named(std::as_const(*target), "Badge");
    const auto* mark = layer_named(std::as_const(*target), "Mark");
    CHECK(badge != nullptr && mark != nullptr);
    if (badge != nullptr && mark != nullptr) {
      CHECK(badge->vector_shape() != nullptr);  // still a shape
      CHECK(badge->bounds().contains(64, 48));
      CHECK(mark->vector_shape() == nullptr && !patchy::layer_is_text(*mark));
      // A 20 x 12 layer centered on (64, 48).
      CHECK(mark->bounds().x == 54 && mark->bounds().y == 42);
      CHECK(mark->bounds().width == 20 && mark->bounds().height == 12);
      CHECK(MainWindowTestAccess::selected_layer_ids(window).size() == 2U);
    }
  }
  // An independent copy: deleting the asset leaves the placed layers alone.
  CHECK(widgets.library->remove_asset(asset_id_named(*widgets.library, QStringLiteral("Mark"))));
  QApplication::processEvents();
  CHECK(layer_named(std::as_const(*target), "Mark") != nullptr);
  CHECK(widgets.grid->count() == 2);

  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(std::as_const(*target).layers().size() == 2U);
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(std::as_const(*target).layers().size() == 1U);
  // The source document was never touched by any of it.
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, source_canvas) == source_undo);

  // With no document open, placing says so and does nothing.
  {
    patchy::ui::MainWindow empty;
    show_window_empty(empty);
    MainWindowTestAccess::show_panel(empty, kAssetsPanelId);
    QApplication::processEvents();
    const auto empty_widgets = asset_widgets(empty);
    CHECK(empty_widgets.grid->count() == 2);
    emit empty_widgets.grid->itemDoubleClicked(empty_widgets.grid->item(0));
    QApplication::processEvents();
    CHECK(MainWindowTestAccess::session_count(empty) == 0U);
    CHECK(empty.statusBar()->currentMessage().contains(QStringLiteral("Open a document")));
  }
}

// A canvas drop centers the asset on the drop point; a tab drop centers it on that
// document's canvas. Each is one undo step in the document dropped on.
void ui_assets_canvas_drop_centers_on_the_drop_point() {
  ScopedAssetsDir dir("drop");
  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(plain_document(128, 96, QColor(40, 40, 40)), QStringLiteral("Target"));
  QApplication::processEvents();
  auto* target_canvas = MainWindowTestAccess::canvas(window);
  auto* target = MainWindowTestAccess::document_for_canvas(window, target_canvas);
  window.add_document_session(asset_source_document(), QStringLiteral("Source"));
  QApplication::processEvents();
  auto* source_canvas = MainWindowTestAccess::canvas(window);
  auto* source = MainWindowTestAccess::document_for_canvas(window, source_canvas);
  CHECK(target != nullptr && source != nullptr);
  if (target == nullptr || source == nullptr) {
    return;
  }
  const auto widgets = asset_widgets(window);
  add_layers_as_assets(window, *source, {"Mark"});
  const auto mark_id = asset_id_named(*widgets.library, QStringLiteral("Mark"));

  // Onto the active document's own canvas: the drop point (20, 30) becomes the center.
  auto drop = send_asset_drop(*source_canvas, source_canvas->widget_position_for_document_point(QPoint(20, 30)),
                              {mark_id});
  CHECK(drop.entered && drop.move_accepted && drop.drop_accepted);
  CHECK(std::as_const(*source).layers().size() == 4U);  // deferred past the drop
  QApplication::processEvents();
  CHECK(std::as_const(*source).layers().size() == 5U);
  {
    // Above the active layer ("Mark"), named apart from it.
    const auto& placed = std::as_const(*source).layers()[2];
    CHECK(placed.name() != "Mark" && placed.name().find("Mark") == 0);
    CHECK(placed.bounds().x == 10 && placed.bounds().y == 24);
  }
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, source_canvas) == 1);

  // Onto the other document's tab: centered on its canvas, and it becomes the active one.
  auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(tabs != nullptr);
  if (tabs == nullptr) {
    return;
  }
  auto* tab_bar = tabs->tabBar();
  const auto target_tab = tabs->indexOf(target_canvas);
  CHECK(target_tab >= 0);
  drop = send_asset_drop(*tab_bar, tab_bar->tabRect(target_tab).center(), {mark_id});
  CHECK(drop.entered && drop.drop_accepted);
  QApplication::processEvents();
  CHECK(std::as_const(*target).layers().size() == 2U);
  CHECK(std::as_const(*target).layers().back().bounds().x == 54);
  CHECK(std::as_const(*target).layers().back().bounds().y == 42);
  CHECK(MainWindowTestAccess::canvas(window) == target_canvas);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, target_canvas) == 1);
  // A gap beside the tabs is no document: refused.
  drop = send_asset_drop(*tab_bar, QPoint(tab_bar->width() - 1, tab_bar->height() / 2), {mark_id});
  if (tab_bar->tabAt(QPoint(tab_bar->width() - 1, tab_bar->height() / 2)) < 0) {
    CHECK(!drop.entered && !drop.drop_accepted);
  }

  // Onto a floated document's canvas: the same drop-point rule in its own window.
  MainWindowTestAccess::float_active_document(window);
  QApplication::processEvents();
  drop = send_asset_drop(*target_canvas, target_canvas->widget_position_for_document_point(QPoint(100, 70)), {mark_id});
  CHECK(drop.entered && drop.drop_accepted);
  QApplication::processEvents();
  CHECK(std::as_const(*target).layers().size() == 3U);
  CHECK(std::as_const(*target).layers().back().bounds().x == 90);
  CHECK(std::as_const(*target).layers().back().bounds().y == 64);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, target_canvas) == 2);
}

// The preview-dialog edit lock refuses an asset drop like every other drop, and an asset
// drag is never taken for a file or a layer drag.
void ui_assets_drop_respects_edit_lock_and_is_not_a_file_or_layer_drag() {
  ScopedAssetsDir dir("lock");
  SettingsValueRestorer drop_policy(QStringLiteral("imports/canvasFileDrop"));
  {
    // Canvas file drops set to place Smart Objects (GitHub issue 749): an asset drop still
    // goes to the asset handler.
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/canvasFileDrop"), QStringLiteral("smartObject"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(asset_source_document(), QStringLiteral("Source"));
  QApplication::processEvents();
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* document = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(document != nullptr);
  if (document == nullptr) {
    return;
  }
  const auto widgets = asset_widgets(window);
  add_layers_as_assets(window, *document, {"Mark"});
  const auto mark_id = asset_id_named(*widgets.library, QStringLiteral("Mark"));
  const auto sessions = MainWindowTestAccess::session_count(window);
  const auto center = canvas->rect().center();

  // A drag from another Patchy process is refused.
  auto drop = send_asset_drop(*canvas, center, {mark_id}, /*own_process=*/false);
  CHECK(!drop.entered && !drop.drop_accepted);
  QApplication::processEvents();
  CHECK(std::as_const(*document).layers().size() == 4U);

  // The Layers panel and the window itself take no asset drag.
  auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layer_list != nullptr);
  if (layer_list != nullptr) {
    send_asset_drop(*layer_list->viewport(), layer_list->viewport()->rect().center(), {mark_id});
    QApplication::processEvents();
    CHECK(std::as_const(*document).layers().size() == 4U);
  }
  drop = send_asset_drop(window, QPoint(4, 4), {mark_id});
  CHECK(!drop.drop_accepted);
  QApplication::processEvents();
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  CHECK(std::as_const(*document).layers().size() == 4U);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 0);

  // While a preview dialog holds the edit lock the canvas refuses the drop.
  bool saw_dialog = false;
  QTimer::singleShot(0, guarded([&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (widget->objectName() != QStringLiteral("patchyFilterDialog")) {
        continue;
      }
      auto* dialog = qobject_cast<QDialog*>(widget);
      CHECK(dialog != nullptr);
      const auto locked_drop = send_asset_drop(*canvas, center, {mark_id});
      CHECK(!locked_drop.entered && !locked_drop.drop_accepted);
      QApplication::processEvents();
      CHECK(std::as_const(*document).layers().size() == 4U);
      // Add to Assets is refused under the lock too.
      CHECK(!require_action(window, "layerAddToAssetsAction")->isEnabled());
      saw_dialog = true;
      dialog->reject();
      return;
    }
    CHECK(false);
  }));
  require_action(window, "imageAdjustDesaturateAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(std::as_const(*document).layers().size() == 4U);

  // Unlocked again: the same drop places a plain pixel layer, not a Smart Object, in one
  // undo step, and opens no tab.
  const auto undo_before = MainWindowTestAccess::undo_depth_for_canvas(window, canvas);
  drop = send_asset_drop(*canvas, center, {mark_id});
  CHECK(drop.entered && drop.drop_accepted);
  QApplication::processEvents();
  CHECK(std::as_const(*document).layers().size() == 5U);
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == undo_before + 1);
  CHECK(!patchy::layer_is_smart_object(std::as_const(*document).layers()[2]));
  CHECK(std::as_const(*document).metadata().smart_objects.blocks.empty());
}

// The search box filters by name across every category; the picker switches categories.
void ui_assets_search_and_categories_filter_the_grid() {
  ScopedAssetsDir dir("search");
  QString logos;
  {
    AssetLibrary seed(dir.path);
    CHECK(seed.add_asset(QString(), QStringLiteral("Red Button"), fake_psd_bytes(1), solid_thumbnail(Qt::red)).has_value());
    CHECK(seed.add_asset(QString(), QStringLiteral("Blue Badge"), fake_psd_bytes(2), solid_thumbnail(Qt::blue)).has_value());
    logos = seed.add_category(QStringLiteral("Logos")).value_or(QString());
    CHECK(seed.add_asset(logos, QStringLiteral("Round badge"), fake_psd_bytes(3), solid_thumbnail(Qt::green)).has_value());
  }
  patchy::ui::MainWindow window;
  show_window_empty(window);
  MainWindowTestAccess::show_panel(window, kAssetsPanelId);
  QApplication::processEvents();
  const auto widgets = asset_widgets(window);
  auto* combo = window.findChild<QComboBox*>(QStringLiteral("assetsCategoryCombo"));
  auto* search = window.findChild<QLineEdit*>(QStringLiteral("assetsSearchEdit"));
  auto* hint = window.findChild<QLabel*>(QStringLiteral("assetsEmptyHint"));
  CHECK(combo != nullptr && search != nullptr && hint != nullptr);
  if (combo == nullptr || search == nullptr || hint == nullptr) {
    return;
  }
  // The first category has no stored name and shows as General.
  CHECK(combo->count() == 2);
  CHECK(combo->itemText(0) == QStringLiteral("General") && combo->itemText(1) == QStringLiteral("Logos"));
  CHECK(combo->currentIndex() == 0);
  CHECK(widgets.panel->shown_asset_ids().size() == 2);
  CHECK(widgets.grid->item(0)->text() == QStringLiteral("Red Button"));

  combo->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(widgets.panel->current_category_id() == logos);
  CHECK(widgets.grid->count() == 1 && widgets.grid->item(0)->text() == QStringLiteral("Round badge"));

  // A search looks in every category, whatever case, and says where each hit lives.
  search->setText(QStringLiteral("BADGE"));
  QApplication::processEvents();
  CHECK(widgets.grid->count() == 2);
  CHECK(widgets.grid->item(0)->text() == QStringLiteral("Blue Badge"));
  CHECK(widgets.grid->item(1)->text() == QStringLiteral("Round badge"));
  CHECK(widgets.grid->item(0)->toolTip().contains(QStringLiteral("General")));
  CHECK(widgets.grid->item(1)->toolTip().contains(QStringLiteral("Logos")));
  CHECK(!combo->isEnabled());
  search->setText(QStringLiteral("nothing like this"));
  QApplication::processEvents();
  CHECK(widgets.grid->count() == 0 && !widgets.grid->isVisible());
  CHECK(hint->isVisible() && hint->text() == QStringLiteral("No asset matches the search."));
  search->clear();
  QApplication::processEvents();
  CHECK(combo->isEnabled());
  CHECK(widgets.grid->count() == 1 && widgets.grid->isVisible() && !hint->isVisible());

  // Sort by Name orders the current category; Move to Category is the library's move.
  combo->setCurrentIndex(0);
  QApplication::processEvents();
  widgets.panel->sort_current_category();
  QApplication::processEvents();
  CHECK(widgets.grid->item(0)->text() == QStringLiteral("Blue Badge"));
  CHECK(widgets.library->move_asset(widgets.panel->shown_asset_ids().front(), logos));
  QApplication::processEvents();
  CHECK(widgets.grid->count() == 1 && widgets.grid->item(0)->text() == QStringLiteral("Red Button"));
  // The picked category is remembered for the next window.
  combo->setCurrentIndex(1);
  QApplication::processEvents();
  CHECK(widgets.grid->count() == 2);
  patchy::ui::MainWindow second;
  show_window_empty(second);
  MainWindowTestAccess::show_panel(second, kAssetsPanelId);
  QApplication::processEvents();
  CHECK(asset_widgets(second).panel->current_category_id() == logos);
}

// Rename, delete and the category entries run through their dialogs.
void ui_assets_manage_dialogs_rename_and_delete() {
  ScopedAssetsDir dir("manage");
  QString keep;
  QString drop;
  {
    AssetLibrary seed(dir.path);
    keep = seed.add_asset(QString(), QStringLiteral("Keep"), fake_psd_bytes(1), solid_thumbnail(Qt::red)).value_or(QString());
    drop = seed.add_asset(QString(), QStringLiteral("Drop"), fake_psd_bytes(2), solid_thumbnail(Qt::blue)).value_or(QString());
  }
  patchy::ui::MainWindow window;
  show_window_empty(window);
  MainWindowTestAccess::show_panel(window, kAssetsPanelId);
  QApplication::processEvents();
  const auto widgets = asset_widgets(window);
  CHECK(widgets.grid->count() == 2);

  const auto answer_text = [](const QString& object_name, const QString& text, bool accept) {
    QTimer::singleShot(0, guarded([object_name, text, accept] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* dialog = qobject_cast<QInputDialog*>(widget);
        if (dialog == nullptr || dialog->objectName() != object_name) {
          continue;
        }
        dialog->setTextValue(text);
        if (accept) {
          dialog->accept();
        } else {
          dialog->reject();
        }
        return;
      }
      CHECK(false);
    }));
  };
  const auto answer_box = [](const QString& object_name, QMessageBox::StandardButton button) {
    QTimer::singleShot(0, guarded([object_name, button] {
      for (auto* widget : QApplication::topLevelWidgets()) {
        auto* box = qobject_cast<QMessageBox*>(widget);
        if (box == nullptr || box->objectName() != object_name) {
          continue;
        }
        CHECK(box->button(button) != nullptr);
        box->button(button)->click();
        return;
      }
      CHECK(false);
    }));
  };

  answer_text(QStringLiteral("assetNameDialog"), QStringLiteral("  Kept and renamed "), true);
  widgets.panel->rename_asset_interactive(keep);
  QApplication::processEvents();
  CHECK(widgets.library->find_asset(keep)->name == QStringLiteral("Kept and renamed"));
  CHECK(widgets.grid->item(0)->text() == QStringLiteral("Kept and renamed"));
  // Cancel, and an empty name, change nothing.
  answer_text(QStringLiteral("assetNameDialog"), QStringLiteral("Other"), false);
  widgets.panel->rename_asset_interactive(keep);
  answer_text(QStringLiteral("assetNameDialog"), QStringLiteral("   "), true);
  widgets.panel->rename_asset_interactive(keep);
  QApplication::processEvents();
  CHECK(widgets.library->find_asset(keep)->name == QStringLiteral("Kept and renamed"));

  // Delete asks first: No keeps the asset, Yes removes it and its file.
  answer_box(QStringLiteral("assetDeleteMessageBox"), QMessageBox::No);
  widgets.panel->delete_assets_interactive({drop});
  QApplication::processEvents();
  CHECK(widgets.library->find_asset(drop) != nullptr);
  answer_box(QStringLiteral("assetDeleteMessageBox"), QMessageBox::Yes);
  widgets.panel->delete_assets_interactive({drop});
  QApplication::processEvents();
  CHECK(widgets.library->find_asset(drop) == nullptr);
  CHECK(!QFileInfo::exists(QDir(dir.path).filePath(drop + QStringLiteral(".psd"))));
  CHECK(widgets.grid->count() == 1);

  // New Category makes it the current one; Rename renames it.
  answer_text(QStringLiteral("assetCategoryNameDialog"), QStringLiteral("Icons"), true);
  widgets.panel->new_category_interactive();
  QApplication::processEvents();
  CHECK(widgets.library->categories().size() == 2U);
  const auto icons = widgets.library->categories()[1].id;
  CHECK(widgets.panel->current_category_id() == icons);
  CHECK(widgets.grid->count() == 0);
  answer_text(QStringLiteral("assetCategoryNameDialog"), QStringLiteral("Icon set"), true);
  widgets.panel->rename_category_interactive();
  QApplication::processEvents();
  CHECK(widgets.library->categories()[1].name == QStringLiteral("Icon set"));
  auto* combo = window.findChild<QComboBox*>(QStringLiteral("assetsCategoryCombo"));
  CHECK(combo != nullptr && combo->currentText() == QStringLiteral("Icon set"));

  // A category that holds assets asks before it goes; an empty one goes at once.
  CHECK(widgets.library->move_asset(keep, icons));
  QApplication::processEvents();
  answer_box(QStringLiteral("assetCategoryDeleteMessageBox"), QMessageBox::No);
  widgets.panel->delete_category_interactive();
  QApplication::processEvents();
  CHECK(widgets.library->categories().size() == 2U);
  CHECK(widgets.library->move_asset(keep, QLatin1String(patchy::ui::kDefaultAssetCategoryId)));
  QApplication::processEvents();
  widgets.panel->delete_category_interactive();  // empty now: no question
  QApplication::processEvents();
  CHECK(widgets.library->categories().size() == 1U);
  CHECK(widgets.library->find_asset(keep) != nullptr);
  CHECK(widgets.grid->count() == 1);
  // The last category cannot be deleted.
  widgets.panel->delete_category_interactive();
  QApplication::processEvents();
  CHECK(widgets.library->categories().size() == 1U);
}

// Rows dragged from the Layers panel onto the Assets panel become assets, like the button.
void ui_assets_layer_rows_dropped_on_the_panel_become_assets() {
  ScopedAssetsDir dir("layer-drop");
  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.add_document_session(asset_source_document(), QStringLiteral("Source"));
  QApplication::processEvents();
  auto* canvas = MainWindowTestAccess::canvas(window);
  auto* document = MainWindowTestAccess::document_for_canvas(window, canvas);
  CHECK(document != nullptr);
  if (document == nullptr) {
    return;
  }
  MainWindowTestAccess::show_panel(window, kAssetsPanelId);
  QApplication::processEvents();
  const auto widgets = asset_widgets(window);
  const auto session = MainWindowTestAccess::session_id_for_canvas(window, canvas);

  bool entered = false;
  send_layer_drop_to_widget(*widgets.panel, widgets.panel->rect().center(),
                            {layer_id_named(*document, "Badge"), layer_id_named(*document, "Set")}, session,
                            Qt::NoModifier, &entered);
  CHECK(entered);
  settle_assets(window);
  CHECK(widgets.library->asset_count() == 2);
  CHECK(asset_names(widgets.library->categories().front()) ==
        (QStringList{QStringLiteral("Badge"), QStringLiteral("Set")}));
  // The document is as it was: the drag copied, it did not move or reorder anything.
  CHECK(std::as_const(*document).layers().size() == 4U);
  CHECK(MainWindowTestAccess::undo_depth_for_canvas(window, canvas) == 0);

  // A layer drag without a source document (another process) is not taken.
  entered = true;
  send_layer_drop_to_widget(*widgets.panel, widgets.panel->rect().center(), {layer_id_named(*document, "Mark")},
                            std::nullopt, Qt::NoModifier, &entered);
  CHECK(!entered);
  QApplication::processEvents();
  CHECK(widgets.library->asset_count() == 2);
}

// A library of `count` assets written straight to disk, all in the first category: stand-in
// PSDs (the library looks at the signature only) with real thumbnail pictures.
QStringList seed_library_files(const QString& directory, int count) {
  CHECK(QDir().mkpath(directory));
  QStringList ids;
  QJsonArray assets;
  const auto psd = fake_psd_bytes(5);
  for (int index = 0; index < count; ++index) {
    const auto id = QStringLiteral("%1").arg(index + 1, 32, 16, QLatin1Char('0'));
    QFile file(QDir(directory).filePath(id + QStringLiteral(".psd")));
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(reinterpret_cast<const char*>(psd.data()), static_cast<qint64>(psd.size()));
    CHECK(solid_thumbnail(QColor::fromHsv((index * 7) % 360, 200, 220))
              .save(QDir(directory).filePath(id + QStringLiteral(".png")), "PNG"));
    assets.push_back(QJsonObject{{QStringLiteral("id"), id},
                                 {QStringLiteral("name"), QStringLiteral("Asset %1").arg(index + 1, 4, 10, QLatin1Char('0'))}});
    ids.push_back(id);
  }
  const QJsonObject root{{QStringLiteral("format"), QStringLiteral("patchy-assets")},
                         {QStringLiteral("version"), 1},
                         {QStringLiteral("categories"),
                          QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("default")},
                                                 {QStringLiteral("name"), QString()},
                                                 {QStringLiteral("assets"), assets}}}}};
  QFile index(QDir(directory).filePath(QStringLiteral("index.json")));
  CHECK(index.open(QIODevice::WriteOnly));
  index.write(QJsonDocument(root).toJson());
  return ids;
}

// Startup with the panel closed opens no asset file, whatever the library holds. With the
// panel open the reading happens on the worker: names show before any picture, and only the
// assets the grid paints are decoded.
void ui_assets_startup_reads_nothing_and_the_grid_decodes_only_what_it_paints() {
  ScopedAssetsDir dir("lazy");
  const auto ids = seed_library_files(dir.path, 600);
  const auto before = patchy::ui::asset_file_access_counts();
  patchy::ui::MainWindow window;
  show_window_empty(window);
  for (int pass = 0; pass < 6; ++pass) {
    QApplication::processEvents();
  }
  {
    const auto started = patchy::ui::asset_file_access_counts();
    CHECK(started.ui_thread == before.ui_thread);
    CHECK(started.worker == before.worker);
  }

  auto* library = window.findChild<AssetLibrary*>();
  auto* panel = window.findChild<AssetsPanel*>(QStringLiteral("assetsPanel"));
  auto* grid = window.findChild<QListWidget*>(QStringLiteral("assetsGrid"));
  CHECK(library != nullptr && panel != nullptr && grid != nullptr);
  if (library == nullptr || panel == nullptr || grid == nullptr) {
    return;
  }
  MainWindowTestAccess::show_panel(window, kAssetsPanelId);
  // Showing the panel starts the read on the worker and returns: the library is not there yet.
  CHECK(!library->loaded() && library->busy());
  CHECK(grid->count() == 0);
  // The index lands (this waits for the worker's read; it reads nothing itself).
  library->ensure_loaded();
  CHECK(library->loaded());
  CHECK(grid->count() == 600);
  CHECK(grid->item(0)->text() == QStringLiteral("Asset 0001"));
  CHECK(grid->item(599)->text() == QStringLiteral("Asset 0600"));
  // Names first: nothing has been painted, so no picture has even been asked for.
  CHECK(panel->thumbnails()->requested_ids().isEmpty());
  CHECK(panel->thumbnails()->image(ids.front()) == nullptr);

  settle_assets(window);
  QSet<QString> visible;
  for (int row = 0; row < grid->count(); ++row) {
    if (grid->visualItemRect(grid->item(row)).intersects(grid->viewport()->rect())) {
      visible.insert(ids[row]);
    }
  }
  const auto requested = panel->thumbnails()->requested_ids();
  CHECK(!visible.isEmpty() && visible.size() < 200);
  CHECK(!requested.isEmpty() && requested.size() <= visible.size());
  for (const auto& id : requested) {
    CHECK(visible.contains(id));
    CHECK(panel->thumbnails()->state(id) == AssetThumbnailState::ready);
    CHECK(panel->thumbnails()->image(id) != nullptr);
  }
  CHECK(panel->thumbnails()->state(ids.back()) == AssetThumbnailState::unknown);
  {
    // All of it happened off the UI thread.
    const auto opened = patchy::ui::asset_file_access_counts();
    CHECK(opened.ui_thread == before.ui_thread);
    CHECK(opened.worker > before.worker);
  }

  // Scrolled to the end, the last assets are decoded; the ones in between never are.
  grid->scrollToBottom();
  settle_assets(window);
  CHECK(panel->thumbnails()->state(ids.back()) == AssetThumbnailState::ready);
  CHECK(panel->thumbnails()->requested_ids().size() < 400);
  CHECK(panel->thumbnails()->state(ids[300]) == AssetThumbnailState::unknown);
  CHECK(patchy::ui::asset_file_access_counts().ui_thread == before.ui_thread);
  // What is kept in memory is the pictures shown, a few kilobytes each.
  CHECK(panel->thumbnails()->memory_bytes() > 0);
  CHECK(panel->thumbnails()->memory_bytes() < patchy::ui::kAssetThumbnailMemoryBytes);
}

// Deleting an asset, changing the library and destroying the window while loads are pending
// are all safe, and a stale answer from the worker is dropped.
void ui_assets_pending_loads_survive_changes_and_a_closed_window() {
  ScopedAssetsDir dir("pending");
  const auto ids = seed_library_files(dir.path, 120);
  {
    patchy::ui::MainWindow window;
    show_window_empty(window);
    QApplication::processEvents();
    MainWindowTestAccess::show_panel(window, kAssetsPanelId);
    auto* library = window.findChild<AssetLibrary*>();
    auto* panel = window.findChild<AssetsPanel*>(QStringLiteral("assetsPanel"));
    auto* grid = window.findChild<QListWidget*>(QStringLiteral("assetsGrid"));
    CHECK(library != nullptr && panel != nullptr && grid != nullptr);
    if (library == nullptr || panel == nullptr || grid == nullptr) {
      return;
    }
    // A change made while the worker still reads the index: it waits for that read and then
    // applies, so nothing is lost and nothing is read twice.
    CHECK(library->busy());
    CHECK(library->remove_asset(ids[0]));
    CHECK(library->rename_asset(ids[60], QStringLiteral("Renamed while loading")));
    CHECK(library->loaded());
    CHECK(library->asset_count() == 119);
    CHECK(grid->count() == 119 && grid->item(59)->text() == QStringLiteral("Renamed while loading"));
    // One pass paints the first cells and queues their pictures; an asset deleted now has its
    // picture pending or just made.
    for (int pass = 0; pass < 200 && panel->thumbnails()->requested_ids().isEmpty(); ++pass) {
      QApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    const auto requested = panel->thumbnails()->requested_ids();
    CHECK(!requested.isEmpty());
    CHECK(library->remove_asset(requested.front()));
    CHECK(panel->thumbnails()->state(requested.front()) == AssetThumbnailState::unknown);
    settle_assets(window);
    // The late answer for the deleted asset changed nothing, and its files are gone.
    CHECK(panel->thumbnails()->state(requested.front()) == AssetThumbnailState::unknown);
    CHECK(panel->thumbnails()->image(requested.front()) == nullptr);
    CHECK(!QFileInfo::exists(library->asset_path(requested.front())));
    CHECK(library->asset_count() == 118);
    CHECK(grid->count() == 118);

    // Scroll, so more pictures are queued, and destroy the window with them pending.
    grid->scrollToBottom();
    QApplication::processEvents();
  }
  {
    // A window opened and destroyed before its load ever lands.
    patchy::ui::MainWindow window;
    show_window_empty(window);
    MainWindowTestAccess::show_panel(window, kAssetsPanelId);
  }
  for (int pass = 0; pass < 6; ++pass) {
    QApplication::processEvents();  // answers for windows that are gone arrive and are dropped
  }
  AssetLibrary reloaded(dir.path);
  reloaded.load();
  CHECK(reloaded.asset_count() == 118);
  CHECK(reloaded.find_asset(ids[60]) != nullptr && reloaded.find_asset(ids[60])->name == QStringLiteral("Renamed while loading"));
}

// Missing thumbnails and a missing index are recovered on the worker, with the results a
// load on the UI thread used to give: the pictures are rendered from the assets and stored
// again, and the assets come back named after their layers.
void ui_assets_missing_thumbnails_and_index_recover_in_the_background() {
  ScopedAssetsDir dir("recover");
  QString mark_id;
  QString set_id;
  {
    patchy::ui::MainWindow window;
    show_window_empty(window);
    window.add_document_session(asset_source_document(), QStringLiteral("Source"));
    QApplication::processEvents();
    auto* document = MainWindowTestAccess::document_for_canvas(window, MainWindowTestAccess::canvas(window));
    CHECK(document != nullptr);
    if (document == nullptr) {
      return;
    }
    const auto widgets = asset_widgets(window);
    add_layers_as_assets(window, *document, {"Set", "Mark"});
    CHECK(widgets.library->asset_count() == 2);
    mark_id = asset_id_named(*widgets.library, QStringLiteral("Mark"));
    set_id = asset_id_named(*widgets.library, QStringLiteral("Set"));
  }
  const QDir folder(dir.path);
  CHECK(QFile::remove(folder.filePath(mark_id + QStringLiteral(".png"))));
  CHECK(QFile::remove(folder.filePath(set_id + QStringLiteral(".png"))));
  CHECK(QFile::remove(folder.filePath(QStringLiteral("index.json"))));
  {
    // A stray file beside them stays out of the rebuilt library.
    QFile stray(folder.filePath(QStringLiteral("notes.psd")));
    CHECK(stray.open(QIODevice::WriteOnly));
    stray.write("8BPS not an asset of this library");
  }

  const auto before = patchy::ui::asset_file_access_counts();
  patchy::ui::MainWindow window;
  show_window_empty(window);
  MainWindowTestAccess::show_panel(window, kAssetsPanelId);
  const auto widgets = asset_widgets(window);  // waits for the worker
  {
    // None of the recovery ran on the UI thread.
    const auto recovered = patchy::ui::asset_file_access_counts();
    CHECK(recovered.ui_thread == before.ui_thread);
    CHECK(recovered.worker > before.worker);
  }
  CHECK(widgets.library->asset_count() == 2);
  CHECK(widgets.library->find_asset(mark_id) != nullptr && widgets.library->find_asset(mark_id)->name == QStringLiteral("Mark"));
  CHECK(widgets.library->find_asset(set_id) != nullptr && widgets.library->find_asset(set_id)->name == QStringLiteral("Set"));
  CHECK(widgets.grid->count() == 2);
  // Both pictures were rendered from the asset files and stored again.
  for (const auto& id : {mark_id, set_id}) {
    CHECK(widgets.panel->thumbnails()->state(id) == AssetThumbnailState::ready);
    CHECK(widgets.panel->thumbnails()->image(id) != nullptr);
    CHECK(QFileInfo::exists(widgets.library->thumbnail_path(id)));
    CHECK(image_has_opaque_pixel(widgets.library->thumbnail(id)));
  }
  CHECK(widgets.library->thumbnail(mark_id).size() == QSize(20, 12));
  // The index is written again by the next change, with the recovered names.
  CHECK(widgets.library->rename_asset(mark_id, QStringLiteral("Mark again")));
  AssetLibrary reloaded(dir.path);
  reloaded.load();
  CHECK(reloaded.asset_count() == 2);
  CHECK(reloaded.find_asset(set_id) != nullptr && reloaded.find_asset(set_id)->name == QStringLiteral("Set"));
}

// Assets written in the background are listed in the order they were queued, a failed one
// leaves nothing behind, and one still being written is listed before the library goes.
void ui_asset_library_background_adds_are_ordered_and_finished_at_close() {
  ScopedAssetsDir dir("background-add");
  const auto encoded = [](std::uint8_t fill, QColor color) {
    return [fill, color] {
      AssetLibrary::EncodedAsset asset;
      asset.psd_bytes = fake_psd_bytes(fill);
      asset.thumbnail = solid_thumbnail(color);
      return asset;
    };
  };
  {
    AssetLibrary library(dir.path);
    QStringList added;
    int finished_count = -1;
    QString finished_failure;
    QObject::connect(&library, &AssetLibrary::asset_added,
                     [&added](const QString& id, const QImage& thumbnail) {
                       CHECK(!thumbnail.isNull());
                       added.push_back(id);
                     });
    QObject::connect(&library, &AssetLibrary::background_adds_finished,
                     [&](int count, const QString& failure) {
                       finished_count = count;
                       finished_failure = failure;
                     });
    library.add_asset_in_background(QString(), QStringLiteral("First"), encoded(1, Qt::red));
    library.add_asset_in_background(QString(), QStringLiteral("Second"), encoded(2, Qt::green));
    library.add_asset_in_background(QString(), QStringLiteral("Broken"), [] {
      AssetLibrary::EncodedAsset asset;
      asset.error = QStringLiteral("could not encode");
      return asset;
    });
    library.add_asset_in_background(QString(), QStringLiteral("Third"), encoded(3, Qt::blue));
    // Nothing is listed until its files are whole.
    CHECK(library.busy());
    CHECK(library.asset_count() == 0);
    library.finish_pending_adds();
    CHECK(!library.busy());
    CHECK(asset_names(library.categories().front()) ==
          (QStringList{QStringLiteral("First"), QStringLiteral("Second"), QStringLiteral("Third")}));
    CHECK(added.size() == 3);
    CHECK(finished_count == 3);
    CHECK(finished_failure == QStringLiteral("could not encode"));
    // Three assets with their pictures and the index: the failed one left no file.
    CHECK(folder_files(dir.path).size() == 7);
    for (int pass = 0; pass < 4; ++pass) {
      QApplication::processEvents();  // the worker's own queued answers: already handled
    }
    CHECK(library.asset_count() == 3 && added.size() == 3);
    // Queued and then the library is destroyed at once, as a closing window does.
    library.add_asset_in_background(QString(), QStringLiteral("Last"), encoded(4, Qt::yellow));
  }
  AssetLibrary reloaded(dir.path);
  reloaded.load();
  CHECK(asset_names(reloaded.categories().front()) ==
        (QStringList{QStringLiteral("First"), QStringLiteral("Second"), QStringLiteral("Third"), QStringLiteral("Last")}));
  CHECK(folder_files(dir.path).size() == 9);
}

// The shots for the review: the panel holding a pixel, a text and a shape asset, and a
// second document after the three were placed in it.
void ui_assets_review_capture() {
  ScopedAssetsDir dir("capture");
  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto source = plain_document(480, 320, QColor(244, 242, 236));
  {
    QImage art(140, 100, QImage::Format_RGBA8888);
    art.fill(Qt::transparent);
    QPainter painter(&art);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(226, 88, 62));
    painter.drawEllipse(QRect(4, 4, 92, 92));
    painter.setBrush(QColor(250, 196, 70));
    painter.drawEllipse(QRect(58, 22, 78, 74));
    painter.end();
    patchy::Layer photo(source.allocate_layer_id(), "Sunset", patchy::ui::pixels_from_image_rgba(art));
    photo.set_bounds(patchy::Rect{40, 40, 140, 100});
    source.add_layer(std::move(photo));
  }
  source.add_layer(shape_layer(source, "Badge", patchy::Rect{250, 60, 150, 70}, patchy::RgbColor{36, 110, 190}));
  window.add_document_session(std::move(source), QStringLiteral("Source"));
  QApplication::processEvents();
  auto* source_canvas = MainWindowTestAccess::canvas(window);
  auto* source_document = MainWindowTestAccess::document_for_canvas(window, source_canvas);
  CHECK(source_document != nullptr);
  if (source_document == nullptr) {
    return;
  }
  CHECK(run_script(window, QStringLiteral(
                               "var d = app.activeDocument; var t = d.addTextLayer('Summer Sale', {size: 44, x: 60, "
                               "y: 250, color: '#1c4f8a', bold: true}); t.name = 'Headline';")));
  QApplication::processEvents();
  const auto widgets = asset_widgets(window);
  // Room for the Layers rows and the asset grid in one picture: a taller window with the
  // Color group collapsed.
  window.resize(1280, 1000);
  if (auto* column = MainWindowTestAccess::panel_column(window); column != nullptr) {
    column->set_panel_expanded(QStringLiteral("colorDock"), false);
  }
  QApplication::processEvents();
  add_layers_as_assets(window, *source_document, {"Headline", "Badge", "Sunset"});
  CHECK(widgets.library->asset_count() == 3);
  settle_assets(window);
  save_widget_artifact("issue_751_assets_panel", window);
  // The three asset files as the feature wrote them, kept for the Photoshop open check
  // (docs/photoshop-com.md).
  for (const auto* name : {"Headline", "Badge", "Sunset"}) {
    const auto kept = QDir::current().filePath(QStringLiteral("test-artifacts/issue_751_asset_%1.psd")
                                                   .arg(QString::fromLatin1(name).toLower()));
    QFile::remove(kept);
    CHECK(QFile::copy(widgets.library->asset_path(asset_id_named(*widgets.library, QString::fromLatin1(name))), kept));
  }

  window.add_document_session(plain_document(520, 360, QColor(250, 250, 250)), QStringLiteral("Poster"));
  QApplication::processEvents();
  auto* poster_canvas = MainWindowTestAccess::canvas(window);
  auto* poster = MainWindowTestAccess::document_for_canvas(window, poster_canvas);
  CHECK(poster != nullptr);
  if (poster == nullptr) {
    return;
  }
  const auto drop_at = [&](const QString& name, QPoint point) {
    const auto drop = send_asset_drop(*poster_canvas, poster_canvas->widget_position_for_document_point(point),
                                      {asset_id_named(*widgets.library, name)});
    CHECK(drop.drop_accepted);
    QApplication::processEvents();
  };
  drop_at(QStringLiteral("Sunset"), QPoint(140, 120));
  drop_at(QStringLiteral("Badge"), QPoint(380, 110));
  drop_at(QStringLiteral("Headline"), QPoint(260, 270));
  CHECK(std::as_const(*poster).layers().size() == 4U);
  const auto* headline = layer_named(std::as_const(*poster), "Headline");
  const auto* badge = layer_named(std::as_const(*poster), "Badge");
  CHECK(headline != nullptr && patchy::layer_is_text(*headline));
  CHECK(badge != nullptr && badge->vector_shape() != nullptr);
  MainWindowTestAccess::show_panel(window, QStringLiteral("layersDock"));
  settle_assets(window);
  save_widget_artifact("issue_751_assets_placed", window);
}

}  // namespace

std::vector<patchy::test::TestCase> assets_panel_tests() {
  return {
      {"ui_asset_library_adds_reloads_renames_moves_and_deletes",
       ui_asset_library_adds_reloads_renames_moves_and_deletes},
      {"ui_asset_library_skips_damaged_and_foreign_files", ui_asset_library_skips_damaged_and_foreign_files},
      {"ui_unicode_asset_library_round_trips", ui_unicode_asset_library_round_trips},
      {"ui_assets_panel_is_closed_by_default_and_opens_from_window_menu",
       ui_assets_panel_is_closed_by_default_and_opens_from_window_menu},
      {"ui_panel_layout_from_before_assets_panel_keeps_it_closed",
       ui_panel_layout_from_before_assets_panel_keeps_it_closed},
      {"ui_assets_add_selected_layers_creates_assets_with_thumbnails",
       ui_assets_add_selected_layers_creates_assets_with_thumbnails},
      {"ui_assets_place_keeps_text_and_shape_layers_editable", ui_assets_place_keeps_text_and_shape_layers_editable},
      {"ui_assets_canvas_drop_centers_on_the_drop_point", ui_assets_canvas_drop_centers_on_the_drop_point},
      {"ui_assets_drop_respects_edit_lock_and_is_not_a_file_or_layer_drag",
       ui_assets_drop_respects_edit_lock_and_is_not_a_file_or_layer_drag},
      {"ui_assets_search_and_categories_filter_the_grid", ui_assets_search_and_categories_filter_the_grid},
      {"ui_assets_manage_dialogs_rename_and_delete", ui_assets_manage_dialogs_rename_and_delete},
      {"ui_assets_layer_rows_dropped_on_the_panel_become_assets",
       ui_assets_layer_rows_dropped_on_the_panel_become_assets},
      {"ui_assets_startup_reads_nothing_and_the_grid_decodes_only_what_it_paints",
       ui_assets_startup_reads_nothing_and_the_grid_decodes_only_what_it_paints},
      {"ui_assets_pending_loads_survive_changes_and_a_closed_window",
       ui_assets_pending_loads_survive_changes_and_a_closed_window},
      {"ui_assets_missing_thumbnails_and_index_recover_in_the_background",
       ui_assets_missing_thumbnails_and_index_recover_in_the_background},
      {"ui_asset_library_background_adds_are_ordered_and_finished_at_close",
       ui_asset_library_background_adds_are_ordered_and_finished_at_close},
      {"ui_assets_review_capture", ui_assets_review_capture},
  };
}
