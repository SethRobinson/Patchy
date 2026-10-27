// The Character panel's styles (GitHub issue 346): All Caps, Small Caps, Superscript, Subscript,
// Underline, Strikethrough, Ordinals and Fractions. The panel toggles write the run flags, and the
// renders match Photoshop 2026's COM captures (docs/text-character-styles.md).
#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/dialog_utils.hpp"
#include "ui/edit_conversions.hpp"
#include "ui/qt_paths.hpp"
#include "render/compositor.hpp"
#include "local_psd_fixtures.hpp"
#include "psd/psd_document_io.hpp"
#include "ui/main_window.hpp"
#include "scripting_test_support.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QPushButton>
#include <QStatusBar>
#include <QTextEdit>
#include <QTextLayout>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace patchy::test::ui;
using namespace patchy::test::ui::scripting_support;
using patchy::ui::MainWindowTestAccess;

namespace {

constexpr double kSize = 100.0;

struct Ink {
  int left{0};
  int top{0};
  int right{0};   // exclusive
  int bottom{0};  // exclusive
  [[nodiscard]] int width() const { return right - left; }
  [[nodiscard]] int height() const { return bottom - top; }
};

double alpha_at(const patchy::Layer& layer, int x, int y) {
  const auto& pixels = layer.pixels();
  if (x < 0 || y < 0 || x >= pixels.width() || y >= pixels.height()) {
    return 0.0;
  }
  return pixels.pixel(x, y)[3] / 255.0;
}

// Ink extents (alpha > 50%) in document pixels, limited to layer columns [from, to).
std::optional<Ink> ink_of(const patchy::Layer& layer, double from_fraction = 0.0, double to_fraction = 1.0) {
  const auto& pixels = layer.pixels();
  const int from = static_cast<int>(std::floor(pixels.width() * from_fraction));
  const int to = static_cast<int>(std::ceil(pixels.width() * to_fraction));
  std::optional<Ink> ink;
  for (int y = 0; y < pixels.height(); ++y) {
    for (int x = from; x < to; ++x) {
      if (alpha_at(layer, x, y) <= 0.5) {
        continue;
      }
      if (!ink) {
        ink = Ink{x, y, x + 1, y + 1};
      }
      ink->left = std::min(ink->left, x);
      ink->top = std::min(ink->top, y);
      ink->right = std::max(ink->right, x + 1);
      ink->bottom = std::max(ink->bottom, y + 1);
    }
  }
  if (ink) {
    const auto bounds = layer.bounds();
    ink->left += bounds.x;
    ink->right += bounds.x;
    ink->top += bounds.y;
    ink->bottom += bounds.y;
  }
  return ink;
}

// The bar's top and bottom edges in document pixels: the rows whose 10th-percentile coverage
// across the inked columns is set (glyph gaps are bar only), with fractional edge coverage.
std::optional<std::pair<double, double>> bar_edges(const patchy::Layer& layer) {
  const auto ink = ink_of(layer);
  if (!ink) {
    return std::nullopt;
  }
  const auto bounds = layer.bounds();
  const int x0 = ink->left - bounds.x + 2;
  const int x1 = ink->right - bounds.x - 2;
  std::vector<double> coverage(static_cast<std::size_t>(layer.pixels().height()), 0.0);
  for (int y = 0; y < layer.pixels().height(); ++y) {
    std::vector<double> row;
    for (int x = x0; x < x1; ++x) {
      row.push_back(alpha_at(layer, x, y));
    }
    if (row.empty()) {
      continue;
    }
    std::ranges::sort(row);
    coverage[static_cast<std::size_t>(y)] = row[row.size() / 10U];
  }
  int first = -1;
  int last = -1;
  for (int y = 0; y < static_cast<int>(coverage.size()); ++y) {
    if (coverage[static_cast<std::size_t>(y)] > 0.02) {
      if (first < 0) first = y;
      last = y;
    }
  }
  if (first < 0) {
    return std::nullopt;
  }
  const double top = first + (1.0 - coverage[static_cast<std::size_t>(first)]) + bounds.y;
  const double bottom = last + coverage[static_cast<std::size_t>(last)] + bounds.y;
  return std::pair{top, bottom};
}

bool has_family(const char* family) {
  return QFontDatabase::hasFamily(QString::fromLatin1(family));
}

// Whether the family's OpenType `tag` changes the glyphs of `text` (Patchy applies a feature only
// then). Windows' Arial has smcp and frac; the Arial macOS ships has neither.
bool font_feature_changes(const char* family, const char* tag, const QString& text) {
  const auto glyphs = [&text](const QFont& font) {
    QTextLayout layout(text, font);
    layout.beginLayout();
    auto line = layout.createLine();
    layout.endLayout();
    QList<quint32> indexes;
    if (line.isValid()) {
      for (const auto& run : line.glyphRuns()) {
        indexes += run.glyphIndexes();
      }
    }
    return indexes;
  };
  QFont font(QString::fromLatin1(family));
  font.setPixelSize(static_cast<int>(kSize));
  auto featured = font;
  featured.setFeature(QFont::Tag::fromString(QLatin1String(tag)).value(), 1);
  return glyphs(featured) != glyphs(font);
}

// Adds a 100 px text layer made of `runs` (a JS array literal) at a fixed anchor and returns it.
// A copy: the next add can move the document's layer vector.
std::optional<patchy::Layer> add_runs_layer(patchy::ui::MainWindow& window, const QString& runs, const char* family,
                                    bool photoshop_layout) {
  CHECK(run_script(window, QStringLiteral("app.activeDocument.addTextLayer(%1, {font: '%2', size: %3, x: 40, y: 60, "
                                          "color: '#000000'});")
                               .arg(runs, QLatin1String(family))
                               .arg(kSize)));
  auto& document = MainWindowTestAccess::document(window);
  const auto id = document.active_layer_id();
  CHECK(id.has_value());
  if (!id) {
    return std::nullopt;
  }
  if (photoshop_layout) {
    document.find_layer(*id)->metadata()[patchy::kLayerMetadataTextLayoutMode] = patchy::kTextLayoutModePhotoshop;
    CHECK(run_script(window, QStringLiteral("app.activeDocument.activeLayer.rerenderText();")));
  }
  const auto* layer = std::as_const(document).find_layer(*id);
  CHECK(layer != nullptr);
  return layer != nullptr ? std::optional<patchy::Layer>(*layer) : std::nullopt;
}

// The plain layer's baseline: the bottom of its first capital H (no descender, no overshoot).
double baseline_of(const patchy::Layer& plain) {
  const auto ink = ink_of(plain, 0.0, 0.2);
  CHECK(ink.has_value());
  return ink ? static_cast<double>(ink->bottom) : 0.0;
}

void expect_near(double actual, double expected, double tolerance, const char* what) {
  if (std::abs(actual - expected) > tolerance) {
    std::printf("  %s: %.2f, expected %.2f +- %.2f\n", what, actual, expected, tolerance);
  }
  CHECK(std::abs(actual - expected) <= tolerance);
}

// Underline and strikethrough sit where Photoshop draws them for Arial at 100 px (COM capture):
// the underline covers 18.0 to 25.25 px below the baseline, the strikethrough is centred 25.9 px
// above it, both with the font's 7.3 px underline thickness. Both layout modes draw them.
void ui_text_underline_and_strikethrough_match_photoshop_if_available() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  if (!has_family("Arial")) {
    patchy::test::skip("Arial is not installed");
  }
  for (const bool photoshop_layout : {false, true}) {
    patchy::ui::MainWindow window;
    show_window(window);
    const auto plain = add_runs_layer(window, QStringLiteral("[{text: 'HxH'}]"), "Arial", photoshop_layout);
    const auto underlined =
        add_runs_layer(window, QStringLiteral("[{text: 'HxH', underline: true}]"), "Arial", photoshop_layout);
    const auto struck =
        add_runs_layer(window, QStringLiteral("[{text: 'HxH', strikethrough: true}]"), "Arial", photoshop_layout);
    CHECK(plain && underlined && struck);
    if (!plain || !underlined || !struck) {
      return;
    }
    const auto baseline = baseline_of(*plain);
    const auto underline = bar_edges(*underlined);
    CHECK(underline.has_value());
    if (underline) {
      expect_near(underline->first - baseline, 18.0, 0.8, "underline top below baseline");
      expect_near(underline->second - underline->first, 7.3, 0.6, "underline thickness");
    }
    const auto strike = bar_edges(*struck);
    CHECK(strike.has_value());
    if (strike) {
      expect_near(baseline - (strike->first + strike->second) / 2.0, 25.9, 0.8, "strikethrough centre above baseline");
      expect_near(strike->second - strike->first, 7.3, 0.6, "strikethrough thickness");
    }
    // The bar runs from the pen start to the last advance, as in Photoshop.
    const auto plain_ink = ink_of(*plain);
    const auto underline_ink = ink_of(*underlined);
    CHECK(plain_ink && underline_ink);
    if (plain_ink && underline_ink) {
      CHECK(underline_ink->left <= plain_ink->left);
      CHECK(underline_ink->right >= plain_ink->right);
    }
    if (!photoshop_layout) {
      save_widget_artifact("ui_text_underline_and_strikethrough", window);
    }
    CHECK(run_script(window, QStringLiteral(
        "var runs = app.activeDocument.activeLayer.textRuns; console.log('strike-read=' + runs[0].strikethrough + "
        "',' + runs[0].underline + ',' + runs[0].caps + ',' + runs[0].script);")));
    CHECK(backlog_contains(window, QStringLiteral("strike-read=true,false,none,none")));
  }
}

