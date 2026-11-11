// GitHub issue 683: Arabic PSD text. Photoshop's paragraph direction and alignment on open, the
// Photoshop layout model kept through a Patchy save, and re-editing a Patchy box whose lines
// overflow it. See docs/text-render-calibration-vertical-rtl.md ("Paragraph direction") and
// docs/text-render-calibration-patchy-authored.md.
#include "core/layer_metadata.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "ui/alpha_row_bands.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "ui/text_document.hpp"
#include "ui/text_layout.hpp"
#include "local_psd_fixtures.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QMessageBox>
#include <QPushButton>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;

bool run_script_to_end(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("rtl-psd-test-script");
  // Hosts without the fixtures' fonts (Arial on Linux) ask before editing the PSD's text; the
  // prompt opens inside the run, so watch for it there and continue with the stand-in font.
  QTimer missing_font_watcher;
  missing_font_watcher.setInterval(50);
  QObject::connect(&missing_font_watcher, &QTimer::timeout, [] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("missingPsdTextFontMessageBox")));
    if (dialog == nullptr || !dialog->isVisible()) {
      return;
    }
    for (auto* button : dialog->findChildren<QPushButton*>()) {
      if (dialog->buttonRole(button) == QMessageBox::AcceptRole) {
        button->click();
        return;
      }
    }
  });
  missing_font_watcher.start();
  (void)host.run_source(source, std::move(options));
  QElapsedTimer timer;
  timer.start();
  while (host.run_active() && timer.elapsed() < 30000) {
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  }
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
  CHECK(!host.run_active());
  return !host.last_run_had_error();
}

const patchy::Layer* only_text_layer(const patchy::Document& document) {
  const patchy::Layer* found = nullptr;
  for (const auto& layer : document.layers()) {
    if (patchy::layer_is_text(layer)) {
      found = &layer;
    }
  }
  return found;
}

std::string layout_mode(const patchy::Layer& layer) {
  const auto found = layer.metadata().find(patchy::kLayerMetadataTextLayoutMode);
  return found != layer.metadata().end() ? found->second : std::string{};
}

// Each text line's ink extent: the row band and the leftmost and rightmost visible column in it.
struct InkLine {
  AlphaRowBand band;
  int left{0};
  int right{0};
};

std::vector<InkLine> ink_lines(const patchy::PixelBuffer& pixels) {
  std::vector<InkLine> lines;
  for (const auto& band : alpha_row_bands(pixels)) {
    if (const auto bounds = alpha_pixel_bounds_in_rows(pixels, band.top, band.bottom); bounds.has_value()) {
      lines.push_back(InkLine{band, bounds->left(), bounds->right()});
    }
  }
  return lines;
}

// Writes a PSD into test-artifacts for the Photoshop open check (docs/txt2.md acceptance).
void write_psd_artifact(const patchy::Document& document, const char* name) {
  std::filesystem::create_directories("test-artifacts");
  const auto bytes = patchy::psd::DocumentIo::write_layered_rgb8(document);
  std::ofstream out(std::string("test-artifacts/") + name, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void print_lines(const char* label, const std::vector<InkLine>& lines) {
  for (const auto& line : lines) {
    std::printf("  %s rows %d-%d cols %d-%d\n", label, line.band.top, line.band.bottom, line.left, line.right);
  }
  std::fflush(stdout);
}

// A Photoshop box layer re-edited in Patchy keeps Photoshop's auto leading (1.2 x size) after a
// Patchy save and reopen: the save marks it as Photoshop layout, so the next edit does not fall
// back to Qt's natural line pitch and move every line.
void ui_issue_683_photoshop_leading_survives_patchy_save_and_reedit() {
  register_test_fonts(TestFontRole::UiDefault);
  auto document = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-text-box-auto-leading.psd"));
  const auto* source = only_text_layer(document);
  CHECK(source != nullptr);
  if (source == nullptr) {
    return;
  }
  CHECK(layout_mode(*source) == patchy::kTextLayoutModePhotoshop);
  const auto layer_id = source->id();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Leading 683"));
  QApplication::processEvents();
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    for (const layer of app.activeDocument.layers) {
      if (layer.isText) layer.text = 'Line one\nLine two\nLine three\nLine four\nLine five';
    }
  )JS")));
  const auto& first_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* edited = std::as_const(first_document).find_layer(layer_id);
  CHECK(edited != nullptr);
  if (edited == nullptr) {
    return;
  }
  CHECK(layout_mode(*edited) == patchy::kTextLayoutModePhotoshop);
  const auto before = ink_lines(edited->pixels());
  print_lines("first edit", before);
  CHECK(before.size() == 5U);

  auto reopened = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(first_document));
  const auto* reopened_layer = only_text_layer(reopened);
  CHECK(reopened_layer != nullptr);
  if (reopened_layer == nullptr) {
    return;
  }
  const bool reopened_in_photoshop_layout = layout_mode(*reopened_layer) == patchy::kTextLayoutModePhotoshop;
  const auto reopened_id = reopened_layer->id();
  window.add_document_session(std::move(reopened), QStringLiteral("Leading 683 reopened"));
  QApplication::processEvents();
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    for (const layer of app.activeDocument.layers) {
      if (layer.isText) layer.text = layer.text;
    }
  )JS")));
  const auto* reedited = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(reopened_id);
  CHECK(reedited != nullptr);
  if (reedited == nullptr) {
    return;
  }
  const auto after = ink_lines(reedited->pixels());
  print_lines("after reopen", after);
  write_psd_artifact(patchy::ui::MainWindowTestAccess::document(window), "issue_683_leading_patchy.psd");
  CHECK(after.size() == before.size());
  for (std::size_t index = 1; index < std::min(before.size(), after.size()); ++index) {
    // Line pitch, measured from the first line so a whole-raster offset does not count.
    const auto before_pitch = before[index].band.top - before.front().band.top;
    const auto after_pitch = after[index].band.top - after.front().band.top;
    CHECK(std::abs(before_pitch - after_pitch) <= 1);
  }
  CHECK(reopened_in_photoshop_layout);
}

