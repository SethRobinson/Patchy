// The PDF viewer and its Print as Book (docs/pdf-viewer.md, docs/pdf-book-print.md).

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#if defined(PATCHY_HAVE_QT_PDF)

#include "core/book_imposition.hpp"
#include "import_print_resolution_test_support.hpp"
#include "ui/app_settings.hpp"
#include "ui/canvas_widget.hpp"
#include "ui/main_window.hpp"
#include "ui/pdf_book_print_dialog.hpp"
#include "ui/pdf_page_view.hpp"
#include "ui/pdf_text_search.hpp"
#include "ui/pdf_viewer_window.hpp"
#include "ui/qt_paths.hpp"
#include "ui/recent_file_list.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"
#include "unicode_path_names.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QAbstractItemView>
#include <QDialog>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFontMetrics>
#include <QFile>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTest>
#include <QTextBrowser>
#include <QToolBar>
#include <QWheelEvent>

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using patchy::test::ui::find_top_level_dialog;
using patchy::test::ui::process_events_until;
using patchy::test::ui::SettingsValueRestorer;
using patchy::test::ui::show_window;
using patchy::test::ui::when_dialog_visible;
using patchy::test::ui::import_print_resolution_support::pdf_from_objects;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::PdfPageView;
using patchy::ui::PdfTextMatch;
using patchy::ui::PdfViewerWindow;

