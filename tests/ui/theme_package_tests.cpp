// Theme packages (docs/theme-files.md, "Theme packages"): folder and zip loading,
// the path and size limits, icon overrides with their fallback, the backdrop
// painter and the scroll-bar gutter, the folder auto-reload, the Preferences
// entries and the theme sound events.

#include "formats/ora_document_io.hpp"
#include "core/document.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
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
#include "ui/theme_script_host.hpp"
#include "ui/theme_sounds.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <algorithm>
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
                             QStringLiteral("warning"), QStringLiteral("script-alert"),
                             QStringLiteral("layer-deleted"), QStringLiteral("document-opened"),
                             QStringLiteral("document-new"), QStringLiteral("document-closed"),
                             QStringLiteral("undo"), QStringLiteral("redo"), QStringLiteral("startup"),
                             QStringLiteral("tool-changed"), QStringLiteral("idle"),
                             QStringLiteral("stroke-start"), QStringLiteral("stroke-end")};
  QStringList ids;
  for (const auto event : patchy::ui::theme_sound_events()) {
    ids.push_back(QString(patchy::ui::theme_sound_event_id(event)));
    CHECK(patchy::ui::theme_sound_event_from_id(ids.back()) == event);
  }
  CHECK(ids == expected);
  CHECK(!patchy::ui::theme_sound_event_from_id(QStringLiteral("launch")).has_value());

  CHECK(!patchy::ui::play_theme_sound(patchy::ui::ThemeSoundEvent::SaveDone));  // built-in scheme: silent
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Sounds")), QColor(10, 20, 30));
  write_bytes(themes.file(QStringLiteral("Sounds/sounds/launch.wav")),
              patchy::ui::build_tone_wav(440.0, 20, 0.5, patchy::ui::ToneWave::Sine));
  write_bytes(themes.file(QStringLiteral("Sounds/sounds/error.wav")), QByteArrayLiteral("not a wav"));
  write_bytes(themes.file(QStringLiteral("Sounds/sounds/startup.wav")),
              patchy::ui::build_tone_wav(440.0, 20, 0.5, patchy::ui::ToneWave::Sine));
  const auto loaded = patchy::ui::load_theme_by_id(QStringLiteral("Sounds"));
  CHECK(loaded.warnings.size() == 2);  // the unknown event and the bad file
  apply_package(QStringLiteral("Sounds"));

  patchy::ui::set_theme_sounds_enabled(true);
  CHECK(patchy::ui::theme_sounds_enabled());
  CHECK(patchy::ui::play_theme_sound(patchy::ui::ThemeSoundEvent::SaveDone));
  CHECK(!patchy::ui::play_theme_sound(patchy::ui::ThemeSoundEvent::Error));
  // The launch's startup sound waits for the woken output; later ones play at once.
  patchy::ui::prepare_theme_startup_sound();
  CHECK(patchy::ui::play_theme_sound(patchy::ui::ThemeSoundEvent::Startup));
  CHECK(patchy::ui::play_theme_sound(patchy::ui::ThemeSoundEvent::Startup));
  process_events_for(patchy::ui::kStartupSoundWakeMs + 100);  // the held sound plays (muted)
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
    auto* list = preferences_theme_list(*dialog);
    auto* sounds = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPlayThemeSoundsCheck"));
    CHECK(list != nullptr && sounds != nullptr);
    CHECK(dialog->findChild<QPushButton*>(QStringLiteral("preferencesExportThemePackageButton")) != nullptr);
    CHECK(dialog->findChild<QPushButton*>(QStringLiteral("preferencesExportIconTemplateButton")) != nullptr);
    if (list == nullptr || sounds == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(sounds->isChecked());
    const auto folder_row = preferences_theme_row(*list, QStringLiteral("custom:Folder Theme"));
    const auto zip_row = preferences_theme_row(*list, QStringLiteral("custom:zipped.zip"));
    CHECK(folder_row >= 0 && zip_row >= 0);
    CHECK(zip_row >= 0 && list->item(zip_row)->text() == QStringLiteral("Zipped Theme"));
    CHECK(select_preferences_theme(*dialog, QStringLiteral("custom:Folder Theme")));
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

// A zip whose icons/apply.svg is stored with a byte flipped after its CRC was taken:
// theme.json and the backdrop read, the full load fails on the icon.
QByteArray zip_with_damaged_icon(const QColor& window_bg) {
  const auto marker = QByteArrayLiteral("<!-- patchy-damaged-entry -->");
  auto svg = red_svg();
  svg.append(marker);
  std::vector<patchy::ora::detail::ZipEntry> entries;
  entries.push_back({"theme.json",
                     to_vector(manifest_json(window_bg, QJsonObject{{QStringLiteral("backdrop"),
                                                                     QJsonObject{{QStringLiteral("image"),
                                                                                  QStringLiteral("images/tile.png")}}}})),
                     true});
  entries.push_back({"images/tile.png", to_vector(png_bytes(split_tile())), false});
  entries.push_back({"icons/apply.svg", to_vector(svg), false});
  const auto built = patchy::ora::detail::zip_archive(entries);
  QByteArray zip(reinterpret_cast<const char*>(built.data()), static_cast<qsizetype>(built.size()));
  const auto at = zip.indexOf(marker);
  CHECK(at >= 0);
  if (at >= 0) {
    zip[at + 5] = 'X';
  }
  return zip;
}

void ui_theme_package_summary_reads_only_manifest_and_backdrop() {
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Sample")), QColor(12, 34, 56));
  const auto check_summary = [](const patchy::ui::ThemeLoadResult& result) {
    CHECK(result.error.isEmpty());
    CHECK(result.theme.has_value());
    if (!result.theme || result.theme->assets == nullptr) {
      CHECK(false);
      return;
    }
    const auto& assets = *result.theme->assets;
    CHECK(result.theme->palette.window_bg == QColor(12, 34, 56));
    CHECK(assets.summary);
    CHECK(assets.backdrop.size() == QSize(8, 8));
    CHECK(assets.options.backdrop_image == QStringLiteral("images/tile.png"));
    CHECK(assets.icons.isEmpty());
    CHECK(assets.sounds.isEmpty());
    CHECK(assets.files.isEmpty());
  };
  check_summary(patchy::ui::load_theme_summary_by_id(QStringLiteral("Sample")));
  const auto full = patchy::ui::load_theme_by_id(QStringLiteral("Sample"));
  CHECK(full.theme && full.theme->assets != nullptr && !full.theme->assets->summary);
  if (!full.theme) {
    return;
  }
  write_bytes(themes.file(QStringLiteral("Sample.zip")),
              patchy::ui::serialize_theme_package_zip(full.theme->palette, full.theme->base, QStringLiteral("Z"),
                                                      full.theme->assets.get()));
  check_summary(patchy::ui::load_theme_summary_by_id(QStringLiteral("Sample.zip")));

  // The summary skips the per-file checks a full load makes...
  patchy::ui::ThemePackageLimits tight;
  tight.max_total_bytes = 64;
  CHECK(!patchy::ui::load_theme_package_directory(themes.file(QStringLiteral("Sample")), tight).theme.has_value());
  CHECK(patchy::ui::load_theme_package_directory_summary(themes.file(QStringLiteral("Sample")), tight)
            .theme.has_value());
  const auto damaged = zip_with_damaged_icon(QColor(12, 34, 56));
  CHECK(patchy::ui::load_theme_package_zip_summary(damaged).theme.has_value());
  CHECK(!patchy::ui::load_theme_package_zip(damaged).theme.has_value());
  // ...but a zip naming a path outside the package is still refused as a whole.
  const auto climbing = zip_of({{"theme.json", manifest_json(QColor(1, 2, 3))}, {"../evil.txt", QByteArrayLiteral("x")}});
  CHECK(!patchy::ui::load_theme_package_zip_summary(climbing).theme.has_value());
  // A single-file theme loads in full either way.
  write_bytes(themes.file(QStringLiteral("plain.patchytheme")), manifest_json(QColor(7, 8, 9)));
  const auto plain = patchy::ui::load_theme_summary_by_id(QStringLiteral("plain.patchytheme"));
  CHECK(plain.theme && plain.theme->assets == nullptr && plain.theme->palette.window_bg == QColor(7, 8, 9));
}

// The theme list shows package summaries and loads a package in full only once its
// row is selected; a package that summarizes but fails its full load becomes a
// disabled "Could not be loaded" row and leaves the previewed theme alone.
void ui_preferences_theme_list_loads_packages_when_selected() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Folder Theme")), QColor(21, 43, 65));
  write_bytes(themes.file(QStringLiteral("damaged.zip")), zip_with_damaged_icon(QColor(99, 88, 77)));

  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* list = preferences_theme_list(*dialog);
    CHECK(list != nullptr);
    if (list == nullptr) {
      dialog->reject();
      return;
    }
    const auto damaged_row = preferences_theme_row(*list, QStringLiteral("custom:damaged.zip"));
    CHECK(damaged_row >= 0);
    if (damaged_row < 0) {
      dialog->reject();
      return;
    }
    // Listed from its summary: named, with its swatch, and selectable.
    CHECK(list->item(damaged_row)->text() == QStringLiteral("Pkg"));
    CHECK((list->item(damaged_row)->flags() & Qt::ItemIsSelectable) != 0);

    CHECK(select_preferences_theme(*dialog, QStringLiteral("custom:Folder Theme")));
    CHECK(patchy::ui::theme().window_bg == QColor(21, 43, 65));
    const auto* assets = patchy::ui::active_theme_assets();
    CHECK(assets != nullptr && !assets->summary && assets->icons.contains(QStringLiteral("apply")));
    CHECK(assets != nullptr && !patchy::ui::theme_sound_for_event(assets, patchy::ui::ThemeSoundEvent::SaveDone).isEmpty());

    CHECK(select_preferences_theme(*dialog, QStringLiteral("custom:damaged.zip")));
    CHECK((list->item(damaged_row)->flags() & Qt::ItemIsSelectable) == 0);
    CHECK(!list->item(damaged_row)->toolTip().isEmpty());
    CHECK(patchy::ui::theme().window_bg == QColor(21, 43, 65));
    auto* meta = dialog->findChild<QLabel*>(QStringLiteral("preferencesThemeMeta"));
    CHECK(meta != nullptr && meta->text() == list->item(damaged_row)->toolTip());
    saw_dialog = true;
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  // Cancel put the entry scheme back.
  CHECK(!patchy::ui::has_active_custom_palette());
}

