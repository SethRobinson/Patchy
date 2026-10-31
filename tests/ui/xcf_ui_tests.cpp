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
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
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

using patchy::test::ui::SettingsValueRestorer;
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

[[nodiscard]] std::string layer_metadata_of(const patchy::Layer& layer, const char* key) {
  const auto found = layer.metadata().find(key);
  return found == layer.metadata().end() ? std::string() : found->second;
}

// GitHub issues 508 and 509: an outlined GIMP text layer opens as text that Patchy has redrawn,
// with the outline as a Stroke style (no fill for GIMP's "stroke only"), and an edit keeps the
// stroke, the underline and the span colors.
void ui_xcf_outlined_text_opens_editable_with_a_stroke() {
  namespace builder = patchy::test::xcf;
  builder::Image image;
  image.width = 360;
  image.height = 160;
  const auto text_layer = [](const std::string& name, const std::string& parasite, std::int32_t y) {
    builder::Layer layer;
    layer.name = name;
    layer.drawable = builder::solid(60, 30, {10, 20, 30, 255});
    layer.properties.push_back(builder::prop_offsets(20, y));
    layer.properties.push_back(builder::prop_u32(builder::kTextLayerFlags, 1));
    layer.properties.push_back(builder::prop_parasites({builder::text_parasite(parasite)}));
    return layer;
  };
  image.layers.push_back(text_layer(
      "Outlined",
      "(text \"Outlined\")\n(font \"Sans-serif Bold\")\n(font-size 40)\n(font-size-unit pixels)\n"
      "(color (color-rgb 0 0 1))\n(outline stroke-only)\n(outline-direction outer)\n(outline-width 3)\n"
      "(outline-unit pixels)\n(outline-foreground (color-rgb 1 0 0))\n(box-mode dynamic)\n",
      10));
  image.layers.push_back(text_layer(
      "Underlined",
      "(markup \"<markup><u>Under</u> <span foreground=\\\"#00ff00\\\">green</span></markup>\")\n"
      "(font \"Sans-serif\")\n(font-size 30)\n(font-size-unit pixels)\n(color (color-rgb 0.5 0 0))\n"
      "(box-mode dynamic)\n",
      90));
  const auto bytes = builder::write(image);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = temp.filePath(QStringLiteral("outlined.xcf"));
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  CHECK(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
        static_cast<qint64>(bytes.size()));
  file.close();

  patchy::ui::MainWindow window;
  show_window(window);
  window.set_cli_automation_mode(true);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* outlined = find_named(document.layers(), "Outlined");
  CHECK(outlined != nullptr && patchy::layer_is_text(*outlined));
  if (outlined == nullptr) {
    return;
  }
  const auto outlined_id = outlined->id();
  const auto* underlined_before = find_named(document.layers(), "Underlined");
  CHECK(underlined_before != nullptr);
  if (underlined_before == nullptr) {
    return;
  }
  const auto underlined_id = underlined_before->id();
  CHECK(!outlined->metadata().contains(patchy::kLayerMetadataXcfPendingOutline));
  CHECK(layer_metadata_of(*outlined, patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  CHECK(outlined->layer_style().strokes.size() == 1);
  if (outlined->layer_style().strokes.size() == 1) {
    const auto& stroke = outlined->layer_style().strokes.front();
    CHECK(stroke.enabled && stroke.position == patchy::LayerStrokePosition::Outside);
    CHECK(std::abs(stroke.size - 3.0F) < 0.01F && (stroke.color == patchy::RgbColor{255, 0, 0}));
  }
  CHECK(outlined->fill_opacity() == 0.0F);
  // Patchy's redraw sits where GIMP's text box starts: the glyph ink is near the layer origin.
  const auto before = outlined->bounds();
  CHECK(std::abs(before.x - 20) <= 6 && std::abs(before.y - 10) <= 10);
  CHECK(document.find_layer(outlined_id) != nullptr);

  const int mutated = patchy::ui::MainWindowTestAccess::cli_append_text_to_text_layers(window, QStringLiteral("!"));
  CHECK(mutated >= 2);
  const auto* edited = document.find_layer(outlined_id);
  CHECK(edited != nullptr && layer_metadata_of(*edited, patchy::kLayerMetadataText) == "Outlined!");
  if (edited != nullptr) {
    CHECK(edited->layer_style().strokes.size() == 1 && edited->fill_opacity() == 0.0F);
    // The redraw at open and the edit's redraw agree: the text does not jump.
    CHECK(std::abs(edited->bounds().x - before.x) <= 2 && std::abs(edited->bounds().y - before.y) <= 2);
  }
  const auto* underlined = document.find_layer(underlined_id);
  CHECK(underlined != nullptr);
  if (underlined != nullptr) {
    CHECK(layer_metadata_of(*underlined, patchy::kLayerMetadataText) == "Under green!");
    const auto runs = layer_metadata_of(*underlined, patchy::kLayerMetadataTextRuns);
    CHECK(runs.find("underline") != std::string::npos);
    CHECK(runs.find("#00ff00") != std::string::npos && runs.find("#800000") != std::string::npos);
  }
}

// For the cards (GitHub issues 508 and 509): philipzae's files as Patchy opens them, then after
// an edit (local-test-fixtures/xcf-issue-508).
void ui_xcf_issue_508_review_capture() {
  const auto samples = patchy::test::source_root_path() / "local-test-fixtures" / "xcf-issue-508";
  if (!std::filesystem::exists(samples / "README.txt")) {
    return;
  }
  for (const char* stem : {"text-editable", "text-turned-pixels"}) {
    patchy::ui::MainWindow window;
    show_window(window);
    window.resize(1280, 800);
    window.set_cli_automation_mode(true);
    patchy::ui::MainWindowTestAccess::open_document_path(
        window, QString::fromStdWString((samples / (std::string(stem) + ".xcf")).wstring()));
    QApplication::processEvents();
    patchy::ui::MainWindowTestAccess::canvas(window)->set_zoom_centered(1.0);
    window.statusBar()->clearMessage();
    QApplication::processEvents();
    patchy::test::ui::save_widget_artifact(std::string("xcf508_") + stem + "_open", window);
    (void)patchy::ui::MainWindowTestAccess::cli_append_text_to_text_layers(window, QStringLiteral(" edit"));
    window.statusBar()->clearMessage();
    QApplication::processEvents();
    patchy::test::ui::save_widget_artifact(std::string("xcf508_") + stem + "_edited", window);
  }
}

// Writes GIMP file bytes into `dir` and returns the path.
QString write_xcf_file(const QTemporaryDir& dir, const char* name, const std::vector<std::uint8_t>& bytes) {
  const auto path = dir.filePath(QString::fromLatin1(name));
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  CHECK(file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
        static_cast<qint64>(bytes.size()));
  file.close();
  return path;
}

std::size_t stack_index(const std::vector<patchy::Layer>& layers, patchy::LayerId id) {
  const auto found =
      std::find_if(layers.begin(), layers.end(), [id](const patchy::Layer& layer) { return layer.id() == id; });
  return static_cast<std::size_t>(found - layers.begin());
}

// GitHub issue 551: a GIMP file with two painted-on text layers ("Painted" at half opacity and
// "Locked" with GIMP's content lock) over a plain pixel layer.
QString write_painted_text_xcf(const QTemporaryDir& dir) {
  namespace builder = patchy::test::xcf;
  builder::Image image;
  image.width = 240;
  image.height = 120;
  builder::Layer plain;
  plain.name = "Plain";
  plain.drawable = builder::solid(240, 120, {255, 255, 255, 255});
  plain.properties.push_back(builder::prop_offsets(0, 0));
  const auto painted_text = [](const std::string& name, std::int32_t y) {
    builder::Layer layer;
    layer.name = name;
    layer.type = 1;
    layer.drawable = builder::solid(80, 30, {255, 0, 0, 255});
    layer.properties.push_back(builder::prop_offsets(20, y));
    layer.properties.push_back(builder::prop_u32(builder::kTextLayerFlags, 3));
    layer.properties.push_back(builder::prop_parasites({builder::text_parasite(
        "(text \"Hello\")\n(font \"Sans-serif\")\n(font-size 24)\n(font-size-unit pixels)\n"
        "(color (color-rgb 0 0 1))\n(box-mode dynamic)\n")}));
    return layer;
  };
  auto painted = painted_text("Painted", 10);
  painted.properties.push_back(builder::prop_f32(builder::kFloatOpacity, 0.5F));
  auto locked = painted_text("Locked", 70);
  locked.properties.push_back(builder::prop_u32(builder::kLockContent, 1));
  // GIMP lists the top layer first.
  image.layers.push_back(locked);
  image.layers.push_back(painted);
  image.layers.push_back(plain);
  return write_xcf_file(dir, "painted-text.xcf", builder::write(image));
}

// Opens `path` and, when the painted-on text question comes up, saves it as an artifact
// (`capture`), ticks Don't ask again (`dont_ask`) and clicks the button named `button`.
// Returns whether the question came up.
bool open_answering_painted_text_question(patchy::ui::MainWindow& window, const QString& path, const char* button,
                                          bool dont_ask = false, const std::string& capture = {}) {
  bool saw_dialog = false;
  int attempts = 0;
  QTimer poller;
  QObject::connect(&poller, &QTimer::timeout, [&] {
    if (++attempts > 500) {
      poller.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* box = qobject_cast<QMessageBox*>(widget);
      if (box == nullptr || box->objectName() != QStringLiteral("xcfPaintedTextMessageBox") || !box->isVisible()) {
        continue;
      }
      saw_dialog = true;
      poller.stop();
      if (!capture.empty()) {
        patchy::test::ui::save_widget_artifact(capture, *box);
      }
      if (auto* check = box->findChild<QCheckBox*>(QStringLiteral("xcfPaintedTextDontAskCheck")); check != nullptr) {
        check->setChecked(dont_ask);
      }
      auto* chosen = box->findChild<QPushButton*>(QString::fromLatin1(button));
      CHECK(chosen != nullptr);
      if (chosen != nullptr) {
        chosen->click();
      } else {
        box->reject();
      }
      return;
    }
  });
  poller.start(20);
  patchy::ui::MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  poller.stop();
  return saw_dialog;
}

// The import notes ride the status bar after an open (the first note, then a count).
bool notes_contain(patchy::ui::MainWindow& window, const char* words) {
  return window.statusBar()->currentMessage().contains(QString::fromLatin1(words));
}

// The painted-on text question's setting, cleared or set for one test, with the Missing Fonts
// dialog off so a converted layer's font cannot raise it.
struct PaintedTextSettings {
  SettingsValueRestorer policy{QStringLiteral("imports/xcfPaintedTextLayers")};
  SettingsValueRestorer missing_fonts{QStringLiteral("imports/showMissingFontsDialog")};
  explicit PaintedTextSettings(const char* value = nullptr) {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("imports/showMissingFontsDialog"), false);
    if (value != nullptr) {
      settings.setValue(QStringLiteral("imports/xcfPaintedTextLayers"), QString::fromLatin1(value));
    } else {
      settings.remove(QStringLiteral("imports/xcfPaintedTextLayers"));
    }
  }
};

// "Keep as Pixel Layers" (the default button) leaves GIMP's pixels, the document unmodified
// and the reader's notice in place.
void ui_xcf_painted_text_question_keeps_pixels() {
  const PaintedTextSettings settings;
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = write_painted_text_xcf(temp);
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(open_answering_painted_text_question(window, path, "xcfPaintedTextKeepButton"));
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* painted = find_named(document.layers(), "Painted");
  CHECK(painted != nullptr && !patchy::layer_is_text(*painted));
  CHECK(count_layers(document.layers()) == 3);
  CHECK(!patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(notes_contain(window, "opened as pixel layers"));
  CHECK(!patchy::ui::app_settings().contains(QStringLiteral("imports/xcfPaintedTextLayers")));
}

// "Convert to Text Layers" replaces both painted-on layers with GIMP's text (the content lock
// does not stop it), keeping name, place, opacity, blend mode and visibility, as one undo step
// that marks the document modified; Undo brings GIMP's pixels back.
void ui_xcf_painted_text_question_converts_to_text() {
  const PaintedTextSettings settings;
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = write_painted_text_xcf(temp);
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(open_answering_painted_text_question(window, path, "xcfPaintedTextConvertButton"));
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  CHECK(count_layers(document.layers()) == 3);
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(notes_contain(window, "became editable text"));
  const auto* painted = find_named(document.layers(), "Painted");
  const auto* locked = find_named(document.layers(), "Locked");
  CHECK(painted != nullptr && locked != nullptr);
  if (painted == nullptr || locked == nullptr) {
    return;
  }
  const auto painted_id = painted->id();
  for (const auto* layer : {painted, locked}) {
    CHECK(patchy::layer_is_text(*layer) && !patchy::layer_has_stored_xcf_text(*layer));
    CHECK(layer_metadata_of(*layer, patchy::kLayerMetadataText) == "Hello");
    CHECK(layer_metadata_of(*layer, patchy::kLayerMetadataTextRasterStatus) == "patchy_raster");
  }
  CHECK(std::abs(painted->opacity() - 0.5F) < 0.01F && painted->visible());
  CHECK(stack_index(document.layers(), painted_id) == 1);
  CHECK((locked->lock_flags() & patchy::kLayerLockImagePixels) != 0);
  // Patchy's glyphs in GIMP's blue replace the red paint, starting near GIMP's text box.
  CHECK(std::abs(painted->bounds().x - 20) <= 6 && std::abs(painted->bounds().y - 10) <= 10);
  const auto& pixels = painted->pixels();
  bool inked = false;
  bool red = false;
  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = 0; x < pixels.width(); ++x) {
      const auto* p = pixels.pixel(x, y);
      inked = inked || (p[3] > 128 && p[2] > 128);
      red = red || (p[3] > 0 && p[0] > p[2]);
    }
  }
  CHECK(inked && !red);

  patchy::ui::MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  const auto* undone = document.find_layer(painted_id);
  CHECK(undone != nullptr && !patchy::layer_is_text(*undone));
  if (undone != nullptr) {
    CHECK(undone->bounds() == (patchy::Rect{20, 10, 80, 30}));
    CHECK(undone->pixels().pixel(0, 0)[0] == 255 && undone->pixels().pixel(0, 0)[2] == 0);
  }
  const auto* locked_undone = find_named(document.layers(), "Locked");
  CHECK(locked_undone != nullptr && !patchy::layer_is_text(*locked_undone));
  patchy::ui::MainWindowTestAccess::redo(window);
  QApplication::processEvents();
  const auto* redone = document.find_layer(painted_id);
  CHECK(redone != nullptr && patchy::layer_is_text(*redone));
}

