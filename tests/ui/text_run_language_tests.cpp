// A text run's Photoshop language reaches the shaper (GitHub issue 683, follow-up 4): Patchy
// shapes the run with a copy of its face whose default OpenType language system is the run's
// language (src/ui/text_language_font.hpp).

#include "core/document.hpp"
#include "core/layer_metadata.hpp"
#include "local_psd_fixtures.hpp"
#include "psd/psd_document_io.hpp"
#include "render/compositor.hpp"
#include "test_fonts.hpp"
#include "test_harness.hpp"
#include "ui/font_face_name_index.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/script_engine.hpp"
#include "ui/text_language_font.hpp"
#include "ui/text_render.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

using namespace patchy::test::ui;

namespace {

void append_u16(QByteArray& bytes, quint16 value) {
  bytes.append(static_cast<char>(value >> 8));
  bytes.append(static_cast<char>(value & 0xFFU));
}

quint16 u16_at(const QByteArray& bytes, qsizetype at) {
  return static_cast<quint16>((static_cast<quint8>(bytes[at]) << 8) | static_cast<quint8>(bytes[at + 1]));
}

QList<quint32> shaped_glyphs(const QString& text, const QFont& font) {
  QTextLayout layout(text, font);
  QTextOption option;
  option.setWrapMode(QTextOption::NoWrap);
  option.setUseDesignMetrics(true);
  layout.setTextOption(option);
  layout.beginLayout();
  auto line = layout.createLine();
  layout.endLayout();
  QList<quint32> glyphs;
  if (line.isValid()) {
    for (const auto& run : line.glyphRuns()) {
      glyphs += run.glyphIndexes();
    }
  }
  return glyphs;
}

// Runs v9: one run over `length` characters in `family` at 60 px, column 16 `styles`.
QString one_run(int length, const QString& family, const QString& styles) {
  return QStringLiteral("v9\n0\t%1\t60\t0\t0\t#000000\t%2\tauto\t0\t1\t1\t0\t\t0\t0\t0\t%3")
      .arg(length)
      .arg(family, styles);
}

patchy::PixelBuffer render_run(const QString& text, const QString& family, const QString& styles) {
  patchy::ui::TextToolSettings settings;
  settings.text = text;
  settings.family = family;
  settings.size = 60;
  return patchy::ui::render_text_pixels_with_local_rect(settings, QColor(Qt::black), 4096, QString(),
                                                        one_run(static_cast<int>(text.size()), family, styles))
      .pixels;
}

bool same_pixels(const patchy::PixelBuffer& a, const patchy::PixelBuffer& b) {
  return a.width() == b.width() && a.height() == b.height() && std::ranges::equal(a.data(), b.data());
}

bool run_script_to_end(patchy::ui::MainWindow& window, const QString& source) {
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("run-language-test-script");
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

// A registered font for the length of a test.
struct ScopedFont {
  explicit ScopedFont(const std::filesystem::path& path)
      : id(QFontDatabase::addApplicationFont(patchy::ui::to_qstring(path))),
        family(QFontDatabase::applicationFontFamilies(id).value(0)) {}
  ~ScopedFont() {
    if (id >= 0) {
      QFontDatabase::removeApplicationFont(id);
    }
  }
  ScopedFont(const ScopedFont&) = delete;
  ScopedFont& operator=(const ScopedFont&) = delete;
  int id;
  QString family;
};

}  // namespace

// The script list rewrite on a hand-built GSUB: 'arab' with ARA and FAR language systems and
// 'latn' with TRK. Asking for ARA makes the arab default point at the ARA LangSys and drops every
// script's language records; a language no script lists leaves the table alone.
void ui_text_language_layout_table_uses_the_language_system() {
  QByteArray table;
  append_u16(table, 1);   // majorVersion
  append_u16(table, 0);   // minorVersion
  append_u16(table, 10);  // ScriptList at 10
  append_u16(table, 0);   // FeatureList (unused here)
  append_u16(table, 0);   // LookupList
  // ScriptList at 10: two records, Script tables at +14 (arab, 24) and +48 (latn, 58).
  append_u16(table, 2);
  table.append("arab", 4);
  append_u16(table, 14);
  table.append("latn", 4);
  append_u16(table, 48);
  // arab Script at 24: default LangSys at +16, ARA at +22, FAR at +28.
  append_u16(table, 16);
  append_u16(table, 2);
  table.append("ARA ", 4);
  append_u16(table, 22);
  table.append("FAR ", 4);
  append_u16(table, 28);
  for (int langsys = 0; langsys < 3; ++langsys) {  // three empty LangSys tables, 6 bytes each
    append_u16(table, 0);
    append_u16(table, 0xFFFF);
    append_u16(table, 0);
  }
  // latn Script at 58: default LangSys at +10, TRK at +10 too.
  append_u16(table, 10);
  append_u16(table, 1);
  table.append("TRK ", 4);
  append_u16(table, 10);
  append_u16(table, 0);
  append_u16(table, 0xFFFF);
  append_u16(table, 0);
  CHECK(table.size() == 74);

  const auto arabic = patchy::ui::layout_table_for_language(table, {QByteArrayLiteral("ARA ")});
  CHECK(arabic.has_value());
  if (arabic.has_value()) {
    CHECK(arabic->size() == table.size());
    CHECK(u16_at(*arabic, 24) == 22);  // arab default is now the ARA LangSys
    CHECK(u16_at(*arabic, 26) == 0);   // and lists no languages
    CHECK(u16_at(*arabic, 58) == 10);  // latn keeps its default
    CHECK(u16_at(*arabic, 60) == 0);   // without its TRK record
  }
  // The first tag the script lists wins: Farsi before the fallback.
  const auto farsi = patchy::ui::layout_table_for_language(table, {QByteArrayLiteral("FAR "), QByteArrayLiteral("ARA ")});
  CHECK(farsi.has_value() && u16_at(*farsi, 24) == 28);
  CHECK(!patchy::ui::layout_table_for_language(table, {QByteArrayLiteral("ROM ")}).has_value());
  CHECK(!patchy::ui::layout_table_for_language(table.left(20), {QByteArrayLiteral("ARA ")}).has_value());

  // The Photoshop language map: English and unset shape as before.
  CHECK(patchy::ui::opentype_language_tags_for_photoshop_language(39) == QList<QByteArray>{QByteArrayLiteral("ARA ")});
  CHECK(patchy::ui::opentype_language_tags_for_photoshop_language(41) == QList<QByteArray>{QByteArrayLiteral("FAR ")});
  CHECK(patchy::ui::opentype_language_tags_for_photoshop_language(24).value(0) == QByteArrayLiteral("ROM "));
  for (const int english : {-1, 0, 14, 45, 29, 999}) {
    CHECK(patchy::ui::opentype_language_tags_for_photoshop_language(english).isEmpty());
  }
}

// Patchy's bundled Noto Naskh Arabic files a Farsi form of the Arabic decimal separator (U+066B)
// under 'FAR '. A Farsi run gets the face's language copy and that form; English and Arabic (which
// the face does not list) keep the face as it is, and the copy is never offered as a font.
void ui_text_run_language_reaches_the_shaper() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto path = std::filesystem::path(PATCHY_SOURCE_DIR) / "third_party" / "fonts" / "noto_naskh_arabic" /
                    "NotoNaskhArabic-Regular.ttf";
  patchy::test::skip_unless_exists(path);
  const ScopedFont naskh(path);
  CHECK(!naskh.family.isEmpty());
  if (naskh.family.isEmpty()) {
    return;
  }
  QFont font(naskh.family);
  font.setPixelSize(60);
  const auto farsi = patchy::ui::text_language_font_families(font, 41);
  CHECK(farsi.size() == 1);
  CHECK(patchy::ui::is_text_language_font_family(farsi.value(0)));
  CHECK(patchy::ui::text_language_font_families(font, 0).isEmpty());
  CHECK(patchy::ui::text_language_font_families(font, 39).isEmpty());
  // Asked again, the same copy (registered once).
  CHECK(patchy::ui::text_language_font_families(font, 41) == farsi);
  const auto text = QStringLiteral("١٫٢");
  auto in_farsi = font;
  in_farsi.setFamilies(farsi);
  const auto plain = shaped_glyphs(text, font);
  const auto shaped = shaped_glyphs(text, in_farsi);
  std::printf("  %s: %s, Farsi copy %s\n", naskh.family.toUtf8().constData(),
              QString::number(plain.value(1)).toUtf8().constData(), QString::number(shaped.value(1)).toUtf8().constData());
  std::fflush(stdout);
  CHECK(plain.size() == 3 && shaped.size() == 3);
  CHECK(plain != shaped);
  // Only the separator changes: the digits are the same glyphs, so the copy keeps the glyph ids.
  CHECK(plain.value(0) == shaped.value(0) && plain.value(2) == shaped.value(2));
  const auto listed = patchy::ui::font_families_without_foundries(QFontDatabase::families());
  CHECK(QFontDatabase::families().contains(farsi.value(0)));
  CHECK(!listed.contains(farsi.value(0)));
}

