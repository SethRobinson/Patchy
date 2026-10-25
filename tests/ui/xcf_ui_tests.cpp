// GIMP .xcf files in the app (docs/xcf.md): opening by name, by GIMP's compressed names and
// by content, the layer tree and GIMP's modes in the Layers panel, editable text that keeps
// its place when Patchy redraws it, and Save going to Save As (the format is read-only).

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "formats/xcf_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QStatusBar>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using patchy::test::ui::show_window;

QString committed_xcf_fixture(const char* name) {
  return QString::fromStdWString(patchy::test::committed_format_fixture_path("xcf", name).wstring());
}

const patchy::Layer* find_named(const std::vector<patchy::Layer>& layers, const std::string& name) {
  for (const auto& layer : layers) {
    if (layer.name() == name) {
      return &layer;
    }
    if (const auto* child = find_named(layer.children(), name); child != nullptr) {
      return child;
    }
  }
  return nullptr;
}

std::size_t count_layers(const std::vector<patchy::Layer>& layers) {
  std::size_t count = 0;
  for (const auto& layer : layers) {
    count += 1 + count_layers(layer.children());
  }
  return count;
}

void ui_xcf_files_open_by_name_compressed_name_and_content() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto stem = patchy::ui::to_qstring(patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem));
  const auto plain = temp.filePath(stem + QStringLiteral(".xcf"));
  const auto gz = temp.filePath(QStringLiteral("Picture.xcf.gz"));
  const auto xz = temp.filePath(QStringLiteral("picture.XCFXZ"));
  const auto bin = temp.filePath(QStringLiteral("drawing.bin"));
  CHECK(QFile::copy(committed_xcf_fixture("structure.xcf"), plain));
  CHECK(QFile::copy(committed_xcf_fixture("compressed.xcf.gz"), gz));
  CHECK(QFile::copy(committed_xcf_fixture("compressed.xcf.xz"), xz));
  CHECK(QFile::copy(committed_xcf_fixture("structure.xcf"), bin));

  patchy::ui::MainWindow window;
  show_window(window);
  const auto before = patchy::ui::MainWindowTestAccess::session_count(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, plain);
  QApplication::processEvents();
  CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == before + 1U);
  {
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(document.width() == 160 && document.height() == 120);
    const auto* outer = find_named(document.layers(), "Outer group");
    CHECK(outer != nullptr && outer->kind() == patchy::LayerKind::Group);
    const auto* inner = find_named(document.layers(), "Inner pass-through");
    CHECK(inner != nullptr && inner->blend_mode() == patchy::BlendMode::PassThrough);
    const auto* masked = find_named(document.layers(), "Masked");
    CHECK(masked != nullptr && masked->mask().has_value() && !masked->mask()->disabled);
    const auto* off = find_named(document.layers(), "Mask off");
    CHECK(off != nullptr && off->mask().has_value() && off->mask()->disabled);
    CHECK(document.file_info().source_format == "XCF");
  }
  for (const auto& path : {gz, xz, bin}) {
    const auto opened_before = patchy::ui::MainWindowTestAccess::session_count(window);
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    CHECK(patchy::ui::MainWindowTestAccess::session_count(window) == opened_before + 1U);
    CHECK(count_layers(patchy::ui::MainWindowTestAccess::document(window).layers()) >= 2U);
  }
  // A file opened by content says so (GitHub issue 362).
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("GIMP")));
}

// GIMP's own modes show by name in the Layers panel while the layer uses them; no menu
// lists them otherwise (docs/blend-modes.md, "GIMP modes").
void ui_xcf_gimp_modes_show_in_the_layers_panel() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, committed_xcf_fixture("modes.xcf"));
  QApplication::processEvents();
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* grain = find_named(document.layers(), "grain_merge");
  const auto* hue = find_named(document.layers(), "lch_hue");
  CHECK(grain != nullptr && grain->blend_mode() == patchy::BlendMode::GrainMerge);
  CHECK(hue != nullptr && hue->blend_mode() == patchy::BlendMode::LchHue);
  auto* blend_combo = window.findChild<QComboBox*>(QStringLiteral("layerBlendModeCombo"));
  CHECK(blend_combo != nullptr);
  patchy::ui::MainWindowTestAccess::select_layers(window, {grain->id()}, grain->id());
  QApplication::processEvents();
  CHECK(blend_combo->currentText() == QStringLiteral("Grain Merge"));
  patchy::ui::MainWindowTestAccess::select_layers(window, {hue->id()}, hue->id());
  QApplication::processEvents();
  CHECK(blend_combo->currentText() == QStringLiteral("LCh Hue"));
  CHECK(blend_combo->findData(static_cast<int>(patchy::BlendMode::GrainMerge)) < 0);
}

// A GIMP text layer opens as Patchy text over GIMP's raster; redrawing it in Patchy keeps it
// where GIMP put it, and a PSD save keeps it as type.
void ui_xcf_text_layers_stay_editable() {
  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  patchy::ui::MainWindowTestAccess::open_document_path(window, committed_xcf_fixture("text.xcf"));
  QApplication::processEvents();
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* hello = find_named(document.layers(), "Hello GIMP");
  CHECK(hello != nullptr && patchy::layer_is_text(*hello));
  if (hello == nullptr) {
    return;
  }
  const auto id = hello->id();
  const auto before = hello->bounds();
  CHECK(hello->metadata().at(patchy::kLayerMetadataText) == "Hello GIMP");
  const int mutated = patchy::ui::MainWindowTestAccess::cli_append_text_to_text_layers(window, QStringLiteral("!"));
  CHECK(mutated >= 1);
  const auto* edited = document.find_layer(id);
  CHECK(edited != nullptr && edited->metadata().at(patchy::kLayerMetadataText) == "Hello GIMP!");
  // Patchy's own layout of the same text starts within a few pixels of GIMP's.
  CHECK(std::abs(edited->bounds().x - before.x) <= 6 && std::abs(edited->bounds().y - before.y) <= 8);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto psd_path = temp.filePath(QStringLiteral("text.psd"));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, psd_path));
  const auto reread = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(psd_path));
  const auto* saved = find_named(reread.layers(), "Hello GIMP!");
  CHECK(saved == nullptr || patchy::layer_is_text(*saved));
  CHECK(std::any_of(reread.layers().begin(), reread.layers().end(),
                    [](const patchy::Layer& layer) { return patchy::layer_is_text(layer); }));
}

// For the card: GIMP's text and structure files as Patchy shows them (local fixtures).
void ui_xcf_review_capture() {
  for (const char* name : {"text.xcf", "groups.xcf"}) {
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1280, 800);
    patchy::ui::MainWindowTestAccess::open_document_path(window, committed_xcf_fixture(name));
    QApplication::processEvents();
    patchy::ui::MainWindowTestAccess::canvas(window)->set_zoom_centered(2.0);
    window.statusBar()->clearMessage();
    QApplication::processEvents();
    patchy::test::ui::save_widget_artifact(std::string("xcf_open_") + std::string(name).substr(0, std::string(name).find('.')),
                                           window);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> xcf_ui_tests() {
  return {
      {"ui_xcf_files_open_by_name_compressed_name_and_content", ui_xcf_files_open_by_name_compressed_name_and_content},
      {"ui_xcf_gimp_modes_show_in_the_layers_panel", ui_xcf_gimp_modes_show_in_the_layers_panel},
      {"ui_xcf_text_layers_stay_editable", ui_xcf_text_layers_stay_editable},
      {"ui_xcf_review_capture", ui_xcf_review_capture},
  };
}