// Superscript and subscript draw at 58.3% of the size, moved 33.3% of the size off the
// baseline (Photoshop's /SuperscriptSize and /SuperscriptPosition, the H moves 33 px at 100 px).
// All Caps draws lowercase as capitals; Small Caps uses Arial's own small capitals (0.56 em
// tall, where a synthesized 70% capital would be 0.50 em).
void ui_text_script_and_caps_match_photoshop_if_available() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  if (!has_family("Arial")) {
    patchy::test::skip("Arial is not installed");
  }
  for (const bool photoshop_layout : {false, true}) {
    patchy::ui::MainWindow window;
    show_window(window);
    const auto plain = add_runs_layer(window, QStringLiteral("[{text: 'HH'}]"), "Arial", photoshop_layout);
    const auto raised = add_runs_layer(window, QStringLiteral("[{text: 'H'}, {text: 'H', script: 'superscript'}]"),
                                        "Arial", photoshop_layout);
    const auto lowered = add_runs_layer(window, QStringLiteral("[{text: 'H'}, {text: 'H', script: 'subscript'}]"),
                                         "Arial", photoshop_layout);
    CHECK(plain && raised && lowered);
    if (!plain || !raised || !lowered) {
      return;
    }
    const auto baseline = baseline_of(*plain);
    const auto cap = ink_of(*plain, 0.0, 0.45);
    const auto super_h = ink_of(*raised, 0.62, 1.0);
    const auto sub_h = ink_of(*lowered, 0.62, 1.0);
    CHECK(cap && super_h && sub_h);
    if (cap && super_h && sub_h) {
      expect_near(super_h->height(), cap->height() * 0.583, 1.5, "superscript H height");
      expect_near(baseline - super_h->bottom, 33.3, 1.2, "superscript rise");
      expect_near(sub_h->bottom - baseline, 33.3, 1.2, "subscript drop");
    }

    const auto capitals = add_runs_layer(window, QStringLiteral("[{text: 'HXH'}]"), "Arial", photoshop_layout);
    const auto all_caps =
        add_runs_layer(window, QStringLiteral("[{text: 'hxh', caps: 'all'}]"), "Arial", photoshop_layout);
    const auto small_caps =
        add_runs_layer(window, QStringLiteral("[{text: 'Hxh', caps: 'small'}]"), "Arial", photoshop_layout);
    CHECK(capitals && all_caps && small_caps);
    if (!capitals || !all_caps || !small_caps) {
      return;
    }
    const auto capitals_ink = ink_of(*capitals);
    const auto all_caps_ink = ink_of(*all_caps);
    CHECK(capitals_ink && all_caps_ink);
    if (capitals_ink && all_caps_ink) {
      expect_near(all_caps_ink->width(), capitals_ink->width(), 1.0, "All Caps width");
      expect_near(all_caps_ink->height(), capitals_ink->height(), 1.0, "All Caps height");
    }
    const auto small = ink_of(*small_caps, 0.45, 1.0);
    CHECK(small.has_value());
    if (small) {
      // Without smcp (macOS's Arial) the small capital is synthesized at 70% of the 0.716 em cap.
      const double expected = font_feature_changes("Arial", "smcp", QStringLiteral("xh")) ? 0.56 : 0.50;
      expect_near(small->height(), expected * kSize, 2.5, "Arial small capital height");
    }
    if (!photoshop_layout) {
      save_widget_artifact("ui_text_script_and_caps", window);
    }
  }
}

