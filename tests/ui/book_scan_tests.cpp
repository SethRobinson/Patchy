// Scan a Book to PDF (docs/book-scan.md): the wizard driven offscreen through the test
// scanner (PATCHY_FAKE_BOOK_SCANNER), the project manifest, page order, the PDF it
// writes, and app.buildBookPdf.
#include "core/book_scan.hpp"
#include "formats/pdf_content.hpp"
#include "formats/pdf_file.hpp"
#include "ocr/ocr_fake_engine.hpp"
#include "ocr_test_support.hpp"
#include "ui/app_settings.hpp"
#include "ui/book_pdf_export.hpp"
#include "ui/book_scan_dialog.hpp"
#include "ui/book_scan_project.hpp"
#include "ui/main_window.hpp"
#include "ui/theme_manager.hpp"
#include "ui/theme_qss.hpp"
#include "scripting_test_support.hpp"
#include "test_harness.hpp"
#include "ui_test_access.hpp"
#include "ui_test_groups.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QCheckBox>
#include <QFileInfo>
#include <QFontInfo>
#include <QFontMetrics>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QTransform>
#include <QtGlobal>

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
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
  if (!capture.empty()) {
    QApplication::processEvents();
    patchy::test::ui::save_widget_artifact(capture + "_setup", dialog);
  }
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

// Cancel Scan answers at once (the button greys out as "Cancelling...", the status says so, and
// a "Cancelling Scan" notice opens), and all of it goes away when the scanner reports back.
void ui_book_scan_cancel_shows_feedback() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_cancel_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_cancel_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 1).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.set_title(QStringLiteral("Cancel Book"));
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  dialog.start_page_scanning();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
  auto* cancel = dialog.findChild<QPushButton*>(QStringLiteral("bookScanCancelButton"));
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("bookScanStatusLabel"));
  CHECK(cancel != nullptr && status != nullptr);
  if (cancel == nullptr || status == nullptr) {
    return;
  }
  const auto notice = [&dialog] {
    return dialog.findChild<QProgressDialog*>(QStringLiteral("bookScanCancellingDialog"));
  };
  {
    const EnvGuard slow("PATCHY_FAKE_BOOK_SCANNER_DELAY_MS", "400");
    dialog.start_page_scan();
    CHECK(dialog.scan_in_progress() && cancel->isVisible() && cancel->isEnabled());
    cancel->click();
    CHECK(dialog.scan_in_progress());
    CHECK(!cancel->isEnabled() && cancel->text() == QStringLiteral("Cancelling..."));
    CHECK(status->text() == QStringLiteral("Cancelling the scan..."));
    CHECK(notice() != nullptr && notice()->isVisible());
    if (notice() != nullptr) {
      patchy::test::ui::save_widget_artifact("ui_book_scan_cancelling_notice", *notice());
    }
    wait_for_scan(dialog);
  }
  CHECK(process_events_until([&notice] { return notice() == nullptr; }));
  CHECK(status->text() == QStringLiteral("Scan cancelled."));
  CHECK(!cancel->isVisible() && cancel->isEnabled() && cancel->text() == QStringLiteral("Cancel Scan"));
  CHECK(dialog.project().scans.empty());
  // The next scan runs as usual and gets the image the cancelled one did not use.
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 1U);
  dialog.reject();
}

// The setup step: the step bar, and the guide card with its picture and numbered steps for a
// new book (hidden when the folder already holds one), in Dark and Light.
void ui_book_scan_setup_page_guides_the_cover_scan() {
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_setup_project")) + QStringLiteral("/book");
  // The test scanner, listed but never asked to scan here.
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", "unused.png");
  auto& manager = patchy::ui::ThemeManager::instance();
  const patchy::ui::ColorSchemePreference entry_preference = manager.preference();
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.set_title(QStringLiteral("Field Notes"));
  auto* bar = dialog.findChild<QWidget*>(QStringLiteral("bookScanStepBar"));
  auto* guide = dialog.findChild<QWidget*>(QStringLiteral("bookScanSetupGuide"));
  auto* picture = dialog.findChild<QWidget*>(QStringLiteral("bookScanPlacementPicture"));
  auto* heading = dialog.findChild<QLabel*>(QStringLiteral("bookScanGuideHeading"));
  auto* cover = dialog.findChild<QPushButton*>(QStringLiteral("bookScanCoverButton"));
  CHECK(bar != nullptr && guide != nullptr && picture != nullptr && heading != nullptr && cover != nullptr);
  if (bar == nullptr || guide == nullptr || picture == nullptr || heading == nullptr || cover == nullptr) {
    return;
  }
  QApplication::processEvents();
  CHECK(bar->isVisible() && guide->isVisible() && picture->isVisible());
  CHECK(heading->text() == QStringLiteral("Scan the cover first"));
  // Large where it is drawn: the page's sheet, not a setFont the application sheet replaces.
  CHECK(QFontInfo(heading->font()).pixelSize() >= 20);
  CHECK(cover->isVisible() && cover->isEnabled() && cover->isDefault());
  // The bold label fits its button: it was drawn wider than the size Qt gave the button.
  QFont bold = cover->font();
  bold.setBold(true);
  CHECK(cover->width() >= QFontMetrics(bold).horizontalAdvance(cover->text()) + 2 * 28);
  CHECK(bar->accessibleName() == QStringLiteral("1. Set up and scan the cover"));
  patchy::test::ui::save_widget_artifact("ui_book_scan_setup_dark", dialog);
  // MainWindow re-resolves every window's themed sheets on a scheme change; here the test does.
  manager.set_preference(patchy::ui::ColorSchemePreference::Light, /*persist=*/false);
  patchy::ui::rebuild_themed_styles_in(dialog);
  QApplication::processEvents();
  patchy::test::ui::save_widget_artifact("ui_book_scan_setup_light", dialog);
  manager.set_preference(entry_preference, /*persist=*/false);
  patchy::ui::rebuild_themed_styles_in(dialog);
  QApplication::processEvents();
  // A folder that holds a book hides the guide: Continue This Book is the next step.
  patchy::ui::BookProject book;
  book.folder = project;
  book.title = QStringLiteral("Field Notes");
  QDir().mkpath(project);
  patchy::ui::save_book_project(book);
  dialog.set_project_folder(QString());
  dialog.set_project_folder(project);
  CHECK(guide->isHidden());
  dialog.reject();
}

// Covers one at a time (a book too large to lie open on the glass): the setup guide shows
// the two-pane picture and its steps, the cover step asks for the back cover after the
// front, each cover is its own page, and the choice is kept with the book.
void ui_book_scan_covers_one_at_a_time() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_separate_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_separate_project")) + QStringLiteral("/book");
  // The closed book is 1.2 x 1.6 inches, like one page of the test spreads.
  QStringList files;
  files << write_bed(inputs, QStringLiteral("front.png"), 900, 600, {420.0, 300.0, 360.0, 480.0, 0.0}, false, 0);
  files << write_bed(inputs, QStringLiteral("back.png"), 900, 600, {480.0, 290.0, 360.0, 480.0, 0.0}, false, 0);
  files << write_book_inputs(inputs, 2).mid(1);
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  auto settings = patchy::ui::app_settings();
  const QVariant entry_style = settings.value(QStringLiteral("bookScan/coverStyle"));
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.set_title(QStringLiteral("Atlas"));
  auto* style = dialog.findChild<QComboBox*>(QStringLiteral("bookScanCoverStyleCombo"));
  auto* heading = dialog.findChild<QLabel*>(QStringLiteral("bookScanGuideHeading"));
  CHECK(style != nullptr && heading != nullptr);
  if (style == nullptr || heading == nullptr) {
    return;
  }
  style->setCurrentIndex(1);  // One cover at a time
  CHECK(dialog.project().cover_style == patchy::ui::BookCoverStyle::Separate);
  CHECK(heading->text() == QStringLiteral("Scan the covers first"));
  QApplication::processEvents();
  patchy::test::ui::save_widget_artifact("ui_book_scan_separate_setup", dialog);

  // The front cover, then the cover step asks for the back cover.
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty() && dialog.step() == patchy::ui::BookScanDialog::Step::Cover);
  auto* back_button = dialog.findChild<QPushButton*>(QStringLiteral("bookScanBackCoverButton"));
  auto* side = dialog.findChild<QComboBox*>(QStringLiteral("bookScanCoverSideCombo"));
  auto* hint = dialog.findChild<QLabel*>(QStringLiteral("bookScanCoverHint"));
  auto* covers = dialog.findChild<QCheckBox*>(QStringLiteral("bookScanCoversCheck"));
  CHECK(back_button != nullptr && side != nullptr && hint != nullptr && covers != nullptr);
  if (back_button == nullptr || side == nullptr || hint == nullptr || covers == nullptr || !dialog.project().cover) {
    return;
  }
  CHECK(!dialog.project().back_cover.has_value());
  CHECK(back_button->isVisible() && hint->text().contains(QStringLiteral("Scan Back Cover")));
  // One cover does not show where the open book lies: the pages scan the whole glass.
  if (auto* area = dialog.findChild<QComboBox*>(QStringLiteral("bookScanAreaCombo"))) {
    CHECK(!area->isEnabled() && area->currentIndex() == 1);
  }
  CHECK(!dialog.project().book_area_inches.has_value());
  const auto& front = dialog.project().cover->layout.outline;
  CHECK(std::abs(front.width - 360.0) < 10.0 && std::abs(front.height - 480.0) < 10.0);
  patchy::test::ui::save_widget_artifact("ui_book_scan_separate_front", dialog);
  dialog.start_back_cover_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty() && dialog.project().back_cover.has_value());
  CHECK(dialog.back_cover_shown() && side->isVisible() && side->currentIndex() == 1);
  CHECK(!back_button->isVisible());
  if (dialog.project().back_cover) {
    CHECK(dialog.project().back_cover->file == QStringLiteral("scans/back-cover.png"));
    CHECK(std::abs(dialog.project().back_cover->layout.outline.center_x - 480.0) < 6.0);
  }
  patchy::test::ui::save_widget_artifact("ui_book_scan_separate_back", dialog);
  // The cover page size is one cover, not half of it.
  const auto cover_size = patchy::ui::book_cover_page_size(dialog.project());
  CHECK(cover_size && std::abs(cover_size->width - 1.2) < 0.05 && std::abs(cover_size->height - 1.6) < 0.05);
  side->setCurrentIndex(0);
  CHECK(!dialog.back_cover_shown());
  covers->setChecked(true);
  if (auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanResolutionCombo"))) {
    resolution->setCurrentIndex(resolution->findData(kDpi));
  }

  dialog.start_page_scanning();
  for (int i = 0; i < 2; ++i) {
    dialog.start_page_scan();
    wait_for_scan(dialog);
  }
  CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 2U);
  const auto order = patchy::ui::book_page_order(dialog.project());
  CHECK(order.size() == 6U);
  if (order.size() == 6U) {
    CHECK(order.front().kind == patchy::ui::BookPageRef::Kind::FrontCover);
    CHECK(order.back().kind == patchy::ui::BookPageRef::Kind::BackCover);
    const patchy::BookPageSize size = patchy::ui::resolved_page_size(dialog.project());
    const auto front_source = patchy::ui::book_page_source(dialog.project(), order.front(), size);
    const auto back_source = patchy::ui::book_page_source(dialog.project(), order.back(), size);
    CHECK(front_source && front_source->file == QStringLiteral("scans/cover.png"));
    CHECK(back_source && back_source->file == QStringLiteral("scans/back-cover.png"));
  }
  // Each cover is picked on its own in the scanning step's strip.
  dialog.show_back_cover(true);
  CHECK(dialog.cover_selected() && dialog.back_cover_shown());
  auto* strip = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPageStrip"));
  CHECK(strip != nullptr && strip->count() == 6);
  if (strip != nullptr && strip->count() == 6) {
    CHECK(strip->item(5)->text() == QStringLiteral("Back cover") && strip->item(5)->isSelected());
    CHECK(!strip->item(0)->isSelected());
  }
  patchy::test::ui::save_widget_artifact("ui_book_scan_separate_scanning", dialog);
  dialog.back_to_next_pages();
  dialog.finish_scanning();
  const QString pdf_path = QDir(project).filePath(QStringLiteral("atlas.pdf"));
  CHECK(dialog.export_pdf(pdf_path, false));
  CHECK(media_boxes(read_file(pdf_path)).size() == 6);
  // The style and the back cover are kept with the book.
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved && saved->cover_style == patchy::ui::BookCoverStyle::Separate && saved->back_cover.has_value());
  dialog.reject();
  if (entry_style.isValid()) {
    settings.setValue(QStringLiteral("bookScan/coverStyle"), entry_style);
  } else {
    settings.remove(QStringLiteral("bookScan/coverStyle"));
  }
}

QStringList turned_inputs(const QStringList& files);

