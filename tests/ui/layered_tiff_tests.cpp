// Layered TIFF through the app's open path (docs/tiff.md): Photoshop layers in tag 37724
// open as layers, damaged layer data falls back to the composite with a note, and a
// multi-page TIFF opens page 1 with a note.
#include "core/document.hpp"
#include "psd/psd_tiff.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "layered_tiff_fixture.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QStatusBar>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;
namespace fixture = patchy::test::layered_tiff;

QString artifact_dir() {
  const auto dir = QStringLiteral("test-artifacts/layered-tiff");
  QDir().mkpath(dir);
  return dir;
}

QString write_file(const QString& name, const std::vector<std::uint8_t>& bytes) {
  const auto path = QDir(artifact_dir()).absoluteFilePath(name);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  return path;
}

int layer_count(const std::vector<patchy::Layer>& layers) {
  int count = 0;
  for (const auto& layer : layers) {
    count += 1 + layer_count(layer.children());
  }
  return count;
}

void ui_layered_tiff_opens_with_its_layers() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  for (const bool little : {true, false}) {
    const auto path = write_file(little ? QStringLiteral("layers-ibm.tif") : QStringLiteral("layers-mac.tif"),
                                 fixture::sample_tiff(little, 8, 24));
    patchy::ui::MainWindow window;
    show_window(window);
    MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    const auto& document = std::as_const(MainWindowTestAccess::document(window));
    CHECK(document.width() == 192 && document.height() == 144);
    CHECK(document.layers().size() == 3U);
    CHECK(layer_count(document.layers()) == 4);  // Background, the masked layer, Group > Inside
    CHECK(document.layers().back().name() == "Group");
    CHECK(document.layers().back().kind() == patchy::LayerKind::Group);
    CHECK(document.file_info().source_format == "TIFF");
    // The layers panel lists them; capture it for review.
    auto* list = window.findChild<QListWidget*>(QStringLiteral("layerList"));
    CHECK(list != nullptr);
    if (list != nullptr) {
      CHECK(list->count() >= 3);
    }
    if (little) {
      if (auto* panel = window.findChild<QWidget*>(QStringLiteral("layersPanel")); panel != nullptr) {
        panel->grab().save(QDir(artifact_dir()).absoluteFilePath(QStringLiteral("layers-panel.png")));
      }
      window.grab().save(QDir(artifact_dir()).absoluteFilePath(QStringLiteral("window.png")));
    }
  }
}

void ui_layered_tiff_with_damaged_layers_opens_the_composite() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  auto tiff = fixture::sample_tiff(true);
  // Cut the channel data short: the header still says layers, the records cannot be read.
  const std::array<std::uint8_t, 4> needle{'r', 'y', 'a', 'L'};
  const auto found = std::search(tiff.begin(), tiff.end(), needle.begin(), needle.end());
  CHECK(found != tiff.end());
  if (found == tiff.end()) {
    return;
  }
  // Layer count 0x7FFF: far more records than the block holds.
  const auto count_at = static_cast<std::size_t>(found - tiff.begin()) + 8U;
  tiff[count_at] = 0xFF;
  tiff[count_at + 1U] = 0x7F;
  const auto path = write_file(QStringLiteral("damaged-layers.tif"), tiff);
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(document.width() == 8 && document.height() == 6);
  CHECK(document.layers().size() == 1U);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("could not be read")));
}

void ui_multi_page_tiff_opens_its_first_page_with_a_note() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  const auto path = write_file(QStringLiteral("pages.tif"), fixture::tiff_file(true, 8, 6, 8, false, {}, 2, 3));
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(document.width() == 8 && document.height() == 6);
  CHECK(document.layers().size() == 1U);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("3 pages")));
}

}  // namespace

std::vector<patchy::test::TestCase> layered_tiff_ui_tests() {
  return {
      {"ui_layered_tiff_opens_with_its_layers", ui_layered_tiff_opens_with_its_layers},
      {"ui_layered_tiff_with_damaged_layers_opens_the_composite",
       ui_layered_tiff_with_damaged_layers_opens_the_composite},
      {"ui_multi_page_tiff_opens_its_first_page_with_a_note", ui_multi_page_tiff_opens_its_first_page_with_a_note},
  };
}