// A Qt-natural box whose story overflows it, re-edited after a PSD save and reopen, draws the same
// glyphs. The reporter's path: a Photoshop box saved by a Patchy without the layout mark reopens
// Qt-natural, its first edit overflows the box (Qt's taller line pitch), and every later edit
// session's advance calibration matched the whole story against the clipped raster, read the
// hidden lines as a too-wide font and squeezed the text.
void ui_issue_683_overflowing_box_text_reedit_keeps_its_glyphs() {
  register_test_fonts(TestFontRole::UiDefault);
  auto imported = patchy::psd::DocumentIo::read_file(
      patchy::test::committed_psd_fixture_path("photoshop-text-box-auto-leading.psd"));
  for (auto& layer : imported.layers()) {
    if (patchy::layer_is_text(layer)) {
      layer.metadata().erase(patchy::kLayerMetadataTextLayoutMode);
      layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
    }
  }
  auto document = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(imported));
  const auto* source = only_text_layer(document);
  CHECK(source != nullptr);
  if (source == nullptr) {
    return;
  }
  CHECK(layout_mode(*source) != patchy::kTextLayoutModePhotoshop);
  const auto layer_id = source->id();

  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Overflow 683"));
  QApplication::processEvents();
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    for (const layer of app.activeDocument.layers) {
      if (layer.isText) {
        layer.text = 'alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron ' +
                     'pi rho sigma tau upsilon phi chi psi omega one two three four five six seven';
      }
    }
  )JS")));
  const auto& first_document = patchy::ui::MainWindowTestAccess::document(window);
  const auto* edited = std::as_const(first_document).find_layer(layer_id);
  CHECK(edited != nullptr);
  if (edited == nullptr) {
    return;
  }
  const auto before = ink_lines(edited->pixels());
  print_lines("first edit", before);
  CHECK(before.size() >= 3U);

  auto reopened = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(first_document));
  const auto* reopened_layer = only_text_layer(reopened);
  CHECK(reopened_layer != nullptr);
  if (reopened_layer == nullptr) {
    return;
  }
  const auto reopened_id = reopened_layer->id();
  window.add_document_session(std::move(reopened), QStringLiteral("Overflow 683 reopened"));
  QApplication::processEvents();
  for (int edit = 0; edit < 2; ++edit) {
    CHECK(run_script_to_end(window, QStringLiteral(R"JS(
      for (const layer of app.activeDocument.layers) {
        if (layer.isText) layer.text = layer.text;
      }
    )JS")));
    const auto* reedited = std::as_const(patchy::ui::MainWindowTestAccess::document(window)).find_layer(reopened_id);
    CHECK(reedited != nullptr);
    if (reedited == nullptr) {
      return;
    }
    const auto after = ink_lines(reedited->pixels());
    print_lines("re-edit", after);
    CHECK(after.size() == before.size());
    for (std::size_t index = 0; index < std::min(before.size(), after.size()); ++index) {
      // Rows from the first line (a whole-raster offset does not count) and each line's ink width.
      CHECK(std::abs((before[index].band.top - before.front().band.top) -
                     (after[index].band.top - after.front().band.top)) <= 1);
      CHECK(std::abs((before[index].band.bottom - before[index].band.top) -
                     (after[index].band.bottom - after[index].band.top)) <= 1);
      CHECK(std::abs((before[index].right - before[index].left) - (after[index].right - after[index].left)) <= 2);
    }
  }
}

// The reporter's Photoshop file: three right-aligned Arabic boxes with no direction set. Patchy
// opens them left to right, as Photoshop does, so a re-render keeps every line flush right (auto
// direction resolved them right to left and moved the text to the left edge).
void ui_issue_683_arabic_psd_rerenders_right_aligned_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("issue-683/arabic-photoshop.psd");
  patchy::test::skip_unless_exists(path);
  register_test_fonts(TestFontRole::UiDefault);
  auto document = patchy::psd::DocumentIo::read_file(path);
  std::vector<patchy::LayerId> ids;
  for (auto& layer : document.layers()) {
    if (patchy::layer_is_text(layer)) {
      ids.push_back(layer.id());
      // Amiri Quran and Adobe Arabic are not on every host: a Photoshop raster preview would ask
      // before substituting them, which blocks the script.
      layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
    }
  }
  CHECK(ids.size() == 3U);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Arabic 683"));
  QApplication::processEvents();
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    for (const layer of app.activeDocument.layers) {
      if (layer.isText) {
        layer.rerenderText();
        console.log('683 ' + layer.name + ' ' + layer.textDirection + ' ' + layer.textAlign);
      }
    }
  )JS")));
  const auto& live = patchy::ui::MainWindowTestAccess::document(window);
  save_image_artifact("issue_683_arabic_patchy", image_from_pixels_for_visuals(patchy::Compositor{}.flatten_rgb8(live)));
  for (const auto id : ids) {
    const auto* layer = std::as_const(live).find_layer(id);
    CHECK(layer != nullptr);
    if (layer == nullptr) {
      continue;
    }
    // Left to right and right aligned, as Photoshop reads the file (paragraph runs columns 9 and 2).
    const auto runs = QString::fromStdString(layer->metadata().at(patchy::kLayerMetadataTextParagraphRuns));
    const auto first_run = runs.split(QLatin1Char('\n')).value(1).split(QLatin1Char('\t'));
    CHECK(first_run.value(2) == QStringLiteral("right"));
    CHECK(first_run.value(9) == QStringLiteral("ltr"));
    // Flush right: many pixel rows end at the text's right edge and few start at its left edge
    // (auto direction drew them flush left: the reverse, about 60 rows against 25 on the Noto
    // layer). Counted per row, not per line: Arabic marks join lines into one row band, and the
    // line breaks depend on the fonts the host has.
    const auto& pixels = layer->pixels();
    std::vector<QRect> rows;
    for (int y = 0; y < pixels.height(); ++y) {
      if (const auto row = alpha_pixel_bounds_in_rows(pixels, y, y + 1); row.has_value() && row->width() >= 3) {
        rows.push_back(*row);
      }
    }
    CHECK(rows.size() >= 20U);
    if (rows.empty()) {
      continue;
    }
    int left_edge = rows.front().left();
    int right_edge = rows.front().right();
    for (const auto& row : rows) {
      left_edge = std::min(left_edge, row.left());
      right_edge = std::max(right_edge, row.right());
    }
    const auto near_left = std::count_if(rows.begin(), rows.end(), [&](const QRect& row) { return row.left() <= left_edge + 4; });
    const auto near_right =
        std::count_if(rows.begin(), rows.end(), [&](const QRect& row) { return row.right() >= right_edge - 4; });
    std::printf("  %s: %d rows, %d at the left edge, %d at the right edge\n", layer->name().c_str(),
                static_cast<int>(rows.size()), static_cast<int>(near_left), static_cast<int>(near_right));
    std::fflush(stdout);
    CHECK(near_right >= 2 * near_left);
  }
  // Saved by Patchy with an auto Arabic paragraph of its own, which the save writes right to
  // left: the Photoshop open check reads that file.
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    app.activeDocument.addTextLayer('\u0645\u0631\u062d\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645',
        {size: 20, x: 20, y: 460, box: {width: 360, height: 36}, align: 'right'});
  )JS")));
  write_psd_artifact(live, "issue_683_arabic_patchy.psd");
}

