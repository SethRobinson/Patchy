// Scan a Book to PDF (docs/book-scan.md): the wizard driven offscreen through the test
// scanner (PATCHY_FAKE_BOOK_SCANNER), the project manifest, page order, the PDF it
// writes, and app.buildBookPdf.
#include "core/book_scan.hpp"
#include "ui/book_pdf_export.hpp"
#include "ui/book_scan_dialog.hpp"
#include "ui/book_scan_project.hpp"
#include "ui/main_window.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

#include <cmath>
#include <functional>
#include <string>
#include <vector>

using patchy::test::ui::process_events_until;
using patchy::test::ui::scripting_support::backlog_contains;
using patchy::test::ui::scripting_support::run_script;
using patchy::test::ui::show_window;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kDpi = 300;

struct EnvGuard {
  EnvGuard(const char* name, const QByteArray& value) : name_(name) { qputenv(name, value); }
  ~EnvGuard() { qunsetenv(name_); }
  EnvGuard(const EnvGuard&) = delete;
  EnvGuard& operator=(const EnvGuard&) = delete;

private:
  const char* name_;
};

bool inside(const patchy::OrientedBox& box, double x, double y, double& local_x, double& local_y) {
  const double radians = box.angle_degrees * kPi / 180.0;
  const double c = std::cos(radians);
  const double s = std::sin(radians);
  const double dx = x - box.center_x;
  const double dy = y - box.center_y;
  local_x = dx * c + dy * s + box.width / 2.0;
  local_y = -dx * s + dy * c + box.height / 2.0;
  return local_x >= 0.0 && local_y >= 0.0 && local_x < box.width && local_y < box.height;
}

// A whole-bed scan (the test scanner takes each image as the bed at the requested
// resolution): a dark open-lid background with the book on it. `spread` true draws
// two pages with a fold shadow and text lines; false draws a colored cover.
QString write_bed(const QString& folder, const QString& name, int width, int height, const patchy::OrientedBox& book,
                  bool spread, int marker) {
  QImage image(width, height, QImage::Format_RGB888);
  image.fill(QColor(22, 22, 22));
  for (int y = 0; y < height; ++y) {
    auto* row = image.scanLine(y);
    for (int x = 0; x < width; ++x) {
      double lx = 0.0;
      double ly = 0.0;
      if (!inside(book, x + 0.5, y + 0.5, lx, ly)) {
        continue;
      }
      int r = 40;
      int g = 70;
      int b = 150;
      if (spread) {
        const double fold = std::abs(lx - book.width / 2.0) / book.width;
        int value = fold < 0.02 ? 150 + static_cast<int>(fold * 4000.0) : 238;
        const double page_x = std::fmod(lx, book.width / 2.0) / (book.width / 2.0);
        if (page_x > 0.15 && page_x < 0.85 && ly > book.height * 0.15 && ly < book.height * 0.85 &&
            static_cast<int>(ly) % 16 < 4) {
          value = 40;
        }
        // A page-number block in the top outer corner tells the pages apart.
        if (ly < book.height * 0.08 && ((lx < book.width * 0.08) || (lx > book.width * 0.92))) {
          value = marker;
        }
        r = g = b = std::min(value, 255);
      }
      row[x * 3] = static_cast<uchar>(r);
      row[x * 3 + 1] = static_cast<uchar>(g);
      row[x * 3 + 2] = static_cast<uchar>(b);
    }
  }
  const QString path = QDir(folder).filePath(name);
  image.save(path, "PNG");
  return path;
}

struct BookFixture {
  QString project_folder;
  QString inputs;
};

QString fresh_artifact_folder(const QString& name) {
  patchy::test::ui::ensure_artifact_dir();
  const QString folder = QDir::current().filePath(QStringLiteral("test-artifacts/") + name);
  if (QDir(folder).exists()) {
    patchy::test::ui::remove_test_scratch_dir(folder);
  }
  QDir().mkpath(folder);
  return folder;
}