// End to end through the text render: Noto Kufi Arabic (OFL, local fixture) has Romanian
// comma-below forms under 'ROM '. A run whose language is Romanian (24) draws them; English (0)
// and no language draw the same pixels as before.
void ui_text_run_language_renders_romanian_forms_if_available() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto path = patchy::test::local_fixtures_root() / "fonts" / "NotoKufiArabic-Regular.ttf";
  patchy::test::skip_unless_exists(path);
  const ScopedFont kufi(path);
  CHECK(!kufi.family.isEmpty());
  if (kufi.family.isEmpty()) {
    return;
  }
  const auto text = QStringLiteral("şţ");  // s and t with cedilla
  const auto unset = render_run(text, kufi.family, QStringLiteral("-"));
  const auto english = render_run(text, kufi.family, QStringLiteral("language:0"));
  const auto romanian = render_run(text, kufi.family, QStringLiteral("language:24"));
  CHECK(unset.width() > 0 && romanian.width() > 0);
  CHECK(same_pixels(unset, english));
  CHECK(!same_pixels(unset, romanian));
  save_image_artifact("ui_text_run_language_romanian_unset", image_from_pixels_for_visuals(unset));
  save_image_artifact("ui_text_run_language_romanian", image_from_pixels_for_visuals(romanian));
}