// The first of `preferred` the font database has, else any family of the writing system.
QString family_for_writing_system(QFontDatabase::WritingSystem system, const QStringList& preferred) {
  const auto families = QFontDatabase::families(system);
  for (const auto& family : preferred) {
    if (families.contains(family)) {
      return family;
    }
  }
  return families.value(0);
}

// The empty column runs inside a row band, between its first and last inked column.
std::vector<int> ink_gaps(const patchy::PixelBuffer& pixels, const InkLine& line) {
  std::vector<int> gaps;
  const auto channels = pixels.format().channels;
  if (channels != 1U && channels < 4U) {
    return gaps;
  }
  const auto alpha_channel = channels == 1U ? 0U : 3U;
  const auto stride = pixels.stride_bytes();
  const auto bytes = pixels.data();
  int run = 0;
  for (int x = std::max(0, line.left); x <= std::min(pixels.width() - 1, line.right); ++x) {
    bool inked = false;
    for (int y = std::max(0, line.band.top); y < std::min(pixels.height(), line.band.bottom) && !inked; ++y) {
      const auto offset = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * channels + alpha_channel;
      inked = offset < bytes.size() && bytes[offset] >= 12;
    }
    if (inked) {
      if (run > 0) {
        gaps.push_back(run);
      }
      run = 0;
    } else {
      ++run;
    }
  }
  return gaps;
}

// GitHub issue 683: justified lines of a right-to-left paragraph reach both edges of the box.
// Qt ends such a line one trailing space short of the right edge (about 6 px at this size);
// Patchy now spreads the line over its word spaces itself. Under Justify All the last line fills
// the box too. The sample has no letter that reaches below the baseline (qof, the final forms):
// on Linux the lines of a sample with qof left no blank pixel row between them, and the row scan
// that tells the lines apart read two lines as one.
void ui_rtl_justified_text_fills_the_box_to_both_edges() {
  register_test_fonts(TestFontRole::UiDefault);
  const auto family = family_for_writing_system(
      QFontDatabase::Hebrew, {QStringLiteral("Arial"), QStringLiteral("Liberation Sans"), QStringLiteral("DejaVu Sans")});
  if (family.isEmpty()) {
    patchy::test::skip("no Hebrew-capable font available on this machine");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(patchy::Document(420, 420, patchy::PixelFormat::rgb8()), QStringLiteral("RTL justify 683"));
  QApplication::processEvents();
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    const words = 'זהו כתב עברי לדוגמה ' +
        'של יישור מלא בתיבה עם ' +
        'כמה שורות ועם מילים ' +
        'זעירות וארוכות בכל שורה ' +
        'כדי לראות את הגבולות שלה.';
    const doc = app.activeDocument;
    doc.addTextLayer(words, {font: '%1', size: 20, x: 60, y: 20, box: {width: 300, height: 170},
                             align: 'justify', direction: 'rtl'}).name = 'justify';
    doc.addTextLayer(words, {font: '%1', size: 20, x: 60, y: 220, box: {width: 300, height: 170},
                             align: 'justifyAll', direction: 'rtl'}).name = 'justifyAll';
  )JS").arg(family)));
  const auto& live = patchy::ui::MainWindowTestAccess::document(window);
  save_image_artifact("issue_683_rtl_justify", image_from_pixels_for_visuals(patchy::Compositor{}.flatten_rgb8(live)));
  constexpr int kBoxLeft = 60;
  constexpr int kBoxRight = 359;  // the box's last column
  constexpr int kTolerance = 3;   // side bearings and antialiasing
  int checked = 0;
  for (const auto& layer : live.layers()) {
    if (!patchy::layer_is_text(layer)) {
      continue;
    }
    const bool justify_all = layer.name() == "justifyAll";
    const auto lines = ink_lines(layer.pixels());
    print_lines(layer.name().c_str(), lines);
    CHECK(lines.size() >= 3U);
    for (std::size_t index = 0; index < lines.size(); ++index) {
      const auto left = layer.bounds().x + lines[index].left;
      const auto right = layer.bounds().x + lines[index].right;
      const bool last = index + 1 == lines.size();
      // Every line starts at the right edge; every justified line also ends at the left edge.
      CHECK(std::abs(right - kBoxRight) <= kTolerance);
      if (!last || justify_all) {
        CHECK(std::abs(left - kBoxLeft) <= kTolerance);
      } else {
        CHECK(left > kBoxLeft + 20);  // Justify leaves the last line at its natural width
      }
    }
    ++checked;
  }
  CHECK(checked == 2);
}

