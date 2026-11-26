// The Home Screen's header layout and its Recent Files grid view with thumbnails (GitHub
// issue 750; docs/ui-conventions-menus.md, "Recent file thumbnails").

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#include "core/pdf_encrypted_fixture.hpp"
#include "formats/format_registry.hpp"
#include "formats/metadata/iptc_iim.hpp"
#include "formats/ora_document_io.hpp"
#include "formats/xcf_document_io.hpp"
#include "local_psd_fixtures.hpp"
#include "psd/psd_document_io.hpp"
#include "psd/psd_tiff.hpp"
#include "patchy_version.hpp"
#include "ui/app_settings.hpp"
#include "ui/build_info.hpp"
#include "ui/image_document_io.hpp"
#include "ui/main_window.hpp"
#include "ui/network_mounts.hpp"
#include "ui/qt_paths.hpp"
#include "ui/recent_file_list.hpp"
#include "ui/recent_thumbnail_sources.hpp"
#include "ui/recent_thumbnails.hpp"
#include "ui/start_panel.hpp"
#include "ui/ui_text_size.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMarginsF>
#include <QMenu>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QScrollBar>
#include <QTabWidget>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

// --- Files and checks for the pictures of the kinds a reader opens ---------------------------

QString marker_file(const QString& path) {
  auto file = patchy::ui::recent_thumbnail_cache_file(path);
  CHECK(file.endsWith(QStringLiteral(".png")));
  file.chop(4);
  return file + QStringLiteral(".none");
}

QByteArray read_bytes(const QString& path) {
  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

QString write_bytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  CHECK(file.write(bytes) == bytes.size());
  return path;
}

QString write_bytes(const QString& path, const std::vector<std::uint8_t>& bytes) {
  return write_bytes(path, QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())));
}

std::vector<std::uint8_t> to_vector(const QByteArray& bytes) {
  return {reinterpret_cast<const std::uint8_t*>(bytes.constData()),
          reinterpret_cast<const std::uint8_t*>(bytes.constData()) + bytes.size()};
}

QByteArray png_bytes(QColor color, QSize size) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(color);
  QByteArray bytes;
  QBuffer buffer(&bytes);
  CHECK(buffer.open(QIODevice::WriteOnly));
  CHECK(image.save(&buffer, "PNG"));
  return bytes;
}

// A committed fixture copied under `name` into the test's own folder.
QString copy_fixture(std::string_view format_dir, std::string_view fixture, const QString& target) {
  const auto source = patchy::ui::to_qstring(patchy::test::committed_format_fixture_path(format_dir, fixture));
  CHECK(QFileInfo::exists(source));
  QFile::remove(target);
  CHECK(QFile::copy(source, target));
  return target;
}

// The canvas of a file as Patchy's own reader opens it.
QSize reader_canvas(const QString& path, const char* extension) {
  const auto* handler = patchy::builtin_format_registry().find_by_extension(extension);
  CHECK(handler != nullptr);
  const auto bytes = to_vector(read_bytes(path));
  const auto document = handler->read(bytes).document;
  return {document.width(), document.height()};
}

bool has_visible_pixel(const QImage& image) {
  const auto pixels = image.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < pixels.height(); ++y) {
    const auto* row = pixels.constScanLine(y);
    for (int x = 0; x < pixels.width(); ++x) {
      if (row[4 * x + 3] != 0) {
        return true;
      }
    }
  }
  return false;
}

bool same_shape(QSize picture, QSize canvas) {
  if (picture.isEmpty() || canvas.isEmpty()) {
    return false;
  }
  const double difference = std::abs(static_cast<double>(picture.width()) * canvas.height() -
                                     static_cast<double>(picture.height()) * canvas.width());
  // A stored side is rounded to a whole pixel: one pixel of the short side at most.
  return difference <= static_cast<double>(std::max(canvas.width(), canvas.height())) * 1.5;
}

// The stored picture of `path`, checked: made, at most the stored size, shaped like `canvas`
// (an empty `canvas` skips that), with something in it.
QImage stored_picture(RecentThumbnailCache& cache, const QString& path, QSize canvas) {
  if (settled_state(cache, path) != RecentThumbnailState::ready) {
    std::cerr << "no picture for " << path.toStdString() << std::endl;
    CHECK(false);
  }
  CHECK(cache.image(path) != nullptr);
  CHECK(wait_until_idle(cache));
  QImage stored;
  CHECK(stored.load(patchy::ui::recent_thumbnail_cache_file(path), "PNG"));
  CHECK(stored.width() <= patchy::ui::kRecentThumbnailStoredSide);
  CHECK(stored.height() <= patchy::ui::kRecentThumbnailStoredSide);
  if (!canvas.isEmpty() && !same_shape(stored.size(), canvas)) {
    std::cerr << "picture of " << path.toStdString() << " is " << stored.width() << " x " << stored.height()
              << " for a canvas of " << canvas.width() << " x " << canvas.height() << std::endl;
    CHECK(false);
  }
  CHECK(has_visible_pixel(stored));
  CHECK(!QFileInfo::exists(marker_file(path)));
  return stored;
}

QString fresh_cache_dir(const QString& name) {
  const auto cache_dir = scratch_file(name);
  if (QFileInfo::exists(cache_dir)) {
    CHECK(patchy::test::ui::remove_test_scratch_dir(cache_dir));
  }
  return cache_dir;
}

// A zip holding `entries` after the stored "mimetype" entry the OpenRaster and Krita sniffers
// look for.
QString write_container(const QString& path, const char* mime_type,
                        std::vector<patchy::ora::detail::ZipEntry> entries) {
  std::vector<patchy::ora::detail::ZipEntry> all;
  all.push_back({"mimetype", to_vector(QByteArray(mime_type)), false});
  for (auto& entry : entries) {
    all.push_back(std::move(entry));
  }
  return write_bytes(path, patchy::ora::detail::zip_archive(all));
}

#ifdef PATCHY_HAVE_QT_PDF
// A PDF of 2:1 pages, each filled with one color.
QString write_pdf(const QString& path, const std::vector<QColor>& pages) {
  QFile::remove(path);
  QPdfWriter writer(path);
  writer.setPageSize(QPageSize(QSizeF(400, 200), QPageSize::Point));
  writer.setPageMargins(QMarginsF(0, 0, 0, 0));
  QPainter painter(&writer);
  for (std::size_t index = 0; index < pages.size(); ++index) {
    if (index > 0) {
      CHECK(writer.newPage());
    }
    painter.fillRect(QRect(0, 0, writer.width(), writer.height()), pages[index]);
  }
  painter.end();
  CHECK(QFileInfo(path).size() > 0);
  return path;
}
#endif

// `text` as a one-member gzip stream (an .svgz). The checksum is left zero: nothing here reads it.
QByteArray gzip_bytes(const QByteArray& text) {
  const auto zlib = qCompress(text, 9);  // 4 bytes of length, a 2-byte zlib header, deflate data, 4 bytes of Adler-32
  QByteArray gz = QByteArray::fromHex("1f8b0800000000000003");
  gz.append(zlib.mid(6, zlib.size() - 10));
  gz.append(QByteArray(4, '\0'));
  const auto size = static_cast<quint32>(text.size());
  for (int shift = 0; shift < 32; shift += 8) {
    gz.append(static_cast<char>((size >> shift) & 0xFFU));
  }
  return gz;
}

// --- Header layout (part A of GitHub issue 750) -------------------------------------------

// A rich-text label as it is shown: its plain text, and each link's address with the text it
// covers.
struct RenderedLabel {
  QString plain;
  QMap<QString, QString> links;  // address -> linked text
};

RenderedLabel rendered_label(const QLabel& label) {
  QTextDocument document;
  document.setHtml(label.text());
  RenderedLabel rendered;
  rendered.plain = document.toPlainText();
  for (auto block = document.begin(); block != document.end(); block = block.next()) {
    for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
      const auto format = fragment.fragment().charFormat();
      if (format.isAnchor()) {
        rendered.links[format.anchorHref()] += fragment.fragment().text();
      }
    }
  }
  return rendered;
}

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
  CHECK(links.size() == 1);
  auto* github = links.front();
  CHECK(panel->findChildren<QLabel*>(QStringLiteral("startPanelCredit")).size() == 1);
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

  // Order down the header: title, version, tagline, the credit line; all of it above the
  // buttons.
  CHECK(bottom_in_panel(title) <= top_in_panel(version));
  CHECK(bottom_in_panel(version) <= top_in_panel(tagline));
  CHECK(bottom_in_panel(tagline) <= top_in_panel(credit));
  CHECK(bottom_in_panel(credit) <= top_in_panel(new_button));
  CHECK(bottom_in_panel(github) <= top_in_panel(new_button));
  CHECK(column->isAncestorOf(credit) && column->isAncestorOf(github));

  // The version and build date sit directly under the title (Seth, November 26, 2026): in the
  // header's text block, left edge on the title's, one line, close under the title and well
  // clear of nothing else, smaller than the title and larger than the tagline.
  const auto left_in_panel = [panel](const QWidget* widget) { return widget->mapTo(panel, QPoint(0, 0)).x(); };
  CHECK(column->isAncestorOf(version));
  CHECK(version->parentWidget() == title->parentWidget());
  CHECK(version->text() == QStringLiteral("Version %1 (built %2)")
                               .arg(QStringLiteral(PATCHY_VERSION), patchy::ui::build_timestamp_text()));
  CHECK(left_in_panel(version) == left_in_panel(title));
  CHECK(left_in_panel(version) == left_in_panel(tagline));
  const int version_line = QFontMetrics(version->font()).horizontalAdvance(version->text());
  std::cout << "[i750] version line: text " << version_line << " px in a label " << version->width() << " x "
            << version->height() << " px, " << top_in_panel(version) - bottom_in_panel(title)
            << " px under the title, " << top_in_panel(tagline) - bottom_in_panel(version) << " px over the tagline\n";
  CHECK(version->width() >= version_line);
  CHECK(version->height() < 2 * QFontMetrics(version->font()).lineSpacing());
  CHECK(top_in_panel(version) - bottom_in_panel(title) <= 8);
  CHECK(QFontInfo(version->font()).pixelSize() < QFontInfo(title->font()).pixelSize());
  CHECK(QFontInfo(version->font()).pixelSize() > QFontInfo(tagline->font()).pixelSize());
  CHECK(version->toolTip().isEmpty());
  // Nothing but the update status is left at the bottom of the panel, and that is hidden.
  auto* status = window.findChild<QLabel*>(QStringLiteral("startPanelUpdateStatus"));
  CHECK(status != nullptr && !status->isVisible());
  CHECK(!column->isAncestorOf(status));

  // The credit line (Seth, November 2026) reads
  //   Created by Seth A. Robinson (rtsoft.com)   GitHub: SethRobinson/Patchy
  // The name is plain text; the website and the repository are the line's two links.
  const auto credit_shown = rendered_label(*credit);
  const auto github_shown = rendered_label(*github);
  CHECK(credit_shown.plain == QStringLiteral("Created by Seth A. Robinson (rtsoft.com)"));
  CHECK(github_shown.plain == QStringLiteral("GitHub: SethRobinson/Patchy"));
  CHECK(credit_shown.links.size() == 1);
  CHECK(credit_shown.links.value(QStringLiteral("https://rtsoft.com")) == QStringLiteral("rtsoft.com"));
  CHECK(github_shown.links.size() == 1);
  CHECK(github_shown.links.value(QStringLiteral("https://github.com/SethRobinson/Patchy")) ==
        QStringLiteral("SethRobinson/Patchy"));
  {
    // No link anywhere on the name, and none to the GitHub profile on this screen.
    QTextDocument document;
    document.setHtml(credit->text());
    const auto name_at = document.toPlainText().indexOf(QStringLiteral("Seth A. Robinson"));
    CHECK(name_at >= 0);
    for (int offset = 0; offset < QStringLiteral("Seth A. Robinson").size(); ++offset) {
      QTextCursor cursor(&document);
      cursor.setPosition(name_at + offset + 1);
      CHECK(!cursor.charFormat().isAnchor());
    }
    CHECK(!credit->text().contains(QStringLiteral("github.com/SethRobinson\"")));
    CHECK(!github->text().contains(QStringLiteral("github.com/SethRobinson\"")));
  }
  CHECK(credit->toolTip().isEmpty() && github->toolTip().isEmpty());
  for (const auto* label : {credit, github}) {
    CHECK(label->textFormat() == Qt::RichText);
    CHECK(label->openExternalLinks());
    CHECK(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByMouse));
    CHECK(label->textInteractionFlags().testFlag(Qt::LinksAccessibleByKeyboard));
    CHECK(!label->text().contains(QStringLiteral("@link_text")));  // the theme's link color is filled in
  }
