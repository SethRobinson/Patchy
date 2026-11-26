// The Home Screen's header layout and its Recent Files grid view with thumbnails (GitHub
// issue 750; docs/ui-conventions-menus.md, "Recent file thumbnails").

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include "formats/metadata/iptc_iim.hpp"
#include "local_psd_fixtures.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/network_mounts.hpp"
#include "ui/qt_paths.hpp"
#include "ui/recent_file_list.hpp"
#include "ui/recent_thumbnails.hpp"
#include "ui/start_panel.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using patchy::ui::kRecentPathRole;
using patchy::ui::MainWindow;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::RecentFileList;
using patchy::ui::RecentThumbnailCache;
using patchy::ui::RecentThumbnailState;
using patchy::ui::StartPanel;

QString q(std::u8string_view text) {
  return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), static_cast<qsizetype>(text.size()));
}

// Sets an environment variable for the scope and puts the old value back.
class EnvironmentGuard {
 public:
  EnvironmentGuard(const char* name, const QString& value)
      : name_(name), had_(qEnvironmentVariableIsSet(name)), old_(qEnvironmentVariable(name)) {
    set(value);
  }
  ~EnvironmentGuard() {
    if (had_) {
      set(old_);
    } else {
      qunsetenv(name_);
    }
  }
  EnvironmentGuard(const EnvironmentGuard&) = delete;
  EnvironmentGuard& operator=(const EnvironmentGuard&) = delete;

 private:
  // qputenv hands Windows the bytes as the process's 8-bit code page, which cannot hold a
  // Japanese or emoji folder name; the wide call can, and qEnvironmentVariable reads it back.
  void set(const QString& value) {
#ifdef Q_OS_WIN
    _wputenv_s(reinterpret_cast<const wchar_t*>(QString::fromLatin1(name_).utf16()),
               reinterpret_cast<const wchar_t*>(value.utf16()));
#else
    qputenv(name_, value.toUtf8());
#endif
  }

  const char* name_;
  bool had_;
  QString old_;
};

// The grid setting, the recent list and (for a test that needs its own) the thumbnail folder,
// all put back at the end of the scope.
struct GridTestSettings {
  patchy::test::ui::SettingsValueRestorer recent{QStringLiteral("recentFiles")};
  patchy::test::ui::SettingsValueRestorer folders{QStringLiteral("recentFolders")};
  patchy::test::ui::SettingsValueRestorer grid{QStringLiteral("startPanel/recentFilesGridView")};

  void set_recent(const QStringList& paths) {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), paths);
    settings.sync();
  }
  void set_grid(bool on) {
    patchy::ui::set_stored_start_panel_recent_grid_view(on);
    patchy::ui::app_settings().sync();
  }
};

QString scratch_file(const QString& name) {
  patchy::test::ui::ensure_artifact_dir();
  const QDir dir(QStringLiteral("test-artifacts/start-panel-grid"));
  CHECK(QDir().mkpath(dir.absolutePath()));
  return QFileInfo(dir.filePath(name)).absoluteFilePath();
}

QString write_png(const QString& path, QColor color, QSize size = QSize(320, 200)) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(color);
  CHECK(image.save(path, "PNG"));
  return path;
}

void put_u32(QByteArray& bytes, quint32 value) {
  bytes.append(static_cast<char>(value >> 24));
  bytes.append(static_cast<char>(value >> 16));
  bytes.append(static_cast<char>(value >> 8));
  bytes.append(static_cast<char>(value));
}

void put_u16(QByteArray& bytes, quint16 value) {
  bytes.append(static_cast<char>(value >> 8));
  bytes.append(static_cast<char>(value));
}

// The front of a PSD, which is all the thumbnail reader looks at: the header, an empty color
// mode section and an image-resource section holding one JPEG thumbnail (resource 1036). The
// layer and image data a real file continues with are left out on purpose: reading this
// file's picture must never need them.
QString write_psd_with_thumbnail(const QString& path, QColor color) {
  QImage thumbnail(64, 48, QImage::Format_RGB32);
  thumbnail.fill(color);
  QByteArray jpeg;
  {
    QBuffer buffer(&jpeg);
    CHECK(buffer.open(QIODevice::WriteOnly));
    CHECK(thumbnail.save(&buffer, "JPG", 95));
  }
  QByteArray resource;
  put_u32(resource, 1);  // kJpegRGB
  put_u32(resource, 64);
  put_u32(resource, 48);
  put_u32(resource, 64 * 3);
  put_u32(resource, 64 * 3 * 48);
  put_u32(resource, static_cast<quint32>(jpeg.size()));
  put_u16(resource, 24);
  put_u16(resource, 1);
  resource.append(jpeg);
  patchy::metadata::IrbResource block;
  block.id = patchy::metadata::psd_resource::kThumbnail;
  block.data.assign(reinterpret_cast<const std::uint8_t*>(resource.constData()),
                    reinterpret_cast<const std::uint8_t*>(resource.constData()) + resource.size());
  const auto irb = patchy::metadata::write_irb({block});

  QByteArray file("8BPS");
  put_u16(file, 1);
  file.append(QByteArray(6, '\0'));
  put_u16(file, 3);     // channels
  put_u32(file, 2000);  // height
  put_u32(file, 3000);  // width
  put_u16(file, 8);
  put_u16(file, 3);  // RGB
  put_u32(file, 0);  // color mode data
  put_u32(file, static_cast<quint32>(irb.size()));
  file.append(reinterpret_cast<const char*>(irb.data()), static_cast<qsizetype>(irb.size()));
  QFile out(path);
  CHECK(out.open(QIODevice::WriteOnly));
  CHECK(out.write(file) == file.size());
  return path;
}

