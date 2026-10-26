// Theme packages (docs/theme-files.md, "Theme packages"): folder and zip loading,
// the path and size limits, icon overrides with their fallback, the backdrop
// painter and the scroll-bar gutter, the folder auto-reload, the Preferences
// entries and the theme sound events.

#include "formats/ora_document_io.hpp"
#include "core/document.hpp"
#include "test_harness.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/icon_theme.hpp"
#include "ui/main_window.hpp"
#include "ui/main_window_shared.hpp"
#include "ui/qt_paths.hpp"
#include "ui/sound_effects.hpp"
#include "ui/theme_backdrop.hpp"
#include "ui/theme_file.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_package.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_qss.hpp"
#include "ui/theme_sounds.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace patchy::test::ui;

using patchy::ui::ThemeManager;

// Redirects user_themes_directory() to a scratch folder for one test.
class PackageThemesDir {
public:
  PackageThemesDir() : restore_("PATCHY_THEMES_DIR") {
    CHECK(dir_.isValid());
    qputenv("PATCHY_THEMES_DIR", dir_.path().toUtf8());
  }
  [[nodiscard]] QString path() const { return dir_.path(); }
  [[nodiscard]] QString file(const QString& relative) const { return QDir(dir_.path()).filePath(relative); }

private:
  EnvironmentVariableRestorer restore_;
  QTemporaryDir dir_;
};

void write_bytes(const QString& path, const QByteArray& bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(bytes);
  file.close();
}

QByteArray png_bytes(const QImage& image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes;
}

QImage solid_image(int width, int height, QColor color) {
  QImage image(width, height, QImage::Format_ARGB32);
  image.fill(color);
  return image;
}

// An 8x8 tile: red left half, blue right half, so any anchoring error shows.
QImage split_tile() {
  QImage image(8, 8, QImage::Format_ARGB32);
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      image.setPixelColor(x, y, x < 4 ? QColor(220, 30, 30) : QColor(30, 30, 220));
    }
  }
  return image;
}

QByteArray manifest_json(const QColor& window_bg, const QJsonObject& extra = {}) {
  auto palette = patchy::ui::dark_palette();
  palette.window_bg = window_bg;
  auto object = QJsonDocument::fromJson(
                    patchy::ui::serialize_theme_to_json(palette, patchy::ui::ColorScheme::Dark, QStringLiteral("Pkg")))
                    .object();
  for (auto it = extra.begin(); it != extra.end(); ++it) {
    object.insert(it.key(), it.value());
  }
  return QJsonDocument(object).toJson();
}

QByteArray red_svg() {
  return QByteArrayLiteral(
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><rect width=\"32\" height=\"32\" "
      "fill=\"#ff0000\"/></svg>");
}

// An SVG drawn in the authored icon ink, which recolorIcons turns into the theme's.
QByteArray ink_svg() {
  return QByteArrayLiteral(
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><rect width=\"32\" height=\"32\" "
      "fill=\"#dce2eb\"/></svg>");
}

// A folder package with an SVG override, a bitmap override with @2x, a tiled
// backdrop and the save-done sound.
void write_sample_package(const QString& root, const QColor& window_bg, const QJsonObject& extra = {}) {
  QJsonObject keys = extra;
  if (!keys.contains(QStringLiteral("backdrop"))) {
    keys.insert(QStringLiteral("backdrop"),
                QJsonObject{{QStringLiteral("image"), QStringLiteral("images/tile.png")},
                            {QStringLiteral("mode"), QStringLiteral("tile")}});
  }
  write_bytes(QDir(root).filePath(QStringLiteral("theme.json")), manifest_json(window_bg, keys));
  write_bytes(QDir(root).filePath(QStringLiteral("icons/apply.svg")), red_svg());
  write_bytes(QDir(root).filePath(QStringLiteral("icons/cancel.png")), png_bytes(solid_image(16, 16, QColor(0, 200, 0))));
  write_bytes(QDir(root).filePath(QStringLiteral("icons/cancel@2x.png")),
              png_bytes(solid_image(32, 32, QColor(0, 0, 200))));
  write_bytes(QDir(root).filePath(QStringLiteral("images/tile.png")), png_bytes(split_tile()));
  write_bytes(QDir(root).filePath(QStringLiteral("sounds/save-done.wav")),
              patchy::ui::build_tone_wav(660.0, 40, 0.5, patchy::ui::ToneWave::Sine));
}

std::vector<std::uint8_t> to_vector(const QByteArray& bytes) {
  const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.constData());
  return {data, data + bytes.size()};
}

