#include <QStatusBar>
#include <QListWidget>
#include "color/color_management.hpp"
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "core/pixel_depth.hpp"
#include "formats/avif_document_io.hpp"
#include "formats/jxl_document_io.hpp"
#include "formats/webp_animation_io.hpp"
#include "psd/psd_binary.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/qt_paths.hpp"
#include "local_psd_fixtures.hpp"
#include <QImageReader>
#include "ui/canvas_widget.hpp"
#include "ui/brush_tip_library.hpp"
#include "ui/app_settings.hpp"
#include "ui/font_face_name_index.hpp"
#include "ui/main_window.hpp"
#include "ui/script_editor_dialog.hpp"
#include <QFileInfo>
#include <QRawFont>
#include "ui/script_engine.hpp"
#include "ui/script_folders.hpp"
#include "ui/theme_manager.hpp"
#include "test_harness.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_support.hpp"
#include <QAction>
#include <QFontDatabase>
#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#if defined(PATCHY_HAVE_QT_PDF)
#include <QPdfDocument>
#endif
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <span>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "scripting_test_support.hpp"

namespace {
using namespace patchy::test::ui;
using namespace patchy::test::ui::scripting_support;

// Reads the fields of a DDS header this test cares about (docs/dds.md "Wire layout").
struct DdsHeaderProbe {
  QByteArray four_cc;
  quint32 mip_count{0};
  bool valid{false};
};

DdsHeaderProbe probe_dds_header(const QString& path);

QString write_user_script(const QString& relative_path, const QString& text);

DdsHeaderProbe probe_dds_header(const QString& path) {
  DdsHeaderProbe probe;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return probe;
  }
  const auto header = file.read(128);
  if (header.size() < 128 || !header.startsWith("DDS ")) {
    return probe;
  }
  const auto* bytes = reinterpret_cast<const unsigned char*>(header.constData());
  probe.mip_count = quint32(bytes[28]) | (quint32(bytes[29]) << 8) | (quint32(bytes[30]) << 16) |
                    (quint32(bytes[31]) << 24);
  probe.four_cc = header.mid(84, 4);
  probe.valid = true;
  return probe;
}

QString write_user_script(const QString& relative_path, const QString& text) {
  const auto path = QDir(patchy::ui::MainWindow::user_scripts_directory()).absoluteFilePath(relative_path);
  CHECK(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
  file.write(text.toUtf8());
  file.close();
  return path;
}

// A registered face is reachable by every name its own name table carries, not only by the
// family the platform database lists it under: the PostScript name and the full name, which no
// database lists as a family, both resolve to the face's family, and a text layer created with
// the PostScript name renders exactly like one created with the family (no substitution, no
// missing-font notice). Platform-agnostic: it reads the names out of the suite's own UiDefault
// face, so it exercises the index on Windows (where DirectWrite answers first), macOS and Linux.
void ui_text_name_table_names_resolve_to_the_registered_face() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  QString path;
  for (const auto& candidate : patchy::test::test_font_candidates(patchy::test::TestFontRole::UiDefault)) {
    if (QFileInfo::exists(candidate)) {
      path = candidate;
      break;
    }
  }
  if (path.isEmpty()) {
    patchy::test::skip("no UiDefault font file on this machine");
  }
  // Registering a file the role already registered adds nothing new; fonts are never removed.
  const int id = QFontDatabase::addApplicationFont(path);
  CHECK(id >= 0);
  const auto families = QFontDatabase::applicationFontFamilies(id);
  CHECK(!families.isEmpty());
  if (families.isEmpty()) {
    return;
  }
  const auto family = families.front();
  const auto styles = QFontDatabase::styles(family);
  const auto raw = QRawFont::fromFont(QFontDatabase::font(family, styles.isEmpty() ? QString() : styles.front(), 12));
  CHECK(raw.isValid());
  const auto names = patchy::ui::parse_opentype_face_names(raw.fontTable("name"));
  CHECK(names.has_value());
  if (!names.has_value()) {
    return;
  }
  std::cout << "[name-table] family '" << family.toStdString() << "' postscript '" << names->postscript_name.toStdString()
            << "' full '" << names->full_name.toStdString() << "' windows-family '" << names->family.toStdString() << "'\n";
  CHECK(!names->postscript_name.isEmpty());
  for (const auto& name : {names->postscript_name, names->full_name, names->family}) {
    if (name.isEmpty()) {
      continue;
    }
    const auto match = patchy::ui::font_face_for_name_table_name(name);
    CHECK(match.has_value());
    if (match.has_value()) {
      CHECK(match->family.compare(family, Qt::CaseInsensitive) == 0);
    }
  }
  CHECK(!patchy::ui::font_face_for_name_table_name(QStringLiteral("NoSuchFace-BoldCondensedXYZ")).has_value());

  patchy::ui::MainWindow window;
  show_window(window);
  const auto script = QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var byFamily = doc.addTextLayer('Diorama', {font: 'FAMILY_NAME', size: 40, x: 10, y: 40});
    var byPostScript = doc.addTextLayer('Diorama', {font: 'POSTSCRIPT_NAME', size: 40, x: 10, y: 140});
    var byCompact = doc.addTextLayer('Diorama', {font: 'COMPACT_NAME', size: 40, x: 10, y: 240});
    byCompact.text = byCompact.text;
    console.log('compact-name-matches=' + (byCompact.bounds.width === byFamily.bounds.width &&
                                          byCompact.bounds.height === byFamily.bounds.height));
    var same = function () {
      return byPostScript.bounds.width === byFamily.bounds.width && byPostScript.bounds.height === byFamily.bounds.height;
    };
    console.log('postscript-name-matches=' + same());
    byPostScript.text = byPostScript.text;
    console.log('reedit-keeps-face=' + same());
    console.log('stored=' + byPostScript.textFont);
  )JS")
                          .replace(QStringLiteral("FAMILY_NAME"), family)
                          .replace(QStringLiteral("COMPACT_NAME"), patchy::ui::compact_text_family_key(family))
                          .replace(QStringLiteral("POSTSCRIPT_NAME"), names->postscript_name);
  CHECK(run_script(window, script));
  CHECK(backlog_contains(window, QStringLiteral("compact-name-matches=true")));
  CHECK(backlog_contains(window, QStringLiteral("postscript-name-matches=true")));
  CHECK(backlog_contains(window, QStringLiteral("reedit-keeps-face=true")));
  // The stored name is the request, unless the database already lists the family under the
  // same compact key ("LiberationSans" is "Liberation Sans"), which canonicalizes to the family.
  CHECK(backlog_contains(window, QStringLiteral("stored=") + names->postscript_name) ||
        backlog_contains(window, QStringLiteral("stored=") + family));
  CHECK(!backlog_contains(window, QStringLiteral("font not available")));
  CHECK(!backlog_contains(window, QStringLiteral("font has no glyphs")));
}

// windows_named_font_data: a font whose Macintosh family differs from its Windows family loses
// its Macintosh name records (CoreText then lists the Windows names) and keeps every other
// table byte for byte; a font whose names agree, a collection and junk are left alone. The
// disagreeing case is the local Futura fixture (Macintosh "Futura", Windows "Futura BdCn BT");
// the suite's UiDefault files cover the agreeing case on every platform.
void ui_text_windows_named_font_data_drops_macintosh_records_if_available() {
  const auto read_all = [](const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  };
  const auto family_by_platform = [](const QByteArray& name_table, quint16 platform) {
    for (const auto& record : patchy::ui::opentype_name_records(name_table)) {
      if (record.platform == platform && record.name_id == 1) {
        return record.text;
      }
    }
    return QString();
  };
  CHECK(!patchy::ui::windows_named_font_data(QByteArray()).has_value());
  CHECK(!patchy::ui::windows_named_font_data(QByteArrayLiteral("ttcf\0\0\0\1\0\0\0\2")).has_value());
  CHECK(!patchy::ui::windows_named_font_data(QByteArray(64, 'x')).has_value());

  QStringList probed;
  for (const auto& candidate : patchy::test::test_font_candidates(patchy::test::TestFontRole::UiDefault)) {
    const auto bytes = read_all(candidate);
    const auto names = patchy::ui::opentype_table(bytes, "name");
    if (names.isEmpty()) {
      continue;
    }
    probed.append(candidate);
    const auto macintosh = family_by_platform(names, 1);
    const auto windows = family_by_platform(names, 3);
    const bool differ = !macintosh.isEmpty() && !windows.isEmpty() &&
                        patchy::ui::compact_text_family_key(macintosh) != patchy::ui::compact_text_family_key(windows);
    CHECK(patchy::ui::windows_named_font_data(bytes).has_value() == differ);
    if (probed.size() >= 2) {
      break;
    }
  }
  std::cout << "[windows-names] agreeing fonts probed: " << probed.size() << '\n';

  const auto futura = QStringLiteral(PATCHY_SOURCE_DIR) + QStringLiteral("/local-test-fixtures/fonts/FUTURABC.TTF");
  const auto original = read_all(futura);
  if (original.isEmpty()) {
    std::cout << "  note: optional fixture missing: local-test-fixtures/fonts/FUTURABC.TTF (Macintosh name records)\n";
    return;
  }
  const auto original_names = patchy::ui::opentype_table(original, "name");
  CHECK(family_by_platform(original_names, 1) == QStringLiteral("Futura"));
  CHECK(family_by_platform(original_names, 3) == QStringLiteral("Futura BdCn BT"));
  const auto renamed = patchy::ui::windows_named_font_data(original);
  CHECK(renamed.has_value());
  if (!renamed.has_value()) {
    return;
  }
  CHECK(renamed->size() == original.size());
  const auto renamed_names = patchy::ui::opentype_table(*renamed, "name");
  CHECK(!renamed_names.isEmpty() && renamed_names.size() < original_names.size());
  CHECK(family_by_platform(renamed_names, 1).isEmpty());
  CHECK(family_by_platform(renamed_names, 3) == QStringLiteral("Futura BdCn BT"));
  const auto parsed = patchy::ui::parse_opentype_face_names(renamed_names);
  CHECK(parsed.has_value());
  if (parsed.has_value()) {
    CHECK(parsed->family == QStringLiteral("Futura BdCn BT"));
    CHECK(parsed->subfamily == QStringLiteral("Bold"));
    CHECK(parsed->postscript_name == QStringLiteral("FuturaBT-BoldCondensed"));
    CHECK(parsed->full_name == QStringLiteral("Futura Bold Condensed BT"));
  }
  int macintosh_records = 0;
  int windows_records = 0;
  for (const auto& record : patchy::ui::opentype_name_records(renamed_names)) {
    macintosh_records += record.platform == 1 ? 1 : 0;
    windows_records += record.platform == 3 ? 1 : 0;
  }
  CHECK(macintosh_records == 0);
  CHECK(windows_records > 0);
  for (const char* tag : {"glyf", "loca", "cmap", "hmtx", "OS/2", "post"}) {
    CHECK(patchy::ui::opentype_table(*renamed, tag) == patchy::ui::opentype_table(original, tag));
  }
  // Already Windows-named: a second pass changes nothing.
  CHECK(!patchy::ui::windows_named_font_data(*renamed).has_value());
}

// Rich runs: one layer typed from an array of runs keeps every run's own face, size and color,
// reads them back in order, and a bold run really renders bolder than the plain layer.
void ui_script_text_runs_create_and_read_back() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var plain = doc.addTextLayer('Hold the LEFT TRIGGER', {font: 'Arial', size: 24, x: 10, y: 40});
    var rich = doc.addTextLayer([{text: 'Hold the '}, {text: 'LEFT TRIGGER', bold: true, color: '#ff0000'},
                                 {text: '\nsmall print', size: 12, italic: true}],
                                {font: 'Arial', size: 24, x: 10, y: 140});
    console.log('text=' + JSON.stringify(rich.text));
    // The paragraph break is its own run: Qt gives the block separator the format of the text
    // before it, so "\n" lands between the bold run and the italic one it was typed with.
    var runs = rich.textRuns;
    console.log('count=' + runs.length);
    console.log('run0=' + runs[0].text + '|' + runs[0].bold + '|' + runs[0].size + '|' + runs[0].color + '|' + runs[0].font);
    console.log('run1=' + runs[1].text + '|' + runs[1].bold + '|' + runs[1].size + '|' + runs[1].color);
    console.log('run2=' + JSON.stringify(runs[2].text));
    console.log('run3=' + runs[3].text + '|' + runs[3].italic + '|' + runs[3].size);
    console.log('joined=' + (runs.map(function (r) { return r.text; }).join('') === rich.text));
    console.log('font=' + rich.textFont);
    console.log('bold-wider=' + (rich.bounds.width > plain.bounds.width));
    console.log('two-lines=' + (rich.bounds.height > plain.bounds.height * 1.3));
    console.log('plain-runs=' + plain.textRuns.length + '|' + plain.textRuns[0].text);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("text=\"Hold the LEFT TRIGGER\\nsmall print\"")));
  CHECK(backlog_contains(window, QStringLiteral("count=4")));
  CHECK(backlog_contains(window, QStringLiteral("run0=Hold the |false|24|#000000|Arial")));
  CHECK(backlog_contains(window, QStringLiteral("run1=LEFT TRIGGER|true|24|#ff0000")));
  CHECK(backlog_contains(window, QStringLiteral("run2=\"\\n\"")));
  CHECK(backlog_contains(window, QStringLiteral("run3=small print|true|12")));
  CHECK(backlog_contains(window, QStringLiteral("joined=true")));
  CHECK(backlog_contains(window, QStringLiteral("font=Arial")));
  CHECK(backlog_contains(window, QStringLiteral("bold-wider=true")));
  CHECK(backlog_contains(window, QStringLiteral("two-lines=true")));
  CHECK(backlog_contains(window, QStringLiteral("plain-runs=1|Hold the LEFT TRIGGER")));
}

// A paragraph box wraps at its width and records its size; align sets every paragraph, and the
// textAlign setter re-aligns an existing layer.
void ui_script_text_box_wraps_and_aligns() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var words = 'The modified emulator renders the scene once per viewpoint from a slightly different camera.';
    var point = doc.addTextLayer(words, {font: 'Arial', size: 20, x: 10, y: 20});
    var box = doc.addTextLayer(words, {font: 'Arial', size: 20, x: 10, y: 120, box: {width: 300, height: 400}});
    console.log('point-box=' + JSON.stringify(point.textBox));
    console.log('box=' + JSON.stringify(box.textBox));
    console.log('wraps=' + (box.bounds.width <= 300 && box.bounds.height > point.bounds.height * 2));
    console.log('align-default=' + box.textAlign);
    var centered = doc.addTextLayer(words, {font: 'Arial', size: 20, x: 10, y: 560, box: {width: 300, height: 200}, align: 'center'});
    console.log('align-option=' + centered.textAlign);
    box.textAlign = 'right';
    console.log('align-set=' + box.textAlign);
    var threw = false;
    try { doc.addTextLayer('x', {box: {width: 4, height: 4}}); } catch (e) { threw = true; }
    console.log('tiny-box-throws=' + threw);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("point-box=null")));
  CHECK(backlog_contains(window, QStringLiteral("box={\"width\":300,\"height\":400}")));
  CHECK(backlog_contains(window, QStringLiteral("wraps=true")));
  CHECK(backlog_contains(window, QStringLiteral("align-default=left")));
  CHECK(backlog_contains(window, QStringLiteral("align-option=center")));
  CHECK(backlog_contains(window, QStringLiteral("align-set=right")));
  CHECK(backlog_contains(window, QStringLiteral("tiny-box-throws=true")));
}