// Adobe Arabic 3.005 (Photoshop 2018's, local fixture; Adobe's, never committed) builds the Allah
// ligature under its ARA language system with Discretionary Ligatures off, and only with them on
// otherwise (a Contextual Alternates rule there). A run whose language is Arabic forms it with
// Contextual Alternates at their default, English does not (philipzae's video).
void ui_text_run_language_forms_the_allah_ligature_if_available() {
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const auto path =
      patchy::test::local_fixtures_root() / "fonts" / "adobe-arabic-3.005" / "AdobeArabic-Regular.otf";
  patchy::test::skip_unless_exists(path);
  const ScopedFont adobe(path);
  CHECK(!adobe.family.isEmpty());
  if (adobe.family.isEmpty()) {
    return;
  }
  const auto word = QStringLiteral("الله");
  QFont font(adobe.family);
  font.setPixelSize(60);
  const auto arabic = patchy::ui::text_language_font_families(font, 39);
  CHECK(arabic.size() == 1);
  // The ligature is a calt rule of the ARA language system: Contextual Alternates stay on in an
  // Arabic run (text_language_keeps_contextual_alternates).
  CHECK(patchy::ui::text_language_keeps_contextual_alternates(39));
  CHECK(!patchy::ui::text_language_keeps_contextual_alternates(24));
  font.setFeature(QFont::Tag("calt"), 0);
  auto in_arabic = font;
  in_arabic.setFamilies(arabic);
  in_arabic.setFeature(QFont::Tag("calt"), 1);
  const auto apart = shaped_glyphs(word, font).size();
  const auto joined = shaped_glyphs(word, in_arabic).size();
  std::printf("  %s: %d glyphs, %d in Arabic\n", adobe.family.toUtf8().constData(), static_cast<int>(apart),
              static_cast<int>(joined));
  std::fflush(stdout);
  CHECK(apart == 4);
  CHECK(joined == 1);
  const auto english = render_run(word, adobe.family, QStringLiteral("language:0"));
  const auto in_language = render_run(word, adobe.family, QStringLiteral("language:39"));
  CHECK(!same_pixels(english, in_language));
  save_image_artifact("ui_text_run_language_allah_english", image_from_pixels_for_visuals(english));
  save_image_artifact("ui_text_run_language_allah_arabic", image_from_pixels_for_visuals(in_language));
}