QByteArray zip_of(const std::vector<std::pair<std::string, QByteArray>>& files) {
  std::vector<patchy::ora::detail::ZipEntry> entries;
  for (const auto& [name, bytes] : files) {
    entries.push_back({name, to_vector(bytes), true});
  }
  const auto zip = patchy::ora::detail::zip_archive(entries);
  return QByteArray(reinterpret_cast<const char*>(zip.data()), static_cast<qsizetype>(zip.size()));
}

QColor icon_center(const QIcon& icon, int size, qreal ratio = 1.0) {
  const auto image = icon.pixmap(QSize(size, size), ratio).toImage();
  return image.pixelColor(image.width() / 2, image.height() / 2);
}

void apply_package(const QString& id) {
  auto result = patchy::ui::load_theme_by_id(id);
  CHECK(result.theme.has_value());
  if (result.theme) {
    ThemeManager::instance().set_custom_theme(id, *result.theme, /*persist=*/false);
  }
  QApplication::processEvents();
}

void ui_theme_package_folder_loads_icons_backdrop_and_sounds() {
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Sample")), QColor(10, 20, 30),
                       QJsonObject{{QStringLiteral("recolorIcons"), false},
                                   {QStringLiteral("iconScaling"), QStringLiteral("nearest")}});
  // A folder without theme.json is not a theme.
  QDir().mkpath(themes.file(QStringLiteral("NotATheme/icons")));

  const auto ids = patchy::ui::user_theme_ids();
  CHECK(ids.contains(QStringLiteral("Sample")));
  CHECK(!ids.contains(QStringLiteral("NotATheme")));
  CHECK(patchy::ui::is_theme_package_directory_id(QStringLiteral("Sample")));

  const auto result = patchy::ui::load_theme_by_id(QStringLiteral("Sample"));
  CHECK(result.error.isEmpty());
  CHECK(result.theme.has_value());
  if (!result.theme) {
    return;
  }
  CHECK(result.theme->palette.window_bg == QColor(10, 20, 30));
  CHECK(!result.theme->options.recolor_icons);
  CHECK(result.theme->options.icon_scaling == patchy::ui::ThemeIconScaling::Nearest);
  const auto* assets = result.theme->assets.get();
  CHECK(assets != nullptr);
  if (assets == nullptr) {
    return;
  }
  CHECK(assets->icons.value(QStringLiteral("apply")).svg == red_svg());
  CHECK(assets->icons.value(QStringLiteral("cancel")).image.size() == QSize(16, 16));
  CHECK(assets->icons.value(QStringLiteral("cancel")).image_2x.size() == QSize(32, 32));
  CHECK(assets->backdrop.size() == QSize(8, 8));
  CHECK(assets->options.backdrop_mode == patchy::ui::ThemeBackdropMode::Tile);
  CHECK(!patchy::ui::theme_sound_for_event(assets, patchy::ui::ThemeSoundEvent::SaveDone).isEmpty());
  CHECK(patchy::ui::theme_sound_for_event(assets, patchy::ui::ThemeSoundEvent::Error).isEmpty());
  CHECK(assets->files.contains(QStringLiteral("images/tile.png")));
  CHECK(!assets->files.contains(QStringLiteral("theme.json")));
}

void ui_theme_package_zip_loads_from_root_and_from_one_top_folder() {
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Source")), QColor(40, 50, 60));
  const auto source = patchy::ui::load_theme_by_id(QStringLiteral("Source"));
  CHECK(source.theme.has_value());
  if (!source.theme) {
    return;
  }

  // Export Theme Package's own output: theme.json at the root.
  const auto exported = patchy::ui::serialize_theme_package_zip(source.theme->palette, source.theme->base,
                                                                QStringLiteral("Zipped"), source.theme->assets.get());
  write_bytes(themes.file(QStringLiteral("Zipped.zip")), exported);
  CHECK(patchy::ui::user_theme_ids().contains(QStringLiteral("Zipped.zip")));
  const auto zipped = patchy::ui::load_theme_by_id(QStringLiteral("Zipped.zip"));
  CHECK(zipped.error.isEmpty());
  CHECK(zipped.theme.has_value());
  if (zipped.theme) {
    CHECK(zipped.theme->name == QStringLiteral("Zipped"));
    CHECK(zipped.theme->palette.window_bg == QColor(40, 50, 60));
    CHECK(zipped.theme->assets != nullptr && zipped.theme->assets->files == source.theme->assets->files);
    CHECK(zipped.theme->assets->options.backdrop_image == QStringLiteral("images/tile.png"));
    CHECK(!zipped.theme->assets->backdrop.isNull());
  }

  // A zipped folder: everything inside one top folder, plus a stray file beside it.
  const auto nested = zip_of({{"My Theme/theme.json", manifest_json(QColor(1, 2, 3))},
                              {"My Theme/icons/apply.svg", red_svg()},
                              {"My Theme/notes.txt", QByteArrayLiteral("ignored")}});
  const auto from_folder = patchy::ui::load_theme_package_zip(nested);
  CHECK(from_folder.error.isEmpty());
  CHECK(from_folder.theme.has_value());
  if (from_folder.theme) {
    CHECK(from_folder.theme->palette.window_bg == QColor(1, 2, 3));
    CHECK(from_folder.theme->assets->icons.contains(QStringLiteral("apply")));
    CHECK(!from_folder.theme->assets->files.contains(QStringLiteral("notes.txt")));
  }

  // Two candidate folders, or none: no manifest to pick.
  const auto ambiguous = zip_of({{"a/theme.json", manifest_json(QColor(1, 2, 3))},
                                 {"b/theme.json", manifest_json(QColor(1, 2, 3))}});
  CHECK(!patchy::ui::load_theme_package_zip(ambiguous).theme.has_value());
  CHECK(!patchy::ui::load_theme_package_zip(QByteArrayLiteral("not a zip at all")).theme.has_value());
}