namespace {

constexpr double kPageWidth = 200.0;
constexpr double kPageHeight = 300.0;

// Page i is filled with gray 0.1 + 0.07 i, so a sample names the page, and has a black
// square at its top-left corner, so a sample shows which way up it is.
double page_gray(int page) {
  return 0.1 + 0.07 * page;
}

std::string number(double value) {
  return QString::number(value, 'f', 3).toStdString();
}

QByteArray numbered_pdf_bytes(int pages) {
  std::vector<std::string> objects;
  objects.emplace_back("<</Type/Catalog/Pages 2 0 R>>");
  std::string kids;
  for (int page = 0; page < pages; ++page) {
    kids += std::to_string(3 + 2 * page) + " 0 R ";
  }
  objects.push_back("<</Type/Pages/Kids[" + kids + "]/Count " + std::to_string(pages) + ">>");
  for (int page = 0; page < pages; ++page) {
    const std::string gray = number(page_gray(page));
    const std::string content = gray + " " + gray + " " + gray + " rg 0 0 " + number(kPageWidth) + " " +
                                number(kPageHeight) + " re f 0 0 0 rg 0 " + number(kPageHeight - 20.0) +
                                " 20 20 re f";
    objects.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 " + number(kPageWidth) + " " + number(kPageHeight) +
                      "]/Contents " + std::to_string(4 + 2 * page) + " 0 R>>");
    objects.push_back("<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
  }
  return pdf_from_objects(objects);
}

// Pages of two 200 x 300 halves side by side, like a scanned booklet laid flat: halves[i] is
// {left, right}, each filled with page_gray(number), so a sample names the half.
QByteArray spread_pdf_bytes(const std::vector<std::pair<int, int>>& halves) {
  std::vector<std::string> objects;
  objects.emplace_back("<</Type/Catalog/Pages 2 0 R>>");
  std::string kids;
  for (std::size_t page = 0; page < halves.size(); ++page) {
    kids += std::to_string(3 + 2 * page) + " 0 R ";
  }
  objects.push_back("<</Type/Pages/Kids[" + kids + "]/Count " + std::to_string(halves.size()) + ">>");
  for (std::size_t page = 0; page < halves.size(); ++page) {
    const std::string left = number(page_gray(halves[page].first));
    const std::string right = number(page_gray(halves[page].second));
    const std::string content = left + " " + left + " " + left + " rg 0 0 " + number(kPageWidth) + " " +
                                number(kPageHeight) + " re f " + right + " " + right + " " + right + " rg " +
                                number(kPageWidth) + " 0 " + number(kPageWidth) + " " + number(kPageHeight) +
                                " re f";
    objects.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 " + number(2 * kPageWidth) + " " +
                      number(kPageHeight) + "]/Contents " + std::to_string(4 + 2 * page) + " 0 R>>");
    objects.push_back("<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
  }
  return pdf_from_objects(objects);
}

// One 200 x 200 point page in four colours: red top left, green top right, blue bottom
// left, yellow bottom right.
QByteArray quadrant_pdf_bytes() {
  const std::string content =
      "1 0 0 rg 0 100 100 100 re f 0 1 0 rg 100 100 100 100 re f 0 0 1 rg 0 0 100 100 re f "
      "1 1 0 rg 100 0 100 100 re f";
  return pdf_from_objects({"<</Type/Catalog/Pages 2 0 R>>", "<</Type/Pages/Kids[3 0 R]/Count 1>>",
                           "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]/Contents 4 0 R>>",
                           "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content +
                               "\nendstream"});
}

QString write_file(const QString& path, const QByteArray& bytes) {
  QFile::remove(path);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  file.write(bytes);
  return path;
}

// The page a sample shows: -1 for white paper (a blank page or no page).
int page_at(const QImage& image, int x, int y) {
  const QColor color = image.pixelColor(x, y);
  if (color.red() > 245 && color.green() > 245 && color.blue() > 245) {
    return -1;
  }
  return static_cast<int>(std::lround((color.redF() - 0.1) / 0.07));
}

int dark_pixels(const QImage& image, const QRect& area) {
  int count = 0;
  for (int y = area.top(); y <= area.bottom(); ++y) {
    for (int x = area.left(); x <= area.right(); ++x) {
      if (qGray(image.pixel(x, y)) < 20) {
        ++count;
      }
    }
  }
  return count;
}

bool near_color(const QColor& color, int r, int g, int b) {
  return std::abs(color.red() - r) < 40 && std::abs(color.green() - g) < 40 && std::abs(color.blue() - b) < 40;
}

void wait_for_renders(PdfPageView& view) {
  CHECK(process_events_until([&view] { return !view.render_pending(); }, 15000));
}

void send_wheel(QWidget* target, int delta) {
  const QPointF position(target->width() / 2.0, target->height() / 2.0);
  QWheelEvent event(position, target->mapToGlobal(position), QPoint(), QPoint(0, delta), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(target, &event);
}

void ui_pdf_viewer_opens_pdf_files() {
  SettingsValueRestorer viewer_setting(QStringLiteral("imports/pdfOpenInViewer"));
  SettingsValueRestorer mode_setting(QStringLiteral("imports/pdfMode"));
  SettingsValueRestorer target_setting(QStringLiteral("imports/pdfPagesTarget"));
  SettingsValueRestorer positions_setting(QStringLiteral("pdfViewer/positions"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfOpenInViewer"), true);
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_open.pdf"), numbered_pdf_bytes(5));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions_before = MainWindowTestAccess::session_count(window);

  // Open goes to the viewer: no import dialog, no document.
  MainWindowTestAccess::open_document_path_as_user(window, path);
  auto* viewer = window.findChild<PdfViewerWindow*>();
  CHECK(viewer != nullptr);
  if (viewer == nullptr) {
    return;
  }
  CHECK(viewer->isVisible());
  CHECK(MainWindowTestAccess::session_count(window) == sessions_before);
  CHECK(viewer->page_view()->page_count() == 5);
  CHECK(viewer->findChild<QPushButton*>(QStringLiteral("pdfViewerEditButton")) != nullptr);
  CHECK(viewer->findChild<QPushButton*>(QStringLiteral("pdfViewerPrintBookButton")) != nullptr);
  // Opening the same file again raises that viewer.
  MainWindowTestAccess::open_document_path_as_user(window, path);
  CHECK(window.findChildren<PdfViewerWindow*>().size() == 1);

  // Edit...: the Import PDF dialog starts on the viewer's page and opens it for editing;
  // the viewer then closes.
  viewer->page_view()->go_to_page(2);
  CHECK(viewer->page_view()->current_page() == 2);
  bool saw_dialog = false;
  when_dialog_visible(QStringLiteral("pdfImportDialog"), [&saw_dialog](QDialog& dialog) {
    saw_dialog = true;
    auto* pages = dialog.findChild<QListWidget*>(QStringLiteral("pdfImportPagesList"));
    auto* mode = dialog.findChild<QComboBox*>(QStringLiteral("pdfImportModeCombo"));
    auto* target = dialog.findChild<QComboBox*>(QStringLiteral("pdfImportPagesTargetCombo"));
    auto* resolution = dialog.findChild<QComboBox*>(QStringLiteral("pdfImportResolutionCombo"));
    auto* import_button = dialog.findChild<QPushButton*>(QStringLiteral("pdfImportButton"));
    CHECK(pages != nullptr && mode != nullptr && target != nullptr && resolution != nullptr &&
          import_button != nullptr);
    if (pages == nullptr || mode == nullptr || target == nullptr || resolution == nullptr ||
        import_button == nullptr) {
      dialog.reject();
      return;
    }
    const auto selected = pages->selectedItems();
    CHECK(selected.size() == 1 && pages->row(selected.front()) == 2);
    mode->setCurrentIndex(std::max(0, mode->findData(QStringLiteral("flatten"))));
    target->setCurrentIndex(std::max(0, target->findData(QStringLiteral("documents"))));
    resolution->setCurrentText(QStringLiteral("72"));
    import_button->click();
  });
  const QPointer<PdfViewerWindow> guard(viewer);
  viewer->edit_pages();
  CHECK(saw_dialog);
  CHECK(MainWindowTestAccess::session_count(window) == sessions_before + 1);
  CHECK(MainWindowTestAccess::document(window).width() == static_cast<int>(kPageWidth));
  CHECK(process_events_until([&guard] { return guard.isNull(); }));
  // The page read last is remembered for the next open.
  MainWindowTestAccess::open_document_path_as_user(window, path);
  viewer = window.findChild<PdfViewerWindow*>();
  CHECK(viewer != nullptr && viewer->page_view()->current_page() == 2);
  if (viewer != nullptr) {
    const QPointer<PdfViewerWindow> second(viewer);
    viewer->close();
    CHECK(process_events_until([&second] { return second.isNull(); }));
  }

  // Preferences can turn the viewer off: Open shows the Import PDF dialog again. (Another
  // file: the page imported above is open, and opening its file just switches to it.)
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfOpenInViewer"), false);
  const QString other = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_off.pdf"), numbered_pdf_bytes(2));
  bool saw_import = false;
  when_dialog_visible(QStringLiteral("pdfImportDialog"), [&saw_import](QDialog& dialog) {
    saw_import = true;
    dialog.reject();
  });
  MainWindowTestAccess::open_document_path_as_user(window, other);
  CHECK(saw_import);
  CHECK(window.findChild<PdfViewerWindow*>() == nullptr);
}

// A PDF opens in its viewer with the Home screen still up behind it: the Home screen's
// Recent Files list shows that PDF as its first row, in view, whatever was typed in the
// filter and wherever the list was scrolled.
void ui_pdf_viewer_open_updates_home_recent_list() {
  SettingsValueRestorer viewer_setting(QStringLiteral("imports/pdfOpenInViewer"));
  SettingsValueRestorer positions_setting(QStringLiteral("pdfViewer/positions"));
  SettingsValueRestorer geometry_setting(QStringLiteral("pdfViewer/geometry"));
  SettingsValueRestorer recent_files_restorer(QStringLiteral("recentFiles"));
  SettingsValueRestorer recent_folders_restorer(QStringLiteral("recentFolders"));
  SettingsValueRestorer open_dir_restorer(QStringLiteral("lastOpenDirectory"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfOpenInViewer"), true);
  const auto absolute = [](const QString& path) { return QFileInfo(path).absoluteFilePath(); };
  const QString first = absolute(write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_recent_1.pdf"), numbered_pdf_bytes(2)));
  const QString second = absolute(write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_recent_2.pdf"), numbered_pdf_bytes(3)));
  const QString third = absolute(write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_recent_3.pdf"), numbered_pdf_bytes(2)));
  const QString image_path = absolute(QStringLiteral("test-artifacts/ui_pdf_viewer_recent.png"));
  QImage image(24, 16, QImage::Format_ARGB32);
  image.fill(QColor(30, 120, 200));
  CHECK(image.save(image_path));
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), QStringList{image_path, second});
    settings.sync();
  }

  patchy::ui::MainWindow window;
  patchy::test::ui::show_window_empty(window);
  auto* panel = window.findChild<QWidget*>(QStringLiteral("startPanel"));
  auto* list = window.findChild<QListWidget*>(QStringLiteral("startPanelRecentList"));
  auto* recent_menu = window.findChild<QMenu*>(QStringLiteral("fileOpenRecentMenu"));
  CHECK(panel != nullptr && list != nullptr && recent_menu != nullptr);
  const auto rows = [list] {
    QStringList paths;
    for (int row = 0; row < list->count(); ++row) {
      paths << list->item(row)->data(patchy::ui::kRecentPathRole).toString();
    }
    return paths;
  };
  // The Home screen list and File > Open Recent File both hold exactly `wanted`, newest first.
  const auto expect = [&](const char* step, const QStringList& wanted) {
    QApplication::processEvents();
    const auto menu_rows = patchy::test::ui::recent_menu_paths(*recent_menu);
    const bool matches = panel->isVisible() && rows() == wanted && menu_rows == wanted;
    if (!matches) {
      std::cerr << "recent lists after \"" << step << "\": panel " << rows().join(QStringLiteral(" | ")).toStdString()
                << "; menu " << menu_rows.join(QStringLiteral(" | ")).toStdString() << '\n';
    }
    CHECK(matches);
  };
  const auto click_row = [list](int row) {
    const auto center = list->visualItemRect(list->item(row)).center();
    patchy::test::ui::send_mouse(*list->viewport(), QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    patchy::test::ui::send_mouse(*list->viewport(), QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
  };
  const auto close_viewers = [&window] {
    for (auto* open_viewer : window.findChildren<PdfViewerWindow*>()) {
      open_viewer->close();
    }
    CHECK(process_events_until([&window] { return window.findChildren<PdfViewerWindow*>().isEmpty(); }));
  };
  expect("start", {image_path, second});

  // A PDF not in the history: the viewer opens and the Home screen lists it first.
  MainWindowTestAccess::open_document_path_as_user(window, first);
  CHECK(window.findChildren<PdfViewerWindow*>().size() == 1);
  expect("viewer open", {first, image_path, second});
  close_viewers();
  expect("viewer closed", {first, image_path, second});

  // A PDF row of the Home screen list that is not the first.
  click_row(2);
  CHECK(window.findChildren<PdfViewerWindow*>().size() == 1);
  expect("row click", {second, first, image_path});

  // A PDF dropped on that viewer opens a viewer of its own.
  if (auto* viewer = window.findChild<PdfViewerWindow*>(); viewer != nullptr) {
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(third)});
    const QPoint position = viewer->rect().center();
    QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewer, &enter);
    QDropEvent drop(QPointF(position), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewer, &drop);
    CHECK(process_events_until([&window] { return window.findChildren<PdfViewerWindow*>().size() == 2; }));
  }
  expect("drop on viewer", {third, second, first, image_path});
  close_viewers();
  expect("viewers closed", {third, second, first, image_path});

  // Opened while the main window is minimized (the viewer is in front), then restored.
  window.showMinimized();
  QApplication::processEvents();
  MainWindowTestAccess::open_document_path_as_user(window, first);
  close_viewers();
  window.showNormal();
  QApplication::processEvents();
  expect("minimized", {first, third, second, image_path});

  // Opened while the main window is hidden, then shown: the panel was only told of a
  // change while it was showing, and nothing repaired its rows afterwards.
  window.hide();
  QApplication::processEvents();
  MainWindowTestAccess::open_document_path_as_user(window, second);
  close_viewers();
  window.show();
  QApplication::processEvents();
  expect("hidden", {second, first, third, image_path});

  // A long list scrolled to its end: a PDF opened from there is the first row in view.
  QStringList many;
  for (int index = 0; index < 13; ++index) {
    const auto png = absolute(QStringLiteral("test-artifacts/ui_pdf_viewer_recent_%1.png").arg(index, 2, 10, QLatin1Char('0')));
    CHECK(image.save(png));
    many << png;
  }
  many << third;
  {
    auto settings = patchy::ui::app_settings();
    settings.setValue(QStringLiteral("recentFiles"), many);
    settings.sync();
  }
  CHECK(process_events_until([&] { return rows() == many; }, 6000));
  auto* bar = list->verticalScrollBar();
  CHECK(bar->maximum() > 0);
  bar->setValue(bar->maximum());
  QApplication::processEvents();
  CHECK(bar->value() == bar->maximum());
  click_row(13);
  CHECK(window.findChildren<PdfViewerWindow*>().size() == 1);
  close_viewers();
  auto moved = many;
  moved.move(13, 0);
  expect("scrolled row", moved);
  (void)process_events_until([] { return false; }, 100);  // the refilled rows' layout
  CHECK(bar->value() == 0);
  CHECK(list->visualItemRect(list->item(0)).intersects(list->viewport()->rect()));

  // The panel stays up behind a viewer, so a filter typed to find a PDF used to stay too
  // and hide the next file, opened another way.
  auto* filter_edit = window.findChild<QLineEdit*>(QStringLiteral("startPanelRecentFilterEdit"));
  CHECK(filter_edit != nullptr);
  filter_edit->setText(QStringLiteral("recent_3.pdf"));
  QApplication::processEvents();
  CHECK(list->count() == 1);
  click_row(0);
  CHECK(window.findChildren<PdfViewerWindow*>().size() == 1);
  CHECK(filter_edit->text().isEmpty());
  close_viewers();
  expect("filtered row", moved);
  filter_edit->setText(QStringLiteral("recent_3.pdf"));
  QApplication::processEvents();
  MainWindowTestAccess::open_document_path_as_user(window, first);
  close_viewers();
  moved.prepend(first);
  CHECK(filter_edit->text().isEmpty());
  expect("opened past a filter", moved);
}

// Files dropped on the viewer open as they would dropped on the main window: an image as a
// document, a PDF in a viewer of its own. The viewer stays open.
void ui_pdf_viewer_opens_dropped_files() {
  SettingsValueRestorer viewer_setting(QStringLiteral("imports/pdfOpenInViewer"));
  SettingsValueRestorer positions_setting(QStringLiteral("pdfViewer/positions"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfOpenInViewer"), true);
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_drop_target.pdf"), numbered_pdf_bytes(2));
  const QString other = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_drop_other.pdf"), numbered_pdf_bytes(3));
  const QString image_path = QStringLiteral("test-artifacts/ui_pdf_viewer_drop.png");
  QImage image(24, 16, QImage::Format_ARGB32);
  image.fill(QColor(30, 120, 200));
  CHECK(image.save(image_path));
  patchy::ui::MainWindow window;
  show_window(window);
  const auto sessions = MainWindowTestAccess::session_count(window);
  MainWindowTestAccess::open_document_path_as_user(window, path);
  auto* viewer = window.findChild<PdfViewerWindow*>();
  CHECK(viewer != nullptr && viewer->acceptDrops());
  if (viewer == nullptr) {
    return;
  }
  const QPointer<PdfViewerWindow> guard(viewer);
  const auto drop_on_viewer = [viewer](const QString& file) {
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(QFileInfo(file).absoluteFilePath())});
    const QPoint position = viewer->rect().center();
    QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewer, &enter);
    CHECK(enter.isAccepted());
    QDropEvent drop(QPointF(position), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewer, &drop);
    CHECK(drop.isAccepted());
  };
  drop_on_viewer(image_path);
  CHECK(process_events_until([&window, sessions] { return MainWindowTestAccess::session_count(window) == sessions + 1; }));
  CHECK(MainWindowTestAccess::document(window).width() == 24);
  CHECK(!guard.isNull());
  drop_on_viewer(other);
  CHECK(process_events_until([&window] { return window.findChildren<PdfViewerWindow*>().size() == 2; }));
  CHECK(MainWindowTestAccess::session_count(window) == sessions + 1);
  for (auto* open_viewer : window.findChildren<PdfViewerWindow*>()) {
    open_viewer->close();
  }
  CHECK(process_events_until([&window] { return window.findChildren<PdfViewerWindow*>().isEmpty(); }));
}

void ui_pdf_viewer_spreads_and_page_turns() {
  SettingsValueRestorer pages_setting(QStringLiteral("pdfViewer/pages"));
  SettingsValueRestorer continuous_setting(QStringLiteral("pdfViewer/continuous"));
  SettingsValueRestorer direction_setting(QStringLiteral("pdfViewer/direction"));
  SettingsValueRestorer zoom_setting(QStringLiteral("pdfViewer/zoom"));
  SettingsValueRestorer geometry_setting(QStringLiteral("pdfViewer/geometry"));
  SettingsValueRestorer positions_setting(QStringLiteral("pdfViewer/positions"));
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_spreads.pdf"), numbered_pdf_bytes(7));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = PdfViewerWindow::open(&window, path, {});
  CHECK(viewer != nullptr);
  if (viewer == nullptr) {
    return;
  }
  viewer->resize(900, 700);
  viewer->show();
  QApplication::processEvents();
  auto* view = viewer->page_view();
  view->set_continuous(false);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPagesCoverAlone);
  view->first_page();
  CHECK(view->current_pages() == std::vector<int>({0}));
  view->next_row();
  CHECK(view->current_pages() == std::vector<int>({1, 2}));
  // Right to left: the left arrow goes forward.
  view->set_reading_direction(patchy::PageReadingDirection::RightToLeft);
  QTest::keyClick(view, Qt::Key_Left);
  CHECK(view->current_pages() == std::vector<int>({3, 4}));
  QTest::keyClick(view, Qt::Key_Right);
  CHECK(view->current_pages() == std::vector<int>({1, 2}));
  view->set_reading_direction(patchy::PageReadingDirection::LeftToRight);
  // One notch of the wheel at the page edge turns the page; the next notch right after it
  // is the tail of the same gesture and turns nothing.
  send_wheel(view->viewport(), -120);
  CHECK(view->current_pages() == std::vector<int>({3, 4}));
  send_wheel(view->viewport(), -120);
  CHECK(view->current_pages() == std::vector<int>({3, 4}));
  QTest::keyClick(view, Qt::Key_End);
  CHECK(view->current_pages() == std::vector<int>({5, 6}));
  QTest::keyClick(view, Qt::Key_Home);
  CHECK(view->current_pages() == std::vector<int>({0}));
  // The toolbar's page field follows.
  auto* page_spin = viewer->findChild<QSpinBox*>(QStringLiteral("pdfViewerPageSpin"));
  CHECK(page_spin != nullptr && page_spin->value() == 1 && page_spin->maximum() == 7);
  if (page_spin != nullptr) {
    page_spin->setValue(6);
    CHECK(view->current_pages() == std::vector<int>({5, 6}));
  }
  wait_for_renders(*view);
  CHECK(view->cached_width_for_testing(5) > 0 && view->cached_width_for_testing(6) > 0);
  viewer->grab().save(QStringLiteral("test-artifacts/ui_pdf_viewer_spreads.png"));
  // The zoom and layout lists never elide an entry and show them all without scrolling.
  for (const auto* name : {"pdfViewerZoomCombo", "pdfViewerLayoutCombo"}) {
    auto* combo = viewer->findChild<QComboBox*>(QLatin1String(name));
    CHECK(combo != nullptr);
    if (combo == nullptr) {
      continue;
    }
    const QFontMetrics metrics(combo->view()->font());
    for (int index = 0; index < combo->count(); ++index) {
      CHECK(combo->view()->minimumWidth() >= metrics.horizontalAdvance(combo->itemText(index)) + 24);
    }
    CHECK(combo->view()->textElideMode() == Qt::ElideNone && combo->maxVisibleItems() >= combo->count());
    // Open, the list really is that wide.
    combo->showPopup();
    QApplication::processEvents();
    int widest = 0;
    for (int index = 0; index < combo->count(); ++index) {
      widest = std::max(widest, metrics.horizontalAdvance(combo->itemText(index)));
    }
    CHECK(combo->view()->viewport()->width() >= widest + 8);
    combo->view()->window()->grab().save(QStringLiteral("test-artifacts/") + QLatin1String(name) +
                                          QStringLiteral(".png"));
    combo->hidePopup();
  }
  // Thumbnail cells span the list, so each page is centred in it.
  auto* thumbnails = viewer->findChild<QListWidget*>(QStringLiteral("pdfViewerThumbnails"));
  CHECK(thumbnails != nullptr && thumbnails->count() == 7);
  if (thumbnails != nullptr && thumbnails->count() > 0) {
    const QRect cell = thumbnails->visualItemRect(thumbnails->item(0));
    CHECK(std::abs(cell.center().x() - thumbnails->viewport()->rect().center().x()) <= 2);
  }

  // Continuous: the wheel scrolls through the pages instead.
  view->set_continuous(true);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_fit_mode(patchy::PageFitMode::Width);
  view->first_page();
  QApplication::processEvents();
  const int before = view->verticalScrollBar()->value();
  send_wheel(view->viewport(), -120);
  CHECK(view->verticalScrollBar()->value() > before);
  CHECK(view->verticalScrollBar()->maximum() > 0);

  // Zoom steps and actual size.
  view->set_zoom_percent(100.0);
  CHECK(std::abs(view->zoom_percent() - 100.0) < 1e-6 && view->fit_mode() == patchy::PageFitMode::None);
  view->zoom_in();
  CHECK(std::abs(view->zoom_percent() - 125.0) < 1e-6);

  // Full screen hides the chrome; Esc brings it back.
  viewer->set_full_screen(true);
  CHECK(viewer->full_screen_mode() && !viewer->findChild<QToolBar*>(QStringLiteral("pdfViewerToolbar"))->isVisible());
  QTest::keyClick(view, Qt::Key_Escape);
  CHECK(!viewer->full_screen_mode() && viewer->findChild<QToolBar*>(QStringLiteral("pdfViewerToolbar"))->isVisible());

  // The viewer's keys beat the main window's application-wide hotkeys of the same keys
  // (Ctrl+0 is Fit on Screen there, Ctrl+W closes its document): with both bound, Qt would
  // find the key ambiguous and run neither.
  MainWindowTestAccess::create_document(window, 64, 64);
  const auto sessions = MainWindowTestAccess::session_count(window);
  viewer->activateWindow();
  CHECK(process_events_until([viewer] { return viewer->isActiveWindow(); }));
  view->set_zoom_percent(100.0);
  QTest::keyClick(view, Qt::Key_0, Qt::ControlModifier);
  CHECK(view->fit_mode() == patchy::PageFitMode::WholePage);
  QTest::keyClick(page_spin != nullptr ? static_cast<QWidget*>(page_spin) : view, Qt::Key_2, Qt::ControlModifier);
  CHECK(view->fit_mode() == patchy::PageFitMode::Width);
  const QPointer<PdfViewerWindow> guard(viewer);
  // A literal Ctrl+W: QKeySequence(QKeySequence::Close) is Ctrl+F4 on Windows.
  QTest::keyClick(view, Qt::Key_W, Qt::ControlModifier);
  CHECK(process_events_until([&guard] { return guard.isNull(); }));
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  if (!guard.isNull()) {
    viewer->close();
    process_events_until([&guard] { return guard.isNull(); });
  }
  // The layout choices persist.
  CHECK(patchy::ui::app_settings().value(QStringLiteral("pdfViewer/pages")).toString() == QStringLiteral("single"));
  CHECK(patchy::ui::app_settings().value(QStringLiteral("pdfViewer/continuous")).toBool());
}

// Ctrl+W, each of the platform's Close keys (Ctrl+F4 on Windows) and the key the user gave
// File > Close close the viewer they are typed in and leave the main window's documents
// alone. Typed in the main window, Ctrl+W still closes its document while a viewer is open.
void ui_pdf_viewer_close_keys_close_only_the_viewer() {
  SettingsValueRestorer viewer_setting(QStringLiteral("imports/pdfOpenInViewer"));
  SettingsValueRestorer pages_setting(QStringLiteral("pdfViewer/pages"));
  SettingsValueRestorer continuous_setting(QStringLiteral("pdfViewer/continuous"));
  SettingsValueRestorer direction_setting(QStringLiteral("pdfViewer/direction"));
  SettingsValueRestorer zoom_setting(QStringLiteral("pdfViewer/zoom"));
  SettingsValueRestorer sidebar_setting(QStringLiteral("pdfViewer/sidebar"));
  SettingsValueRestorer geometry_setting(QStringLiteral("pdfViewer/geometry"));
  SettingsValueRestorer positions_setting(QStringLiteral("pdfViewer/positions"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfOpenInViewer"), true);
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_close_keys.pdf"), numbered_pdf_bytes(2));
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 64, 64);
  MainWindowTestAccess::create_document(window, 48, 48);

  // The viewer as the user gets it (File > Open), active, or nullptr.
  const auto open_viewer = [&window, &path]() -> PdfViewerWindow* {
    MainWindowTestAccess::open_document_path_as_user(window, path);
    auto* viewer = window.findChild<PdfViewerWindow*>();
    CHECK(viewer != nullptr);
    if (viewer != nullptr) {
      CHECK(process_events_until([viewer] { return viewer->isActiveWindow(); }));
    }
    return viewer;
  };
  // Types `key` into `target` (the page area when null) of a fresh viewer. True when the
  // viewer then closed, or stayed open, as `closes` says, and the main window kept its
  // documents; otherwise says what happened. A viewer the key left open is closed again, so
  // each key starts from the same state.
  using KeyTarget = std::function<QWidget*(PdfViewerWindow&)>;
  const auto typed_in_viewer = [&window, &open_viewer](QKeyCombination key, bool closes, const KeyTarget& target = {}) {
    auto* viewer = open_viewer();
    if (viewer == nullptr) {
      return false;
    }
    const QPointer<PdfViewerWindow> guard(viewer);
    const auto sessions = MainWindowTestAccess::session_count(window);
    QWidget* widget = target ? target(*viewer) : viewer->page_view();
    CHECK(widget != nullptr);
    if (widget != nullptr) {
      QTest::keyClick(widget, key.key(), key.keyboardModifiers());
    }
    const bool closed = process_events_until([&guard] { return guard.isNull(); }, closes ? 3000 : 300);
    const auto sessions_after = MainWindowTestAccess::session_count(window);
    if (!guard.isNull()) {
      viewer->close();
      CHECK(process_events_until([&guard] { return guard.isNull(); }));
    }
    const bool as_expected = closed == closes && sessions_after == sessions;
    if (!as_expected) {
      std::cerr << QKeySequence(key).toString(QKeySequence::PortableText).toStdString()
                << " typed in the viewer: viewer " << (closed ? "closed" : "still open") << ", main window documents "
                << sessions << " -> " << sessions_after << '\n';
    }
    return as_expected;
  };
  const auto page_field = [](PdfViewerWindow& viewer) {
    return static_cast<QWidget*>(viewer.findChild<QSpinBox*>(QStringLiteral("pdfViewerPageSpin")));
  };

  // Ctrl+W, typed in the page area and in the toolbar's page field.
  const QKeyCombination ctrl_w(Qt::ControlModifier, Qt::Key_W);
  CHECK(typed_in_viewer(ctrl_w, true));
  CHECK(typed_in_viewer(ctrl_w, true, page_field));
  // Every Close key of the platform, not only its first.
  const auto platform_close_keys = QKeySequence::keyBindings(QKeySequence::Close);
  CHECK(!platform_close_keys.isEmpty());
#if defined(Q_OS_WIN)
  CHECK(platform_close_keys.contains(QKeySequence(Qt::CTRL | Qt::Key_F4)));
#endif
  for (const auto& key : platform_close_keys) {
    CHECK(typed_in_viewer(key[0], true));
  }

  // The main window focused, a viewer open: Ctrl+W closes the main window's document, and
  // the viewer stays.
  if (auto* viewer = open_viewer(); viewer != nullptr) {
    const QPointer<PdfViewerWindow> guard(viewer);
    const auto sessions = MainWindowTestAccess::session_count(window);
    window.activateWindow();
    CHECK(process_events_until([&window] { return window.isActiveWindow(); }));
    QTest::keyClick(MainWindowTestAccess::canvas(window), Qt::Key_W, Qt::ControlModifier);
    CHECK(process_events_until(
        [&window, sessions] { return MainWindowTestAccess::session_count(window) == sessions - 1; }, 1000));
    CHECK(!guard.isNull() && viewer->isVisible());
    if (!guard.isNull()) {
      viewer->close();
      CHECK(process_events_until([&guard] { return guard.isNull(); }));
    }
  }

  // File > Close moved to another key: that key closes the viewer too, and Ctrl+W still does.
  auto* file_close = window.findChild<QAction*>(QStringLiteral("fileCloseAction"));
  CHECK(file_close != nullptr);
  if (file_close == nullptr) {
    return;
  }
  file_close->setShortcuts({QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F9)});
  const QKeyCombination ctrl_alt_f9(Qt::ControlModifier | Qt::AltModifier, Qt::Key_F9);
  CHECK(typed_in_viewer(ctrl_alt_f9, true));
  CHECK(typed_in_viewer(ctrl_alt_f9, true, page_field));
  CHECK(typed_in_viewer(ctrl_w, true));
  // A plain letter as the Close key closes from the page area, and is still a letter in a
  // text field of the viewer.
  file_close->setShortcuts({QKeySequence(Qt::Key_J)});
  CHECK(typed_in_viewer(QKeyCombination(Qt::Key_J), true));
  QString typed;
  const KeyTarget zoom_field = [&typed](PdfViewerWindow& viewer) -> QWidget* {
    auto* zoom = viewer.findChild<QComboBox*>(QStringLiteral("pdfViewerZoomCombo"));
    if (zoom == nullptr || zoom->lineEdit() == nullptr) {
      return nullptr;
    }
    zoom->lineEdit()->clear();
    QObject::connect(zoom->lineEdit(), &QLineEdit::textEdited, zoom, [&typed](const QString& text) { typed = text; });
    return zoom->lineEdit();
  };
  CHECK(typed_in_viewer(QKeyCombination(Qt::Key_J), false, zoom_field));
  CHECK(typed == QStringLiteral("j"));
  // A key of the viewer's own keeps its meaning there when File > Close is given the same key.
  file_close->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_0)});
  CHECK(typed_in_viewer(QKeyCombination(Qt::ControlModifier, Qt::Key_0), false));
}

