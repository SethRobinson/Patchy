// The pictures of the theme guide (docs/making-themes.md and its advanced pages), as
// ui_theme_guide_shot_* scenes: each one stages the real windows offscreen, applies a theme
// built from themes/example-package the way the guide's steps build it, and writes
// test-artifacts/theme_guide_*.png. scripts/dev/copy-theme-guide-shots.py copies those
// into docs/images/themes/. The scenes stay in the suite as smoke tests of the steps the
// guide describes; docs/theme-files.md ("User guide") has the recipe.

#include "core/document.hpp"
#include "local_psd_fixtures.hpp"
#include "test_harness.hpp"
#include "ui/app_settings.hpp"
#include "ui/icon_theme.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_effects.hpp"
#include "ui/theme_file.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_package.hpp"
#include "ui/theme_palette.hpp"
#include "ui/theme_script_host.hpp"
#include "readme_screenshot_test_support.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScopeGuard>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace patchy::test::ui;
using patchy::ui::ThemeManager;

// The folder name the guide tells its reader to create.
const QString kGuideThemeId = QStringLiteral("My First Theme");

// A scratch themes folder for one scene: no sounds, no stored script answers, effects on,
// an empty recent-files list (the Home Screen would show this machine's paths), and no
// theme left active when the folder goes.
class GuideThemesDir {
public:
  GuideThemesDir()
      : restore_dir_("PATCHY_THEMES_DIR"), restore_sound_("PATCHY_NO_SOUND"),
        restore_trust_(QStringLiteral("themeScripts/trust")),
        restore_effects_(QStringLiteral("preferences/playThemeEffects")),
        restore_scheme_(QStringLiteral("preferences/colorScheme")),
        restore_custom_id_(QStringLiteral("preferences/customThemeId")),
        restore_page_(QStringLiteral("preferences/lastPage")) {
    CHECK(dir_.isValid());
    qputenv("PATCHY_THEMES_DIR", dir_.path().toUtf8());
    qputenv("PATCHY_NO_SOUND", "1");
    auto settings = patchy::ui::app_settings();
    settings.remove(QStringLiteral("themeScripts/trust"));
    settings.setValue(QStringLiteral("preferences/playThemeEffects"), true);
    auto history = patchy::ui::recent_history_settings();
    saved_recent_ = history.value(QStringLiteral("recentFiles")).toStringList();
    history.setValue(QStringLiteral("recentFiles"), QStringList());
    history.sync();
    ColorSchemeRestorer::apply(patchy::ui::ColorSchemePreference::Dark);
  }
  ~GuideThemesDir() {
    ThemeManager::instance().clear_custom_theme(/*persist=*/false);
    QApplication::processEvents();
    auto history = patchy::ui::recent_history_settings();
    history.setValue(QStringLiteral("recentFiles"), saved_recent_);
    history.sync();
  }
  [[nodiscard]] QString file(const QString& relative) const { return QDir(dir_.path()).filePath(relative); }

private:
  EnvironmentVariableRestorer restore_dir_;
  EnvironmentVariableRestorer restore_sound_;
  SettingsValueRestorer restore_trust_;
  SettingsValueRestorer restore_effects_;
  SettingsValueRestorer restore_scheme_;
  SettingsValueRestorer restore_custom_id_;
  SettingsValueRestorer restore_page_;
  ColorSchemeRestorer restore_active_;
  QStringList saved_recent_;
  QTemporaryDir dir_;
};

void write_bytes(const QString& path, const QByteArray& bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(bytes);
}

QByteArray png_bytes(const QImage& image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes;
}

QString example_path(const QString& relative) {
  return QDir(QStringLiteral(PATCHY_SOURCE_DIR)).filePath(QStringLiteral("themes/example-package/") + relative);
}

