// GitHub issue 683: Arabic PSD text. Photoshop's paragraph direction and alignment on open, the
// Photoshop layout model kept through a Patchy save, and re-editing a Patchy box whose lines
// overflow it. See docs/text-render-calibration-vertical-rtl.md ("Paragraph direction") and
// docs/text-render-calibration-patchy-authored.md.
#include "core/layer_metadata.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "ui/alpha_row_bands.hpp"
#include "ui/main_window.hpp"
#include "ui/script_engine.hpp"
#include "local_psd_fixtures.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QRect>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;

bool run_script_to_end(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("rtl-psd-test-script");
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
    // Each paragraph ends in a short line ("...al-iliktruni."). Flush right puts it against the
    // right edge of the text, flush left (what auto direction drew) against the left. Measured on
    // the last inked rows. Amiri Quran's tall marks reach across lines into those rows, so that
    // layer only checks its direction and alignment above.
    if (layer->name() == "Amiri Quran") {
      continue;
    }
    const auto& pixels = layer->pixels();
    const auto all = alpha_pixel_bounds_in_rows(pixels, 0, pixels.height());
    int last_row = pixels.height() - 1;
    while (last_row > 0 && !alpha_pixel_bounds_in_rows(pixels, last_row, last_row + 1).has_value()) {
      --last_row;
    }
    const auto last = alpha_pixel_bounds_in_rows(pixels, std::max(0, last_row - 6), last_row + 1);
    CHECK(all.has_value() && last.has_value());
    if (!all.has_value() || !last.has_value()) {
      continue;
    }
    std::printf("  %s ink %d-%d, last line %d-%d\n", layer->name().c_str(), all->left(), all->right(), last->left(),
                last->right());
    std::fflush(stdout);
    CHECK(all->right() - last->right() <= 8);
    CHECK(last->left() - all->left() >= all->width() / 4);
  }
  // Saved by Patchy with an auto Arabic paragraph of its own, which the save writes right to
  // left: the Photoshop open check reads that file.
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    app.activeDocument.addTextLayer('\u0645\u0631\u062d\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645',
        {size: 20, x: 20, y: 460, box: {width: 360, height: 36}, align: 'right'});
  )JS")));
  write_psd_artifact(live, "issue_683_arabic_patchy.psd");
}

}  // namespace

std::vector<patchy::test::TestCase> text_rtl_psd_tests() {
  return {
      {"ui_issue_683_photoshop_leading_survives_patchy_save_and_reedit",
       ui_issue_683_photoshop_leading_survives_patchy_save_and_reedit},
      {"ui_issue_683_overflowing_box_text_reedit_keeps_its_glyphs",
       ui_issue_683_overflowing_box_text_reedit_keeps_its_glyphs},
      {"ui_issue_683_arabic_psd_rerenders_right_aligned_if_available",
       ui_issue_683_arabic_psd_rerenders_right_aligned_if_available},
  };
}