// The view renders the visible part of a page zoomed past a whole-page image through Qt PDF's
// scaled clip rectangle, and turns the page itself. Both are pinned here: a clip must give
// the same pixels as that part of the whole page, and each quarter turn must show the right
// corner of the page at the top left.
void ui_pdf_viewer_detail_renders_match_the_page() {
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_quadrants.pdf"), quadrant_pdf_bytes());
  QPdfDocument document;
  CHECK(document.load(path) == QPdfDocument::Error::None);
  QPdfDocumentRenderOptions options;
  options.setScaledSize(QSize(200, 200));
  options.setScaledClipRect(QRect(100, 0, 100, 100));
  const QImage top_right = document.render(0, QSize(100, 100), options);
  CHECK(near_color(top_right.pixelColor(50, 50), 0, 255, 0));
  options.setScaledClipRect(QRect(0, 100, 100, 100));
  const QImage bottom_left = document.render(0, QSize(100, 100), options);
  CHECK(near_color(bottom_left.pixelColor(50, 50), 0, 0, 255));

  SettingsValueRestorer zoom_setting(QStringLiteral("pdfViewer/zoom"));
  SettingsValueRestorer continuous_setting(QStringLiteral("pdfViewer/continuous"));
  SettingsValueRestorer geometry_setting(QStringLiteral("pdfViewer/geometry"));
  SettingsValueRestorer positions_setting(QStringLiteral("pdfViewer/positions"));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = PdfViewerWindow::open(&window, path, {});
  CHECK(viewer != nullptr);
  if (viewer == nullptr) {
    return;
  }
  viewer->resize(800, 600);
  viewer->show();
  QApplication::processEvents();
  auto* view = viewer->page_view();
  view->set_continuous(false);
  view->set_zoom_percent(1600.0);
  // Top left of the page, past the 16 px margin.
  const auto top_left_color = [view] {
    view->horizontalScrollBar()->setValue(0);
    view->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    wait_for_renders(*view);
    return view->viewport()->grab().toImage().pixelColor(60, 60);
  };
  CHECK(near_color(top_left_color(), 255, 0, 0));
  // The page is far past a whole-page image: the sharp render is a detail of it.
  CHECK(view->cached_width_for_testing(0) >= 3000);
  view->rotate_clockwise();  // a quarter turn clockwise brings bottom left to the top left
  CHECK(near_color(top_left_color(), 0, 0, 255));
  view->rotate_clockwise();
  CHECK(near_color(top_left_color(), 255, 255, 0));
  view->rotate_clockwise();
  CHECK(near_color(top_left_color(), 0, 255, 0));
  const QPointer<PdfViewerWindow> guard(viewer);
  viewer->close();
  CHECK(process_events_until([&guard] { return guard.isNull(); }));
}