bool near_color(QColor actual, QColor expected, int tolerance = 12) {
  return std::abs(actual.red() - expected.red()) <= tolerance &&
         std::abs(actual.green() - expected.green()) <= tolerance &&
         std::abs(actual.blue() - expected.blue()) <= tolerance;
}

QColor center_color(const QImage& image) {
  return image.pixelColor(image.width() / 2, image.height() / 2);
}

// Waits for the worker's answer for `path` (asking for it first, as a painted cell does).
RecentThumbnailState settled_state(RecentThumbnailCache& cache, const QString& path) {
  cache.request(path);
  CHECK(patchy::test::ui::process_events_until(
      [&] {
        const auto state = cache.state(path);
        return state == RecentThumbnailState::ready || state == RecentThumbnailState::unavailable;
      },
      8000));
  return cache.state(path);
}

bool wait_until_idle(RecentThumbnailCache& cache) {
  return patchy::test::ui::process_events_until([&] { return !cache.busy(); }, 8000);
}

struct PanelParts {
  StartPanel* panel{nullptr};
  RecentFileList* list{nullptr};
  QLineEdit* filter{nullptr};
  QToolButton* list_button{nullptr};
  QToolButton* grid_button{nullptr};
  QTabWidget* tabs{nullptr};
};

PanelParts panel_parts(MainWindow& window) {
  PanelParts parts;
  parts.panel = window.findChild<StartPanel*>(QStringLiteral("startPanel"));
  parts.list = dynamic_cast<RecentFileList*>(window.findChild<QListWidget*>(QStringLiteral("startPanelRecentList")));
  parts.filter = window.findChild<QLineEdit*>(QStringLiteral("startPanelRecentFilterEdit"));
  parts.list_button = window.findChild<QToolButton*>(QStringLiteral("startPanelRecentListViewButton"));
  parts.grid_button = window.findChild<QToolButton*>(QStringLiteral("startPanelRecentGridViewButton"));
  parts.tabs = window.findChild<QTabWidget*>(QStringLiteral("documentTabs"));
  CHECK(parts.panel != nullptr && parts.list != nullptr && parts.filter != nullptr);
  CHECK(parts.list_button != nullptr && parts.grid_button != nullptr && parts.tabs != nullptr);
  return parts;
}