#if QT_CONFIG(accessibility)
  // Assistive technology reads each part as it is shown.
  for (const auto& [label, shown] : {std::pair{credit, credit_shown.plain}, std::pair{github, github_shown.plain}}) {
    auto* accessible = QAccessible::queryAccessibleInterface(label);
    CHECK(accessible != nullptr);
    CHECK(accessible->text(QAccessible::Name) == shown);
  }
#endif

  // One line while there is room: the credit, a clear gap, the repository, both whole.
  const int credit_line = credit->sizeHint().width();
  const int github_line = github->sizeHint().width();
  const int gap = github->mapTo(panel, QPoint(0, 0)).x() - credit->mapTo(panel, QPoint(credit->width(), 0)).x();
  std::cout << "[i750] credit line: credit " << credit_line << " px, gap " << gap << " px, repository " << github_line
            << " px, row " << credit->parentWidget()->width() << " px\n";
  CHECK(top_in_panel(credit) == top_in_panel(github));
  CHECK(gap >= 10 && gap <= 24);
  CHECK(credit->width() >= credit_line && github->width() >= github_line);
  CHECK(credit->height() < 2 * QFontMetrics(credit->font()).lineSpacing());
  CHECK(clipped_labels(*panel).isEmpty());
  // The block stays compact and centered, and the header rows keep one left edge.
  CHECK(column->width() <= patchy::ui::ui_text_length(560));
  CHECK(std::abs((column->mapTo(panel, QPoint(0, 0)).x() + column->width() / 2) - panel->width() / 2) <= 1);
  CHECK(credit->mapTo(panel, QPoint(0, 0)).x() == tagline->mapTo(panel, QPoint(0, 0)).x());
  save_widget_artifact("ui_start_panel_header_wide", window);

  // Every part of the line lies inside the row, and the row inside the header: a part wider
  // than its row would be cut by it without the label itself knowing.
  auto* row = credit->parentWidget();
  CHECK(row != nullptr && row == github->parentWidget());
  const auto parts_inside_row = [&] {
    return row->rect().contains(credit->geometry()) && row->rect().contains(github->geometry()) &&
           row->mapTo(column, QPoint(0, 0)).x() >= 0 &&
           row->mapTo(column, QPoint(row->width(), 0)).x() <= column->width() &&
           credit->height() >= credit->heightForWidth(credit->width()) &&
           github->height() >= github->heightForWidth(github->width());
  };
  CHECK(parts_inside_row());

  // A little less room than the line needs: it folds between its two parts. Two lines, the
  // repository under the credit at the same left edge, each part whole and on one line.
  window.resize(900, 800);
  QApplication::processEvents();
  QApplication::processEvents();
  std::cout << "[i750] 900 px window: column " << column->width() << " px, row " << row->width() << " x "
            << row->height() << " px\n";
  CHECK(row->width() < credit_line + gap + github_line);
  CHECK(top_in_panel(github) >= bottom_in_panel(credit));
  CHECK(github->mapTo(panel, QPoint(0, 0)).x() == credit->mapTo(panel, QPoint(0, 0)).x());
  CHECK(credit->width() >= credit_line && github->width() >= github_line);
  CHECK(credit->height() < 2 * QFontMetrics(credit->font()).lineSpacing());
  CHECK(github->height() < 2 * QFontMetrics(github->font()).lineSpacing());
  CHECK(parts_inside_row());
  CHECK(bottom_in_panel(github) <= top_in_panel(new_button));
  CHECK(clipped_labels(*panel).isEmpty());
  save_widget_artifact("ui_start_panel_header_folded", window);

  // A small window: the header narrows with it. The tagline wraps instead of being cut, the
  // credit line folds, a part wider than what is left wraps inside it, and nothing reaches
  // past the panel.
  window.resize(800, 800);
  QApplication::processEvents();
  QApplication::processEvents();
  std::cout << "[i750] small window: panel " << panel->width() << " px, column " << column->width()
            << " px, tagline label " << tagline->width() << " x " << tagline->height() << " px\n";
  CHECK(column->width() <= panel->width());
  CHECK(tagline->width() < tagline_line);
  CHECK(tagline->height() >= 2 * QFontMetrics(tagline->font()).lineSpacing() - 2);
  CHECK(tagline->height() >= tagline->heightForWidth(tagline->width()));
  // The version wraps like the tagline does, whole, still under the title.
  std::cout << "[i750] small window: version label " << version->width() << " x " << version->height() << " px\n";
  CHECK(version->width() < version_line);
  CHECK(version->height() >= version->heightForWidth(version->width()));
  CHECK(version->height() >= 2 * QFontMetrics(version->font()).lineSpacing() - 2);
  CHECK(bottom_in_panel(title) <= top_in_panel(version));
  CHECK(bottom_in_panel(version) <= top_in_panel(tagline));
  for (const auto* label : {static_cast<const QWidget*>(title), static_cast<const QWidget*>(version),
                            static_cast<const QWidget*>(tagline), static_cast<const QWidget*>(credit),
                            static_cast<const QWidget*>(github)}) {
    const int left = label->mapTo(panel, QPoint(0, 0)).x();
    CHECK(left >= 0);
    CHECK(left + label->width() <= panel->width());
  }
  // The repository under the credit at the same left edge, nothing cut by the row.
  std::cout << "[i750] small window: row " << row->width() << " x " << row->height() << " px, credit "
            << credit->width() << " x " << credit->height() << " px, repository " << github->width() << " x "
            << github->height() << " px\n";
  CHECK(top_in_panel(github) >= bottom_in_panel(credit));
  CHECK(github->mapTo(panel, QPoint(0, 0)).x() == credit->mapTo(panel, QPoint(0, 0)).x());
  CHECK(parts_inside_row());
  CHECK(bottom_in_panel(github) <= top_in_panel(new_button));
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
    // The box is rounded up to a coarse step, so a growing window reloads pictures rarely.
    const auto box = cache.display_box();
    CHECK(box.width() >= 120 && box.width() < 120 + 48 && box.height() >= 84 && box.height() < 84 + 48);
    for (qsizetype index = 0; index < paths.size(); ++index) {
      CHECK(settled_state(cache, paths[index]) == RecentThumbnailState::ready);
      const auto* image = cache.image(paths[index]);
      CHECK(image != nullptr);
      // Fitted to the display box, aspect kept (600 x 300 is 2:1).
      CHECK(image->width() <= box.width() && image->height() <= box.height());
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

    // The window grew: a picture that was scaled down for the smaller box loads again at the
    // larger one the next time its cell is painted, so it is never drawn enlarged. One that
    // already holds all its stored pixels (the 64 x 48 PSD thumbnail) is left alone.
    const QSize small = cache.image(paths[0])->size();
    cache.set_display_box(QSize(400, 300));
    cache.request(psd);
    CHECK(cache.state(psd) == RecentThumbnailState::ready);
    cache.request(paths[0]);
    CHECK(cache.state(paths[0]) == RecentThumbnailState::pending);
    CHECK(cache.image(paths[0]) != nullptr);  // the old picture stays up meanwhile
    CHECK(process_events_until([&] { return cache.state(paths[0]) == RecentThumbnailState::ready; }, 8000));
    CHECK(cache.image(paths[0])->width() > small.width());
    CHECK(cache.image(paths[0])->width() <= cache.display_box().width());
    CHECK(near_color(center_color(*cache.image(paths[0])), colors[0]));
    // A smaller box again asks for nothing: a larger picture draws down cleanly.
    cache.set_display_box(QSize(120, 84));
    cache.request(paths[0]);
    CHECK(cache.state(paths[0]) == RecentThumbnailState::ready);
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

  // A picture stored by the first grid (256 px, marked version 1) is replaced by one of the
  // current size as soon as the file can be read again, in the same cache file.
  {
    const auto cache_file = patchy::ui::recent_thumbnail_cache_file(paths[3]);
    QImage stored;
    CHECK(stored.load(cache_file, "PNG"));
    QImage older = stored.scaled(256, 128);
    older.fill(QColor(90, 90, 90));
    for (const auto& key : stored.textKeys()) {
      older.setText(key, stored.text(key));
    }
    older.setText(QStringLiteral("PatchyThumbnail"), QStringLiteral("1"));
    CHECK(older.save(cache_file, "PNG"));
    RecentThumbnailCache cache;
    CHECK(settled_state(cache, paths[3]) == RecentThumbnailState::ready);
    CHECK(near_color(center_color(*cache.image(paths[3])), colors[3]));
    CHECK(wait_until_idle(cache));
    QImage replaced;
    CHECK(replaced.load(cache_file, "PNG"));
    CHECK(replaced.width() == patchy::ui::kRecentThumbnailStoredSide);
    CHECK(replaced.text(QStringLiteral("PatchyThumbnail")) == QStringLiteral("2"));
    CHECK(QDir(cache_dir).entryList(QDir::Files).size() == paths.size() + 1);
  }
  // A file the worker cannot picture (this one is not a real XCF) keeps the older picture
  // Patchy stored for it: there is nothing to make a new one from until it is opened or saved
  // again. It is marked, so the next start does not read it again, and forgetting the entry
  // deletes the marker with the picture.
  {
    const auto no_preview = root + QStringLiteral("/kept-picture.xcf");
    {
      QFile file(no_preview);
      CHECK(file.open(QIODevice::WriteOnly));
      file.write(QByteArray(600, 'g'));
    }
    QImage composite(400, 200, QImage::Format_RGBA8888);
    composite.fill(QColor(30, 200, 120));
    {
      RecentThumbnailCache cache;
      cache.store(no_preview, composite);
      CHECK(process_events_until([&] { return cache.state(no_preview) == RecentThumbnailState::ready; }, 8000));
      CHECK(wait_until_idle(cache));
    }
    const auto cache_file = patchy::ui::recent_thumbnail_cache_file(no_preview);
    QImage stored;
    CHECK(stored.load(cache_file, "PNG"));
    stored.setText(QStringLiteral("PatchyThumbnail"), QStringLiteral("1"));
    CHECK(stored.save(cache_file, "PNG"));
    {
      RecentThumbnailCache cache;
      CHECK(settled_state(cache, no_preview) == RecentThumbnailState::ready);
      CHECK(near_color(center_color(*cache.image(no_preview)), QColor(30, 200, 120)));
      CHECK(wait_until_idle(cache));
      auto marker = cache_file;
      marker.chop(4);
      marker += QStringLiteral(".none");
      CHECK(QFileInfo::exists(marker));
      cache.forget({no_preview});
      CHECK(wait_until_idle(cache));
      CHECK(!QFileInfo::exists(cache_file));
      CHECK(!QFileInfo::exists(marker));
    }
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
  // An XCF header with nothing after it: Patchy's reader refuses it.
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

  // Network names of the kinds the slower readers open: refused just the same.
  QStringList not_local = network_paths;
  not_local << QStringLiteral("\\\\patchy-test-host.invalid\\share\\layers.xcf")
            << QStringLiteral("//patchy-test-host.invalid/share/pages.pdf")
            << QStringLiteral("\\\\patchy-test-host.invalid\\share\\drawing.svg")
            << QStringLiteral("\\\\patchy-test-host.invalid\\share\\photo.nef")
            << QStringLiteral("\\\\patchy-test-host.invalid\\share\\painting.kra");
  RecentThumbnailCache cache;
  {
    const int opens = patchy::ui::recent_thumbnail_file_opens_for_testing();
    const int reads = patchy::ui::recent_thumbnail_document_reads_for_testing();
    QStringList untouched = not_local;
    untouched << missing << folder;
    for (const auto& path : untouched) {
      CHECK(settled_state(cache, path) == RecentThumbnailState::unavailable);
      CHECK(cache.image(path) == nullptr);
    }
    CHECK(wait_until_idle(cache));
    // No reader was reached, and nothing was written: the folder was not even created.
    CHECK(patchy::ui::recent_thumbnail_file_opens_for_testing() == opens);
    CHECK(patchy::ui::recent_thumbnail_document_reads_for_testing() == reads);
    CHECK(!QFileInfo::exists(cache_dir) || QDir(cache_dir).entryList(QDir::Files).isEmpty());
  }
  // Files that are there and have no picture: each gets a "no picture" marker (so it is not
  // read again at every start) and never a picture.
  const QStringList no_picture{garbage, unsupported, huge};
  for (const auto& path : no_picture) {
    CHECK(settled_state(cache, path) == RecentThumbnailState::unavailable);
    CHECK(cache.image(path) == nullptr);
  }
  CHECK(wait_until_idle(cache));
  CHECK(QDir(cache_dir).entryList({QStringLiteral("*.png")}, QDir::Files).isEmpty());
  CHECK(QDir(cache_dir).entryList({QStringLiteral("*.none")}, QDir::Files).size() == no_picture.size());
  cache.retain_only({});
  CHECK(wait_until_idle(cache));
  // The picture of an open document is refused for a network path too.
  QImage composite(64, 64, QImage::Format_RGBA8888);
  composite.fill(Qt::blue);
  cache.store(network_paths[0], composite);
  CHECK(process_events_until([&] { return cache.state(network_paths[0]) == RecentThumbnailState::unavailable; }, 8000));
  CHECK(wait_until_idle(cache));
  CHECK(QDir(cache_dir).entryList(QDir::Files).isEmpty());
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

    // With the list chosen no thumbnail is made or stored.
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
    CHECK(first.size() == parts.list->grid_cell_size());
    CHECK(first.height() == RecentFileList::grid_cell_height(first.width()));
    CHECK(second.top() == first.top());
    CHECK(second.left() >= first.right());
    CHECK(first.width() >= patchy::ui::kRecentGridMinCellWidth);
    // The picture box is 4:3 and grows with the cell.
    const auto picture_box = RecentFileList::grid_picture_rect(first);
    CHECK(picture_box.height() == picture_box.width() * 3 / 4);
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

    // Back to the list: stored as an explicit value (a missing key means the default, which
    // is the grid), rows again, and the cached pictures are deleted: the list keeps none of
    // the user's files as pictures on disk.
    for (const auto& path : recent) {
      CHECK(QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(path)));
    }
    click(*parts.list_button, parts.list_button->rect().center());
    CHECK(!parts.panel->recent_grid_view());
    CHECK(!patchy::ui::stored_start_panel_recent_grid_view());
    CHECK(patchy::ui::app_settings().contains(QStringLiteral("startPanel/recentFilesGridView")));
    CHECK(!patchy::ui::app_settings().value(QStringLiteral("startPanel/recentFilesGridView")).toBool());
    CHECK(process_events_until(
        [&] {
          for (const auto& path : recent) {
            if (QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(path))) {
              return false;
            }
          }
          return true;
        },
        8000));
    for (const auto& path : recent) {
      CHECK(parts.panel->recent_thumbnails().state(path) == RecentThumbnailState::unknown);
      CHECK(QFileInfo::exists(path));  // the pictures go, never the files themselves
    }
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
  const QRect unfiltered_box = parts.list->geometry();
  const QPoint unfiltered_filter_place = parts.filter->mapTo(parts.panel, QPoint(0, 0));
  parts.filter->setText(QStringLiteral("grid-beta"));
  QApplication::processEvents();
  CHECK(parts.list->count() == 1);
  CHECK(parts.list->shows_cells());
  CHECK(parts.list->item(0)->data(kRecentPathRole).toString() == recent[1]);
  // Typing a filter never resizes the box or moves anything: it keeps the size the whole
  // list gave it, with the matches at its top.
  process_events_for(50);
  CHECK(parts.list->geometry() == unfiltered_box);
  CHECK(parts.filter->mapTo(parts.panel, QPoint(0, 0)) == unfiltered_filter_place);
  save_widget_artifact("ui_start_panel_recent_grid_filtered", window);

  // No match: the placeholder row, full width, as in list view.
  parts.filter->setText(QStringLiteral("zzz-no-such-file"));
  QApplication::processEvents();
  CHECK(parts.list->count() == 1);
  CHECK(parts.list->item(0)->data(kRecentPathRole).toString().isEmpty());
  CHECK(parts.list->item(0)->text() == QStringLiteral("No matching recent files"));
  CHECK(parts.list->grid_view() && !parts.list->shows_cells());
  CHECK(parts.list->visualItemRect(parts.list->item(0)).width() > parts.list->viewport()->width() / 2);
  process_events_for(50);
  CHECK(parts.list->geometry() == unfiltered_box);
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
  // An OpenRaster file whose own merged image (green) is not what its layer holds (red). The
  // worker's picture is the file's merged image; once Patchy has opened the file, the picture
  // is the document as Patchy opened it.
  const QColor file_preview(40, 190, 70);
  const QColor layer_color(210, 50, 50);
  const auto path = write_container(
      scratch_file(QStringLiteral("stored-picture.ora")), "image/openraster",
      {{"stack.xml",
        to_vector(QByteArray("<?xml version='1.0' encoding='UTF-8'?><image version=\"0.0.3\" w=\"240\" h=\"120\">"
                             "<stack><layer name=\"Layer\" src=\"data/layer.png\" x=\"0\" y=\"0\"/></stack></image>")),
        true},
       {"data/layer.png", to_vector(png_bytes(layer_color, QSize(240, 120))), true},
       {"mergedimage.png", to_vector(png_bytes(file_preview, QSize(240, 120))), true}});
  QFile::remove(patchy::ui::recent_thumbnail_cache_file(path));

  MainWindow window;
  show_window_empty(window);
  window.resize(1280, 860);
  QApplication::processEvents();
  auto parts = panel_parts(window);
  const auto cache_file = patchy::ui::recent_thumbnail_cache_file(path);
  {
    // Before Patchy has opened it, the picture is the one the file carries.
    RecentThumbnailCache probe;
    CHECK(settled_state(probe, path) == RecentThumbnailState::ready);
    CHECK(near_color(center_color(*probe.image(path)), file_preview));
    CHECK(wait_until_idle(probe));
  }

  MainWindowTestAccess::open_document_path(window, path);
  CHECK(parts.tabs->count() == 1);
  // The first render lands a moment after the open; the window stores the picture then.
  QImage stored;
  CHECK(process_events_until(
      [&] { return stored.load(cache_file, "PNG") && near_color(center_color(stored), layer_color); }, 10000));
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

