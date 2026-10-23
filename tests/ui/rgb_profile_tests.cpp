// RGB profiles in the app (docs/rgb-profiles.md): every reader family converts a Display P3
// file to sRGB on open with an Import Notes entry, the preference turns that off, Edit >
// Convert to Color Profile is one undo step, and doc.convertToProfile mirrors it.
#include "color/rgb_profiles.hpp"
#include "core/document.hpp"
#include "core/document_depth.hpp"
#include "core/pixel_depth.hpp"
#include "formats/avif_document_io.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/script_engine.hpp"
#include "ui/ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::MainWindowTestAccess;
using patchy::test::ui::scripting_support::wait_for_run_end;

const QString kConvertSetting = QStringLiteral("imports/convertRgbProfilesToSrgb");

std::vector<std::uint8_t> display_p3() {
  return patchy::create_rgb_matrix_profile(
      {"Display P3 (test)", 0.3127, 0.3290, 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.0});
}

QString artifact_dir() {
  const auto dir = QFileInfo(QStringLiteral("test-artifacts/rgb-profiles")).absoluteFilePath();
  QDir().mkpath(dir);
  return dir;
}

void set_convert_setting(bool enabled) {
  auto settings = patchy::ui::app_settings();
  settings.setValue(kConvertSetting, enabled);
  settings.sync();
}

std::array<std::uint8_t, 4> first_pixel(patchy::ui::MainWindow& window) {
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  for (const auto& layer : document.layers()) {
    if (layer.kind() == patchy::LayerKind::Pixel && !layer.pixels().empty()) {
      return patchy::display_rgba8_at(layer.pixels(), 0, 0);
    }
  }
  return {};
}

// An 8x8 (200, 100, 50) document tagged Display P3 written as `name`; `layered` adds a
// half-transparent top layer so PSD and TIFF keep their layers.
QString write_tagged_file(const QString& name, bool layered, std::optional<patchy::BitDepth> depth = std::nullopt) {
  const auto plain = artifact_dir() + QStringLiteral("/plain.png");
  QImage image(8, 8, QImage::Format_RGB888);
  image.fill(QColor(200, 100, 50));
  CHECK(image.save(plain));
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, plain);
  QApplication::processEvents();
  auto& document = MainWindowTestAccess::document(window);
  if (layered) {
    patchy::PixelBuffer top(8, 8, patchy::PixelFormat::rgba8());
    for (std::int32_t y = 0; y < 8; ++y) {
      for (std::int32_t x = 0; x < 8; ++x) {
        auto* px = top.pixel(x, y);
        px[0] = 20;
        px[1] = 200;
        px[2] = 90;
        px[3] = x < 4 ? 0 : 128;
      }
    }
    document.add_pixel_layer("Top", std::move(top));
  }
  if (depth.has_value()) {
    patchy::convert_document_depth(document, *depth);
  }
  document.color_state().embedded_icc_profile = display_p3();
  const auto path = artifact_dir() + QLatin1Char('/') + name;
  QFile::remove(path);
  CHECK(MainWindowTestAccess::save_document_to_path(window, path, patchy::ui::ImageSaveOptions{}));
  return path;
}

struct Opened {
  std::vector<std::uint8_t> profile;
  std::array<std::uint8_t, 4> pixel{};
  QString status;
  patchy::BitDepth depth{patchy::BitDepth::UInt8};
};

Opened open_with(const QString& path, bool convert) {
  set_convert_setting(convert);
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  return Opened{document.color_state().embedded_icc_profile, first_pixel(window), window.statusBar()->currentMessage(),
                document.color_state().bit_depth};
}