// "Add Text-Layer Copies" with Don't ask again: each painted-on layer stays as GIMP's pixels
// with a hidden, unlocked text layer above it, and the choice is saved, so the next open
// copies again without asking.
void ui_xcf_painted_text_question_adds_copies_and_remembers() {
  const PaintedTextSettings settings;
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = write_painted_text_xcf(temp);
  {
    patchy::ui::MainWindow window;
    show_window(window);
    CHECK(open_answering_painted_text_question(window, path, "xcfPaintedTextCopiesButton", true));
    const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(count_layers(document.layers()) == 5);
    CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
    CHECK(notes_contain(window, "copies"));
    const auto* painted = find_named(document.layers(), "Painted");
    const auto* copy = find_named(document.layers(), "Painted (text)");
    CHECK(painted != nullptr && copy != nullptr);
    if (painted != nullptr && copy != nullptr) {
      CHECK(!patchy::layer_is_text(*painted) && !patchy::layer_has_stored_xcf_text(*painted));
      CHECK(painted->pixels().pixel(0, 0)[0] == 255);
      CHECK(patchy::layer_is_text(*copy) && layer_metadata_of(*copy, patchy::kLayerMetadataText) == "Hello");
      CHECK(!copy->visible() && copy->lock_flags() == patchy::kLayerLockNone);
      CHECK(stack_index(document.layers(), copy->id()) == stack_index(document.layers(), painted->id()) + 1);
    }
    const auto* locked_copy = find_named(document.layers(), "Locked (text)");
    CHECK(locked_copy != nullptr && locked_copy->lock_flags() == patchy::kLayerLockNone);
    patchy::ui::MainWindowTestAccess::undo(window);
    QApplication::processEvents();
    CHECK(count_layers(document.layers()) == 3);
  }
  CHECK(patchy::ui::app_settings().value(QStringLiteral("imports/xcfPaintedTextLayers")).toString() ==
        QStringLiteral("copies"));
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(!open_answering_painted_text_question(window, path, "xcfPaintedTextKeepButton"));
  CHECK(count_layers(std::as_const(patchy::ui::MainWindowTestAccess::document(window)).layers()) == 5);
}

