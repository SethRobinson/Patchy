// Layered TIFF through the app's open path (docs/tiff.md): Photoshop layers in tag 37724
// open as layers, damaged layer data falls back to the composite with a note, a
// multi-page TIFF opens page 1 with a note, and a flat TIFF Qt refuses opens through
// Patchy's own decoder.
#include "core/document.hpp"
#include "core/pixel_depth.hpp"
#include "psd/psd_tiff.hpp"
#include "ui/app_settings.hpp"
#include "ui/image_save_options_dialog.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "layered_tiff_fixture.hpp"
#include "flat_tiff_fixture.hpp"
#include "local_psd_fixtures.hpp"
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
#include <QMessageBox>
#include <QAbstractButton>
#include <QStatusBar>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <optional>
#include <array>
#include <cstdint>
#include <iostream>
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

// Answers the multi-page TIFF question (tiffPagesMessageBox) with the named button once it
// shows; `asked` counts the questions seen. Polls, because the open runs other event loops
// before it asks.
class TiffPagesAnswer {
 public:
  TiffPagesAnswer(QString button, int* asked) : button_(std::move(button)), asked_(asked) {
    timer_.setInterval(5);
    QObject::connect(&timer_, &QTimer::timeout, [this] {
      auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("tiffPagesMessageBox")));
      if (box == nullptr || !box->isVisible()) {
        return;
      }
      ++*asked_;
      if (auto* button = box->findChild<QAbstractButton*>(button_); button != nullptr) {
        button->click();
      } else {
        patchy::test::report_failure("tiffPagesMessageBox has no button " + button_.toStdString());
        box->reject();
      }
    });
    timer_.start();
  }

 private:
  QString button_;
  int* asked_;
  QTimer timer_;
};

// A multi-page TIFF of mixed pages: page 1 8-bit RGB (Qt reads it), page 2 a larger 16-bit
// page, page 3 32-bit float with unspecified extra samples (only the fallback decoder
// reads it), page 4 in a compression neither decoder has, page 5 palette with a PageName.
QString write_mixed_pages_tiff(const QString& name) {
  namespace flat = patchy::test::flat_tiff;
  flat::Spec first;
  first.width = 8;
  first.height = 6;
  first.value = [](std::uint32_t, std::uint32_t, std::uint32_t sample) { return sample == 0U ? 200.0 : 10.0; };
  flat::Spec second;
  second.kind = flat::Kind::U16;
  second.width = 14;
  second.height = 5;
  second.value = [](std::uint32_t, std::uint32_t, std::uint32_t sample) { return sample == 2U ? 65535.0 : 0.0; };
  flat::Spec third;
  third.kind = flat::Kind::F32;
  third.samples = 7;
  third.extra = std::vector<std::uint16_t>{0, 0, 0, 0};  // Qt refuses this layout (issue 623)
  third.width = 6;
  third.height = 10;
  third.value = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.5; };
  flat::Spec unreadable = first;
  unreadable.compression = 50000;
  flat::Spec palette;
  palette.photometric = 3;
  palette.samples = 1;
  palette.width = 4;
  palette.height = 4;
  palette.color_map = std::vector<std::uint16_t>(3U * 256U, 0);
  palette.color_map[1] = 65535;  // index 1: red
  palette.page_name = "Named page";
  palette.value = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0; };
  return write_file(name, flat::write_tiff(std::vector<flat::Spec>{first, second, third, unreadable, palette}));
}

struct DeepEditingOn {
  DeepEditingOn() { patchy::set_deep_editing_override(true); }
  ~DeepEditingOn() { patchy::set_deep_editing_override(std::nullopt); }
};