// Baseline shifts move individual glyphs and scale with the text during Image Size.
void ui_text_baseline_shift_moves_individual_runs() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var shifted = app.activeDocument.addTextLayer('HH', {size: 48, x: 30, y: 80, color: '#000000'});
  )JS")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto id = document.active_layer_id();
  CHECK(id.has_value());
  if (!id) return;
  auto* layer = document.find_layer(*id);
  layer->metadata()[patchy::kLayerMetadataTextLayoutMode] = patchy::kTextLayoutModePhotoshop;
  layer->metadata()[patchy::kLayerMetadataTextRuns] =
      "v8\n0\t1\t48\t0\t0\t#000000\tArial\tauto\t0\t1\t1\t0\t\t0\t0\t0"
      "\n1\t1\t48\t0\t0\t#000000\tArial\tauto\t0\t1\t1\t0\t\t0\t0\t-8";
  CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.rerenderText();")));
  const auto baseline_difference = [](const patchy::Layer& text_layer) {
    const auto& pixels = text_layer.pixels();
    std::vector<int> ink_columns;
    for (int x = 0; x < pixels.width(); ++x) {
      for (int y = 0; y < pixels.height(); ++y) {
        if (pixels.pixel(x, y)[3] > 128) { ink_columns.push_back(x); break; }
      }
    }
    CHECK(!ink_columns.empty());
    if (ink_columns.empty()) return 0;
    const int middle = (ink_columns.front() + ink_columns.back()) / 2;
    int top_left = pixels.height(), top_right = pixels.height();
    for (int y = 0; y < pixels.height(); ++y) for (int x = 0; x < pixels.width(); ++x) {
      if (pixels.pixel(x, y)[3] > 128) {
        auto& top = x <= middle ? top_left : top_right;
        top = std::min(top, y);
      }
    }
    return top_right - top_left;
  };
  CHECK(baseline_difference(*std::as_const(document).find_layer(*id)) == 8);
  CHECK(std::as_const(document).find_layer(*id)->metadata().at(patchy::kLayerMetadataTextRuns).starts_with("v8\n"));
  CHECK(run_script(window, QStringLiteral(
      "var doc = app.activeDocument; doc.resizeImage(doc.width * 2, doc.height * 2);")));
  const auto& resized = patchy::ui::MainWindowTestAccess::document(window);
  const auto* resized_layer = resized.find_layer(*id);
  CHECK(resized_layer != nullptr);
  if (resized_layer) CHECK(baseline_difference(*resized_layer) == 16);
}

// setTextRuns retypes an existing layer with formatted runs on top of the first character's
// formatting (the family and size survive, the runs' own bold and color apply), and a plain
// `text` assignment afterwards keeps the first run's formatting as before.
void ui_script_set_text_runs_edits_existing_layer() {
  // The family must be installed: an edit session moves a layer whose family is missing onto
  // the substitute it draws with (substituted_text_family), and Linux has no Arial.
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  QString family;
  for (const auto* candidate : {"Arial", "Liberation Sans", "DejaVu Sans"}) {
    if (QFontDatabase::hasFamily(QString::fromLatin1(candidate))) {
      family = QString::fromLatin1(candidate);
      break;
    }
  }
  if (family.isEmpty()) {
    patchy::test::skip("no Arial-class family installed");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addTextLayer('Ask Seth for a game', {font: '__FAMILY__', size: 24, x: 10, y: 40, color: '#102030'});
    var plainWidth = layer.bounds.width;
    layer.setTextRuns([{text: 'Ask '}, {text: 'Seth', bold: true, color: '#ff0000'}, ' for a game']);
    var runs = layer.textRuns;
    console.log('text=' + layer.text);
    console.log('count=' + runs.length);
    console.log('kept=' + runs[0].font + '|' + runs[0].size + '|' + runs[0].color + '|' + runs[0].bold);
    console.log('bolded=' + runs[1].text + '|' + runs[1].bold + '|' + runs[1].color);
    console.log('wider=' + (layer.bounds.width > plainWidth));
    layer.text = 'Ask Seth for a game';
    console.log('back=' + layer.textRuns.length + '|' + layer.textRuns[0].bold);
    var threw = false;
    try { layer.setTextRuns([]); } catch (e) { threw = true; }
    console.log('empty-throws=' + threw);
  )JS").replace(QStringLiteral("__FAMILY__"), family)));
  CHECK(backlog_contains(window, QStringLiteral("text=Ask Seth for a game")));
  CHECK(backlog_contains(window, QStringLiteral("count=3")));
  CHECK(backlog_contains(window, QStringLiteral("kept=%1|24|#102030|false").arg(family)));
  CHECK(backlog_contains(window, QStringLiteral("bolded=Seth|true|#ff0000")));
  CHECK(backlog_contains(window, QStringLiteral("wider=true")));
  CHECK(backlog_contains(window, QStringLiteral("back=1|false")));
  CHECK(backlog_contains(window, QStringLiteral("empty-throws=true")));
}

// rerenderText lays a text layer out again from what it stores. The layer's pixels are wiped
// first, standing in for a raster a PSD carried: the call must bring the ink back without
// touching the text, its runs or its position, and must refuse a layer that is not text.
void ui_script_rerender_text_replaces_stored_pixels() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addTextLayer([{text: 'Ask '}, {text: 'Seth', bold: true, color: '#ff0000'}],
                                 {size: 24, x: 10, y: 40, color: '#102030'});
    var before = layer.bounds;
    var runsBefore = JSON.stringify(layer.textRuns);
    console.log('ink-before=' + (before.width > 0 && before.height > 0));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("ink-before=true")));
  // Wipe the raster behind the script's back, as if the file had brought other pixels.
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto layer_id = document.active_layer_id();
  CHECK(layer_id.has_value());
  if (!layer_id.has_value()) {
    return;
  }
  auto* layer = document.find_layer(*layer_id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  const auto bounds_before = std::as_const(*layer).bounds();
  {
    auto& pixels = layer->pixels();
    for (std::int32_t y = 0; y < pixels.height(); ++y) {
      auto row = pixels.row(y);
      std::fill(row.begin(), row.end(), std::uint8_t{0});
    }
  }
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.activeLayer;
    layer.rerenderText();
    console.log('text-kept=' + layer.text);
    console.log('runs-kept=' + (layer.textRuns.length === 2 && layer.textRuns[1].bold && layer.textRuns[1].color === '#ff0000'));
    var plain = doc.addLayer('plain');
    var threw = false;
    try { plain.rerenderText(); } catch (e) { threw = true; }
    console.log('plain-throws=' + threw);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("text-kept=Ask Seth")));
  CHECK(backlog_contains(window, QStringLiteral("runs-kept=true")));
  CHECK(backlog_contains(window, QStringLiteral("plain-throws=true")));
  const auto* after = std::as_const(document).find_layer(*layer_id);
  CHECK(after != nullptr);
  if (after == nullptr) {
    return;
  }
  // The ink is back, where it was.
  bool has_ink = false;
  const auto& pixels = after->pixels();
  for (std::int32_t y = 0; y < pixels.height() && !has_ink; ++y) {
    const auto row = pixels.row(y);
    has_ink = std::any_of(row.begin(), row.end(), [](std::uint8_t value) { return value != 0; });
  }
  CHECK(has_ink);
  const auto bounds_after = after->bounds();
  CHECK(std::abs(bounds_after.x - bounds_before.x) <= 1);
  CHECK(std::abs(bounds_after.y - bounds_before.y) <= 1);
  CHECK(std::abs(bounds_after.width - bounds_before.width) <= 2);
  CHECK(std::abs(bounds_after.height - bounds_before.height) <= 2);
}

// textParagraph reads the first paragraph's indents and spacing in document px, the addTextLayer
// `paragraph` option seeds them, and the setter merges only the fields it is given into every
// paragraph. Other layers read null; a non-number throws.
void ui_script_text_paragraph_reads_and_sets_metrics() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    // The values travel through the editor's zoom, so compare them rounded.
    function fmt(p) {
      if (p === null) { return 'null'; }
      return [p.firstLineIndent, p.startIndent, p.endIndent, p.spaceBefore, p.spaceAfter]
          .map(function (v) { return Math.round(v * 100) / 100; }).join(',');
    }
    var layer = doc.addTextLayer('First paragraph\nSecond paragraph',
                                 {font: 'Arial', size: 20, x: 10, y: 20, box: {width: 300, height: 200},
                                  paragraph: {firstLineIndent: 12, startIndent: 8, spaceAfter: 6}});
    console.log('set=' + fmt(layer.textParagraph));
    var narrow = layer.bounds.width;
    layer.textParagraph = {startIndent: 20};
    console.log('merged=' + fmt(layer.textParagraph));
    var plain = doc.addTextLayer('No indents', {font: 'Arial', size: 20, x: 10, y: 300});
    console.log('plain=' + fmt(plain.textParagraph));
    console.log('pixel=' + fmt(doc.addLayer('px').textParagraph));
    var threw = false;
    try { layer.textParagraph = {startIndent: 'wide'}; } catch (e) { threw = true; }
    console.log('bad-throws=' + threw);
    console.log('kept=' + fmt(layer.textParagraph));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("set=12,8,0,0,6")));
  CHECK(backlog_contains(window, QStringLiteral("merged=12,20,0,0,6")));
  CHECK(backlog_contains(window, QStringLiteral("plain=0,0,0,0,0")));
  CHECK(backlog_contains(window, QStringLiteral("pixel=null")));
  CHECK(backlog_contains(window, QStringLiteral("bad-throws=true")));
  CHECK(backlog_contains(window, QStringLiteral("kept=12,20,0,0,6")));
}

// The auto-leading fraction the PSD writer hands Photoshop is the measured line pitch over the
// largest run size ON THOSE LINES. It used to divide by the layer's largest run, so a layer whose
// 49 px lines were separated by a 155 px spacer paragraph wrote 0.35 and Photoshop stacked the
// 49 px lines 17 px apart (the September 2026 Steam Frame poster). The spacer layer must record
// the same fraction as the identical layer without the spacer.
void ui_script_text_auto_leading_ignores_spacer_paragraphs() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var plain = doc.addTextLayer('Play VR games, or regular\ngames on a virtual screen.\n\nStream from a PC: supported\ngames also run on the headset.',
                                 {font: 'Arial', size: 49, x: 10, y: 10, box: {width: 970, height: 600}});
    plain.name = 'Plain block';
    var spaced = doc.addTextLayer([{text: 'Play VR games, or regular\ngames on a virtual screen.\n'},
                                   {text: '\n', size: 155},
                                   {text: 'Stream from a PC: supported\ngames also run on the headset.'}],
                                  {font: 'Arial', size: 49, x: 10, y: 10, box: {width: 970, height: 600}});
    spaced.name = 'Spaced block';
    console.log('runs=' + spaced.textRuns.length);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("runs=")));
  auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const auto fraction_of = [&document](const char* name) -> std::optional<double> {
    for (const auto& layer : document.layers()) {
      if (layer.name() != name) {
        continue;
      }
      const auto found = layer.metadata().find(patchy::kLayerMetadataTextAutoLeading);
      if (found == layer.metadata().end()) {
        return std::nullopt;
      }
      return std::stod(found->second);
    }
    return std::nullopt;
  };
  const std::optional<double> without_spacer = fraction_of("Plain block");
  const std::optional<double> with_spacer = fraction_of("Spaced block");
  CHECK(without_spacer.has_value() && with_spacer.has_value());
  if (!without_spacer.has_value() || !with_spacer.has_value()) {
    return;
  }
  CHECK(*without_spacer > 0.9 && *without_spacer < 1.6);
  CHECK(std::abs(*with_spacer - *without_spacer) < 0.0005);
}

void ui_script_list_fonts_reports_registered_families() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto noto_path = QStringLiteral(PATCHY_SOURCE_DIR "/third_party/fonts/noto_naskh_arabic/NotoNaskhArabic-Bold.ttf");
  CHECK(QFontDatabase::addApplicationFont(noto_path) >= 0);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var fonts = app.listFonts();
    var noto = fonts.filter(function (f) { return f.family === 'Noto Naskh Arabic'; })[0];
    console.log('count-ok=' + (fonts.length >= 1));
    console.log('noto=' + (noto ? noto.styles.indexOf('Bold') >= 0 && noto.writingSystems.indexOf('Arabic') >= 0 : 'missing'));
    console.log('private=' + fonts.filter(function (f) { return f.family.charAt(0) === '.'; }).length);
    var sorted = fonts.map(function (f) { return f.family; });
    console.log('sorted=' + (JSON.stringify(sorted) === JSON.stringify(sorted.slice().sort(function (a, b) { return a.localeCompare(b); }))));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("count-ok=true")));
  CHECK(backlog_contains(window, QStringLiteral("noto=true")));
  CHECK(backlog_contains(window, QStringLiteral("private=0")));
}

// app.displayInfo() (ui/display_hdr_info.hpp): one entry per screen, primary first, every key
// typed. The offscreen platform cannot ask the system, so HDR stays unqueried there with a
// headroom of 1 and null nits; real HDR numbers are checked by hand on HDR hardware.
void ui_script_display_info_reports_screens() {
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var screens = app.displayInfo();
    console.log('count=' + screens.length);
    console.log('primary-first=' + (screens.length > 0 && screens[0].primary === true));
    console.log('primaries=' + screens.filter(function (s) { return s.primary; }).length);
    var typed = screens.every(function (s) {
      var nits = ['sdrWhiteNits', 'peakNits', 'fullFrameNits', 'minNits'].every(function (k) {
        return s[k] === null || typeof s[k] === 'number';
      });
      return typeof s.name === 'string' && ['x', 'y', 'width', 'height', 'devicePixelRatio', 'headroom',
          'potentialHeadroom'].every(function (k) { return typeof s[k] === 'number'; }) &&
          ['primary', 'hdrQueried', 'hdrEnabled', 'remoteSession'].every(function (k) {
            return typeof s[k] === 'boolean'; }) && nits && s.headroom >= 1 && s.potentialHeadroom >= 1 &&
          s.width > 0 && s.height > 0;
    });
    console.log('typed=' + typed);
    var s = screens[0];
    console.log('offscreen=' + [s.hdrQueried, s.hdrEnabled, s.headroom, s.peakNits, s.sdrWhiteNits].join(','));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("count=%1").arg(QGuiApplication::screens().size())));
  CHECK(backlog_contains(window, QStringLiteral("primary-first=true")));
  CHECK(backlog_contains(window, QStringLiteral("primaries=1")));
  CHECK(backlog_contains(window, QStringLiteral("typed=true")));
  if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
    CHECK(backlog_contains(window, QStringLiteral("offscreen=false,false,1,,")));
  }
}