// A book scanned without a cover can still say which way its top points: in the cover step
// before any scan, and in the scanning step once the first scan shows it lying sideways.
void ui_book_scan_skip_cover_sets_top_of_book() {
  // Top of the book is remembered for the next book: put the setting back afterwards.
  const patchy::test::ui::SettingsGroupRestorer book_settings(QStringLiteral("bookScan"));
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_skip_cover_inputs"));
  const QString project =
      fresh_artifact_folder(QStringLiteral("ui_book_scan_skip_cover_project")) + QStringLiteral("/book");
  // Two spreads and no cover scan, each lying with the top of the book to the right.
  const QStringList files = turned_inputs(write_book_inputs(inputs, 2).mid(1));
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "2x3");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.set_title(QStringLiteral("Sideways Book"));
  const QVariant include_covers = patchy::ui::app_settings().value(QStringLiteral("bookScan/includeCovers"));
  dialog.skip_cover();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Cover);
  CHECK(!dialog.project().cover.has_value());
  auto* cover_up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUpCombo"));
  CHECK(cover_up != nullptr && cover_up->isEnabled());
  if (cover_up == nullptr) {
    return;
  }
  // Set before the first scan, with nothing scanned yet to turn.
  cover_up->setCurrentIndex(2);  // Down
  CHECK(dialog.project().up_turns == 2);
  cover_up->setCurrentIndex(0);  // Up: the user does not know yet
  CHECK(dialog.project().up_turns == 0);
  if (auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanResolutionCombo"))) {
    resolution->setCurrentIndex(resolution->findData(kDpi));
  }
  dialog.start_page_scanning();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 1U);
  auto* up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningUpCombo"));
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("bookScanStatusLabel"));
  CHECK(up != nullptr && status != nullptr);
  if (up == nullptr || status == nullptr || dialog.project().scans.empty()) {
    return;
  }
  // The first scan shows the book on its side: the scanning step turns it upright.
  CHECK(!up->isHidden() && up->isEnabled());
  patchy::test::ui::save_widget_artifact("ui_book_scan_skip_cover_sideways", dialog);
  up->setCurrentIndex(1);  // Right
  CHECK(dialog.project().up_turns == 3);
  CHECK(status->text() == QStringLiteral("Every scan of this book was turned."));
  const auto check_upright = [&dialog](std::size_t index) {
    const auto& spread = dialog.project().scans[index].layout.spread;
    CHECK(std::abs(spread.width - 720.0) < 20.0 && std::abs(spread.height - 480.0) < 20.0);
    CHECK(std::abs(spread.angle_degrees) < 3.0);
    CHECK(std::abs(dialog.project().scans[index].layout.gutter - 0.5) < 0.03);
  };
  check_upright(0);
  patchy::test::ui::save_widget_artifact("ui_book_scan_skip_cover_turned", dialog);
  // Tilt reaches a quarter turn either way, so one page laid sideways can be turned alone.
  auto* tilt = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("bookScanSpreadTiltSpin"));
  auto* cover_tilt = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("bookScanCoverTiltSpin"));
  CHECK(tilt != nullptr && cover_tilt != nullptr);
  if (tilt != nullptr && cover_tilt != nullptr) {
    CHECK(tilt->minimum() == -90.0 && tilt->maximum() == 90.0);
    CHECK(cover_tilt->minimum() == -90.0 && cover_tilt->maximum() == 90.0);
    dialog.select_scan_for_editing(0);
    const double before = dialog.project().scans[0].layout.spread.angle_degrees;
    tilt->setValue(90.0);
    CHECK(dialog.project().scans[0].layout.spread.angle_degrees == 90.0);
    tilt->setValue(before);
    CHECK(dialog.project().scans[0].layout.spread.angle_degrees == before);
    dialog.back_to_next_pages();
  }
  // The next scan comes in upright, and the turn is saved with the book.
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 2U);
  if (dialog.project().scans.size() == 2U) {
    check_upright(1);
  }
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->up_turns == 3 && !saved->cover.has_value());
  // A book without a cover leaves the next book's Include covers choice alone.
  CHECK(patchy::ui::app_settings().value(QStringLiteral("bookScan/includeCovers")) == include_covers);
  dialog.reject();
}

void ui_book_scan_menu_actions_registered() {
  patchy::ui::MainWindow window;
  show_window(window);
  auto* scan_action = window.findChild<QAction*>(QStringLiteral("fileImportScanBookAction"));
  // Continuing a book is part of the wizard's setup step, not a menu command of its own.
  CHECK(window.findChild<QAction*>(QStringLiteral("fileImportResumeBookScanAction")) == nullptr);
  CHECK(window.hotkey_registry().find_command(QStringLiteral("file.import_resume_book_scan")) == nullptr);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
  CHECK(scan_action != nullptr);
  auto* import_menu = window.findChild<QMenu*>(QStringLiteral("fileImportMenu"));
  CHECK(import_menu != nullptr && import_menu->actions().contains(scan_action));
  const auto* command = window.hotkey_registry().find_command(QStringLiteral("file.import_scan_book_pdf"));
  CHECK(command != nullptr && command->action == scan_action);
  CHECK(scan_action->menuRole() == QAction::NoRole);
#else
  CHECK(scan_action == nullptr);
#endif
}

void ui_book_scan_full_flow_exports_uniform_pages() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 3).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
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
    patchy::ui::BookScanDialog dialog(nullptr);
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
    patchy::ui::BookScanDialog dialog(nullptr);
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
  {
    // The next wizard opens on the same folder and offers to continue its book.
    patchy::ui::BookScanDialog resumed(nullptr);
    resumed.set_quiet(true);
    auto* folder_edit = resumed.findChild<QLineEdit*>(QStringLiteral("bookScanFolderEdit"));
    CHECK(folder_edit != nullptr && QDir::fromNativeSeparators(folder_edit->text()) == QDir(project).absolutePath());
    auto* continue_button = resumed.findChild<QPushButton*>(QStringLiteral("bookScanContinueButton"));
    auto* cover_button = resumed.findChild<QPushButton*>(QStringLiteral("bookScanCoverButton"));
    CHECK(continue_button != nullptr && !continue_button->isHidden());
    CHECK(cover_button != nullptr && cover_button->isHidden());
    auto* status = resumed.findChild<QLabel*>(QStringLiteral("bookScanFolderStatus"));
    CHECK(status != nullptr && status->text().contains(QStringLiteral("3")));
    resumed.show();
    patchy::test::ui::save_widget_artifact("ui_book_scan_step_setup_continue", resumed);
    resumed.continue_book();
    CHECK(resumed.last_error().isEmpty());
    CHECK(resumed.step() == patchy::ui::BookScanDialog::Step::Scanning);
    CHECK(resumed.project().scans.size() == 2);
  }

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
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  // The cover cannot show whether a book that fills the glass fits open: two pages per
  // scan stays, with a notice offering one page per scan.
  CHECK(dialog.project().mode == patchy::ui::BookScanMode::TwoPages);
  auto* notice = dialog.findChild<QLabel*>(QStringLiteral("bookScanModeNotice"));
  CHECK(notice != nullptr && !notice->text().isEmpty());
  auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("bookScanModeCombo"));
  CHECK(mode != nullptr && mode->currentIndex() == 0);
  if (mode != nullptr) {
    mode->setCurrentIndex(1);
  }
  CHECK(dialog.project().mode == patchy::ui::BookScanMode::OnePage);
}

// Pages per scan changed after scanning: the scans so far are read again in the new mode.
void ui_book_scan_switch_pages_per_scan_redetects() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_switch_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_switch_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 2).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  dialog.start_page_scanning();
  for (int i = 0; i < 2; ++i) {
    dialog.start_page_scan();
    wait_for_scan(dialog);
  }
  auto* pages = dialog.findChild<QComboBox*>(QStringLiteral("bookScanPagesCombo"));
  auto* use = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUseCombo"));
  CHECK(pages != nullptr && pages->currentIndex() == 0 && use != nullptr && !use->isHidden());
  CHECK(patchy::ui::book_page_order(dialog.project()).size() == 6);
  if (pages == nullptr || use == nullptr) {
    return;
  }
  pages->setCurrentIndex(1);
  CHECK(dialog.project().mode == patchy::ui::BookScanMode::OnePage);
  CHECK(use->isHidden());
  // Each scan is one page now: its box is the whole open book on the dark bed.
  CHECK(patchy::ui::book_page_order(dialog.project()).size() == 4);
  CHECK(std::abs(dialog.project().scans[0].layout.spread.width / dialog.project().scans[0].dpi - 2.4) < 0.1);
  pages->setCurrentIndex(0);
  CHECK(dialog.project().mode == patchy::ui::BookScanMode::TwoPages);
  CHECK(!use->isHidden());
  CHECK(patchy::ui::book_page_order(dialog.project()).size() == 6);
  for (const auto& scan : dialog.project().scans) {
    CHECK(std::abs(scan.layout.gutter - 0.5) < 0.02);
  }
}

// The two spine lines start close together; either can be dragged past the other.
void ui_book_scan_spine_lines_drag_past_each_other() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_spine_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_spine_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 0).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  QApplication::processEvents();
  auto* preview = dialog.findChild<QWidget*>(QStringLiteral("bookScanCoverPreview"));
  CHECK(preview != nullptr && dialog.project().cover.has_value());
  if (preview == nullptr || !dialog.project().cover) {
    return;
  }
  // The cover scan is 900 x 600 pixels, shown fitted in the preview.
  const double scale = std::min(preview->width() / 900.0, preview->height() / 600.0);
  const QPointF origin((preview->width() - 900.0 * scale) / 2.0, (preview->height() - 600.0 * scale) / 2.0);
  const auto& outline = dialog.project().cover->layout.outline;
  const auto at = [&](double fraction) {
    const double x = outline.center_x + (fraction - 0.5) * outline.width;
    return (origin + QPointF(x, outline.center_y) * scale).toPoint();
  };
  const auto drag = [&](double from, double to) {
    QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, at(from));
    QTest::mouseMove(preview, at((from + to) / 2.0));
    QTest::mouseMove(preview, at(to));
    QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, at(to));
  };
  patchy::test::ui::save_widget_artifact("ui_book_scan_spine_before", dialog);
  const double left = dialog.project().cover->layout.spine_left;
  const double right = dialog.project().cover->layout.spine_right;
  // Press on the right line and drag it well right.
  drag(right, 0.65);
  CHECK(std::abs(dialog.project().cover->layout.spine_left - left) < 0.01);
  CHECK(std::abs(dialog.project().cover->layout.spine_right - 0.65) < 0.02);
  // Drag the left line past the right one: they swap roles and stay ordered.
  drag(left, 0.8);
  CHECK(std::abs(dialog.project().cover->layout.spine_left - 0.65) < 0.02);
  CHECK(std::abs(dialog.project().cover->layout.spine_right - 0.8) < 0.02);
  patchy::test::ui::save_widget_artifact("ui_book_scan_spine_after", dialog);
  // The edit is what the manifest holds.
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->cover && std::abs(saved->cover->layout.spine_right - 0.8) < 0.02);
}

// Start Over deletes the old book's files but keeps the folder and anything else in it.
void ui_book_scan_start_over_keeps_folder() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_over_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_over_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 1).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  {
    patchy::ui::BookScanDialog dialog(nullptr);
    scan_book(dialog, project, 1);
    CHECK(dialog.export_pdf(QDir(project).filePath(QStringLiteral("Test Book.pdf")), false));
  }
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.set_project_folder(project);
  auto* title = dialog.findChild<QLineEdit*>(QStringLiteral("bookScanTitleEdit"));
  CHECK(title != nullptr && title->text() == QStringLiteral("Test Book"));
  dialog.start_over();
  CHECK(QDir(project).exists());
  CHECK(!QFile::exists(QDir(project).filePath(QStringLiteral("book.json"))));
  CHECK(!QDir(project).exists(QStringLiteral("scans")));
  CHECK(QFile::exists(QDir(project).filePath(QStringLiteral("Test Book.pdf"))));
  auto* cover_button = dialog.findChild<QPushButton*>(QStringLiteral("bookScanCoverButton"));
  auto* continue_button = dialog.findChild<QPushButton*>(QStringLiteral("bookScanContinueButton"));
  CHECK(cover_button != nullptr && !cover_button->isHidden());
  CHECK(continue_button != nullptr && continue_button->isHidden());
  CHECK(title != nullptr && title->text().isEmpty());
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

// Each input image turned a quarter turn clockwise: the top of the book points right.
QStringList turned_inputs(const QStringList& files) {
  QStringList turned;
  for (const QString& file : files) {
    const QString path = QFileInfo(file).dir().filePath(QStringLiteral("right-") + QFileInfo(file).fileName());
    QImage(file).transformed(QTransform().rotate(90.0)).save(path, "PNG");
    turned << path;
  }
  return turned;
}

// Where a fraction across a box's width lands at its middle height, in the preview
// widget that shows an upright image of `image_size` fitted.
QPoint preview_point(const QWidget& preview, const QSize& image_size, const patchy::OrientedBox& box, double fraction) {
  const double scale = std::min(preview.width() / static_cast<double>(image_size.width()),
                                preview.height() / static_cast<double>(image_size.height()));
  const QPointF origin((preview.width() - image_size.width() * scale) / 2.0,
                       (preview.height() - image_size.height() * scale) / 2.0);
  const double radians = box.angle_degrees * kPi / 180.0;
  const double local_x = (fraction - 0.5) * box.width;
  const QPointF at(box.center_x + local_x * std::cos(radians), box.center_y + local_x * std::sin(radians));
  return (origin + at * scale).toPoint();
}

QSize upright_scan_size(const patchy::ui::BookProject& book, const QString& file, int turns) {
  const QSize raw = QImageReader(QDir(book.folder).filePath(file)).size();
  return turns % 2 != 0 ? raw.transposed() : raw;
}

// Drags the gutter line of the scan the scanning step shows to `to`.
void drag_gutter(patchy::ui::BookScanDialog& dialog, int scan_index, double to) {
  auto* preview = dialog.findChild<QWidget*>(QStringLiteral("bookScanSpreadPreview"));
  CHECK(preview != nullptr);
  if (preview == nullptr) {
    return;
  }
  const auto& book = dialog.project();
  const auto& scan = book.scans[static_cast<std::size_t>(scan_index)];
  const QSize size = upright_scan_size(book, scan.file, book.up_turns + scan.extra_turns);
  const auto box = scan.layout.spread;
  const double from = scan.layout.gutter;
  QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, preview_point(*preview, size, box, from));
  QTest::mouseMove(preview, preview_point(*preview, size, box, (from + to) / 2.0));
  QTest::mouseMove(preview, preview_point(*preview, size, box, to));
  QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, preview_point(*preview, size, box, to));
  QApplication::processEvents();
}