QByteArray example_bytes(const QString& relative) {
  QFile file(example_path(relative));
  CHECK(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

QJsonObject example_manifest() { return QJsonDocument::fromJson(example_bytes(QStringLiteral("theme.json"))).object(); }

QImage sunset() {
  const auto path = patchy::test::committed_format_fixture_path("readme", "stylized_sunset_cc0.png");
  QImage image(QString::fromStdString(path.string()));
  CHECK(!image.isNull());
  return image;
}

// The guide builds its theme in steps; each step is the example package with less in it.
enum class GuideStep {
  Colors,      // name, base and the chrome colors
  IconColors,  // plus the icon_* roles, which recolor every built-in icon
  Icons,       // plus the icons folder, drawn with the default options
  Complete,    // the example package as it is in the repository
};

QJsonObject guide_manifest(GuideStep step) {
  const auto example = example_manifest();
  if (step == GuideStep::Complete) {
    return example;
  }
  QJsonObject roles;
  const auto all = example.value(QStringLiteral("roles")).toObject();
  for (auto it = all.begin(); it != all.end(); ++it) {
    if (step != GuideStep::Colors || !it.key().startsWith(QStringLiteral("icon_"))) {
      roles.insert(it.key(), it.value());
    }
  }
  QJsonObject manifest;
  manifest.insert(QStringLiteral("name"), QStringLiteral("My First Theme"));
  manifest.insert(QStringLiteral("base"), example.value(QStringLiteral("base")));
  manifest.insert(QStringLiteral("roles"), roles);
  return manifest;
}

void copy_example_folder(const GuideThemesDir& themes, const QString& id, const QString& folder) {
  const QDir source(example_path(folder));
  for (const auto& name : source.entryList(QDir::Files)) {
    write_bytes(themes.file(id + QLatin1Char('/') + folder + QLatin1Char('/') + name),
                example_bytes(folder + QLatin1Char('/') + name));
  }
}

// Writes the guide's theme folder as it stands after `step`.
void write_guide_theme(const GuideThemesDir& themes, GuideStep step, const QString& id = kGuideThemeId,
                       const QJsonObject& extra = {}) {
  auto manifest = guide_manifest(step);
  for (auto it = extra.begin(); it != extra.end(); ++it) {
    manifest.insert(it.key(), it.value());
  }
  write_bytes(themes.file(id + QStringLiteral("/theme.json")), QJsonDocument(manifest).toJson());
  if (step == GuideStep::Icons || step == GuideStep::Complete) {
    copy_example_folder(themes, id, QStringLiteral("icons"));
  }
  if (step == GuideStep::Complete) {
    for (const auto& folder : {QStringLiteral("images"), QStringLiteral("sounds"), QStringLiteral("scripts")}) {
      copy_example_folder(themes, id, folder);
    }
  }
}

// Loads a theme from the scratch folder and shows it, with its scripts allowed or not
// (stored first, so no prompt opens). A load warning fails the scene: the guide never
// shows a theme Patchy complains about.
bool apply_guide_theme(const QString& id, bool run_scripts = false) {
  auto loaded = patchy::ui::load_theme_by_id(id);
  CHECK(loaded.error.isEmpty());
  CHECK(loaded.warnings.isEmpty());
  CHECK(loaded.theme.has_value());
  if (!loaded.theme) {
    return false;
  }
  if (loaded.theme->assets != nullptr && loaded.theme->assets->has_scripts()) {
    patchy::ui::store_theme_script_trust(id, patchy::ui::theme_scripts_digest(*loaded.theme->assets), run_scripts);
  }
  ThemeManager::instance().set_custom_theme(id, *loaded.theme, /*persist=*/false);
  process_events_for(250);
  return true;
}

void save_guide_shot(const std::string& name, const QImage& image, bool window_corners = false) {
  ensure_artifact_dir();
  CHECK(!image.isNull());
  const auto path = QString::fromStdString((std::filesystem::path("test-artifacts") / (name + ".png")).string());
  CHECK((window_corners ? rounded_readme_window_image(image) : image).save(path));
}

// A grab that first runs the capture guard (ui_test_support: every visible widget under
// `widget` carries the active theme), so a half-themed window never becomes a picture.
QImage grab_themed(QWidget& widget, const QRect& rect = QRect()) {
  const auto mismatch = capture_theme_mismatch(widget);
  if (!mismatch.isEmpty()) {
    std::fprintf(stderr, "theme guide capture: %s\n", qPrintable(mismatch));
  }
  CHECK(mismatch.isEmpty());
  return (rect.isNull() ? widget.grab() : widget.grab(rect)).toImage();
}

// Pictures on a transparent sheet, `columns` to a row, each under a small label that reads
// on a light or a dark page.
QImage labeled_grid(const std::vector<std::pair<QString, QImage>>& parts, int columns) {
  constexpr int kGap = 16;
  constexpr int kLabel = 30;
  auto font = QApplication::font();
  font.setPixelSize(13);
  font.setBold(true);
  const QFontMetrics metrics(font);
  const auto pill_width = [&metrics](const QString& label) { return metrics.horizontalAdvance(label) + 20; };
  int cell_width = 1;
  int cell_height = 1;
  for (const auto& [label, image] : parts) {
    cell_width = std::max({cell_width, image.width(), pill_width(label)});
    cell_height = std::max(cell_height, image.height());
  }
  const int count = static_cast<int>(parts.size());
  columns = std::clamp(columns, 1, std::max(1, count));
  const int rows = (count + columns - 1) / columns;
  QImage sheet(columns * cell_width + (columns - 1) * kGap, rows * (cell_height + kLabel) + (rows - 1) * kGap,
               QImage::Format_ARGB32_Premultiplied);
  sheet.fill(Qt::transparent);
  QPainter painter(&sheet);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setFont(font);
  for (int i = 0; i < count; ++i) {
    const auto& [label, image] = parts[static_cast<std::size_t>(i)];
    const int x = (i % columns) * (cell_width + kGap);
    const int y = (i / columns) * (cell_height + kLabel + kGap);
    const QRectF pill(x, y + 2, pill_width(label), kLabel - 10);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(52, 60, 72));
    painter.drawRoundedRect(pill, 10, 10);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(pill, Qt::AlignCenter, label);
    painter.drawImage(x, y + kLabel, image);
  }
  return sheet;
}

QImage labeled_row(const std::vector<std::pair<QString, QImage>>& parts) {
  return labeled_grid(parts, static_cast<int>(parts.size()));
}

// A main window with the sunset open, at the size the guide's window pictures use.
void stage_document_window(patchy::ui::MainWindow& window, QSize size = QSize(1180, 740)) {
  window.resize(size);
  window.show();
  QApplication::processEvents();
  const auto art = sunset().scaled(900, 560, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  window.add_document_session(patchy::ui::document_from_qimage(art, "Sunset"), QStringLiteral("Sunset"));
  close_untitled_start_tab(window);
  process_events_for(150);
  reset_readme_status_bar(window);
  QApplication::processEvents();
}

// The upper left of the window: the top of the tool palette, the options bar's first
// controls and a corner of the canvas, where a theme's icons show best.
QImage tools_corner(patchy::ui::MainWindow& window) {
  return grab_themed(window, QRect(0, 0, 430, 470));
}

// Opens Preferences, runs `stage` on the dialog (already on Colors and Themes, sized for
// the guide) and closes it again without keeping anything.
template <typename Stage>
void with_preferences(patchy::ui::MainWindow& window, const Stage& stage, int height = 700) {
  bool staged = false;
  QTimer::singleShot(0, &window, guarded([&] {
    auto* dialog = find_top_level_dialog(QStringLiteral("patchyPreferencesDialog"));
    CHECK(dialog != nullptr);
    if (dialog == nullptr) {
      return;
    }
    CHECK(preferences_theme_list(*dialog) != nullptr);
    dialog->resize(std::max(dialog->minimumSizeHint().width(), 800), height);
    process_events_for(150);
    stage(*dialog);
    staged = true;
    dialog->reject();
  }));
  require_action(window, "filePreferencesAction")->trigger();
  QApplication::processEvents();
  CHECK(staged);
}

void scroll_to_theme(QDialog& dialog, const QString& token) {
  auto* list = dialog.findChild<QListWidget*>(QStringLiteral("preferencesThemeList"));
  CHECK(list != nullptr);
  if (list == nullptr) {
    return;
  }
  for (int row = 0; row < list->count(); ++row) {
    if (list->item(row)->data(Qt::UserRole).toString() == token) {
      list->scrollToItem(list->item(row), QAbstractItemView::PositionAtBottom);
    }
  }
  process_events_for(100);
}

// Step 1 and 2 of "Your first theme": the Colors and Themes page as it opens, then the
// new theme listed under Your themes and selected; the window before and after.
void ui_theme_guide_shot_first_theme() {
  GuideThemesDir themes;
  patchy::ui::MainWindow window;
  stage_document_window(window);
  save_guide_shot("theme_guide_colors_before", grab_themed(window), /*window_corners=*/true);

  with_preferences(window, [](QDialog& dialog) {
    CHECK(select_preferences_theme(dialog, QStringLiteral("dark")));
    CHECK(dialog.findChild<QPushButton*>(QStringLiteral("preferencesThemeGuideButton")) != nullptr);
    save_guide_shot("theme_guide_preferences", grab_themed(dialog), /*window_corners=*/true);
  });

  write_guide_theme(themes, GuideStep::Colors);
  with_preferences(window, [](QDialog& dialog) {
    auto* reload = dialog.findChild<QPushButton*>(QStringLiteral("preferencesReloadThemesButton"));
    CHECK(reload != nullptr);
    if (reload != nullptr) {
      reload->click();
    }
    CHECK(select_preferences_theme(dialog, QStringLiteral("custom:") + kGuideThemeId));
    scroll_to_theme(dialog, QStringLiteral("custom:") + kGuideThemeId);
    save_guide_shot("theme_guide_first_theme_selected", grab_themed(dialog), /*window_corners=*/true);
  });

  CHECK(apply_guide_theme(kGuideThemeId));
  save_guide_shot("theme_guide_colors_after", grab_themed(window), /*window_corners=*/true);
}

// A contact sheet of some of the files Export Icon Template writes, each under its file
// name: the names are what a theme's icons/ folder has to match.
void ui_theme_guide_shot_icon_template() {
  GuideThemesDir themes;
  QTemporaryDir out;
  CHECK(out.isValid());
  QString error;
  const auto folder = QDir(out.path()).filePath(QStringLiteral("icon-template"));
  const int written = patchy::ui::export_icon_template(folder, &error);
  CHECK(written == patchy::ui::overridable_icon_stems().size());
  const QStringList stems{
      QStringLiteral("tool-move"),    QStringLiteral("tool-marquee"),   QStringLiteral("tool-lasso"),
      QStringLiteral("tool-wand"),    QStringLiteral("tool-crop"),      QStringLiteral("tool-eyedropper"),
      QStringLiteral("tool-brush"),   QStringLiteral("tool-eraser"),    QStringLiteral("tool-fill"),
      QStringLiteral("tool-gradient"), QStringLiteral("tool-clone"),    QStringLiteral("tool-healing"),
      QStringLiteral("tool-pen"),     QStringLiteral("tool-text"),      QStringLiteral("tool-rect"),
      QStringLiteral("tool-pan"),     QStringLiteral("tool-zoom"),      QStringLiteral("tool-smudge"),
  };
  constexpr int kColumns = 6;
  constexpr int kCellWidth = 150;
  constexpr int kCellHeight = 92;
  const int rows = static_cast<int>((stems.size() + kColumns - 1) / kColumns);
  const auto& colors = patchy::ui::theme();
  QImage sheet(kColumns * kCellWidth, rows * kCellHeight, QImage::Format_ARGB32_Premultiplied);
  sheet.fill(colors.panel_bg);
  QPainter painter(&sheet);
  painter.setRenderHint(QPainter::Antialiasing);
  auto font = QApplication::font();
  font.setPixelSize(12);
  painter.setFont(font);
  for (int i = 0; i < stems.size(); ++i) {
    const auto& stem = stems[i];
    // Every listed stem is a file the export wrote.
    CHECK(QFile::exists(QDir(folder).filePath(stem + QStringLiteral(".svg"))));
    const QRect cell((i % kColumns) * kCellWidth, (i / kColumns) * kCellHeight, kCellWidth, kCellHeight);
    const auto icon = patchy::ui::themed_svg_icon(stem).pixmap(QSize(40, 40));
    CHECK(!icon.isNull());
    painter.drawPixmap(cell.center().x() - 20, cell.top() + 12, icon);
    painter.setPen(colors.text_secondary);
    painter.drawText(cell.adjusted(2, 58, -2, -6), Qt::AlignHCenter | Qt::AlignTop, stem + QStringLiteral(".svg"));
  }
  painter.end();
  save_guide_shot("theme_guide_icon_template", sheet, /*window_corners=*/true);
}

// The icon steps: the built-in icons recolored by the icon_* roles, then three of them
// replaced by the theme's own files.
void ui_theme_guide_shot_icons() {
  GuideThemesDir themes;
  patchy::ui::MainWindow window;
  stage_document_window(window);

  write_guide_theme(themes, GuideStep::Colors);
  CHECK(apply_guide_theme(kGuideThemeId));
  const auto before = tools_corner(window);

  write_guide_theme(themes, GuideStep::IconColors);
  CHECK(apply_guide_theme(kGuideThemeId));
  const auto recolored = tools_corner(window);
  save_guide_shot("theme_guide_icon_colors", labeled_row({{QStringLiteral("Before"), before},
                                                          {QStringLiteral("With icon_ink and icon_accent"), recolored}}));

  write_guide_theme(themes, GuideStep::Icons);
  CHECK(apply_guide_theme(kGuideThemeId));
  const auto replaced = tools_corner(window);
  save_guide_shot("theme_guide_icons_replaced",
                  labeled_row({{QStringLiteral("Built-in icons"), recolored},
                               {QStringLiteral("Move, Brush and Eraser replaced"), replaced}}));
  // The three overrides are really in use.
  CHECK(replaced != recolored);
}

// "recolorIcons": an SVG drawn in the template's own colors follows the theme (here a
// Light-based one) unless the theme turns recoloring off.
void ui_theme_guide_shot_recolor_icons() {
  GuideThemesDir themes;
  patchy::ui::MainWindow window;
  stage_document_window(window);
  // Move is the active tool, so the brush button shows on the plain palette.
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  // The built-in brush with a star added, still in the template's two colors.
  const QByteArray star_brush = QByteArrayLiteral(
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">\n"
      "  <path d=\"M27.5 4.5L17 15\" fill=\"none\" stroke=\"#dce2eb\" stroke-width=\"4.2\" stroke-linecap=\"round\"/>\n"
      "  <path d=\"M19.4 17.4L14.6 12.6L12.4 14.8L17.2 19.6Z\" fill=\"#dce2eb\"/>\n"
      "  <path d=\"M11.6 15.6L16.4 20.4C14.6 24.6 10.4 27.4 4 29C5.6 22.6 8.4 18.4 11.6 15.6Z\" fill=\"#74c0ff\"/>\n"
      "  <path d=\"M7 3L8.4 6.6L12 8L8.4 9.4L7 13L5.6 9.4L2 8L5.6 6.6Z\" fill=\"#74c0ff\"/>\n"
      "</svg>\n");
  std::vector<std::pair<QString, QImage>> parts;
  for (const bool recolor : {true, false}) {
    const QString id = recolor ? QStringLiteral("Recolored") : QStringLiteral("As drawn");
    QJsonObject manifest;
    manifest.insert(QStringLiteral("name"), id);
    manifest.insert(QStringLiteral("base"), QStringLiteral("light"));
    manifest.insert(QStringLiteral("recolorIcons"), recolor);
    write_bytes(themes.file(id + QStringLiteral("/theme.json")), QJsonDocument(manifest).toJson());
    write_bytes(themes.file(id + QStringLiteral("/icons/tool-brush.svg")), star_brush);
    CHECK(apply_guide_theme(id));
    parts.emplace_back(recolor ? QStringLiteral("\"recolorIcons\": true (default)")
                               : QStringLiteral("\"recolorIcons\": false"),
                       grab_themed(window, QRect(0, 112, 300, 270)));
  }
  CHECK(parts[0].second != parts[1].second);
  save_guide_shot("theme_guide_recolor_icons", labeled_row(parts));
}

// "iconScaling": the 16-pixel eraser drawn at 64 pixels, smooth and nearest.
void ui_theme_guide_shot_icon_scaling() {
  GuideThemesDir themes;
  std::vector<std::pair<QString, QImage>> parts;
  for (const bool nearest : {false, true}) {
    const QString id = nearest ? QStringLiteral("Nearest") : QStringLiteral("Smooth");
    QJsonObject manifest;
    manifest.insert(QStringLiteral("name"), id);
    manifest.insert(QStringLiteral("base"), QStringLiteral("dark"));
    manifest.insert(QStringLiteral("iconScaling"), nearest ? QStringLiteral("nearest") : QStringLiteral("smooth"));
    write_bytes(themes.file(id + QStringLiteral("/theme.json")), QJsonDocument(manifest).toJson());
    write_bytes(themes.file(id + QStringLiteral("/icons/tool-eraser.png")),
                example_bytes(QStringLiteral("icons/tool-eraser.png")));
    CHECK(apply_guide_theme(id));
    const auto icon = patchy::ui::themed_svg_icon(QStringLiteral("tool-eraser")).pixmap(QSize(64, 64)).toImage();
    CHECK(!icon.isNull());
    // Drawn at three times that size without smoothing, so each of the icon's pixels shows.
    QImage tile(256, 224, QImage::Format_ARGB32_Premultiplied);
    tile.fill(patchy::ui::theme().tool_palette_bg);
    QPainter painter(&tile);
    painter.drawImage(QRect(32, 16, 192, 192), icon);
    painter.end();
    parts.emplace_back(nearest ? QStringLiteral("\"iconScaling\": \"nearest\"")
                               : QStringLiteral("\"iconScaling\": \"smooth\" (default)"),
                       tile);
  }
  CHECK(parts[0].second != parts[1].second);
  save_guide_shot("theme_guide_icon_scaling", labeled_row(parts));
}

// The three backdrop modes behind the Home Screen, each a crop at full size: the tool
// palette and the left of the Home Screen, where the picture's edges show.
void ui_theme_guide_shot_backdrop() {
  GuideThemesDir themes;
  patchy::ui::MainWindow window;
  window.resize(1000, 560);
  window.show();
  QApplication::processEvents();
  close_untitled_start_tab(window);
  process_events_for(100);
  std::vector<std::pair<QString, QImage>> parts;
  for (const auto& mode : {QStringLiteral("tile"), QStringLiteral("stretch"), QStringLiteral("center")}) {
    const QString id = QStringLiteral("Backdrop ") + mode;
    QJsonObject backdrop;
    backdrop.insert(QStringLiteral("image"), QStringLiteral("images/backdrop.png"));
    backdrop.insert(QStringLiteral("mode"), mode);
    // The example's own tile; the sunset for the two modes that show one picture.
    const auto image =
        mode == QStringLiteral("tile")
            ? example_bytes(QStringLiteral("images/backdrop.png"))
            : png_bytes(sunset().scaled(mode == QStringLiteral("center") ? QSize(260, 165) : QSize(900, 560),
                                        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto manifest = guide_manifest(GuideStep::Colors);
    manifest.insert(QStringLiteral("name"), id);
    manifest.insert(QStringLiteral("backdrop"), backdrop);
    write_bytes(themes.file(id + QStringLiteral("/theme.json")), QJsonDocument(manifest).toJson());
    write_bytes(themes.file(id + QStringLiteral("/images/backdrop.png")), image);
    CHECK(apply_guide_theme(id));
    reset_readme_status_bar(window);
    QApplication::processEvents();
    parts.emplace_back(QStringLiteral("\"mode\": \"%1\"").arg(mode), grab_themed(window, QRect(0, 110, 660, 385)));
  }
  CHECK(parts[0].second != parts[1].second);
  CHECK(parts[1].second != parts[2].second);
  save_guide_shot("theme_guide_backdrop_modes", labeled_grid(parts, 1));
}

// The three built-in effects, each caught part way through over a document.
void ui_theme_guide_shot_effects() {
  GuideThemesDir themes;
  patchy::ui::MainWindow window;
  stage_document_window(window, QSize(1000, 660));
  write_guide_theme(themes, GuideStep::Complete);
  CHECK(apply_guide_theme(kGuideThemeId));
  auto* overlay = window.theme_effects_overlay();
  CHECK(overlay != nullptr);
  if (overlay == nullptr) {
    return;
  }
  std::vector<std::pair<QString, QImage>> parts;
  for (const auto& name : {QStringLiteral("fireworks"), QStringLiteral("sparkles"), QStringLiteral("confetti")}) {
    overlay->clear();
    CHECK(overlay->play(QVariantMap{{QStringLiteral("name"), name}}, nullptr));
    overlay->advance_for_testing(name == QStringLiteral("confetti") ? 900 : 550);
    process_events_for(60);
    // The document area around the effect's default position.
    parts.emplace_back(QStringLiteral("\"%1\"").arg(name), grab_themed(window, QRect(195, 170, 280, 300)));
  }
  overlay->clear();
  save_guide_shot("theme_guide_effects", labeled_row(parts));
}

// A theme script's menu: the example's one command, right of Help.
void ui_theme_guide_shot_theme_menu() {
  GuideThemesDir themes;
  patchy::ui::MainWindow window;
  stage_document_window(window);
  write_guide_theme(themes, GuideStep::Complete);
  CHECK(apply_guide_theme(kGuideThemeId, /*run_scripts=*/true));
  process_events_for(200);
  auto* menu = window.findChild<QMenu*>(QStringLiteral("themeMenu"));
  CHECK(menu != nullptr);
  if (menu == nullptr) {
    return;
  }
  CHECK(menu->menuAction()->isVisible());
  CHECK(menu->actions().size() == 1);
  const auto title_rect = window.menuBar()->actionGeometry(menu->menuAction());
  const auto below = window.menuBar()->mapTo(&window, title_rect.bottomLeft());
  menu->ensurePolished();
  menu->resize(menu->sizeHint());
  const QPoint corner(380, 0);
  auto base = grab_themed(window, QRect(corner, QSize(600, 200)));
  draw_readme_overlay(base, menu->grab().toImage(), below + QPoint(0, 2) - corner);
  save_guide_shot("theme_guide_theme_menu", base);
}

// What the user sees the first time a theme with scripts is applied, and the theme's
// details in Preferences with its own Run this theme's scripts box.
void ui_theme_guide_shot_script_trust() {
  GuideThemesDir themes;
  patchy::ui::MainWindow window;
  stage_document_window(window);
  const QString id = QStringLiteral("Example Package");
  write_guide_theme(themes, GuideStep::Complete, id);

  bool prompted = false;
  when_dialog_visible(QStringLiteral("themeScriptTrustPrompt"), [&prompted](QDialog& prompt) {
    process_events_for(100);
    save_guide_shot("theme_guide_trust_prompt", grab_themed(prompt), /*window_corners=*/true);
    prompted = true;
    auto* deny = prompt.findChild<QPushButton*>(QStringLiteral("themeScriptTrustDenyButton"));
    CHECK(deny != nullptr);
    if (deny != nullptr) {
      deny->click();
    }
  });
  // No stored answer: applying the theme asks, as it does for a user.
  auto loaded = patchy::ui::load_theme_by_id(id);
  CHECK(loaded.warnings.isEmpty());
  CHECK(loaded.theme.has_value());
  if (!loaded.theme) {
    return;
  }
  ThemeManager::instance().set_custom_theme(id, *loaded.theme, /*persist=*/false);
  CHECK(process_events_until([&prompted] { return prompted; }, 15000));

  with_preferences(window, [](QDialog& dialog) {
    const auto token = QStringLiteral("custom:Example Package");
    CHECK(select_preferences_theme(dialog, token));
    scroll_to_theme(dialog, token);
    save_guide_shot("theme_guide_example_details", grab_themed(dialog), /*window_corners=*/true);
  }, 800);
}

// Troubleshooting: a theme whose theme.json has a mistake stays in the list, greyed out.
void ui_theme_guide_shot_load_error() {
  GuideThemesDir themes;
  patchy::ui::MainWindow window;
  stage_document_window(window);
  write_guide_theme(themes, GuideStep::Colors);
  write_bytes(themes.file(QStringLiteral("Broken Theme/theme.json")),
              QByteArrayLiteral("{\n  \"name\": \"Broken Theme\",\n  \"base\": \"dark\",\n  \"roles\": {\n"
                                "    \"accent\": \"orange\"\n  }\n}\n"));
  const auto broken = patchy::ui::load_theme_by_id(QStringLiteral("Broken Theme"));
  CHECK(!broken.theme.has_value());
  CHECK(broken.error.contains(QStringLiteral("orange")));
  with_preferences(window, [](QDialog& dialog) {
    const auto token = QStringLiteral("custom:") + kGuideThemeId;
    CHECK(select_preferences_theme(dialog, token));
    scroll_to_theme(dialog, token);
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("preferencesThemeList"));
    CHECK(list != nullptr);
    if (list != nullptr) {
      save_guide_shot("theme_guide_load_error", grab_themed(*list));
    }
  });
}


// "Icons from an icon set": a line icon that says stroke="currentColor", the way Tabler
// and Lucide files do, used as the Brush icon exactly as downloaded. Patchy draws it in
// the theme's icon_ink.
void ui_theme_guide_shot_icon_set() {
  GuideThemesDir themes;
  patchy::ui::MainWindow window;
  stage_document_window(window);
  // Move is the active tool, so the brush button shows on the plain palette.
  require_action_by_text(window, QStringLiteral("Move"))->trigger();
  QApplication::processEvents();
  write_guide_theme(themes, GuideStep::IconColors);
  CHECK(apply_guide_theme(kGuideThemeId));
  const auto built_in = tools_corner(window);
  // Drawn for this guide in the icon sets' format: 24 units, no fill, a 2-unit round stroke.
  const QByteArray line_brush = QByteArrayLiteral(
      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\"\n"
      "     stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n"
      "  <path d=\"M20 4L10.5 13.5\"/>\n"
      "  <path d=\"M12.5 15.5L8.5 11.5\"/>\n"
      "  <path d=\"M8.5 11.5C5.5 12 4.5 15 4.5 17.5C4.5 18.5 4 19.5 3 20C7 20.5 12 19.5 12.5 15.5\"/>\n"
      "</svg>\n");
  CHECK(!line_brush.contains("#"));
  write_bytes(themes.file(kGuideThemeId + QStringLiteral("/icons/tool-brush.svg")), line_brush);
  CHECK(apply_guide_theme(kGuideThemeId));
  const auto from_set = tools_corner(window);
  save_guide_shot("theme_guide_icon_set", labeled_row({{QStringLiteral("Built-in Brush icon"), built_in},
                                                       {QStringLiteral("An icon-set file as tool-brush.svg"), from_set}}));
  CHECK(from_set != built_in);
  // The file's lines are the theme's icon_ink, and nothing in the icon is left black.
  const auto ink = patchy::ui::theme().icon_ink;
  CHECK(ink == QColor(QStringLiteral("#f1dcc0")));
  const auto drawn = patchy::ui::themed_svg_icon(QStringLiteral("tool-brush")).pixmap(QSize(48, 48)).toImage();
  int in_ink = 0;
  int black = 0;
  for (int y = 0; y < drawn.height(); ++y) {
    for (int x = 0; x < drawn.width(); ++x) {
      const auto pixel = drawn.pixelColor(x, y);
      if (pixel.alpha() == 255) {
        in_ink += pixel == ink ? 1 : 0;
        black += pixel == QColor(0, 0, 0) ? 1 : 0;
      }
    }
  }
  CHECK(in_ink > 40);
  CHECK(black == 0);
}

// Troubleshooting: a theme that loads with two mistakes in it (a misspelled role, an icon
// under a name no built-in icon has) selected in Preferences, which lists both under
// "Problems found".
void ui_theme_guide_shot_problems() {
  GuideThemesDir themes;
  patchy::ui::MainWindow window;
  stage_document_window(window);
  auto manifest = guide_manifest(GuideStep::Colors);
  auto roles = manifest.value(QStringLiteral("roles")).toObject();
  roles.insert(QStringLiteral("acent"), roles.take(QStringLiteral("accent")));
  manifest.insert(QStringLiteral("roles"), roles);
  write_bytes(themes.file(kGuideThemeId + QStringLiteral("/theme.json")), QJsonDocument(manifest).toJson());
  write_bytes(themes.file(kGuideThemeId + QStringLiteral("/icons/tool-brsh.svg")),
              example_bytes(QStringLiteral("icons/tool-brush.svg")));
  const auto loaded = patchy::ui::load_theme_by_id(kGuideThemeId);
  CHECK(loaded.theme.has_value());
  CHECK(loaded.warnings.size() == 2);
  with_preferences(window, [](QDialog& dialog) {
    const auto token = QStringLiteral("custom:") + kGuideThemeId;
    CHECK(select_preferences_theme(dialog, token));
    scroll_to_theme(dialog, token);
    save_guide_shot("theme_guide_problems_found", grab_themed(dialog), /*window_corners=*/true);
    auto* heading = dialog.findChild<QLabel*>(QStringLiteral("preferencesThemeProblemsHeading"));
    auto* problems = dialog.findChild<QLabel*>(QStringLiteral("preferencesThemeProblems"));
    CHECK(heading != nullptr && heading->isVisible());
    CHECK(problems != nullptr && problems->isVisible() && problems->text().contains(QStringLiteral("\"acent\"")) &&
          problems->text().contains(QStringLiteral("\"tool-brsh.svg\"")));
  });
}

}  // namespace

std::vector<patchy::test::TestCase> theme_guide_screenshot_tests() {
  return {
      {"ui_theme_guide_shot_first_theme", ui_theme_guide_shot_first_theme},
      {"ui_theme_guide_shot_icon_template", ui_theme_guide_shot_icon_template},
      {"ui_theme_guide_shot_icons", ui_theme_guide_shot_icons},
      {"ui_theme_guide_shot_recolor_icons", ui_theme_guide_shot_recolor_icons},
      {"ui_theme_guide_shot_icon_scaling", ui_theme_guide_shot_icon_scaling},
      {"ui_theme_guide_shot_backdrop", ui_theme_guide_shot_backdrop},
      {"ui_theme_guide_shot_effects", ui_theme_guide_shot_effects},
      {"ui_theme_guide_shot_theme_menu", ui_theme_guide_shot_theme_menu},
      {"ui_theme_guide_shot_script_trust", ui_theme_guide_shot_script_trust},
      {"ui_theme_guide_shot_load_error", ui_theme_guide_shot_load_error},
      {"ui_theme_guide_shot_icon_set", ui_theme_guide_shot_icon_set},
      {"ui_theme_guide_shot_problems", ui_theme_guide_shot_problems},
  };
}