// app.listFonts() reports what addTextLayer can resolve: a family registered in this process
// shows up with its face names and writing systems, and Qt's private families stay out.
// The scripted conversions go through the working CMYK space (docs/cmyk.md).
void ui_script_cmyk_conversions_use_working_space() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::formula());
  CHECK(run_script(window, QStringLiteral(R"JS(
    var profile = app.cmykProfile;
    console.log('profile=' + JSON.stringify(profile));
    var ink = app.colorToCmyk('#ff0000');
    console.log('ink=' + [ink.c, ink.m, ink.y, ink.k].join(','));
    console.log('named=' + JSON.stringify(app.colorToCmyk('black')));
    console.log('color=' + app.cmykToColor(50, 100, 100, 0));
    console.log('white=' + app.cmykToColor(0, 0, 0, 0));
    try { app.cmykToColor(0, 0, 0, 101); console.log('range=accepted'); } catch (e) { console.log('range=' + e.message); }
  )JS")));
  CHECK(backlog_contains(
      window, QStringLiteral("profile={\"name\":\"\",\"hasProfile\":false,\"intent\":\"relative-colorimetric\","
                             "\"blackPointCompensation\":true}")));
  CHECK(backlog_contains(window, QStringLiteral("ink=0,100,100,0")));
  CHECK(backlog_contains(window, QStringLiteral("named={\"c\":0,\"m\":0,\"y\":0,\"k\":100}")));
  CHECK(backlog_contains(window, QStringLiteral("color=#800000")));
  CHECK(backlog_contains(window, QStringLiteral("white=#ffffff")));
  CHECK(backlog_contains(window, QStringLiteral("range=CMYK values are percentages from 0 to 100")));
}

// Image > Mode > CMYK Proof (docs/cmyk.md): a per-document mode that proofs the canvas and
// makes saves to JPEG, TIFF and flattened PDF write CMYK; other formats stay RGB after a
// warning, and an explicit colorMode still wins.
void ui_cmyk_proof_mode_saves_cmyk() {
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] {
    patchy::set_working_cmyk_space(previous);
    patchy::ui::app_settings().remove(QStringLiteral("saveOptions/cmykProofSaveNotice"));
    patchy::ui::app_settings().remove(QStringLiteral("saveOptions/exportBlackInkOnlyJpeg"));
  });
  patchy::ui::MainWindow window;
  show_window(window);
  auto* cmyk_action = window.findChild<QAction*>(QStringLiteral("imageModeCmykProofAction"));
  auto* rgb_action = window.findChild<QAction*>(QStringLiteral("imageModeRgbAction"));
  CHECK(cmyk_action != nullptr && rgb_action != nullptr);
  QTemporaryDir directory;
  CHECK(directory.isValid());
  auto& host = window.script_engine_host();
  const auto wait_for_run = [&host] {
    QElapsedTimer timer;
    timer.start();
    while (host.run_active() && timer.elapsed() < 60000) {
      QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QApplication::processEvents();
  };

  // Without a profile the mode is refused, from the menu and from a script.
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::formula());
  window.add_document_session(patchy::Document(20, 20, patchy::PixelFormat::rgb8()), QStringLiteral("No profile"));
  QApplication::processEvents();
  cmyk_action->trigger();
  QApplication::processEvents();
  CHECK(!cmyk_action->isChecked());
  CHECK(rgb_action->isChecked());
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("needs a CMYK profile")));

  const auto space = patchy::CmykWorkingSpace::from_icc_profile(patchy::ui::bundled_cmyk_profile(),
                                                                patchy::RenderingIntent::RelativeColorimetric, true);
  patchy::set_working_cmyk_space(space);
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("cmyk-proof");
  options.args = QStringList{QStringLiteral("base=") + directory.path() + QStringLiteral("/proof")};
  options.unattended = true;
  const bool run = host.run_source(QStringLiteral(R"JS(
    var p = patchy.args.base;
    var indexed = app.newDocument(8, 8);
    indexed.setPalette(['#000000', '#ffffff']);
    try { indexed.cmykProof = true; console.log('indexed=accepted'); }
    catch (e) { console.log('indexed=refused'); }
    var d = app.newDocument(40, 30);
    d.activeLayer.fill('#00a0e0');
    console.log('before=' + d.cmykProof);
    d.cmykProof = true;
    console.log('after=' + d.cmykProof);
    if (!d.exportAs(p + '.jpg', {quality: 95})) throw Error('jpg');
    if (!d.exportAs(p + '.tif')) throw Error('tif');
    if (!d.exportAs(p + '.png')) throw Error('png');
    if (!d.exportAs(p + '-rgb.jpg', {colorMode: 'rgb'})) throw Error('explicit rgb');
    d.cmykProof = false;
    if (!d.exportAs(p + '-off.jpg')) throw Error('off');
    d.cmykProof = true;
  )JS"), options);
  if (!run) {
    for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  }
  CHECK(run);
  wait_for_run();
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("indexed=refused")));
  CHECK(backlog_contains(window, QStringLiteral("before=false")));
  CHECK(backlog_contains(window, QStringLiteral("after=true")));
  const auto base = directory.path() + QStringLiteral("/proof");
  const auto format_of = [](const QString& path) { return QImageReader(path).read().format(); };
  CHECK(format_of(base + QStringLiteral(".jpg")) == QImage::Format_CMYK8888);
  CHECK(format_of(base + QStringLiteral(".tif")) == QImage::Format_CMYK8888);
  CHECK(QFileInfo::exists(base + QStringLiteral(".png")));
  CHECK(format_of(base + QStringLiteral("-rgb.jpg")) != QImage::Format_CMYK8888);
  CHECK(format_of(base + QStringLiteral("-off.jpg")) != QImage::Format_CMYK8888);

  // The active document is in CMYK Proof: the Mode menu says so, its canvas simulates the
  // print, and the Channels panel lists the ink plates although View > Proof Colors is off.
  QApplication::processEvents();
  CHECK(cmyk_action->isChecked());
  CHECK(!rgb_action->isChecked());
  auto* canvas = require_canvas(window);
  CHECK(canvas->proof_display_transform() != nullptr && canvas->proof_display_transform()->simulates_colors());
  auto* proof_view = window.findChild<QAction*>(QStringLiteral("viewProofColorsAction"));
  CHECK(proof_view != nullptr && !proof_view->isChecked());
  auto* channels = window.findChild<QListWidget*>(QStringLiteral("channelList"));
  CHECK(channels != nullptr && channels->count() == 8);

  // An interactive save explains the conversion; its black-ink choice is used and remembered.
  const auto jpg_path = directory.filePath(QStringLiteral("interactive.jpg"));
  bool notice_seen = false;
  QTimer::singleShot(0, [&notice_seen] {
    auto* dialog = find_top_level_dialog(QStringLiteral("cmykProofSaveDialog"));
    CHECK(dialog != nullptr);
    auto* black_ink = dialog->findChild<QCheckBox*>(QStringLiteral("cmykProofSaveBlackInkOnlyCheck"));
    auto* dont_show = dialog->findChild<QCheckBox*>(QStringLiteral("cmykProofSaveDontShowCheck"));
    CHECK(black_ink != nullptr && dont_show != nullptr);
    black_ink->setChecked(true);
    dont_show->setChecked(true);
    notice_seen = true;
    qobject_cast<QDialog*>(dialog)->accept();
  });
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, jpg_path));
  CHECK(notice_seen);
  CHECK(format_of(jpg_path) == QImage::Format_CMYK8888);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("in CMYK")));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("saveOptions/exportBlackInkOnlyJpeg")).toBool());
  // "Don't show this again" holds: the next save asks nothing.
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, jpg_path));

  // A format without a CMYK form warns once per session, then saves RGB.
  const auto png_path = directory.filePath(QStringLiteral("interactive.png"));
  bool warning_seen = false;
  QTimer::singleShot(0, [&warning_seen] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("cmykProofRgbSaveMessageBox")));
    CHECK(box != nullptr);
    warning_seen = true;
    box->button(QMessageBox::Save)->click();
  });
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, png_path));
  CHECK(warning_seen);
  CHECK(QFileInfo::exists(png_path));
  CHECK(patchy::ui::MainWindowTestAccess::save_document_to_path(window, png_path));

  // RGB Color leaves the mode: the canvas returns to plain RGB and the plates go.
  rgb_action->trigger();
  QApplication::processEvents();
  CHECK(!cmyk_action->isChecked());
  CHECK(canvas->proof_display_transform() == nullptr);
  CHECK(channels->count() == 4);

  // Switching the profile to the formula turns the mode off.
  cmyk_action->trigger();
  QApplication::processEvents();
  CHECK(cmyk_action->isChecked());
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::formula());
  patchy::ui::MainWindowTestAccess::refresh_proof_display(window);
  QApplication::processEvents();
  CHECK(!cmyk_action->isChecked());
  CHECK(canvas->proof_display_transform() == nullptr);
}

// CMYK export for print (issue 90): JPEG, TIFF and flat PDF separated through the working
// profile, which each file embeds; other formats refuse the option.
// A CMYK JPEG or TIFF opens through its CMYK profile (not Qt's plain conversion) and in
// CMYK Proof mode, so saving it again without a colorMode writes CMYK (docs/cmyk.md).
void ui_cmyk_files_open_in_cmyk_proof() {
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::from_icc_profile(
      patchy::ui::bundled_cmyk_profile(), patchy::RenderingIntent::RelativeColorimetric, true));
  QTemporaryDir directory;
  CHECK(directory.isValid());
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("cmyk-open");
  options.args = QStringList{QStringLiteral("base=") + directory.path() + QStringLiteral("/print")};
  options.unattended = true;
  const bool run = host.run_source(QStringLiteral(R"JS(
    var p = patchy.args.base;
    var d = app.newDocument(24, 16);
    d.activeLayer.fill('#00a0e0');
    if (!d.exportAs(p + '.jpg', {colorMode: 'cmyk', quality: 100})) throw Error('jpg');
    if (!d.exportAs(p + '.tif', {colorMode: 'cmyk'})) throw Error('tif');
    if (!d.exportAs(p + '-rgb.jpg')) throw Error('rgb');
    ['.jpg', '.tif', '-rgb.jpg'].forEach(function (suffix) {
      var opened = app.open(p + suffix);
      var px = new Uint8Array(opened.activeLayer.getPixels().data);
      console.log(suffix + ' proof=' + opened.cmykProof + ' rgb=' + px[0] + ',' + px[1] + ',' + px[2]);
      if (suffix !== '-rgb.jpg' && !opened.saveAs(p + '-again' + suffix)) throw Error('again ' + suffix);
    });
  )JS"), options);
  if (!run) {
    for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  }
  CHECK(run);
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 60000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  QApplication::processEvents();
  CHECK(!host.last_run_had_error());
  // Photoshop converts #00a0e0's ISO Coated v2 separation back to 8,159,218 (relative
  // colorimetric with black point compensation); Little CMS lands on 11,159,218 (the
  // engine difference docs/cmyk.md records). Qt's profile-free conversion gives about
  // 64,204,255.
  const auto pixel_from_backlog = [&window](const QString& prefix) {
    for (const auto& line : window.script_engine_host().message_backlog()) {
      const auto at = line.indexOf(prefix);
      if (at < 0) {
        continue;
      }
      const auto values = line.mid(line.indexOf(QStringLiteral("rgb="), at) + 4).trimmed().split(QLatin1Char(','));
      if (values.size() == 3) {
        return QColor(values[0].toInt(), values[1].toInt(), values[2].toInt());
      }
    }
    return QColor();
  };
  for (const auto& suffix : {QStringLiteral(".jpg"), QStringLiteral(".tif")}) {
    CHECK(backlog_contains(window, suffix + QStringLiteral(" proof=true")));
    const auto color = pixel_from_backlog(suffix + QStringLiteral(" proof="));
    CHECK(color.isValid());
    const int tolerance = suffix == QStringLiteral(".jpg") ? 6 : 4;
    CHECK(std::abs(color.red() - 8) <= tolerance && std::abs(color.green() - 159) <= tolerance &&
          std::abs(color.blue() - 218) <= tolerance);
    CHECK(QImageReader(directory.path() + QStringLiteral("/print-again") + suffix).read().format() ==
          QImage::Format_CMYK8888);
  }
  CHECK(backlog_contains(window, QStringLiteral("-rgb.jpg proof=false")));
  // The active document (the RGB JPEG) is plain RGB; the Mode menu follows the active tab.
  auto* cmyk_action = window.findChild<QAction*>(QStringLiteral("imageModeCmykProofAction"));
  CHECK(cmyk_action != nullptr && !cmyk_action->isChecked());
}

// A flat 8-bit CMYK PSD as Photoshop saves a one-layer document (no layer records, the
// merged image only), every pixel 75/20/0/0 ink (the separation of #00a0e0), with
// `profile` as its ICC resource (GitHub issue 220).
void write_flat_cmyk_psd(const QString& path, std::span<const std::uint8_t> profile) {
  constexpr std::uint32_t width = 8;
  constexpr std::uint32_t height = 6;
  patchy::psd::BigEndianWriter resources;
  for (const char letter : {'8', 'B', 'I', 'M'}) resources.write_u8(static_cast<std::uint8_t>(letter));
  resources.write_u16(1039);
  resources.write_u16(0);  // empty Pascal name, padded to even
  resources.write_u32(static_cast<std::uint32_t>(profile.size()));
  resources.write_bytes(profile);
  if (profile.size() % 2U != 0U) resources.write_u8(0);
  patchy::psd::BigEndianWriter file;
  patchy::psd::write_header(file, patchy::psd::Header{false, 4, height, width, 8, 4});
  file.write_u32(0);
  file.write_u32(static_cast<std::uint32_t>(resources.bytes().size()));
  file.write_bytes(resources.bytes());
  file.write_u32(0);  // no layer and mask information
  file.write_u16(0);  // raw
  // PSD stores ink inverted: 255 is no ink.
  for (const int ink : {255 - 191, 255 - 51, 255, 255}) {
    for (std::uint32_t i = 0; i < width * height; ++i) file.write_u8(static_cast<std::uint8_t>(ink));
  }
  QFile out(path);
  CHECK(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
  const auto& bytes = file.bytes();
  CHECK(out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
        static_cast<qint64>(bytes.size()));
}

// The PSD header's color mode (3 RGB, 4 CMYK), or -1.
int psd_file_color_mode(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return -1;
  const auto header = file.read(26);
  if (header.size() < 26 || !header.startsWith("8BPS")) return -1;
  return (static_cast<unsigned char>(header[24]) << 8) | static_cast<unsigned char>(header[25]);
}

// Saves through the interactive path; true when CMYK Proof's "Save in RGB" question came
// up, answered with `answer`. `text` receives its wording.
bool save_asks_rgb_question(patchy::ui::MainWindow& window, const QString& path, QMessageBox::StandardButton answer,
                            QString* text = nullptr) {
  bool asked = false;
  QTimer::singleShot(0, [&asked, answer, text] {
    auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("cmykProofRgbSaveMessageBox")));
    if (box == nullptr) return;
    asked = true;
    if (text != nullptr) *text = box->text();
    box->button(answer)->click();
  });
  static_cast<void>(patchy::ui::MainWindowTestAccess::save_document_to_path(window, path));
  QApplication::processEvents();
  return asked;
}