// The gutter dragged in the box editor stays where it was dropped (it jumped back when
// dragged to the right), through the next scan and a reopen of book.json, upright and
// with the top of the book pointing right (a quarter turn).
void check_gutter_drag_sticks(bool quarter_turn) {
  // Top of the book is remembered for the next book: put the setting back afterwards.
  const patchy::test::ui::SettingsGroupRestorer book_settings(QStringLiteral("bookScan"));
  const QString tag = quarter_turn ? QStringLiteral("_turned") : QString();
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_gutter_inputs") + tag);
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_gutter_project") + tag) + QStringLiteral("/book");
  QStringList files = write_book_inputs(inputs, 3);
  if (quarter_turn) {
    files = turned_inputs(files);
  }
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", quarter_turn ? "2x3" : "3x2");
  double dropped = 0.0;
  {
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.show();
    dialog.set_project_folder(project);
    dialog.start_cover_scan();
    wait_for_scan(dialog);
    if (quarter_turn) {
      auto* up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUpCombo"));
      CHECK(up != nullptr);
      if (up != nullptr) {
        up->setCurrentIndex(1);  // Right
      }
      CHECK(dialog.project().up_turns == 3);
    }
    dialog.start_page_scanning();
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.project().scans.size() == 1);
    if (dialog.project().scans.empty()) {
      return;
    }
    QApplication::processEvents();
    const double found = dialog.project().scans[0].layout.gutter;
    CHECK(std::abs(found - 0.5) < 0.03);
    // To the right first: on release the line used to jump back to where it was found.
    dropped = found + 0.08;
    drag_gutter(dialog, 0, dropped);
    CHECK(std::abs(dialog.project().scans[0].layout.gutter - dropped) < 0.01);
    CHECK(dialog.project().scans[0].user_edited);
    patchy::test::ui::save_widget_artifact("ui_book_scan_gutter_dragged" + tag.toStdString(), dialog);
    // And to the left.
    dropped = found - 0.06;
    drag_gutter(dialog, 0, dropped);
    CHECK(std::abs(dialog.project().scans[0].layout.gutter - dropped) < 0.01);
    // The next scan leaves the edited one alone.
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.project().scans.size() == 2);
    CHECK(std::abs(dialog.project().scans[0].layout.gutter - dropped) < 0.01);
    // An earlier scan picked from the strip takes a drag too, and only it changes.
    const double second = dialog.project().scans[1].layout.gutter;
    dialog.select_scan_for_editing(0);
    CHECK(dialog.selected_scan() == 0);
    dropped = found + 0.05;
    drag_gutter(dialog, 0, dropped);
    CHECK(std::abs(dialog.project().scans[0].layout.gutter - dropped) < 0.01);
    CHECK(std::abs(dialog.project().scans[1].layout.gutter - second) < 1e-9);
  }
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->scans.size() == 2);
  if (saved && !saved->scans.empty()) {
    CHECK(std::abs(saved->scans[0].layout.gutter - dropped) < 0.01);
    CHECK(saved->scans[0].user_edited);
  }
  // Continue This Book shows the gutter where it was left.
  patchy::ui::BookScanDialog resumed(nullptr);
  resumed.set_quiet(true);
  resumed.set_project_folder(project);
  resumed.continue_book();
  CHECK(resumed.step() == patchy::ui::BookScanDialog::Step::Scanning);
  CHECK(resumed.project().scans.size() == 2);
  if (resumed.project().scans.size() == 2) {
    CHECK(std::abs(resumed.project().scans[0].layout.gutter - dropped) < 0.01);
  }
}

void ui_book_scan_gutter_drag_sticks() {
  check_gutter_drag_sticks(false);
  check_gutter_drag_sticks(true);
}

// The strip shows the covers and every page; a click on one shows its scan with its
// options, Rescan replaces only that scan (or the cover), and Back to Next Pages returns.
void ui_book_scan_edit_and_rescan_earlier_scans() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_edit_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_edit_project")) + QStringLiteral("/book");
  QStringList files = write_book_inputs(inputs, 3);
  // Then: the cover again, the first spread again, the cover once more.
  files << files[0] << files[1] << files[0];
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  // BMP files, as the WIA driver hands over: they must not stay behind in scans/.
  const EnvGuard format("PATCHY_FAKE_BOOK_SCANNER_FORMAT", "bmp");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  dialog.start_page_scanning();
  for (int i = 0; i < 3; ++i) {
    dialog.start_page_scan();
    wait_for_scan(dialog);
  }
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.project().scans.size() == 3);
  if (dialog.project().scans.size() != 3 || !dialog.project().cover) {
    return;
  }
  CHECK(QDir(project).entryList({QStringLiteral("*.bmp")}, QDir::Files).isEmpty());
  CHECK(QDir(project + QStringLiteral("/scans")).entryList({QStringLiteral("*.bmp")}, QDir::Files).isEmpty());
  QApplication::processEvents();

  // Front cover, six pages, back cover.
  auto* strip = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPageStrip"));
  auto* instruction = dialog.findChild<QLabel*>(QStringLiteral("bookScanInstructionLabel"));
  auto* back = dialog.findChild<QPushButton*>(QStringLiteral("bookScanBackToNextButton"));
  auto* rescan = dialog.findChild<QPushButton*>(QStringLiteral("bookScanRescanButton"));
  auto* use = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUseCombo"));
  auto* up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningUpCombo"));
  CHECK(strip != nullptr && instruction != nullptr && back != nullptr && rescan != nullptr && use != nullptr &&
        up != nullptr);
  if (strip == nullptr || instruction == nullptr || back == nullptr || rescan == nullptr || use == nullptr ||
      up == nullptr) {
    return;
  }
  CHECK(strip->count() == 8);
  CHECK(strip->item(0)->text() == QStringLiteral("Front cover"));
  CHECK(strip->item(1)->text() == QStringLiteral("1"));
  CHECK(strip->item(7)->text() == QStringLiteral("Back cover"));
  CHECK(!strip->item(0)->icon().isNull());
  CHECK(back->isHidden());
  CHECK(instruction->text().contains(QStringLiteral("7")));

  // A click on the front cover shows the cover with its own options.
  const auto click = [strip](int row) {
    strip->scrollToItem(strip->item(row));
    QTest::mouseClick(strip->viewport(), Qt::LeftButton, Qt::NoModifier, strip->visualItemRect(strip->item(row)).center());
  };
  click(0);
  CHECK(process_events_until([&dialog] { return dialog.cover_selected(); }, 3000));
  CHECK(!back->isHidden());
  CHECK(!up->isHidden() && use->isHidden());
  CHECK(rescan->text() == QStringLiteral("Rescan Cover"));
  CHECK(instruction->text().contains(QStringLiteral("Back to Next Pages")));
  CHECK(strip->item(0)->isSelected() && strip->item(7)->isSelected() && !strip->item(1)->isSelected());
  patchy::test::ui::save_widget_artifact("ui_book_scan_cover_selected", dialog);

  // Rescanning the cover keeps the book's settings and pages, and the file's name.
  const QString cover_file = dialog.project().cover->file;
  const auto mode = dialog.project().mode;
  dialog.rescan_selected();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.cover_selected());
  CHECK(dialog.project().cover && dialog.project().cover->file == cover_file);
  CHECK(dialog.project().mode == mode && dialog.project().include_covers);
  CHECK(dialog.project().scans.size() == 3);
  CHECK(QDir(project + QStringLiteral("/scans")).entryList({QStringLiteral("cover-new*")}, QDir::Files).isEmpty());

  // A click on page 2 (the first spread's right page) picks the first scan.
  click(2);
  CHECK(process_events_until([&dialog] { return !dialog.cover_selected() && dialog.selected_scan() == 0; }, 3000));
  CHECK(rescan->text() == QStringLiteral("Rescan This Scan"));
  CHECK(!use->isHidden() && !up->isHidden());
  CHECK(instruction->text().contains(QStringLiteral("1")) && instruction->text().contains(QStringLiteral("Back to Next Pages")));
  CHECK(strip->item(1)->isSelected() && strip->item(2)->isSelected() && !strip->item(3)->isSelected());
  // Its Use choice applies to it, not to the newest scan.
  use->setCurrentIndex(1);  // Left page only
  CHECK(dialog.project().scans[0].use == patchy::ui::BookPageUse::LeftOnly);
  CHECK(dialog.project().scans[2].use == patchy::ui::BookPageUse::Both);
  CHECK(strip->count() == 7);

  // Rescan This Scan replaces the first scan in place.
  const QString first_file = dialog.project().scans[0].file;
  const QString last_file = dialog.project().scans[2].file;
  dialog.rescan_selected();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.project().scans.size() == 3);
  CHECK(dialog.project().scans[0].file == first_file && dialog.project().scans[2].file == last_file);
  CHECK(dialog.project().scans[0].use == patchy::ui::BookPageUse::LeftOnly);
  CHECK(dialog.selected_scan() == 0);
  CHECK(QDir(project + QStringLiteral("/scans")).entryList({QStringLiteral("*-new*"), QStringLiteral("*.bmp")}, QDir::Files).isEmpty());

  // Top of the book turned half a turn and back again keeps every box.
  const auto before = dialog.project().scans[1].layout;
  dialog.select_cover_for_editing();
  up->setCurrentIndex(2);  // Down
  CHECK(dialog.project().up_turns == 2);
  CHECK(std::abs(dialog.project().scans[1].layout.gutter - (1.0 - before.gutter)) < 1e-9);
  CHECK(dialog.project().scans[0].use == patchy::ui::BookPageUse::RightOnly);
  up->setCurrentIndex(0);  // Up
  CHECK(dialog.project().up_turns == 0);
  CHECK(std::abs(dialog.project().scans[1].layout.spread.center_x - before.spread.center_x) < 1e-6);
  CHECK(std::abs(dialog.project().scans[1].layout.spread.center_y - before.spread.center_y) < 1e-6);
  CHECK(std::abs(dialog.project().scans[1].layout.gutter - before.gutter) < 1e-9);
  CHECK(dialog.project().scans[0].use == patchy::ui::BookPageUse::LeftOnly);

  // Back to Next Pages shows the newest scan and the next pages again.
  back->click();
  CHECK(!dialog.cover_selected() && dialog.selected_scan() == 2);
  CHECK(back->isHidden());
  CHECK(rescan->text() == QStringLiteral("Rescan Last"));
  CHECK(instruction->text().contains(QStringLiteral("Turn to pages 6 and 7")));

  // From the review step, Edit Scan opens a page's scan; a custom page order stays.
  dialog.finish_scanning();
  auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPagesList"));
  auto* turn = dialog.findChild<QPushButton*>(QStringLiteral("bookScanRotatePagesButton"));
  auto* edit = dialog.findChild<QPushButton*>(QStringLiteral("bookScanEditPageButton"));
  CHECK(pages != nullptr && turn != nullptr && edit != nullptr);
  if (pages == nullptr || turn == nullptr || edit == nullptr) {
    return;
  }
  pages->setCurrentRow(3);  // the second scan's left page
  pages->item(3)->setSelected(true);
  turn->click();
  CHECK(dialog.project().custom_order.has_value());
  const auto order = dialog.project().custom_order.value_or(std::vector<patchy::ui::BookPageRef>{});
  pages->setCurrentRow(3);
  edit->click();
  CHECK(process_events_until([&dialog] { return dialog.step() == patchy::ui::BookScanDialog::Step::Scanning; }, 3000));
  CHECK(dialog.selected_scan() == 1);
  dialog.select_cover_for_editing();
  dialog.rescan_selected();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.project().custom_order && *dialog.project().custom_order == order);
  // Everything above is in book.json.
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->scans.size() == 3 && saved->custom_order && *saved->custom_order == order);
  if (saved && saved->scans.size() == 3) {
    CHECK(saved->scans[0].use == patchy::ui::BookPageUse::LeftOnly);
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
  patchy::ui::BookScanDialog dialog(nullptr);
  scan_book(dialog, project, 1);
  CHECK(QFile::exists(QDir(project).filePath(QStringLiteral("book.json"))));
  CHECK(patchy::ui::load_book_project(project, nullptr).has_value());
  const QString pdf_path = QDir(project).filePath(QString::fromUtf8(
      reinterpret_cast<const char*>(patchy::test::kUnicodeDirName.data()),
      static_cast<qsizetype>(patchy::test::kUnicodeDirName.size())) + QStringLiteral(".pdf"));
  CHECK(dialog.export_pdf(pdf_path, false));
  CHECK(media_boxes(read_file(pdf_path)).size() == 4);
}

// A scanner switched on and off while the window is open (docs/book-scan.md, "Scanners
// turning on and off"): the scanner list, Scan Cover and the resolution list follow
// without Refresh, the open scanner is closed and opened again, a scan the scanner drops
// out of is repeated once, and a scanner that stays off is reported.
void ui_book_scan_follows_scanner_power() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_power_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_power_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 3).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  const EnvGuard devices("PATCHY_FAKE_BOOK_SCANNER_DEVICES", "none");
  const EnvGuard dpis("PATCHY_FAKE_BOOK_SCANNER_DPIS", "150,300");
  const auto set_devices = [](const char* list) {
    qputenv("PATCHY_FAKE_BOOK_SCANNER_DEVICES", list);
    patchy::ui::notify_test_scanner_devices_changed();
  };
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.set_project_folder(project);
  dialog.set_title(QStringLiteral("Power Book"));
  auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("bookScanDeviceCombo"));
  auto* cover = dialog.findChild<QPushButton*>(QStringLiteral("bookScanCoverButton"));
  auto* skip = dialog.findChild<QPushButton*>(QStringLiteral("bookScanSkipCoverButton"));
  auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanResolutionCombo"));
  auto* status = dialog.findChild<QLabel*>(QStringLiteral("bookScanStatusLabel"));
  CHECK(combo != nullptr && cover != nullptr && skip != nullptr && resolution != nullptr && status != nullptr);
  // Switched off when the window opens.
  CHECK(combo->count() == 1 && combo->currentData().toString().isEmpty());
  CHECK(!cover->isEnabled() && !skip->isEnabled());
  CHECK(!cover->toolTip().isEmpty());
  dialog.show();
  patchy::test::ui::save_widget_artifact("ui_book_scan_power_off", dialog);
  // Switched on: the list fills by itself.
  set_devices("fake=Test scanner");
  CHECK(combo->count() == 1 && combo->currentData().toString() == QStringLiteral("fake"));
  CHECK(cover->isEnabled() && skip->isEnabled() && cover->toolTip().isEmpty());
  patchy::test::ui::save_widget_artifact("ui_book_scan_power_on", dialog);
  // A second scanner appears: the pick stays.
  set_devices("other=Other scanner;fake=Test scanner");
  CHECK(combo->count() == 2 && combo->currentData().toString() == QStringLiteral("fake"));
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Cover);
  CHECK(dialog.scanner_open());
  CHECK(resolution->count() == 2 && resolution->isEnabled());
  resolution->setCurrentIndex(resolution->findData(kDpi));
  // Switched off: the scanner is closed and its capture settings are unavailable.
  set_devices("other=Other scanner");
  CHECK(!dialog.scanner_open());
  CHECK(!resolution->isEnabled());
  // Switched on again, now offering other resolutions: the next scan opens it again
  // (the book's scanner, not the other one) and the resolution list follows.
  qputenv("PATCHY_FAKE_BOOK_SCANNER_DPIS", "300,1200");
  set_devices("other=Other scanner;fake=Test scanner");
  CHECK(resolution->isEnabled());
  dialog.start_page_scanning();
  CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.scanner_open());
  CHECK(dialog.project().device_id == QStringLiteral("fake"));
  CHECK(dialog.project().scans.size() == 1);
  CHECK(resolution->count() == 2 && resolution->findData(1200) >= 0 && resolution->currentData().toInt() == kDpi);
  // Switched off and on during a scan: the scan fails, is repeated once, and succeeds.
  dialog.start_page_scan();
  set_devices("other=Other scanner;fake=Test scanner");
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(dialog.project().scans.size() == 2);
  CHECK(dialog.scanner_open());
  // Switched off: the window says so until it is back.
  set_devices("other=Other scanner");
  CHECK(!status->text().isEmpty());
  set_devices("other=Other scanner;fake=Test scanner");
  CHECK(status->text().isEmpty());
  // Off for good: the next scan reports it instead of using the other scanner.
  set_devices("other=Other scanner");
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(!dialog.last_error().isEmpty());
  CHECK(dialog.project().scans.size() == 2);
}

