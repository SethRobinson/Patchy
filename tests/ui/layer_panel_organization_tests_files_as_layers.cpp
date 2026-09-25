// Files as Layers (issue 25): OS files dropped on the Layers panel and File >
// Import > Files as Layers, a part file of the layer_panel_organization group
// (the aggregator in layer_panel_organization_tests.cpp appends this part's
// vector). Paste and the script API have their own tests beside their peers.

#include "core/layer_tree.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"

#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QListWidget>
#include <QMimeData>
#include <QPoint>
#include <QStatusBar>
#include <QStringList>
#include <QUrl>

#include <string>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

constexpr int kTargetWidth = 64;
constexpr int kTargetHeight = 48;

patchy::PixelBuffer opaque_pixels(int width, int height, QColor color) {
  return solid_pixels(width, height, patchy::PixelFormat::rgba8(), color);
}

// Bottom to top: Background, "Mark" (20x12 at 10,8), folder "Set" holding "Inner"
// (8x8 at 40,30); "Mark" is active.
patchy::Document target_document() {
  patchy::Document document(kTargetWidth, kTargetHeight, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Background", opaque_pixels(kTargetWidth, kTargetHeight, QColor(255, 255, 255)));
  patchy::Layer mark(document.allocate_layer_id(), "Mark", opaque_pixels(20, 12, QColor(200, 30, 30)));
  mark.set_bounds(patchy::Rect{10, 8, 20, 12});
  document.add_layer(std::move(mark));
  patchy::Layer group(document.allocate_layer_id(), "Set", patchy::LayerKind::Group);
  patchy::Layer inner(document.allocate_layer_id(), "Inner", opaque_pixels(8, 8, QColor(30, 200, 30)));
  inner.set_bounds(patchy::Rect{40, 30, 8, 8});
  group.add_child(std::move(inner));
  document.add_layer(std::move(group));
  document.set_active_layer(document.layers()[1].id());
  return document;
}

QString artifact_path(const QString& name) {
  ensure_artifact_dir();
  const auto dir = QFileInfo(QStringLiteral("test-artifacts/files-as-layers")).absoluteFilePath();
  CHECK(QDir().mkpath(dir));
  return QDir::toNativeSeparators(dir + QLatin1Char('/') + name);
}

QString write_image(const QString& name, int width, int height, QColor color) {
  QImage image(width, height, QImage::Format_RGBA8888);
  image.fill(color);
  const auto path = artifact_path(name);
  QFile::remove(path);
  CHECK(image.save(path));
  return path;
}

QString write_bytes(const QString& name, const QByteArray& bytes) {
  const auto path = artifact_path(name);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(bytes);
  return path;
}

struct DropOutcome {
  bool entered{false};
  bool moved{false};
  bool dropped{false};
  Qt::DropAction action{Qt::IgnoreAction};
};

// The window's file drops offer Copy|Move like a real Explorer/Finder drag.
DropOutcome send_mime_drop(QWidget& target, QPoint position, const QMimeData& mime) {
  DropOutcome outcome;
  QDragEnterEvent enter(position, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&target, &enter);
  outcome.entered = enter.isAccepted();
  QDragMoveEvent move(position, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&target, &move);
  outcome.moved = move.isAccepted();
  QDropEvent drop(QPointF(position), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&target, &drop);
  outcome.dropped = drop.isAccepted();
  outcome.action = drop.dropAction();
  QApplication::processEvents();
  QApplication::processEvents();
  return outcome;
}

DropOutcome send_file_drop(QWidget& target, QPoint position, const QStringList& paths) {
  QMimeData mime;
  QList<QUrl> urls;
  for (const auto& path : paths) {
    urls.push_back(QUrl::fromLocalFile(path));
  }
  mime.setUrls(urls);
  return send_mime_drop(target, position, mime);
}

struct OpenedTarget {
  patchy::ui::CanvasWidget* canvas{nullptr};
  patchy::Document* document{nullptr};
  QListWidget* layer_list{nullptr};
};

OpenedTarget open_target(patchy::ui::MainWindow& window) {
  window.add_document_session(target_document(), QStringLiteral("Target"));
  QApplication::processEvents();
  OpenedTarget opened;
  opened.canvas = patchy::ui::MainWindowTestAccess::canvas(window);
  opened.document = patchy::ui::MainWindowTestAccess::document_for_canvas(window, opened.canvas);
  opened.layer_list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(opened.canvas != nullptr && opened.document != nullptr && opened.layer_list != nullptr);
  return opened;
}

QPoint top_edge_of_row(QListWidget& list, const QString& name) {
  const auto rect = list.visualItemRect(require_layer_item(list, name));
  return QPoint(rect.center().x(), rect.top() + 2);
}

std::vector<std::string> root_names(const patchy::Document& document) {
  std::vector<std::string> names;
  for (const auto& layer : document.layers()) {
    names.push_back(layer.name());
  }
  return names;
}

// Two PNGs dropped on the Background row's top edge land directly above it, in
// drop order, centered 1:1, both selected with the last one active, one undo step.
void ui_layer_panel_file_drop_adds_layers_at_drop_position() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  const auto target = open_target(window);
  const auto a = write_image(QStringLiteral("a.png"), 20, 12, QColor(200, 30, 30, 255));
  // A translucent file: its flat alpha becomes a layer mask on open.
  const auto b = write_image(QStringLiteral("b.png"), 8, 8, QColor(30, 200, 30, 128));

  const auto outcome =
      send_file_drop(*target.layer_list->viewport(), top_edge_of_row(*target.layer_list, QStringLiteral("Background")),
                     {a, b});
  CHECK(outcome.entered && outcome.moved && outcome.dropped);
  CHECK(outcome.action == Qt::CopyAction);

  const auto& document = std::as_const(*target.document);
  CHECK(root_names(document) == std::vector<std::string>({"Background", "a", "b", "Mark", "Set"}));
  const auto& a_layer = document.layers()[1];
  CHECK(a_layer.bounds().x == (kTargetWidth - 20) / 2 && a_layer.bounds().y == (kTargetHeight - 12) / 2);
  CHECK(a_layer.bounds().width == 20 && a_layer.bounds().height == 12);
  const auto& b_layer = document.layers()[2];
  CHECK(b_layer.bounds().width == 8 && b_layer.bounds().height == 8);
  CHECK(b_layer.mask().has_value());
  CHECK(document.active_layer_id() == b_layer.id());
  CHECK(target.layer_list->selectedItems().size() == 2);
  CHECK(require_layer_item(*target.layer_list, QStringLiteral("a"))->isSelected());
  CHECK(require_layer_item(*target.layer_list, QStringLiteral("b"))->isSelected());
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, target.canvas) == 1);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Added 2 layer")));

  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(root_names(std::as_const(*target.document)) == std::vector<std::string>({"Background", "Mark", "Set"}));
  patchy::ui::MainWindowTestAccess::redo(window);
  QApplication::processEvents();
  CHECK(root_names(std::as_const(*target.document)) ==
        std::vector<std::string>({"Background", "a", "b", "Mark", "Set"}));
}