// --- The section uses the window's space (GitHub issue 750 follow-up) ---------------------------

// What the Recent Files section looks like right now, for comparing two window sizes.
struct SectionShape {
  QRect section;       // in panel coordinates
  QRect box;           // the list box, in panel coordinates
  QRect block;         // the compact header-and-buttons block, in panel coordinates
  int columns{0};      // cells in the first row (1 in list view)
  int rows_in_view{0}; // rows (of cells) wholly inside the box
  // Where the room for the section ends: the panel's bottom margin, or the top of the update
  // status line while that shows.
  int floor{0};
  int panel_width{0};
};

SectionShape section_shape(MainWindow& window, PanelParts& parts) {
  QApplication::processEvents();
  QApplication::processEvents();
  auto* section = window.findChild<QWidget*>(QStringLiteral("startPanelRecentSection"));
  auto* column = window.findChild<QWidget*>(QStringLiteral("startPanelColumn"));
  auto* status = window.findChild<QLabel*>(QStringLiteral("startPanelUpdateStatus"));
  CHECK(section != nullptr && column != nullptr && status != nullptr && parts.panel->layout() != nullptr);
  SectionShape shape;
  shape.section = QRect(section->mapTo(parts.panel, QPoint(0, 0)), section->size());
  shape.box = QRect(parts.list->mapTo(parts.panel, QPoint(0, 0)), parts.list->size());
  shape.block = QRect(column->mapTo(parts.panel, QPoint(0, 0)), column->size());
  shape.floor = status->isVisible() ? status->mapTo(parts.panel, QPoint(0, 0)).y()
                                    : parts.panel->height() - parts.panel->layout()->contentsMargins().bottom();
  shape.panel_width = parts.panel->width();
  const int top = parts.list->visualItemRect(parts.list->item(0)).top();
  const QRect view = parts.list->viewport()->rect();
  int last_whole_bottom = -1;
  for (int row = 0; row < parts.list->count(); ++row) {
    const auto rect = parts.list->visualItemRect(parts.list->item(row));
    if (rect.top() == top) {
      ++shape.columns;
    }
    if (view.contains(rect) && rect.bottom() > last_whole_bottom) {
      last_whole_bottom = rect.bottom();
      ++shape.rows_in_view;
    }
  }
  return shape;
}