// GitHub issue 683: Justify All's last line in Arabic. Qt gives a cursive script its slack
// between letters, which pulled the joined letters apart, or all of it to the one space after a
// Latin word. A lone four-word line now spreads over its three word spaces evenly, like
// Photoshop's, and the letters stay joined.
void ui_arabic_justify_all_spreads_the_last_line_over_word_spaces() {
  register_test_fonts(TestFontRole::UiDefault);
  const auto family = family_for_writing_system(
      QFontDatabase::Arabic, {QStringLiteral("Arial"), QStringLiteral("Noto Naskh Arabic"), QStringLiteral("DejaVu Sans")});
  if (family.isEmpty()) {
    patchy::test::skip("no Arabic-capable font available on this machine");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(patchy::Document(520, 260, patchy::PixelFormat::rgb8()), QStringLiteral("Arabic justify all 683"));
  QApplication::processEvents();
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    const doc = app.activeDocument;
    // "salam alaykum marhaban bialealam", then the same line with a Latin word in it.
    doc.addTextLayer('سلام عليكم مرحبا بالعالم',
        {font: '%1', size: 24, x: 60, y: 20, box: {width: 400, height: 60}, align: 'justifyAll', direction: 'rtl'}).name = 'arabic';
    doc.addTextLayer('سلام عليكم Latin بالعالم',
        {font: '%1', size: 24, x: 60, y: 140, box: {width: 400, height: 60}, align: 'justifyAll', direction: 'rtl'}).name = 'mixed';
  )JS").arg(family)));
  const auto& live = patchy::ui::MainWindowTestAccess::document(window);
  save_image_artifact("issue_683_arabic_justify_all", image_from_pixels_for_visuals(patchy::Compositor{}.flatten_rgb8(live)));
  constexpr int kBoxLeft = 60;
  constexpr int kBoxRight = 459;
  int checked = 0;
  for (const auto& layer : live.layers()) {
    if (!patchy::layer_is_text(layer)) {
      continue;
    }
    const auto bounds = alpha_pixel_bounds_in_rows(layer.pixels(), 0, layer.pixels().height());
    CHECK(bounds.has_value());
    if (!bounds.has_value()) {
      continue;
    }
    // One line: every inked row, marks included.
    const InkLine line{AlphaRowBand{bounds->top(), bounds->bottom() + 1}, bounds->left(), bounds->right()};
    CHECK(std::abs(layer.bounds().x + line.left - kBoxLeft) <= 3);
    CHECK(std::abs(layer.bounds().x + line.right - kBoxRight) <= 3);
    // Three word gaps of about the same width and nothing else wider than a letter joint: the
    // letters of each word stay together (spread letter by letter, every gap was about 13 px).
    std::vector<int> wide;
    for (const auto gap : ink_gaps(layer.pixels(), line)) {
      if (gap >= 5) {
        wide.push_back(gap);
      }
    }
    std::printf("  %s: ink %d-%d, gaps of 5 px or more:", layer.name().c_str(), layer.bounds().x + line.left,
                layer.bounds().x + line.right);
    for (const auto gap : wide) {
      std::printf(" %d", gap);
    }
    std::printf("\n");
    std::fflush(stdout);
    CHECK(wide.size() == 3U);
    if (wide.size() == 3U) {
      const auto [narrowest, widest] = std::minmax_element(wide.begin(), wide.end());
      CHECK(*narrowest >= 20);
      CHECK(*widest - *narrowest <= 8);
    }
    ++checked;
  }
  CHECK(checked == 2);
}

// GitHub issue 683: the settings Patchy keeps for Photoshop (Hyphenate, the hyphenation
// settings and the Kashida width per paragraph; digit set, diacritic position and Justification
// Alternates per run) ride on the editor's blocks and characters, so typing, a style change and
// a new paragraph keep them.
void ui_text_keeps_photoshop_paragraph_and_middle_eastern_settings_through_edits() {
  register_test_fonts(TestFontRole::UiDefault);
  const QString columns = QStringLiteral("\t30\t0\t0\t#000000\tArial\tauto\t0\t1\t1\t0\t\t0\t0\t0\t");
  const QString runs = QStringLiteral("v9\n0\t6") + columns + QStringLiteral("digits:2,diacx:-100,marky:0,justalt\n6\t5") +
                       columns + QStringLiteral("-");
  const QString paragraphs =
      QStringLiteral("v6\n0\t11\tleft\t0\t0\t0\t0\t0\t1.2\trtl\t1\thyphenate,hyphenation:7:3:4:5:30:0,kashida:0");
  QTextDocument document;
  document.setPlainText(QStringLiteral("Hindi plain"));
  patchy::ui::apply_patchy_text_runs_to_document(document, runs, QFont(QStringLiteral("Arial")), Qt::black, 1.0, 3);
  patchy::ui::apply_paragraph_runs_to_document(document, paragraphs);
  patchy::ui::TextToolSettings fallback;
  fallback.family = QStringLiteral("Arial");
  fallback.size = 30;
  const auto column = [](const QString& serialized, int index) {
    QStringList out;
    for (const auto& line : serialized.split(QLatin1Char('\n')).mid(1)) {
      out << line.split(QLatin1Char('\t')).value(index);
    }
    return out.join(QLatin1Char('|'));
  };
  const auto run_styles = [&] { return column(patchy::ui::rich_text_runs_from_document(document, fallback, Qt::black), 16); };
  const auto paragraph_settings = [&] { return column(patchy::ui::paragraph_runs_from_document(document), 11); };
  CHECK(run_styles() == QStringLiteral("digits:2,diacx:-100,marky:0,justalt|-"));
  CHECK(paragraph_settings() == QStringLiteral("hyphenate,hyphenation:7:3:4:5:30:0,kashida:0"));
  CHECK(patchy::ui::paragraph_runs_from_document(document).startsWith(QStringLiteral("v6\n")));

  QTextCursor cursor(&document);
  cursor.setPosition(3);
  cursor.insertText(QStringLiteral("xx"));  // typed inside the tagged run
  cursor.select(QTextCursor::Document);
  QTextCharFormat underline;
  underline.setProperty(patchy::ui::kTextUnderlineFormatProperty, true);
  cursor.mergeCharFormat(underline);
  CHECK(run_styles() == QStringLiteral("underline,digits:2,diacx:-100,marky:0,justalt|underline"));
  cursor.setPosition(8);
  cursor.insertBlock();  // Return: both paragraphs keep the settings
  CHECK(paragraph_settings() ==
        QStringLiteral("hyphenate,hyphenation:7:3:4:5:30:0,kashida:0|hyphenate,hyphenation:7:3:4:5:30:0,kashida:0"));

  // A new document's paragraphs and runs carry none of it.
  QTextDocument plain;
  plain.setPlainText(QStringLiteral("New layer"));
  CHECK(!patchy::ui::paragraph_runs_from_document(plain).startsWith(QStringLiteral("v6")));
  CHECK(!patchy::ui::rich_text_runs_from_document(plain, fallback, Qt::black).contains(QStringLiteral("digits")));
}