// GitHub issue 625: Pages as Layers stacks every page it can read, page 1 on top, on the
// largest page's canvas at the highest depth; the unreadable page is named in one note.
void ui_multi_page_tiff_opens_pages_as_layers() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  SettingsValueRestorer target_setting(QStringLiteral("imports/tiffPagesTarget"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  DeepEditingOn deep;
  const auto path = write_mixed_pages_tiff(QStringLiteral("mixed-pages.tif"));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions_before = MainWindowTestAccess::session_count(window);
  int asked = 0;
  {
    TiffPagesAnswer answer(QStringLiteral("tiffPagesLayersButton"), &asked);
    MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
  }
  CHECK(asked == 1);
  CHECK(MainWindowTestAccess::session_count(window) == sessions_before + 1U);
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(document.width() == 14 && document.height() == 10);
  CHECK(document.color_state().bit_depth == patchy::BitDepth::Float32);
  CHECK(document.channels().empty());
  CHECK(document.file_info().source_format == "TIFF");
  const auto& layers = document.layers();
  CHECK(layers.size() == 4U);
  if (layers.size() == 4U) {
    CHECK(layers[0].name() == "Named page");
    CHECK(layers[1].name() == "Page 3");
    CHECK(layers[2].name() == "Page 2");
    CHECK(layers[3].name() == "Page 1");
    CHECK(document.active_layer_id() == layers[3].id());
    CHECK(layers[3].bounds().width == 8 && layers[3].bounds().height == 6);
    CHECK(layers[2].bounds().width == 14 && layers[2].bounds().height == 5);
    for (const auto& layer : layers) {
      CHECK(layer.visible());
      CHECK(layer.bounds().x == 0 && layer.bounds().y == 0);
    }
    // The palette page expanded to RGB: red, opaque.
    std::vector<float> row(4U);
    patchy::load_rgba_row(layers[0].pixels(), 0, 0, 1, patchy::DeepDomain::Linear, row);
    CHECK(std::fabs(row[0] - 255.0F) < 0.5F && std::fabs(row[1]) < 0.5F && std::fabs(row[3] - 255.0F) < 0.5F);
  }
  const auto& notes = MainWindowTestAccess::active_session_import_notes(window);
  CHECK(notes.join(QLatin1Char('\n')).contains(QStringLiteral("opened 4 pages as layers")));
  CHECK(notes.join(QLatin1Char('\n')).contains(QStringLiteral("left out: 4")));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("imports/tiffPagesTarget")).toString() ==
        QStringLiteral("layers"));
  window.grab().save(QDir(artifact_dir()).absoluteFilePath(QStringLiteral("pages-as-layers.png")));
}