void ui_open_converts_rgb_profiles_to_srgb() {
  SettingsValueRestorer restore(kConvertSetting);
  SettingsValueRestorer notes(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::set_deep_editing_override(true);
  struct Case {
    QString name;
    bool layered;
    std::optional<patchy::BitDepth> depth;
  };
  std::vector<Case> cases{{QStringLiteral("p3.png"), false, std::nullopt},
                          {QStringLiteral("p3.jpg"), false, std::nullopt},
                          {QStringLiteral("p3.tif"), false, std::nullopt},
                          {QStringLiteral("p3.webp"), false, std::nullopt},
                          {QStringLiteral("p3.jxl"), false, std::nullopt},
                          {QStringLiteral("p3-16.png"), false, patchy::BitDepth::UInt16},
                          {QStringLiteral("p3-layers.psd"), true, std::nullopt},
                          {QStringLiteral("p3-layers.psb"), true, std::nullopt},
                          {QStringLiteral("p3-layers.tif"), true, std::nullopt},
                          {QStringLiteral("p3-16.psd"), true, patchy::BitDepth::UInt16}};
  if (patchy::avif::is_available()) {
    cases.push_back({QStringLiteral("p3.avif"), false, std::nullopt});
  }
  for (const auto& test : cases) {
    std::cerr << "  [rgb-profiles] " << test.name.toStdString() << '\n';
    const auto path = write_tagged_file(test.name, test.layered, test.depth);
    // Off: today's behavior, the file's values with its profile.
    const auto kept = open_with(path, false);
    // JPEG XL hands back a profile libjxl synthesizes from its encoded color space.
    const bool exact_profile = !test.name.endsWith(QStringLiteral(".jxl"));
    CHECK(exact_profile ? kept.profile == display_p3() : patchy::rgb_profile_needs_conversion(kept.profile));
    CHECK(!kept.status.contains(QStringLiteral("Converted from the color profile")));
    // On (the default): converted pixels, no profile, an import note.
    const auto converted = open_with(path, true);
    CHECK(converted.profile.empty());
    CHECK(converted.status.contains(exact_profile
                                        ? QStringLiteral("Converted from the color profile Display P3 (test) to sRGB")
                                        : QStringLiteral("Converted from the color profile ")));
    CHECK(converted.depth == kept.depth);
    const auto file_transform = patchy::RgbProfileTransform::create(
        kept.profile, {}, patchy::RenderingIntent::RelativeColorimetric, true);
    CHECK(file_transform.has_value());
    if (file_transform.has_value()) {
      const auto expected = file_transform->convert_color(patchy::RgbColor{kept.pixel[0], kept.pixel[1], kept.pixel[2]});
      CHECK(std::abs(converted.pixel[0] - expected.red) <= 2);
      CHECK(std::abs(converted.pixel[1] - expected.green) <= 2);
      CHECK(std::abs(converted.pixel[2] - expected.blue) <= 2);
      CHECK(converted.pixel[0] != kept.pixel[0] || converted.pixel[1] != kept.pixel[1]);
    }
  }
  patchy::set_deep_editing_override(std::nullopt);
}

// Before/after captures for the review card: a color chart in Display P3 opened with the
// preference off (the old behavior: P3 values shown as sRGB) and on (converted, with the
// import note in the status bar). Written to test-artifacts/rgb-profiles.
void ui_rgb_profile_open_captures() {
  SettingsValueRestorer restore(kConvertSetting);
  SettingsValueRestorer notes(QStringLiteral("imports/showPsdWarningsAndInfo"));
  patchy::ui::app_settings().remove(QStringLiteral("imports/showPsdWarningsAndInfo"));
  QImage chart(480, 280, QImage::Format_RGB888);
  for (int y = 0; y < 100; ++y) {
    for (int x = 0; x < chart.width(); ++x) {
      const double s = x / double(chart.width() - 1);
      const double t = y / 99.0;
      chart.setPixelColor(x, y, QColor(std::min(255, int(30 + 225 * s * (0.6 + 0.4 * t))),
                                       std::min(255, int(90 + 120 * (1 - s) * (1 - t) + 40 * t)),
                                       std::min(255, int(220 * (1 - s) + 35 * s))));
    }
  }
  const std::array<QColor, 8> patches{QColor(255, 0, 0),   QColor(255, 140, 0), QColor(255, 230, 0),
                                      QColor(0, 200, 60),  QColor(0, 190, 230), QColor(30, 60, 255),
                                      QColor(200, 0, 200), QColor(255, 80, 140)};
  for (int y = 100; y < 280; ++y) {
    for (int x = 0; x < chart.width(); ++x) {
      chart.setPixelColor(x, y, y < 200 ? patches[static_cast<std::size_t>(x * 8 / chart.width())]
                                        : QColor::fromRgb(x * 255 / (chart.width() - 1), x * 255 / (chart.width() - 1),
                                                          x * 255 / (chart.width() - 1)));
    }
  }
  const auto plain = artifact_dir() + QStringLiteral("/chart-srgb.png");
  CHECK(chart.save(plain));
  const auto tagged = artifact_dir() + QStringLiteral("/chart-display-p3.jpg");
  {
    set_convert_setting(false);
    patchy::ui::MainWindow window;
    show_window(window);
    MainWindowTestAccess::open_document_path(window, plain);
    QApplication::processEvents();
    CHECK(patchy::convert_document_rgb_profile(MainWindowTestAccess::document(window), display_p3(),
                                               patchy::RenderingIntent::RelativeColorimetric, true)
              .has_value());
    QFile::remove(tagged);
    CHECK(MainWindowTestAccess::save_document_to_path(window, tagged, patchy::ui::ImageSaveOptions{}));
  }
  for (const bool convert : {false, true}) {
    set_convert_setting(convert);
    patchy::ui::MainWindow window;
    window.resize(1000, 640);
    show_window(window);
    MainWindowTestAccess::open_document_path(window, tagged);
    QApplication::processEvents();
    CHECK(window.grab().save(artifact_dir() + (convert ? QStringLiteral("/after-open-converted.png")
                                                        : QStringLiteral("/before-open-kept.png"))));
  }
}

// The preference: on by default, and the checkbox writes it.
void ui_preferences_convert_rgb_profiles_checkbox() {
  SettingsValueRestorer restore(kConvertSetting);
  patchy::ui::app_settings().remove(kConvertSetting);
  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) return;
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesConvertRgbProfilesCheck"));
    CHECK(check != nullptr);
    if (check != nullptr) {
      CHECK(check->isChecked());
      check->setChecked(false);
    }
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(!patchy::ui::app_settings().value(kConvertSetting, true).toBool());
}