// The Image > Mode menu as it stands, with the status bar's document info field under it.
QImage image_mode_capture(patchy::ui::MainWindow& window) {
  auto* menu = window.findChild<QMenu*>(QStringLiteral("imageModeMenu"));
  auto* info = window.findChild<QLabel*>(QStringLiteral("statusDocumentInfoLabel"));
  CHECK(menu != nullptr && info != nullptr);
  menu->popup(QPoint(0, 0));
  QApplication::processEvents();
  const auto menu_image = menu->grab().toImage();
  menu->hide();
  const auto info_image = info->grab().toImage();
  constexpr int gap = 12;
  QImage composed(std::max(menu_image.width(), info_image.width()) + 2 * gap,
                  menu_image.height() + info_image.height() + 3 * gap, QImage::Format_ARGB32);
  composed.fill(window.palette().color(QPalette::Window));
  QPainter painter(&composed);
  painter.drawImage(gap, gap, menu_image);
  painter.drawImage(gap, menu_image.height() + 2 * gap, info_image);
  painter.end();
  return composed;
}

// A CMYK PSD opens in Image > Mode > CMYK Proof like a CMYK JPEG or TIFF (GitHub issue 220).
// Saved unchanged or with edited pixels it stays CMYK with no question; once a layer uses a
// blend mode that would mix the inks differently, the save asks, naming that reason, and
// writes RGB.
void ui_cmyk_psd_opens_in_cmyk_proof() {
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::from_icc_profile(
      patchy::ui::bundled_cmyk_profile(), patchy::RenderingIntent::RelativeColorimetric, true));
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto source = directory.filePath(QStringLiteral("print.psd"));
  write_flat_cmyk_psd(source, patchy::ui::bundled_cmyk_profile());
  patchy::ui::MainWindowTestAccess::open_document_path(window, source);
  QApplication::processEvents();
  auto* cmyk_action = window.findChild<QAction*>(QStringLiteral("imageModeCmykProofAction"));
  auto* rgb_action = window.findChild<QAction*>(QStringLiteral("imageModeRgbAction"));
  CHECK(cmyk_action != nullptr && rgb_action != nullptr);
  CHECK(cmyk_action->isChecked());
  CHECK(!rgb_action->isChecked());
  auto* info = window.findChild<QLabel*>(QStringLiteral("statusDocumentInfoLabel"));
  CHECK(info != nullptr && info->text().contains(QStringLiteral("CMYK Proof")));
  // Converted through the file's own profile (PSD ink storage, 255 = no ink).
  {
    const auto& document = std::as_const(patchy::ui::MainWindowTestAccess::document(window));
    CHECK(document.layers().size() == 1);
    const auto transform = patchy::CmykToRgbTransform::from_icc_profile(patchy::ui::bundled_cmyk_profile());
    CHECK(transform.has_value());
    const std::array<std::uint8_t, 4> inks{255 - 191, 255 - 51, 255, 255};
    std::array<std::uint8_t, 3> expected{};
    transform->convert(inks.data(), expected.data(), 1);
    const auto* pixel = document.layers().front().pixels().pixel(2, 2);
    for (int c = 0; c < 3; ++c) CHECK(std::abs(int{pixel[c]} - int{expected[static_cast<std::size_t>(c)]}) <= 1);
  }

  // Unchanged: Save writes CMYK again and asks nothing.
  const auto unchanged = directory.filePath(QStringLiteral("unchanged.psd"));
  CHECK(!save_asks_rgb_question(window, unchanged, QMessageBox::Cancel));
  CHECK(psd_file_color_mode(unchanged) == 4);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("in CMYK")));

  // Edited pixels are separated through the file's profile: still CMYK, still no question.
  patchy::ui::MainWindowTestAccess::document(window).layers().front().pixels().pixel(0, 0)[0] ^= 0x40;
  patchy::ui::MainWindowTestAccess::canvas(window)->document_changed();
  const auto edited = directory.filePath(QStringLiteral("edited.psd"));
  CHECK(!save_asks_rgb_question(window, edited, QMessageBox::Cancel));
  CHECK(psd_file_color_mode(edited) == 4);
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("in CMYK")));

  // A Multiply layer needs RGB: the question says why, Cancel writes nothing, Save writes RGB.
  auto& host = window.script_engine_host();
  {
    patchy::ui::ScriptEngineHost::RunOptions text_options;
    text_options.name = QStringLiteral("cmyk-psd-multiply");
    text_options.unattended = true;
    CHECK(host.run_source(QStringLiteral("var l = app.activeDocument.addLayer('Ink'); l.fillRect(0, 0, 4, 4, "
                                         "'#ff0000'); l.blendMode = 'multiply';"),
                          text_options));
    QElapsedTimer text_timer;
    text_timer.start();
    while (host.run_active() && text_timer.elapsed() < 60000) {
      QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QApplication::processEvents();
    CHECK(!host.last_run_had_error());
  }
  const auto texted = directory.filePath(QStringLiteral("text.psd"));
  QString question;
  CHECK(save_asks_rgb_question(window, texted, QMessageBox::Cancel, &question));
  CHECK(question.contains(QStringLiteral("blend modes would mix the inks")));
  CHECK(!QFileInfo::exists(texted));
  CHECK(save_asks_rgb_question(window, texted, QMessageBox::Save));
  CHECK(psd_file_color_mode(texted) == 3);
  CHECK(!window.statusBar()->currentMessage().contains(QStringLiteral("in CMYK")));

  // Scripts see the mode too.
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("cmyk-psd-open");
  options.args = QStringList{QStringLiteral("path=") + source};
  options.unattended = true;
  CHECK(host.run_source(QStringLiteral(R"JS(
    console.log('psd proof=' + app.open(patchy.args.path).cmykProof);
  )JS"), options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 60000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  QApplication::processEvents();
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("psd proof=true")));
}

// A document made in Patchy saves a layered CMYK PSD and PSB in CMYK Proof (docs/cmyk.md):
// the interactive save asks nothing and says "in CMYK", the layers stay layers, scripted saves
// follow the mode, and leaving CMYK Proof saves RGB again.
void ui_cmyk_proof_saves_layered_psd_in_cmyk() {
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::from_icc_profile(
      patchy::ui::bundled_cmyk_profile(), patchy::RenderingIntent::RelativeColorimetric, true));
  QTemporaryDir directory;
  CHECK(directory.isValid());
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("cmyk-proof-psd");
  options.args = QStringList{QStringLiteral("base=") + directory.path() + QStringLiteral("/layers")};
  options.unattended = true;
  CHECK(host.run_source(QStringLiteral(R"JS(
    var d = app.newDocument(40, 30);
    d.activeLayer.fill('#00a0e0');
    var top = d.addLayer('Top');
    top.fillRect(4, 4, 12, 10, '#e02060');
    d.cmykProof = true;
    if (!d.exportAs(patchy.args.base + '-script.psd')) throw Error('psd');
  )JS"), options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 60000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  QApplication::processEvents();
  CHECK(!host.last_run_had_error());
  const auto base = directory.path() + QStringLiteral("/layers");
  CHECK(psd_file_color_mode(base + QStringLiteral("-script.psd")) == 4);
  for (const auto* suffix : {".psd", ".psb"}) {
    const auto path = base + QString::fromLatin1(suffix);
    CHECK(!save_asks_rgb_question(window, path, QMessageBox::Cancel));
    CHECK(psd_file_color_mode(path) == 4);
    CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("in CMYK")));
    const auto reopened = patchy::psd::DocumentIo::read_file(patchy::ui::to_filesystem_path(path));
    CHECK(reopened.layers().size() == 2);
    CHECK(reopened.layers()[1].name() == "Top");
    CHECK(reopened.layers()[1].psd_native_colors() != nullptr);
  }
  // RGB Color leaves the mode: the next PSD save is RGB.
  auto* rgb_action = window.findChild<QAction*>(QStringLiteral("imageModeRgbAction"));
  CHECK(rgb_action != nullptr);
  rgb_action->trigger();
  QApplication::processEvents();
  const auto rgb_path = base + QStringLiteral("-rgb.psd");
  CHECK(!save_asks_rgb_question(window, rgb_path, QMessageBox::Cancel));
  CHECK(psd_file_color_mode(rgb_path) == 3);
  CHECK(!window.statusBar()->currentMessage().contains(QStringLiteral("in CMYK")));
}

// Opening a CMYK file into CMYK Proof says once, in a dialog, that the mode is an emulation
// (Patchy edits in RGB), until "Don't show this again"; the import note says it every time.
// Scripted opens never show the dialog.
void ui_cmyk_open_explains_cmyk_proof_emulation() {
  const auto previous = patchy::working_cmyk_space();
  const auto key = QStringLiteral("imports/cmykProofOpenNotice");
  const auto restore = qScopeGuard([&previous, &key] {
    patchy::set_working_cmyk_space(previous);
    patchy::ui::app_settings().setValue(key, false);  // the suite default (tests/ui/main.cpp)
  });
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::from_icc_profile(
      patchy::ui::bundled_cmyk_profile(), patchy::RenderingIntent::RelativeColorimetric, true));
  patchy::ui::app_settings().setValue(key, true);
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto source = directory.filePath(QStringLiteral("print.psd"));
  write_flat_cmyk_psd(source, patchy::ui::bundled_cmyk_profile());
  // Opens interactively; true when the notice came up. `dont_show` ticks its check box.
  const auto open_sees_notice = [&window](const QString& path, bool dont_show, QString* text = nullptr) {
    bool seen = false;
    // Polled for the whole open: the open pumps events itself before the notice comes up.
    QTimer poll;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, [&poll, &seen, dont_show, text] {
      auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("cmykProofOpenNoticeDialog")));
      if (dialog == nullptr) return;
      poll.stop();
      seen = true;
      if (text != nullptr) {
        for (const auto* label : dialog->findChildren<QLabel*>()) *text += label->text() + QLatin1Char('\n');
      }
      auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("cmykProofOpenNoticeDontShowCheck"));
      CHECK(check != nullptr);
      check->setChecked(dont_show);
      dialog->accept();
    });
    poll.start();
    patchy::ui::MainWindowTestAccess::open_document_path(window, path);
    QApplication::processEvents();
    poll.stop();
    return seen;
  };
  QString text;
  CHECK(open_sees_notice(source, false, &text));
  CHECK(text.contains(QStringLiteral("emulation")));
  CHECK(text.contains(QStringLiteral("edits in RGB")));
  auto* cmyk_action = window.findChild<QAction*>(QStringLiteral("imageModeCmykProofAction"));
  CHECK(cmyk_action != nullptr && cmyk_action->isChecked());
  // The short note rides the status bar after the CMYK Proof one.
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("more import note")));
  CHECK(patchy::ui::app_settings().value(key, true).toBool());
  // Shown again until it is turned off; then never.
  CHECK(open_sees_notice(source, true));
  CHECK(!patchy::ui::app_settings().value(key, true).toBool());
  CHECK(!open_sees_notice(source, false));
  // A script's open never shows it, even with the notice on.
  patchy::ui::app_settings().setValue(key, true);
  bool scripted_seen = false;
  QTimer scripted_poll;
  scripted_poll.setInterval(20);
  QObject::connect(&scripted_poll, &QTimer::timeout, [&scripted_seen] {
    if (auto* dialog = qobject_cast<QDialog*>(find_top_level_dialog(QStringLiteral("cmykProofOpenNoticeDialog")))) {
      scripted_seen = true;
      dialog->accept();
    }
  });
  scripted_poll.start();
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("cmyk-open-notice");
  options.args = QStringList{QStringLiteral("path=") + source};
  CHECK(host.run_source(QStringLiteral("console.log('opened proof=' + app.open(patchy.args.path).cmykProof);"),
                        options));
  QElapsedTimer timer;
  timer.start();
  while ((host.run_active() || timer.elapsed() < 400) && timer.elapsed() < 60000) {
    QApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  scripted_poll.stop();
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("opened proof=true")));
  CHECK(!scripted_seen);
}

// The reporter's Photoshop file from GitHub issue 220: a flat 8-bit CMYK PSD. It opens in
// CMYK Proof, and saving it unchanged writes CMYK. The capture shows the Mode menu.
void ui_issue_220_photoshop_cmyk_psd_opens_in_cmyk_proof_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("issue-220/Photoshop CMYK test.psd");
  if (!std::filesystem::exists(path)) {
    patchy::test::skip("local fixture local-test-fixtures/psd/issue-220/Photoshop CMYK test.psd is absent");
  }
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::from_icc_profile(
      patchy::ui::bundled_cmyk_profile(), patchy::RenderingIntent::RelativeColorimetric, true));
  patchy::ui::MainWindowTestAccess::open_document_path(window, patchy::ui::to_qstring(path));
  QApplication::processEvents();
  save_image_artifact("ui_issue_220_cmyk_psd_image_mode", image_mode_capture(window));
  auto* cmyk_action = window.findChild<QAction*>(QStringLiteral("imageModeCmykProofAction"));
  CHECK(cmyk_action != nullptr && cmyk_action->isChecked());
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto saved = directory.filePath(QStringLiteral("again.psd"));
  CHECK(!save_asks_rgb_question(window, saved, QMessageBox::Cancel));
  CHECK(psd_file_color_mode(saved) == 4);
}