void click(QWidget& widget, QPoint position) {
  using patchy::test::ui::send_mouse;
  send_mouse(widget, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(widget, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
  QApplication::processEvents();
}

bool all_thumbnails_settled(PanelParts& parts) {
  return patchy::test::ui::process_events_until(
      [&] {
        // A cell asks for its picture when it is painted; keep the paints coming.
        parts.list->viewport()->repaint();
        for (int row = 0; row < parts.list->count(); ++row) {
          const auto path = parts.list->item(row)->data(kRecentPathRole).toString();
          const auto state = parts.panel->recent_thumbnails().state(path);
          if (!path.isEmpty() && parts.list->viewport()->rect().intersects(parts.list->visualItemRect(parts.list->item(row))) &&
              state != RecentThumbnailState::ready && state != RecentThumbnailState::unavailable) {
            return false;
          }
        }
        return true;
      },
      8000);
}

// --- Header layout (part A of GitHub issue 750) -------------------------------------------

void ui_start_panel_tagline_takes_one_line_and_wraps_when_narrow() {
  using namespace patchy::test::ui;
  MainWindow window;
  show_window_empty(window);
  window.resize(1280, 860);
  QApplication::processEvents();

  auto* panel = window.findChild<StartPanel*>(QStringLiteral("startPanel"));
  auto* column = window.findChild<QWidget*>(QStringLiteral("startPanelColumn"));
  auto* title = window.findChild<QLabel*>(QStringLiteral("startPanelTitle"));
  auto* tagline = window.findChild<QLabel*>(QStringLiteral("startPanelTagline"));
  auto* credit = window.findChild<QLabel*>(QStringLiteral("startPanelCredit"));
  auto* version = window.findChild<QLabel*>(QStringLiteral("startPanelVersion"));
  auto* new_button = window.findChild<QWidget*>(QStringLiteral("startPanelNewButton"));
  CHECK(panel != nullptr && column != nullptr && title != nullptr && tagline != nullptr);
  CHECK(credit != nullptr && version != nullptr && new_button != nullptr);
  const auto links = panel->findChildren<QLabel*>(QStringLiteral("startPanelHome"));
  CHECK(links.size() == 2);
  // The drop hint is gone (dropping still works: ui_drop_* cover it).
  CHECK(window.findChild<QLabel*>(QStringLiteral("startPanelHint")) == nullptr);

  const auto top_in_panel = [panel](const QWidget* widget) { return widget->mapTo(panel, QPoint(0, 0)).y(); };
  const auto bottom_in_panel = [panel](const QWidget* widget) {
    return widget->mapTo(panel, QPoint(0, widget->height())).y();
  };

  // One line each, in a window with room: the title and the tagline.
  const int tagline_line = QFontMetrics(tagline->font()).horizontalAdvance(tagline->text());
  const int title_line = QFontMetrics(title->font()).horizontalAdvance(title->text());
  std::cout << "[i750] wide window: column " << column->width() << " px, tagline text " << tagline_line
            << " px in a label " << tagline->width() << " x " << tagline->height() << " px, title text " << title_line
            << " px in a label " << title->width() << " x " << title->height() << " px\n";
  CHECK(tagline->width() >= tagline_line);
  CHECK(tagline->height() < 2 * QFontMetrics(tagline->font()).lineSpacing());
  CHECK(title->width() >= title_line);
  CHECK(title->height() < 2 * QFontMetrics(title->font()).lineSpacing());

  // Order down the header: title, tagline, credit, links; all of it above the buttons. The
  // version stays in the footer, under everything else.
  CHECK(bottom_in_panel(title) <= top_in_panel(tagline));
  CHECK(bottom_in_panel(tagline) <= top_in_panel(credit));
  for (const auto* link : links) {
    CHECK(bottom_in_panel(credit) <= top_in_panel(link));
    CHECK(bottom_in_panel(link) <= top_in_panel(new_button));
    CHECK(column->isAncestorOf(link));
  }
  CHECK(column->isAncestorOf(credit));
  CHECK(!column->isAncestorOf(version));
  CHECK(top_in_panel(version) >= bottom_in_panel(new_button));
  // The two links share a line while there is room.
  CHECK(top_in_panel(links[0]) == top_in_panel(links[1]));
  CHECK(clipped_labels(*panel).isEmpty());
  save_widget_artifact("ui_start_panel_header_wide", window);

  // A small window: the header narrows with it. The tagline wraps instead of being cut, the
  // links fold onto two lines, and nothing reaches past the panel.
  window.resize(820, 700);
  QApplication::processEvents();
  QApplication::processEvents();
  std::cout << "[i750] small window: panel " << panel->width() << " px, column " << column->width()
            << " px, tagline label " << tagline->width() << " x " << tagline->height() << " px\n";
  CHECK(column->width() <= panel->width());
  CHECK(tagline->width() < tagline_line);
  CHECK(tagline->height() >= 2 * QFontMetrics(tagline->font()).lineSpacing() - 2);
  CHECK(tagline->height() >= tagline->heightForWidth(tagline->width()));
  for (const auto* label : {static_cast<const QWidget*>(title), static_cast<const QWidget*>(tagline),
                            static_cast<const QWidget*>(credit), static_cast<const QWidget*>(links[0]),
                            static_cast<const QWidget*>(links[1])}) {
    const int left = label->mapTo(panel, QPoint(0, 0)).x();
    CHECK(left >= 0);
    CHECK(left + label->width() <= panel->width());
  }
  CHECK(top_in_panel(links[1]) >= bottom_in_panel(links[0]));
  CHECK(clipped_labels(*panel).isEmpty());
  save_widget_artifact("ui_start_panel_header_narrow", window);
}

// --- The thumbnail cache --------------------------------------------------------------------

void ui_recent_thumbnails_cache_reads_and_writes_unicode_paths() {
  using namespace patchy::test::ui;
  // Unicode in the files' folder, in their names and in the cache folder itself.
  const auto root =
      QFileInfo(patchy::ui::to_qstring(patchy::test::unicode_artifact_dir(u8"recent-thumbnails"))).absoluteFilePath();
  const auto cache_dir = root + QStringLiteral("/") + q(patchy::test::kUnicodeDirName) + QStringLiteral(" cache");
  EnvironmentGuard cache_guard("PATCHY_RECENT_THUMBNAILS_DIR", cache_dir);
  CHECK(patchy::ui::recent_thumbnails_directory() == QDir(cache_dir).absolutePath());

  const std::vector<QColor> colors{QColor(200, 40, 40), QColor(40, 170, 60), QColor(50, 80, 210), QColor(210, 180, 30),
                                   QColor(150, 60, 190)};
  QStringList paths;
  for (std::size_t index = 0; index < patchy::test::kUnicodePathStems.size(); ++index) {
    paths << write_png(root + QLatin1Char('/') + q(patchy::test::kUnicodePathStems[index]) + QStringLiteral(".png"),
                       colors[index], QSize(600, 300));
  }
  const auto psd = write_psd_with_thumbnail(
      root + QLatin1Char('/') + q(patchy::test::kUnicodeCombinedStem) + QStringLiteral(".psd"), QColor(20, 150, 170));

  {
    RecentThumbnailCache cache;
    cache.set_display_box(QSize(120, 84));
    for (qsizetype index = 0; index < paths.size(); ++index) {
      CHECK(settled_state(cache, paths[index]) == RecentThumbnailState::ready);
      const auto* image = cache.image(paths[index]);
      CHECK(image != nullptr);
      // Fitted to the display box, aspect kept (600 x 300 is 2:1).
      CHECK(image->width() <= 120 && image->height() <= 84);
      CHECK(std::abs(image->width() - 2 * image->height()) <= 2);
      CHECK(near_color(center_color(*image), colors[static_cast<std::size_t>(index)]));

      // One cache file per path, stored at the cache's own size whatever the display box is.
      const auto cache_file = patchy::ui::recent_thumbnail_cache_file(paths[index]);
      CHECK(QFileInfo(cache_file).absolutePath() == QDir(cache_dir).absolutePath());
      QImage stored;
      CHECK(stored.load(cache_file, "PNG"));
      CHECK(stored.width() == patchy::ui::kRecentThumbnailStoredSide);
      CHECK(stored.height() == patchy::ui::kRecentThumbnailStoredSide / 2);
      CHECK(near_color(center_color(stored), colors[static_cast<std::size_t>(index)]));
    }
    // A PSD's picture is its embedded thumbnail, read from the front of the file.
    CHECK(settled_state(cache, psd) == RecentThumbnailState::ready);
    CHECK(near_color(center_color(*cache.image(psd)), QColor(20, 150, 170), 20));
    CHECK(wait_until_idle(cache));
  }
  // Nothing but the cache's own files in its folder, no temporary left behind.
  CHECK(QDir(cache_dir).entryList(QDir::Files).size() == paths.size() + 1);

  // A second cache (the next start of Patchy) shows the stored picture without decoding the
  // file again: the cache file is repainted here, and that is what comes back.
  {
    const auto cache_file = patchy::ui::recent_thumbnail_cache_file(paths[0]);
    QImage stored;
    CHECK(stored.load(cache_file, "PNG"));
    QImage repainted = stored.copy();
    repainted.fill(QColor(255, 255, 255));
    for (const auto& key : stored.textKeys()) {
      repainted.setText(key, stored.text(key));
    }
    CHECK(repainted.save(cache_file, "PNG"));
    RecentThumbnailCache cache;
    CHECK(settled_state(cache, paths[0]) == RecentThumbnailState::ready);
    CHECK(near_color(center_color(*cache.image(paths[0])), QColor(255, 255, 255)));
  }

  // The file changed (another size on disk): its stored picture no longer belongs to it.
  write_png(paths[0], QColor(10, 10, 10), QSize(610, 305));
  {
    RecentThumbnailCache cache;
    CHECK(settled_state(cache, paths[0]) == RecentThumbnailState::ready);
    CHECK(near_color(center_color(*cache.image(paths[0])), QColor(10, 10, 10)));

    // The picture of an open document replaces what the file's own decode gave.
    QImage composite(900, 450, QImage::Format_RGBA8888);
    composite.fill(QColor(250, 120, 0));
    cache.store(paths[1], patchy::ui::recent_thumbnail_source(composite));
    CHECK(process_events_until([&] { return cache.state(paths[1]) == RecentThumbnailState::ready; }, 8000));
    CHECK(near_color(center_color(*cache.image(paths[1])), QColor(250, 120, 0)));
    QImage stored;
    CHECK(stored.load(patchy::ui::recent_thumbnail_cache_file(paths[1]), "PNG"));
    CHECK(near_color(center_color(stored), QColor(250, 120, 0)));
    CHECK(stored.width() == patchy::ui::kRecentThumbnailStoredSide);

    // Removing an entry deletes its file and nothing else.
    cache.forget({paths[1]});
    CHECK(wait_until_idle(cache));
    CHECK(!QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(paths[1])));
    CHECK(cache.state(paths[1]) == RecentThumbnailState::unknown);
    CHECK(QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(paths[2])));
    CHECK(QFileInfo::exists(paths[1]));  // the user's file itself is never touched

    // The sweep keeps the listed entries and leaves files that are not the cache's alone.
    const auto foreign = cache_dir + QStringLiteral("/holiday.png");
    write_png(foreign, QColor(1, 2, 3));
    cache.retain_only({paths[2], psd});
    CHECK(wait_until_idle(cache));
    CHECK(QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(paths[2])));
    CHECK(QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(psd)));
    CHECK(!QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(paths[0])));
    CHECK(!QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(paths[3])));
    CHECK(QFileInfo::exists(foreign));
    CHECK(QDir(cache_dir).entryList(QDir::Files).size() == 3);
  }
}