bool operator==(const SectionShape& a, const SectionShape& b) {
  return a.section == b.section && a.box == b.box && a.block == b.block && a.columns == b.columns &&
         a.rows_in_view == b.rows_in_view && a.floor == b.floor && a.panel_width == b.panel_width;
}

// The shape once the layout has come to rest: the block, the section and the panel around
// them each lay out on their own pass of the event loop, and the list lays its cells out on
// a timer after a resize.
SectionShape settled_shape(MainWindow& window, PanelParts& parts) {
  auto shape = section_shape(window, parts);
  for (int attempt = 0; attempt < 40; ++attempt) {
    patchy::test::ui::process_events_for(40);
    const auto next = section_shape(window, parts);
    if (next == shape) {
      break;
    }
    shape = next;
  }
  return shape;
}

// clipped_labels, with what it found printed so a failure names the label.
bool no_clipped_labels(QWidget& panel) {
  const auto clipped = patchy::test::ui::clipped_labels(panel);
  for (const auto& label : clipped) {
    std::cout << "[i750] clipped label: " << label.toStdString() << "\n";
  }
  return clipped.isEmpty();
}

QStringList seed_recent_pictures(GridTestSettings& settings, const QString& stem, int count) {
  QStringList recent;
  for (int index = 0; index < count; ++index) {
    recent << write_png(scratch_file(QStringLiteral("%1-%2.png").arg(stem).arg(index, 2, 10, QLatin1Char('0'))),
                        QColor::fromHsv((index * 37) % 360, 170, 210), QSize(96, 64));
  }
  settings.set_recent(recent);
  return recent;
}

// The whole section stays inside the panel, above its bottom margin (and the update status
// line while that shows), and the compact block stays one centered block of its own width.
void check_section_fits(const SectionShape& shape) {
  CHECK(shape.section.left() >= 0 && shape.section.right() < shape.panel_width);
  CHECK(shape.box.left() >= shape.section.left() && shape.box.right() <= shape.section.right());
  CHECK(shape.box.bottom() < shape.floor);
  CHECK(shape.block.left() >= 0 && shape.block.right() < shape.panel_width);
  CHECK(shape.block.bottom() < shape.box.top());
  CHECK(std::abs(shape.block.center().x() - shape.panel_width / 2) <= 2);
  CHECK(std::abs(shape.section.center().x() - shape.panel_width / 2) <= 2);
}

void ui_start_panel_recent_section_grows_with_a_large_window() {
  using namespace patchy::test::ui;
  GridTestSettings settings;
  settings.set_grid(true);
  const auto recent = seed_recent_pictures(settings, QStringLiteral("large"), 40);

  MainWindow window;
  show_window_empty(window);
  window.resize(1280, 860);
  auto parts = panel_parts(window);
  CHECK(process_events_until([&] { return parts.list->count() == 40; }));
  CHECK(parts.list->shows_cells());
  const auto grid_small = settled_shape(window, parts);
  check_section_fits(grid_small);
  CHECK(grid_small.columns >= 3);
  CHECK(grid_small.rows_in_view >= 1);
  CHECK(parts.list->verticalScrollBar()->maximum() > 0);
  save_widget_artifact("ui_start_panel_section_grid_1280", window);

  // About the window of the report: the panel is some 2700 x 1150 px.
  window.resize(3126, 1283);
  const auto grid_large = settled_shape(window, parts);
  check_section_fits(grid_large);
  std::cout << "[i750] grid, 1280 x 860: section " << grid_small.section.width() << " px, " << grid_small.columns
            << " columns, " << grid_small.rows_in_view << " whole rows; 3126 x 1283: section "
            << grid_large.section.width() << " px, " << grid_large.columns << " columns, " << grid_large.rows_in_view
            << " whole rows, cell " << parts.list->grid_cell_size().width() << " x "
            << parts.list->grid_cell_size().height() << " px, block " << grid_large.block.width() << " px at y "
            << grid_large.block.top() << "\n";
  // More columns and more rows than the ordinary window, in a section far wider than the
  // header block, which itself has not stretched.
  CHECK(grid_large.columns > grid_small.columns);
  CHECK(grid_large.rows_in_view > grid_small.rows_in_view);
  CHECK(grid_large.section.width() > 2 * grid_large.block.width());
  CHECK(grid_large.box.width() > grid_small.box.width());
  CHECK(grid_large.section.width() <= 1900);
  CHECK(grid_large.block.width() == grid_small.block.width());
  CHECK(parts.list->grid_cell_size().width() >= patchy::ui::kRecentGridMinCellWidth);
  CHECK(parts.list->grid_cell_size().width() <= 260);
  // The list is long enough to fill the height: the block sits at the top margin and the box
  // reaches down to the panel's bottom margin, with nothing under it (the version is in the
  // header now, and the hidden update status takes no height).
  CHECK(grid_large.block.top() <= 40);
  CHECK(grid_large.floor - grid_large.box.bottom() <= 2);
  CHECK(grid_large.floor == parts.panel->height() - 24);
  CHECK(all_thumbnails_settled(parts));
  save_widget_artifact("ui_start_panel_section_grid_large", window);

  // The update status appears a moment after the window shows. It takes its line from the
  // bottom of the box: the header and the top of the box stay where they are.
  {
    auto* status = window.findChild<QLabel*>(QStringLiteral("startPanelUpdateStatus"));
    CHECK(status != nullptr);
    parts.panel->set_update_status(QStringLiteral("Update available: Patchy 9.99."));
    const auto with_status = settled_shape(window, parts);
    CHECK(status->isVisible());
    check_section_fits(with_status);
    CHECK(with_status.block == grid_large.block);
    CHECK(with_status.box.top() == grid_large.box.top());
    CHECK(with_status.box.bottom() < grid_large.box.bottom());
    CHECK(with_status.floor - with_status.box.bottom() >= 8 && with_status.floor - with_status.box.bottom() <= 16);
    CHECK(status->mapTo(parts.panel, QPoint(0, status->height())).y() <= parts.panel->height() - 24 + 1);
    CHECK(std::abs(status->mapTo(parts.panel, QPoint(status->width() / 2, 0)).x() - parts.panel->width() / 2) <= 2);
    save_widget_artifact("ui_start_panel_section_grid_large_update_status", window);
    parts.panel->set_update_status(QString());
    CHECK(settled_shape(window, parts) == grid_large);
  }

  // List view on the same window: wider and taller than on the ordinary one, but not as wide
  // as the grid (a row that long is hard to read across).
  click(*parts.list_button, parts.list_button->rect().center());
  CHECK(process_events_until([&] { return !parts.list->shows_cells(); }));
  const auto list_large = settled_shape(window, parts);
  CHECK(list_large.box.width() < grid_large.box.width());
  check_section_fits(list_large);
  CHECK(list_large.columns == 1);
  CHECK(list_large.section.width() > list_large.block.width());
  CHECK(list_large.section.width() <= 900);
  CHECK(list_large.rows_in_view > 12);
  CHECK(list_large.block.top() <= 40);
  save_widget_artifact("ui_start_panel_section_list_large", window);

  window.resize(1280, 860);
  const auto list_small = settled_shape(window, parts);
  check_section_fits(list_small);
  std::cout << "[i750] list, 1280 x 860: section " << list_small.section.width() << " px, " << list_small.rows_in_view
            << " whole rows; 3126 x 1283: section " << list_large.section.width() << " px, "
            << list_large.rows_in_view << " whole rows\n";
  save_widget_artifact("ui_start_panel_section_list_1280", window);
  CHECK(list_small.rows_in_view >= 8);  // at least the eight rows the box used to stop at
  CHECK(list_small.rows_in_view < list_large.rows_in_view);
  CHECK(list_small.section.width() <= list_large.section.width());

  // Back to the grid at the ordinary size: exactly the layout this window had before the
  // trip to the large one.
  click(*parts.grid_button, parts.grid_button->rect().center());
  CHECK(process_events_until([&] { return parts.list->shows_cells(); }));
  const auto grid_again = settled_shape(window, parts);
  CHECK(grid_again.box == grid_small.box);
  CHECK(grid_again.section == grid_small.section);
  CHECK(grid_again.block == grid_small.block);
  CHECK(grid_again.columns == grid_small.columns);
  CHECK(grid_again.rows_in_view == grid_small.rows_in_view);
  // And once more through the large size and back, by steps, as a drag of the window edge.
  for (const int width : {1500, 2100, 2700, 3126, 2400, 1700, 1280}) {
    window.resize(width, 860 + (width - 1280) / 5);
    check_section_fits(settled_shape(window, parts));
  }
  window.resize(1280, 860);
  CHECK(settled_shape(window, parts) == grid_small);
}