void ui_script_export_cmyk_writes_jpeg_tiff_and_pdf() {
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  // The 16-bit export below needs deep editing, which the suite leaves off by default.
  patchy::set_deep_editing_override(true);
  const auto restore_depth = qScopeGuard([] { patchy::set_deep_editing_override(std::nullopt); });
  patchy::ui::MainWindow window;
  show_window(window);
  const auto space = patchy::CmykWorkingSpace::from_icc_profile(patchy::ui::bundled_cmyk_profile(),
                                                                patchy::RenderingIntent::RelativeColorimetric, true);
  patchy::set_working_cmyk_space(space);
  QTemporaryDir directory;
  CHECK(directory.isValid());
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("cmyk-export");
  options.args = QStringList{QStringLiteral("base=") + directory.path() + QStringLiteral("/print")};
  // Unattended, as from the command line: a failed save reaches the script instead of an
  // error box waiting for a click.
  options.unattended = true;
  const bool export_run = host.run_source(QStringLiteral(R"JS(
    var d = app.newDocument(40, 30);
    d.activeLayer.fill('#00a0e0');
    d.activeLayer.fillRect(0, 0, 20, 30, 'transparent');
    d.activeLayer.fillRect(0, 0, 10, 30, '#000000');
    var p = patchy.args.base;
    if (!d.exportAs(p + '.jpg', {colorMode: 'cmyk', quality: 95})) throw Error('jpg');
    if (!d.exportAs(p + '.tif', {colorMode: 'cmyk'})) throw Error('tif');
    if (!d.exportAs(p + '-k.tif', {colorMode: 'cmyk', blackInkOnly: true})) throw Error('black ink tif');
    try { d.exportAs(p + '-bad.tif', {blackInkOnly: true}); console.log('k-without-cmyk=accepted'); }
    catch (e) { console.log('k-without-cmyk=refused'); }
    var deep = app.newDocument(16, 16);
    deep.convertBitDepth(16);
    deep.activeLayer.fill('#00a0e0');
    if (!deep.exportAs(p + '-16.tif', {colorMode: 'cmyk'})) throw Error('16-bit tif');
    if (!d.exportAs(p + '.pdf', {colorMode: 'cmyk'})) throw Error('pdf');
    if (!d.exportAs(p + '-rgb.jpg', {colorMode: 'rgb'})) throw Error('rgb jpg');
    try { d.exportAs(p + '.png', {colorMode: 'cmyk'}); console.log('png=accepted'); }
    catch (e) { console.log('png=refused'); }
  )JS"), options);
  if (!export_run) {
    for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  }
  CHECK(export_run);
  // A run that opened documents finishes a moment later; the next run_source must wait.
  const auto wait_for_run = [&host] {
    QElapsedTimer timer;
    timer.start();
    while (host.run_active() && timer.elapsed() < 60000) {
      QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QApplication::processEvents();
  };
  wait_for_run();
  CHECK(!host.last_run_had_error());
  CHECK(backlog_contains(window, QStringLiteral("png=refused")));
  CHECK(backlog_contains(window, QStringLiteral("k-without-cmyk=refused")));
  const auto base = directory.path() + QStringLiteral("/print");

  // The ink values the working space gives the fill, and paper white where it was clear.
  const auto expected = space->separate_rgba8(std::array<std::uint8_t, 4>{0x00, 0xa0, 0xe0, 255});
  const auto read_cmyk = [](const QString& path) {
    QImageReader reader(path);
    return reader.read();
  };
  for (const auto& extension : {QStringLiteral(".jpg"), QStringLiteral(".tif")}) {
    const auto image = read_cmyk(base + extension);
    CHECK(image.format() == QImage::Format_CMYK8888);
    CHECK(image.size() == QSize(40, 30));
    const auto* ink = image.constScanLine(15) + 30 * 4;
    const int tolerance = extension == QStringLiteral(".jpg") ? 6 : 0;
    for (std::size_t channel = 0; channel < 4; ++channel) {
      CHECK(std::abs(static_cast<int>(ink[channel]) - static_cast<int>(expected[channel])) <= tolerance);
    }
    const auto* paper = image.constScanLine(15) + 15 * 4;
    CHECK(paper[0] <= tolerance && paper[1] <= tolerance && paper[2] <= tolerance && paper[3] <= tolerance);
    QFile file(base + extension);
    CHECK(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll().contains("ISO Coated v2 (basICColor)"));
  }
  // Black through the profile is rich black; with blackInkOnly it is the K plate alone.
  {
    const auto rich = read_cmyk(base + QStringLiteral(".tif"));
    const auto* rich_black = rich.constScanLine(15) + 5 * 4;
    CHECK(rich_black[0] > 100 && rich_black[3] > 200);
    const auto plain = read_cmyk(base + QStringLiteral("-k.tif"));
    CHECK(plain.format() == QImage::Format_CMYK8888);
    const auto* plain_black = plain.constScanLine(15) + 5 * 4;
    CHECK(plain_black[0] == 0 && plain_black[1] == 0 && plain_black[2] == 0 && plain_black[3] == 255);
    const auto* plain_blue = plain.constScanLine(15) + 30 * 4;
    for (std::size_t channel = 0; channel < 4; ++channel) {
      CHECK(plain_blue[channel] == expected[channel]);
    }
  }
  QFile pdf(base + QStringLiteral(".pdf"));
  CHECK(pdf.open(QIODevice::ReadOnly));
  const auto pdf_bytes = pdf.readAll();
  CHECK(pdf_bytes.contains("/N 4 /Alternate /DeviceCMYK"));
  CHECK(pdf_bytes.contains("/OutputIntents [<< /Type /OutputIntent /S /GTS_PDFX"));
  CHECK(pdf_bytes.contains("/ICCBased"));
  CHECK(pdf_bytes.contains("ISO Coated v2 (basICColor)"));
  CHECK(read_cmyk(base + QStringLiteral("-rgb.jpg")).format() != QImage::Format_CMYK8888);
  // A 16-bit document separates at 8 bits per ink.
  {
    const auto deep = read_cmyk(base + QStringLiteral("-16.tif"));
    CHECK(deep.format() == QImage::Format_CMYK8888);
    const auto* ink = deep.constScanLine(8) + 8 * 4;
    for (std::size_t channel = 0; channel < 4; ++channel) {
      CHECK(std::abs(static_cast<int>(ink[channel]) - static_cast<int>(expected[channel])) <= 1);
    }
  }
#ifdef PATCHY_HAVE_QT_PDF
  // A real PDF renderer (Qt PDF, PDFium) reads the CMYK page back through its embedded
  // profile: an in-gamut color returns to about the color it came from.
  {
    QPdfDocument rendered;
    CHECK(rendered.load(base + QStringLiteral(".pdf")) == QPdfDocument::Error::None);
    const auto page = rendered.render(0, QSize(40, 30));
    CHECK(!page.isNull());
    const auto seen = page.pixelColor(30, 15);
    CHECK(color_close(seen, QColor(0x00, 0xa0, 0xe0), 8));
    // Proof Colors shows the inks as Photoshop does: this file's 75/20/0/0 converted back to
    // sRGB relative colorimetric with black point compensation reads 8,159,218 in Photoshop
    // 2026 (34,161,219 without compensation). PDFium converts back perceptually, so it
    // lands a few levels away.
    const auto proof = patchy::CmykProofTransform::create(*patchy::working_cmyk_space(), true, false);
    CHECK(proof != nullptr);
    if (proof != nullptr) {
      const auto printed = proof->printed(patchy::RgbColor{0x00, 0xa0, 0xe0});
      const QColor proofed(printed.red, printed.green, printed.blue);
      CHECK(color_close(proofed, QColor(8, 159, 218), 2));
      CHECK(color_close(seen, proofed, 10));
    }
    // The soft mask keeps the clear area transparent, as in an RGB PDF; rich black prints black.
    CHECK(page.pixelColor(15, 15).alpha() == 0);
    CHECK(color_close(page.pixelColor(5, 15), QColor(0, 0, 0), 12));
  }
#endif

  // With no profile there is nothing to describe the inks by.
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::formula());
  CHECK(host.run_source(QStringLiteral(R"JS(
    var written = 'threw';
    try { written = app.activeDocument.exportAs(patchy.args.base + '-formula.jpg', {colorMode: 'cmyk'}); } catch (e) {}
    console.log('formula=' + written);
  )JS"), options));
  wait_for_run();
  // Unattended, the export reports failure (its reason goes to the run's error output).
  CHECK(!backlog_contains(window, QStringLiteral("formula=true")));
  CHECK(backlog_contains(window, QStringLiteral("formula=false")) || backlog_contains(window, QStringLiteral("formula=threw")));
  CHECK(!QFile::exists(base + QStringLiteral("-formula.jpg")));
}

// View > Proof Colors and Gamut Warning (issue 90) change what the canvas shows and
// nothing else: the pixel readout stays the document's color.
void ui_proof_colors_and_gamut_warning_change_only_the_view() {
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  patchy::ui::MainWindow window;
  show_window(window);
  // The bundled profile, whatever this machine has installed.
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::from_icc_profile(
      patchy::ui::bundled_cmyk_profile(), patchy::RenderingIntent::RelativeColorimetric, true));
  auto* canvas = require_canvas(window);
  auto* proof_action = window.findChild<QAction*>(QStringLiteral("viewProofColorsAction"));
  auto* gamut_action = window.findChild<QAction*>(QStringLiteral("viewGamutWarningAction"));
  CHECK(proof_action != nullptr && gamut_action != nullptr);
  CHECK(proof_action->isCheckable() && !proof_action->isChecked());
  CHECK(gamut_action->isCheckable() && !gamut_action->isChecked());

  // Left half pure green (no offset press can print it), right half a muted in-gamut tone.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    doc.activeLayer.fill('#966e5a');
    doc.activeLayer.fillRect(0, 0, Math.floor(doc.width / 2), doc.height, '#00ff00');
    patchy.ui.fitOnScreen();
    console.log('gamut=' + app.colorInCmykGamut('#00ff00') + ',' + app.colorInCmykGamut('#966e5a'));
    console.log('views=' + patchy.ui.proofColors + ',' + patchy.ui.gamutWarning);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("gamut=false,true")));
  CHECK(backlog_contains(window, QStringLiteral("views=false,false")));
  const auto& document = patchy::ui::MainWindowTestAccess::document(window);
  const QPoint green_point(document.width() / 4, document.height() / 2);
  const QPoint muted_point(document.width() * 3 / 4, document.height() / 2);
  const auto shown = [&](QPoint document_point) {
    QApplication::processEvents();
    return canvas->grab().toImage().pixelColor(canvas->widget_position_for_document_point(document_point));
  };
  const QColor green(0, 255, 0);
  const QColor muted(0x96, 0x6e, 0x5a);
  CHECK(color_close(shown(green_point), green, 2));
  CHECK(color_close(shown(muted_point), muted, 2));

  // Proof Colors: the green dulls, the in-gamut tone barely moves.
  CHECK(run_script(window, QStringLiteral("patchy.ui.proofColors = true; console.log('proof=' + patchy.ui.proofColors);")));
  CHECK(backlog_contains(window, QStringLiteral("proof=true")));
  CHECK(proof_action->isChecked());
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("ISO Coated v2")));
  const auto proofed_green = shown(green_point);
  CHECK(!color_close(proofed_green, green, 12));
  CHECK(proofed_green.green() > proofed_green.red() && proofed_green.green() > proofed_green.blue());
  CHECK(color_close(shown(muted_point), muted, 8));
  save_widget_artifact("ui_proof_colors", *canvas);

  // The readout under the pointer is still the document's color.
  auto* info = window.findChild<QLabel*>(QStringLiteral("canvasInfoLabel"));
  patchy::test::ui::send_mouse(*canvas, QEvent::MouseMove, canvas->widget_position_for_document_point(green_point), Qt::NoButton,
             Qt::NoButton);
  CHECK(info->text().contains(QStringLiteral("RGB: 0, 255, 0")));

  // Gamut Warning on top: the green becomes the flat warning gray.
  gamut_action->trigger();
  CHECK(gamut_action->isChecked());
  const QColor warning(patchy::kGamutWarningColor.red, patchy::kGamutWarningColor.green,
                       patchy::kGamutWarningColor.blue);
  CHECK(color_close(shown(green_point), warning, 1));
  CHECK(color_close(shown(muted_point), muted, 8));
  save_widget_artifact("ui_gamut_warning", *canvas);

  // An edit made under the view is shown through it too.
  CHECK(run_script(window, QStringLiteral(
      "var d = app.activeDocument; d.activeLayer.fillRect(0, 0, d.width, d.height, '#00ff00');")));
  CHECK(color_close(shown(muted_point), warning, 1));
  CHECK(run_script(window, QStringLiteral("app.activeDocument.undo();")));

  // Gamut Warning alone leaves in-gamut colors exactly as they are.
  proof_action->trigger();
  CHECK(!proof_action->isChecked());
  CHECK(color_close(shown(green_point), warning, 1));
  CHECK(color_close(shown(muted_point), muted, 2));

  gamut_action->trigger();
  CHECK(color_close(shown(green_point), green, 2));
  CHECK(canvas->proof_display_transform() == nullptr);

  // A new document opened while the view is on gets it as well.
  proof_action->trigger();
  CHECK(run_script(window, QStringLiteral("var n = app.newDocument(64, 64); n.activeLayer.fill('#00ff00');")));
  CHECK(require_canvas(window)->proof_display_transform() != nullptr);
  proof_action->trigger();

  // Without a profile there is nothing to proof against: the toggles refuse and say why.
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::formula());
  CHECK(run_script(window, QStringLiteral(
      "patchy.ui.proofColors = true; patchy.ui.gamutWarning = true;"
      "console.log('formula=' + patchy.ui.proofColors + ',' + patchy.ui.gamutWarning + ',' + app.colorInCmykGamut('#00ff00'));")));
  CHECK(backlog_contains(window, QStringLiteral("formula=false,false,true")));
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("need a CMYK profile")));
}