void ui_theme_package_rejects_paths_that_escape() {
  CHECK(patchy::ui::is_safe_theme_package_path(QStringLiteral("icons/apply.svg")));
  CHECK(!patchy::ui::is_safe_theme_package_path(QStringLiteral("../theme.json")));
  CHECK(!patchy::ui::is_safe_theme_package_path(QStringLiteral("icons/../../x.svg")));
  CHECK(!patchy::ui::is_safe_theme_package_path(QStringLiteral("/etc/passwd")));
  CHECK(!patchy::ui::is_safe_theme_package_path(QStringLiteral("C:/Windows/x.png")));
  CHECK(!patchy::ui::is_safe_theme_package_path(QStringLiteral("icons\\apply.svg")));
  CHECK(!patchy::ui::is_safe_theme_package_path(QString()));

  for (const std::string& bad : {std::string("../evil.wav"), std::string("icons/../../evil.svg"),
                                 std::string("/abs/theme.json"), std::string("C:/evil.png"),
                                 std::string("sounds\\..\\evil.wav")}) {
    const auto zip = zip_of({{"theme.json", manifest_json(QColor(1, 2, 3))}, {bad, QByteArrayLiteral("x")}});
    const auto result = patchy::ui::load_theme_package_zip(zip);
    CHECK(!result.theme.has_value());
    CHECK(!result.error.isEmpty());
  }

  // A backdrop path that climbs out of the package is ignored with a warning; the
  // colors still load.
  const auto climbing = zip_of(
      {{"theme.json", manifest_json(QColor(1, 2, 3),
                                    QJsonObject{{QStringLiteral("backdrop"),
                                                 QJsonObject{{QStringLiteral("image"), QStringLiteral("../bg.png")}}}})}});
  const auto loaded = patchy::ui::load_theme_package_zip(climbing);
  CHECK(loaded.theme.has_value());
  CHECK(!loaded.warnings.isEmpty());
  if (loaded.theme) {
    CHECK(loaded.theme->assets->backdrop.isNull());
  }
}

void ui_theme_package_size_cap_refuses_large_packages() {
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Big")), QColor(1, 2, 3));
  write_bytes(themes.file(QStringLiteral("Big/images/huge.bin")), QByteArray(4096, 'x'));

  patchy::ui::ThemePackageLimits tight;
  tight.max_entry_bytes = 2048;
  CHECK(!patchy::ui::load_theme_package_directory(themes.file(QStringLiteral("Big")), tight).theme.has_value());
  tight = {};
  tight.max_total_bytes = 1024;
  CHECK(!patchy::ui::load_theme_package_directory(themes.file(QStringLiteral("Big")), tight).theme.has_value());
  tight = {};
  tight.max_entries = 3;
  CHECK(!patchy::ui::load_theme_package_directory(themes.file(QStringLiteral("Big")), tight).theme.has_value());
  CHECK(patchy::ui::load_theme_package_directory(themes.file(QStringLiteral("Big"))).theme.has_value());

  const auto zip = zip_of({{"theme.json", manifest_json(QColor(1, 2, 3))},
                           {"images/huge.bin", QByteArray(4096, 'x')}});
  tight = {};
  tight.max_entry_bytes = 2048;
  CHECK(!patchy::ui::load_theme_package_zip(zip, tight).theme.has_value());
  tight = {};
  tight.max_package_bytes = 64;
  CHECK(!patchy::ui::load_theme_package_zip(zip, tight).theme.has_value());
  CHECK(patchy::ui::load_theme_package_zip(zip).theme.has_value());

  // A bitmap icon past the side limit is skipped with a warning, not loaded.
  const auto big_icon = zip_of({{"theme.json", manifest_json(QColor(1, 2, 3))},
                                {"icons/apply.png", png_bytes(solid_image(64, 64, Qt::red))}});
  tight = {};
  tight.max_icon_side = 32;
  const auto skipped = patchy::ui::load_theme_package_zip(big_icon, tight);
  CHECK(skipped.theme.has_value());
  CHECK(!skipped.warnings.isEmpty());
  if (skipped.theme) {
    CHECK(!skipped.theme->assets->icons.contains(QStringLiteral("apply")));
  }
}