// A remembered "Convert to text layers" applies without a question and says so in the notes.
void ui_xcf_painted_text_preference_converts_without_asking() {
  const PaintedTextSettings settings("text");
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = write_painted_text_xcf(temp);
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(!open_answering_painted_text_question(window, path, "xcfPaintedTextKeepButton"));
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* painted = find_named(document.layers(), "Painted");
  CHECK(painted != nullptr && patchy::layer_is_text(*painted));
  CHECK(patchy::ui::MainWindowTestAccess::active_session_is_modified(window));
  CHECK(notes_contain(window, "became editable text"));
  CHECK(!notes_contain(window, "opened as pixel layers"));
}

// Scripted and headless opens never wait on the question: "Ask" keeps GIMP's pixels there, and a
// remembered choice still applies.
void ui_xcf_painted_text_headless_opens_never_ask() {
  const PaintedTextSettings settings;
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto path = write_painted_text_xcf(temp);
  for (const auto* setting : {"ask", "copies"}) {
    patchy::ui::app_settings().setValue(QStringLiteral("imports/xcfPaintedTextLayers"), QString::fromLatin1(setting));
    patchy::ui::MainWindow window;
    show_window(window);
    window.set_cli_automation_mode(true);
    CHECK(!open_answering_painted_text_question(window, path, "xcfPaintedTextConvertButton"));
    const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    const auto* painted = find_named(document.layers(), "Painted");
    CHECK(painted != nullptr && !patchy::layer_is_text(*painted));
    CHECK(count_layers(document.layers()) == (std::string(setting) == "ask" ? 3U : 5U));
  }
}