// Capture settings per scan: a spread scanned in color at 600 dpi among grayscale 300 dpi
// ones, an earlier scan rescanned at other settings, a page with its own output in a
// black and white PDF, and the screen-sized scan copies the editor and preview show.
void ui_book_scan_per_scan_settings_and_page_output() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_settings_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_settings_project")) + QStringLiteral("/book");
  QStringList files = write_book_inputs(inputs, 3);
  // The second spread and the rescan of the first are whole-bed images at 600 dpi.
  files[2] = write_bed(inputs, QStringLiteral("spread-2-600.png"), 1800, 1200, {900.0, 600.0, 1440.0, 960.0, 0.0}, true, 80);
  files << write_bed(inputs, QStringLiteral("spread-1-600.png"), 1800, 1200, {880.0, 610.0, 1440.0, 960.0, 0.0}, true, 60);
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  dialog.set_quiet(true);
  dialog.show();
  dialog.set_project_folder(project);
  dialog.start_cover_scan();
  wait_for_scan(dialog);
  dialog.start_page_scanning();
  auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningResolutionCombo"));
  auto* color = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningColorCombo"));
  auto* note = dialog.findChild<QLabel*>(QStringLiteral("bookScanCaptureNote"));
  CHECK(resolution != nullptr && color != nullptr && note != nullptr);
  if (resolution == nullptr || color == nullptr || note == nullptr) {
    return;
  }
  CHECK(!resolution->isHidden() && resolution->isEnabled() && color->isEnabled());
  const auto set_capture = [&](int dpi, int mode) {
    resolution->setCurrentIndex(resolution->findData(dpi));
    color->setCurrentIndex(mode);
  };
  set_capture(kDpi, 1);  // Grayscale
  CHECK(dialog.project().capture_mode == patchy::ui::ScanColorMode::Grayscale);
  dialog.start_page_scan();
  wait_for_scan(dialog);
  set_capture(600, 0);  // Color
  dialog.start_page_scan();
  wait_for_scan(dialog);
  set_capture(kDpi, 1);
  dialog.start_page_scan();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  const auto& book = dialog.project();
  CHECK(book.scans.size() == 3);
  if (book.scans.size() != 3) {
    return;
  }
  CHECK(book.scans[0].dpi == kDpi && book.scans[0].capture_mode == patchy::ui::ScanColorMode::Grayscale);
  CHECK(book.scans[1].dpi == 600 && book.scans[1].capture_mode == patchy::ui::ScanColorMode::Color);
  CHECK(book.scans[2].dpi == kDpi && book.scans[2].capture_mode == patchy::ui::ScanColorMode::Grayscale);
  CHECK(QImageReader(QDir(project).filePath(book.scans[0].file)).imageFormat() == QImage::Format_Grayscale8);
  // Pages are measured in inches, so the 600 dpi spread has the same page size.
  const auto size = patchy::ui::resolved_page_size(book);
  CHECK(std::abs(size.width - 1.2) < 0.05 && std::abs(size.height - 1.6) < 0.05);
  // Every scan has its view copy beside its thumbnail.
  for (const auto& scan : book.scans) {
    CHECK(QFile::exists(QDir(project).filePath(QStringLiteral("thumbs/") + QFileInfo(scan.file).completeBaseName() +
                                               QStringLiteral("-view.jpg"))));
  }

  // An earlier scan shows its own settings; changing them is for its rescan only.
  dialog.select_scan_for_editing(0);
  CHECK(resolution->currentData().toInt() == kDpi && color->currentIndex() == 1);
  CHECK(!note->isHidden() && note->text().contains(QStringLiteral("300")));
  set_capture(600, 0);
  CHECK(dialog.project().capture_dpi == kDpi && dialog.project().capture_mode == patchy::ui::ScanColorMode::Grayscale);
  CHECK(note->text().contains(QStringLiteral("Rescan This Scan")));
  const QString first_file = book.scans[0].file;
  dialog.rescan_selected();
  wait_for_scan(dialog);
  CHECK(dialog.last_error().isEmpty());
  CHECK(book.scans[0].file == first_file && book.scans[0].dpi == 600 &&
        book.scans[0].capture_mode == patchy::ui::ScanColorMode::Color);
  CHECK(std::abs(book.scans[0].layout.spread.width / 600.0 - 2.4) < 0.05);
  CHECK(dialog.project().capture_dpi == kDpi);
  // Back to the next pages: the book's settings again.
  dialog.back_to_next_pages();
  CHECK(resolution->currentData().toInt() == kDpi && color->currentIndex() == 1 && note->isHidden());

  // Review: a black and white book with the color spread's pages in color.
  dialog.finish_scanning();
  auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPagesList"));
  auto* output = dialog.findChild<QComboBox*>(QStringLiteral("bookScanOutputCombo"));
  auto* page_output = dialog.findChild<QComboBox*>(QStringLiteral("bookScanPageOutputCombo"));
  auto* preview = dialog.findChild<QLabel*>(QStringLiteral("bookScanReviewPreview"));
  CHECK(pages != nullptr && output != nullptr && page_output != nullptr && preview != nullptr);
  if (pages == nullptr || output == nullptr || page_output == nullptr || preview == nullptr) {
    return;
  }
  output->setCurrentIndex(2);  // Black and white
  CHECK(pages->count() == 8);  // front cover, six pages, back cover
  pages->clearSelection();
  pages->item(3)->setSelected(true);  // the second scan's two pages
  pages->item(4)->setSelected(true);
  page_output->setCurrentIndex(page_output->findData(static_cast<int>(patchy::BookOutputMode::Color)));
  CHECK(dialog.project().custom_order.has_value());
  const auto order = patchy::ui::book_page_order(dialog.project());
  CHECK(order.size() == 8 && order[3].scan == 1 && order[4].scan == 1);
  CHECK(order[3].output == patchy::BookOutputMode::Color && order[4].output == patchy::BookOutputMode::Color);
  CHECK(!order[2].output.has_value() && !order[5].output.has_value());
  CHECK(pages->item(3)->text().contains(QStringLiteral("Color")) && !pages->item(2)->text().contains(QStringLiteral("(")));
  CHECK(pages->item(3)->isSelected() && pages->item(4)->isSelected() && !pages->item(2)->isSelected());
  CHECK(patchy::ui::page_output_options(dialog.project(), order[3]).mode == patchy::BookOutputMode::Color);
  CHECK(patchy::ui::page_output_options(dialog.project(), order[2]).mode == patchy::BookOutputMode::BlackAndWhite);
  patchy::test::ui::save_widget_artifact("ui_book_scan_page_output", dialog);
  const QString pdf_path = QDir(inputs).filePath(QStringLiteral("mixed.pdf"));
  CHECK(dialog.export_pdf(pdf_path, false));
  const QByteArray pdf = read_file(pdf_path);
  CHECK(pdf.contains("/BitsPerComponent 1"));
  CHECK(pdf.contains("/DCTDecode"));
  CHECK(media_boxes(pdf).size() == 8);
  // Saved: each scan's mode and each page's own output.
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->scans.size() == 3 && saved->custom_order && *saved->custom_order == order);
  if (saved && saved->scans.size() == 3) {
    CHECK(saved->scans[0].capture_mode == patchy::ui::ScanColorMode::Color);
    CHECK(saved->scans[2].capture_mode == patchy::ui::ScanColorMode::Grayscale);
  }

  // The review preview comes from the view copy: it shows a page whose full scan is gone.
  const QString full = QDir(project).filePath(book.scans[2].file);
  const QString aside = full + QStringLiteral(".aside");
  CHECK(QFile::rename(full, aside));
  pages->setCurrentRow(5);  // the third scan's left page
  CHECK(!preview->pixmap().isNull());
  CHECK(QFile::rename(aside, full));
}