void ui_pdf_book_print_imposes_sheets() {
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_book_pages.pdf"), numbered_pdf_bytes(6));
  QPdfDocument document;
  CHECK(document.load(path) == QPdfDocument::Error::None);
  patchy::ui::PdfBookPrintOptions options;
  options.paper = QPageSize::Letter;
  options.marks = false;
  constexpr double kDpi = 36.0;  // a Letter sheet is 396 x 306
  const auto sizes = patchy::ui::book_pdf_page_sizes(document);
  const auto side = [&](int sheet, bool back) {
    const auto pages = patchy::ui::book_source_pages(options, sizes);
    const auto imposition = patchy::ui::book_imposition_for(options, pages);
    return patchy::ui::render_book_sheet_side(document, options, imposition, pages, sheet, back, kDpi);
  };
  // Booklet of 6 pages padded to 8: [blank|1][2|blank] and [6|3][4|5] (one-based).
  QImage image = side(0, false);
  CHECK(image.size() == QSize(396, 306));
  CHECK(page_at(image, 99, 153) == -1 && page_at(image, 297, 153) == 0);
  image = side(0, true);
  CHECK(page_at(image, 99, 153) == 1 && page_at(image, 297, 153) == -1);
  image = side(1, false);
  CHECK(page_at(image, 99, 153) == 5 && page_at(image, 297, 153) == 2);
  // The last page as the back cover: it shares the outside of sheet 1 with the cover.
  options.last_page_is_back_cover = true;
  image = side(0, false);
  CHECK(page_at(image, 99, 153) == 5 && page_at(image, 297, 153) == 0);
  // Backs for a long-edge printer are turned half way round.
  options.turn_backs = true;
  image = side(1, true);
  CHECK(page_at(image, 99, 153) == 4 && page_at(image, 297, 153) == 3);
  options.turn_backs = false;
  options.last_page_is_back_cover = false;
  // Right to left mirrors the sheet: the cover on the left half.
  options.right_to_left = true;
  image = side(0, false);
  CHECK(page_at(image, 99, 153) == 0 && page_at(image, 297, 153) == -1);
  options.right_to_left = false;
  // A page range prints only those pages.
  options.first_page = 2;
  options.last_page = 5;
  image = side(0, false);
  CHECK(page_at(image, 99, 153) == 5 && page_at(image, 297, 153) == 2);
  options.first_page = 0;
  options.last_page = -1;

  // The mini zine: four by two, top row upside down.
  options.style = patchy::BookPrintStyle::MiniZine;
  image = side(0, false);
  const int cell_w = 99;
  const int cell_h = 153;
  const auto centre = [&](int column, int row) { return page_at(image, column * cell_w + 49, row * cell_h + 76); };
  CHECK(centre(0, 0) == 4 && centre(1, 0) == 3 && centre(2, 0) == 2 && centre(3, 0) == 1);
  CHECK(centre(0, 1) == 5 && centre(1, 1) == -1 && centre(2, 1) == -1 && centre(3, 1) == 0);
  // The black corner square: bottom right of an upside-down panel, top left of an upright one.
  const QRect top_cell(0, 0, cell_w, cell_h);
  const QRect bottom_cell(0, cell_h, cell_w, cell_h);
  const auto upper_left = [](QRect cell) { return QRect(cell.topLeft(), cell.size() / 2); };
  const auto lower_right = [](QRect cell) {
    return QRect(cell.topLeft() + QPoint(cell.width() / 2, cell.height() / 2), cell.size() / 2);
  };
  CHECK(dark_pixels(image, lower_right(top_cell)) > 4 && dark_pixels(image, upper_left(top_cell)) == 0);
  CHECK(dark_pixels(image, upper_left(bottom_cell)) > 4 && dark_pixels(image, lower_right(bottom_cell)) == 0);

  // The steps name what the first sheet shows.
  options.style = patchy::BookPrintStyle::Booklet;
  const auto imposition = patchy::ui::book_imposition_for(options, patchy::ui::book_source_pages(options, sizes));
  const auto guide = patchy::ui::book_assembly_guide(options, imposition, patchy::ui::book_source_pages(options, sizes));
  CHECK(guide.steps.size() == 6U && guide.notes.size() == 2);
  CHECK(guide.steps[1].title == QStringLiteral("Check sheet 1"));
  const QString text = patchy::ui::book_assembly_text(guide);
  CHECK(text.contains(QStringLiteral("One side shows a blank page and page 1 side by side.")));
  // Staple the flat stack first, then fold it all at once.
  CHECK(text.indexOf(QStringLiteral("Staple along the centre line")) < text.indexOf(QStringLiteral("Fold it in half")));
  const QString html = patchy::ui::book_assembly_html(guide);
  CHECK(html.contains(QStringLiteral("<ol>")) && html.contains(QStringLiteral("<ul>")));

  // Page numbers: the book's own numbering under each page, none on the covers when asked.
  options.marks = false;
  options.page_numbers = patchy::ui::BookPageNumbers::SkipCovers;
  constexpr double kNumberDpi = 150.0;
  const auto number_ink = [&](int sheet, bool back, int x0, int x1) {
    const auto pages = patchy::ui::book_source_pages(options, sizes);
    const auto numbered = patchy::ui::book_imposition_for(options, pages);
    const QImage sheet_image =
        patchy::ui::render_book_sheet_side(document, options, numbered, pages, sheet, back, kNumberDpi);
    // The strip below the pages: a Letter half is 396 points across, a 200 x 300 page fits it by
    // height (the outer margin of 5 mm) and leaves the margin under it.
    const int top = static_cast<int>(std::lround((612.0 - 14.17) / 72.0 * kNumberDpi));
    return dark_pixels(sheet_image, QRect(x0, top, x1 - x0, sheet_image.height() - top - 1));
  };
  const int half = static_cast<int>(std::lround(396.0 / 72.0 * kNumberDpi));
  CHECK(number_ink(0, false, half, 2 * half - 1) == 0);  // the front cover
  CHECK(number_ink(0, true, 0, half - 1) > 0);           // page 2
  options.page_numbers = patchy::ui::BookPageNumbers::All;
  CHECK(number_ink(0, false, half, 2 * half - 1) > 0);
}

// A PDF of two-page spreads prints its halves as the book's pages: a booklet sheet holds two
// of them, not two whole spreads (four pages to a sheet).
void ui_pdf_book_print_splits_spreads() {
  // The outside of the cover first (back cover 7 beside front cover 0), then spreads 1-2 to 5-6.
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_book_spreads.pdf"),
                                  spread_pdf_bytes({{7, 0}, {1, 2}, {3, 4}, {5, 6}}));
  QPdfDocument document;
  CHECK(document.load(path) == QPdfDocument::Error::None);
  const auto sizes = patchy::ui::book_pdf_page_sizes(document);
  patchy::ui::PdfBookPrintOptions options;
  options.paper = QPageSize::Letter;
  options.marks = false;
  constexpr double kDpi = 36.0;  // a Letter sheet is 396 x 306, a cell 198 x 306
  const auto side = [&](int sheet, bool back) {
    const auto pages = patchy::ui::book_source_pages(options, sizes);
    const auto imposition = patchy::ui::book_imposition_for(options, pages);
    return patchy::ui::render_book_sheet_side(document, options, imposition, pages, sheet, back, kDpi);
  };
  const auto cells = [](const QImage& image) { return std::make_pair(page_at(image, 99, 153), page_at(image, 297, 153)); };
  // Unsplit, each spread is one book page.
  CHECK(patchy::ui::book_source_pages(options, sizes).size() == 4U);
  // Split: 8 pages in file order, the cover's back half first.
  options.spreads = patchy::BookSpreads::Split;
  CHECK(patchy::ui::book_source_pages(options, sizes).size() == 8U);
  CHECK(cells(side(0, false)) == std::make_pair(6, 7));
  // Cover spread: front cover first, back cover last, so the booklet reads 0 to 7.
  options.spreads = patchy::BookSpreads::SplitCoverSpread;
  QImage image = side(0, false);
  image.save(QStringLiteral("test-artifacts/ui_pdf_book_spreads_front.png"));
  CHECK(cells(image) == std::make_pair(7, 0));
  CHECK(cells(side(0, true)) == std::make_pair(1, 6));
  CHECK(cells(side(1, false)) == std::make_pair(5, 2));
  CHECK(cells(side(1, true)) == std::make_pair(3, 4));
  // The check step names the halves.
  const auto pages = patchy::ui::book_source_pages(options, sizes);
  const QString text = patchy::ui::book_assembly_text(
      patchy::ui::book_assembly_guide(options, patchy::ui::book_imposition_for(options, pages), pages));
  CHECK(text.contains(QStringLiteral("the left half of page 1 and the right half of page 1")));
  // One spread short (6 pages padded to 8): the blanks go before the back cover, which stays
  // on the outside: 0 1 2 3 4 blank blank 7.
  options.last_page = 2;
  CHECK(cells(side(0, false)) == std::make_pair(7, 0));
  CHECK(cells(side(1, false)) == std::make_pair(-1, 2));

  // The dialog: wide pages get a hint, the cover spread locks "Keep the last page as the back
  // cover" on, and the user's own choice comes back after.
  SettingsValueRestorer back_cover_setting(QStringLiteral("pdfBookPrint/backCover"));
  patchy::ui::app_settings().setValue(QStringLiteral("pdfBookPrint/backCover"), false);
  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  when_dialog_visible(QStringLiteral("pdfBookPrintDialog"), [&saw_dialog](QDialog& dialog) {
    saw_dialog = true;
    auto* spreads = dialog.findChild<QComboBox*>(QStringLiteral("pdfBookPrintSpreadsCombo"));
    auto* summary = dialog.findChild<QLabel*>(QStringLiteral("pdfBookPrintSummaryLabel"));
    auto* back_cover = dialog.findChild<QCheckBox*>(QStringLiteral("pdfBookPrintBackCoverCheck"));
    auto* style = dialog.findChild<QComboBox*>(QStringLiteral("pdfBookPrintStyleCombo"));
    CHECK(spreads != nullptr && summary != nullptr && back_cover != nullptr && style != nullptr);
    if (spreads == nullptr || summary == nullptr || back_cover == nullptr || style == nullptr) {
      dialog.reject();
      return;
    }
    style->setCurrentIndex(std::max(0, style->findData(QStringLiteral("booklet"))));
    CHECK(spreads->currentData().toString() == QStringLiteral("none"));
    CHECK(summary->text().contains(QStringLiteral("Two-page spreads")));
    spreads->setCurrentIndex(spreads->findData(QStringLiteral("cover")));
    CHECK(summary->text().startsWith(QStringLiteral("8 page(s)")) && !summary->text().contains(QStringLiteral("Two-page")));
    CHECK(!back_cover->isEnabled() && back_cover->isChecked());
    dialog.grab().save(QStringLiteral("test-artifacts/ui_pdf_book_spreads_dialog.png"));
    spreads->setCurrentIndex(spreads->findData(QStringLiteral("split")));
    CHECK(back_cover->isEnabled() && !back_cover->isChecked());
    dialog.reject();
  });
  patchy::ui::run_pdf_book_print_dialog(&window, document, path, {});
  CHECK(saw_dialog);
}