void ui_convert_to_profile_dialog_converts_and_undoes() {
  SettingsValueRestorer restore(kConvertSetting);
  SettingsValueRestorer destination_setting(QStringLiteral("color/convertProfileDestination"));
  patchy::ui::app_settings().remove(QStringLiteral("color/convertProfileDestination"));
  const auto path = write_tagged_file(QStringLiteral("p3-dialog.png"), false);
  set_convert_setting(false);
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::open_document_path(window, path);
  QApplication::processEvents();
  const auto before = first_pixel(window);
  CHECK(std::as_const(MainWindowTestAccess::document(window)).color_state().embedded_icc_profile == display_p3());

  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("convertToProfileDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) return;
    auto* source = dialog->findChild<QLabel*>(QStringLiteral("convertToProfileSourceLabel"));
    CHECK(source != nullptr && source->text() == QStringLiteral("Display P3 (test)"));
    auto* destination = dialog->findChild<QComboBox*>(QStringLiteral("convertToProfileDestinationCombo"));
    CHECK(destination != nullptr && destination->currentIndex() == 0);
    CHECK(destination != nullptr && destination->currentText() == QString::fromLatin1(patchy::kSrgbProfileName));
    CHECK(dialog->windowTitle() == QStringLiteral("Convert to Color Profile"));
    // The dialog says where the destination list comes from: sRGB built in, the rest installed.
    auto* sources = dialog->findChild<QLabel*>(QStringLiteral("convertToProfileSourcesNote"));
    CHECK(sources != nullptr && sources->text().contains(QStringLiteral("installed on this computer")));
    auto* intent = dialog->findChild<QComboBox*>(QStringLiteral("convertToProfileIntentCombo"));
    CHECK(intent != nullptr &&
          intent->currentData().toInt() == static_cast<int>(patchy::RenderingIntent::RelativeColorimetric));
    auto* black_point = dialog->findChild<QCheckBox*>(QStringLiteral("convertToProfileBlackPointCheck"));
    CHECK(black_point != nullptr && black_point->isChecked());
    dialog->grab().save(artifact_dir() + QStringLiteral("/convert-to-profile-dialog.png"));
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "editConvertToProfileAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  const auto& document = std::as_const(MainWindowTestAccess::document(window));
  CHECK(document.color_state().embedded_icc_profile.empty());
  const auto after = first_pixel(window);
  const auto transform =
      patchy::RgbProfileTransform::create(display_p3(), {}, patchy::RenderingIntent::RelativeColorimetric, true);
  CHECK(transform.has_value());
  if (transform.has_value()) {
    const auto expected = transform->convert_color(patchy::RgbColor{before[0], before[1], before[2]});
    CHECK(after[0] == expected.red && after[1] == expected.green && after[2] == expected.blue);
  }
  CHECK(window.statusBar()->currentMessage().contains(QStringLiteral("Converted from Display P3 (test) to")));
  MainWindowTestAccess::undo(window);
  QApplication::processEvents();
  CHECK(std::as_const(MainWindowTestAccess::document(window)).color_state().embedded_icc_profile == display_p3());
  CHECK(first_pixel(window) == before);
}

void ui_script_convert_to_profile() {
  SettingsValueRestorer restore(kConvertSetting);
  const auto path = write_tagged_file(QStringLiteral("p3-script.png"), false);
  set_convert_setting(false);
  const auto profile_path = artifact_dir() + QStringLiteral("/display-p3-test.icc");
  {
    const auto bytes = display_p3();
    QFile file(profile_path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
  }
  patchy::ui::MainWindow window;
  show_window(window);
  auto& host = window.script_engine_host();
  patchy::ui::ScriptEngineHost::RunOptions options;
  options.name = QStringLiteral("convert-to-profile");
  options.args = QStringList{QStringLiteral("file=") + path, QStringLiteral("icc=") + profile_path};
  (void)host.run_source(QStringLiteral(R"JS(
    var d = app.open(patchy.args.file);
    if (d.colorProfile !== 'Display P3 (test)') throw Error('profile ' + d.colorProfile);
    var red = function () { return new Uint8Array(d.layers[0].getPixels().data)[0]; };
    var before = red();
    var refused = 0;
    try { d.convertToProfile('sRGB', {intent: 'sideways'}); } catch (e) { refused++; }
    try { d.convertToProfile('sRGB', {blackPointCompensation: 'yes'}); } catch (e) { refused++; }
    try { d.convertToProfile(patchy.args.file); } catch (e) { refused++; }
    if (refused !== 3) throw Error('refused ' + refused);
    if (d.colorProfile !== 'Display P3 (test)') throw Error('a refused call converted');
    d.convertToProfile('sRGB');
    if (d.colorProfile !== 'sRGB IEC61966-2.1') throw Error('after ' + d.colorProfile);
    var after = red();
    if (after === before) throw Error('pixels unchanged');
    d.convertToProfile(patchy.args.icc, {intent: 'perceptual', blackPointCompensation: false});
    if (d.colorProfile !== 'Display P3 (test)') throw Error('to file ' + d.colorProfile);
    console.log('done');
  )JS"),
                        std::move(options));
  wait_for_run_end(host);
  if (host.last_run_had_error()) {
    for (const auto& line : host.message_backlog()) std::cerr << line.toStdString() << '\n';
  }
  CHECK(!host.last_run_had_error());
}

}  // namespace

std::vector<patchy::test::TestCase> rgb_profile_ui_tests() {
  return {
      {"ui_open_converts_rgb_profiles_to_srgb", ui_open_converts_rgb_profiles_to_srgb},
      {"ui_rgb_profile_open_captures", ui_rgb_profile_open_captures},
      {"ui_preferences_convert_rgb_profiles_checkbox", ui_preferences_convert_rgb_profiles_checkbox},
      {"ui_convert_to_profile_dialog_converts_and_undoes", ui_convert_to_profile_dialog_converts_and_undoes},
      {"ui_script_convert_to_profile", ui_script_convert_to_profile},
  };
}