void ui_theme_package_icon_override_and_fallback() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Icons")), QColor(10, 20, 30),
                       QJsonObject{{QStringLiteral("recolorIcons"), false}});

  // Icons made before the theme switch are the ones checked: a live switch must
  // repaint existing QActions without a new setIcon.
  const auto overridden = patchy::ui::themed_svg_icon(QStringLiteral("apply"));
  const auto bitmap = patchy::ui::themed_svg_icon(QStringLiteral("cancel"));
  const auto untouched = patchy::ui::themed_svg_icon(QStringLiteral("copy"));
  const auto builtin_apply = overridden.pixmap(QSize(32, 32)).toImage();
  const auto builtin_copy = untouched.pixmap(QSize(32, 32)).toImage();

  apply_package(QStringLiteral("Icons"));
  CHECK(icon_center(overridden, 32) == QColor(255, 0, 0));
  CHECK(icon_center(bitmap, 16) == QColor(0, 200, 0));
  // The @2x drawing serves a 2x device, the 1x one a 1x device.
  CHECK(icon_center(bitmap, 16, 2.0) == QColor(0, 0, 200));
  // An icon the theme leaves out falls back to the shipped one.
  CHECK(untouched.pixmap(QSize(32, 32)).toImage() == builtin_copy);

  // recolorIcons: an SVG in the authored ink takes the theme's icon_ink.
  write_bytes(themes.file(QStringLiteral("Ink/icons/apply.svg")), ink_svg());
  auto palette = patchy::ui::dark_palette();
  palette.icon_ink = QColor(0, 160, 90);
  write_bytes(themes.file(QStringLiteral("Ink/theme.json")),
              patchy::ui::serialize_theme_to_json(palette, patchy::ui::ColorScheme::Dark, QStringLiteral("Ink")));
  apply_package(QStringLiteral("Ink"));
  CHECK(icon_center(overridden, 32) == QColor(0, 160, 90));

  // Back to the built-in scheme: the same icon draws the shipped SVG again.
  ThemeManager::instance().clear_custom_theme(/*persist=*/false);
  QApplication::processEvents();
  CHECK(overridden.pixmap(QSize(32, 32)).toImage() == builtin_apply);
  CHECK(patchy::ui::active_theme_assets() == nullptr);
}

void ui_theme_package_nearest_scaling_keeps_pixel_art_sharp() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  PackageThemesDir themes;
  QImage checker(2, 2, QImage::Format_ARGB32);
  checker.setPixelColor(0, 0, Qt::black);
  checker.setPixelColor(1, 1, Qt::black);
  checker.setPixelColor(1, 0, Qt::white);
  checker.setPixelColor(0, 1, Qt::white);
  write_bytes(themes.file(QStringLiteral("Pixel/theme.json")),
              manifest_json(QColor(1, 2, 3), QJsonObject{{QStringLiteral("iconScaling"), QStringLiteral("nearest")}}));
  write_bytes(themes.file(QStringLiteral("Pixel/icons/apply.png")), png_bytes(checker));
  const auto icon = patchy::ui::themed_svg_icon(QStringLiteral("apply"));
  apply_package(QStringLiteral("Pixel"));
  const auto image = icon.pixmap(QSize(32, 32)).toImage();
  // Each source pixel becomes a hard-edged 16x16 block; smooth scaling would blend
  // the pixels next to the middle.
  CHECK(image.pixelColor(15, 15) == QColor(Qt::black));
  CHECK(image.pixelColor(16, 15) == QColor(Qt::white));
  CHECK(image.pixelColor(15, 16) == QColor(Qt::white));
  CHECK(image.pixelColor(16, 16) == QColor(Qt::black));
  ThemeManager::instance().clear_custom_theme(/*persist=*/false);
}