// Ordinals and Fractions apply the font's OpenType features: Arial has fractions (1/2 turns into
// one fraction, much narrower) but no ordinals, so "1st" stays exactly as typed, as in Photoshop;
// Calibri has ordinals.
void ui_text_ordinals_and_fractions_use_the_font_features_if_available() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  if (!has_family("Arial")) {
    patchy::test::skip("Arial is not installed");
  }
  patchy::ui::MainWindow window;
  show_window(window);
  const auto plain_fraction = add_runs_layer(window, QStringLiteral("[{text: '1/2'}]"), "Arial", false);
  const auto fraction = add_runs_layer(window, QStringLiteral("[{text: '1/2', fractions: true}]"), "Arial", false);
  const auto plain_ordinal = add_runs_layer(window, QStringLiteral("[{text: '1st'}]"), "Arial", false);
  const auto ordinal = add_runs_layer(window, QStringLiteral("[{text: '1st', ordinals: true}]"), "Arial", false);
  CHECK(plain_fraction && fraction && plain_ordinal && ordinal);
  if (!plain_fraction || !fraction || !plain_ordinal || !ordinal) {
    return;
  }
  const auto plain_fraction_ink = ink_of(*plain_fraction);
  const auto fraction_ink = ink_of(*fraction);
  CHECK(plain_fraction_ink && fraction_ink);
  if (plain_fraction_ink && fraction_ink) {
    if (font_feature_changes("Arial", "frac", QStringLiteral("1/2"))) {
      CHECK(fraction_ink->width() < plain_fraction_ink->width() * 0.75);
    } else {
      // A face without fractions (macOS's Arial) leaves the text as typed, as Photoshop does.
      CHECK(std::ranges::equal(plain_fraction->pixels().data(), fraction->pixels().data()));
    }
  }
  CHECK(std::ranges::equal(plain_ordinal->pixels().data(), ordinal->pixels().data()));
  if (has_family("Calibri")) {
    const auto calibri_plain = add_runs_layer(window, QStringLiteral("[{text: '1st'}]"), "Calibri", false);
    const auto calibri_ordinal =
        add_runs_layer(window, QStringLiteral("[{text: '1st', ordinals: true}]"), "Calibri", false);
    CHECK(calibri_plain && calibri_ordinal);
    if (calibri_plain && calibri_ordinal) {
      // The raised "t" ends well above the baseline the plain one sits on.
      // The last 20% of each layer is the t alone (a superior t is narrow, so wider slices would
      // reach the 1, which stays on the baseline).
      const auto plain_st = ink_of(*calibri_plain, 0.8, 1.0);
      const auto raised_st = ink_of(*calibri_ordinal, 0.8, 1.0);
      CHECK(plain_st && raised_st);
      if (plain_st && raised_st) {
        CHECK(raised_st->bottom < plain_st->bottom - 0.2 * kSize);
      }
    }
  }
  save_widget_artifact("ui_text_ordinals_and_fractions", window);
}