// The proof view follows live edits (issue 90): a brush stroke lands through the render
// cache's partial patches, and a Move drag draws its base and patches through the view.
void ui_proof_colors_follow_brush_strokes_and_move_drags() {
  const auto previous = patchy::working_cmyk_space();
  const auto restore = qScopeGuard([&previous] { patchy::set_working_cmyk_space(previous); });
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::set_working_cmyk_space(patchy::CmykWorkingSpace::from_icc_profile(
      patchy::ui::bundled_cmyk_profile(), patchy::RenderingIntent::RelativeColorimetric, true));
  auto* canvas = require_canvas(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    doc.activeLayer.fill('#966e5a');
    var top = doc.addLayer('Green square');
    top.fillRect(40, 300, 120, 120, '#00ff00');
    patchy.ui.fitOnScreen();
  )JS")));
  window.findChild<QAction*>(QStringLiteral("viewProofColorsAction"))->trigger();
  QApplication::processEvents();
  const auto view = canvas->proof_display_transform();
  CHECK(view != nullptr);
  const auto printed_green = view->printed({0, 255, 0});
  const QColor proofed_green(printed_green.red, printed_green.green, printed_green.blue);
  CHECK(!color_close(proofed_green, QColor(0, 255, 0), 12));
  const auto shown = [&](QPoint document_point) {
    QApplication::processEvents();
    return canvas->grab().toImage().pixelColor(canvas->widget_position_for_document_point(document_point));
  };
  CHECK(color_close(shown(QPoint(100, 360)), proofed_green, 3));

  // A hard round brush stroke in pure green across the muted background.
  canvas->set_tool(patchy::ui::CanvasTool::Brush);
  canvas->set_primary_color(QColor(0, 255, 0));
  canvas->set_brush_size(40);
  const auto patches_before = canvas->render_cache_diagnostics().partial_patches;
  drag_document_path(*canvas, {QPoint(300, 100), QPoint(700, 100)}, 12);
  QApplication::processEvents();
  CHECK(canvas->render_cache_diagnostics().partial_patches > patches_before);
  CHECK(color_close(shown(QPoint(500, 100)), proofed_green, 4));
  CHECK(!color_close(shown(QPoint(500, 100)), QColor(0, 255, 0), 12));

  // Mid-drag, the moved square still shows in proof colors.
  canvas->set_tool(patchy::ui::CanvasTool::Move);
  const auto from = canvas->widget_position_for_document_point(QPoint(100, 360));
  const auto to = canvas->widget_position_for_document_point(QPoint(500, 560));
  send_mouse(*canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
  for (int step = 1; step <= 8; ++step) {
    send_mouse(*canvas, QEvent::MouseMove, from + (to - from) * step / 8, Qt::LeftButton, Qt::LeftButton);
  }
  QApplication::processEvents();
  CHECK(color_close(shown(QPoint(500, 560)), proofed_green, 4));
  save_widget_artifact("ui_proof_colors_move_drag", *canvas);
  send_mouse(*canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(color_close(shown(QPoint(500, 560)), proofed_green, 4));
  window.findChild<QAction*>(QStringLiteral("viewProofColorsAction"))->trigger();
}

// A font that is installed but holds no glyph for the text gets its own warning. "Font not
// available" sent a script author looking for a font file that was already registered (the
// bundled Noto Naskh Arabic asked for Latin text); text the font can draw warns about nothing.
void ui_script_text_font_without_glyphs_warns_with_the_real_cause() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto noto_path = QStringLiteral(PATCHY_SOURCE_DIR "/third_party/fonts/noto_naskh_arabic/NotoNaskhArabic-Bold.ttf");
  CHECK(QFontDatabase::addApplicationFont(noto_path) >= 0);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var arabic = doc.addTextLayer('سلام', {font: 'Noto Naskh Arabic', bold: true, size: 24, x: 10, y: 40});
    console.log('arabic=' + arabic.textFont);
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("arabic=Noto Naskh Arabic")));
  // The Bold face is the one this test registers, beside the suite's other Arabic-capable
  // families, so the layers ask for it by name: an earlier test may have registered a Regular
  // face from a folder it has since deleted. The coverage probe has to ask the face the family
  // really has, not lose a Regular request to Arial.
  CHECK(!backlog_contains(window, QStringLiteral("rendered with a fallback")));

  CHECK(run_script(window, QStringLiteral(R"JS(
    app.activeDocument.addTextLayer('Blazing Star', {font: 'Noto Naskh Arabic', bold: true, size: 24, x: 10, y: 90});
  )JS")));
  CHECK(backlog_contains(
      window, QStringLiteral("font has no glyphs for this text, rendered with a fallback: Noto Naskh Arabic")));
  CHECK(!backlog_contains(window, QStringLiteral("font not available")));
}

// The setters that re-edit a text layer warn like addTextLayer does. An edit session moves a
// family it cannot draw onto a substitute, so `layer.text = ...` on a layer whose font was missing
// used to change the font with no word to the script; a run that names a missing or glyphless
// font was just as quiet. A warning is a console line: nothing here opens a dialog.
void ui_script_text_setters_warn_about_fonts() {
  patchy::ui::MainWindow window;
  show_window(window);
  const auto noto_path = QStringLiteral(PATCHY_SOURCE_DIR "/third_party/fonts/noto_naskh_arabic/NotoNaskhArabic-Bold.ttf");
  CHECK(QFontDatabase::addApplicationFont(noto_path) >= 0);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto good = patchy::test::visual_test_font().family();
  CHECK(QFontDatabase::families().contains(good));
  const auto missing = QStringLiteral("Patchy No Such Family");
  const auto naskh = QStringLiteral("Noto Naskh Arabic");

  // A layer in a good font: no setter says a word.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var quiet = app.activeDocument.addTextLayer('Quiet', {font: '%1', size: 24, x: 10, y: 40});
    quiet.text = 'Still quiet';
    quiet.setTextRuns([{text: 'Still '}, {text: 'quiet', bold: true}]);
    quiet.textAlign = 'center';
    console.log('quiet-done');
  )JS").arg(good)));
  CHECK(backlog_contains(window, QStringLiteral("quiet-done")));
  CHECK(!backlog_contains(window, QStringLiteral("rendered with a fallback")));

  // The text setter on a layer whose font is missing: the session substitutes it, and says so.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var lost = app.activeDocument.addTextLayer('First', {font: 'Patchy No Such Family', size: 24, x: 10, y: 80});
    lost.text = 'Second';
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("layer.text: font not available, rendered with a fallback: ") + missing));

  // The text setter that leaves an installed font nothing it can draw.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var arabic = app.activeDocument.addTextLayer('سلام', {font: 'Noto Naskh Arabic', bold: true, size: 24, x: 10, y: 120});
    arabic.text = 'Latin now';
  )JS")));
  CHECK(backlog_contains(
      window, QStringLiteral("layer.text: font has no glyphs for this text, rendered with a fallback: ") + naskh));

  // Runs that name one font of each kind report both.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var mixed = app.activeDocument.addTextLayer('Good', {font: '%1', size: 24, x: 10, y: 160});
    mixed.setTextRuns([{text: 'one ', font: 'Patchy No Such Family'}, {text: 'two', font: 'Noto Naskh Arabic', bold: true},
                       {text: ' three'}]);
  )JS").arg(good)));
  CHECK(backlog_contains(
      window, QStringLiteral("layer.setTextRuns: font not available, rendered with a fallback: ") + missing));
  CHECK(backlog_contains(
      window, QStringLiteral("layer.setTextRuns: font has no glyphs for this text, rendered with a fallback: ") + naskh));

  // Runs that give every character a good font replace the missing family on purpose: silence.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var fixed = app.activeDocument.addTextLayer('Broken', {font: 'Patchy Other Missing Family', size: 24, x: 10, y: 200});
    fixed.setTextRuns([{text: 'Fixed', font: '%1'}]);
    console.log('fixed=' + fixed.textFont);
  )JS").arg(good)));
  CHECK(backlog_contains(window, QStringLiteral("fixed=") + good));
  CHECK(backlog_contains(window, QStringLiteral("addTextLayer: font not available, rendered with a fallback: "
                                                "Patchy Other Missing Family")));
  CHECK(!backlog_contains(window, QStringLiteral("layer.setTextRuns: font not available, rendered with a fallback: "
                                                 "Patchy Other Missing Family")));
}

void ui_script_text_layer_with_uncovered_script_does_not_crash() {
  // No registered face covers Thai in the offscreen suite (the registry rescue is off), so
  // Qt answers the per-writing-system probe with its glyph-box engine. The missing-font
  // check used to index that engine's empty family list and take the process down.
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    var layer = doc.addTextLayer('สวัสดี', {size: 24, x: 10, y: 40});
    var first = layer.bounds.height;
    layer.text = 'สวัสดี こんにちは';
    console.log('survived=' + (first > 0 && layer.bounds.height > 0 && layer.isText));
  )JS")));
  CHECK(backlog_contains(window, QStringLiteral("survived=true")));
}

void ui_script_run_command_writes_output_file() {
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir temp;
  CHECK(temp.isValid());
  const auto script_path = temp.filePath(QStringLiteral("cli-test.js"));
  {
    QFile file(script_path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("console.log('from cli script');\nconsole.warn('careful');\n");
  }
  const auto output_path = temp.filePath(QStringLiteral("out.txt"));
  window.run_script_command(script_path, output_path);
  auto& host = window.script_engine_host();
  wait_for_run_end(host);
  QElapsedTimer timer;
  timer.start();
  while (!QFile::exists(output_path) && timer.elapsed() < 5000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QFile output(output_path);
  CHECK(output.open(QIODevice::ReadOnly | QIODevice::Text));
  const auto text = QString::fromUtf8(output.readAll());
  CHECK(text.contains(QStringLiteral("from cli script")));
  CHECK(text.contains(QStringLiteral("[warn] careful")));
  CHECK(text.endsWith(QStringLiteral("[done]\n")));
}

void ui_script_editor_dialog_runs_and_shows_console() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* console_pane = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorConsole"));
  auto* run_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorRunButton"));
  CHECK(code != nullptr && console_pane != nullptr && run_button != nullptr);
  code->setPlainText(QStringLiteral("console.log('hello from editor');"));
  run_button->click();
  wait_for_run_end(window.script_engine_host());
  CHECK(console_pane->toPlainText().contains(QStringLiteral("hello from editor")));
  save_widget_artifact("script_editor_dialog", dialog);
  auto& theme = patchy::ui::ThemeManager::instance();
  const auto old_preference = theme.preference();
  theme.set_preference(patchy::ui::ColorSchemePreference::Light, false);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  save_widget_artifact("script_editor_dialog_light", dialog);
  theme.set_preference(old_preference, false);
  dialog.close();
}

void ui_script_editor_status_shows_running_and_ready() {
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* run_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorRunButton"));
  auto* stop_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorStopButton"));
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("scriptEditorStatusLabel"));
  CHECK(code != nullptr && run_button != nullptr && stop_button != nullptr && status != nullptr);
  CHECK(status->text() == QStringLiteral("Ready"));
  CHECK(!stop_button->isEnabled());
  // A long timer keeps the run alive after the synchronous phase, so the
  // running state is observable; the stop-sign button ends it.
  code->setPlainText(QStringLiteral("setTimeout(function () {}, 60000);"));
  run_button->click();
  CHECK(window.script_engine_host().run_active());
  CHECK(status->text().startsWith(QStringLiteral("Running")));
  CHECK(stop_button->isEnabled());
  CHECK(!run_button->isEnabled());
  save_widget_artifact("script_editor_running_status", dialog);
  stop_button->click();
  wait_for_run_end(window.script_engine_host());
  CHECK(status->text() == QStringLiteral("Ready"));
  CHECK(!stop_button->isEnabled());
  CHECK(run_button->isEnabled());
  dialog.close();
}

void ui_script_canvas_window_renders_frames() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = start_script(window, QStringLiteral(R"JS(
    var win = patchy.ui.createCanvas({width: 200, height: 140, title: 'Test Window'});
    win.onFrame = function () {
      var g = win.graphics;
      g.clear('#204060');
      g.fillRect(20, 20, 80, 50, '#ffcc00');
      g.circle(150, 90, 30, '#40ff40', true);
      g.text(16, 120, 'scripted', '#ffffff', 12);
    };
  )JS"));
  CHECK(host.run_active());
  auto* dialog = window.findChild<QDialog*>(QStringLiteral("scriptCanvasWindowDialog"));
  CHECK(dialog != nullptr);
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 400) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  const QImage frame = dialog->grab().toImage();
  CHECK(frame.pixelColor(60, 40) == QColor(0xff, 0xcc, 0x00));
  CHECK(frame.pixelColor(150, 90) == QColor(0x40, 0xff, 0x40));
  save_widget_artifact("script_canvas_window", *dialog);
  host.stop_active_run();
  wait_for_run_end(host);
  CHECK(!host.run_active());
}

// doc.saveAs(path, options) / doc.exportAs(path, options): explicit format options reach
// the writer with no dialog, exportAs never retargets the document, saveAs does.
void ui_script_save_as_options_write_dds_and_jpeg() {
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto base = directory.path() + QStringLiteral("/options");
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("save-options");
  options.args = QStringList{QStringLiteral("base=") + base};
  const auto source = QStringLiteral(R"JS(
    var d = app.newDocument(64, 64);
    d.activeLayer.fill('#80ff0000');  // translucent: Automatic would pick BC3
    var p = patchy.args.base;
    if (!d.exportAs(p + '-bc1.dds', {compression: 'bc1', mipmaps: 'on'})) throw Error('bc1');
    if (!d.exportAs(p + '-bc3.dds', {compression: 'bc3', mipmaps: 'off'})) throw Error('bc3');
    if (d.path !== '' || !d.modified) throw Error('exportAs retargeted the document');
    if (!d.exportAs(p + '-q20.jpg', {quality: 20})) throw Error('jpg 20');
    if (!d.exportAs(p + '-q95.jpg', {quality: 95})) throw Error('jpg 95');
    if (patchy.io.fileSize(p + '-q20.jpg') >= patchy.io.fileSize(p + '-q95.jpg')) throw Error('quality ignored');
    // saveAs with options still retargets the session like a plain saveAs.
    if (!d.saveAs(p + '-saved.jpg', {quality: 50})) throw Error('saveAs');
    if (d.path.indexOf('-saved.jpg') < 0 || d.modified) throw Error('saveAs did not retarget');
    console.log('done');
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  }
  CHECK(!host.last_run_had_error());
  const auto bc1 = probe_dds_header(base + QStringLiteral("-bc1.dds"));
  CHECK(bc1.valid);
  CHECK(bc1.four_cc == QByteArray("DXT1"));
  CHECK(bc1.mip_count == 7);  // 64 px: 1 + floor(log2(64))
  const auto bc3 = probe_dds_header(base + QStringLiteral("-bc3.dds"));
  CHECK(bc3.valid);
  CHECK(bc3.four_cc == QByteArray("DXT5"));
  CHECK(bc3.mip_count == 0);  // level 0 only: the count field stays 0 (docs/dds.md)
}

// Export for Web's JPEG keys (docs/web-export.md): progressive, optimizeCoding and
// thumbnail, strict like every other save option.
void ui_script_export_as_jpeg_coding_options() {
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto base = directory.path() + QStringLiteral("/coding");
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("jpeg-coding");
  options.args = QStringList{QStringLiteral("base=") + base};
  const auto source = QStringLiteral(R"JS(
    var d = app.newDocument(96, 64);
    d.activeLayer.fill('#336699');
    var p = patchy.args.base;
    if (!d.exportAs(p + '-plain.jpg', {quality: 70})) throw Error('plain');
    if (!d.exportAs(p + '-web.jpg', {quality: 70, progressive: true, optimizeCoding: true, thumbnail: false})) throw Error('web');
    var threw = 0;
    try { d.exportAs(p + '-bad.png', {progressive: true}); } catch (e) { threw++; }
    try { d.exportAs(p + '-bad.jpg', {thumbnail: 'no'}); } catch (e) { threw++; }
    if (threw !== 2) throw Error('not strict: ' + threw);
    console.log('done');
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  }
  CHECK(!host.last_run_had_error());
  // The first start-of-frame marker: 0xC0 baseline, 0xC2 progressive.
  const auto frame_marker = [](const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      return -1;
    }
    const auto bytes = file.readAll();
    qsizetype at = 2;
    while (at + 4 <= bytes.size()) {
      const auto marker = static_cast<unsigned char>(bytes[at + 1]);
      if (marker >= 0xC0 && marker <= 0xC2) {
        return static_cast<int>(marker);
      }
      at += 2 + ((static_cast<unsigned char>(bytes[at + 2]) << 8) | static_cast<unsigned char>(bytes[at + 3]));
    }
    return -1;
  };
  CHECK(frame_marker(base + QStringLiteral("-plain.jpg")) == 0xC0);
  CHECK(frame_marker(base + QStringLiteral("-web.jpg")) == 0xC2);
  CHECK(!QFileInfo::exists(base + QStringLiteral("-bad.png")));
  CHECK(!QFileInfo::exists(base + QStringLiteral("-bad.jpg")));
}

