// Selecting and copying text in the PDF viewer (docs/pdf-viewer-selection.md).

#include "test_harness.hpp"
#include "ui_test_groups.hpp"

#if defined(PATCHY_HAVE_QT_PDF)

#include "import_print_resolution_test_support.hpp"
#include "pdf_viewer_test_support.hpp"
#include "ui/app_settings.hpp"
#include "ui/main_window.hpp"
#include "ui/pdf_links.hpp"
#include "ui/pdf_page_view.hpp"
#include "ui/pdf_text_selection.hpp"
#include "ui/pdf_viewer_window.hpp"
#include "ui_test_access.hpp"
#include "ui_test_support.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFile>
#include <QImage>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPointer>
#include <QScrollBar>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using patchy::test::ui::process_events_until;
using patchy::test::ui::send_mouse;
using patchy::test::ui::SettingsValueRestorer;
using patchy::test::ui::show_window;
using patchy::test::ui::import_print_resolution_support::pdf_from_objects;
using patchy::test::ui::pdf_viewer_support::find_fixture_pdf_bytes;
using patchy::test::ui::pdf_viewer_support::find_sample_pdf_bytes;
using patchy::ui::MainWindowTestAccess;
using patchy::ui::PdfPageView;
using patchy::ui::PdfViewerWindow;

