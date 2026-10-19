// Layered TIFF through the app's open path (docs/tiff.md): Photoshop layers in tag 37724
// open as layers, damaged layer data falls back to the composite with a note, and a
// multi-page TIFF opens page 1 with a note.
#include "core/document.hpp"
#include "psd/psd_tiff.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "layered_tiff_fixture.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "scripting_test_support.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFileInfo>
#include <QImage>
#include <QSysInfo>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QStatusBar>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <optional>
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


std::vector<std::uint8_t> read_bytes(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto data = file.readAll();
  return {reinterpret_cast<const std::uint8_t*>(data.constData()),
          reinterpret_cast<const std::uint8_t*>(data.constData()) + data.size()};
}

// A document a flat TIFF would lose layers from: two pixel layers, the top one hidden.
void add_layered_document(patchy::ui::MainWindow& window) {
  patchy::Document document(32, 24, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Bottom", solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(40, 90, 200)));
  auto& top = document.add_pixel_layer("Top", solid_pixels(32, 24, patchy::PixelFormat::rgba8(), QColor(220, 60, 30)));
  top.set_opacity(0.5F);
  window.add_document_session(std::move(document), QStringLiteral("Layered TIFF Save"));
}

void ui_layered_tiff_save_keeps_layers_and_flat_saves_stay_flat() {
  patchy::ui::MainWindow window;
  show_window(window);
  add_layered_document(window);
  const auto layered_path = QDir(artifact_dir()).absoluteFilePath(QStringLiteral("saved-layers.tif"));
  QFile::remove(layered_path);

  // A layered document saves a layered TIFF by default: a real save that owns the path.
  CHECK(MainWindowTestAccess::save_document_to_path(window, layered_path));
  CHECK(MainWindowTestAccess::active_session_path(window) == layered_path);
  CHECK(!MainWindowTestAccess::active_session_is_modified(window));
  const auto layered = read_bytes(layered_path);
  CHECK(patchy::psd::tiff_has_photoshop_layers(layered));
  // Other readers see the composite: Qt decodes it.
  const auto composite = QImage(layered_path);
  CHECK(!composite.isNull() && composite.width() == 32 && composite.height() == 24);

  // Reopened in another window, the layers come back.
  {
    patchy::ui::MainWindow reader;
    show_window(reader);
    MainWindowTestAccess::open_document_path(reader, layered_path);
    QApplication::processEvents();
    const auto& reopened = std::as_const(MainWindowTestAccess::document(reader));
    CHECK(reopened.layers().size() == 2U);
    if (reopened.layers().size() == 2U) {
      CHECK(reopened.layers().back().name() == "Top");
      CHECK(std::abs(reopened.layers().back().opacity() - 0.5F) < 0.01F);
    }
  }

  // Layers turned off: a flattened copy, no layer data, the session keeps its path.
  const auto flat_path = QDir(artifact_dir()).absoluteFilePath(QStringLiteral("saved-flat-copy.tif"));
  QFile::remove(flat_path);
  patchy::ui::ImageSaveOptions flat_options;
  flat_options.tiff_layers = false;
  CHECK(MainWindowTestAccess::save_document_to_path_flatten_confirmed(window, flat_path, flat_options));
  CHECK(!patchy::psd::tiff_has_photoshop_layers(read_bytes(flat_path)));
  CHECK(MainWindowTestAccess::active_session_path(window) == layered_path);

  // A flat document takes the unchanged Qt path: no layer data, Qt's byte order.
  patchy::Document single(16, 16, patchy::PixelFormat::rgb8());
  single.add_pixel_layer("Background", solid_pixels(16, 16, patchy::PixelFormat::rgb8(), QColor(10, 200, 30)));
  window.add_document_session(std::move(single), QStringLiteral("Flat"));
  const auto single_path = QDir(artifact_dir()).absoluteFilePath(QStringLiteral("saved-single.tif"));
  QFile::remove(single_path);
  CHECK(MainWindowTestAccess::save_document_to_path(window, single_path));
  const auto single_bytes = read_bytes(single_path);
  CHECK(!patchy::psd::tiff_has_photoshop_layers(single_bytes));
  CHECK(single_bytes.size() > 2U && single_bytes[0] == (QSysInfo::ByteOrder == QSysInfo::LittleEndian ? 'I' : 'M'));
}

void ui_tiff_options_dialog_offers_keep_layers() {
  bool saw_dialog = false;
  bool checked_by_default = false;
  QTimer poller;
  int attempts = 0;
  QObject::connect(&poller, &QTimer::timeout, [&] {
    if (++attempts > 500) {
      poller.stop();
      return;
    }
    auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("tiffSaveOptionsDialog")));
    if (dialog == nullptr || !dialog->isVisible()) {
      return;
    }
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("tiffLayersCheck"));
    saw_dialog = check != nullptr;
    if (check != nullptr) {
      checked_by_default = check->isChecked();
      dialog->grab().save(QDir(artifact_dir()).absoluteFilePath(QStringLiteral("tiff-options.png")));
      check->setChecked(false);
    }
    dialog->accept();
    poller.stop();
  });
  poller.start(20);
  const auto result = patchy::ui::prompt_tiff_save_options(nullptr, patchy::ui::ImageSaveOptions{});
  CHECK(saw_dialog);
  CHECK(checked_by_default);
  CHECK(result.has_value() && result->tiff_layers == std::optional<bool>(false));
}