void ui_theme_package_backdrop_paints_pasteboard_anchored_to_the_window() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Backdrop")), QColor(10, 20, 30));
  apply_package(QStringLiteral("Backdrop"));
  CHECK(patchy::ui::theme_backdrop_active());
  const auto tile = split_tile();

  // The painter itself, in a child widget offset inside its window.
  QWidget parent;
  parent.resize(200, 160);
  QWidget child(&parent);
  child.setGeometry(13, 21, 100, 80);
  QImage painted(100, 80, QImage::Format_ARGB32);
  painted.fill(Qt::transparent);
  {
    QPainter painter(&painted);
    CHECK(patchy::ui::paint_theme_backdrop(painter, QRect(0, 0, 100, 80), child, QColor(0, 0, 0)));
  }
  for (const QPoint point : {QPoint(0, 0), QPoint(5, 7), QPoint(50, 40), QPoint(99, 79)}) {
    CHECK(painted.pixelColor(point) == tile.pixelColor((point.x() + 13) % 8, (point.y() + 21) % 8));
  }

  // The canvas pasteboard around a small document.
  patchy::Document document(20, 20, patchy::PixelFormat::rgba8());
  document.add_pixel_layer("Layer", solid_pixels(20, 20, patchy::PixelFormat::rgba8(), QColor(255, 255, 255)));
  patchy::ui::CanvasWidget canvas;
  canvas.resize(160, 120);
  canvas.set_document(&document);
  canvas.show();
  QApplication::processEvents();
  const auto grabbed = canvas.grab().toImage();
  const auto ratio = grabbed.devicePixelRatio();
  for (const QPoint point : {QPoint(2, 2), QPoint(6, 3), QPoint(135, 50)}) {
    const auto shown = grabbed.pixelColor(QPoint(qRound(point.x() * ratio), qRound(point.y() * ratio)));
    if (shown != tile.pixelColor(point.x() % 8, point.y() % 8)) {
      fprintf(stderr, "  pasteboard (%d,%d) shows %s, ratio %.2f, size %dx%d\n", point.x(), point.y(),
              qPrintable(shown.name()), ratio, grabbed.width(), grabbed.height());
    }
    CHECK(shown == tile.pixelColor(point.x() % 8, point.y() % 8));
  }

  // A user pasteboard color still wins over the theme's image.
  canvas.set_backdrop_color_override(QColor(90, 200, 90));
  QApplication::processEvents();
  CHECK(canvas.grab().toImage().pixelColor(2, 2) == QColor(90, 200, 90));

  // Center mode leaves the fallback color around the image.
  write_sample_package(themes.file(QStringLiteral("Centered")), QColor(10, 20, 30),
                       QJsonObject{{QStringLiteral("backdrop"),
                                    QJsonObject{{QStringLiteral("image"), QStringLiteral("images/tile.png")},
                                                {QStringLiteral("mode"), QStringLiteral("center")}}}});
  apply_package(QStringLiteral("Centered"));
  painted.fill(Qt::transparent);
  {
    QPainter painter(&painted);
    CHECK(patchy::ui::paint_theme_backdrop(painter, QRect(0, 0, 100, 80), child, QColor(1, 2, 3)));
  }
  CHECK(painted.pixelColor(0, 0) == QColor(1, 2, 3));
  CHECK(painted.pixelColor(46, 36) == tile.pixelColor(0, 0));

  ThemeManager::instance().clear_custom_theme(/*persist=*/false);
  CHECK(!patchy::ui::theme_backdrop_active());
}

void ui_theme_package_scroll_gutter_follows_the_theme() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  CHECK(patchy::ui::themed_icon_url(QStringLiteral("scroll-dither")).startsWith(QStringLiteral(":/")));

  // A custom track: a generated two-tone dither in the track's colors.
  patchy::ui::CustomTheme custom;
  custom.base = patchy::ui::ColorScheme::Dark;
  custom.palette = patchy::ui::dark_palette();
  custom.palette.canvas_scrollbar_track = QColor(0x40, 0x60, 0x80);
  ThemeManager::instance().set_custom_theme(QStringLiteral("gutter.patchytheme"), custom, /*persist=*/false);
  auto url = patchy::ui::themed_icon_url(QStringLiteral("scroll-dither"));
  url.remove(QLatin1Char('"'));
  CHECK(!url.startsWith(QStringLiteral(":/")));
  const QImage dither(url);
  CHECK(dither.size() == QSize(2, 2));
  CHECK(dither.pixelColor(1, 0) == QColor(0x40, 0x60, 0x80));
  CHECK(dither.pixelColor(0, 0) == QColor(0x4e, 0x6e, 0x8e));
  CHECK(patchy::ui::photoshop_style().contains(url));

  // A tiled backdrop: the gutter shows the tile.
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Gutter")), QColor(10, 20, 30));
  apply_package(QStringLiteral("Gutter"));
  url = patchy::ui::themed_icon_url(QStringLiteral("scroll-dither"));
  url.remove(QLatin1Char('"'));
  CHECK(QImage(url).convertToFormat(QImage::Format_ARGB32) == split_tile());

  // A custom theme on the built-in track keeps the authored asset.
  custom.palette = patchy::ui::dark_palette();
  ThemeManager::instance().set_custom_theme(QStringLiteral("same.patchytheme"), custom, /*persist=*/false);
  CHECK(patchy::ui::themed_icon_url(QStringLiteral("scroll-dither")).startsWith(QStringLiteral(":/")));
  ThemeManager::instance().clear_custom_theme(/*persist=*/false);
}