void ui_recent_thumbnails_skip_network_missing_and_unreadable_files() {
  using namespace patchy::test::ui;
  const auto cache_dir = scratch_file(QStringLiteral("skip-cache"));
  if (QFileInfo::exists(cache_dir)) {
    CHECK(remove_test_scratch_dir(cache_dir));
  }
  EnvironmentGuard cache_guard("PATCHY_RECENT_THUMBNAILS_DIR", cache_dir);

  // Network forms are told apart from their text alone, before anything could stat them. The
  // host is under the reserved .invalid domain, so even a mistake could reach no machine.
  const QStringList network_paths{
      QStringLiteral("\\\\patchy-test-host.invalid\\share\\file.psd"),
      QStringLiteral("//patchy-test-host.invalid/share/photo.png"),
      QStringLiteral("\\\\?\\UNC\\patchy-test-host.invalid\\share\\file.psd"),
      QStringLiteral("\\\\.\\pipe\\patchy-test"),
  };
  for (const auto& path : network_paths) {
    CHECK(patchy::ui::path_is_on_network_location(path, {}));
  }

  const auto missing = scratch_file(QStringLiteral("never-written.png"));
  QFile::remove(missing);
  const auto garbage = scratch_file(QStringLiteral("garbage.png"));
  {
    QFile file(garbage);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(4096, 'x'));
  }
  // A kind with no cheap preview: only Patchy opening or saving it gives it a picture.
  const auto unsupported = scratch_file(QStringLiteral("layers.xcf"));
  {
    QFile file(unsupported);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("gimp xcf v011\0", 14);
    file.write(QByteArray(512, '\0'));
  }
  // A PNG whose header claims more pixels than the cap: never decoded.
  const auto huge = scratch_file(QStringLiteral("huge-header.png"));
  {
    QImage small(8, 8, QImage::Format_RGB32);
    small.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    CHECK(buffer.open(QIODevice::WriteOnly));
    CHECK(small.save(&buffer, "PNG"));
    // IHDR's width and height follow the 8-byte signature, the chunk length and "IHDR"; the
    // chunk's CRC covers "IHDR" and its 13 data bytes, so the header stays a valid one.
    const QByteArray big = QByteArray::fromHex("0000753000007530");  // 30000 x 30000
    bytes.replace(16, 8, big);
    quint32 crc = 0xFFFFFFFFU;
    for (const char byte : bytes.mid(12, 17)) {
      crc ^= static_cast<unsigned char>(byte);
      for (int bit = 0; bit < 8; ++bit) {
        crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
      }
    }
    QByteArray crc_bytes;
    put_u32(crc_bytes, ~crc);
    bytes.replace(29, 4, crc_bytes);
    QFile file(huge);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(bytes);
  }
  const auto folder = scratch_file(QStringLiteral("a-folder.png"));
  CHECK(QDir().mkpath(folder));

  RecentThumbnailCache cache;
  QStringList all = network_paths;
  all << missing << garbage << unsupported << huge << folder;
  for (const auto& path : all) {
    CHECK(settled_state(cache, path) == RecentThumbnailState::unavailable);
    CHECK(cache.image(path) == nullptr);
  }
  CHECK(wait_until_idle(cache));
  // No picture was made, so nothing was written: the folder was not even created.
  CHECK(!QFileInfo::exists(cache_dir) || QDir(cache_dir).entryList(QDir::Files).isEmpty());
  // The picture of an open document is refused for a network path too.
  QImage composite(64, 64, QImage::Format_RGBA8888);
  composite.fill(Qt::blue);
  cache.store(network_paths[0], composite);
  CHECK(process_events_until([&] { return cache.state(network_paths[0]) == RecentThumbnailState::unavailable; }, 8000));
  CHECK(wait_until_idle(cache));
  CHECK(!QFileInfo::exists(cache_dir) || QDir(cache_dir).entryList(QDir::Files).isEmpty());
  // An empty path (the no-match placeholder row) is never a request.
  cache.request(QString());
  CHECK(cache.state(QString()) == RecentThumbnailState::unknown);
  CHECK(!cache.busy());
}