// Cover plus `spreads` spreads, 3 x 2 inch bed at 300 dpi; the open book is 2.4 x 1.6
// inches (two 1.2 x 1.6 inch pages).
QStringList write_book_inputs(const QString& folder, int spreads) {
  QStringList files;
  files << write_bed(folder, QStringLiteral("cover.png"), 900, 600, {450.0, 300.0, 720.0, 480.0, 0.0}, false, 0);
  for (int i = 0; i < spreads; ++i) {
    const double shift = (i % 2 == 0 ? -1.0 : 1.0) * 12.0 * (i + 1);
    const double angle = i == 1 ? 1.5 : 0.0;
    files << write_bed(folder, QStringLiteral("spread-%1.png").arg(i + 1), 900, 600,
                       {450.0 + shift, 300.0 - shift / 2.0, 720.0, 480.0, angle}, true, 60 + i * 20);
  }
  return files;
}

void wait_for_scan(patchy::ui::BookScanDialog& dialog) {
  CHECK(process_events_until([&dialog] { return !dialog.scan_in_progress(); }, 10000));
}

// Runs the wizard to the review step with the given inputs. `capture` saves each step
// as a test artifact under that prefix.
void scan_book(patchy::ui::BookScanDialog& dialog, const QString& project_folder, int spreads,
               const std::string& capture = {}) {
  dialog.set_quiet(true);
  if (!capture.empty()) {
    dialog.show();
  }
  dialog.set_project_folder(project_folder);
  dialog.set_title(QStringLiteral("Test Book"));
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Cover);
  if (auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanResolutionCombo"))) {
    resolution->setCurrentIndex(resolution->findData(kDpi));
  }
  if (!capture.empty()) {
    patchy::test::ui::save_widget_artifact(capture + "_cover", dialog);
  }
  dialog.start_page_scanning();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
  for (int i = 0; i < spreads; ++i) {
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty());
  }
  if (!capture.empty()) {
    patchy::test::ui::save_widget_artifact(capture + "_scanning", dialog);
  }
  dialog.finish_scanning();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Review);
  if (!capture.empty()) {
    patchy::test::ui::save_widget_artifact(capture + "_review", dialog);
  }
}

QStringList media_boxes(const QByteArray& pdf) {
  QStringList boxes;
  const QRegularExpression pattern(QStringLiteral(R"(/MediaBox \[([^\]]*)\])"));
  auto it = pattern.globalMatch(QString::fromLatin1(pdf));
  while (it.hasNext()) {
    boxes << it.next().captured(1);
  }
  return boxes;
}

QString utf16_hex(const QString& text) {
  QString hex = QStringLiteral("FEFF");
  for (const QChar c : text) {
    hex += QStringLiteral("%1").arg(static_cast<uint>(c.unicode()), 4, 16, QLatin1Char('0')).toUpper();
  }
  return hex;
}

QByteArray read_file(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void ui_book_scan_menu_actions_registered() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* scan_action = window.findChild<QAction*>(QStringLiteral("fileImportScanBookAction"));
  auto* resume_action = window.findChild<QAction*>(QStringLiteral("fileImportResumeBookScanAction"));
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
  CHECK(scan_action != nullptr);
  CHECK(resume_action != nullptr);
  auto* import_menu = window.findChild<QMenu*>(QStringLiteral("fileImportMenu"));
  CHECK(import_menu != nullptr && import_menu->actions().contains(scan_action));
  const auto* command = window.hotkey_registry().find_command(QStringLiteral("file.import_scan_book_pdf"));
  CHECK(command != nullptr && command->action == scan_action);
  const auto* resume = window.hotkey_registry().find_command(QStringLiteral("file.import_resume_book_scan"));
  CHECK(resume != nullptr && resume->action == resume_action);
  CHECK(scan_action->menuRole() == QAction::NoRole);
#else
  CHECK(scan_action == nullptr);
  CHECK(resume_action == nullptr);
#endif
}