namespace {

// The text of the fixture's pages (find_fixture_pdf_bytes) as Copy hands it out.
const QString kPageOneText =
    QStringLiteral("The quick brown fox jumps\nover the lazy dog. Patchy\nopens PDF files.");
const QString kPageTwoText = QStringLiteral("A fox is quick. FOX and Fox\nfoxes are not one fox.");
const QString kPageThreeText = QStringLiteral(
    "This line ends with an exam-\nple of a hyphenated word and\na well-\nknown phrase split\nacross lines.");

QString write_file(const QString& path, const QByteArray& bytes) {
  QFile::remove(path);
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  file.write(bytes);
  return path;
}

// Two pages with a gray square each and no text: a scan.
QByteArray scan_pdf_bytes() {
  const std::string content = "0.6 0.6 0.6 rg 40 40 120 200 re f";
  const std::string stream = "<</Length " + std::to_string(content.size()) + ">>\nstream\n" + content + "\nendstream";
  return pdf_from_objects({"<</Type/Catalog/Pages 2 0 R>>", "<</Type/Pages/Kids[3 0 R 5 0 R]/Count 2>>",
                           "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 300]/Contents 4 0 R>>", stream,
                           "<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 300]/Contents 6 0 R>>", stream});
}

// The settings a test here may change, put back when it ends.
struct SelectSettingsGuard {
  SettingsValueRestorer pages{QStringLiteral("pdfViewer/pages")};
  SettingsValueRestorer continuous{QStringLiteral("pdfViewer/continuous")};
  SettingsValueRestorer direction{QStringLiteral("pdfViewer/direction")};
  SettingsValueRestorer zoom{QStringLiteral("pdfViewer/zoom")};
  SettingsValueRestorer sidebar{QStringLiteral("pdfViewer/sidebar")};
  SettingsValueRestorer sidebar_view{QStringLiteral("pdfViewer/sidebarView")};
  SettingsValueRestorer geometry{QStringLiteral("pdfViewer/geometry")};
  SettingsValueRestorer positions{QStringLiteral("pdfViewer/positions")};
  SelectSettingsGuard() {
    auto settings = patchy::ui::app_settings();
    for (const char* key : {"pdfViewer/pages", "pdfViewer/continuous", "pdfViewer/direction", "pdfViewer/zoom",
                            "pdfViewer/sidebar", "pdfViewer/sidebarView", "pdfViewer/geometry",
                            "pdfViewer/positions"}) {
      settings.remove(QLatin1String(key));
    }
  }
};

// What the viewer copied, instead of the clipboard.
struct CopyLog {
  QStringList copied;
};

PdfViewerWindow* open_viewer(patchy::ui::MainWindow& window, const QString& path, CopyLog* log = nullptr,
                             QSize size = QSize(900, 700)) {
  auto* viewer = PdfViewerWindow::open(&window, path, {});
  CHECK(viewer != nullptr);
  if (viewer != nullptr) {
    viewer->set_link_handlers([](const QUrl&) { return true; }, [log](const QString& text) {
      if (log != nullptr) {
        log->copied.append(text);
      }
    });
    viewer->resize(size);
    viewer->show();
    QApplication::processEvents();
  }
  return viewer;
}

void close_viewer(PdfViewerWindow* viewer) {
  const QPointer<PdfViewerWindow> guard(viewer);
  viewer->close();
  CHECK(process_events_until([&guard] { return guard.isNull(); }));
}

// Waits until the pages are rendered, their links and text areas are read and the selection
// has caught up with the pointer.
void settle(PdfViewerWindow& viewer) {
  CHECK(process_events_until(
      [&viewer] {
        auto* view = viewer.page_view();
        return !view->render_pending() && !view->links()->busy() && !view->text_selection()->busy();
      },
      15000));
}

QString selected(PdfViewerWindow& viewer) {
  settle(viewer);
  return viewer.page_view()->selected_text();
}

// Where `word` (its nth occurrence) is on `page`, in points from the page's top left. The
// page text's offsets are PDFium's character indices, give or take a few characters where
// PDFium counts one its text leaves out: the selection's own text says which index is right.
QRectF word_area(PdfViewerWindow& viewer, int page, const QString& word, int occurrence = 0) {
  const QString text = viewer.document()->getAllText(page).text();
  qsizetype at = -1;
  for (int n = 0; n <= occurrence; ++n) {
    at = text.indexOf(word, at + 1);
    if (at < 0) {
      break;
    }
  }
  if (at >= 0) {
    for (int probe = 0; probe <= 12; ++probe) {
      const QPdfSelection found =
          viewer.document()->getSelectionAtIndex(page, static_cast<int>(at) + probe, static_cast<int>(word.size()));
      if (found.text() == word) {
        return found.boundingRectangle();
      }
    }
  }
  std::cout << "  \"" << word.toStdString() << "\" is not on page " << page + 1 << '\n';
  CHECK(false);
  return {};
}

// A point of a page (points from its top left) as a point of the page area.
QPoint at(PdfViewerWindow& viewer, int page, QPointF page_point) {
  return viewer.page_view()->page_area_rect_for_testing(page, QRectF(page_point, QSizeF(0.0, 0.0))).topLeft().toPoint();
}

// Just before the first letter of a word and just behind its last: the places a drag over
// the word starts and ends at. (Not a point inside the letters: Qt PDF's rectangles are
// whole points, so a point "one point in" could be in either half of a narrow letter such as
// an l or a full stop.) Then the middle of the word.
QPoint word_start(PdfViewerWindow& viewer, int page, const QString& word, int occurrence = 0) {
  const QRectF area = word_area(viewer, page, word, occurrence);
  return at(viewer, page, QPointF(area.left() - 0.3, area.center().y()));
}

QPoint word_end(PdfViewerWindow& viewer, int page, const QString& word, int occurrence = 0) {
  const QRectF area = word_area(viewer, page, word, occurrence);
  return at(viewer, page, QPointF(area.right() + 0.3, area.center().y()));
}

QPoint word_middle(PdfViewerWindow& viewer, int page, const QString& word, int occurrence = 0) {
  const QRectF area = word_area(viewer, page, word, occurrence);
  return at(viewer, page, area.center());
}

// The word as the page area shows it.
QRectF word_rect(PdfViewerWindow& viewer, int page, const QString& word, int occurrence = 0) {
  return viewer.page_view()->page_area_rect_for_testing(page, word_area(viewer, page, word, occurrence));
}

// Scrolls so `rect` (viewport coordinates) is inside the page area, a little in from its
// top left corner, when it is not on screen whole.
void bring_into_view(PdfPageView& view, const QRectF& rect) {
  if (QRectF(view.viewport()->rect()).adjusted(4.0, 4.0, -4.0, -4.0).contains(rect)) {
    return;
  }
  auto* horizontal = view.horizontalScrollBar();
  auto* vertical = view.verticalScrollBar();
  horizontal->setValue(horizontal->value() + static_cast<int>(std::lround(rect.left())) - 60);
  vertical->setValue(vertical->value() + static_cast<int>(std::lround(rect.top())) - 60);
  QApplication::processEvents();
}

void hover(PdfPageView& view, QPoint position) {
  send_mouse(*view.viewport(), QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton);
}

void click(PdfPageView& view, QPoint position, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  send_mouse(*view.viewport(), QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton, modifiers);
  send_mouse(*view.viewport(), QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton, modifiers);
}

// The second click of a double-click, as Qt delivers it after a first click.
void double_click(PdfPageView& view, QPoint position) {
  click(view, position);
  send_mouse(*view.viewport(), QEvent::MouseButtonDblClick, position, Qt::LeftButton, Qt::LeftButton);
  send_mouse(*view.viewport(), QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
}

void drag(PdfPageView& view, QPoint from, QPoint to, Qt::MouseButton button = Qt::LeftButton, int steps = 5) {
  send_mouse(*view.viewport(), QEvent::MouseButtonPress, from, button, button);
  for (int step = 1; step <= steps; ++step) {
    const QPoint position = from + (to - from) * step / steps;
    send_mouse(*view.viewport(), QEvent::MouseMove, position, Qt::NoButton, button);
  }
  send_mouse(*view.viewport(), QEvent::MouseButtonRelease, to, button, Qt::NoButton);
}

Qt::CursorShape pointer_shape(const PdfPageView& view) {
  return view.viewport()->testAttribute(Qt::WA_SetCursor) ? view.viewport()->cursor().shape() : Qt::ArrowCursor;
}

bool expect_text(const char* what, PdfViewerWindow& viewer, const QString& wanted) {
  const QString text = selected(viewer);
  if (text == wanted) {
    return true;
  }
  std::cout << "  " << what << ": selected \"" << text.toStdString() << "\", wanted \"" << wanted.toStdString() << "\"\n";
  CHECK(false);
  return false;
}

struct PageMenu {
  bool shown{false};
  QStringList actions;
  QList<bool> enabled;
  [[nodiscard]] bool action_enabled(const QString& name) const {
    const auto index = actions.indexOf(name);
    return index >= 0 && enabled[index];
  }
};

// Right-clicks the page area at `position` and picks `pick` (an action objectName; empty
// closes the menu) from the page menu, if one opens.
PageMenu right_click(PdfPageView& view, QPoint position, const QString& pick = {}) {
  PageMenu seen;
  QTimer poll;
  int polls_left = 300;
  poll.setInterval(10);
  QObject::connect(&poll, &QTimer::timeout, [&seen, &poll, &polls_left, pick] {
    if (--polls_left <= 0) {
      poll.stop();
      return;
    }
    for (auto* widget : QApplication::topLevelWidgets()) {
      auto* menu = qobject_cast<QMenu*>(widget);
      if (menu == nullptr || !menu->isVisible() || menu->objectName() != QStringLiteral("pdfViewerPageMenu")) {
        continue;
      }
      seen.shown = true;
      QAction* chosen = nullptr;
      for (auto* action : menu->actions()) {
        seen.actions.push_back(action->objectName());
        seen.enabled.push_back(action->isEnabled());
        if (!pick.isEmpty() && action->objectName() == pick && action->isEnabled()) {
          chosen = action;
        }
      }
      poll.stop();
      // QMenu closes before it triggers a picked entry.
      menu->close();
      if (chosen != nullptr) {
        chosen->trigger();
      }
      return;
    }
  });
  poll.start();
  QContextMenuEvent event(QContextMenuEvent::Mouse, position, view.viewport()->mapToGlobal(position));
  QApplication::sendEvent(view.viewport(), &event);
  poll.stop();
  QApplication::processEvents();
  return seen;
}

const QString kCopyAction = QStringLiteral("pdfViewerCopyAction");
const QString kSelectPageAction = QStringLiteral("pdfViewerSelectPageAction");

// The page area's pixels inside `area`: how many are paper under the selection tint, and how
// many are letters. #1f6fe5 at 35% over white paper is about (177, 205, 246).
struct Tint {
  int tinted{0};
  int ink{0};
};

Tint tint_inside(PdfPageView& view, const QRectF& area) {
  Tint tint;
  const QImage image = view.viewport()->grab().toImage();
  const double ratio = image.devicePixelRatio();
  const QRect inside = area.toAlignedRect().intersected(view.viewport()->rect());
  for (int y = inside.top(); y <= inside.bottom(); ++y) {
    for (int x = inside.left(); x <= inside.right(); ++x) {
      const QColor color = image.pixelColor(static_cast<int>(x * ratio), static_cast<int>(y * ratio));
      if (std::abs(color.red() - 177) < 18 && std::abs(color.green() - 205) < 18 && color.blue() > 225) {
        ++tint.tinted;
      } else if (color.red() < 120 && color.green() < 150) {
        ++tint.ink;
      }
    }
  }
  return tint;
}

int tinted_pixels(PdfPageView& view) {
  return tint_inside(view, QRectF(view.viewport()->rect())).tinted;
}

QRectF united(const std::vector<QRectF>& rects) {
  QRectF all;
  for (const auto& rect : rects) {
    all = all.united(rect);
  }
  return all;
}

// --- Dragging ---

// A drag over text selects it: inside a line, across lines, backwards, to the end of a line
// from beside or below it. The pointer says what a press would do. A drag beside the text
// pans as before.
void ui_pdf_viewer_select_text_by_dragging() {
  SelectSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_select_drag.pdf"), find_fixture_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  CopyLog log;
  auto* viewer = open_viewer(window, path, &log);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  view->set_continuous(true);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_zoom_percent(150.0);
  view->first_page();
  settle(*viewer);
  const QPoint beside = at(*viewer, 0, QPointF(150.0, 300.0));
  const QPoint margin = at(*viewer, 0, QPointF(8.0, 8.0));

  // The pointer: an I-beam over text, the arrow beside it.
  hover(*view, margin);
  CHECK(pointer_shape(*view) == Qt::ArrowCursor);
  hover(*view, word_middle(*viewer, 0, QStringLiteral("quick")));
  CHECK(pointer_shape(*view) == Qt::IBeamCursor);
  hover(*view, beside);
  CHECK(pointer_shape(*view) == Qt::ArrowCursor);
  CHECK(!view->has_text_selection());
  // The gap between two lines of a paragraph is text too: a press there selects.
  {
    const QRectF quick = word_area(*viewer, 0, QStringLiteral("quick"));
    const QRectF lazy = word_area(*viewer, 0, QStringLiteral("lazy"));
    const QPointF gap(quick.center().x(), (quick.bottom() + lazy.top()) / 2.0);
    CHECK(gap.y() > quick.bottom() + 1.0 && gap.y() < lazy.top() - 1.0);
    hover(*view, at(*viewer, 0, gap));
    CHECK(pointer_shape(*view) == Qt::IBeamCursor);
    // Two line heights under the last line it is not.
    const QRectF files = word_area(*viewer, 0, QStringLiteral("files."));
    hover(*view, at(*viewer, 0, QPointF(files.center().x(), files.bottom() + 2.0 * files.height())));
    CHECK(pointer_shape(*view) == Qt::ArrowCursor);
  }

  // Inside a line: from the first letter of one word to the last letter of another.
  drag(*view, word_start(*viewer, 0, QStringLiteral("quick")), word_end(*viewer, 0, QStringLiteral("fox")));
  expect_text("quick to fox", *viewer, QStringLiteral("quick brown fox"));
  {
    // One mark, on those words.
    const auto rects = view->selection_rects_for_testing(0);
    const QRectF wanted =
        word_rect(*viewer, 0, QStringLiteral("quick")).united(word_rect(*viewer, 0, QStringLiteral("fox")));
    const QRectF mark = united(rects);
    if (rects.empty() || std::abs(mark.left() - wanted.left()) > 3.0 || std::abs(mark.right() - wanted.right()) > 3.0 ||
        std::abs(mark.center().y() - wanted.center().y()) > 4.0) {
      std::cout << "  the mark is at " << mark.left() << ".." << mark.right() << ", the words at " << wanted.left()
                << ".." << wanted.right() << "\n";
      CHECK(false);
    }
    const Tint tint = tint_inside(*view, mark);
    if (tint.tinted < 40 || tint.ink < 20) {
      std::cout << "  inside the mark: tinted " << tint.tinted << ", ink " << tint.ink << "\n";
      CHECK(false);
    }
  }
  // The second half of a letter is past it, the first half before it.
  {
    const QRectF q = word_area(*viewer, 0, QStringLiteral("q"));
    drag(*view, at(*viewer, 0, QPointF(q.right() - 1.0, q.center().y())),
         word_end(*viewer, 0, QStringLiteral("quick")));
    expect_text("from the second half of q", *viewer, QStringLiteral("uick"));
    drag(*view, at(*viewer, 0, QPointF(q.left() + 1.0, q.center().y())),
         word_end(*viewer, 0, QStringLiteral("quick")));
    expect_text("from the first half of q", *viewer, QStringLiteral("quick"));
  }
  // Across lines, and the same backwards.
  drag(*view, word_start(*viewer, 0, QStringLiteral("brown")), word_end(*viewer, 0, QStringLiteral("lazy")));
  expect_text("brown down to lazy", *viewer, QStringLiteral("brown fox jumps\nover the lazy"));
  CHECK(view->selection_rects_for_testing(0).size() == 2);
  drag(*view, word_end(*viewer, 0, QStringLiteral("lazy")), word_start(*viewer, 0, QStringLiteral("brown")));
  expect_text("lazy back up to brown", *viewer, QStringLiteral("brown fox jumps\nover the lazy"));
  // Beside a line is that line's end; below the last line is the end of the text.
  {
    const QRectF fox = word_area(*viewer, 0, QStringLiteral("fox"));
    drag(*view, word_start(*viewer, 0, QStringLiteral("fox")), at(*viewer, 0, QPointF(292.0, fox.center().y())));
    expect_text("fox to the right margin", *viewer, QStringLiteral("fox jumps"));
    drag(*view, word_start(*viewer, 0, QStringLiteral("opens")), at(*viewer, 0, QPointF(292.0, 250.0)));
    expect_text("opens to below the text", *viewer, QStringLiteral("opens PDF files."));
    drag(*view, word_start(*viewer, 0, QStringLiteral("lazy")), at(*viewer, 0, QPointF(4.0, fox.center().y())));
    expect_text("lazy back to the left margin of line one", *viewer,
                QStringLiteral("The quick brown fox jumps\nover the "));
  }
  // A run of moves with no answer taken in between: the selection ends where the last one
  // was, whatever the pointer passed on its way.
  {
    const QPoint from = word_start(*viewer, 0, QStringLiteral("The"));
    const QPoint to = word_end(*viewer, 0, QStringLiteral("dog."));
    const QPoint far = word_end(*viewer, 0, QStringLiteral("files."));
    send_mouse(*view->viewport(), QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int move = 0; move < 30; ++move) {
      send_mouse(*view->viewport(), QEvent::MouseMove, move % 2 == 0 ? far : from + QPoint(20, 0), Qt::NoButton,
                 Qt::LeftButton);
    }
    send_mouse(*view->viewport(), QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    send_mouse(*view->viewport(), QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    expect_text("after 31 moves", *viewer, QStringLiteral("The quick brown fox jumps\nover the lazy dog."));
  }
  // The selection is on the page, so it stays through a scroll and back.
  CHECK(tinted_pixels(*view) > 200);
  // A drag beside the text pans, as it always did, and keeps the selection.
  {
    const int before = view->verticalScrollBar()->value();
    drag(*view, beside, beside - QPoint(0, 60));
    CHECK(view->verticalScrollBar()->value() == before + 60);
    expect_text("after a pan", *viewer, QStringLiteral("The quick brown fox jumps\nover the lazy dog."));
    // So does a middle-button drag, on text as well.
    const QPoint on_text = word_middle(*viewer, 0, QStringLiteral("lazy"));
    drag(*view, on_text, on_text + QPoint(0, 60), Qt::MiddleButton);
    CHECK(view->verticalScrollBar()->value() == before);
    expect_text("after a middle-button pan", *viewer, QStringLiteral("The quick brown fox jumps\nover the lazy dog."));
  }
  // Copy hands the text over; a click on text or beside it selects nothing.
  view->copy_selection();
  CHECK(log.copied == QStringList({QStringLiteral("The quick brown fox jumps\nover the lazy dog.")}));
  click(*view, word_middle(*viewer, 0, QStringLiteral("brown")));
  settle(*viewer);
  CHECK(!view->has_text_selection() && tinted_pixels(*view) == 0);
  drag(*view, word_start(*viewer, 0, QStringLiteral("quick")), word_end(*viewer, 0, QStringLiteral("fox")));
  expect_text("quick to fox again", *viewer, QStringLiteral("quick brown fox"));
  click(*view, beside);
  settle(*viewer);
  CHECK(!view->has_text_selection());
  // With nothing selected Copy hands nothing over.
  view->copy_selection();
  CHECK(log.copied.size() == 1);
  close_viewer(viewer);
}

// --- Double-click, triple-click, Shift+click ---

void ui_pdf_viewer_select_text_words_lines_and_shift_click() {
  SelectSettingsGuard settings_guard;
  const QString path =
      write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_select_clicks.pdf"), find_fixture_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  view->set_continuous(true);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_zoom_percent(150.0);
  view->first_page();
  settle(*viewer);
  const QPoint beside = at(*viewer, 0, QPointF(150.0, 300.0));

  // A double-click selects the word, and is no longer the double-click for full screen.
  double_click(*view, word_middle(*viewer, 0, QStringLiteral("lazy")));
  expect_text("double-click on lazy", *viewer, QStringLiteral("lazy"));
  CHECK(!viewer->full_screen_mode());
  double_click(*view, word_start(*viewer, 0, QStringLiteral("Patchy")));
  expect_text("double-click on the P of Patchy", *viewer, QStringLiteral("Patchy"));
  double_click(*view, word_end(*viewer, 0, QStringLiteral("jumps")));
  expect_text("double-click on the s of jumps", *viewer, QStringLiteral("jumps"));
  // A full stop stands by itself (behind it: before it the click belongs to the word).
  {
    const QRectF stop = word_area(*viewer, 0, QStringLiteral("."));
    double_click(*view, at(*viewer, 0, QPointF(stop.right() + 0.3, stop.center().y())));
    expect_text("double-click on a full stop", *viewer, QStringLiteral("."));
    double_click(*view, at(*viewer, 0, QPointF(stop.left() - 0.3, stop.center().y())));
    expect_text("double-click between a word and its full stop", *viewer, QStringLiteral("dog"));
  }
  // A third click right behind selects the line.
  {
    const QPoint spot = word_middle(*viewer, 0, QStringLiteral("lazy"));
    double_click(*view, spot);
    click(*view, spot);
    expect_text("triple-click on lazy", *viewer, QStringLiteral("over the lazy dog. Patchy"));
    const QPoint first = word_middle(*viewer, 0, QStringLiteral("quick"));
    double_click(*view, first);
    click(*view, first);
    expect_text("triple-click on quick", *viewer, QStringLiteral("The quick brown fox jumps"));
  }
  // Beside the text a double-click is still full screen and back.
  click(*view, beside);
  double_click(*view, beside);
  CHECK(viewer->full_screen_mode());
  viewer->set_full_screen(false);
  settle(*viewer);
  CHECK(!view->has_text_selection());

  // Shift+click reaches from the last click to here, and on from there.
  click(*view, word_start(*viewer, 0, QStringLiteral("quick")));
  click(*view, word_end(*viewer, 0, QStringLiteral("fox")), Qt::ShiftModifier);
  expect_text("click, then Shift+click", *viewer, QStringLiteral("quick brown fox"));
  click(*view, word_end(*viewer, 0, QStringLiteral("lazy")), Qt::ShiftModifier);
  expect_text("Shift+click further on", *viewer, QStringLiteral("quick brown fox jumps\nover the lazy"));
  click(*view, word_start(*viewer, 0, QStringLiteral("The")), Qt::ShiftModifier);
  expect_text("Shift+click before the start", *viewer, QStringLiteral("The "));
  // After a drag it reaches from where the drag began.
  drag(*view, word_start(*viewer, 0, QStringLiteral("brown")), word_end(*viewer, 0, QStringLiteral("fox")));
  click(*view, word_end(*viewer, 0, QStringLiteral("dog.")), Qt::ShiftModifier);
  expect_text("Shift+click after a drag", *viewer, QStringLiteral("brown fox jumps\nover the lazy dog."));
  // After a double-click the word stays in: before it from its end, after it from its start.
  double_click(*view, word_middle(*viewer, 0, QStringLiteral("brown")));
  expect_text("double-click on brown", *viewer, QStringLiteral("brown"));
  click(*view, word_start(*viewer, 0, QStringLiteral("The")), Qt::ShiftModifier);
  expect_text("Shift+click before the word", *viewer, QStringLiteral("The quick brown"));
  click(*view, word_end(*viewer, 0, QStringLiteral("jumps")), Qt::ShiftModifier);
  expect_text("Shift+click after the word", *viewer, QStringLiteral("brown fox jumps"));
  // A Shift+click held and dragged goes on selecting.
  {
    const QPoint from = word_end(*viewer, 0, QStringLiteral("jumps"));
    const QPoint to = word_end(*viewer, 0, QStringLiteral("opens"));
    send_mouse(*view->viewport(), QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    send_mouse(*view->viewport(), QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    send_mouse(*view->viewport(), QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    expect_text("Shift+drag", *viewer, QStringLiteral("brown fox jumps\nover the lazy dog. Patchy\nopens"));
  }
  // With nothing clicked before, Shift+click on text is a plain click.
  click(*view, beside);
  settle(*viewer);
  click(*view, word_middle(*viewer, 0, QStringLiteral("lazy")), Qt::ShiftModifier);
  settle(*viewer);
  CHECK(!view->has_text_selection());
  close_viewer(viewer);
}

// --- Keys and the menu ---

// Copy, Select All and Esc as the window system delivers them: to the viewer's window,
// through Qt's shortcut map and every application event filter. The main window, in use
// before, has Copy, Select All and Esc of its own; none of them runs.
void ui_pdf_viewer_select_text_copy_keys_and_menu() {
  SelectSettingsGuard settings_guard;
  SettingsValueRestorer viewer_setting(QStringLiteral("imports/pdfOpenInViewer"));
  patchy::ui::app_settings().setValue(QStringLiteral("imports/pdfOpenInViewer"), true);
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_select_keys.pdf"), find_fixture_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  MainWindowTestAccess::create_document(window, 64, 64);
  CHECK(process_events_until([&window] { return window.isActiveWindow(); }));
  QTest::keyClick(window.windowHandle(), Qt::Key_B);
  const auto sessions = MainWindowTestAccess::session_count(window);
  MainWindowTestAccess::open_document_path_as_user(window, path);
  auto* viewer = window.findChild<PdfViewerWindow*>();
  CHECK(viewer != nullptr);
  if (viewer == nullptr) {
    return;
  }
  CHECK(process_events_until([viewer] { return viewer->isActiveWindow(); }));
  auto* view = viewer->page_view();
  QWindow* handle = viewer->windowHandle();
  auto* find_edit = viewer->findChild<QLineEdit*>(QStringLiteral("pdfViewerFindEdit"));
  CHECK(handle != nullptr && find_edit != nullptr);
  if (handle == nullptr || find_edit == nullptr) {
    return;
  }
  // Every main-window command on the keys typed here, counted if it runs.
  int main_window_commands = 0;
  {
    const auto viewer_actions = viewer->findChildren<QAction*>();
    const QList<QKeySequence> keys = {QKeySequence(Qt::CTRL | Qt::Key_C), QKeySequence(Qt::CTRL | Qt::Key_A),
                                      QKeySequence(Qt::Key_Escape), QKeySequence(Qt::CTRL | Qt::Key_Insert)};
    for (auto* action : window.findChildren<QAction*>()) {
      if (viewer_actions.contains(action)) {
        continue;
      }
      for (const auto& key : action->shortcuts()) {
        if (keys.contains(key)) {
          QObject::connect(action, &QAction::triggered, viewer, [&main_window_commands] { ++main_window_commands; });
          break;
        }
      }
    }
  }
  view->set_continuous(false);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->first_page();
  view->setFocus();
  settle(*viewer);
  const auto type = [handle](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::keyClick(handle, key, modifiers);
  };
  auto* clipboard = QApplication::clipboard();
  const QString sentinel = QStringLiteral("what was on the clipboard");
  clipboard->setText(sentinel);

  // With nothing selected Copy copies nothing, here or in the main window.
  type(Qt::Key_C, Qt::ControlModifier);
  QApplication::processEvents();
  CHECK(clipboard->text() == sentinel && !clipboard->mimeData()->hasImage());
  // A selection, then Copy.
  drag(*view, word_start(*viewer, 0, QStringLiteral("quick")), word_end(*viewer, 0, QStringLiteral("lazy")));
  expect_text("the drag", *viewer, QStringLiteral("quick brown fox jumps\nover the lazy"));
  type(Qt::Key_C, Qt::ControlModifier);
  CHECK(clipboard->text() == QStringLiteral("quick brown fox jumps\nover the lazy"));
  CHECK(!clipboard->mimeData()->hasImage());
#if defined(Q_OS_WIN)
  // The platform's other Copy key.
  clipboard->setText(sentinel);
  type(Qt::Key_Insert, Qt::ControlModifier);
  CHECK(clipboard->text() == QStringLiteral("quick brown fox jumps\nover the lazy"));
#endif
  // Select All takes the page being read.
  type(Qt::Key_A, Qt::ControlModifier);
  expect_text("Ctrl+A", *viewer, kPageOneText);
  type(Qt::Key_C, Qt::ControlModifier);
  CHECK(clipboard->text() == kPageOneText);
  // With the keyboard on the sidebar's page list the keys are still the pages'.
  if (auto* thumbnails = viewer->findChild<QWidget*>(QStringLiteral("pdfViewerThumbnails")); thumbnails != nullptr) {
    clipboard->setText(sentinel);
    thumbnails->setFocus(Qt::MouseFocusReason);
    type(Qt::Key_C, Qt::ControlModifier);
    CHECK(clipboard->text() == kPageOneText);
  }

  // In the find field they are the field's: its own text is selected and copied, and the
  // pages keep their selection.
  type(Qt::Key_F, Qt::ControlModifier);
  CHECK(viewer->find_bar_open() && viewer->focusWidget() == find_edit);
  for (const char letter : {'f', 'o', 'x'}) {
    QTest::keyClick(handle, letter);
  }
  find_edit->deselect();
  type(Qt::Key_A, Qt::ControlModifier);
  CHECK(find_edit->selectedText() == QStringLiteral("fox"));
  type(Qt::Key_C, Qt::ControlModifier);
  CHECK(clipboard->text() == QStringLiteral("fox"));
  expect_text("after Ctrl+A and Ctrl+C in the find field", *viewer, kPageOneText);
  // Esc closes the find bar and leaves the selection; on the pages it then drops it.
  type(Qt::Key_Escape);
  CHECK(!viewer->find_bar_open() && view->has_text_selection());
  CHECK(viewer->focusWidget() == view);
  type(Qt::Key_Escape);
  settle(*viewer);
  CHECK(!view->has_text_selection());
  // In full screen Esc drops the selection first and leaves full screen second.
  type(Qt::Key_A, Qt::ControlModifier);
  settle(*viewer);
  viewer->set_full_screen(true);
  type(Qt::Key_Escape);
  CHECK(viewer->full_screen_mode() && !view->has_text_selection());
  type(Qt::Key_Escape);
  CHECK(!viewer->full_screen_mode());
  settle(*viewer);
  // Space still turns the page.
  view->setFocus(Qt::MouseFocusReason);
  type(Qt::Key_Space);
  CHECK(view->current_page() == 1);
  type(Qt::Key_Space, Qt::ShiftModifier);
  CHECK(view->current_page() == 0);
  settle(*viewer);

  // The menu: on a page, Copy (off with nothing selected) and Select All on This Page.
  auto menu = right_click(*view, word_middle(*viewer, 0, QStringLiteral("brown")));
  CHECK(menu.shown && menu.actions == QStringList({kCopyAction, kSelectPageAction}));
  CHECK(!menu.action_enabled(kCopyAction) && menu.action_enabled(kSelectPageAction));
  drag(*view, word_start(*viewer, 0, QStringLiteral("over")), word_end(*viewer, 0, QStringLiteral("dog.")));
  expect_text("a drag for the menu", *viewer, QStringLiteral("over the lazy dog."));
  clipboard->setText(sentinel);
  menu = right_click(*view, word_middle(*viewer, 0, QStringLiteral("lazy")), kCopyAction);
  CHECK(menu.action_enabled(kCopyAction));
  CHECK(clipboard->text() == QStringLiteral("over the lazy dog."));
  right_click(*view, word_middle(*viewer, 0, QStringLiteral("lazy")), kSelectPageAction);
  expect_text("Select All on This Page", *viewer, kPageOneText);
  // In a spread it is the page under the pointer, and Ctrl+A is both pages.
  view->set_spread_mode(patchy::PageSpreadMode::TwoPages);
  settle(*viewer);
  right_click(*view, word_middle(*viewer, 1, QStringLiteral("quick")), kSelectPageAction);
  expect_text("Select All on This Page, second page", *viewer, kPageTwoText);
  view->setFocus(Qt::MouseFocusReason);
  type(Qt::Key_A, Qt::ControlModifier);
  expect_text("Ctrl+A on a spread", *viewer, kPageOneText + QLatin1Char('\n') + kPageTwoText);
  // Beside the pages the menu still offers Copy while text is selected.
  menu = right_click(*view, QPoint(3, 3));
  CHECK(menu.shown && menu.actions == QStringList({kCopyAction}));
  type(Qt::Key_Escape);
  settle(*viewer);
  menu = right_click(*view, QPoint(3, 3));
  CHECK(!menu.shown);

  // None of this ran a command of the main window or touched its documents.
  CHECK(main_window_commands == 0);
  CHECK(MainWindowTestAccess::session_count(window) == sessions);
  close_viewer(viewer);
}

// --- Zoom, rotation, layouts ---

// The selection stays on its words through zoom, a detail render and every quarter turn,
// and text is selected the same in a turned view, in a spread, right to left and across
// pages.
void ui_pdf_viewer_select_text_follows_the_page() {
  SelectSettingsGuard settings_guard;
  const QString path =
      write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_select_marks.pdf"), find_fixture_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_viewer(window, path);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  view->set_continuous(true);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_zoom_percent(150.0);
  view->first_page();
  settle(*viewer);
  drag(*view, word_start(*viewer, 0, QStringLiteral("brown")), word_end(*viewer, 0, QStringLiteral("fox")));
  expect_text("brown fox", *viewer, QStringLiteral("brown fox"));

  // The mark is where the words are, tinted, with the letters showing through.
  const auto words = [&] {
    return word_rect(*viewer, 0, QStringLiteral("brown")).united(word_rect(*viewer, 0, QStringLiteral("fox")));
  };
  const auto on_its_words = [&](const char* what) {
    settle(*viewer);
    bring_into_view(*view, words());
    settle(*viewer);
    const QRectF wanted = words();
    const QRectF mark = united(view->selection_rects_for_testing(0));
    const Tint tint = tint_inside(*view, mark);
    const bool placed = !mark.isEmpty() && std::abs(mark.left() - wanted.left()) < 4.0 &&
                        std::abs(mark.right() - wanted.right()) < 4.0 && std::abs(mark.top() - wanted.top()) < 6.0 &&
                        std::abs(mark.bottom() - wanted.bottom()) < 6.0;
    if (!placed || tint.tinted < 40 || tint.ink < 20) {
      std::cout << "  " << what << ": mark " << mark.x() << "," << mark.y() << " " << mark.width() << "x"
                << mark.height() << ", words " << wanted.x() << "," << wanted.y() << " " << wanted.width() << "x"
                << wanted.height() << ", tinted " << tint.tinted << ", ink " << tint.ink << "\n";
      CHECK(false);
    }
    return mark;
  };
  const QRectF at_150 = on_its_words("at 150%");
  view->set_zoom_percent(300.0);
  const QRectF at_300 = on_its_words("at 300%");
  CHECK(std::abs(at_300.width() / std::max(1.0, at_150.width()) - 2.0) < 0.2);
  // Past a whole-page image the page is drawn from a detail render.
  view->set_zoom_percent(700.0);
  view->go_to_page(0);
  on_its_words("at 700%");
  CHECK(view->cached_width_for_testing(0) >= 2700);
  view->set_zoom_percent(150.0);
  view->go_to_page(0);
  for (int turn = 1; turn <= 4; ++turn) {
    view->rotate_clockwise();
    view->go_to_page(0);
    const QRectF mark = on_its_words("turned");
    CHECK((mark.width() > mark.height()) == (turn % 2 == 0));
  }
  expect_text("after zoom and rotation", *viewer, QStringLiteral("brown fox"));

  // Selecting in a turned view: the text runs down the window, the drag follows it.
  view->rotate_clockwise();
  view->go_to_page(0);
  settle(*viewer);
  bring_into_view(*view, word_rect(*viewer, 0, QStringLiteral("lazy dog. Patchy")));
  settle(*viewer);
  CHECK(word_rect(*viewer, 0, QStringLiteral("lazy")).height() > word_rect(*viewer, 0, QStringLiteral("lazy")).width());
  drag(*view, word_start(*viewer, 0, QStringLiteral("lazy")), word_end(*viewer, 0, QStringLiteral("dog.")));
  expect_text("a drag in a view turned once", *viewer, QStringLiteral("lazy dog."));
  double_click(*view, word_middle(*viewer, 0, QStringLiteral("Patchy")));
  expect_text("a double-click in a view turned once", *viewer, QStringLiteral("Patchy"));
  for (int turn = 0; turn < 3; ++turn) {
    view->rotate_clockwise();
  }

  // Two pages side by side: a drag from one page into the other takes the rest of the
  // first and the start of the second, whichever side the first page is on.
  const QString across = QStringLiteral("lazy dog. Patchy\nopens PDF files.\nA fox is quick. FOX");
  view->set_continuous(false);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPages);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  view->first_page();
  settle(*viewer);
  drag(*view, word_start(*viewer, 0, QStringLiteral("lazy")), word_end(*viewer, 1, QStringLiteral("FOX")));
  expect_text("across a spread", *viewer, across);
  CHECK(!view->selection_rects_for_testing(0).empty() && !view->selection_rects_for_testing(1).empty());
  view->set_reading_direction(patchy::PageReadingDirection::RightToLeft);
  settle(*viewer);
  // The same pages hold the same selection, now the other way around on screen.
  expect_text("after turning to right to left", *viewer, across);
  CHECK(word_rect(*viewer, 0, QStringLiteral("lazy")).left() > word_rect(*viewer, 1, QStringLiteral("FOX")).left());
  click(*view, at(*viewer, 0, QPointF(150.0, 300.0)));
  drag(*view, word_start(*viewer, 0, QStringLiteral("lazy")), word_end(*viewer, 1, QStringLiteral("FOX")));
  expect_text("across a spread, right to left", *viewer, across);
  drag(*view, word_end(*viewer, 1, QStringLiteral("FOX")), word_start(*viewer, 0, QStringLiteral("lazy")));
  expect_text("across a spread, backwards", *viewer, across);
  view->set_reading_direction(patchy::PageReadingDirection::LeftToRight);

  // Continuous: down from one page into the next (small enough to show both).
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_continuous(true);
  view->set_zoom_percent(75.0);
  view->first_page();
  settle(*viewer);
  drag(*view, word_start(*viewer, 0, QStringLiteral("files.")), word_end(*viewer, 1, QStringLiteral("fox")));
  expect_text("down into the next page", *viewer, QStringLiteral("files.\nA fox"));

  // Shift+click pages away, over a page without text (page 4): everything in between.
  view->go_to_page(2);
  settle(*viewer);
  click(*view, word_start(*viewer, 2, QStringLiteral("across")));
  view->go_to_page(4);
  settle(*viewer);
  click(*view, word_end(*viewer, 4, QStringLiteral("last")), Qt::ShiftModifier);
  expect_text("Shift+click two pages on", *viewer,
              QStringLiteral("across lines.\nÄrger and ärger\nThe last"));
  // A drag that ends on the page without text stops at the end of the text before it.
  view->set_continuous(false);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPages);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  view->go_to_page(2);
  settle(*viewer);
  CHECK(view->current_pages() == std::vector<int>({2, 3}));
  drag(*view, word_start(*viewer, 2, QStringLiteral("across")), at(*viewer, 3, QPointF(150.0, 200.0)));
  expect_text("a drag onto a page without text", *viewer, QStringLiteral("across lines."));
  close_viewer(viewer);
}

// --- What is copied ---

// Line breaks are kept, and a word hyphenated at a line end is copied as the page shows it.
void ui_pdf_viewer_select_text_copies_plain_text() {
  using patchy::ui::pdf_copied_text;
  CHECK(pdf_copied_text(QStringLiteral("one\r\ntwo\rthree\nfour")) == QStringLiteral("one\ntwo\nthree\nfour"));
  // PDFium's marks for a hyphen at a line end, with and without a line break behind them.
  for (const char16_t mark : {u'\xfffe', u'\x02'}) {
    const QString broken = QStringLiteral("exam") + QChar(mark);
    CHECK(pdf_copied_text(broken + QStringLiteral("ple")) == QStringLiteral("exam-\nple"));
    CHECK(pdf_copied_text(broken + QStringLiteral("\r\nple")) == QStringLiteral("exam-\nple"));
    CHECK(pdf_copied_text(broken) == QStringLiteral("exam-"));
  }
  // A typed hyphen inside a line, tabs and other scripts stay as they are; control
  // characters go.
  CHECK(pdf_copied_text(QStringLiteral("well-known\tcafé 日本")) ==
        QStringLiteral("well-known\tcafé 日本"));
  CHECK(pdf_copied_text(QString(QStringLiteral("a")) + QChar(0x00) + QChar(0x1f) + QStringLiteral("b")) ==
        QStringLiteral("ab"));

  SelectSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_select_copy.pdf"), find_fixture_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  CopyLog log;
  auto* viewer = open_viewer(window, path, &log);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  view->set_continuous(false);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  view->go_to_page(2);
  settle(*viewer);
  view->select_all_on_current_pages();
  expect_text("the page with words broken at line ends", *viewer, kPageThreeText);
  // The whole page is marked, the last line included.
  {
    const QRectF mark = united(view->selection_rects_for_testing(2));
    const QRectF first = word_rect(*viewer, 2, QStringLiteral("This"));
    const QRectF last = word_rect(*viewer, 2, QStringLiteral("lines."));
    CHECK(std::abs(mark.top() - first.top()) < 6.0 && std::abs(mark.bottom() - last.bottom()) < 6.0);
  }
  // A drag over the broken word, both lines: the hyphen and the line break are in it.
  drag(*view, word_start(*viewer, 2, QStringLiteral("an")), word_end(*viewer, 2, QStringLiteral("of")));
  expect_text("a drag over a word broken at a line end", *viewer, QStringLiteral("an exam-\nple of"));
  view->copy_selection();
  CHECK(log.copied == QStringList({QStringLiteral("an exam-\nple of")}));
  // The marks reach the end of what was dragged over, on both lines.
  {
    const auto rects = view->selection_rects_for_testing(2);
    const QRectF mark = united(rects);
    const QRectF of = word_rect(*viewer, 2, QStringLiteral("of"));
    const QRectF an = word_rect(*viewer, 2, QStringLiteral("an"));
    CHECK(rects.size() >= 2);
    CHECK(std::abs(mark.bottom() - of.bottom()) < 6.0 && std::abs(mark.top() - an.top()) < 6.0);
    bool second_line_reaches_of = false;
    for (const auto& rect : rects) {
      second_line_reaches_of = second_line_reaches_of ||
                               (std::abs(rect.right() - of.right()) < 4.0 && std::abs(rect.bottom() - of.bottom()) < 6.0);
    }
    CHECK(second_line_reaches_of);
  }
  // Accented letters come out as letters.
  view->go_to_page(4);
  settle(*viewer);
  view->select_all_on_current_pages();
  expect_text("the last page", *viewer, QStringLiteral("Ärger and ärger\nThe last fox."));
  close_viewer(viewer);
}

// --- A scan ---

// A PDF without a text layer: nothing can be selected, nothing is copied, and the pointer,
// the drag and the double-click do what they did before.
void ui_pdf_viewer_select_text_without_a_text_layer() {
  SelectSettingsGuard settings_guard;
  const QString path = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_select_scan.pdf"), scan_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  CopyLog log;
  auto* viewer = open_viewer(window, path, &log);
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  view->set_continuous(true);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_zoom_percent(200.0);
  view->first_page();
  settle(*viewer);
  const QPoint middle = at(*viewer, 0, QPointF(100.0, 150.0));
  hover(*view, middle);
  CHECK(pointer_shape(*view) == Qt::ArrowCursor);
  // A drag pans.
  const int before = view->verticalScrollBar()->value();
  drag(*view, middle, middle - QPoint(0, 50));
  CHECK(view->verticalScrollBar()->value() == before + 50);
  settle(*viewer);
  CHECK(!view->has_text_selection());
  // Shift+click, Select All and Copy do nothing.
  click(*view, middle);
  click(*view, middle + QPoint(40, 40), Qt::ShiftModifier);
  view->select_all_on_current_pages();
  settle(*viewer);
  CHECK(!view->has_text_selection() && view->selected_text().isEmpty());
  view->copy_selection();
  CHECK(log.copied.isEmpty());
  // The menu says so: both entries are off, and the page's text can be read on request.
  const auto menu = right_click(*view, middle);
  CHECK(menu.shown && menu.actions == QStringList({kCopyAction, kSelectPageAction,
                                                    QStringLiteral("pdfViewerRecognizePageAction")}));
  CHECK(!menu.action_enabled(kCopyAction) && !menu.action_enabled(kSelectPageAction));
  // A double-click is still full screen.
  double_click(*view, middle);
  CHECK(viewer->full_screen_mode());
  viewer->set_full_screen(false);
  close_viewer(viewer);

  // A page without text among pages with text: the same on that page.
  const QString mixed = write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_select_mixed.pdf"), find_fixture_pdf_bytes());
  viewer = open_viewer(window, mixed, &log);
  if (viewer == nullptr) {
    return;
  }
  view = viewer->page_view();
  view->set_continuous(false);
  view->set_spread_mode(patchy::PageSpreadMode::Single);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  view->go_to_page(3);
  settle(*viewer);
  const QPoint spot = at(*viewer, 3, QPointF(150.0, 200.0));
  hover(*view, spot);
  CHECK(pointer_shape(*view) == Qt::ArrowCursor);
  drag(*view, spot, spot + QPoint(60, 30));
  view->select_all_on_current_pages();
  settle(*viewer);
  CHECK(!view->has_text_selection());
  const auto mixed_menu = right_click(*view, spot);
  CHECK(mixed_menu.shown && !mixed_menu.action_enabled(kCopyAction) && !mixed_menu.action_enabled(kSelectPageAction));
  close_viewer(viewer);
}

// --- The picture ---

// A paragraph selected on a document of ordinary prose (the desk's test file): the picture
// for the feature's card.
void ui_pdf_viewer_select_text_sample_document() {
  SelectSettingsGuard settings_guard;
  const QString path =
      write_file(QStringLiteral("test-artifacts/ui_pdf_viewer_select_sample.pdf"), find_sample_pdf_bytes());
  patchy::ui::MainWindow window;
  show_window(window);
  auto* viewer = open_viewer(window, path, nullptr, QSize(1240, 820));
  if (viewer == nullptr) {
    return;
  }
  auto* view = viewer->page_view();
  view->set_continuous(false);
  view->set_spread_mode(patchy::PageSpreadMode::TwoPages);
  view->set_fit_mode(patchy::PageFitMode::WholePage);
  view->first_page();
  settle(*viewer);
  drag(*view, word_start(*viewer, 0, QStringLiteral("Layers let")), word_end(*viewer, 0, QStringLiteral("stack.")), Qt::LeftButton, 12);
  expect_text("the second paragraph", *viewer,
              QStringLiteral("Layers let you change one part of a picture and leave the rest alone.\n"
                             "Paint on a new layer and the photo under it is untouched. Hide the layer\n"
                             "and the paint is gone; show it again and the paint is back. Nothing is\n"
                             "lost until you flatten the stack."));
  CHECK(view->selection_rects_for_testing(0).size() == 4);
  // The words broken at line ends on pages 3 and 4, copied as the page shows them.
  view->go_to_page(2);
  settle(*viewer);
  const QString before = view->selected_text();
  view->select_all_on_page(2);
  settle(*viewer);
  CHECK(view->selected_text().contains(QStringLiteral("is called non-\ndestructive editing")));
  view->select_all_on_page(3);
  settle(*viewer);
  CHECK(view->selected_text().contains(QStringLiteral("Delete the adjust-\nment layer and")));
  // Back to the paragraph for the picture, thumbnails rendered.
  view->first_page();
  settle(*viewer);
  drag(*view, word_start(*viewer, 0, QStringLiteral("Layers let")), word_end(*viewer, 0, QStringLiteral("stack.")), Qt::LeftButton, 12);
  settle(*viewer);
  CHECK(view->selected_text() == before);
  (void)process_events_until([] { return false; }, 300);
  settle(*viewer);
  patchy::test::ui::save_widget_artifact("ui_pdf_viewer_select_text", *viewer);
  close_viewer(viewer);
}

}  // namespace

std::vector<patchy::test::TestCase> pdf_viewer_select_ui_tests() {
  return {
      {"ui_pdf_viewer_select_text_by_dragging", ui_pdf_viewer_select_text_by_dragging},
      {"ui_pdf_viewer_select_text_words_lines_and_shift_click", ui_pdf_viewer_select_text_words_lines_and_shift_click},
      {"ui_pdf_viewer_select_text_copy_keys_and_menu", ui_pdf_viewer_select_text_copy_keys_and_menu},
      {"ui_pdf_viewer_select_text_follows_the_page", ui_pdf_viewer_select_text_follows_the_page},
      {"ui_pdf_viewer_select_text_copies_plain_text", ui_pdf_viewer_select_text_copies_plain_text},
      {"ui_pdf_viewer_select_text_without_a_text_layer", ui_pdf_viewer_select_text_without_a_text_layer},
      {"ui_pdf_viewer_select_text_sample_document", ui_pdf_viewer_select_text_sample_document},
  };
}

#else

std::vector<patchy::test::TestCase> pdf_viewer_select_ui_tests() {
  return {};
}

#endif