void ui_script_save_as_tiff_layers_option() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto base = QDir(artifact_dir()).absoluteFilePath(QStringLiteral("script"));
  for (const auto* suffix : {"-default.tif", "-off.tif", "-export.tif", "-export-on.tif"}) {
    QFile::remove(base + QString::fromLatin1(suffix));
  }
  const auto source = QStringLiteral(R"JS(
    var p = '%1';
    var d = app.newDocument(20, 10);
    d.activeLayer.fill('#204080');
    d.addLayer('Second').fill('#c03010');
    if (!d.exportAs(p + '-export.tif')) throw Error('export');
    if (!d.exportAs(p + '-export-on.tif', {layers: true})) throw Error('export on');
    if (!d.saveAs(p + '-off.tif', {layers: false})) throw Error('off');
    try { d.saveAs(p + '-cmyk.tif', {layers: true, colorMode: 'cmyk'}); console.log('cmyk=accepted'); }
    catch (e) { console.log('cmyk=refused'); }
    try { d.saveAs(p + '-bad.png', {layers: true}); console.log('png=accepted'); }
    catch (e) { console.log('png=refused'); }
    if (!d.saveAs(p + '-default.tif')) throw Error('default');
  )JS").arg(base);
  CHECK(patchy::test::ui::scripting_support::run_script(window, source));
  CHECK(patchy::test::ui::scripting_support::backlog_contains(window, QStringLiteral("cmyk=refused")));
  CHECK(patchy::test::ui::scripting_support::backlog_contains(window, QStringLiteral("png=refused")));
  CHECK(patchy::psd::tiff_has_photoshop_layers(read_bytes(base + QStringLiteral("-default.tif"))));
  CHECK(!patchy::psd::tiff_has_photoshop_layers(read_bytes(base + QStringLiteral("-off.tif"))));
  CHECK(!patchy::psd::tiff_has_photoshop_layers(read_bytes(base + QStringLiteral("-export.tif"))));
  CHECK(patchy::psd::tiff_has_photoshop_layers(read_bytes(base + QStringLiteral("-export-on.tif"))));
  CHECK(!QFileInfo::exists(base + QStringLiteral("-cmyk.tif")));
}

}  // namespace

std::vector<patchy::test::TestCase> layered_tiff_ui_tests() {
  return {
      {"ui_layered_tiff_opens_with_its_layers", ui_layered_tiff_opens_with_its_layers},
      {"ui_layered_tiff_with_damaged_layers_opens_the_composite",
       ui_layered_tiff_with_damaged_layers_opens_the_composite},
      {"ui_multi_page_tiff_opens_its_first_page_with_a_note", ui_multi_page_tiff_opens_its_first_page_with_a_note},
      {"ui_layered_tiff_save_keeps_layers_and_flat_saves_stay_flat",
       ui_layered_tiff_save_keeps_layers_and_flat_saves_stay_flat},
      {"ui_tiff_options_dialog_offers_keep_layers", ui_tiff_options_dialog_offers_keep_layers},
      {"ui_script_save_as_tiff_layers_option", ui_script_save_as_tiff_layers_option},
  };
}