// Each paragraph run sets only its own paragraphs. A run ends with its paragraph break, and the
// editor document used to merge its block format one position further, into the next
// paragraph, which then kept whatever its own run leaves unset: the kept Photoshop settings
// (an Every-line paragraph made the Single-line one after it Every-line), a composer other than
// World-Ready, and a right-to-left direction ahead of an automatic one.
void ui_text_paragraph_runs_stay_on_their_own_paragraphs() {
  const auto column = [](const QString& serialized, int index) {
    QStringList out;
    for (const auto& line : serialized.split(QLatin1Char('\n')).mid(1)) {
      out << line.split(QLatin1Char('\t')).value(index);
    }
    return out.join(QLatin1Char('|'));
  };
  // "One\n" 0-4, "Two\n" 4-8, "\n" 8-9 (an empty paragraph), "Four" 9-13.
  const QString text = QStringLiteral("One\nTwo\n\nFour");
  const QString paragraphs = QStringLiteral(
      "v6\n"
      "0\t4\tleft\t0\t0\t0\t0\t0\t1.2\trtl\t0\thyphenate,everyline\n"
      "4\t4\tleft\t0\t0\t0\t0\t0\t1.2\tauto\t1\t-\n"
      "8\t1\tleft\t0\t0\t0\t0\t0\t1.2\tauto\t1\tkashida:0\n"
      "9\t4\tleft\t0\t0\t0\t0\t0\t1.2\tauto\t1\t-");
  QTextDocument document;
  document.setPlainText(text);
  patchy::ui::apply_paragraph_runs_to_document(document, paragraphs);
  const auto serialized = patchy::ui::paragraph_runs_from_document(document);
  CHECK(column(serialized, 11) == QStringLiteral("hyphenate,everyline|-|kashida:0|-"));
  CHECK(column(serialized, 10) == QStringLiteral("0|1|1|1"));
  CHECK(column(serialized, 9) == QStringLiteral("rtl|auto|auto|auto"));

  // One run over several paragraphs still reaches each of them, and stops at its end.
  QTextDocument spanning;
  spanning.setPlainText(text);
  patchy::ui::apply_paragraph_runs_to_document(
      spanning, QStringLiteral("v6\n0\t8\tleft\t0\t0\t0\t0\t0\t1.2\tauto\t1\teveryline\n"
                               "8\t5\tleft\t0\t0\t0\t0\t0\t1.2\tauto\t1\t-"));
  CHECK(column(patchy::ui::paragraph_runs_from_document(spanning), 11) == QStringLiteral("everyline|everyline|-|-"));

  // The commit's copy of the editor document and its change of scale keep each paragraph's own
  // format too: the first paragraph's indent, settings, composer and direction stay off the others.
  QTextDocument indented;
  indented.setPlainText(text);
  patchy::ui::apply_paragraph_runs_to_document(
      indented, QStringLiteral("v6\n"
                               "0\t4\tleft\t24\t12\t0\t0\t6\t1.2\trtl\t0\thyphenate,everyline\n"
                               "4\t4\tcenter\t0\t0\t0\t0\t0\t1.2\tauto\t1\t-\n"
                               "8\t1\tleft\t0\t0\t0\t0\t0\t1.2\tauto\t1\tkashida:0\n"
                               "9\t4\tright\t0\t0\t0\t0\t0\t1.2\tauto\t1\t-"));
  const auto check_paragraphs = [&](const QTextDocument& checked, const QString& first_indent) {
    const auto runs = patchy::ui::paragraph_runs_from_document(checked);
    CHECK(column(runs, 2) == QStringLiteral("left|center|left|right"));
    CHECK(column(runs, 3) == first_indent + QStringLiteral("|0|0|0"));
    CHECK(column(runs, 9) == QStringLiteral("rtl|auto|auto|auto"));
    CHECK(column(runs, 10) == QStringLiteral("0|1|1|1"));
    CHECK(column(runs, 11) == QStringLiteral("hyphenate,everyline|-|kashida:0|-"));
  };
  check_paragraphs(indented, QStringLiteral("24"));
  const auto copy = patchy::ui::copy_text_document_formats(indented);
  check_paragraphs(*copy, QStringLiteral("24"));
  patchy::ui::scale_document_font_sizes(*copy, 2.0);
  check_paragraphs(*copy, QStringLiteral("48"));
}