// Picking a theme row plays that theme's startup sound, under the dialog's own (not yet
// stored) Play theme sounds box; a theme without one stays silent.
void ui_preferences_theme_row_plays_its_startup_sound() {
  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  SettingsValueRestorer restore_sounds(QStringLiteral("preferences/playThemeSounds"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  PackageThemesDir themes;
  write_sample_package(themes.file(QStringLiteral("Loud")), QColor(30, 40, 50));
  write_bytes(themes.file(QStringLiteral("Loud/sounds/startup.wav")),
              patchy::ui::build_tone_wav(440.0, 40, 0.5, patchy::ui::ToneWave::Sine));
  write_sample_package(themes.file(QStringLiteral("Quiet")), QColor(60, 70, 80));
  patchy::ui::set_theme_sounds_enabled(true);

  const auto loud = patchy::ui::load_theme_by_id(QStringLiteral("Loud"));
  CHECK(loud.theme && patchy::ui::play_theme_startup_preview(loud.theme->assets.get(), true));
  CHECK(loud.theme && !patchy::ui::play_theme_startup_preview(loud.theme->assets.get(), false));
  CHECK(!patchy::ui::play_theme_startup_preview(nullptr, true));

  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* sounds = dialog->findChild<QCheckBox*>(QStringLiteral("preferencesPlayThemeSoundsCheck"));
    CHECK(sounds != nullptr && preferences_theme_list(*dialog) != nullptr);
    if (sounds == nullptr || preferences_theme_list(*dialog) == nullptr) {
      dialog->reject();
      return;
    }
    auto plays = patchy::ui::theme_sound_play_count_for_testing();
    CHECK(select_preferences_theme(*dialog, QStringLiteral("custom:Loud")));
    CHECK(patchy::ui::theme_sound_play_count_for_testing() == plays + 1);
    plays = patchy::ui::theme_sound_play_count_for_testing();
    CHECK(select_preferences_theme(*dialog, QStringLiteral("custom:Quiet")));
    CHECK(select_preferences_theme(*dialog, QStringLiteral("dark")));
    CHECK(patchy::ui::theme_sound_play_count_for_testing() == plays);
    sounds->setChecked(false);
    CHECK(select_preferences_theme(*dialog, QStringLiteral("custom:Loud")));
    CHECK(patchy::ui::theme_sound_play_count_for_testing() == plays);
    saw_dialog = true;
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(patchy::ui::theme_sounds_enabled());  // the box was never stored
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

// A bundled package (docs/theme-files.md, "Bundled themes") loads from the generated
// resource folder, which holds exactly its source files but the README; its scripts run
// until the user turns them off; PATCHY_THEME_SOURCE_DIR reads and watches the source.
void ui_bundled_theme_package_loads_from_resources_and_source() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  EnvironmentVariableRestorer restore_source("PATCHY_THEME_SOURCE_DIR");
  qunsetenv("PATCHY_THEME_SOURCE_DIR");
  const auto id = patchy::ui::kBundledThemeIdPrefix + QStringLiteral("dink-smallwood");
  CHECK(patchy::ui::bundled_theme_file_names().contains(QStringLiteral("dink-smallwood")));
  const auto resource = patchy::ui::theme_file_path_for_id(id);
  CHECK(resource == QStringLiteral(":/patchy/themes/dink-smallwood"));
  CHECK(!patchy::ui::is_theme_package_directory_id(id));  // nothing on disk to watch

  const QDir source(QDir(QStringLiteral(PATCHY_SOURCE_DIR)).filePath(QStringLiteral("themes/bundled/dink-smallwood")));
  int compared = 0;
  QDirIterator files(source.path(), QDir::Files, QDirIterator::Subdirectories);
  while (files.hasNext()) {
    const auto relative = source.relativeFilePath(files.next());
    QFile packaged(QDir(resource).filePath(relative));
    if (relative == QStringLiteral("README.txt")) {
      CHECK(!packaged.exists());
      continue;
    }
    QFile original(files.filePath());
    CHECK(packaged.open(QIODevice::ReadOnly) && original.open(QIODevice::ReadOnly));
    if (packaged.readAll() != original.readAll()) {
      std::printf("  resource differs from source: %s\n", qPrintable(relative));
      CHECK(false);
    }
    ++compared;
  }
  CHECK(compared > 20);

  const auto summary = patchy::ui::load_theme_summary_by_id(id);
  CHECK(summary.theme.has_value() && summary.theme->assets != nullptr && summary.theme->assets->summary);
  const auto result = patchy::ui::load_theme_by_id(id);
  CHECK(result.error.isEmpty());
  CHECK(result.warnings.isEmpty());
  CHECK(result.theme.has_value());
  if (!result.theme) {
    return;
  }
  const auto* assets = result.theme->assets.get();
  CHECK(assets != nullptr && !assets->summary && assets->has_scripts());
  CHECK(assets != nullptr && !assets->backdrop.isNull() && !assets->icons.isEmpty());
  CHECK(!patchy::ui::theme_sound_for_event(assets, patchy::ui::ThemeSoundEvent::Startup).isEmpty());

  SettingsValueRestorer restore_trust(QStringLiteral("themeScripts/trust"));
  patchy::ui::app_settings().remove(QStringLiteral("themeScripts/trust"));
  const auto digest = patchy::ui::theme_scripts_digest(*assets);
  CHECK(patchy::ui::stored_theme_script_trust(id, digest) == patchy::ui::ThemeScriptTrust::Allowed);
  CHECK(patchy::ui::stored_theme_script_trust(QStringLiteral("dink-smallwood"), digest) ==
        patchy::ui::ThemeScriptTrust::Unknown);  // a user copy still asks
  patchy::ui::store_theme_script_trust(id, QStringLiteral("older-scripts"), false);
  CHECK(patchy::ui::stored_theme_script_trust(id, digest) == patchy::ui::ThemeScriptTrust::Denied);
  patchy::ui::store_theme_script_trust(id, digest, true);
  CHECK(patchy::ui::stored_theme_script_trust(id, digest) == patchy::ui::ThemeScriptTrust::Allowed);

  // The source override: a folder named like the package, read and watched from disk.
  QTemporaryDir bundled_source;
  write_sample_package(QDir(bundled_source.path()).filePath(QStringLiteral("dink-smallwood")), QColor(40, 50, 60));
  qputenv("PATCHY_THEME_SOURCE_DIR", bundled_source.path().toUtf8());
  CHECK(QDir(patchy::ui::theme_file_path_for_id(id)) ==
        QDir(QDir(bundled_source.path()).filePath(QStringLiteral("dink-smallwood"))));
  CHECK(patchy::ui::is_theme_package_directory_id(id));
  CHECK(!patchy::ui::is_theme_package_directory_id(QStringLiteral("bundled:../dink-smallwood")));
  CHECK(patchy::ui::theme_file_path_for_id(QStringLiteral("bundled:nord.patchytheme")).startsWith(QStringLiteral(":/")));
  apply_package(id);
  CHECK(patchy::ui::theme().window_bg == QColor(40, 50, 60));
  CHECK(!ThemeManager::instance().watched_package_paths().isEmpty());
  write_bytes(QDir(bundled_source.path()).filePath(QStringLiteral("dink-smallwood/theme.json")),
              manifest_json(QColor(70, 80, 90)));
  CHECK(process_events_until([] { return patchy::ui::theme().window_bg == QColor(70, 80, 90); }, 5000));
  ThemeManager::instance().clear_custom_theme(/*persist=*/false);
  CHECK(ThemeManager::instance().watched_package_paths().isEmpty());
}

// Card screenshots: the Preferences Application tab, then the example package's
// tool palette and pasteboard (artifacts theme-package-*.png).
void ui_theme_package_capture_shots() {
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  SettingsValueRestorer restore_trust(QStringLiteral("themeScripts/trust"));
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
        // The example ships a script; these shots are about the package, so no trust prompt.
        patchy::ui::store_theme_script_trust(QStringLiteral("example-package"),
                                             patchy::ui::theme_scripts_digest(*result.theme->assets), false);
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

// A theme's SVG icon may not reach outside itself: QtSvg would load an <image> href as
// a file path (a network path leaks the Windows login hash). Every shipped icon, which
// Export Icon Template hands authors, still passes.
void ui_theme_svg_icons_reaching_outside_are_refused() {
  const auto svg = [](const char* body) {
    return QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                      "viewBox=\"0 0 32 32\">") +
           body + "</svg>";
  };
  for (const auto* body : {
           "<image href=\"file:///C:/secret.png\" width=\"32\" height=\"32\"/>",
           "<image xlink:href=\"//server/share/a.png\" width=\"32\" height=\"32\"/>",
           "<image href=\"data:image/svg+xml;base64,PHN2Zy8+\" width=\"32\" height=\"32\"/>",
           "<image width=\"32\" height=\"32\"/>",
           "<use href=\"other.svg#shape\"/>",
           "<rect width=\"32\" height=\"32\" fill=\"url(other.svg#g)\"/>",
           "<rect width=\"32\" height=\"32\" style=\"fill: url( 'http://example.com/a' )\"/>",
           "<style>@import 'theme.css';</style>",
           "<style>rect { fill: url(x.svg#g); }</style>",
       }) {
    CHECK(!patchy::ui::is_safe_theme_svg(svg(body)));
  }
  CHECK(!patchy::ui::is_safe_theme_svg(QByteArrayLiteral(
      "<?xml version=\"1.0\"?><!DOCTYPE svg [<!ENTITY a \"aaaa\">]><svg xmlns=\"http://www.w3.org/2000/svg\"/>")));
  CHECK(!patchy::ui::is_safe_theme_svg(
      QByteArrayLiteral("<?xml-stylesheet href=\"x.css\"?><svg xmlns=\"http://www.w3.org/2000/svg\"/>")));
  CHECK(!patchy::ui::is_safe_theme_svg(QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\"><rect")));
  for (const auto* body : {
           "<image href=\"data:image/png;base64,iVBORw0KGgo=\" width=\"32\" height=\"32\"/>",
           "<defs><linearGradient id=\"g\"/></defs><rect width=\"32\" height=\"32\" fill=\"url(#g)\"/>",
           "<rect width=\"32\" height=\"32\" style=\"fill: url( '#g' )\"/>",
           "<defs><path id=\"p\" d=\"M0 0h4\"/></defs><use xlink:href=\"#p\"/>",
       }) {
    if (!patchy::ui::is_safe_theme_svg(svg(body))) {
      fprintf(stderr, "  safe SVG refused: %s\n", body);
      CHECK(false);
    }
  }
  CHECK(patchy::ui::is_safe_theme_svg(red_svg()));

  QTemporaryDir out;
  QString error;
  CHECK(patchy::ui::export_icon_template(out.path(), &error) > 0);
  for (const auto& info : QDir(out.path()).entryInfoList({QStringLiteral("*.svg")}, QDir::Files)) {
    QFile file(info.absoluteFilePath());
    CHECK(file.open(QIODevice::ReadOnly));
    if (!patchy::ui::is_safe_theme_svg(file.readAll())) {
      fprintf(stderr, "  shipped icon refused: %s\n", info.fileName().toUtf8().constData());
      CHECK(false);
    }
  }

  // In a package: a load warning, and the shipped icon stays.
  PackageThemesDir themes;
  const auto root = themes.file(QStringLiteral("Reach"));
  write_bytes(QDir(root).filePath(QStringLiteral("theme.json")), manifest_json(QColor(1, 2, 3)));
  write_bytes(QDir(root).filePath(QStringLiteral("icons/apply.svg")),
              svg("<image href=\"//server/share/a.png\" width=\"32\" height=\"32\"/>"));
  const auto result = patchy::ui::load_theme_package_directory(root);
  CHECK(result.theme.has_value());
  CHECK(result.warnings.size() == 1 && result.warnings.value(0).contains(QStringLiteral("apply.svg")));
  CHECK(result.theme && !result.theme->assets->icons.contains(QStringLiteral("apply")));
}

// parse_wav / make_wav round-trip, and the loop channel starts and stops on the real
// backend (silent samples, so the run makes no sound) and refuses a too-short loop.
void ui_theme_wav_loop_helpers_and_player() {
  const auto tone = patchy::ui::build_tone_wav(440.0, 200, 0.5, patchy::ui::ToneWave::Sine);
  const auto pcm = patchy::ui::parse_wav(tone);
  CHECK(pcm.format_tag == 1 && pcm.channels == 1 && pcm.sample_rate == 44100 && pcm.block_align == 2);
  CHECK(pcm.bits == 16 && !pcm.is_float);
  CHECK(pcm.format_chunk.size() == 16);
  CHECK(pcm.data.size() == 44100 / 5 * 2);
  CHECK(patchy::ui::wav_duration_ms(tone) == 200);
  CHECK(patchy::ui::make_wav(pcm.format_chunk, pcm.data) == tone);
  CHECK(patchy::ui::parse_wav(QByteArrayLiteral("RIFF")).data.isEmpty());
  CHECK(patchy::ui::wav_duration_ms(QByteArrayLiteral("not a wav")) == 0);

  const auto silent = [&pcm](int ms) {
    return patchy::ui::make_wav(pcm.format_chunk, QByteArray(44100 * ms / 1000 * 2, '\0'));
  };
  const EnvironmentVariableRestorer restore_no_sound("PATCHY_NO_SOUND");
  qputenv("PATCHY_NO_SOUND", "1");
  patchy::ui::play_wav_loop(silent(100), silent(200));
  CHECK(!patchy::ui::wav_loop_active());  // muted: the player is never touched
  qunsetenv("PATCHY_NO_SOUND");
  patchy::ui::play_wav_loop({}, silent(patchy::ui::kMinWavLoopMs / 2));
  CHECK(!patchy::ui::wav_loop_active());
  // A machine without an audio device refuses the loop; one with a device plays it
  // (Linux never loops: it plays the intro once).
  patchy::ui::play_wav_loop(silent(100), silent(200));
  if (patchy::ui::wav_loop_active()) {
    std::fprintf(stderr, "  loop channel: playing on this machine's audio device\n");
    process_events_for(150);
    patchy::ui::play_wav_loop({}, silent(120));  // replaces the playing loop
    CHECK(patchy::ui::wav_loop_active());
  }
  patchy::ui::stop_wav_loop();
  CHECK(!patchy::ui::wav_loop_active());
  patchy::ui::stop_wav_loop();  // a second stop is a no-op
  CHECK(!patchy::ui::wav_loop_active());
  // The silent wake-up stream opens once; a second call while it plays is a no-op.
  patchy::ui::wake_audio_output();
  patchy::ui::wake_audio_output();
  process_events_for(patchy::ui::kAudioWakeStreamMs + 300);  // its device closes again
}

// Players trust a WAV's own sizes (winmm reads the RIFF size's worth of bytes), so only
// well-formed WAVs play, and a package's malformed one is a load warning.
void ui_theme_sounds_refuse_malformed_wav() {
  const auto good = patchy::ui::build_tone_wav(440.0, 50, 0.5, patchy::ui::ToneWave::Sine);
  CHECK(patchy::ui::is_well_formed_wav(good));
  const auto put_u32 = [](QByteArray bytes, int offset, quint32 value) {
    for (int i = 0; i < 4; ++i) {
      bytes[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }
    return bytes;
  };
  CHECK(!patchy::ui::is_well_formed_wav(put_u32(good, 4, static_cast<quint32>(good.size()) * 4)));  // RIFF size lies
  CHECK(!patchy::ui::is_well_formed_wav(put_u32(good, 40, static_cast<quint32>(good.size()) * 4)));  // data size lies
  CHECK(!patchy::ui::is_well_formed_wav(good.left(40)));  // no data chunk
  CHECK(!patchy::ui::is_well_formed_wav(QByteArrayLiteral("RIFF\x04\x00\x00\x00WAVE")));
  auto odd_bits = good;
  odd_bits[34] = 12;  // bits per sample
  CHECK(!patchy::ui::is_well_formed_wav(odd_bits));
  auto data_first = good;
  data_first.replace(12, 4, "junk");  // the fmt chunk renamed: data without a format
  CHECK(!patchy::ui::is_well_formed_wav(data_first));
  // A padding byte after the RIFF size is fine.
  CHECK(patchy::ui::is_well_formed_wav(good + QByteArray(1, '\0')));

  PackageThemesDir themes;
  const auto root = themes.file(QStringLiteral("Noisy"));
  write_bytes(QDir(root).filePath(QStringLiteral("theme.json")), manifest_json(QColor(1, 2, 3)));
  write_bytes(QDir(root).filePath(QStringLiteral("sounds/save-done.wav")),
              put_u32(good, 4, static_cast<quint32>(good.size()) * 4));
  const auto result = patchy::ui::load_theme_package_directory(root);
  CHECK(result.theme.has_value() && result.warnings.size() == 1);
  CHECK(result.theme &&
        patchy::ui::theme_sound_for_event(result.theme->assets.get(), patchy::ui::ThemeSoundEvent::SaveDone).isEmpty());
}

// The theme cache: week-old PNGs are pruned, a reused file's time is refreshed, and one
// process writes at most its byte cap there.
void ui_theme_cache_prunes_old_files_and_caps_writes() {
  const auto file_of = [](QString url) {
    if (url.startsWith(QLatin1Char('"'))) {
      url = url.mid(1, url.size() - 2);
    }
    return url;
  };
  const auto prefix = QStringLiteral("test%1").arg(QCoreApplication::applicationPid());
  const auto fresh = file_of(patchy::ui::theme_cache_png_url(solid_image(3, 3, QColor(1, 2, 3)), prefix));
  CHECK(!fresh.isEmpty() && QFileInfo::exists(fresh));
  const auto stale = QFileInfo(fresh).dir().filePath(prefix + QStringLiteral("-old.png"));
  write_bytes(stale, png_bytes(solid_image(2, 2, QColor(4, 5, 6))));
  const auto month_ago = QDateTime::currentDateTime().addDays(-30);
  for (const auto& path : {stale, fresh}) {
    QFile file(path);
    CHECK(file.open(QIODevice::ReadWrite));
    file.setFileTime(month_ago, QFileDevice::FileModificationTime);
  }
  // Reusing the fresh one refreshes its time, so only the stale one goes.
  CHECK(file_of(patchy::ui::theme_cache_png_url(solid_image(3, 3, QColor(1, 2, 3)), prefix)) == fresh);
  CHECK(patchy::ui::prune_theme_cache(QDateTime::currentDateTime().addDays(-7)) >= 1);
  CHECK(!QFileInfo::exists(stale));
  CHECK(QFileInfo::exists(fresh));

  patchy::ui::set_theme_cache_session_limit_for_testing(16);
  CHECK(patchy::ui::theme_cache_png_url(solid_image(5, 5, QColor(7, 8, 9)), prefix).isEmpty());
  CHECK(file_of(patchy::ui::theme_cache_png_url(solid_image(3, 3, QColor(1, 2, 3)), prefix)) == fresh);
  patchy::ui::set_theme_cache_session_limit_for_testing(patchy::ui::kThemeCacheSessionBytes);
  CHECK(QFile::remove(fresh));
}

// theme.json "version", "author" and "minPatchyVersion": parsed, written back, shown in
// the Colors and Themes list and details, and a theme for a newer Patchy warns once.
void ui_theme_info_shows_version_author_and_minimum_patchy() {
  const auto parse = [](const QJsonObject& extra) {
    QJsonObject object{{QStringLiteral("base"), QStringLiteral("dark")}, {QStringLiteral("name"), QStringLiteral("Info")}};
    for (auto it = extra.begin(); it != extra.end(); ++it) {
      object.insert(it.key(), it.value());
    }
    return patchy::ui::load_theme_from_json(QJsonDocument(object).toJson());
  };
  const auto future = parse(QJsonObject{{QStringLiteral("version"), QStringLiteral(" 2.1 ")},
                                        {QStringLiteral("author"), QStringLiteral("Ann <b>Bold</b>")},
                                        {QStringLiteral("minPatchyVersion"), QStringLiteral("999.0")}});
  CHECK(future.theme.has_value() && future.warnings.isEmpty());
  if (!future.theme) {
    return;
  }
  CHECK(future.theme->info.version == QStringLiteral("2.1"));
  CHECK(future.theme->info.author == QStringLiteral("Ann <b>Bold</b>"));
  CHECK(patchy::ui::theme_needs_newer_patchy(future.theme->info));
  const auto current = parse(QJsonObject{{QStringLiteral("minPatchyVersion"), QStringLiteral("1.0")}});
  CHECK(current.theme && !patchy::ui::theme_needs_newer_patchy(current.theme->info));
  CHECK(!patchy::ui::theme_needs_newer_patchy(patchy::ui::ThemeInfo{}));
  // Bad values are warnings and stay empty; the theme still loads.
  const auto bad = parse(QJsonObject{{QStringLiteral("minPatchyVersion"), QStringLiteral("soon")},
                                     {QStringLiteral("author"), 7}});
  CHECK(bad.theme.has_value() && bad.warnings.size() == 2);
  CHECK(bad.theme && bad.theme->info.min_patchy_version.isEmpty() && bad.theme->info.author.isEmpty());
  // Written back only when given, and read back the same.
  const auto json = patchy::ui::serialize_theme_to_json(future.theme->palette, future.theme->base,
                                                        QStringLiteral("Info"), nullptr, &future.theme->info);
  const auto reread = patchy::ui::load_theme_from_json(json);
  CHECK(reread.theme && reread.theme->info.version == QStringLiteral("2.1") &&
        reread.theme->info.min_patchy_version == QStringLiteral("999.0"));
  CHECK(!patchy::ui::serialize_theme_to_json(future.theme->palette, future.theme->base, QStringLiteral("Info"))
             .contains("minPatchyVersion"));

  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  PackageThemesDir themes;
  write_bytes(themes.file(QStringLiteral("future.patchytheme")), json);
  write_bytes(themes.file(QStringLiteral("broken.patchytheme")), QByteArrayLiteral("{ not json"));

  patchy::ui::MainWindow window;
  show_window(window);
  int warnings_seen = 0;
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* list = preferences_theme_list(*dialog);
    CHECK(list != nullptr);
    if (list == nullptr) {
      dialog->reject();
      return;
    }
    const int row = preferences_theme_row(*list, QStringLiteral("custom:future.patchytheme"));
    CHECK(row >= 0);
    if (row < 0) {
      dialog->reject();
      return;
    }
    const auto subtitle = list->item(row)->data(Qt::UserRole + 1).toString();
    CHECK(subtitle.contains(QStringLiteral("Version 2.1")) && subtitle.contains(QStringLiteral("by Ann")) &&
          subtitle.contains(QStringLiteral("Needs Patchy 999.0")));
    // A theme that does not load is listed, but cannot be chosen.
    const int broken = preferences_theme_row(*list, QStringLiteral("custom:broken.patchytheme"));
    CHECK(broken >= 0 && (list->item(broken)->flags() & Qt::ItemIsSelectable) == 0);
    CHECK(broken >= 0 && !list->item(broken)->toolTip().isEmpty());

    // Selecting it warns once per dialog, as plain text.
    const auto answer_warning = [&warnings_seen] {
      QTimer::singleShot(0, [&warnings_seen] {
        auto* box = qobject_cast<QMessageBox*>(find_top_level_dialog(QStringLiteral("themeNeedsNewerPatchyMessageBox")));
        CHECK(box != nullptr);
        if (box != nullptr) {
          ++warnings_seen;
          CHECK(box->textFormat() == Qt::PlainText);
          CHECK(box->text().contains(QStringLiteral("999.0")));
          box->accept();
        }
      });
    };
    // Selected directly: the select helper processes events, which would run the
    // answer before the box opens.
    answer_warning();
    list->setCurrentRow(row);
    process_events_for(50);
    CHECK(warnings_seen == 1);
    auto* meta = dialog->findChild<QLabel*>(QStringLiteral("preferencesThemeMeta"));
    auto* compatibility = dialog->findChild<QLabel*>(QStringLiteral("preferencesThemeCompatibility"));
    CHECK(meta != nullptr && meta->textFormat() == Qt::PlainText && meta->text().contains(QStringLiteral("by Ann <b>")));
    CHECK(compatibility != nullptr && compatibility->isVisible() &&
          compatibility->text().contains(QStringLiteral("999.0")));
    CHECK(select_preferences_theme(*dialog, QStringLiteral("dark")));
    CHECK(compatibility != nullptr && !compatibility->isVisible());
    CHECK(select_preferences_theme(*dialog, QStringLiteral("custom:future.patchytheme")));
    process_events_for(50);
    CHECK(warnings_seen == 1);
    save_widget_artifact("preferences_theme_needs_newer_patchy", *dialog);
    saw_dialog = true;
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(!patchy::ui::has_active_custom_palette());
}

// theme.json "description": kept with its line breaks, capped, carried by Export Theme
// Package, and shown under "Theme description:" in Colors and Themes only for a theme
// that has one (Dink Smallwood does, Dark does not).
void ui_theme_description_parses_round_trips_and_shows_in_details() {
  const auto parse = [](const QJsonValue& description) {
    const QJsonObject object{{QStringLiteral("base"), QStringLiteral("dark")},
                             {QStringLiteral("name"), QStringLiteral("Described")},
                             {QStringLiteral("description"), description}};
    return patchy::ui::load_theme_from_json(QJsonDocument(object).toJson());
  };
  const auto described = parse(QStringLiteral("  First   line.\r\n\r\nSecond <b>line</b>.  "));
  CHECK(described.theme.has_value() && described.warnings.isEmpty());
  CHECK(described.theme && described.theme->info.description == QStringLiteral("First line.\n\nSecond <b>line</b>."));
  const auto wrong = parse(QJsonValue(42));
  CHECK(wrong.theme.has_value() && wrong.warnings.size() == 1);
  CHECK(wrong.theme && wrong.theme->info.description.isEmpty() &&
        wrong.warnings.value(0).contains(QStringLiteral("description")));
  const auto long_one = parse(QString(patchy::ui::kThemeDescriptionMaxLength + 50, QLatin1Char('x')));
  CHECK(long_one.theme && long_one.theme->info.description.size() == patchy::ui::kThemeDescriptionMaxLength);
  if (described.theme) {
    CHECK(!patchy::ui::serialize_theme_to_json(described.theme->palette, described.theme->base,
                                               QStringLiteral("Described"))
               .contains("description"));
  }

  {
    // Export Theme Package writes it back.
    PackageThemesDir themes;
    write_sample_package(themes.file(QStringLiteral("Source")), QColor(40, 50, 60),
                         QJsonObject{{QStringLiteral("description"), QStringLiteral("Line one.\nLine two.")}});
    const auto source = patchy::ui::load_theme_by_id(QStringLiteral("Source"));
    CHECK(source.theme && source.theme->info.description == QStringLiteral("Line one.\nLine two."));
    if (source.theme) {
      write_bytes(themes.file(QStringLiteral("Zipped.zip")),
                  patchy::ui::serialize_theme_package_zip(source.theme->palette, source.theme->base,
                                                          QStringLiteral("Zipped"), source.theme->assets.get()));
      const auto zipped = patchy::ui::load_theme_by_id(QStringLiteral("Zipped.zip"));
      CHECK(zipped.theme && zipped.theme->info.description == QStringLiteral("Line one.\nLine two."));
    }
  }

  SettingsValueRestorer restore_scheme(QStringLiteral("preferences/colorScheme"));
  SettingsValueRestorer restore_custom_id(QStringLiteral("preferences/customThemeId"));
  ColorSchemeRestorer restore_active;
  ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  PackageThemesDir no_user_themes;  // only the bundled set in the list (and the capture)
  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  QTimer::singleShot(0, [&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    auto* heading = dialog->findChild<QLabel*>(QStringLiteral("preferencesThemeDescriptionHeading"));
    auto* text = dialog->findChild<QLabel*>(QStringLiteral("preferencesThemeDescription"));
    CHECK(heading != nullptr && text != nullptr);
    if (heading == nullptr || text == nullptr) {
      dialog->reject();
      return;
    }
    CHECK(select_preferences_theme(*dialog, QStringLiteral("custom:bundled:dink-smallwood")));
    process_events_for(50);
    CHECK(heading->isVisible() && heading->text() == QStringLiteral("Theme description:"));
    CHECK(text->isVisible() && text->textFormat() == Qt::PlainText && text->wordWrap());
    CHECK(text->text().startsWith(QStringLiteral("This unholy eyesore")) &&
          text->text().endsWith(QStringLiteral("as a prank on friends.")));
    // The capture: tall enough to show the whole details column, scrolled to its end.
    dialog->resize(dialog->width(), std::max(dialog->height(), 680));
    process_events_for(50);
    for (auto* parent = text->parentWidget(); parent != nullptr; parent = parent->parentWidget()) {
      if (auto* scroll = qobject_cast<QScrollArea*>(parent); scroll != nullptr) {
        scroll->ensureWidgetVisible(text);
        break;
      }
    }
    process_events_for(50);
    save_widget_artifact("preferences_theme_description_dink", *dialog);
    CHECK(select_preferences_theme(*dialog, QStringLiteral("dark")));
    CHECK(!heading->isVisible() && !text->isVisible());
    saw_dialog = true;
    dialog->reject();
  });
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(saw_dialog);
  CHECK(!patchy::ui::has_active_custom_palette());
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
      {"ui_theme_package_summary_reads_only_manifest_and_backdrop",
       ui_theme_package_summary_reads_only_manifest_and_backdrop},
      {"ui_preferences_theme_list_loads_packages_when_selected", ui_preferences_theme_list_loads_packages_when_selected},
      {"ui_preferences_theme_row_plays_its_startup_sound", ui_preferences_theme_row_plays_its_startup_sound},
      {"ui_theme_package_icon_template_and_example_package", ui_theme_package_icon_template_and_example_package},
      {"ui_bundled_theme_package_loads_from_resources_and_source",
       ui_bundled_theme_package_loads_from_resources_and_source},
      {"ui_theme_package_capture_shots", ui_theme_package_capture_shots},
      {"ui_theme_info_shows_version_author_and_minimum_patchy", ui_theme_info_shows_version_author_and_minimum_patchy},
      {"ui_theme_description_parses_round_trips_and_shows_in_details",
       ui_theme_description_parses_round_trips_and_shows_in_details},
      {"ui_theme_svg_icons_reaching_outside_are_refused", ui_theme_svg_icons_reaching_outside_are_refused},
      {"ui_theme_sounds_refuse_malformed_wav", ui_theme_sounds_refuse_malformed_wav},
      {"ui_theme_wav_loop_helpers_and_player", ui_theme_wav_loop_helpers_and_player},
      {"ui_theme_cache_prunes_old_files_and_caps_writes", ui_theme_cache_prunes_old_files_and_caps_writes},
  };
}