void ui_pdf_book_print_writes_sheets_pdf() {
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_book_source.pdf"), numbered_pdf_bytes(6));
  QPdfDocument document;
  CHECK(document.load(path) == QPdfDocument::Error::None);
  patchy::ui::PdfBookPrintOptions options;
  options.paper = QPageSize::Letter;
  options.dpi = 150;
  options.steps_page = true;
  const QString out = QStringLiteral("test-artifacts/ui_pdf_book_sheets.pdf");
  QFile::remove(out);
  int calls = 0;
  const auto result = patchy::ui::write_pdf_book(document, options, out, [&calls](int done, int count) {
    ++calls;
    return done <= count;
  });
  CHECK(result.ok && result.pages == 5 && calls == 5);
  QPdfDocument written;
  CHECK(written.load(out) == QPdfDocument::Error::None);
  CHECK(written.pageCount() == 5);
  const QSizeF sheet = written.pagePointSize(0);
  CHECK(std::abs(sheet.width() - 792.0) < 0.5 && std::abs(sheet.height() - 612.0) < 0.5);
  const QImage front = written.render(0, QSize(396, 306));
  CHECK(page_at(front, 297, 153) == 0);
  // Cancel keeps the file that was there.
  const auto size_before = QFileInfo(out).size();
  const auto cancelled = patchy::ui::write_pdf_book(document, options, out, [](int, int) { return false; });
  CHECK(cancelled.cancelled && !cancelled.ok && QFileInfo(out).size() == size_before);

  // A Unicode folder and name, both for the viewer's open and the book written from it.
  const auto directory = patchy::test::unicode_artifact_dir(u8"pdf-viewer");
  const auto unicode_source = patchy::ui::to_qstring(
      directory / patchy::test::unicode_path_piece(std::u8string(patchy::test::kUnicodeCombinedStem) + u8".pdf"));
  write_file(unicode_source, numbered_pdf_bytes(4));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = PdfViewerWindow::open(&window, unicode_source, {});
  CHECK(viewer != nullptr && viewer->page_view()->page_count() == 4);
  if (viewer != nullptr) {
    const auto unicode_out = patchy::ui::to_qstring(
        directory / patchy::test::unicode_path_piece(std::u8string(patchy::test::kUnicodePathStems[0]) + u8" book.pdf"));
    options.steps_page = false;
    options.dpi = 72;
    const auto unicode_result = patchy::ui::write_pdf_book(*viewer->document(), options, unicode_out);
    CHECK(unicode_result.ok && unicode_result.pages == 2 && QFileInfo::exists(unicode_out));
    const QPointer<PdfViewerWindow> guard(viewer);
    viewer->close();
    CHECK(process_events_until([&guard] { return guard.isNull(); }));
  }
}

void ui_pdf_book_print_dialog_follows_choices() {
  SettingsValueRestorer style_setting(QStringLiteral("pdfBookPrint/style"));
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_book_dialog.pdf"), numbered_pdf_bytes(6));
  QPdfDocument document;
  CHECK(document.load(path) == QPdfDocument::Error::None);
  patchy::ui::MainWindow window;
  show_window(window);
  bool saw_dialog = false;
  when_dialog_visible(QStringLiteral("pdfBookPrintDialog"), [&saw_dialog](QDialog& dialog) {
    saw_dialog = true;
    auto* style = dialog.findChild<QComboBox*>(QStringLiteral("pdfBookPrintStyleCombo"));
    auto* summary = dialog.findChild<QLabel*>(QStringLiteral("pdfBookPrintSummaryLabel"));
    auto* sheet = dialog.findChild<QSpinBox*>(QStringLiteral("pdfBookPrintSheetSpin"));
    auto* steps = dialog.findChild<QTextBrowser*>(QStringLiteral("pdfBookPrintStepsView"));
    auto* front = dialog.findChild<QLabel*>(QStringLiteral("pdfBookPrintFrontPreview"));
    auto* back = dialog.findChild<QLabel*>(QStringLiteral("pdfBookPrintBackPreview"));
    CHECK(style != nullptr && summary != nullptr && sheet != nullptr && steps != nullptr && front != nullptr &&
          back != nullptr);
    if (style == nullptr || summary == nullptr || sheet == nullptr || steps == nullptr || front == nullptr ||
        back == nullptr) {
      dialog.reject();
      return;
    }
    style->setCurrentIndex(std::max(0, style->findData(QStringLiteral("booklet"))));
    CHECK(sheet->maximum() == 2);
    CHECK(process_events_until([front] { return !front->pixmap().isNull(); }, 10000));
    CHECK(steps->toPlainText().contains(QStringLiteral("Staple along the centre line")));
    dialog.grab().save(QStringLiteral("test-artifacts/ui_pdf_book_print_dialog.png"));
    // The zine is one-sided: no second side to preview.
    style->setCurrentIndex(std::max(0, style->findData(QStringLiteral("zine"))));
    CHECK(sheet->maximum() == 1 && !back->isVisible());
    CHECK(summary->text().contains(QStringLiteral("2 blank page")));
    CHECK(steps->toPlainText().contains(QStringLiteral("Cut the middle")));
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("pdfBookPrintPageNumbersCombo")) != nullptr);
    dialog.reject();
  });
  patchy::ui::PdfBookPrintStart start;
  patchy::ui::run_pdf_book_print_dialog(&window, document, path, start);
  CHECK(saw_dialog);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("pdfBookPrint/style")).toString() == QStringLiteral("zine"));
}

// --- Find (docs/pdf-viewer.md, "Find") ---

constexpr double kTextPageWidth = 300.0;
constexpr double kTextPageHeight = 400.0;

// Pages of Helvetica text, one Tj per line (so PDFium sees the line breaks), WinAnsi encoded:
// write a character past ASCII as its octal escape. A page without lines has no text at all.
QByteArray text_pdf_bytes(const std::vector<std::vector<std::string>>& pages, double width = kTextPageWidth,
                          double height = kTextPageHeight, double font_size = 14.0, double left = 30.0,
                          double top_margin = 40.0) {
  std::vector<std::string> objects;
  objects.emplace_back("<</Type/Catalog/Pages 2 0 R>>");
  std::string kids;
  for (std::size_t page = 0; page < pages.size(); ++page) {
    kids += std::to_string(4 + 2 * page) + " 0 R ";
  }
  objects.push_back("<</Type/Pages/Kids[" + kids + "]/Count " + std::to_string(pages.size()) + ">>");
  objects.emplace_back("<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>");
  for (std::size_t page = 0; page < pages.size(); ++page) {
    std::string content;
    if (!pages[page].empty()) {
      content = "BT /F1 " + number(font_size) + " Tf " + number(left) + " " + number(height - top_margin) + " Td " +
                number(font_size * 1.5) + " TL";
      for (std::size_t line = 0; line < pages[page].size(); ++line) {
        content += (line == 0 ? " (" : " T* (") + pages[page][line] + ") Tj";
      }
      content += " ET";
    } else {
      content = "0.8 0.8 0.8 rg 40 40 80 80 re f";
    }
    objects.push_back("<</Type/Page/Parent 2 0 R/MediaBox[0 0 " + number(width) + " " + number(height) +
                      "]/Resources<</Font<</F1 3 0 R>>>>/Contents " + std::to_string(5 + 2 * page) + " 0 R>>");
    objects.push_back("<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream");
  }
  return pdf_from_objects(objects);
}

// Five pages. "fox" is on pages 1, 2 and 5 (seven times, three of them not lower case, one
// inside "foxes"); page 3 breaks words and a phrase across lines; page 4 has no text.
QByteArray find_fixture_pdf_bytes() {
  return text_pdf_bytes({
      {"The quick brown fox jumps", "over the lazy dog. Patchy", "opens PDF files."},
      {"A fox is quick. FOX and Fox", "foxes are not one fox."},
      {"This line ends with an exam-", "ple of a hyphenated word and", "a well-", "known phrase split",
       "across lines."},
      {},
      {"\\304rger and \\344rger", "The last fox."},
  });
}

// A six-page booklet of ordinary prose for the card's screenshot and for trying Find by hand
// (the same file is the desk's test file).
QByteArray find_sample_pdf_bytes() {
  const std::vector<std::vector<std::string>> pages = {
      {"A Short Guide to Layers", "",
       "Every picture in an editor is a stack of layers. A layer is a sheet of",
       "pixels, and the sheets are laid one over another like panes of glass.",
       "What you see on the canvas is the whole stack looked at from above.",
       "", "Layers let you change one part of a picture and leave the rest alone.",
       "Paint on a new layer and the photo under it is untouched. Hide the layer",
       "and the paint is gone; show it again and the paint is back. Nothing is",
       "lost until you flatten the stack.", "",
       "The Layers panel lists the stack from top to bottom. Click a row to make",
       "that layer the active one. The active layer is the one your tools change.",
       "Drag a row up or down to move its layer in the stack."},
      {"Opacity and Blending", "",
       "Each layer has an opacity. At 100 percent the layer hides what is under",
       "it. At 50 percent the layer below shows through, half and half. Opacity",
       "is the quickest way to soften an effect you have painted on its own layer.",
       "", "A blend mode decides how a layer mixes with the layers below. Normal",
       "simply covers them. Multiply darkens, like ink on paper. Screen lightens,",
       "like two slides projected on one wall. Try a few; the picture is never",
       "harmed, because the pixels of every layer stay as they were.", "",
       "Blend modes and opacity work together. A Multiply layer at low opacity",
       "is a gentle shadow. A Screen layer at low opacity is a soft glow."},
      {"Masks", "",
       "A mask hides part of a layer without erasing it. Paint black on the mask",
       "and that part of the layer disappears. Paint white and it returns. Gray",
       "hides it partly. The pixels are still there, behind the mask.", "",
       "Masks are how a sky from one photo is joined to the hills of another.",
       "Put the sky on a layer above the hills, add a mask, and paint black over",
       "everything that is not sky. A soft brush gives the join a soft edge.", "",
       "A masked layer can be changed at any time. Unmask a corner today, mask",
       "it again tomorrow. This way of working is called non-",
       "destructive editing, and it is the reason masks matter so much."},
      {"Adjustment Layers", "",
       "An adjustment layer holds no pixels of its own. It holds a change, such",
       "as more contrast or a warmer color, and applies that change to every",
       "layer below it. Delete the adjust-",
       "ment layer and the picture is exactly as it was before.", "",
       "Because an adjustment is a layer, it has opacity, a blend mode and a",
       "mask like any other layer. Paint on its mask to keep the change away",
       "from a face, or lower its opacity to make the change weaker.", "",
       "Stack several adjustment layers to build a look in steps: one for",
       "brightness, one for color, one for contrast. Each step can be switched",
       "off to see what it adds."},
      {"Groups", "",
       "A long stack is hard to read, so layers can be put in groups. A group is",
       "a folder in the Layers panel. Close the folder and its layers take one",
       "row. Open it to work on them again.", "",
       "A group can be moved, hidden, masked and faded as one thing. Give the",
       "group a mask and every layer inside is hidden by it. Lower the opacity",
       "of the group and the layers inside fade together.", "",
       "Name your groups after what they hold: Sky, Hills, Text. A stack with",
       "good names can be understood months later, by you or by someone else."},
      {"Saving Your Work", "",
       "Save the picture with its layers while you are still working on it. A",
       "layered file keeps every layer, mask, group and adjustment, so nothing",
       "has to be done twice.", "",
       "When the picture is finished, export a copy for the people who will see",
       "it. An exported JPEG or PNG is flat: one sheet, no layers. It is small",
       "and opens anywhere, but it cannot be taken apart again.", "",
       "Keep both files. The flat copy is the picture you share. The layered",
       "file is the picture you can still change, layer by layer."},
  };
  return text_pdf_bytes(pages, 612.0, 792.0, 12.0, 72.0, 84.0);
}