// The reporter's justified Hebrew file (GitHub issue 683, local fixture): after a re-render the
// Alef and Noto layers' justified lines reach the box's right edge like the left one. Skips the
// edge check for a layer whose font the host lacks (its substitute wraps differently, but still
// has to fill the box).
void ui_issue_683_hebrew_psd_justified_lines_reach_both_edges_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("issue-683/hebrew-rtl-justified-photoshop.psd");
  patchy::test::skip_unless_exists(path);
  register_test_fonts(TestFontRole::UiDefault);
  if (QFontDatabase::families(QFontDatabase::Hebrew).isEmpty()) {
    patchy::test::skip("no Hebrew-capable font available on this machine");
  }
  auto document = patchy::psd::DocumentIo::read_file(path);
  struct Box {
    patchy::LayerId id;
    int left;
    int right;
  };
  std::vector<Box> boxes;
  for (auto& layer : document.layers()) {
    if (patchy::layer_is_text(layer)) {
      // The files' fonts (Adobe Hebrew, Alef, Noto Sans Hebrew) are not on every host: a
      // Photoshop raster preview would ask before substituting them, which blocks the script.
      layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
      // The box is 368 px wide and its right edge is the last inked column of Photoshop's raster.
      const auto right = layer.bounds().x + layer.bounds().width - 1;
      boxes.push_back(Box{layer.id(), right - 367, right});
    }
  }
  CHECK(boxes.size() == 3U);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Hebrew 683"));
  QApplication::processEvents();
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    for (const layer of app.activeDocument.layers) {
      if (layer.isText) {
        layer.rerenderText();
        console.log('683 ' + layer.name + ' ' + layer.textDirection + ' ' + layer.textAlign);
      }
    }
  )JS")));
  const auto& live = patchy::ui::MainWindowTestAccess::document(window);
  save_image_artifact("issue_683_hebrew_justified_patchy", image_from_pixels_for_visuals(patchy::Compositor{}.flatten_rgb8(live)));
  for (const auto& box : boxes) {
    const auto* layer = std::as_const(live).find_layer(box.id);
    CHECK(layer != nullptr);
    if (layer == nullptr) {
      continue;
    }
    const auto runs = QString::fromStdString(layer->metadata().at(patchy::kLayerMetadataTextParagraphRuns));
    CHECK(runs.split(QLatin1Char('\n')).value(1).split(QLatin1Char('\t')).value(9) == QStringLiteral("rtl"));
    const auto lines = ink_lines(layer->pixels());
    CHECK(lines.size() >= 4U);
    int short_lines = 0;
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
      const auto left = layer->bounds().x + lines[index].left;
      const auto right = layer->bounds().x + lines[index].right;
      if (std::abs(right - box.right) > 3 || std::abs(left - box.left) > 3) {
        ++short_lines;
        std::printf("  %s line %d: ink %d-%d, box %d-%d\n", layer->name().c_str(), static_cast<int>(index), left, right,
                    box.left, box.right);
      }
    }
    std::fflush(stdout);
    CHECK(short_lines == 0);
  }
  write_psd_artifact(live, "issue_683_hebrew_justified_patchy.psd");
}

// GitHub issue 683 (follow-up 3): the reporter's "Indent left 40pt, Indent First Line 20pt" layer
// (right to left, justified). Photoshop draws the 40 pt Indent Left (its /EndIndent) on the left
// and the first-line indent on the right. A re-render was pinned to the file's ink box, which hid
// that Patchy laid the 40 pt out on the right; a box resize drops that pin and the gap jumped to
// the right edge. Patchy now reads Photoshop's start and end indents as the paragraph's sides, so
// after a resize the gap stays on the left and the first line starts 20 pt in from the right.
void ui_issue_683_rtl_left_indent_stays_left_after_a_box_resize_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("issue-683/arabic-rtl-settings-photoshop.psd");
  patchy::test::skip_unless_exists(path);
  register_test_fonts(TestFontRole::UiDefault);
  if (QFontDatabase::families(QFontDatabase::Arabic).isEmpty()) {
    patchy::test::skip("no Arabic-capable font available on this machine");
  }
  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::LayerId id{};
  for (auto& layer : document.layers()) {
    if (patchy::layer_is_text(layer)) {
      // Adobe Arabic is not on every host: a Photoshop raster preview would ask first.
      layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
      if (layer.name().rfind("Indent left 40pt", 0) == 0) {
        id = layer.id();
      }
    }
  }
  CHECK(id != patchy::LayerId{});
  if (id == patchy::LayerId{}) {
    return;
  }
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Arabic indents 683"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(1.0);
  QApplication::processEvents();
  // Hosts without Adobe Arabic ask before editing: continue with the stand-in font.
  QTimer missing_font_watcher;
  missing_font_watcher.setInterval(50);
  QObject::connect(&missing_font_watcher, &QTimer::timeout, [] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("missingPsdTextFontMessageBox")));
    if (dialog == nullptr || !dialog->isVisible()) {
      return;
    }
    for (auto* button : dialog->findChildren<QPushButton*>()) {
      if (dialog->buttonRole(button) == QMessageBox::AcceptRole) {
        button->click();
        return;
      }
    }
  });
  missing_font_watcher.start();
  patchy::ui::MainWindowTestAccess::edit_text_layer(window, id);
  process_events_for(300);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  // Drag the bottom handle down: the width, and so the line breaks, stay as they are.
  auto* bottom = canvas->findChild<QWidget*>(QStringLiteral("textBoxResizeHandleBottom"), Qt::FindDirectChildrenOnly);
  CHECK(bottom != nullptr && bottom->isVisible());
  if (bottom == nullptr) {
    return;
  }
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  const auto center = bottom->geometry().center();
  drag(*canvas, center, center + QPoint(0, 30));
  process_events_for(200);
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  const auto box_left = editor->property("patchy.documentTextX").toInt();
  const auto box_right = box_left + editor->property("patchy.documentTextWidth").toInt();
  send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
  process_events_for(300);
  missing_font_watcher.stop();
  const auto& live = patchy::ui::MainWindowTestAccess::document(window);
  const auto* layer = std::as_const(live).find_layer(id);
  CHECK(layer != nullptr);
  if (layer == nullptr) {
    return;
  }
  save_image_artifact("issue_683_rtl_indent_after_resize",
                      image_from_pixels_for_visuals(patchy::Compositor{}.flatten_rgb8(live)));
  const auto lines = ink_lines(layer->pixels());
  print_lines("resized", lines);
  std::printf("  box %d-%d, layer x %d\n", box_left, box_right, layer->bounds().x);
  std::fflush(stdout);
  // The paragraph keeps 40 as its left indent (column 4) and 20 as its first-line indent.
  const auto paragraph = QString::fromStdString(layer->metadata().at(patchy::kLayerMetadataTextParagraphRuns));
  const auto fields = paragraph.split(QLatin1Char('\n')).value(1).split(QLatin1Char('\t'));
  CHECK(std::abs(fields.value(3).toDouble() - 20.0) < 0.5 && std::abs(fields.value(4).toDouble() - 40.0) < 0.5 &&
        std::abs(fields.value(5).toDouble()) < 0.5);
  CHECK(lines.size() >= 4U);
  if (lines.size() < 4U) {
    return;
  }
  // Every line clears the 40 px Indent Left (a few px of side bearing aside); the justified middle
  // lines reach the right edge; the first line stops about 20 px short of it.
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto left = layer->bounds().x + lines[index].left;
    const auto right = layer->bounds().x + lines[index].right;
    CHECK(left >= box_left + 36);
    if (index > 0 && index + 1 < lines.size()) {
      CHECK(right >= box_right - 6);
    }
  }
  const auto first_right = layer->bounds().x + lines.front().right;
  CHECK(first_right <= box_right - 14 && first_right >= box_right - 28);
}