void ui_book_scan_full_flow_exports_uniform_pages() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 3).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr, std::nullopt);
  scan_book(dialog, project, 3, "ui_book_scan_step");
  const auto& book = dialog.project();
  CHECK(book.cover.has_value());
  CHECK(book.scans.size() == 3);
  CHECK(book.mode == patchy::ui::BookScanMode::TwoPages);
  // The cover outline was found on the dark background: 2.4 x 1.6 inches.
  CHECK(std::abs(book.cover->layout.outline.width / book.cover->dpi - 2.4) < 0.05);
  // Every spread found its fold near the middle and its own tilt.
  for (const auto& scan : book.scans) {
    CHECK(std::abs(scan.layout.gutter - 0.5) < 0.02);
  }
  CHECK(std::abs(book.scans[1].layout.spread.angle_degrees - 1.5) < 0.5);
  const auto size = patchy::ui::resolved_page_size(book);
  CHECK(std::abs(size.width - 1.2) < 0.05);
  CHECK(std::abs(size.height - 1.6) < 0.05);
  // Front cover, six pages, back cover.
  const auto order = patchy::ui::book_page_order(book);
  CHECK(order.size() == 8);
  CHECK(order.front().kind == patchy::ui::BookPageRef::Kind::FrontCover);
  CHECK(order.back().kind == patchy::ui::BookPageRef::Kind::BackCover);
  CHECK(order[1].scan == 0 && order[1].side == patchy::BookPageSide::Left);
  CHECK(order[2].scan == 0 && order[2].side == patchy::BookPageSide::Right);

  const QString pdf_path = QDir(inputs).filePath(QStringLiteral("book.pdf"));
  CHECK(dialog.export_pdf(pdf_path, false));
  const QByteArray pdf = read_file(pdf_path);
  const QStringList boxes = media_boxes(pdf);
  CHECK(boxes.size() == 8);
  CHECK(QSet<QString>(boxes.begin(), boxes.end()).size() == 1);  // every page the same size
  CHECK(pdf.contains(utf16_hex(QStringLiteral("Test Book")).toLatin1()));
  CHECK(pdf.contains("/DCTDecode"));

  // The first page of the first spread starts with its fore-edge marker in the top-left
  // corner (gray 60), the right page with it in the top-right corner.
  patchy::ui::BookScanCache cache(book);
  const auto left = patchy::ui::render_book_page(book, order[1], size, 100, cache);
  const auto right = patchy::ui::render_book_page(book, order[2], size, 100, cache);
  CHECK(left.width() == 120 && left.height() == 160);
  CHECK(std::abs(static_cast<int>(left.pixel(3, 3)[0]) - 60) < 25);
  CHECK(std::abs(static_cast<int>(right.pixel(right.width() - 4, 3)[0]) - 60) < 25);
  // The tilted spread comes out straight: a text line's row is dark at both its ends.
  const auto tilted = patchy::ui::render_book_page(book, order[3], size, 300, cache, false);
  patchy::test::ui::save_image_artifact("ui_book_scan_tilted_page", patchy::ui::book_image_from_pixels(tilted));
  int straight_rows = 0;
  for (std::int32_t y = tilted.height() / 4; y < tilted.height() * 3 / 4; ++y) {
    const auto at = [&](std::int32_t x) { return static_cast<int>(tilted.pixel(x, y)[0]); };
    if (at(tilted.width() * 3 / 10) < 120 && at(tilted.width() * 7 / 10) < 120) {
      ++straight_rows;
    }
  }
  CHECK(straight_rows > 40);
}