// Waits until the search has nothing left to do and the pages are rendered.
void settle_find(PdfViewerWindow& viewer) {
  CHECK(process_events_until(
      [&viewer] { return !viewer.text_search()->busy() && !viewer.page_view()->render_pending(); }, 15000));
}

std::vector<PdfTextMatch> matches_in(const QString& text, const QString& query, bool match_case = false,
                                     bool whole_words = false) {
  return patchy::ui::find_pdf_text_matches(text, query, {match_case, whole_words});
}

struct FindBar {
  QLineEdit* edit{nullptr};
  QLabel* status{nullptr};
  QLabel* note{nullptr};
  QCheckBox* match_case{nullptr};
  QCheckBox* whole_words{nullptr};
  QWidget* bar{nullptr};
  [[nodiscard]] bool complete() const {
    return edit != nullptr && status != nullptr && note != nullptr && match_case != nullptr &&
           whole_words != nullptr && bar != nullptr;
  }
};

FindBar find_bar_of(PdfViewerWindow& viewer) {
  FindBar bar;
  bar.edit = viewer.findChild<QLineEdit*>(QStringLiteral("pdfViewerFindEdit"));
  bar.status = viewer.findChild<QLabel*>(QStringLiteral("pdfViewerFindStatusLabel"));
  bar.note = viewer.findChild<QLabel*>(QStringLiteral("pdfViewerFindNoteLabel"));
  bar.match_case = viewer.findChild<QCheckBox*>(QStringLiteral("pdfViewerFindMatchCaseCheck"));
  bar.whole_words = viewer.findChild<QCheckBox*>(QStringLiteral("pdfViewerFindWholeWordsCheck"));
  bar.bar = viewer.findChild<QWidget*>(QStringLiteral("pdfViewerFindBar"));
  CHECK(bar.complete());
  return bar;
}

// The settings a find test may change, put back when it ends.
struct FindSettingsGuard {
  SettingsValueRestorer pages{QStringLiteral("pdfViewer/pages")};
  SettingsValueRestorer continuous{QStringLiteral("pdfViewer/continuous")};
  SettingsValueRestorer direction{QStringLiteral("pdfViewer/direction")};
  SettingsValueRestorer zoom{QStringLiteral("pdfViewer/zoom")};
  SettingsValueRestorer sidebar{QStringLiteral("pdfViewer/sidebar")};
  SettingsValueRestorer geometry{QStringLiteral("pdfViewer/geometry")};
  SettingsValueRestorer positions{QStringLiteral("pdfViewer/positions")};
  SettingsValueRestorer match_case{QStringLiteral("pdfViewer/findMatchCase")};
  SettingsValueRestorer whole_words{QStringLiteral("pdfViewer/findWholeWords")};
  FindSettingsGuard() {
    auto settings = patchy::ui::app_settings();
    for (const char* key : {"pdfViewer/pages", "pdfViewer/continuous", "pdfViewer/direction", "pdfViewer/zoom",
                            "pdfViewer/sidebar", "pdfViewer/geometry", "pdfViewer/positions",
                            "pdfViewer/findMatchCase", "pdfViewer/findWholeWords"}) {
      settings.remove(QLatin1String(key));
    }
  }
};

PdfViewerWindow* open_find_viewer(patchy::ui::MainWindow& window, const QString& path, QSize size = QSize(900, 700)) {
  auto* viewer = PdfViewerWindow::open(&window, path, {});
  CHECK(viewer != nullptr);
  if (viewer != nullptr) {
    viewer->resize(size);
    viewer->show();
    QApplication::processEvents();
  }
  return viewer;
}

void close_find_viewer(PdfViewerWindow* viewer) {
  const QPointer<PdfViewerWindow> guard(viewer);
  viewer->close();
  CHECK(process_events_until([&guard] { return guard.isNull(); }));
}

// The matcher alone: case, whole words, line breaks, hyphens at line ends, other scripts.
void ui_pdf_viewer_find_text_matching_rules() {
  using Matches = std::vector<PdfTextMatch>;
  // Case: folded unless asked, in every script.
  CHECK(matches_in(QStringLiteral("Hello World"), QStringLiteral("world")) == Matches({{6, 5}}));
  CHECK(matches_in(QStringLiteral("Hello World"), QStringLiteral("world"), true).empty());
  CHECK(matches_in(QStringLiteral("Hello World"), QStringLiteral("World"), true) == Matches({{6, 5}}));
  // Greek capitals against small letters; the accented word is another word.
  CHECK(matches_in(QStringLiteral("ΣΟΦΙΑ σοφία"),
                   QStringLiteral("σοφια")) == Matches({{0, 5}}));
  // Cyrillic.
  CHECK(matches_in(QStringLiteral("Привет ПРИВЕТ"),
                   QStringLiteral("привет"))
            .size() == 2);
  CHECK(matches_in(QStringLiteral("Привет ПРИВЕТ"),
                   QStringLiteral("привет"), true)
            .empty());
  // Spaces and line breaks: any run is one space, in the text and in the query.
  CHECK(matches_in(QStringLiteral("quick\r\nbrown   fox"), QStringLiteral("quick brown fox")) == Matches({{0, 18}}));
  CHECK(matches_in(QStringLiteral("quick brown fox"), QStringLiteral("quick   brown\tfox")) == Matches({{0, 15}}));
  CHECK(matches_in(QStringLiteral("quick brown fox"), QStringLiteral("   ")).empty());
  CHECK(matches_in(QStringLiteral("quick brown fox"), QString()).empty());
  // A word hyphenated at a line end: the typed hyphen, a soft hyphen, PDFium's two marks.
  CHECK(matches_in(QStringLiteral("an exam-\r\nple here"), QStringLiteral("example")) == Matches({{3, 10}}));
  CHECK(matches_in(QStringLiteral("an exam­\nple here"), QStringLiteral("example")) == Matches({{3, 9}}));
  CHECK(matches_in(QStringLiteral("an exam\x02\r\nple here"), QStringLiteral("example")) == Matches({{3, 10}}));
  CHECK(matches_in(QStringLiteral("an exam\x02ple here"), QStringLiteral("example")) == Matches({{3, 8}}));
  CHECK(matches_in(QStringLiteral("an exam\ufffeple here"), QStringLiteral("example")) == Matches({{3, 8}}));
  CHECK(matches_in(QStringLiteral("an exam￾\r\nple here"), QStringLiteral("example")) == Matches({{3, 10}}));
  // The halves alone still match, and a hyphen inside a line is a hyphen.
  CHECK(matches_in(QStringLiteral("an exam-\r\nple here"), QStringLiteral("exam")) == Matches({{3, 4}}));
  CHECK(matches_in(QStringLiteral("an exam-\r\nple here"), QStringLiteral("ple")) == Matches({{10, 3}}));
  CHECK(matches_in(QStringLiteral("a well-known fact"), QStringLiteral("wellknown")).empty());
  // A compound broken at its own hyphen is found with the hyphen typed, and without.
  CHECK(matches_in(QStringLiteral("a well-\r\nknown fact"), QStringLiteral("well-known")) == Matches({{2, 12}}));
  CHECK(matches_in(QStringLiteral("a well-\r\nknown fact"), QStringLiteral("wellknown")) == Matches({{2, 12}}));
  CHECK(matches_in(QStringLiteral("pages 3-\n5 and on"), QStringLiteral("3-5")) == Matches({{6, 4}}));
  // A dash that ends a line after a space is punctuation, not a broken word.
  CHECK(matches_in(QStringLiteral("stop -\ngo"), QStringLiteral("stopgo")).empty());
  // Ligatures, full-width letters, curly quotes, dashes, soft hyphens inside a line.
  CHECK(matches_in(QStringLiteral("ﬁnd the ofﬁce"), QStringLiteral("office")) == Matches({{8, 5}}));
  CHECK(matches_in(QStringLiteral("ﬁnd the ofﬁce"), QStringLiteral("find")) == Matches({{0, 3}}));
  CHECK(matches_in(QStringLiteral("ＡＢＣ"), QStringLiteral("abc")) == Matches({{0, 3}}));
  CHECK(matches_in(QStringLiteral("it’s “quoted”"), QStringLiteral("it's \"quoted\"")) ==
        Matches({{0, 13}}));
  CHECK(matches_in(QStringLiteral("2019–2020"), QStringLiteral("2019-2020")) == Matches({{0, 9}}));
  CHECK(matches_in(QStringLiteral("co­operate"), QStringLiteral("cooperate")) == Matches({{0, 10}}));
  // Accents count, composed or not, and a match never stops before a letter's accent.
  CHECK(matches_in(QStringLiteral("café café cafe"), QStringLiteral("café")) ==
        Matches({{0, 4}, {5, 5}}));
  CHECK(matches_in(QStringLiteral("café café cafe"), QStringLiteral("cafe")) == Matches({{11, 4}}));
  // Whole words.
  CHECK(matches_in(QStringLiteral("cat concatenate cat."), QStringLiteral("cat")).size() == 3);
  CHECK(matches_in(QStringLiteral("cat concatenate cat."), QStringLiteral("cat"), false, true) ==
        Matches({{0, 3}, {16, 3}}));
  CHECK(matches_in(QStringLiteral("cafés café"), QStringLiteral("café"), false, true) ==
        Matches({{6, 4}}));
  CHECK(matches_in(QStringLiteral("a_cat cat_b (cat)"), QStringLiteral("cat"), false, true) == Matches({{13, 3}}));
  // An end of the query that is not a letter needs no word edge there.
  CHECK(matches_in(QStringLiteral("end. Then"), QStringLiteral("end."), false, true) == Matches({{0, 4}}));
  // Matches never overlap.
  CHECK(matches_in(QStringLiteral("aaaa"), QStringLiteral("aa")) == Matches({{0, 2}, {2, 2}}));
}