// The Character panel's style row toggles each run flag on the selected layer without a session,
// as one undo step each; the two caps and the two baselines exclude each other, and turning every
// style off again leaves the runs without the style column.
void ui_text_character_style_buttons_toggle_run_styles() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  patchy::ui::MainWindow window;
  show_window(window);
  CHECK(run_script(window, QStringLiteral(
      "app.activeDocument.addTextLayer('Styled', {size: 48, x: 30, y: 80, color: '#000000'});")));
  auto& document = MainWindowTestAccess::document(window);
  const auto id = document.active_layer_id();
  CHECK(id.has_value());
  if (!id) {
    return;
  }
  require_action_by_text(window, QStringLiteral("Type"))->trigger();
  const auto style_column = [&]() {
    const auto runs = QString::fromStdString(std::as_const(document).find_layer(*id)->metadata().at(
        patchy::kLayerMetadataTextRuns));
    QStringList columns;
    for (const auto& line : runs.split(QLatin1Char('\n')).mid(1)) {
      const auto fields = line.split(QLatin1Char('\t'));
      columns << (fields.size() >= 17 ? fields[16] : QStringLiteral("-"));
    }
    columns.removeDuplicates();
    return columns.join(QLatin1Char('|'));
  };
  bool drove = false;
  QTimer::singleShot(0, [&] {
    try {
      auto* dialog = window.findChild<QDialog*>(QStringLiteral("textCharacterDialog"));
      CHECK(dialog != nullptr);
      if (dialog == nullptr) {
        return;
      }
      const auto button = [&](const char* name) {
        auto* found = dialog->findChild<QPushButton*>(QLatin1String(name));
        CHECK(found != nullptr);
        return found;
      };
      auto* all_caps = button("textCharacterAllCapsButton");
      auto* small_caps = button("textCharacterSmallCapsButton");
      auto* superscript = button("textCharacterSuperscriptButton");
      auto* subscript = button("textCharacterSubscriptButton");
      auto* underline = button("textCharacterUnderlineButton");
      auto* strike = button("textCharacterStrikethroughButton");
      auto* ordinals = button("textCharacterOrdinalsButton");
      auto* fractions = button("textCharacterFractionsButton");
      if (!all_caps || !small_caps || !superscript || !subscript || !underline || !strike || !ordinals || !fractions) {
        dialog->reject();
        return;
      }
      CHECK(all_caps->isEnabled() && !all_caps->isChecked() && all_caps->focusPolicy() == Qt::NoFocus);
      const auto depth = MainWindowTestAccess::active_session_undo_depth(window);
      all_caps->click();
      CHECK(style_column() == QStringLiteral("allcaps"));
      CHECK(MainWindowTestAccess::active_session_undo_depth(window) == depth + 1);
      small_caps->click();
      CHECK(!all_caps->isChecked() && small_caps->isChecked());
      CHECK(style_column() == QStringLiteral("smallcaps"));
      superscript->click();
      subscript->click();
      CHECK(!superscript->isChecked() && subscript->isChecked());
      CHECK(style_column() == QStringLiteral("smallcaps,subscript"));
      underline->click();
      strike->click();
      ordinals->click();
      fractions->click();
      CHECK(style_column() == QStringLiteral("smallcaps,subscript,underline,strikethrough,ordinals,fractions"));
      save_widget_artifact("ui_text_character_style_panel", *dialog);
      require_action_by_text(window, QStringLiteral("Undo"))->trigger();
      CHECK(style_column() == QStringLiteral("smallcaps,subscript,underline,strikethrough,ordinals"));
      CHECK(!fractions->isChecked() && ordinals->isChecked());
      for (auto* on : {small_caps, subscript, underline, strike, ordinals}) {
        on->click();
      }
      CHECK(style_column() == QStringLiteral("-"));
      CHECK(!QString::fromStdString(std::as_const(document).find_layer(*id)->metadata().at(
                                        patchy::kLayerMetadataTextRuns))
                 .startsWith(QStringLiteral("v9")));
      drove = true;
      dialog->reject();
    } catch (...) {
      patchy::ui::unwind_non_modal_dialog_loop(std::current_exception());
    }
  });
  window.findChild<QPushButton*>(QStringLiteral("textCharacterButton"))->click();
  CHECK(drove);
}