// The insertion follows the panel's drop target: onto a folder row adds inside it
// (and keeps the folder open), the top edge of the top row adds on top, and the
// empty space below the last row adds at the bottom of the stack.
void ui_layer_panel_file_drop_targets_folder_and_stack_ends() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  const auto target = open_target(window);
  const auto c = write_image(QStringLiteral("c.png"), 4, 4, QColor(0, 0, 255, 255));
  const auto d = write_image(QStringLiteral("d.png"), 4, 4, QColor(0, 255, 255, 255));
  const auto e = write_image(QStringLiteral("e.png"), 4, 4, QColor(255, 0, 255, 255));
  auto& list = *target.layer_list;

  const auto set_rect = list.visualItemRect(require_layer_item(list, QStringLiteral("Set")));
  CHECK(send_file_drop(*list.viewport(), set_rect.center(), {c}).dropped);
  {
    const auto& document = std::as_const(*target.document);
    CHECK(root_names(document) == std::vector<std::string>({"Background", "Mark", "Set"}));
    const auto& set = document.layers()[2];
    CHECK(set.children().size() == 2);
    CHECK(set.children().back().name() == "c");
    CHECK(document.active_layer_id() == set.children().back().id());
  }
  // The folder stayed expanded: the new child has a row.
  CHECK(find_layer_item(list, QStringLiteral("c")) != nullptr);

  CHECK(send_file_drop(*list.viewport(), top_edge_of_row(list, QStringLiteral("Set")), {d}).dropped);
  CHECK(root_names(std::as_const(*target.document)) == std::vector<std::string>({"Background", "Mark", "Set", "d"}));

  const auto last_rect = list.visualItemRect(list.item(list.count() - 1));
  const auto below = QPoint(last_rect.center().x(), std::min(last_rect.bottom() + 6, list.viewport()->rect().bottom() - 1));
  CHECK(below.y() > last_rect.bottom());
  CHECK(send_file_drop(*list.viewport(), below, {e}).dropped);
  CHECK(root_names(std::as_const(*target.document)) ==
        std::vector<std::string>({"e", "Background", "Mark", "Set", "d"}));
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, target.canvas) == 3);
}