// --- The grid view on the Home Screen ---------------------------------------------------------

void ui_start_panel_recent_grid_view_toggles_and_persists() {
  using namespace patchy::test::ui;
  GridTestSettings settings;
  settings.set_grid(false);
  QStringList recent;
  recent << write_png(scratch_file(QStringLiteral("toggle-red.png")), QColor(200, 50, 50))
         << write_png(scratch_file(QStringLiteral("toggle-green.png")), QColor(50, 170, 70))
         << write_png(scratch_file(QStringLiteral("toggle-blue.png")), QColor(60, 90, 210));
  settings.set_recent(recent);

  {
    MainWindow window;
    show_window_empty(window);
    window.resize(1280, 860);
    QApplication::processEvents();
    auto parts = panel_parts(window);
    CHECK(process_events_until([&] { return parts.list->count() == 3; }));

    // The list is the default: nothing changes for a user who never presses Grid, and no
    // thumbnail is made or stored.
    CHECK(parts.list_button->isVisible() && parts.grid_button->isVisible());
    CHECK(parts.list_button->isCheckable() && parts.grid_button->isCheckable());
    CHECK(parts.list_button->text() == QStringLiteral("List"));
    CHECK(parts.grid_button->text() == QStringLiteral("Grid"));
    CHECK(parts.list_button->isChecked() && !parts.grid_button->isChecked());
    CHECK(!parts.panel->recent_grid_view());
    CHECK(!parts.list->grid_view() && !parts.list->shows_cells());
    const int list_row_height = parts.list->visualItemRect(parts.list->item(0)).height();
    CHECK(list_row_height == patchy::ui::recent_row_height());
    parts.list->viewport()->repaint();
    CHECK(parts.panel->recent_thumbnails().state(recent[0]) == RecentThumbnailState::unknown);
    CHECK(!parts.panel->recent_thumbnails().busy());
    CHECK(!QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(recent[0])));

    // Grid: the same three entries as cells, side by side, each with its picture.
    click(*parts.grid_button, parts.grid_button->rect().center());
    CHECK(parts.panel->recent_grid_view());
    CHECK(parts.grid_button->isChecked() && !parts.list_button->isChecked());
    CHECK(patchy::ui::stored_start_panel_recent_grid_view());
    CHECK(parts.list->grid_view() && parts.list->shows_cells());
    CHECK(parts.list->count() == 3);
    QApplication::processEvents();
    const auto first = parts.list->visualItemRect(parts.list->item(0));
    const auto second = parts.list->visualItemRect(parts.list->item(1));
    CHECK(first.height() == patchy::ui::recent_grid_cell_height());
    CHECK(second.top() == first.top());
    CHECK(second.left() >= first.right());
    CHECK(first.width() >= patchy::ui::kRecentGridMinCellWidth);
    for (int row = 0; row < 3; ++row) {
      CHECK(parts.list->item(row)->data(kRecentPathRole).toString() == recent[row]);
      CHECK(parts.list->item(row)->toolTip() == QDir::toNativeSeparators(recent[row]));
    }
    CHECK(all_thumbnails_settled(parts));
    for (const auto& path : recent) {
      CHECK(parts.panel->recent_thumbnails().state(path) == RecentThumbnailState::ready);
      CHECK(QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(path)));
    }
    // The cached picture is what the cell shows: the middle of each picture box has its color.
    parts.list->viewport()->repaint();
    const QImage shot = parts.list->viewport()->grab().toImage();
    const std::vector<QColor> colors{QColor(200, 50, 50), QColor(50, 170, 70), QColor(60, 90, 210)};
    for (int row = 0; row < 3; ++row) {
      const auto picture = RecentFileList::grid_picture_rect(parts.list->visualItemRect(parts.list->item(row)));
      CHECK(near_color(shot.pixelColor(picture.center()), colors[static_cast<std::size_t>(row)]));
    }
    save_widget_artifact("ui_start_panel_recent_grid", window);
  }

  // The choice survives a restart: a new window starts in the grid.
  {
    MainWindow window;
    show_window_empty(window);
    auto parts = panel_parts(window);
    CHECK(process_events_until([&] { return parts.list->count() == 3; }));
    CHECK(parts.panel->recent_grid_view());
    CHECK(parts.grid_button->isChecked());
    CHECK(parts.list->shows_cells());

    // Back to the list: stored as no key, rows again.
    click(*parts.list_button, parts.list_button->rect().center());
    CHECK(!parts.panel->recent_grid_view());
    CHECK(!patchy::ui::stored_start_panel_recent_grid_view());
    CHECK(!patchy::ui::app_settings().contains(QStringLiteral("startPanel/recentFilesGridView")));
    CHECK(!parts.list->shows_cells());
    QApplication::processEvents();
    CHECK(parts.list->visualItemRect(parts.list->item(0)).height() == patchy::ui::recent_row_height());
    CHECK(parts.list->visualItemRect(parts.list->item(1)).top() > parts.list->visualItemRect(parts.list->item(0)).top());
    // A click on the button that is already checked changes nothing.
    click(*parts.list_button, parts.list_button->rect().center());
    CHECK(parts.list_button->isChecked() && !parts.grid_button->isChecked());
  }
  {
    MainWindow window;
    show_window_empty(window);
    auto parts = panel_parts(window);
    CHECK(!parts.panel->recent_grid_view());
    CHECK(parts.list_button->isChecked());
  }
}