void ui_script_save_as_options_write_jxl() {
  patchy::set_deep_editing_override(true);
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto base = directory.path() + QStringLiteral("/jxl");
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("save-options-jxl");
  options.args = QStringList{QStringLiteral("base=") + base};
  const auto source = QStringLiteral(R"JS(
    var d = app.newDocument(48, 32);
    d.activeLayer.fill('#ff3366');
    d.addLayer('second 0.25s').fill('#3366ff');
    var p = patchy.args.base;
    if (!d.exportAs(p + '-q20.jxl', {quality: 20, effort: 3})) throw Error('q20');
    if (!d.exportAs(p + '-anim.jxl', {animate: true, frameDelayMs: 80, loopCount: 4, lossless: true})) throw Error('anim');
    var a = app.open(p + '-anim.jxl');
    if (a.layers.length !== 2) throw Error('frames ' + a.layers.length);
    var bad = false;
    try { d.exportAs(p + '-bad.jxl', {effort: 12}); } catch (e) { bad = true; }
    if (!bad) throw Error('effort 12 was accepted');
    d.convertBitDepth(16);
    if (!d.saveAs(p + '-16.jxl', {lossless: true, effort: 1})) throw Error('16');
    var e = app.open(p + '-16.jxl');
    if (e.bitDepth !== 16) throw Error('reopened at ' + e.bitDepth + ' bits');
    console.log('done');
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  patchy::set_deep_editing_override(std::nullopt);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  }
  CHECK(!host.last_run_had_error());
  CHECK(!QFileInfo::exists(base + QStringLiteral("-bad.jxl")));
  QFile animated(base + QStringLiteral("-anim.jxl"));
  CHECK(animated.open(QIODevice::ReadOnly));
  const auto bytes = animated.readAll();
  const auto read = patchy::jxl::read_jxl_detailed(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
  CHECK(read.frame_count == 2);
  CHECK(read.read.document.metadata().values.at(patchy::jxl::kLoopCountMetadata) == "4");
  // The top layer is frame 1 and its name token wins over frameDelayMs.
  CHECK(read.read.document.layers().back().name() == "Frame 1 0.25s");
  CHECK(read.read.document.layers().front().name() == "Frame 2 0.08s");
}

// JPEG XL through the script API: still and animated exports, the effort key, 16-bit
// saves and reopening (docs/jxl.md).
void ui_script_save_as_options_write_avif() {
  if (!patchy::avif::is_available()) {
    patchy::test::skip("this build has no AVIF codec");
  }
  patchy::set_deep_editing_override(true);
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto base = directory.path() + QStringLiteral("/avif");
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("save-options-avif");
  options.args = QStringList{QStringLiteral("base=") + base};
  const auto source = QStringLiteral(R"JS(
    var d = app.newDocument(24, 16);
    d.activeLayer.fill('#ff3366');
    var p = patchy.args.base;
    if (!d.exportAs(p + '-q30.avif', {quality: 30, speed: 10})) throw Error('q30');
    if (!d.saveAs(p + '-lossless.avif', {lossless: true, speed: 10, metadata: 'none'})) throw Error('lossless');
    var a = app.open(p + '-lossless.avif');
    if (a.width !== 24 || a.height !== 16) throw Error('size ' + a.width + 'x' + a.height);
    var bad = false;
    try { d.exportAs(p + '-bad.avif', {speed: 12}); } catch (e) { bad = true; }
    if (!bad) throw Error('speed 12 was accepted');
    bad = false;
    try { d.exportAs(p + '-bad2.avif', {effort: 3}); } catch (e) { bad = true; }
    if (!bad) throw Error('effort was accepted for avif');
    d.convertBitDepth(16);
    if (!d.saveAs(p + '-16.avif', {lossless: true, speed: 10})) throw Error('16');
    var e = app.open(p + '-16.avif');
    if (e.bitDepth !== 16) throw Error('reopened at ' + e.bitDepth + ' bits');
    console.log('done');
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  patchy::set_deep_editing_override(std::nullopt);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  }
  CHECK(!host.last_run_had_error());
  CHECK(!QFileInfo::exists(base + QStringLiteral("-bad.avif")));
  CHECK(!QFileInfo::exists(base + QStringLiteral("-bad2.avif")));
  CHECK(QFileInfo::exists(base + QStringLiteral("-q30.avif")));
}

// Option objects are strict, and a scripted save never rewrites the user's persisted
// saveOptions defaults (its choices are its own).
void ui_script_save_as_options_are_strict() {
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("saveOptions/ddsCompression"), QStringLiteral("bc7"));
    settings.setValue(QStringLiteral("saveOptions/jpegQuality"), 77);
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  QTemporaryDir directory;
  CHECK(directory.isValid());
  const auto base = directory.path() + QStringLiteral("/strict");
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("strict-options");
  options.args = QStringList{QStringLiteral("base=") + base};
  const auto source = QStringLiteral(R"JS(
    var d = app.newDocument(8, 8);
    var p = patchy.args.base;
    function expectThrow(path, o, needle) {
      var message = '';
      try { d.exportAs(path, o); } catch (e) { message = String(e); }
      if (!message) throw Error('accepted ' + JSON.stringify(o) + ' for ' + path);
      if (message.indexOf(needle) < 0) throw Error('message "' + message + '" lacks "' + needle + '"');
      if (patchy.io.fileExists(path)) throw Error('a rejected save wrote ' + path);
    }
    expectThrow(p + '.dds', {compression: 'bc9'}, '"compression"');
    expectThrow(p + '.dds', {mipmaps: true}, '"mipmaps"');
    expectThrow(p + '.dds', {quality: 50}, 'does not apply to a .dds');
    expectThrow(p + '.png', {compression: 'bc1'}, 'does not apply to a .png');
    expectThrow(p + '.jpg', {quality: 101}, '0 to 100');
    expectThrow(p + '.jpg', {quality: 1.5}, '"quality"');
    expectThrow(p + '.jpg', {typo: 1}, '"typo"');
    expectThrow(p + '.jpg', null, 'object');
    expectThrow(p + '.jpg', [1], 'object');
    expectThrow(p + '.ico', {sizes: [17]}, '"sizes"');
    expectThrow(p + '.cur', {hotspot: {x: 1}}, '"hotspot"');
    expectThrow(p + '.ico', {hotspot: {x: 1, y: 1}}, 'does not apply to a .ico');
    // Valid choices for the formats with their own dialogs are accepted.
    if (!d.exportAs(p + '.webp', {quality: 60, lossless: false})) throw Error('webp');
    if (!d.exportAs(p + '.ico', {sizes: [16, 32], resample: 'nearest'})) throw Error('ico');
    if (!d.exportAs(p + '.cur', {sizes: [32], hotspot: {x: 3, y: 4}})) throw Error('cur');
    if (!d.exportAs(p + '.bmp', {encoding: 'rgb24'})) throw Error('bmp');
    if (!d.exportAs(p + '.gif', {frameDelayMs: 250})) throw Error('gif');
    if (!d.exportAs(p + '-explicit.dds', {compression: 'bc1'})) throw Error('dds');
    if (!d.exportAs(p + '-explicit.jpg', {quality: 30})) throw Error('jpg');
  )JS");
  (void)host.run_source(source, std::move(options));
  wait_for_run_end(host);
  CHECK(!host.run_active());
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  }
  CHECK(!host.last_run_had_error());
  auto settings = patchy::ui::app_settings();
  CHECK(settings.value(QStringLiteral("saveOptions/ddsCompression")).toString() == QStringLiteral("bc7"));
  CHECK(settings.value(QStringLiteral("saveOptions/jpegQuality")).toInt() == 77);
  settings.remove(QStringLiteral("saveOptions/ddsCompression"));
  settings.remove(QStringLiteral("saveOptions/jpegQuality"));
  settings.sync();
}

// A user script's "@hotkey" directive is its default shortcut, registered at startup under
// the stable id derived from its relative path; the key runs the script, and a Preferences
// override stored under that id wins over the directive.
void ui_script_hotkey_runs_user_script() {
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("hotkeys"));
    settings.sync();
  }
  write_user_script(QStringLiteral("Mine/hot key.js"), QStringLiteral(
      "// @name Hot Key Script\n// @hotkey Ctrl+Alt+F9\nconsole.log('hot key script ran');\n"));
  const auto id = patchy::ui::script_hotkey_command_id(QStringLiteral("Mine/hot key.js"));
  CHECK(id == QStringLiteral("script.Mine%2Fhot%20key.js"));
  {
    patchy::ui::MainWindow window;
    show_window(window);
    const auto* command = window.hotkey_registry().find_command(id);
    CHECK(command != nullptr);
    CHECK(command->category == QStringLiteral("scripts"));
    CHECK(command->action != nullptr);
    CHECK(command->action->text() == QStringLiteral("Hot Key Script"));
    CHECK(command->action->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F9));
    CHECK(command->action->menuRole() == QAction::NoRole);
    // The bound action runs the script without the Scripts menu ever having been opened.
    // The shortcut-to-action step is Qt's own shortcut map, which the registered
    // tool-cycle keys already exercise; a synthetic Ctrl+Alt+F9 is not sent here because
    // the offscreen platforms deliver it inconsistently (it reached the map only on
    // Windows, and only in a fresh process).
    command->action->trigger();
    wait_for_run_end(window.script_engine_host());
    CHECK(backlog_contains(window, QStringLiteral("hot key script ran")));
    // app.runCommand refuses script commands: one run at a time.
    CHECK(run_script(window, QStringLiteral("if (app.runCommand('") + id +
                                 QStringLiteral("')) throw Error('ran a script from a script');")));
    // The menu lists the same action, so it shows the shortcut.
    auto* menu = window.findChild<QMenu*>(QStringLiteral("fileScriptsMenu"));
    CHECK(menu != nullptr);
    menu->popup(QPoint(0, 0));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    bool listed = false;
    for (auto* action : menu->actions()) {
      if (auto* submenu = action->menu(); submenu != nullptr && action->text() == QStringLiteral("Mine")) {
        for (const auto* entry : submenu->actions()) {
          listed = listed || entry == command->action.data();
        }
      }
    }
    menu->close();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    CHECK(listed);
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("hotkeys/") + id, QStringLiteral("Ctrl+Alt+F10"));
    settings.sync();
  }
  {
    patchy::ui::MainWindow window;
    const auto* command = window.hotkey_registry().find_command(id);
    CHECK(command != nullptr);
    CHECK(command->action->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F10));
    CHECK(command->default_shortcuts == QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F9)});
  }
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("hotkeys"));
    settings.sync();
  }
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
}

// The command id follows the relative path: a shadow override of a bundled script keeps
// the bundled id (and the override's @hotkey becomes the default), a renamed file gets a
// new id, a deleted one unregisters, and a recreated one registers again.
void ui_script_hotkey_ids_follow_relative_path() {
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("hotkeys"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const auto rescan = [&window] {
    window.refresh_script_commands(patchy::ui::scan_scripts(patchy::ui::MainWindow::bundled_scripts_directory(),
                                                            patchy::ui::MainWindow::user_scripts_directory()));
  };
  const auto pong_id = patchy::ui::script_hotkey_command_id(QStringLiteral("Games/pong.js"));
  const auto* pong = window.hotkey_registry().find_command(pong_id);
  CHECK(pong != nullptr);
  CHECK(pong->default_shortcuts.isEmpty());  // bundled scripts never ship a shortcut
  const auto commands_before = window.hotkey_registry().commands().size();

  // Shadow override: same id, the override's directive is the default, "(modified)" text.
  const auto override_path = write_user_script(
      QStringLiteral("Games/pong.js"), QStringLiteral("// @name Pong\n// @hotkey Ctrl+Alt+F11\nconsole.log('override');\n"));
  rescan();
  CHECK(window.hotkey_registry().commands().size() == commands_before);
  pong = window.hotkey_registry().find_command(pong_id);
  CHECK(pong != nullptr);
  CHECK(pong->default_shortcuts == QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F11)});
  CHECK(pong->action->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F11));
  CHECK(pong->action->text() == QStringLiteral("Pong (modified)"));
  CHECK(QFile::remove(override_path));
  rescan();
  pong = window.hotkey_registry().find_command(pong_id);
  CHECK(pong != nullptr);
  CHECK(pong->default_shortcuts.isEmpty());
  CHECK(pong->action->shortcut().isEmpty());
  CHECK(pong->action->text() == QStringLiteral("Pong"));

  // A user script registers, follows a rename, unregisters when deleted, and re-registers.
  const auto first = write_user_script(QStringLiteral("a.js"), QStringLiteral("// @hotkey Ctrl+Alt+F12\n"));
  rescan();
  const auto a_id = patchy::ui::script_hotkey_command_id(QStringLiteral("a.js"));
  const auto b_id = patchy::ui::script_hotkey_command_id(QStringLiteral("b.js"));
  CHECK(window.hotkey_registry().find_command(a_id) != nullptr);
  CHECK(window.hotkey_registry().find_command(b_id) == nullptr);
  const auto second = QDir(patchy::ui::MainWindow::user_scripts_directory()).absoluteFilePath(QStringLiteral("b.js"));
  CHECK(QFile::rename(first, second));
  rescan();
  CHECK(window.hotkey_registry().find_command(a_id) == nullptr);
  const auto* b = window.hotkey_registry().find_command(b_id);
  CHECK(b != nullptr);
  CHECK(b->action->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F12));
  CHECK(QFile::remove(second));
  rescan();
  CHECK(window.hotkey_registry().find_command(b_id) == nullptr);
  write_user_script(QStringLiteral("b.js"), QStringLiteral("console.log('back');\n"));
  rescan();  // no duplicate-id logic_error: the old command was unregistered
  CHECK(window.hotkey_registry().find_command(b_id) != nullptr);
  CHECK(window.hotkey_registry().commands().size() == commands_before + 1);
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
  rescan();
  CHECK(window.hotkey_registry().commands().size() == commands_before);
}

void ui_scripts_menu_lists_bundled_scripts() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* menu = window.findChild<QMenu*>(QStringLiteral("fileScriptsMenu"));
  CHECK(menu != nullptr);
  // aboutToShow triggers the folder rescan.
  menu->popup(QPoint(0, 0));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  bool editor_entry = false;
  QMenu* games_menu = nullptr;
  for (auto* action : menu->actions()) {
    if (action->objectName() == QStringLiteral("fileScriptEditorAction")) {
      editor_entry = true;
    }
    if (action->menu() != nullptr && action->text() == QStringLiteral("Games")) {
      games_menu = action->menu();
    }
  }
  CHECK(editor_entry);
  // The bundled scripts are staged next to the test binary by CMake; folders
  // (Games/Demos/Effects/Utilities) become submenus. Entries show their @name
  // display name and carry an icon (the sidecar PNG or the generic fallback).
  CHECK(games_menu != nullptr);
  bool pong_entry = false;
  const auto* pong_command =
      window.hotkey_registry().find_command(patchy::ui::script_hotkey_command_id(QStringLiteral("Games/pong.js")));
  CHECK(pong_command != nullptr);
  for (const auto* action : games_menu->actions()) {
    if (action->text() == QStringLiteral("Pong")) {
      pong_entry = true;
      CHECK(!action->icon().isNull());
      // The entry is the persistent hotkey command action, so a bound shortcut shows here.
      CHECK(action == pong_command->action.data());
      CHECK(action->menuRole() == QAction::NoRole);
    }
  }
  menu->close();
  CHECK(pong_entry);
}

// Right-clicking a File > Scripts entry opens the Script Manager's script actions for it.
void ui_scripts_menu_context_menu_offers_script_actions() {
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
  write_user_script(QStringLiteral("Mine/context.js"),
                    QStringLiteral("// @name Context Script\nconsole.log('context script ran');\n"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* menu = window.findChild<QMenu*>(QStringLiteral("fileScriptsMenu"));
  CHECK(menu != nullptr);
  menu->popup(QPoint(0, 0));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  QMenu* utilities_menu = nullptr;
  for (auto* action : menu->actions()) {
    if (action->menu() != nullptr && action->text() == QStringLiteral("Mine")) {
      utilities_menu = action->menu();
    }
  }
  CHECK(utilities_menu != nullptr);
  QAction* quick_export = nullptr;
  for (auto* action : utilities_menu->actions()) {
    if (action->text() == QStringLiteral("Context Script")) {
      quick_export = action;
    }
  }
  CHECK(quick_export != nullptr);
  utilities_menu->popup(window.mapToGlobal(QPoint(120, 120)));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

  bool saw_context_menu = false;
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* context_menu = qobject_cast<QMenu*>(widget);
      if (context_menu == nullptr || context_menu->objectName() != QStringLiteral("scriptMenuContextMenu")) {
        continue;
      }
      QStringList names;
      for (const auto* action : context_menu->actions()) {
        if (!action->isSeparator()) {
          names << action->objectName();
        }
      }
      // A script that shadows nothing offers everything but Revert to Bundled.
      const QStringList expected{QStringLiteral("scriptMenuRunAction"),    QStringLiteral("scriptMenuEditAction"),
                                 QStringLiteral("scriptMenuRevealAction"), QStringLiteral("scriptMenuCliAction"),
                                 QStringLiteral("scriptMenuHotkeyAction"), QStringLiteral("scriptMenuIconAction")};
      CHECK(names == expected);
      save_widget_artifact("scripts_menu_context_menu", *context_menu);
      // Run goes through the same path as a left click.
      context_menu->actions().first()->trigger();
      context_menu->close();
      saw_context_menu = true;
      return;
    }
    CHECK(false);
  });
  const auto point = utilities_menu->actionGeometry(quick_export).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, point, utilities_menu->mapToGlobal(point));
  QApplication::sendEvent(utilities_menu, &context_event);
  QApplication::processEvents();
  CHECK(saw_context_menu);
  wait_for_run_end(window.script_engine_host());
  CHECK(backlog_contains(window, QStringLiteral("context script ran")));

  // The right button never triggers the entry itself (a QMenu would run it on release).
  utilities_menu->popup(window.mapToGlobal(QPoint(120, 120)));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  bool triggered = false;
  const auto connection = QObject::connect(quick_export, &QAction::triggered, [&triggered] { triggered = true; });
  QTimer::singleShot(0, [&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      if (auto* context_menu = qobject_cast<QMenu*>(widget);
          context_menu != nullptr && context_menu->objectName() == QStringLiteral("scriptMenuContextMenu")) {
        context_menu->close();
      }
    }
  });
  QTest::mouseClick(utilities_menu, Qt::RightButton, Qt::NoModifier, point);
  QApplication::processEvents();
  QObject::disconnect(connection);
  CHECK(!triggered);
  utilities_menu->close();
  menu->close();
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
}