// The review list reorders by drag and drop: the selected pages move to the gap under
// the drop, and the order is saved.
void ui_book_scan_review_drag_reorders_pages() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_drag_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_drag_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 2).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  scan_book(dialog, project, 2);
  dialog.show();
  auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPagesList"));
  CHECK(pages != nullptr);
  if (pages == nullptr) {
    return;
  }
  QApplication::processEvents();
  const auto before = patchy::ui::book_page_order(dialog.project());
  CHECK(before.size() == 6 && pages->count() == 6);
  if (before.size() != 6 || pages->count() != 6) {
    return;
  }
  // Drag page 1 (row 1) to the gap after the last page.
  const auto drop = [pages](const std::vector<int>& rows, QPoint at) {
    QStringList parts;
    for (const int row : rows) {
      parts << QString::number(row);
    }
    QMimeData mime;
    mime.setData(QStringLiteral("application/x-patchy-book-pages"),
                 QByteArray::number(reinterpret_cast<quintptr>(pages)) + ':' + parts.join(QLatin1Char(',')).toLatin1());
    QDragEnterEvent enter(at, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(pages->viewport(), &enter);
    CHECK(enter.isAccepted());
    QDragMoveEvent move(at, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(pages->viewport(), &move);
    CHECK(move.isAccepted());
    QDropEvent event(QPointF(at), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(pages->viewport(), &event);
    CHECK(event.isAccepted());
    QApplication::processEvents();
  };
  const QRect last = pages->visualItemRect(pages->item(5));
  drop({1}, QPoint(last.right() - 2, last.center().y()));
  auto order = patchy::ui::book_page_order(dialog.project());
  CHECK(dialog.project().custom_order.has_value());
  CHECK(order.size() == 6);
  if (order.size() != 6) {
    return;
  }
  CHECK(order[5] == before[1] && order[0] == before[0] && order[1] == before[2]);
  CHECK(pages->item(5)->isSelected());
  // Two pages dropped on the left half of the first page go to the front, in order.
  pages->clearSelection();
  const QRect first = pages->visualItemRect(pages->item(0));
  drop({3, 4}, QPoint(first.left() + 2, first.center().y()));
  order = patchy::ui::book_page_order(dialog.project());
  CHECK(order.size() == 6 && order[0] == before[4] && order[1] == before[5] && order[2] == before[0]);
  // A drop from somewhere else is refused.
  QMimeData foreign;
  foreign.setData(QStringLiteral("application/x-patchy-book-pages"), QByteArrayLiteral("1:0"));
  QDragEnterEvent enter(first.center(), Qt::MoveAction, &foreign, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(pages->viewport(), &enter);
  CHECK(!enter.isAccepted());
  // The order is saved, and the same move by the test surface agrees.
  const auto saved = patchy::ui::load_book_project(project, nullptr);
  CHECK(saved.has_value() && saved->custom_order && *saved->custom_order == order);
  dialog.move_review_pages({0}, 6);
  const auto moved = patchy::ui::book_page_order(dialog.project());
  CHECK(moved.size() == 6 && moved[5] == order[0] && moved[0] == order[1]);
  // Back is the row's first button and returns to scanning.
  auto* back = dialog.findChild<QPushButton*>(QStringLiteral("bookScanReviewBackButton"));
  auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("bookScanDeletePagesButton"));
  CHECK(back != nullptr && remove != nullptr);
  if (back != nullptr && remove != nullptr) {
    CHECK(back->text() == QStringLiteral("Back"));
    CHECK(back->geometry().right() < remove->geometry().left());
    back->click();
    CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
  }
}

}  // namespace

// --- Searchable export (docs/book-scan.md, "Searchable PDF") --------------------------------

// What a reader finds on each page: the invisible runs, and how many images.
struct BookPageText final : patchy::pdf::ContentSink {
  std::vector<patchy::pdf::TextRun> hidden;
  int visible{0};
  int images{0};

  void on_path(const patchy::pdf::PaintedPath&) override {}
  void on_text(const patchy::pdf::TextRun&) override { ++visible; }
  void on_hidden_text(const patchy::pdf::TextRun& run) override { hidden.push_back(run); }
  void on_image(const patchy::pdf::PlacedImage&) override { ++images; }
  void on_shading(std::shared_ptr<const patchy::pdf::ResolvedShading>, const patchy::VectorPath&) override {}
  void on_notice(const std::string&) override {}
};

struct BookPdfRead {
  std::vector<BookPageText> pages;
  // Each page's image stream, as stored.
  std::vector<std::vector<std::uint8_t>> images;
};

BookPdfRead read_book_pdf(const QString& path) {
  const QByteArray raw = read_file(path);
  const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
  BookPdfRead read;
  auto file = patchy::pdf::File::open(bytes, nullptr);
  CHECK(file.has_value());
  if (!file.has_value()) {
    return read;
  }
  read.pages.resize(file->pages().size());
  for (std::size_t index = 0; index < read.pages.size(); ++index) {
    const auto& page = file->pages()[index];
    patchy::pdf::execute_page(*file, page, 1.0, read.pages[index]);
    read.images.push_back(file->stream_data(file->get(file->get(page.resources, "XObject"), "Im0")).data);
  }
  return read;
}

constexpr char kBookLine[] = "Recognized book line";

std::shared_ptr<patchy::ocr::FakeEngine> book_fake_engine(int delay_ms = 0) {
  patchy::ocr::FakeEngineConfig config;
  config.lines = {patchy::ocr::FakeLine{patchy::ocr::Box{20, 30, 200, 24}, kBookLine, false}};
  config.delay_ms = delay_ms;
  return patchy::ocr::make_fake_engine(std::move(config));
}

void ui_book_scan_searchable_export_writes_text() {
  const patchy::test::ui::SettingsGroupRestorer ocr_settings(QStringLiteral("ocr"));
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_searchable_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_searchable_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 2).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  patchy::ui::BookScanDialog dialog(nullptr);
  scan_book(dialog, project, 2);
  auto* check = dialog.findChild<QCheckBox*>(QStringLiteral("bookScanSearchableCheck"));
  auto* engine_combo = dialog.findChild<QComboBox*>(QStringLiteral("bookScanOcrEngineCombo"));
  auto* note = dialog.findChild<QLabel*>(QStringLiteral("bookScanOcrUnavailableNote"));
  CHECK(check != nullptr && engine_combo != nullptr && note != nullptr);
  if (check == nullptr || engine_combo == nullptr || note == nullptr) {
    return;
  }
  // Off until asked for, and the engine choice stays out of the way until then.
  CHECK(!check->isChecked() && !dialog.project().output_searchable);
  CHECK(!engine_combo->isVisibleTo(&dialog));
  const QString book_json = QDir(project).filePath(QStringLiteral("book.json"));
  CHECK(!read_file(book_json).contains("searchable"));
  const QString plain_path = QDir(inputs).filePath(QStringLiteral("plain.pdf"));
  CHECK(dialog.export_pdf(plain_path, false));

  // No engine on this system (the suite's default): ticking shows why, and the export says
  // so instead of writing a PDF that silently has no text.
  check->setChecked(true);
  CHECK(dialog.project().output_searchable);
  CHECK(engine_combo->isVisibleTo(&dialog));
  CHECK(note->isVisibleTo(&dialog) && !note->text().isEmpty());
  CHECK(read_file(book_json).contains("\"searchable\": true"));
  const QString refused_path = QDir(inputs).filePath(QStringLiteral("refused.pdf"));
  CHECK(!dialog.export_pdf(refused_path, false));
  CHECK(dialog.last_error().contains(QStringLiteral("searchable")));
  CHECK(!QFile::exists(refused_path));
  check->setChecked(false);
  CHECK(!read_file(book_json).contains("searchable"));

  // With an engine: every page is read once and its line goes out invisibly, on the page.
  const auto fake = book_fake_engine();
  const patchy::test::ui::ScopedOcrEngines engines({fake});
  check->setChecked(true);
  CHECK(engine_combo->currentData().toString() == QStringLiteral("fake"));
  const QString text_path = QDir(inputs).filePath(QStringLiteral("searchable.pdf"));
  CHECK(dialog.export_pdf(text_path, false));
  CHECK(dialog.last_error().isEmpty());
  const auto plain = read_book_pdf(plain_path);
  const auto text = read_book_pdf(text_path);
  CHECK(plain.pages.size() == 6 && text.pages.size() == 6);
  const auto calls = fake->calls();
  CHECK(calls.size() == 6);
  CHECK(fake->open_count() == 1);  // one recognizer for the whole book
  for (const auto& call : calls) {
    CHECK(call.language == "en-US");
    CHECK(!call.only_black_and_white);  // the page as cut, in the scan's own tones
    CHECK(call.image_width == calls.front().image_width && call.image_height == calls.front().image_height);
  }
  const double scale = 72.0 / kDpi;
  for (std::size_t index = 0; index < text.pages.size() && index < plain.pages.size(); ++index) {
    CHECK(plain.pages[index].hidden.empty());
    CHECK(text.pages[index].images == 1 && text.pages[index].visible == 0);
    CHECK(text.pages[index].hidden.size() == 1);
    if (text.pages[index].hidden.size() == 1) {
      const auto& run = text.pages[index].hidden[0];
      CHECK(run.utf8 == kBookLine);
      CHECK(run.render_mode == 3);
      CHECK(std::abs(run.transform.e - 20.0 * scale) < 0.05);
      CHECK(std::abs(run.transform.f - 54.0 * scale) < 0.05);
      CHECK(std::abs(run.intended_width - 200.0 * scale) < 0.05);
    }
    // The page itself is the one an export without recognition writes.
    CHECK(text.images[index] == plain.images[index]);
  }
  CHECK(media_boxes(read_file(text_path)) == media_boxes(read_file(plain_path)));
  CHECK(!read_file(text_path).contains(" TJ"));
  // Only the choice is kept: nothing that was read is in the project.
  CHECK(read_file(book_json).contains("\"searchable\": true"));
  CHECK(!read_file(book_json).contains(kBookLine));
  // The picks are remembered like every other recognition's.
  CHECK(patchy::ui::app_settings().value(QStringLiteral("ocr/engine")).toString() == QStringLiteral("fake"));

  // Black and white output: the engine still reads the page before it is thresholded.
  auto* output = dialog.findChild<QComboBox*>(QStringLiteral("bookScanOutputCombo"));
  CHECK(output != nullptr);
  if (output != nullptr) {
    output->setCurrentIndex(2);
    const QString bw_path = QDir(inputs).filePath(QStringLiteral("searchable-bw.pdf"));
    CHECK(dialog.export_pdf(bw_path, false));
    CHECK(read_file(bw_path).contains("/BitsPerComponent 1"));
    const auto all_calls = fake->calls();
    CHECK(all_calls.size() == 12);
    for (std::size_t index = 6; index < all_calls.size(); ++index) {
      CHECK(!all_calls[index].only_black_and_white);
    }
    const auto bw = read_book_pdf(bw_path);
    CHECK(bw.pages.size() == 6);
    for (const auto& page : bw.pages) {
      CHECK(page.hidden.size() == 1 && page.hidden[0].utf8 == kBookLine);
    }
  }
  // The reopened project still asks for a searchable PDF.
  const auto reloaded = patchy::ui::load_book_project(project, nullptr);
  CHECK(reloaded.has_value() && reloaded->output_searchable);
}

void ui_book_scan_searchable_cancel_failure_and_script() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_searchable2_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_searchable2_project")) + QStringLiteral("/book");
  patchy::ui::BookProject book;
  {
    const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_book_inputs(inputs, 2).join(QDir::listSeparator()).toUtf8());
    const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
    patchy::ui::BookScanDialog dialog(nullptr);
    scan_book(dialog, project, 2);
    book = dialog.project();
  }
  // A file already at the destination survives a cancelled or failed export.
  const QString path = QDir(inputs).filePath(QStringLiteral("kept.pdf"));
  const auto write_sentinel = [&path] {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("sentinel");
  };
  write_sentinel();

  // Cancel while pages are being read: the read in flight stops, nothing is written.
  {
    const auto slow = book_fake_engine(400);
    int asked = 0;
    const auto result = patchy::ui::write_book_pdf(
        book, path,
        [&asked](int, int) {
          ++asked;
          return asked < 3;
        },
        patchy::ui::BookPdfRecognition{slow, "en-US"});
    CHECK(result.cancelled && !result.ok);
    CHECK(asked == 3);  // asked while the first page was still being read
    CHECK(slow->calls().size() < 6);
    CHECK(read_file(path) == QByteArray("sentinel"));
  }
  // The engine fails on a page: the export fails with the engine's words.
  {
    patchy::ocr::FakeEngineConfig config;
    config.fail_with = "engine broke";
    const auto result = patchy::ui::write_book_pdf(
        book, path, {}, patchy::ui::BookPdfRecognition{patchy::ocr::make_fake_engine(std::move(config)), "en-US"});
    CHECK(!result.ok && !result.cancelled);
    CHECK(result.error.contains(QStringLiteral("engine broke")));
    CHECK(read_file(path) == QByteArray("sentinel"));
  }
  // The engine cannot start at all.
  {
    patchy::ocr::FakeEngineConfig config;
    config.unavailable = patchy::ocr::Unavailable::Failed;
    config.unavailable_reason = "no runtime here";
    const auto result = patchy::ui::write_book_pdf(
        book, path, {}, patchy::ui::BookPdfRecognition{patchy::ocr::make_fake_engine(std::move(config)), "en-US"});
    CHECK(!result.ok && !result.cancelled);
    CHECK(result.error.contains(QStringLiteral("no runtime here")));
    CHECK(read_file(path) == QByteArray("sentinel"));
  }

  // Scripts: `searchable` with the engine and language named.
  patchy::ui::MainWindow window;
  show_window(window);
  const auto json_string = [](const QString& text) {
    return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).chopped(1).mid(1);
  };
  const QString on_path = QDir(inputs).filePath(QStringLiteral("script-on.pdf"));
  const QString off_path = QDir(inputs).filePath(QStringLiteral("script-off.pdf"));
  {
    const patchy::test::ui::ScopedOcrEngines engines({book_fake_engine()});
    CHECK(run_script(window, QStringLiteral(R"JS(
      var on = app.buildBookPdf(%1, %2, { searchable: true, ocrEngine: 'fake', ocrLanguage: 'en-US' });
      var off = app.buildBookPdf(%1, %3);
      if (on !== 6 || off !== 6) throw new Error('pages ' + on + ' ' + off);
      var threw = false;
      try { app.buildBookPdf(%1, %2, { searchable: true, ocrEngine: 'nope' }); } catch (e) { threw = true; }
      if (!threw) throw new Error('unknown engine accepted');
      console.log('book-ocr-ok');
    )JS")
                                    .arg(json_string(project), json_string(on_path), json_string(off_path))));
    CHECK(backlog_contains(window, QStringLiteral("book-ocr-ok")));
  }
  const auto on = read_book_pdf(on_path);
  const auto off = read_book_pdf(off_path);
  CHECK(on.pages.size() == 6 && off.pages.size() == 6);
  for (const auto& page : on.pages) {
    CHECK(page.hidden.size() == 1 && page.hidden[0].utf8 == kBookLine);
  }
  for (const auto& page : off.pages) {
    CHECK(page.hidden.empty());
  }
  // No engine: asking for a searchable PDF is an error, not a PDF without text.
  CHECK(run_script(window, QStringLiteral(R"JS(
    var threw = false;
    try { app.buildBookPdf(%1, %2, { searchable: true }); } catch (e) { threw = true; }
    if (!threw) throw new Error('searchable accepted without an engine');
    console.log('book-ocr-none-ok');
  )JS")
                                  .arg(json_string(project), json_string(on_path))));
  CHECK(backlog_contains(window, QStringLiteral("book-ocr-none-ok")));
}

// --- The divider decides the cut; binding margin; detection per scan (docs/book-scan.md) -----

// A whole-bed scan of an open book whose fold has a wide dark shadow, as a thick book
// leaves on the glass. The left page holds text lines and the right page text lines and a
// framed block, so a page that shows the other side is seen at once.
QString write_shadow_spread(const QString& folder, const QString& name, int width, int height,
                            const patchy::OrientedBox& book, int marker) {
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
      const double u = lx / book.width;
      const double v = ly / book.height;
      const double fold = std::abs(u - 0.5);
      int value = fold < 0.035 ? 120 + static_cast<int>(fold / 0.035 * 118.0) : 238;
      const bool left = u < 0.5;
      const double page_u = left ? u / 0.5 : (u - 0.5) / 0.5;
      const bool text_rows = v > 0.14 && v < 0.88 && static_cast<int>(v * 48.0) % 2 == 1;
      if (text_rows && page_u > 0.14 && page_u < 0.86 && !(v > 0.3 && v < 0.55 && !left)) {
        value = std::min(value, 60);
      }
      // The right page's framed block.
      if (!left && v > 0.32 && v < 0.53 && page_u > 0.2 && page_u < 0.8) {
        const bool frame = v < 0.335 || v > 0.515 || page_u < 0.212 || page_u > 0.788;
        value = std::min(value, frame ? 60 : 200);
      }
      // A page-number block in the top outer corner tells the pages apart.
      if (v < 0.07 && (u < 0.07 || u > 0.93)) {
        value = marker;
      }
      row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = static_cast<uchar>(std::min(value, 255));
    }
  }
  const QString path = QDir(folder).filePath(name);
  image.save(path, "PNG");
  return path;
}

// The darkest and lightest value in a block of a Gray8 or RGB8 page.
std::pair<int, int> page_block_range(const patchy::PixelBuffer& page, int x0, int x1, int y0, int y1) {
  int lightest = 0;
  int darkest = 255;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const int value = page.pixel(x, y)[0];
      lightest = std::max(lightest, value);
      darkest = std::min(darkest, value);
    }
  }
  return {darkest, lightest};
}

std::pair<int, int> image_block_range(const QImage& image, int x0, int x1, int y0, int y1) {
  int lightest = 0;
  int darkest = 255;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const int value = qGray(image.pixel(x, y));
      lightest = std::max(lightest, value);
      darkest = std::min(darkest, value);
    }
  }
  return {darkest, lightest};
}

// A cover and `spreads` spreads on a 6 x 4 inch bed at 300 dpi: a 4.8 x 3.2 inch open book
// with a wide fold shadow.
QStringList write_shadow_book_inputs(const QString& folder, int spreads) {
  QStringList files;
  files << write_bed(folder, QStringLiteral("cover.png"), 1800, 1200, {900.0, 600.0, 1440.0, 960.0, 0.0}, false, 0);
  for (int i = 0; i < spreads; ++i) {
    files << write_shadow_spread(folder, QStringLiteral("spread-%1.png").arg(i + 1), 1800, 1200,
                                 {900.0, 600.0, 1440.0, 960.0, 0.0}, 60 + i * 20);
  }
  return files;
}

// The column where a row of a page or picture is darkest, as a fraction of its width. On
// a row with no text (just under the page-number blocks) that is the fold's shadow.
double darkest_fraction(const QImage& image, double row_fraction = 0.105) {
  if (image.isNull()) {
    return -1.0;
  }
  const int y = std::clamp(static_cast<int>(image.height() * row_fraction), 0, image.height() - 1);
  int darkest = 256;
  int first = 0;
  int last = 0;
  for (int x = 0; x < image.width(); ++x) {
    const int value = qGray(image.pixel(x, y));
    if (value < darkest) {
      darkest = value;
      first = x;
    }
    if (value == darkest) {
      last = x;
    }
  }
  return ((first + last) / 2.0 + 0.5) / image.width();
}