void ui_start_panel_recent_section_is_only_as_large_as_few_entries_need() {
  using namespace patchy::test::ui;
  GridTestSettings settings;
  settings.set_grid(true);
  const auto recent = seed_recent_pictures(settings, QStringLiteral("few"), 3);

  MainWindow window;
  show_window_empty(window);
  window.resize(3126, 1283);
  auto parts = panel_parts(window);
  CHECK(process_events_until([&] { return parts.list->count() == 3; }));
  const auto grid = settled_shape(window, parts);
  check_section_fits(grid);
  // One row of three cells in a box of their size: no wide empty frame, no empty rows.
  CHECK(grid.columns == 3);
  CHECK(grid.box.height() == parts.list->grid_cell_size().height() + patchy::ui::kRecentListChrome);
  CHECK(grid.box.width() < 900);
  CHECK(grid.box.width() >= grid.block.width());
  CHECK(parts.list->visualItemRect(parts.list->item(2)).right() > parts.list->viewport()->width() * 3 / 4);
  CHECK(parts.list->verticalScrollBar()->maximum() == 0);
  // With this little content the whole block floats about in the middle, as it always did.
  CHECK(grid.block.top() > 150);
  CHECK(grid.floor - grid.box.bottom() > 150);
  save_widget_artifact("ui_start_panel_section_grid_three_entries", window);
  // The update status showing at the bottom moves neither the block nor the box.
  parts.panel->set_update_status(QStringLiteral("Update available: Patchy 9.99."));
  {
    const auto with_status = settled_shape(window, parts);
    CHECK(with_status.block == grid.block);
    CHECK(with_status.box == grid.box);
    CHECK(with_status.floor > with_status.box.bottom() + 150);
  }
  parts.panel->set_update_status(QString());
  CHECK(settled_shape(window, parts) == grid);

  click(*parts.list_button, parts.list_button->rect().center());
  CHECK(process_events_until([&] { return !parts.list->shows_cells(); }));
  const auto list = settled_shape(window, parts);
  CHECK(list.box.height() == 3 * patchy::ui::recent_row_height() + patchy::ui::kRecentListChrome);
  check_section_fits(list);
  CHECK(list.block.top() > 150);
  CHECK(parts.list->verticalScrollBar()->maximum() == 0);
}

void ui_start_panel_recent_section_fits_a_small_window() {
  using namespace patchy::test::ui;
  GridTestSettings settings;
  settings.set_grid(true);
  const auto recent = seed_recent_pictures(settings, QStringLiteral("small"), 24);

  MainWindow window;
  show_window_empty(window);
  window.resize(800, 800);
  auto parts = panel_parts(window);
  CHECK(process_events_until([&] { return parts.list->count() == 24; }));
  const auto grid = settled_shape(window, parts);
  check_section_fits(grid);
  CHECK(grid.columns >= 2);
  CHECK(grid.rows_in_view >= 1);
  CHECK(parts.list->verticalScrollBar()->maximum() > 0);
  save_widget_artifact("ui_start_panel_section_grid_800", window);
  CHECK(no_clipped_labels(*parts.panel));

  click(*parts.list_button, parts.list_button->rect().center());
  CHECK(process_events_until([&] { return !parts.list->shows_cells(); }));
  const auto list = settled_shape(window, parts);
  check_section_fits(list);
  CHECK(list.rows_in_view >= 3);
  CHECK(parts.list->verticalScrollBar()->maximum() > 0);
  save_widget_artifact("ui_start_panel_section_list_800", window);
  CHECK(no_clipped_labels(*parts.panel));

  // Shorter still: the box gives way, down to one row, and scrolls, inside the panel.
  window.resize(800, 560);
  const auto shorter = settled_shape(window, parts);
  CHECK(shorter.box.height() < list.box.height());
  CHECK(shorter.box.height() >= patchy::ui::recent_row_height());
  CHECK(shorter.box.bottom() < shorter.floor);
  // The update status still has its line there.
  parts.panel->set_update_status(QStringLiteral("Update available: Patchy 9.99."));
  const auto shorter_with_status = settled_shape(window, parts);
  CHECK(shorter_with_status.box.bottom() < shorter_with_status.floor);
  CHECK(shorter_with_status.box.height() >= patchy::ui::recent_row_height());
  CHECK(no_clipped_labels(*parts.panel));
  parts.panel->set_update_status(QString());
}

// The grid is what a profile that never chose gets (GitHub issue 750 follow-up).
void ui_start_panel_recent_files_open_in_grid_by_default() {
  using namespace patchy::test::ui;
  GridTestSettings settings;
  const auto key = QStringLiteral("startPanel/recentFilesGridView");
  QStringList recent;
  recent << write_png(scratch_file(QStringLiteral("default-one.png")), QColor(200, 50, 50))
         << write_png(scratch_file(QStringLiteral("default-two.png")), QColor(50, 170, 70))
         << write_png(scratch_file(QStringLiteral("default-three.png")), QColor(60, 90, 210));
  settings.set_recent(recent);
  {
    auto store = patchy::ui::app_settings();
    store.remove(key);
    store.sync();
  }
  CHECK(patchy::ui::kDefaultStartPanelRecentGridView);
  CHECK(patchy::ui::stored_start_panel_recent_grid_view());

  {
    // A fresh profile: the grid, with nothing pressed, and looking does not write the key.
    MainWindow window;
    show_window_empty(window);
    window.resize(1280, 860);
    auto parts = panel_parts(window);
    CHECK(process_events_until([&] { return parts.list->count() == 3; }));
    CHECK(parts.panel->recent_grid_view());
    CHECK(parts.grid_button->isChecked() && !parts.list_button->isChecked());
    CHECK(parts.list->shows_cells());
    CHECK(all_thumbnails_settled(parts));
    CHECK(parts.panel->recent_thumbnails().state(recent[0]) == RecentThumbnailState::ready);
    CHECK(!patchy::ui::app_settings().contains(key));
    save_widget_artifact("ui_start_panel_recent_grid_by_default", window);

    // Choosing the list is remembered as an explicit false.
    click(*parts.list_button, parts.list_button->rect().center());
    CHECK(!parts.panel->recent_grid_view());
    CHECK(patchy::ui::app_settings().contains(key));
    CHECK(!patchy::ui::app_settings().value(key).toBool());
  }
  {
    // The list survives a restart, and the grid chosen again is stored as true.
    MainWindow window;
    show_window_empty(window);
    auto parts = panel_parts(window);
    CHECK(process_events_until([&] { return parts.list->count() == 3; }));
    CHECK(!parts.panel->recent_grid_view());
    CHECK(parts.list_button->isChecked());
    CHECK(!parts.list->shows_cells());
    click(*parts.grid_button, parts.grid_button->rect().center());
    CHECK(parts.panel->recent_grid_view());
    CHECK(patchy::ui::app_settings().contains(key));
    CHECK(patchy::ui::app_settings().value(key).toBool());
  }
  {
    MainWindow window;
    show_window_empty(window);
    auto parts = panel_parts(window);
    CHECK(process_events_until([&] { return parts.list->count() == 3; }));
    CHECK(parts.panel->recent_grid_view());
    CHECK(parts.list->shows_cells());
  }
}

// --- Startup: nothing for the grid runs before the first paint (docs/startup-performance.md) ---

void ui_start_panel_grid_does_no_thumbnail_work_before_its_first_paint() {
  using namespace patchy::test::ui;
  GridTestSettings settings;
  settings.set_grid(true);
  const auto recent = seed_recent_pictures(settings, QStringLiteral("startup"), 200);
  CHECK(recent.size() == 200);
  // A picture no recent entry owns: the sweep that removes it is housekeeping for later.
  const auto orphan =
      QDir(patchy::ui::recent_thumbnails_directory()).filePath(QString(40, QLatin1Char('a')) + QStringLiteral(".png"));
  CHECK(QDir().mkpath(QFileInfo(orphan).absolutePath()));
  write_png(orphan, QColor(1, 2, 3), QSize(8, 8));

  MainWindow window;
  auto* panel = window.findChild<StartPanel*>(QStringLiteral("startPanel"));
  CHECK(panel != nullptr);
  CHECK(panel->recent_grid_view());
  auto& thumbnails = panel->recent_thumbnails();
  // Built with Grid on and a full list: no picture was asked for, no worker was started and
  // the folder sweep is not queued. The zero-timer startup tasks change none of that.
  CHECK(thumbnails.requests_made() == 0);
  CHECK(thumbnails.loads_in_flight() == 0);
  CHECK(!thumbnails.busy());
  QApplication::processEvents();
  QApplication::processEvents();
  CHECK(thumbnails.requests_made() == 0);
  CHECK(!thumbnails.busy());
  CHECK(QFileInfo::exists(orphan));

  show_window_empty(window);
  window.resize(1280, 860);
  auto parts = panel_parts(window);
  CHECK(process_events_until([&] { return parts.list->count() == 200; }));
  (void)settled_shape(window, parts);
  parts.list->viewport()->repaint();
  // After the first paint only the cells in view were asked for: a dozen or so, not 200.
  int in_view = 0;
  for (int row = 0; row < parts.list->count(); ++row) {
    if (parts.list->visualItemRect(parts.list->item(row)).intersects(parts.list->viewport()->rect())) {
      ++in_view;
    }
  }
  CHECK(in_view > 0 && in_view < 40);
  CHECK(thumbnails.requests_made() > 0);
  CHECK(thumbnails.requests_made() <= in_view);
  CHECK(all_thumbnails_settled(parts));
  CHECK(process_events_until([&] { return thumbnails.loads_in_flight() == 0; }, 8000));
  CHECK(thumbnails.requests_made() <= in_view);

  // An arriving picture repaints its own cell: the list finds the row by its path.
  CHECK(parts.list->update_path(recent[0]));
  CHECK(!parts.list->update_path(QStringLiteral("C:/no/such/recent/file.png")));

  // The sweep runs by itself once startup is well over.
  CHECK(process_events_until([&] { return !QFileInfo::exists(orphan); },
                             patchy::ui::kRecentThumbnailSweepDelayMs + 8000));
  CHECK(QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(recent[0])));
}