void ui_theme_package_folder_reloads_when_its_files_change() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Live")), QColor(10, 20, 30));
  apply_package(QStringLiteral("Live"));
  CHECK(!ThemeManager::instance().watched_package_paths().isEmpty());
  CHECK(patchy::ui::theme().window_bg == QColor(10, 20, 30));

  write_bytes(themes.file(QStringLiteral("Live/theme.json")), manifest_json(QColor(70, 80, 90)));
  CHECK(process_events_until([] { return patchy::ui::theme().window_bg == QColor(70, 80, 90); }, 5000));

  // Broken JSON mid-edit keeps the last good theme.
  write_bytes(themes.file(QStringLiteral("Live/theme.json")), QByteArrayLiteral("{ \"base\": "));
  process_events_for(ThemeManager::kPackageReloadDelayMs * 3);
  CHECK(patchy::ui::theme().window_bg == QColor(70, 80, 90));
  CHECK(ThemeManager::instance().active_custom_theme_id() == QStringLiteral("Live"));

  // An icon edit reloads too.
  write_bytes(themes.file(QStringLiteral("Live/theme.json")), manifest_json(QColor(70, 80, 90)));
  CHECK(process_events_until([] { return patchy::ui::theme().window_bg == QColor(70, 80, 90); }, 5000));
  const auto icon = patchy::ui::themed_svg_icon(QStringLiteral("apply"));
  CHECK(icon_center(icon, 32) == QColor(255, 0, 0));
  write_bytes(themes.file(QStringLiteral("Live/icons/apply.svg")),
              QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\"><rect width=\"32\" "
                                "height=\"32\" fill=\"#00ff00\"/></svg>"));
  CHECK(process_events_until([&icon] { return icon_center(icon, 32) == QColor(0, 255, 0); }, 5000));

  ThemeManager::instance().clear_custom_theme(/*persist=*/false);
  CHECK(ThemeManager::instance().watched_package_paths().isEmpty());
}