QImage pdf_page_image(const BookPdfRead& pdf, std::size_t index) {
  if (index >= pdf.images.size()) {
    return {};
  }
  return QImage::fromData(QByteArray(reinterpret_cast<const char*>(pdf.images[index].data()),
                                     static_cast<qsizetype>(pdf.images[index].size())));
}

// The divider decides where BOTH pages of a spread are cut: each page's inner edge is the
// divider, wherever it is dragged, and the page runs outward from there by the book's page
// width. A page that reaches past the outline's outer edge is white there. The strip, the
// review thumbnail and preview, the PDF and a script's export all show the same cut.
void ui_book_scan_divider_decides_the_page_cut() {
  const patchy::test::ui::SettingsGroupRestorer book_settings(QStringLiteral("bookScan"));
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_divider_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_divider_project")) + QStringLiteral("/book");
  const QString pdf_start = QDir(inputs).filePath(QStringLiteral("divider-start.pdf"));
  const QString pdf_left = QDir(inputs).filePath(QStringLiteral("divider-left.pdf"));
  const QString pdf_right = QDir(inputs).filePath(QStringLiteral("divider-right.pdf"));
  const QString pdf_script = QDir(inputs).filePath(QStringLiteral("divider-script.pdf"));
  // Everything is captured first and checked after, so a failing run still shows each step.
  struct Pages {
    QImage left;
    QImage right;
    double gutter{0.5};
  };
  Pages start;
  Pages dragged_left;
  Pages dragged_right;
  QImage strip_start[2];
  QImage strip_left[2];
  QImage thumbnail;
  QImage previewed;
  QImage right_only;
  QImage rtl_first;
  double spread_width = 0.0;
  bool exported = true;
  {
    const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER",
                           write_shadow_book_inputs(inputs, 1).join(QDir::listSeparator()).toUtf8());
    const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "6x4");
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.show();
    dialog.set_project_folder(project);
    dialog.set_title(QStringLiteral("Field Notes"));
    dialog.start_cover_scan();
    wait_for_scan(dialog);
    if (auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanResolutionCombo"))) {
      resolution->setCurrentIndex(resolution->findData(kDpi));
    }
    dialog.start_page_scanning();
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 1);
    auto* strip = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPageStrip"));
    auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPagesList"));
    auto* preview = dialog.findChild<QLabel*>(QStringLiteral("bookScanReviewPreview"));
    auto* back = dialog.findChild<QPushButton*>(QStringLiteral("bookScanReviewBackButton"));
    auto* use = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUseCombo"));
    CHECK(strip != nullptr && pages != nullptr && preview != nullptr && back != nullptr && use != nullptr);
    if (dialog.project().scans.size() != 1 || strip == nullptr || pages == nullptr || preview == nullptr ||
        back == nullptr || use == nullptr) {
      return;
    }
    QApplication::processEvents();
    const auto& book = dialog.project();
    CHECK(std::abs(book.scans[0].layout.gutter - 0.5) < 0.02);
    CHECK(strip->count() == 4);  // front cover, two pages, back cover
    spread_width = book.scans[0].layout.spread.width;
    const auto strip_icon = [strip](int row) {
      return row < strip->count() ? strip->item(row)->icon().pixmap(64, 90).toImage() : QImage();
    };
    const auto rendered = [&book] {
      Pages result;
      result.gutter = book.scans[0].layout.gutter;
      const auto size = patchy::ui::resolved_page_size(book);
      patchy::ui::BookScanCache cache(book);
      for (const auto& page : patchy::ui::book_page_order(book)) {
        if (page.kind == patchy::ui::BookPageRef::Kind::Scan) {
          (page.side == patchy::BookPageSide::Left ? result.left : result.right) =
              patchy::ui::book_image_from_pixels(patchy::ui::render_book_page(book, page, size, 0, cache, false));
        }
      }
      return result;
    };
    const auto export_now = [&](const QString& path) {
      dialog.finish_scanning();
      exported = dialog.export_pdf(path, false) && exported;
    };
    const auto back_to_scanning = [&] {
      back->click();
      QApplication::processEvents();
    };

    // As detected: the divider on the fold.
    start = rendered();
    strip_start[0] = strip_icon(1);
    strip_start[1] = strip_icon(2);
    export_now(pdf_start);
    back_to_scanning();

    // Dragged well left of the fold.
    drag_gutter(dialog, 0, 0.40);
    dragged_left = rendered();
    strip_left[0] = strip_icon(1);
    strip_left[1] = strip_icon(2);
    patchy::test::ui::save_widget_artifact("ui_book_scan_divider_scanning", dialog);
    patchy::test::ui::save_image_artifact("ui_book_scan_divider_left_page", dragged_left.left);
    patchy::test::ui::save_image_artifact("ui_book_scan_divider_right_page", dragged_left.right);
    dialog.finish_scanning();
    if (pages->count() == 4) {
      pages->setCurrentRow(2);
      pages->clearSelection();
      pages->item(2)->setSelected(true);
      QApplication::processEvents();
      // Before the export, whose summary line names the PDF's folder.
      patchy::test::ui::save_widget_artifact("ui_book_scan_divider_review", dialog);
      thumbnail = pages->item(2)->icon().pixmap(pages->iconSize()).toImage();
      previewed = preview->pixmap().toImage();
    }
    export_now(pdf_left);
    back_to_scanning();

    // Dragged right of the fold.
    drag_gutter(dialog, 0, 0.62);
    dragged_right = rendered();
    patchy::test::ui::save_widget_artifact("ui_book_scan_divider_scanning_right", dialog);
    // Use: Right page only, and a right-to-left book, which only changes the order: its
    // first page is the same right page.
    use->setCurrentIndex(2);
    right_only = rendered().right;
    use->setCurrentIndex(0);
    patchy::ui::BookProject rtl = book;
    rtl.direction = patchy::ui::BookReadingDirection::RightToLeft;
    const auto rtl_order = patchy::ui::book_page_order(rtl);
    if (rtl_order.size() == 4 && rtl_order[1].side == patchy::BookPageSide::Right) {
      patchy::ui::BookScanCache cache(rtl);
      rtl_first = patchy::ui::book_image_from_pixels(
          patchy::ui::render_book_page(rtl, rtl_order[1], patchy::ui::resolved_page_size(rtl), 0, cache, false));
    }
    export_now(pdf_right);
    dialog.reject();
  }
  // A script exports the saved book: the same pages.
  {
    patchy::ui::MainWindow window;
    show_window(window);
    const auto json_string = [](const QString& text) {
      return QString::fromUtf8(QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact)).chopped(1).mid(1);
    };
    CHECK(run_script(window, QStringLiteral("var pages = app.buildBookPdf(%1, %2); if (pages !== 4) throw new "
                                            "Error('pages ' + pages); console.log('book-divider-ok');")
                                 .arg(json_string(project), json_string(pdf_script))));
    CHECK(backlog_contains(window, QStringLiteral("book-divider-ok")));
  }

  CHECK(exported);
  CHECK(!start.left.isNull() && !start.right.isNull() && !dragged_left.left.isNull() && !dragged_left.right.isNull() &&
        !dragged_right.left.isNull() && !dragged_right.right.isNull());
  if (start.left.isNull() || dragged_left.left.isNull() || dragged_left.right.isNull() || dragged_right.left.isNull() ||
      dragged_right.right.isNull()) {
    return;
  }
  CHECK(std::abs(dragged_left.gutter - 0.40) < 0.01 && std::abs(dragged_right.gutter - 0.62) < 0.01);
  const int width = dragged_left.right.width();
  const int height = dragged_left.right.height();
  // One spread measured at the median: each page is half the spread wide wherever the divider is.
  CHECK(std::abs(width - spread_width / 2.0) < 2.0 && dragged_left.left.width() == width &&
        dragged_right.left.width() == width);
  const int rows0 = height / 10;
  const int rows1 = height * 9 / 10;
  const auto fold_at = [&](const QImage& page, double from_left_px) {
    return std::abs(darkest_fraction(page) * page.width() - from_left_px) < 5.0;
  };

  // The drag to the left moved BOTH pages, in the strip too.
  CHECK(dragged_left.left != start.left);
  CHECK(dragged_left.right != start.right);
  CHECK(!strip_left[0].isNull() && strip_left[0] != strip_start[0]);
  CHECK(!strip_left[1].isNull() && strip_left[1] != strip_start[1]);
  // The right page starts at the divider: the fold, a tenth of the spread right of the
  // divider, lies that far into the page, and the page's outer end is still the scan.
  const double left_shift = (0.5 - dragged_left.gutter) * spread_width;
  CHECK(fold_at(dragged_left.right, left_shift));
  CHECK(image_block_range(dragged_left.right, width - 20, width - 2, rows0, rows1).first < 250);
  // The left page ends at the divider and runs outward past the outline, where it is white;
  // its page-number block marks the outline's outer edge.
  CHECK(image_block_range(dragged_left.left, 0, static_cast<int>(left_shift) - 4, 0, height).first >= 250);
  CHECK(std::abs(qGray(dragged_left.left.pixel(static_cast<int>(left_shift) + 12, 8)) - 60) < 25);
  CHECK(image_block_range(dragged_left.left, width - 20, width - 2, rows0, rows1).first < 250);

  // Dragged to the right, the same holds mirrored: the left page ends at the divider (the
  // fold lies short of its inner edge) and the right page starts there and is white past
  // the outline.
  const double right_shift = (dragged_right.gutter - 0.5) * spread_width;
  CHECK(dragged_right.left != dragged_left.left && dragged_right.right != dragged_left.right);
  CHECK(fold_at(dragged_right.left, width - right_shift));
  CHECK(image_block_range(dragged_right.right, width - static_cast<int>(right_shift) + 4, width, 0, height).first >= 250);
  CHECK(std::abs(qGray(dragged_right.right.pixel(width - static_cast<int>(right_shift) - 12, 8)) - 60) < 25);
  // Use: Right page only measures that page alone, so the page is exactly its side of the
  // divider: from the divider to the outline's outer edge, with its page-number block.
  CHECK(!right_only.isNull() && std::abs(right_only.width() - (width - right_shift)) < 2.0);
  if (!right_only.isNull() && right_only.width() > 24) {
    CHECK(std::abs(qGray(right_only.pixel(right_only.width() - 12, 8)) - 60) < 25);
    CHECK(image_block_range(right_only, right_only.width() - 6, right_only.width() - 2, rows0, rows1).first < 250);
  }
  CHECK(!rtl_first.isNull() && rtl_first == dragged_right.right);

  // Review: the thumbnail and the preview show the right page from the divider on.
  const double fold_fraction = left_shift / width;
  CHECK(std::abs(darkest_fraction(thumbnail) - fold_fraction) < 0.04);
  CHECK(std::abs(darkest_fraction(previewed) - fold_fraction) < 0.04);
  // The PDF: both pages changed with the divider, and a script writes the same pages.
  const BookPdfRead read_start = read_book_pdf(pdf_start);
  const BookPdfRead read_left = read_book_pdf(pdf_left);
  const BookPdfRead read_right = read_book_pdf(pdf_right);
  const BookPdfRead read_script = read_book_pdf(pdf_script);
  CHECK(read_start.images.size() == 4 && read_left.images.size() == 4 && read_right.images.size() == 4 &&
        read_script.images.size() == 4);
  if (read_start.images.size() != 4 || read_left.images.size() != 4 || read_right.images.size() != 4 ||
      read_script.images.size() != 4) {
    return;
  }
  CHECK(read_left.images[1] != read_start.images[1]);
  CHECK(read_left.images[2] != read_start.images[2]);
  const QImage pdf_right_page = pdf_page_image(read_left, 2);
  CHECK(pdf_right_page.width() == width && pdf_right_page.height() == height);
  CHECK(fold_at(pdf_right_page, left_shift));
  CHECK(fold_at(pdf_page_image(read_right, 1), width - right_shift));
  for (std::size_t index = 0; index < 4; ++index) {
    CHECK(read_script.images[index] == read_right.images[index]);
  }
}

// The binding margin's value for a spin box in the locale's unit, and the inches it means.
struct MarginValue {
  double shown;
  double inches;
};
MarginValue margin_value(double millimeters, double inches) {
  const bool metric = QLocale().measurementSystem() == QLocale::MetricSystem;
  return metric ? MarginValue{millimeters, millimeters / 25.4} : MarginValue{inches, inches};
}