void ui_recent_thumbnails_folder_stays_inside_an_isolated_settings_folder() {
  // A run with its own settings folder has its own recent list; its pictures, and the sweep
  // that deletes the ones no entry owns, must stay out of the user's real folder.
  const auto isolated = scratch_file(QStringLiteral("isolated-settings"));
  {
    EnvironmentGuard no_override("PATCHY_RECENT_THUMBNAILS_DIR", QString());
    EnvironmentGuard settings_root("PATCHY_SETTINGS_DIR", isolated);
    CHECK(patchy::ui::recent_thumbnails_directory() ==
          QDir(isolated).absoluteFilePath(QStringLiteral("RTsoft/Patchy/recent-thumbnails")));
    // The explicit folder still wins.
    const auto chosen = scratch_file(QStringLiteral("chosen-thumbnails"));
    EnvironmentGuard override_dir("PATCHY_RECENT_THUMBNAILS_DIR", chosen);
    CHECK(patchy::ui::recent_thumbnails_directory() == QDir(chosen).absolutePath());
  }
  // Back to this process's own folder, set by the test runner.
  CHECK(patchy::ui::recent_thumbnails_directory() ==
        QDir(qEnvironmentVariable("PATCHY_RECENT_THUMBNAILS_DIR")).absolutePath());
}

}  // namespace

// --- A picture for every kind Patchy opens (GitHub issue 750 follow-up 4) -----------------------

const char* const kSvgText =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"20\" viewBox=\"0 0 40 20\">"
    "<rect width=\"40\" height=\"20\" fill=\"#2060d0\"/></svg>";

void ui_recent_thumbnails_picture_every_kind_patchy_opens() {
  using namespace patchy::test::ui;
  const auto cache_dir = fresh_cache_dir(QStringLiteral("kinds-cache"));
  EnvironmentGuard cache_guard("PATCHY_RECENT_THUMBNAILS_DIR", cache_dir);
  const auto root = scratch_file(QStringLiteral("kinds"));
  CHECK(QDir().mkpath(root));
  const auto in_root = [&](const char* name) { return root + QLatin1Char('/') + QString::fromLatin1(name); };

  RecentThumbnailCache cache;
  const int reads_before = patchy::ui::recent_thumbnail_document_reads_for_testing();

  // Kinds Patchy's own reader opens and composites: the picture has the reader's canvas.
  struct ReaderKind {
    const char* format_dir;
    const char* fixture;
    const char* extension;
  };
  const std::vector<ReaderKind> reader_kinds{
      {"xcf", "plain-rgba.xcf", "xcf"},   {"xcf", "groups.xcf", "xcf"},
      {"xcf", "compressed.xcf", "xcf"},   {"xcf", "gegl-effects.xcf", "xcf"},
      {"af", "tiny-rgba8.af", "af"},      {"aseprite", "aseprite-layered.aseprite", "aseprite"},
      {"avif", "frames.avif", "avif"},    {"dds", "pillow-dxt5-16x16.dds", "dds"},
      {"jxl", "conformance-patches-lossless.jxl", "jxl"},
      {"rttex", "rgba-64x32.rttex", "rttex"},
  };
  int slow_kinds = 0;
  for (const auto& kind : reader_kinds) {
    const auto path = copy_fixture(kind.format_dir, kind.fixture, in_root(kind.fixture));
    stored_picture(cache, path, reader_canvas(path, kind.extension));
    ++slow_kinds;
  }
  CHECK(patchy::ui::recent_thumbnail_document_reads_for_testing() == reads_before + slow_kinds);

  // Kinds whose decoder the platform supplies: a picture wherever Patchy itself can open the file.
  for (const auto& kind :
       std::vector<ReaderKind>{{"heif", "quadrants.heic", "heic"}, {"jxr", "hdr-ramp.jxr", "jxr"}}) {
    const auto path = copy_fixture(kind.format_dir, kind.fixture, in_root(kind.fixture));
    QSize canvas;
    try {
      canvas = reader_canvas(path, kind.extension);
    } catch (const std::exception&) {
      // No decoder on this machine: Patchy cannot open the file either.
    }
    if (!canvas.isEmpty()) {
      stored_picture(cache, path, canvas);
    } else if (settled_state(cache, path) == RecentThumbnailState::ready) {
      CHECK(has_visible_pixel(*cache.image(path)));  // Qt's own plug-in read it
    } else if (std::string_view(kind.extension) == "heic") {
      // A missing HEIF codec can be installed later: no marker, so the next start looks again.
      CHECK(wait_until_idle(cache));
      CHECK(!QFileInfo::exists(marker_file(path)));
    }
  }

  // Flat kinds written here with Patchy's own writers: 32-bit float (EXR, Radiance HDR) and
  // JPEG 2000. A float picture is display-encoded, so the color comes back.
  QImage flat(96, 48, QImage::Format_RGBA8888);
  flat.fill(QColor(200, 90, 40));
  const auto flat_document = patchy::ui::document_from_qimage(flat, "Background");
  for (const char* extension : {"exr", "hdr", "jp2", "tga", "pcx", "bmp"}) {
    const auto* handler = patchy::builtin_format_registry().find_by_extension(extension);
    CHECK(handler != nullptr && handler->can_write());
    const auto path = write_bytes(in_root("written.") + QString::fromLatin1(extension), handler->write(flat_document));
    const auto stored = stored_picture(cache, path, QSize(96, 48));
    if (!near_color(center_color(stored), QColor(200, 90, 40), 16)) {
      std::cerr << extension << " picture center is " << center_color(stored).name().toStdString() << std::endl;
      CHECK(false);
    }
  }

  // A PSD saved without a thumbnail resource: the composite the file ends with.
  {
    const auto path = write_bytes(in_root("no-thumbnail.psd"), patchy::psd::DocumentIo::write_layered_rgb8(flat_document));
    const auto stored = stored_picture(cache, path, QSize(96, 48));
    CHECK(near_color(center_color(stored), QColor(200, 90, 40), 16));
  }

  // SVG, plain and compressed: drawn at the stored size, however small the file says it is.
  for (const auto& path : {write_bytes(in_root("icon.svg"), QByteArray(kSvgText)),
                           write_bytes(in_root("icon.svgz"), gzip_bytes(QByteArray(kSvgText)))}) {
    const auto stored = stored_picture(cache, path, QSize(40, 20));
    CHECK(stored.width() == patchy::ui::kRecentThumbnailStoredSide);
    CHECK(near_color(center_color(stored), QColor(0x20, 0x60, 0xd0)));
  }
  stored_picture(cache, copy_fixture("svg", "hot_air_balloons_cc0.svg", in_root("balloons.svg")), QSize());

#ifdef PATCHY_HAVE_QT_PDF
  // PDF: page 1, by position, whatever the other pages hold.
  {
    const auto path = write_pdf(in_root("three-pages.pdf"), {QColor(210, 40, 40), QColor(40, 40, 210), QColor(40, 180, 40)});
    const auto stored = stored_picture(cache, path, QSize(400, 200));
    CHECK(near_color(center_color(stored), QColor(210, 40, 40), 16));
    CHECK(stored.width() == patchy::ui::kRecentThumbnailStoredSide);
  }
#endif
  CHECK(QApplication::activeModalWidget() == nullptr);
}

void ui_recent_thumbnails_use_the_preview_a_file_carries() {
  using namespace patchy::test::ui;
  const auto cache_dir = fresh_cache_dir(QStringLiteral("embedded-cache"));
  EnvironmentGuard cache_guard("PATCHY_RECENT_THUMBNAILS_DIR", cache_dir);
  const auto root = scratch_file(QStringLiteral("embedded"));
  CHECK(QDir().mkpath(root));
  const auto in_root = [&](const char* name) { return root + QLatin1Char('/') + QString::fromLatin1(name); };
  const QColor merged(40, 190, 70);
  const QColor small(200, 60, 160);

  RecentThumbnailCache cache;
  const int reads_before = patchy::ui::recent_thumbnail_document_reads_for_testing();

  // OpenRaster and Krita: the application's own render of the whole image, straight from the
  // zip. These files hold nothing else, so no reader could have opened them.
  const auto ora = write_container(in_root("merged.ora"), "image/openraster",
                                   {{"mergedimage.png", to_vector(png_bytes(merged, QSize(300, 100))), true},
                                    {"Thumbnails/thumbnail.png", to_vector(png_bytes(small, QSize(120, 40))), true}});
  CHECK(near_color(center_color(stored_picture(cache, ora, QSize(300, 100))), merged));
  const auto kra = write_container(in_root("merged.kra"), "application/x-krita",
                                   {{"preview.png", to_vector(png_bytes(small, QSize(120, 40))), true},
                                    {"mergedimage.png", to_vector(png_bytes(merged, QSize(300, 100))), true}});
  CHECK(near_color(center_color(stored_picture(cache, kra, QSize(300, 100))), merged));
  // Without the merged image, the small preview each format has.
  const auto ora_small = write_container(in_root("small.ora"), "image/openraster",
                                         {{"Thumbnails/thumbnail.png", to_vector(png_bytes(small, QSize(120, 40))), true}});
  CHECK(near_color(center_color(stored_picture(cache, ora_small, QSize(120, 40))), small));
  const auto kra_small = write_container(in_root("small.kra"), "application/x-krita",
                                         {{"preview.png", to_vector(png_bytes(small, QSize(120, 40))), true}});
  CHECK(near_color(center_color(stored_picture(cache, kra_small, QSize(120, 40))), small));

  // Paint.NET: the thumbnail in the file's header.
  const auto pdn = copy_fixture("pdn", "blend-modes.pdn", in_root("blend-modes.pdn"));
  stored_picture(cache, pdn, reader_canvas(pdn, "pdn"));

  // A committed OpenRaster file with real layers has its merged image too.
  const auto layers = copy_fixture("ora", "python-layers.ora", in_root("python-layers.ora"));
  stored_picture(cache, layers, reader_canvas(layers, "ora"));

  CHECK(patchy::ui::recent_thumbnail_document_reads_for_testing() == reads_before);
}