// philipzae's "0220 - Text Direction - RTL - Arabic" file: its Diacritics layers are Adobe Arabic
// runs in Arabic. With Adobe Arabic 3.005 loaded, a re-render forms the Allah ligature as his
// Photoshop 2018 raster shows; the same layer with its languages taken out (what Patchy drew
// before, when it ignored them) draws lam, lam and heh apart.
void ui_issue_683_diacritics_layers_form_the_allah_ligature_if_available() {
  const auto psd = patchy::test::local_psd_fixture_path("issue-683/arabic-rtl-settings-photoshop.psd");
  const auto font_path =
      patchy::test::local_fixtures_root() / "fonts" / "adobe-arabic-3.005" / "AdobeArabic-Regular.otf";
  patchy::test::skip_unless_exists(psd);
  patchy::test::skip_unless_exists(font_path);
  patchy::test::register_test_fonts(patchy::test::TestFontRole::UiDefault);
  const ScopedFont adobe(font_path);
  CHECK(adobe.family == QStringLiteral("Adobe Arabic"));
  auto document = patchy::psd::DocumentIo::read_file(psd);
  std::vector<std::pair<patchy::LayerId, std::string>> diacritics;
  for (auto& layer : document.layers()) {
    if (!patchy::layer_is_text(layer)) {
      continue;
    }
    layer.metadata()[patchy::kLayerMetadataTextRasterStatus] = "patchy_raster";
    if (layer.name().find("Diacretics") != std::string::npos) {
      diacritics.emplace_back(layer.id(), layer.name());
    }
  }
  CHECK(!diacritics.empty());
  // A twin of each Diacritics layer with every run's language taken out.
  for (const auto& [id, name] : std::vector(diacritics)) {
    const auto* source = std::as_const(document).find_layer(id);
    auto runs = QString::fromStdString(source->metadata().at(patchy::kLayerMetadataTextRuns));
    std::printf("  %s: %s\n", name.c_str(), runs.contains(QStringLiteral("language:39")) ? "Arabic runs" : "no Arabic run");
    CHECK(runs.contains(QStringLiteral("language:39")));
    runs.replace(QStringLiteral("language:39"), QString());
    patchy::Layer twin(document.allocate_layer_id(), name + " (no language)", source->pixels());
    twin.set_bounds(source->bounds());
    twin.metadata() = source->metadata();
    twin.metadata()[patchy::kLayerMetadataTextRuns] = runs.toStdString();
    document.add_layer(std::move(twin));
  }
  std::fflush(stdout);
  patchy::ui::MainWindow window;
  show_window(window);
  window.add_document_session(std::move(document), QStringLiteral("Arabic 683 settings"));
  QApplication::processEvents();
  CHECK(run_script_to_end(window, QStringLiteral(R"JS(
    for (const layer of app.activeDocument.layers) {
      if (layer.isText && layer.name.indexOf('Diacretics') >= 0) {
        layer.rerenderText();
      }
    }
  )JS")));
  const auto& live = patchy::ui::MainWindowTestAccess::document(window);
  int index = 0;
  for (const auto& [id, name] : diacritics) {
    const auto* layer = std::as_const(live).find_layer(id);
    const patchy::Layer* twin = nullptr;
    for (const auto& candidate : live.layers()) {
      if (candidate.name() == name + " (no language)") {
        twin = &candidate;
      }
    }
    CHECK(layer != nullptr && twin != nullptr);
    if (layer == nullptr || twin == nullptr) {
      continue;
    }
    // The ligature is narrower than the three letters it replaces.
    CHECK(!same_pixels(layer->pixels(), twin->pixels()));
    save_image_artifact("issue_683_diacritics_" + std::to_string(index) + "_arabic",
                        image_from_pixels_for_visuals(layer->pixels()));
    save_image_artifact("issue_683_diacritics_" + std::to_string(index) + "_no_language",
                        image_from_pixels_for_visuals(twin->pixels()));
    ++index;
  }
}

std::vector<patchy::test::TestCase> text_run_language_tests() {
  return {
      {"ui_text_language_layout_table_uses_the_language_system", ui_text_language_layout_table_uses_the_language_system},
      {"ui_text_run_language_reaches_the_shaper", ui_text_run_language_reaches_the_shaper},
      {"ui_text_run_language_renders_romanian_forms_if_available",
       ui_text_run_language_renders_romanian_forms_if_available},
      {"ui_text_run_language_forms_the_allah_ligature_if_available",
       ui_text_run_language_forms_the_allah_ligature_if_available},
      {"ui_issue_683_diacritics_layers_form_the_allah_ligature_if_available",
       ui_issue_683_diacritics_layers_form_the_allah_ligature_if_available},
  };
}