// Separate Documents opens one tab per readable page ("file - Page N"), page 1 active;
// Cancel opens nothing; a script's app.open gets page 1 without being asked.
void ui_multi_page_tiff_separate_documents_cancel_and_scripts() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  SettingsValueRestorer target_setting(QStringLiteral("imports/tiffPagesTarget"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  DeepEditingOn deep;
  const auto path = write_mixed_pages_tiff(QStringLiteral("mixed-pages-documents.tif"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    const auto before = MainWindowTestAccess::session_count(window);
    int asked = 0;
    {
      TiffPagesAnswer answer(QStringLiteral("tiffPagesDocumentsButton"), &asked);
      MainWindowTestAccess::open_document_path(window, path);
      QApplication::processEvents();
    }
    CHECK(asked == 1);
    CHECK(MainWindowTestAccess::session_count(window) == before + 4U);
    if (MainWindowTestAccess::session_count(window) == before + 4U) {
      CHECK(MainWindowTestAccess::session_title(window, before) ==
            QStringLiteral("mixed-pages-documents.tif - Page 1"));
      CHECK(MainWindowTestAccess::session_title(window, before + 3U) ==
            QStringLiteral("mixed-pages-documents.tif - Page 5"));
      CHECK(MainWindowTestAccess::active_session_title(window) ==
            QStringLiteral("mixed-pages-documents.tif - Page 1"));
      const auto& second = std::as_const(MainWindowTestAccess::session_document(window, before + 1U));
      CHECK(second.width() == 14 && second.height() == 5);
      CHECK(second.color_state().bit_depth == patchy::BitDepth::UInt16);
      const auto& third = std::as_const(MainWindowTestAccess::session_document(window, before + 2U));
      CHECK(third.color_state().bit_depth == patchy::BitDepth::Float32);
      CHECK(third.channels().size() == 4U);
    }
    const auto& first = std::as_const(MainWindowTestAccess::document(window));
    CHECK(first.width() == 8 && first.height() == 6);
    CHECK(MainWindowTestAccess::active_session_import_notes(window).join(QLatin1Char('\n')).contains(
        QStringLiteral("opened 4 pages as separate documents")));
  }
  {
    patchy::ui::MainWindow window;
    show_window(window);
    const auto before = MainWindowTestAccess::session_count(window);
    int asked = 0;
    {
      TiffPagesAnswer answer(QStringLiteral("tiffPagesCancelButton"), &asked);
      MainWindowTestAccess::open_document_path(window, path);
      QApplication::processEvents();
    }
    CHECK(asked == 1);
    CHECK(MainWindowTestAccess::session_count(window) == before);
  }
  {
    patchy::ui::MainWindow window;
    show_window(window);
    int asked = 0;
    {
      TiffPagesAnswer answer(QStringLiteral("tiffPagesCancelButton"), &asked);
      const auto source = QStringLiteral(R"JS(
        const d = app.open('%1');
        console.log('layers=' + d.layers.length + ' width=' + d.width);
      )JS")
                              .arg(path);
      CHECK(patchy::test::ui::scripting_support::run_script(window, source));
    }
    CHECK(asked == 0);
    CHECK(patchy::test::ui::scripting_support::backlog_contains(window, QStringLiteral("layers=1 width=8")));
  }
}

// GitHub issue 625's own samples (tlnagy/exampletiffs, copied into
// local-test-fixtures/tiff-pages, not committed): mri.tif's 27 palette PackBits pages and
// shapes_multi_color.tif's five pages of mixed types all open as layers.
void ui_multi_page_tiff_samples_open_as_layers_if_available() {
  const auto dir = patchy::test::local_fixtures_root() / "tiff-pages";
  patchy::test::skip_unless_exists(dir / "mri.tif");
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  SettingsValueRestorer target_setting(QStringLiteral("imports/tiffPagesTarget"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  DeepEditingOn deep;
  struct Sample {
    const char* file;
    std::size_t pages;
  };
  for (const auto& sample : {Sample{"mri.tif", 27U}, Sample{"shapes_multi_color.tif", 5U}}) {
    const auto path = patchy::ui::to_qstring(dir / sample.file);
    if (!QFileInfo::exists(path)) {
      continue;
    }
    patchy::ui::MainWindow window;
    window.resize(1280, 860);
    show_window(window);
    int asked = 0;
    {
      TiffPagesAnswer answer(QStringLiteral("tiffPagesLayersButton"), &asked);
      MainWindowTestAccess::open_document_path(window, path);
      QApplication::processEvents();
    }
    CHECK(asked == 1);
    const auto& document = std::as_const(MainWindowTestAccess::document(window));
    CHECK(document.layers().size() == sample.pages);
    const auto notes = MainWindowTestAccess::active_session_import_notes(window).join(QLatin1Char('\n'));
    CHECK(!notes.contains(QStringLiteral("left out")));
    std::cout << "[INFO] " << sample.file << ": " << document.width() << "x" << document.height() << ", "
              << document.layers().size() << " layers, " << static_cast<int>(document.color_state().bit_depth)
              << "-bit; notes: " << notes.toStdString() << "\n";
    if (sample.pages == 5U && document.layers().size() == 5U) {
      // Every page is the same picture in another encoding: the 12-bit page (4, which only
      // the fallback decoder reads) and the planar page (5) match page 1.
      const auto& page_one = document.layers()[4].pixels();
      for (const std::size_t index : {std::size_t{1}, std::size_t{0}}) {
        const auto& page = document.layers()[index].pixels();
        CHECK(page.width() == page_one.width() && page.height() == page_one.height());
        double total = 0.0;
        std::vector<float> a(static_cast<std::size_t>(page.width()) * 4U);
        std::vector<float> b(a.size());
        for (std::int32_t y = 0; y < page.height(); ++y) {
          patchy::load_rgba_row(page_one, y, 0, page.width(), patchy::DeepDomain::Encoded, a);
          patchy::load_rgba_row(page, y, 0, page.width(), patchy::DeepDomain::Encoded, b);
          for (std::size_t i = 0; i < a.size(); ++i) {
            total += std::fabs(a[i] - b[i]);
          }
        }
        const auto mean = total / (static_cast<double>(page.width()) * page.height() * 4.0);
        std::cout << "[INFO] page " << (5U - index) << " differs from page 1 by " << mean << " levels on average\n";
        CHECK(mean < 2.0);
      }
    }
    for (int step = 0; step < 10; ++step) {
      QApplication::processEvents();
    }
    window.grab().save(QDir(artifact_dir()).absoluteFilePath(QString::fromLatin1(sample.file) +
                                                              QStringLiteral("-as-layers.png")));
  }
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
  SettingsValueRestorer target_setting(QStringLiteral("imports/tiffPagesTarget"));
  const auto path = write_file(QStringLiteral("pages.tif"), fixture::tiff_file(true, 8, 6, 8, false, {}, 2, 3));
  patchy::ui::MainWindow window;
  show_window(window);
  int asked = 0;
  {
    TiffPagesAnswer answer(QStringLiteral("tiffPagesFirstButton"), &asked);
    MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
  }
  CHECK(asked == 1);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("imports/tiffPagesTarget")).toString() ==
        QStringLiteral("first"));
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(document.width() == 8 && document.height() == 6);
  CHECK(document.layers().size() == 1U);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("3 pages")));
}