void ui_recent_thumbnails_camera_raw_and_refused_tiff_use_local_fixtures() {
  using namespace patchy::test::ui;
  const auto raw_fixture = patchy::test::local_format_fixture_path("raw", "nikon_d80.nef");
  const auto tiff_fixture = patchy::test::local_format_fixture_path("tiff-flat", "shapes_lzw_14bps.tif");
  patchy::test::skip_unless_exists(raw_fixture);
  patchy::test::skip_unless_exists(tiff_fixture);
  const auto cache_dir = fresh_cache_dir(QStringLiteral("local-fixture-cache"));
  EnvironmentGuard cache_guard("PATCHY_RECENT_THUMBNAILS_DIR", cache_dir);
  const auto root = scratch_file(QStringLiteral("local-fixture-kinds"));
  CHECK(QDir().mkpath(root));

  RecentThumbnailCache cache;
  // A camera raw: the JPEG the camera embedded, with no develop.
  const int reads_before = patchy::ui::recent_thumbnail_document_reads_for_testing();
  const auto raw = root + QStringLiteral("/nikon_d80.nef");
  QFile::remove(raw);
  CHECK(QFile::copy(patchy::ui::to_qstring(raw_fixture), raw));
  const auto stored = stored_picture(cache, raw, QSize());
  // The D80's sensor is 3:2, whichever way the camera was held.
  CHECK(same_shape(stored.size(), QSize(3, 2)) || same_shape(stored.size(), QSize(2, 3)));
  CHECK(patchy::ui::recent_thumbnail_document_reads_for_testing() == reads_before);

  // A TIFF layout Qt's plug-in refuses: Patchy's flat decoder, as the open path does.
  const auto tiff = root + QStringLiteral("/shapes_lzw_14bps.tif");
  QFile::remove(tiff);
  CHECK(QFile::copy(patchy::ui::to_qstring(tiff_fixture), tiff));
  const auto document = patchy::psd::read_flat_tiff(to_vector(read_bytes(tiff)));
  stored_picture(cache, tiff, QSize(document.width(), document.height()));
}

void ui_recent_thumbnails_damaged_files_get_a_marker_and_no_retry() {
  using namespace patchy::test::ui;
  const auto cache_dir = fresh_cache_dir(QStringLiteral("marker-cache"));
  EnvironmentGuard cache_guard("PATCHY_RECENT_THUMBNAILS_DIR", cache_dir);
  const auto root = scratch_file(QStringLiteral("damaged"));
  CHECK(QDir().mkpath(root));
  const auto in_root = [&](const char* name) { return root + QLatin1Char('/') + QString::fromLatin1(name); };
  const auto fixture_bytes = [](std::string_view format_dir, std::string_view fixture) {
    return read_bytes(patchy::ui::to_qstring(patchy::test::committed_format_fixture_path(format_dir, fixture)));
  };
  const auto cut_in_half = [](const QByteArray& bytes) { return bytes.left(bytes.size() / 2); };

  QStringList damaged;
  // One damaged file per kind a reader or renderer opens: cut off inside its header (a reader
  // that opens what is left of a longer file gives that file its picture, as Patchy would open
  // it), or garbage after the signature.
  damaged << write_bytes(in_root("cut.xcf"), fixture_bytes("xcf", "groups.xcf").left(30));
  damaged << write_bytes(in_root("garbage.xcf"), QByteArray("gimp xcf v011\0", 14) + QByteArray(4000, '\x7f'));
  damaged << write_bytes(in_root("cut.ora"), cut_in_half(fixture_bytes("ora", "python-layers.ora")));
  damaged << write_bytes(in_root("cut.pdn"), fixture_bytes("pdn", "blend-modes.pdn").left(64));
  damaged << write_bytes(in_root("cut.af"), fixture_bytes("af", "tiny-rgba8.af").left(48));
  damaged << write_bytes(in_root("cut.avif"), fixture_bytes("avif", "frames.avif").left(48));
  damaged << write_bytes(in_root("cut.jxl"), fixture_bytes("jxl", "conformance-patches-lossless.jxl").left(24));
  damaged << write_bytes(in_root("cut.dds"), fixture_bytes("dds", "pillow-dxt5-16x16.dds").left(60));
  damaged << write_bytes(in_root("garbage.kra"), QByteArray(3000, 'k'));
  damaged << write_bytes(in_root("garbage.exr"), QByteArray(3000, 'e'));
  damaged << write_bytes(in_root("garbage.nef"), QByteArray(3000, 'n'));
  damaged << write_bytes(in_root("garbage.psd"), QByteArray("8BPS") + QByteArray(3000, 'p'));
  damaged << write_bytes(in_root("garbage.tif"), QByteArray("II*\0", 4) + QByteArray(3000, 't'));
  damaged << write_bytes(in_root("garbage.svg"), QByteArray("not an SVG at all"));
  damaged << write_bytes(in_root("cut.svgz"), gzip_bytes(QByteArray(kSvgText)).left(40));
  damaged << write_bytes(in_root("garbage.pdf"), QByteArray("%PDF-1.4\n") + QByteArray(3000, 'x'));
  // A PDF that opens only with a password: no picture and no question.
  damaged << write_bytes(in_root("password.pdf"), encrypted_aesv2_pdf_bytes());
  // An SVG that places an outside image file (here on a server, which nothing may reach unasked),
  // and one whose entity definitions could hide such a reference.
  damaged << write_bytes(in_root("outside-image.svg"),
                         QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                                    "width=\"40\" height=\"20\"><rect width=\"40\" height=\"20\" fill=\"red\"/>"
                                    "<image width=\"10\" height=\"10\" "
                                    "xlink:href=\"//patchy-test-host.invalid/share/picture.png\"/></svg>"));
  damaged << write_bytes(in_root("entities.svg"),
                         QByteArray("<!DOCTYPE svg [<!ENTITY e \"x\">]><svg xmlns=\"http://www.w3.org/2000/svg\" "
                                    "width=\"40\" height=\"20\"><rect width=\"40\" height=\"20\" fill=\"red\"/></svg>"));

  {
    RecentThumbnailCache cache;
    for (const auto& path : damaged) {
      if (settled_state(cache, path) != RecentThumbnailState::unavailable) {
        std::cerr << "a picture was made for " << path.toStdString() << std::endl;
        CHECK(false);
      }
      CHECK(cache.image(path) == nullptr);
    }
    CHECK(wait_until_idle(cache));
    CHECK(QApplication::activeModalWidget() == nullptr);
  }
  // Each has its marker and no picture; nothing else is in the folder.
  for (const auto& path : damaged) {
    if (!QFileInfo::exists(marker_file(path))) {
      std::cerr << "no marker for " << path.toStdString() << std::endl;
      CHECK(false);
    }
    CHECK(!QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(path)));
  }
  CHECK(QDir(cache_dir).entryList(QDir::Files).size() == damaged.size());

  // The next start: the markers answer, and no file is opened or read again.
  {
    const int opens = patchy::ui::recent_thumbnail_file_opens_for_testing();
    const int reads = patchy::ui::recent_thumbnail_document_reads_for_testing();
    RecentThumbnailCache cache;
    for (const auto& path : damaged) {
      CHECK(settled_state(cache, path) == RecentThumbnailState::unavailable);
    }
    CHECK(wait_until_idle(cache));
    CHECK(patchy::ui::recent_thumbnail_file_opens_for_testing() == opens);
    CHECK(patchy::ui::recent_thumbnail_document_reads_for_testing() == reads);

    // A file that changed is looked at again: this one is whole now and gets its picture,
    // and its marker goes.
    const auto repaired = damaged[0];
    write_bytes(repaired, fixture_bytes("xcf", "groups.xcf"));
    RecentThumbnailCache next;
    stored_picture(next, repaired, reader_canvas(repaired, "xcf"));
    CHECK(patchy::ui::recent_thumbnail_document_reads_for_testing() == reads + 1);

    // Removing an entry deletes its marker; the sweep deletes the markers no entry owns;
    // choosing the list (clear) deletes the rest. Never anything that is not the cache's.
    const auto foreign = cache_dir + QStringLiteral("/notes.none");
    write_bytes(foreign, QByteArray("mine"));
    next.forget({damaged[1]});
    CHECK(wait_until_idle(next));
    CHECK(!QFileInfo::exists(marker_file(damaged[1])));
    CHECK(QFileInfo::exists(marker_file(damaged[2])));
    next.retain_only({damaged[2], damaged[3], repaired});
    CHECK(wait_until_idle(next));
    CHECK(QFileInfo::exists(marker_file(damaged[2])) && QFileInfo::exists(marker_file(damaged[3])));
    CHECK(!QFileInfo::exists(marker_file(damaged[4])));
    CHECK(QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(repaired)));
    next.clear();
    CHECK(wait_until_idle(next));
    CHECK(QDir(cache_dir).entryList(QDir::Files) == QStringList{QStringLiteral("notes.none")});
    for (const auto& path : damaged) {
      CHECK(QFileInfo::exists(path));  // the user's files are never touched
    }
  }
}