void ui_start_panel_recent_grid_filter_menu_and_remove() {
  using namespace patchy::test::ui;
  GridTestSettings settings;
  settings.set_grid(true);
  QStringList recent;
  recent << write_png(scratch_file(QStringLiteral("grid-alpha.png")), QColor(200, 50, 50))
         << write_png(scratch_file(QStringLiteral("grid-beta.png")), QColor(50, 170, 70))
         << write_png(scratch_file(QStringLiteral("grid-gamma.png")), QColor(60, 90, 210))
         << write_png(scratch_file(QStringLiteral("grid-delta.png")), QColor(210, 180, 40));
  // One entry with no picture: a file that is not an image at all.
  const auto broken = scratch_file(QStringLiteral("grid-broken.png"));
  {
    QFile file(broken);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(2048, 'z'));
  }
  recent << broken;
  settings.set_recent(recent);
  const auto stored_recent_files = [] {
    auto store = patchy::ui::app_settings();
    store.sync();
    return store.value(QStringLiteral("recentFiles")).toStringList();
  };

  MainWindow window;
  show_window_empty(window);
  window.resize(1280, 860);
  QApplication::processEvents();
  auto parts = panel_parts(window);
  CHECK(process_events_until([&] { return parts.list->count() == 5; }));
  CHECK(parts.list->shows_cells());
  CHECK(all_thumbnails_settled(parts));
  auto& thumbnails = parts.panel->recent_thumbnails();
  for (int index = 0; index < 4; ++index) {
    CHECK(thumbnails.state(recent[index]) == RecentThumbnailState::ready);
    CHECK(QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(recent[index])));
  }
  // The file with no picture is a placeholder cell, quietly: no dialog, no cache file.
  CHECK(thumbnails.state(broken) == RecentThumbnailState::unavailable);
  CHECK(!QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(broken)));
  CHECK(QApplication::activeModalWidget() == nullptr);
  save_widget_artifact("ui_start_panel_recent_grid_placeholder", window);

  // The name filter narrows the cells the way it narrows the rows.
  parts.filter->setText(QStringLiteral("grid-beta"));
  QApplication::processEvents();
  CHECK(parts.list->count() == 1);
  CHECK(parts.list->shows_cells());
  CHECK(parts.list->item(0)->data(kRecentPathRole).toString() == recent[1]);
  // The box is one row of cells tall now, and the column closes up around it: the height the
  // second row gave back does not stay inside the column as a gap under the header.
  auto* column = window.findChild<QWidget*>(QStringLiteral("startPanelColumn"));
  auto* artwork = window.findChild<QWidget*>(QStringLiteral("startPanelArtwork"));
  auto* new_button = window.findChild<QWidget*>(QStringLiteral("startPanelNewButton"));
  CHECK(column != nullptr && artwork != nullptr && new_button != nullptr);
  // (The column and the panel around it each lay out on their own pass of the event loop.)
  CHECK(process_events_until([&] { return column->height() <= column->sizeHint().height(); }));
  CHECK(parts.list->height() == patchy::ui::recent_grid_cell_height() + patchy::ui::kRecentListChrome);
  const int header_gap =
      new_button->mapTo(column, QPoint(0, 0)).y() - artwork->mapTo(column, QPoint(0, artwork->height())).y();
  CHECK(header_gap < 60);
  save_widget_artifact("ui_start_panel_recent_grid_filtered", window);

  // No match: the placeholder row, full width, as in list view.
  parts.filter->setText(QStringLiteral("zzz-no-such-file"));
  QApplication::processEvents();
  CHECK(parts.list->count() == 1);
  CHECK(parts.list->item(0)->data(kRecentPathRole).toString().isEmpty());
  CHECK(parts.list->item(0)->text() == QStringLiteral("No matching recent files"));
  CHECK(parts.list->grid_view() && !parts.list->shows_cells());
  CHECK(parts.list->visualItemRect(parts.list->item(0)).width() > parts.list->viewport()->width() / 2);
  click(*parts.list->viewport(), parts.list->visualItemRect(parts.list->item(0)).center());
  CHECK(parts.tabs->count() == 0);
  parts.filter->clear();
  QApplication::processEvents();
  CHECK(parts.list->count() == 5);
  CHECK(parts.list->shows_cells());

  // The remove (x) button sits on the hovered cell's picture and removes that entry only,
  // taking its cached thumbnail with it.
  const auto button_center = [&parts](int row) {
    return RecentFileList::grid_remove_button_rect(parts.list->visualItemRect(parts.list->item(row))).center();
  };
  CHECK(parts.list->has_remove_button());
  send_mouse(*parts.list->viewport(), QEvent::MouseMove, button_center(2), Qt::NoButton, Qt::NoButton);
  QApplication::processEvents();
  CHECK(parts.list->remove_button_hovered(2));
  CHECK(RecentFileList::grid_picture_rect(parts.list->visualItemRect(parts.list->item(2))).contains(button_center(2)));
  save_widget_artifact("ui_start_panel_recent_grid_remove_hover", window);
  click(*parts.list->viewport(), button_center(2));
  QApplication::processEvents();
  CHECK(parts.tabs->count() == 0);
  CHECK(process_events_until([&] { return parts.list->count() == 4; }));
  CHECK(!stored_recent_files().contains(recent[2]));
  CHECK(process_events_until([&] { return !QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(recent[2])); }, 8000));
  CHECK(QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(recent[0])));
  CHECK(QFileInfo::exists(recent[2]));  // only the entry and its thumbnail go, never the file

  // The context menu on a cell is the list's: Remove from Recent Files does the same.
  bool saw_remove = false;
  QTimer::singleShot(0, guarded([&] {
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || menu->objectName() != QStringLiteral("recentFileContextMenu")) {
        continue;
      }
      auto* remove_action = find_menu_action_by_text(*menu, QStringLiteral("Remove from Recent Files"));
      CHECK(remove_action != nullptr);
      if (remove_action != nullptr) {
        remove_action->trigger();
        saw_remove = true;
      }
      menu->close();
      return;
    }
  }));
  const auto context_point = parts.list->visualItemRect(parts.list->item(0)).center();
  QContextMenuEvent context_event(QContextMenuEvent::Mouse, context_point,
                                  parts.list->viewport()->mapToGlobal(context_point));
  QApplication::sendEvent(parts.list->viewport(), &context_event);
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(saw_remove);
  CHECK(process_events_until([&] { return parts.list->count() == 3; }));
  CHECK(!stored_recent_files().contains(recent[0]));
  CHECK(process_events_until([&] { return !QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(recent[0])); }, 8000));
  CHECK(QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(recent[1])));

  // File > Open Recent File > Clear Recent Files empties the cache folder of them all.
  auto* clear_action = window.findChild<QAction*>(QStringLiteral("fileClearRecentAction"));
  CHECK(clear_action != nullptr);
  clear_action->trigger();
  QApplication::processEvents();
  CHECK(stored_recent_files().isEmpty());
  CHECK(!parts.list->isVisible());
  CHECK(!parts.grid_button->isVisible() && !parts.list_button->isVisible());
  CHECK(process_events_until(
      [&] {
        return !QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(recent[1])) &&
               !QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(recent[3]));
      },
      8000));
}

