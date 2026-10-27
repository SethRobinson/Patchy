// GIMP .xcf files in the app (docs/xcf.md): opening by name, by GIMP's compressed names and
// by content, the layer tree and GIMP's modes in the Layers panel, editable text that keeps
// its place when Patchy redraws it, and Save going to Save As (the format is read-only).

#include "core/blend_math.hpp"
#include "core/layer_metadata.hpp"
#include "formats/metadata/metadata_edit.hpp"
#include "formats/xcf_document_io.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/panel_column.hpp"
#include "ui/qt_paths.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "scripting_test_support.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"
#include "xcf_test_builder.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
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
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(count_layers(document.layers()) >= 2U);
    if (path != bin) {
      // The layer GIMP had selected stays selected.
      const auto* active = document.active_layer_id().has_value()
                               ? std::as_const(document).find_layer(*document.active_layer_id())
                               : nullptr;
      CHECK(active != nullptr && active->name() == "big-tile.png");
    }
  }
  // A file opened by content says so (GitHub issue 362).
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("GIMP")));

  // A one-layer GIMP file keeps its layer's transparency: no mask is made from it, as
  // a flat PNG's would be.
  namespace builder = patchy::test::xcf;
  builder::Image single;
  single.width = 4;
  single.height = 4;
  builder::Layer layer;
  layer.name = "Only";
  layer.drawable = builder::solid(4, 4, {200, 40, 90, 128});
  layer.properties.push_back(builder::prop_offsets(0, 0));
  single.layers.push_back(std::move(layer));
  const auto bytes = builder::write(single);
  const auto single_path = temp.filePath(QStringLiteral("single.xcf"));
  QFile file(single_path);
  CHECK(file.open(QIODevice::WriteOnly));
  CHECK(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
        static_cast<qint64>(bytes.size()));
  file.close();
  patchy::ui::MainWindowTestAccess::open_document_path(window, single_path);
  QApplication::processEvents();
  const auto& single_document = patchy::ui::MainWindowTestAccess::document(window);
  CHECK(single_document.layers().size() == 1U);
  CHECK(!single_document.layers().front().mask().has_value());
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

// Scripts open GIMP files like the Open command, and read what the import approximated.
void ui_script_opens_xcf_files() {
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("modes.xcf"));
  CHECK(QFile::copy(committed_xcf_fixture("modes.xcf"), path));
  patchy::ui::MainWindow window;
  show_window(window);
  auto source = QStringLiteral(R"JS(
    var doc = app.open('%PATH%');
    if (doc.layers.length < 2) throw new Error('layers: ' + doc.layers.length);
    if (doc.importNotes.length < 1) throw new Error('no import notes');
  )JS");
  source.replace(QStringLiteral("%PATH%"), QDir::fromNativeSeparators(path));
  CHECK(patchy::test::ui::scripting_support::run_script(window, source));
}