// Issue 466 for every style button: with part of the line selected in a live session, each toggle
// (faux bold, faux italic and the eight styles) sets its flag on the selected runs alone and leaves
// the other word's glyphs exactly as they were; with both words styled, turning it off on one word
// leaves the other styled and untouched; a PSD save and reopen keeps the runs as they were.
void ui_text_character_styles_apply_only_to_the_selection() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  constexpr int kWidth = 480;
  constexpr int kHeight = 160;
  const QString text = QStringLiteral("Hab Hab1/2");
  patchy::ui::MainWindow window;
  show_window(window);
  // Each style starts on its own fresh document holding one plain text layer, made the way a
  // script or the Type tool makes one.
  patchy::Document* live = nullptr;
  patchy::LayerId layer_id{};
  const auto fresh_layer = [&](const char* name) {
    patchy::Document seed(kWidth, kHeight, patchy::PixelFormat::rgba8());
    seed.add_pixel_layer("Background", solid_pixels(kWidth, kHeight, patchy::PixelFormat::rgba8(), QColor(Qt::white)));
    window.add_document_session(std::move(seed), QString::fromLatin1(name));
    QApplication::processEvents();
    CHECK(run_script(window, QStringLiteral("app.activeDocument.addTextLayer('%1', {font: 'Arial', size: 64, x: 20, "
                                            "y: 30, color: '#000000'});")
                                 .arg(text)));
    live = &MainWindowTestAccess::document(window);
    const auto id = live->active_layer_id();
    CHECK(id.has_value());
    layer_id = id.value_or(patchy::LayerId{});
  };

  // Opens the layer in the Type tool, selects [from, to), clicks `button` in the Character dialog
  // (checking it showed `checked_before` and flipped) and commits through Move, which re-renders.
  const auto edit = [&](int from, int to, const char* button, bool checked_before) {
    auto* canvas = require_canvas(window);
    require_action_by_text(window, QStringLiteral("Type"))->trigger();
    const auto bounds = std::as_const(*live).find_layer(layer_id)->bounds();
    const auto hit = canvas->widget_position_for_document_point(QPoint(bounds.x + 25, bounds.y + bounds.height / 2));
    send_mouse(*canvas, QEvent::MouseButtonPress, hit, Qt::LeftButton, Qt::LeftButton);
    send_mouse(*canvas, QEvent::MouseButtonRelease, hit, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
    process_events_for(250);
    auto* editor = canvas->findChild<QTextEdit*>(QStringLiteral("inlineTextEditor"));
    CHECK(editor != nullptr);
    if (editor != nullptr && button != nullptr) {
      auto cursor = editor->textCursor();
      cursor.setPosition(from);
      cursor.setPosition(to, QTextCursor::KeepAnchor);
      editor->setTextCursor(cursor);
      QApplication::processEvents();
      bool flipped = false;
      QTimer::singleShot(0, [&] {
        try {
          auto* dialog = window.findChild<QDialog*>(QStringLiteral("textCharacterDialog"));
          CHECK(dialog != nullptr);
          if (dialog == nullptr) {
            return;
          }
          if (auto* toggle = dialog->findChild<QPushButton*>(QLatin1String(button)); toggle != nullptr) {
            CHECK(toggle->isEnabled());
            CHECK(toggle->isChecked() == checked_before);
            toggle->click();
            process_events_for(50);
            flipped = toggle->isChecked() != checked_before;
          }
          dialog->close();
        } catch (...) {
          patchy::ui::unwind_non_modal_dialog_loop(std::current_exception());
        }
      });
      window.findChild<QPushButton*>(QStringLiteral("textCharacterButton"))->click();
      QApplication::processEvents();
      CHECK(flipped);
    }
    require_action_by_text(window, QStringLiteral("Move"))->trigger();
    QApplication::processEvents();
    process_events_for(200);
  };

  // Layer alpha at a DOCUMENT point, and the whole frame of it (the raster may move or grow).
  const auto alpha_at = [&](int x, int y) -> int {
    const auto* layer = std::as_const(*live).find_layer(layer_id);
    if (layer == nullptr) {
      return 0;
    }
    const auto& pixels = layer->pixels();
    const int local_x = x - layer->bounds().x;
    const int local_y = y - layer->bounds().y;
    if (local_x < 0 || local_y < 0 || local_x >= pixels.width() || local_y >= pixels.height()) {
      return 0;
    }
    return pixels.pixel(local_x, local_y)[3];
  };
  const auto snapshot = [&](int x0, int x1) {
    std::vector<int> alpha;
    for (int y = 0; y < kHeight; ++y) {
      for (int x = x0; x < x1; ++x) {
        alpha.push_back(alpha_at(x, y));
      }
    }
    return alpha;
  };
  constexpr int kInk = 12;
  const auto column_inked = [&](int x) {
    for (int y = 0; y < kHeight; ++y) {
      if (alpha_at(x, y) >= kInk) {
        return true;
      }
    }
    return false;
  };
  // The middle of the word space: the widest blank column run inside the ink.
  const auto word_split = [&]() -> int {
    int first = -1;
    int last = -1;
    for (int x = 0; x < kWidth; ++x) {
      if (column_inked(x)) {
        first = first < 0 ? x : first;
        last = x;
      }
    }
    int best_start = -1;
    int best_end = -1;
    int blank_start = -1;
    for (int x = first; first >= 0 && x <= last; ++x) {
      if (!column_inked(x)) {
        blank_start = blank_start < 0 ? x : blank_start;
      } else if (blank_start >= 0) {
        if (x - blank_start > best_end - best_start) {
          best_start = blank_start;
          best_end = x;
        }
        blank_start = -1;
      }
    }
    CHECK(best_start > first + 30 && best_end > best_start + 4 && last > best_end + 30);
    return best_start < 0 ? -1 : (best_start + best_end) / 2;
  };

  struct Style {
    const char* button;
    const char* name;
    int faux_column;     // the runs column of a faux flag, or -1
    const char* token;   // the v9 style token, or nullptr
    bool visible;        // the selected word must look different (the font has no ordn/frac to rely on)
  };
  const std::array<Style, 10> styles{{
      {"textCharacterFauxBold", "faux bold", 11, nullptr, true},
      {"textCharacterFauxItalic", "faux italic", 13, nullptr, true},
      {"textCharacterAllCapsButton", "all caps", -1, "allcaps", true},
      {"textCharacterSmallCapsButton", "small caps", -1, "smallcaps", true},
      {"textCharacterSuperscriptButton", "superscript", -1, "superscript", true},
      {"textCharacterSubscriptButton", "subscript", -1, "subscript", true},
      {"textCharacterUnderlineButton", "underline", -1, "underline", true},
      {"textCharacterStrikethroughButton", "strikethrough", -1, "strikethrough", true},
      {"textCharacterOrdinalsButton", "ordinals", -1, "ordinals", false},
      {"textCharacterFractionsButton", "fractions", -1, "fractions", false},
  }};
  const auto styled = [](const Style& style, const QStringList& fields) {
    if (style.faux_column >= 0) {
      return fields.size() > style.faux_column && fields[style.faux_column] == QStringLiteral("1");
    }
    return fields.size() > 16 && fields[16].split(QLatin1Char(',')).contains(QLatin1String(style.token));
  };
  // Failed checks are counted and described rather than thrown, so one run reports every style.
  int failures = 0;
  // Every run lies wholly inside or outside `ranges` and carries the style exactly when inside.
  const auto expect_runs = [&](const Style& style, const std::string& runs, std::initializer_list<std::pair<int, int>> ranges,
                               const char* step) {
    const auto inside = [&ranges](int at) {
      return std::ranges::any_of(ranges, [at](const auto& range) { return at >= range.first && at < range.second; });
    };
    int covered = 0;
    const auto lines = QString::fromStdString(runs).split(QLatin1Char('\n'));
    for (int index = 1; index < lines.size(); ++index) {
      const auto fields = lines[index].split(QLatin1Char('\t'));
      if (fields.size() < 2) {
        continue;
      }
      const int start = fields[0].toInt();
      const int end = start + fields[1].toInt();
      covered = std::max(covered, end);
      bool all = true;
      bool any = false;
      for (int at = start; at < end; ++at) {
        all = all && inside(at);
        any = any || inside(at);
      }
      const bool ok = all == any && styled(style, fields) == all;
      if (!ok) {
        std::printf("  %s, %s: run %d..%d styled=%d, expected %d\n", style.name, step, start, end,
                    styled(style, fields) ? 1 : 0, all ? 1 : 0);
      }
      failures += ok ? 0 : 1;
    }
    CHECK(covered == text.size());
  };
  const auto layer_runs = [&]() {
    const auto* layer = std::as_const(*live).find_layer(layer_id);
    return layer == nullptr ? std::string() : layer->metadata().at(patchy::kLayerMetadataTextRuns);
  };

  // Whether columns [x0, x1) still hold `before`, the whole line allowed to sit up to 2 px away:
  // a re-edit pins the layer's visible ink top-left (layer_source_visible_anchor), so a style that
  // raises the ink elsewhere (a superscript, faux bold's stroke) moves the line, not the word's
  // glyphs. A difference is counted and described, not thrown, so one run reports every style.
  const auto unchanged = [&](const std::vector<int>& before, int x0, int x1, const char* name, const char* step) {
    const int columns = x1 - x0;
    for (int dy = -2; dy <= 2; ++dy) {
      for (int dx = -2; dx <= 2; ++dx) {
        bool same = true;
        for (int index = 0; same && index < static_cast<int>(before.size()); ++index) {
          same = alpha_at(x0 + index % columns + dx, index / columns + dy) == before[static_cast<std::size_t>(index)];
        }
        if (same) {
          if (dx != 0 || dy != 0) {
            std::printf("  %s, %s: the line moved %d,%d px\n", name, step, dx, dy);
          }
          return;
        }
      }
    }
    // A fractional move re-rasterizes the glyphs on another sub-pixel phase; any style reaching the
    // word changes its ink far more than that (faux bold adds a quarter, faux italic leans the box
    // 9 px wider, a bar adds a band, caps and scripts change the letters).
    const auto ink = [](const std::vector<int>& alpha, int width) {
      long long mass = 0;
      int left = width;
      int right = -1;
      int top = kHeight;
      int bottom = -1;
      for (int index = 0; index < static_cast<int>(alpha.size()); ++index) {
        const auto value = alpha[static_cast<std::size_t>(index)];
        mass += value;
        if (value >= kInk) {
          left = std::min(left, index % width);
          right = std::max(right, index % width);
          top = std::min(top, index / width);
          bottom = std::max(bottom, index / width);
        }
      }
      return std::array<long long, 3>{mass, right - left, bottom - top};
    };
    const auto was = ink(before, columns);
    const auto now = ink(snapshot(x0, x1), columns);
    const bool close = was[0] > 0 && std::llabs(now[0] - was[0]) * 50 <= was[0] && std::llabs(now[1] - was[1]) <= 1 &&
                       std::llabs(now[2] - was[2]) <= 1;
    std::printf("  %s, %s: the unselected word %s (ink %lld -> %lld, %lldx%lld -> %lldx%lld)\n", name, step,
                close ? "moved by a fraction of a pixel" : "changed", was[0], now[0], was[1], was[2], now[1], now[2]);
    failures += close ? 0 : 1;
  };
  for (const auto& style : styles) {
    std::printf("  %s\n", style.name);
    std::fflush(stdout);
    fresh_layer(style.name);
    edit(0, 0, nullptr, false);
    const int split = word_split();
    if (split < 0) {
      return;
    }
    const auto plain_first = snapshot(0, split);
    const auto plain_second = snapshot(split, kWidth);

    // On the second word only: the first keeps its flags and its pixels.
    edit(4, 10, style.button, false);
    expect_runs(style, layer_runs(), {{4, 10}}, "second word on");
    unchanged(plain_first, 0, split, style.name, "second word on");
    if (style.visible && snapshot(split, kWidth) == plain_second) {
      ++failures;
      std::printf("  %s: the selected word did not change\n", style.name);
    }
    save_image_artifact(std::string("ui_text_character_styles_selection_") + style.button,
                        patchy::ui::qimage_from_pixel_buffer(std::as_const(*live).find_layer(layer_id)->pixels()));

    // On the first word too, then off on the second: the first stays styled and untouched.
    edit(0, 3, style.button, false);
    expect_runs(style, layer_runs(), {{0, 3}, {4, 10}}, "both words on");
    const int both_split = word_split();
    if (both_split < 0) {
      return;
    }
    const auto styled_first = snapshot(0, both_split);
    edit(4, 10, style.button, true);
    expect_runs(style, layer_runs(), {{0, 3}}, "second word off");
    unchanged(styled_first, 0, both_split, style.name, "second word off");

    // The runs survive a PSD save and reopen as they are.
    const auto reread = patchy::psd::DocumentIo::read(patchy::psd::DocumentIo::write_layered_rgb8(*live));
    bool reread_text = false;
    for (const auto& reread_layer : reread.layers()) {
      const auto found = reread_layer.metadata().find(patchy::kLayerMetadataTextRuns);
      if (found != reread_layer.metadata().end()) {
        reread_text = true;
        expect_runs(style, found->second, {{0, 3}}, "reopened");
      }
    }
    CHECK(reread_text);
  }
  std::fflush(stdout);
  CHECK(failures == 0);
}