void ui_start_panel_recent_grid_click_and_enter_open_files() {
  using namespace patchy::test::ui;
  GridTestSettings settings;
  settings.set_grid(true);
  QStringList recent;
  recent << write_png(scratch_file(QStringLiteral("open-one.png")), QColor(200, 50, 50), QSize(48, 32))
         << write_png(scratch_file(QStringLiteral("open-two.png")), QColor(50, 170, 70), QSize(40, 30))
         << write_png(scratch_file(QStringLiteral("open-three.png")), QColor(60, 90, 210), QSize(36, 24));
  settings.set_recent(recent);

  MainWindow window;
  show_window_empty(window);
  window.resize(1280, 860);
  QApplication::processEvents();
  auto parts = panel_parts(window);
  CHECK(process_events_until([&] { return parts.list->count() == 3; }));
  CHECK(parts.list->shows_cells());

  // A click on a cell opens its file.
  click(*parts.list->viewport(), parts.list->visualItemRect(parts.list->item(1)).center());
  CHECK(process_events_until([&] { return parts.tabs->count() == 1; }));
  CHECK(MainWindowTestAccess::active_session_path(window) == recent[1]);
  CHECK(!parts.panel->isVisible());
  CHECK(MainWindowTestAccess::close_document_tab(window, 0));
  CHECK(process_events_until([&] { return parts.panel->isVisible(); }));
  // The file just opened is the first cell now, as it is the first row in list view.
  CHECK(parts.list->shows_cells());
  CHECK(parts.list->item(0)->data(kRecentPathRole).toString() == recent[1]);

  // Enter in the filter opens the first match.
  parts.filter->setFocus();
  parts.filter->setText(QStringLiteral("open-three"));
  QApplication::processEvents();
  CHECK(parts.list->count() == 1);
  QTest::keyClick(parts.filter, Qt::Key_Return);
  CHECK(process_events_until([&] { return parts.tabs->count() == 1; }));
  CHECK(MainWindowTestAccess::active_session_path(window) == recent[2]);
  MainWindowTestAccess::mark_active_session_saved(window);
}