// The binding margin leaves a strip out on each side of every spread's divider: the pages
// lose the fold's shadow, the measured page is narrower by the margin, the box editor
// shades the strip, and the value is the book's (book.json) and the next book's default.
void ui_book_scan_binding_margin_leaves_the_strip_out() {
  const patchy::test::ui::SettingsGroupRestorer book_settings(QStringLiteral("bookScan"));
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_margin_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_margin_project")) + QStringLiteral("/book");
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", write_shadow_book_inputs(inputs, 2).join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "6x4");
  double last_margin = 0.0;
  {
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.show();
    dialog.set_project_folder(project);
    dialog.set_title(QStringLiteral("Field Notes"));
    dialog.start_cover_scan();
    wait_for_scan(dialog);
    if (auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanResolutionCombo"))) {
      resolution->setCurrentIndex(resolution->findData(kDpi));
    }
    dialog.start_page_scanning();
    for (int i = 0; i < 2; ++i) {
      dialog.start_page_scan();
      wait_for_scan(dialog);
    }
    CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 2);
    auto* margin = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("bookScanBindingMarginSpin"));
    auto* detect = dialog.findChild<QCheckBox*>(QStringLiteral("bookScanDetectCheck"));
    auto* preview = dialog.findChild<QWidget*>(QStringLiteral("bookScanSpreadPreview"));
    auto* strip = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPageStrip"));
    auto* pages_combo = dialog.findChild<QComboBox*>(QStringLiteral("bookScanPagesCombo"));
    CHECK(margin != nullptr && detect != nullptr && preview != nullptr && strip != nullptr && pages_combo != nullptr);
    if (dialog.project().scans.size() != 2 || margin == nullptr || detect == nullptr || preview == nullptr ||
        strip == nullptr || pages_combo == nullptr) {
      return;
    }
    QApplication::processEvents();
    const auto& book = dialog.project();
    // A new book: no margin, every scan detected.
    CHECK(!margin->isHidden() && margin->value() == 0.0 && book.binding_margin == 0.0);
    CHECK(!detect->isHidden() && detect->isChecked() && book.detect_each_scan);
    CHECK(!read_file(QDir(project).filePath(QStringLiteral("book.json"))).contains("bindingMargin"));
    for (const auto& scan : book.scans) {
      CHECK(std::abs(scan.layout.gutter - 0.5) < 0.01);
    }
    const auto order = patchy::ui::book_page_order(book);
    CHECK(order.size() == 6);
    if (order.size() != 6) {
      return;
    }
    const auto size_before = patchy::ui::resolved_page_size(book);
    patchy::ui::BookScanCache cache(book);
    // Without a margin the right page's inner edge is the fold: its shadow is on the page.
    const auto with_shadow = patchy::ui::render_book_page(book, order[4], size_before, 0, cache, false);
    CHECK(!with_shadow.empty());
    if (with_shadow.empty()) {
      return;
    }
    CHECK(page_block_range(with_shadow, 0, 20, with_shadow.height() / 10, with_shadow.height() * 9 / 10).first < 170);
    const auto& shown = book.scans[1];
    const QSize upright = upright_scan_size(book, shown.file, book.up_turns + shown.extra_turns);
    const QImage strip_before = strip->item(4)->icon().pixmap(64, 90).toImage();

    // 5 mm (0.2 inch) on each side of the divider: wider than the fixture's shadow.
    const MarginValue wanted = margin_value(5.0, 0.2);
    const QPoint in_strip =
        preview_point(*preview, upright, shown.layout.spread,
                      shown.layout.gutter + wanted.inches * shown.dpi / shown.layout.spread.width / 2.0);
    const int strip_pixel_before = qGray(preview->grab().toImage().pixel(in_strip));
    margin->setValue(wanted.shown);
    QApplication::processEvents();
    CHECK(std::abs(book.binding_margin - wanted.inches) < 1e-9);
    last_margin = book.binding_margin;
    CHECK(std::abs(patchy::ui::app_settings().value(QStringLiteral("bookScan/bindingMargin")).toDouble() - wanted.inches) <
          1e-9);
    const auto saved = patchy::ui::load_book_project(project, nullptr);
    CHECK(saved.has_value() && std::abs(saved->binding_margin - wanted.inches) < 1e-9);
    // The measured page leaves the strip out.
    const auto size_after = patchy::ui::resolved_page_size(book);
    CHECK(std::abs(size_after.width - (size_before.width - wanted.inches)) < 1e-6);
    CHECK(std::abs(size_after.height - size_before.height) < 1e-9);
    // The box editor shades the strip beside the divider, and the strip's pages changed.
    CHECK(qGray(preview->grab().toImage().pixel(in_strip)) < strip_pixel_before - 15);
    CHECK(strip->item(4)->icon().pixmap(64, 90).toImage() != strip_before);
    patchy::test::ui::save_widget_artifact("ui_book_scan_binding_margin_scanning", dialog);
    // The page starts past the shadow now: paper at its inner edge, on both pages.
    cache.clear();
    const auto right = patchy::ui::render_book_page(book, order[4], size_after, 0, cache, false);
    const auto left = patchy::ui::render_book_page(book, order[3], size_after, 0, cache, false);
    CHECK(!right.empty() && !left.empty());
    if (right.empty() || left.empty()) {
      return;
    }
    patchy::test::ui::save_image_artifact("ui_book_scan_binding_margin_right_page", patchy::ui::book_image_from_pixels(right));
    CHECK(page_block_range(right, 0, 20, right.height() / 10, right.height() * 9 / 10).first > 190);
    CHECK(page_block_range(left, left.width() - 20, left.width(), left.height() / 10, left.height() * 9 / 10).first > 190);
    CHECK(std::abs(static_cast<int>(right.pixel(right.width() - 8, 8)[0]) - 80) < 25);

    // One page per scan has no divider: the margin is hidden there.
    pages_combo->setCurrentIndex(1);
    CHECK(margin->isHidden());
    pages_combo->setCurrentIndex(0);
    CHECK(!margin->isHidden());
    CHECK(std::abs(book.binding_margin - wanted.inches) < 1e-9);

    // The review step has the same setting beside the page size; changing it there changes
    // the thumbnails, the preview and the PDF pages.
    dialog.finish_scanning();
    auto* review_margin = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("bookScanReviewBindingMarginSpin"));
    auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("bookScanPagesList"));
    auto* summary = dialog.findChild<QLabel*>(QStringLiteral("bookScanReviewSummary"));
    CHECK(review_margin != nullptr && pages != nullptr && summary != nullptr);
    if (review_margin == nullptr || pages == nullptr || summary == nullptr) {
      return;
    }
    CHECK(!review_margin->isHidden() && std::abs(review_margin->value() - wanted.shown) < 1e-6);
    pages->setCurrentRow(4);
    pages->clearSelection();
    pages->item(4)->setSelected(true);
    QApplication::processEvents();
    patchy::test::ui::save_widget_artifact("ui_book_scan_binding_margin_review", dialog);
    const QImage thumbnail_before = pages->item(4)->icon().pixmap(pages->iconSize()).toImage();
    const QString summary_before = summary->text();
    const MarginValue smaller = margin_value(2.0, 0.08);
    review_margin->setValue(smaller.shown);
    QApplication::processEvents();
    CHECK(std::abs(book.binding_margin - smaller.inches) < 1e-9);
    last_margin = book.binding_margin;
    CHECK(pages->count() == 6 && pages->item(4)->icon().pixmap(pages->iconSize()).toImage() != thumbnail_before);
    CHECK(summary->text() != summary_before);
    review_margin->setValue(wanted.shown);
    QApplication::processEvents();
    last_margin = book.binding_margin;
    const QString pdf_path = QDir(inputs).filePath(QStringLiteral("margin.pdf"));
    CHECK(dialog.export_pdf(pdf_path, false));
    const BookPdfRead pdf = read_book_pdf(pdf_path);
    CHECK(pdf.images.size() == 6);
    if (pdf.images.size() == 6) {
      const QImage page = QImage::fromData(QByteArray(reinterpret_cast<const char*>(pdf.images[4].data()),
                                                      static_cast<qsizetype>(pdf.images[4].size())));
      CHECK(page.width() == right.width() && page.height() == right.height());
      CHECK(!page.isNull() && image_block_range(page, 0, 20, page.height() / 10, page.height() * 9 / 10).first > 180);
    }
    dialog.reject();
  }
  // The next book starts with the last margin, and with detection on.
  const QString next = fresh_artifact_folder(QStringLiteral("ui_book_scan_margin_next_project")) + QStringLiteral("/book");
  patchy::ui::BookScanDialog fresh(nullptr);
  fresh.set_quiet(true);
  fresh.set_project_folder(next);
  CHECK(last_margin > 0.0 && std::abs(fresh.project().binding_margin - last_margin) < 1e-9);
  CHECK(fresh.project().detect_each_scan);
}

// "Detect the pages on each scan" off: a new scan takes the boxes of the scan before it
// (rescaled at another resolution), a rescan keeps its own, and boxes the user moved lead
// the scans after them. On again, scans are detected. A rescan keeps boxes the user moved
// either way, and the first scan of a book is detected even with it off.
void ui_book_scan_detection_off_reuses_the_layout() {
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_detect_inputs"));
  const QString project = fresh_artifact_folder(QStringLiteral("ui_book_scan_detect_project")) + QStringLiteral("/book");
  const QStringList made = write_book_inputs(inputs, 3);
  // The same bed at 600 dpi, the book lying elsewhere again.
  const QString at_600 =
      write_bed(inputs, QStringLiteral("spread-600.png"), 1800, 1200, {940.0, 580.0, 1440.0, 960.0, 0.0}, true, 100);
  const auto same_layout = [](const patchy::SpreadLayout& a, const patchy::SpreadLayout& b, double scale = 1.0) {
    return std::abs(a.spread.center_x - b.spread.center_x * scale) < 1e-6 &&
           std::abs(a.spread.center_y - b.spread.center_y * scale) < 1e-6 &&
           std::abs(a.spread.width - b.spread.width * scale) < 1e-6 &&
           std::abs(a.spread.height - b.spread.height * scale) < 1e-6 &&
           std::abs(a.spread.angle_degrees - b.spread.angle_degrees) < 1e-9 && std::abs(a.gutter - b.gutter) < 1e-9;
  };
  {
    // Cover, spread 1, its rescan (spread 2's image), spread 2, spread 3, a rescan of the
    // first scan (spread 3's image), the 600 dpi spread, spread 1 again.
    const QStringList files{made[0], made[1], made[2], made[2], made[3], made[3], at_600, made[1]};
    const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
    const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.show();
    dialog.set_project_folder(project);
    dialog.start_cover_scan();
    wait_for_scan(dialog);
    dialog.start_page_scanning();
    auto* detect = dialog.findChild<QCheckBox*>(QStringLiteral("bookScanDetectCheck"));
    auto* status = dialog.findChild<QLabel*>(QStringLiteral("bookScanStatusLabel"));
    auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningResolutionCombo"));
    auto* turn = dialog.findChild<QPushButton*>(QStringLiteral("bookScanRotateSpreadButton"));
    CHECK(detect != nullptr && status != nullptr && resolution != nullptr && turn != nullptr);
    if (detect == nullptr || status == nullptr || resolution == nullptr || turn == nullptr) {
      return;
    }
    const auto& book = dialog.project();
    CHECK(detect->isChecked() && book.detect_each_scan);
    resolution->setCurrentIndex(resolution->findData(kDpi));
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty() && book.scans.size() == 1);
    if (book.scans.size() != 1) {
      return;
    }
    QApplication::processEvents();
    CHECK(std::abs(book.scans[0].layout.gutter - 0.5) < 0.02 && !book.scans[0].user_edited);

    // Detection on: a rescan still keeps a divider the user moved, though the book lies
    // elsewhere in the new image.
    drag_gutter(dialog, 0, book.scans[0].layout.gutter + 0.07);
    const patchy::SpreadLayout first = book.scans[0].layout;
    CHECK(book.scans[0].user_edited && first.gutter > 0.55);
    dialog.rescan_last();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty() && book.scans.size() == 1);
    CHECK(same_layout(book.scans[0].layout, first) && book.scans[0].user_edited);
    CHECK(status->text().contains(QStringLiteral("kept")));

    // Off: the next scan takes the first scan's boxes as they are.
    detect->click();
    CHECK(!detect->isChecked() && !book.detect_each_scan);
    CHECK(read_file(QDir(project).filePath(QStringLiteral("book.json"))).contains("\"detectEachScan\": false"));
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty() && book.scans.size() == 2);
    if (book.scans.size() != 2) {
      return;
    }
    QApplication::processEvents();
    CHECK(same_layout(book.scans[1].layout, first) && !book.scans[1].user_edited);
    // Boxes moved by hand on this scan lead the next one.
    drag_gutter(dialog, 1, 0.44);
    const patchy::SpreadLayout second = book.scans[1].layout;
    CHECK(std::abs(second.gutter - 0.44) < 0.01 && book.scans[1].user_edited);
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(book.scans.size() == 3);
    if (book.scans.size() != 3) {
      return;
    }
    QApplication::processEvents();
    CHECK(same_layout(book.scans[2].layout, second) && !same_layout(book.scans[2].layout, first));
    // Turn This Scan 180 Degrees turns the boxes with the scan instead of detecting.
    turn->click();
    CHECK(book.scans[2].extra_turns == 2 && std::abs(book.scans[2].layout.gutter - (1.0 - second.gutter)) < 1e-9);
    turn->click();
    CHECK(book.scans[2].extra_turns == 0 && same_layout(book.scans[2].layout, second));
    patchy::test::ui::save_widget_artifact("ui_book_scan_detection_off", dialog);

    // A rescan of an earlier scan keeps that scan's own boxes.
    dialog.select_scan_for_editing(0);
    CHECK(dialog.selected_scan() == 0);
    dialog.rescan_selected();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty() && book.scans.size() == 3);
    CHECK(same_layout(book.scans[0].layout, first) && same_layout(book.scans[2].layout, second));
    dialog.back_to_next_pages();

    // Another resolution: the same boxes, rescaled.
    resolution->setCurrentIndex(resolution->findData(600));
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty() && book.scans.size() == 4);
    if (book.scans.size() != 4) {
      return;
    }
    CHECK(book.scans[3].dpi == 600 && same_layout(book.scans[3].layout, second, 2.0));
    const auto size = patchy::ui::resolved_page_size(book);
    CHECK(size.width > 0.9 && size.width < 1.5);

    // On again: the next scan is detected where its book lies.
    resolution->setCurrentIndex(resolution->findData(kDpi));
    detect->click();
    CHECK(detect->isChecked() && book.detect_each_scan);
    CHECK(!read_file(QDir(project).filePath(QStringLiteral("book.json"))).contains("detectEachScan"));
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty() && book.scans.size() == 5);
    if (book.scans.size() != 5) {
      return;
    }
    CHECK(std::abs(book.scans[4].layout.gutter - 0.5) < 0.02);
    CHECK(std::abs(book.scans[4].layout.spread.center_x - 438.0) < 4.0);
    CHECK(std::abs(book.scans[4].layout.spread.center_y - 306.0) < 4.0);
    detect->click();
    dialog.reject();
  }
  // The choice is the book's: Continue This Book shows it off again.
  {
    const auto saved = patchy::ui::load_book_project(project, nullptr);
    CHECK(saved.has_value() && !saved->detect_each_scan && saved->scans.size() == 5);
    patchy::ui::BookScanDialog resumed(nullptr);
    resumed.set_quiet(true);
    resumed.set_project_folder(project);
    resumed.continue_book();
    CHECK(resumed.step() == patchy::ui::BookScanDialog::Step::Scanning);
    auto* detect = resumed.findChild<QCheckBox*>(QStringLiteral("bookScanDetectCheck"));
    CHECK(detect != nullptr && !detect->isChecked() && !resumed.project().detect_each_scan);
  }
  // Off before the first scan: with no scan to take boxes from, that scan is detected.
  // Changing Pages per scan detects again, off or not.
  {
    const QString first_project =
        fresh_artifact_folder(QStringLiteral("ui_book_scan_detect_first_project")) + QStringLiteral("/book");
    const QStringList files{made[0], made[1]};
    const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
    const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.show();
    dialog.set_project_folder(first_project);
    dialog.start_cover_scan();
    wait_for_scan(dialog);
    dialog.start_page_scanning();
    CHECK(dialog.project().detect_each_scan);  // a new book starts with detection on
    dialog.set_detect_each_scan(false);
    auto* detect = dialog.findChild<QCheckBox*>(QStringLiteral("bookScanDetectCheck"));
    auto* pages_combo = dialog.findChild<QComboBox*>(QStringLiteral("bookScanPagesCombo"));
    CHECK(detect != nullptr && !detect->isChecked() && pages_combo != nullptr);
    dialog.start_page_scan();
    wait_for_scan(dialog);
    const auto& book = dialog.project();
    CHECK(dialog.last_error().isEmpty() && book.scans.size() == 1);
    if (book.scans.size() != 1 || pages_combo == nullptr) {
      return;
    }
    QApplication::processEvents();
    CHECK(std::abs(book.scans[0].layout.gutter - 0.5) < 0.02);
    CHECK(std::abs(book.scans[0].layout.spread.width / book.scans[0].dpi - 2.4) < 0.1);
    CHECK(std::abs(book.scans[0].layout.spread.center_x - 438.0) < 4.0);
    drag_gutter(dialog, 0, 0.6);
    CHECK(std::abs(book.scans[0].layout.gutter - 0.6) < 0.01);
    pages_combo->setCurrentIndex(1);
    pages_combo->setCurrentIndex(0);
    CHECK(std::abs(book.scans[0].layout.gutter - 0.5) < 0.02 && !book.scans[0].user_edited);
    CHECK(!book.detect_each_scan);
    dialog.reject();
  }
}