// A file drop never falls into the panel's selected-rows reorder (the pre-fix
// behavior), and a foreign non-file drag (text) is refused at enter.
void ui_layer_panel_file_drop_never_reorders_selected_rows() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  const auto target = open_target(window);
  auto& list = *target.layer_list;
  require_layer_item(list, QStringLiteral("Mark"))->setSelected(true);
  require_layer_item(list, QStringLiteral("Inner"))->setSelected(true);
  QApplication::processEvents();
  CHECK(list.selectedItems().size() == 2);

  const auto f = write_image(QStringLiteral("f.png"), 4, 4, QColor(10, 20, 30, 255));
  CHECK(send_file_drop(*list.viewport(), top_edge_of_row(list, QStringLiteral("Background")), {f}).dropped);
  const auto& document = std::as_const(*target.document);
  CHECK(root_names(document) == std::vector<std::string>({"Background", "f", "Mark", "Set"}));
  CHECK(document.layers()[3].children().size() == 1);
  CHECK(document.layers()[3].children().front().name() == "Inner");
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, target.canvas) == 1);

  require_layer_item(list, QStringLiteral("Mark"))->setSelected(true);
  require_layer_item(list, QStringLiteral("Inner"))->setSelected(true);
  QApplication::processEvents();
  QMimeData text;
  text.setText(QStringLiteral("not a layer"));
  const auto outcome = send_mime_drop(*list.viewport(), top_edge_of_row(list, QStringLiteral("Background")), text);
  CHECK(!outcome.entered);
  CHECK(!outcome.dropped);
  CHECK(root_names(document) == std::vector<std::string>({"Background", "f", "Mark", "Set"}));
  CHECK(document.layers()[3].children().front().name() == "Inner");
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, target.canvas) == 1);
}

// A multi-layer file becomes one folder named after it (its layers inside, exact
// positions when the sizes match); a single-layer file becomes one layer.
void ui_layer_panel_file_drop_wraps_psd_in_folder() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  const auto target = open_target(window);
  auto& list = *target.layer_list;

  patchy::Document stack(kTargetWidth, kTargetHeight, patchy::PixelFormat::rgba8());
  stack.add_pixel_layer("Base", opaque_pixels(kTargetWidth, kTargetHeight, QColor(240, 240, 240)));
  patchy::Layer spot(stack.allocate_layer_id(), "Spot", opaque_pixels(6, 6, QColor(20, 20, 200)));
  spot.set_bounds(patchy::Rect{10, 8, 6, 6});
  stack.add_layer(std::move(spot));
  const auto psd_path = artifact_path(QStringLiteral("stack.psd"));
  QFile::remove(psd_path);
  patchy::psd::DocumentIo::write_layered_rgb8_file(stack, patchy::ui::to_filesystem_path(psd_path));
  const auto bmp_path = write_image(QStringLiteral("single.bmp"), 5, 3, QColor(90, 90, 90, 255));

  CHECK(send_file_drop(*list.viewport(), top_edge_of_row(list, QStringLiteral("Background")), {psd_path, bmp_path})
            .dropped);
  const auto& document = std::as_const(*target.document);
  CHECK(root_names(document) == std::vector<std::string>({"Background", "stack", "single", "Mark", "Set"}));
  const auto& folder = document.layers()[1];
  CHECK(folder.kind() == patchy::LayerKind::Group);
  CHECK(folder.children().size() == 2);
  CHECK(folder.children()[0].name() == "Base");
  CHECK(folder.children()[1].name() == "Spot");
  const auto spot_bounds = folder.children()[1].bounds();
  CHECK(spot_bounds.x == 10 && spot_bounds.y == 8 && spot_bounds.width == 6 && spot_bounds.height == 6);
  CHECK(document.layers()[2].kind() == patchy::LayerKind::Pixel);
  CHECK(document.active_layer_id() == document.layers()[2].id());
  CHECK(require_layer_item(list, QStringLiteral("stack"))->isSelected());
  CHECK(require_layer_item(list, QStringLiteral("single"))->isSelected());
  CHECK(list.selectedItems().size() == 2);
}