void ui_book_scan_black_and_white_and_script_export() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_bw_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_bw_project")) + QStringLiteral("/book");
  {
    const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 2).join(QDir::listSeparator()).toUtf8());
    const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
    patchy::ui::BookScanDialog dialog(nullptr, std::nullopt);
    scan_book(dialog, project, 2);
    auto* output = dialog.findChild<QComboBox*>(QStringLiteral("bookScanOutputCombo"));
    CHECK(output != nullptr);
    output->setCurrentIndex(2);  // Black and white
    CHECK(dialog.project().output.mode == patchy::BookOutputMode::BlackAndWhite);
    const QString pdf_path = QDir(inputs).filePath(QStringLiteral("bw.pdf"));
    CHECK(dialog.export_pdf(pdf_path, false));
    const QByteArray pdf = read_file(pdf_path);
    CHECK(pdf.contains("/BitsPerComponent 1"));
    CHECK(media_boxes(pdf).size() == 6);
  }
  // The saved project exports again from a script, with overrides.
  patchy::ui::MainWindow window;
  show_window(window);
  const QString script_pdf = QDir(inputs).filePath(QStringLiteral("script.pdf"));
  const auto json_string = [](const QString& text) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).chopped(1).mid(1);
  };
  CHECK(run_script(window, QStringLiteral(R"JS(
    var pages = app.buildBookPdf(%1, %2, { mode: 'gray', pageSize: [1.25, 1.75], dpi: 150, title: 'Scripted' });
    if (pages !== 6) throw new Error('pages ' + pages);
    var threw = false;
    try { app.buildBookPdf(%1, %2, { mode: 'sepia' }); } catch (e) { threw = true; }
    if (!threw) throw new Error('bad mode accepted');
    threw = false;
    try { app.buildBookPdf(%2, %2); } catch (e) { threw = true; }
    if (!threw) throw new Error('non-project folder accepted');
    console.log('book-ok');
  )JS")
                                  .arg(json_string(project), json_string(script_pdf))));
  CHECK(backlog_contains(window, QStringLiteral("book-ok")));
  const QByteArray pdf = read_file(script_pdf);
  CHECK(pdf.contains("/DeviceGray"));
  CHECK(pdf.contains("/MediaBox [0 0 90 126]"));
  CHECK(pdf.contains(utf16_hex(QStringLiteral("Scripted")).toLatin1()));
}

void ui_book_scan_resume_and_rescan() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_resume_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_resume_project")) + QStringLiteral("/book");
  QStringList files = write_book_inputs(inputs, 2);
  // A third spread image serves the rescan.
  files << files.last();
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  QByteArray saved_json;
  {
    patchy::ui::BookScanDialog dialog(nullptr, std::nullopt);
    dialog.set_quiet(true);
    dialog.set_project_folder(project);
    dialog.start_cover_scan();
    wait_for_scan(dialog);
    dialog.start_page_scanning();
    dialog.start_page_scan();
    wait_for_scan(dialog);
    dialog.start_page_scan();
    wait_for_scan(dialog);
    const QString second = dialog.project().scans.back().file;
    dialog.rescan_last();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty());
    // The rescan replaced the last spread under its own name.
    CHECK(dialog.project().scans.size() == 2);
    CHECK(dialog.project().scans.back().file == second);
    CHECK(QFile::exists(QDir(project).filePath(second)));
    CHECK(QDir(project).entryList({QStringLiteral("*-new.png")}, QDir::Files).isEmpty());
    saved_json = patchy::ui::book_project_json(dialog.project());
    // Closing mid-scan keeps the project for Resume.
  }
  QString error;
  auto loaded = patchy::ui::load_book_project(project, &error);
  CHECK(loaded.has_value());
  if (!loaded) {
    return;
  }
  CHECK(error.isEmpty());
  CHECK(patchy::ui::book_project_json(*loaded) == saved_json);
  patchy::ui::BookScanDialog resumed(nullptr, std::move(loaded));
  CHECK(resumed.step() == patchy::ui::BookScanDialog::Step::Scanning);
  CHECK(resumed.project().scans.size() == 2);

  // Not a book folder: a clear error, nothing created.
  QString not_book_error;
  CHECK(!patchy::ui::load_book_project(inputs, &not_book_error).has_value());
  CHECK(!not_book_error.isEmpty());
  // A manifest naming a file outside its folder is refused.
  QByteArray hostile = saved_json;
  hostile.replace("scans/spread-0001.png", "../../outside.png");
  CHECK(!patchy::ui::book_project_from_json(hostile, &not_book_error).has_value());
}

void ui_book_scan_one_page_mode_offered_for_wide_book() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_wide_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_wide_project")) + QStringLiteral("/book");
  // A 2 x 2 inch bed cannot hold the 2.4 inch open book.
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 0).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "2x2");
  patchy::ui::BookScanDialog dialog(nullptr, std::nullopt);
  dialog.set_quiet(true);
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  CHECK(dialog.project().mode == patchy::ui::BookScanMode::OnePage);
  auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("bookScanModeCombo"));
  CHECK(mode != nullptr && mode->currentIndex() == 1);
}