void ui_start_panel_recent_grid_stores_the_opened_documents_picture() {
  using namespace patchy::test::ui;
  GridTestSettings settings;
  settings.set_grid(true);
  settings.set_recent({});
  // An OpenRaster file: no cheap preview a worker could read, so its only picture is the one
  // the window stores from the document it opened.
  const auto source = patchy::ui::to_qstring(patchy::test::committed_format_fixture_path("ora", "python-layers.ora"));
  CHECK(QFileInfo::exists(source));
  const auto path = scratch_file(QStringLiteral("stored-picture.ora"));
  QFile::remove(path);
  CHECK(QFile::copy(source, path));
  QFile::remove(patchy::ui::recent_thumbnail_cache_file(path));

  MainWindow window;
  show_window_empty(window);
  window.resize(1280, 860);
  QApplication::processEvents();
  auto parts = panel_parts(window);
  {
    // Before Patchy has opened it, the file has no picture.
    RecentThumbnailCache probe;
    CHECK(settled_state(probe, path) == RecentThumbnailState::unavailable);
  }

  MainWindowTestAccess::open_document_path(window, path);
  CHECK(parts.tabs->count() == 1);
  // The first render lands a moment after the open; the window stores the picture then.
  const auto cache_file = patchy::ui::recent_thumbnail_cache_file(path);
  CHECK(process_events_until([&] { return QFileInfo::exists(cache_file); }, 10000));
  QImage stored;
  CHECK(process_events_until([&] { return stored.load(cache_file, "PNG"); }, 4000));
  CHECK(stored.width() <= patchy::ui::kRecentThumbnailStoredSide);
  CHECK(stored.height() <= patchy::ui::kRecentThumbnailStoredSide);
  CHECK(std::max(stored.width(), stored.height()) > 16);

  CHECK(MainWindowTestAccess::close_document_tab(window, 0));
  CHECK(process_events_until([&] { return parts.panel->isVisible() && parts.list->count() == 1; }));
  CHECK(parts.list->shows_cells());
  CHECK(all_thumbnails_settled(parts));
  CHECK(parts.panel->recent_thumbnails().state(path) == RecentThumbnailState::ready);
  save_widget_artifact("ui_start_panel_recent_grid_stored_picture", window);
}

}  // namespace

std::vector<patchy::test::TestCase> start_panel_grid_tests() {
  return {
      {"ui_start_panel_tagline_takes_one_line_and_wraps_when_narrow",
       ui_start_panel_tagline_takes_one_line_and_wraps_when_narrow},
      {"ui_recent_thumbnails_cache_reads_and_writes_unicode_paths",
       ui_recent_thumbnails_cache_reads_and_writes_unicode_paths},
      {"ui_recent_thumbnails_skip_network_missing_and_unreadable_files",
       ui_recent_thumbnails_skip_network_missing_and_unreadable_files},
      {"ui_start_panel_recent_grid_view_toggles_and_persists", ui_start_panel_recent_grid_view_toggles_and_persists},
      {"ui_start_panel_recent_grid_filter_menu_and_remove", ui_start_panel_recent_grid_filter_menu_and_remove},
      {"ui_start_panel_recent_grid_click_and_enter_open_files", ui_start_panel_recent_grid_click_and_enter_open_files},
      {"ui_start_panel_recent_grid_stores_the_opened_documents_picture",
       ui_start_panel_recent_grid_stores_the_opened_documents_picture},
  };
}