// The first row in `area` of a flattened white-background document that holds dark ink.
std::optional<int> first_dark_row(const QImage& image, QRect area) {
  area = area.intersected(image.rect());
  for (int y = area.top(); y <= area.bottom(); ++y) {
    for (int x = area.left(); x <= area.right(); ++x) {
      if (qGray(image.pixel(x, y)) < 128) {
        return y;
      }
    }
  }
  return std::nullopt;
}

// Opens `path`, edits its box layer named `prefix...`, and drags `handle_name` straight down:
// the box grows, and the first line stays where the session drew it, in the live preview and
// after the commit (GitHub issue 683, follow-up 5). A Photoshop-layout box's render buffer starts
// above the frame top; a resize clears the import frame's render rect, and the buffer was then
// placed on the frame corner, every line dropping by the overhang (5 px for 16 px Arabic).
void check_box_bottom_edge_drag_keeps_the_first_line(const std::filesystem::path& path, const std::string& prefix,
                                                     const char* handle_name, double zoom,
                                                     const char* artifact) {
  auto document = patchy::psd::DocumentIo::read_file(path);
  patchy::LayerId id{};
  for (const auto& layer : document.layers()) {
    if (patchy::layer_is_text(layer) && layer.name().rfind(prefix, 0) == 0) {
      id = layer.id();
    }
  }
  CHECK(id != patchy::LayerId{});
  if (id == patchy::LayerId{}) {
    return;
  }
  // Every top below is read the same way, from the flattened document: the file's raster, the
  // session's live preview, and the committed layer.
  const auto file_image = image_from_pixels_for_visuals(patchy::Compositor{}.flatten_rgb8(document));
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Box drag 683"));
  auto* canvas = require_canvas(window);
  canvas->set_zoom(zoom);
  QApplication::processEvents();
  // Hosts without the file's font (Adobe Arabic; Arial on Linux) ask before editing: continue
  // with the stand-in font.
  QTimer missing_font_watcher;
  missing_font_watcher.setInterval(50);
  QObject::connect(&missing_font_watcher, &QTimer::timeout, [] {
    auto* dialog = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("missingPsdTextFontMessageBox")));
    if (dialog == nullptr || !dialog->isVisible()) {
      return;
    }
    for (auto* button : dialog->findChildren<QPushButton*>()) {
      if (dialog->buttonRole(button) == QMessageBox::AcceptRole) {
        button->click();
        return;
      }
    }
  });
  missing_font_watcher.start();
  patchy::ui::MainWindowTestAccess::edit_text_layer(window, id);
  process_events_for(300);
  auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  // A caret, not a selection, so the shots show the glyphs.
  editor->moveCursor(QTextCursor::End);
  process_events_for(200);
  const QRect box(editor->property("patchy.documentTextX").toInt(), editor->property("patchy.documentTextY").toInt(),
                  editor->property("patchy.documentTextWidth").toInt(),
                  editor->property("patchy.documentTextHeight").toInt());
  // The first line's ink may rise a little over the frame top (Arabic marks, Photoshop's
  // ascent rule); look from well above it down past the first line.
  const QRect first_line_area(box.left(), box.top() - 12, box.width(), 40);
  const auto& live = patchy::ui::MainWindowTestAccess::document(window);
  const auto composite_top = [&live, &first_line_area] {
    return first_dark_row(image_from_pixels_for_visuals(patchy::Compositor{}.flatten_rgb8(live)), first_line_area);
  };
  const auto screen_crop = [canvas, &box] {
    const auto top_left = canvas->widget_position_for_document_point(box.topLeft() - QPoint(8, 14));
    const auto bottom_right = canvas->widget_position_for_document_point(box.bottomRight() + QPoint(8, 46));
    return QRect(top_left, bottom_right);
  };
  const auto file_top = first_dark_row(file_image, first_line_area);
  const auto session_top = composite_top();
  // `artifact` (or null) names a before and after shot of the box, which must be on screen.
  const auto before_shot = artifact != nullptr
                               ? capture_widget_artifact(std::string(artifact) + "_canvas_before", *canvas).copy(screen_crop())
                               : QImage();
  auto* handle = canvas->findChild<QWidget*>(QString::fromLatin1(handle_name), Qt::FindDirectChildrenOnly);
  CHECK(handle != nullptr && handle->isVisible());
  if (handle == nullptr) {
    return;
  }
  editor->setFocus(Qt::OtherFocusReason);
  QApplication::processEvents();
  const auto center = handle->geometry().center();
  drag(*canvas, center, center + QPoint(0, static_cast<int>(std::lround(30.0 * zoom))));
  process_events_for(300);
  editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
  CHECK(editor != nullptr);
  if (editor == nullptr) {
    return;
  }
  CHECK(editor->property("patchy.documentTextY").toInt() == box.top());
  CHECK(editor->property("patchy.documentTextHeight").toInt() == box.height() + 30);
  const auto resized_top = composite_top();
  if (artifact != nullptr) {
    const auto after_shot = capture_widget_artifact(std::string(artifact) + "_canvas_after", *canvas).copy(screen_crop());
    save_stacked_artifact(artifact, {before_shot, after_shot});
  }
  send_key(*editor, Qt::Key_Return, Qt::ControlModifier);
  process_events_for(300);
  missing_font_watcher.stop();
  CHECK(std::as_const(live).find_layer(id) != nullptr);
  const auto committed_top = composite_top();
  std::printf("  %s: file top %d, session %d, after the drag %d, committed %d (box top %d)\n", prefix.c_str(),
              file_top.value_or(-1), session_top.value_or(-1), resized_top.value_or(-1), committed_top.value_or(-1), box.top());
  std::fflush(stdout);
  CHECK(session_top.has_value() && resized_top.has_value() && committed_top.has_value());
  if (!session_top.has_value() || !resized_top.has_value() || !committed_top.has_value()) {
    return;
  }
  // The session draws at the file's raster position (the file's top is printed for reference: a
  // stand-in font's faint top rows read it a pixel or two off). Unpinned by the resize, the
  // stand-in may lay the first line a pixel off the pinned one, never the overhang the bug
  // dropped it by.
  CHECK(std::abs(*resized_top - *session_top) <= 1);
  CHECK(*committed_top == *resized_top);
}