void ui_book_scan_rtl_orders_pages() {
  patchy::ui::BookProject project;
  project.include_covers = true;
  project.cover = patchy::ui::BookCover{};
  project.scans.resize(2);
  project.scans[1].use = patchy::ui::BookPageUse::LeftOnly;
  project.direction = patchy::ui::BookReadingDirection::RightToLeft;
  const auto order = patchy::ui::generated_page_order(project);
  CHECK(order.size() == 5);
  CHECK(order[0].kind == patchy::ui::BookPageRef::Kind::FrontCover);
  CHECK(order[1].scan == 0 && order[1].side == patchy::BookPageSide::Right);
  CHECK(order[2].scan == 0 && order[2].side == patchy::BookPageSide::Left);
  CHECK(order[3].scan == 1 && order[3].side == patchy::BookPageSide::Left);
  CHECK(order[4].kind == patchy::ui::BookPageRef::Kind::BackCover);
  // A custom order wins, and a page's own turn round-trips through the manifest.
  project.custom_order = std::vector<patchy::ui::BookPageRef>{
      {patchy::ui::BookPageRef::Kind::Scan, 1, patchy::BookPageSide::Left, 2},
      {patchy::ui::BookPageRef::Kind::Blank, -1, patchy::BookPageSide::Left, 0}};
  project.cover->file = QStringLiteral("scans/cover.png");
  for (auto& scan : project.scans) {
    scan.file = QStringLiteral("scans/x.png");
  }
  const auto reloaded = patchy::ui::book_project_from_json(patchy::ui::book_project_json(project), nullptr);
  CHECK(reloaded.has_value());
  if (reloaded) {
    CHECK(patchy::ui::book_page_order(*reloaded) == *project.custom_order);
    CHECK(reloaded->direction == patchy::ui::BookReadingDirection::RightToLeft);
  }
}

void ui_unicode_book_scan_project_folder() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_unicode_inputs"));
  const QString base = fresh_artifact_folder(QStringLiteral("ui_book_scan_unicode_project"));
  const QString project =
      QDir(base).filePath(QString::fromUtf8(reinterpret_cast<const char*>(patchy::test::kUnicodeDirName.data()),
                                            static_cast<qsizetype>(patchy::test::kUnicodeDirName.size())));
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 1).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr, std::nullopt);
  scan_book(dialog, project, 1);
  CHECK(QFile::exists(QDir(project).filePath(QStringLiteral("book.json"))));
  CHECK(patchy::ui::load_book_project(project, nullptr).has_value());
  const QString pdf_path = QDir(project).filePath(QString::fromUtf8(
      reinterpret_cast<const char*>(patchy::test::kUnicodeDirName.data()),
      static_cast<qsizetype>(patchy::test::kUnicodeDirName.size())) + QStringLiteral(".pdf"));
  CHECK(dialog.export_pdf(pdf_path, false));
  CHECK(media_boxes(read_file(pdf_path)).size() == 4);
}

}  // namespace

std::vector<patchy::test::TestCase> book_scan_ui_tests() {
  return {
      {"ui_book_scan_menu_actions_registered", ui_book_scan_menu_actions_registered},
      {"ui_book_scan_full_flow_exports_uniform_pages", ui_book_scan_full_flow_exports_uniform_pages},
      {"ui_book_scan_black_and_white_and_script_export", ui_book_scan_black_and_white_and_script_export},
      {"ui_book_scan_resume_and_rescan", ui_book_scan_resume_and_rescan},
      {"ui_book_scan_one_page_mode_offered_for_wide_book", ui_book_scan_one_page_mode_offered_for_wide_book},
      {"ui_book_scan_rtl_orders_pages", ui_book_scan_rtl_orders_pages},
      {"ui_unicode_book_scan_project_folder", ui_unicode_book_scan_project_folder},
  };
}