// An unreadable file is reported and skipped; the rest still land under one undo
// step. Nothing but unreadable files adds nothing and pushes no snapshot.
void ui_layer_panel_file_drop_reports_unreadable_files() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  window.set_cli_automation_mode(true);  // the failure box stays closed
  const auto target = open_target(window);
  auto& list = *target.layer_list;
  const auto good = write_image(QStringLiteral("good.png"), 4, 4, QColor(1, 2, 3, 255));
  const auto garbage = write_bytes(QStringLiteral("garbage.png"), QByteArray("this is not a png"));

  CHECK(send_file_drop(*list.viewport(), top_edge_of_row(list, QStringLiteral("Background")), {good, garbage})
            .dropped);
  CHECK(root_names(std::as_const(*target.document)) == std::vector<std::string>({"Background", "good", "Mark", "Set"}));
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, target.canvas) == 1);
  const auto status = window.statusBar()->currentMessage();
  CHECK(status.contains(QStringLiteral("Added 1 layer")));
  CHECK(status.contains(QStringLiteral("could not be opened")));

  CHECK(send_file_drop(*list.viewport(), top_edge_of_row(list, QStringLiteral("Background")), {garbage}).dropped);
  CHECK(root_names(std::as_const(*target.document)) == std::vector<std::string>({"Background", "good", "Mark", "Set"}));
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, target.canvas) == 1);
}

// File > Import > Files as Layers: registered with its hotkey id, disabled with no
// document, and its path-taking core inserts above the topmost selected row
// (inside that row's folder), selecting the new layers under one undo step.
void ui_import_files_as_layers_action_registered_and_inserts_above_selection() {
  patchy::ui::MainWindow window;
  show_window_empty(window);
  auto* action = window.findChild<QAction*>(QStringLiteral("fileImportFilesAsLayersAction"));
  CHECK(action != nullptr);
  const auto* command = window.hotkey_registry().find_command(QStringLiteral("file.import_files_as_layers"));
  CHECK(command != nullptr);
  CHECK(command != nullptr && command->action == action);
  CHECK(!action->isEnabled());

  const auto target = open_target(window);
  CHECK(action->isEnabled());
  auto& list = *target.layer_list;
  list.setCurrentItem(require_layer_item(list, QStringLiteral("Inner")));
  QApplication::processEvents();
  CHECK(std::as_const(*target.document).active_layer_id() ==
        std::as_const(*target.document).layers()[2].children()[0].id());

  const auto g = write_image(QStringLiteral("g.png"), 4, 4, QColor(50, 60, 70, 255));
  const auto h = write_image(QStringLiteral("h.png"), 4, 4, QColor(80, 90, 100, 255));
  patchy::ui::MainWindowTestAccess::import_files_as_layers_with_paths(window, {g, h});
  QApplication::processEvents();
  const auto& document = std::as_const(*target.document);
  CHECK(root_names(document) == std::vector<std::string>({"Background", "Mark", "Set"}));
  const auto& set = document.layers()[2];
  CHECK(set.children().size() == 3);
  CHECK(set.children()[0].name() == "Inner");
  CHECK(set.children()[1].name() == "g");
  CHECK(set.children()[2].name() == "h");
  CHECK(document.active_layer_id() == set.children()[2].id());
  CHECK(list.selectedItems().size() == 2);
  CHECK(require_layer_item(list, QStringLiteral("g"))->isSelected());
  CHECK(require_layer_item(list, QStringLiteral("h"))->isSelected());
  CHECK(patchy::ui::MainWindowTestAccess::undo_depth_for_canvas(window, target.canvas) == 1);
}

}  // namespace

std::vector<patchy::test::TestCase> layer_panel_organization_tests_files_as_layers_part() {
  return {
      {"ui_layer_panel_file_drop_adds_layers_at_drop_position", ui_layer_panel_file_drop_adds_layers_at_drop_position},
      {"ui_layer_panel_file_drop_targets_folder_and_stack_ends",
       ui_layer_panel_file_drop_targets_folder_and_stack_ends},
      {"ui_layer_panel_file_drop_never_reorders_selected_rows", ui_layer_panel_file_drop_never_reorders_selected_rows},
      {"ui_layer_panel_file_drop_wraps_psd_in_folder", ui_layer_panel_file_drop_wraps_psd_in_folder},
      {"ui_layer_panel_file_drop_reports_unreadable_files", ui_layer_panel_file_drop_reports_unreadable_files},
      {"ui_import_files_as_layers_action_registered_and_inserts_above_selection",
       ui_import_files_as_layers_action_registered_and_inserts_above_selection},
  };
}
