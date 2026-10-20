// Table-described native Smart Filters in the app (core/smart_filter_schema.hpp,
// docs/smart-filters-distort-stylize.md): an imported Photoshop entry renders,
// edits through its settings dialog, and undoes.

#include "core/document.hpp"
#include "core/smart_filter.hpp"
#include "core/smart_filter_schema.hpp"
#include "core/smart_object.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/filter_workflows.hpp"
#include "ui/main_window.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "smart_filter_test_support.hpp"

#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;

// Writes an RGBA8 layer raster to test-artifacts/<name>.png for review shots.
void save_layer_artifact(const patchy::PixelBuffer& pixels, const char* name) {
  ensure_artifact_dir();
  QImage image(pixels.width(), pixels.height(), QImage::Format_RGBA8888);
  for (std::int32_t y = 0; y < pixels.height(); ++y) {
    std::memcpy(image.scanLine(y), pixels.pixel(0, y), static_cast<std::size_t>(pixels.width()) * 4U);
  }
  CHECK(image.save(QStringLiteral("test-artifacts/%1.png").arg(QString::fromLatin1(name))));
}

void ui_smart_filter_imported_find_edges_renders_edits_and_undoes() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto path = QString::fromStdWString(
      patchy::test::committed_psd_fixture_path("photoshop-smart-filter-find-edges.psd").wstring());
  CHECK(QFileInfo::exists(path));
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();

  const auto layer_id = select_named_layer(window, QStringLiteral("Layer 1"));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto current_layer = [&]() -> const patchy::Layer& {
    const auto* layer = std::as_const(document).find_layer(layer_id);
    CHECK(layer != nullptr && layer->smart_filter_stack() != nullptr);
    return *layer;
  };
  CHECK(patchy::smart_object_lock_reason(current_layer()).empty());
  CHECK(current_layer().smart_filter_stack()->support ==
        patchy::SmartFilterStackSupport::Supported);
  CHECK(current_layer().smart_filter_stack()->entries.front().kind ==
        patchy::SmartFilterKind::FindEdges);

  auto* layers = window.findChild<QListWidget*>(QStringLiteral("layerList"));
  CHECK(layers != nullptr);
  const auto row = [&]() -> QWidget* {
    auto* item = require_layer_item(*layers, QStringLiteral("Layer 1"));
    auto* widget = layers->itemWidget(item);
    CHECK(widget != nullptr);
    return widget;
  };
  bool labelled = false;
  for (auto* label : row()->findChildren<QLabel*>(QStringLiteral("layerSmartFilterEntryLabel"))) {
    labelled = labelled || label->text() == QStringLiteral("Find Edges");
  }
  CHECK(labelled);

  const auto original_pixels = current_layer().pixels();
  save_layer_artifact(original_pixels, "smart_filter_find_edges_photoshop_baked");
  const auto undo_before = patchy::ui::MainWindowTestAccess::active_session_undo_depth(window);
  bool dialog_opened = false;
  QTimer::singleShot(20, [&] {
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("patchyFilterDialog")));
    CHECK(dialog != nullptr);
    auto* opacity = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("smartFilterOpacitySpin"));
    CHECK(opacity != nullptr);
    opacity->setValue(50.0);
    dialog_opened = true;
    dialog->accept();
  });
  auto* edit = row()->findChild<QToolButton*>(QStringLiteral("layerSmartFilterEditButton"));
  CHECK(edit != nullptr && edit->isEnabled());
  edit->click();
  CHECK(process_events_until([&] {
    return std::abs(current_layer().smart_filter_stack()->entries.front().opacity - 0.5) < 1e-9;
  }));
  CHECK(dialog_opened);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_undo_depth(window) == undo_before + 1U);
  // Half-strength Find Edges is a new render, not Photoshop's full-strength preview.
  CHECK(!patchy::ui::pixel_buffers_equal(current_layer().pixels(), original_pixels));
  save_layer_artifact(current_layer().pixels(), "smart_filter_find_edges_patchy_half_opacity");

  require_hotkey_action(window, QStringLiteral("edit.undo"))->trigger();
  CHECK(process_events_until([&] {
    return std::abs(current_layer().smart_filter_stack()->entries.front().opacity - 1.0) < 1e-9;
  }));
  CHECK(patchy::ui::pixel_buffers_equal(current_layer().pixels(), original_pixels));
}

}  // namespace

std::vector<patchy::test::TestCase> smart_filter_tests_part4() {
  return {
      {"ui_smart_filter_imported_find_edges_renders_edits_and_undoes",
       ui_smart_filter_imported_find_edges_renders_edits_and_undoes},
  };
}