// The counts a document gives: every page searched, the options, phrases across lines, and
// the options remembered.
void ui_pdf_viewer_find_text_counts_and_options() {
  FindSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_find.pdf"), find_fixture_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_find_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  auto* search = viewer->text_search();
  const FindBar bar = find_bar_of(*viewer);
  if (!bar.complete()) {
    return;
  }
  CHECK(!viewer->find_bar_open() && !bar.bar->isVisible());
  // The toolbar button opens the bar.
  auto* find_action = viewer->findChild<QAction*>(QStringLiteral("pdfViewerFindAction"));
  CHECK(find_action != nullptr);
  if (find_action != nullptr) {
    find_action->trigger();
  }
  CHECK(viewer->find_bar_open() && bar.bar->isVisible());
  CHECK(bar.status->text().isEmpty());
  const auto count_of = [&](const QString& query) {
    bar.edit->setText(query);
    settle_find(*viewer);
    return search->match_count();
  };
  CHECK(!bar.match_case->isChecked() && !bar.whole_words->isChecked());
  CHECK(count_of(QStringLiteral("fox")) == 7);
  CHECK(bar.status->text() == QStringLiteral("1 of 7"));
  CHECK(count_of(QStringLiteral("FOX")) == 7);
  bar.match_case->setChecked(true);
  settle_find(*viewer);
  CHECK(search->match_count() == 1);
  CHECK(count_of(QStringLiteral("fox")) == 5);
  bar.whole_words->setChecked(true);
  settle_find(*viewer);
  CHECK(search->match_count() == 4);
  bar.match_case->setChecked(false);
  settle_find(*viewer);
  CHECK(search->match_count() == 6);
  bar.whole_words->setChecked(false);
  settle_find(*viewer);
  CHECK(search->match_count() == 7);
  // Letters past ASCII fold too.
  CHECK(count_of(QStringLiteral("ärger")) == 2);
  bar.match_case->setChecked(true);
  settle_find(*viewer);
  CHECK(search->match_count() == 1);
  bar.match_case->setChecked(false);
  // A phrase across a line break, and across two.
  CHECK(count_of(QStringLiteral("jumps over the lazy")) == 1);
  CHECK(count_of(QStringLiteral("Patchy opens PDF")) == 1);
  CHECK(count_of(QStringLiteral("phrase split across lines")) == 1);
  // Words broken at a line end, whatever PDFium made of the hyphen and the break (Qt 6.8's
  // hands "exam", U+FFFE, "ple" back, with no line break).
  CHECK(search->page_text_for_testing(2).contains(QStringLiteral("exam")));
  CHECK(count_of(QStringLiteral("example")) == 1);
  CHECK(search->current_match().page == 2);
  CHECK(count_of(QStringLiteral("well-known")) == 1);
  CHECK(count_of(QStringLiteral("hyphenated word and a well")) == 1);
  // Nothing found says so, and the step buttons go quiet.
  CHECK(count_of(QStringLiteral("zebra")) == 0);
  CHECK(bar.status->text() == QStringLiteral("No matches"));
  auto* next_button = viewer->findChild<QWidget*>(QStringLiteral("pdfViewerFindNextButton"));
  auto* previous_button = viewer->findChild<QWidget*>(QStringLiteral("pdfViewerFindPreviousButton"));
  CHECK(next_button != nullptr && !next_button->isEnabled());
  CHECK(previous_button != nullptr && !previous_button->isEnabled());
  CHECK(count_of(QStringLiteral("fox")) == 7);
  CHECK(next_button != nullptr && next_button->isEnabled());
  // Clearing the field clears the search.
  CHECK(count_of(QString()) == 0);
  CHECK(!search->active() && bar.status->text().isEmpty());
  Q_UNUSED(view);

  // The options are remembered.
  bar.whole_words->setChecked(true);
  close_find_viewer(viewer);
  CHECK(patchy::ui::app_settings().value(QStringLiteral("pdfViewer/findWholeWords")).toBool());
  CHECK(!patchy::ui::app_settings().value(QStringLiteral("pdfViewer/findMatchCase")).toBool());
  viewer = open_find_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  const FindBar again = find_bar_of(*viewer);
  if (again.complete()) {
    CHECK(again.whole_words->isChecked() && !again.match_case->isChecked());
    viewer->open_find_bar();
    again.edit->setText(QStringLiteral("fox"));
    settle_find(*viewer);
    CHECK(viewer->text_search()->match_count() == 6);
  }
  close_find_viewer(viewer);
}

// Next and Previous: the order across pages, around both ends, every key, and the page each
// match turns to in the spread layouts.
void ui_pdf_viewer_find_text_steps_through_matches() {
  FindSettingsGuard settings_guard;
  const QString path =
      write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_find_steps.pdf"), find_fixture_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_find_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  auto* search = viewer->text_search();
  const FindBar bar = find_bar_of(*viewer);
  if (!bar.complete()) {
    return;
  }
  view->set_continuous(false);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->first_page();
  view->setFocus();
  // Ctrl+F opens the bar with the keyboard in the field.
  QTest::keyClick(view, Qt::Key_F, Qt::ControlModifier);
  CHECK(viewer->find_bar_open() && bar.bar->isVisible());
  if (viewer->isActiveWindow()) {
    CHECK(QApplication::focusWidget() == bar.edit);
  }
  QTest::keyClicks(bar.edit, QStringLiteral("fox"));
  settle_find(*viewer);
  const auto expect = [&](const char* what, int number, int page) {
    settle_find(*viewer);
    const QString wanted = QStringLiteral("%1 of 7").arg(number);
    if (bar.status->text() != wanted || search->current_match().page != page || view->current_page() != page) {
      std::cout << "  " << what << ": status \"" << bar.status->text().toStdString() << "\" (wanted \""
                << wanted.toStdString() << "\"), match on page " << search->current_match().page + 1
                << ", page shown " << view->current_page() + 1 << " (wanted " << page + 1 << ")\n";
      CHECK(false);
    }
  };
  expect("typed", 1, 0);
  CHECK(bar.note->text().isEmpty());
  // Enter walks forward through page 2's five matches, then to page 5.
  for (int number = 2; number <= 6; ++number) {
    QTest::keyClick(bar.edit, Qt::Key_Return);
    expect("Enter", number, 1);
  }
  QTest::keyClick(bar.edit, Qt::Key_Return);
  expect("Enter to the last", 7, 4);
  CHECK(bar.note->text().isEmpty());
  // Past the last match: around to the first, with a note.
  QTest::keyClick(bar.edit, Qt::Key_Return);
  expect("Enter around the end", 1, 0);
  CHECK(bar.note->text() == QStringLiteral("Reached the end, continued from the start"));
  // Shift+Enter goes back, around the start.
  QTest::keyClick(bar.edit, Qt::Key_Return, Qt::ShiftModifier);
  expect("Shift+Enter around the start", 7, 4);
  CHECK(bar.note->text() == QStringLiteral("Reached the start, continued from the end"));
  QTest::keyClick(bar.edit, Qt::Key_Return, Qt::ShiftModifier);
  expect("Shift+Enter", 6, 1);
  // The keypad's Enter is Enter.
  QTest::keyClick(bar.edit, Qt::Key_Enter, Qt::KeypadModifier);
  expect("keypad Enter", 7, 4);
  // F3 and Ctrl+G, with Shift for back, from the field and from the pages.
  QTest::keyClick(bar.edit, Qt::Key_F3);
  expect("F3 in the field", 1, 0);
  view->setFocus();
  QTest::keyClick(view, Qt::Key_F3);
  expect("F3", 2, 1);
  QTest::keyClick(view, Qt::Key_F3, Qt::ShiftModifier);
  expect("Shift+F3", 1, 0);
  QTest::keyClick(view, Qt::Key_G, Qt::ControlModifier);
  expect("Ctrl+G", 2, 1);
  QTest::keyClick(view, Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier);
  expect("Ctrl+Shift+G", 1, 0);
  // The buttons.
  auto* next_button = viewer->findChild<QAbstractButton*>(QStringLiteral("pdfViewerFindNextButton"));
  auto* previous_button = viewer->findChild<QAbstractButton*>(QStringLiteral("pdfViewerFindPreviousButton"));
  CHECK(next_button != nullptr && previous_button != nullptr);
  if (next_button != nullptr && previous_button != nullptr) {
    next_button->click();
    expect("Next button", 2, 1);
    previous_button->click();
    expect("Previous button", 1, 0);
  }

  // Esc closes the bar, clears the search and hands the keyboard back to the pages.
  QTest::keyClick(bar.edit, Qt::Key_Escape);
  CHECK(!viewer->find_bar_open() && !bar.bar->isVisible() && !search->active());
  if (viewer->isActiveWindow()) {
    CHECK(QApplication::focusWidget() == view);
  }
  // Opened again on another page, the same text is searched from that page on.
  view->go_to_page(4);
  QTest::keyClick(view, Qt::Key_F, Qt::ControlModifier);
  CHECK(viewer->find_bar_open() && bar.edit->text() == QStringLiteral("fox"));
  CHECK(bar.edit->selectedText() == QStringLiteral("fox"));
  expect("reopened on page 5", 7, 4);
  // Esc on the pages closes it too; F3 with the bar closed opens it and finds from here.
  view->setFocus();
  QTest::keyClick(view, Qt::Key_Escape);
  CHECK(!viewer->find_bar_open());
  view->go_to_page(1);
  QTest::keyClick(view, Qt::Key_F3);
  CHECK(viewer->find_bar_open());
  expect("F3 with the bar closed", 2, 1);
  // Typing on keeps the place while the match still fits.
  QTest::keyClick(bar.edit, Qt::Key_Return);
  expect("Enter", 3, 1);
  bar.edit->setText(QStringLiteral("fo"));
  settle_find(*viewer);
  CHECK(search->match_count() == 7 && search->current_index() == 2);
  bar.edit->setText(QStringLiteral("fox"));
  expect("typed on", 3, 1);

  // Two pages side by side: the row that holds the match is the one shown.
  const auto step_to_page = [&](int page, bool forward) {
    for (int tries = 0; tries < 8 && search->current_match().page != page; ++tries) {
      forward ? viewer->find_next() : viewer->find_previous();
    }
    settle_find(*viewer);
    CHECK(search->current_match().page == page);
  };
  const auto on_the_right = [&] {
    const QRectF rect = view->current_text_match_rect_for_testing();
    CHECK(!rect.isEmpty());
    return rect.center().x() > view->viewport()->width() / 2.0;
  };
  view->set_spread_mode(patchy::PageSpreadMode::TwoPages);
  step_to_page(4, true);
  CHECK(view->current_pages() == std::vector<int>({4}));
  step_to_page(1, false);
  CHECK(view->current_pages() == std::vector<int>({0, 1}));
  CHECK(on_the_right());
  step_to_page(0, false);
  CHECK(view->current_pages() == std::vector<int>({0, 1}));
  CHECK(!on_the_right());
  // The cover alone: page 2 is the left page of the second row.
  view->set_spread_mode(patchy::PageSpreadMode::TwoPagesCoverAlone);
  step_to_page(1, true);
  CHECK(view->current_pages() == std::vector<int>({1, 2}));
  CHECK(!on_the_right());
  step_to_page(4, true);
  CHECK(view->current_pages() == std::vector<int>({3, 4}));
  CHECK(on_the_right());
  // Right to left puts the earlier page of each row on the right.
  view->set_reading_direction(patchy::PageReadingDirection::RightToLeft);
  step_to_page(1, true);
  CHECK(view->current_pages() == std::vector<int>({1, 2}));
  CHECK(on_the_right());
  step_to_page(4, true);
  CHECK(view->current_pages() == std::vector<int>({3, 4}));
  CHECK(!on_the_right());
  step_to_page(0, true);
  CHECK(view->current_pages() == std::vector<int>({0}));
  close_find_viewer(viewer);
}