// For the #551 card: philipzae's file (local-test-fixtures/xcf-issue-547) asks about its
// painted-on "Lock all" text layer, and Convert to Text Layers makes it editable text.
void ui_xcf_issue_551_review_capture() {
  const auto sample = patchy::test::source_root_path() / "local-test-fixtures" / "xcf-issue-547" / "layer-switches.xcf";
  if (!std::filesystem::exists(sample)) {
    return;
  }
  const PaintedTextSettings settings;
  patchy::ui::MainWindow window;
  show_window(window);
  window.resize(1280, 800);
  CHECK(open_answering_painted_text_question(window, QString::fromStdWString(sample.wstring()),
                                             "xcfPaintedTextConvertButton", false, "xcf551_question"));
  patchy::ui::MainWindowTestAccess::canvas(window)->set_zoom_centered(1.0);
  QApplication::processEvents();
  const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
  const auto* layer = find_named(document.layers(), "Lock all");
  CHECK(layer != nullptr && patchy::layer_is_text(*layer));
  QApplication::processEvents();
  patchy::test::ui::save_widget_artifact("xcf551_converted", window);
}

}  // namespace

std::vector<patchy::test::TestCase> xcf_ui_tests() {
  return {
      {"ui_xcf_files_open_by_name_compressed_name_and_content", ui_xcf_files_open_by_name_compressed_name_and_content},
      {"ui_xcf_gimp_modes_show_in_the_layers_panel", ui_xcf_gimp_modes_show_in_the_layers_panel},
      {"ui_xcf_text_layers_stay_editable", ui_xcf_text_layers_stay_editable},
      {"ui_xcf_outlined_text_opens_editable_with_a_stroke", ui_xcf_outlined_text_opens_editable_with_a_stroke},
      {"ui_xcf_issue_508_review_capture", ui_xcf_issue_508_review_capture},
      {"ui_xcf_painted_text_question_keeps_pixels", ui_xcf_painted_text_question_keeps_pixels},
      {"ui_xcf_painted_text_question_converts_to_text", ui_xcf_painted_text_question_converts_to_text},
      {"ui_xcf_painted_text_question_adds_copies_and_remembers", ui_xcf_painted_text_question_adds_copies_and_remembers},
      {"ui_xcf_painted_text_preference_converts_without_asking", ui_xcf_painted_text_preference_converts_without_asking},
      {"ui_xcf_painted_text_headless_opens_never_ask", ui_xcf_painted_text_headless_opens_never_ask},
      {"ui_xcf_issue_551_review_capture", ui_xcf_issue_551_review_capture},
      {"ui_script_opens_xcf_files", ui_script_opens_xcf_files},
      {"ui_xcf_review_capture", ui_xcf_review_capture},
      {"ui_xcf_metadata_reaches_file_info", ui_xcf_metadata_reaches_file_info},
      {"ui_xcf_indexed_open_offers_palette_mode", ui_xcf_indexed_open_offers_palette_mode},
  };
}