// patchy.scripts: an agent writes a script into the user folder and puts it on a key in
// one call; the entry reports what the menu and Preferences now show.
void ui_script_library_installs_and_binds_hotkey() {
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("hotkeys"));
    settings.sync();
  }
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(R"JS(
    var before = patchy.scripts.list();
    if (!before.some(function (e) { return e.relativePath === 'Games/pong.js' && e.bundled && e.commandId === 'script.Games%2Fpong.js'; })) {
      throw Error('bundled pong missing from list');
    }
    if (patchy.scripts.userFolder.indexOf('\\') >= 0) throw Error('userFolder must use / separators');
    var entry = patchy.scripts.install('Mine/agent made.js',
      "// @name Agent Made\n// @hotkey Ctrl+Alt+F7\nconsole.log('agent made ran');\n",
      {hotkey: 'Ctrl+Alt+F8'});
    if (entry.name !== 'Agent Made' || entry.bundled || entry.modified) throw Error('entry ' + JSON.stringify(entry));
    if (entry.defaultHotkey !== 'Ctrl+Alt+F7') throw Error('default ' + entry.defaultHotkey);
    if (entry.hotkey !== 'Ctrl+Alt+F8') throw Error('override ' + entry.hotkey);
    if (entry.commandId !== 'script.Mine%2Fagent%20made.js') throw Error('id ' + entry.commandId);
    if (!patchy.io.fileExists(patchy.scripts.userFolder + '/Mine/agent made.js')) throw Error('file missing');
    if (patchy.scripts.getHotkey('Mine/agent made.js') !== 'Ctrl+Alt+F8') throw Error('getHotkey');
    // Clearing the override restores the @hotkey default.
    if (patchy.scripts.setHotkey('Mine/agent made.js', '') !== 'Ctrl+Alt+F7') throw Error('clear');
    var bad = [['../escape.js', 'x'], ['notes.txt', 'x'], ['/abs.js', 'x'], ['C:/abs.js', 'x'], ['Mine/../../up.js', 'x']];
    bad.forEach(function (b) {
      var threw = false;
      try { patchy.scripts.install(b[0], b[1]); } catch (e) { threw = true; }
      if (!threw) throw Error('accepted ' + b[0]);
    });
    var threw = false;
    try { patchy.scripts.setHotkey('Mine/agent made.js', 'Return'); } catch (e) { threw = true; }
    if (!threw) throw Error('accepted a reserved key');
    threw = false;
    try { patchy.scripts.setHotkey('Mine/nope.js', 'Ctrl+Alt+F6'); } catch (e) { threw = true; }
    if (!threw) throw Error('bound a missing script');
    patchy.scripts.setHotkey('Mine/agent made.js', 'Ctrl+Alt+F9');
  )JS")));
  const auto id = patchy::ui::script_hotkey_command_id(QStringLiteral("Mine/agent made.js"));
  const auto* command = window.hotkey_registry().find_command(id);
  CHECK(command != nullptr);
  CHECK(command->action->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F9));
  CHECK(command->default_shortcuts == QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F7)});
  {
    auto settings = patchy::ui::app_settings();
    CHECK(settings.value(QStringLiteral("hotkeys/") + id).toString() == QStringLiteral("Ctrl+Alt+F9"));
  }
  // The bound action runs the installed script.
  command->action->trigger();
  wait_for_run_end(window.script_engine_host());
  CHECK(backlog_contains(window, QStringLiteral("agent made ran")));
  {
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("hotkeys"));
    settings.sync();
  }
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
}

void ui_script_editor_tree_shadow_override() {
  const StandardPathsTestMode test_paths;
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* save_button = dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorSaveButton"));
  auto* refresh_button =
      dialog.findChild<QPushButton*>(QStringLiteral("scriptEditorRefreshButton"));
  CHECK(tree != nullptr && code != nullptr && save_button != nullptr && refresh_button != nullptr);

  // The Bundled root is expanded by default and mirrors the folder structure.
  QTreeWidgetItem* bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  CHECK(bundled_root->isExpanded());
  auto* games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  // Scripts show their "@name" display name, sidecar icon, and @window flag.
  auto* breakout_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(breakout_item != nullptr);
  CHECK(!breakout_item->icon(0).isNull());
  constexpr int kFileNameRole = Qt::UserRole + 3;
  constexpr int kWindowRole = Qt::UserRole + 5;
  CHECK(breakout_item->data(0, kFileNameRole).toString() == QStringLiteral("breakout.js"));
  CHECK(breakout_item->data(0, kWindowRole).toBool());
  save_widget_artifact("script_manager_tree", *tree);

  // Folder rows (roots included) carry their on-disk path for the context
  // menu's Show in Folder.
  constexpr int kFolderPathRole = Qt::UserRole + 2;
  CHECK(bundled_root->data(0, kFolderPathRole).toString() ==
        patchy::ui::MainWindow::bundled_scripts_directory());
  CHECK(games_folder->data(0, kFolderPathRole).toString().endsWith(QStringLiteral("/Games")));

  // Selecting a script loads it (single click); the itemActivated emit also
  // exercises the explicit activation path, which stays for switching away
  // from a dirty editor. Emitted directly rather than synthesizing a
  // key/click: the gesture that raises it is platform-styled (Return does not
  // fire it on the mac offscreen platform).
  tree->setCurrentItem(breakout_item);
  QMetaObject::invokeMethod(tree, "itemActivated", Qt::DirectConnection,
                            Q_ARG(QTreeWidgetItem*, breakout_item), Q_ARG(int, 0));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(code->toPlainText().contains(QStringLiteral("Breakout")));

  // Save writes the user-folder shadow copy, and the tree keeps the entry in
  // place, now carrying the bundled-original path (what the delegate renders
  // as the amber "modified" tag).
  save_button->click();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  const auto override_path = QDir(patchy::ui::MainWindow::user_scripts_directory())
                                 .absoluteFilePath(QStringLiteral("Games/breakout.js"));
  CHECK(QFile::exists(override_path));
  bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  constexpr int kPathRole = Qt::UserRole;
  constexpr int kBundledPathRole = Qt::UserRole + 1;
  auto* override_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(override_item != nullptr);
  CHECK(override_item->data(0, kPathRole).toString() == override_path);
  CHECK(!override_item->data(0, kBundledPathRole).toString().isEmpty());
  // The override does NOT show under My Scripts (it replaces the bundled row).
  QTreeWidgetItem* user_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("My Scripts")) {
      user_root = tree->topLevelItem(i);
    }
  }
  CHECK(user_root != nullptr);
  CHECK(find_child_item(user_root, QStringLiteral("Games")) == nullptr);

  // Removing the copy (what Revert to Bundled does) restores the plain entry.
  CHECK(QFile::remove(override_path));
  refresh_button->click();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  auto* restored_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(restored_item != nullptr);
  CHECK(restored_item->data(0, kBundledPathRole).toString().isEmpty());
  dialog.close();
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
}

// A single click (tree selection) loads the clicked script into the editor;
// once the editor holds unsaved edits, selection changes leave them alone (no
// load, no prompt) - only activation switches, behind the discard prompt.
void ui_script_manager_single_click_loads_and_preserves_edits() {
  const StandardPathsTestMode test_paths;
  remove_test_scratch_dir(patchy::ui::MainWindow::user_scripts_directory());
  patchy::ui::MainWindow window;
  show_window(window);
  patchy::ui::ScriptEditorDialog dialog(window, window.script_engine_host());
  dialog.show();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("scriptEditorTree"));
  auto* code = dialog.findChild<QPlainTextEdit*>(QStringLiteral("scriptEditorCode"));
  auto* file_label = dialog.findChild<QLabel*>(QStringLiteral("scriptEditorFileLabel"));
  CHECK(tree != nullptr && code != nullptr && file_label != nullptr);
  QTreeWidgetItem* bundled_root = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == QStringLiteral("Bundled")) {
      bundled_root = tree->topLevelItem(i);
    }
  }
  CHECK(bundled_root != nullptr);
  auto* games_folder = find_child_item(bundled_root, QStringLiteral("Games"));
  CHECK(games_folder != nullptr);
  auto* breakout_item = find_child_item(games_folder, QStringLiteral("Breakout"));
  CHECK(breakout_item != nullptr);
  auto* utilities_folder = find_child_item(bundled_root, QStringLiteral("Utilities"));
  CHECK(utilities_folder != nullptr);
  auto* batch_export_item = find_child_item(utilities_folder, QStringLiteral("Batch Export"));
  CHECK(batch_export_item != nullptr);

  // Selection alone (what a single click or an arrow-key step raises) loads
  // the script; no itemActivated needed.
  tree->setCurrentItem(breakout_item);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(code->toPlainText().contains(QStringLiteral("Breakout")));
  CHECK(file_label->text() == QStringLiteral("breakout.js"));

  // Unsaved edits pin the editor: selecting another script neither loads it
  // nor prompts (the discard prompt lives on the activation path only).
  code->textCursor().insertText(QStringLiteral("// dirty-edit-marker\n"));
  CHECK(code->document()->isModified());
  tree->setCurrentItem(batch_export_item);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  CHECK(code->toPlainText().contains(QStringLiteral("dirty-edit-marker")));
  CHECK(code->toPlainText().contains(QStringLiteral("Breakout")));
  CHECK(file_label->text() == QStringLiteral("breakout.js *"));
  dialog.close();
}

}  // namespace

std::vector<patchy::test::TestCase> scripting_tests_part2() {
  return {
      {"ui_text_name_table_names_resolve_to_the_registered_face", ui_text_name_table_names_resolve_to_the_registered_face},
      {"ui_text_windows_named_font_data_drops_macintosh_records_if_available", ui_text_windows_named_font_data_drops_macintosh_records_if_available},
      {"ui_script_text_runs_create_and_read_back", ui_script_text_runs_create_and_read_back},
      {"ui_script_text_box_wraps_and_aligns", ui_script_text_box_wraps_and_aligns},
      {"ui_text_baseline_shift_moves_individual_runs", ui_text_baseline_shift_moves_individual_runs},
      {"ui_script_set_text_runs_edits_existing_layer", ui_script_set_text_runs_edits_existing_layer},
      {"ui_script_rerender_text_replaces_stored_pixels", ui_script_rerender_text_replaces_stored_pixels},
      {"ui_script_text_paragraph_reads_and_sets_metrics", ui_script_text_paragraph_reads_and_sets_metrics},
      {"ui_script_text_auto_leading_ignores_spacer_paragraphs", ui_script_text_auto_leading_ignores_spacer_paragraphs},
      {"ui_script_list_fonts_reports_registered_families", ui_script_list_fonts_reports_registered_families},
      {"ui_script_display_info_reports_screens", ui_script_display_info_reports_screens},
      {"ui_script_cmyk_conversions_use_working_space", ui_script_cmyk_conversions_use_working_space},
      {"ui_cmyk_proof_mode_saves_cmyk", ui_cmyk_proof_mode_saves_cmyk},
      {"ui_cmyk_files_open_in_cmyk_proof", ui_cmyk_files_open_in_cmyk_proof},
      {"ui_cmyk_psd_opens_in_cmyk_proof", ui_cmyk_psd_opens_in_cmyk_proof},
      {"ui_cmyk_proof_saves_layered_psd_in_cmyk", ui_cmyk_proof_saves_layered_psd_in_cmyk},
      {"ui_cmyk_open_explains_cmyk_proof_emulation", ui_cmyk_open_explains_cmyk_proof_emulation},
      {"ui_issue_220_photoshop_cmyk_psd_opens_in_cmyk_proof_if_available",
       ui_issue_220_photoshop_cmyk_psd_opens_in_cmyk_proof_if_available},
      {"ui_script_export_cmyk_writes_jpeg_tiff_and_pdf", ui_script_export_cmyk_writes_jpeg_tiff_and_pdf},
      {"ui_proof_colors_and_gamut_warning_change_only_the_view", ui_proof_colors_and_gamut_warning_change_only_the_view},
      {"ui_proof_colors_follow_brush_strokes_and_move_drags", ui_proof_colors_follow_brush_strokes_and_move_drags},
      {"ui_script_text_font_without_glyphs_warns_with_the_real_cause", ui_script_text_font_without_glyphs_warns_with_the_real_cause},
      {"ui_script_text_setters_warn_about_fonts", ui_script_text_setters_warn_about_fonts},
      {"ui_script_text_layer_with_uncovered_script_does_not_crash", ui_script_text_layer_with_uncovered_script_does_not_crash},
      {"ui_script_run_command_writes_output_file", ui_script_run_command_writes_output_file},
      {"ui_script_editor_dialog_runs_and_shows_console", ui_script_editor_dialog_runs_and_shows_console},
      {"ui_script_editor_status_shows_running_and_ready", ui_script_editor_status_shows_running_and_ready},
      {"ui_script_canvas_window_renders_frames", ui_script_canvas_window_renders_frames},
      {"ui_script_save_as_options_write_dds_and_jpeg", ui_script_save_as_options_write_dds_and_jpeg},
      {"ui_script_save_as_options_write_jxl", ui_script_save_as_options_write_jxl},
      {"ui_script_save_as_options_write_avif", ui_script_save_as_options_write_avif},
      {"ui_script_save_as_options_are_strict", ui_script_save_as_options_are_strict},
      {"ui_script_export_as_jpeg_coding_options", ui_script_export_as_jpeg_coding_options},
      {"ui_script_hotkey_runs_user_script", ui_script_hotkey_runs_user_script},
      {"ui_script_hotkey_ids_follow_relative_path", ui_script_hotkey_ids_follow_relative_path},
      {"ui_scripts_menu_lists_bundled_scripts", ui_scripts_menu_lists_bundled_scripts},
      {"ui_scripts_menu_context_menu_offers_script_actions", ui_scripts_menu_context_menu_offers_script_actions},
      {"ui_script_library_installs_and_binds_hotkey", ui_script_library_installs_and_binds_hotkey},
      {"ui_script_editor_tree_shadow_override", ui_script_editor_tree_shadow_override},
      {"ui_script_manager_single_click_loads_and_preserves_edits", ui_script_manager_single_click_loads_and_preserves_edits},
  };
}