// Highlights sit on the text they mark through scrolling, zoom, rotation and detail renders,
// the current one in its own color and in view.
void ui_pdf_viewer_find_text_highlights_follow_the_page() {
  FindSettingsGuard settings_guard;
  const QString path =
      write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_find_marks.pdf"), find_fixture_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_find_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  auto* search = viewer->text_search();
  const FindBar bar = find_bar_of(*viewer);
  if (!bar.complete()) {
    return;
  }
  view->set_continuous(true);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_zoom_percent(150.0);
  view->first_page();
  viewer->open_find_bar();

  // What the window shows inside the current match: paper tinted with the current-match
  // color, and the letters under it.
  struct Marked {
    QRectF rect;
    int tinted{0};
    int ink{0};
    bool in_view{false};
  };
  const auto current_mark = [&] {
    settle_find(*viewer);
    Marked marked;
    marked.rect = view->current_text_match_rect_for_testing();
    const QRect inside = marked.rect.toAlignedRect();
    marked.in_view = !marked.rect.isEmpty() && view->viewport()->rect().contains(inside);
    if (!marked.in_view) {
      return marked;
    }
    const QImage image = view->viewport()->grab().toImage();
    const double ratio = image.devicePixelRatio();
    for (int y = inside.top(); y <= inside.bottom(); ++y) {
      for (int x = inside.left(); x <= inside.right(); ++x) {
        const QColor color = image.pixelColor(static_cast<int>(x * ratio), static_cast<int>(y * ratio));
        // #ff7a00 at 55% over white paper is about (255, 182, 115).
        if (color.red() > 240 && std::abs(color.green() - 182) < 18 && std::abs(color.blue() - 115) < 18) {
          ++marked.tinted;
        } else if (color.red() < 190 && color.blue() < 90) {
          ++marked.ink;
        }
      }
    }
    return marked;
  };
  const auto report = [](const char* what, const Marked& marked) {
    std::cout << "  " << what << ": match at " << marked.rect.x() << "," << marked.rect.y() << " "
              << marked.rect.width() << "x" << marked.rect.height() << ", in view " << marked.in_view
              << ", tinted " << marked.tinted << ", ink " << marked.ink << "\n";
  };
  const auto good = [](const Marked& marked) { return marked.in_view && marked.tinted > 40 && marked.ink > 20; };

  // A match four pages down scrolls into view.
  bar.edit->setText(QStringLiteral("last fox"));
  const Marked far = current_mark();
  CHECK(search->match_count() == 1 && search->current_match().page == 4);
  if (!good(far)) {
    report("scrolled to", far);
    CHECK(false);
  }
  CHECK(view->current_page() == 4);
  // "last fox" is the second line of its page, 30 pt from the left: the mark starts there
  // (The and a space come first) and is one line high.
  CHECK(far.rect.height() > 14.0 && far.rect.height() < 60.0);
  CHECK(far.rect.width() > 60.0 && far.rect.width() < 200.0);

  // Zoomed, the mark grows with the page and stays on its letters.
  view->set_zoom_percent(300.0);
  view->show_current_text_match();
  const Marked zoomed = current_mark();
  if (!good(zoomed) || std::abs(zoomed.rect.width() / far.rect.width() - 2.0) > 0.2) {
    report("at 150%", far);
    report("at 300%", zoomed);
    CHECK(false);
  }
  // Past a whole-page image the page is drawn from a detail render; the mark still fits.
  view->set_zoom_percent(800.0);
  view->show_current_text_match();
  const Marked detailed = current_mark();
  CHECK(view->cached_width_for_testing(4) >= 3000);
  if (!good(detailed)) {
    report("at 800%", detailed);
    CHECK(false);
  }
  // Every quarter turn: the mark turns with the page.
  view->set_zoom_percent(150.0);
  for (int turn = 1; turn <= 4; ++turn) {
    view->rotate_clockwise();
    view->show_current_text_match();
    const Marked turned = current_mark();
    const bool upright = turn % 2 == 0;
    if (!good(turned) || (turned.rect.width() > turned.rect.height()) != upright) {
      report("turned", turned);
      CHECK(false);
    }
  }

  // The other matches of a page are marked in the plain color: page 2 has five "fox", one
  // of them current.
  bar.edit->clear();
  view->go_to_page(1);
  bar.edit->setText(QStringLiteral("fox"));
  settle_find(*viewer);
  CHECK(search->match_count() == 7 && search->current_match().page == 1);
  const auto* found = search->page_matches(1);
  CHECK(found != nullptr && found->matches.size() == 5);
  if (found != nullptr) {
    for (const auto& rects : found->rects) {
      CHECK(!rects.isEmpty());
    }
  }
  {
    const QImage image = view->viewport()->grab().toImage();
    int plain = 0;
    int strong = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const QColor color = image.pixelColor(x, y);
        // #ffd400 at 42% over white paper is about (255, 237, 148).
        if (color.red() > 240 && std::abs(color.green() - 237) < 12 && std::abs(color.blue() - 148) < 18) {
          ++plain;
        } else if (color.red() > 240 && std::abs(color.green() - 182) < 18 && std::abs(color.blue() - 115) < 18) {
          ++strong;
        }
      }
    }
    if (plain < 4 * 40 || strong < 40 || plain < 2 * strong) {
      std::cout << "  page 2: plain " << plain << ", current " << strong << "\n";
      CHECK(false);
    }
  }
  // Closing the bar takes the marks away.
  viewer->close_find_bar();
  settle_find(*viewer);
  {
    const QImage image = view->viewport()->grab().toImage();
    int marked = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const QColor color = image.pixelColor(x, y);
        if (color.red() > 240 && color.blue() < 170 && color.green() > 150) {
          ++marked;
        }
      }
    }
    CHECK(marked == 0);
  }
  close_find_viewer(viewer);
}

// A scan has pages and no text: the bar says so instead of "No matches".
void ui_pdf_viewer_find_text_reports_no_text_layer() {
  FindSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_find_scan.pdf"), numbered_pdf_bytes(3));
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_find_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  const FindBar bar = find_bar_of(*viewer);
  if (!bar.complete()) {
    return;
  }
  viewer->open_find_bar();
  bar.edit->setText(QStringLiteral("fox"));
  settle_find(*viewer);
  CHECK(viewer->text_search()->match_count() == 0);
  CHECK(viewer->text_search()->text_layer() == patchy::ui::PdfTextSearch::TextLayer::Absent);
  CHECK(bar.status->text() == QStringLiteral("This PDF has no searchable text"));
  // Steps do nothing and say nothing.
  QTest::keyClick(bar.edit, Qt::Key_Return);
  QTest::keyClick(bar.edit, Qt::Key_Return, Qt::ShiftModifier);
  CHECK(bar.note->text().isEmpty());
  close_find_viewer(viewer);

  // A document with text on some pages only is searched like any other.
  const QString mixed = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_find_mixed.pdf"), find_fixture_pdf_bytes());
  viewer = open_find_viewer(window, mixed);
  if (viewer == nullptr) {
    return;
  }
  const FindBar mixed_bar = find_bar_of(*viewer);
  if (mixed_bar.complete()) {
    viewer->open_find_bar();
    mixed_bar.edit->setText(QStringLiteral("zebra"));
    settle_find(*viewer);
    CHECK(viewer->text_search()->text_layer() == patchy::ui::PdfTextSearch::TextLayer::Present);
    CHECK(mixed_bar.status->text() == QStringLiteral("No matches"));
  }
  close_find_viewer(viewer);
}

// The viewer with a search under way on a document of ordinary prose, in both schemes'
// guard: the picture for the feature's card.
void ui_pdf_viewer_find_text_sample_document() {
  FindSettingsGuard settings_guard;
  const QString path =
      write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_find_sample.pdf"), find_sample_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_find_viewer(window, path, QSize(1240, 820));
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  auto* search = viewer->text_search();
  const FindBar bar = find_bar_of(*viewer);
  if (!bar.complete()) {
    return;
  }
  view->set_continuous(false);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPages);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  viewer->open_find_bar();
  bar.edit->setText(QStringLiteral("layer"));
  settle_find(*viewer);
  // Counted by hand in find_sample_pdf_bytes: 42 in all; Whole words leaves "layers" and
  // "layered" out (27); Match case leaves the five "Layers" out (37).
  const int all = search->match_count();
  bar.whole_words->setChecked(true);
  settle_find(*viewer);
  const int whole = search->match_count();
  bar.whole_words->setChecked(false);
  bar.match_case->setChecked(true);
  settle_find(*viewer);
  const int cased = search->match_count();
  bar.match_case->setChecked(false);
  if (all != 42 || whole != 27 || cased != 37) {
    std::cout << "  \"layer\": " << all << " in all, " << whole << " as a whole word, " << cased
              << " with its case\n";
    CHECK(false);
  }
  // The two words broken at line ends.
  bar.edit->setText(QStringLiteral("non-destructive editing"));
  settle_find(*viewer);
  CHECK(search->match_count() == 1 && search->current_match().page == 2);
  bar.edit->setText(QStringLiteral("adjustment layer"));
  settle_find(*viewer);
  CHECK(search->match_count() == 4);
  // From the first page again: a cleared field forgets the place.
  bar.edit->clear();
  view->first_page();
  bar.edit->setText(QStringLiteral("layer"));
  settle_find(*viewer);
  CHECK(bar.status->text() == QStringLiteral("1 of %1").arg(all));
  viewer->find_next();
  viewer->find_next();
  settle_find(*viewer);
  CHECK(bar.status->text() == QStringLiteral("3 of %1").arg(all));
  // The thumbnails too, so the picture is whole.
  (void)process_events_until([] { return false; }, 300);
  settle_find(*viewer);
  patchy::test::ui::save_widget_artifact("ui_pdf_viewer_find_text", *viewer);
  close_find_viewer(viewer);
}

}  // namespace

std::vector<patchy::test::TestCase> pdf_viewer_ui_tests() {
  return {
      {"ui_pdf_viewer_opens_pdf_files", ui_pdf_viewer_opens_pdf_files},
      {"ui_pdf_viewer_opens_dropped_files", ui_pdf_viewer_opens_dropped_files},
      {"ui_pdf_viewer_open_updates_home_recent_list", ui_pdf_viewer_open_updates_home_recent_list},
      {"ui_pdf_viewer_spreads_and_page_turns", ui_pdf_viewer_spreads_and_page_turns},
      {"ui_pdf_viewer_close_keys_close_only_the_viewer", ui_pdf_viewer_close_keys_close_only_the_viewer},
      {"ui_pdf_viewer_detail_renders_match_the_page", ui_pdf_viewer_detail_renders_match_the_page},
      {"ui_pdf_viewer_find_text_matching_rules", ui_pdf_viewer_find_text_matching_rules},
      {"ui_pdf_viewer_find_text_counts_and_options", ui_pdf_viewer_find_text_counts_and_options},
      {"ui_pdf_viewer_find_text_steps_through_matches", ui_pdf_viewer_find_text_steps_through_matches},
      {"ui_pdf_viewer_find_text_highlights_follow_the_page", ui_pdf_viewer_find_text_highlights_follow_the_page},
      {"ui_pdf_viewer_find_text_reports_no_text_layer", ui_pdf_viewer_find_text_reports_no_text_layer},
      {"ui_pdf_viewer_find_text_sample_document", ui_pdf_viewer_find_text_sample_document},
      {"ui_pdf_book_print_imposes_sheets", ui_pdf_book_print_imposes_sheets},
      {"ui_pdf_book_print_splits_spreads", ui_pdf_book_print_splits_spreads},
      {"ui_pdf_book_print_writes_sheets_pdf", ui_pdf_book_print_writes_sheets_pdf},
      {"ui_pdf_book_print_dialog_follows_choices", ui_pdf_book_print_dialog_follows_choices},
  };
}

#else

std::vector<patchy::test::TestCase> pdf_viewer_ui_tests() {
  return {};
}

#endif