// File Info shows what the XCF reader read from the parasites: the open path used to rescan
// the file for metadata and replace it (GitHub issue 450). Application is "GIMP" without XMP. With philipzae's samples present
// (local-test-fixtures/xcf-metadata) File Info's Description and IPTC pages are saved for
// the card.
void ui_xcf_metadata_reaches_file_info() {
  namespace builder = patchy::test::xcf;
  namespace md = patchy::metadata;
  const auto parasite = [](const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); };
  builder::Image image;
  image.version = 11;
  image.width = 4;
  image.height = 4;
  image.properties.push_back(builder::prop_parasites(
      {{"gimp-comment", parasite(std::string("Made in GIMP\0", 13))},
       {"gimp-image-metadata", parasite("<metadata><tag name=\"Xmp.dc.title\">lang=&quot;x-default&quot; GIMP title"
                                        "</tag><tag name=\"Exif.Image.Artist\">Ann</tag></metadata>")}}));
  builder::Layer layer;
  layer.name = "Only";
  layer.drawable = builder::solid(4, 4, {200, 40, 90, 255});
  layer.properties.push_back(builder::prop_offsets(0, 0));
  image.layers.push_back(std::move(layer));
  const auto bytes = builder::write(image);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("metadata.xcf"));
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  CHECK(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
        static_cast<qint64>(bytes.size()));
  file.close();

  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  {
    const auto& info = patchy::ui::MainWindowTestAccess::document(window).file_info();
    CHECK(info.source_format == "XCF" && info.source_file_size == bytes.size());
    CHECK(info.text.size() == 1 && info.text[0].value == "Made in GIMP");
    CHECK(md::read_fields(info).title == "GIMP title");
    CHECK(md::read_fields(info).authors == std::vector<std::string>({"Ann"}));
    CHECK(md::read_fields(info).creator_tool == "GIMP");
  }

  const auto samples = patchy::test::source_root_path() / "local-test-fixtures" / "xcf-metadata";
  for (const auto& [name, page] : {std::pair<const char*, const char*>{"description", "fileInfoDescriptionPage"},
                                   std::pair<const char*, const char*>{"iptc", "fileInfoIptcPage"}}) {
    const auto sample = samples / (std::string(name) + ".xcf");
    if (!std::filesystem::exists(sample)) {
      return;
    }
    patchy::ui::MainWindowTestAccess::open_document_path(window, QString::fromStdWString(sample.wstring()));
    QApplication::processEvents();
    bool captured = false;
    QString created;
    QString application;
    QTimer::singleShot(0, [&captured, &created, &application, name, page] {
      auto* dialog = patchy::test::ui::find_top_level_dialog(QStringLiteral("patchyFileInfoDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      dialog->resize(900, 640);
      auto* categories = dialog->findChild<QListWidget*>(QStringLiteral("fileInfoCategoryList"));
      CHECK(categories != nullptr);
      for (int row = 0; categories != nullptr && row < categories->count(); ++row) {
        if (categories->item(row)->data(Qt::UserRole).toString() == QLatin1String(page)) {
          categories->setCurrentRow(row);
        }
      }
      QApplication::processEvents();
      if (const auto* label = dialog->findChild<QLabel*>(QStringLiteral("fileInfoCreatedLabel"))) {
        created = label->text();
      }
      if (const auto* label = dialog->findChild<QLabel*>(QStringLiteral("fileInfoApplicationLabel"))) {
        application = label->text();
      }
      std::error_code ignored;
      std::filesystem::create_directories(std::filesystem::path("test-artifacts") / "xcf-metadata", ignored);
      patchy::test::ui::save_widget_artifact(std::string("xcf-metadata/file-info-") + name, *dialog);
      captured = true;
      dialog->reject();
    });
    patchy::test::ui::require_action(window, "fileInfoAction")->trigger();
    QApplication::processEvents();
    CHECK(captured);
    // Created carries OffsetTimeOriginal, as Modified carries its zone (issue 450).
    CHECK(created == QStringLiteral("2026-10-26T12:56:05+03:00"));
    // Application names GIMP from the XMP history's softwareAgent (issue 450).
    CHECK(application == QStringLiteral("GIMP 3.2.6 (Linux)"));
  }
}

// An indexed GIMP file opens like an indexed PNG: the Indexed Image offer, then Palette mode
// with the colormap in file order (GitHub issue 452; docs/palette-mode.md, "I/O").
void ui_xcf_indexed_open_offers_palette_mode() {
  using patchy::test::ui::SettingsValueRestorer;
  namespace builder = patchy::test::xcf;
  SettingsValueRestorer policy_restorer(QStringLiteral("imports/adoptIndexedPalette"));
  const auto set_policy = [](const char* policy) {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/adoptIndexedPalette"), QString::fromLatin1(policy));
    settings.sync();
  };
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto write_xcf = [&temp](const builder::Image& image, const QString& name) {
    const auto bytes = builder::write(image);
    const auto path = temp.filePath(name);
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
          static_cast<qint64>(bytes.size()));
    return path;
  };
  const std::vector<patchy::RgbColor> colormap{{200, 100, 50}, {10, 20, 30}, {0, 255, 0}};
  builder::Image indexed;
  indexed.base_type = 2;
  indexed.width = 4;
  indexed.height = 4;
  builder::Bytes colormap_bytes;
  colormap_bytes.u32(static_cast<std::uint32_t>(colormap.size()));
  for (const auto& color : colormap) {
    colormap_bytes.u8(color.red);
    colormap_bytes.u8(color.green);
    colormap_bytes.u8(color.blue);
  }
  indexed.properties.push_back(builder::Property{builder::kColormap, colormap_bytes.data, 4U + 9U});
  builder::Layer indexed_layer;
  indexed_layer.type = 4;
  indexed_layer.drawable = builder::solid(4, 4, {2});
  indexed_layer.drawable.pixels[1] = 1;
  indexed.layers.push_back(std::move(indexed_layer));
  const auto indexed_path = write_xcf(indexed, QStringLiteral("indexed.xcf"));
  builder::Image rgb;
  rgb.width = 4;
  rgb.height = 4;
  builder::Layer rgb_layer;
  rgb_layer.drawable = builder::solid(4, 4, {200, 40, 90, 255});
  rgb.layers.push_back(std::move(rgb_layer));
  const auto rgb_path = write_xcf(rgb, QStringLiteral("rgb.xcf"));

  patchy::ui::MainWindow window;
  show_window(window);
  // Answers the offer when it shows (the open runs its own event loops first, so poll for
  // the box); `offered` says whether it did. Each open stops the poll afterwards.
  bool offered = false;
  bool use_palette = true;
  QString offer_text;
  QTimer offer_poll;
  offer_poll.setInterval(20);
  QObject::connect(&offer_poll, &QTimer::timeout, [&offered, &offer_text, &use_palette, &offer_poll] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box == nullptr || !box->isVisible() || box->objectName() != QLatin1String("adoptIndexedPaletteMessageBox")) {
        continue;
      }
      offered = true;
      offer_text = box->text();
      offer_poll.stop();
      for (auto* button : box->buttons()) {
        if (box->buttonRole(button) == (use_palette ? QMessageBox::AcceptRole : QMessageBox::RejectRole)) {
          button->click();
          return;
        }
      }
    }
  });
  const auto answer_offer = [&offered, &offer_text, &use_palette, &offer_poll](bool use) {
    offered = false;
    offer_text.clear();
    use_palette = use;
    offer_poll.start();
  };

  set_policy("ask");
  answer_offer(true);
  patchy::ui::MainWindowTestAccess::open_document_path(window, indexed_path);
  QApplication::processEvents();
  offer_poll.stop();
  CHECK(offered && offer_text.contains(QStringLiteral("3-color")));
  {
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(document.palette_editing().has_value() && document.palette_editing()->palette.colors == colormap);
    const auto& layer = document.layers().front();
    const auto first = patchy::display_rgba8_at(std::as_const(layer).pixels(), 0, 0);
    const auto second = patchy::display_rgba8_at(std::as_const(layer).pixels(), 1, 0);
    CHECK(first[0] == 0 && first[1] == 255 && first[2] == 0);
    CHECK(second[0] == 10 && second[1] == 20 && second[2] == 30);
    auto* chip = window.findChild<QToolButton*>(QStringLiteral("paletteModeChip"));
    CHECK(chip != nullptr && !chip->isHidden());
  }

  // Edit as RGB keeps the colormap attached without the constraint.
  answer_offer(false);
  patchy::ui::MainWindowTestAccess::open_document_path(window, indexed_path);
  QApplication::processEvents();
  offer_poll.stop();
  CHECK(offered);
  {
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(!document.palette_editing().has_value());
    CHECK(document.indexed_palette().has_value() && document.indexed_palette()->colors == colormap);
  }

  // An RGB GIMP file has no palette and no offer.
  answer_offer(true);
  patchy::ui::MainWindowTestAccess::open_document_path(window, rgb_path);
  QApplication::processEvents();
  offer_poll.stop();
  CHECK(!offered);
  {
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(!document.palette_editing().has_value() && !document.indexed_palette().has_value());
  }

  // philipzae's samples (local-test-fixtures/xcf-indexed) open in Palette mode with "always".
  const auto samples = patchy::test::source_root_path() / "local-test-fixtures" / "xcf-indexed";
  if (!std::filesystem::exists(samples / "README.txt")) {
    return;
  }
  set_policy("always");
  window.resize(1280, 800);
  for (const auto& [name, count] : {std::pair<const char*, std::size_t>{"palette-1bit", 2},
                                    std::pair<const char*, std::size_t>{"palette-4bit", 7},
                                    std::pair<const char*, std::size_t>{"palette-8bit", 228}}) {
    const auto sample = samples / (std::string(name) + ".xcf");
    patchy::ui::MainWindowTestAccess::open_document_path(window, QString::fromStdWString(sample.wstring()));
    QApplication::processEvents();
    const auto& document = patchy::ui::MainWindowTestAccess::document(window);
    CHECK(document.palette_editing().has_value() && document.palette_editing()->palette.colors.size() == count);
    CHECK(document.indexed_palette().has_value() && document.palette_editing().has_value() &&
          document.palette_editing()->palette.colors == document.indexed_palette()->colors);
    if (std::string(name) == "palette-4bit") {
      // For the card: the 4-bit sample in Palette mode, its colormap in the Palette panel.
      if (auto* column = patchy::ui::MainWindowTestAccess::panel_column(window); column != nullptr) {
        column->bring_to_front(QStringLiteral("paletteDock"));
      }
      patchy::ui::MainWindowTestAccess::canvas(window)->set_zoom_centered(8.0);
      window.statusBar()->clearMessage();
      QApplication::processEvents();
      patchy::test::ui::save_widget_artifact("xcf_indexed_palette_mode", window);
    }
  }
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
      {"ui_script_opens_xcf_files", ui_script_opens_xcf_files},
      {"ui_xcf_review_capture", ui_xcf_review_capture},
      {"ui_xcf_metadata_reaches_file_info", ui_xcf_metadata_reaches_file_info},
      {"ui_xcf_indexed_open_offers_palette_mode", ui_xcf_indexed_open_offers_palette_mode},
  };
}