void ui_recent_thumbnails_caps_refuse_a_file_before_it_is_decoded() {
  using namespace patchy::test::ui;
  const auto cache_dir = fresh_cache_dir(QStringLiteral("caps-cache"));
  EnvironmentGuard cache_guard("PATCHY_RECENT_THUMBNAILS_DIR", cache_dir);
  const auto root = scratch_file(QStringLiteral("caps"));
  CHECK(QDir().mkpath(root));
  const auto in_root = [&](const char* name) { return root + QLatin1Char('/') + QString::fromLatin1(name); };
  const auto grown = [&](const char* name, qint64 bytes) {
    const auto path = in_root(name);
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    CHECK(file.resize(bytes));
    return path;
  };

  // The XCF reader's limits, as the worker sets them: refused from the header and the layer
  // sizes, before a tile is decoded; untouched reads are as before.
  const auto xcf_bytes = to_vector(read_bytes(
      patchy::ui::to_qstring(patchy::test::committed_format_fixture_path("xcf", "groups.xcf"))));
  const auto xcf_canvas = patchy::xcf::DocumentIo::read(xcf_bytes);
  const auto canvas_pixels = static_cast<std::uint64_t>(xcf_canvas.width()) * static_cast<std::uint64_t>(xcf_canvas.height());
  const auto refused = [&](patchy::xcf::ReadLimits limits) {
    const patchy::xcf::ScopedReadLimits scope(limits);
    try {
      (void)patchy::xcf::DocumentIo::read(xcf_bytes);
    } catch (const std::exception&) {
      return true;
    }
    return false;
  };
  CHECK(refused({canvas_pixels - 1, 0, true}));
  CHECK(!refused({canvas_pixels, 0, true}));
  CHECK(refused({0, 16, true}));
  CHECK(!refused({0, canvas_pixels * 4 * 64, false}));
  CHECK(!refused({}));
  (void)patchy::xcf::DocumentIo::read(xcf_bytes);  // the limits ended with their scope

  // Files past the byte caps are never opened: only their size is looked at.
  QStringList over;
  over << grown("large.xcf", patchy::ui::kRecentThumbnailMaxDocumentBytes + 1);
  over << grown("large.jxl", patchy::ui::kRecentThumbnailMaxDocumentBytes + 1);
  over << grown("large.svg", patchy::ui::kRecentThumbnailMaxSvgBytes + 1);
  {
    const int opens = patchy::ui::recent_thumbnail_file_opens_for_testing();
    const int reads = patchy::ui::recent_thumbnail_document_reads_for_testing();
    RecentThumbnailCache cache;
    for (const auto& path : over) {
      CHECK(settled_state(cache, path) == RecentThumbnailState::unavailable);
    }
    CHECK(wait_until_idle(cache));
    CHECK(patchy::ui::recent_thumbnail_file_opens_for_testing() == opens);
    CHECK(patchy::ui::recent_thumbnail_document_reads_for_testing() == reads);
  }
  for (const auto& path : over) {
    CHECK(QFileInfo::exists(marker_file(path)));
    CHECK(QFile::remove(path));
  }

  // A canvas past the pixel cap, told from the header: an XCF and a PSD of 9000 x 9000 whose
  // pixel data is not even there.
  QByteArray xcf_header("gimp xcf v011\0", 14);
  put_u32(xcf_header, 9000);
  put_u32(xcf_header, 9000);
  put_u32(xcf_header, 0);    // RGB
  put_u32(xcf_header, 150);  // 8-bit perceptual
  xcf_header.append(QByteArray(64, '\0'));  // no properties, no layers, no channels
  QByteArray psd_header("8BPS");
  put_u16(psd_header, 1);
  psd_header.append(QByteArray(6, '\0'));
  put_u16(psd_header, 3);
  put_u32(psd_header, 9000);
  put_u32(psd_header, 9000);
  put_u16(psd_header, 8);
  put_u16(psd_header, 3);
  psd_header.append(QByteArray(64, '\0'));  // empty color mode, resource and layer sections
  RecentThumbnailCache cache;
  for (const auto& path : {write_bytes(in_root("wide.xcf"), xcf_header), write_bytes(in_root("wide.psd"), psd_header)}) {
    CHECK(settled_state(cache, path) == RecentThumbnailState::unavailable);
    CHECK(wait_until_idle(cache));
    CHECK(QFileInfo::exists(marker_file(path)));
  }
}

void ui_recent_thumbnails_quick_pictures_come_before_slow_ones() {
  using namespace patchy::test::ui;
  const auto cache_dir = fresh_cache_dir(QStringLiteral("order-cache"));
  EnvironmentGuard cache_guard("PATCHY_RECENT_THUMBNAILS_DIR", cache_dir);
  const auto root = scratch_file(QStringLiteral("order"));
  CHECK(QDir().mkpath(root));
  const auto in_root = [&](const char* name) { return root + QLatin1Char('/') + QString::fromLatin1(name); };

  const auto slow_a = copy_fixture("xcf", "groups.xcf", in_root("a.xcf"));
  const auto slow_b = copy_fixture("xcf", "modes.xcf", in_root("b.xcf"));
  const auto quick_a = write_png(in_root("a.png"), QColor(10, 200, 10));
  const auto quick_b = write_png(in_root("b.png"), QColor(10, 10, 200));
  const auto removed = copy_fixture("xcf", "plain-rgba.xcf", in_root("removed.xcf"));

  RecentThumbnailCache cache;
  QStringList answered;
  QObject::connect(&cache, &RecentThumbnailCache::thumbnail_changed, &cache,
                   [&answered](const QString& path) { answered << QFileInfo(path).fileName(); });
  // Asked for in the order a grid paints them: the two layered files first. The loads of a
  // first paint go to the worker together, so the worker's own order is what answers.
  cache.request(slow_a);
  cache.request(removed);
  cache.request(slow_b);
  cache.request(quick_a);
  cache.request(quick_b);
  // An entry removed while its file waits for the slow pass: its picture is never made.
  cache.forget({removed});
  CHECK(wait_until_idle(cache));
  CHECK(process_events_until([&] { return answered.size() >= 4; }, 8000));
  CHECK(answered == QStringList({QStringLiteral("a.png"), QStringLiteral("b.png"), QStringLiteral("a.xcf"),
                                 QStringLiteral("b.xcf")}));
  for (const auto& path : {slow_a, slow_b, quick_a, quick_b}) {
    CHECK(cache.state(path) == RecentThumbnailState::ready);
  }
  CHECK(cache.loads_in_flight() == 0);
  CHECK(!QFileInfo::exists(patchy::ui::recent_thumbnail_cache_file(removed)));
  CHECK(!QFileInfo::exists(marker_file(removed)));
}

void ui_recent_thumbnails_new_kinds_read_unicode_paths() {
  using namespace patchy::test::ui;
  const auto root =
      QFileInfo(patchy::ui::to_qstring(patchy::test::unicode_artifact_dir(u8"recent-thumbnail-kinds"))).absoluteFilePath();
  const auto cache_dir = root + QStringLiteral("/") + q(patchy::test::kUnicodeDirName) + QStringLiteral(" cache");
  EnvironmentGuard cache_guard("PATCHY_RECENT_THUMBNAILS_DIR", cache_dir);
  const auto named = [&](const char* extension) {
    return root + QLatin1Char('/') + q(patchy::test::kUnicodeCombinedStem) + QLatin1Char('.') +
           QString::fromLatin1(extension);
  };

  RecentThumbnailCache cache;
  // Every way a file is opened for its picture: a reader's whole-file read (XCF), the PSD
  // front read, the zip preview (OpenRaster, Krita), the header read (Paint.NET), the SVG
  // read, and Qt PDF's own load.
  const auto xcf = copy_fixture("xcf", "groups.xcf", named("xcf"));
  stored_picture(cache, xcf, reader_canvas(xcf, "xcf"));
  CHECK(near_color(center_color(stored_picture(cache, write_psd_with_thumbnail(named("psd"), QColor(20, 150, 170)), QSize())),
                   QColor(20, 150, 170), 20));
  const auto ora = copy_fixture("ora", "python-layers.ora", named("ora"));
  stored_picture(cache, ora, reader_canvas(ora, "ora"));
  const auto kra = write_container(named("kra"), "application/x-krita",
                                   {{"mergedimage.png", to_vector(png_bytes(QColor(40, 190, 70), QSize(300, 100))), true}});
  stored_picture(cache, kra, QSize(300, 100));
  const auto pdn = copy_fixture("pdn", "blend-modes.pdn", named("pdn"));
  stored_picture(cache, pdn, reader_canvas(pdn, "pdn"));
  stored_picture(cache, write_bytes(named("svg"), QByteArray(kSvgText)), QSize(40, 20));
#ifdef PATCHY_HAVE_QT_PDF
  stored_picture(cache, write_pdf(named("pdf"), {QColor(210, 40, 40), QColor(40, 40, 210)}), QSize(400, 200));
#endif
}

std::vector<patchy::test::TestCase> start_panel_grid_tests() {
  return {
      {"ui_recent_thumbnails_picture_every_kind_patchy_opens", ui_recent_thumbnails_picture_every_kind_patchy_opens},
      {"ui_recent_thumbnails_use_the_preview_a_file_carries", ui_recent_thumbnails_use_the_preview_a_file_carries},
      {"ui_recent_thumbnails_camera_raw_and_refused_tiff_use_local_fixtures",
       ui_recent_thumbnails_camera_raw_and_refused_tiff_use_local_fixtures},
      {"ui_recent_thumbnails_damaged_files_get_a_marker_and_no_retry",
       ui_recent_thumbnails_damaged_files_get_a_marker_and_no_retry},
      {"ui_recent_thumbnails_caps_refuse_a_file_before_it_is_decoded",
       ui_recent_thumbnails_caps_refuse_a_file_before_it_is_decoded},
      {"ui_recent_thumbnails_quick_pictures_come_before_slow_ones",
       ui_recent_thumbnails_quick_pictures_come_before_slow_ones},
      {"ui_recent_thumbnails_new_kinds_read_unicode_paths", ui_recent_thumbnails_new_kinds_read_unicode_paths},
      {"ui_start_panel_recent_files_open_in_grid_by_default", ui_start_panel_recent_files_open_in_grid_by_default},
      {"ui_start_panel_grid_does_no_thumbnail_work_before_its_first_paint",
       ui_start_panel_grid_does_no_thumbnail_work_before_its_first_paint},
      {"ui_recent_thumbnails_folder_stays_inside_an_isolated_settings_folder",
       ui_recent_thumbnails_folder_stays_inside_an_isolated_settings_folder},
      {"ui_start_panel_recent_section_grows_with_a_large_window",
       ui_start_panel_recent_section_grows_with_a_large_window},
      {"ui_start_panel_recent_section_is_only_as_large_as_few_entries_need",
       ui_start_panel_recent_section_is_only_as_large_as_few_entries_need},
      {"ui_start_panel_recent_section_fits_a_small_window", ui_start_panel_recent_section_fits_a_small_window},
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