// book.json: the binding margin and the detection choice are written only when set, read
// back, and a manifest without them means no margin and detection on. The margin feeds
// the measured page width and each spread page's cut.
void ui_book_scan_manifest_margin_and_detection_keys() {
  patchy::ui::BookProject book;
  book.title = QStringLiteral("Keys");
  patchy::ui::BookScanEntry scan;
  scan.file = QStringLiteral("scans/spread-0001.png");
  scan.dpi = 300.0;
  scan.layout.spread = {450.0, 300.0, 720.0, 480.0, 0.0};
  scan.layout.gutter = 0.55;
  book.scans.push_back(scan);
  const QByteArray plain = patchy::ui::book_project_json(book);
  CHECK(!plain.contains("bindingMargin") && !plain.contains("detectEachScan"));
  QString error;
  const auto plain_read = patchy::ui::book_project_from_json(plain, &error);
  CHECK(plain_read.has_value() && error.isEmpty());
  if (plain_read) {
    CHECK(plain_read->binding_margin == 0.0 && plain_read->detect_each_scan);
    CHECK(patchy::ui::book_project_json(*plain_read) == plain);
  }

  book.binding_margin = 0.1;
  book.detect_each_scan = false;
  const QByteArray set = patchy::ui::book_project_json(book);
  CHECK(set.contains("\"bindingMargin\": 0.1") && set.contains("\"detectEachScan\": false"));
  const auto set_read = patchy::ui::book_project_from_json(set, &error);
  CHECK(set_read.has_value());
  if (set_read) {
    CHECK(set_read->binding_margin == 0.1 && !set_read->detect_each_scan);
    CHECK(patchy::ui::book_project_json(*set_read) == set);
  }
  // A margin outside the range is brought into it.
  QByteArray wide = set;
  wide.replace("\"bindingMargin\": 0.1", "\"bindingMargin\": 9");
  const auto wide_read = patchy::ui::book_project_from_json(wide, &error);
  CHECK(wide_read.has_value() && wide_read->binding_margin == patchy::ui::kBookBindingMarginMax);
  QByteArray negative = set;
  negative.replace("\"bindingMargin\": 0.1", "\"bindingMargin\": -1");
  const auto negative_read = patchy::ui::book_project_from_json(negative, &error);
  CHECK(negative_read.has_value() && negative_read->binding_margin == 0.0);

  // Measured widths: each side of the divider less the margin (30 px at 300 dpi).
  const auto sizes = patchy::ui::measured_page_sizes(book);
  CHECK(sizes.size() == 2);
  if (sizes.size() == 2) {
    CHECK(std::abs(sizes[0].width - (0.55 * 720.0 - 30.0) / 300.0) < 1e-9);
    CHECK(std::abs(sizes[1].width - (0.45 * 720.0 - 30.0) / 300.0) < 1e-9);
    CHECK(std::abs(sizes[0].height - 1.6) < 1e-9);
  }
  // Each page's box starts a margin from the divider and runs outward.
  const patchy::BookPageSize size{1.2, 1.6};
  const auto left =
      patchy::ui::book_page_source(book, {patchy::ui::BookPageRef::Kind::Scan, 0, patchy::BookPageSide::Left, 0}, size);
  const auto right =
      patchy::ui::book_page_source(book, {patchy::ui::BookPageRef::Kind::Scan, 0, patchy::BookPageSide::Right, 0}, size);
  CHECK(left.has_value() && right.has_value());
  if (left && right) {
    // The left page ends at 396 - 30 px into the spread (which starts at x = 90) and takes
    // the scan from the outline's left edge, 6 px into its box.
    CHECK(std::abs(left->cut.box.center_x + 180.0 - (90.0 + 366.0)) < 1e-9);
    CHECK(std::abs(left->cut.source_from - (-6.0)) < 1e-9 && std::abs(left->cut.source_to - 360.0) < 1e-9);
    // The right page starts at 396 + 30 px and takes the scan up to the outline's right
    // edge, 294 px into its box.
    CHECK(std::abs(right->cut.box.center_x - 180.0 - (90.0 + 426.0)) < 1e-9);
    CHECK(std::abs(right->cut.source_from) < 1e-9 && std::abs(right->cut.source_to - 294.0) < 1e-9);
    CHECK(left->cut.box.width == 360.0 && right->cut.box.width == 360.0);
  }
  // One page per scan has no divider: the margin changes nothing there.
  book.mode = patchy::ui::BookScanMode::OnePage;
  const auto one_sizes = patchy::ui::measured_page_sizes(book);
  CHECK(one_sizes.size() == 1 && std::abs(one_sizes[0].width - 2.4) < 1e-9);
  const auto one =
      patchy::ui::book_page_source(book, {patchy::ui::BookPageRef::Kind::Scan, 0, patchy::BookPageSide::Left, 0}, size);
  CHECK(one.has_value() && one->cut.source_from < -1e100 && one->cut.source_to > 1e100);
}

// "Top of the book points" is remembered: the last choice (bookScan/upTurns) starts every
// new book, with a cover scan and with Skip Cover, so its first detection already runs on
// the turned scan. A book that is continued keeps the turn saved in its book.json.
void ui_book_scan_top_of_book_is_remembered() {
  const patchy::test::ui::SettingsGroupRestorer book_settings(QStringLiteral("bookScan"));
  const QString inputs = fresh_artifact_folder(QStringLiteral("ui_book_scan_top_inputs"));
  const QString first = fresh_artifact_folder(QStringLiteral("ui_book_scan_top_first")) + QStringLiteral("/book");
  const QString second = fresh_artifact_folder(QStringLiteral("ui_book_scan_top_second")) + QStringLiteral("/book");
  const QString third = fresh_artifact_folder(QStringLiteral("ui_book_scan_top_third")) + QStringLiteral("/book");
  const QStringList made = write_book_inputs(inputs, 1);
  // The first book's cover and spread, the second book's cover, the third book's spread.
  const QStringList files{made[0], made[1], made[0], made[1]};
  const EnvGuard scanner("PATCHY_FAKE_BOOK_SCANNER", files.join(QDir::listSeparator()).toUtf8());
  const EnvGuard bed("PATCHY_FAKE_BOOK_SCANNER_BED", "3x2");
  const auto stored = [] { return patchy::ui::app_settings().value(QStringLiteral("bookScan/upTurns"), -1).toInt(); };
  {
    // Nothing stored yet: a book starts with its top pointing up.
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.show();
    dialog.set_project_folder(first);
    CHECK(dialog.project().up_turns == 0 && stored() == -1);
    dialog.start_cover_scan();
    wait_for_scan(dialog);
    auto* up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUpCombo"));
    CHECK(up != nullptr && up->currentIndex() == 0);
    if (up == nullptr) {
      return;
    }
    up->setCurrentIndex(1);  // Right
    CHECK(dialog.project().up_turns == 3 && stored() == 3);
    dialog.start_page_scanning();
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 1);
    dialog.reject();
  }
  {
    // The next book, in another folder, starts turned the same way: the cover step shows
    // Right, and the cover was detected on the turned scan (taller than wide).
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.show();
    dialog.set_project_folder(second);
    CHECK(dialog.project().up_turns == 3);
    dialog.start_cover_scan();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty() && dialog.project().cover.has_value() && dialog.project().up_turns == 3);
    auto* up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUpCombo"));
    CHECK(up != nullptr && up->currentIndex() == 1);
    if (dialog.project().cover) {
      const auto& outline = dialog.project().cover->layout.outline;
      CHECK(outline.height > outline.width * 1.2);
      CHECK(std::abs(outline.height / dialog.project().cover->dpi - 2.4) < 0.1);
    }
    const auto saved = patchy::ui::load_book_project(second, nullptr);
    CHECK(saved.has_value() && saved->up_turns == 3);
    // Changed in the scanning step: that is the next book's start.
    dialog.start_page_scanning();
    auto* scan_up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningUpCombo"));
    CHECK(scan_up != nullptr && scan_up->currentIndex() == 1);
    if (scan_up == nullptr) {
      return;
    }
    scan_up->setCurrentIndex(2);  // Down
    CHECK(dialog.project().up_turns == 2 && stored() == 2);
    dialog.reject();
  }
  {
    // Skip Cover: the book without a cover starts with the last choice too.
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.show();
    dialog.set_project_folder(third);
    dialog.skip_cover();
    CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Cover && dialog.project().up_turns == 2);
    auto* up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanUpCombo"));
    CHECK(up != nullptr && up->currentIndex() == 2);
    dialog.start_page_scanning();
    auto* scan_up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningUpCombo"));
    CHECK(scan_up != nullptr && scan_up->currentIndex() == 2);
    dialog.start_page_scan();
    wait_for_scan(dialog);
    CHECK(dialog.last_error().isEmpty() && dialog.project().scans.size() == 1 && dialog.project().up_turns == 2);
    dialog.reject();
  }
  {
    // Continue This Book: the first book keeps its own turn, and continuing it does not
    // change what the next new book starts with.
    patchy::ui::BookScanDialog dialog(nullptr);
    dialog.set_quiet(true);
    dialog.show();
    dialog.set_project_folder(first);
    dialog.continue_book();
    CHECK(dialog.step() == patchy::ui::BookScanDialog::Step::Scanning);
    CHECK(dialog.project().up_turns == 3 && stored() == 2);
    auto* scan_up = dialog.findChild<QComboBox*>(QStringLiteral("bookScanScanningUpCombo"));
    CHECK(scan_up != nullptr && scan_up->currentIndex() == 1);
    dialog.reject();
  }
}

std::vector<patchy::test::TestCase> book_scan_ui_tests() {
  return {
      {"ui_book_scan_menu_actions_registered", ui_book_scan_menu_actions_registered},
      {"ui_book_scan_cancel_shows_feedback", ui_book_scan_cancel_shows_feedback},
      {"ui_book_scan_skip_cover_sets_top_of_book", ui_book_scan_skip_cover_sets_top_of_book},
      {"ui_book_scan_setup_page_guides_the_cover_scan", ui_book_scan_setup_page_guides_the_cover_scan},
      {"ui_book_scan_covers_one_at_a_time", ui_book_scan_covers_one_at_a_time},
      {"ui_book_scan_full_flow_exports_uniform_pages", ui_book_scan_full_flow_exports_uniform_pages},
      {"ui_book_scan_black_and_white_and_script_export", ui_book_scan_black_and_white_and_script_export},
      {"ui_book_scan_resume_and_rescan", ui_book_scan_resume_and_rescan},
      {"ui_book_scan_one_page_mode_offered_for_wide_book", ui_book_scan_one_page_mode_offered_for_wide_book},
      {"ui_book_scan_switch_pages_per_scan_redetects", ui_book_scan_switch_pages_per_scan_redetects},
      {"ui_book_scan_spine_lines_drag_past_each_other", ui_book_scan_spine_lines_drag_past_each_other},
      {"ui_book_scan_start_over_keeps_folder", ui_book_scan_start_over_keeps_folder},
      {"ui_book_scan_rtl_orders_pages", ui_book_scan_rtl_orders_pages},
      {"ui_book_scan_gutter_drag_sticks", ui_book_scan_gutter_drag_sticks},
      {"ui_book_scan_edit_and_rescan_earlier_scans", ui_book_scan_edit_and_rescan_earlier_scans},
      {"ui_book_scan_follows_scanner_power", ui_book_scan_follows_scanner_power},
      {"ui_book_scan_per_scan_settings_and_page_output", ui_book_scan_per_scan_settings_and_page_output},
      {"ui_book_scan_review_drag_reorders_pages", ui_book_scan_review_drag_reorders_pages},
      {"ui_unicode_book_scan_project_folder", ui_unicode_book_scan_project_folder},
      {"ui_book_scan_searchable_export_writes_text", ui_book_scan_searchable_export_writes_text},
      {"ui_book_scan_searchable_cancel_failure_and_script", ui_book_scan_searchable_cancel_failure_and_script},
      {"ui_book_scan_divider_decides_the_page_cut", ui_book_scan_divider_decides_the_page_cut},
      {"ui_book_scan_binding_margin_leaves_the_strip_out", ui_book_scan_binding_margin_leaves_the_strip_out},
      {"ui_book_scan_detection_off_reuses_the_layout", ui_book_scan_detection_off_reuses_the_layout},
      {"ui_book_scan_manifest_margin_and_detection_keys", ui_book_scan_manifest_margin_and_detection_keys},
      {"ui_book_scan_top_of_book_is_remembered", ui_book_scan_top_of_book_is_remembered},
  };
}