// philipzae's sample (Photoshop CC 2018, GitHub issue 346) re-rendered by Patchy: every text
// layer redrawn from its runs, so the styles must come from the file's TySh and Txt2 keys rather
// than Photoshop's stored pixels. The artifact is the before/after shot on the issue.
void ui_text_character_styles_sample_rerenders_if_available() {
  const auto path = patchy::test::local_psd_fixture_path("Character - Styling and Extras.psd");
  patchy::test::skip_unless_exists(path);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  auto sample = patchy::psd::DocumentIo::read_file(path);
  // The Extras group is set in Myriad Pro, which the suite does not have; redrawing a layer whose
  // font is missing stops at the missing-font prompt, so those runs move to Arial (which has
  // fractions but no ordinals) first.
  std::function<void(std::vector<patchy::Layer>&)> substitute = [&](std::vector<patchy::Layer>& layers) {
    for (auto& layer : layers) {
      for (const auto* key : {patchy::kLayerMetadataTextRuns, patchy::kLayerMetadataTextFont}) {
        if (auto found = layer.metadata().find(key); found != layer.metadata().end()) {
          for (const std::string from : {"Myriad%20Pro", "Myriad Pro"}) {
            for (auto at = found->second.find(from); at != std::string::npos; at = found->second.find(from)) {
              found->second.replace(at, from.size(), "Arial");
            }
          }
        }
      }
      substitute(layer.children());
    }
  };
  substitute(sample.layers());
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(sample), QStringLiteral("Character - Styling and Extras"));
  QApplication::processEvents();
  CHECK(run_script(window, QStringLiteral(R"JS(
    var redraw = function (layers) {
      for (var i = 0; i < layers.length; ++i) {
        var layer = layers[i];
        if (layer.isText) layer.rerenderText();
        if (layer.children && layer.children.length) redraw(layer.children);
      }
    };
    redraw(app.activeDocument.layers);
  )JS")));
  auto& document = MainWindowTestAccess::document(window);
  // The file as saved (the Extras group hidden), then the Extras group alone.
  save_image_artifact("ui_text_character_styles_sample_rerendered",
                      patchy::ui::qimage_from_pixel_buffer(patchy::Compositor{}.flatten_rgb8(document)));
  CHECK(run_script(window, QStringLiteral(R"JS(
    var layers = app.activeDocument.layers;
    for (var i = 0; i < layers.length; ++i) {
      if (layers[i].name === 'Styling') layers[i].visible = false;
      if (layers[i].name === 'Extras') layers[i].visible = true;
    }
  )JS")));
  save_image_artifact("ui_text_character_styles_sample_extras_rerendered",
                      patchy::ui::qimage_from_pixel_buffer(patchy::Compositor{}.flatten_rgb8(document)));
  // The Photoshop open check's file: the re-rendered sample plus a Patchy-born layer carrying all
  // eight styles, saved as a PSD into test-artifacts.
  ensure_artifact_dir();
  const auto saved = QDir::current().absoluteFilePath(QStringLiteral("test-artifacts/ui_text_character_styles_sample.psd"));
  CHECK(run_script(window, QStringLiteral(R"JS(
    var doc = app.activeDocument;
    doc.addTextLayer([{text: 'all ', caps: 'all'}, {text: 'Small ', caps: 'small'}, {text: 'x'},
                      {text: '2', script: 'superscript'}, {text: 'H'}, {text: '2', script: 'subscript'},
                      {text: ' under', underline: true}, {text: ' struck', strikethrough: true},
                      {text: ' 1st', ordinals: true}, {text: ' 1/2', fractions: true}],
                     {font: 'Arial', size: 40, x: 20, y: 440, color: '#203080'});
    doc.saveAs('%1');
  )JS").arg(saved)));
  const patchy::Layer* underline = nullptr;
  const patchy::Layer* strike = nullptr;
  const patchy::Layer* superscript = nullptr;
  std::function<void(const std::vector<patchy::Layer>&)> find = [&](const std::vector<patchy::Layer>& layers) {
    for (const auto& layer : layers) {
      if (layer.name() == "Underline") underline = &layer;
      if (layer.name() == "Strikethrough") strike = &layer;
      if (layer.name() == "Superscript") superscript = &layer;
      find(layer.children());
    }
  };
  find(std::as_const(document).layers());
  CHECK(underline && strike && superscript);
  if (!underline || !strike || !superscript) {
    return;
  }
  CHECK(bar_edges(*underline).has_value());
  CHECK(bar_edges(*strike).has_value());
  const auto base = ink_of(*superscript, 0.0, 0.4);
  const auto raised = ink_of(*superscript, 0.7, 1.0);
  CHECK(base && raised);
  if (base && raised) {
    CHECK(raised->bottom < base->bottom - 10);
  }
}

}  // namespace

std::vector<patchy::test::TestCase> text_character_styles_tests() {
  return {
      {"ui_text_underline_and_strikethrough_match_photoshop_if_available",
       ui_text_underline_and_strikethrough_match_photoshop_if_available},
      {"ui_text_script_and_caps_match_photoshop_if_available", ui_text_script_and_caps_match_photoshop_if_available},
      {"ui_text_ordinals_and_fractions_use_the_font_features_if_available",
       ui_text_ordinals_and_fractions_use_the_font_features_if_available},
      {"ui_text_character_style_buttons_toggle_run_styles", ui_text_character_style_buttons_toggle_run_styles},
      {"ui_text_character_styles_apply_only_to_the_selection", ui_text_character_styles_apply_only_to_the_selection},
      {"ui_text_character_styles_sample_rerenders_if_available", ui_text_character_styles_sample_rerenders_if_available},
  };
}