void ui_theme_sound_events_resolve_from_the_package() {
  const EnvironmentVariableRestorer restore_no_sound("PATCHY_NO_SOUND");
  qputenv("PATCHY_NO_SOUND", "1");
  SettingsValueRestorer restore_sounds(QStringLiteral("preferences/playThemeSounds"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);

  // The identifiers are persisted (they are the file names themes ship).
  const QStringList expected{QStringLiteral("save-done"), QStringLiteral("export-done"),
                             QStringLiteral("long-operation-done"), QStringLiteral("error"),
                             QStringLiteral("warning"), QStringLiteral("script-alert")};
  QStringList ids;
  for (const auto event : patchy::ui::theme_sound_events()) {
    ids.push_back(QString(patchy::ui::theme_sound_event_id(event)));
    CHECK(patchy::ui::theme_sound_event_from_id(ids.back()) == event);
  }
  CHECK(ids == expected);
  CHECK(!patchy::ui::theme_sound_event_from_id(QStringLiteral("startup")).has_value());

  CHECK(!patchy::ui::play_theme_sound(patchy::ui::ThemeSoundEvent::SaveDone));  // built-in scheme: silent
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Sounds")), QColor(10, 20, 30));
  write_bytes(themes.file(QStringLiteral("Sounds/sounds/startup.wav")),
              patchy::ui::build_tone_wav(440.0, 20, 0.5, patchy::ui::ToneWave::Sine));
  write_bytes(themes.file(QStringLiteral("Sounds/sounds/error.wav")), QByteArrayLiteral("not a wav"));
  const auto loaded = patchy::ui::load_theme_by_id(QStringLiteral("Sounds"));
  CHECK(loaded.warnings.size() == 2);  // the unknown event and the bad file
  apply_package(QStringLiteral("Sounds"));

  patchy::ui::set_theme_sounds_enabled(true);
  CHECK(patchy::ui::theme_sounds_enabled());
  CHECK(patchy::ui::play_theme_sound(patchy::ui::ThemeSoundEvent::SaveDone));
  CHECK(!patchy::ui::play_theme_sound(patchy::ui::ThemeSoundEvent::Error));
  patchy::ui::set_theme_sounds_enabled(false);
  CHECK(!patchy::ui::play_theme_sound(patchy::ui::ThemeSoundEvent::SaveDone));
  ThemeManager::instance().clear_custom_theme(/*persist=*/false);
}

void ui_theme_package_unicode_paths() {
  const auto dir = patchy::test::unicode_artifact_dir(patchy::test::kUnicodePathStems[0]);
  const auto root = patchy::ui::to_qstring(dir);
  const auto folder = QDir(root).filePath(patchy::ui::to_qstring(
      patchy::test::unicode_path_piece(patchy::test::kUnicodePathStems[4])));
  write_sample_package(folder, QColor(5, 6, 7));
  const auto from_folder = patchy::ui::load_theme_package_directory(folder);
  if (!from_folder.theme) {
    fprintf(stderr, "  folder package did not load: %s\n", from_folder.error.toUtf8().constData());
  }
  CHECK(from_folder.theme.has_value());
  if (!from_folder.theme) {
    return;
  }
  CHECK(from_folder.theme->assets->icons.contains(QStringLiteral("apply")));

  const auto zip_path = QDir(root).filePath(
      patchy::ui::to_qstring(patchy::test::unicode_path_piece(patchy::test::kUnicodeCombinedStem)) +
      QStringLiteral(".zip"));
  write_bytes(zip_path, patchy::ui::serialize_theme_package_zip(from_folder.theme->palette, from_folder.theme->base,
                                                                QStringLiteral("U"), from_folder.theme->assets.get()));
  const auto from_zip = patchy::ui::load_theme_package_zip_file(zip_path);
  CHECK(from_zip.theme.has_value());
  CHECK(from_zip.theme && from_zip.theme->palette.window_bg == QColor(5, 6, 7));
  CHECK(from_zip.theme && !from_zip.theme->assets->backdrop.isNull());
}

void ui_preferences_lists_theme_packages_and_sound_preference() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  SettingsValueRestorer restore_sounds(QStringLiteral("preferences/playThemeSounds"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Folder Theme")), QColor(11, 22, 33));
  const auto loaded = patchy::ui::load_theme_by_id(QStringLiteral("Folder Theme"));
  CHECK(loaded.theme.has_value());
  write_bytes(themes.file(QStringLiteral("zipped.zip")),
              patchy::ui::serialize_theme_package_zip(loaded.theme->palette, loaded.theme->base,
                                                      QStringLiteral("Zipped Theme"), loaded.theme->assets.get()));
  patchy::ui::set_theme_sounds_enabled(true);

  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("preferencesColorSchemeCombo"));
    auto* sounds = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPlayThemeSoundsCheck"));
    CHECK(combo != nullptr && sounds != nullptr);
    CHECK(dialog->findChild<QPushButton*>(QStringLiteral("preferencesExportThemePackageButton")) != nullptr);
    CHECK(dialog->findChild<QPushButton*>(QStringLiteral("preferencesExportIconTemplateButton")) != nullptr);
    if (combo == nullptr || sounds == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(sounds->isChecked());
    const auto folder_index = combo->findData(QStringLiteral("custom:Folder Theme"));
    const auto zip_index = combo->findData(QStringLiteral("custom:zipped.zip"));
    CHECK(folder_index >= 0 && zip_index >= 0);
    CHECK(combo->itemText(zip_index) == QStringLiteral("Zipped Theme"));
    combo->setCurrentIndex(folder_index);
    QApplication::processEvents();
    CHECK(patchy::ui::theme().window_bg == QColor(11, 22, 33));
    CHECK(patchy::ui::theme_backdrop_active());
    sounds->setChecked(false);
    saw_dialog = true;
    dialog->accept();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(ThemeManager::instance().active_custom_theme_id() == QStringLiteral("Folder Theme"));
  CHECK(!patchy::ui::theme_sounds_enabled());
  ThemeManager::instance().clear_custom_theme(/*persist=*/true);
}

void ui_theme_package_icon_template_and_example_package() {
  const auto stems = patchy::ui::overridable_icon_stems();
  CHECK(stems.contains(QStringLiteral("tool-brush")));
  CHECK(!stems.contains(QStringLiteral("scroll-dither")));  // QSS url() assets cannot be replaced
  QTemporaryDir out;
  QString error;
  const auto written = patchy::ui::export_icon_template(QDir(out.path()).filePath(QStringLiteral("icons")), &error);
  CHECK(written == stems.size());
  CHECK(QFile(QDir(out.path()).filePath(QStringLiteral("icons/tool-brush.svg"))).size() > 0);

  // The example package in the repository loads cleanly with every part in use.
  const auto example = QDir(QStringLiteral(PATCHY_SOURCE_DIR)).filePath(QStringLiteral("themes/example-package"));
  const auto result = patchy::ui::load_theme_package_directory(example);
  CHECK(result.error.isEmpty());
  CHECK(result.warnings.isEmpty());
  CHECK(result.theme.has_value());
  if (result.theme) {
    const auto* assets = result.theme->assets.get();
    CHECK(assets->icons.size() >= 2);
    CHECK(!assets->backdrop.isNull());
    CHECK(!patchy::ui::theme_sound_for_event(assets, patchy::ui::ThemeSoundEvent::SaveDone).isEmpty());
  }
}

// Card screenshots: the Preferences Application tab, then the example package's
// tool palette and pasteboard (artifacts theme-package-*.png).
void ui_theme_package_capture_shots() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  {
    patchy::ui::MainWindow window;
    show_window(window);
    QTimer::singleShot(0, [&] {
      auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
      CHECK(dialog != nullptr);
      if (dialog != nullptr) {
        QApplication::processEvents();
        (void)capture_widget_artifact("theme-package-after-preferences", *dialog);
        dialog->reject();
      }
    });
    require_action(window, "filePreferencesAction")->trigger();
    QApplication::processEvents();
  }
  for (const bool with_package : {false, true}) {
    if (with_package) {
      const auto example = QDir(QStringLiteral(PATCHY_SOURCE_DIR)).filePath(QStringLiteral("themes/example-package"));
      auto result = patchy::ui::load_theme_package_directory(example);
      CHECK(result.theme.has_value());
      if (result.theme) {
        ThemeManager::instance().set_custom_theme(QStringLiteral("example-package"), *result.theme, false);
      }
    } else {
      // Before: the same colors as a single-file theme, which is all a package was before.
      QFile manifest(QDir(QStringLiteral(PATCHY_SOURCE_DIR)).filePath(QStringLiteral("themes/example-package/theme.json")));
      CHECK(manifest.open(QIODevice::ReadOnly));
      auto result = patchy::ui::load_theme_from_json(manifest.readAll());
      CHECK(result.theme.has_value());
      if (result.theme) {
        ThemeManager::instance().set_custom_theme(QStringLiteral("example.patchytheme"), *result.theme, false);
      }
    }
    QApplication::processEvents();
    patchy::ui::MainWindow window;
    window.resize(1100, 720);
    show_window(window);
    patchy::Document document(320, 220, patchy::PixelFormat::rgba8());
    document.add_pixel_layer("Layer", solid_pixels(320, 220, patchy::PixelFormat::rgba8(), QColor(236, 230, 218)));
    window.add_document_session(std::move(document), QStringLiteral("Backdrop"));
    process_events_for(100);
    const std::string prefix = with_package ? "theme-package-after-" : "theme-package-before-";
    if (auto* palette = window.findChild<QWidget*>(QStringLiteral("toolPalette")); palette != nullptr) {
      (void)capture_widget_artifact(prefix + "tool-palette", *palette);
    }
    (void)capture_widget_artifact(prefix + "backdrop", window);
  }
  ThemeManager::instance().clear_custom_theme(/*persist=*/false);
}

}  // namespace