// GitHub issue 623: 32-bit float RGB with four unspecified extra samples, which Qt's TIFF
// plug-in refuses, opens through psd::read_flat_tiff as a 32-bit document whose extras are
// alpha channels; page 2 adds the page note.
void ui_flat_tiff_qt_refuses_opens_through_the_fallback_decoder() {
  SettingsValueRestorer notes_setting(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  SettingsValueRestorer target_setting(QStringLiteral("imports/tiffPagesTarget"));
  namespace flat = patchy::test::flat_tiff;
  flat::Spec page;
  page.kind = flat::Kind::F32;
  page.samples = 7;
  page.extra = std::vector<std::uint16_t>{0, 0, 0, 0};
  page.width = 24;
  page.height = 12;
  page.value = [](std::uint32_t x, std::uint32_t, std::uint32_t sample) {
    return sample >= 3U ? 0.1 * (sample - 2U) : (sample == 0U && x < 12U ? 1.0 : 0.25);
  };
  flat::Spec second = page;
  second.kind = flat::Kind::U8;
  second.samples = 3;
  second.extra.clear();
  second.value = [](std::uint32_t, std::uint32_t, std::uint32_t) { return 9.0; };
  const auto path = write_file(QStringLiteral("float-extras.tif"), flat::write_tiff(std::vector<flat::Spec>{page, second}));
  CHECK(QImage(path).isNull());  // Qt alone cannot read it
  // With deep editing on the file keeps its depth; with it off (the suite's default, and
  // PATCHY_DEEP_EDITING=0) it opens at 8 bits like every other deep file.
  struct DeepEditingOverride {
    ~DeepEditingOverride() { patchy::set_deep_editing_override(std::nullopt); }
  } restore_deep_editing;
  for (const bool deep : {true, false}) {
    patchy::set_deep_editing_override(deep);
    patchy::ui::MainWindow window;
    show_window(window);
    int asked = 0;
    {
      TiffPagesAnswer answer(QStringLiteral("tiffPagesFirstButton"), &asked);
      MainWindowTestAccess::open_document_path(window, path);
      QApplication::processEvents();
    }
    CHECK(asked == 1);
    const auto& document = std::as_const(MainWindowTestAccess::document(window));
    CHECK(document.width() == 24 && document.height() == 12);
    CHECK(document.color_state().bit_depth == (deep ? patchy::BitDepth::Float32 : patchy::BitDepth::UInt8));
    CHECK(document.layers().size() == 1U);
    CHECK(!document.layers().empty() && document.layers().front().name() == "float-extras");
    CHECK(document.channels().size() == 4U);
    CHECK(document.file_info().source_format == "TIFF");
    CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("2 pages")));
  }
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
      {"ui_multi_page_tiff_opens_pages_as_layers", ui_multi_page_tiff_opens_pages_as_layers},
      {"ui_multi_page_tiff_separate_documents_cancel_and_scripts",
       ui_multi_page_tiff_separate_documents_cancel_and_scripts},
      {"ui_multi_page_tiff_samples_open_as_layers_if_available",
       ui_multi_page_tiff_samples_open_as_layers_if_available},
      {"ui_flat_tiff_qt_refuses_opens_through_the_fallback_decoder",
       ui_flat_tiff_qt_refuses_opens_through_the_fallback_decoder},
      {"ui_layered_tiff_save_keeps_layers_and_flat_saves_stay_flat",
       ui_layered_tiff_save_keeps_layers_and_flat_saves_stay_flat},
      {"ui_tiff_options_dialog_offers_keep_layers", ui_tiff_options_dialog_offers_keep_layers},
      {"ui_script_save_as_tiff_layers_option", ui_script_save_as_tiff_layers_option},
  };
}