// GitHub issue 683 (follow-up 5): philipzae's Arabic paragraphs (16 px right aligned, and the
// 12 pt one with indents and 24 pt line spacing).
void ui_issue_683_box_bottom_edge_drag_keeps_the_first_line_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("issue-683/arabic-rtl-settings-photoshop.psd");
  patchy::test::skip_unless_exists(path);
  register_test_fonts(TestFontRole::UiDefault);
  check_box_bottom_edge_drag_keeps_the_first_line(path, "Right align, Kashidas medium", "textBoxResizeHandleBottom",
                                                  1.5, "issue_683_box_bottom_drag_right_align");
  check_box_bottom_edge_drag_keeps_the_first_line(path, "Indent left 40pt", "textBoxResizeHandleBottom", 1.0,
                                                  nullptr);
}

// The same for a left-to-right Photoshop box (Arial 24 px in a 300 x 40 frame, too short for
// side handles, so the bottom right corner drags straight down).
void ui_photoshop_box_bottom_edge_drag_keeps_the_first_line() {
  register_test_fonts(TestFontRole::UiDefault);
  check_box_bottom_edge_drag_keeps_the_first_line(
      patchy::test::committed_psd_fixture_path("photoshop-box-text-overflow.psd"), "Overflow Box",
      "textBoxResizeHandleBottomRight", 2.0, "photoshop_box_bottom_drag");
}

}  // namespace

std::vector<patchy::test::TestCase> text_rtl_psd_tests() {
  return {
      {"ui_rtl_justified_text_fills_the_box_to_both_edges", ui_rtl_justified_text_fills_the_box_to_both_edges},
      {"ui_arabic_justify_all_spreads_the_last_line_over_word_spaces",
       ui_arabic_justify_all_spreads_the_last_line_over_word_spaces},
      {"ui_text_keeps_photoshop_paragraph_and_middle_eastern_settings_through_edits",
       ui_text_keeps_photoshop_paragraph_and_middle_eastern_settings_through_edits},
      {"ui_text_paragraph_runs_stay_on_their_own_paragraphs", ui_text_paragraph_runs_stay_on_their_own_paragraphs},
      {"ui_issue_683_hebrew_psd_justified_lines_reach_both_edges_if_available",
       ui_issue_683_hebrew_psd_justified_lines_reach_both_edges_if_available},
      {"ui_issue_683_photoshop_leading_survives_patchy_save_and_reedit",
       ui_issue_683_photoshop_leading_survives_patchy_save_and_reedit},
      {"ui_issue_683_overflowing_box_text_reedit_keeps_its_glyphs",
       ui_issue_683_overflowing_box_text_reedit_keeps_its_glyphs},
      {"ui_issue_683_arabic_psd_rerenders_right_aligned_if_available",
       ui_issue_683_arabic_psd_rerenders_right_aligned_if_available},
      {"ui_issue_683_rtl_left_indent_stays_left_after_a_box_resize_if_available",
       ui_issue_683_rtl_left_indent_stays_left_after_a_box_resize_if_available},
      {"ui_issue_683_box_bottom_edge_drag_keeps_the_first_line_if_available",
       ui_issue_683_box_bottom_edge_drag_keeps_the_first_line_if_available},
      {"ui_photoshop_box_bottom_edge_drag_keeps_the_first_line",
       ui_photoshop_box_bottom_edge_drag_keeps_the_first_line},
  };
}