std::vector<patchy::test::TestCase> theme_package_tests() {
  return {
      {"ui_theme_package_folder_loads_icons_backdrop_and_sounds", ui_theme_package_folder_loads_icons_backdrop_and_sounds},
      {"ui_theme_package_zip_loads_from_root_and_from_one_top_folder",
       ui_theme_package_zip_loads_from_root_and_from_one_top_folder},
      {"ui_theme_package_rejects_paths_that_escape", ui_theme_package_rejects_paths_that_escape},
      {"ui_theme_package_size_cap_refuses_large_packages", ui_theme_package_size_cap_refuses_large_packages},
      {"ui_theme_package_icon_override_and_fallback", ui_theme_package_icon_override_and_fallback},
      {"ui_theme_package_nearest_scaling_keeps_pixel_art_sharp", ui_theme_package_nearest_scaling_keeps_pixel_art_sharp},
      {"ui_theme_package_backdrop_paints_pasteboard_anchored_to_the_window",
       ui_theme_package_backdrop_paints_pasteboard_anchored_to_the_window},
      {"ui_theme_package_scroll_gutter_follows_the_theme", ui_theme_package_scroll_gutter_follows_the_theme},
      {"ui_theme_package_folder_reloads_when_its_files_change", ui_theme_package_folder_reloads_when_its_files_change},
      {"ui_theme_sound_events_resolve_from_the_package", ui_theme_sound_events_resolve_from_the_package},
      {"ui_theme_package_unicode_paths", ui_theme_package_unicode_paths},
      {"ui_preferences_lists_theme_packages_and_sound_preference",
       ui_preferences_lists_theme_packages_and_sound_preference},
      {"ui_theme_package_icon_template_and_example_package", ui_theme_package_icon_template_and_example_package},
      {"ui_theme_package_capture_shots", ui_theme_package_capture_shots},
  };
}
